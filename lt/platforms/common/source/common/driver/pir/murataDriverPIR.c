/*******************************************************************************
 * <platforms/common/source/common/driver/pir/murataDriverPIR.c>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 * IRS-D200ST00R1 Murata PIR Driver
 ******************************************************************************/
/** @file murataDriverPIR.c Implementation of PIR Sernsor driver */

#include <lt/core/LTCore.h>
#include <lt/core/LTThread.h>
#include <lt/core/LTTime.h>
#include <lt/device/i2c/LTDeviceI2C.h>
#include <lt/device/pins/LTDevicePins.h>
#include <lt/device/pir/LTDevicePIRDefs.h>
#include <lt/device/pir/LTDevicePIR.h>
#include <lt/system/settings/LTSystemSettings.h>

DEFINE_LTLOG_SECTION("lt.drv.MurataPIR");

/******************************************************************************
 * consts
******************************************************************************/

static const char * s_DeviceName = "D200ST00R1";
static const LTTime s_DefaultMotionDelay = LTTimeInitializer_Seconds(5);
static u8 s_PIRSensitivityPercent = 50; /* Default corresponds to "medium" */
static const s64 s_PIRSensitivityMaxWindowTime = 0x03FF; /* Max Window time corresponds to "10.23 seconds @100hz ODR" */

#define LOG_MOTION_EVENTS 1
#ifdef LOG_MOTION_EVENTS
static const LTTime kMotionLogInterval = LTTimeInitializer_Seconds(3600);
#endif

typedef enum {          /**<  PIR Sensitivity Threshold Values */
    /* NB: A smaller threshold value means a higher sensitivity. So here the word "low" means highest threshold value,
     * but lowest actual sensitivity.
     * These values have been suggested by Murata in their DVT1 evaluation. */
    kPIRSensitivityThreshold_Low    = 0x08, /* ±1024 (8 * 128) */
    kPIRSensitivityThreshold_Medium = 0x07, /* ±896 (7 * 128) */
    kPIRSensitivityThreshold_High   = 0x05, /* ±640 (5 * 128) */
} PIRSensitivityThreshold;

enum {
    kMURATA_I2C_ADDR           = 0x48
};

typedef enum {        /**< commands supported by D200ST00R1 */
    kMurataRegisters_Opt           = 0x00, /**<  Operation setting */
    kMurataRegisters_DataL         = 0x02, /**<  Measurement data of the sensor L */
    kMurataRegisters_DataH         = 0x03, /**<  Measurement data of the sensor H */
    kMurataRegisters_Status        = 0x04, /**<  Interrupt status */
    kMurataRegisters_Overcount     = 0x05, /**<  Check and clear the Over count */
    kMurataRegisters_Samplr        = 0x06, /**<  Select the ODR(Output Data Rate) */
    kMurataRegisters_Pfr           = 0x07, /**<  Select the HPF(High-Pass Filter) and LPF(Low-Pass Filter) */
    kMurataRegisters_Intsel        = 0x09, /**<  Interrupt setting */
    kMurataRegisters_ThreshCount   = 0x0A, /**<  Over count setting */
    kMurataRegisters_ThreshHr      = 0x0B, /**<  Upper threshold value */
    kMurataRegisters_ThreshLr      = 0x0C, /**<  Lower threshold value */
    kMurataRegisters_TimerLr       = 0x0D, /**<  LSB*1 of Window Time */
    kMurataRegisters_TimerHr       = 0x0E, /**<  MSB*1 of Window Time */
} MurataRegisters;

typedef enum {               /**<  Operational modes */
    kPirMode_Active = 0x00,
    kPirMode_Sleep  = 0x01,
} PirMode;

typedef enum {                /**<  ODR(Output Data Rate) */
	kPirOdr_50HZ  = 0x00,
	kPirOdr_100HZ = 0x01,
} PirOdr;

typedef enum {               /**<  High-Pass Filter and Low-Pass Filter */
	kPirHfp_03HZ = 0x00,
	kPirHfp_05HZ = 0x01,
	kPirLpf_10HZ = 0x00 << 1,
	kPirLpf_7HZ  = 0x01 << 1,
} PirPfr;

typedef enum {               /**<  Interrupt sources */
    kInterruptSource_DisableInterrupt 		= 0x00,
    kInterruptSource_DataUpdateInterrupt 	= 0x01,
    kInterruptSource_TimerInterrupt 	    = 0x02,
    kInterruptSource_AndModeInterrupt 	    = 0x04,
    kInterruptSource_OrModeInterrupt       	= 0x08,
    kInterruptSource_AllSourcesInterrupt 	= 0x0F,
} InterruptSource;

typedef enum {       /**<  Count modes */
	kThresholdCountNone = 0x00,
	kThresholdCount1    = 0x01,
	kThresholdCount2    = 0x02,
	kThresholdCount3    = 0x03,
	kThresholdCount4    = 0x04,
	kThresholdCount5    = 0x05,
	kThresholdCount6    = 0x06,
	kThresholdCount7    = 0x07,
} ThresholdCount;

/*******************************************************************************
 * static variables
*******************************************************************************/

static struct {
    struct {
        LTTime       tmMotionDelay;
        u16          nMotionSensitivity;
    } cfg;

     struct {
        u32          controlBusIndex;
        u16          i2cAddress;
        LTDeviceI2C *libI2C;
        LTDeviceUnit hI2cBus;
    } i2c;

    struct {
        LTDevicePins                    *pDevPins;
        LTDeviceUnit                     irqPin;
        ILTDriverPins_BidirectionalBank *iPinBank;
    } pins;

    struct {
        u8                     driverId;
        ILTThread             *thread;
        LTThread               notificationThread;
        LTThread_TaskProc     *pMotionTaskProc;
    } proc;
    LTSystemSettings *systemSettings;
    #ifdef LOG_MOTION_EVENTS
    struct {
        LTTime driverInitTime;
        u32 PIRMotionEventCountTotal;
        u32 PIRMotionEventCountLastInterval;
        u32 PIRMotionEventIntervalCount;
    } motionEventStats;
    #endif
} S;

/*******************************************************************************
 * I2C functions
*******************************************************************************/

static bool WriteRegister(u8 registerAddress, u8 value) {
    u8 cmd[2] = {
        registerAddress,
        value,
    };

    return S.i2c.libI2C->I2CMasterTransfer(S.i2c.hI2cBus, S.i2c.i2cAddress,
        NULL, 0,
        cmd, sizeof(cmd),
        true, true,
        NULL, NULL);
}

static bool ReadRegister(u8 registerAddress, u8 *value) {
    return S.i2c.libI2C->I2CMasterTransfer(S.i2c.hI2cBus, S.i2c.i2cAddress,
        value, 1,
        &registerAddress, sizeof(registerAddress),
        true, true,
        NULL, NULL);
}

/*******************************************************************************
 * ISR related functions
*******************************************************************************/

static void PIRISR(bool bPinHigh, void * pClientData);
static void PIRTaskProc(void * pClientData);

static void EnableIRQ(void) {
    S.pins.iPinBank->EnableIRQ(S.pins.irqPin, kLTDevicePin_PinConfiguration_Trigger_HighLevel, LTTime_Milliseconds(0), PIRISR, NULL);
}

static void DisableIRQ(void) {
    S.pins.iPinBank->DisableIRQ(S.pins.irqPin);
}

static void clearInterrupt(void) {
    // set window detection time (starts from first event, until either window expires, or events threshold is met)
    // based on 100hz ODR, target value of 7.67 seconds window
    WriteRegister(kMurataRegisters_TimerLr, 0xFF);
    WriteRegister(kMurataRegisters_TimerHr, 0x02);

    // read overcount to clear it
    u8 count = 0;
    ReadRegister(kMurataRegisters_Overcount, &count);

    // write 1 to clear status bit
    WriteRegister(kMurataRegisters_Status, kInterruptSource_AndModeInterrupt);
}

LT_ISR_SAFE void PIRISR(bool bPinHigh, void * pClientData) {
    LT_UNUSED(pClientData);

    DisableIRQ();

    // StopMotionDetection() called before Disable().
    // We cannot clear the interrupt source without the thread, so events will not work until re-enabled.
    if (!(S.proc.notificationThread)) return;

    S.proc.thread->QueueTaskProcIfRequired(S.proc.notificationThread, PIRTaskProc, NULL, (void *)bPinHigh);
}

static void PIRTaskProc(void * pClientData) {
    bool bPinHigh = (bool)pClientData;

    if (!bPinHigh) {
        // If the pin is low, it means that the interrupt was spurious.
        // We still have to clear the interrupt and re-arm the IRQ.
        LTLOG("isr.low", "PIR interrupt spurious, pin is low");
    } else {
        // Try to confirm the interrupt by checking the status register.
        // If reading the register failed, proceed on the assumption that the interrupt was spurious.
        // We still have to clear it and re-arm the IRQ, so do not return early.
        u8 status = 0;
        ReadRegister(kMurataRegisters_Status, &status);
        if (status & kInterruptSource_AndModeInterrupt) {
            // Emit motion start event as soon as we confirm the interrupt, so we can start handling it.
            LTDevicePIRMotionEvent event = { S.proc.driverId, 0, true, 0 };
            u32 u32Event = *(u32 *)(&event);
            S.proc.pMotionTaskProc((void *)u32Event);
            #ifdef LOG_MOTION_EVENTS
            ++S.motionEventStats.PIRMotionEventCountTotal;
            ++S.motionEventStats.PIRMotionEventCountLastInterval;
            #endif
        } else {
            // If the status is not set, it means that the interrupt was spurious.
            LTLOG("isr.sp", "PIR interrupt spurious, status is not set");
        }
    }

    // Clear the interrupt even if it was spurious, as per Murata spec.
    clearInterrupt();

    // Re-enable the IRQ for the next interrupt.
    EnableIRQ();
}

#ifdef LOG_MOTION_EVENTS
static void LogMotionEvents(void *pClientData) { LT_UNUSED(pClientData);
    ++S.motionEventStats.PIRMotionEventIntervalCount;
    s64 minutesElapsed = LTTime_GetSeconds(LTTime_Subtract(LT_GetCore()->GetKernelTime(), S.motionEventStats.driverInitTime)) / 60;
    LTLOG_SERVER("motion.stats", "seq: %lu total: %lu elapsed: %lldh %lldm mean per interval: %lld last interval: %lu",
                                 LT_Pu32(S.motionEventStats.PIRMotionEventIntervalCount),
                                 LT_Pu32(S.motionEventStats.PIRMotionEventCountTotal),
                                 LT_Ps64(minutesElapsed / 60), LT_Ps64(minutesElapsed % 60),
                                 LT_Ps64(S.motionEventStats.PIRMotionEventCountTotal / S.motionEventStats.PIRMotionEventIntervalCount),
                                 LT_Pu32(S.motionEventStats.PIRMotionEventCountLastInterval));
    S.motionEventStats.PIRMotionEventCountLastInterval = 0;
}
#endif

static void setupInt(void) {
    WriteRegister(kMurataRegisters_Intsel, kInterruptSource_AndModeInterrupt);
    clearInterrupt();
    S.pins.iPinBank->ConfigureAsWakeupSource(S.pins.irqPin, true, true, kLTDevicePower_WakeupReason_MotionEvent);
    EnableIRQ();
}

static void cleanupInt(void) {
    DisableIRQ();
    S.pins.iPinBank->ConfigureAsWakeupSource(S.pins.irqPin, false, true, kLTDevicePower_WakeupReason_Unknown);
    WriteRegister(kMurataRegisters_Intsel, kInterruptSource_DisableInterrupt);
    // write 1 to clear status bit
    WriteRegister(kMurataRegisters_Status, kInterruptSource_AndModeInterrupt);
}

/*******************************************************************************
 * Init Sensor function
*******************************************************************************/

static bool MurataDriverPIRImpl_SetSensitivity(u32 nDeviceIndex, u8 nPercent);
static bool InitSensor(void) {
    S.cfg.tmMotionDelay       = s_DefaultMotionDelay;
    S.cfg.nMotionSensitivity  = s_PIRSensitivityPercent;

    if (!WriteRegister(kMurataRegisters_Opt, kPirMode_Active)) {
        LTLOG_YELLOWALERT("mode.write", "failed to write OPT");
        return false;
    }

    if (!WriteRegister(kMurataRegisters_Samplr, kPirOdr_100HZ)) {
        LTLOG_YELLOWALERT("init.odr", "failed to set ODR");
        return false;
    }
    // Setting the ODR register incurs a 3 second settling time according to the spec sheet.
    // It doesn't specify what needs to settle, and I2C operations work just fine during this time.
    // So proceed with the rest of the init without waiting.

    if (!WriteRegister(kMurataRegisters_Pfr, kPirLpf_10HZ | kPirHfp_03HZ)) {
        LTLOG_YELLOWALERT("init.pfr", "failed to set PFR");
        return false;
    }

    if (!MurataDriverPIRImpl_SetSensitivity(0, s_PIRSensitivityPercent)) {
        LTLOG_YELLOWALERT("init.sens", "failed to set sensitivity");
        return false;
    }

    LTLOG("init.success", "PIR sensor initialized");
    return true;
}

static bool DeinitSensor(void) {
    if (!S.i2c.libI2C || !S.i2c.hI2cBus) {
        return false;
    }
    LTLOG("deinit", "Shutting down PIR sensor");
    if (!WriteRegister(kMurataRegisters_Opt, kPirMode_Sleep)) {
        LTLOG("deinit.write", "failed to write OPT");
        return false;
    }
    return true;
}

/*******************************************************************************
 *
 * driver interface implementation
 *
*******************************************************************************/

/*******************************************************************************
 * driver lib fini
 *******************************************************************************/

static void MurataDriverPIRImpl_LibFini(void) {
    DeinitSensor();

    S.proc.notificationThread        = 0;
    S.proc.pMotionTaskProc           = NULL;
    S.cfg.nMotionSensitivity         = s_PIRSensitivityPercent;

    lt_destroyhandle(S.pins.irqPin);
    lt_destroyhandle(S.i2c.hI2cBus);
    lt_closelibrary(S.pins.pDevPins);
    lt_closelibrary(S.i2c.libI2C);
    lt_closelibrary(S.systemSettings);
    S.pins.irqPin = LTHANDLE_INVALID;
    S.pins.pDevPins = LTHANDLE_INVALID;
    S.i2c.hI2cBus = LTHANDLE_INVALID;
}

/*******************************************************************************
 * driver lib init
 *******************************************************************************/

static bool MurataDriverPIRImpl_LibInit(void) {
    S.i2c.i2cAddress = kMURATA_I2C_ADDR;
    const char controlBusName[] = "I2C0";

    do {
        if (!(S.proc.thread  = lt_getlibraryinterface(ILTThread, LT_GetCore())) ||
            !(S.i2c.libI2C   = lt_openlibrary(LTDeviceI2C)) ||
            !(S.pins.pDevPins = lt_openlibrary(LTDevicePins))) {
            LTLOG_YELLOWALERT("init.libs", "failed to get resources");
            break;
        }

        S.systemSettings = lt_openlibrary(LTSystemSettings);
        if (!S.systemSettings) {
            LTLOG_YELLOWALERT("init.sysset", "Could not open LTSystemSettings");
            break;
        }

        S.i2c.controlBusIndex = S.i2c.libI2C->GetBusIndexFromName(controlBusName);
        S.i2c.hI2cBus = S.i2c.libI2C->CreateDeviceUnitHandle(S.i2c.controlBusIndex);
        if (!S.i2c.hI2cBus) {
            LTLOG_YELLOWALERT("init.i2c", "failed to init i2c");
            break;
        }

        u32 nDeviceUnitIndex = 0;
        if (!S.pins.pDevPins->GetUnitNumberFromBankName("PIR Interrupt", &nDeviceUnitIndex)) break;
        S.pins.irqPin = S.pins.pDevPins->CreateDeviceUnitHandle(nDeviceUnitIndex);
        if (!S.pins.irqPin) break;
        S.pins.iPinBank = lt_gethandleinterface(ILTDriverPins_BidirectionalBank, S.pins.irqPin);
        if (!S.pins.iPinBank) {
            LTLOG_YELLOWALERT("init.pins", "failed to init irq");
            break;
        }

        S.pins.iPinBank->ConfigureAsInput(S.pins.irqPin, kLTDevicePin_PinConfiguration_PullType_NoPull);

        if (!InitSensor()) {
            break;
        }
        #ifdef LOG_MOTION_EVENTS
        S.motionEventStats.driverInitTime = LT_GetCore()->GetKernelTime();
        #endif
        return true;
    } while (0);

    MurataDriverPIRImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Sets the sensitivity for movement detection
 *******************************************************************************/

static bool MurataDriverPIRImpl_SetSensitivity(u32 nDeviceIndex, u8 nPercent) {
    LT_UNUSED(nDeviceIndex);
    if (nPercent > 100) { nPercent = 100; }
    s_PIRSensitivityPercent = nPercent;

    /* Convert percentage to internal threshold value */
    u32 thresholdValue;
    if (nPercent > 75) { thresholdValue = kPIRSensitivityThreshold_High; } /* 76-100 is "high" */
    else if (nPercent > 25) { thresholdValue = kPIRSensitivityThreshold_Medium; } /* 26-75 is "medium" */
    else { thresholdValue = kPIRSensitivityThreshold_Low; } /* 0-25 is "low" */

    do {
        if (!WriteRegister(kMurataRegisters_ThreshCount, kThresholdCount1)) break;
        if (!WriteRegister(kMurataRegisters_ThreshHr, thresholdValue)) break;
        if (!WriteRegister(kMurataRegisters_ThreshLr, thresholdValue)) break;

        LTLOG_DEBUG("set.sense", "Set PIR sensitivity to %lu (threshold 0x%02lx).", LT_Pu32(nPercent), LT_Pu32(thresholdValue));

        s64 valueToSet;
        if (S.systemSettings->GetIntegerValue("PIR/ThreshCount", &valueToSet)) {
            if (!WriteRegister(kMurataRegisters_ThreshCount, valueToSet) && (valueToSet <= kThresholdCount7) && (valueToSet >= kThresholdCount1))
                LTLOG("set.sense.err", "Failed to set PIR ThreshCount from settings using defaults");
        }

        if (S.systemSettings->GetIntegerValue("PIR/WindowTime", &valueToSet)) {
            // window size is set by 10's of ms based on 100hz ODR, max value is 0x03FF = 10.23 seconds
            if ((valueToSet <= s_PIRSensitivityMaxWindowTime) && (valueToSet >= 0)) {
                    if (!WriteRegister(kMurataRegisters_TimerLr, valueToSet & 0xFF)) break;
                    if (!WriteRegister(kMurataRegisters_TimerHr, (valueToSet >> 8) & 0xFF)) break;
            } else {
                LTLOG("set.sense.err", "Failed to set PIR WindowTime from settings using defaults");
            }
        }

        return true;
    } while (0);

    LTLOG("sense.err", "Failed to set PIR sensitivity");
    return false;
}

/*******************************************************************************
 * Gets the sensitivity for movement detection
 *******************************************************************************/

 static bool MurataDriverPIRImpl_GetSensitivity(u32 nDeviceIndex, u8 *pValue) {
    LT_UNUSED(nDeviceIndex);
    if (!pValue) {return false; }
    *pValue = s_PIRSensitivityPercent;
    return true;
}

/*******************************************************************************
 * Enable/disable the driver by enabling/disabling IRQ handler
 *******************************************************************************/

static void MurataDriverPIRImpl_Enable(u32 nDeviceIndex, bool bEnable) {
    LT_UNUSED(nDeviceIndex);

    if (bEnable) {
        if (!S.proc.notificationThread) {
            // StartMotionDetection must be called before enabling the driver.
            // This is because we cannot properly service an interrupt without the notification thread.
            LTLOG_YELLOWALERT("enable", "PIR driver cannot be enabled before motion detection is started");
            return;
        }
        setupInt();
    } else {
        cleanupInt();
    }
}

/*******************************************************************************
 * Sets the interval where no motion is re-detected to avoid flip-flopping
 *******************************************************************************/

static bool MurataDriverPIRImpl_SetMotionEndDelay(u32 nDeviceIndex, LTTime interval) {
    LT_UNUSED(nDeviceIndex);

    //TODO ensure that we do not spam the notifications with events.
    //Setup a timer, to prevent proc motion events within the interval window.
    S.cfg.tmMotionDelay  = interval;
    return true;
}

/*******************************************************************************
 * Starts reporting motion to the given TaskProc
 *******************************************************************************/

static void MurataDriverPIRImpl_StartMotionDetection(LTThread_TaskProc * pMotionTaskProc, u8 nDriverId) {
    S.proc.notificationThread  = S.proc.thread->GetCurrentThread();
    S.proc.pMotionTaskProc     = pMotionTaskProc;
    S.proc.driverId            = nDriverId;
    #ifdef LOG_MOTION_EVENTS
    S.proc.thread->SetTimer(S.proc.notificationThread, kMotionLogInterval, LogMotionEvents, NULL, NULL);
    #endif
}

/*******************************************************************************
 * Stop reporting motion
 *******************************************************************************/

static void MurataDriverPIRImpl_StopMotionDetection(void) {
    #ifdef LOG_MOTION_EVENTS
    S.proc.thread->KillTimer(S.proc.notificationThread, LogMotionEvents, NULL);
    #endif
    S.proc.notificationThread = 0;
    S.proc.pMotionTaskProc    = NULL;
}

/*******************************************************************************
 * returns the driver's name
 *******************************************************************************/

static const char * MurataDriverPIRImpl_GetDeviceName(u32 nDeviceIndex) {
    LT_UNUSED(nDeviceIndex);
    return s_DeviceName;
}

/*******************************************************************************
 * returns the number of device units
 *******************************************************************************/

static u32 MurataDriverPIRImpl_GetNumDeviceUnits(void) {
    // only one device is supported
    return 1;
}

/*******************************************************************************
 * create device unit
 *******************************************************************************/

static LTDeviceUnit MurataDriverPIRImpl_CreateDeviceUnitHandle(u32 nDeviceUnitNumber) {
    LT_UNUSED(nDeviceUnitNumber);
    // no device unit created by the driver
    return 0;
}

define_LTDEVICE_DRIVER_IMPLEMENTATION(ILTDriverPIR, MurataDriverPIR);

LTLIBRARY_EXPORT_INTERFACES(MurataDriverPIR, (ILTDriverPIR))

define_LTLIBRARY_INTERFACE(ILTDriverPIR) {
    .GetDeviceName                      = MurataDriverPIRImpl_GetDeviceName,
    .Enable                             = MurataDriverPIRImpl_Enable,
    .SetSensitivity                     = MurataDriverPIRImpl_SetSensitivity,
    .GetSensitivity                     = MurataDriverPIRImpl_GetSensitivity,
    .SetMotionEndDelay                  = MurataDriverPIRImpl_SetMotionEndDelay,
    .StartMotionDetection               = MurataDriverPIRImpl_StartMotionDetection,
    .StopMotionDetection                = MurataDriverPIRImpl_StopMotionDetection
} LTLIBRARY_DEFINITION;
