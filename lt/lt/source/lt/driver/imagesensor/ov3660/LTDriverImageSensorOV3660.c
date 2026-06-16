/******************************************************************************
 * lt/source/lt/driver/imagesensor/ov3660/LTDriverImageSensorOV3660.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * LT Driver Library for OV3660 image sensor.
 *
 * The OV3660 is a 3MP DVP-parallel image sensor controlled over SCCB (I2C-
 * compatible) using 16-bit register addresses.  It has no internal bank switch;
 * all registers are accessed directly by their full 16-bit address.
 *
 * Register tables and image-quality tuning data are derived from the
 * Espressif esp32-camera open-source driver (MIT / Apache-2.0 licence).
 *   https://github.com/espressif/esp32-camera
 *****************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/core/LTTime.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/imagesensor/LTDeviceImageSensor.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/config/LTDeviceConfig.h>

DEFINE_LTLOG_SECTION("drv.imagesensor.ov3660");
#define P(...) LTLOG_DEBUG(__VA_ARGS__)

/* =========================================================================
 * OV3660 hardware constants
 * ========================================================================= */

enum {
    kOV3660_I2cFrequency    = 100000,
    kOV3660_SccbAddr        = 0x3C,    /* 7-bit: 0x78 >> 1 */
    kOV3660_PidExpected     = 0x3660,

    kOV3660_OutputWidth     = 2048,
    kOV3660_OutputHeight    = 1536,

    kOV3660_PowerOnDelayMs  = 200,
};

/* =========================================================================
 * 16-bit register addresses
 * ========================================================================= */

enum {
    /* System */
    OV3660_SYSTEM_CTROL0        = 0x3008,

    /* PLL */
    OV3660_SC_PLLS_CTRL0        = 0x303a,
    OV3660_SC_PLLS_CTRL1        = 0x303b,
    OV3660_SC_PLLS_CTRL2        = 0x303c,
    OV3660_SC_PLLS_CTRL3        = 0x303d,
    OV3660_PCLK_RATIO           = 0x3824,
    OV3660_VFIFO_CTRL0C         = 0x460C,

    /* Timing / windowing */
    OV3660_X_ADDR_ST_H          = 0x3800,
    OV3660_X_ADDR_END_H         = 0x3804,
    OV3660_X_OUTPUT_SIZE_H      = 0x3808,
    OV3660_X_TOTAL_SIZE_H       = 0x380C,
    OV3660_X_OFFSET_H           = 0x3810,
    OV3660_X_INCREMENT          = 0x3814,
    OV3660_Y_INCREMENT          = 0x3815,
    OV3660_TIMING_TC_REG20      = 0x3820,
    OV3660_TIMING_TC_REG21      = 0x3821,

    /* AEC / AGC */
    OV3660_AEC_PK_MANUAL        = 0x3503,
    OV3660_AEC_EXP_H            = 0x3500,
    OV3660_AEC_EXP_M            = 0x3501,
    OV3660_AEC_EXP_L            = 0x3502,
    OV3660_AEC_GAIN_H           = 0x350A,
    OV3660_AEC_GAIN_L           = 0x350B,
    OV3660_AEC_AE_LEVEL_0F      = 0x3a0f,
    OV3660_AEC_AE_LEVEL_10      = 0x3a10,
    OV3660_AEC_AE_LEVEL_11      = 0x3a11,
    OV3660_AEC_AE_LEVEL_1B      = 0x3a1b,
    OV3660_AEC_AE_LEVEL_1E      = 0x3a1e,
    OV3660_AEC_AE_LEVEL_1F      = 0x3a1f,
    OV3660_NIGHT_MODE_CTRL      = 0x3a00,
    OV3660_GAIN_CEIL_H          = 0x3A18,
    OV3660_GAIN_CEIL_L          = 0x3A19,

    /* AWB */
    OV3660_AWB_MANUAL           = 0x3406,
    OV3660_AWB_R_H              = 0x3400,
    OV3660_AWB_G_H              = 0x3402,
    OV3660_AWB_B_H              = 0x3404,

    /* Format */
    OV3660_FORMAT_CTRL          = 0x501F,
    OV3660_FORMAT_CTRL00        = 0x4300,

    /* ISP */
    OV3660_ISP_CONTROL_01       = 0x5001,
    OV3660_ISP_5000             = 0x5000,
    OV3660_ISP_5183             = 0x5183,
    OV3660_ISP_5308             = 0x5308,
    OV3660_ISP_5306             = 0x5306,

    /* SDE (special effects / brightness / contrast / saturation) */
    OV3660_SDE_5580             = 0x5580,
    OV3660_SDE_5583             = 0x5583,
    OV3660_SDE_5584             = 0x5584,
    OV3660_SDE_5003             = 0x5003,
    OV3660_SDE_5586             = 0x5586,
    OV3660_SDE_5587             = 0x5587,
    OV3660_SDE_5588             = 0x5588,

    /* JPEG compression quality */
    OV3660_COMPRESSION_CTRL07   = 0x4407,

    /* Colorbar test */
    OV3660_PRE_ISP_TEST         = 0x503D,

    /* Sharpness */
    OV3660_SHARP_5300           = 0x5300,
    OV3660_SHARP_5302           = 0x5302,
    OV3660_SHARP_5303           = 0x5303,

    /* ID registers */
    OV3660_REG_PID_H            = 0x300A,
    OV3660_REG_PID_L            = 0x300B,

    /* Special register marker used in init table for delays */
    OV3660_REG_DLY              = 0xFFFF,
};

/* Bit masks */
#define OV3660_SYSCTRL0_SW_RESET    0x80
#define OV3660_SYSCTRL0_SW_PWDN     0x40

#define OV3660_AEC_AGC_MANUAL       0x02
#define OV3660_AEC_AEC_MANUAL       0x01

#define OV3660_TC20_VFLIP           0x06
#define OV3660_TC21_HMIRROR         0x06
#define OV3660_TC20_BINNING_V       0x01
#define OV3660_TC21_BINNING_H       0x01
#define OV3660_TC21_JPEG_EN         0x20

#define OV3660_ISP5000_BPC          0x04
#define OV3660_ISP5000_WPC          0x02
#define OV3660_ISP5000_GAMMA        0x20
#define OV3660_ISP5000_LENC         0x80

#define OV3660_TEST_COLOR_BAR       0xC0

#define OV3660_ISP_AWB_EN           0x01
#define OV3660_ISP_SCALE_EN         0x20

#define OV3660_SHARP_AUTO           0x40
#define OV3660_DENOISE_EN           0x10

/* =========================================================================
 * Register init table
 * Each entry is {u16 reg, u8 val}.  reg==0xFFFF means delay val ms.
 * reg==0x0000 (REGLIST_TAIL) terminates.
 * ========================================================================= */

typedef struct { u16 reg; u8 val; } OV3660_RegVal;

static const OV3660_RegVal s_defaultRegs[] = {
    {0x3008, 0x82},  /* software reset */
    {OV3660_REG_DLY, 10},

    {0x3103, 0x13},
    {0x3008, 0x42},
    {0x3017, 0xff},
    {0x3018, 0xff},
    {0x302c, 0xc3},  /* DRIVE_CAPABILITY */
    {0x4740, 0x21},  /* CLOCK_POL_CONTROL */
    {0x3611, 0x01},
    {0x3612, 0x2d},
    {0x3032, 0x00},
    {0x3614, 0x80},
    {0x3618, 0x00},
    {0x3619, 0x75},
    {0x3622, 0x80},
    {0x3623, 0x00},
    {0x3624, 0x03},
    {0x3630, 0x52},
    {0x3632, 0x07},
    {0x3633, 0xd2},
    {0x3704, 0x80},
    {0x3708, 0x66},
    {0x3709, 0x12},
    {0x370b, 0x12},
    {0x3717, 0x00},
    {0x371b, 0x60},
    {0x371c, 0x00},
    {0x3901, 0x13},
    {0x3600, 0x08},
    {0x3620, 0x43},
    {0x3702, 0x20},
    {0x3739, 0x48},
    {0x3730, 0x20},
    {0x370c, 0x0c},
    {0x3a18, 0x00},
    {0x3a19, 0xf8},
    {0x3000, 0x10},
    {0x3004, 0xef},
    {0x6700, 0x05},
    {0x6701, 0x19},
    {0x6702, 0xfd},
    {0x6703, 0xd1},
    {0x6704, 0xff},
    {0x6705, 0xff},
    {0x3c01, 0x80},
    {0x3c00, 0x04},
    {0x3a08, 0x00}, {0x3a09, 0x62},
    {0x3a0e, 0x08},
    {0x3a0a, 0x00}, {0x3a0b, 0x52},
    {0x3a0d, 0x09},
    {0x3a00, 0x3a},
    {0x3a14, 0x09},
    {0x3a15, 0x30},
    {0x3a02, 0x09},
    {0x3a03, 0x30},
    {0x440e, 0x08},
    {0x4520, 0x0b},
    {0x460b, 0x37},
    {0x4713, 0x02},
    {0x471c, 0xd0},
    {0x5086, 0x00},
    {0x5002, 0x00},
    {0x501f, 0x00},
    {0x3008, 0x02},
    {0x5180, 0xff}, {0x5181, 0xf2}, {0x5182, 0x00}, {0x5183, 0x14},
    {0x5184, 0x25}, {0x5185, 0x24}, {0x5186, 0x16}, {0x5187, 0x16},
    {0x5188, 0x16}, {0x5189, 0x68}, {0x518a, 0x60}, {0x518b, 0xe0},
    {0x518c, 0xb2}, {0x518d, 0x42}, {0x518e, 0x35}, {0x518f, 0x56},
    {0x5190, 0x56}, {0x5191, 0xf8}, {0x5192, 0x04}, {0x5193, 0x70},
    {0x5194, 0xf0}, {0x5195, 0xf0}, {0x5196, 0x03}, {0x5197, 0x01},
    {0x5198, 0x04}, {0x5199, 0x12}, {0x519a, 0x04}, {0x519b, 0x00},
    {0x519c, 0x06}, {0x519d, 0x82}, {0x519e, 0x38},
    {0x5381, 0x1d}, {0x5382, 0x60}, {0x5383, 0x03}, {0x5384, 0x0c},
    {0x5385, 0x78}, {0x5386, 0x84}, {0x5387, 0x7d}, {0x5388, 0x6b},
    {0x5389, 0x12}, {0x538a, 0x01}, {0x538b, 0x98},
    {0x5480, 0x01},
    {0x5000, 0xa7},
    {0x5800, 0x0c}, {0x5801, 0x09}, {0x5802, 0x0c}, {0x5803, 0x0c},
    {0x5804, 0x0d}, {0x5805, 0x17}, {0x5806, 0x06}, {0x5807, 0x05},
    {0x5808, 0x04}, {0x5809, 0x06}, {0x580a, 0x09}, {0x580b, 0x0e},
    {0x580c, 0x05}, {0x580d, 0x01}, {0x580e, 0x01}, {0x580f, 0x01},
    {0x5810, 0x05}, {0x5811, 0x0d}, {0x5812, 0x05}, {0x5813, 0x01},
    {0x5814, 0x01}, {0x5815, 0x01}, {0x5816, 0x05}, {0x5817, 0x0d},
    {0x5818, 0x08}, {0x5819, 0x06}, {0x581a, 0x05}, {0x581b, 0x07},
    {0x581c, 0x0b}, {0x581d, 0x0d}, {0x581e, 0x12}, {0x581f, 0x0d},
    {0x5820, 0x0e}, {0x5821, 0x10}, {0x5822, 0x10}, {0x5823, 0x1e},
    {0x5824, 0x53}, {0x5825, 0x15}, {0x5826, 0x05}, {0x5827, 0x14},
    {0x5828, 0x54}, {0x5829, 0x25}, {0x582a, 0x33}, {0x582b, 0x33},
    {0x582c, 0x34}, {0x582d, 0x16}, {0x582e, 0x24}, {0x582f, 0x41},
    {0x5830, 0x50}, {0x5831, 0x42}, {0x5832, 0x15}, {0x5833, 0x25},
    {0x5834, 0x34}, {0x5835, 0x33}, {0x5836, 0x24}, {0x5837, 0x26},
    {0x5838, 0x54}, {0x5839, 0x25}, {0x583a, 0x15}, {0x583b, 0x25},
    {0x583c, 0x53}, {0x583d, 0xcf},
    {0x3a0f, 0x30}, {0x3a10, 0x28}, {0x3a1b, 0x30}, {0x3a1e, 0x28},
    {0x3a11, 0x60}, {0x3a1f, 0x14},
    {0x5302, 0x28}, {0x5303, 0x20},
    {0x5306, 0x1c}, {0x5307, 0x28},
    {0x4002, 0xc5}, {0x4003, 0x81}, {0x4005, 0x12},
    {0x5688, 0x11}, {0x5689, 0x11}, {0x568a, 0x11}, {0x568b, 0x11},
    {0x568c, 0x11}, {0x568d, 0x11}, {0x568e, 0x11}, {0x568f, 0x11},
    {0x5580, 0x06}, {0x5588, 0x00}, {0x5583, 0x40}, {0x5584, 0x2c},
    {0x5001, 0x83},
    {0x0000, 0x00}  /* terminator */
};

/* Pixel format tables */
static const OV3660_RegVal s_fmtJpeg[] = {
    {OV3660_FORMAT_CTRL,   0x00},   /* YUV422 */
    {OV3660_FORMAT_CTRL00, 0x30},   /* YUYV */
    {0x3002, 0x00},
    {0x3006, 0xff},
    {0x471c, 0x50},
    {0x0000, 0x00}
};

static const OV3660_RegVal s_fmtYuv422[] = {
    {OV3660_FORMAT_CTRL,   0x00},
    {OV3660_FORMAT_CTRL00, 0x30},   /* YUYV */
    {0x0000, 0x00}
};

static const OV3660_RegVal s_fmtGrayscale[] = {
    {OV3660_FORMAT_CTRL,   0x00},
    {OV3660_FORMAT_CTRL00, 0x10},   /* Y8 */
    {0x0000, 0x00}
};

static const OV3660_RegVal s_fmtRgb565[] = {
    {OV3660_FORMAT_CTRL,   0x01},
    {OV3660_FORMAT_CTRL00, 0x61},   /* RGB565 BGR */
    {0x0000, 0x00}
};

/* Saturation lookup (9 levels: -4..+4), 11 bytes each */
static const u8 s_saturationRegs[9][11] = {
    {0x1d,0x60,0x03,0x07,0x48,0x4f,0x4b,0x40,0x0b,0x01,0x98}, /* -4 */
    {0x1d,0x60,0x03,0x08,0x54,0x5c,0x58,0x4b,0x0d,0x01,0x98}, /* -3 */
    {0x1d,0x60,0x03,0x0a,0x60,0x6a,0x64,0x56,0x0e,0x01,0x98}, /* -2 */
    {0x1d,0x60,0x03,0x0b,0x6c,0x77,0x70,0x60,0x10,0x01,0x98}, /* -1 */
    {0x1d,0x60,0x03,0x0c,0x78,0x84,0x7d,0x6b,0x12,0x01,0x98}, /*  0 */
    {0x1d,0x60,0x03,0x0d,0x84,0x91,0x8a,0x76,0x14,0x01,0x98}, /* +1 */
    {0x1d,0x60,0x03,0x0e,0x90,0x9e,0x96,0x80,0x16,0x01,0x98}, /* +2 */
    {0x1d,0x60,0x03,0x10,0x9c,0xac,0xa2,0x8b,0x17,0x01,0x98}, /* +3 */
    {0x1d,0x60,0x03,0x11,0xa8,0xb9,0xaf,0x96,0x19,0x01,0x98}, /* +4 */
};

/* Special effects lookup (7 effects), 4 bytes each */
static const u8 s_specialEffectRegs[7][4] = {
    {0x06,0x40,0x2c,0x08}, /* Normal    */
    {0x46,0x40,0x28,0x08}, /* Negative  */
    {0x1e,0x80,0x80,0x08}, /* Grayscale */
    {0x1e,0x80,0xc0,0x08}, /* Red tint  */
    {0x1e,0x60,0x60,0x08}, /* Green tint*/
    {0x1e,0xa0,0x40,0x08}, /* Blue tint */
    {0x1e,0x40,0xa0,0x08}, /* Sepia     */
};

/* =========================================================================
 * Aspect-ratio / window table (from ov3660_settings.h ratio_table)
 * ========================================================================= */

typedef struct {
    u16 maxW, maxH;
    u16 startX, startY;
    u16 endX, endY;
    u16 offsetX, offsetY;
    u16 totalX, totalY;
} OV3660_RatioSettings;

/* Indexed by aspect_ratio_t order: 4x3, 3x2, 16x10, 5x3, 16x9, 21x9, 5x4, 1x1, 9x16 */
static const OV3660_RatioSettings s_ratioTable[] = {
    {2048,1536,   0,  0,2079,1547,16,6,2300,1564}, /* 4x3  */
    {1920,1280,  64,128,2015,1419,16,6,2172,1436}, /* 3x2  */
    {2048,1280,   0,128,2079,1419,16,6,2300,1436}, /* 16x10*/
    {1920,1152,  64,192,2015,1355,16,6,2172,1372}, /* 5x3  */
    {1920,1080,  64,242,2015,1333,16,6,2172,1322}, /* 16x9 */
    {2048, 880,   0,328,2079,1219,16,6,2300,1236}, /* 21x9 */
    {1920,1536,  64,  0,2015,1547,16,6,2172,1564}, /* 5x4  */
    {1536,1536, 256,  0,1823,1547,16,6,2044,1564}, /* 1x1  */
    { 864,1536, 592,  0,1487,1547,16,6,2044,1564}, /* 9x16 */
};

/* Frame size → {output W, H, ratio index} */
typedef struct {
    u16 w, h;
    u8  ratioIdx; /* index into s_ratioTable */
} OV3660_FrameInfo;

/* Aspect ratio for each frame size (4:3 unless otherwise noted) */
static const OV3660_FrameInfo s_frameSizeTable[] = {
    [kLTImageSensorFrameSize_96x96]    = {  96,  96, 7}, /* 1:1  */
    [kLTImageSensorFrameSize_160x120]  = { 160, 120, 0}, /* 4:3  */
    [kLTImageSensorFrameSize_176x144]  = { 176, 144, 0},
    [kLTImageSensorFrameSize_240x176]  = { 240, 176, 0},
    [kLTImageSensorFrameSize_240x240]  = { 240, 240, 7},
    [kLTImageSensorFrameSize_320x240]  = { 320, 240, 0},
    [kLTImageSensorFrameSize_400x296]  = { 400, 296, 0},
    [kLTImageSensorFrameSize_480x320]  = { 480, 320, 3}, /* 3x2  */
    [kLTImageSensorFrameSize_640x480]  = { 640, 480, 0},
    [kLTImageSensorFrameSize_800x600]  = { 800, 600, 0},
    [kLTImageSensorFrameSize_1024x768] = {1024, 768, 0},
    [kLTImageSensorFrameSize_1280x720] = {1280, 720, 4}, /* 16:9 */
    [kLTImageSensorFrameSize_1280x1024]= {1280,1024, 0},
    [kLTImageSensorFrameSize_1600x1200]= {1600,1200, 0},
    [kLTImageSensorFrameSize_2048x1536]= {2048,1536, 0}, /* QXGA native */
};

/* =========================================================================
 * Instance state
 * ========================================================================= */

typedef struct {
    LTAtomic     refCount;
    struct {
        const char *name;
        u32         resetPinNumber;
        u32         powerdownPinNumber;
        u32         i2cBusIndex;
        u16         i2cAddress;
    } cfg;
    LTEvent          hEvent;
    LTDeviceUnit     hResetPin;
    LTDeviceUnit     hPowerdownPin;
    LTDeviceUnit     hI2cBus;
    LTImageSensorPixelFormat pixelFormat;
    LTImageSensorFrameSize   frameSize;
    bool                     binning;
    bool                     vflip;
    bool                     hmirror;
} OV3660_Instance;

typedef struct {
    OV3660_Instance *pDeviceUnits;
    u32              nNumDeviceUnits;
} DeviceUnits;

static DeviceUnits      s_DeviceUnits;
static LTDeviceI2C     *s_libI2C;
static LTDevicePins    *s_libPins;
static ILTThread       *iThread;
static ILTEvent        *iEvent;

static const LTArgsDescriptor s_SensorEventArgs = {
    .nNumArgs = 1, .argTypes = { kLTArgType_u32 }
};

define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceImageSensor, LTDriverImageSensorOV3660);

/* =========================================================================
 * SCCB (16-bit address) I2C helpers
 * ========================================================================= */

static void WriteReg(OV3660_Instance *inst, u16 reg, u8 val) {
    u8 cmd[3] = { (u8)(reg >> 8), (u8)(reg & 0xFF), val };
    s_libI2C->I2CMasterTransfer(inst->hI2cBus, inst->cfg.i2cAddress,
        NULL, 0, cmd, sizeof(cmd), true, true, NULL, inst);
}

static u8 ReadReg(OV3660_Instance *inst, u16 reg) {
    u8 cmd[2] = { (u8)(reg >> 8), (u8)(reg & 0xFF) };
    u8 result = 0;
    s_libI2C->I2CMasterTransfer(inst->hI2cBus, inst->cfg.i2cAddress,
        &result, 1, cmd, sizeof(cmd), true, true, NULL, inst);
    return result;
}

static u16 ReadReg16(OV3660_Instance *inst, u16 reg) {
    return ((u16)ReadReg(inst, reg) << 8) | ReadReg(inst, reg + 1);
}

static void WriteReg16(OV3660_Instance *inst, u16 reg, u16 val) {
    WriteReg(inst, reg,     (u8)(val >> 8));
    WriteReg(inst, reg + 1, (u8)(val & 0xFF));
}

static void SetRegBits(OV3660_Instance *inst, u16 reg, u8 mask, bool enable) {
    u8 cur = ReadReg(inst, reg);
    WriteReg(inst, reg, enable ? (cur | mask) : (cur & ~mask));
}

static void WriteRegTable(OV3660_Instance *inst, const OV3660_RegVal *tbl) {
    for (; tbl->reg != 0x0000; ++tbl) {
        if (tbl->reg == OV3660_REG_DLY) {
            iThread->Sleep(LTTime_Milliseconds(tbl->val));
        } else {
            WriteReg(inst, tbl->reg, tbl->val);
        }
    }
}

/* =========================================================================
 * Sensor programming helpers
 * ========================================================================= */

static void ApplyPixelFormat(OV3660_Instance *inst) {
    const OV3660_RegVal *tbl;
    switch (inst->pixelFormat) {
        case kLTImageSensorPixelFormat_RGB565:   tbl = s_fmtRgb565;    break;
        case kLTImageSensorPixelFormat_YUV422:   tbl = s_fmtYuv422;    break;
        case kLTImageSensorPixelFormat_Grayscale:tbl = s_fmtGrayscale; break;
        case kLTImageSensorPixelFormat_JPEG:     tbl = s_fmtJpeg;      break;
        default:                                 return;
    }
    WriteRegTable(inst, tbl);
}

static void ApplyImageOptions(OV3660_Instance *inst) {
    u8 reg20 = 0, reg21 = 0, reg4514 = 0;

    /* JPEG compression flag in TIMING_TC_REG21 */
    if (inst->pixelFormat == kLTImageSensorPixelFormat_JPEG)
        reg21 |= OV3660_TC21_JPEG_EN;

    if (inst->binning) {
        reg20 |= OV3660_TC20_BINNING_V;
        reg21 |= OV3660_TC21_BINNING_H;
    } else {
        reg20 |= 0x40;
    }

    if (inst->vflip)   reg20 |= OV3660_TC20_VFLIP;
    if (inst->hmirror) reg21 |= OV3660_TC21_HMIRROR;

    /* reg4514 lookup: bits [vflip|hmirror|binning] */
    u8 sel = (inst->vflip ? 1 : 0) | (inst->hmirror ? 2 : 0) | (inst->binning ? 4 : 0);
    static const u8 kReg4514[8] = {0x88,0x88,0xbb,0xbb,0xaa,0xbb,0xbb,0xaa};
    reg4514 = kReg4514[sel];

    WriteReg(inst, OV3660_TIMING_TC_REG20, reg20);
    WriteReg(inst, OV3660_TIMING_TC_REG21, reg21);
    WriteReg(inst, 0x4514, reg4514);

    if (inst->binning) {
        WriteReg(inst, 0x4520, 0x0b);
        WriteReg(inst, OV3660_X_INCREMENT, 0x31);
        WriteReg(inst, OV3660_Y_INCREMENT, 0x31);
    } else {
        WriteReg(inst, 0x4520, 0xb0);
        WriteReg(inst, OV3660_X_INCREMENT, 0x11);
        WriteReg(inst, OV3660_Y_INCREMENT, 0x11);
    }
}

static void ApplyPll(OV3660_Instance *inst, bool bypass, u8 multiplier,
                     u8 sysDiv, u8 preDiv, bool root2x, u8 seld5,
                     bool pclkManual, u8 pclkDiv) {
    WriteReg(inst, OV3660_SC_PLLS_CTRL0, bypass ? 0x80 : 0x00);
    WriteReg(inst, OV3660_SC_PLLS_CTRL1, multiplier & 0x1f);
    WriteReg(inst, OV3660_SC_PLLS_CTRL2, 0x10 | (sysDiv & 0x0f));
    WriteReg(inst, OV3660_SC_PLLS_CTRL3,
        (u8)((preDiv & 0x3) << 4) | seld5 | (root2x ? 0x40 : 0x00));
    WriteReg(inst, OV3660_PCLK_RATIO,   pclkDiv & 0x1f);
    WriteReg(inst, OV3660_VFIFO_CTRL0C, pclkManual ? 0x22 : 0x20);
}

static void ApplyFrameSize(OV3660_Instance *inst) {
    if (inst->frameSize >= (sizeof(s_frameSizeTable) / sizeof(s_frameSizeTable[0]))) return;
    const OV3660_FrameInfo *fi = &s_frameSizeTable[inst->frameSize];
    if (!fi->w || !fi->h) return;

    const OV3660_RatioSettings *rs = &s_ratioTable[fi->ratioIdx];
    bool binning = (fi->w <= rs->maxW / 2 && fi->h <= rs->maxH / 2);
    inst->binning = binning;
    bool scale = !((fi->w == rs->maxW && fi->h == rs->maxH)
                || (fi->w == rs->maxW / 2 && fi->h == rs->maxH / 2));

    /* Write window registers */
    WriteReg16(inst, OV3660_X_ADDR_ST_H,     rs->startX);
    WriteReg16(inst, OV3660_X_ADDR_ST_H + 2, rs->startY);
    WriteReg16(inst, OV3660_X_ADDR_END_H,    rs->endX);
    WriteReg16(inst, OV3660_X_ADDR_END_H + 2,rs->endY);
    WriteReg16(inst, OV3660_X_OUTPUT_SIZE_H,  fi->w);
    WriteReg16(inst, OV3660_X_OUTPUT_SIZE_H + 2, fi->h);

    if (binning) {
        WriteReg16(inst, OV3660_X_TOTAL_SIZE_H,     rs->totalX);
        WriteReg16(inst, OV3660_X_TOTAL_SIZE_H + 2, rs->totalY / 2 + 1);
        WriteReg16(inst, OV3660_X_OFFSET_H,     8);
        WriteReg16(inst, OV3660_X_OFFSET_H + 2, 2);
    } else {
        WriteReg16(inst, OV3660_X_TOTAL_SIZE_H,     rs->totalX);
        WriteReg16(inst, OV3660_X_TOTAL_SIZE_H + 2, rs->totalY);
        WriteReg16(inst, OV3660_X_OFFSET_H,     rs->offsetX);
        WriteReg16(inst, OV3660_X_OFFSET_H + 2, rs->offsetY);
    }

    /* Scale enable */
    SetRegBits(inst, OV3660_ISP_CONTROL_01, OV3660_ISP_SCALE_EN, scale);

    ApplyImageOptions(inst);

    /* PLL: tuned for 16 MHz XCLK */
    if (inst->pixelFormat == kLTImageSensorPixelFormat_JPEG) {
        if (inst->frameSize == kLTImageSensorFrameSize_2048x1536) {
            ApplyPll(inst, false, 24, 1, 3, false, 0, true, 8);   /* 40 MHz SYSCLK */
        } else {
            ApplyPll(inst, false, 30, 1, 3, false, 0, true, 10);  /* 50 MHz SYSCLK */
        }
    } else {
        if (fi->w > 480) {
            ApplyPll(inst, false, 4, 1, 0, false, 2, true, 2);   /* 8 MHz  */
        } else if (fi->w >= 320) {
            ApplyPll(inst, false, 8, 1, 0, false, 2, true, 4);   /* 16 MHz */
        } else {
            ApplyPll(inst, false, 8, 1, 0, false, 0, true, 8);   /* 32 MHz */
        }
    }
}

/* =========================================================================
 * Power-on sequence
 * ========================================================================= */

static void DispatchSensorEvent(LTEvent hEvent, void *eventProc, LTArgs *eventArgs, void *eventProcClientData) {
    LT_UNUSED(hEvent);
    LTDeviceImageSensor_OnEventProc *cb = eventProc;
    LTImageSensorEvent event = LTArgs_u32At(0, eventArgs);
    cb(event, eventProcClientData);
}

static void PowerOnTimerProc(void *clientData) {
    iThread->KillTimer(iThread->GetCurrentThread(), PowerOnTimerProc, clientData);
    OV3660_Instance *inst = clientData;

    /* SCCB (OV sensors) does not ACK a plain I²C address probe, so skip it.
     * Instead verify the sensor is present by reading the PID registers. */
    u16 pid = ReadReg16(inst, OV3660_REG_PID_H);
    if (pid != kOV3660_PidExpected) {
        LTLOG_YELLOWALERT("pow.pid", "unexpected PID 0x%04lx (want 0x%04lx)",
            LT_Pu32(pid), LT_Pu32(kOV3660_PidExpected));
        iEvent->NotifyEvent(inst->hEvent, kLTImageSensorEvent_PowerOnFailed);
        return;
    }

    /* Software reset and full init */
    WriteReg(inst, OV3660_SYSTEM_CTROL0, 0x82);
    iThread->Sleep(LTTime_Milliseconds(10));
    WriteRegTable(inst, s_defaultRegs);
    ApplyPixelFormat(inst);
    ApplyFrameSize(inst);

    P("pow.ok", "OV3660 ready, PID=0x%04lx", LT_Pu32(pid));
    iEvent->NotifyEvent(inst->hEvent, kLTImageSensorEvent_PowerOn);
}

/* =========================================================================
 * Per-instance init/shutdown
 * ========================================================================= */

static bool InitSensor(OV3660_Instance *inst) {
    inst->pixelFormat = kLTImageSensorPixelFormat_JPEG;
    inst->frameSize   = kLTImageSensorFrameSize_320x240;
    inst->binning     = false;
    inst->vflip       = false;
    inst->hmirror     = false;

    if (!(inst->hEvent = LT_GetCore()->CreateEvent(&s_SensorEventArgs, DispatchSensorEvent, NULL, NULL, NULL))) {
        LTLOG_REDALERT("ins.ev", "Failed to create event");
        return false;
    }

    /* reset pin — optional */
    if (inst->cfg.resetPinNumber != LT_U32_MAX) {
        LTDeviceUnit hPin = s_libPins->CreateDeviceUnitHandle(inst->cfg.resetPinNumber);
        if (!hPin) {
            LTLOG_REDALERT("ins.rst.pin", NULL);
            return false;
        }
        ILTDriverPins_BidirectionalBank *ib = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPin);
        if (!ib) {
            lt_destroyhandle(hPin);
            LTLOG_REDALERT("ins.rst.intf", NULL);
            return false;
        }
        ib->ConfigureAsOutput(hPin, kLTDevicePin_PinConfiguration_OutputType_OpenDrain);
        ib->Set(hPin, 0);
        inst->hResetPin = hPin;
    }

    /* power-down pin — optional */
    if (inst->cfg.powerdownPinNumber != LT_U32_MAX) {
        LTDeviceUnit hPin = s_libPins->CreateDeviceUnitHandle(inst->cfg.powerdownPinNumber);
        if (!hPin) {
            LTLOG_REDALERT("ins.pwdn.pin", NULL);
            return false;
        }
        ILTDriverPins_BidirectionalBank *ib = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, hPin);
        if (!ib) {
            lt_destroyhandle(hPin);
            LTLOG_REDALERT("ins.pwdn.intf", NULL);
            return false;
        }
        ib->ConfigureAsOutput(hPin, kLTDevicePin_PinConfiguration_OutputType_PushPull);
        ib->Set(hPin, 0);   /* PWDN low = sensor powered on */
        inst->hPowerdownPin = hPin;
    }

    if (!(inst->hI2cBus = s_libI2C->CreateDeviceUnitHandle(inst->cfg.i2cBusIndex))) {
        LTLOG_REDALERT("ins.i2c.open", "Failed to open I2C bus %lu", LT_Pu32(inst->cfg.i2cBusIndex));
        return false;
    }
    if (!s_libI2C->SetDeviceConfiguration(inst->hI2cBus, &(LTDeviceI2C_Configuration){
            .Async = false, .Master = true, .Frequency = kOV3660_I2cFrequency })) {
        LTLOG_REDALERT("ins.i2c.cfg", "Failed to configure I2C bus");
        return false;
    }
    return true;
}


static void ShutdownSensor(OV3660_Instance *inst) {
    lt_destroyhandle(inst->hResetPin);
    lt_destroyhandle(inst->hPowerdownPin);
    lt_destroyhandle(inst->hI2cBus);
    lt_destroyhandle(inst->hEvent);
}

/* =========================================================================
 * Device unit configuration from LTDeviceConfig
 * ========================================================================= */

static bool ConfigureDeviceUnit(LTDeviceConfig *cfg, OV3660_Instance *inst, u32 section) {
    if (!(inst->cfg.name = cfg->ReadString(section, "name"))) {
        LTLOG_REDALERT("cdu.name", NULL); return false;
    }
    const char *busName = cfg->ReadString(section, "i2c-bus");
    if (!busName) {
        LTLOG_REDALERT("cdu.i2c.name", NULL);
        return false;
    }
    inst->cfg.i2cBusIndex = s_libI2C->GetBusIndexFromName(busName);
    if (inst->cfg.i2cBusIndex == LT_U32_MAX) {
        LTLOG_REDALERT("cdu.i2c.idx", NULL);
        return false;
    }

    u32 addr = cfg->ReadInteger(section, "i2c-address");
    inst->cfg.i2cAddress = addr ? (u16)addr : kOV3660_SccbAddr;

    /* reset-pin is optional */
    inst->cfg.resetPinNumber = LT_U32_MAX;
    const char *pinName = cfg->ReadString(section, "reset-pin");
    if (pinName && !s_libPins->GetUnitNumberFromBankName(pinName, &inst->cfg.resetPinNumber)) {
        LTLOG_REDALERT("cdu.rst.num", NULL); return false;
    }

    /* powerdown-pin is optional; if absent the sensor is assumed always powered */
    inst->cfg.powerdownPinNumber = LT_U32_MAX;
    pinName = cfg->ReadString(section, "powerdown-pin");
    if (pinName && !s_libPins->GetUnitNumberFromBankName(pinName, &inst->cfg.powerdownPinNumber)) {
        LTLOG_REDALERT("cdu.pwdn.num", NULL); return false;
    }
    return true;
}

static bool ConfigureDeviceUnits(void) {
    if (s_DeviceUnits.nNumDeviceUnits || s_DeviceUnits.pDeviceUnits) return false;
    LTDeviceConfig *pCfg = lt_openlibrary(LTDeviceConfig);
    u32 driverSection = pCfg->GetDriverSection("LTDeviceImageSensor", "LTDriverImageSensorOV3660");
    s_DeviceUnits.nNumDeviceUnits = pCfg->GetNumDeviceUnits(driverSection);
    if (!s_DeviceUnits.nNumDeviceUnits) { lt_closelibrary(pCfg); return true; }

    if (!(s_DeviceUnits.pDeviceUnits = lt_malloc(s_DeviceUnits.nNumDeviceUnits * sizeof(OV3660_Instance)))) {
        LTLOG_REDALERT("cdus.oom", NULL); lt_closelibrary(pCfg); return false;
    }
    lt_memset(s_DeviceUnits.pDeviceUnits, 0, s_DeviceUnits.nNumDeviceUnits * sizeof(OV3660_Instance));

    for (u32 i = 0; i < s_DeviceUnits.nNumDeviceUnits; ++i) {
        u32 unitSection = pCfg->GetDeviceUnitSectionAt(driverSection, i);
        if (!unitSection || !ConfigureDeviceUnit(pCfg, &s_DeviceUnits.pDeviceUnits[i], unitSection)) {
            LTLOG_REDALERT("cdus.err", "Failed to configure device unit %lu", LT_Pu32(i));
            lt_closelibrary(pCfg);
            return false;
        }
    }
    lt_closelibrary(pCfg);
    return true;
}

/* =========================================================================
 * Library init / fini
 * ========================================================================= */

static void Shutdown(void) {
    if (s_DeviceUnits.pDeviceUnits) {
        for (u32 i = s_DeviceUnits.nNumDeviceUnits; i; --i)
            ShutdownSensor(&s_DeviceUnits.pDeviceUnits[i - 1]);
        lt_free(s_DeviceUnits.pDeviceUnits);
        s_DeviceUnits.pDeviceUnits = NULL;
    }
    s_DeviceUnits.nNumDeviceUnits = 0;
    lt_closelibrary(s_libI2C);  s_libI2C  = NULL;
    lt_closelibrary(s_libPins); s_libPins = NULL;
    iThread = NULL;
}

static bool LTDriverImageSensorOV3660Impl_LibInit(void) {
    if (!(iThread = lt_getlibraryinterface(ILTThread, LT_GetCore())) ||
        !(iEvent  = lt_getlibraryinterface(ILTEvent,  LT_GetCore())) ||
        !(s_libPins = lt_openlibrary(LTDevicePins)) ||
        !(s_libI2C  = lt_openlibrary(LTDeviceI2C))  ||
        !ConfigureDeviceUnits()) {
        Shutdown();
        return false;
    }
    return true;
}

static void LTDriverImageSensorOV3660Impl_LibFini(void) {
    Shutdown();
}

/* =========================================================================
 * ILTDriverImageSensor implementation
 * ========================================================================= */

static ILTDriverImageSensor s_ILTDriverImageSensor;

static u32 LTDriverImageSensorOV3660Impl_GetNumDeviceUnits(void) {
    return s_DeviceUnits.nNumDeviceUnits;
}

static OV3660_Instance *InstanceFromHandle(LTDeviceUnit hUnit) {
    OV3660_Instance **pp = (OV3660_Instance **)LT_GetCore()->ReserveHandlePrivateData(hUnit);
    OV3660_Instance *inst = NULL;
    if (pp) {
        inst = *pp;
        LT_GetCore()->ReleaseHandlePrivateData(hUnit, pp);
    }
    return inst;
}

static LTDeviceUnit LTDriverImageSensorOV3660Impl_CreateDeviceUnitHandle(u32 n) {
    if (n >= s_DeviceUnits.nNumDeviceUnits) return 0;
    LTDeviceUnit hDevice = LT_GetCore()->CreateHandle(
        (LTInterface *)&s_ILTDriverImageSensor, sizeof(OV3660_Instance *));
    if (!hDevice) return 0;
    bool ok = false;
    OV3660_Instance **pp = (OV3660_Instance **)LT_GetCore()->ReserveHandlePrivateData(hDevice);
    if (pp) {
        OV3660_Instance *inst = *pp = &s_DeviceUnits.pDeviceUnits[n];
        ok = true;
        if (LTAtomic_FetchAdd(&inst->refCount, 1) == 0)
            ok = InitSensor(inst);
        LT_GetCore()->ReleaseHandlePrivateData(hDevice, pp);
    }
    if (!ok) { lt_destroyhandle(hDevice); return 0; }
    return hDevice;
}

static void OnDestroyHandle(LTHandle hDevice) {
    OV3660_Instance *inst = InstanceFromHandle(hDevice);
    if (inst && LTAtomic_FetchSubtract(&inst->refCount, 1) == 1)
        ShutdownSensor(inst);
}

static void LTDriverImageSensorOV3660Impl_OnImageSensorEvent(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *cb, void *clientData) {
    OV3660_Instance *inst = InstanceFromHandle(hUnit);
    if (!inst) return;
    iEvent->RegisterForEvent(inst->hEvent, cb, NULL, clientData, false);
}

static void LTDriverImageSensorOV3660Impl_NoImageSensorEvent(LTDeviceUnit hUnit, LTDeviceImageSensor_OnEventProc *cb) {
    OV3660_Instance *inst = InstanceFromHandle(hUnit);
    if (!inst) return;
    iEvent->UnregisterFromEvent(inst->hEvent, cb);
}

static void LTDriverImageSensorOV3660Impl_PowerOn(LTDeviceUnit hUnit) {
    OV3660_Instance *inst = InstanceFromHandle(hUnit);
    if (!inst) return;

    if (inst->hPowerdownPin) {
        ILTDriverPins_BidirectionalBank *ib = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, inst->hPowerdownPin);
        ib->Set(inst->hPowerdownPin, 0);    /* deassert PWDN */
    }

    if (inst->hResetPin) {
        ILTDriverPins_BidirectionalBank *ibRst = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, inst->hResetPin);
        ibRst->Set(inst->hResetPin, 0);
        iThread->Sleep(LTTime_Milliseconds(5));
        ibRst->Set(inst->hResetPin, 1);
    }
    iThread->SetTimer(iThread->GetCurrentThread(),
        LTTime_Milliseconds(kOV3660_PowerOnDelayMs), PowerOnTimerProc, NULL, inst);
}

static void LTDriverImageSensorOV3660Impl_PowerOff(LTDeviceUnit hUnit) {
    OV3660_Instance *inst = InstanceFromHandle(hUnit);
    if (!inst) return;
    if (inst->hResetPin) {
        ILTDriverPins_BidirectionalBank *ibRst = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, inst->hResetPin);
        ibRst->Set(inst->hResetPin, 0);
    }
    if (inst->hPowerdownPin) {
        ILTDriverPins_BidirectionalBank *ib = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, inst->hPowerdownPin);
        ib->Set(inst->hPowerdownPin, 1);    /* assert PWDN */
    }
}

static bool LTDriverImageSensorOV3660Impl_SetAttribute(
        LTDeviceUnit hUnit, LTImageSensorAttribute attr, const void *value) {
    OV3660_Instance *inst = InstanceFromHandle(hUnit);
    if (!inst) return false;

    switch (attr) {

        case kLTImageSensorAttribute_PixelFormat: {
            inst->pixelFormat = *(const LTImageSensorPixelFormat *)value;
            ApplyPixelFormat(inst);
            ApplyImageOptions(inst);   /* JPEG flag in TC_REG21 depends on format */
            return true;
        }

        case kLTImageSensorAttribute_FrameSize: {
            LTImageSensorFrameSize sz = *(const LTImageSensorFrameSize *)value;
            if (sz >= (sizeof(s_frameSizeTable) / sizeof(s_frameSizeTable[0]))) return false;
            inst->frameSize = sz;
            ApplyFrameSize(inst);
            return true;
        }

        case kLTImageSensorAttribute_FrameResolution: {
            const LTImageSensorFrameResolution *res = value;
            for (LTImageSensorFrameSize i = 0; i < (LTImageSensorFrameSize)(sizeof(s_frameSizeTable)/sizeof(s_frameSizeTable[0])); ++i) {
                if (s_frameSizeTable[i].w == res->width && s_frameSizeTable[i].h == res->height) {
                    inst->frameSize = i;
                    ApplyFrameSize(inst);
                    return true;
                }
            }
            return false;
        }

        case kLTImageSensorAttribute_JpegQuality: {
            int q = *(const int *)value;
            if (q < 0) q = 0;
            if (q > 63) q = 63;
            WriteReg(inst, OV3660_COMPRESSION_CTRL07, (u8)q);
            return true;
        }

        case kLTImageSensorAttribute_Brightness: {
            /* OV3660 supports ±3; clamp inputs beyond that */
            int level = *(const int *)value;
            if (level < -3) level = -3;
            if (level > 3) level = 3;
            static const u8 kBrightVals[7] = {0x30,0x20,0x10,0x00,0x10,0x20,0x30};
            u8 val = kBrightVals[level + 3];
            bool neg = (level < 0);
            WriteReg(inst, OV3660_SDE_5587, val);
            SetRegBits(inst, OV3660_SDE_5588, 0x08, neg);
            return true;
        }

        case kLTImageSensorAttribute_Contrast: {
            int level = *(const int *)value;
            if (level < -3) level = -3;
            if (level > 3) level = 3;
            WriteReg(inst, OV3660_SDE_5586, (u8)((level + 4) << 3));
            return true;
        }

        case kLTImageSensorAttribute_Saturation: {
            /* OV3660 supports ±4 */
            int level = *(const int *)value;
            if (level < -4) level = -4;
            if (level > 4) level = 4;
            const u8 *regs = s_saturationRegs[level + 4];
            for (int i = 0; i < 11; ++i)
                WriteReg(inst, (u16)(0x5381 + i), regs[i]);
            return true;
        }

        case kLTImageSensorAttribute_Sharpness: {
            int level = *(const int *)value;
            if (level < -3) level = -3;
            if (level > 3) level = 3;
            u8 offset2 = (u8)((level + 3) * 8);
            u8 offset1 = offset2 + 1;
            SetRegBits(inst, OV3660_ISP_5308, OV3660_SHARP_AUTO, false);
            WriteReg(inst, OV3660_SHARP_5300, 0x10);
            WriteReg(inst, 0x5301, 0x10);
            WriteReg(inst, OV3660_SHARP_5302, offset1);
            WriteReg(inst, OV3660_SHARP_5303, offset2);
            WriteReg(inst, 0x5309, 0x10);
            WriteReg(inst, 0x530a, 0x10);
            WriteReg(inst, 0x530b, 0x04);
            WriteReg(inst, 0x530c, 0x06);
            return true;
        }

        case kLTImageSensorAttribute_Denoise: {
            int level = *(const int *)value;
            if (level < 0) level = 0;
            if (level > 8) level = 8;
            SetRegBits(inst, OV3660_ISP_5308, OV3660_DENOISE_EN, level > 0);
            if (level > 0)
                WriteReg(inst, OV3660_ISP_5306, (u8)((level - 1) * 4));
            return true;
        }

        case kLTImageSensorAttribute_Binning: {
            inst->binning = *(const bool *)value;
            ApplyFrameSize(inst);   /* re-derive and re-apply everything */
            return true;
        }

        case kLTImageSensorAttribute_SpecialEffect: {
            LTImageSensorSpecialEffect fx = *(const LTImageSensorSpecialEffect *)value;
            if (fx >= 7) return false;
            const u8 *r = s_specialEffectRegs[fx];
            WriteReg(inst, OV3660_SDE_5580, r[0]);
            WriteReg(inst, OV3660_SDE_5583, r[1]);
            WriteReg(inst, OV3660_SDE_5584, r[2]);
            WriteReg(inst, OV3660_SDE_5003, r[3]);
            return true;
        }

        case kLTImageSensorAttribute_WhiteBalanceMode: {
            LTImageSensorWBMode mode = *(const LTImageSensorWBMode *)value;
            if (mode > kLTImageSensorWBMode_Home) return false;
            WriteReg(inst, OV3660_AWB_MANUAL, mode != kLTImageSensorWBMode_Auto ? 1 : 0);
            if (mode != kLTImageSensorWBMode_Auto) {
                static const u16 kRGain[4] = {0x5e0,0x650,0x520,0x420};
                static const u16 kGGain[4] = {0x410,0x410,0x410,0x3f0};
                static const u16 kBGain[4] = {0x540,0x4f0,0x660,0x710};
                u8 idx = (u8)(mode - 1);
                WriteReg16(inst, OV3660_AWB_R_H, kRGain[idx]);
                WriteReg16(inst, OV3660_AWB_G_H, kGGain[idx]);
                WriteReg16(inst, OV3660_AWB_B_H, kBGain[idx]);
            }
            return true;
        }

        case kLTImageSensorAttribute_AeLevel: {
            /* OV3660 supports ±5 */
            int level = *(const int *)value;
            if (level < -5) level = -5;
            if (level > 5) level = 5;
            int target = ((level + 5) * 10) + 5;
            int lo = target * 23 / 25;
            int hi = target * 27 / 25;
            int fast_lo = lo >> 1;
            int fast_hi = hi << 1;
            if (fast_hi > 255) fast_hi = 255;
            WriteReg(inst, OV3660_AEC_AE_LEVEL_0F, (u8)hi);
            WriteReg(inst, OV3660_AEC_AE_LEVEL_10, (u8)lo);
            WriteReg(inst, OV3660_AEC_AE_LEVEL_1B, (u8)hi);
            WriteReg(inst, OV3660_AEC_AE_LEVEL_1E, (u8)lo);
            WriteReg(inst, OV3660_AEC_AE_LEVEL_11, (u8)fast_hi);
            WriteReg(inst, OV3660_AEC_AE_LEVEL_1F, (u8)fast_lo);
            return true;
        }

        case kLTImageSensorAttribute_AGain: {
            int gain = *(const int *)value;
            if (gain < 0) gain = 0;
            if (gain > 64) gain = 64;
            int gainv = gain << 4;
            if (gainv) gainv -= 1;
            WriteReg(inst, OV3660_AEC_GAIN_H, (u8)(gainv >> 8));
            WriteReg(inst, OV3660_AEC_GAIN_L, (u8)(gainv & 0xFF));
            return true;
        }

        case kLTImageSensorAttribute_Exposure: {
            /* Read VTS for clamping */
            int max_val = (int)ReadReg16(inst, 0x380e);
            int value_ = *(const int *)value;
            if (value_ > max_val) value_ = max_val;
            WriteReg(inst, OV3660_AEC_EXP_H, (u8)((value_ >> 12) & 0x0F));
            WriteReg(inst, OV3660_AEC_EXP_M, (u8)((value_ >> 4) & 0xFF));
            WriteReg(inst, OV3660_AEC_EXP_L, (u8)((value_ << 4) & 0xF0));
            return true;
        }

        case kLTImageSensorAttribute_GainCeiling: {
            /* OV3660 stores gainceiling as a raw 10-bit integer, not an enum.
               Map from LTImageSensorGainCeiling enum to approximate equivalent values. */
            static const u16 kCeilMap[7] = {2,4,8,16,32,64,128};
            LTImageSensorGainCeiling gc = *(const LTImageSensorGainCeiling *)value;
            if (gc >= 7) return false;
            u16 v = kCeilMap[gc];
            WriteReg(inst, OV3660_GAIN_CEIL_H, (u8)((v >> 8) & 0x3));
            WriteReg(inst, OV3660_GAIN_CEIL_L, (u8)(v & 0xFF));
            return true;
        }

        case kLTImageSensorAttribute_FlipVerticalHorizontal: {
            const LTImageSensorAttributeParams *p = value;
            if (p->inLen != 2) return false;
            const u8 *flips = (const u8 *)p->in;
            inst->vflip   = flips[0] != 0;
            inst->hmirror = flips[1] != 0;
            ApplyImageOptions(inst);
            return true;
        }

        case kLTImageSensorAttribute_AutoGainControl:
            SetRegBits(inst, OV3660_AEC_PK_MANUAL, OV3660_AEC_AGC_MANUAL,
                       !(*(const bool *)value));
            return true;

        case kLTImageSensorAttribute_AutoExposureControl:
            SetRegBits(inst, OV3660_AEC_PK_MANUAL, OV3660_AEC_AEC_MANUAL,
                       !(*(const bool *)value));
            return true;

        case kLTImageSensorAttribute_AutoWhiteBalance:
            SetRegBits(inst, OV3660_ISP_CONTROL_01, OV3660_ISP_AWB_EN,
                       *(const bool *)value);
            return true;

        case kLTImageSensorAttribute_AwbGain: {
            /* Toggling AWB gain re-applies or clears the manual WB mode */
            bool en = *(const bool *)value;
            u8 cur = ReadReg(inst, OV3660_AWB_MANUAL) & 0x01;
            WriteReg(inst, OV3660_AWB_MANUAL, en ? cur : 0);
            return true;
        }

        case kLTImageSensorAttribute_Aec2:
            SetRegBits(inst, OV3660_NIGHT_MODE_CTRL, 0x04, *(const bool *)value);
            return true;

        case kLTImageSensorAttribute_BadPixelCorrection:
            SetRegBits(inst, OV3660_ISP_5000, OV3660_ISP5000_BPC, *(const bool *)value);
            return true;

        case kLTImageSensorAttribute_WhitePixelCorrection:
            SetRegBits(inst, OV3660_ISP_5000, OV3660_ISP5000_WPC, *(const bool *)value);
            return true;

        case kLTImageSensorAttribute_RawGamma:
            SetRegBits(inst, OV3660_ISP_5000, OV3660_ISP5000_GAMMA, *(const bool *)value);
            return true;

        case kLTImageSensorAttribute_LensCorrection:
            SetRegBits(inst, OV3660_ISP_5000, OV3660_ISP5000_LENC, *(const bool *)value);
            return true;

        case kLTImageSensorAttribute_DownsizeCropWindow:
            SetRegBits(inst, OV3660_ISP_5183, 0x80, !(*(const bool *)value));
            return true;

        case kLTImageSensorAttribute_Colorbar:
            SetRegBits(inst, OV3660_PRE_ISP_TEST, OV3660_TEST_COLOR_BAR,
                       *(const bool *)value);
            return true;

        case kLTImageSensorAttribute_Register: {
            const LTImageSensorRegAccess *ra = value;
            u8 cur = ReadReg(inst, ra->reg);
            WriteReg(inst, ra->reg, (u8)((cur & ~(u8)ra->mask) | ((u8)ra->value & (u8)ra->mask)));
            return true;
        }

        default:
            return false;
    }
}

static bool LTDriverImageSensorOV3660Impl_GetAttribute(
        LTDeviceUnit hUnit, LTImageSensorAttribute attr, void *value) {
    OV3660_Instance *inst = InstanceFromHandle(hUnit);
    if (!inst) return false;

    switch (attr) {

        case kLTImageSensorAttribute_Id:
            *(u32 *)value = kOV3660_PidExpected;
            return true;

        case kLTImageSensorAttribute_PixelFormat:
            *(LTImageSensorPixelFormat *)value = inst->pixelFormat;
            return true;

        case kLTImageSensorAttribute_FrameSize:
            *(LTImageSensorFrameSize *)value = inst->frameSize;
            return true;

        case kLTImageSensorAttribute_FrameResolution: {
            if (inst->frameSize < (sizeof(s_frameSizeTable)/sizeof(s_frameSizeTable[0]))) {
                LTImageSensorFrameResolution *res = value;
                res->width  = s_frameSizeTable[inst->frameSize].w;
                res->height = s_frameSizeTable[inst->frameSize].h;
            }
            return true;
        }

        case kLTImageSensorAttribute_Binning:
            *(bool *)value = inst->binning;
            return true;

        case kLTImageSensorAttribute_Crop: {
            LTImageSensorCropCap *cap = value;
            cap->bounds.width  = kOV3660_OutputWidth;
            cap->bounds.height = kOV3660_OutputHeight;
            cap->bounds.left   = 0;
            cap->bounds.top    = 0;
            cap->defrect       = cap->bounds;
            cap->pixelaspect.numerator   = 1;
            cap->pixelaspect.denominator = 1;
            return true;
        }

        case kLTImageSensorAttribute_FlipVerticalHorizontal: {
            LTImageSensorAttributeParams *p = value;
            if (p->outLen < 2) return false;
            u8 *out = (u8 *)p->out;
            out[0] = inst->vflip   ? 1 : 0;
            out[1] = inst->hmirror ? 1 : 0;
            return true;
        }

        case kLTImageSensorAttribute_Exposure: {
            int ra = ReadReg(inst, OV3660_AEC_EXP_H);
            int rb = ReadReg(inst, OV3660_AEC_EXP_M);
            int rc = ReadReg(inst, OV3660_AEC_EXP_L);
            *(int *)value = (ra & 0x0F) << 12 | (rb & 0xFF) << 4 | (rc & 0xF0) >> 4;
            return true;
        }

        case kLTImageSensorAttribute_Status: {
            u16 pid = ReadReg16(inst, OV3660_REG_PID_H);
            LTLOG_SERVER("get.status", "PID=0x%04lx fmt=%lu fsz=%lu binning=%lu",
                LT_Pu32(pid), LT_Pu32(inst->pixelFormat),
                LT_Pu32(inst->frameSize), LT_Pu32(inst->binning));
            return true;
        }

        case kLTImageSensorAttribute_Register: {
            LTImageSensorRegAccess *ra = value;
            ra->value = ReadReg(inst, ra->reg) & (u8)ra->mask;
            return true;
        }

        default:
            return false;
    }
}

/* =========================================================================
 * Interface and library root binding
 * ========================================================================= */

define_LTLIBRARY_INTERFACE(ILTDriverImageSensor, OnDestroyHandle) {
    .OnImageSensorEvent = LTDriverImageSensorOV3660Impl_OnImageSensorEvent,
    .NoImageSensorEvent = LTDriverImageSensorOV3660Impl_NoImageSensorEvent,
    .PowerOn            = LTDriverImageSensorOV3660Impl_PowerOn,
    .PowerOff           = LTDriverImageSensorOV3660Impl_PowerOff,
    .SetAttribute       = LTDriverImageSensorOV3660Impl_SetAttribute,
    .GetAttribute       = LTDriverImageSensorOV3660Impl_GetAttribute,
} LTLIBRARY_DEFINITION;

LTLIBRARY_EXPORT_INTERFACES(LTDriverImageSensorOV3660, (ILTDriverImageSensor))

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  31-May-26   created  — derived from espressif/esp32-camera OV3660 driver
 */
