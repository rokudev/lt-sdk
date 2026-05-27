/*******************************************************************************
 * source/lt/media/dsp/LTMediaDSP.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/media/dsp/LTMediaDSP.h>
#include <lt/media/dsp/LTIntegerFFT.h>
#include <lt/media/dsp/LTMediaDSPPWMRender.h>
#include <lt/media/dsp/LTEchoCancel.h>
#include <lt/media/dsp/LTResample.h>

#include "LTMediaDSPIntegerFFT.h"
#include "LTMediaDSPPWMRender.h"

/*___________________________________________
 / LTMediaByteDSP library initialization */
static bool LTMediaDSPImpl_LibInit(void) {
    return true;
}

static void LTMediaDSPImpl_LibFini(void) {
}

/*___________________________________________________
 / LTMediaDSP library root interface binding */
define_LTLIBRARY_ROOT_INTERFACE(LTMediaDSP) {
    .CreateIntegerFFT                   = &LTMediaDSPIntegerFFTImpl_Create,
    .CreatePWMRender                    = &LTMediaDSPPWMRenderImpl_Create,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(
  LTMediaDSP,
    (ILTIntegerFFT)
    (ILTMediaDSPPWMRender)
    (LTEchoCancelImpl)
    (LTResampleImpl)
)

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  31-May-23   diocletian  created
 */
