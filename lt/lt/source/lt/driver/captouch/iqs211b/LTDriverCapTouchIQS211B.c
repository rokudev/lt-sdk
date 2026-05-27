/*******************************************************************************
 * lt/source/lt/driver/captouch/LTDriverCapTouchIQS211B.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * Azoteq IQS211B Capacitive Touch Driver
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/device/gpio/LTDeviceGpio.h>
#include <lt/driver/captouch/LTDriverCapTouch.h>
#include <lt/system/settings/LTSystemSettings.h>

/*___________________________________
  LTDriverCapTouchIQS211B #defines */
DEFINE_LTLOG_SECTION("lt.drv.captouch.iqs211b");

#define USE_DLOG 0
#if USE_DLOG
    #define DLOG        LTLOG
    #define DLOG_YELLOW LTLOG_YELLOWALERT
    #define DLOG_RED    LTLOG_REDALERT
#else
    #define DLOG        LTLOG_LOGNULL
    #define DLOG_YELLOW LTLOG_LOGNULL
    #define DLOG_RED    LTLOG_LOGNULL
#endif


/* IQS211B I2C address */
#define IQS211B_ADDR                0x47

/* IQS211B Register addresses */
#define PROX_SETTINGS_0             0xC6
#define PROX_SETTINGS_1             0xC7
#define PROX_SETTINGS_2             0xC8
#define ATI_TARGET                  0xC9
#define LP_PERIOD                   0xCA
#define PROX_THRESHOLD              0xCB
#define TOUCH_THRESHOLD             0xCC
#define MOVEMENT_THRESHOLD          0xCD
#define AUTO_RESEED_LMT             0xCE

/* Configuration constants */
#define MAX_REINIT_ATTEMPTS         3
#define I2C_DATA_LENGTH             10
#define DEFAULT_MOVEMENT_THRESHOLD  0x07
#define AZOTEQ_REBOOT_PULSE_DURATION_MS 4

/* Power-on timing */
#define AZOTEQ_PWR_ON_TO_READY_TIME_NORMAL_MS  50
#define AZOTEQ_PWR_ON_TO_READY_TIME_LPM_MS     30
#define AZOTEQ_PWR_RST_PWR_OFF_TIME_MS         20

/* settings key */
#define MOVEMENT_THRESHOLD_SETTINGS_KEY "ltlib/LTDriverCapTouchIQS211B/movementThreshold"

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/*________________________________________
  LTDriverCapTouchIQS211B LTObject impl */
typedef_LTObjectImpl(LTDriverCapTouch, LTDriverCapTouchIQS211B) {
    LTTime                   pwrOnToReadyTime;
    LTTime                   lastInterruptTime;
    /* GPIO resources */
    LTDeviceGpio            *pGpio;
    s16                      i2cSclPin;
    s16                      i2cSdaPin;
    s16                      captEnPin;

    /* Thread and notification */
    LTThread                 hNotificationThread;
    ILTThread               *iThread;
    LTDriverCapTouch_MotionProc *pMotionProc;
    void                    *pMotionProcData;

    /* State */
    LTDeviceCapTouch_Mode    mode;
    LTDeviceCapTouch_Mode    desiredMode;
    s32                      resetAttemptsDone;
    bool                     doNotResuscitate;
    u8                       movementThreshold;
} LTOBJECT_API;

/*___________________________________________
  LTDriverCapTouchIQS211B static variables */
static LTDriverCapTouchIQS211B * s_capTouch;

/*******************************************************************************
 * Forward declarations
 ******************************************************************************/
static void AzotecIRQHandler(void *pClientData);
static void AzoteqI2CInit(LTDriverCapTouchIQS211B *capTouch);
static void AzoteqI2CInitTimer(void *pClientData);
static void AzoteqPower(LTDriverCapTouchIQS211B *capTouch, bool on);

/*******************************************************************************
 * I2C Bit-bang implementation
 ******************************************************************************/
static void I2CDelay(void) {
    for (volatile int i = 0; i < 20; i++) { }
    // LTTime untilTime = LTTime_Add(LT_GetCore()->GetKernelTime(), LTTime_Microseconds(29));
    // while (LTTime_IsLessThan(LT_GetCore()->GetKernelTime(), untilTime)) { }
}

static void SdaLow(LTDriverCapTouchIQS211B *capTouch) {
    capTouch->pGpio->API->SetOutputValue(capTouch->pGpio, capTouch->i2cSdaPin, 0);  // Pre-set value
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_Output);
    I2CDelay();
}

static void SdaHigh(LTDriverCapTouchIQS211B *capTouch) {
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_HighZ);
    I2CDelay();
}

static void SclLow(LTDriverCapTouchIQS211B *capTouch) {
    capTouch->pGpio->API->SetOutputValue(capTouch->pGpio, capTouch->i2cSclPin, 0);
    I2CDelay();
}

static void SclHigh(LTDriverCapTouchIQS211B *capTouch) {
    capTouch->pGpio->API->SetOutputValue(capTouch->pGpio, capTouch->i2cSclPin, 1);
    I2CDelay();
}

// Wait for slave clock stretching. The slave holds SCL low when not ready.
// When the slave is ready, it releases SCL allowing the pull-up to bring it high.
static bool SclHighWait(LTDriverCapTouchIQS211B *capTouch) {
    /* Wait for slave clock stretching with timeout */
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSclPin, kLTDeviceGpio_ModeType_Input);
    // ISSUE: performing a delay programmatically by decrementing a variable is processor dependent
    u32 timeout = 10000;
    while ((--timeout > 0) && (!capTouch->pGpio->API->GetInputValue(capTouch->pGpio, capTouch->i2cSclPin))) {
        /* Wait for slave to release SCL */
    }
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSclPin, kLTDeviceGpio_ModeType_Output);
    return (timeout > 0);
}

static bool SdaRead(LTDriverCapTouchIQS211B *capTouch) {
    return capTouch->pGpio->API->GetInputValue(capTouch->pGpio, capTouch->i2cSdaPin);
}

static void I2CSendBit(LTDriverCapTouchIQS211B *capTouch, bool bit) {
    SclLow(capTouch);
    if (bit) SdaHigh(capTouch); else SdaLow(capTouch);
    I2CDelay();
    SclHigh(capTouch);
    I2CDelay();
    SclLow(capTouch);
    I2CDelay();
}

// Read a single bit (ACK/NACK from slave)
// Uses clock stretching - waits for slave to release SCL
static bool I2CReadBit(LTDriverCapTouchIQS211B *capTouch) {
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_Input);
    SclHighWait(capTouch);
    bool bit = SdaRead(capTouch);
    SclLow(capTouch);
    I2CDelay();
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_HighZ);
    return bit;
}

static void I2CRepeatedStart(LTDriverCapTouchIQS211B *capTouch) {
    SclLow(capTouch);
    I2CDelay();
    SdaHigh(capTouch);
    I2CDelay();
    SclHigh(capTouch);
    I2CDelay();
    SdaLow(capTouch);
    I2CDelay();
}

/*******************************************************************************
 * Setup interrupt GPIO for touch events
 ******************************************************************************/
static bool SetupCaptouchInterruptGPIO(LTDriverCapTouchIQS211B *capTouch) {
    if (capTouch->pGpio && (!capTouch->doNotResuscitate)) {
        capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSclPin, kLTDeviceGpio_ModeType_Input);
        capTouch->pGpio->API->SetISR(capTouch->pGpio, capTouch->i2cSclPin, &AzotecIRQHandler, kLTDeviceGPIO_TriggerType_FallingEdge, NULL);
        return true;
    }
    return false;
}

/*******************************************************************************
 * Handle I2C communication failure
 ******************************************************************************/
static void AzoteqI2CHandleFailure(LTDriverCapTouchIQS211B *capTouch) {

    if (capTouch->resetAttemptsDone < MAX_REINIT_ATTEMPTS) {
        capTouch->resetAttemptsDone++;
        DLOG_YELLOW("i2c", "Attempt %ld/%d", LT_Ps32(capTouch->resetAttemptsDone), MAX_REINIT_ATTEMPTS);

        AzoteqPower(capTouch, false);
        capTouch->mode = kLTDeviceCapTouch_Mode_Unset;

        capTouch->iThread->Sleep(LTTime_Milliseconds(AZOTEQ_PWR_RST_PWR_OFF_TIME_MS));
        AzoteqPower(capTouch, true);

        capTouch->iThread->SetTimer(capTouch->hNotificationThread, capTouch->pwrOnToReadyTime, &AzoteqI2CInitTimer, NULL, capTouch);
    } else {
        if (capTouch->pGpio) {
            /* Disable the ISR so it doesn't trigger callbacks during reset */
            capTouch->pGpio->API->SetISR(capTouch->pGpio, capTouch->i2cSclPin, NULL, kLTDeviceGPIO_TriggerType_FallingEdge, NULL);
            capTouch->pGpio->API->ClearGPIOPendingIRQ(capTouch->pGpio, capTouch->i2cSclPin);
            capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSclPin, kLTDeviceGpio_ModeType_HighZ);
            capTouch->pGpio->API->SetGpioPullFromIndex(capTouch->pGpio, capTouch->i2cSclPin, kLTDeviceGpio_PullType_PullUp);
        }
        DLOG_RED("i2c.failed", "Attempts Maxed");
        // ISSUE: The LTLOG_SERVER below was RCU_LOG, but that can't happen
        //        is this product independent driver.  So RCUApp should register
        //        a log hook function with LTCore and catch logs with the kLTCore_LogFlags_LogToServer flag set
        //        and do your transmit thing
        // LTLOG_SERVER("i2c.failed", "");

        capTouch->doNotResuscitate = true;
        capTouch->resetAttemptsDone = 0;
        AzoteqPower(capTouch, false);
    }
}

/*******************************************************************************
 * Initialize Azoteq via I2C
 ******************************************************************************/
static void AzoteqI2CInitTimer(void *pClientData) {
    AzoteqI2CInit((LTDriverCapTouchIQS211B *)pClientData);
}

static void AzoteqI2CInit(LTDriverCapTouchIQS211B *capTouch) {
   capTouch->iThread->KillTimer(capTouch->hNotificationThread, &AzoteqI2CInitTimer, capTouch);

   if (capTouch->mode == capTouch->desiredMode) {
        return;
   }

    /* Configuration register values */
    const u8 PROX_DIR = 0x01 << 3;              /* Both directions */
    const u8 REDO_ATI = 0x00 << 4;
    const u8 DO_RESEED = 0x00 << 5;
    const u8 BASEVAL_COARSEMULT = 0x00 << 6;   /* 150 counts/0 */ // ISSUE 0 << 6 is 0; is this correct?

    // const u8 OP_UI_SEL = 0x03 << 0; //Standalone Touch, Prox
    const u8 OP_UI_SEL = 0x01 << 0;            /* Movement Latch & Movement */
    const u8 MOVEMENT_OP_TYPE = 0x00 << 3;     /* Normal (PFM) */
    const u8 HALT_CHARGE_RESEED_ON_IO1 = 0x00 << 5;
    const u8 HALT_CHARGE_RESEED = 0x00 << 6;
    const u8 AUTO_RESEED_VAL = 0x00 << 7;

    const u8 INCREASE_ACFILTER_TOUCHTHRESHOLD = 0x00 << 0;
    const u8 AUTO_ATI_OFF = 0x00 << 1;
    const u8 PARTIAL_ATI_ENABLE = 0x00 << 2;
    const u8 TOUCH_LATE_RLS = 0x00 << 3;
    // This may be useful if we want to use both Movement and Touch interrupts
    // const u8 TOUCH_LATE_RLS = 0x01 << 3;
    const u8 AUTO_ACTIVE_ON_START = 0x00 << 5;
    const u8 PROX_TIMEOUT = 0x00 << 7;

    const u8 TARGET_x8 = 0x60;             /* ATI_TARGET (0xC9) */
    const u8 SLEEP_TIME_x4 = 0x28;         /* LP_PERIOD (0xCA) */
    const u8 PROX_THRESHOLD_VAL = 0x05;    /* PROX_THRESHOLD (0xCB) */
    const u8 TOUCH_THRESHOLD_VAL = 0x05;   /* TOUCH_THRESHOLD (0xCC) */
    const u8 AUTO_RESEED_LMT_VAL = 0x01;   /* AUTO_RESEED_LMT (0xCE) */

    u8 i2cdatawrsrc[I2C_DATA_LENGTH];

    DLOG("i2c", "addr=%x", IQS211B_ADDR);

    i2cdatawrsrc[0] = PROX_SETTINGS_0;
    i2cdatawrsrc[1] = BASEVAL_COARSEMULT | DO_RESEED | REDO_ATI | PROX_DIR;
    i2cdatawrsrc[3] = PROX_TIMEOUT | AUTO_ACTIVE_ON_START | TOUCH_LATE_RLS | PARTIAL_ATI_ENABLE | AUTO_ATI_OFF | INCREASE_ACFILTER_TOUCHTHRESHOLD;
    i2cdatawrsrc[4] = TARGET_x8;
    i2cdatawrsrc[5] = SLEEP_TIME_x4;

    if (capTouch->desiredMode == kLTDeviceCapTouch_Mode_Normal) {
        i2cdatawrsrc[2] = AUTO_RESEED_VAL | HALT_CHARGE_RESEED | HALT_CHARGE_RESEED_ON_IO1 | MOVEMENT_OP_TYPE | OP_UI_SEL;
        i2cdatawrsrc[6] = PROX_THRESHOLD_VAL;
        i2cdatawrsrc[7] = TOUCH_THRESHOLD_VAL;
        i2cdatawrsrc[8] = capTouch->movementThreshold;
        i2cdatawrsrc[9] = AUTO_RESEED_LMT_VAL;
        DLOG("mvmt", "%d", (int)capTouch->movementThreshold);
    } else if (capTouch->desiredMode == kLTDeviceCapTouch_Mode_LowPower) {
        i2cdatawrsrc[2] = 0x23;  /* Touch / Prox mode */
        i2cdatawrsrc[6] = 0xEE;  /* Very high prox threshold */
        i2cdatawrsrc[7] = 0xEE;  /* Very high touch threshold */
        i2cdatawrsrc[8] = 0xEE;  /* Very high movement threshold */
        i2cdatawrsrc[9] = 0xFF;  /* Always halt with 0xCE = 0xFF */
    } else {
        DLOG_YELLOW("mode.err", NULL);
        return;
    }

    /* Setup I2C pins */
    // SCL stays in output mode (master always controls clock)
    // SDA uses open-drain: OUTPUT+0 to drive low, HighZ to release
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSclPin, kLTDeviceGpio_ModeType_Output);
    capTouch->pGpio->API->SetOutputValue(capTouch->pGpio, capTouch->i2cSclPin, 1);
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_HighZ);

    /* START: SDA goes low while SCL is high */
    SdaHigh(capTouch);
    SclHigh(capTouch);
    I2CDelay();
    SdaLow(capTouch);
    I2CDelay();

    /* WRITE PHASE */
    bool success = true;
    // if (capTouch->resetAttemptsDone == 0) success = false;  /* Force failure on first attempt for testing */

    /* Send address byte with WRITE bit */
    u8 addrByte = (IQS211B_ADDR << 1) | 0;
    for (int i = 7; i >= 0; i--) {
        I2CSendBit(capTouch, (addrByte >> i) & 1);
    }
    success &= I2CReadBit(capTouch);  /* ACK from slave */

    /* Send 10 bytes of config data */
    for (int b = 0; b < I2C_DATA_LENGTH; b++) {
        u8 byte = i2cdatawrsrc[b];
        for (int i = 7; i >= 0; i--) {
            I2CSendBit(capTouch, (byte >> i) & 1);
        }
        success &= I2CReadBit(capTouch);  /* ACK from slave */
    }

    I2CRepeatedStart(capTouch);

    /* REGISTER ADDRESS PHASE */
    u8 regAddr = PROX_SETTINGS_1;
    u8 readData = 0;

    /* Send address byte with WRITE bit (for register address) */
    addrByte = (IQS211B_ADDR << 1) | 0;
    for (int i = 7; i >= 0; i--) {
        I2CSendBit(capTouch, (addrByte >> i) & 1);
    }
    success &= I2CReadBit(capTouch);

    /* Send register address */
    for (int i = 7; i >= 0; i--) {
        I2CSendBit(capTouch, (regAddr >> i) & 1);
    }
    success &= I2CReadBit(capTouch);

    I2CRepeatedStart(capTouch);

    /* READ PHASE */
    /* Send address byte with READ bit */
    addrByte = (IQS211B_ADDR << 1) | 1;
    for (int i = 7; i >= 0; i--) {
        I2CSendBit(capTouch, (addrByte >> i) & 1);
    }
    success &= I2CReadBit(capTouch);

    /* Switch SDA to INPUT mode for reading */
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_Input);

    /* Wait for slave clock stretching */
    SclHighWait(capTouch);

    /* Read data byte */
    readData = 0;
    for (int i = 7; i >= 0; i--) {
        if (SdaRead(capTouch)) {
            readData |= (1 << i);
        }
        SclLow(capTouch);
        I2CDelay();
        SclHigh(capTouch);
        I2CDelay();
        I2CDelay();
    }
    SclLow(capTouch);

    /* Verify read data matches what we wrote */
    if (readData != i2cdatawrsrc[2]) {
        success &= false;
    }

    /* Switch back to HighZ to send ACK */
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_HighZ);

    /* ACK after read */
    I2CSendBit(capTouch, 0);

    // Switch back to HighZ to send NACK
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_HighZ);

    /* Send NACK (master doesn't pull SDA low) */
    I2CSendBit(capTouch, 1);

    /* STOP: SDA goes high while SCL is high */
    SclLow(capTouch);
    SdaLow(capTouch);
    I2CDelay();
    SclHigh(capTouch);
    I2CDelay();
    SdaHigh(capTouch);
    I2CDelay();

    /* Restore pins to input mode */
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_Input);
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSclPin, kLTDeviceGpio_ModeType_Input);

    if (success) {
        DLOG("i2c.init.ok", "C7=0x%x", readData);
        capTouch->resetAttemptsDone = 0;
        SetupCaptouchInterruptGPIO(capTouch);
        capTouch->mode = capTouch->desiredMode;
    } else {
        DLOG_YELLOW("i2c.read.fail", "C7=0x%x expected=0x%x", readData, i2cdatawrsrc[2]);
        AzoteqI2CHandleFailure(capTouch);
    }
}

/*******************************************************************************
 * Reboot and reinitialize Azoteq
 ******************************************************************************/
static void AzoteqRebootReinit(void *clientData) {
    LTDriverCapTouchIQS211B *capTouch = (LTDriverCapTouchIQS211B *)clientData;

    if (capTouch->pGpio) {
        /* Disable the ISR so it doesn't trigger callbacks during reset */
        capTouch->pGpio->API->SetISR(capTouch->pGpio, capTouch->i2cSclPin, NULL, kLTDeviceGPIO_TriggerType_FallingEdge, NULL);
        capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_Input);
    }
    AzoteqPower(capTouch, false);
    capTouch->mode = kLTDeviceCapTouch_Mode_Unset;

    capTouch->iThread->Sleep(LTTime_Milliseconds(AZOTEQ_PWR_RST_PWR_OFF_TIME_MS));
    AzoteqPower(capTouch, true);
    capTouch->iThread->Sleep(capTouch->pwrOnToReadyTime);
    AzoteqI2CInit(capTouch);
}

/*******************************************************************************
 * GPIO interrupt callback for touch events
 ******************************************************************************/
static void AzotechCallI2CInitViaISRTaskProc(void *pClientData) {
    LT_UNUSED(pClientData);
    if (s_capTouch) {
        LTTime powerOnReadyTime = LTTime_Add(s_capTouch->lastInterruptTime, s_capTouch->pwrOnToReadyTime);
        LTTime kernelTime = LT_GetCore()->GetKernelTime();
        if (LTTime_IsLessThan(kernelTime, powerOnReadyTime)) {
            powerOnReadyTime = LTTime_Subtract(powerOnReadyTime, kernelTime);
            s_capTouch->iThread->SetTimer(s_capTouch->hNotificationThread, powerOnReadyTime, &AzoteqI2CInitTimer, NULL, s_capTouch);
        }
        else {
            AzoteqI2CInit(s_capTouch);
        }
    }
}

static void AzotecIRQHandler(void *pClientData) {
    LT_UNUSED(pClientData);
    if (! s_capTouch) return;

    /* Clear the interrupt flag */
    s_capTouch->pGpio->API->ClearGPIOPendingIRQ(s_capTouch->pGpio, s_capTouch->i2cSclPin);

    /* Time difference since last pulse to determine if this is a touch or reset event */
    LTTime currentInterruptTime = LT_GetCore()->GetKernelTime();
    LTTime intTimeDiff = LTTime_Subtract(currentInterruptTime, s_capTouch->lastInterruptTime);

    /* If it is a reset event, reinitialize the Azoteq */
    if (LTTime_IsLessThan(intTimeDiff, LTTime_Milliseconds(AZOTEQ_REBOOT_PULSE_DURATION_MS + 1))) {
        DLOG_YELLOW("azoteq", "Unexpected reboot");
        /* Turn off the interrupts */
        s_capTouch->pGpio->API->SetISR(s_capTouch->pGpio, s_capTouch->i2cSclPin, NULL, kLTDeviceGPIO_TriggerType_FallingEdge, NULL);
        /* Make sure Azoteq is powered on */
        s_capTouch->mode = kLTDeviceCapTouch_Mode_Unset;
        AzoteqPower(s_capTouch, true);
        /* Set a timer to reinitialize the Azoteq */
        s_capTouch->lastInterruptTime = LT_GetCore()->GetKernelTime();
        s_capTouch->iThread->QueueTaskProcIfRequired(s_capTouch->hNotificationThread, &AzotechCallI2CInitViaISRTaskProc, NULL, NULL);
    } else {
        /* This is a touch event. Call the motion proc directly */
        if (s_capTouch->pMotionProc) s_capTouch->pMotionProc(s_capTouch->pMotionProcData);
        s_capTouch->lastInterruptTime = currentInterruptTime;
    }
}

/*******************************************************************************
 * Power control for Azoteq
 ******************************************************************************/
static void AzoteqPower(LTDriverCapTouchIQS211B *capTouch, bool on) {
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->captEnPin, kLTDeviceGpio_ModeType_Output);
    if (on) {
        capTouch->pGpio->API->SetOutputValue(capTouch->pGpio, capTouch->captEnPin, 0);
    } else {
        capTouch->pGpio->API->SetOutputValue(capTouch->pGpio, capTouch->captEnPin, 1);
        capTouch->mode = kLTDeviceCapTouch_Mode_Unset;
    }
}

/*_______________________________________
  LTDriverCapTouchIQS211B constructors */
static bool LTDriverCapTouchIQS211B_ConstructObject(LTDriverCapTouchIQS211B *capTouch) {
    capTouch->mode = kLTDeviceCapTouch_Mode_Unset;
    capTouch->desiredMode = kLTDeviceCapTouch_Mode_Unset;
    capTouch->lastInterruptTime = LTTime_Zero();

    /* Get thread interface */
    capTouch->iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    /* Open GPIO device */
    capTouch->pGpio = lt_createdeviceobject(LTDeviceGpio);
    if (capTouch->pGpio == NULL) {
        DLOG_RED("init", "gpio");
        return false;
    }

    /* Get pin assignments */
    capTouch->i2cSclPin = capTouch->pGpio->API->GetNamedPinValueFromName(capTouch->pGpio, "capt_i2c_scl");
    capTouch->i2cSdaPin = capTouch->pGpio->API->GetNamedPinValueFromName(capTouch->pGpio, "capt_i2c_sda");
    capTouch->captEnPin = capTouch->pGpio->API->GetNamedPinValueFromName(capTouch->pGpio, "capt_en");

    if (capTouch->i2cSclPin < 0 || capTouch->i2cSdaPin < 0 || capTouch->captEnPin < 0) {
        DLOG_YELLOW("init.pins", "Missing GPIO pin configuration");
        lt_destroyobject(capTouch->pGpio);
        return false;
    }

    capTouch->movementThreshold = DEFAULT_MOVEMENT_THRESHOLD;
    LTSystemSettings *settings = lt_openlibrary(LTSystemSettings);
    if (settings) {
        s64 val = 0;
        if (settings->GetIntegerValue(MOVEMENT_THRESHOLD_SETTINGS_KEY, &val)) {
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            capTouch->movementThreshold = (u8)val;
        }
        else {
            LTString string = NULL;
            if (settings->GetStringValue(MOVEMENT_THRESHOLD_SETTINGS_KEY, &string)) {
                u32 thresh = lt_strtou32(string, NULL, 10);
                if (thresh > 255) thresh = 255;
                capTouch->movementThreshold = (u8)thresh;
                lt_free(string);
            }
        }
        lt_closelibrary(settings);
    }

    // lt_consolestomp("SCL=%d SDA=%d EN=%d\n", capTouch->i2cSclPin, capTouch->i2cSdaPin, capTouch->captEnPin);
    s_capTouch = capTouch;
    AzoteqPower(capTouch, false);
    return true;
}

static void LTDriverCapTouchIQS211B_DestructObject(LTDriverCapTouchIQS211B *capTouch) {
    /* Disable interrupts */
    s_capTouch = NULL;
    if (capTouch->pGpio) {
        capTouch->pGpio->API->SetISR(capTouch->pGpio, capTouch->i2cSclPin, NULL, kLTDeviceGPIO_TriggerType_FallingEdge, NULL);
    }

    /* Power off the device */
    AzoteqPower(capTouch, false);

    /* destroy GPIO device */
    lt_destroyobject(capTouch->pGpio);
}

/*_______________________________
  LTDriverCapTouch API functions */
static bool LTDriverCapTouchIQS211B_Initialize(LTDriverCapTouchIQS211B *capTouch, LTDeviceCapTouch_Mode mode, LTThread hThread, LTDriverCapTouch_MotionProc *pMotionProc, void *pMotionProcData) {

    capTouch->desiredMode = mode;
    capTouch->resetAttemptsDone = 0;
    capTouch->hNotificationThread = hThread;
    capTouch->pMotionProc = pMotionProc;
    capTouch->pMotionProcData = pMotionProcData;

    capTouch->pwrOnToReadyTime = LTTime_Milliseconds((mode == kLTDeviceCapTouch_Mode_LowPower) ? AZOTEQ_PWR_ON_TO_READY_TIME_LPM_MS : AZOTEQ_PWR_ON_TO_READY_TIME_NORMAL_MS);

    AzoteqPower(capTouch, true);
    capTouch->iThread->QueueTaskProc(capTouch->hNotificationThread, AzoteqRebootReinit, NULL, capTouch);

    return true;
}

static LTDeviceCapTouch_Mode LTDriverCapTouchIQS211B_GetMode(LTDriverCapTouchIQS211B *capTouch) {
    return capTouch->mode;
}

static bool LTDriverCapTouchIQS211B_IsCapTouchTriggerActive(LTDriverCapTouchIQS211B *capTouch) {
    if (capTouch->pGpio && (capTouch->i2cSclPin != -1)) {
        capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSclPin, kLTDeviceGpio_ModeType_Input);
        return (capTouch->pGpio->API->GetInputValue(capTouch->pGpio, capTouch->i2cSclPin) == 0); // Active low
    }
    return false;
}

static void LTDriverCapTouchIQS211B_Enable(LTDriverCapTouchIQS211B *capTouch, bool bEnable) {
    if (bEnable) {
        AzoteqPower(capTouch, true);
        if (capTouch->mode == kLTDeviceCapTouch_Mode_Unset && capTouch->desiredMode != kLTDeviceCapTouch_Mode_Unset) {
            /* Re-initialize if needed */
            if (capTouch->hNotificationThread) {
                if (capTouch->iThread->GetCurrentThread() == capTouch->hNotificationThread) AzoteqRebootReinit(capTouch);
                else capTouch->iThread->QueueTaskProc(capTouch->hNotificationThread, AzoteqRebootReinit, NULL, capTouch);
            }
        } else {
            SetupCaptouchInterruptGPIO(capTouch);  // Re-enable interrupt
        }
    } else {
        AzoteqPower(capTouch, false);
    }
}

static bool LTDriverCapTouchIQS211B_ResetTest(LTDriverCapTouchIQS211B *capTouch) {
    // This test should work even when doNotResuscitate is set.
    AzoteqPower(capTouch, false);
    capTouch->iThread->Sleep(LTTime_Milliseconds(100));
    capTouch->pGpio->API->SetGpioModeFromIndex(capTouch->pGpio, capTouch->i2cSdaPin, kLTDeviceGpio_ModeType_Input);
    bool sdaState = capTouch->pGpio->API->GetInputValue(capTouch->pGpio, capTouch->i2cSdaPin);
    // At the factory, the previous command is `touch check`. This will leave SDA high.
    // On power off, SDA should go low. This confirms power-off is working.
    return (sdaState == 0);
}

/*_____________________________
  LTDriverCapTouch api binding */
define_LTObjectImplPublic(LTDriverCapTouch, LTDriverCapTouchIQS211B,
    Initialize,
    Enable,
    IsCapTouchTriggerActive,
    GetMode,
    ResetTest
);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  30-Jan-26   macrinus    created
 *  27-Mar-26   augustus    recreated as object based
 */
