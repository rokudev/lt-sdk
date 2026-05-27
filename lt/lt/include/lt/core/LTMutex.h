/******************************************************************************
 * <lt/core/LTMutex.h>                LT Priority-Inversion-Safe Nestable Mutex
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/**
 * @defgroup ltcore_mutex LTMutex
 * @ingroup ltcore
 * @{
 *
 * @brief  LTMutex is a lightweight mutex that provides extremely easy-to-use
 * thread locking.
 *
 * Do not attempt to directly instantiate or destruct LTMutex
 * instances.  Instead call @p lt_createobject(LTMutex) and
 * @p lt_destroyobject().  <b>LTMutexes are recursive (they can nest).</b>
 * Priority inversion is handled so that if a low-priority thread is executing
 * with the mutex locked and a separate high-priority thread attempts a lock on
 * the mutex held by the lower-priority thread, the lower-priority thread will
 * be elevated to the priority of the higher-priority thread until it Unlocks
 * the mutex, at which point it will be restored to its original priority.
 */

#ifndef ROKU_LT_INCLUDE_LT_CORE_LTMUTEX_H
#define ROKU_LT_INCLUDE_LT_CORE_LTMUTEX_H

#include <lt/LTTypes.h>
#include <lt/LTObject.h>
LT_EXTERN_C_BEGIN

/***********************
***********************
* LTMutex *Object      */
typedef_LTObject(LTMutex, 1) {

    void (* Lock)    (LTMutex *mutex);
        /**< @brief Locks the mutex.
         *
         * Call Lock() to lock the mutex.  If the mutex is locked by another thread
         * the caller will block until it is able to acquire the lock itself.
         *
         * Calls to Lock() can be nested; a call to Unlock() must take place for each
         * call to Lock() for the mutext to become unlocked
         *
         * @param[in] mutex The handle of the mutex to be locked.
         */

    void (* Unlock)  (LTMutex *mutex);
        /**< @brief Unlocks the mutex.
         *
         * Call Unlock() to unlock the mutex.  Be sure to call Unlock() for each
         * call to Lock() (or successful TryLock()).
         *
         * @param[in] mutex The handle of the mutex to be unlocked.
         */

    bool (* TryLock) (LTMutex *mutex);
        /**< @brief Attempts to lock the mutex.
         *
         * If TryLock() is able to acquire the mutex without blocking,
         * it returns @p true with the mutex locked on behalf of the caller.
         * TryLock() never blocks - if the mutex is currently
         * locked by another thread, TryLock() returns @p false without blocking
         * and without holding the mutex.
         *
         * @param[in] mutex The handle of the mutex to be locked.
         * @return @p true if the mutex was locked successfully, @p false otherwise.
         */

} LTOBJECT_API;

/* Example Usage:
 *
 *  LTCore * pCore = LT_GetCore();
 *  LTMutex *mutex = lt_createobject(LTMutex);
 *  if (mutex) {
 *      mutex->API->Lock(mutex);
 *          / * do something with mutex locked * /
 *      mutex->API->Unlock(mutex);
 *      / * when it comes time to destroy the mutex: * /
 *      lt_destroyobject(mutex);
 *  }
 *
 */

/** @} */

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_CORE_LTMUTEX_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  30-Apr-20   augustus    re-created in C
 *  12-Aug-20   augustus    converted from object to handle
 *  13-Sep-20   augustus    typedef LTHandle LTMutex
 */
