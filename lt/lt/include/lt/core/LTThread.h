/******************************************************************************
 * <lt/core/LTThread.h>                                     LTThread Interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltcore_thread LTThread
 * @ingroup ltcore
 * @{
 */

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTTHREAD_H
#define ROKU_LT_INCLUDE_LT_CORE_LTTHREAD_H

#include <lt/LTObject.h>
#include <lt/core/LTTime.h>
LT_EXTERN_C_BEGIN

/**********************
 * LTThread Constants */
enum {
    kLTThread_PriorityLowest            = 0,
    kLTThread_PriorityHighest           = 30,

    kLTThread_PriorityDefault           = kLTThread_PriorityLowest,
    kLTThread_NumberOfPriorities        = kLTThread_PriorityHighest + 1,

    kLTThread_MaxNameLen                = 19,
    kLTThread_MaxNameBuff               = kLTThread_MaxNameLen + 1,

    kLTThread_MaxThreadSpecificIDLen    = 7,
    kLTThread_MaxThreadSpecificIDBuff   = kLTThread_MaxThreadSpecificIDLen + 1

};

typedef_LTENUM_SIZED(LTThread_ThreadState, u32) {
    kLTThread_ThreadState_InvalidHandle         = 0, /**< the thread handle is invalid */
    kLTThread_ThreadState_NotStarted            = 1, /**< the thread has not yet been started */
    kLTThread_ThreadState_ReadyToRun            = 2, /**< the thread is neither waiting nor blocked - it is on the scheduler's ready queue */
    kLTThread_ThreadState_Running               = 3, /**< the thread is currently executing on the cpu (%GetCurrentThread() always returns the thread in this state) */
    kLTThread_ThreadState_WaitBlocked           = 4, /**< the thread is waiting for a TaskProc to be queued or for a timer to expire */
    kLTThread_ThreadState_MutexBlocked          = 5, /**< the thread is blocked waiting to acquire a mutex */
    kLTThread_ThreadState_Sleeping              = 6, /**< the thread is sleeping */
    kLTThread_ThreadState_TerminatePending      = 7, /**< Terminate() has been called and the thread is in process of gracefully shutting down */
    kLTThread_ThreadState_Terminated            = 8  /**< the thread is no longer running */
} ;

typedef_LTENUM_SIZED(LTThread_ReleaseReason, u32) {
    kLTThread_ReleaseReason_NullReason          =  0, /**< useful for variable initialization; value never delivered to callbacks */
    kLTThread_ReleaseReason_Because             =  1, /**< no specific reason given, just because */
    kLTThread_ReleaseReason_TaskProcComplete    =  2, /**< the queued TaskProc completed normally. */
    kLTThread_ReleaseReason_TaskProcPurged      =  3, /**< Terminate() was called while the task proc was still in the queue and was purged without being run */
    kLTThread_ReleaseReason_TaskProcQueueFull   =  4, /**< task proc wasn't queued because target thread queue was full */
    kLTThread_ReleaseReason_ThreadInvalid       =  5, /**< task proc wasn't queued because target thread handle was invalid */
    kLTThread_ReleaseReason_ThreadNotStarted    =  6, /**< task proc wasn't queued because target thread wasn't started */
    kLTThread_ReleaseReason_ThreadTerminal      =  7, /**< task proc wasn't queued because target thread was terminated or pending termination */
    kLTThread_ReleaseReason_ThreadSpecificReset =  8, /**< thread specific client data was set to a new value or cleared */
    kLTThread_ReleaseReason_ThreadSpecificPurge =  9, /**< thread is shutting down due to Terminate(); all thread specific data is released */
    kLTThread_ReleaseReason_TimerKilled         = 10, /**< user has called KillTimer */
    kLTThread_ReleaseReason_TimerPurge          = 11, /**< thread is shutting down due to Terminate(); all timers are being killed */
    kLTThread_ReleaseReason_EventUnregistered   = 12, /**< the client unregistered for notification from the event  */
    kLTThread_ReleaseReason_EventDestroyed      = 13, /**< the client was registered for notification on the LTEvent when the LTEvent was destroyed. */
    kLTThread_ReleaseReason_EventPurged         = 14, /**< LTEvent purged a notification registration because the receiver thread is no longer running. */

    kLTThread_ReleaseReason_GatesOfHell,
    kLTThread_ReleaseReason_Last                = kLTThread_ReleaseReason_GatesOfHell - 1,
    kLTThread_ReleaseReason_First               = kLTThread_ReleaseReason_NullReason
};

/*********************************
 * LTThread forward declarations */

typedef        LTHandle                 LTThread;                 /**< represents a thread */
typedef struct LTThread_Snapshot        LTThread_Snapshot;        /**< represents a snapshot of an executing thread */

/*******************************
 * LTThread Procedure Typedefs */
typedef bool (LTThread_InitProc)(void);
    /**< type of procedure called to initialize a thread instance
     *
     * The client may supply an optional LTThread_InitProc to perform thread initialization which
     * indicates whether thread should stay alive or terminate when the InitProc concludes.
     * @return true if the thread should stay alive, false if it should terminate
     * @note If parameterization is needed, use SetThreadSpecificClientData
     * @see Start, Terminate, SetThreadSpecificClientData
     * @note if your LTThread_InitProc returns false, standard thread shutdown will begin immediately
     *       this includes calling the thread's LTThread_ExitProc, if specified
     */

typedef void (LTThread_ExitProc)(void);
    /**< type of procedure called to perform final cleanup of a thread instance
     *
     * The client may optionally supply an LTThread_ExitProc to perform final thread cleanup.
     *
     * @see Start, Terminate
     */

typedef void (LTThread_TaskProc)(void *pClientData);
    /**< type of procedure that is queued to an LTThread for execution
      *
      *  LTThreads perform tasks via the execution of queued LTThread_TaskProcs; this mechanism is explained
      *  in detail in the documentation of the function %QueueTaskProc()
      *
      *  @param pClientData the client data supplied when the task proc was queued
      *  @see QueueTaskProc
      */

typedef void (LTThread_TimerProc)(void *pClientData);
    /**< type of procedure executed when a timer fires
      *
      *  LTThreads specify an LTThread_TimerProc and optional pClientData when setting periodic and one shot timers.
      *
      *  @param pClientData the client data supplied when the timer was set
      *  @see SetTimer
      */

typedef void (LTThread_ClientDataReleaseProc)(LTThread_ReleaseReason releaseReason, void *pClientData);
    /**< type of procedure called to notify a client that LTThread has released ownership of client data
     *
     * LTThread functions that take a callback procedure may also take an optional pClientData pointer value, and an
     * optional LTThread_ClientDataReleaseProc.  LTThread caches the pClientData pointer value for supplying to the client's
     * callback procedure, assuming ownership of the pointer value.  The ClientDataReleaseProc is called to
     * inform the client when LTThread drops its cached copy of the pClientData pointer value, effectively
     * releasing ownership of the pointer value back to the client.  Clients should take appropriate action
     * in their ClientDataReleaseProc which could be to free the pointer, release it back into the pool, take no action, etc.
     *
     * An LTThread_ReleaseReason indicates the reason the client data is being released.  The following table lists
     * the release reasons and when they are indicated:
     * <pre>
     * ______               ____________
     * Reason               Circumstance
     *
     * TaskProcComplete     The queued TaskProc completed normally.
     * TaskProcQueueFull    The LTThread TaskProc Queue was full.  Note in this case the client is called back in their own thread before QueueTaskProc returns
     * TaskProcPurge        Terminate() has been called and the thread is shutting down; all existing queued TaskProcs are discarded unexecuted
     * ThreadSpecificReset  Thread specific client data was set to a new value or cleared
     * ThreadSpecificPurge  Terminate() has been called and the thread is shutting down; all thread specific data is released
     * TimerKilled          User has called KillTimer
     * TimerPurge           Terminate() has been called and the thread is shutting down; all thread specific data is released
     * EventDestroyed       The client was registered for notification on an LTEvent when the LTEvent was destroyed.
     *
     * ______               ______________
     * Reason               Thread Context
     *
     * TaskProcComplete     called from TaskProc Thread immediately following TaskProc invocation
     * TaskProcPurge        called from TaskProc Thread during thread shutdown
     * ThreadSpecificPurge  called from TaskProc Thread during thread shutdown
     * TimerPurge           called from TaskProc Thread during thread shutdown
     * TaskProcQueueFull    called from caller's Thread synchronously before QueueTaskProc() returns
     * ThreadSpecificReset  called from caller's Thread synchronously before SetThreadSpecificClientData() returns
     * TimerKilled          called from caller's Thread synchronously before KillTimer() returns
     * EventDestroyed       called from caller's Thread synchronously before Destroy() returns
     *
     * </pre>
     * @note Care must be taken to avoid performing any significant processing in a ClientDataReleaseProc.  Typically the activities performed
     *     fall into one of the following categories:
     *     <ol>
     *      <li>freeing pClientData, i.e free(pClientData);  This does NOT require mutex protection.</li>
     *      <li>return pClientData to a private pool - This REQUIRES mutex protection or other mutual exclusion technique, e.g. by using atomics</li>
     *      <li>take no action - if you're going to take no action, better to pass NULL in as the pClientDataReleaseProc parameter to the original function, e.g. QueueTaskProc</li>
     *      <li>examine of the contents of pClientData and take appropriate action.  <b>Important</b>: Remember not to consume time
     *      in your ClientDataReleaseProc, best to queue a TaskProc with this pClientData to your own thread for this type of infrequent circumstance.</li>
     *     </ol>
     * @see QueueTaskProc, SetTimer, SetThreadSpecificClientData, LTEvent
     *
     */

typedef void (LTThread_SnapshotRunningThreadsCB)(LTThread_Snapshot * pSnapshots, u32 nCount, void *pClientData);
    /**< type of procedure called back synchronously during the execution of SnapshotRunningThreads
      *
      * Clients supply a SnapshotRunningThreadsCB function and optional pClientData when calling SnapshotRunningThreads.
      * The callback is called back synchronously from within SnapshotRunningThreads, and as such requires no LTThread_ClientDataReleaseProc
      * because LTThread never takes ownership of the pClientData (because the operation of SnapshotRunningThreads is synchronous.
      *
      *  @param pSnapshots the thread snapshots of all running threads.
      *  @param pClientData the pClientData pointer value that was passed into SnapshotRunningThreads */

/**********************
 * LTThread Interface */
TYPEDEF_LTLIBRARY_INTERFACE(ILTThread, 1);

struct ILTThreadApi {

    INHERIT_INTERFACE_BASE

/*  ________________________________________________________
 *  ________________________________________________________
 *  Functions that operate on the specified hThread argument
 *  ________________
 *  Thread Execution - Start() and QueueTaskProc()
 */
    void (*Start)(LTThread hThread, LTThread_InitProc *pInitProc, LTThread_ExitProc *pExitProc);
       /**< starts thread execution
         *  Start() starts a thread running. The thread will remain in a wait state until
         *  a timer expires, a task proc is queued, or an event that this thread has registered for executes.
         *  occurs, the thread wakes up to process timers and queued TaskProcs and then re-enters its wait-state.
         *
         *  To communicate data to the thread's IniProct, ExitProc, and future queues TaskProcs, ThreadSpecificData, keyed by string may be set with SetThreadSpecificClientData()
         *  ThreadSpecificData is also known as Thread Local Storage on some systems.  This capability should be used sparingly so as to avoid bloating threads
         *  with the carriage of userdata.
         *
         *  The thread will remain in its dispatch-wait loop indefinitely (waiting for timers to expire, for TaskProcs
         *  to be queued, and for events this thread has registered for to notify); it will not exit this loop until Terminate() is called.  Terminate() causes the thread
         *  to gracefully and unconditionally exit.

         *  If the thread is in its wait state when Terminate() is called, the thread's internal shutdown
         *  sequence begins immediately.  If the thread is executing a TimerProc or TaskProc+ClientDataReleaseProc, or event callback
         *  pair when Terminate() is called, the shutdown sequence begins immediately upon return from the
         *  TimerProc, TaskProc+ClientDataReleaseProc, or Event notification..
         *
         *  LT Software Development guidelines state that to assure fidelity of experience, each individual
         *  TimerProc, TaskProc+ClientDataReleaseProc and event handler should take no more than 100ms to execute.
         *  Of course, TaskProcs that implement long running asynchronous operations for the purpose of offloading
         *  client threads are not subject to that guideline.  Any long running TimerProc, TaskProc+ClientDataReleaseProc, or EventHandler
         *  should call IsTerminatePending() at least every 100ms, and when it returns true, the long running
         *  operation should immediately gracefully end control should be relinquished to LTThread..  Any aborted state cleanup or
         *  state preservation must be handled at all such early return points.
         *
         *  The thread shutdown sequence:
         *    1 Cancels all timers
         *    2 Dequeues each queued task proc in turn without calling the TaskProc.  If the TaskProc has an associated ClientDataReleaseProc,
         *      the ClientDataReleaseProc is called.   The ClientDataReleaseProc should perform all required
         *      state cleanup and state preservation pertaining to its pClientData as applicable.  The queued task proc record
         *      is then discarded.  This is continued until all queued task procs are disposed of.
         *    3 Calls the thread's ExitProc if one was specified when the thread was created.  The thread's ExitProc
         *      should perform overall thread state cleanup and state preservation as applicable.  In particular, the
         *      ExitProc is the designated place to handle thread context cleanup as desired or required.
         *    4 As soon as the ThreadExit proc returns, the thread exits.  The LTThread handle remains valid until
         *      Destroy() is called.  Each LTThread can only be Run() once.
         *
         *  @param hThread the LTThread to Start
         *  @note If Destroy() is called on a thread that is running, Destroy() will terminate the thread, block until
         *        the thread exits, and then Destroy the LTThread handle.
         *  @note If start is called when the thread is not in its initial ThreadStateNotStarted state, then Start is a no-op.
         */


    bool (*StartSynchronous)(LTThread hThread, LTThread_InitProc *pInitProc, LTThread_ExitProc *pExitProc);
       /**< starts thread execution, blocking until the thread finishes executing its InitProc
        * This version of Start doesn't return until the thread finishes executing its InitProc in its own
        * thread context.  The return value from the thread's InitProc is returned, and, if false, the thread has been
        * automatically terminated (which occurs asynchronously!).
        *
        * @param hThread the LTThread to Start
        * @param pInitProc the thread's InitProc
        * @param pExitProc the thread's ExitProc
        * @return the return value from the starting thread's InitProc, or true if no InitProc was specified.
        */

        bool (*QueueTaskProc)(LTThread hThread, LTThread_TaskProc *pTaskProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) LT_ISR_SAFE;
       /**< queues a TaskProc with optional ClientDataReleaseProc for execution on the thread
         *  call QueueTaskProc to queue a TaskProc for execution on the thread.  An optional ClientDataReleaseProc may be queued along with the TaskProc and if so,
         *  will be called in hThread's thread context immediately upon return from the TaskProc or, in the event of thread shutdown before task proc execution,
         *  when the TaskProc is dequeued and discarded.  In the event the TaskProc is discarded, only the ClientDataReleaseProc will be called.
         *  The ClientDataReleaseProc is the designated place for the owner of pClientData to reclaim, release, or free it as desired or required.
         *  @param hThread the thread upon which to run pTaskProc and pClientDataReleaseProc if specified.
         *  @param pTaskProc the task proc to execute on hThread
         *  @param pClientDataReleaseProc the client data release proc to run after pTaskProc completes or when the pTaskProc is discarded due to thread shutdown.  NULL may
                   be specified if no pClientDataReleaseProc is desired or required.  pClientDataReleaseProc is the designated place for the owner of pClientData to reclaim, release,
                   or free it as desired or required.
         *  @param pClientData the client data that will be passed in to pTaskProc and pClientDataReleaseProc or NULL if no client data is required
         *  @return true if queueing successful, false on failure.
         *  @note  ANY RESOURCE HANDOFF OF pClientData via QueueTaskProc or QueueTaskProcIfRequired can ONLY BE GUARANTEED reclaimed by the pClientDataReleaseProc.
         *         YOU **MUST** (this means you) SUPPLY pClientDataReleaseProc FOR DETERMINISTIC RESOURCE RECLAMATION of pClientData after handoff.
         *         (Because it is expressly not guaranteed that pTaskProc will be called in all circumstances such as when the thread has been instructed to terminate with task procs in the queue).
         *         Must use example: If you malloc a client data pointer and queue a task proc to put it in a list maintained by the target thread,
         *         the only way to know if that task proc to put the item in the list actually ran is to supply a pClientDataReleaseProc that checks that the LTThread_ReleaseReason parameter is kLTThread_ReleaseReason_TaskProcComplete.
         *         If not, your release proc must dispose of that pointer. Don't lose resources.  Don't be a resource loser.
         *  @see   LTThread_ReleaseReason
         */
    bool (*QueueTaskProcIfRequired)(LTThread hThread, LTThread_TaskProc *pTaskProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) LT_ISR_SAFE;
        /** queues a task proc if not already queued
         *  QueueTaskProcIfRequired works like QueueTaskProc except that it will not queue pTaskProc if there is a matching pTaskProc, pClientDataReleaseProc, and pClientData
         *  triplet already in the queue.  The intended use of this function is to handle the situation in which one
         *  wishes to wake up a thread with pTaskProc to service a mutex protected shared record, and one task proc invocation is sufficient
         *  to handle all of the updates that occurred since the last pTaskProc invocation. This function guarantees 1 and only 1 invocation
         *  of pTaskProc with the same pClientDataReleaseProc and pClientData will be in a thread's queue at a time.
         *  @param hThread the thread upon which to run pTaskProc and pClientDataReleaseProc if specified.
         *  @param pTaskProc the task proc to execute on hThread
         *  @param pClientDataReleaseProc the task complete proc to run after pTaskProc completes or in the event pTaskProc is discarded due to thread shutdown.  NULL may
                   be specified if no pClientDataReleaseProc is desired or required.  pClientDataReleaseProc is the designated place for the owner of pClientData to reclaim, release,
                   or free it as desired or required.
         *  @param pClientData the client data that will be passed in to pTaskProc and pClientDataReleaseProc or NULL if no client data is required
         *  @return true if queueing successful (or already queued), false on failure.
         *  @see   QueueTaskProc
         *  @note  ANY RESOURCE HANDOFF OF pClientData via QueueTaskProc or QueueTaskProcIfRequired can ONLY BE GUARANTEED reclaimed by the pClientDataReleaseProc.
         *         YOU **MUST** (this means you) SUPPLY pClientDataReleaseProc FOR DETERMINISTIC RESOURCE RECLAMATION of pClientData after handoff.
         *         (Because it is expressly not guaranteed that pTaskProc will be called in all circumstances such as when the thread has been instructed to terminate with task procs in the queue).
         *         Must use example: If you malloc a client data pointer and queue a task proc to put it in a list maintained by the target thread,
         *         the only way to know if that task proc to put the item in the list actually ran is to supply a pClientDataReleaseProc that checks that the LTThread_ReleaseReason parameter is kLTThread_ReleaseReason_TaskProcComplete.
         *         If not, your release proc must dispose of that pointer. Don't lose resources.  Don't be a resource loser.
         *  @see   LTThread_ReleaseReason
         */

/*  _________________________
 *  Getting thread attributes
 */ void   (*GetName)(LTThread hThread, char nameBuffToFill[kLTThread_MaxNameBuff]);
       /**< fills nameBuff with the name of the thread
         *  @param hThread the LTThread to get the name of
         *  @param nameBuffToFill a buffer of at least size kLTThread_MaxNameBuff to receive the thread's name
         *  @note if hThread is an invalid handle, GetName() sets nameBuffToFill[0] = 0
         */
    void * (*GetThreadSpecificClientData)(LTThread hThread, const char *pKey);
       /**< gets the client set context for the thread or NULL if no context has been set
         *  @param hThread the LTThread on which to get the thread specific client data
         *  @param pKey the key of the thread specific client data
         *  @return thread specific client data for given pKey or NULL if nonexistent or hThread is an invalid handle
         */
    LTThread_ThreadState(*GetThreadState)(LTThread hThread);
       /**< gets the thread's current state
         *  @param hThread the LTThread whose thread state to get
         *  @return the thread's state
         *  @note If hThread is an invalid handle, kLTThread_ThreadState_InvalidHandle is returned
         */
    u32    (*GetStackSize)(LTThread hThread);
       /**< gets the thread's stack size in bytes
         *  @param hThread the LTThread whose stacksize to get
         *  @return the thread's stack size in bytes or 0 if hThread is invalid
         *  @see GetStackUsage
         */
    void   (*GetStackUsage)(LTHandle hThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet);
       /**< gets a thread's stack size, current stack usage and max stack usage (high water mark) in bytes.
         *  @param hThread the LTThread whose stack usage to get
         *  @param pStackSizeToSet a pointer that will receive the thread's actual stack size in bytes
         *  @param pCurrentStackUsageToSet a pointer that will receive the thread's current stack usage in bytes
         *  @param pMaxStackSizeToSet a pointer that will receive the thread's max ever stack usage in bytes
         *  @note all parameters must be non-null for this function to work. if an invalid thread handle is passed or any of the numeric values are null,
         *        then all non-null numeric values will be set to zero.  If all of the parameters are valid, and any individual value is unobtainable, that value
         *        will be assigned zero.
         *  @note the value placed into *pStackSizeToSet may differ from the value returned by GetStackSize() because GetStackSize()
         *        returns the amount of stack that was requested before the thread starts.  The actual stack size may have been modified for alignment reasons.
         *        This function places the actual stack size into *pStackSizeToSet.
         *  @note the value placed into *pMaxUsageToSet is typically a value that is sampled periodically (e.g. on context switch).  This makes the value
         *        a close approximation of the max stack usage, rather than a precise figure.
         */
    void   (*GetHeapUsage)(LTHandle hThread, u32 * pCurrentHeapUsageToSet, u32 * pMaxHeapUsageToSet);
       /**< gets a thread's current heap usage and max heap usage (high water mark) in bytes.
         *  @param hThread the LTThread whose heap usage to get
         *  @param pCurrentHeapUsageToSet a pointer that will receive the thread's current heap usage in bytes
         *  @param pMaxHeapSizeToSet a pointer that will receive the thread's max ever heap usage in bytes
         *  @note all parameters must be non-null for this function to work. if an invalid thread handle is passed or any of the numeric values are null,
         *        then all non-null numeric values will be set to zero.  If all of the parameters are valid, and any individual value is unobtainable, that value
         *        will be assigned zero.
         */
    u8     (*GetPriority)(LTThread hThread);
       /**< get the priority for a thread
         *  Obtain the priority for the given thread.
         *  @param hThread the thread to get the priority of
         *  @return thread priority in the range [0 .. 30], where 0 is the lowest thread priority and 30 is the highest.
         */
    bool   (*IsSystemThread)(LTThread hThread);
       /**< returns true if the thread is a system thread
         *  @param hThread the LTThread to determine whether or not is a system thread
         *  @return true if hThread is a system thread, false otherwise
         *  @note System threads are created by LTCore and cannot be terminated
         */
    bool   (*IsSystemThreadTimeCritical)(LTThread hThread);
       /**< returns true if the thread is a system thread that is also time critical
         *  @param hThread the LTThread to determine whether or not is a system thread that is also time critical
         *  @return true if hThread is a system thread that is time critical, false otherwise
         *  @note System threads are created by LTCore and cannot be terminated.  System threads that are time critical
         *        can not have their priority changed.
         */
    bool   (*IsCurrentThread)(LTThread hThread);
       /**< returns true if the thread is the currently running thread
         *  @param hThread the LTThread to determine whether or not is the currently running thread
         *  @return true if hThread is the currently running thread, false otherwise
         */
    void   (*GetSnapshot)(LTThread hThread, LTThread_Snapshot * pSnapshotToFill);
       /**< fills *pSnapsotToSet with a snapshot of the thread.
         *  @param hThread the thread to get the snapshot of
         *  @param pSnapshotToSet a pointer to an LTThread_Snapshot that has nStructureSize set to sizeof(LTThread_Snapshot)
         *  @note IMPORTANT: set pSnapsotToFill->nStructureSize = sizeof(*pSnapshotToFill); before calling GetSnapshot
         *  @note if pSnapshotToFill->nStructureSize is not set to a known sizeof(LTThread_Snapshot) then this function will no-op
         *  @note if hThread is invalid, GetSnapshot() will zero all fields except m_nStructure size
         */

/*  _________________________
 *  Setting thread attributes
 */    void (*SetThreadSpecificClientData)(LTThread hThread, const char *pKey, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
       /**< sets a thread specific client data, identified by id
         *  Call SetThreadSpecificClientData to associate thread instance specific pClientData with a thread instance.

         *  @param hThread the thread to set the context for
         *  @param pKey the identifier of the thread specific client data type
         *  @param pClientDataReleaseProc the client data release proc to run when the thread specific client data is reset, cleared, or purged
         *  @param pClientData the thread specific client data to set
         *  @note  use a value of NULL for pClientData to clear the client data for the given ID
         */
   void   (*SetStackSize)(LTThread hThread, u32 nStackSize);
       /**< sets the stacksize for the thread
         *  Call SetStackSize() to change the thread's stacksize.  This function only works before Start() is called.
         *  When a thread object is created, the stack size gets set to the LT default stack size.  Use this function
         *  to change the stack size, before calling Start().  Once the thread is started, the stacksize is fixed.
         *  @param hThread the thread to set the stacksize for
         *  @param nStackSize stacksize in bytes
         *  @note This function only works when the thread is in the state ThreadStateNotStarted
         */
    void   (*SetPriority)(LTThread hThread, u8 nPriority);
       /**< sets the priority for a thread
         *  Sets thread priority to nPriority, which must be in the range [0 .. 30], where 0 is the lowest priority and 30 is the highest.
         *  @param hThread the thread to set the priority for
         *  @param nPriority the priority to set the thread to.  This parameter must be in the range [0 .. 30].
         *  @note The default thread priority is 0.
         */

/*  ___________________________
 *  Controlling thread shutdown
 */ void   (*Terminate)(LTThread hThread);
       /**< instructs a thread to gracefully shut down
         *  Call Terminate() to instruct a thread to gracefully shut down.  Terminate() is asynchronous.
         *  @param hThread the thread to terminate
         *  @note System threads cannot be terminated
         *  @see IsSystemThread()
         */
    bool   (*WaitUntilFinished)(LTThread hThread, LTTime timeout);
       /**< blocks until a thread is completely shut down
         *  Call WaitUntilFinished() to block until a thread is completely shut down.  WaitUntilFinished()
         *  is usually preceded by a call to Terminate().
         *  @param hThread the thread to wait for
         *  @return true if the thread finished within the timeout, false otherwise
         *  @note The current thread cannot wait on itself.
         *  @see Terminate()
         */

    bool   (*IsTerminatePending)(LTThread hThread);
       /**< returns true if a Terminate() request is pending
         *  Call IsTerminatePending() when executing inside your thread for long periods of time.
         *  If the result is true, gracefully abandon your processing and return from your TaskProc to
         *  return control to the LTThread main loop, which will proceed with graceful thread termination.
         *  @param hThread the thread for which to determine if terminate is pending
         *  @return true if Terminate() has been called on a thread and the terminate is still pending, false otherwise
         *  @note This function is equivalent to: (iThread->GetThreadState(hThread) == kLTThread_ThreadState_TerminatePending)
         *  @see Terminate(), GetThreadState()
         */

    void (*SetTimer)(LTThread hThread, LTTime timeout, LTThread_TimerProc *pTimerProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
       /**< sets a timer
         *
         *  Call %SetTimer() to set a timer in hThread that will fire when the specified timeout expires.  The timer will repeat until %KillTimer() is called.
         *  A one-shot timer may be implemented by calling %KillTimer() in the timer's TimerProc.<br><br>
         *  <b>Timer identity</b><br>
         *  Timers are uniquely associated with a thread and are uniquely identified on that thread by TimerProc + pClientData.
         *  If %SetTimer() is called with the TimerProc and pClientData of an existing timer, it will update the existing timer's
         *  timeout/pClientDataReleaseProc and reschedule the timer.  This is illustrated in the following table showing the effect of
         *  calls to %SetTimer() in each of 4 possible scenarios with respect to existing timers.
         *  <pre><i>Effect of %SetTimer() calls on existing timers</i>
         *  ________   ______________   ________________        _____________________________
         *  Scenario   Same TimerProc   Same Client Data        Subsequent %SetTimer() Effect \\__________________________
         *         a              Yes                Yes        Reset timeout/ReleaseProc of existing timer & reschedule
         *         b              Yes                 No        Set new timer
         *         c               No                Yes        Set new timer
         *         d               No                 No        Set new timer
         *  </pre>
         *  <b>Timer accuracy</b><br>
         *  Timers can be scheduled to fire with any timeout value down to 1ms and are accurate except when circumstances
         *  arise which cause delayed firing such as the execution of higher priority threads or long running TaskProcs on
         *  the same thread. Of interest is how the next firing is scheduled when a delay has occurred.  %SetTimer() always
         *  schedules a timer's next firing relative to its current <i>actual</i> fire time as outlined in the following example:
         *  <div style="margin-left: 24px; margin-right: 24px"><i>Example of delayed firing</i><br>Let's say kernelTime is
         *  at 1000ms (1 second after boot) which we will write as kernelTime(1000ms).  Now, let's say at kernelTime(1000ms)
         *  a timer is scheduled for 50ms.  Then, let's say at kernelTime(1040ms) a flurry of activities of higher priority
         *  threads takes place, consuming 20ms before the thread with the timer can run.  In this example, the timer actually
         *  fires at kernelTime(1060ms), 10ms late and then next timer will be scheduled to run 50ms later at kernelTime(1110ms).
         *  </div>
         *  <b>Applicability</b><br>
         *  The scheduling behavior of %SetTimer() is sufficient for all timer use cases except for very specific real-time
         *  deadline-based timer use cases.  Even so, a thread using %SetTimer() with a high enough priority will never incur delays,
         *  provided the overall system design can accommodate and guarantee this.  For the very specific deadline-based real
         *  time use case, the alternative form, %SetTimerAbsolute(), treats the timeout duration as a deadline and varies the
         *  inter-firing scheduling duration to keep the deadline.  As a general rule of thumb, if you think you should use
         *  %SetTimerAbsolute() you probably shouldn't.
         *
         *  @param hThread the thread on which to set the timer
         *  @param timeout the timeout duration for the timer
         *  @param pTimerProc the TimerProc to execute in the context of hThread, each time the timer fires
         *  @param pClientDataReleaseProc the client data release proc to run when the timer is killed or purged
         *  @param pClientData the client data to pass into pTimerProc when the timer fires
         *  @see KillTimer, RestartTimer, SetTimerAbsolute
         */

    void (*SetTimerAbsolute)(LTThread hThread, LTTime kernelStartTime, LTTime deadlinePeriod, LTThread_TimerProc *pTimerProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
        /**< sets a timer with absolute start time that repeats on a deadline
          *
          *  %SetTimerAbsolute() sets a timer that first fires when kernel time reaches the specified kernelStartTime, and then fires repeatedly at absolute
          *  multiples of the specified duration known as the <i>deadlinePeriod</i>.   The anchor point of the deadline and how the first deadline is determined
          *  depends on the value of the kernelStartTime parameter relative to the current kernel time, which we will write as kernelTime(now).
          *  The following table illustrates how the value of kernelStartTime affects %SetTimerAbsolute:
          *  <pre><i>Effect of kernelStartTime value vis a vis kernelTime(now)</i>
          *  _______________________    __________________                     ____________________   ________
          *  when kernelStartTime is    deadline anchor is                     First Timer Fires at   Use Case
          *            LTTime_Zero()       kernelTime(now)         kernelTime(now) + deadlinePeriod   Start deadline based timer now
          *       <= kernelTime(now)       kernelStartTime   kst + dp * (1 + (((kt(now) - kst)/dp))   Synchronize deadlines with past event
          *        > kernelTime(now)       kernelStartTime                          kernelStartTime   Synchronize deadlines with future event
          *
          *  Note: abbreviated equation represents first kernelStartTime relative deadline occurring after kernelTime(now); it expands to:
          *             kernelStartTime + deadlinePeriod * (1 + (((kernelTime(now) - kernelStartTime) / deadlinePeriod)))
          *  </pre>
          *  <b>Accuracy</b><br>
          *  In a well running system, deadlines are always met.  However, just as in the %SetTimer() example, the actions of higher priority threads can
          *  cause deadlines to be missed.  When a deadline is missed, the timer will fire as soon as the thread regains the cpu and finishes its currently
          *  executing TaskProc or TimerProc, if any, or, if idle, immediately upon regaining execution.  The next firing is scheduled in realignment with the
          *  original deadline anchor and deadline period to maintain synchronization.  If multiple consecutive deadlines are missed in a period, only
          *  only one off-period timer fires before realignment.
          *
          *  @param hThread the thread on which to set the timer
          *  @param kernelStartTime future time of first timer fire, or past time of deadline anchor, or LTTime_Zero to start timer now
          *  @param deadlinePeriod the duration of time between deadlines
          *  @param pTimerProc the TimerProc to execute in the context of hThread, each time the timer fires
          *  @param pClientDataReleaseProc the client data release proc to run when the timer is killed or purged
          *  @param pClientData the client data to pass into pTimerProc when the timer fires
          *  @see KillTimer, SetTimer
          *  @note The functions %SetTimer,%SetTimerAbsolute, and %RestartTimer have no effect on existing absolute timers because absolute timers
          *        once set can not be re-set or restarted.  To start a new timer with the same TimerProc and client data as an existing absolute timer, kill the existing
          *        absolute timer first.
          */

    LTTime (*GetTimerExpirationKernelTime)(LTThread hThread, LTThread_TimerProc *pTimerProc, void *pClientData);
        /**< returns the kernel time when the timer will expire or LTTime_Zero() if no such timer is set
          *
          *  %GetTimerExpirationKernelTime() can be used to determine if a timer has been set, and if so, the absolute kernel time
          *   when the timer will fire next.
          *
          *  @param hThread the thread for which to query the time
          *  @param pTimerProc the timer proc of the timer in question
          *  @param pClientData the clientdata that would have been set with the hThead and timerProc in SetTimer or SetTimerAbsolute
          *  @return the kernel time when the timer will fire, if set, or LTTime_Zero() if there is no such timer set
          *
          *  @note  <pre>The following example shows how this function may be employed to determine if a timer exists for the purpose
          *         of calling SetTimer() to start a timer only if it is not already started:
          *
          *             if (LTTime_IsZero(iLTThread->GetTimerExpirationKernelTime(hThread, pTimerProc, pClientData))) {
          *                 / * timer is not currently set; go ahead and set it now * /
          *                 iLTThread->SetTimer(hThread, timeoutDuration, &MyTimerProc, NULL, &pClientData);
          *             }
          *
          */

    void   (*KillTimer)           (LTThread hThread, LTThread_TimerProc *pTimerProc, void *pClientData);
        /**< Kills a previously set timer
         *
         * call %KillTimer to eliminate a previously set timer.  The client data release proc passed into SetTimer, if any,
         * will be called with the releaseReason kLTThread_ReleaseReason_TimerKilled.
         *
         *  @param hThread the thread that has the timer to kill
         *  @param pTimerProc the TimerProc of the timer to kill
         *  @param pClientData the client data of timer to kill
         *
         */

    void   (*RestartTimer)        (LTThread hThread, LTThread_TimerProc *pTimerProc, void *pClientData);
        /**< restarts the timer countdown
         *
         * %RestartTimer will restart the timer to expire from time now plus its initially set duration
         *
         *  @param hThread the thread that has the timer to restart
         *  @param pTimerProc the TimerProc of the timer to restart
         *  @param pClientData the client data of timer to restart
         *
         *  @note Absolute timers can not be restarted (because it doesn't make sense).  To change
         *        parameters of an absolute timer, Kill the absolute timer and call SetTimerAbsolute() with
         *        the new parameters.
         *
         */

    void   (*SetAsWakeupTimer)    (LTThread hThread, LTThread_TimerProc *pTimerProc, void *pClientData);
        /**< empowers a timer to wake the system up from low power sleep, if necessary
         *
         * %SetAsWakeupTimer modifies any pre-existing timer to be able to wake the system up from
         * low power sleep mode, if necessary, to execute pTimerProc on schedule.  Normally timers
         * do not fire when the system is in low power sleep mode.   When the system wakes up from low power
         * sleep mode, any timers that elapsed while the system was in low power sleep mode, will fire immediately,
         * but only once, and reschedule.  For absolutely critical circumstances where it is important to have the
         * timer fire on schedule, even if the system is in low power sleep mode, call %SetAsWakeupTimer to grant
         * your timer the power to wake the system up from low power sleep mode to execute pTimerProc on schedule.
         * WARNING: use of this function will adversely affect battery life.
         * WARNING: you have been warned!
         *
         *  @param hThread the thread that has the desired timer
         *  @param pTimerProc the TimerProc of the desired timer
         *  @param pClientData the client data of the desired timer
         *
         *  @note There is no way to change a timer back to not being a wakeup timer once SetAsWakeupTimer is set.
         *        Instead, kill the timer and set a new one.
         */
/*  _____________________________________________________________
 *  _____________________________________________________________
 *  Functions that only operate on the currently executing thread
 */
    void (*Sleep)(LTTime sleepTime);
        /**< Puts the currently executing thread to sleep for sleepTime
         * @param sleepTime the time duration to sleep for
         */
    void (*Yield)(void);
        /**< causes the currently executing thread to relinquish the remainder of its time slice
         * @note the thread will only yield if there is at least one other thread ready to run at the same priority level
         */

/*  __________________________________________________
 *  __________________________________________________
 *  Functions that don't depend on any specific thread
 */ LTThread (*GetCurrentThread)(void);
    /**< returns the currently executing thread.
      *  @return the currently executing LTThread or 0 if called from a non-LT thread on a host operating system, e.g. Linux, Windows, MacOS, iOS, Android, etc.
      */
    void (*SnapshotRunningThreads)(LTThread_SnapshotRunningThreadsCB * pSnapshotRunningThreadsCB, void *pClientData);
    /**< snapshots the running threads
      *  SnapshotRunningThreads() takes the snapshot of all running threads and delivers them to pSnapshotRunningThreadsCB
      *  synchronously before returning.  The snapshots are delivered to the callback in an arbitrary, non-deterministic (to the client) order.
      *  @param pSnapshotRunningThreadsCB the callback to receive the snapshots
      *  @param pClientData client data that will be passed to the callback
      *  @note the list of snapshots delivered to the callback is only valid for the duration of the callback invocation.  Do not cache
      *        any pointers in your LTThread_SnapshotRunningThreadsCB.  Instead copy any snapshot data you wish to preserve beyond the lifetime
      *        of the callback function invocation
      *  @see LTThread_Snapshot
      */
    u32    (*GetDefaultStackSize) (void);
    /**< gets the default stack size a thread will receive when started
      *  @return the default stack size in bytes
      */
    const char * (*ThreadStateToString)(LTThread_ThreadState threadState);
    /**< returns the string name of LTThread_ThreadState enum values
      *  ThreadStateToString() is provided for the purpose of logging and printing
      *  LTThread_ThreadState values as human readable strings.  These strings are
      *  substrings of the labels and are not localizable; this function is provided
      *  for debugging support.  The names returned are the unique part of the enum
      *  label, for example ThreadStateToString(kLTThread_ThreadState_Ready) returns
      *  "Ready", ThreadStateToString(kLTThread_ThreadState_TerminatePending) returns
      *  "TerminatePending", etc.
      *  @param threadState - the thread state to get the string name of
      *  @return the string name of the thread state enum value
      */
};

typedef_LTObject(LTOThread, 1) {

    void    (*Start)                        (LTOThread *thread, const char *pName, LTThread_InitProc *pInitProc, LTThread_ExitProc *pExitProc);
    bool    (*StartSynchronous)             (LTOThread *thread, const char *pName, LTThread_InitProc *pInitProc, LTThread_ExitProc *pExitProc);
    bool    (*QueueTaskProc)                (LTOThread *thread, LTThread_TaskProc *pTaskProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) LT_ISR_SAFE;
    bool    (*QueueTaskProcIfRequired)      (LTOThread *thread, LTThread_TaskProc *pTaskProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData) LT_ISR_SAFE;
    void    (*GetName)                      (LTOThread *thread, char nameBuffToFill[kLTThread_MaxNameBuff]);
    void *  (*GetThreadSpecificClientData)  (LTOThread *thread, const char *pKey);
    u32     (*GetStackSize)                 (LTOThread *thread);
    void    (*GetStackUsage)                (LTOThread *thread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet);
    void    (*GetHeapUsage)                 (LTOThread *thread, u32 * pCurrentHeapUsageToSet, u32 * pMaxHeapUsageToSet);
    u8      (*GetPriority)                  (LTOThread *thread);
    void    (*GetSnapshot)                  (LTOThread *thread, LTThread_Snapshot * pSnapshotToFill);
    bool    (*IsSystemThread)               (LTOThread *thread);
    bool    (*IsSystemThreadTimeCritical)   (LTOThread *thread);
    bool    (*IsCurrentThread)              (LTOThread *thread);
    void    (*SetThreadSpecificClientData)  (LTOThread *thread, const char *pKey, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
    void    (*SetStackSize)                 (LTOThread *thread, u32 nStackSize);
    void    (*SetPriority)                  (LTOThread *thread, u8 nPriority);
    void    (*Terminate)                    (LTOThread *thread);
    bool    (*WaitUntilFinished)            (LTOThread *thread, LTTime timeout);
    bool    (*IsTerminatePending)           (LTOThread *thread);
    void    (*SetTimer)                     (LTOThread *thread, LTTime timeout, LTThread_TimerProc *pTimerProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
    void    (*SetTimerAbsolute)             (LTOThread *thread, LTTime kernelStartTime, LTTime deadlinePeriod, LTThread_TimerProc *pTimerProc, LTThread_ClientDataReleaseProc *pClientDataReleaseProc, void *pClientData);
    LTTime  (*GetTimerExpirationKernelTime) (LTOThread *thread, LTThread_TimerProc *pTimerProc, void *pClientData);
    void    (*KillTimer)                    (LTOThread *thread, LTThread_TimerProc *pTimerProc, void *pClientData);
    void    (*RestartTimer)                 (LTOThread *thread, LTThread_TimerProc *pTimerProc, void *pClientData);
    void    (*SetAsWakeupTimer)             (LTOThread *thread, LTThread_TimerProc *pTimerProc, void *pClientData);
    void    (*Sleep)                        (LTTime sleepTime);
    void    (*Yield)                        (void);

    void    (*SnapshotRunningThreads)       (LTThread_SnapshotRunningThreadsCB * pSnapshotRunningThreadsCB, void *pClientData);
    u32     (*GetDefaultStackSize)          (void);

    LTThread                                (*GetThreadHandle)(LTOThread *thread);
    LTThread_ThreadState                    (*GetThreadState)(LTOThread *thread);
    LTOThread  *                            (*GetCurrentThread)(void);
        /**< The return value from GetCurrentThread should not be cached.  It is only valid
             while the thread is running */
    const char *                            (*ThreadStateToString)(LTThread_ThreadState threadState);
} LTOBJECT_API;

/**********************
 * LTThread typedefs */

typedef struct LTThread_Snapshot { /**< A snapshot of an active thread - suitable for a ps command */
    LTThread hThread;                         /**< Thread handle                  - 4 or 8, total  4 or  8, alignment 4 or 8                              */
    LT_SIZE  reserved1;                       /**< reserved                       - 4 or 8, total  8 or 16, alignment 8, check!                           */
    LTTime   runTime;                         /**< CPU run time in thread         -      8, total 16 or 24, alignment 8                                   */
    u32      nThreadNumber;                   /**< ThreadNumber for printing only -      4, total 20 or 28, alignment 4                                   */
    u32      nStackSize;                      /**< stack size                     -      4, total 24 or 32, alignment 8                                   */
    u32      nStackCurrent;                   /**< stack current use              -      4, total 28 or 36, alignment 4                                   */
    u32      nStackHiWatermark;               /**< highest stack use              -      4, total 32 or 40, alignment 8                                   */
    u32      nHeapCurrent;                    /**< heap current use               -      4, total 36 or 44, alignment 4                                   */
    u32      nHeapHiWatermark;                /**< highest heap use               -      4, total 40 or 48, alignment 8                                   */
    u16      nStructureSize;                  /**< size of this structure         -      2, total 42 or 50, alignment 2                                   */
    u8       nThreadState;                    /**< thread state                   -      1, total 43 or 51, alignment 1                                   */
    u8       nPriority;                       /**< thread priority                -      1, total 44 or 52, alignment 4                                   */
    char     name[kLTThread_MaxNameBuff];     /**< thread name                    -     20, total 64 or 72, alignment 8 <- Final alignment 8, check!      */
} LTThread_Snapshot;

/** @} */

LT_STATIC_ASSERT_SIZE_32_64(LTThread_Snapshot, 64, 72)

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTTHREAD_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Jan-19   augustus    created
 *  25-May-19   augustus    refined priorities - 1 is lowest and non-realtime
 *                          added PercentToPriority() and GetHighestPriority()
 *  27-May-19   augustus    ruminating: should threads just have built in queues
 *                          and a stop message?  Also: should threads have a set context?
 *                          i.e. SetContext(const char *pName, void *pValue);
 *                          or SetContext(void *pContext), or GetThreadID() ?
 *  22-May-19   constantine Doxygenation
 *  09-Aug-19   augustus    redid class for new ThreadRunner thread model
 *  19-Aug-19   augustus    added ResetTimer() functions; removed delay from OnTimer
 *  25-Aug-19   constantine Move Doxymentation to lt/doc/lt/core/LTTypes.dox
 *  06-Sep-19   augustus    axed PostMessageFromInterruptHandler(); now PostMessage() handles both cases
 *  06-Sep-19   augustus    axed PostMessageFromInterruptHandler(); now PostMessage() handles both cases
 *  09-Jun-20   augustus    re-created in C
 *  05-Aug-20   augustus    added IsTerminatePending
 *  06-Sep-20   augustus    redid interface to queue TaskProc/ClientDataReleaseProc pairs; no more thread messaging
 *                          added LTThread_ThreadState and LTThread_Snapshot and associated functions
 *  11-Oct-20   augustus    Start() now takes InitProc and ExitProc
 *                          SetRealtimePriority and SetNonRealtime now return previous priority
 *  23-Dec-20   augustus    added IsSystemThread and IsCurrentThread
 *  26-Dec-20   augustus    added IsSystemThreadTimeCritical
 *  05-Feb-21   augustus    added QueueTaskProcIfRequired
 *  21-Feb-21   augustus    added GetStackUsage
 *  18-Jun-21   augustus    added back IsTerminatePending()
 *  09-Sep-21   tiberius    simplify thread priority API
 *  20-Oct-21   augustus    redid Timers to be based on procedure and clientdata, not timer id, allow setting on arbitrary threads
 *  19-Aug-22   augustus    got rid of hThread parameter to TaskProc, TimerProc, InitProc, and ExitProc,
 *                          InitProc returns boolean indicating whether to continue or terminate the thread upon return
 *                          added LTThread_ClientDataReleaseProc for explicit lifecycle management of clientdata pointer value ownership
 *                          enhanced timers to support one-shot, periodic-interval, and periodic-absolute types
 *                          replace Set/GetContext with Set/GetThreadSpecificClientData which is equivalent to thread local storage
 *                          got rid of m_ from structure members
 *  30-Apr-24   augustus    added GetTimerExpirationKernelTime
 *  05-May-24   augustus    added GetThreadHandle to LTOThread
 *  26-Mar-25   augustus    added StartSynchronous
*/
