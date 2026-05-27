/******************************************************************************
 * amebaD.c                                                AmebaD Flash Support
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "lt/LTTypes.h"

#include "Image.h"
#include "FlashDevice.h"
#include "Serial.h"

//
// NOTE: AmebaD uses a Legacy Realtek Xmodem-1k serial protocol
//

enum {
    kInitialBaudRate       = 115200,
    kFastBaudRate          = 1500000,

    kDefaultTimeoutMS      = 2500,
    kEraseTimePerBlockMS   = 100,
    kVerifyTimePerBlockMS  = 2,

    kFlashOffset           = 0x08000000    // AmebaD flash address offset
};

enum {
    // Legacy Realtek Xmodem (non-standard values)
    BAUDSET = 0x05,
    BAUDCHK = 0x07,
    ERASE   = 0x17,
    FSTATUS = 0x26,
    CHKSUM  = 0x27,
    TXREG   = 0x29,
    RXREG   = 0x31
};

// Multi-byte fields are little endian
//   Some messages use annoying 3 byte fields
typedef struct __attribute__((packed)) {
    u8 hdr;
    u8 rate[3];
} BaudsetReq;

typedef struct __attribute__((packed)) {
    u8 hdr;
    u8 addr[3];
    u16 blk_cnt;
} EraseReq;

// This is supposed to turn on write enable for some flash chips
//   or clear status register
typedef struct __attribute__((packed)) {
    u8 hdr;
    u8 data[3];
} WriteFlashStatusReq;

typedef struct __attribute__((packed)) {
    u8 hdr;
    u8 addr[3];
    u8 len[3];
} ChecksumReq;

typedef struct __attribute__((packed)) {
    u8 hdr;
    u32 nChecksum;
} ChecksumResp;

typedef struct __attribute__((packed)) {
    u8 nHdr;
    u8 nPktNo;
    u8 nPktNoInv;
    u32 nAddr;
    u8 data[1024];
    u8 nChecksum;
} XmodemFrame;

typedef struct __attribute__((packed)) {
    u8 hdr;
    u32 addr;
    u32 data;
} Write32;

typedef struct __attribute__((packed)) {
    u8 hdr;
    u32 addr;
} Read32;

typedef struct __attribute__((packed)) {
    u8 hdr;
    u32 data;
} Read32Resp;

static int Reboot(void);

static int SyncDevice(void) {
    SerialFlush();
    if (SerialExpectChar(NAK) < 0) {
        printf("Is device connected?\n");
        return -1;
    }
    return 0;
}

static int sendBaudSet(u32 nBaudRate) {
    BaudsetReq baudset;
    baudset.hdr = BAUDSET;
    baudset.rate[0] = (nBaudRate >> 16) & 0xff;
    baudset.rate[1] = (nBaudRate >> 8)  & 0xff;
    baudset.rate[2] = nBaudRate & 0xff;
    if (SyncDevice() < 0) {
        return -1;
    }
    if (SerialSend((u8 *) &baudset, sizeof(baudset)) < 0) {
        return -1;
    }
    if (SerialExpectChar(ACK) < 0) {
        return -1;
    }
    return 0;
}

static int flashErase(u32 addr, u16 blk_cnt) {
    EraseReq erase;
    erase.hdr = ERASE;
    erase.addr[0] = addr & 0xff;
    erase.addr[1] = (addr >> 8)  & 0xff;
    erase.addr[2] = (addr >> 16) & 0xff;
    erase.blk_cnt = blk_cnt;

    if (SyncDevice() < 0) {
        return -1;
    }
    if (SerialSend((u8 *)&erase, sizeof(erase)) < 0) {
        return -1;
    }
    s64 timeout_ns = (s64)blk_cnt * kEraseTimePerBlockMS * 1000000;
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        u8 ch;
        if (SerialRecv(&ch, 1) < 0) {
            if (errno == ETIMEDOUT) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);
                s64 elapsed_ns = (now.tv_sec - start.tv_sec) * 1000000000;
                elapsed_ns += (now.tv_nsec - start.tv_nsec);
                if (elapsed_ns > timeout_ns) {
                    return -1;
                }
            }
        } else break;
    }
    while (1);
    return 0;
}

// Send Xmodem frame
static int sendXmodemFrame(u8 nPktNo, u32 nAddr, u8 * pBuf) {
    XmodemFrame frame;
    frame.nHdr      = STX;
    frame.nPktNo    = nPktNo;
    frame.nPktNoInv = ~nPktNo;
    frame.nAddr     = nAddr;
    memcpy(frame.data, pBuf, 1024);

    frame.nChecksum = 0;
    for (u32 nIdx = 0; nIdx < (sizeof(frame.nAddr) + sizeof(frame.data)); nIdx++)
        frame.nChecksum += ((u8 *) &frame.nAddr)[nIdx];

    // Retry if NAK'ed
    u8 nCh;
    do {
        if (SerialSend((u8 *)&frame, sizeof(frame)) < 0)
            return -1;
        if (SerialRecv(&nCh, 1) < 0)
            return -1;
    }
    while (nCh == NAK);
    return 0;
}

static int sendFile(u32 nStartAddr, Image * pImage) {
    const unsigned nBufSize = 1024;
    u8 buf[nBufSize];
    u8 * pData = pImage->pData;
    ssize_t nRem = pImage->nSize;

    if (SerialSendChar(BAUDCHK) < 0) {
        return -1;
    }
    if (SerialExpectChar(ACK) < 0) {
        return -1;
    }

    u8 nPktNo = 1;
    u32 nAddr = nStartAddr;
    int nMinPct = 10;

    while (nRem > 0)  {
        int nCopySize = nBufSize;
        // Pad frame to end of 1k buffer with 0xff
        if (nRem < nBufSize) {
            memset(buf, 0xff, nBufSize);
            nCopySize = nRem;
        }
        memcpy(buf, pData, nCopySize);
        if (sendXmodemFrame(nPktNo, nAddr, buf) < 0)
            return -1;
        nPktNo++;
        nAddr += nBufSize;
        pData += nBufSize;
        nRem -= nCopySize;
        int nPct = 100 * (pImage->nSize - nRem) / pImage->nSize;
        if (nPct >= nMinPct) {
            printf("%d%% ", nPct);
            fflush(stdout);
            nMinPct = nPct + 10;
        }
    }
    printf("\n");
    if (SerialSendChar(EOT) < 0)
        return -1;
    if (SerialExpectChar(ACK) < 0)
        return -1;
    return 0;
}

static int doChecksum(u32 addr, u32 len, u32 * pChecksum) {
    ChecksumReq checksum;
    checksum.hdr = CHKSUM;
    checksum.addr[0] = addr & 0xff;
    checksum.addr[1] = (addr >> 8)  & 0xff;
    checksum.addr[2] = (addr >> 16) & 0xff;
    checksum.len[0] = len & 0xff;
    checksum.len[1] = (len >> 8)  & 0xff;
    checksum.len[2] = (len >> 16) & 0xff;

    if (SyncDevice() < 0) {
        return -1;
    }
    if (SerialSend((u8 *)&checksum, sizeof(checksum)) < 0) {
        return -1;
    }
    usleep(len / ImageGetBlockSize() * kVerifyTimePerBlockMS * 1000);
    ChecksumResp resp;
    if (SerialRecv((u8 *)&resp, sizeof(resp)) < 0) {
        return -1;
    }
    *pChecksum = resp.nChecksum;
    return 0;
}

static int doLoadFile(u32 nAddr, Image * pImage) {
    if (SyncDevice() < 0)
        return -1;
    if (sendBaudSet(kFastBaudRate) < 0)
        return -1;
    if (SerialSetSpeed(kFastBaudRate) < 0)
        return -1;
    if (sendFile(nAddr | kFlashOffset, pImage) < 0)
        return -1;
    if (SerialSetSpeed(kInitialBaudRate) < 0)
        return -1;
    return 0;
}

static int writeFlashStatus() {
    WriteFlashStatusReq fstatus;
    fstatus.hdr = FSTATUS;
    // This will work for Winbond and MXIC chips, but other chips
    // may require other settings.
    fstatus.data[0] = 0x1;
    fstatus.data[1] = 0x1;
    fstatus.data[2] = 0x0;

    if (SyncDevice() < 0) {
        return -1;
    }
    if (SerialSend((u8 *)&fstatus, sizeof(fstatus)) < 0) {
        return -1;
    }
    if (SerialExpectChar(ACK) < 0) {
        return -1;
    }
    return 0;
}

static int TargetInit(bool bAutoProgram) {
    if (bAutoProgram) {
        // Place in programming mode
        SerialSetRTS(false);
        usleep(80000);
        SerialSetRTS(true);
        usleep(200000);
    }

    printf("Injecting flashloader image into RAM\n");
    Image flasherImage;
    if (ImageGetFlasherImage(&flasherImage, "AmebaD") < 0) {
        return -1;
    }
    if (SyncDevice() < 0)
        return -1;
    const u32 nRamStart = 0x082000;
    if (sendFile(nRamStart, &flasherImage) < 0)
        return -1;
    ImageFree(&flasherImage);
    const unsigned max_buf_size = 100;
    u8 buf[max_buf_size];
    for (unsigned cnt = 0; cnt < (max_buf_size - 1); cnt++) {
        if (SerialRecv(&buf[cnt], 1) < 0) {
            return -1;
        }
        if (cnt == 0 && buf[cnt] == NAK) {
            printf("Flashloader already running?\n");
            break;
        } else if (buf[cnt] == NAK) {
            const char * chk_str = "UARTIMG_Download 2\n\r";
            buf[cnt + 1] = '\0';
            if (strstr((char *)buf, (char *)chk_str) != NULL) {
                printf("Flashloader started\n");
                break;
            } else {
                printf("Flashloader not started properly?\n");
                return -1;
            }
        }
    }
    if (writeFlashStatus() < 0) {
        return -1;
    }
    return 0;
}

static int doChecksumVerify(Area * pArea, Image * pImage) {
    u32 nChecksum;
    int nRtn = -1;
    if (doChecksum(pArea->nOffset, pImage->nSize, &nChecksum) == 0) {
        if (nChecksum == pImage->nChecksum) {
            nRtn = 0;
            printf("Checksum OK [%08X][%08X] = %08X\n", pArea->nOffset,
                       pImage->nSize, nChecksum);
        } else {
            printf("Checksum BAD [%08X][%08X] = %08X, expected %08X\n",
                       pArea->nOffset, pImage->nSize, nChecksum, pImage->nChecksum);
        }
    }
    return nRtn;
}

static int Open(const char * pDeviceName, bool bAutoProgram, const char * pPlatformArgs) {
    if (pPlatformArgs && pPlatformArgs[0] != '\0') {
        printf("Platform-specific flags do not exist for this device\n");
        return -1;
    }
    int nRtn = SerialOpen(pDeviceName, kInitialBaudRate, kDefaultTimeoutMS);
    if (nRtn < 0) return nRtn;
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
    }
    if (pArea) {
        if (pArea->bMustFlashAll && image.nSize != pArea->nMaxSize) {
            printf("Error, image must be exact size of area\n");
            pArea = NULL;
        } else if (image.nSize > pArea->nMaxSize) {
            printf("Error, image will overflow area\n");
            pArea = NULL;
        } else if (image.nSize & 0x3) {
            printf("Error, image must be a multiple of 4 bytes\n");
            pArea = NULL;
        }
    }
    if (pArea) {
        u32 blk_sz = ImageGetBlockSize();
        u32 num_blks = (image.nSize + blk_sz - 1) / ImageGetBlockSize();
        printf("Erasing [%08X][%08X]\n", pArea->nOffset, num_blks * blk_sz);
        nRtn = flashErase(pArea->nOffset, num_blks);
        if (nRtn == 0) {
            printf("Programming %s [%08X][%08X]\n", pFilename, pArea->nOffset, image.nSize);
            nRtn = doLoadFile(pArea->nOffset, &image);
            if (nRtn == 0) {
                nRtn = doChecksumVerify(pArea, &image);
                if (bAutoReboot) Reboot();
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
        return flashErase(pArea->nOffset, pArea->nMaxSize / ImageGetBlockSize());
    }
    return -1;
}

static int Reboot(void) {
    printf("Rebooting\n");
    SerialSetDTR(false);
    usleep(80000);
    SerialSetDTR(true);
    return 0;
}

FlashDeviceInterface FlashDeviceInterface_amebaD = {
    &Open,
    &Close,
    &Program,
    &Read,
    &Erase,
    &Reboot,
};

const FlashDeviceInterfaceMapEntry FlashDeviceMapEntry_amebaD = {
    .pFamily = "ameba",
    .pDevice = "amebaD",
    .pLegacyDevice = "AmebaD",
    .pInterface = &FlashDeviceInterface_amebaD
};

static void LT_USED_CONSTRUCTOR FlashDevice_RegisterSelf_amebaD(void) {
    (void)FlashDevice_Register(&FlashDeviceMapEntry_amebaD);
}
