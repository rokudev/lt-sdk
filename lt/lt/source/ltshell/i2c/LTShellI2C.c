/*******************************************************************************
 *
 * LTShellI2C: I2C Shell
 * -----------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/device/i2c/LTDeviceI2C.h>

// DEFINE_LTLOG_SECTION("ltshell.i2c");
LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(LTShellI2C, (LTSystemShell) (LTDeviceI2C));

/** Standard LT Interfaces ****************************************************/
static ILTShell         *SHL_iShell;
static struct Statics {
    LTDeviceUnit             i2cDeviceUnit;
} S;

/** Forward references ********************************************************/
static int  SHL_Help(LTShell hShell, int argc, const char *argv[]);

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/
static bool 
ReadRegister8(u8 deviceAddr, u8 registerAddress, void *recvBuf, u32 recvBufSize, bool repeatStart) {
    if (repeatStart) {
        return LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, deviceAddr,
            recvBuf, recvBufSize,
            &registerAddress, 1,
            true, true,
            NULL, NULL);
    } else {
        // Read in two transfers
        // First transfer: send register address
        // Second transfer: read data
        if (LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, deviceAddr,
            NULL, 0,
            &registerAddress, 1,
            true, true,
            NULL, NULL)) {
            // Second transfer: read data
            if (LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, deviceAddr,
                recvBuf, recvBufSize,
                NULL, 0,
                true, true,
                NULL, NULL)) {
                return true;
            }
        }
        return false;
    }
}

static bool WriteRegister8(u8 deviceAddr, u8 registerAddress, u8 *value, u32 valueSize) {
    bool ret;
    u8 *cmd;
    u32 txLen = valueSize + 1;

    cmd = lt_malloc(txLen);
    if (!cmd) {
        return false;
    }

    cmd[0] = registerAddress;
    lt_memcpy(&cmd[1], value, valueSize);

    ret = LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, deviceAddr,
        NULL, 0,
        cmd, txLen,
        true, true,
        NULL, NULL);

    lt_free(cmd);
    return ret;
}

static bool
ReadRegister16(u8 deviceAddr, u16 registerAddress, void *recvBuf, u32 recvBufSize, bool repeatStart) {
    u8 cmd[2] = {
        (registerAddress >> 8) & 0xFF,
        (registerAddress >> 0) & 0xFF,
    };

    if (repeatStart) {
        return LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, deviceAddr,
            recvBuf, recvBufSize,
            cmd, sizeof(cmd),
            true, true,
            NULL, NULL);
    } else {
        // Read in two transfers
        // First transfer: send register address
        // Second transfer: read data
        if (LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, deviceAddr,
            NULL, 0,
            cmd, sizeof(cmd),
            true, true,
            NULL, NULL)) {
            // Second transfer: read data
            if (LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, deviceAddr,
                recvBuf, recvBufSize,
                NULL, 0,
                true, true,
                NULL, NULL)) {
                return true;
            }
        }
        return false;
    }
}

static bool
WriteRegister16(u8 deviceAddr, u16 registerAddress, u8 *value, u32 valueSize) {
    bool ret;
    u8 *cmd;
    u32 txLen = valueSize + 2;

    cmd = lt_malloc(txLen);
    if (!cmd) {
        return false;
    }

    cmd[0] = (registerAddress >> 8) & 0xFF;
    cmd[1] = (registerAddress >> 0) & 0xFF;
    lt_memcpy(&cmd[2], value, valueSize);

    ret = LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, deviceAddr,
        NULL, 0,
        cmd, txLen,
        true, true,
        NULL, NULL);

    lt_free(cmd);
    return ret;
}

/*******************************************************************************
 * Shell Commands
 ******************************************************************************/
static int SHL_I2COpenBus(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        SHL_iShell->Print(hShell, "i2c open <bus name>\n");
        return -1;
    }

    u32 busId = LT_GetLTDeviceI2C()->GetBusIndexFromName(argv[1]);
    if (busId == LT_U32_MAX) {
        SHL_iShell->Print(hShell, "Invalid bus name\n");
        return -1;
    }

    if (S.i2cDeviceUnit != LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "A bus already opened, closing...\n");
        lt_destroyhandle(S.i2cDeviceUnit);
    }

    S.i2cDeviceUnit = LT_GetLTDeviceI2C()->CreateDeviceUnitHandle(busId);
    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "Failed to create device unit handle for %s\n", argv[1]);
        return -1;
    }

    SHL_iShell->Print(hShell, "Opened I2C Bus %s\n", argv[1]);
    return 0;
}

static int SHL_I2CCloseBus(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);

    if (S.i2cDeviceUnit != LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "Closing I2C Bus\n");
        lt_destroyhandle(S.i2cDeviceUnit);
        S.i2cDeviceUnit = LTHANDLE_INVALID;
    } else {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
    }

    return 0;
}

static int SHL_I2CRead(LTShell hShell, int argc, const char *argv[]) {
    u32 regLen;
    u32 regAddr;
    u32 bytesToRead;
    u32 ret = -1;
    u8 devAddr;
    u8 *data;
    bool repeatStart = true;

    if (argc < 4) {
        SHL_iShell->Print(hShell, "i2cread <devAddr> <register> <bytes> [repeated start = true(1)]\n");
        return -1;
    }

    if (argc >= 5) {
        repeatStart = lt_strcmp(argv[4], "0") != 0;
    }

    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
        return -1;
    }

    devAddr = (u8)(lt_strtou32(argv[1], NULL, 0) & 0xFF);
    regAddr = lt_strtou32(argv[2], NULL, 0);
    if (lt_strlen(argv[2]) > 4 || regAddr > 0xFF) {
        regLen = 2;
    } else {
        regLen = 1;
    }
    bytesToRead = lt_strtou32(argv[3], NULL, 0);
    if (bytesToRead == 0 || bytesToRead > 256) {
        SHL_iShell->Print(hShell, "Invalid number of bytes to read\n");
        return -1;
    }
    data = lt_malloc(bytesToRead);

    if (regLen == 1) {
        if (ReadRegister8(devAddr, regAddr, data, bytesToRead, repeatStart)) {
            SHL_iShell->Print(hShell, "Read ");
            for (u32 i = 0; i < bytesToRead; ++i) {
                SHL_iShell->Print(hShell, "%02lx ", LT_Pu32(data[i]));
            }
            SHL_iShell->Print(hShell, "from Device 0x%02lx register 0x%02lx\n", LT_Pu32(devAddr), LT_Pu32(regAddr));

            ret = 0;
        } else {
            SHL_iShell->Print(hShell, "Failed to read8 from device 0x%02lx register 0x%02lx\n", LT_Pu32(devAddr), LT_Pu32(regAddr));
        }

    } else {
        if (ReadRegister16(devAddr, regAddr, data, bytesToRead, repeatStart)) {
            SHL_iShell->Print(hShell, "Read ");
            for (u32 i = 0; i < bytesToRead; ++i) {
                SHL_iShell->Print(hShell, "%02lx ", LT_Pu32(data[i]));
            }
            SHL_iShell->Print(hShell, "from Device 0x%02lx register 0x%04lx\n", LT_Pu32(devAddr), LT_Pu32(regAddr));
            ret = 0;
        } else {
            SHL_iShell->Print(hShell, "Failed to read16 from device 0x%02lx register 0x%04lx\n", LT_Pu32(devAddr), LT_Pu32(regAddr));
        }
    }
    lt_free(data);
    return ret;
}

static int SHL_I2CWrite(LTShell hShell, int argc, const char *argv[]) {
    u32 regLen;
    u32 regAddr;
    u32 bytesToWrite;
    u32 ret = -1;
    u8 devAddr;
    u8 *values;

    if (argc < 4) {
        SHL_iShell->Print(hShell, "i2cwrite <devAddr> <register> <value ...>\n");
        return -1;
    }

    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
        return -1;
    }

    devAddr = (u8)lt_strtou32(argv[1], NULL, 0);
    regAddr = lt_strtou32(argv[2], NULL, 0);
    if (lt_strlen(argv[2]) > 4 || regAddr > 0xFF) {
        regLen = 2;
    } else {
        regLen = 1;
    }

    bytesToWrite = argc - 3;

    if (bytesToWrite == 0 || bytesToWrite > 256) {
        SHL_iShell->Print(hShell, "Invalid number of bytes to write\n");
        return -1;
    }

    values = lt_malloc(bytesToWrite);

    for (u32 i = 0; i < bytesToWrite; ++i) {
        values[i] = (u8)lt_strtou32(argv[3 + i], NULL, 0);
    }

    if (regLen == 1) {
        if (WriteRegister8(devAddr, regAddr, values, bytesToWrite)) {
            SHL_iShell->Print(hShell, "Wrote");
            for (u32 i = 0; i < bytesToWrite; ++i) {
                SHL_iShell->Print(hShell, " 0x%02lx", LT_Pu32(values[i]));
            }
            SHL_iShell->Print(hShell, " to Device 0x%02lx register 0x%02lx\n", LT_Pu32(devAddr), LT_Pu32(regAddr));
            ret = 0;
        } else {
            SHL_iShell->Print(hShell, "Failed to write8 to device 0x%02lx register 0x%02lx\n", LT_Pu32(devAddr), LT_Pu32(regAddr));
        }
    } else {
        if (WriteRegister16(devAddr, regAddr, values, bytesToWrite)) {
            SHL_iShell->Print(hShell, "Wrote");
            for(u32 i = 0; i < bytesToWrite; ++i) {
                SHL_iShell->Print(hShell, " 0x%02lx", LT_Pu32(values[i]));
            }
            SHL_iShell->Print(hShell, " to Device 0x%02lx register 0x%0lx\n", LT_Pu32(devAddr), LT_Pu32(regAddr));
            ret = 0;
        } else {
            SHL_iShell->Print(hShell, "Failed to write16 to device 0x%02lx register 0x%04lx\n", LT_Pu32(devAddr), LT_Pu32(regAddr));
        }
    }
    lt_free(values);
    return ret;
}

static int SHL_I2CProbe(LTShell hShell, int argc, const char *argv[]) {
    u8 devAddr;

    if (argc != 2) {
        SHL_iShell->Print(hShell, "i2cprobe <devAddr>\n");
        return -1;
    }

    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
        return -1;
    }

    devAddr = (u8)lt_strtou32(argv[1], NULL, 0);
    if (LT_GetLTDeviceI2C()->ProbeAddress(S.i2cDeviceUnit, devAddr)) {
        SHL_iShell->Print(hShell, "Device 0x%02lx found\n", LT_Pu32(devAddr));
    } else {
        SHL_iShell->Print(hShell, "Device 0x%02lx not found\n", LT_Pu32(devAddr));
    }
    return 0;
}

static int SHL_I2CScan(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    u8 devAddr;

    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
        return -1;
    }

    SHL_iShell->Print(hShell, "Scanning I2C Bus...\n");
    for (devAddr = 0; devAddr < 0x80; ++devAddr) {
        if (LT_GetLTDeviceI2C()->ProbeAddress(S.i2cDeviceUnit, devAddr)) {
            SHL_iShell->Print(hShell, "Device 0x%02lx found\n", LT_Pu32(devAddr));
        }
    }
    SHL_iShell->Print(hShell, "I2C Bus Scan Complete\n");
    return 0;
}

//Raw I2C operations
static int SHL_I2CStart(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
        return -1;
    }

    if (LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, 0, NULL, 0, NULL, 0, true, false, NULL, NULL)) {
        SHL_iShell->Print(hShell, "Sent I2C Start\n");
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to send I2C Start\n");
        return -1;
    }
}

static int SHL_I2CStop(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
        return -1;
    }

    if (LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, 0, NULL, 0, NULL, 0, false, true, NULL, NULL)) {
        SHL_iShell->Print(hShell, "Sent I2C Stop\n");
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to send I2C Stop\n");
        return -1;
    }
}

static int SHL_I2CRawRead(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc); LT_UNUSED(argv);
    u8 buf;

    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
        return -1;
    }

    if (LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, 0, &buf, 1, NULL, 0, false, false, NULL, NULL)) {
        SHL_iShell->Print(hShell, "Read 0x%02lx\n",LT_Pu32(buf));
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to raw read\n");
        return -1;
    }
}

static int SHL_I2CRawWrite(LTShell hShell, int argc, const char *argv[]) {
    u8 buf;

    if (argc != 2) {
        SHL_iShell->Print(hShell, "i2crawwrite <value>\n");
        return -1;
    }

    if (S.i2cDeviceUnit == LTHANDLE_INVALID) {
        SHL_iShell->Print(hShell, "No I2C Bus opened\n");
        return -1;
    }

    buf = (u8)lt_strtou32(argv[1], NULL, 0);
    if (LT_GetLTDeviceI2C()->I2CMasterTransfer(S.i2cDeviceUnit, 0, NULL, 0, &buf, 1, false, false, NULL, NULL)) {
        SHL_iShell->Print(hShell, "Wrote 0x%02lx\n", LT_Pu32(buf));
        return 0;
    } else {
        SHL_iShell->Print(hShell, "Failed to raw write\n");
        return -1;
    }
}

static const LTSystemShell_CommandDesc I2C_Commands[] = {
    { "help",        SHL_Help,        "list of I2C commands",       NULL},
    { "open",        SHL_I2COpenBus,  "I2C Open Bus",               NULL},
    { "close",       SHL_I2CCloseBus, "I2C Close Bus",              NULL},
    { "read",        SHL_I2CRead,     "I2C Read",                   NULL},
    { "write",       SHL_I2CWrite,    "I2C Write",                  NULL},
    { "probe",       SHL_I2CProbe,    "Probe a device address",     NULL},
    { "scan",        SHL_I2CScan,     "Scan for i2c devices",       NULL},
    //Raw I2C operations, driver might not support these
    { "start",       SHL_I2CStart,    "I2C Raw Start (driver may not support)",              NULL},
    { "stop",        SHL_I2CStop,     "I2C Raw Stop (driver may not support)",               NULL},
    { "rawread",     SHL_I2CRawRead,  "I2C Raw Read (driver may not support)",               NULL},
    { "rawwrite",    SHL_I2CRawWrite, "I2C Raw Write (driver may not support)",              NULL},
    { NULL,          NULL,            NULL,                         NULL}
};

static int SHL_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    SHL_iShell->Print(hShell, "usage: i2c <command> [args]\nCommands:\n");
    for (int n = 0; I2C_Commands[n].pCommand; n++) {
        SHL_iShell->Print(hShell, "  %-10s - %s\n", I2C_Commands[n].pCommand,
            I2C_Commands[n].pDescription);
    }
    SHL_iShell->Print(hShell,
        "Examples:\n"
        "  i2c open TWI0\n"
        "  i2c close\n"
        "  i2c probe 0x30\n"
        "  i2c scan\n"
        "  i2c read 0x30 0x3e09\n"
        "  i2c write 0x30 0x3e09 1\n"
    );
    return 0;
}

static int SHL_I2C(LTShell hShell, int argc, const char *argv[]) {
    int cmd = 0;
    if (argc > 1) {
        for (int n = 0; I2C_Commands[n].pCommand; n++) {
            if (lt_strcmp(I2C_Commands[n].pCommand, argv[1]) == 0) {
                cmd = n;
                break;
            }
        }
    }
    return I2C_Commands[cmd].pCommandProc(hShell, argc-1, argv+1);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    (void)SHL_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc SHL_Commands[] = {
    { "i2c",        SHL_I2C,  "I2C Command", LTShell_Help}
};

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellI2CImpl_LibFini(void) {
    LT_GetLTSystemShell()->UnregisterCommands(SHL_Commands);
}

static bool LTShellI2CImpl_LibInit(void) {
    SHL_iShell = lt_getlibraryinterface(ILTShell, LT_GetLTSystemShell());
    LT_GetLTSystemShell()->RegisterCommands(SHL_Commands, sizeof(SHL_Commands) / sizeof(SHL_Commands[0]));
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellI2C, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellI2C) LTLIBRARY_DEFINITION;
