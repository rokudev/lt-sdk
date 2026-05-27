/******************************************************************************
 * anyka.c                                                  Anyka Flash Support
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

/* Eliminate a macro that interferes with lt stdlib functions on some platforms: */
#undef snprintf

#include "lt/LTTypes.h"
#include "lt/core/LTCore.h"

#include "Image.h"
#include "FlashDevice.h"
#include "Serial.h"

/* ROM software prompt */
#define ANYKA_ROM_PROMPT               "H322_UART_BOOT>#"

/* Flasher software prefix/suffix */
#define ANYKA_FLASHER_PREFIX           "<DA>"
#define ANYKA_FLASHER_SUFFIX           "</D>"

enum {
    kNominalBaudRate                   = 2000000,
    kInitialBaudRate                   = 115200,

    kAckSignatureCheck                 = 0x2345,

    kBurnTypeUSB                       = 0,        /* Not used here */
    kBurnTypeUART                      = 1,
    kFlashPageSize                     = 256,      /* Must be 256 */

    /* Serial Timeouts (milliseconds) */
    kDefaultSerialTimeoutMS            = 100,
    kFlashLoaderSerialTimeoutMS        = 2000,

    /* Serial Delays (microseconds) */
    kEchoDelayUS                       = 2000,
    kButtonReleaseDelayUS              = 50000,

    /* Parition name len in binary Read Parameter structure */
    kPartitionNameLen                  = 8,
    kFlashReadChunkSize                = 32 * kFlashPageSize,   /* Read 32 pages at a time */
};

/* Command Block IDs */
typedef u8 CommandBlockID;
enum CommandBlockID {
    /* General commands */
    kCommandBlockID_TestConnect        = 0x02,  /* Test connection */
    kCommandBlockID_SetFlashMode       = 0x03,  /* Set device flash mode */
    kCommandBlockID_GetFlashID         = 0x04,  /* Read flash ID */
    kCommandBlockID_SetSPIParams       = 0x31,  /* Set SPI parameters for flash */
    kCommandBlockID_Reset              = 0x4a,  /* Reset SoC */

    /* Flash operations */
    kCommandBlockID_FlashReadStart     = 0x1c,  /* Start read operation */
    kCommandBlockID_FlashReadData      = 0x1d,  /* Read data from flash */
    kCommandBlockID_FlashErase         = 0x46,  /* Erase flash sectors */
    kCommandBlockID_FlashWriteStart    = 0x47,  /* Start write operation */
    kCommandBlockID_FlashWriteData     = 0x48,  /* Write data to flash */
    kCommandBlockID_FlashVerify        = 0x49,  /* Verify bytes on flash (CRC) */
};

// MediumType Enum used in flasher command payload.
enum MediumType {
    kMediumType_SPI_NOR,                //NOR FLASH
    kMediumType_SPI_NAND,               //SPI NAND
};

/* These values are stuffed into the "flags" field */
typedef u8 CommandBlockFlags;
enum CommandBlockFlags {
    kCommandBlockFlags_CommandOnly     = 0x0,   /* No data, just the command and its params */
    kCommandBlockFlags_ReadFromDevice  = 0x1,   /* Host wants to read data (after command) */
    kCommandBlockFlags_WriteFromHost   = 0x2,   /* Host wants to write data (after command) */
};

typedef struct __attribute__((packed)) {
    u8                rsvd;          /* Set to zero */
    CommandBlockID    id;            /* Command ID */
    u8                pad[3];        /* Set to zero */
    u32               param[2];      /* Command params (optional) */
    u8                rsvd2[3];      /* Set to zero */
} CommandBlock;

/* Pseudo-"USB Mass Storage Device Command Block Wrapper" (sent by host to UART) */
typedef struct __attribute__((packed)) {
    u32               sig;           /* set to zero (ignored) */
    u32               tag;           /* set to zero (ignored) */
    u32               transferLen;   /* length of data to transfer AFTER this message (not command only) */
    CommandBlockFlags flags;
    u8                LUN;           /* set to zero (ignored) */
    u8                cbLen;         /* set to zero (ignored) */
    CommandBlock      cb;
} CommandBlockWrapper;

/* Pseudo-"USB Mass Storage Device Command Status Wrapper" (received by host from UART) */
typedef struct __attribute__((packed)) {
    u32               sig;           /* Head=0x2345 */
    u32               tag;           /* Generally 0 */
    u32               dataResidue;   /* Result, generally 0 */
    u8                status;        /* 1=good, 0=bad */
    u8                pad[3];        /* Generally 0s */
} CommandStatusWrapper;

/*
 * NOTE: The Anyka protocol adds an additional wrapper to distinguish data from serial text (received data only).
 *    char prefix[4];   = ANYKA_FLASHER_PREFIX
 *    u32  len;         e.g.: 0x10 for CommandStatusWrapper
 *    u8   data[len];   e.g.: CommandStatusWrapper...
 *    char suffix[4];   = ANYKA_FLASHER_SUFFIX
 */

/* Flash Mode */
typedef union {
    struct {
        u8  medium;                  /* This flasher supports SPI NOR Flash only, set to 1 */
        u8  burnMode;                /* Update device - set to 1 */
        u8  erasePartitionSize;      /* set 0 */
        u8  noErase;                 /* set 0 */
    } data;
    u32 param;
} FlashMode;

/* Flash chip info */
typedef struct {
    u32  id;
    u32  cnt;
} FlashChipInfo;

/* Flash Parameters */
typedef struct {
    u32  id;
    u32  totalSize;
    u32  pageSize;
    u32  programSize;
    u32  eraseSize;
    u32  clock;
    u8   flags;
    u8   protectMask;
    u8   extFlags;
    u8   rsvd;
} FlashParams;

typedef struct __attribute__((packed)) {
    u32 dataLength;
    u8  partitionName[kPartitionNameLen];
    u32 partitionFlashOffset;
    u32 partitionSize;
    u32 writeOOBSize;                           // For SPI NAND only
    u8  medium_type;                            // SPI NOR or SPI NAND
    u8  rev1_0;
    u8  rev1_1;
    u8  rev1_2;
    u32 rev2;
    u32 rev3;
    u32 rev4;
} BinParams;

/* Supported device types */
typedef u16 DeviceType;
enum DeviceType {
    kDeviceType_ak3918av100n   = 0,
    kDeviceType_Total
};

typedef struct {
    DeviceType   type;
    u32          id;
    u8           flashParamIdx;
} FlashParamsMap;

static const char * s_deviceTypes[kDeviceType_Total] = {
    "ak3918av100n"
};

/* DDR register address/value pairs for ak3918av100n (64MiB DDR) */
static u32 s_ddrParams[36][2] = {
    { 0x21000014, 0x00423e71 }, { 0x21000018, 0xff070301 },
    { 0x2100001c, 0xf7773717 }, { 0x21000080, 0x00008890 },
    { 0x080000dc, 0x01100003 }, { 0x08000004, 0x40000024 },
    { 0x66668888, 0x0000000a }, { 0x21000078, 0x0001b2cb },
    { 0x21000004, 0x1ab46638 }, { 0x21000008, 0x00146567 },
    { 0x21000000, 0x00134db0 }, { 0x21000098, 0x9a76e700 },
    { 0x2100009c, 0x00080806 }, { 0x66668881, 0x00000001 },
    { 0x210000a4, 0x00010000 }, { 0x21000010, 0x02000000 },
    { 0x21000010, 0x02f00000 }, { 0x21000010, 0x02a00400 },
    { 0x21000010, 0x02820000 }, { 0x21000010, 0x02830000 },
    { 0x21000010, 0x02810042 }, { 0x21000010, 0x02800f72 },
    { 0x21000010, 0x02a00400 }, { 0x21000010, 0x02c00000 },
    { 0x21000010, 0x02c00000 }, { 0x21000010, 0x02800e72 },
    { 0x66668888, 0x00000002 }, { 0x21000010, 0x028103c2 },
    { 0x21000010, 0x02810042 }, { 0x21000010, 0x02f00000 },
    { 0x2100000c, 0x00001a5d }, { 0x66668888, 0x000000c8 },
    { 0x21000084, 0x88900c08 }, { 0x21000024, 0x00000001 },
    { 0x66668882, 0x54535150 }, { 0x88888888, 0x00000000 },
};

/* Flash parameter Map */
enum { kFlashParams_Total = 5 };
static const FlashParamsMap s_flashParamsMap[kFlashParams_Total] = {
    { kDeviceType_ak3918av100n, 0x1740c8, 0 }, /* 0: Gigadevices GD25Q64  */
    { kDeviceType_ak3918av100n, 0x1840c8, 1 }, /* 1: Gigadevices GD25Q128 */
    { kDeviceType_ak3918av100n, 0x182085, 1 }, /* 2: Puya PY25Q128HA */
    { kDeviceType_ak3918av100n, 0x192085, 3 }, /* 3: Puya PY25Q256HB */
    { kDeviceType_ak3918av100n, 0x194068, 4 }, /* 4: BYT BY25FQ256ES */
};
/* This Anyka flasher only supports SPI NOR flash devices */
static const FlashParams s_flashParams[kFlashParams_Total] = {
    { /* Idx 0 */
        0x1740c8,  8388608, kFlashPageSize, kFlashPageSize, 4096, 25000000, 0x80, 0x0, 0xa, 0x0
    },
    { /* Idx 1 */
        0x1840c8, 16777216, kFlashPageSize, kFlashPageSize, 4096, 25000000, 0x80, 0x0, 0xa, 0x0
    },
    { /* Idx 2 */
        0x182085, 16777216, kFlashPageSize, kFlashPageSize, 4096, 25000000, 0x80, 0x0, 0xa, 0x0
    },
    { /* Idx 3 */
        0x192085, 33554432, kFlashPageSize, kFlashPageSize, 4096, 25000000, 0x80, 0x0, 0xa, 0x0
    },
    { /* Idx 4 */
        0x194068, 33554432, kFlashPageSize, kFlashPageSize, 4096, 25000000, 0x80, 0x0, 0xa, 0x0
    },
};

static int Reboot(void);

static int GetDeviceTypeIndex(const char * pDeviceType) {
    for (u32 nIdx = 0; nIdx < kDeviceType_Total; nIdx++) {
        if (lt_strcmp(pDeviceType, s_deviceTypes[nIdx]) == 0) return nIdx;
    }
    printf("Device type %s not supported!\n", pDeviceType);
    return -1;
}

static int GetFlashConfig(u32 nDeviceTypeIdx, FlashParams * pParams, u32 id) {
    /* Find flash config */
    for (u32 nIdx = 0; nIdx < kFlashParams_Total; nIdx++) {
        if ((s_flashParamsMap[nIdx].type == nDeviceTypeIdx) && (id == s_flashParamsMap[nIdx].id)) {
            lt_memcpy(pParams, &s_flashParams[s_flashParamsMap[nIdx].flashParamIdx], sizeof(FlashParams));
            /* Fixup endian on 32-bit fields */
            ImageHostToDeviceU32(&pParams->id,          pParams->id);
            ImageHostToDeviceU32(&pParams->totalSize,   pParams->totalSize);
            ImageHostToDeviceU32(&pParams->pageSize,    pParams->pageSize);
            ImageHostToDeviceU32(&pParams->programSize, pParams->programSize);
            ImageHostToDeviceU32(&pParams->eraseSize,   pParams->eraseSize);
            ImageHostToDeviceU32(&pParams->clock,       pParams->clock);
            return 0;
        }
    }
    printf("Cannot find flash config for the given device type (%d) and flash ID (0x%08X).\n", nDeviceTypeIdx, id);
    printf(" -OR- User held flash programming button too long.\n");
    return -1;
}

/* Send text string and drain until finding a specified response string */
static int SendTextAndReceiveResponse(char * pCommand, char * pExpectedResponse, u32 nMaxDrainChars) {
    u32 nEnd = lt_strlen(pCommand);
    /* Send one character at a time, fairly slowly. */
    for (u32 nCnt = 0; nCnt < nEnd; nCnt++) {
        int nRtn = SerialSendChar(pCommand[nCnt]);
        if (nRtn < 0) return nRtn;
        usleep(kEchoDelayUS);
    }
    return SerialDrainUntilMatch((u8 *)pExpectedResponse, nMaxDrainChars);
}

/* (optionally) send binary data and (optionally) drain until finding a response string */
static int SendBinaryAndOrReceiveResponse(u8 * pData, u32 nDataLength, char * pExpectedResponse, u32 nMaxDrainChars) {
    int nRtn = SerialSend(pData, nDataLength);
    if (nRtn < 0) return nRtn;
    if (!pExpectedResponse) return 0;
    else return SerialDrainUntilMatch((u8 *)pExpectedResponse, nMaxDrainChars);
}

/* Initial register configuration */
static int RegisterConfig(void) {
    /* Disable watchdog timer */
    int nRtn = SendTextAndReceiveResponse("setvalue\r", "Input addr(0xfffffff0):", 100);
    if (nRtn < 0) return nRtn;
    nRtn = SendTextAndReceiveResponse("080000e8\r", "Input value(0xfffffff0):", 100);
    if (nRtn < 0) return nRtn;
    nRtn = SendTextAndReceiveResponse("aa000000\r", ":0x00000000\n" ANYKA_ROM_PROMPT, 100);
    if (nRtn < 0) return nRtn;
    /* Write DDR parameters */
    nRtn = SendTextAndReceiveResponse("draminit\r", "Config -> init\n\n", 100);
    if (nRtn < 0) return nRtn;
    u32 nParams = sizeof(s_ddrParams)/sizeof(s_ddrParams[0]);
    for (u32 nIdx = 0; nIdx < nParams; nIdx++) {
        char expected[32];
        lt_snprintf(expected, sizeof(expected), "%08x, %08x", s_ddrParams[nIdx][0], s_ddrParams[nIdx][1]);
        /* Write binary address and then value */
        u32 nTemp[2];
        nTemp[0] = ImageHostToDeviceU32(NULL, s_ddrParams[nIdx][0]);
        nTemp[1] = ImageHostToDeviceU32(NULL, s_ddrParams[nIdx][1]);
        if (nIdx < nParams - 1) {
            nRtn = SendBinaryAndOrReceiveResponse((u8 *)nTemp, sizeof(nTemp), expected, 100);
        } else {
            /* Last write is actually the DDR parameter terminator */
            nRtn = SendBinaryAndOrReceiveResponse((u8 *)nTemp, sizeof(nTemp), ANYKA_ROM_PROMPT, 100);
        }
        if (nRtn < 0) {
            printf("Error setting DDR params\n");
            return nRtn;
        }
    }
    return 0;
}

static int SendFlasherCommand(CommandBlockID id, u32 * pParams, CommandBlockFlags flags, u32 transferLen) {
    CommandBlockWrapper cmd = {
        .transferLen  = ImageHostToDeviceU32(NULL, transferLen),
        .flags        = flags,
        .cb.id        = id
    };
    if (pParams) {
        cmd.cb.param[0] = ImageHostToDeviceU32(NULL, pParams[0]);
        cmd.cb.param[1] = ImageHostToDeviceU32(NULL, pParams[1]);
    }
    return SerialSend((u8 *)&cmd, sizeof(cmd));
}

static int WaitForBinaryData(u8 * pData, u32 nDataLength, u32 nMaxDrainChars) {
    int nRtn = SendBinaryAndOrReceiveResponse(NULL, 0, ANYKA_FLASHER_PREFIX, nMaxDrainChars);
    if (nRtn < 0) return nRtn;
    u32 len;
    nRtn = SerialRecv((u8 *)&len, sizeof(u32));
    if (nRtn < 0) return nRtn;
    len = ImageDeviceToHostU32(NULL, len);
    if (len != nDataLength) return -EPROTO;
    nRtn = SerialRecv(pData, nDataLength);
    if (nRtn < 0) return nRtn;
    return SerialExpect((u8 *)ANYKA_FLASHER_SUFFIX);
}

static int WaitForFlasherAck(u32 nMaxDrainChars) {
    CommandStatusWrapper status;
    int nRtn = WaitForBinaryData((u8 *)&status, sizeof(status), nMaxDrainChars);
    if (nRtn < 0) return nRtn;
    if (ImageDeviceToHostU32(NULL, status.sig) != kAckSignatureCheck || status.status != 0x1) {
        printf("Flasher command failure\n");
        return -EBADMSG;
    }
    return 0;
}

/* Load and invoke image that performs actual flash operations */
static int LoadAndRunFlasher(void) {
    printf("Injecting flashloader image into RAM\n");
    Image flasherImage;
    if (ImageGetFlasherImage(&flasherImage, "ak3918") < 0) {
        return -1;
    }
    /* Set baud rate and burn type in image */
    ImageHostToDeviceU32((u32 *)(flasherImage.pData + 52), kNominalBaudRate);
    ImageHostToDeviceU32((u32 *)(flasherImage.pData + 56), kBurnTypeUART);
    /* Calculate byte checksum */
    u16 checksum = 0;
    for (u32 nIdx = 0; nIdx < flasherImage.nSize; nIdx++) {
        checksum += flasherImage.pData[nIdx];
    }
    int nRtn = SendTextAndReceiveResponse("download\r", "Input addr(0x80000000):", 100);
    if (nRtn < 0) return nRtn;
    nRtn = SendTextAndReceiveResponse("\r", "Select file:", 100);
    if (nRtn < 0) return nRtn;
    /* Write size header */
    u8 nTemp[4];
    ImageHostToDeviceU32((u32 *)nTemp, flasherImage.nSize + 6);
    nRtn = SendBinaryAndOrReceiveResponse(nTemp, sizeof(nTemp), NULL, 0);
    if (nRtn < 0) return nRtn;
    /* Send data */
    u32 nRem = flasherImage.nSize;
    int nMinPct = 10;
    while (nRem > 0) {
        enum { kMaxSendSize = 2048 };
        u32 nToSend = kMaxSendSize;
        if (nRem < kMaxSendSize) nToSend = nRem;
        nRtn = SendBinaryAndOrReceiveResponse(flasherImage.pData + flasherImage.nSize - nRem, nToSend, NULL, 0);
        if (nRtn < 0) return nRtn;
        nRem -= nToSend;
        int nPct = 100 * (flasherImage.nSize - nRem) / flasherImage.nSize;
        if (nPct >= nMinPct) {
            printf("%d%% ", nPct);
            fflush(stdout);
            nMinPct = nPct + 10;
        }
    }
    ImageFree(&flasherImage);
    printf("\n");
    /* Write checksum footer */
    nTemp[0] = checksum & 0xff;
    nTemp[1] = checksum >> 8;
    nRtn = SendBinaryAndOrReceiveResponse(nTemp, 2, "Check...", 100);
    if (nRtn < 0) return nRtn;
    /* Give some time for target to perform checksum */
    nRtn = SerialSetTimeout(kFlashLoaderSerialTimeoutMS);
    if (nRtn < 0) return nRtn;
    nRtn = SendBinaryAndOrReceiveResponse(NULL, 0, "Down OK!\n" ANYKA_ROM_PROMPT, 100);
    if (nRtn < 0) {
        printf("flasher Download failed\n");
        return nRtn;
    }
    /* Run flasher (NB: response line endings switch to \r) */
    nRtn = SendTextAndReceiveResponse("go\r", "Input addr(0x80000000):", 100);
    if (nRtn < 0) return nRtn;
    nRtn = SendTextAndReceiveResponse("\r", "chip type:0x3918\r", 256);
    if (nRtn < 0) {
        printf("Wrong chip type?\n");
        return nRtn;
    }
    nRtn = SendBinaryAndOrReceiveResponse(NULL, 0, "===Enter uart download===\r", 512);
    if (nRtn < 0) return nRtn;
    return SerialSetTimeout(kDefaultSerialTimeoutMS);
}

static int ConfigureDeviceFlashMode(u32 nDeviceTypeIdx) {
    /* Set device mode */
    FlashMode mode = { .data.medium = 1, .data.burnMode = 1 };
    u32 params[2] = { mode.param, 0 };
    int nRtn = SendFlasherCommand(kCommandBlockID_SetFlashMode, params, kCommandBlockFlags_CommandOnly, 0);
    if (nRtn < 0) return nRtn;
    nRtn = WaitForFlasherAck(100);
    if (nRtn < 0) return nRtn;
    /* Give user extra time to release programming button */
    usleep(kButtonReleaseDelayUS);
    /* Get flash ID */
    params[0] = 0;
    params[1] = 0;
    nRtn = SendFlasherCommand(kCommandBlockID_GetFlashID, params, kCommandBlockFlags_ReadFromDevice, sizeof(FlashChipInfo));
    if (nRtn < 0) return nRtn;
    FlashChipInfo chipInfo;
    nRtn = WaitForBinaryData((u8 *)&chipInfo, sizeof(chipInfo), 300);
    if (nRtn < 0) return nRtn;
    nRtn = WaitForFlasherAck(200);
    /* Set SPI Params */
    FlashParams flashParams;
    nRtn = GetFlashConfig(nDeviceTypeIdx, &flashParams, ImageDeviceToHostU32(NULL, chipInfo.id));
    if (nRtn < 0) return nRtn;
    nRtn = SendFlasherCommand(kCommandBlockID_SetSPIParams, params, kCommandBlockFlags_WriteFromHost, sizeof(FlashParams));
    if (nRtn < 0) return nRtn;
    nRtn = SendBinaryAndOrReceiveResponse(NULL, 0, "++set SPI param++", 100);
    if (nRtn < 0) return nRtn;
    nRtn = SendBinaryAndOrReceiveResponse((u8 *)&flashParams, sizeof(FlashParams), "SPI initialized ok!", 400);
    if (nRtn < 0) return nRtn;
    return WaitForFlasherAck(100);
}

static int TargetInit(u32 nDeviceTypeIdx) {
    int nRtn;
    /* Initial sync, try for about 7 seconds */
    for (u32 nTry = 0; nTry < 70; nTry++)  {
        /* Target is waiting for "kkk", no less */
        nRtn = SendTextAndReceiveResponse("kkk", ANYKA_ROM_PROMPT, 100);
        if (nRtn == 0) break;
        if (nRtn != -ETIMEDOUT) return nRtn;
        if (nTry == 99) return -ETIMEDOUT;
    }
    nRtn = SerialSetTimeout(kDefaultSerialTimeoutMS);
    if (nRtn < 0) return nRtn;
    /* Switch to nominal baudrate */
    nRtn = SendTextAndReceiveResponse("baudrate\r", "3. 2M.\n", 100);
    if (nRtn < 0) return nRtn;
    nRtn = SendTextAndReceiveResponse("3\r", "2M\n", 50);
    if (nRtn < 0) return nRtn;
    nRtn = SerialSetSpeed(kNominalBaudRate);
    if (nRtn < 0) return nRtn;
    /* Target serial port switches speed very fast, so hit CR again */
    nRtn = SendTextAndReceiveResponse("\r", ANYKA_ROM_PROMPT, 100);
    /* Write SoC registers */
    if (nRtn < 0) return nRtn;
    nRtn = RegisterConfig();
    if (nRtn < 0) return nRtn;
    /* Switch from ROM to flasher program */
    nRtn = LoadAndRunFlasher();
    if (nRtn < 0) return nRtn;
    /* Test connection to flasher */
    u32 params[2];
    params[0] = ImageHostToDeviceU32(NULL, 'B');
    params[1] = ImageHostToDeviceU32(NULL, 'T');
    nRtn = SendFlasherCommand(kCommandBlockID_TestConnect, params, kCommandBlockFlags_CommandOnly, 0);
    if (nRtn < 0) return nRtn;
    nRtn = WaitForFlasherAck(100);
    return ConfigureDeviceFlashMode(nDeviceTypeIdx);
};

static int EraseFlash(Area * pArea, Image * pImage) {
    u32 nSectorSize = ImageGetBlockSize();
    u32 nStartSector = pArea->nOffset / nSectorSize;
    u32 nSectors = pArea->nMaxSize / nSectorSize;
    if (pImage) nSectors = (pImage->nSize + nSectorSize - 1) / nSectorSize;
    u32 params[2];
    params[0] = ImageHostToDeviceU32(NULL, nStartSector);
    params[1] = ImageHostToDeviceU32(NULL, nStartSector + nSectors - 1);
    int nRtn = SendFlasherCommand(kCommandBlockID_FlashErase, params, kCommandBlockFlags_WriteFromHost, sizeof(params));
    if (nRtn < 0) return nRtn;
    /* Send the actual params */
    nRtn = SendBinaryAndOrReceiveResponse((u8 *)&params, sizeof(params), "erase startb:", 100);
    if (nRtn < 0) return nRtn;
    /* Swallow each "e." and everything else up to the Ack */
    return WaitForFlasherAck(100 + 2*nSectors);
}

static int ProgramFlash(Area * pArea, Image * pImage) {
    int nRtn = EraseFlash(pArea, pImage);
    if (nRtn < 0) return nRtn;
    /* Start flash write */
    u32 params[2];
    params[0] = ImageHostToDeviceU32(NULL, pImage->nSize);
    params[1] = ImageHostToDeviceU32(NULL, pArea->nOffset / kFlashPageSize);
    nRtn = SendFlasherCommand(kCommandBlockID_FlashWriteStart, NULL, kCommandBlockFlags_WriteFromHost, sizeof(params));
    if (nRtn < 0) return nRtn;
    nRtn = SendBinaryAndOrReceiveResponse((u8 *)&params, sizeof(params), "g_startpage:", 200);
    if (nRtn < 0) return nRtn;
    nRtn = WaitForFlasherAck(100);
    if (nRtn < 0) return nRtn;
    /* Send flash data */
    u32 nRem = pImage->nSize;
    int nMinPct = 10;
    while (nRem > 0) {
        enum { kMaxSendSize = 8192 };
        u32 nToSend = kMaxSendSize;
        if (nRem < kMaxSendSize) nToSend = nRem;
        nRtn = SendFlasherCommand(kCommandBlockID_FlashWriteData, NULL, kCommandBlockFlags_WriteFromHost, nToSend);
        if (nRtn < 0) return nRtn;
        nRtn = SendBinaryAndOrReceiveResponse(NULL, 0, "program_data_byte++", 100);
        if (nRtn < 0) return nRtn;
        nRtn = SendBinaryAndOrReceiveResponse(pImage->pData + pImage->nSize - nRem, nToSend, "++program data", 100);
        if (nRtn < 0) return nRtn;
        nRtn = WaitForFlasherAck(100);
        if (nRtn < 0) return nRtn;
        nRem -= nToSend;
        int nPct = 100 * (pImage->nSize - nRem) / pImage->nSize;
        if (nPct >= nMinPct) {
            printf("%d%% ", nPct);
            fflush(stdout);
            nMinPct = nPct + 10;
        }
    }
    printf("\n");
    return 0;
}

static int VerifyFlash(Area * pArea, Image * pImage) {
    u32 params[3];
    params[0] = ImageHostToDeviceU32(NULL, pArea->nOffset / kFlashPageSize);
    params[1] = ImageHostToDeviceU32(NULL, pImage->nSize);
    params[2] = ImageHostToDeviceU32(NULL, pImage->nCRC);
    int nRtn = SendFlasherCommand(kCommandBlockID_FlashVerify, NULL, kCommandBlockFlags_WriteFromHost, sizeof(params));
    if (nRtn < 0) return nRtn;
    nRtn = SendBinaryAndOrReceiveResponse((u8 *)&params, sizeof(params), NULL, 0);
    if (nRtn < 0) return nRtn;
    u32 nSectors = pArea->nMaxSize / ImageGetBlockSize();
    nRtn = WaitForFlasherAck(300 + 2*nSectors);
    if (nRtn < 0) {
        if (nRtn == -EBADMSG) {
            printf("Flash verification failure\n");
        }
        return nRtn;
    }
    return 0;
}

static int FlashReadChunk(Image *pImage, u32 chunkOffset, u32 chunkSize) {
    if (chunkOffset > pImage->nSize) {
        printf("Reading beyond image size %d by %d\n", pImage->nSize, chunkOffset - pImage->nSize);
        return -EINVAL;
    }

    if (chunkOffset + chunkSize > pImage->nSize) {
        chunkSize = pImage->nSize - chunkOffset;
    }

    if (chunkSize % kFlashPageSize != 0) {
        printf("Chunk size %d is not a multiple of page size %d\n", chunkSize, kFlashPageSize);
        return -EINVAL;
    }

    u32 params[2] = {0, 0};
    u8 *pData = pImage->pData + chunkOffset;
    int nRtn = SendFlasherCommand(kCommandBlockID_FlashReadData, params, kCommandBlockFlags_ReadFromDevice, chunkSize);

    if (nRtn < 0){
        printf("Error sending read binary data command: %d\n", nRtn);
        return nRtn;
    }

    nRtn = WaitForBinaryData(pData, chunkSize, 100);
    if (nRtn < 0) {
        printf("Error reading data: %d\n", nRtn);
        return nRtn;
    }

    nRtn = WaitForFlasherAck(100);
    if (nRtn < 0) {
        printf("Error waiting for read command ack: %d\n", nRtn);
        return nRtn;
    }
    return chunkSize;
}

static int FlashRead(Area * pArea, Image *pImage) {
    BinParams param = {0};
    if (!pArea || !pImage) {
        printf("Invalid area or image\n");
        return -EINVAL;
    }

    param.dataLength = ImageHostToDeviceU32(NULL, pArea->nMaxSize);             // Not current used, set to parition size
    param.partitionFlashOffset = ImageHostToDeviceU32(NULL, pArea->nOffset);
    param.partitionSize = ImageHostToDeviceU32(NULL, pArea->nMaxSize);
    param.medium_type = kMediumType_SPI_NOR;        // Only SPI NOR supported

    int nRtn = SendFlasherCommand(kCommandBlockID_FlashReadStart, NULL, kCommandBlockFlags_WriteFromHost, sizeof(BinParams));
    if (nRtn < 0) {
        printf("Error starting read command: %d\n", nRtn);
        return nRtn;
    }

    nRtn = SendBinaryAndOrReceiveResponse(NULL, 0, "PR_T ++start upload bin++", 100);
    if (nRtn < 0) {
        printf("Error matching start read response: %d\n", nRtn);
        return nRtn;
    }

    nRtn = SerialSend((u8 *)&param, sizeof(BinParams));
    if (nRtn < 0) {
        printf("Error sending read parameters: %d\n", nRtn);
        return nRtn;
    }

    nRtn = WaitForFlasherAck(500); // Allow for verbosity from the flasher.
    if (nRtn < 0) {
        printf("Error waiting for read command ack: %d\n", nRtn);
        return nRtn;
    }

    // Read the data in chunks
    u32 readOffset = 0;
    u32 rem = pArea->nMaxSize;
    int nMinPct = 10;
    while (rem > 0) {
        u32 chunkSize = rem;
        if (chunkSize > kFlashReadChunkSize) {
            chunkSize = kFlashReadChunkSize;
        }
        nRtn = FlashReadChunk(pImage, readOffset, chunkSize);
        if (nRtn < 0) {
            printf("Error reading chunk at offset %d: %d\n", readOffset, nRtn);
            return nRtn;
        }
        readOffset += nRtn;
        rem -= nRtn;
        int nPct = (100 * (pArea->nMaxSize - rem)) / pArea->nMaxSize;
        if (nPct >= nMinPct) {
            nMinPct += 10;
            printf("%d%% ", nPct);
            fflush(stdout);
        }
    }
    printf("\n");
    return 0;
}

static int Open(const char * pDeviceName, bool bAutoProgram, const char * pPlatformArgs) {
    if (!bAutoProgram) printf("Auto-programming features not supported on this platform\n");
    if (pPlatformArgs && pPlatformArgs[0] != '\0') {
        printf("Platform-specific flags do not exist for this device\n");
        return -1;
    }
    /* Check device type */
    int nDeviceTypeIdx = GetDeviceTypeIndex(ImageGetDeviceType());
    if (nDeviceTypeIdx < 0) return nDeviceTypeIdx;
    int nRtn = SerialOpen(pDeviceName, kInitialBaudRate, kDefaultSerialTimeoutMS);
    if (nRtn < 0) return nRtn;
    return TargetInit((u32)nDeviceTypeIdx);
}

static void Close() {
    SerialClose();
}

static int Program(u32 nAreaIdx, char * pFilename, bool bPad, bool bAutoReboot) {
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
            if (nRtn == 0) {
                if (bAutoReboot) nRtn = Reboot();
            }
        }
    }
    ImageFree(&image);
    return nRtn;
}

static int Read(u32 nAreaIdx, char * pFilename, bool bAutoReboot) {
    Area * pArea = ImageGetArea(nAreaIdx);
    if (!pArea) {
        printf("Invalid area index %d\n", nAreaIdx);
        return -1;
    }

    Image image;
    if (ImageCreateBlank(&image, pArea->nMaxSize) < 0) {
        printf("Error creating image for reading\n");
        return -1;
    }

    printf("Reading %s [%08X][%08X]\n", pArea->name, pArea->nOffset, pArea->nMaxSize);
    int nRet = FlashRead(pArea, &image);
    if (nRet == 0) {
        nRet = ImageWriteToFile(&image, pFilename);
        if (nRet < 0) {
            printf("Error saving image to %s\n", pFilename);
        } else {
            printf("Image saved to %s\n", pFilename);
        }
    }

    ImageFree(&image);
    return nRet;
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
    u32 params[2] = { 0, 0 };
    return SendFlasherCommand(kCommandBlockID_Reset, params, kCommandBlockFlags_CommandOnly, 0);
}

FlashDeviceInterface FlashDeviceInterface_anyka = {
    &Open,
    &Close,
    &Program,
    &Read,
    &Erase,
    &Reboot,
};

const FlashDeviceInterfaceMapEntry FlashDeviceMapEntry_anyka = {
    .pFamily = "anyka",
    .pDevice = NULL,
    .pLegacyDevice = NULL,
    .pInterface = &FlashDeviceInterface_anyka
};

static void LT_USED_CONSTRUCTOR FlashDevice_RegisterSelf_anyka(void) {
    (void)FlashDevice_Register(&FlashDeviceMapEntry_anyka);
}
