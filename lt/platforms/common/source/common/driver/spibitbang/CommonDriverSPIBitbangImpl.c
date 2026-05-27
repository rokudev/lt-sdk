/*******************************************************************************
 * platforms/common/source/common/driver/spibitbang/CommonDriverSPIBitbangImpl.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * Common LT Driver Library for bitbang SPI
 *
 ******************************************************************************/
/** @file CommonDriverSPIBitbangImpl.c Implementation of bitbang SPI */

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/spi/LTDeviceSPI.h>
#include <lt/device/pins/LTDevicePins.h>

DEFINE_LTLOG_SECTION("spibitbangdriver");
/*******************************************************************************
 * SPI descriptions                                                   */
static ILTDriverPins_OutputBank                 * s_pIOutputBank  = NULL;
static ILTDriverPins_InputBank                  * s_pIInputBank  = NULL;
static LTDevicePins                             * s_pDevicePins = NULL;
typedef struct {
    LTMutex *                   mutex;                    /**< Mutex protection, mainly for reference count     */
    u32                         nRefCount;                /**< How many clients have a LTHandle to this Group   */
    LTDeviceSPI_Configuration   Cfg;
    LTDeviceSPI_Capabilities    Caps;
    char const *                pinNameMosi;
    LTDeviceUnit                hMosiBank;
    char const *                pinNameMiso;
    LTDeviceUnit                hMisoBank;
    char const *                pinNameCs;
    LTDeviceUnit                hCsBank;
    char const *                pinNameClk;
    LTDeviceUnit                hClkBank;
} SPI_Instance;

static SPI_Instance s_SPI_Instance[] = {
    #define SPIBBINSTANCE(PinNameMosi, PinNameMiso, PinNameCs, PinNameClk) { \
        .Caps = { .Freq_min = 1, \
                  .Freq_max = 1000000, \
                  .Bits_min = 16, \
                  .Bits_max = 16, \
                  .Caps_mask = (kLTDeviceSPI_Capability_Master | kLTDeviceSPI_Capability_BitBang), \
                }, \
        .pinNameMosi = #PinNameMosi, .pinNameMiso = #PinNameMiso, .pinNameCs = #PinNameCs,  .pinNameClk = #PinNameClk, \
    },
    #include LT_STRINGIFY(CONFIGURATION_FILE)
    #undef SPIBBINSTANCE
};

enum { kNumSPIConnections = sizeof s_SPI_Instance / sizeof (SPI_Instance) };
/********************************************************************************************************************************
 * Platform-specific GPIO initialization and input                                                                             */
static bool InitializeSPI(SPI_Instance *pInstSPI) {
    // Initialize GPIO pins via LT Device interface
    u32 nBankNumber = LT_U32_MAX;
    if (!s_pDevicePins->GetUnitNumberFromBankName(pInstSPI->pinNameMosi, &nBankNumber)) {
        LTLOG_REDALERT( "fail.no.bank.number",
                                       "Unable to obtain bank number for MOSI");
        return false;
    }
    pInstSPI->hMosiBank = s_pDevicePins ? s_pDevicePins->CreateDeviceUnitHandle(nBankNumber) : 0;
    s_pIOutputBank = pInstSPI->hMosiBank ? lt_gethandleinterface(ILTDriverPins_OutputBank, pInstSPI->hMosiBank) : 0;
    if (!s_pDevicePins->GetUnitNumberFromBankName(pInstSPI->pinNameMiso, &nBankNumber)) {
        LTLOG( "fail.no.bank.number",
                                       "Unable to obtain bank number for MISO");
    } else {
        pInstSPI->hMisoBank = s_pDevicePins ? s_pDevicePins->CreateDeviceUnitHandle(nBankNumber) : 0;
        s_pIInputBank = pInstSPI->hMisoBank ? lt_gethandleinterface(ILTDriverPins_InputBank, pInstSPI->hMisoBank) : 0;
    }
    if (!s_pDevicePins->GetUnitNumberFromBankName(pInstSPI->pinNameCs, &nBankNumber)) {
        LTLOG_REDALERT( "fail.no.bank.number",
                                       "Unable to obtain bank number for CS");
        return false;
    }
    pInstSPI->hCsBank = s_pDevicePins ? s_pDevicePins->CreateDeviceUnitHandle(nBankNumber) : 0;
    if (!s_pDevicePins->GetUnitNumberFromBankName(pInstSPI->pinNameClk, &nBankNumber)) {
        LTLOG_REDALERT( "fail.no.bank.number",
                                       "Unable to obtain bank number for CLK");
        return false;
    }
    pInstSPI->hClkBank = s_pDevicePins ? s_pDevicePins->CreateDeviceUnitHandle(nBankNumber) : 0;

    if (s_pIOutputBank) {
        s_pIOutputBank->ConfigureOutputType(pInstSPI->hMosiBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
        s_pIOutputBank->Set(pInstSPI->hMosiBank, 0);
        s_pIOutputBank->ConfigureOutputType(pInstSPI->hClkBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
        s_pIOutputBank->Set(pInstSPI->hClkBank, 0);
        s_pIOutputBank->ConfigureOutputType(pInstSPI->hCsBank, kLTDevicePin_PinConfiguration_OutputType_PushPull);
        s_pIOutputBank->Set(pInstSPI->hCsBank, 1);
    } else {
        return false;
    }
    if (s_pIInputBank) {
        s_pIInputBank->ConfigurePullType(pInstSPI->hMisoBank, kLTDevicePin_PinConfiguration_PullType_NoPull);
    }
    return true;
}

/* Retrieve a pointer to the Group Instance data from the Device Unit handle.
 * Return 0 if the handle or the private data pointer are invalid. */
static SPI_Instance * InstanceFromHandle(LTDeviceUnit hDevice) {
    SPI_Instance ** ppInstance = (SPI_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    SPI_Instance * pInstance = NULL;
    if (ppInstance) {
        pInstance = *ppInstance;
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, ppInstance);
    }
    return pInstance;
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(ILTDriverSPI, CommonDriverSPIBitbang);

/********************************************************************************************************************************
 * Library initialization and deinitialization                                                                                 */
static bool CommonDriverSPIBitbangImpl_LibInit(void) {
    s_pDevicePins = (LTDevicePins *)LT_GetCore()->OpenLibrary("LTDevicePins");
    if (!s_pDevicePins) return false;
    SPI_Instance * pInstance = s_SPI_Instance;
    for (u32 i = 0; i < kNumSPIConnections; ++i, ++pInstance) {
        pInstance->nRefCount = 0;
        if (!(pInstance->mutex = lt_createobject(LTMutex))) {
            CommonDriverSPIBitbangImpl_LibFini();
            return false;
        }
    }
    return true;
}

static void CommonDriverSPIBitbangImpl_LibFini(void) {
    SPI_Instance * pInstance = s_SPI_Instance;
    for (u32 i = kNumSPIConnections; i--; ++pInstance)
        if (pInstance->mutex) {
            lt_destroyobject(pInstance->mutex);
            pInstance->mutex = NULL;
        }
}

static ILTDriverSPI s_ILTDriverSPI;
/* This is a forward declaration of the ILTDriverSPI interface instance that gets
 * defined at the end of this file by the macro define_LTLIBRARY_INTERFACE(ILTDriverSPI).
 * The variable name has to be s_ILTDriverSPI because that is what the macro defines. */

/********************************************************************************************************************************
 * Device-unit creation interface.
 */
static u32 CommonDriverSPIBitbangImpl_GetNumDeviceUnits(void) { return kNumSPIConnections; }

LTDeviceUnit CommonDriverSPIBitbangImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    /* Use private data for handles: SPI pins allocation and so on */
    LTDeviceUnit hDevice = 0;
    if (nDeviceUnitNumber < kNumSPIConnections) {
        hDevice = LT_GetCore()->CreateHandle((LTInterface *)&s_ILTDriverSPI, sizeof(SPI_Instance *));
    }
    if (hDevice) {
        bool bInterfaceOK = false;  /* A handle has been created.  Do not leak it if something goes
                                       wrong with preparing the handle or initializing the interface. */
        SPI_Instance ** ppInstance = (SPI_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
        if (ppInstance) {
            SPI_Instance * pInstance = *ppInstance = &s_SPI_Instance[nDeviceUnitNumber];
            bInterfaceOK = true;    /* all okay, unless first-reference initialization (below) fails */
            pInstance->mutex->API->Lock(pInstance->mutex);
            if (++pInstance->nRefCount == 1) {   /* Just starting up this instance. */
                bInterfaceOK = InitializeSPI(pInstance);
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
static bool CommonDriverSPIBitbangImpl_GetDeviceCapabilities(LTDeviceUnit unit, LTDeviceSPI_Capabilities* pCaps) {
    SPI_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance || !pCaps) return false;
    lt_memcpy(pCaps, &pInstance->Caps, sizeof(LTDeviceSPI_Capabilities));
    return true;
}
static bool CommonDriverSPIBitbangImpl_GetDeviceConfiguration(LTDeviceUnit unit, LTDeviceSPI_Configuration* pSPIConfig) {
    SPI_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance || !pSPIConfig) return false;
    lt_memcpy(pSPIConfig, &pInstance->Cfg, sizeof(LTDeviceSPI_Configuration));
    return true;
}

static bool CommonDriverSPIBitbangImpl_SetDeviceConfiguration(LTDeviceUnit unit, const LTDeviceSPI_Configuration* pSPIConfig) {
    SPI_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance || !pSPIConfig) return false;

    if (!SPI_CONFIG_CAPS_OK(pSPIConfig, &(pInstance->Caps))) {
            LTLOG("cfg.check.fail", "error");
            return false;
    }
    lt_memcpy(&pInstance->Cfg, pSPIConfig, sizeof(LTDeviceSPI_Configuration));
    // TODO: Re-initilize bitbang
    return true;
}

static bool CommonDriverSPIBitbangImpl_SPIMasterTransfer(LTDeviceUnit unit, u8 *rx_buffer, u8 *tx_buffer, u32 buff_len, LTSPI_SPIMasterTransferStatusCallback *pCallback, void *pClientData) {
    /* TODO: add async dma modes support with callbacks */
    LT_UNUSED(pCallback);
    LT_UNUSED(pClientData);
    SPI_Instance * pInstance = InstanceFromHandle(unit);
    if (!pInstance || !buff_len || (!rx_buffer && !tx_buffer)) return false;
    if (buff_len % 2 != 0 ) return false;
    u16 *tx_buff = (u16*) tx_buffer;
    u16 *rx_buff = (u16*) rx_buffer;
    /* 16 bit transfers Mode 0 : CPOL == 0, CPHA == 0 */
    /* TODO: add other modes support */
    /* This code running full-speed drives  */
    /* the SPI bus with a clock rate around */
    /* 1MHz.  If delays are ever needed     */
    /* (e.g., to slow down the transaction, */
    /* or to make the timing more           */
    /* consistent), place equal delays      */
    /* where marked (four places).          */
    for (u32 i = 0; i< buff_len/2; i++) {
        s_pIOutputBank->Set(pInstance->hClkBank, 0);
        /* delay */
        s_pIOutputBank->Set(pInstance->hCsBank, 0);
        /* delay */
        if (rx_buff) {
            rx_buff[i] = 0;
        }
        for (u16 bit = 1 << 15; bit; bit >>= 1) {
            s_pIOutputBank->Set(pInstance->hMosiBank, tx_buff[i] & bit ? 1 : 0);
            /* delay */
            s_pIOutputBank->Set(pInstance->hClkBank, 1);
            /* delay */
            if(s_pIInputBank) {
                u32 rx_bit = s_pIInputBank->Read(pInstance->hMisoBank);
                if (rx_bit && rx_buff) {
                    rx_buff[i] |= bit;
                }
            }
            s_pIOutputBank->Set(pInstance->hClkBank, 0);

        }
        s_pIOutputBank->Set(pInstance->hCsBank, 1);

    }
    return true;
}


define_LTLIBRARY_INTERFACE(ILTDriverSPI) {
    .GetDeviceCapabilities = CommonDriverSPIBitbangImpl_GetDeviceCapabilities,
    .GetDeviceConfiguration = CommonDriverSPIBitbangImpl_GetDeviceConfiguration,
    .SetDeviceConfiguration = CommonDriverSPIBitbangImpl_SetDeviceConfiguration,
    .SPIMasterTransfer = CommonDriverSPIBitbangImpl_SPIMasterTransfer
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(CommonDriverSPIBitbang, (ILTDriverSPI))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  15-Jun-21   titus       created
 */
