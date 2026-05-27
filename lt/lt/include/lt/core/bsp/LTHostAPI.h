/******************************************************************************
 * <lt/core/LTHostAPI.h>                                            LT Host API
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTHOSTAPI_H
#define ROKU_LT_INCLUDE_LT_CORE_LTHOSTAPI_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/*__________________________________________________________________________________________
  Callback function types for LTHostAPI functions that have a callback function parameter */
typedef bool (LTHostAPI_LibraryEnumProc)(const char * pLTLibraryName, void * pClientData); /**< Enumeration callback type for LTHostAPI_LibraryEnumerate */

/*______________________________
 / LTCore Hosted OS Interface */
typedef struct LTHostAPI LTHostAPI;

struct LTHostAPI {
  /*________________________________________________________________________________________________
    Functions for hosted architectures that have an existing underlying host operating system and   /
    don't use LTK as the real time kernel, e.g. Roku OS and other Linux platforms.                 /

    The functions declared in struct LTCoreHosted are only required to enable LT instances to run
    in process contexts of an existing host operating systems, for example in the application
    process of RokuOS or in the ltrun command line process in desktop linux. */

    /* allocation functions */
    void *      (* malloc)(LT_SIZE nBytes);                 /**< allocate nBytes from the heap */
    void *      (* realloc)(void * pMem, LT_SIZE nBytes);   /**< reallocate pMem with size nBytes */
    void        (* free)(void * pMem);                      /**< free memory allocated with malloc and realloc */

    /* RAM functions      - these will be updated such that the BSP will communicate lists of
                            address regions and their properties for LTCore to manage as heaps */
    LT_SIZE     (* GetTotalSystemRAM)(void);          /**< returns the size of the heap in bytes. */
    LT_SIZE     (* GetAvailableSystemRAM)(void);      /**< returns the number of free bytes in the heap. */
    LT_SIZE     (* GetSystemRAMLowWatermark)(void);   /**< returns the least ever number of free bytes in the heap. */

    /* mutex */
    LT_SIZE     (* MutexInstanceSize)(void);            /**< LTCore allocates this size for the mutex */
    void        (* MutexInitialize)(void * pMutex);     /**< DOCUMENTATION_NEEDED */
    void        (* MutexFinalize)(void * pMutex);       /**< DOCUMENTATION_NEEDED */
    void        (* MutexLock)(void * pMutex);           /**< DOCUMENTATION_NEEDED */
    void        (* MutexUnlock)(void * pMutex);         /**< DOCUMENTATION_NEEDED */
    bool        (* MutexTryLock)(void * pMutex);        /**< DOCUMENTATION_NEEDED */

    /* monitor */
    LT_SIZE     (* MonitorInstanceSize)(void);                              /**< LTCore allocates this size for the monitor */
    void        (* MonitorInitialize)(void * pMonitor);                     /**< DOCUMENTATION_NEEDED */
    void        (* MonitorFinalize)(void * pMonitor);                       /**< DOCUMENTATION_NEEDED */
    void        (* MonitorEnter)(void * pMonitor);                          /**< DOCUMENTATION_NEEDED */
    void        (* MonitorExit)(void * pMonitor);                           /**< DOCUMENTATION_NEEDED */
    void        (* MonitorNotify)(void * pMonitor) LT_ISR_SAFE;             /**< DOCUMENTATION_NEEDED */
    bool        (* MonitorWait)(void * pMonitor, s64 nTimeoutNanoseconds);  /**< DOCUMENTATION_NEEDED */

    /* thread hosted os */
    LT_SIZE     (* ThreadInstanceSize)(void);                                         /**< LTCore allocates this size as your instance data for the Thread */
    u32         (* ThreadGetDefaultStackSize)(void);                                  /**< default stack size used if function present and user doesn't specify */
    void        (* ThreadInitializeAndStartScheduler)(
                    void * pThread,           /**< initialize pThread instance data, and start scheduler with it and subsequent parameters */
                    u8 nPriority,             /**< priority to make the thread, 1-31 are real time priorities, 1 is lowest, 31 is highest, 0 is non-real time */
                    u32 nStackSize,           /**< thread stack size in bytes */
                    const char * pName,       /**< name of thread; provided for parameterizing thread aware debugging if available, otherwise ignore */
                    void (* pInitialThreadProc)(void * pClientData), /**< entry point function for the thread */
                    void * pClientData);      /**< client data to pass into the thread entry point function
                                                   called only once by LT_Run().  This function should do three things:
                                                   1. perform any instance initialization of pThread.
                                                   2. start the scheduler, running the initial thread as specified by the function parameters.
                                                   3. block until the thread exits, then UNINITIALIZE THE INSTANCE DATA,  STOP THE SCHEDULER and return */
    void        (* ThreadStopScheduler)(void); /**< called to stop the scheduler.  All client tasks will have exited before this is called */
    bool        (* ThreadInitializeAndRun)(
                    void * pThread,           /**< pointer to thread */
                    u8 nPriority,             /**< priority to make the thread, 1-31 are real time priorities, 1 is lowest, 31 is highest, 0 is non-real time */
                    u32 nStackSize,           /**< thread stack size in bytes */
                    const char * pName,       /**< name of thread; provided for parameterizing thread aware debugging if available, otherwise ignore */
                    void (* pThreadProc)(void * pClientData), /**< entry point for the thread */
                    void * pClientData);      /**< client data to pass into the thread entry point function
                                                   called to spawn threads after the scheduler has been started.  This function should do three things:
                                                   1. perform any instance initialization of pThread.
                                                   2. run the initial thread as specified by the function parms.
                                                   3. return true from this function immediately after spawning the thread
                                                      or return false with the instance data uninitialized if the thread couldn't be started. */
    void        (* ThreadGetStackUsage)(void * pThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet);
                                /**< gets a thread's stack size, current usage and max usage (high water mark) in bytes. */
    bool        (* ThreadSetPriority)(void * pThread, u8 nPriority); /**< sets the priority of a running thread; 1-31 are real time priorities, 1 is lowest, 31 is highest, 0 is non-real time */
    bool        (* ThreadWaitUntilFinished)(void * pThread, s64 nTimeoutNanoseconds); /**< block for [at least] nTimeoutNanoseconds or return true at the point when the thread completely finishes all execution,
                           or false if the thread has not finished execution within the timeout period.  By returning true, this function indicates it is now legal to subsequently call ThreadFinalize(). */
    void        (* ThreadFinalize)(void * pThread);
                        /**< called to uninitialize the private instance data.  This function call will always be preceded by a call to
                           ThreadWaitUntilFinished() that returned a value of true, therefore it is guaranteed the thread is no longer running when this function is called. */
    void *      (* ThreadGetCurrentThreadInstanceData)(void);
                        /**< returns the thread instance data of the current running thread. LT uses this to get the LTThread handle of the current thread. This function is
                           specced to return the instance data pointer because for some implementations it's easier to retrieve the implementation specific instance data
                           from the current thread rather than creating additional thread local storage or a hash map from the instance data to the void *.  LT knows how
                           to get the void * from the instance data pointer. NOTE: don't return a copy of your instance data, return the actual pointer value previously
                           passed into LTCoreBSP_ThreadInitializeAndRun(). */
    void        (* ThreadSleep)(s64 nNanoseconds); /**< sleeps the calling thread for nNanoseconds */

    s64         (* GetThreadCpuUtilization)(void *pThread);
                    /**< Get the total amount of CPU time spent in a thread since it was started in nanoseconds.
                        @param[in] pThread thread structure pointer
                        @return the total amount of CPU time spent executing in the thread in nanoseconds.
                         return -1 on error */
#ifndef LT_NO_DYNAMIC_LOADER
/* libraries */
    bool        (* LibraryLoad)(const char * pLTLibraryName, void ** pLibraryHandleToSet);            /**< DOCUMENTATION_NEEDED */
    void        (* LibraryUnload)(void * pLibraryHandle);                                             /**< DOCUMENTATION_NEEDED */
    void *      (* LibraryLookupSymbol)(void * pLibraryHandle, const char * pSymbolName);             /**< DOCUMENTATION_NEEDED */
    bool        (* LibraryEnumerate)(LTHostAPI_LibraryEnumProc * pEnumProc, void * pClientData);   /**< DOCUMENTATION_NEEDED */
    LTLibrary_LTObjectMapEntry * (* GetLTLibraryObjectMap)(void);
#endif
};

#ifndef LT_NO_DYNAMIC_LOADER
#define LTHOSTAPI_LTLIBRARY_MAXNAMELEN        kLTLibrary_MaxNameLen         /* the maximum string length of an LTLibrary name */
#define LTHOSTAPI_LTLIBRARY_MAXNAMEBUFFERSIZE kLTLibrary_MaxNameBufferSize  /* the buffer size required to hold an LTLibrary name */

#define LTHOSTAPI_LOADLIBRARY_SUCCESS                0
#define LTHOSTAPI_LOADLIBRARY_ERROR_LIBNOTFOUND      1
#define LTHOSTAPI_LOADLIBRARY_ERROR_UNRESOLVEDSYMBOL 2
#define LTHOSTAPI_LOADLIBRARY_ERROR_LIBINVALID       3
#define LTHOSTAPI_LOADLIBRARY_ERROR_GENERIC          4
#endif

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTHOSTAPI_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 */
