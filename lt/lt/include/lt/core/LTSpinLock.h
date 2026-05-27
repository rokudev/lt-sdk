/******************************************************************************
 * <lt/core/LTSpinLock.h>              LTSpinLock - for device drivers only
 *                                   smp critical sections for threads and ISRs
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltcore_bsp_spinlock LTSpinLock
 * @ingroup ltcore_bsp
 * @{
 *
 * @brief  LTSpinLock is a low-level spinlock that disables interrupts on the
 *         current core before spinning in a busy loop to acquire the lock.
 *         There is never any contention on a single core system where locking
 *         and unlocking the spinlock is equivalent to disable/enable.
 *
 *         On a multi-core system, locking the spinlock disables interrupts on the
 *         active core before attempting to acquire the lock.  As on an SMP system,
 *         there is never any contention for the lock on the current core; if the lock
 *         can't be acquired immediately, another core is holding the lock and on the
 *         current core the spinlock will spin in a busy loop attempting to acquire the
 *         lock until it is able to, which only occurs when no other core has the lock.
 */

#ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTSPINLOCK_H
#define ROKU_LT_INCLUDE_LT_CORE_BSP_LTSPINLOCK_H

#include <lt/LTObject.h>

LT_EXTERN_C_BEGIN

/*_____________
 / LTMonitor */
typedef_LTObject(LTSpinLock, 1) {

    void (* Lock)(LTSpinLock *spinlock);
        /**< @brief Locks the spinlock
         *
         * Call Lock() to lock the spinlock.  This will disable interrupts
         * on the current core and acquire the lock, which is always immediate without spinning
         * on a uni-core system and on a multi-core system when no thread or isr running on a different core
         * holds the lock.  If the lock is held on another core, the spinlock will spin with interrupts
         * disabled trying to acquire the lock, which it will do when the lock no longer held by any other core.
         *
         * @param[in] spinlock The spinlock to lock.
         *
         * @note Unlock() must be called by the same ISR or thread that has called Lock().  The execution duration
         *       between Lock() and Unlock() should be as short as possible.
         * @note calls to Lock() DO NOT NEST!  A deadlock will result if the same thread or ISR calls Lock() in succession.
         * @see Unlock()
         */

    void (* Unlock)(LTSpinLock *spinlock);
        /**< @brief Unlocks the spinlock
         *
         * Call Unlock() to unlock the spinlock.  This will release the spinlock and enable interrupts
         * on the current core.  Unlock must only be called from within the same ISR or thread that
         * has called Lock().
         *
         * @param[in] spinlock The spinlock to unlock.
         * @note Unlock() must be only be called within the same ISR or thread that has already called Lock().
         *       The execution duration between Lock() and Unlock() should be as short as possible.
         * @see Lock()
         *
         */

} LTOBJECT_API;

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_BSP_LTMONITOR_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  03-Apr-24   augustus    created
 */
