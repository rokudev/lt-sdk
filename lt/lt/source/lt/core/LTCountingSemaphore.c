/******************************************************************************
 * <lt/core/LTCountingSemaphore.c>     LT ISR-Safe Counting Semaphore
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCountingSemaphore.h>
#include <lt/core/LTMonitor.h>
#include "LTCoreImpl.h"


/*___________________________________________________
  LTCountingSemaphore object private data members */
typedef_LTObjectImpl(LTCountingSemaphore, LTCountingSemaphoreImpl) {
    LTMonitor *monitor;
    LTAtomic   count;
    u32        maxCount;
} LTOBJECT_API;

/*___________________________________________
  LTCountingSemaphore object constructors */
static bool LTCountingSemaphoreImpl_ConstructObject(LTCountingSemaphoreImpl *sem) {
    sem->monitor = lt_createobject(LTMonitor);
    if (!sem->monitor) return false;
    LTAtomic_Store(&sem->count, 0);
    sem->maxCount = 0;
    return true;
}

static void LTCountingSemaphoreImpl_DestructObject(LTCountingSemaphoreImpl *sem) {
    if (sem->monitor) lt_destroyobject(sem->monitor);
}

/*_____________________________________________
  LTCountingSemaphore object api functions */

static void LTCountingSemaphoreImpl_Init(LTCountingSemaphoreImpl *sem, u32 maxCount, u32 initCount) {
    LT_ASSERT(initCount <= maxCount);
    sem->maxCount = maxCount;
    LTAtomic_Store(&sem->count, initCount);
}

static bool LTCountingSemaphoreImpl_Wait(LTCountingSemaphoreImpl *sem, LTTime timeout) {
    LTMonitor *mon = sem->monitor;

    /* Fast path: try to decrement count without blocking */
    for (;;) {
        u32 c = LTAtomic_Load(&sem->count);
        if (c == 0) break;
        if (LTAtomic_CompareAndExchange(&sem->count, c, c - 1)) return true;
    }

    /* Slow path: block on monitor until we can CAS-decrement count, or timeout */
    mon->API->Enter(mon);

    LTTime remaining = timeout;
    LTTime start     = LT_GetCore()->GetKernelTime();

    for (;;) {
        /* Try to CAS-decrement count (may fail if TryWait() races us) */
        for (;;) {
            u32 c = LTAtomic_Load(&sem->count);
            if (c == 0) break;
            if (LTAtomic_CompareAndExchange(&sem->count, c, c - 1)) {
                mon->API->Exit(mon);
                return true;
            }
        }

        /* Count is zero — block until signaled or timeout */
        bool notified = mon->API->Wait(mon, remaining);

        /* Recalculate remaining time after every wakeup */
        if (!LTTime_IsInfinite(timeout)) {
            LTTime now = LT_GetCore()->GetKernelTime();
            remaining = LTTime_Subtract(timeout, LTTime_Subtract(now, start));
        }

        /* Re-check count: handles real notification and the lost-notify
         * case where Signal's naked Notify arrived between Enter and Wait. */
        if (LTAtomic_Load(&sem->count) > 0) continue;

        if (!notified || LTTime_IsLessThanOrEqual(remaining, LTTime_Zero())) {
            mon->API->Exit(mon);
            return false;
        }
    }
}

static bool LTCountingSemaphoreImpl_TryWait(LTCountingSemaphoreImpl *sem) LT_ISR_SAFE {
    for (;;) {
        u32 c = LTAtomic_Load(&sem->count);
        if (c == 0) return false;
        if (LTAtomic_CompareAndExchange(&sem->count, c, c - 1)) return true;
    }
}

static void LTCountingSemaphoreImpl_Signal(LTCountingSemaphoreImpl *sem) LT_ISR_SAFE {
    for (;;) {
        u32 c = LTAtomic_Load(&sem->count);
        if (c >= sem->maxCount) return;
        if (LTAtomic_CompareAndExchange(&sem->count, c, c + 1)) {
            sem->monitor->API->Notify(sem->monitor);   /* naked Notify — ISR-safe */
            return;
        }
    }
}

static void LTCountingSemaphoreImpl_SignalFromThread(LTCountingSemaphoreImpl *sem) {
    LTMonitor *mon = sem->monitor;
    for (;;) {
        u32 c = LTAtomic_Load(&sem->count);
        if (c >= sem->maxCount) return;
        if (LTAtomic_CompareAndExchange(&sem->count, c, c + 1)) {
            mon->API->Enter(mon);
            mon->API->Notify(mon);
            mon->API->Exit(mon);
            return;
        }
    }
}
static u32 LTCountingSemaphoreImpl_GetCount(LTCountingSemaphoreImpl *sem) LT_ISR_SAFE {
    return LTAtomic_Load(&sem->count);
}

/*_______________________________________________
  LTCountingSemaphore LTObjectApi definition */
define_LTObjectImplPublic(LTCountingSemaphore, LTCountingSemaphoreImpl,
    Init, Wait, TryWait, Signal, SignalFromThread, GetCount
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  26-Mar-26   nerva       created
 */
