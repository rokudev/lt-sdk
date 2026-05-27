/*******************************************************************************
 * lt/source/lt/driver/media/audio/opus/LTDriverMediaAudioOpus.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Driver Library for Opus audio codec
 ******************************************************************************/
/** @file LTDriverMediaAudioOpus.c Implementation of Opus audio codec driver
 */

#include <lt/core/LTCore.h>
#include <lt/device/media/LTDeviceMedia.h>

//DEFINE_LTLOG_SECTION("opus.drv");

LTMediaSource OpusEncoder_Open(LTMediaFormat *pFormat);
LTMediaSink   OpusDecoder_Open(LTMediaFormat * pFormat);
bool          OpusEncoder_Init(void);
void          OpusEncoder_Destroy(void);

static bool
LTDriverMediaImpl_SupportsFormat(LTMediaFormat * pFormat) {
    return (pFormat->nKind == kLTMediaKind_Audio) &&
           (pFormat->nEncoding == kLTMediaEncoding_Opus);
}

static bool
LTDriverMediaImpl_GetProperty(const char *name, void *value) {
    LT_UNUSED(name);
    LT_UNUSED(value);
    return false;
}

static bool
LTDriverMediaImpl_SetProperty(const char *name, const void *value) {
    LT_UNUSED(name);
    LT_UNUSED(value);
    return false;
}

/*_______________________________________
/ LTDriverMedia library initialization */
static bool
LTDriverMediaAudioOpusImpl_LibInit(void) {
    return OpusEncoder_Init();
}

static void
LTDriverMediaAudioOpusImpl_LibFini(void) {
    OpusEncoder_Destroy();
}

/*_________________________________________________________
 / LTDriverMediaAudioOpus library root interface binding */
typedef_LTLIBRARY_ROOT_INTERFACE(LTDriverMediaAudioOpus, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTDriverMediaAudioOpus)    LTLIBRARY_DEFINITION;

define_LTLIBRARY_INTERFACE(ILTDriverMedia) {
    .SupportsFormat = &LTDriverMediaImpl_SupportsFormat,
    .GetProperty    = &LTDriverMediaImpl_GetProperty,
    .SetProperty    = &LTDriverMediaImpl_SetProperty,
    .OpenSource     = &OpusEncoder_Open,
    .OpenSink       = &OpusDecoder_Open,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverMediaAudioOpus, (ILTDriverMedia));

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  20-Nov-23   trajan      created
 */
