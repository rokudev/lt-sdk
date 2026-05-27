/*******************************************************************************
 * platforms/common/source/common/driver/i2cbitbang/CommonDriverI2CBitbangImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 *  Common LT Driver Library for I2C bitbanging
 *
 ******************************************************************************/
/** @file CommonDriverI2CBitbangImpl.c Implementation of I2C bitbang driver */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/device/pins/LTDevicePins.h>


DEFINE_LTLOG_SECTION("lt.drv.bitbang.i2c");

/*******************************************************************************
 * I2C descriptions                                                   */

static ILTDriverPins_BidirectionalBank          * s_pIBiDirBank   = NULL;
static LTDevicePins                             * s_pDevicePins   = NULL;

typedef struct {
    LTMutex                    *mutex;                    /**< Mutex protection, mainly for reference count     */
    u32                         nRefCount;                /**< How many clients have a LTHandle to this Group   */
    const char                 *name;                     /**< Name of the I2C instance                         */
    char const *                pinNameSda;
    LTDeviceUnit                hSdaBank;
    char const *                pinNameScl;
    LTDeviceUnit                hSclBank;
    LTDeviceI2C_Configuration   cfg;
    LTDeviceI2C_Capabilities    caps;
} I2C_Instance;

static struct statics {
    I2C_Instance               *pI2C_Instance;
    u32                         NumI2CConnections;
} S;

/********************************************************************************************************************************
 * Platform-specific I2C initialization and input                                                                             */

static bool InitializeI2C(I2C_Instance *pInstI2C) {
    // Initialize GPIO pins via LT Device interface
    /* SCL is always output on I2C master */
    u32 nBankNumber = LT_U32_MAX;
    LTDevicePin_PinType PinType = kLTDevicePin_PinType_Invalid;
    if (pInstI2C->hSdaBank && pInstI2C->hSclBank) {
        return true;
    }

    if (!(pInstI2C->mutex = lt_createobject(LTMutex))) {
        LTLOG("lib.init.err", "mutex init fail");
        return false;
    }

    if (!s_pDevicePins->GetUnitNumberFromBankName(pInstI2C->pinNameScl, &nBankNumber)) {
        LTLOG_REDALERT( "fail.no.bank.number",
                                       "Unable to obtain bank number for SCL");
        return false;
    }
    if (!s_pDevicePins->GetBankTypeFromUnitNumber(nBankNumber, &PinType)) {
        LTLOG_REDALERT( "fail.no.bank.type",
                                       "Unable to obtain bank type for SCL");
        return false;
    }
    if (PinType != kLTDevicePin_PinType_Bidirectional) {
        LTLOG_REDALERT( "fail.inval.bank.type",
                                       "Invalid bank type for SCL");
        return false;
    }
    pInstI2C->hSclBank = s_pDevicePins ? s_pDevicePins->CreateDeviceUnitHandle(nBankNumber) : 0;

    s_pIBiDirBank = pInstI2C->hSclBank ? lt_gethandleinterface(ILTDriverPins_BidirectionalBank, pInstI2C->hSclBank) : 0;

    /* SDA is sometimes input/sometimes output */
    if (!s_pDevicePins->GetUnitNumberFromBankName(pInstI2C->pinNameSda, &nBankNumber)) {
        LTLOG_REDALERT( "fail.no.bank.number",
                                       "Unable to obtain bank number for SDA");
        LT_GetCore()->DestroyHandle(pInstI2C->hSclBank);
        return false;
    }
    if (!s_pDevicePins->GetBankTypeFromUnitNumber(nBankNumber, &PinType)) {
        LTLOG_REDALERT( "fail.no.bank.type",
                                       "Unable to obtain bank type for SDA");
        LT_GetCore()->DestroyHandle(pInstI2C->hSclBank);
        return false;
    }
    if (PinType != kLTDevicePin_PinType_Bidirectional) {
        LTLOG_REDALERT( "fail.inval.bank.type",
                                       "Invalid bank type for SDA");
        LT_GetCore()->DestroyHandle(pInstI2C->hSclBank);
        return false;
    }
    pInstI2C->hSdaBank = s_pDevicePins ? s_pDevicePins->CreateDeviceUnitHandle(nBankNumber) : 0;

    if (s_pIBiDirBank) {
        s_pIBiDirBank->ConfigureAsOutput(pInstI2C->hSclBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
        s_pIBiDirBank->ConfigureAsOutput(pInstI2C->hSdaBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
        s_pIBiDirBank->Set(pInstI2C->hSdaBank, 1);
    } else {
        LT_GetCore()->DestroyHandle(pInstI2C->hSdaBank);
        LT_GetCore()->DestroyHandle(pInstI2C->hSclBank);
        return false;
    }
    return true;
}

static void ShutdownI2C(I2C_Instance *pInstI2C) {
    LT_GetCore()->DestroyHandle(pInstI2C->hSdaBank);
    LT_GetCore()->DestroyHandle(pInstI2C->hSclBank);
    lt_destroyobject(pInstI2C->mutex);
    return;
}

/* Retrieve a pointer to the Group Instance data from the Device Unit handle.
 * Return 0 if the handle or the private data pointer are invalid. */
static I2C_Instance * InstanceFromHandle(LTDeviceUnit hDevice) {
    I2C_Instance ** ppInstance = (I2C_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    I2C_Instance * pInstance = NULL;
    if (ppInstance) {
        pInstance = *ppInstance;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
    }
    return pInstance;
}

static bool ConfigureDeviceUnit(LTDeviceConfig *pDeviceConfig, I2C_Instance *instance, u32 deviceUnitSection) {
    if (!(instance->name = pDeviceConfig->ReadString(deviceUnitSection, "name"))) {
        LTLOG_YELLOWALERT("cdu.name", NULL);
        return false;
    }

    instance->pinNameScl = pDeviceConfig->ReadString(deviceUnitSection, "scl");
    if (!instance->pinNameScl) {
        LTLOG_YELLOWALERT("cdu.scl.err", NULL);
        return false;
    }
    
    instance->pinNameSda = pDeviceConfig->ReadString(deviceUnitSection, "sda");
    if (!instance->pinNameSda) {
        LTLOG_YELLOWALERT("cdu.sda.err", NULL);
        return false;
    }

    instance->caps.Freq_min = 1;
    instance->caps.Freq_max = 100000;
    instance->caps.Caps_mask = (kLTDeviceI2C_Capability_Master | kLTDeviceI2C_Capability_BitBang);

    return InitializeI2C(instance);
}

static bool ConfigureDeviceUnits(void) {
    bool ret = false;
    if (S.NumI2CConnections || S.pI2C_Instance) return false;   /* already configured - do not allocate and configure again */
    LTDeviceConfig *pDeviceConfig = lt_openlibrary(LTDeviceConfig);

    if (!pDeviceConfig) {
        LTLOG_REDALERT("cdus.config", NULL);
        return false;
    }

    do {
        u32 driverSection = pDeviceConfig->GetDriverSection("LTDeviceI2C", "CommonDriverI2CBitbang");
        u32 initialized;
        S.NumI2CConnections = pDeviceConfig->GetNumDeviceUnits(driverSection);
        if (S.NumI2CConnections == 0) {
            ret = true;
            break;
        }

        if (!(S.pI2C_Instance = lt_malloc(S.NumI2CConnections * sizeof(I2C_Instance)))) {
            LTLOG_REDALERT("cdus.oom", NULL);
            break;
        }
        lt_memset(S.pI2C_Instance, 0, S.NumI2CConnections * sizeof(I2C_Instance));

        for (initialized = 0; initialized < S.NumI2CConnections; initialized++) {
            I2C_Instance *instance = &S.pI2C_Instance[initialized];
            u32 deviceUnitSection = pDeviceConfig->GetDeviceUnitSectionAt(driverSection, initialized);
            if (!deviceUnitSection) {
                LTLOG_YELLOWALERT("cdus.no", NULL);
                break;
            }

            if (!ConfigureDeviceUnit(pDeviceConfig, instance, deviceUnitSection)) {
                LTLOG_REDALERT("cdus.err", "Failed to configure device unit %lu", LT_Pu32(initialized));
                break;
            }
        }
        if (initialized == S.NumI2CConnections) {
            ret = true;
        }
    } while (0);
    lt_closelibrary(pDeviceConfig);
    return ret;
}


static bool Shutdown(void) {
    for (u32 i = 0; i < S.NumI2CConnections; ++i) {
        ShutdownI2C(&S.pI2C_Instance[i]);
    }
    lt_free(S.pI2C_Instance);
    S.NumI2CConnections = 0;
    return true;
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(ILTDriverI2C, CommonDriverI2CBitbang);
/********************************************************************************************************************************
 * Library initialization and deinitialization                                                                                 */
static bool CommonDriverI2CBitbangImpl_LibInit(void) {
    S = (struct statics) {0};

    s_pDevicePins = (LTDevicePins *)LT_GetCore()->OpenLibrary("LTDevicePins");
    if (!s_pDevicePins) return false;
    if (!ConfigureDeviceUnits()) {
        Shutdown();
        LTLOG("lib.ConfigureDeviceUnits", "fail");
        return false;
    }
    return true;
}

static void CommonDriverI2CBitbangImpl_LibFini(void) {
    Shutdown();
}

static ILTDriverI2C s_ILTDriverI2C;

/********************************************************************************************************************************
 * Device-unit creation interface.
 */
static u32 CommonDriverI2CBitbangImpl_GetNumDeviceUnits(void) { return S.NumI2CConnections; }

static u32 CommonDriverI2CBitbangImpl_GetBusIndexFromName(const char *busName) {
    for (u32 i = 0; i < S.NumI2CConnections; ++i) {
        if (lt_strcmp(busName, S.pI2C_Instance[i].name) == 0) {
            return i;
        }
    }
    return LT_U32_MAX;
}

static LTDeviceUnit CommonDriverI2CBitbangImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LTDeviceUnit hDevice = 0;
    if (nDeviceUnitNumber < S.NumI2CConnections) {
        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverI2C, sizeof(I2C_Instance *));
    }
    if (hDevice) {
        bool bInterfaceOK = false;  /* A handle has been created.  Do not leak it if something goes
                                       wrong with preparing the handle or initializing the interface. */
        I2C_Instance ** ppInstance = (I2C_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
        if (ppInstance) {
            I2C_Instance * pInstance = *ppInstance = &S.pI2C_Instance[nDeviceUnitNumber];
            bInterfaceOK = true;    /* all okay, unless first-reference initialization (below) fails */
            pInstance->mutex->API->Lock(pInstance->mutex);
            if (++pInstance->nRefCount == 1) {   /* Just starting up this instance. */
                bInterfaceOK = InitializeI2C(pInstance);
            }
            pInstance->mutex->API->Unlock(pInstance->mutex);
            LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
        }
        if (!bInterfaceOK) {
            LT_GetCore()->DestroyHandle(hDevice);
            hDevice = 0;
        }
    }
    return hDevice;
}

static bool CommonDriverI2CBitbangImpl_GetDeviceCapabilities(LTDeviceUnit unit, LTDeviceI2C_Capabilities* pCaps) {
    I2C_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance || !pCaps) {
        LTLOG("i2c.init.err.caps", "handle %lx pInstance %p pCaps %p", LT_PLT_HANDLE(unit), pInstance, pCaps);
        return false;
    }
    lt_memcpy(pCaps,&pInstance->caps, sizeof(LTDeviceI2C_Capabilities));
    return true;
}

static bool CommonDriverI2CBitbangImpl_GetDeviceConfiguration(LTDeviceUnit unit, LTDeviceI2C_Configuration* pI2CConfig) {
    I2C_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance || !pI2CConfig) return false;
    lt_memcpy(pI2CConfig, &pInstance->cfg, sizeof(LTDeviceI2C_Configuration));
    return true;
}

static bool CommonDriverI2CBitbangImpl_SetDeviceConfiguration(LTDeviceUnit unit, const LTDeviceI2C_Configuration* pI2CConfig) {
    I2C_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance || !pI2CConfig) {
        LTLOG("cfg.instance.fail", "error");
        return false;
    }
    if (!I2C_CONFIG_CAPS_OK(pI2CConfig, &(pInstance->caps))) {
        LTLOG("cfg.check.fail", "error");
        return false;
    }
    lt_memcpy(&pInstance->cfg, pI2CConfig, sizeof(LTDeviceI2C_Configuration));
    return true;
}

/* START_BIT        delay 6us */
/* STOP_BIT         delay 6us */
/* TSU_DAT          delay 1us */
/* CLOCK_HIGH_LEVEL delay 9us */
/* CLOCK_LOW_LEVEL  delay 3us */

#define START_BIT 6
#define STOP_BIT 6
#define TSU_DAT 1
#define CLOCK_HIGH_LEVEL 9
#define CLOCK_LOW_LEVEL 3
#define LOW 0
#define HIGH 1

/* TODO: make a better busy uDelay function */
#define NUM_NOPS 10
static void I2CusDelayBusy(u32 uSec) {
    LTTime target = LTTime_Add(LT_GetCore()->GetKernelTime(), LTTime_Microseconds(uSec));
    LTTime current = LTTime_Microseconds(0);
    do {
        u32 i = NUM_NOPS;
        while (i) {
            i--;
            asm volatile ("nop\n\t"); \
        }
        current = LT_GetCore()->GetKernelTime();
    }while (LTTime_IsLessThan(current, target));
    return;
}

#define DELAY_I2C_BITBANG_US(us) I2CusDelayBusy(us)
#define SDA_CONFIG_OUTPUT(pInst) s_pIBiDirBank->ConfigureAsOutput((pInst)->hSdaBank, kLTDevicePin_PinConfiguration_OutputType_PushPull)
#define SDA_CONFIG_INPUT(pInst)  s_pIBiDirBank->ConfigureAsInput((pInst)->hSdaBank, kLTDevicePin_PinConfiguration_PullType_PullUp)
#define SDA_SET(pInst,val) do { SDA_CONFIG_OUTPUT(pInst); s_pIBiDirBank->Set((pInst)->hSdaBank, val);}while (0)
#define SCL_SET(pInst,val) s_pIBiDirBank->Set((pInst)->hSclBank, val)
#define SDA_GET(pInst) s_pIBiDirBank->Read((pInst)->hSdaBank)

static void CommonDriverI2CBitbangImpl_SendStartBit(I2C_Instance * pInstance) {
    SDA_SET(pInstance,HIGH);
    DELAY_I2C_BITBANG_US(START_BIT);
    SCL_SET(pInstance,HIGH);
    DELAY_I2C_BITBANG_US(START_BIT);
    SDA_SET(pInstance,LOW);
    DELAY_I2C_BITBANG_US(START_BIT);
    SCL_SET(pInstance,LOW);
    DELAY_I2C_BITBANG_US(START_BIT);
}

static void CommonDriverI2CBitbangImpl_SendStopBit(I2C_Instance * pInstance) {
    SCL_SET(pInstance,LOW);
    DELAY_I2C_BITBANG_US(STOP_BIT);
    SDA_SET(pInstance,LOW);
    DELAY_I2C_BITBANG_US(STOP_BIT);
    SCL_SET(pInstance,HIGH);
    DELAY_I2C_BITBANG_US(STOP_BIT);
    SDA_SET(pInstance,HIGH);
}

static void CommonDriverI2CBitbangImpl_SendBit(I2C_Instance * pInstance, u8 bit) {
    if(bit) {
        SDA_SET(pInstance,HIGH);
    } else {
        SDA_SET(pInstance,LOW);
    }
    /* Tsu_dat - 2050ns of more */
    DELAY_I2C_BITBANG_US(TSU_DAT);
    SCL_SET(pInstance,HIGH);
    DELAY_I2C_BITBANG_US(CLOCK_HIGH_LEVEL);
    SCL_SET(pInstance,LOW);
    DELAY_I2C_BITBANG_US(CLOCK_LOW_LEVEL);
}

static u8 CommonDriverI2CBitbangImpl_ReceiveBit(I2C_Instance * pInstance) {
    u8 bit;
    SDA_CONFIG_INPUT(pInstance);
    SCL_SET(pInstance,HIGH);
    DELAY_I2C_BITBANG_US(CLOCK_HIGH_LEVEL);
    if (SDA_GET(pInstance)) {
        bit = 1;
    } else {
        bit = 0;
    }
    SCL_SET(pInstance,LOW);
    DELAY_I2C_BITBANG_US(CLOCK_LOW_LEVEL);
    return bit;
}

static u8 CommonDriverI2CBitbangImpl_ReceiveByte(I2C_Instance * pInstance, bool AckBit) {
    u8 byte = 0;
    for (u8 bit = 1 << 7; bit; bit >>= 1) {
        if (CommonDriverI2CBitbangImpl_ReceiveBit(pInstance)) {
            byte |= bit;
        }
    }
    CommonDriverI2CBitbangImpl_SendBit(pInstance, ((AckBit)?(1):(0)));
    return byte;
}

static bool CommonDriverI2CBitbangImpl_SendByte(I2C_Instance * pInstance, u8 byte) {
    for (u8 bit = 1 << 7; bit; bit >>= 1) {
        CommonDriverI2CBitbangImpl_SendBit(pInstance, ((byte & bit)?(1):(0)));
    }
    /* Read ACK bit */
    return CommonDriverI2CBitbangImpl_ReceiveBit(pInstance);
}

static void CommonDriverI2CBitbangImpl_SetTransferTimeout(LTDeviceUnit unit, LTTime timeout) {
    LT_UNUSED(unit);
    LT_UNUSED(timeout);
    // this device has no loops for waiting on registers/data, so no timeout is needed
}

static bool CommonDriverI2CBitbangImpl_I2CMasterTransfer(LTDeviceUnit unit,
    u8 addr, void * rx_buffer, u32 rx_len, const void * tx_buffer, u32 tx_len,
    bool issue_start, bool issue_stop,
    LTI2C_I2CMasterTransferStatusCallback * pCallback, void * pClientData) {

    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);
    I2C_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance || (!rx_len && !tx_len) || (!rx_buffer && !tx_buffer)) return false;

    if (tx_buffer) {
        if (issue_start) CommonDriverI2CBitbangImpl_SendStartBit(pInstance);
        bool nack = CommonDriverI2CBitbangImpl_SendByte(pInstance, ((addr << 1) | 0));
        if (nack) goto error_stop;
        for (u32 i = 0; i< tx_len; i++) {
            nack = CommonDriverI2CBitbangImpl_SendByte(pInstance, ((u8 *)tx_buffer)[i]);
            if (nack) goto error_stop;
        }
    }

    if (rx_buffer) {
        if (issue_start) CommonDriverI2CBitbangImpl_SendStartBit(pInstance);
        bool nack = CommonDriverI2CBitbangImpl_SendByte(pInstance, ((addr << 1) | 1));
        if (nack) goto error_stop;
        for (u32 i = 0; i< rx_len; i++) {
            bool lastByte = (i >= rx_len - 1);
            ((u8 *)rx_buffer)[i] = CommonDriverI2CBitbangImpl_ReceiveByte(pInstance, lastByte);
        }
    }

    if (issue_stop) {
        CommonDriverI2CBitbangImpl_SendStopBit(pInstance);
    }
    return true;
error_stop:
    CommonDriverI2CBitbangImpl_SendStopBit(pInstance);
    return false;
}

static void OnDestroyHandle(LTHandle hDevice) {
    I2C_Instance * pInstance = InstanceFromHandle(hDevice);
    if (pInstance) {
        u32 nRefCount = LT_U32_MAX;
        pInstance->mutex->API->Lock(pInstance->mutex);
        if (pInstance->nRefCount > 0)
            nRefCount = --pInstance->nRefCount;
        pInstance->mutex->API->Unlock(pInstance->mutex);
        if (nRefCount == 0) {
            ShutdownI2C(pInstance);
        }
    }
}

static bool CommonDriverI2CBitbangImpl_Reset(LTDeviceUnit unit) {
    I2C_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance) {
        LTLOG("reset.instance.fail", "error");
        return false;
    }
    return true;
}

static bool CommonDriverI2CBitbangImpl_ProbeAddress(LTDeviceUnit unit, u8 addr) {
    I2C_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance) {
        LTLOG("probe.instance.fail", "error");
        return false;
    }
    CommonDriverI2CBitbangImpl_SendStartBit(pInstance);
    bool nack = CommonDriverI2CBitbangImpl_SendByte(pInstance, ((addr << 1) | 0));
    CommonDriverI2CBitbangImpl_SendStopBit(pInstance);
    LTLOG_DEBUG("probe", "address %lx result %d", LT_Pu32((u32)addr), (!nack)?(1):(0));
    return !nack;
}

define_LTLIBRARY_INTERFACE(ILTDriverI2C, OnDestroyHandle) {
    .GetBusIndexFromName    = CommonDriverI2CBitbangImpl_GetBusIndexFromName,
    .GetDeviceCapabilities  = CommonDriverI2CBitbangImpl_GetDeviceCapabilities,
    .GetDeviceConfiguration = CommonDriverI2CBitbangImpl_GetDeviceConfiguration,
    .SetDeviceConfiguration = CommonDriverI2CBitbangImpl_SetDeviceConfiguration,
    .SetTransferTimeout     = CommonDriverI2CBitbangImpl_SetTransferTimeout,
    .I2CMasterTransfer      = CommonDriverI2CBitbangImpl_I2CMasterTransfer,
    .Reset                  = CommonDriverI2CBitbangImpl_Reset,
    .ProbeAddress           = CommonDriverI2CBitbangImpl_ProbeAddress
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(CommonDriverI2CBitbang, (ILTDriverI2C))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  30-Jun-21   titus       created
 */
