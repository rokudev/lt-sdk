/******************************************************************************
 * rtl87x2x.c                                    Realtek RTL87x2x Flash Support
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "lt/LTTypes.h"
#include "lt/core/LTCore.h"

#include "Image.h"
#include "FlashDevice.h"
#include "Serial.h"

enum {
    kNominalBaudRate                = 1000000,
    kInitialBaudRate                = 115200,

    /* Serial Timeouts (milliseconds) */
    kDefaultSerialTimeoutMS         = 300,
    kEraseSerialTimeoutPerSectorMS  = 50,
    kVerifySerialTimeoutPerSectorMS = 40,
    kWriteSerialTimeoutMS           = 500,

    kFlasherAckPayloadSize          = 5,

    kFlashOffset                    = 0x00800000,
    kFlashSectorSize                = 4096
};

/* Serial commands (ROM) */
typedef u8 CommandID_ROM;
enum CommandID_ROM {
    kCommandID_ROM_Identify         = 0x61,  /* Identify chip */
    kCommandID_ROM_LoadFirmware     = 0x20,  /* Load firmware to RAM */
    kCommandID_ROM_RunFirmware      = 0x62,  /* Run firmware in RAM */
};

/* Serial commands (Flasher) */
typedef u8 CommandID_Flasher;
enum CommandID_Flasher {
    kCommandID_Flasher_BootMessage  = 0x00,  /* Flasher boot response */
    kCommandID_Flasher_SetBaudRate  = 0x10,  /* Set serial baud rate */
    kCommandID_Flasher_EraseSector  = 0x30,  /* Erase flash page(s) */
    kCommandID_Flasher_EraseBlock   = 0x35,  /* TODO: Erase 64k flash block */
    kCommandID_Flasher_Write        = 0x32,  /* Write data to flash */
    kCommandID_Flasher_Verify       = 0x50,  /* Verify data in flash */
    kCommandID_Flasher_Reboot       = 0x41,  /* Reboot chip */
};

typedef struct {
    u8                 one;   /* 0x01 */
    CommandID_ROM      id;    /* The command ID */
    u8                 hfc;   /* 0xfc */
    u8                 len;   /* Payload length */
} CommandHeaderROM;

typedef struct {
    u8                 four;  /* 0x04 */
    u8                 e;     /* 0x0e */
    u8                 len;   /* Payload length (including next four header bytes that follow) */
    u8                 two;   /* 0x02 */
    CommandID_ROM      id;    /* The response ID: should match command ID */
    u8                 hfc;   /* 0xfc */
    u8                 zero;  /* 0x00 */
} ResponseHeaderROM;

typedef struct __attribute__((packed)) {
    u8                 h87;   /* 0x87 */
    CommandID_Flasher  id;    /* The command or response ID */
    u8                 h10;   /* 0x10 */
} HeaderFlasher;

typedef struct {
    u32   chipID;
    char *name;
    char *flasher;
} Device;

/* Device Types */
enum { kDeviceType_Total = 1 };
static const Device s_devices[kDeviceType_Total] = {
    { 0x46209101,  "rtl8752h",  "RTL8762H_FW_A" }     /* 0: RTL8752H */
};

static int SendROMCommandAndReceiveResponse(u8 *cmd, u32 cmdLen, u8 *resp, u32 respLen) {
    CommandHeaderROM  *hdrCmd  =  (CommandHeaderROM *)cmd;
    ResponseHeaderROM *hdrResp = (ResponseHeaderROM *)resp;
    int nRtn = SerialSend(cmd, cmdLen);
    if (nRtn < 0) return nRtn;
    nRtn = SerialRecv(resp, respLen);
    if (nRtn < 0) return nRtn;
    /* Check response header values and length */
    if (hdrResp->four != 4 || hdrResp->e != 0xe || hdrResp->two != 0x2 || hdrResp->hfc != 0xfc || hdrResp->zero != 0) {
        return -EBADMSG;
    }
    /* Check response length and expected command ID */
    if (respLen != hdrResp->len + 3 || hdrCmd->id != hdrResp->id) {
        return -EBADMSG;
    }
    return 0;
}

static int IdentifyChip(void) {
    /* Identify command */
    struct {
        CommandHeaderROM hdr;
        struct {
            u8 data[5];
        } pl;
    } identifyCmd = {
        .hdr.one    = 0x01,
        .hdr.hfc    = 0xfc,
        .hdr.id     = kCommandID_ROM_Identify,
        .hdr.len    = sizeof(identifyCmd.pl),
        .pl.data[0] = 0x20,  /* unknown field */
        .pl.data[1] = 0x00,  /* unknown field */
        .pl.data[2] = 0x20,  /* unknown field */
        .pl.data[3] = 0x03,  /* unknown field */
        .pl.data[4] = 0x00   /* unknown field */
    };
    /* Identify response */
    struct {
        ResponseHeaderROM hdr;
        struct {
            u8 data[4];
        } pl;
    } identifyResp;
    int nRtn = SendROMCommandAndReceiveResponse((u8 *)&identifyCmd, sizeof(identifyCmd), (u8 *)&identifyResp, sizeof(identifyResp));
    if (nRtn < 0) return nRtn;
    u32 chipID = identifyResp.pl.data[0];
    chipID    += identifyResp.pl.data[1] << 8;
    chipID    += identifyResp.pl.data[2] << 16;
    chipID    += identifyResp.pl.data[3] << 24;
    for (u32 device = 0; device < kDeviceType_Total; device++) {
        if (chipID == s_devices[device].chipID) return device;
    }
    LT_GetCore()->ConsolePrint("RTL87x2x Chip ID 0x%08x not supported\n", chipID);
    return -1;
}

static int RunFlasher(void) {
    /* Run Firmware command */
    struct {
        CommandHeaderROM hdr;
        struct {
            u8 data[9];
        } pl;
    } runCmd = {
        .hdr.one    = 0x01,
        .hdr.hfc    = 0xfc,
        .hdr.id     = kCommandID_ROM_RunFirmware,
        .hdr.len    = sizeof(runCmd.pl),
        .pl.data[0] = 0x20,  /* unknown field */
        .pl.data[1] = 0xcc,  /* unknown field */
        .pl.data[2] = 0x0a,  /* unknown field */
        .pl.data[3] = 0x20,  /* unknown field */
        .pl.data[4] = 0x00,  /* unknown field */
        .pl.data[5] = 0x31,  /* unknown field */
        .pl.data[6] = 0x48,  /* unknown field */
        .pl.data[7] = 0x20,  /* unknown field */
        .pl.data[8] = 0x00   /* unknown field */
    };
    /* Run response */
    struct {
        ResponseHeaderROM hdr;
        /* No payload */
    } runResp;
    int nRtn = SendROMCommandAndReceiveResponse((u8 *)&runCmd, sizeof(runCmd), (u8 *)&runResp, sizeof(runResp));
    if (nRtn < 0) return nRtn;
    return 0;
}

static int DownloadAndRunFlasher(u32 device) {
    /* Get flasher image and flash AVL data and append them together */
    Image flasherImage;
    int nRtn = ImageGetFlasherImage(&flasherImage, s_devices[device].flasher);
    if (nRtn < 0) {
        LT_GetCore()->ConsolePrint("Cannot find flasher image %s\n", s_devices[device].flasher);
        return nRtn;
    }
    Image avlImage;
    nRtn = ImageGetFlasherImage(&avlImage, "FLASH_AVL");
    if (nRtn < 0) {
        LT_GetCore()->ConsolePrint("Cannot find AVL image\n");
        ImageFree(&flasherImage);
        return nRtn;
    }
    nRtn = ImageAppend(&flasherImage, &avlImage);
    if (nRtn < 0) {
        ImageFree(&flasherImage);
        ImageFree(&avlImage);
        return nRtn;
    }
    ImageFree(&avlImage);
    /* Load firmware command */
    enum { kMaxFirmwareChunkSize = 252 };
    struct {
        CommandHeaderROM hdr;
        struct {
            u8 ctr;
            u8 data[kMaxFirmwareChunkSize];
        } pl;
    } loadCmd = {
        .hdr.one = 0x01,
        .hdr.hfc = 0xfc,
        .hdr.id  = kCommandID_ROM_LoadFirmware
    };
    /* Load firmware response */
    struct {
        ResponseHeaderROM hdr;
        struct {
            u8 ctr;
        } pl;
    } loadResp;
    /* Flash image, one block at a time */
    LT_GetCore()->ConsolePrint("Injecting flashloader image into RAM\n");
    u8 *pData   = flasherImage.pData;
    u32 nRem    = flasherImage.nSize;
    u8  ctr     = 0;
    int nMinPct = 10;
    while (nRem > 0) {
        u32 chunkSize = kMaxFirmwareChunkSize;
        if (nRem < kMaxFirmwareChunkSize) chunkSize = nRem;
        u32 cmdLen      = sizeof(CommandHeaderROM) + chunkSize + 1;
        loadCmd.hdr.len = chunkSize + 1;
        loadCmd.pl.ctr  = ctr;
        lt_memcpy(loadCmd.pl.data, pData, chunkSize);
        int nRtn = SendROMCommandAndReceiveResponse((u8 *)&loadCmd, cmdLen, (u8 *)&loadResp, sizeof(loadResp));
        if (nRtn < 0) return nRtn;
        if (loadResp.pl.ctr != ctr) {
            LT_GetCore()->ConsolePrint("Load error, aborting...\n");
            return -1;
        }
        pData += chunkSize;
        nRem  -= chunkSize;
        ctr++;
        int nPct = 100 * (flasherImage.nSize - nRem) / flasherImage.nSize;
        if (nPct >= nMinPct) {
            LT_GetCore()->ConsolePrint("%d%% ", nPct);
            nMinPct = nPct + 10;
        }
    }
    LT_GetCore()->ConsolePrint("\n");
    ImageFree(&flasherImage);
    return RunFlasher();
}

static void Crc16_ARC(u16 *crc_, u8 *data, u32 nSizeInBytes) {
    const u16 bitReversedPoly = 0xa001;
    u16 crc = *crc_;
    for (u32 byte = 0; byte < nSizeInBytes; byte++) {
        crc ^= data[byte];
        for (u32 bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (bitReversedPoly & -(crc & 0x1));
        }
    }
    *crc_ = crc;
}

static int SendFlasherCommand(CommandID_Flasher id, u8 *payload, u32 len) {
    HeaderFlasher hdr = { .h87 = 0x87, .id = id, .h10 = 0x10 };
    u16 crc = 0;
    Crc16_ARC(&crc, (u8 *)&hdr, sizeof(hdr));
    Crc16_ARC(&crc, payload, len);
    int nRtn = SerialSend((u8 *)&hdr, sizeof(hdr));
    if (nRtn < 0) return nRtn;
    nRtn = SerialSend(payload, len);
    if (nRtn < 0) return nRtn;
    nRtn = SerialSend((u8 *)&crc, sizeof(crc));
    if (nRtn < 0) return nRtn;
    return 0;
}

static int RecvFlasherResponse(CommandID_Flasher id, u8 *payload, u32 len) {
    HeaderFlasher hdr;
    int nRtn = SerialRecv((u8 *)&hdr, sizeof(hdr));
    if (nRtn < 0) return nRtn;
    nRtn = SerialRecv(payload, len);
    if (nRtn < 0) return nRtn;
    u16 crc = 0;
    Crc16_ARC(&crc, (u8 *)&hdr, sizeof(hdr));
    Crc16_ARC(&crc, payload, len);
    u16 crcRecv;
    nRtn = SerialRecv((u8 *)&crcRecv, sizeof(crcRecv));
    if (nRtn < 0) return nRtn;
    if (hdr.h87 != 0x87 || hdr.h10 != 0x10) {
        printf("Bad response header\n");
        return -EBADMSG;
    }
    if (hdr.id != id) {
        printf("Bad command ID in response\n");
        return -EBADMSG;
    }
    if (crc != crcRecv) {
        printf("Bad CRC in response\n");
        return -EBADMSG;
    }
    if (payload[0] != 0) {
        printf("Command failed\n");
        return -EBADMSG;
    }
    return 0;
}

static int SetBaudRate(u32 baudRate) {
    struct __attribute__((packed)) {
        u32 baudRate;
        u8  hff;
    } setBaudRateCmd = {
        .baudRate = ImageHostToDeviceU32(NULL, baudRate),
        .hff      = 0xff   /* Unknown field */
    };
    int nRtn = SendFlasherCommand(kCommandID_Flasher_SetBaudRate, (u8 *)&setBaudRateCmd, sizeof(setBaudRateCmd));
    if (nRtn < 0) return nRtn;
    u8 resp[kFlasherAckPayloadSize];
    nRtn = RecvFlasherResponse(kCommandID_Flasher_SetBaudRate, resp, sizeof(resp));
    if (nRtn < 0) return nRtn;
    return SerialSetSpeed(baudRate);
}

static int TargetInit(bool bAutoProgram) {
    if (bAutoProgram) {
        // Place in programming mode
        printf("Place device in programming mode...\n");
        SerialSetRTS(false);
        usleep(150000);
        SerialSetRTS(true);
        usleep(500000);
    }
    int nRtn = IdentifyChip();
    if (nRtn < 0) return nRtn;
    u32 device = nRtn;
    nRtn = DownloadAndRunFlasher(device);
    if (nRtn < 0) return nRtn;
    u8 resp[81];     /* Unknown fields */
    nRtn = RecvFlasherResponse(kCommandID_Flasher_BootMessage, resp, sizeof(resp));
    if (nRtn < 0) return nRtn;
    return SetBaudRate(kNominalBaudRate);
}

static int EraseFlash(Area * pArea, Image * pImage) {
    u32 nSectors;
    if (pImage) {
        nSectors = (pImage->nSize + kFlashSectorSize - 1) / kFlashSectorSize;
    } else {
        nSectors = pArea->nMaxSize / kFlashSectorSize;
    }
    struct __attribute__((packed)) {
        u32 address;
        u32 len;
    } eraseCmd = {
        .address = ImageHostToDeviceU32(NULL, kFlashOffset + pArea->nOffset),
        .len     = ImageHostToDeviceU32(NULL, nSectors * kFlashSectorSize)
    };
    int nRtn = SerialSetTimeout(kEraseSerialTimeoutPerSectorMS * nSectors);
    if (nRtn < 0) return nRtn;
    nRtn = SendFlasherCommand(kCommandID_Flasher_EraseSector, (u8 *)&eraseCmd, sizeof(eraseCmd));
    if (nRtn < 0) return nRtn;
    u8 eraseResp[kFlasherAckPayloadSize];
    nRtn = RecvFlasherResponse(kCommandID_Flasher_EraseSector, eraseResp, sizeof(eraseResp));
    if (nRtn < 0) return nRtn;
    return SerialSetTimeout(kDefaultSerialTimeoutMS);
}

static int ProgramFlash(Area * pArea, Image * pImage) {
    int nRtn = EraseFlash(pArea, pImage);
    if (nRtn < 0) return nRtn;
    nRtn = SerialSetTimeout(kWriteSerialTimeoutMS);
    if (nRtn < 0) return nRtn;
    u8 *pData   = pImage->pData;
    u32 nRem    = pImage->nSize;
    int nMinPct = 10;
    while (nRem > 0) {
        u32 chunkSize = kFlashSectorSize;
        if (nRem < kFlashSectorSize) chunkSize = nRem;
        struct __attribute__((packed)) {
            u32 address;
            u32 len;
            u8  payload[kFlashSectorSize];
        } writeCmd = {
            .address = ImageHostToDeviceU32(NULL, kFlashOffset + pArea->nOffset + (pData - pImage->pData)),
            .len     = ImageHostToDeviceU32(NULL, chunkSize)
        };
        lt_memcpy(writeCmd.payload, pData, chunkSize);
        nRtn = SendFlasherCommand(kCommandID_Flasher_Write, (u8 *)&writeCmd, 2*sizeof(u32) + chunkSize);
        if (nRtn < 0) return nRtn;
        u8 writeResp[kFlasherAckPayloadSize];
        nRtn = RecvFlasherResponse(kCommandID_Flasher_Write, (u8 *)&writeResp, sizeof(writeResp));
        if (nRtn < 0) return nRtn;
        pData += chunkSize;
        nRem  -= chunkSize;
        int nPct = 100 * (pImage->nSize - nRem) / pImage->nSize;
        if (nPct >= nMinPct) {
            LT_GetCore()->ConsolePrint("%d%% ", nPct);
            nMinPct = nPct + 10;
        }
    }
    LT_GetCore()->ConsolePrint("\n");
    return SerialSetTimeout(kDefaultSerialTimeoutMS);
}

static int VerifyFlash(Area * pArea, Image * pImage) {
    u16 crc = 0;
    Crc16_ARC(&crc, pImage->pData, pImage->nSize);
    struct __attribute__((packed)) {
        u32 address;
        u32 len;
        u16 crc;
    } verifyCmd = {
        .address = ImageHostToDeviceU32(NULL, kFlashOffset + pArea->nOffset),
        .len     = ImageHostToDeviceU32(NULL, pImage->nSize),
        .crc     = ImageHostToDeviceU16(NULL, crc)
    };
    u32 nSectors = (pImage->nSize + kFlashSectorSize - 1) / kFlashSectorSize;
    int nRtn = SerialSetTimeout(kVerifySerialTimeoutPerSectorMS * nSectors);
    if (nRtn < 0) return nRtn;
    nRtn = SendFlasherCommand(kCommandID_Flasher_Verify, (u8 *)&verifyCmd, sizeof(verifyCmd));
    if (nRtn < 0) return nRtn;
    u8 verifyResp[kFlasherAckPayloadSize];
    nRtn = RecvFlasherResponse(kCommandID_Flasher_Verify, (u8 *)&verifyResp, sizeof(verifyResp));
    if (nRtn < 0) return nRtn;
    return SerialSetTimeout(kDefaultSerialTimeoutMS);
}

static int Open(const char * pDeviceName, bool bAutoProgram, const char * pPlatformArgs) {
    if (!bAutoProgram) printf("Auto-programming features not supported on this platform\n");
    if (pPlatformArgs && pPlatformArgs[0] != '\0') {
        printf("Platform-specific flags do not exist for this device\n");
        return -1;
    }
    int err = SerialOpen(pDeviceName, kInitialBaudRate, kDefaultSerialTimeoutMS);
    if (err < 0) return err;
    return TargetInit(bAutoProgram);
}

static void Close() {
    SerialClose();
}

static int Reboot(void) {
    struct __attribute__((packed)) {
        u8 three;
    } rebootCmd = {
        .three = 3
    };
    int nRtn = SendFlasherCommand(kCommandID_Flasher_Reboot, (u8 *)&rebootCmd, sizeof(rebootCmd));
    if (nRtn < 0) return nRtn;
    u8 rebootResp[kFlasherAckPayloadSize];
    nRtn = RecvFlasherResponse(kCommandID_Flasher_Reboot, (u8 *)&rebootResp, sizeof(rebootResp));
    if (nRtn < 0) return nRtn;
    printf("Rebooting...\n");
    return 0;
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
            if (nRtn == 0 && bAutoReboot) {
                nRtn = Reboot();
            }
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

FlashDeviceInterface FlashDeviceInterface_rtl87x2x = {
    &Open,
    &Close,
    &Program,
    &Read,
    &Erase,
    &Reboot,
};

const FlashDeviceInterfaceMapEntry FlashDeviceMapEntry_rtl87x2x = {
    .pFamily = "rtl87x2x",
    .pDevice = NULL,
    .pLegacyDevice = NULL,
    .pInterface = &FlashDeviceInterface_rtl87x2x
};

static void LT_USED_CONSTRUCTOR FlashDevice_RegisterSelf_rtl87x2x(void) {
    (void)FlashDevice_Register(&FlashDeviceMapEntry_rtl87x2x);
}

