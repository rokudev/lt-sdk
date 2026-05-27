/******************************************************************************
 * Esp32_I2C.h                                                        ESP32 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#ifndef PLATFORMS_ESP32_INCLUDE_ESP32_I2C_H
#define PLATFORMS_ESP32_INCLUDE_ESP32_I2C_H

/*
 *  I2C Addresses and Hosts
 */
typedef u8 Esp32_I2CAddress;
enum Esp32_I2CAddress {
    kEsp32_I2CAddress_BBPLL       = 0x66,
};

typedef u8 Esp32_I2CHost;
enum Esp32_I2CHost {
    kEsp32_I2CHost_BBPLL          = 4,
};

/*
 * I2C Device Registers
 */

/* BBPLL I2C Device Registers */
typedef u16 Esp32_I2CRegisterBBPLL;
enum Esp32_I2CRegisterBBPLL {
    kEsp32_I2CRegisterBBPLL_IR_CAL_DELAY           = 0,
    kEsp32_I2CRegisterBBPLL_IR_CAL_DELAY_V         = 0x18,

    kEsp32_I2CRegisterBBPLL_IR_CAL_EXT_CAP         = 1,
    kEsp32_I2CRegisterBBPLL_IR_CAL_EXT_CAP_V       = 0x20,

    kEsp32_I2CRegisterBBPLL_OC_LREF                = 2,
    kEsp32_I2CRegisterBBPLL_OC_LREF_40M_480M_V     = 0,

    kEsp32_I2CRegisterBBPLL_OC_DIV_7_0             = 3,
    kEsp32_I2CRegisterBBPLL_OC_DIV_7_0_40M_480M_V  = 28,

    kEsp32_I2CRegisterBBPLL_OC_ENB_FCAL            = 4,
    kEsp32_I2CRegisterBBPLL_OC_ENB_FCAL_V          = 0x9a,

    kEsp32_I2CRegisterBBPLL_OC_DCUR                = 5,
    kEsp32_I2CRegisterBBPLL_OC_DCUR_40M_480M_V     = (3 << 6) | 6,

    kEsp32_I2CRegisterBBPLL_BBADC_DSMP             = 9,
    kEsp32_I2CRegisterBBPLL_BBADC_DSMP_320M_V      = 0x84,
    kEsp32_I2CRegisterBBPLL_BBADC_DSMP_480M_V      = 0x74,

    kEsp32_I2CRegisterBBPLL_OC_ENB_VCON            = 10,
    kEsp32_I2CRegisterBBPLL_OC_ENB_VCON_V          = 0x00,

    kEsp32_I2CRegisterBBPLL_ENDIV5                 = 11,
    kEsp32_I2CRegisterBBPLL_ENDIV5_320M_V          = 0x43,
    kEsp32_I2CRegisterBBPLL_ENDIV5_480M_V          = 0xc3,

    kEsp32_I2CRegisterBBPLL_BBADC_CAL_7_0          = 12,
    kEsp32_I2CRegisterBBPLL_BBADC_CAL_7_0_V        = 0x00,
};

/*
 * I2C Access
 */
#define ESP32_I2C_ADDR(d)            (kEsp32_I2CAddress_ ## d)
#define ESP32_I2C_HOST(d)            (kEsp32_I2CHost_ ## d)
#define ESP32_I2C_REG(d, r)          (kEsp32_I2CRegister ## d ## _ ## r)
#define ESP32_I2C_VAL(d, v)          (kEsp32_I2CRegister ## d ## _ ## v ## _V)

#define ESP32_I2C_WRITE(d, r, v)     rom_i2c_writeReg(ESP32_I2C_ADDR(d), ESP32_I2C_HOST(d), r, v)
#define ESP32_I2C_READ(d, r)         rom_i2c_readReg(ESP32_I2C_ADDR(d), ESP32_I2C_HOST(d), r)

int rom_i2c_writeReg(int, int, int, int);
int rom_i2c_readReg(int, int, int);

#endif // #ifndef PLATFORMS_ESP32_INCLUDE_ESP32_I2C_H

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  02-Jun-22   tiberius    created
 */
