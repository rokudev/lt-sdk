/*******************************************************************************
 * source/lt/media/dsp/LTMediaDSPPWMRender.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 * 
 *******************************************************************************/

#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/media/dsp/LTMediaDSP.h>
#include <lt/media/dsp/LTMediaDSPPWMRender.h>

#include "LTMediaDSPPWMRender.h"

struct sLTMediaDSPPWMRender {
    u16                 uPeriodTicks;
    u16                 uZeroTicks;
    u16                 uOneTicks;
};

/* ----------------------------------------------------------------- */
/* ----------------------------------------------------------------- */

LTMediaDSPPWMRender *LTMediaDSPPWMRenderImpl_Create(void)
{
    LTMediaDSPPWMRender *pPWM = lt_malloc(sizeof(*pPWM));

    return pPWM;
}

static void LTMediaDSPPWMRenderImpl_SetBitstreamTickCounts(LTMediaDSPPWMRender *pPWM, u16 period_ticks, u16 zero_ticks, u16 one_ticks )
{
    pPWM->uPeriodTicks = period_ticks;
    pPWM->uZeroTicks = zero_ticks;
    pPWM->uOneTicks = one_ticks;
}

static void LTMediaDSPPWMRenderImpl_RenderElements(LTMediaDSPPWMRender *pPWM, 
    LTPWMRenderElement *pElems,
    const u8           *pBits,
    LT_SIZE             bitOffset,
    LT_SIZE             nBits )
{
    LT_SIZE oPos = 0;
    for ( LT_SIZE i = bitOffset; i < (bitOffset+nBits); i++ ) {
        if ( pBits[i>>3] & (1<<(i&0x7)) ) { /* one bit */
            pElems[oPos].hi_ticks_val = pPWM->uOneTicks  |  0x01;
            pElems[oPos].lo_ticks_val = (pPWM->uPeriodTicks - pPWM->uOneTicks) & ~0x01;
        } else  { /* zero bit */
            pElems[oPos].hi_ticks_val = pPWM->uZeroTicks  |  0x01;
            pElems[oPos].lo_ticks_val = (pPWM->uPeriodTicks - pPWM->uZeroTicks) & ~0x01;
        }
        oPos++;
    }
}

static void LTMediaDSPPWMRenderImpl_DestroyPWMRender(LTMediaDSPPWMRender *pPWM)
{
    if ( NULL != pPWM ) {
        lt_free(pPWM);
    }
}

define_LTLIBRARY_INTERFACE(ILTMediaDSPPWMRender)

    .SetBitstreamTickCounts = &LTMediaDSPPWMRenderImpl_SetBitstreamTickCounts,
    .RenderElements         = &LTMediaDSPPWMRenderImpl_RenderElements,
    .DestroyPWMRender       = &LTMediaDSPPWMRenderImpl_DestroyPWMRender

LTLIBRARY_DEFINITION;


/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-23   diocletian  created
 */
