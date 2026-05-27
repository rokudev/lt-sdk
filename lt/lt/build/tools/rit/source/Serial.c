/******************************************************************************
 * Serial.c                                                Serial Device Driver
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>

#include "lt/LTTypes.h"
#include "lt/core/LTCore.h"

#include "Image.h"
#include "Serial.h"

static bool s_bSerialInit = false;
static char s_deviceName[128] = "ttyUSB0";
static int  s_nSerialFD = -1;

static struct timeval s_timeout;

int SerialOpen(const char * pDeviceName, u32 nBaudRate, u32 nTimeoutMilliseconds) {
    if (!s_bSerialInit && pDeviceName) {
        errno = 0;
        s_nSerialFD = open(pDeviceName, O_RDWR);
        if (s_nSerialFD < 0) {
            int nRtn = -errno;
            perror("open");
            return nRtn;
        }
        lt_strncpyTerm(s_deviceName, pDeviceName, sizeof(s_deviceName));
        struct termios termOpts;
        int nRtn = tcgetattr(s_nSerialFD, &termOpts);
        if (nRtn < 0) {
            nRtn = -errno;
            perror("tcgetattr");
            return nRtn;
        }
        termOpts.c_cflag &= ~PARENB;
        termOpts.c_cflag &= ~CSTOPB;
        termOpts.c_cflag &= ~CSIZE;
        termOpts.c_cflag &= ~CRTSCTS;
        termOpts.c_cflag |= (CS8 | CLOCAL | CREAD);
        termOpts.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        termOpts.c_iflag &= ~(IXON | IXOFF | IXANY);
        termOpts.c_iflag &= ~(INPCK | IGNPAR | PARMRK);
        termOpts.c_iflag &= ~(ISTRIP | IGNBRK | BRKINT);

#ifndef __linux__
/* IUCLC is not a part of POSIX, so define it to 0 here. */
#define IUCLC 0
#endif

        termOpts.c_iflag &= ~(INLCR | IGNCR | ICRNL | IUCLC | IMAXBEL);
        termOpts.c_oflag &= ~OPOST;
        nRtn = tcsetattr(s_nSerialFD, TCSANOW, &termOpts);
        if (nRtn < 0) {
            nRtn = -errno;
            perror("tcsetattr");
            return nRtn;
        }
        nRtn = SerialSetSpeed(nBaudRate);
        if (nRtn < 0) return nRtn;

        SerialSetTimeout(nTimeoutMilliseconds);
        s_bSerialInit = true;
    }
    return 0;
}

void SerialClose() {
    if (s_bSerialInit) {
        close(s_nSerialFD);
        s_bSerialInit = false;
    }
}

int SerialSetTimeout(u32 nTimeoutMilliseconds) {
    s_timeout.tv_sec = nTimeoutMilliseconds / 1000;
    s_timeout.tv_usec = 1000 * (nTimeoutMilliseconds % 1000);
    return 0;
}

#ifdef __linux__

static speed_t GetTermiosSpeed(u32 nBaudRate) {
    switch (nBaudRate) {
    case 9600:    return B9600;
    case 19200:   return B19200;
    case 38400:   return B38400;
    case 57600:   return B57600;
    case 115200:  return B115200;
    case 230400:  return B230400;
    case 460800:  return B460800;
    case 500000:  return B500000;
    case 576000:  return B576000;
    case 921600:  return B921600;
    case 1000000: return B1000000;
    case 1152000: return B1152000;
    case 1500000: return B1500000;
    case 2000000: return B2000000;
    case 2500000: return B2500000;
    case 3000000: return B3000000;
    case 3500000: return B3500000;
    case 4000000: return B4000000;
    default:
        printf("I don't recognize that baud rate!\n");
        return B115200;
    }
}

int SerialSetSpeed(u32 nBaudRate) {
    struct termios termOpts;
    int nRtn = tcgetattr(s_nSerialFD, &termOpts);
    if (nRtn < 0) {
        nRtn = -errno;
        perror("tcgetattr");
        return nRtn;
    }
    cfsetospeed(&termOpts, GetTermiosSpeed(nBaudRate));
    cfsetispeed(&termOpts, GetTermiosSpeed(nBaudRate));
    nRtn = tcsetattr(s_nSerialFD, TCSANOW, &termOpts);
    if (nRtn < 0) {
        nRtn = -errno;
        perror("tcsetattr");
        return nRtn;
    }
    usleep(50000);
    tcflush(s_nSerialFD, TCIOFLUSH);
    return 0;
}

#elif __APPLE__

#ifndef IOSSIOSPEED
#define IOSSIOSPEED    _IOW('T', 2, speed_t)
#endif

int SerialSetSpeed(u32 nBaudRate) {
    unsigned int nSpeed = nBaudRate;
    errno = 0;
    if (ioctl(s_nSerialFD, IOSSIOSPEED, &nSpeed) < 0) {
        int nRtn = -errno;
        printf("ioctl(IOSSIOSPEED)");
        return nRtn;
    }
    usleep(50000);
    tcflush(s_nSerialFD, TCIOFLUSH);
    return 0;
}

#endif

void SerialFlush(void) {
    tcflush(s_nSerialFD, TCIOFLUSH);
}

int SerialSetDTR(bool bLevel) {
    int nArgp = TIOCM_DTR;
    int nRtn;
    if (bLevel) nRtn = ioctl(s_nSerialFD, TIOCMBIS, &nArgp);
    else nRtn = ioctl(s_nSerialFD, TIOCMBIC, &nArgp);
    if (nRtn < 0) {
        nRtn = -errno;
        perror("ioctl(TIOCMBIX)");
        return nRtn;
    }
    return 0;
}

int SerialSetRTS(bool bLevel) {
    int nArgp = TIOCM_RTS;
    int nRtn;
    if (bLevel) nRtn = ioctl(s_nSerialFD, TIOCMBIS, &nArgp);
    else nRtn = ioctl(s_nSerialFD, TIOCMBIC, &nArgp);
    if (nRtn < 0) {
        nRtn = -errno;
        perror("ioctl(TIOCMBIX)");
        return nRtn;
    }
    return 0;
}

int SerialRecv(u8 * pReadChars, int nNumCharsToRead) {
    int nRem = nNumCharsToRead;
    int nCnt = 0;
    while (nRem > 0) {
        static fd_set readFDs;
        FD_ZERO(&readFDs);
        FD_SET(s_nSerialFD, &readFDs);
        struct timeval timeout = s_timeout;
        errno = 0;
        int nRtn = select(s_nSerialFD + 1, &readFDs, NULL, NULL, &timeout);
        if (nRtn == 0) {
            errno = ETIMEDOUT;
            return -errno;
        } else if (nRtn == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                nRtn = -errno;
                perror("select");
                return nRtn;
            }
        }
        nRtn = read(s_nSerialFD, pReadChars + nCnt, nRem);
        if (nRtn > 0) {
#ifdef DEBUG_SERIAL_DATA
            for (int i = 0; i < nRtn; ++i) {
                if ((i % 16) == 0) {
                    printf("%s: ", (i) ? "\nR" : "R");
                }
                printf("%02x ", pReadChars[i]);
            }
            printf("\n");
#endif // DEBUG_SERIAL_DATA
            nCnt += nRtn;
            nRem -= nRtn;
        } else if (nRtn == 0) {
            errno = ETIMEDOUT;
            return -errno;
        } else {
            if (errno == EINTR) {
                continue;
            } else {
                nRtn = -errno;
                perror("read");
                return nRtn;
            }
        }
    }
    return 0;
}

int SerialExpect(u8 * pExpectedCharString) {
    for (u8 * pChar = pExpectedCharString; *pChar != '\0'; pChar++) {
        int nRtn = SerialExpectChar(*pChar);
        if (nRtn < 0) return nRtn;
    }
    return 0;
}

int SerialExpectChar(u8 nExpectedChar) {
    u8 nCh;
    int nRtn = SerialRecv(&nCh, 1);
    if (nRtn < 0) return nRtn;
    else if (nCh != nExpectedChar) return -EINVAL;
    else return 0;
}

int SerialDrainUntilMatch(u8 * pExpectedCharString, u32 nMaxCharsToDrain) {
    u8 * pChar = pExpectedCharString;
    if (*pChar == '\0') return 0;
    for (u32 nChars = 0; nChars < nMaxCharsToDrain; nChars++) {
        u8 nCh;
        int nRtn = SerialRecv(&nCh, 1);
        if (nRtn < 0) return nRtn;
        if (nCh == *pChar) {
            pChar++;
            if (*pChar == '\0') return 0;
        } else {
            pChar = pExpectedCharString;
        }
    }
    return -ETIMEDOUT;
}

int SerialSend(u8 * pCharsToSend, int nNumCharsToSend) {
    int nRem = nNumCharsToSend;
    int nCnt = 0;
    while (nRem > 0) {
        errno = 0;
        int nRtn = write(s_nSerialFD, pCharsToSend + nCnt, nRem);
        if (nRtn > 0) {
#ifdef DEBUG_SERIAL_DATA
            for (int i = 0; i < nRtn; ++i) {
                if ((i % 16) == 0) {
                    printf("%s: ", (i) ? "\nW" : "W");
                }
                printf("%02x ", pCharsToSend[i]);
            }
            printf("\n");
#endif // DEBUG_SERIAL_DATA
            nCnt += nRtn;
            nRem -= nRtn;
        } else if (nRtn == 0) {
            errno = ETIMEDOUT;
            return -errno;
        } else {
            if (errno == EINTR) {
                continue;
            } else {
                int nRtn = -errno;
                perror("write");
                return nRtn;
            }
        }
    }
    return 0;
}

int SerialSendChar(u8 nCharToSend) {
    return SerialSend(&nCharToSend, 1);
}
