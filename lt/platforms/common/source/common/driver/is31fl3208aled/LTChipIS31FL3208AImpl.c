/***************************************************************************************************
 * platforms/common/source/common/driver/is31fl3208aled/LTChipIS31FL3208AImpl.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Control of the Lumissil IS31FL3208A 18-channel LED driver IC.
 * Typically driven by <platform>DriverLED to control indicator lamp LEDs.
 **************************************************************************************************/

#include <lt/core/LTCore.h>
#include "LTChipIS31FL3208A.h"

DEFINE_LTLOG_SECTION("IS31FL3208Achip");

struct LTChipIS31FL3208A {
    /**< The configuration of one IS31FL3208A IC. */
    LTChipIS31FL3208ALEDConfig * pLEDConfigs;   /**< The LED configurations for this IC */
    u8                           nNumLEDs;      /**< The number of LEDs configured for this IC */
    LTDeviceI2C                * pDeviceI2C;    /**< The I2C device - used to communicate with the IC */
    LTDevicePins               * pDevicePins;   /**< The Pins device - used to enable/disable the device */
    LTDeviceUnit		         hI2C;          /**< Handle to the I2C device used to communicate with the IC */
    u8                           nI2CAddress;   /**< I2C address of the IC */
    u32                        * pRawColor;     /**< Buffer containing the raw IRGB color values for each LED */
    LTDeviceUnit                 hResetPin;     /**< Handle to the reset pin */
    ILTDriverPins_OutputBank   * iOutput;       /**< Output pin interface */
};

typedef enum {
    kIS31FL3208A_Register_Shutdown        = 0x00,
    kIS31FL3208A_Register_PWM_OUT1        = 0x01,
    kIS31FL3208A_Register_PWMUpdate       = 0x13,
    kIS31FL3208A_Register_LEDControl_OUT1 = 0x14,
    kIS31FL3208A_Register_Reset           = 0x2F,
} IS31FL3208A_register_address_value;

enum { kNumChannels  = 18 };

/***********************************************************************************************************************
 * Controller I/O particulars                                                                                          */

static bool LTChipIS31FL3208A_InitializeI2C(LTChipIS31FL3208A * pChip, u32 nI2CUnitNum) {
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

static void LTChipIS31FL3208A_FinalizeI2C(LTChipIS31FL3208A * pChip) {
    if (pChip->hI2C) {
        LT_GetCore()->DestroyHandle(pChip->hI2C);
        pChip->hI2C = 0;
    }
}

static bool LTChipIS31FL3208A_InitializePins(LTChipIS31FL3208A * pChip, char const * pResetPinName) {
    u32 nResetPinUnitNumber;
    if (!pChip->pDevicePins->GetUnitNumberFromBankName(pResetPinName, &nResetPinUnitNumber)) {
        LTLOG_YELLOWALERT("fail.pins.getunitnumber", "LTDevicePins bank name %s not found", pResetPinName);
        return false;
    }
    if (!(pChip->hResetPin = pChip->pDevicePins->CreateDeviceUnitHandle(nResetPinUnitNumber))) {
        LTLOG_REDALERT("fail.pins.handle", "Unable to obtain Device Unit handle for pin bank");
        return false;
    }
    if (!(pChip->iOutput = lt_gethandleinterface(ILTDriverPins_OutputBank, pChip->hResetPin))) {
        return false;
    }
    pChip->iOutput->Set(pChip->hResetPin, 1); /* Enable the controller */
    return true;
}

static void LTChipIS31FL3208A_FinalizePins(LTChipIS31FL3208A * pChip) {
    if (pChip->hResetPin) {
        if (pChip->iOutput) pChip->iOutput->Set(pChip->hResetPin, 0);
        LT_GetCore()->DestroyHandle(pChip->hResetPin);
        pChip->hResetPin = 0;
    }
    pChip->iOutput = NULL;
}


/***************************************************************************************************
   Chip register manipulation.

   NOTE: This function has the precondition that pChip is valid.                            */

static void WriteRegister(LTChipIS31FL3208A * pChip, u8 address, u8 value) {
    u8 packet[] = { address, value };
    bool bResult = pChip->pDeviceI2C->I2CMasterTransfer(pChip->hI2C, pChip->nI2CAddress, NULL, 0, packet, sizeof(packet), true, true, NULL, NULL);
    if (!bResult) {
        LTLOG("i2c.xfer.fail", "handle %lx",LT_PLT_HANDLE(pChip->hI2C));
    }
}

/***************************************************************************************************
   Interface functions.                                                                           */

LTChipIS31FL3208A * LTChipIS31FL3208A_Initialize(LTDeviceI2C * pDeviceI2C, LTDevicePins * pDevicePins, LTChipIS31FL3208AConfig const * pConfig) {
    if (!pConfig || !pDeviceI2C || !pDevicePins || !pConfig->pLEDConfigs || !pConfig->pResetPinName || pConfig->nNumLEDs > kNumChannels) return NULL;
    LTChipIS31FL3208A * pChip = lt_malloc(sizeof(LTChipIS31FL3208A));
    if (!pChip) return NULL;
    pChip->pDeviceI2C = pDeviceI2C;
    pChip->pDevicePins = pDevicePins;
    pChip->nI2CAddress = pConfig->nI2CAddress;
    pChip->nNumLEDs = pConfig->nNumLEDs;
    pChip->pLEDConfigs = pConfig->pLEDConfigs;
    pChip->pRawColor = lt_malloc(pConfig->nNumLEDs * sizeof(u32));
    if (!pChip->pRawColor) {
        LTChipIS31FL3208A_Finalize(pChip);
        return NULL;
    }
    lt_memset(pChip->pRawColor, 0, pConfig->nNumLEDs * sizeof(u32));
    if (!LTChipIS31FL3208A_InitializePins(pChip, pConfig->pResetPinName)) {
        LTChipIS31FL3208A_Finalize(pChip);
        return NULL;
    }
    if (!LTChipIS31FL3208A_InitializeI2C(pChip, pConfig->nI2CUnitNum)) {
        LTChipIS31FL3208A_Finalize(pChip);
        return NULL;
    }

    /* Reset and enable the controller */
    WriteRegister(pChip, kIS31FL3208A_Register_Reset, 0x00);
    WriteRegister(pChip, kIS31FL3208A_Register_Shutdown, 0x01);

    for (int i = 0; i < pChip->nNumLEDs; ++i) {
        LTChipIS31FL3208ALEDConfig * pLEDConfig = &pChip->pLEDConfigs[i];
        for (int j = 0; j < kLTChipIS31FL3208A_NumColorComponents; ++j) {
            u8 nChannelIndex = pLEDConfig->nChannelMap[j];
            if (nChannelIndex != 0) {
                /* Set LED max current */
                WriteRegister(pChip, kIS31FL3208A_Register_LEDControl_OUT1 + nChannelIndex - 1, 0x10);
            }
        }
    }

    return pChip;
}

void LTChipIS31FL3208A_Finalize(LTChipIS31FL3208A * pChip) {
    if (!pChip) return;
    LTChipIS31FL3208A_FinalizeI2C(pChip);
    LTChipIS31FL3208A_FinalizePins(pChip);
    if (pChip->pRawColor) lt_free(pChip->pRawColor);
    lt_free(pChip);
}

u32 LTChipIS31FL3208A_GetColor(LTChipIS31FL3208A * pChip, u8 nLEDIndex) {
    if (!pChip) return 0;
    if (nLEDIndex >= pChip->nNumLEDs) return 0;
    return pChip->pRawColor[nLEDIndex];
}

static void SetChannelLuminance(LTChipIS31FL3208A * pChip, u8 nChannel, u8 nLuminance) {
    if (!pChip) return;
    if (nChannel >= kNumChannels) return;
    WriteRegister(pChip, kIS31FL3208A_Register_PWM_OUT1 + nChannel, nLuminance);
}

void LTChipIS31FL3208A_SetColor(LTChipIS31FL3208A * pChip, u8 nLEDIndex, u32 nColor) {
    if (!pChip) return;
    if (nLEDIndex >= pChip->nNumLEDs) return;
    pChip->pRawColor[nLEDIndex] = nColor;
    LTChipIS31FL3208ALEDConfig * pLEDConfig = &pChip->pLEDConfigs[nLEDIndex];
    for (int i = 0; i < kLTChipIS31FL3208A_NumColorComponents; ++i) {
        u8 nChannelIndex = pLEDConfig->nChannelMap[i];
        if (nChannelIndex != 0) {
            u8 nColorComponent = (nColor >> (16 - (8*i))) & 0xFF;
            SetChannelLuminance(pChip, nChannelIndex - 1, nColorComponent);
        }
    }
}

void LTChipIS31FL3208A_Refresh(LTChipIS31FL3208A * pChip) {
    if (!pChip) return;
    WriteRegister(pChip, kIS31FL3208A_Register_PWMUpdate, 0x00);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  28-Oct-21   trajan      created
 */
