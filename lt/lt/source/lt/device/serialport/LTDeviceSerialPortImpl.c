/*******************************************************************************
 * lt/source/lt/device/serialport/LTDeviceSerialPort.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/device/serialport/LTDeviceSerialPort.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("dev.serialport");

                                      /*  \|/  */
#define LTDEVICESERIALPORT_DO_DLOG         0   /* ALWAYS RESTORE THIS VALUE TO 0 BEFORE MERGING */
                                      /*  /|\  */
#if     LTDEVICESERIALPORT_DO_DLOG
#define DLOG                    LTLOG
#else
#define DLOG                    LTLOG_LOGNULL
#endif

enum { kReceiveBufferSize = 256 };

/* Note:
        LTDeviceSerialPort currently supports the loading and use of one and only one Driver Library.
        This imposed limitation saves code space and RAM usage, and is likely to be consistent
        with all practical use cases in the forseeable future.  If more than one Driver Library
        is needed to support multiple A/D-convert Driver Libraries in a single device, this Device
        Library will have to change to load multiple Driver Libraries in a fashion similar to
        LTDeviceLED. */

/* Describe the Driver, its interface, and the name of the serial port instance object in the Driver: */
static struct {
    LTDriverLibrary     *driverLibrary;
    ILTDriverSerialPort *driverAPI;
    const char          *driverInstanceObjectName;     /* See note below */
} s_Driver;

/* Note:
        In order to avoid requiring the Driver to provide a factory function to create the driver-level
        serial-port instance object, LTDeviceSerialPort requires that the Driver Library name the
        Driver-level serial-port instance object type as the Driver Library name appended with "Instance".
        This allows objects to be created using lt_createobject_named, since
        the LTDeviceSerialPort is aware of the name of the object. */

typedef_LTObjectImpl(LTSerialPort, LTSerialPortImpl) {
    LTDriverSerialPort                   *driverSerialPort;                        /* representation of the serial port in the Driver */
    LTDeviceSerialPort_ReceiveCharsProc  *receiveCharsProc;                                /* client function to call with received characters */
    LTDeviceSerialPort_ReceiveStatusProc *receiveStatusProc;                               /* client function to call for status changes (not supported yet) */
    void                                 *receiveClientData;                               /* client data pointer to pass to the receive functions */
    LTThread                              hReceiveThread;                                   /* thread along which to call the above client functions */
    ILTThread                            *ThreadAPI;
    LTAtomic                              receiveBufferIndex;                               /* write location in the interrupt buffer */
    char                                  receiveInterruptBuffer[kReceiveBufferSize];       /* buffer for characters received in ISR context */
    char                                  receiveThreadBuffer[kReceiveBufferSize];          /* buffer for transferring characters to the client thread */
} LTOBJECT_API;

static bool LTSerialPortImpl_ConstructObject(LTSerialPortImpl *port) {
    DLOG("construct", NULL);
    port->ThreadAPI = lt_getlibraryinterface(ILTThread, LT_GetCore());
    LTAtomic_Store(&port->receiveBufferIndex, 0);
    return true;
}

static void LTSerialPortImpl_DestructObject(LTSerialPortImpl *port) {
    DLOG("destruct", NULL);
    if (port->driverSerialPort) {
        lt_destroyobject(port->driverSerialPort);
        port->driverSerialPort = NULL;
    }
}

static bool LTSerialPortImpl_Acquire(LTSerialPortImpl *port, const char *portName,
                                     u32 nBitsPerSecond, bool bSynchronous,
                                     u32 nDataBits, LTDeviceSerialPort_Parity parity, u32 nStopBits) {
    LT_UNUSED(bSynchronous);    /* not supported yet - always asynchronous */
    LT_UNUSED(nDataBits);       /* not supported yet - always 8 bits       */
    if (port->driverSerialPort) {
        LTLOG_YELLOWALERT("start.already", NULL);
        return false;
    }
    if (!(port->driverSerialPort = (LTDriverSerialPort *)lt_createobject_named("LTDriverSerialPort", s_Driver.driverInstanceObjectName))) {
        LTLOG_YELLOWALERT("start.create", NULL);
        return false;
    }
    if (!port->driverSerialPort->API->Acquire(port->driverSerialPort, portName, nBitsPerSecond, nStopBits, parity)) {
        lt_destroyobject(port->driverSerialPort);
        port->driverSerialPort = NULL;
        LTLOG_YELLOWALERT("start", NULL);
        return false;
    }
    return true;
}

static void LTSerialPortImpl_Release(LTSerialPortImpl *port) {
    if (port->driverSerialPort) {
        port->driverSerialPort->API->Connect(port->driverSerialPort, NULL, NULL, NULL);
        port->driverSerialPort->API->Release(port->driverSerialPort);
    }
    port->driverSerialPort = NULL;
}

/* Transfer received characters from the ISR buffer to the transfer buffer: */
static u32 TransferSerialInterruptBufferChars(LTSerialPortImpl *port) {
    u32 nTransferIndex = 0;
    u32 nDestIndex = LTAtomic_Load(&port->receiveBufferIndex);
    while (nDestIndex > nTransferIndex) {
        u32 nBytes = nDestIndex - nTransferIndex;
        lt_memcpy(port->receiveThreadBuffer + nTransferIndex, port->receiveInterruptBuffer + nTransferIndex, nBytes);
        nTransferIndex += nBytes;
        if (LTAtomic_CompareAndExchange(&port->receiveBufferIndex, nDestIndex, 0)) break;  /* no more characters came in during this transfer operation */
        /* Oops, some more characters came in during the transfer.  Transfer them as well: */
        nDestIndex = LTAtomic_Load(&port->receiveBufferIndex);
    }
    return nTransferIndex;
}

/* (client thread context)
   Transfer received characters from the ISR buffer to the transfer buffer, and call the client callback to process them: */
static void ProcessThreadInputChars(void *clientData) {
    LTSerialPortImpl *port = clientData;
    if (port->receiveCharsProc) {
        u32 nChars;
        while ((nChars = TransferSerialInterruptBufferChars(port)))
            (*port->receiveCharsProc)(port->receiveThreadBuffer, nChars, port->receiveClientData);
    }
}

/* (ISR context)
   Process incoming characters from the ISR.
   NOTE that this buffer (and the transfer buffer) are NOT ring buffers; they are meant to be emptied upon every
   transfer, and filled from the beginning: */
static void ProcessISRInputChars(const char *chars, u32 nChars, void *clientData) LT_ISR_SAFE {
    LTSerialPortImpl *port = clientData;
    if (port->hReceiveThread) { /* only process characters if someone is listening */
        if (chars && nChars) {
            /* There are characters, add them to the buffer, dropping if buffer full: */
            u32 nRoom = kReceiveBufferSize - LTAtomic_Load(&port->receiveBufferIndex);
            if (nChars > nRoom) nChars = nRoom;
            while (nChars--) port->receiveInterruptBuffer[LTAtomic_FetchAdd(&port->receiveBufferIndex, 1)] = *chars++;
        } else if (LTAtomic_Load(&port->receiveBufferIndex)) { /* no more incoming characters - have the thread retrieve them: */
            port->ThreadAPI->QueueTaskProcIfRequired(port->hReceiveThread, ProcessThreadInputChars, NULL, port);
        }
    }
}

static void LTSerialPortImpl_Connect(LTSerialPortImpl *port,
                                         LTDeviceSerialPort_ReceiveCharsProc *receiveCharsProc,
                                         LTDeviceSerialPort_ReceiveStatusProc *receiveStatusProc,
                                         void *clientData) {
    if (port->driverSerialPort) {
        port->receiveCharsProc  = receiveCharsProc;
        port->receiveStatusProc = receiveStatusProc;
        port->receiveClientData = clientData;
        port->hReceiveThread     = port->ThreadAPI->GetCurrentThread();
        if (port->ThreadAPI)
            port->driverSerialPort->API->Connect(port->driverSerialPort, ProcessISRInputChars, NULL /* (not supported yet) */, port);
    }
}

static void LTSerialPortImpl_SendChars(LTSerialPortImpl *port, const char *chars, u32 nChars) {
    if (port->driverSerialPort)
        port->driverSerialPort->API->PutChars(port->driverSerialPort, chars, nChars);
}

static void LTSerialPortImpl_SendCString(LTSerialPortImpl *port, const char *string) {
    if (port->driverSerialPort)
        port->driverSerialPort->API->PutChars(port->driverSerialPort, string, lt_strlen(string));
}

static void LTSerialPortImpl_SendChar(LTSerialPortImpl *port, char c) {
    if (port->driverSerialPort)
        port->driverSerialPort->API->PutChars(port->driverSerialPort, &c, 1);
}

static void LTSerialPortImpl_SendBreak(LTSerialPortImpl *port, LTTime breakDuration) {
    LT_UNUSED(port);
    LT_UNUSED(breakDuration);
    /* Not supported yet */
}

define_LTObjectImplPublic(LTSerialPort, LTSerialPortImpl, Acquire, Release, Connect, SendChars, SendCString, SendChar, SendBreak);

/***********************************************************************************************************************
 * Standard Device Instance access:                                                                                   */

static u32 LTDeviceSerialPortImpl_EnumerateSerialPorts(LTDeviceSerialPort_EnumerateSerialPortProc *enumerationProc, void *clientData) {
    return s_Driver.driverAPI->EnumerateSerialPorts(enumerationProc, clientData);
}

/***********************************************************************************************************************
 * Library startup and shutdown: Open and close Driver Libraries:                                                     */

static void LTDeviceSerialPortImpl_LibFini(void) {
    DLOG("fini", NULL);
    lt_closelibrary(s_Driver.driverLibrary);
    lt_free((void *)s_Driver.driverInstanceObjectName);
    s_Driver.driverInstanceObjectName = NULL;
    s_Driver.driverLibrary            = NULL;
    s_Driver.driverAPI                = NULL;
}

/* Generate the LTDriverSerialPort instance object type name.
   This is assumed to be the name of the Driver Library appended with "Instance".
   Return NULL if couldn't get the memory. */
static const char *GenerateDriverInstanceName(const char *driverLibraryName) {
    static const char kInstance[] = "Instance";
    LT_SIZE driverInstanceNameSize = lt_strlen(driverLibraryName) + sizeof(kInstance) + 1;
    char *instanceName = lt_malloc(driverInstanceNameSize);
    if (instanceName) {
        lt_strncpyTerm(instanceName, driverLibraryName, driverInstanceNameSize);
        lt_strncatTerm(instanceName, kInstance,         driverInstanceNameSize);
    }
    return instanceName;
}

static bool LTDeviceSerialPortImpl_LibInit(void) {
    DLOG("init", NULL);
    LTDeviceConfig *deviceConfig = lt_openlibrary(LTDeviceConfig);
    const char *driverLibraryName = NULL;
    if (deviceConfig)
        driverLibraryName = deviceConfig->GetDriverAt("LTDeviceSerialPort", 0);
    lt_closelibrary(deviceConfig);
    if (   !driverLibraryName
        || !(s_Driver.driverInstanceObjectName = GenerateDriverInstanceName(driverLibraryName))
        || !(s_Driver.driverLibrary            = (LTDriverLibrary *)LT_GetCore()->OpenLibrary(driverLibraryName))
        || !(s_Driver.driverAPI                = lt_getlibraryinterface(ILTDriverSerialPort, s_Driver.driverLibrary))) {
        LTLOG_YELLOWALERT("init.fail", NULL);
        LTDeviceSerialPortImpl_LibFini();
        return false;
    }
    return true;
}

LTLIBRARY_EXPORT_INTERFACES(LTDeviceSerialPort, (LTSerialPortImpl));

define_LTLIBRARY_ROOT_INTERFACE(LTDeviceSerialPort)
    .EnumerateSerialPorts = LTDeviceSerialPortImpl_EnumerateSerialPorts
LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  27-Aug-24   constantine created
 */
