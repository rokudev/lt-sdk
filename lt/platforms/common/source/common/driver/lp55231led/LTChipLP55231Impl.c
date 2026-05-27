/***************************************************************************************************
 * platforms/common/source/common/driver/lp55231led/LTChipLP55231Impl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Control of the TI LP55231 9-channel LED driver IC.
 * Typically driven by <platform>DriverLED to control indicator lamp LEDs.
 **************************************************************************************************/

#include <lt/core/LTCore.h>
#include "LTChipLP55231.h"

DEFINE_LTLOG_SECTION("lp55231chip");

struct LTChipLP55231 {
    /**< The configuration of one LP55231 IC. */
    LTDeviceI2C            * pDeviceI2C;
    u8                       nI2CAddress; /**< The I2C address of the IC */
    LTDeviceUnit		     hI2C;        /**< Handle to the I2C device used to communicate with the IC */
    u8                       nNumLEDs;    /**< The number of LEDs configured for this IC */
    LTChipLP55231LEDConfig * pLEDConfigs; /**< The LED configurations for this IC */
    u32                    * pRawColor;
};

typedef enum {
    kLP55231_Register_Eng_Cntrl1   = 0x00,
    kLP55231_Register_Cntrl_D1     = 0x06,
    kLP55231_Register_PWM_D1       = 0x16,
    kLP55231_Register_Misc         = 0x36,
    kLP55231_Register_MasterFader1 = 0x48,
} LP55231_register_address_value;

enum {
    kNumFaders = 3,
    kNumChannels  = 9
};

/***********************************************************************************************************************
 * Controller I/O particulars                                                                                          */

static bool LTChipLP55231_InitializeI2C(LTChipLP55231 * pChip, u32 nI2CUnitNum) {
    if (!(pChip->hI2C = pChip->pDeviceI2C->CreateDeviceUnitHandle(nI2CUnitNum))) {
        LTLOG_YELLOWALERT("i2c.init.err.createhandle", "LTDeviceI2C port %lu could not be created", nI2CUnitNum);
        return false;
    }
    LTDeviceI2C_Capabilities caps;
    LTDeviceI2C_Configuration cfg;
    if (pChip->pDeviceI2C->GetDeviceCapabilities(pChip->hI2C, &caps)) {
        LTLOG_DEBUG("i2c.init.caps", "freq_min %lu freq_max %lu caps %lu", LT_Pu32(caps.Freq_min), LT_Pu32(caps.Freq_max),LT_Pu32(caps.Caps_mask));
    } else {
        LTLOG("i2c.init.err.caps", "error");
        return false;
    }
    cfg.Frequency = caps.Freq_max;
    cfg.Master = true;
    cfg.Async = false;
    cfg.Dma = false;
    bool bDeviceConfigured = pChip->pDeviceI2C->SetDeviceConfiguration(pChip->hI2C, &cfg);
    if (!bDeviceConfigured) {
        LTLOG("i2c.init.fail", "configuration");
        return false;
    }

    bool bControllerPresent = pChip->pDeviceI2C->ProbeAddress(pChip->hI2C, pChip->nI2CAddress);
    if (!bControllerPresent) {
        LTLOG("i2c.probe.fail", "probe address failed");
        return false;
    }

    return true;
}

static void LTChipLP55231_FinalizeI2C(LTChipLP55231 * pChip) {
    if (pChip->hI2C) {
        LT_GetCore()->DestroyHandle(pChip->hI2C);
        pChip->hI2C = 0;
    }
}

/***************************************************************************************************
   Chip register manipulation.

   NOTE: This function has the precondition that pChip is valid.                            */

static void WriteRegister(LTChipLP55231 * pChip, u8 address, u8 value) {
    u8 packet[] = { address, value };
    bool bResult = pChip->pDeviceI2C->I2CMasterTransfer(pChip->hI2C, pChip->nI2CAddress, NULL, 0, packet, sizeof(packet), true, true, NULL, NULL);
    if (!bResult) {
        LTLOG("i2c.xfer.fail", "handle %lx",LT_PLT_HANDLE(pChip->hI2C));
    }
}

/***************************************************************************************************
   Interface functions.                                                                           */

LTChipLP55231 * LTChipLP55231_Initialize(LTDeviceI2C * pDeviceI2C, LTChipLP55231Config const * pConfig) {
    if (!pConfig || !pDeviceI2C || !pConfig->pLEDConfigs || pConfig->nNumLEDs > kNumChannels) return NULL;
    LTChipLP55231 * pChip = lt_malloc(sizeof(LTChipLP55231));
    if (!pChip) return NULL;
    pChip->pDeviceI2C = pDeviceI2C;
    pChip->nI2CAddress = pConfig->nI2CAddress;
    pChip->nNumLEDs = pConfig->nNumLEDs;
    pChip->pLEDConfigs = pConfig->pLEDConfigs;
    pChip->pRawColor = lt_malloc(pConfig->nNumLEDs * sizeof(u32));
    if (!pChip->pRawColor) {
        LTChipLP55231_Finalize(pChip);
        return NULL;
    }
    lt_memset(pChip->pRawColor, 0, pConfig->nNumLEDs * sizeof(u32));
    if (!LTChipLP55231_InitializeI2C(pChip, pConfig->nI2CUnitNum)) {
        LTChipLP55231_Finalize(pChip);
        return NULL;
    }

    /* Enable the controller */
    WriteRegister(pChip, kLP55231_Register_Eng_Cntrl1, 0x40);
    /* Enable charge pump and internal clock */
    WriteRegister(pChip, kLP55231_Register_Misc, 0x13);

    for (int i = 0; i < pChip->nNumLEDs; ++i) {
        LTChipLP55231LEDConfig * pLEDConfig = &pChip->pLEDConfigs[i];
        for (int j = 0; j < kLTChipLP55231_NumColorComponents; ++j) {
            u8 nChannelIndex = pLEDConfig->nChannelMap[j];
            if (nChannelIndex != 0) {
                u8 nCtrlVal = 0x20;                         /* Enable logarithmic dimming */
                nCtrlVal |= (pLEDConfig->nFaderIndex << 6); /* Master fader mapping */
                WriteRegister(pChip, kLP55231_Register_Cntrl_D1 + nChannelIndex - 1, nCtrlVal);
            }
        }
    }
    return pChip;
}

void LTChipLP55231_Finalize(LTChipLP55231 * pChip) {
    if (!pChip) return;
    LTChipLP55231_FinalizeI2C(pChip);
    if (pChip->pRawColor) lt_free(pChip->pRawColor);
    lt_free(pChip);
}

u32 LTChipLP55231_GetColor(LTChipLP55231 * pChip, u8 nLEDIndex) {
    if (!pChip) return 0;
    if (nLEDIndex >= pChip->nNumLEDs) return 0;
    return pChip->pRawColor[nLEDIndex];
}

static void SetMasterFader(LTChipLP55231 * pChip, u8 nFader, u8 nValue) {
    if (!pChip) return;
    if (nFader >= kNumFaders) return;
    WriteRegister(pChip, kLP55231_Register_MasterFader1 + nFader, nValue);
}

static void SetChannelLuminance(LTChipLP55231 * pChip, u8 nChannel, u8 nLuminance) {
    if (!pChip) return;
    if (nChannel >= kNumChannels) return;
    WriteRegister(pChip, kLP55231_Register_PWM_D1 + nChannel, nLuminance);
}

void LTChipLP55231_SetColor(LTChipLP55231 * pChip, u8 nLEDIndex, u32 nColor) {
    if (!pChip) return;
    if (nLEDIndex >= pChip->nNumLEDs) return;
    pChip->pRawColor[nLEDIndex] = nColor;
    LTChipLP55231LEDConfig * pLEDConfig = &pChip->pLEDConfigs[nLEDIndex];
    if (pLEDConfig->nFaderIndex != 0) {
        u8 nFaderValue = (nColor >> 24) & 0xFF;
        SetMasterFader(pChip, pLEDConfig->nFaderIndex - 1, nFaderValue);
    }
    for (int i = 0; i < kLTChipLP55231_NumColorComponents; ++i) {
        u8 nChannelIndex = pLEDConfig->nChannelMap[i];
        if (nChannelIndex != 0) {
            u8 nColorComponent = (nColor >> (16 - (8*i))) & 0xFF;
            SetChannelLuminance(pChip, nChannelIndex - 1, nColorComponent);
        }
    }
}

void LTChipLP55231_Refresh(LTChipLP55231 * pChip) {
    LT_UNUSED(pChip);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Oct-21   trajan      created
 */
