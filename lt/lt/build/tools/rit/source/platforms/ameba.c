/******************************************************************************
 * ameba.c                                     Common methods for Ameba Devices
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

#include "lt/LTTypes.h"

#include "Image.h"
#include "FlashDevice.h"
#include "Serial.h"
#include "ameba.h"

/*

THIS IS THE BEST DOCUMENTATION WE HAVE FOUND SO FAR FOR PROTOCOL:
THIS IS THE WORST DOCUMENTATION WE HAVE FOUND SO FAR FOR PROTOCOL:

$8710c>?
DB
        DB <Address, Hex> <Len, Dec>:
        Dump memory byte or Read Hw byte register

DHW
        DHW <Address, Hex> <Len, Dec>:
        Dump memory helf-word or Read Hw helf-word register;

DW
        DW <Address, Hex> <Len, Dec>:
        Dump memory word or Read Hw word register;

EB
        EB <Address, Hex> <Value, Hex>:
        Write memory byte or Write Hw byte register
        Supports multiple byte writting by a single command
        Ex: EB Address Value0 Value1

EW
        EW <Address, Hex> <Value, Hex>:
        Write memory word or Write Hw word register
        Supports multiple word writting by a single command
        Ex: EW Address Value0 Value1

WDTRST
        WDTRST:
        To trigger a reset by WDT timeout

fwd
        fwd <tx_pin> <rx_pin> <baud_rate> <parity> <flow ctrl>
            <flash_pin> <flash_io> <flash_offset_4K_aligned>:
        To download Flash image over UART

ceras
        ceras <io_mode> <pin_sel>
        Flash chip erase

seras
        seras <offset> <len> <io_mode> <pin_sel>
        Flash sector erase

efotp
        Internal cmd.

*/

static u8 s_ModeOfMystery = CTHULHU_SLUMBERS;

// Xmodem Frame
typedef struct __attribute__((packed)) {
    u8 nHdr;
    u8 nPktNo;
    u8 nPktNoInv;
    u8 data[1024];
    u8 nChecksum;
} XmodemFrame;

void AmebaSetModeOfMystery(u8 nMode) {
    // NOTE: Mystery mode _might_ be "pin_sel"
    s_ModeOfMystery = nMode;
}

// Verify communications
int AmebaPing(void) {
    char buffer[] = "ping\n";
    if (SerialSend((u8 *)buffer, strlen(buffer)) < 0)
        return -1;
    char *exp = "ping";
    if (SerialRecv((u8 *)buffer, strlen(exp)) < 0)
        return -1;
    if (strncmp(buffer, exp, strlen(exp)) != 0)
        return -1;
    return 0;
}

// Send configuration and set Speed
int AmebaSendUCFGandSetSpeed(u32 nBaudRate) {
    char buffer[32];
    // ucfg [baud rate] [parity=0] [flow control=0]
    snprintf(buffer, sizeof(buffer), "ucfg %u 0 0\n", nBaudRate);
    if (SerialSend((u8 *)buffer, strlen(buffer)) < 0)
        return -1;
    // Allow ucfg message to transit before switching speed
    usleep(50000);
    if (SerialSetSpeed(nBaudRate) < 0)
        return -1;
    if (SerialExpect((u8 *)"OK") < 0)
        return -1;
    return 0;
}

// Erase sectors
int AmebaErase(u32 nOffset, u32 nNumSectors) {
    char buffer[32];
    // seras [offset] [num_sectors] [io_mode?=0] [pin_sel?]
    snprintf(buffer, sizeof(buffer), "seras %x %x 0 %u\n", nOffset, nNumSectors, s_ModeOfMystery);
    if (SerialSend((u8 *)buffer, strlen(buffer)) < 0)
        return -1;
    if (SerialExpect((u8 *)"OK") < 0)
        return -1;
    return 0;
}

// Firmware download (erases as it goes)
static int sendFWD(u32 nOffset) {
    char buffer[32];
    // fwd [io_mode?=0] [pin_sel?] [offset]
    snprintf(buffer, sizeof(buffer), "fwd 0 %d %x\n", s_ModeOfMystery, nOffset);
    if (SerialSend((u8 *)buffer, strlen(buffer)) < 0)
        return -1;
    if (SerialExpectChar(NAK) < 0) {
        return -1;
    }
    return 0;
}

// Send Xmodem frame
static int sendXmodemFrame(u8 nPktNo, u8 * pBuf) {
    XmodemFrame frame;
    frame.nHdr      = STX;
    frame.nPktNo    = nPktNo;
    frame.nPktNoInv = ~nPktNo;
    memcpy(frame.data, pBuf, 1024);

    frame.nChecksum = 0;
    for (u32 nIdx = 0; nIdx < sizeof(frame.data); nIdx++)
        frame.nChecksum += ((u8 *)&frame.data)[nIdx];

    // Retry if NAK'ed
    u8 nCh;
    do {
        if (SerialSend((u8 *)&frame, sizeof(frame)) < 0)
            return -1;
        if (SerialRecv(&nCh, 1) < 0) {
            return -1;
        }
    }
    while (nCh == NAK);
    return 0;
}

static int sendFile(Image * pImage) {
    const unsigned nBufSize = 1024;
    u8 buf[nBufSize];
    u8 * pData = pImage->pData;
    ssize_t nRem = pImage->nSize;

    u8 nPktNo = 1;
    int nMinPct = 10;

    while (nRem > 0)  {
        int nCopySize = nBufSize;
        // Pad frame to end of 1k buffer with 0xff
        if (nRem < nBufSize) {
            memset(buf, 0xff, nBufSize);
            nCopySize = nRem;
        }
        memcpy(buf, pData, nCopySize);
        if (sendXmodemFrame(nPktNo, buf) < 0)
            return -1;
        nPktNo++;
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
    return 0;
}

// Flash an image file
int AmebaProgram(u32 nOffset, Image * pImage) {
    if (sendFWD(nOffset) < 0)
        return -1;
    if (sendFile(pImage) < 0)
        return -1;
    if (SerialSendChar(EOT) < 0)
        return -1;
    // ACK-OK
    if (SerialExpect((u8 *)"\06OK") < 0)
        return -1;
    return 0;
}

// Obtain SHA2 256-bit checksum
static int getHash(u32 nSize, u8 * pDigest) {
    char buffer[32];
    // hashq [size] [?=0] [?=0]
    snprintf(buffer, sizeof(buffer), "hashq %u 0 0\n", nSize);
    if (SerialSend((u8 *)buffer, strlen(buffer)) < 0)
        return -1;
    if (SerialExpect((u8 *)"hashs ") < 0)
        return -1;
    if (SerialRecv(pDigest, kCryptoSHA2DigestLength) < 0)
        return -1;
    return 0;
}

// Verify checksum
int AmebaVerifyChecksum(Image * pImage) {
    u8 digest[kCryptoSHA2DigestLength];
    if (getHash(pImage->nSize, digest) < 0)
        return -1;
    bool bPass = true;
    printf("sha256: ");
    for (s32 nIdx = 0; nIdx < sizeof(digest); nIdx++) {
        printf("%02X", digest[nIdx]);
        if (digest[nIdx] != pImage->digest[nIdx]) bPass = false;
    }
    printf("\n");
    if (bPass == false) {
        printf("Integrity check FAIL\n");
        return -1;
    }
    printf("Integrity check PASS\n");
    return 0;
}

// Reboot the target
int AmebaRebootTarget(void) {
    // Send disconnect, which reboots
    char buffer[] = "disc\n";
    printf("Rebooting\n");
    if (SerialSend((u8 *)buffer, strlen(buffer)) < 0)
        return -1;
    return 0;
}
