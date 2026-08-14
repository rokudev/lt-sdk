/******************************************************************************
 * esp32.c                                                  ESP32 Flash Support
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>

#include "lt/LTTypes.h"

#include "Image.h"
#include "FlashDevice.h"
#include "Serial.h"

enum {
    kFlashSectorSize               = 4096,
    kFlashMaxWriteSize             = 4096,
    kFlashMaxReadSize              = 1024,
    kMemoryMaxWriteSize            = 0x1800,
    kDataHeaderSize                = 16,

    kBaudRateInitial               = 115200,
    kBaudRateFastROM               = 460800,
    kBaudRateFastFlasher           = 1500000,

    kResponseSizeROM               = 12,
    kResponseSizeFlasher           = 10,

    // SPIFlashVerifyMD5 carries its digest inside the response frame, ahead of
    // the status bytes, so its response is this much longer than the others.
    kResponseSizeMD5Extra          = 16,
    kResponseSizeMax               = kResponseSizeROM + kResponseSizeMD5Extra,

    // Location of the chip magic word. This one address is common to every
    // ESP32-family part; all other registers below are chip-specific.
    kRegisterChipID                = 0x40001000,

    // Original ESP32 (Xtensa LX6) only. These addresses are unmapped on the
    // S3 and later, where a read would fault the ROM download loader.
    kRegisterESP32APBControlDate   = 0x3ff6607c,
    kRegisterESP32EFuseReadBase    = 0x3ff5a000,
    kRegisterESP32EFuseReadAESBase = 0x3ff5a038,

    kDefaultSerialTimeoutMS        = 100,   // 100 ms
    kMinEraseSerialTimeoutMS       = 5000,  // 5s (minimum)
    kEraseSerialTimeoutPerSectorMS = 40,    // 40ms per sector
    kFlashSerialTimeoutMS          = 500,   // 500 ms
    kMemorySerialTimeoutMS         = 500,   // 500 ms
};

// RTC_CNTL_SWD_AUTO_FEED_EN, too wide for an enum constant
#define kRtcSwdAutoFeedEnable   0x80000000u

#define countof(x)              (sizeof(x) / sizeof((x)[0]))

#define EFUSE_READ_REG(n)       (kRegisterESP32EFuseReadBase + (sizeof(u32)*(n)))
#define EFUSE_READ_AES_REG(n)   (kRegisterESP32EFuseReadAESBase + (sizeof(u32)*(n)))

// ESP32 uses RFC1055 (SLIP) for serial communications
typedef u8 SLIPCharCode;
enum SLIPCharCode {
    kSLIPCharCode_End             = 0xc0,
    kSLIPCharCode_Escape          = 0xdb,
    kSLIPCharCode_EscapeEnd       = 0xdc,
    kSLIPCharCode_EscapeEscape    = 0xdd
};

// Serial protocol command operations
typedef u8 CommandID;
enum CommandID {
    kCommandID_Sync                = 0x08,  // Initial sync with SoC

    kCommandID_ReadReg             = 0x0a,  // Read a register
    kCommandID_WriteReg            = 0x09,  // Write a register
    kCommandID_MemoryBegin         = 0x05,  // Configure memory write
    kCommandID_MemoryData          = 0x07,  // Write memory
    kCommandID_MemoryEnd           = 0x06,  // Reboot into new program

    kCommandID_ChangeBaudRate      = 0x0f,  // Change serial speed

    kCommandID_SPIFlashAttach      = 0x0d,  // Attach SPI flash
    kCommandID_SPIFlashSetParams   = 0x0b,  // Set SPI params
    kCommandID_SPIFlashBegin       = 0x02,  // Erase flash sectors and configure subsequent write
    kCommandID_SPIFlashData        = 0x03,  // Write up to one flash sector  (per command)
    kCommandID_SPIFlashEnd         = 0x04,  // Leave flash write mode
    kCommandID_SPIFlashAutoEncData = 0xd4,  // Encrypt/Write up to one flash sector (per command)

    kCommandID_SPIFlashRead        = 0xd2,  // Read flash
    kCommandID_SPIFlashVerifyMD5   = 0x13,  // MD5 digest of a flash region
};

// Serial protocol header
typedef struct __attribute__((packed)) CommandHeader {
    u8         nPad0;
    CommandID  nCommandID;
    u8         nLengthLo;
    u8         nLengthHi;
    u8         nChecksum;
    u8         nPad1[3];
    u8         payload[0];
} CommandHeader;

typedef u32 static_assert_CommandHeader[(sizeof(CommandHeader) == 8) ? 0 : -1];

// Supported ESP32 ChipIDs
typedef u32 ChipID;
enum ChipID {
    kChipID_esp32       = 0x00f01d83,
    kChipID_esp32s3     = 0x00000009,
};

//
// Per-chip register locations and serial protocol quirks. Anything that differs
// between ESP32-family parts belongs here rather than in the flow above, so that
// adding a part is a table entry plus a flasher stub.
//
typedef struct ChipInfo {
    ChipID       nChipID;
    const char * pName;                    // Also selects the stub in LTFlasher.json
    bool         bEFuseRevision;           // Has the ESP32 eFuse revision/auto-encrypt layout
    bool         bRomFlashBeginEncryptArg; // ROM SPIFlashBegin takes a trailing 'encrypted' word
    // RTC watchdog registers, zero on parts with no native USB peripheral
    u32          nRegRtcWdtWProtect;
    u32          nRegRtcWdtConfig0;
    u32          nRtcWdtWriteKey;
    u32          nRegRtcSwdWProtect;
    u32          nRegRtcSwdConf;
    u32          nRtcSwdWriteKey;
} ChipInfo;

static const ChipInfo s_chipInfo[] = {
    {   // Original ESP32 (Xtensa LX6). Reached through an external USB-to-UART
        // bridge, so there is no native-USB watchdog quirk to work around.
        .nChipID                  = kChipID_esp32,
        .pName                    = "esp32",
        .bEFuseRevision           = true,
        .bRomFlashBeginEncryptArg = false,
    },
    {   // ESP32-S3 (Xtensa LX7)
        .nChipID                  = kChipID_esp32s3,
        .pName                    = "esp32s3",
        .bEFuseRevision           = false,
        .bRomFlashBeginEncryptArg = true,
        .nRegRtcWdtWProtect       = 0x600080b0,
        .nRegRtcWdtConfig0        = 0x60008098,
        .nRtcWdtWriteKey          = 0x50d83aa1,
        .nRegRtcSwdWProtect       = 0x600080b8,
        .nRegRtcSwdConf           = 0x600080b4,
        .nRtcSwdWriteKey          = 0x8f1d312a,
    },
};

static int Reboot(void);

static ChipID          s_nChipID;
static const ChipInfo *s_pChipInfo      = NULL;
static u8              s_nChipRevision;
static bool            s_bAutoEncrypt   = false;
static bool            s_bNostub        = false;
static bool            s_bNativeUSB     = false;
static int             s_nForceNativeUSB = -1;  // -1: auto-detect, 0: off, 1: on
static bool            s_flasherRunning = false;
static u8              s_response[kResponseSizeMax];

// Send an entire SLIP packet
//
// The frame is encoded into one buffer and written in a single call rather than
// a character at a time. On a native USB port every write becomes its own USB
// transaction, so sending byte by byte turns a six byte acknowledgement into six
// round trips and a stub upload into thousands.
enum { kSlipSendBufferSize = 2 * (kDataHeaderSize + kMemoryMaxWriteSize) + 2 };
static u8 s_slipSendBuffer[kSlipSendBufferSize];

static int SlipSend(u8 * pBufferToSend, u32 nLength) {
    if ((2 * nLength) + 2 > sizeof(s_slipSendBuffer)) return -EMSGSIZE;
    u32 nOut = 0;
    s_slipSendBuffer[nOut++] = kSLIPCharCode_End;
    for (u32 nIdx = 0; nIdx < nLength; nIdx++) {
        u8 nCh = pBufferToSend[nIdx];
        if (nCh == kSLIPCharCode_End) {
            s_slipSendBuffer[nOut++] = kSLIPCharCode_Escape;
            s_slipSendBuffer[nOut++] = kSLIPCharCode_EscapeEnd;
        } else if (nCh == kSLIPCharCode_Escape) {
            s_slipSendBuffer[nOut++] = kSLIPCharCode_Escape;
            s_slipSendBuffer[nOut++] = kSLIPCharCode_EscapeEscape;
        } else {
            s_slipSendBuffer[nOut++] = nCh;
        }
    }
    s_slipSendBuffer[nOut++] = kSLIPCharCode_End;
    return SerialSend(s_slipSendBuffer, (int)nOut);
}

// Receive an entire SLIP packet
static int SlipRecv(u8 * pRecvBuffer, u16 nMaxLength) {
    u16 nNumChars = 0;
    bool bEscaped = false;
    u8 nCh;
    int nRtn = SerialExpectChar(kSLIPCharCode_End);
    if (nRtn < 0) return nRtn;
    while (1) {
        nRtn = SerialRecv(&nCh, 1);
        if (nRtn < 0) return nRtn;
        if (bEscaped) {
            if (nCh == kSLIPCharCode_EscapeEscape) {
                if (nNumChars >= nMaxLength) return -EMSGSIZE;
                pRecvBuffer[nNumChars++] = kSLIPCharCode_Escape;
            } else if (nCh == kSLIPCharCode_EscapeEnd) {
                if (nNumChars >= nMaxLength) return -EMSGSIZE;
                pRecvBuffer[nNumChars++] = kSLIPCharCode_End;
            } else assert(0);
            bEscaped = false;
        } else if (nCh == kSLIPCharCode_Escape) {
            bEscaped = true;
        } else if (nCh == kSLIPCharCode_End) {
            break;
        } else {
            if (nNumChars >= nMaxLength) return -EMSGSIZE;
            pRecvBuffer[nNumChars++] = nCh;
        }
    }
    return (int)nNumChars;
}

// Returns the descriptor for _supported_ ESP32 ChipIDs
static const ChipInfo * GetChipInfoFromID(u32 nChipID) {
    for (u32 nIdx = 0; nIdx < countof(s_chipInfo); nIdx++) {
        if (s_chipInfo[nIdx].nChipID == nChipID) return &s_chipInfo[nIdx];
    }
    // Invalid or non-supported Chip-ID...
    return NULL;
}

// Serial protocol errors
static void PrintError(u16 nRtnCode) {
    static char * s_pErrorString[] = {
        "Invalid command",
        "Cannot act on command",
        "Invalid checksum",
        "Flash write error",
        "Flash read error",
        "Flash read length error",
        "Deflation error"
    };
    if (nRtnCode >= 5 && nRtnCode <= 11) {
        printf("%s Error\n", s_pErrorString[nRtnCode - 5]);
    } else {
        printf("Unknown Error Code: [%02x]\n", nRtnCode);
    }
}

// Single byte XOR checksum
static u8 CalcChecksum(u8 * pInputBuffer, u16 nLength) {
    u8 nTmp = 0xef;
    for (u32 nIdx = 0; nIdx < nLength; nIdx++) {
        nTmp ^= pInputBuffer[nIdx];
    }
    return nTmp;
}

// Send command and receive response
//
// nExtraResponseSize is the size of any in-frame payload the command answers
// with ahead of its status bytes; it is zero for everything but the MD5 digest.
static int
RunCommandWithResponse(CommandID nCommandID, u8 * pCommandPayload,
              u16 nCommandPayloadSize, u8 nChecksum, u16 nExtraResponseSize) {

    u32 nNumBytesToSend = sizeof(CommandHeader) + nCommandPayloadSize;
    u8 commandBuffer[nNumBytesToSend];

    // Create Command
    CommandHeader * pCommand = (CommandHeader *)&commandBuffer;
    pCommand->nPad0      = 0;
    pCommand->nCommandID = nCommandID;
    pCommand->nLengthLo  = (u8)(nCommandPayloadSize & 0xff);
    pCommand->nLengthHi  = (u8)(nCommandPayloadSize >> 8);
    pCommand->nChecksum  = nChecksum;
    pCommand->nPad1[0]   = 0;
    pCommand->nPad1[1]   = 0;
    pCommand->nPad1[2]   = 0;
    memcpy(pCommand->payload, pCommandPayload, nCommandPayloadSize);

    // Transmit Command
    int nRtn = SlipSend(commandBuffer, nNumBytesToSend);
    if (nRtn < 0) return nRtn;

    // Response size depends if running flasher or ROM code
    u16 nExpectedResponseSize = s_flasherRunning ? kResponseSizeFlasher : kResponseSizeROM;
    nExpectedResponseSize += nExtraResponseSize;

    // Receive Response
    nRtn = SlipRecv(s_response, nExpectedResponseSize);
    if (nRtn < 0) return nRtn;
    // Check response size
    if (nRtn != nExpectedResponseSize) return -EPROTO;

    u32 nRtnCode;
    if (s_flasherRunning) {
        // Return code is in the last 2 bytes (flasher)
        nRtnCode = (u32)ImageDeviceToHostU16(NULL, *((u16 *)(s_response + nRtn - 2)));
    } else {
        // Return code is in the last 4 bytes (ESP32 ROM code)
        nRtnCode = ImageDeviceToHostU32(NULL, *((u32 *)(s_response + nRtn - 4))) >> 8;
    }
    if (nRtnCode) {
        PrintError(nRtnCode);
        return -EINVAL;
    }
    // Response operation must match command
    if (s_response[1] != nCommandID) return -EINVAL;
    return nRtn;
}

static int
RunCommand(CommandID nCommandID, u8 * pCommandPayload,
              u16 nCommandPayloadSize, u8 nChecksum) {
    return RunCommandWithResponse(nCommandID, pCommandPayload, nCommandPayloadSize,
                                  nChecksum, 0);
}

// Sync with ROM firmware
static int Sync(void) {
    // Discard boot message
    SerialFlush();

    // Create sync message paylaod
    u8 syncPayload[36] = { 0x07, 0x07, 0x12, 0x20 };
    memset(syncPayload + 4, 0x55, sizeof(syncPayload) - 4);

    // Retry sync until we get a sync response, then flush serial
    const u32 nMaxTries = 20;
    for (u32 nTry = 0; nTry < nMaxTries; nTry++) {
         int nRtn = RunCommand(kCommandID_Sync, syncPayload, sizeof(syncPayload), 0);
         if (nRtn < 0) {
             // Perform retries during sync
             if (nRtn == -ETIMEDOUT) continue;
             else if ((nRtn == -EMSGSIZE || nRtn == -EPROTO) && nTry == 0) {
                 // Assume flasher is already running, and then test hypothesis
                 s_flasherRunning = true;
                 continue;
             } else return nRtn;
         } else if (nRtn >= 0) {
             usleep(100000);
             // Discard left-over junk from sync retries
             SerialFlush();
             return 0;
         }
    }
    return -ETIMEDOUT;
}

// Read register using ROM firmware
static int ReadRegister(u32 nAddress, u32 * pValue) {
    u32 readPayload = ImageHostToDeviceU32(NULL, nAddress);
    int nRtn = RunCommand(kCommandID_ReadReg, (u8 *)&readPayload, sizeof(readPayload), 0);
    if (nRtn < 0) return nRtn;
    ImageDeviceToHostU32(pValue, *((u32 *)(s_response + 4)));
    return 0;
}

// Write register using ROM firmware
static int WriteRegister(u32 nAddress, u32 nValue) {
    u32 writePayload[4];
    ImageHostToDeviceU32(writePayload, nAddress);
    ImageHostToDeviceU32(writePayload + 1, nValue);
    ImageHostToDeviceU32(writePayload + 2, 0xffffffff);  // Write mask
    ImageHostToDeviceU32(writePayload + 3, 0);           // Delay in microseconds
    return RunCommand(kCommandID_WriteReg, (u8 *)writePayload, sizeof(writePayload), 0);
}

//
// When the SoC is reached through its own USB peripheral the ROM leaves the RTC
// watchdog and the super watchdog running, and either one will reset the board
// part-way through a flash operation. Disable the RTC watchdog outright and put
// the super watchdog into auto-feed. Both sit behind write-protect keys.
//
static int DisableWatchdogs(void) {
    if (s_pChipInfo->nRegRtcWdtWProtect == 0) return 0;

    // Disable the RTC watchdog
    int nRtn = WriteRegister(s_pChipInfo->nRegRtcWdtWProtect, s_pChipInfo->nRtcWdtWriteKey);
    if (nRtn < 0) return nRtn;
    nRtn = WriteRegister(s_pChipInfo->nRegRtcWdtConfig0, 0);
    if (nRtn < 0) return nRtn;
    nRtn = WriteRegister(s_pChipInfo->nRegRtcWdtWProtect, 0);
    if (nRtn < 0) return nRtn;

    // Automatically feed the super watchdog
    nRtn = WriteRegister(s_pChipInfo->nRegRtcSwdWProtect, s_pChipInfo->nRtcSwdWriteKey);
    if (nRtn < 0) return nRtn;
    u32 nConf;
    nRtn = ReadRegister(s_pChipInfo->nRegRtcSwdConf, &nConf);
    if (nRtn < 0) return nRtn;
    nRtn = WriteRegister(s_pChipInfo->nRegRtcSwdConf, nConf | kRtcSwdAutoFeedEnable);
    if (nRtn < 0) return nRtn;
    return WriteRegister(s_pChipInfo->nRegRtcSwdWProtect, 0);
}

// Start memory write using ROM firmware
static int MemoryBegin(u32 nAddress, u32 nSize, u32 nNumBlocks) {
    u32 beginPayload[4];
    ImageHostToDeviceU32(beginPayload, nSize);                    // Data Size
    ImageHostToDeviceU32(beginPayload + 1, nNumBlocks);           // Num blocks
    ImageHostToDeviceU32(beginPayload + 2, kMemoryMaxWriteSize);  // Block size
    ImageHostToDeviceU32(beginPayload + 3, nAddress);             // Address to write
    return RunCommand(kCommandID_MemoryBegin, (u8 *)beginPayload, sizeof(beginPayload), 0);
}

// Write one block of memory using ROM firmware
static int MemoryWrite(u8 * pData, u32 nSize, u32 nBlockNo) {
    u8 writePayload[kDataHeaderSize + kMemoryMaxWriteSize];
    ImageHostToDeviceU32((u32 *)(writePayload), nSize);
    ImageHostToDeviceU32((u32 *)(writePayload + 4), nBlockNo);
    ImageHostToDeviceU32((u32 *)(writePayload + 8), 0);
    ImageHostToDeviceU32((u32 *)(writePayload + 12), 0);
    memcpy(writePayload + kDataHeaderSize, pData, nSize);
    u8 nChecksum = CalcChecksum(pData, nSize);
    return RunCommand(kCommandID_MemoryData, writePayload, kDataHeaderSize + nSize, nChecksum);
}

// Reboot into flasher from ROM firmware
static int RebootIntoFlasher(u32 nEntryAddress) {
    u32 rebootPayload[2];
    ImageHostToDeviceU32(rebootPayload, 0);                  // Boot into RAM
    ImageHostToDeviceU32(rebootPayload + 1, nEntryAddress);  // Image entrypoint
    int nRtn = RunCommand(kCommandID_MemoryEnd, (u8 *)rebootPayload, sizeof(rebootPayload), 0);
    if (nRtn < 0) return nRtn;

    // Check handshake
    u8 rebootHandshake[4];
    nRtn = SlipRecv(rebootHandshake, sizeof(rebootHandshake));
    if (nRtn < 0) return nRtn;
    if (strncmp((char *)rebootHandshake, "OHAI", 4) != 0) {
        printf("Flasher handshake failure\n");
        return -1;
    }
    return 0;
}

// Change serial speed (via flasher firmware and serial device)
static int ChangeSpeed(u32 nNewBaudRate) {
    static u32 s_nCurrentBaudRate = kBaudRateInitial;
    u32 speedPayload[2];
    ImageHostToDeviceU32(speedPayload, nNewBaudRate);
    ImageHostToDeviceU32(speedPayload + 1, s_nCurrentBaudRate);
    int nRtn = RunCommand(kCommandID_ChangeBaudRate, (u8 *)speedPayload, sizeof(speedPayload), 0);
    if (nRtn < 0) return nRtn;
    s_nCurrentBaudRate = nNewBaudRate;
    return SerialSetSpeed(nNewBaudRate);
}

static int InitSPIFlash(void) {
    if (!s_flasherRunning) {
        // Attach default flash
        u32 attachPayload[2];
        ImageHostToDeviceU32(attachPayload, 0);
        ImageHostToDeviceU32(attachPayload + 1, 0);
        int nRtn = RunCommand(kCommandID_SPIFlashAttach, (u8 *)attachPayload, sizeof(attachPayload), 0);
        if (nRtn < 0) return nRtn;
    }

    Area * pArea = ImageGetArea(kImageAreaAllIndex);
    if (!pArea) return -1;

    // Configure flash defaults (turns out many of these parameters are ignored)
    u32 configPayload[6];
    ImageHostToDeviceU32(configPayload, 0);                     // Flash ID
    ImageHostToDeviceU32(configPayload + 1, pArea->nMaxSize);   // Flash size
    ImageHostToDeviceU32(configPayload + 2, 65536);             // Block size
    ImageHostToDeviceU32(configPayload + 3, kFlashSectorSize); // Sector size
    ImageHostToDeviceU32(configPayload + 4, 256);              // Page size
    ImageHostToDeviceU32(configPayload + 5, 0xffff);           // Status mask
    return RunCommand(kCommandID_SPIFlashSetParams, (u8 *)configPayload, sizeof(configPayload), 0);
}

static int EraseFlash(Area * pArea, Image * pImage) {
    u32 beginPayload[5];
    u32 nPayloadSize = 4 * sizeof(u32);
    u32 nSectors;
    if (pImage) {
        ImageHostToDeviceU32(beginPayload, pImage->nSize);
        nSectors = (pImage->nSize + kFlashSectorSize - 1) / kFlashSectorSize;
    } else {
        ImageHostToDeviceU32(beginPayload, pArea->nMaxSize);
        nSectors = pArea->nMaxSize / kFlashSectorSize;
    }
    ImageHostToDeviceU32(beginPayload + 1, nSectors);
    ImageHostToDeviceU32(beginPayload + 2, kFlashSectorSize);
    ImageHostToDeviceU32(beginPayload + 3, pArea->nOffset);

    // The S3 and later ROM loaders take a trailing flag that selects encrypted
    // flash mode; the flasher stub does not. Encrypted writes always go through
    // the stub (see Esp32Program), so the flag is always clear here.
    if (s_pChipInfo->bRomFlashBeginEncryptArg && !s_flasherRunning) {
        ImageHostToDeviceU32(beginPayload + 4, 0);
        nPayloadSize = sizeof(beginPayload);
    }

    // Calculate and set timeout for erase operation
    u32 nSerialTimeoutMS = nSectors * kEraseSerialTimeoutPerSectorMS;
    if (nSerialTimeoutMS < kMinEraseSerialTimeoutMS) nSerialTimeoutMS = kMinEraseSerialTimeoutMS;
    int nRtn = SerialSetTimeout(nSerialTimeoutMS);
    if (nRtn < 0) return nRtn;

    // Erase
    nRtn = RunCommand(kCommandID_SPIFlashBegin, (u8 *)beginPayload, nPayloadSize, 0);
    if (nRtn < 0) return nRtn;

    // Reset timeout
    return SerialSetTimeout(kDefaultSerialTimeoutMS);
}

//
// Close out a flash write that went through the flasher stub.
//
// The stub answers each SPIFlashData command as soon as it has taken the block,
// not once the block has reached the flash: its handle_flash_data() waits for
// the part to go idle before the *next* write, so when the response to the last
// block arrives that block's page programs are still running. Reboot() then
// drops EN a few hundred microseconds later and the tail of the image is left
// unwritten -- a varying couple of hundred bytes of 0xff, which the LT
// bootloader reports as "Checksum failed. Calculated 0x.. read 0xff" while rit
// itself prints 100% and [OK]. That is the whole reason LT_NOSTUB=1 looked
// mandatory on this platform; the ROM download loader programs synchronously
// and so needs none of this.
//
// SPIFlashEnd leaves flash mode and hands back the error the stub deferred: a
// failed SPI operation sets fs.last_error and is reported here and nowhere
// else. SPIFlashVerifyMD5 is the barrier -- the stub's read path waits for the
// flash to go idle before it reads a byte -- so once the digest comes back the
// image is committed. The digest is not compared, rit has no MD5 of its own,
// but a read that fails still surfaces as a command error.
//
static int FinishFlashWrite(Area * pArea, Image * pImage) {
    if (!s_flasherRunning) return 0;

    u32 nEndPayload = ImageHostToDeviceU32(NULL, 1);  // Non-zero: do not reboot
    int nRtn = RunCommand(kCommandID_SPIFlashEnd, (u8 *)&nEndPayload, sizeof(nEndPayload), 0);
    if (nRtn < 0) return nRtn;

    // Hashing the last block written is enough. The wait for idle happens
    // before the stub reads its first byte, so the barrier does not get any
    // stronger by covering more, while hashing a whole image would need a
    // size-dependent timeout -- in large flash mode the stub reads through the
    // ROM OPI driver sixteen bytes per command, which is slow enough to trip
    // kFlashSerialTimeoutMS well before a megabyte is digested.
    u32 nBarrierSize = kFlashMaxWriteSize;
    if (pImage->nSize < nBarrierSize) nBarrierSize = pImage->nSize;

    u32 md5Payload[4];
    ImageHostToDeviceU32(md5Payload, pArea->nOffset + pImage->nSize - nBarrierSize);
    ImageHostToDeviceU32(md5Payload + 1, nBarrierSize);
    ImageHostToDeviceU32(md5Payload + 2, 0);
    ImageHostToDeviceU32(md5Payload + 3, 0);
    return RunCommandWithResponse(kCommandID_SPIFlashVerifyMD5, (u8 *)md5Payload,
                                  sizeof(md5Payload), 0, kResponseSizeMD5Extra);
}

//
// Classic auto-reset: DTR and RTS drive GPIO0 and EN through a transistor pair
// alongside the USB-to-UART bridge (ESP32 devkits, ESP32-CAM, ...).
//
static void ResetIntoDownloadModeClassic(void) {
    SerialSetDTR(false);   // IO0 = HIGH
    SerialSetRTS(true);    // EN  = LOW, chip in reset
    usleep(100000);
    SerialSetDTR(true);    // IO0 = LOW
    SerialSetRTS(false);   // EN  = HIGH, chip out of reset
    usleep(50000);
    SerialSetDTR(false);   // IO0 = HIGH, done
}

//
// Reset sequence for a SoC reached through its own USB-Serial/JTAG peripheral.
// There is no external reset circuit here: the peripheral interprets the CDC
// control-line state itself, and only latches the combination when the
// transition passes through (1,1). Going through (0,0), as the classic sequence
// does, drops the USB link instead of entering download mode.
//
// RTS is asserted twice on purpose -- Windows only propagates DTR when RTS is
// set, so the second write is what carries the new DTR state.
//
static void ResetIntoDownloadModeUSBJTAG(void) {
    SerialSetRTS(false);
    SerialSetDTR(false);   // Idle
    usleep(100000);
    SerialSetDTR(true);    // Set IO0
    SerialSetRTS(false);
    usleep(100000);
    SerialSetRTS(true);    // Reset, inverted order to pass through (1,1)
    SerialSetDTR(false);
    SerialSetRTS(true);
    usleep(100000);
    SerialSetDTR(false);
    SerialSetRTS(false);   // Chip out of reset
}

// Probe the ESP32 eFuses for the chip revision and ROM auto-encryption state
static int ReadESP32ChipRevision(bool * pbAutoEncryptAvailable) {
    u32 nTemp;
    s_nChipRevision = 0;
    int nRtn = ReadRegister(EFUSE_READ_REG(3), &nTemp);
    if (nRtn < 0) return nRtn;
    if ((nTemp >> 15) & 0x1) {
        s_nChipRevision = 1;
        nRtn = ReadRegister(EFUSE_READ_REG(5), &nTemp);
        if (nRtn < 0) return nRtn;
        if ((nTemp >> 20) & 0x1) {
            s_nChipRevision = 2;
            nRtn = ReadRegister(kRegisterESP32APBControlDate, &nTemp);
            if (nRtn < 0) return nRtn;
            if ((nTemp >> 31) & 0x1) {
                s_nChipRevision = 3;
            }
        }
    }
    printf("ESP32 chip detected: %s V%u\n", s_pChipInfo->pName, s_nChipRevision);

    if (s_nChipRevision == 3) {
        nRtn = ReadRegister(EFUSE_READ_REG(0), &nTemp);
        if (nRtn < 0) return nRtn;
        if ((nTemp >> 16) & 0x1) {
            *pbAutoEncryptAvailable = true;
            printf("Note: Auto-encryption available (flash AES key read protect set)\n");
        } else {
            for (u32 nIx = 0; nIx < 8; nIx++) {
                nRtn = ReadRegister(EFUSE_READ_AES_REG(nIx), &nTemp);
                if (nRtn < 0) return nRtn;
                if (nTemp) {
                    *pbAutoEncryptAvailable = true;
                    printf("Note: Auto-encryption available (flash AES key is non-zero)\n");
                    break;
                }
            }
        }
    }
    return 0;
}

static int TargetInit(bool bAutoProgram) {
    // Place in download mode (if auto programming enabled)
    if (bAutoProgram) {
        if (s_bNativeUSB) ResetIntoDownloadModeUSBJTAG();
        else              ResetIntoDownloadModeClassic();
    }

    // Sync with SoC ROM code
    int nRtn = Sync();
    if (nRtn < 0) return nRtn;

    // Read and verify the ChipID
    nRtn = ReadRegister(kRegisterChipID, &s_nChipID);
    if (nRtn < 0) return nRtn;
    s_pChipInfo = GetChipInfoFromID(s_nChipID);
    if (s_pChipInfo == NULL) {
        printf("ESP32 chip ID 0x%08x not supported\n", s_nChipID);
        return -1;
    }

    // Silence the watchdogs before they can reset the board mid-operation. The
    // stub does this for itself, so skip it if the stub is already resident.
    if (s_bNativeUSB && !s_flasherRunning) {
        nRtn = DisableWatchdogs();
        if (nRtn < 0) return nRtn;
    }

    // Determine chip revision and ROM auto-encryption state. The eFuse layout
    // below is specific to the original ESP32; on later parts the revision is
    // unused (it only selects a serial speed) so it is not worth probing.
    bool bAutoEncryptAvailable = false;
    if (s_pChipInfo->bEFuseRevision) {
        nRtn = ReadESP32ChipRevision(&bAutoEncryptAvailable);
        if (nRtn < 0) return nRtn;
    } else {
        s_nChipRevision = 0;
        printf("ESP32 chip detected: %s\n", s_pChipInfo->pName);
    }

    // Cancel requested auto-encrypt if not available
    if (s_bAutoEncrypt && !bAutoEncryptAvailable) {
        s_bAutoEncrypt = false;
        printf("Error, requested auto-encryption mode is NOT available...\n");
        return -1;
    }
    return 0;
}

//
// The ESP32 flasher image format starts with the entrypoint address and
// is followed by the image sections. Each section has a header with the
// start address and the section data size.
//
// ESP32 Flasher Image binary format:
//  Byte Offset
//     [0..3]   Image entry address (32-bit, little endian)
//     [4..7]   Section 0 start address (32-bit, little endian)
//     [8..11]  Section 0 size in bytes (32-bit, little endian)
//     [12..]   Section 0 data ... (data blob of above size)
//      ...     Section 1 start (NOTE: may not be aligned to word boundary)
//      ...     Section 1 size  (NOTE: may not be aligned to word boundary)
//      ...     Section 1 data ...
//            ...
//
static int Esp32LoadAndRunFlasher(void) {
    if (s_flasherRunning) return 0;
    if (s_bNostub) return -1;

    Image flasherImage;
    if (ImageGetFlasherImage(&flasherImage, s_pChipInfo->pName) < 0) {
        printf("No flasher image available for %s\n", s_pChipInfo->pName);
        return -1;
    }
    if (flasherImage.nSize < 12) {
        printf("Invalid flasher image\n");
        return -1;
    }
    u32 nEntryAddress = ImageDeviceToHostU32(NULL, *((u32 *)(flasherImage.pData)));
    u8 * pIn  = flasherImage.pData + sizeof(u32);
    u8 * pEnd = flasherImage.pData + flasherImage.nSize;
    while (pIn < pEnd) {
        if (pIn + 2*sizeof(u32) > pEnd) {
            printf("Bad or incomplete flasher section header\n");
            return -1;
        }
        u32 nAddress = ImageDeviceToHostU32(NULL, *((u32 *)(pIn)));
        u32 nSize    = ImageDeviceToHostU32(NULL, *((u32 *)(pIn + 4)));
        if (nSize == 0) {
            printf("Invalid flasher section header size\n");
            return -1;
        }
        pIn += 2*sizeof(u32);
        if (pIn + nSize > pEnd) {
            printf("Truncated flasher payload or invalid size\n");
            return -1;
        }
        // Write memory, a block at a time. Stub sections are small, but not always
        // small enough to fit a single block: the ESP32-S3 text section is 5244 bytes
        // against a 6144 byte block, and later parts are larger still.
        u32 nNumBlocks = (nSize + kMemoryMaxWriteSize - 1) / kMemoryMaxWriteSize;
        int nRtn = MemoryBegin(nAddress, nSize, nNumBlocks);
        if (nRtn < 0) {
            printf("Memory start failed\n");
            return nRtn;
        }
        // Lengthen timeout for memory write operation
        nRtn = SerialSetTimeout(kMemorySerialTimeoutMS);
        if (nRtn < 0) return nRtn;
        for (u32 nBlockNo = 0; nBlockNo < nNumBlocks; nBlockNo++) {
            u32 nOffset    = nBlockNo * kMemoryMaxWriteSize;
            u32 nBlockSize = nSize - nOffset;
            if (nBlockSize > kMemoryMaxWriteSize) nBlockSize = kMemoryMaxWriteSize;
            nRtn = MemoryWrite(pIn + nOffset, nBlockSize, nBlockNo);
            if (nRtn < 0) {
                printf("Memory write failed\n");
                return nRtn;
            }
        }
        nRtn = SerialSetTimeout(kDefaultSerialTimeoutMS);
        if (nRtn < 0) return nRtn;
        pIn += nSize;
    }
    if (pIn != pEnd) {
        printf("Warning: Extraneous characters after flasher payload\n");
    }
    // Reboot into the flasher program
    int nRtn = RebootIntoFlasher(nEntryAddress);
    if (nRtn < 0) return nRtn;

    s_flasherRunning = true;
    return 0;
}

static int Esp32Erase(Area * pArea) {
    // Attach and configure flash (defaults)
    int nRtn = InitSPIFlash();
    if (nRtn < 0) return nRtn;

    // Begin flash operation (erase)
    return EraseFlash(pArea, NULL);
}

static int Esp32GoToHighSpeed(void) {
    // A native USB connection is not a UART: the baud rate is a fiction on both
    // ends, and asking the ROM to change it only risks losing sync.
    if (s_bNativeUSB) return 0;

    // Go to high speed (if supported)
    if (s_flasherRunning && (!s_pChipInfo->bEFuseRevision || s_nChipRevision >= 2)) {
        return ChangeSpeed(kBaudRateFastFlasher);
    } else {
        return ChangeSpeed(kBaudRateFastROM);
    }
}

static int Esp32ReturnToInitialSpeed(void) {
    if (s_bNativeUSB) return 0;
    return ChangeSpeed(kBaudRateInitial);
}

static int Esp32Program(Area * pArea, Image * pImage) {
    // Load and Run Flasher
    int nRtn = Esp32LoadAndRunFlasher();
    if (nRtn < 0) {
        if (!s_bAutoEncrypt) {
            printf("Warning: Continuing without flasher image...\n");
        } else {
            printf("Error: Encrypted operations require flasher stub...\n");
            return -1;
        }
    }
    nRtn = Esp32GoToHighSpeed();
    if (nRtn < 0) return nRtn;

    // Configure flash (defaults)
    nRtn = InitSPIFlash();
    if (nRtn < 0) return nRtn;

    // Begin flash operation (erase)
    nRtn = EraseFlash(pArea, pImage);
    if (nRtn < 0) return nRtn;

    // Lengthen timeout for flash operation */
    nRtn = SerialSetTimeout(kFlashSerialTimeoutMS);
    if (nRtn < 0) return nRtn;

    u8 flashPayload[kDataHeaderSize + kFlashMaxWriteSize];
    ImageHostToDeviceU32((u32 *)(flashPayload + 8), 0);
    ImageHostToDeviceU32((u32 *)(flashPayload + 12), 0);

    u8 * pData = pImage->pData;
    u32 nRem = pImage->nSize;
    int nMinPct = 10;
    u32 nPktNo = 0;

    CommandID flashCommandID = kCommandID_SPIFlashData;
    if (s_bAutoEncrypt) {
        flashCommandID = kCommandID_SPIFlashAutoEncData;
        printf("Note: Flashing using auto-encryption mode\n");
    }

    while (nRem > 0) {
        // Program at most one sector at a time
        u32 nCopySize = kFlashMaxWriteSize;
        if (nRem < kFlashMaxWriteSize) {
            nCopySize = nRem;
        }
        ImageHostToDeviceU32((u32 *)(flashPayload + 0), nCopySize);
        ImageHostToDeviceU32((u32 *)(flashPayload + 4), nPktNo);
        memcpy(flashPayload + kDataHeaderSize, pData, nCopySize);

        u8 nChecksum = CalcChecksum(pData, nCopySize);
        nRtn = RunCommand(flashCommandID, flashPayload, kDataHeaderSize + nCopySize, nChecksum);
        if (nRtn < 0) return nRtn;

        nPktNo++;
        pData += nCopySize;
        nRem -= nCopySize;

        int nPct = 100 * (pImage->nSize - nRem) / pImage->nSize;
        if (nPct >= nMinPct) {
            printf("%d%% ", nPct);
            fflush(stdout);
            nMinPct = nPct + 10;
        }
    }
    printf("\n");

    // Commit the last block before anything can reset the chip
    nRtn = FinishFlashWrite(pArea, pImage);
    if (nRtn < 0) return nRtn;

    // Return to initial timeout and speed
    nRtn = SerialSetTimeout(kDefaultSerialTimeoutMS);
    if (nRtn < 0) return nRtn;

    return Esp32ReturnToInitialSpeed();
}

static int Esp32Read(Area * pArea, Image * pImage) {
    // Load and Run Flasher
    int nRtn = Esp32LoadAndRunFlasher();
    if (nRtn < 0) {
        printf("Error: Fast read operations require flasher stub...\n");
        return -1;
    }
    nRtn = Esp32GoToHighSpeed();
    if (nRtn < 0) return nRtn;

    // Configure flash (defaults)
    nRtn = InitSPIFlash();
    if (nRtn < 0) return nRtn;

    // Lengthen timeout for flash operation */
    nRtn = SerialSetTimeout(kFlashSerialTimeoutMS);
    if (nRtn < 0) return nRtn;

    u32 readPayload[4];
    ImageHostToDeviceU32(readPayload, pArea->nOffset);         // Offset
    ImageHostToDeviceU32(readPayload + 1, pArea->nMaxSize);    // Length in bytes
    ImageHostToDeviceU32(readPayload + 2, kFlashMaxReadSize);  // Block size
    ImageHostToDeviceU32(readPayload + 3, 64);                 // Max packets in flight
    nRtn = RunCommand(kCommandID_SPIFlashRead, (u8 *)readPayload, sizeof(readPayload), 0);
    if (nRtn < 0) return nRtn;

    u8 * pData = pImage->pData;
    u32 nReceived = 0;
    int nMinPct = 10;

    while (nReceived < pArea->nMaxSize) {
        // Receive a data packet
        nRtn = SlipRecv(pData, kFlashMaxReadSize);
        if (nRtn < 0) return nRtn;
        nReceived += nRtn;
        pData += nRtn;

        // Ack with total received bytes
        u32 nTemp = ImageHostToDeviceU32(NULL, nReceived);
        nRtn = SlipSend((u8 *)&nTemp, sizeof(nTemp));
        if (nRtn < 0) return nRtn;

        int nPct = 100 * nReceived / pImage->nSize;
        if (nPct >= nMinPct) {
            printf("%d%% ", nPct);
            fflush(stdout);
            nMinPct = nPct + 10;
        }
    }
    printf("\n");
    if (nReceived > pArea->nMaxSize) {
        printf("Received too many bytes...\n");
        return -1;
    }

    // Read and discard md5 digest (currently)
    u8 dummy[64];
    nRtn = SlipRecv(dummy, sizeof(dummy));
    if (nRtn < 0) return nRtn;

    // Return to initial timeout and speed
    nRtn = SerialSetTimeout(kDefaultSerialTimeoutMS);
    if (nRtn < 0) return nRtn;

    return Esp32ReturnToInitialSpeed();
}

//
// Platform-specific arguments, given to rit as a comma-separated list, e.g.
// "-x nostub,usbjtag":
//
//   encrypt    program through the ROM auto-encryption path
//   nostub     do not load or use the flasher stub image
//   usbjtag    force the native USB (USB-Serial/JTAG) reset and flash sequence
//   nousbjtag  force the classic DTR/RTS auto-reset sequence
//
static int ParsePlatformArgs(const char * pPlatformArgs) {
    while (pPlatformArgs && *pPlatformArgs != '\0') {
        const char * pComma = strchr(pPlatformArgs, ',');
        size_t nLength = pComma ? (size_t)(pComma - pPlatformArgs) : strlen(pPlatformArgs);
        if (nLength == 7 && strncmp(pPlatformArgs, "encrypt", nLength) == 0) {
            s_bAutoEncrypt = true;
        } else if (nLength == 6 && strncmp(pPlatformArgs, "nostub", nLength) == 0) {
            s_bNostub = true;
        } else if (nLength == 7 && strncmp(pPlatformArgs, "usbjtag", nLength) == 0) {
            s_nForceNativeUSB = 1;
        } else if (nLength == 9 && strncmp(pPlatformArgs, "nousbjtag", nLength) == 0) {
            s_nForceNativeUSB = 0;
        } else {
            printf("Invalid platform-specific arguments\n");
            return -1;
        }
        pPlatformArgs = pComma ? (pComma + 1) : (pPlatformArgs + nLength);
    }
    return 0;
}

static int Open(const char * pDeviceName, bool bAutoProgram, const char * pPlatformArgs) {
    int nRtn = ParsePlatformArgs(pPlatformArgs);
    if (nRtn < 0) return nRtn;

    nRtn = SerialOpen(pDeviceName, kBaudRateInitial, kDefaultSerialTimeoutMS);
    if (nRtn < 0) return nRtn;

    s_bNativeUSB = (s_nForceNativeUSB >= 0) ? (s_nForceNativeUSB != 0) : SerialIsNativeUSB();
    if (s_bNativeUSB) {
        printf("Using native USB (USB-Serial/JTAG) reset sequence, fixed serial speed\n");
    }
    return TargetInit(bAutoProgram);
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
    } else if (ImagePadToBoundary(&image, kFlashMaxWriteSize) < 0) {
        printf("Error padding image to boundary\n");
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
        nRtn = Esp32Program(pArea, &image);
        if (nRtn == 0) {
            if (bAutoReboot) Reboot();
        }
    }
    ImageFree(&image);
    return nRtn;
}

static int Read(u32 nAreaIdx, char * pFilename, bool bAutoReboot) {
    int nRtn = -1;
    Area * pArea = ImageGetArea(nAreaIdx);
    Image image;
    nRtn = ImageCreateBlank(&image, pArea->nMaxSize);
    if (nRtn == 0) {
        nRtn = Esp32Read(pArea, &image);
        if (nRtn == 0) {
            if (bAutoReboot) Reboot();
            // Write flash contents to file
            nRtn = ImageWriteToFile(&image, pFilename);
        }
        ImageFree(&image);
    }
    return nRtn;
}

static int Erase(u32 nAreaIdx) {
    Area * pArea = ImageGetArea(nAreaIdx);
    if (pArea) {
        printf("Erasing %s [%08X][%08X]\n", pArea->name, pArea->nOffset, pArea->nMaxSize);
        return Esp32Erase(pArea);
    }
    return -1;
}

static int Reboot(void) {
    printf("Rebooting\n");
    SerialSetDTR(false);
    SerialSetRTS(true);
    usleep(100000);
    SerialSetRTS(false);
    return 0;
}

FlashDeviceInterface FlashDeviceInterface_esp32 = {
    &Open,
    &Close,
    &Program,
    &Read,
    &Erase,
    &Reboot,
};

const FlashDeviceInterfaceMapEntry FlashDeviceMapEntry_esp32 = {
    .pFamily = "esp32",
    .pDevice = NULL,
    .pLegacyDevice = "ESP32",
    .pInterface = &FlashDeviceInterface_esp32
};

static void LT_USED_CONSTRUCTOR FlashDevice_RegisterSelf_esp32(void) {
    (void)FlashDevice_Register(&FlashDeviceMapEntry_esp32);
}
