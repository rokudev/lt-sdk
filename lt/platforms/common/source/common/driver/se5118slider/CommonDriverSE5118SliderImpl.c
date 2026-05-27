/******************************************************************************
 * <common/source/common/driver/slider/CommonDriverSE5118SliderImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * SE5118 I2C slider driver
 *****************************************************************************/
/** @file CommonDriverSE5118SliderImpl.c Implementation of slider driver
 */

#include <lt/core/LTCore.h>
#include <lt/device/slider/LTDeviceSlider.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/pins/LTDevicePins.h>

LTLIBRARY_EXPORT_INTERFACES(CommonDriverSE5118Slider, (ILTDriverSlider))

DEFINE_LTLOG_SECTION("lt.drv.common.slider")

static ILTThread               * s_iThread         = NULL;

static LTDeviceI2C             * s_pDeviceI2C      = NULL;
static LTDevicePins            * s_pDevicePins     = NULL;

/* INM (Bit 2) is set to 0 after applying the enable mask for the IER config
 * register (0x01) to allow only one interrupt for one press.
 */
#define EN_INT_MASK  0xF9
#define DIS_INT_MASK 0x02

enum {
    /* slider registers */
    kSlider_Reg_Control    = 0x00,
    kSlider_Reg_IER_Config = 0x01,
    kSlider_Reg_Status     = 0x02,
    kSlider_Reg_IER        = 0x04,
    kSlider_Reg_KeyEnable  = 0x06,
    kSlider_Reg_MplKeys    = 0x08,
    kSlider_Reg_Key0Value  = 0x20,
    kSlider_Reg_BuzzPin1   = 0xF8,
    kSlider_Reg_BuzzPin2   = 0xF9,
    kSlider_Reg_BuzzPower2 = 0xFB,
    kSlider_Reg_PinSelect1 = 0xFC,
    kSlider_Reg_PinSelect2 = 0xFD
};

enum {
    /* values for control registers */
    Control_Normal_Mode     = 0x00,  /* for kSlider_Reg_Control */
    Control_Save_Params     = 0x08,  /* for kSlider_Reg_Control */
    Control_Params_Reset    = 0x40,  /* for kSlider_Reg_Control */
    Control_System_Reset    = 0x80,  /* for kSlider_Reg_Control */
    Control_Full_Reset      = 0xC0,  /* for kSlider_Reg_Control */
    IER_Int_Disable         = 0x02,  /* for kSlider_Reg_IER_Config */
    IER_SingleInt_NoCalib   = 0x00,  /* for kSlider_Reg_IER_Config */
    IER_MultiInt_NoCalib    = 0x04,  /* for kSlider_Reg_IER_Config */
    MplKeys_Disable         = 0x00,  /* for kSlider_Reg_MplKeys */
    MplKeys_Enable2         = 0x06,  /* for kSlider_Reg_MplKeys */
    MplKeys_Enable3         = 0x07,  /* for kSlider_Reg_MplKeys */
};

/******************************************************************************
 * Slider instance data
 */
typedef struct {
    LTMutex                        * mutex;
    /**< protects pCallback and pClientData */
    LTDriverSlider_ISRDispatchProc * pCallback;
    void                           * pClientData;
    const char                     * InterruptPinName;
    LTDeviceUnit                     hInterruptPinBank;
    LTDeviceUnit                     hI2C;
    /**< handle to the I2C unit */
    const u8                         Address;
    /**< I2C address of slider */
} SliderInstance;

#define SLIDER_INSTANCE(addr, pin_name) { \
    .mutex             = NULL,        \
    .pCallback         = NULL,     \
    .pClientData       = NULL,     \
    .InterruptPinName  = pin_name, \
    .hInterruptPinBank = 0,        \
    .hI2C              = 0,        \
    .Address           = addr,     \
},

static SliderInstance s_SliderInstance[] = {
#include LT_STRINGIFY(CONFIGURATION_FILE)
};
#undef SLIDER_INSTANCE

enum { kNumSliderDeviceUnits = sizeof(s_SliderInstance) / sizeof(SliderInstance) };

enum { kNumKeys = 8 };


/* Structure used for initializing a slider, with register addresses, register
 * values and the delays between I2C transmissions.
 */
typedef struct {
    u8 reg;
    u8 value;
    u16 delay;
} InitTable;

/******************************************************************************
 * Interrupt routine
 */
static void SliderISR(bool risingEdge, void * pISRData) {
    LT_UNUSED(risingEdge);

    SliderInstance * pInstance = (SliderInstance *)pISRData;
    if (pInstance->pCallback) {
        (*(pInstance->pCallback))(pInstance->pClientData);
    }
}

/******************************************************************************
 * I2C setup functions
 */
static bool InitializeI2C(LTDeviceUnit hI2C) {
    LTDeviceI2C_Capabilities caps;
    LTDeviceI2C_Configuration cfg;
    if (!(s_pDeviceI2C->GetDeviceCapabilities(hI2C, &caps))) {
        LTLOG("i2c.init.capabilities", "handle %lx\n", LT_PLT_HANDLE(hI2C));
        return false;
    }
    cfg.Frequency = caps.Freq_max;
    cfg.Master = true;
    cfg.Async = false;
    cfg.Dma = false;
    bool bDeviceConfigured = s_pDeviceI2C->SetDeviceConfiguration(hI2C, &cfg);
    if (!bDeviceConfigured) {
        LTLOG("i2c.init.config", "setting configuration failed\n");
    }
    return bDeviceConfigured;
}

static bool ProbeSliderInstance(LTDeviceUnit hI2C, const u8 addr) {
    bool bResult = InitializeI2C(hI2C);
    if (bResult) {
        bResult = s_pDeviceI2C->ProbeAddress(hI2C, addr);
        if (!bResult) {
            LTLOG("probe.slider", "no response at address 0x%x", addr);
        }
    }
    return bResult;
}

/******************************************************************************
 * Currently unused functions that might be useful later as the driver is
 * developed further. The qualifier "static inline" prevents the error for
 * defined but unused static functions.
 */

static inline bool DisableInt(SliderInstance * pInstance) {
    u8 transfer[2];
    u8 receive;
    transfer[0] = kSlider_Reg_IER_Config;
    if (!s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C, pInstance->Address,
        &receive, 1, transfer, 1, true, true, NULL, NULL))
        return false;
    transfer[1] = receive | DIS_INT_MASK;
    if (!s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C, pInstance->Address,
        NULL, 0, transfer, 2, true, true, NULL, NULL))
        return false;
    s_iThread->Sleep(LTTime_Milliseconds(100));
    return true;
}

static inline bool EnableInt(SliderInstance * pInstance) {
    u8 transfer[2];
    u8 receive;
    transfer[0] = kSlider_Reg_IER_Config;
    if (!s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C, pInstance->Address,
        &receive, 1, transfer, 1, true, true, NULL, NULL))
        return false;
    transfer[1] = receive & EN_INT_MASK;
    if (!s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C, pInstance->Address,
        NULL, 0, transfer, 2, true, true, NULL, NULL))
        return false;
    s_iThread->Sleep(LTTime_Milliseconds(100));
    return true;
}

static inline bool ParamsReset(LTDeviceUnit hI2C, u8 addr) {
    u8 transferByte[2];

    transferByte[0] = kSlider_Reg_Control;
    transferByte[1] = Control_Params_Reset;
    LTLOG("params.reset", "parameters reset code");
    bool res = s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, transferByte, 2, true,
                                               true, NULL, NULL);
    s_iThread->Sleep(LTTime_Seconds(2));
    return res;
}

static inline bool NormalMode(LTDeviceUnit hI2C, u8 addr) {
    u8 transferByte[2];

    transferByte[0] = kSlider_Reg_Control;
    transferByte[1] = Control_Normal_Mode;

    bool res = s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, transferByte, 2, true,
                                               true, NULL, NULL);
    s_iThread->Sleep(LTTime_Seconds(2));
    return res;
}

static inline bool TempChange(LTDeviceUnit hI2C, u8 addr) {
    u8 transferByte[2];

    /* Int disabled */
    transferByte[0] = kSlider_Reg_IER_Config;
    transferByte[1] = 0x2;
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, transferByte, 2, true, true, NULL,
                                    NULL);

    /* Enable P13 as INT */
    transferByte[0] = kSlider_Reg_PinSelect2;
    transferByte[1] = 0x1;
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, transferByte, 2, true, true, NULL, NULL);

    s_iThread->Sleep(LTTime_Seconds(1));

    /* Int enabled */
    transferByte[0] = kSlider_Reg_IER_Config;
    transferByte[1] = 0x0;
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, transferByte, 2, true, true, NULL,
                                    NULL);

    s_iThread->Sleep(LTTime_Seconds(1));

    /* Normal mode */
    transferByte[0] = kSlider_Reg_Control;
    transferByte[1] = Control_System_Reset;
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, transferByte, 2, true, true, NULL,
                                    NULL);

    s_iThread->Sleep(LTTime_Seconds(1));
    return true;
}

static inline bool P13StatusOK(SliderInstance * pInstance) {
    u8 receiveByte;
    u8 addr = pInstance->Address;
    LTDeviceUnit hI2C = pInstance->hI2C;
    u8 regs[] = {kSlider_Reg_BuzzPin2, kSlider_Reg_BuzzPower2, kSlider_Reg_PinSelect2};

    for (unsigned int i = 0; i < sizeof(regs) / sizeof(u8); i++) {
        bool bResult = s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, &regs[i], 1, true,
                                                       false, NULL, NULL);
        if (!bResult) {
            LTLOG_YELLOWALERT("slider.p13.write", "P13 %d reg address failed", i);
            return false;
        }
        bResult = s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, &receiveByte, 1, NULL, 0, true,
                                                  true, NULL, NULL);
        if (!bResult) {
            LTLOG_YELLOWALERT("slider.p13.read", "P13 %d read failed", i);
            return false;
        }
        if (regs[i] == kSlider_Reg_PinSelect2) {
            if (receiveByte != 1) return false;
        }
        else {
            if (receiveByte != 0) return false;
        }
    }
    return true;
}

/******************************************************************************
 * Initialization values and SaveRegs()
 */
static const InitTable s_iTable[] = {
    /* Disable INTs */
    {kSlider_Reg_IER_Config,   DIS_INT_MASK,           100},
    /* Enable interrupts for all keys */
    {kSlider_Reg_IER,          0xFF,                   100},
    /* Enable all keys */
    {kSlider_Reg_KeyEnable,    0xFF,                   100},
    /* Allow multiple keys pressed at once.
     * If this is not set, swipes are raising interrupt less often.
     */
    //{kSlider_Reg_MplKeys,      MplKeys_Enable3,        100},
    {kSlider_Reg_MplKeys,      MplKeys_Disable,        100},

    /* Auto clear interrupts */
    {0x09,                      0x08,                   100},

    /* Allow repeated interrupts */
    {0x0A,                      0x11,                   100},
    /* Changing gain in the first nibble impacts sensitivity. Anything other
     * than 1 generates interrupts without an obvious source.
     */
    {0x0E,                      0x1A,                   100},
    /* Enable Auto calibration */
    {0x11,                      0xFF,                   100},
    /* Set thresholds */
    //{0x30,                      0x04,                   100},
    //{0x31,                      0x04,                   100},
    //{0x32,                      0x04,                   100},
    //{0x33,                      0x04,                   100},
    //{0x34,                      0x04,                   100},
    //{0x35,                      0x04,                   100},
    //{0x36,                      0x04,                   100},
    //{0x37,                      0x04,                   100},
    /* Disable shield driver */
    {0x7C,                      0x00,                   100},
    /* Disable buzzer pin select for keys */
    {0xF8,                      0x00,                   100},
    /* Disable P13 as buzzer power */
    {kSlider_Reg_BuzzPower2,   0x00,                   100},
    /* Disable keys as INT pins */
    {kSlider_Reg_PinSelect1,   0x00,                   100},
    /* Enable P13 as INT */
    {kSlider_Reg_PinSelect2,   0x01,                   500},
    /* Enable INTs */
    {kSlider_Reg_IER_Config,   IER_MultiInt_NoCalib,   500},
    /* Save mode */
    {kSlider_Reg_Control,      Control_Save_Params,    500},
    /* Enable INTs */
    {kSlider_Reg_IER_Config,   0x0C,                   500}
};

static bool SaveRegs(LTDeviceUnit hDevice, u8 addr) {
    u8 transferByte[2];
    bool bResult = false;

    for (u32 i = 0; i < sizeof(s_iTable) / sizeof(s_iTable[0]); i++) {
        transferByte[0] = s_iTable[i].reg;
        transferByte[1] = s_iTable[i].value;
        bResult = s_pDeviceI2C->I2CMasterTransfer(hDevice, addr, NULL, 0, transferByte, 2,
                                                  true, true, NULL, NULL);
        if(!bResult) return false;
        s_iThread->Sleep(LTTime_Milliseconds(s_iTable[i].delay));
    }
    return bResult;
}

/* This function resets the controller but also shuts it down. After being used
 * once, the target must be rebooted and the image that doesn't call this
 * function must be flashed to the target.
 */
static bool SystemReset(LTDeviceUnit hI2C, const u8 addr) {
    bool bResult;
    u8 transferByte[2];

    transferByte[0] = kSlider_Reg_Control;
    transferByte[1] = Control_System_Reset;
    bResult = s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, transferByte, 2, true,
                                              true, NULL, NULL);
    if(!bResult) return false;

    s_iThread->Sleep(LTTime_Milliseconds(500));

    transferByte[0] = kSlider_Reg_Control;
    transferByte[1] = Control_Normal_Mode;
    bResult = s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, NULL, 0, transferByte, 2, true,
                                              true, NULL, NULL);

    if (!bResult) {
        return false;
    }
    else {
        s_iThread->Sleep(LTTime_Milliseconds(500));
        return true;
    }
}

/******************************************************************************
 * Debug functions
 */
static void DisplayCalibrationRegs(SliderInstance * pInstance) {
    u8 receiveByte[2];
    u8 reg = kSlider_Reg_IER_Config;
    u8 addr = pInstance->Address;
    LTDeviceUnit hI2C = pInstance->hI2C;
    LTLOG("calibration", "Calibration registers");
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, receiveByte, 1, &reg, 1, true, true, NULL, NULL);
    LTLOG("calibration.mden", "    MDEN bit %d", (receiveByte[0] & (u8)0x08) >> 3);
    reg = 0x10; /* calibration configure register */
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, receiveByte, 1, &reg, 1, true, true, NULL, NULL);
    LTLOG("calibration.config", "    Calibration config reg 0x%x=0x%x", reg, receiveByte[0]);
    reg = 0x11; /* key calibration register */
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, receiveByte, 1, &reg, 1, true, true, NULL, NULL);
    LTLOG("calibration.enable", "    Key Calibration enable 0x%x=0x%x", reg, receiveByte[0]);
    reg = 0x13; /* noise registers */
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, receiveByte, 2, &reg, 1, true, true, NULL, NULL);
    LTLOG("calibration.threshold", "    Noise threshold 0x%x=0x%x", reg, receiveByte[0]);
    LTLOG("calibration.indication", "    Noise indication 0x%x=0x%x", reg++, receiveByte[1]);
    reg = 0x17; /* negative threshold */
    s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, receiveByte, 1, &reg, 1, true, true, NULL, NULL);
    LTLOG("calibration.neg.threshold", "    Negative threshold 0x%x=0x%x",
          reg, receiveByte[0]);

    reg = 0x40;
    while (reg < 0x50) {
        s_pDeviceI2C->I2CMasterTransfer(hI2C, addr, receiveByte, 2, &reg, 1, true, true, NULL, NULL);
        LTLOG("    calibration.key", "    Calibration for key 0x%x=0x%x 0x%x",
              (reg - 0x40) >> 1, receiveByte[1], receiveByte[0]);
        reg += 2;
    }
}

static void DisplayKeyCounts(SliderInstance * pInstance) {
    u8 reg = kSlider_Reg_Key0Value; /* key count registers */
    u8 receive[kNumKeys];

    LTLOG("key.counts.header", "Key counts");
    s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C, pInstance->Address, receive, kNumKeys, &reg, 1, true, true, NULL, NULL);
    for (u8 i = 0; i < kNumKeys; i++) {
        /* MSB is the sign, and the rest are the value */
        LTLOG("key.counts", "    key %d value %c%d", i, (receive[i] | 0x80) ? '-' : ' ',
              receive[i] & 0x7F);
    }
}

static u8 ReadRegister(SliderInstance * pInstance, u8 reg) {
    u8 receiveByte;
    bool bResult = s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C, pInstance->Address, &receiveByte, 1, &reg, 1, true, true, NULL, NULL);
    if (!bResult) {
        LTLOG_REDALERT("slider.read", "read from register 0x%x failed", reg);
        return 0;
    }
    return receiveByte;
}

/* Helper function that retrieves a pointer to the Instance data from the Device Unit handle.
 * Return 0 if either the handle or the private data pointer is invalid.
 */
static SliderInstance * InstanceFromHandle(LTDeviceUnit hDevice) {
    SliderInstance ** ppInstance = (SliderInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    SliderInstance * pInstance = NULL;
    if (ppInstance) {
        pInstance = *ppInstance;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
    }
    return pInstance;
}

static inline void Debug(SliderInstance * pInstance) {
    if (!pInstance) return;

    u8 receiveByte;
    u8 regs[] = {
        kSlider_Reg_IER_Config, kSlider_Reg_Status, kSlider_Reg_IER, kSlider_Reg_KeyEnable, kSlider_Reg_MplKeys, 0x9, 0x0A, 0x0C, 0x0E, 0x0F, 0x1D, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x7C, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD
    };

    for (u8 i = 0; i < sizeof(regs) / sizeof(u8); i++) {
        bool bResult = s_pDeviceI2C->I2CMasterTransfer(pInstance->hI2C, pInstance->Address, &receiveByte, 1, &regs[i], 1, true, true, NULL, NULL);
        if (!bResult) LTLOG_DEBUG("slider.debug.fail", "I2C read from 0x%x failed", regs[i]);
        LTLOG("slider.debug", "reg 0x%x value = 0x%x", regs[i], receiveByte);
    }
    DisplayKeyCounts(pInstance);
    DisplayCalibrationRegs(pInstance);
}

static u32 ILTDriverSlider_GetKeyCount(LTDeviceUnit hDevice) {
    SliderInstance * pInstance = InstanceFromHandle(hDevice);
    if (pInstance) {
        return kNumKeys;
    }
    else {
        return 0;
    }
}

/******************************************************************************
 * ILTDriverSlider interface functions
 */
static u32 ILTDriverSlider_ReadValue(LTDeviceUnit hDevice) {
    SliderInstance * pInstance = InstanceFromHandle(hDevice);
    if (!pInstance) return LT_U32_MAX;

    return (u32)ReadRegister(pInstance, kSlider_Reg_Status);
}

/* Only one device library should control this driver, so if the device library
 * calls this function with a handle that points to a device that already has
 * a callback, the old callback is simply replaced with a new one.
 */
static void ILTDriverSlider_SetISRDispatchProc(LTDeviceUnit hDevice,
    LTDriverSlider_ISRDispatchProc * pCallback, void * pClientData) {

    SliderInstance * pInstance = InstanceFromHandle(hDevice);
    if (pInstance) {
        pInstance->mutex->API->Lock(pInstance->mutex);
        pInstance->pCallback = pCallback;
        if (!pCallback) {
            pInstance->pClientData = NULL;
        }
        else {
            pInstance->pClientData = pClientData;
        }
        pInstance->mutex->API->Unlock(pInstance->mutex);
    }
}

static void ILTDriverSliderImpl_OnDestroyHandle(LTHandle hDevice) {
    SliderInstance * pInstance = InstanceFromHandle(hDevice);

    if (pInstance) {
        pInstance->mutex->API->Lock(pInstance->mutex);
        pInstance->pCallback = NULL;
        pInstance->pClientData = NULL;
        pInstance->mutex->API->Unlock(pInstance->mutex);
    }
}

define_LTLIBRARY_INTERFACE(ILTDriverSlider, ILTDriverSliderImpl_OnDestroyHandle) {
    .GetKeyCount        = ILTDriverSlider_GetKeyCount,
    .ReadValue          = ILTDriverSlider_ReadValue,
    .SetISRDispatchProc = ILTDriverSlider_SetISRDispatchProc,
} LTLIBRARY_DEFINITION

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceSlider, CommonDriverSE5118Slider)

/******************************************************************************
 * Library initialization and deinitialization:
 */
static void CommonDriverSE5118SliderImpl_LibFini(void) {
    SliderInstance * pInstance = s_SliderInstance;
    for (u32 nSlider = kNumSliderDeviceUnits; nSlider--; ++pInstance) {
        if (pInstance->hI2C) {
            LT_GetCore()->DestroyHandle(pInstance->hI2C);
            pInstance->hI2C = 0;
        }
        if (pInstance->hInterruptPinBank) {
            LTLOG_DEBUG("lib.exit", "disabling interrupt for I2C device %x", pInstance->Address);
            ILTDriverPins_InputBank * s_pInputBank = lt_gethandleinterface(ILTDriverPins_InputBank, pInstance->hInterruptPinBank);
            /* Check if we can call DisableIRQ for the shared interrupt line */
            if (s_pInputBank) s_pInputBank->DisableIRQ(pInstance->hInterruptPinBank);
            LT_GetCore()->DestroyHandle(pInstance->hInterruptPinBank);
            pInstance->hInterruptPinBank = 0;
        }
        if (pInstance->mutex) {
            lt_destroyobject(pInstance->mutex);
            pInstance->mutex = NULL;
        }
    }

    if (s_pDeviceI2C) {
        LTLOG_DEBUG("library.close.i2c", "closing I2C library");
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pDeviceI2C);
        s_pDeviceI2C = NULL;
    }

    if (s_pDevicePins) {
        LTLOG_DEBUG("library.close.pins", "closing pins library");
        LT_GetCore()->CloseLibrary((LTLibrary *)s_pDevicePins);
    }
    s_iThread = NULL;
    return;
}

static bool CommonDriverSE5118Slider_InitGPIO(SliderInstance * pInstance) {
    LTDevicePin_PinType PinType = kLTDevicePin_PinType_Invalid;
    u32 nBankNumber = LT_U32_MAX;
    if (!s_pDevicePins->GetUnitNumberFromBankName(pInstance->InterruptPinName, &nBankNumber)) {
        LTLOG_REDALERT("init.gpio.number", "Unable to obtain bank number for %s", pInstance->InterruptPinName);
        return false;
    }

    if (!s_pDevicePins->GetBankTypeFromUnitNumber(nBankNumber, &PinType)) {
        LTLOG_REDALERT("init.gpio.type", "Unable to obtain bank type");
        return false;
    }
    if (PinType != kLTDevicePin_PinType_Input) {
        LTLOG_REDALERT("init.gpio.invalid.type", "Invalid bank type");
        return false;
    }

    pInstance->hInterruptPinBank = s_pDevicePins->CreateDeviceUnitHandle(nBankNumber);
    if (!(pInstance->hInterruptPinBank)) {
        LTLOG_REDALERT("init.gpio.bank.handle", "Can't create bank handle");
        return false;
    }

    ILTDriverPins_InputBank * s_pInputBank = lt_gethandleinterface(ILTDriverPins_InputBank, pInstance->hInterruptPinBank);
    if (!s_pInputBank) {
        LTLOG_REDALERT( "init.gpio.interface", "Can't open interface");
        LT_GetCore()->DestroyHandle(pInstance->hInterruptPinBank);
        pInstance->hInterruptPinBank = 0;
        return false;
    }

    s_pInputBank->ConfigurePullType(pInstance->hInterruptPinBank,
                                    kLTDevicePin_PinConfiguration_PullType_PullUp);

    s_pInputBank->EnableIRQ(pInstance->hInterruptPinBank,
                            kLTDevicePin_PinConfiguration_Trigger_FallingEdge,
                            LTTime_Zero(), SliderISR, (void *)pInstance);
    return true;
}

static bool CommonDriverSE5118Slider_InitI2C(SliderInstance * pInstance, u32 nNumI2CUnits) {
    for (u32 n = 0; n < nNumI2CUnits; n++) {
        LTDeviceUnit hI2Cloc = s_pDeviceI2C->CreateDeviceUnitHandle(n);
        if (!hI2Cloc) continue;
        if (ProbeSliderInstance(hI2Cloc, pInstance->Address)) {
            bool res = 1 || SystemReset(hI2Cloc, pInstance->Address);
            if (!res) {
                LTLOG_YELLOWALERT("i2c.init.reset", "Controller reset failed");
                LT_GetCore()->DestroyHandle(hI2Cloc);
                continue;
            }
            res = SaveRegs(hI2Cloc, pInstance->Address);
            if (!res) {
                LTLOG_YELLOWALERT("i2c.init.config", "Controller config failed");
                LT_GetCore()->DestroyHandle(hI2Cloc);
                continue;
            }
            else {
                /* the instance can be used now in a separate thread */
                pInstance->hI2C = hI2Cloc;
                return true;
            }
        }
    }
    return false;
}

static bool CommonDriverSE5118SliderImpl_LibInit(void) {
    s_iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    s_pDeviceI2C = (LTDeviceI2C *)LT_GetCore()->OpenLibrary("LTDeviceI2C");
    if (!s_pDeviceI2C) {
        LTLOG_REDALERT("lib.init.i2c", "Could not open LTDeviceI2C");
        CommonDriverSE5118SliderImpl_LibFini();
        return false;
    }

    u32 nNumI2CUnits = s_pDeviceI2C->GetNumDeviceUnits();
    if (nNumI2CUnits == 0) {
        LTLOG_REDALERT("lib.init.units", "LTDeviceI2C provides no Device Units");
        CommonDriverSE5118SliderImpl_LibFini();
        return false;
    }

    s_pDevicePins = (LTDevicePins *)LT_GetCore()->OpenLibrary("LTDevicePins");
    if (!s_pDevicePins) {
        LTLOG_REDALERT("lib.init.pins", "Could not open LTDevicePins");
        CommonDriverSE5118SliderImpl_LibFini();
        return false;
    }

    SliderInstance * pInstance = s_SliderInstance;

    for (u32 nSlider = kNumSliderDeviceUnits; nSlider--; ++pInstance) {
        if (CommonDriverSE5118Slider_InitI2C(pInstance, nNumI2CUnits)
            && CommonDriverSE5118Slider_InitGPIO(pInstance)
            && (pInstance->mutex = lt_createobject(LTMutex))) {
#ifdef LT_DEBUG
            Debug(pInstance);
#endif

        }
        else {
            /* As soon as we find a unit that can't be initialized, we bail.
             * LibFini() will destroy any I2C, PinBank or Mutex handle that is still
             * outstanding in the current or previously initialized SliderInstances.
             */
        }
    }

    if (kNumSliderDeviceUnits == 0) {
        CommonDriverSE5118SliderImpl_LibFini();
        return false;
    }

    return true;
}

/******************************************************************************
 * Provide a Device Unit handle. The handle's private data is a pointer to the
 * corresponding instance. ID can be found if needed, as
 * handle->privateData - &s_SliderInstance[0].
 * No initialization is done here. All instances are already initialized at the
 * library init time.
 */
static LTDeviceUnit CommonDriverSE5118SliderImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDevice = 0;
    if (nDeviceUnitNumber < kNumSliderDeviceUnits) {
        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverSlider,
                                             sizeof(SliderInstance *));
        if (hDevice) {
            SliderInstance ** ppInstance =
                (SliderInstance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
            if (!ppInstance) {
                LT_GetCore()->DestroyHandle(hDevice);
            }
            else {
                *ppInstance = &(s_SliderInstance[nDeviceUnitNumber]);
                LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
            }

            /* Check that this unit was successfully initialized */
            if ((*ppInstance)->hI2C == 0 || (*ppInstance)->hInterruptPinBank == 0) {
                LT_GetCore()->DestroyHandle(hDevice);
                hDevice = 0;
            }
        }
    }

    return hDevice;
}

/* If any of the units are not initialized correctly, we would have failed in LibInit,
 * so the value returned here is equal to the number of units specified by
 * kNumSliderDeviceUnits
 */
static u32 CommonDriverSE5118SliderImpl_GetNumDeviceUnits(void) {
    return kNumSliderDeviceUnits;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  07-Sep-21   commodus    created
 */
