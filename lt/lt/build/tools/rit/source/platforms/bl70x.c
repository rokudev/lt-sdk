/******************************************************************************
 * bl70x.c                                    Bouffalo Labs bl70x Flash Support
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "lt/LTTypes.h"
#include "lt/core/LTCore.h"

#include "Image.h"
#include "FlashDevice.h"
#include "Serial.h"

enum {
    kNominalBaudRate               = 2000000,
    kSlowBaudRate                  = 500000,

    /* Serial Timeouts (milliseconds) */
    kDefaultSerialTimeoutMS        = 300,
    kChecksumSerialTimeoutMS       = 2500,
};

/* Serial commands */
typedef u8 CommandID;
enum CommandID {
    kCommandID_Reset               = 0x21,  /* Reset device */

    /* Configuration */
    kCommandID_GetDeviceInfo       = 0x10,  /* Get Device Information */
    kCommandID_SetDeviceConfig     = 0x22,  /* Set device configuration */
    kCommandID_SetFlashConfig      = 0x3b,  /* Configure flash peripheral */

    /* Erase / Write */
    kCommandID_FlashErase          = 0x30,  /* Erase flash sectors given address range */
    kCommandID_FlashWrite          = 0x31,  /* Write uncompressed data to flash */
    kCommandID_FlashGetID          = 0x36,  /* Get flash ID */
    kCommandID_FlashWriteCheck     = 0x3a,  /* Check success of prior flash operation */
    kCommandID_FlashWriteCompress  = 0x3f,  /* Compressed flash write (uses xz compression) */

    /* Read / Verify */
    kCommandID_FlashReadStart      = 0x60,  /* Start flash read operation */
    kCommandID_FlashGetChecksum    = 0x3e,  /* Get the checksum */
    kCommandID_FlashReadFinish     = 0x61,  /* End flash read operation */
};

typedef struct {
    u8   id;
    u8   csum;
    u8   nLengthLo;
    u8   nLengthHi;
} CommandHeader;

typedef struct {
    /* NOTE: OK is stripped before reading header */
    u8   nLengthLo;
    u8   nLengthHi;
} ResponseHeader;

typedef struct {
    u8 signType;
    u8 encrypted;
    u8 reserved[2];
} eFuseHWConfig;

typedef struct {
    u32 rsvd_12_0 :13;
    u32 anti_rollback_enable : 1; 
    u32 chip_revision :4;
    u32 rsvd_21_18 :4;
    u32 sf_swap_cfg :2;
    u32 psram_cfg :2;
    u32 flash_cfg :3;
    u32 sf_reverse :1;
    u32 pkg_info :2;
} eFuseDevInfo;

typedef struct {
    u8            romVer[4];
    eFuseHWConfig hwCfg;     // 4 bytes
    u8            swCfg0[4];
    eFuseDevInfo  devInfo;   // 4 bytes
    u8            chipID[8];
} DeviceInfo;

typedef struct {
    u8   uartIRQen[4];
    u8   baudRate[4];
    u8   clockParams[16];
} DeviceConfig;

typedef struct {
    /*  0: external flash
        1: internal flash, no swap
        2: internal flash, cs-io2 swap
        3: internal flash, io0-io3 swap
        4: internal flash, both swap */
    u8 flashPin;
    /* bit 7-4: flash_clock_type: 0: XCLK, 1: 64M, 2: BCLK, 3: 42.67M
       bit 3-0: flash_clock_div */
    u8 flashClkCfg;
    /* 0:NIO,1:DO,2:Q0,3:DI0,4:QIO*/
    u8 flashIOMmode;
    /* 0: 0.5T delay, 1: 1T delay, 2: 1.5T delay, 3: 2T delay */
    u8 flashClkDelay;
} InitialFlashConfig;

typedef struct {
    InitialFlashConfig   initFlashConfig;
    u8                   flashParams[84]; // correspond to SPI_Flash_Cfg_Type. See below
} FlashConfig;

typedef struct {
    u32  nOffsetBegin;
    u32  nOffsetEnd;
} FlashErase;

typedef struct {
    u32  nOffset;
} FlashWriteHeader;

typedef struct {
    u8   id[3];
    u8   nPad;
} FlashID;

typedef struct {
    u32  nOffset;
    u32  nLength;
} FlashGetChecksum;

/* Supported device types */
typedef u16 DeviceType;
enum DeviceType {
    kDeviceType_bl702l   = 0,
    kDeviceType_Total
};

typedef struct {
    DeviceType  type;
    u8          flashID[3];
    u8          flashParamIdx;
} FlashParamsMap;

static const char * s_deviceTypes[kDeviceType_Total] = {
    "bl702l"
};

/* Flash parameters */
enum { kFlashParams_Total = 2 };
static const FlashParamsMap s_flashParamsMap[kFlashParams_Total] = {
    { kDeviceType_bl702l, { 0xc8, 0x40, 0x15 }, 0 },  /* 0: Gigadevice GD25Q16 */
    { kDeviceType_bl702l, { 0x5e, 0x32, 0x14 }, 1 },  /* 1: Zbit ZB25D80B */
};
// Explanation of the flashParams can be found in Bouffalo Labs SDK.
// struct SPI_Flash_Cfg_Type defined in bl702l_sflah.h
static const u8 s_flashParams[kFlashParams_Total][84] = {
    { /* Idx 0 */
        0x04, 0x01, 0x00, 0x00, 0x66, 0x99, 0xff, 0x03, 0x9f, 0x00, 0x9f, 0x00, 0x04, 0xc8, 0x00, 0x01,
        0xc7, 0x20, 0x52, 0xd8, 0x06, 0x02, 0x32, 0x00, 0x0b, 0x01, 0x0b, 0x01, 0x3b, 0x01, 0xbb, 0x00,
        0x6b, 0x01, 0xeb, 0x02, 0xeb, 0x02, 0x02, 0x50, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x02, 0x01,
        0x02, 0x01, 0xab, 0x01, 0x05, 0x35, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x38, 0xff, 0xa0, 0xff,
        0x77, 0x03, 0x02, 0x40, 0x77, 0x03, 0x02, 0xf0, 0x2c, 0x01, 0xb0, 0x04, 0xb0, 0x04, 0x05, 0x00,
        0xff, 0xff, 0x14, 0x00
    },
    { /* Idx 1 */
        0x11, 0x00, 0x00, 0x00, 0x66, 0x99, 0xff, 0x03, 0x9f, 0x00, 0x9f, 0x00, 0x04, 0x5e, 0x00, 0x01,
        0xc7, 0x20, 0x52, 0xd8, 0x06, 0x02, 0x32, 0x00, 0x0b, 0x01, 0x0b, 0x01, 0x3b, 0x01, 0xbb, 0x00,
        0x6b, 0x01, 0xeb, 0x02, 0xeb, 0x02, 0x02, 0x50, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x02, 0x01,
        0x01, 0x01, 0xab, 0x01, 0x05, 0x35, 0x00, 0x00, 0x01, 0x31, 0x00, 0x00, 0x38, 0xff, 0x20, 0xff,
        0x77, 0x03, 0x02, 0x40, 0x77, 0x03, 0x02, 0xf0, 0x2c, 0x01, 0xb0, 0x04, 0xb0, 0x04, 0x05, 0x00,
        0xe8, 0x80, 0x08, 0x00
    },
};

static u32 GetFlashSizeFromJedecID(u32 jedec_id) {
    u8 flash_size_level = 0;
    u32 flash_size = 0;
    u32 jid = 0;
    jid = ((jedec_id & 0xff) << 16) + (jedec_id & 0xff00) + ((jedec_id & 0xff0000) >> 16);

    if (jid == 0) {
        return 0;
    }

    flash_size_level = (jid & 0x1f);
    flash_size_level -= 0x13;
    flash_size = (1 << flash_size_level) * 512 * 1024;

    return flash_size;
}

static int SendCommand(CommandID id, u8 * pPayload, u16 nPayloadLength) {
    if (nPayloadLength && !pPayload) return -EPROTO;
    /* Calculate simple checksum */
    u32 csum = (nPayloadLength & 0xff) + (nPayloadLength >> 8);
    for (u32 nIdx = 0; nIdx < nPayloadLength; nIdx++) csum += pPayload[nIdx];
    /* Send header followed by payload */
    CommandHeader header = {
        .id        = id,
        .csum      = (u8)(csum & 0xff),
        .nLengthLo = (u8)(nPayloadLength & 0xff),
        .nLengthHi = (u8)(nPayloadLength >> 8)
    };
    int nRtn = SerialSend((u8 *)&header, sizeof(CommandHeader));
    if (nRtn < 0) return nRtn;
    if (nPayloadLength > 0) {
        return SerialSend((u8 *)pPayload, nPayloadLength);
    }
    return 0;
}

static int WaitForOK(void) {
    u8 data[2];
    while (1) {
        int nRtn = SerialRecv(data, sizeof(data));
        if (nRtn < 0) return nRtn;
        /* OK == I'm done! */
        if (data[0] == 'O' && data[1] == 'K') return 0;
        /* PD == Keepalive */
        if (data[0] == 'P' && data[1] == 'D') continue;
        /* FL == Fail */
        if (data[0] == 'F' && data[1] == 'L') {
            // Grab the 2 byte error code
            nRtn = SerialRecv(data, sizeof(data));
            printf("Operation failed...err code: 0x%02x%02x\n", data[1], data[0]);
            return -1;
        }
        break;
    }
    return -EPROTO;
}

/* For commands with simple [PD][PD]...OK responses... */
static int SendCommandAndWaitForOK(CommandID id, u8 * pPayload, u16 nPayloadLength) {
    int nRtn = SendCommand(id, pPayload, nPayloadLength);
    if (nRtn < 0) return nRtn;
    return WaitForOK();
}

/* For commands with full responses... */
static int WaitForResponse(u8 * pPayload, u16 nExpectedPayloadLength) {
    int nRtn = WaitForOK();
    if (nRtn < 0) return nRtn;
    ResponseHeader header;
    nRtn = SerialRecv((u8 *)&header, sizeof(ResponseHeader));
    if (nRtn < 0) return nRtn;
    u16 nLength = (header.nLengthHi << 8) + header.nLengthLo;
    if (nLength != nExpectedPayloadLength) return -EPROTO;
    return SerialRecv(pPayload, nLength);
}

static int GetDeviceInfo(DeviceInfo * pInfo) {
    int nRtn = SendCommand(kCommandID_GetDeviceInfo, NULL, 0);
    if (nRtn < 0) return nRtn;
    return WaitForResponse((u8 *)pInfo, sizeof(DeviceInfo));
}

static int GetDeviceTypeIndex(const char * pDeviceType) {
    for (u32 nIdx = 0; nIdx < kDeviceType_Total; nIdx++) {
        if (lt_strcmp(pDeviceType, s_deviceTypes[nIdx]) == 0) return nIdx;
    }
    printf("Device type %s not supported!\n", pDeviceType);
    return -1;
}

static int GetDeviceConfig(const char * pDeviceType, DeviceConfig * pConfig) {
    const u8 uartIRQen[4] = { 0x01, 0x00, 0x00, 0x00 }; // Enable IRQ
    const u8 baudRate[4] = { 0x80, 0x84, 0x1e, 0x00 };  // 2000000
    const u8 clockParams[kDeviceType_Total][16] = {
        { 0x50, 0x43, 0x46, 0x47, 0x01, 0x05, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xe0, 0xd9, 0xd0, 0xc6 },
    };
    int nRtn = GetDeviceTypeIndex(pDeviceType);
    if (nRtn < 0) return nRtn;
    lt_memcpy(pConfig->uartIRQen, uartIRQen, sizeof(pConfig->uartIRQen));
    lt_memcpy(pConfig->baudRate, baudRate, sizeof(pConfig->baudRate));
    lt_memcpy(pConfig->clockParams, clockParams[nRtn], sizeof(pConfig->clockParams));
    return 0;
}

static int GetInitialFlashConfig(const char * pDeviceType, InitialFlashConfig * pConfig, DeviceInfo * pInfo) {
    int nDeviceTypeIdx = GetDeviceTypeIndex(pDeviceType);
    if (nDeviceTypeIdx < 0) return nDeviceTypeIdx;
/*
        The recommended settings are as follows:
        0x01: select internal flash with no signal swap (need to configure according to the flash_cfg and sf_swap_ ↪cfg information in bootinfo)
        0x00: select XCLK as flash clock, without frequency division
        0x01: choose 2-wire flash mode
        0x01: choose 1T flash clock delay
        0x01 0x00 0x01 0x01
    */
    if (pInfo->devInfo.flash_cfg == 0) {
        pConfig->flashPin = 0x0;
    } else {
        switch (pInfo->devInfo.sf_swap_cfg) {
            case 0:
                pConfig->flashPin = 0x01;
                break;
            case 1:
                pConfig->flashPin = 0x02;
                break;
            case 2:
                pConfig->flashPin = 0x03;
                break;
            case 3:
                pConfig->flashPin = 0x04;
                break;
            default:
                pConfig->flashPin = 0x01;
                break;
        }
    }     
    // Set some recommended default values
    pConfig->flashClkCfg = 0x00;
    pConfig->flashIOMmode = 0x01;
    pConfig->flashClkDelay = 0x01;
    return 0;
}

static int GetFlashConfig(const char * pDeviceType, FlashConfig * pConfig, FlashID * pFlashID) {
    int nDeviceTypeIdx = GetDeviceTypeIndex(pDeviceType);
    if (nDeviceTypeIdx < 0) return nDeviceTypeIdx;
    if (pFlashID == NULL) return 0;
    /* Find flash config */
    for (u32 nIdx = 0; nIdx < kFlashParams_Total; nIdx++) {
        if ((s_flashParamsMap[nIdx].type == nDeviceTypeIdx) && (lt_memcmp(s_flashParamsMap[nIdx].flashID, pFlashID, 3) == 0)) {
            lt_memcpy(pConfig->flashParams, &s_flashParams[s_flashParamsMap[nIdx].flashParamIdx], sizeof(s_flashParams[0]));
            return 0;
        }
    }
    printf("Cannot find flash config for device type '%s' and flash ID %02x%02x%02x.\n",
        pDeviceType, pFlashID->id[0], pFlashID->id[1], pFlashID->id[2]);

    return -1;
}

static int TargetInit(bool bAutoProgram) {
    if (bAutoProgram) {
        // Place in programming mode
        printf("Place device in programming mode...\n");
        SerialSetRTS(false);
        usleep(150000);
        SerialSetRTS(true);
        usleep(300000);
    }
    int nRtn;
    DeviceInfo info;
    /* Attempt to get device info at nominal baud rate, if attempt fails then send sync
     * to device at the post-reset (slow) baud rate and try again. If second attempt
     * succeeds then switch to nominal (fast) baud rate. */
    for (u32 nTry = 0; nTry < 2; nTry++) {
        nRtn = GetDeviceInfo(&info);
        if (nRtn == 0) break;
        else if (nTry == 0 && nRtn == -ETIMEDOUT) {
            nRtn = SerialSetSpeed(kSlowBaudRate); 
            if (nRtn < 0) return nRtn;
            /* Sync with target: Send Qty 150 0x55 and wait for ACK (0x4f 0x4b) */
            u8 burst[5] = { 0x55, 0x55, 0x55, 0x55, 0x55 };
            for (u32 nIx = 0; nIx < 30; nIx++) {
                nRtn = SerialSend(burst, sizeof(burst));
                if (nRtn < 0) return nRtn;
            }
            nRtn = WaitForOK();
            if (nRtn == 0) usleep(40000); // Wait 40 ms.
            // It is recommended to delay communication by at least 20ms to prevent 
            // subsequent communication data from being mixed with the previous handshake data
        } else return nRtn;
    }
#ifdef DEBUG_SERIAL_DATA
    printf("DeviceInfo: sf_swap_cfg %d, flash_cfg %d \n", info.devInfo.sf_swap_cfg, info.devInfo.flash_cfg);
#endif
    /* Read and set device config to switch to nominal (high) speed if not already there. */
    DeviceConfig devConfig;
    nRtn = GetDeviceConfig(ImageGetDeviceType(), &devConfig);
    if (nRtn < 0) return nRtn;
    nRtn = SendCommandAndWaitForOK(kCommandID_SetDeviceConfig, (u8 *)&devConfig, sizeof(DeviceConfig));
    if (nRtn < 0) return nRtn;
    nRtn = SerialSetSpeed(kNominalBaudRate); 
    if (nRtn < 0) return nRtn;
    usleep(10000);
    /* Obtain flash ID and then set flash config */
    InitialFlashConfig initialFlashConfig;
    nRtn = GetInitialFlashConfig(ImageGetDeviceType(), &initialFlashConfig, &info);
    nRtn = SendCommandAndWaitForOK(kCommandID_SetFlashConfig, (u8 *)&initialFlashConfig, sizeof(InitialFlashConfig));
    if (nRtn < 0) return nRtn;
    nRtn = SendCommand(kCommandID_FlashGetID, NULL, 0);
    if (nRtn < 0) return nRtn;
    FlashID flashID;
    nRtn = WaitForResponse((u8 *)&flashID, sizeof(FlashID));
    u32 jedec_id;
    lt_memcpy(&jedec_id, flashID.id, 3);
    printf("Flash size:0x%08x\n", GetFlashSizeFromJedecID(jedec_id));
    if (nRtn < 0) return nRtn;
    FlashConfig flashConfig;
    flashConfig.initFlashConfig = initialFlashConfig;

#if 0   // If we get no ID back, make it look like we got the one we expected, in
        // order to get farther into the update protocol.
    if ((flashID.id[0] == flashID.id[1]) && (flashID.id[1] == flashID.id[2]) && (flashID.id[2] == 0)) {
        printf("\nEmpty FlashID returned, using assumed ID (%02x%02x%02x)...\n",
            s_flashParamsMap[0].flashID[0],
            s_flashParamsMap[0].flashID[1],
            s_flashParamsMap[0].flashID[2]
        );
        flashID.id[0] = s_flashParamsMap[0].flashID[0];
        flashID.id[1] = s_flashParamsMap[0].flashID[1];
        flashID.id[2] = s_flashParamsMap[0].flashID[2];
    }
#endif

    nRtn = GetFlashConfig(ImageGetDeviceType(), &flashConfig, &flashID);
    if (nRtn < 0) return nRtn;
    nRtn = SendCommandAndWaitForOK(kCommandID_SetFlashConfig, (u8 *)&flashConfig, sizeof(flashConfig));
    if (nRtn < 0) return nRtn;
    return 0;
}

static int EraseFlash(Area * pArea, Image * pImage) {
    FlashErase erase;
    erase.nOffsetBegin = ImageHostToDeviceU32(NULL, pArea->nOffset);
    erase.nOffsetEnd   = ImageHostToDeviceU32(NULL, pArea->nOffset + (pImage ? pImage->nSize : pArea->nMaxSize) - 1);
    return SendCommandAndWaitForOK(kCommandID_FlashErase, (u8 *)&erase, sizeof(erase)); 
}

static int ProgramFlash(Area * pArea, Image * pImage) {
    int nRtn = EraseFlash(pArea, pImage);
    if (nRtn < 0) return nRtn;

    enum { kMaxBufSize = 2048 };
    u8 buf[sizeof(FlashWriteHeader) + kMaxBufSize];

    u8  * pData = pImage->pData;
    u32    nRem = pImage->nSize;
    int nMinPct = 10;

    while (nRem > 0) {
        u32 nCopySize = kMaxBufSize;
        if (nRem < kMaxBufSize) nCopySize = nRem;

        ImageHostToDeviceU32((u32 *)buf, pArea->nOffset + pImage->nSize - nRem);
        lt_memcpy(buf + sizeof(FlashWriteHeader), pData, nCopySize);

        nRtn = SendCommandAndWaitForOK(kCommandID_FlashWrite, buf, sizeof(FlashWriteHeader) + nCopySize);
        if (nRtn < 0) return nRtn;

        pData += nCopySize;
        nRem  -= nCopySize;
        int nPct = 100 * (pImage->nSize - nRem) / pImage->nSize;
        if (nPct >= nMinPct) {
            printf("%d%% ", nPct);
            fflush(stdout);
            nMinPct = nPct + 10;
        }
    }
    printf("\n");
    return SendCommandAndWaitForOK(kCommandID_FlashWriteCheck, NULL, 0);
}

static int GetSha256Sum(u8 * pDigest, u32 nOffset, u32 nLength) {
    int nRtn = SendCommandAndWaitForOK(kCommandID_FlashReadStart, NULL, 0);
    if (nRtn < 0) return nRtn;
    nRtn = SerialSetTimeout(kChecksumSerialTimeoutMS);
    if (nRtn < 0) return nRtn;
    FlashGetChecksum payload;
    payload.nOffset = ImageHostToDeviceU32(NULL, nOffset);
    payload.nLength = ImageHostToDeviceU32(NULL, nLength);
    nRtn = SendCommand(kCommandID_FlashGetChecksum, (u8 *)&payload, sizeof(payload));
    if (nRtn < 0) return nRtn;
    nRtn = WaitForResponse(pDigest, 32);
    if (nRtn < 0) return nRtn;
    nRtn = SerialSetTimeout(kDefaultSerialTimeoutMS);
    if (nRtn < 0) return nRtn;
    return SendCommandAndWaitForOK(kCommandID_FlashReadFinish, NULL, 0);
}

static int VerifyFlash(Area * pArea, Image * pImage) {
    u8 digest[32];
    int nRtn = GetSha256Sum(digest, pArea->nOffset, pImage->nSize);
    if (nRtn < 0) return nRtn;
    if (lt_memcmp(digest, pImage->digest, sizeof(digest)) != 0) {
        printf("Checksum mismatch\n");
        return -1;
    }
    return 0;
}

static int Open(const char * pDeviceName, bool bAutoProgram, const char * pPlatformArgs) {
    if (!bAutoProgram) printf("Auto-programming features not supported on this platform\n");
    if (pPlatformArgs && pPlatformArgs[0] != '\0') {
        printf("Platform-specific flags do not exist for this device\n");
        return -1;
    }
    int err = SerialOpen(pDeviceName, kNominalBaudRate, kDefaultSerialTimeoutMS);
    if (err < 0) return err;
   
    return TargetInit(bAutoProgram);
}

static void Close() {
    SerialClose();
}

static int Program(u32 nAreaIdx, char * pFilename, bool bPad, bool bAutoReboot) {
    if (!bAutoReboot) printf("Auto-reboot feature not supported on this platform\n");
    int nRtn = -1;
    Area * pArea = ImageGetArea(nAreaIdx);
    Image image;
    if (ImageCreateFromFile(&image, pFilename) < 0) {
        printf("Does file exist?\n");
        return -1;
    }
    if (bPad && ImagePadToEnd(&image, nAreaIdx) < 0) {
        printf("Error padding image\n");
        return -1;
    }
    if (pArea) {
        if (pArea->bMustFlashAll && image.nSize != pArea->nMaxSize) {
            printf("Error, image must be exact size of area\n");
            pArea = NULL;
        } else if (image.nSize > pArea->nMaxSize) {
            printf("Error, image will overflow area\n");
            pArea = NULL;
        }
    }
    if (pArea) {
        printf("Programming %s [%08X][%08X]\n", pFilename, pArea->nOffset, image.nSize);
        nRtn = ProgramFlash(pArea, &image);
        if (nRtn == 0) {
            nRtn = VerifyFlash(pArea, &image);
        }
    }
    ImageFree(&image);
    return nRtn;
}

static int Read(u32 nAreaIdx, char * pFilename, bool bAutoReboot) {
    printf("Flash read is not supported on this device\n");
    return -1;
}

static int Erase(u32 nAreaIdx) {
    Area * pArea = ImageGetArea(nAreaIdx);
    if (pArea) {
        printf("Erasing %s [%08X][%08X]\n", pArea->name, pArea->nOffset, pArea->nMaxSize);
        return EraseFlash(pArea, NULL);
    }
    return -1;
}

static int Reboot(void) {
    return SendCommandAndWaitForOK(kCommandID_Reset, NULL, 0);
}

FlashDeviceInterface FlashDeviceInterface_bl70x = {
    &Open,
    &Close,
    &Program,
    &Read,
    &Erase,
    &Reboot,
};

const FlashDeviceInterfaceMapEntry FlashDeviceMapEntry_bl70x = {
    .pFamily = "bl70x",
    .pDevice = NULL,
    .pLegacyDevice = NULL,
    .pInterface = &FlashDeviceInterface_bl70x
};

static void LT_USED_CONSTRUCTOR FlashDevice_RegisterSelf_bl70x(void) {
    (void)FlashDevice_Register(&FlashDeviceMapEntry_bl70x);
}
