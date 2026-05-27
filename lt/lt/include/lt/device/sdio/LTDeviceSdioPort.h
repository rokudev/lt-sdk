/*******************************************************************************
 * lt/device/sdio/LTDeviceSdioPort.h
 *
 * API for SDIO port concept for accessing SDIO bus
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_SDIO_LTSDIOPORT_H
#define ROKU_LT_INCLUDE_LT_DEVICE_SDIO_LTSDIOPORT_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/*_____________________________________________________________________________
 * SDIO bus usage
 *
 * LTSdioBus is used to send data between libraries residing on two different MCUs
 * connected by an SDIO bus.
 * The LTDeviceSdioPort interface uses a notion of a "port" to describe an address to which
 * data is sent. A port is a positive integer between 0 and kLTSdioBusMaxPortNumber.
 *
 * A client attaches itself to a specific port to receive the messages sent to
 * the port. It is done by registering a callback function that will be
 * triggered whenever data is sent to the port:
 *
 * LTDeviceSdioPort *port = lt_createobject(LTDeviceSdioPort);
 * u8 portNumber = 2;
 * port->API->Bind(port, portNumber, PerformReadCallback, pClientData);
 *
 * The callback PerformReadCallback() is expected to call Read() and supply a buffer of the size
 * requested by the LTDeviceSdioPort instance:
 * void PerformReadCallback(LTDeviceSdioPort *port, LT_SIZE requestedLen, void *pClientData) {
 *     u8 *pBuffer = lt_malloc(requestedLen);
 *     port->API->Read(port, pBuffer);
 * }
 *
 * The callback runs on the same thread on which the function Bind() was queued, so if Bind() used
 * the reader thread, PerformReadCallback() can call Read() immediately as above. Otherwise, Read()
 * must be queued on the reader thread.
 *
 * Data is sent by simply calling Write() to a remote port:
 * The remote port is same as the receiving port.
 * port->API->Write(port, pBuffer, len);
 *
 * A library that both sends and receives data must do that on different threads,
 * because both the function Read(), which receives data, and the function Write(),
 * which sends data, can block on a mutex while waiting on access to the bus.
 */

/** Pre-defined SDIO port numbers. Don't change. Otherwise it will break protocols. */
typedef_LTENUM_SIZED(LTDeviceSdioPortNumber, u8) {
    kLTDeviceSdioPort_Mailbox                = 0,   // Mailbox
    kLTDeviceSdioPort_NonSerialWakeup        = 1,   // Wakeup
    kLTDeviceSdioPort_Srtp                   = 2,   // SRTP
    kLTDeviceSdioPort_Ota                    = 3,   // OTA
    kLTDeviceSdioPort_Settings               = 4,   // Settings
    kLTDeviceSdioPort_Socket                 = 5,   // Socket
    kLTDeviceSdioPort_Ltat                   = 6,   // LTAT
    kLTDeviceSdioPort_Log                    = 7,   // Log
    kLTDeviceSdioPort_BufferManager          = 8,   // LTIPCBufferManager control port
    // Add new port number here.
};

typedef struct LTDeviceSdioPort LTDeviceSdioPort;
typedef void (LTDeviceSdioPort_PerformReadCallback)(LTDeviceSdioPort *port, u32 nBytes,
                                                    void *pClientData);
/**<
 * @brief Callback function to inform the client listening at a port that data is arriving.
 *
 * @param pClient     SDIO Bus client registered as a listener at the port
 * @param portNumber  port number at which the data is arriving
 * @param nBytes      number of bytes sent to the port
 * @param pClientData client data pointer passed RegisterCallback when the callback was registered
 */

typedef_LTObject(LTDeviceSdioPort, 1) {
    bool (* Bind)(LTDeviceSdioPort *port, u8 portNumber,
                  LTDeviceSdioPort_PerformReadCallback pCallback, void *pClientData);
        /**< @brief Bind a read callback to a port.
         *
         *   @param[in] port        pointer to the LTDeviceSdioPort instance
         *   @param[in] portNumber  port at which the client is listening
         *   @param[in] pCallback   pointer to a callback function
         *   @param[in] pClientData client data returned by the callback function
         *   @return true if the callback was successfully bound to a port.
         *
         *   @note The callback runs on the thread that called Bind().
         *   @note Once Bind() is called you must call Unbind() or lt_destroy(port) before destroying
         *         the thread that called Bind(), or the system will crash.  Stated another way, if a
         *         port is still bound when the thread that bound the port goes away, an interrupt
         *         coming in on that port will crash because the ISR will dereference an LTOThread
         *         object cached during bind.
         *         The api doxygen for LT_GetCore()->GetCurrentThreadObject(), explicitly states
         *         "Do not cache this thread object."
         *                  ************************************************************************
         *                  ************************************************************************
         *         For now, DON'T DESTROY THE THREAD THAT CALLS BIND BEFORE UNBINDING THE PORT FIRST
         *                  ************************************************************************
         *                  ************************************************************************
         *         This could be fixed *now* by caching the current thread handle instead of the
         *         thread object, but this is not recommended as we're getting rid of handles,
         *         replacing them with weak references to objects, a la:
         *              LTWeakRef ref = LT_GetCore()->GetWeakRef(anyObject);
         *              LTObject *theObject = LT_GetCore()->ReserveObject(ref);
         *               / * if (theObject) use theObject as if you were using anyObject.  anyObject cannot be destroyed until theObject is released * /
         *              LT_GetCore()->ReleaseObject(theObject);
         *
         */

    void (* Unbind)(LTDeviceSdioPort *port);
        /**< @brief Unbind a callback from a port.
         *
         *   @param[in] port        pointer to the LTDeviceSdioPort instance */

    bool (* Read)(LTDeviceSdioPort *port, u8 *buffer);
        /**< @brief Read the data from the bus in the supplied buffer.
         *
         *   @param[in] port        pointer to the LTDeviceSdioPort instance
         *   @param[in] buffer      pointer to a buffer large enough to hold the number of bytes
         *                          requested by the last invocation of the callback bound to 'port'
         *   @return true if the requested number of bytes were read from the bus
         *
         *   This API is always called as a respond to a callback of the type
         *   LTDeviceSdioPort_PerformReadCallback bound to LTDeviceSdioPort instance referenced by
         *   'port'. The callback supplies the required buffer size, and the buffer size passed to
         *   Read() must match that size. */

    bool (* Write)(LTDeviceSdioPort *port, u8 *buffer, u32 len);
        /**< @brief Write the data to the bus from the supplied buffer.
         *
         *   @param[in] port        pointer to the LTDeviceSdioPort instance
         *   @param[in] buffer      pointer to a buffer
         *   @param[in] len         buffer size in bytes
         *   @return true if the requested number of bytes were accepted by the bus. */

} LTOBJECT_API;

/* The default size in bytes of a block sent over an SDIO bus. Data transmitted
 * using Write() and Read() calls may contain multiples of the block size. */
enum {
    kLTDeviceSdioPort_BlockSize     = 256,
    kLTDeviceSdioPort_MaxPortNumber =  15
};

LT_EXTERN_C_END
#endif // ROKU_LT_INCLUDE_LT_DEVICE_SDIO_LTSDIOPORT_H
