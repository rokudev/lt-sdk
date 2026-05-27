/******************************************************************************
 * <lt/core/LTSpinLock.c>                                            LTSpinLock
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTSpinLock.h>
#include "LTCoreImpl.h"

/*________________________________________
  LTSpinLock object private data members */
typedef_LTObjectImpl(LTSpinLock, LTSpinLockImpl) {
    LT_SIZE nMask;
    LTAtomic lock;
} LTOBJECT_API;

/*________________________________
  LTSpinLock object constructors */
static bool LTSpinLockImpl_ConstructObject(LTSpinLockImpl *spinlock) {
    spinlock->nMask = 0;
    LTAtomic_Store(&spinlock->lock, 0);
    return true;
}

static void LTSpinLockImpl_DestructObject(LTSpinLockImpl *spinlock) {
    LT_UNUSED(spinlock); /* in case ASSERTS get compiled out */
    LT_ASSERT(spinlock->nMask == 0);
    LT_ASSERT(LTAtomic_Load(&spinlock->lock) == 0);
}

/*__________________________________
  LTSpinLock object api functions */
static void LTSpinLockImpl_Lock(LTSpinLockImpl *spinlock) {
    LT_SIZE nMask = LTKDisableInterrupts();
    while (! LTAtomic_CompareAndExchange(&spinlock->lock, 0, 1)) {
        while (LTAtomic_Load(&spinlock->lock)) { }
    }
    spinlock->nMask = nMask;
}

static void LTSpinLockImpl_Unlock(LTSpinLockImpl *spinlock) {
    LT_SIZE nMask = spinlock->nMask;
    spinlock->nMask = 0;
    LTAtomic_Store(&spinlock->lock, 0);
    LTKEnableInterrupts(nMask);
}


/*____________________________________
  LTSpinLock LTObjectApi definition */
define_LTObjectImplPublic(LTSpinLock, LTSpinLockImpl,
    Lock, Unlock
);

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Apr-24   augustus    created
 */
