/***************************************************************************************************
 * platforms/common/source/common/driver/sk6805led/LTChipSK6805Impl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Control of chains of Shenzhen Normand Electronic Co SK6805 Serial LEDs.
 * Typically driven by <platform>DriverLED to control indicator lamp LEDs.
 **************************************************************************************************/

#include <lt/core/LTCore.h>
#include "LTChipSK6805.h"

DEFINE_LTLOG_SECTION("ltchipsk6805");

struct LTChipSK6805 {
    /**< The context structure of one SK6805 IC. */

    LTDeviceSPI           * pDeviceSPI;
    LTDeviceUnit            hSPI;             /**< Handle to the SPI device used to communicate with the IC */
    u8                      nNumLEDs;         /**< The number of LEDs configured for this IC */
    u8                    * pFrameBuffer;     /**< The LED frame buffer. Memory is allocated by the calling library
                                                  and must be of size: LTCHIPSK6805_FRAMEBUFFER_SIZE(nNumLEDs)  */
    LT_SIZE                 nFrameBufferSize; /**< The size of the provided frame buffer in bytes */
    u32                   * pRawColor;
};

enum {
    kBitPatternHigh         = 0x6,
    kBitPatternLow          = 0x4,
    kResetDelayUs           = 100,
    kFrameBufferBytesPerLED = 12
};

#define LTCHIPSK6805_FRAMEBUFFER_SIZE(NumLEDs) ((NumLEDs) * kFrameBufferBytesPerLED)

static ILTThread              * s_ILTThread  = NULL;

/***********************************************************************************************************************
 * Controller I/O particulars                                                                                          */

static bool LTChipSK6805_InitializeSPI(LTChipSK6805 * pChip, u32 nSPIUnitNum) {
    if (!(pChip->hSPI = pChip->pDeviceSPI->CreateDeviceUnitHandle(nSPIUnitNum))) {
        LTLOG_YELLOWALERT("spi.init.err.createhandle", "LTDeviceSPI port %lu could not be created", nSPIUnitNum);
        return false;
    }
    LTDeviceSPI_Capabilities caps;
    LTDeviceSPI_Configuration cfg;
    if (pChip->pDeviceSPI->GetDeviceCapabilities(pChip->hSPI, &caps)) {
        LTLOG_DEBUG("spi.init.caps", "freq_min %lu freq_max %lu bits_min %lu bits_max %lu caps %lu",
            LT_Pu32(caps.Freq_min), LT_Pu32(caps.Freq_max),
            LT_Pu32(caps.Bits_min), LT_Pu32(caps.Bits_max),
            LT_Pu32(caps.Caps_mask));
    } else {
        LTLOG("spi.init.err.caps", "error");
        return false;
    }
    cfg.Frequency = 2500000; /* 2.5 MHz */
    cfg.Bits = 8;
    cfg.Master = true;
    cfg.Async = false;
    cfg.Dma = false;
    if (!pChip->pDeviceSPI->SetDeviceConfiguration(pChip->hSPI, &cfg)) {
        LTLOG("spi.init.fail", "configuration");
        return false;
    }

    return true;
}

static void LTChipSK6805_FinalizeSPI(LTChipSK6805 * pChip) {
    if (pChip->hSPI) {
         LT_GetCore()->DestroyHandle(pChip->hSPI);
         pChip->hSPI = 0;
     }
}

/***************************************************************************************************
   Interface functions.                                                                           */

LTChipSK6805 * LTChipSK6805_Initialize(LTDeviceSPI * pDeviceSPI, LTChipSK6805Config const * pConfig) {
    if (!pDeviceSPI || !pConfig) return NULL;
    LTChipSK6805 * pChip = lt_malloc(sizeof(LTChipSK6805));
    if (!pChip) return NULL;
    pChip->pDeviceSPI = pDeviceSPI;
    pChip->nNumLEDs = pConfig->nNumLEDs;
    pChip->nFrameBufferSize = LTCHIPSK6805_FRAMEBUFFER_SIZE(pConfig->nNumLEDs);
    pChip->pFrameBuffer = lt_malloc(pChip->nFrameBufferSize);
    if (!pChip->pFrameBuffer) {
        LTChipSK6805_Finalize(pChip);
        return NULL;
    }
    lt_memset(pChip->pFrameBuffer, 0, pChip->nFrameBufferSize);
    pChip->pRawColor = lt_malloc(pConfig->nNumLEDs * sizeof(u32));
    if (!pChip->pRawColor) {
        LTChipSK6805_Finalize(pChip);
        return NULL;
    }
    lt_memset(pChip->pRawColor, 0, pConfig->nNumLEDs * sizeof(u32));
    if (!(s_ILTThread = lt_getlibraryinterface(ILTThread, LT_GetCore()))) {
        LTChipSK6805_Finalize(pChip);
        return NULL;
    }
    if (!LTChipSK6805_InitializeSPI(pChip, pConfig->nSPIUnitNum)) {
        LTChipSK6805_Finalize(pChip);
        return NULL;
    }
    return pChip;
}

void LTChipSK6805_Finalize(LTChipSK6805 * pChip) {
    if (!pChip) return;
    LTChipSK6805_FinalizeSPI(pChip);
    if (pChip->pRawColor)    lt_free(pChip->pRawColor);
    if (pChip->pFrameBuffer) lt_free(pChip->pFrameBuffer);
    lt_free(pChip);
}

u32 LTChipSK6805_GetColor(LTChipSK6805 * pChip, u8 nLEDIndex) {
    if (!pChip) return 0;
    if (nLEDIndex >= pChip->nNumLEDs) return 0;
    return pChip->pRawColor[nLEDIndex];
}

void LTChipSK6805_SetColor(LTChipSK6805 * pChip, u8 nLEDIndex, u32 nColor) {
    if (!pChip) return;
    if (!pChip->pFrameBuffer) return;
    if (nLEDIndex >= pChip->nNumLEDs) return;

    pChip->pRawColor[nLEDIndex] = nColor;
    /* Convert RGB nColor to native GRB format of the LED */
    nColor = ((nColor & 0xFF0000) >> 8) | ((nColor & 0x00FF00) << 8) | (nColor & 0x0000FF);
    u8 * pLEDFrame = &pChip->pFrameBuffer[nLEDIndex * kFrameBufferBytesPerLED];
    for (int j = 0; j < kFrameBufferBytesPerLED; ++j) {
        u8 nColorBits = nColor >> (2 * (kFrameBufferBytesPerLED - j - 1));
        pLEDFrame[j] = ((nColorBits & 2) ? kBitPatternHigh : kBitPatternLow) << 4;
        pLEDFrame[j] |= ((nColorBits & 1) ? kBitPatternHigh : kBitPatternLow);
    }
}

void LTChipSK6805_Refresh(LTChipSK6805 * pChip) {
    if (!pChip) return;
    bool bResult = pChip->pDeviceSPI->SPIMasterTransfer(pChip->hSPI, NULL, pChip->pFrameBuffer, pChip->nFrameBufferSize, NULL, NULL);
    if (!bResult) {
        LTLOG("spi.xfer.fail", "handle %lx",LT_PLT_HANDLE(pChip->hSPI));
        return;
    }

    /* After receiving 80us of silence, the LEDs will initiate a reset which will
       cause the transmitted color values to be applied. We delay here for 100us
       for good measure. */
    s_ILTThread->Sleep(LTTime_Microseconds(kResetDelayUs));
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Oct-21   trajan      created
 */
