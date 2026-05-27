/******************************************************************************
 * LTKArchArmCortexM_TrustZone.h                         LTK TrustZone Security
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_TRUSTZONE_H
#define ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_TRUSTZONE_H

#include <lt/core/bsp/LTSecurityBSP_TrustZoneM.h>

/**********************************
 * TrustZone Container Descriptor */
typedef struct LTKTrustZoneM {
    /* Application-specific */
    LTSecurity_Info  SecurityInfo;                 /**< application security descriptor */

    /* LTK Private */
    void  (* pInitContexts)(void);                 /**< initialize TrustZone contexts */
    void  (* pResetContext)(s32 nContext);         /**< called by LTK when done with context */
    void  (* pSwitchContext)(s32 nSaveContext,
                             s32 nRestoreContext); /**< called by LTK for secure context switches */
} LTKTrustZoneM;

/* Function type for container initialization and obtaining TrustZone descriptor */
typedef void (LTKTrustZoneInitFunc)(void);
typedef LTKTrustZoneInitFunc * (LTKGetTrustZoneInfoFunc)(LTKTrustZoneM * pInfo);

#endif // #ifndef ROKU_LT_SOURCE_LT_LTK_LTKARCHARMCORTEXM_TRUSTZONE_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  02-Nov-21   tiberius    created
 */
