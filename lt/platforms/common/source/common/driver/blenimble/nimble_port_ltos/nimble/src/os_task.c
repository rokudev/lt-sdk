/*******************************************************************************
 * platforms/esp32/source/esp32/driver/bt/nimble_port_ltos/os_task.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/core/LTTime.h>

#include <osif.h>
#include <os_types.h>

/**
 * Initialize a task.
 *
 * This function initializes the task structure pointed to by t,
 * clearing and setting it's stack pointer, provides sane defaults
 * and sets the task as ready to run, and inserts it into the operating
 * system scheduler.
 *
 * @param pp_handle The task to initialize
 * @param p_name The name of the task to initialize
 * @param p_routine The task function to call
 * @param p_param The argument to pass to this task function
 * @param priority The priority at which to run this task
 * @param stack_size The overall size of the task's stack.
 *
 * @return true on success, non-zero on failure.
 */

int ble_npl_task_init(struct ble_npl_task *pTask, const char *pName, ble_npl_task_func_t pFunc,
         void *arg, uint8_t prio, ble_npl_time_t sanity_itvl,
         ble_npl_stack_t *stack_bottom, uint16_t stack_size)
{
    LT_UNUSED(stack_bottom);
    LT_UNUSED(sanity_itvl);
    return (int)osif_task_create((void **)&(pTask->pHandle), pName, pFunc, arg, stack_size, (uint16_t)prio);
}


/*
 * Removes specified task
 * XXX
 * NOTE: This interface is currently experimental and not ready for common use
 */
int ble_npl_task_remove(struct ble_npl_task *pTask)
{
    return (int)osif_task_delete(pTask->pHandle);
}

/**
 * Return the number of tasks initialized.
 *
 * @return number of tasks initialized
 */
uint8_t ble_npl_task_count(void)
{
    return 0;
}

void * ble_npl_get_current_task_id(void)
{
   ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());
   return (void *)iThread->GetCurrentThread();
}

bool ble_npl_os_started(void)
{
    return true;
}

void ble_npl_task_yield(void)
{
    osif_task_yield();
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jul-22   vespasian   created
 */
