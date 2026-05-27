/******************************************************************************
 * Serial.h                                           FTDI Serial Device Driver
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef _SERIAL_H
#define _SERIAL_H

// Enable this define to log out (to the console) the hex values of all data
// written to and read from the serial port.
//#define DEBUG_SERIAL_DATA

enum {
    // Standard ASCII
    STX = 0x02,
    EOT = 0x04,
    ACK = 0x06,
    NAK = 0x15,
};

int  SerialOpen(const char * pDeviceName, u32 nBaudRate, u32 nTimeoutMilliseconds);
void SerialClose(void);
int  SerialSetSpeed(u32 nBaudRate);
int  SerialSetTimeout(u32 nTimeoutMilliseconds);
void SerialFlush(void);

int  SerialSetDTR(bool bLevel);
int  SerialSetRTS(bool bLevel);

int  SerialRecv(u8 * pReadChars, int nNumCharsToRead);
int  SerialExpect(u8 * pExpectedCharString);
int  SerialExpectChar(u8 nExpectedChar);
int  SerialDrainUntilMatch(u8 * pExpectedCharString, u32 nMaxCharsToDrain);

int  SerialSend(u8 * pCharsToSend, int nNumCharsToSend);
int  SerialSendChar(u8 nCharToSend);

#endif

