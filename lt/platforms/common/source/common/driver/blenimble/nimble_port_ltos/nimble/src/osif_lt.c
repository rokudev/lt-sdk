/********************************************************************************
 * platforms/amebad/source/amebad/driver/bt/nimble_port_ltos/nimble/src/osif_lt.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ********************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTCountingSemaphore.h>

#include <lt/utility/byteops/LTUtilityByteOps.h>
#include "nimble/nimble_npl_os.h"

#include <osif.h>
//DEFINE_LTLOG_SECTION("bt.osif");

/****************************************************************************/
/* Static variables                                                         */
/****************************************************************************/
static LTMutex   *s_MtxCritical = NULL;
static bool       firstLock  = true;
static ILTThread *s_iThread  = NULL;
/****************************************************************************/
/* Typedefs                                                                 */
/****************************************************************************/
enum eTimerState
{
    kTimerStopped,
    kTimerRunning,
    kTimerDeleted,
};

typedef void (*Osif_TaskProc_t)(void * pClientData);

typedef struct taskData_t_
{
    LTThread 		hThread;
    Osif_TaskProc_t pTaskProc;
    void 			* pClientData;
} taskData_t;

typedef struct arrayData_t_
{
    u32       maxQueueMessages;
    void    * p_handle_mutex;
    void    * p_handle_sem_data_available;
    void    * p_handle_sem_data_not_full;
    u32 msg_size;
} arrayData_t;


typedef struct timerData_t_
{
    LTThread hThread;
    LTThread_TimerProc * pTimerProc;
    void * pClientData;
    bool   reload;
    u32 interval_ms;
    u32 timerState;
} timerData_t;

static void wrapThreadFunc(void * pClientData)
{
    taskData_t * pTaskData = (taskData_t *)pClientData;
    pTaskData->pTaskProc(pTaskData->pClientData);
}

/****************************************************************************/
/* Delay current task in a given milliseconds                               */
/****************************************************************************/
void osif_delay(u32 ms)
{
    s_iThread->Sleep(LTTime_Milliseconds(ms));
}

/****************************************************************************/
/* Get system uptime in milliseconds                                        */
/****************************************************************************/
u32 osif_sys_time_get(void)
{
    return (u32)LTTime_GetMilliseconds(LT_GetCore()->GetKernelTime());
}




/****************************************************************************/
/* Create os level task routine                                             */
/****************************************************************************/
bool osif_task_create(void ** pp_handle, const char * p_name, void (* p_routine)(void *),
                      void * p_param, u16 stack_size, u16 priority)
{
    if (!pp_handle || !p_name || !stack_size || !p_routine) return false;

    priority = (priority > kLTThread_PriorityHighest) ? kLTThread_PriorityHighest : priority;

    LTThread hThread = LT_GetCore()->CreateThread(p_name);
    if (!hThread) return false;

    taskData_t * pTaskData = (taskData_t *)lt_malloc(sizeof(taskData_t));
    if (!pTaskData) {
        lt_destroyhandle(hThread);
        return false;
    }

    pTaskData->hThread     = hThread;
    pTaskData->pClientData = p_param;
    pTaskData->pTaskProc   = p_routine;
    *pp_handle             = (void *)pTaskData;

    s_iThread->SetPriority(pTaskData->hThread, priority);
    s_iThread->SetStackSize(pTaskData->hThread, stack_size + 384);
    s_iThread->Start(pTaskData->hThread, NULL, NULL);
    s_iThread->QueueTaskProc(pTaskData->hThread, wrapThreadFunc, NULL, (void*)pTaskData);

    return true;
}

/****************************************************************************/
/* Delete os level task routine                                             */
/****************************************************************************/
bool osif_task_delete(void * p_handle)
{
    if (p_handle == NULL)
    {
        return false;
    }

    taskData_t * pTaskData = (taskData_t *)p_handle;
    s_iThread->Terminate(pTaskData->hThread);
    s_iThread->WaitUntilFinished(pTaskData->hThread, LTTime_Infinite());
    s_iThread->Destroy(pTaskData->hThread);

    return true;
}

/****************************************************************************/
/* Yield current os level task routine                                      */
/****************************************************************************/
bool osif_task_yield(void)
{
    s_iThread->Yield();

    return true;
}

/****************************************************************************/
/* Get current os level task routine handle                                 */
/****************************************************************************/
bool osif_task_handle_get(void ** pp_handle)
{
    if (pp_handle == NULL || *pp_handle == NULL)
    {
        return false;
    }
    else
    {
        taskData_t * pTaskData = (taskData_t *)* pp_handle;
        if (lt_gethandleinterface(ILTThread, pTaskData->hThread) != NULL)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
}

/****************************************************************************/
/* Get os level task routine priority                                       */
/****************************************************************************/
bool osif_task_priority_get(void * p_handle, u16 * p_priority)
{
    if (p_handle == NULL || p_priority == NULL)
    {
        return false;
    }

    taskData_t *pTaskData = (taskData_t *)p_handle;
    *p_priority = (u16)s_iThread->GetPriority(pTaskData->hThread);

    return true;
}

/****************************************************************************/
/* Set os level task routine priority                                       */
/****************************************************************************/
bool osif_task_priority_set(void * p_handle, u16 priority)
{
    if (p_handle == NULL)
    {
        return false;
    }

    taskData_t * pTaskData = (taskData_t *)p_handle;
    s_iThread->SetPriority(pTaskData->hThread, (u8)priority);

    return true;
}

/****************************************************************************/
/* Lock critical section                                                    */
/****************************************************************************/
u32 osif_lock(void)
{
    if (firstLock)
    {
        s_MtxCritical = lt_createobject(LTMutex);
        firstLock      = false;
    }

    s_MtxCritical->API->Lock(s_MtxCritical);

    return 0ul;
}

/****************************************************************************/
/* Unlock critical section                                                  */
/****************************************************************************/
void osif_unlock(u32 flags)
{
    LT_UNUSED(flags);
    s_MtxCritical->API->Unlock(s_MtxCritical);
}

/****************************************************************************/
/* Free memory                                                              */
/****************************************************************************/
void osif_mem_free(void * p_block)
{
    if (p_block == NULL)
    {
        LT_GetCore()->ConsolePrint("%s - exit - NULL\n", __FUNCTION__);
        return;
    }
    lt_free(p_block);
}

/****************************************************************************/
/* Free aligned memory                                                      */
/****************************************************************************/
void osif_mem_aligned_free(void * p_block)
{
    lt_free(p_block);
}

/****************************************************************************/
/* Create mutex                                                             */
/****************************************************************************/
bool osif_mutex_create(void ** pp_handle)
{
    LTMutexMonitor *mutmon = lt_malloc(sizeof(LTMutexMonitor));
    mutmon->mutex = lt_createobject(LTMutex);
    if (!mutmon->mutex) {
        lt_free(mutmon);
        return false;
    }
    *pp_handle = (void *)mutmon;
    return true;
}

/****************************************************************************/
/* Delete mutex                                                             */
/****************************************************************************/
bool osif_mutex_delete(void * p_handle)
{
    if (!p_handle) return true;
    LTMutexMonitor *mutmon = p_handle;
    lt_destroyobject(mutmon->mutex);
    lt_free(mutmon);
    return true;
}

/****************************************************************************/
/* Take recursive mutex                                                     */
/****************************************************************************/
bool osif_mutex_take(void * p_handle, u32 wait_ms)
{
    (void)wait_ms; // wait_ms is not used in this implementation
    if (!p_handle) {
        return false;
    }
    LTMutexMonitor *mutmon = p_handle;
    if (mutmon->mutex == NULL) {
        return false;
    }
    mutmon->mutex->API->Lock(mutmon->mutex);
    return true;
}

/****************************************************************************/
/* Give mutex                                                               */
/****************************************************************************/
bool osif_mutex_give(void * p_handle)
{
    if (!p_handle) return true; // no mutex to unlock, so true, and avoid crash
    LTMutexMonitor *mutmon = p_handle;
    if (mutmon->mutex == NULL) return true;
    mutmon->mutex->API->Unlock(mutmon->mutex);
    return true;
}

/****************************************************************************/
/* Create counting semaphore                                                */
/****************************************************************************/
bool osif_sem_create(void ** pp_handle, u32 init_count, u32 max_count)
{
    LTCountingSemaphore *sem = lt_createobject(LTCountingSemaphore);
    if (!sem) { *pp_handle = NULL; return false; }
    sem->API->Init(sem, max_count, init_count);
    *pp_handle = (void *)sem;
    return true;
}

/****************************************************************************/
/* Delete counting semaphore                                                */
/****************************************************************************/
bool osif_sem_delete(void * p_handle)
{
    if (!p_handle) return false;
    lt_destroyobject((LTCountingSemaphore *)p_handle);
    return true;
}

/****************************************************************************/
/* Give counting semaphore                                                  */
/****************************************************************************/
bool osif_sem_give(void * p_handle)
{
    if (!p_handle) return false;
    LTCountingSemaphore *sem = (LTCountingSemaphore *)p_handle;
    sem->API->SignalFromThread(sem);
    return true;
}

/****************************************************************************/
/* Take counting semaphore                                                  */
/****************************************************************************/
bool osif_sem_take(void * p_handle, u32 wait_ms)
{
    if (!p_handle) return false;
    LTCountingSemaphore *sem = (LTCountingSemaphore *)p_handle;
    LTTime timeout = (wait_ms != BLE_NPL_TIME_FOREVER) ? LTTime_Milliseconds(wait_ms) : LTTime_Infinite();
    return sem->API->Wait(sem, timeout);
}

u16 osif_sem_getvalue(void * p_handle) {
    if (!p_handle) return 0;
    LTCountingSemaphore *sem = (LTCountingSemaphore *)p_handle;
    return (u16)(sem->API->GetCount(sem));
}

/****************************************************************************/
/* Create software timer                                                    */
/****************************************************************************/
bool osif_timer_create(void ** pp_handle, const char * p_timer_name, u32 timer_id,
                       u32 interval_ms, bool reload, void (* p_timer_callback)(void *))
{
    if (p_timer_callback == NULL || pp_handle == NULL)
    {
        return false;
    }

    LT_UNUSED(p_timer_name);

    LTThread hThread = s_iThread->GetCurrentThread();

    timerData_t *pTimerData = (timerData_t *)lt_malloc(sizeof(timerData_t));
    if (pTimerData == NULL)
    {
        return false;
    }

    pTimerData->hThread     = hThread;
    pTimerData->pClientData = (void *)timer_id;
    pTimerData->pTimerProc  = (LTThread_TimerProc *)p_timer_callback;
    pTimerData->reload      = reload;
    pTimerData->interval_ms = interval_ms;
    pTimerData->timerState  = kTimerRunning;

    *pp_handle = (void *)pTimerData;

    //LTOSDO: check reload
    //LTOSDO: when thread ends, timer gets deleted. Data alocated alongside the timer won't get deleted in that case
    // by default. Think about this.
    s_iThread->SetTimer(hThread, LTTime_Milliseconds(interval_ms), (LTThread_TimerProc *)p_timer_callback, NULL, (void *)pTimerData);

    return true;
}

/****************************************************************************/
/* Stop software timer, keep the data                                       */
/****************************************************************************/
bool osif_timer_stop(void ** pp_handle)
{
    if (pp_handle == NULL || *pp_handle == NULL)
    {
        return false;
    }

    timerData_t        * pTimerData       = (timerData_t *)* pp_handle;
    LTThread             hT               = pTimerData->hThread;
    LTThread_TimerProc * p_timer_callback = pTimerData->pTimerProc;
    s_iThread->KillTimer(hT, p_timer_callback, (void *)pTimerData);
    pTimerData->timerState = kTimerStopped;

    return true;
}

/****************************************************************************/
/* Start software timer                                                     */
/****************************************************************************/
bool osif_timer_start(void ** pp_handle)
{
    if (pp_handle == NULL || *pp_handle == NULL)
    {
        return false;
    }

    timerData_t * pTimerData = (timerData_t *)* pp_handle;

    if (pTimerData->timerState != (u32)kTimerStopped)
    {
        return false;
    }

    s_iThread->SetTimer(pTimerData->hThread, LTTime_Milliseconds(pTimerData->interval_ms), (LTThread_TimerProc *)pTimerData->pTimerProc, NULL, (void *)pTimerData);
    pTimerData->timerState = (u32)kTimerRunning;

    return true;
}

/****************************************************************************/
/* Restart software timer, set with new interval                            */
/****************************************************************************/
bool osif_timer_restart(void ** pp_handle, u32 interval_ms)
{
    if (pp_handle == NULL || *pp_handle == NULL)
    {
        return false;
    }

    if (osif_timer_stop(pp_handle) == false)
    {
        return false;
    }

    timerData_t * pTimerData = (timerData_t *)* pp_handle;
    pTimerData->interval_ms = interval_ms;

    return osif_timer_start(pp_handle);
}

/****************************************************************************/
/* Stop software timer, delete the data                                     */
/****************************************************************************/
bool osif_timer_delete(void ** pp_handle)
{
    if (pp_handle == NULL || *pp_handle == NULL)
    {
        return false;
    }

    if (osif_timer_stop(pp_handle) == false)
    {
        return false;
    }
    else
    {
        timerData_t * pTimerData = (timerData_t *)* pp_handle;
        osif_mem_free(pTimerData);
    }

    return true;
}

/****************************************************************************/
/* Get software timer ID                                                    */
/****************************************************************************/
bool osif_timer_id_get(void ** pp_handle, u32 * p_timer_id)
{
    if (pp_handle == NULL || *pp_handle == NULL || p_timer_id == NULL)
    {
        return false;
    }

    timerData_t * pTimerData = (timerData_t *)* pp_handle;
    * p_timer_id = (u32)(pTimerData->pClientData);

    return true;
}

/****************************************************************************/
/* Get timer state - unimplemented                                          */
/****************************************************************************/
bool osif_timer_state_get(void ** pp_handle, u32 * p_timer_state)
{
    if (p_timer_state == NULL)
    {
        return false;
    }

    if (pp_handle == NULL || *pp_handle == NULL)
    {
        * p_timer_state = (u32)kTimerDeleted;
    }
    else
    {
        timerData_t * pTimerData = (timerData_t *)* pp_handle;
        * p_timer_state = pTimerData->timerState;
    }

    return true;
}

/****************************************************************************/
/* LT-OS doesn't have timer dump implemented                                */
/****************************************************************************/
bool osif_timer_dump(void)
{
    return true;
}


bool osif_lt_init(void) {
    s_iThread      = lt_getlibraryinterface(ILTThread, LT_GetCore());
    return true;
}

void osif_lt_printf(const char * fmt, ...) {
    lt_va_list args;
    lt_va_start(args, fmt);
    LT_GetCore()->ConsolePrintV(fmt, args);
    lt_va_end(args);
}

#ifdef __cplusplus
}
#endif

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jul-22   vespasian   created
 */
