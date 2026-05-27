/******************************************************************************
 * lt/source/lt/core/LTKernel.h                                 LTK Microkernel
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_CORE_LTKERNEL_H
#define ROKU_LT_SOURCE_LT_CORE_LTKERNEL_H

#include <lt/core/LTCore.h>
#include <lt/core/bsp/LTCoreBSP.h>

/*************
 * Constants */
enum {
    kLTKNanosecondsPerTick = 1000000,
    kLTKTicksPerSecond     = 1000
};

/***************
 * Linked List */
typedef struct LTKList_Node {
    struct LTKList_Node * pPrev;
    struct LTKList_Node * pNext;
} LTKList_Node;

typedef LTKList_Node LTKList;

/****************
 * Thread State */
typedef enum {
    kLTKThreadState_Uninitialized = 0,
    kLTKThreadState_Initialized,
    kLTKThreadState_Stopped,
    kLTKThreadState_Runnable,
    kLTKThreadState_WaitForMutex,
    kLTKThreadState_WaitForMonitor,
    kLTKThreadState_WaitForStop,
    kLTKThreadState_WaitForTick = 16,
    kLTKThreadState_WaitForMonitorOrTick,
    kLTKThreadState_WaitForStopOrTick,
} LTKThreadState;

/******************
 * LTKThread Data */
struct LTKMonitor;
typedef struct {
    u8           * pStack;            // Current stack pointer
    LTKThreadState nState;            // Current thread state
    s64            nCycleCount;       // Accumulated thread cycle count
    LTKList_Node   runLink;           // Pend and Run list link
    LTKList_Node   timerLink;         // Timer list link
    LTKList        stopList;          // List of threads waiting on stop
    u32            nWakeTick;         // Earliest tick for thread wake up
    s8             nPriority;         // Current (possibly inherited) priority
    s8             nNominalPriority;  // Original priority
    u8             nTimedOut;         // Did operation time out?
    u8             nPad;              //   (For 32-bit alignment)
    s8             nSecureContext;    // Current secure context number
    s8             nSecureContextNew; // Secure context number to switch to
    u16            nPad2;             //   (For 32-bit alignment)
    s32            nReturnValue;      // Return value of stopped thread
    u32            nStackSize;        // Size of stack in bytes
    u8           * pStackBottom;      // Lowest memory location of stack
    struct LTKMonitor   * pMonitor;   // Monitor being waited on
    void         * pInstanceData;     // Private instance data
    u32            nPad3;             //   (places pName at end)
    const char   * pName;             // Thread name (for exceptions) -- place at end
} LTKThread;

LT_STATIC_ASSERT_SIZE_32_64(LTKThread, 80, 136);

/**************
 * Mutex Data */
typedef struct {
    LTKThread  * pOwner;              // Owner thread pointer
    s32          nDepth;              // Recursion depth
    LTKList      pendList;            // List of threads blocked on this mutex
} LTKMutex;

/****************
 * Monitor Data */
typedef struct LTKMonitor {
    u32           nValue;             // Flag used for signaling
    LTKMutex      mutex;              // Mutex that monitor blocks on
    LTKThread   * pWaiter;            // Pointer to the thread blocked on monitor
} LTKMonitor;

/* Fault handler callback (from fatal exception) */
typedef void (LTKFaultCallback)(const char * pABI, u32 * pInterruptStack, LT_SIZE nInterruptStackSize) LT_ISR_SAFE;
    /**< Function called upon fatal fault exception.
     *
     * @param[in] pABI Pointer to ABI version name.
     * @param[in] pInterruptStack Pointer to interrupt stack (NULL if fault occurred in thread).
     * @param[in] nInterruptStackSize Size of register context in bytes (ignored if fault occurred in thread).
     */

/**********************
 * LTK Initialization */
void LTKInitialize(const LTCoreBSP * pBSP, LTCoreBSP_LTCoreLogFunction * pLTCoreLogFunction, LTKFaultCallback * pFaultCallback);
    /**< Initialize LT Kernel
     *
     * @param[in] pBSP LTCoreBSP pointer
     * @param[in] pLTCoreLogFunction LTCoreBSP_LTCoreLogFunction
     * @param[in] pFaultCallback Callback into LTCore called during fault (exception).
     * @note  LTKInitialize() must be invoked before creating threads and
     *        starting the scheduler. LTKInitialize() will start the system
     *        tick if it has NOT already been started by the BSP. NOTE: The
     *        Mutex API can safely be invoked prior to calling this function.
     * @note  Implementations of LTKInitialize can call LTKLOG_STOMP as follows:<pre>
     *        #include <lt/core/LTCore.h>
     *        DEFINE_LTLOG_SECTION("ltk");
     *        void LTKInitialize(const LTCoreBSP * pBSP) {
     *            LTLOG_STOMP("ltk.init", "This is a logstomp message");
     *        } </pre>
     */

/********
 * Time */
s64 LTKGetHighFrequencyCounterNanoseconds(void) LT_ISR_SAFE;
    /**< Obtain monotonic LTK time based on system clocks.
     *
     * @return monotonic time in nanoseconds
     */
s64 LTKGetHighFrequencyCounterNanosecondResolution(void) LT_ISR_SAFE;
    /**< Obtain timer reference resolution in nanoseconds.
     *
     * @return timer reference resolution in nanoseconds.
     */
void LTKAdvanceTickCount(u32 nTicks);
    /**< Advance tick count in case interrupts have been disabled for an extended period of time.
     * TODO: Change parameter to nanoseconds?
     * @param[in] nTicks number of ticks to advance
     */

LT_INLINE void LTKAdvanceNanoseconds(s64 nNanoseconds) {
    if (nNanoseconds > 0) LTKAdvanceTickCount(nNanoseconds / kLTKNanosecondsPerTick);
}

/***************
 * Idle Hooks */
typedef void (LTKIdleHookProc)(void);
void LTKSetIdleHooks(LTKIdleHookProc * pEnterIdleHook, LTKIdleHookProc * pIdleTickHook, LTKIdleHookProc * pExitIdleHook);
    /**< Sets the idle hook procedures
     *
     * % Call LTKSetIdleHooks once only from LTCoreImpl.c prior to calling LTKInitialize.  The Idle hooks inform
     * LTCore of idle state transitions and provide a perodic callback so LTCore can implement a zero overhead timer
     * to trigger events from.  The Enter and Exit idle hook procs should be used to set and clear the trigger time
     * and the IdleTickHook can be used to compare kernel time with the previously set trigger time.  The idle hook can
     * also SetThreadPrioirty and QueueTaskProc on a thread to schedule a thread activity such as entering standby mode
     * after a period of continuous idle time has elapsed.  Only the idle hook may perform operations that invoke
     * the scheduler.  All three hooks are run in the ISR context of the scheduler.  Be brief and act accordingly.
     *
     * @param pEnterIdleHook is called from the scheduler in ISR context whenever the idle thread is not the currently running thread
     *                       and the scheduler makes the idle thread the currently running thread because now all other threads
     *                       are on wait queues.  Called from ISR context before the idle thread gains control.  Do not do anything
     *                       that would cause or require the scheduler to be invoked.
     *
     * @param pIdleTickHook  called periodically in the ISR context of the scheduler.  It is provided so a non-invasive ~zero overhead
     *                       idle timer can be implemented.  It is called whenever a timer interrupt occurs while the system is idle,
     *                       roughly every millisecond.
     *
     * @param pExitIdleHook  is called from the scheduler in ISR context whenever the idle thread is the currently running thread
     *                       and the scheduler is about to make a different (non-idle) thread the currently running thread.
     *                       Called from ISR context before the newly scheduled thread gains control.Do not do anything
     *                       that would cause or require the scheduler to be invoked.
     *
     * @note This function *must* be called *before* LTKInitialize is called. IdleHookProcs won't be called until after
     *       both LTKInitialize() and LTKThreadInitializeAndStartScheduler() are called and after LTKThreadInitializeAndStartScheduler()
     *       returns.  The enterIdleHook will always be called first, followed by [possibly zero but usually many] invocations of the idleTickHook,
     *       followed by the exitIdleHook, always in the complete order of: <enterIdleHook> [idleTickHooks] <exitIdleHook>
     *
     */

/**************
 * Interrupts */
LT_SIZE LTKDisableInterrupts(void) LT_ISR_SAFE;
    /**< Disable all interrupts. Nesting is supported.
     *
     * @return original interrupts state mask
     */
void LTKEnableInterrupts(LT_SIZE nMask) LT_ISR_SAFE;
    /**< Restore interrupts state mask. Possibly enable interrupts.
     * Nesting is supported
     *
     * @param[in] nMask interrupts state mask to restore
     */
bool LTKInsideInterruptContext(void) LT_ISR_SAFE;
    /**< Checks if system is running in interrupt context
     *
     * @return true in interrupt context, false in thread context
     */
bool LTKInterruptsAreDisabled(void) LT_ISR_SAFE;
    /**< Checks if system is running with all interrupts disabled
     *
     * @return true if all interrupts are generally disabled
     */
void LTKSetInterruptVector(u32 nInterrupt, LTCore_InterruptHandler * pInterruptHandler,
                               LTCore_InterruptPriority priority);
    /**< Set interrupt vector and priority
     *
     * @param[in] nInterrupt interrupt line number
     * @param[in] pInterruptHandler interrupt handler function pointer
     * @param[in] priority interrupt priority
     */
void LTKSetInterruptPriority(u32 nInterrupt, LTCore_InterruptPriority priority);
    /**< Change interrupt priority
     *
     * @param[in] nInterrupt interrupt line number
     * @param[in] priority interrupt priority
     */
void LTKDebugBreak(void);
    /**< Call HW debugger
     */

/***********
 * Threads */
LT_SIZE LTKThreadInstanceSize(void);
    /**< Get size of HW-specific thread data structure in bytes
     *
     * @return size of thread data structure in bytes
     */
u32 LTKThreadGetDefaultStackSize(void);
    /**< returns the recommended default thread stack size
     *
     * Typically this function returns a very small value, currently
     * 512 bytes on most platforms.  Some architectures, notably Xtensa,
     * add penalty bytes to the stack at each function call (32 bytes overhead
     * per function call on Xtensa when the register sliding window is enabled).
     * LTK returns a larger default stack size on such platforms.  The current
     * heuristic is to add the overhead penalty
     * of 8 function calls to the base recommended size, e.g. for Xtensa
     * we add 8 function calls * 32 bytes = 256 bytes + 512 (the base),
     * for a total of 768 bytes.
     *
     * @return the recommended default thread stack size
     */
void
LTKThreadInitializeAndStartScheduler(void * pThread, u8 nPriority, u32 nStackSize,
                                        const char * pName, void (*pEntry)(void *),
                                        void * pClientData);
    /**< Create first thread and initiates multi-threaded operation. The Mutex API can
     * safely be invoked and threads can be created prior to calling this function.
     * However, thread execution will be suspended until this function is invoked
     *
     * @param[in] pThread first thread structure pointer
     * @param[in] nPriority first thread priority
     * @param[in] nStackSize first thread stack size
     * @param[in] pName pointer to NULL-terminated string containing first thread name
     * @param[in] pEntry thread function pointer
     * @param[in] pClientData thread context passed to the thread function
     */
bool LTKThreadInitializeAndRun(void * pThread, u8 nPriority, u32 nStackSize,
                                  const char * pName, void (*pEntry)(void *),
                                  void * pClientData);
    /**< Initialize and start thread. Thread executes immediately if scheduler
     * is running and the thread priority is higher than priority of current
     * context.
     *
     * @param[in] pThread thread structure pointer
     * @param[in] nPriority thread priority
     * @param[in] nStackSize thread stack size
     * @param[in] pName pointer to NULL-terminated string containing thread name
     * @param[in] pEntry thread function pointer
     * @param[in] pClientData thread context passed to the thread function
     * @return true if success
     */
void LTKThreadFinalize(void * pThread);
    /**< Clean up thread structure after thread is stopped. This can only be invoked after a
     * call to LTKThreadWaitUntilFinished() returns true for the given thread
     *
     * @param[in] pThread thread structure pointer
     */
LTKThread * LTKGetRunningThreadPtr(void);
    /**< Obtain pointer to currently running thread data structure.
     *
     * @return pointer to currently running thread data structure. NULL if
     * scheduler isn't running.
     */
void * LTKThreadGetCurrentThreadInstanceData(void);
    /**< Obtain private thread data from currently running thread.
     *
     * @return private thread data from currently running thread. NULL if
     * scheduler isn't running.
     */
bool LTKThreadWaitUntilFinished(void * pThread, s64 nNanosecondsToWait);
    /**< Block until thread stopped with given timeout. Set nNanosecondsToWait to
     * is running and the thread priority is higher than priority of current
     * context.
     *
     * @param[in] pThread thread structure pointer
     * @param[in] nNanosecondsToWait timeout in nanoseconds. If LTTime_Infinite - no timeout,
     *  wait until completion
     * @return true if stopped, false for timeout
     */
void LTKThreadSetPriority(void * pThread, u8 nPriority);
    /**< Change a thread's priority.
     *
     * @param[in] pThread thread structure pointer
     * @param[in] nPriority desired thread priority
     */
u8 LTKThreadGetPriority(void * pThread);
    /**< Gets a thread's set (nominal) priority.
     *
     * @param[in] pThread thread structure pointer
     * @return the set priority of the thread
     */
void LTKThreadSleep(s64 nNanosecondsToWait);
    /**< Suspend thread for a period of time (at tick resolution).
     * If nNanosecondsToWait is less than the tick resolution (e.g.: zero) another equal priority thread will
     * be scheduled to run in lieu of the current thread. If the current thread is the only thread at the
     * current priority it will be immediately rescheduled and execution will continue after this function call.
     *
     * @param[in] nNanosecondsToWait desired suspend time in nanoseconds
     */
void LTKThreadGetStackUsage(void * pThread, u32 * pStackSizeToSet, u32 * pCurrentStackUsageToSet, u32 * pMaxStackUsageToSet);
    /**< Returns total stack size, current stack usage and maximum stack usage in bytes for the given thread.
     *
     * @param[in] pThread thread structure pointer
     * @param[out] pStackSizeToSet pointer to unsigned 32 bits of memory to store total stack size in bytes
     * @param[out] pCurrentStackUsageToSet pointer to unsigned 32 bits of memory to store current stack size in bytes
     * @param[out] pMaxStackUsageToSet pointer to unsigned 32 bits of memory to store maximum stack usage in bytes
     */
s64 LTKThreadGetCpuTime(void * pThread);
    /**< Get the total amount of CPU time spent in a thread since it was started in nanoseconds.
     *
     * @param[in] pThread thread structure pointer
     * @return the total amount of CPU time spent executing in the thread in nanoseconds.
     */

/********************************************
 * Nestable Mutex with Priority Inheritance */
void LTKMutexInitialize(void * pMutex);
    /**< Initialize a new mutex
     *
     * @param[in] pMutex mutex structure pointer
     */
void LTKMutexFinalize(void * pMutex);
    /**< Uninitialize mutex
     *
     * @param[in] pMutex mutex structure pointer
     */
void LTKMutexLock(void * pMutex);
    /**< Lock a mutex
     *
     * @param[in] pMutex mutex structure pointer
     */
void LTKMutexUnlock(void * pMutex);
    /**< Unlock a mutex
     *
     * @param[in] pMutex mutex structure pointer
     */
bool LTKMutexTryLock(void * pMutex);
    /**< Non-blocking attempt to lock mutex
     *
     * @param[in] pMutex mutex structure pointer
     * @return true if mutex locked, false if not
     */

/************
 * Monitors */
LT_SIZE LTKMonitorInstanceSize(void);
    /**< Returns size of monitor data structure in bytes. for the currently running platform
     *
     * @return size of monitor data structure in bytes
     */
void LTKMonitorInitialize(void * pMonitor);
    /**< Initialize a new monitor
     *
     * @param[in] pMonitor monitor structure pointer
     */
void LTKMonitorFinalize(void * pMonitor);
    /**< Uninitialize monitor
     *
     * @param[in] pMonitor monitor structure pointer
     */
void LTKMonitorEnter(void * pMonitor);
    /**< Enter monitor (Lock)
     *
     * @param[in] pMonitor monitor structure pointer
     */
void LTKMonitorExit(void * pMonitor);
    /**< Exit monitor (Unlock)
     *
     * @param[in] pMonitor monitor structure pointer
     */
void LTKMonitorNotify(void * pMonitor) LT_ISR_SAFE;
    /**< Notify monitor
     *
     * @param[in] pMonitor monitor structure pointer
     */
bool LTKMonitorWait(void * pMonitor, s64 nTimeoutNanoseconds);
    /**< Unlock, block until notification, then re-lock.
     *
     * @param[in] pMonitor monitor structure pointer
     * @param[in] nTimeoutNanoseconds timeout in nanoseconds. If LTTime_Infinite - blocks without a timeout
     * @return true if notified, false for timeout.
     */

/********************
 * Memory Allocator */

#define LTK_MAX_HEAP_REGIONS 4
    /**< Maximum number of heap regions tracked in the slot table.  Returned by
     *   LTKHeapAddRegionEx() as a sentinel when the table is full. */

void LTKHeapInitialize(void);
    /**< Initialize allocator heap.
     *
     * @param[in] pHeapChangeCallback pointer to the heap change callback
     */
void LTKHeapAddRegion(u8 * pRegionBuffer, u32 nSizeInBytes);
    /**< Add a non-exclusive region to the heap.  Shortcut for
     *   LTKHeapAddRegionEx(buffer, size, false). */

u32 LTKHeapAddRegionEx(u8 * pRegionBuffer, u32 nSizeInBytes, bool bExclusive);
    /**< Add a heap region with optional exclusivity.
     *
     * @param pRegionBuffer  start of the region buffer (must live for program lifetime)
     * @param nSizeInBytes   buffer size
     * @param bExclusive     if true, the region is skipped by default lt_malloc() /
     *                       LTKAlloc() walks; reachable only via lt_malloc_from_region() /
     *                       LTKAllocFromRegion().  Use for DMA-only RAM.
     * @return Internal 0-based slot (registration order) on success, or
     *         LTK_MAX_HEAP_REGIONS if the internal region table is full; in the
     *         latter case the block is still added to the unified free list for
     *         non-exclusive regions, so existing platforms that do not care about
     *         region indices see no behavioural change.
     *
     *         IMPORTANT: this return value is the *internal slot*, NOT an
     *         LTMemoryRegion.  LTMemoryRegion is 1-based and reserves 0 as the
     *         "no specific region" sentinel that routes lt_malloc_from_region
     *         back to lt_malloc.  Convert to a public LTMemoryRegion via
     *         (LTMemoryRegion)(slot + 1) before passing to lt_malloc_from_region
     *         or exposing in LTDeviceConfig.json. */

bool LTKHeap_IsExclusiveByPtr(const void * p);
    /**< Returns true if @p p points inside a heap region that was registered with
     *   bExclusive=true.  Returns false for non-exclusive regions, for unmapped
     *   pointers, and for the NULL pointer.  Used by callers (e.g. LTCoreImpl_ReAlloc)
     *   to distinguish a deliberate refusal-to-move from an out-of-memory failure. */

void * LTKAlloc(LT_SIZE nBytes);
void * LTKAllocFromRegion(LTMemoryRegion region, LT_SIZE nBytes);
    /**< Allocate from a specific region previously registered via LTKHeapAddRegion(Ex).
     *   The @p region argument is an LTMemoryRegion (typically obtained via
     *   LT_GetCore()->GetNamedMemoryRegion(name)).  A value of 0 is a sentinel that
     *   falls through to the unrestricted allocator (equivalent to LTKAlloc).  Non-zero
     *   values are 1-based indices into the BSP region table; this function translates
     *   region N to internal slot N - 1.  Returns NULL on out-of-range region,
     *   exhaustion, or zero size.  Memory is freed via the normal LTKFree() / lt_free()
     *   path. */
void * LTKReAlloc(void * pMem, LT_SIZE nBytes);
    /**< ReAllocate memory chunk.
     *
     * @param[in] pMem pointer to the existing memory chunk to be re-allocated
     * @param[in] nBytes desired chunk new size in bytes
     * @return pointer to successfully re-allocated memory chunk, NULL on allocation fail
     */
void LTKFree(void * pMem);
    /**< Free memory chunk.
     *
     * @param[in] pMem pointer to the existing memory chunk to be freed
     */
LT_SIZE LTKGetActualAllocationSize(void * pMem);
    /**< Get the number of bytes in a memory chunk that is available for user data.
     *
     * @param[in] pMem pointer to allocated memory chunk to obtain size for.
     * @return actual size of block available for use in bytes, 0 if block is invalid.
     */
LT_SIZE LTKGetTotalSystemRAM(void);
    /**< Returns total size of heap in bytes
     *
     * @return total size of heap in bytes
     */
LT_SIZE LTKGetAvailableSystemRAM(void);
    /**< Returns number of bytes currently free in heap
     *
     * @return number of bytes currently free in heap
     */
LT_SIZE LTKGetSystemRAMLowWatermark(void);
    /**< Returns least ever number of free bytes in the heap
     *
     * @return least ever number of free bytes in the heap
     */
LT_SIZE LTKGetLTKCurrentAllocationCount(void);
    /**< returns the number of bytes currently allocated by LTK
     *
     * @return the number of bytes currently allocated by LTK
     * @note nominally this would be  LTKGetTotalSystemRAM() - LTKGetAvailableSystemRAM()
     *       but on hosted architectures those functions return values from the hosted system
     *       not the bytes allocated by LTK.  This function returns the bytes allocated by LTK
     *       deterministically on all architectures.
     */
LT_SIZE LTKGetLTKAllocationHighWatermark(void);
    /**< returns the high watermark number of bytes ever allocated by LTK
     *
     * @return the high watermark number of bytes ever allocated by LTK
     * @note nominally this would be  LTKGetTotalSystemRAM() - LTKGetSystemRAMLowWatermark()
     *       but on hosted architectures those functions return values from the hosted system
     *       not the bytes allocated by LTK.  This function returns the high watermark bytes
     *       allocated by LTK deterministically on all architectures.
     */
LT_SIZE LTKGetLargestAvailableBlockInRAM(void);
    /**< Returns largest available block size currently in the heap
     *
     * @return largest available block size currently in the heap
     */

/******************
 * Circular Lists */
void LTKList_Init(LTKList * pList) LT_ISR_SAFE;
    /**< Initialize an empty circular list
     *
     * @param[in] pList circular list head pointer
     */
void LTKList_InsertHead(LTKList * pList, LTKList_Node * pNodeToAdd) LT_ISR_SAFE;
    /**< Link new element to front of list
     *
     * @param[in] pList circular list head pointer
     * @param[in] pNodeToAdd pointer to the list node to add
     */
void LTKList_AddTail(LTKList * pList, LTKList_Node * pNodeToAdd) LT_ISR_SAFE;
    /**< Link new element to end of list
     *
     * @param[in] pList circular list head pointer
     * @param[in] pNodeToAdd pointer to the list node to add
     */
LT_INLINE void
LTKList_InsertBefore(LTKList_Node * pNodeExisting, LTKList_Node * pNodeToAdd) LT_ISR_SAFE {
    /**< Link new element before existing element
     *
     * @param[in] pNodeExisting pointer into the existing list node
     * @param[in] pNodeToAdd pointer to the list node to add
     */
    /* Link new element before existing element */
    LTKList_AddTail(pNodeExisting, pNodeToAdd);
}
LT_INLINE void
LTKList_AddAfter(LTKList_Node * pNodeExisting, LTKList_Node * pNodeToAdd) LT_ISR_SAFE {
    /**< Link new element after existing element
     *
     * @param[in] pNodeExisting pointer into the existing list node
     * @param[in] pNodeToAdd pointer to the list node to add
     */
    LTKList_InsertHead(pNodeExisting, pNodeToAdd);
}
void LTKList_Remove(LTKList_Node * pNodeToRemove) LT_ISR_SAFE;
    /**< Remove an element from a list
     *
     * @param[in] pNodeToRemove pointer to the existing list node, within the target list
     */
void LTKList_MoveToEnd(LTKList * pList, LTKList_Node * pNodeToMove) LT_ISR_SAFE;
    /**< Move an element to the end of a list
     *
     * @param[in] pList pointer to the target list
     * @param[in] pNodeToMove pointer to the existing list node, within the target list
     */
LT_INLINE bool
LTKList_IsTail(LTKList * pList, LTKList_Node * pNodeToCheck) LT_ISR_SAFE {
    /**< Check if element is at the end of list
     *
     * @param[in] pList pointer to the target list
     * @param[in] pNodeToCheck pointer to the existing list node, within the target list
     * @return true if element is at end of list
     */
    return (pList->pPrev == pNodeToCheck);
}
LT_INLINE bool LTKList_IsEmpty(LTKList * pListToCheck) LT_ISR_SAFE {
    /**< Check if list is empty
     *
     * @param[in] pListToCheck pointer to the target list
     * @return true if list is empty
     */
    return (pListToCheck->pPrev == pListToCheck);
}
LT_INLINE bool LTKList_IsNodeLinked(LTKList_Node * pNodeToCheck) LT_ISR_SAFE {
    /**< Check if element is on a list
     *
     * @param[in] pNodeToCheck pointer to the list node to check
     * @return true if element is on a list */
    return (pNodeToCheck->pPrev != pNodeToCheck);
}

#endif // #ifndef ROKU_LT_SOURCE_LT_CORE_LTKERNEL_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  14-Apr-21   tiberius    created
 *  28-Feb-22   constantine BSP API change for interrupt-driven serial-console TX
 *  13-Sep-22   titus       documentation added
 *  18-Dec-22   augustus    added LTKThreadGetDefaultStackSize()
 *  16-Jul-24   augustus    added LTKAdvanceNanoseconds()
 */
