/******************************************************************************
 * lt/source/lt/system/usbconsole/LTConsoleUSB.c (originally from lt/source/system/shell/LTConsoleUSB.c)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/usb/LTDeviceUsbCDC.h>
#include <lt/product/config/LTProductConfig.h>
#include <lt/system/usbconsole/LTSystemUsbConsole.h>
#include <lt/system/shell/LTShellBanner.h>

/*__________________________
  LTConsoleUSB.c #defines */
#define LTCONSOLEUSB_THREAD_STACKSIZE   (512)
#define LTCONSOLEUSB_THREAD_NAME        "LTConsoleUSB"
#define VT100_CLEAR_SEQUENCE            "\x1b[H\x1b[2J"

/*_________________________
  LTConsoleUSB.c typedefs */

typedef_LTObjectImpl(LTSystemUsbConsole, LTSystemUsbConsoleImpl) {
    LTOThread              * pThread; /* to operate LTDeviceUsbCDC */
    LTConsoleConnector     * pConsoleConnector;
    LTDeviceUsbCDC         * pDeviceUsbCDC;
    u8                     * pUSBReadBuffer;
    u8                     * pUSBWriteBuffer;
    u32                      nReadBufferSize;
    u32                      nWriteBufferSize;
    u32                      nBytesInWriteBuffer;
    u32                      nOutputCharsDropped;
} LTOBJECT_API;

/*__________________________________
  LTConsoleUSB.c static variables */
static LTMutex                  * s_pMutex = NULL;
static LTSystemUsbConsoleImpl   * s_pConsoleUSB   = NULL;

/*_________________________
  LTConsoleUSB.c Helpers */
static bool EnableAuxiliaryUSBConsole(void);
static void DisableAuxiliaryUSBConsole(void);

static void OnUsbWriteReady(void * pClientData);
static void FlushWriteBufferInternal(void) {
    if (s_pConsoleUSB->nBytesInWriteBuffer) {
        s32 nBytesWritten = s_pConsoleUSB->pDeviceUsbCDC->API->Write(s_pConsoleUSB->pDeviceUsbCDC, s_pConsoleUSB->pUSBWriteBuffer, s_pConsoleUSB->nBytesInWriteBuffer);
        if (nBytesWritten < 1) return;
        if ((u32)nBytesWritten < s_pConsoleUSB->nBytesInWriteBuffer) lt_memmove(s_pConsoleUSB->pUSBWriteBuffer, s_pConsoleUSB->pUSBWriteBuffer + (u32)nBytesWritten, s_pConsoleUSB->nBytesInWriteBuffer - (u32)nBytesWritten);
        s_pConsoleUSB->nBytesInWriteBuffer -= (u32)nBytesWritten;
    }
}

/*___________________________
  LTConsoleUSB.c callbacks */
static void LTConsoleUSB_ConsolePutCharProc(const char * pChars, u32 nChars, void * pClientData) {
    /* This is called by LTCore.  It may be called in ISR context or thread context.
       It may be called with interrupts disabled or enabled.
     */
   LT_UNUSED(pChars);
   LT_UNUSED(nChars);
   LT_UNUSED(pClientData);

   LT_SIZE nMask = 0;
   bool bInISR = LT_GetCore()->InsideInterruptContext();

   if (bInISR) nMask = LT_GetCore()->Disable();
   else s_pMutex->API->Lock(s_pMutex);
   if (s_pConsoleUSB && (s_pConsoleUSB == (LTSystemUsbConsoleImpl *)pClientData) && pChars && nChars) {
       if (! bInISR) {
           /* write directly to the usb cdc device if we can, first flushing leftover characters from the write buffer */
           FlushWriteBufferInternal();
           if (s_pConsoleUSB->nBytesInWriteBuffer == 0) {
               // write buffer was cleared
               s32 nBytesWritten = s_pConsoleUSB->pDeviceUsbCDC->API->Write(s_pConsoleUSB->pDeviceUsbCDC, pChars, nChars);
               if (nBytesWritten > 0) {
                   pChars += nBytesWritten;
                   nChars -= nBytesWritten;
               }
           }
       }
       if (nChars) {
           /* still characters to send, copy them into our buffer and queue the thread */
           u32 numCharsAvailable = s_pConsoleUSB->nWriteBufferSize - s_pConsoleUSB->nBytesInWriteBuffer;
           if (nChars > numCharsAvailable) { s_pConsoleUSB->nOutputCharsDropped += (nChars - numCharsAvailable); nChars = numCharsAvailable; }
           lt_memcpy(s_pConsoleUSB->pUSBWriteBuffer + s_pConsoleUSB->nBytesInWriteBuffer, pChars, nChars);
           s_pConsoleUSB->nBytesInWriteBuffer += nChars;
           s_pConsoleUSB->pThread->API->QueueTaskProcIfRequired(s_pConsoleUSB->pThread, &OnUsbWriteReady, NULL, pClientData);
       }
   }
   if (bInISR) LT_GetCore()->Enable(nMask);
   else s_pMutex->API->Unlock(s_pMutex);
}

static void OnUsbReadReady(void * pClientData) {
    LT_UNUSED(pClientData);
    bool bBytesSent = false;
    s_pMutex->API->Lock(s_pMutex);
    if (s_pConsoleUSB && (s_pConsoleUSB == (LTSystemUsbConsoleImpl *)pClientData)) {
        s32 nBytes = (s32)s_pConsoleUSB->nReadBufferSize;
        while (nBytes == (s32)s_pConsoleUSB->nReadBufferSize) {
            nBytes = s_pConsoleUSB->pDeviceUsbCDC->API->Read(s_pConsoleUSB->pDeviceUsbCDC, s_pConsoleUSB->pUSBReadBuffer, nBytes);
            if (nBytes > 0) {
                s_pConsoleUSB->pConsoleConnector->API->SubmitConsoleInput(s_pConsoleUSB->pConsoleConnector, (const char *)s_pConsoleUSB->pUSBReadBuffer, (u32)nBytes);
                bBytesSent = true;
            }
        }
        if (bBytesSent) s_pConsoleUSB->pConsoleConnector->API->SubmitConsoleInput(s_pConsoleUSB->pConsoleConnector, NULL, 0); /* tell console connector we are done */
    }
    s_pMutex->API->Unlock(s_pMutex);
}

static void OnUsbWriteReady(void * pClientData) {
    LT_UNUSED(pClientData);
    s_pMutex->API->Lock(s_pMutex);
    if (s_pConsoleUSB && (s_pConsoleUSB == (LTSystemUsbConsoleImpl *)pClientData)) FlushWriteBufferInternal();
    s_pMutex->API->Unlock(s_pMutex);
}

static void OnUsbError(void * pClientData) {
    LT_UNUSED(pClientData);
}

/*______________________________________
  LTSystemUsbConsole object constructors */
static void LTSystemUsbConsoleImpl_DestructObject(LTSystemUsbConsoleImpl * pConsoleUSB);
static bool LTSystemUsbConsoleImpl_ConstructObject(LTSystemUsbConsoleImpl * pConsoleUSB) {
    /* Build usb machinery from scratch and use it to takeover the system console */
    do {
        /* create my objects and malloc my memory */
        if (NULL == (pConsoleUSB->pThread           = lt_createobject(LTOThread)))          break;
        if (NULL == (pConsoleUSB->pConsoleConnector = lt_createobject(LTConsoleConnector))) break;
        if (NULL == (pConsoleUSB->pDeviceUsbCDC     = lt_createobject(LTDeviceUsbCDC)))     break;

        /*  Init LTDeviceUsbCDC on the thread  */
        if (! pConsoleUSB->pDeviceUsbCDC->API->Init(pConsoleUSB->pDeviceUsbCDC, "LTConsole", pConsoleUSB->pThread, OnUsbReadReady, OnUsbWriteReady, OnUsbError, pConsoleUSB)) break;

        if (NULL == (pConsoleUSB->pUSBReadBuffer    = lt_malloc((pConsoleUSB->nReadBufferSize  = pConsoleUSB->pDeviceUsbCDC->API->GetMaxReadSize(pConsoleUSB->pDeviceUsbCDC)))))  break;
        if (NULL == (pConsoleUSB->pUSBWriteBuffer   = lt_malloc((pConsoleUSB->nWriteBufferSize = pConsoleUSB->pDeviceUsbCDC->API->GetMaxWriteSize(pConsoleUSB->pDeviceUsbCDC))))) break;

        /* configure thread but don't start it yet (to simplify failure recovery) */
        pConsoleUSB->pThread->API->SetStackSize(pConsoleUSB->pThread, LTCONSOLEUSB_THREAD_STACKSIZE);

        /* put a vt100 clear esc sequence followed by the shell banner into the write (output) buffer. */
        pConsoleUSB->nBytesInWriteBuffer = sizeof(VT100_CLEAR_SEQUENCE) - 1;
        lt_memcpy(pConsoleUSB->pUSBWriteBuffer, VT100_CLEAR_SEQUENCE, pConsoleUSB->nBytesInWriteBuffer);

        u32 nBannerSize = (u32)(sizeof(ROKU_LT_BANNER) - 1);
        nBannerSize = LT_MIN(nBannerSize, (u32)(pConsoleUSB->nWriteBufferSize - pConsoleUSB->nBytesInWriteBuffer));
        lt_memcpy((char *)(pConsoleUSB->pUSBWriteBuffer + pConsoleUSB->nBytesInWriteBuffer), ROKU_LT_BANNER, nBannerSize);
        pConsoleUSB->nBytesInWriteBuffer += nBannerSize;

        if (! pConsoleUSB->pDeviceUsbCDC->API->Start(pConsoleUSB->pDeviceUsbCDC)) break;

        /* takeover system console and if successful, set s_pConsoleUSB and start the thread */
        if (pConsoleUSB->pConsoleConnector->API->ConnectConsole(pConsoleUSB->pConsoleConnector, LTConsoleUSB_ConsolePutCharProc, pConsoleUSB)) {
            /* everything is good, set s_pConsoleUSB, and configure and start the thread  */
            LT_SIZE nMask = LT_GetCore()->Disable(); s_pConsoleUSB = pConsoleUSB; LT_GetCore()->Enable(nMask);
            pConsoleUSB->pThread->API->Start(pConsoleUSB->pThread, LTCONSOLEUSB_THREAD_NAME, NULL, NULL);
        }
    } while (false);

    if (s_pConsoleUSB) {
        /* Success! Prime the i/o to get our banner printed
        */
        pConsoleUSB->pThread->API->QueueTaskProcIfRequired(pConsoleUSB->pThread, &OnUsbWriteReady, NULL, pConsoleUSB);
        pConsoleUSB->pThread->API->QueueTaskProcIfRequired(pConsoleUSB->pThread, &OnUsbReadReady, NULL, pConsoleUSB);
    }
    else {
        /* we failed; manually call the destructor to clean up */
        LTSystemUsbConsoleImpl_DestructObject(pConsoleUSB);
    }

    return s_pConsoleUSB ? true : false;
}

static void LTSystemUsbConsoleImpl_DestructObject(LTSystemUsbConsoleImpl * pConsoleUSB) {
    if (pConsoleUSB->pConsoleConnector) pConsoleUSB->pConsoleConnector->API->DisconnectConsole(pConsoleUSB->pConsoleConnector);
    if (pConsoleUSB->pDeviceUsbCDC) {
        pConsoleUSB->pDeviceUsbCDC->API->Stop(pConsoleUSB->pDeviceUsbCDC);
        lt_destroyobject(pConsoleUSB->pDeviceUsbCDC);
    }
    if (pConsoleUSB->pConsoleConnector) lt_destroyobject(pConsoleUSB->pConsoleConnector);
    if (pConsoleUSB->pThread) {
        /* we are in the mutex; we don't want to terminate the thread and then call WaitUntilFinished because
           we would deadlock if the thread was blocked on the mutex in one of the callbacks so instead
           we have orchestrated that this destructor is only called when:
                (a) there was an error in the constructor and the thread has been created but not started, or
             or (b) we are switching from usb to uart and we've already taken the thread out and nullified pConsoleUSB->pThread.
           Therefore it is safe to just call lt_destroyobject(pConsoleUSB->pThread) if pConsoleUSB->pThread exists because we know it won't wait. */
        lt_destroyobject(pConsoleUSB->pThread);
    }
    if (pConsoleUSB->pUSBReadBuffer) lt_free(pConsoleUSB->pUSBReadBuffer);
    if (pConsoleUSB->pUSBWriteBuffer) lt_free(pConsoleUSB->pUSBWriteBuffer);
}

/*______________________________
  LTConsoleUSB enable/disable */

static bool
EnableAuxiliaryUSBConsole(void) {
    s_pMutex->API->Lock(s_pMutex);
    bool bSuccess = s_pConsoleUSB ? true : ((s_pConsoleUSB = (LTSystemUsbConsoleImpl *)lt_createobject_typed(LTSystemUsbConsole, LTSystemUsbConsoleImpl)) ? true : false);
    s_pMutex->API->Unlock(s_pMutex);
    return bSuccess;
}

static void
DisableAuxiliaryUSBConsole(void) {
 /* to disable the auxiliary usb console we simply destroy s_pConsoleUSB, but we have to be careful,
    because LTCore will call the AuxConsolePutcharProc from isr context under various circumstances,
    (e.g. when an Isr does LTLOG_STOMP).  First we lock the mutex to guarantee exclusive access with
    other threads. Then we'll disable and enable interrupts around copying
    s_pConsoleUSB to a local variable and nullifying it.  That way we can be sure it won't be used by
    AuxConsolePutcharProc while we destroy it */
    LTOThread * pDestroyThread = NULL;
    s_pMutex->API->Lock(s_pMutex);
    LT_SIZE nMask = LT_GetCore()->Disable(); LTSystemUsbConsoleImpl * pConsoleUSB = s_pConsoleUSB; s_pConsoleUSB = NULL; LT_GetCore()->Enable(nMask);
    if (pConsoleUSB) {
        /* We have a pConsoleUSB to destroy.  Before destroying it, copy out its thread pointer and nullify
           the pointer in the object (see destructor). Later, after we exit the mutex, call WaitForTermination()
           and destroy. */
        if ((pDestroyThread = pConsoleUSB->pThread)) { pConsoleUSB->pThread = NULL; pDestroyThread->API->Terminate(pDestroyThread); }
        lt_destroyobject(pConsoleUSB); /* destructor unregisters USB i/o from LTCore and releases all built USB resources */
    }
    s_pMutex->API->Unlock(s_pMutex);
    if (pDestroyThread) {
        // now that we have unlocked the mutex, we can wait on the thread to finish
        pDestroyThread->API->WaitUntilFinished(pDestroyThread, LTTime_Infinite());
        lt_destroyobject(pDestroyThread);
    }
}

/*________________________________
  LTConsoleUSB.c Initialization */
void LTSystemUsbConsole_LibInit(void) {
    s_pMutex = lt_createobject(LTMutex);

    /* Enable the auxiliary USB console unless the product config tells us not to.
       We have already passed the check that we are running on an insecure developer unit or an LTAT enabled unit
       so we are all-clear for starting LTSystemShell on the uart and the usb console if we want to */
    bool bStartUSBConsole = true;
    LTProductConfig *pProductConfig = lt_openlibrary(LTProductConfig);
    if (pProductConfig) {
        if (pProductConfig->IsIntegerZero(pProductConfig->GetLibraryConfigSection("LTSystemShell"), "usbconsole/autostart")) bStartUSBConsole = false;
        lt_closelibrary(pProductConfig);
    }
    if (bStartUSBConsole) EnableAuxiliaryUSBConsole();
}

void LTSystemUsbConsole_LibFini(void) {
    DisableAuxiliaryUSBConsole();
    lt_destroyobject(s_pMutex);
    s_pMutex = NULL;
}

define_LTObjectImplPublic(LTSystemUsbConsole, LTSystemUsbConsoleImpl);

define_LTObjectLibrary(1, LTSystemUsbConsole_LibInit, LTSystemUsbConsole_LibFini);
