/*******************************************************************************
 * lt/source/lt/driver/motor/LTDriverMotorPanTiltMS320008N_priv.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/
#ifndef LT_SOURCE_LT_DRIVER_MOTOR_LTDRIVERMOTORPANTILTMS320008N_PRIV_H
#define LT_SOURCE_LT_DRIVER_MOTOR_LTDRIVERMOTORPANTILTMS320008N_PRIV_H

#include <lt/LT.h>

#define MOTORCOMMAND_STEP_ANGLE      5.625  // Motor's full step angle (degrees per step)
#define MOTOR_IC_CLOCK  100000

#define MAX_PAN_MOTOR_PULSE  7964
#define MAX_TILT_MOTOR_PULSE (8192 >> 1)

typedef enum {
    kMotorOperationalStatus_Stop = 0,
    kMotorOperationalStatus_Running = 1,
} MotorOperationalStatus;

typedef struct {
    s32 x;  // Delta steps for X-axis
    s32 y;  // Delta steps for Y-axis
    MotorOperationalStatus status;
    s32 speed;
} MotorStatus;

typedef struct {
    s32 deltaX;  // Delta steps for X-axis
    s32 deltaY;  // Delta steps for Y-axis
} MotorData;

typedef enum {
    kLTMotorChannel_Pan = 0,
    kLTMotorChannel_Tilt,
    kLTMotorChannel_PanTilt,
} MotorChannel;

typedef struct MOTOR_RESET_INFO {
	unsigned int x_max_steps;
	unsigned int y_max_steps;
	unsigned int x_cur_step;
	unsigned int y_cur_step;
}motor_reset_info_s;

/*MS320008N configuration Registers Address*/
typedef enum {
    kLTMotorCommmand_Init = 0,
    kLTMotorCommmand_ConfLoad,
    kLTMotorCommmand_ForceStop,
    kLTMotorCommmand_DcCtrl,
    kLTMotorCommmand_ErrClr  = 0x0D,
    kLTMotorCommmand_SetMode = 0x10,
    kLTMotorCommmand_SetDir,
    kLTMotorCommmand_SetFreqL,
    kLTMotorCommmand_SetFreqH,
    kLTMotorCommmand_SetPulseL,
    kLTMotorCommmand_SetPulseH,
    kLTMotorCommmand_SetAmp,
    kLTMotorCommmand_StatusReq = 0x1D,
    kLTMotorCommmand_PulseRecL,
    kLTMotorCommmand_PulseRecH,
} MotorCommand;

enum {
    kMotorSlaveMS320008NAddress   = 0x10,  // Address of MS320008N i2c slave
    kMotorMicrostepPerFullStep    = 64,    // Microstepping Multiplier; can do upto 1/64 microstepping
    kMotorControlEnable           = 0x60,  // Enable control pdEN=1, nRST=1, nStandby=1
    //Note : due to reverse motor connection, count in reverse dir, hence XCH_recordRev = 1

    kMotorDirClockwise            = 0x71,  // Set Motor direction clockwise
    kMotorDirCounterClockwise     = 0x70,  // Set Motor direction counter lockwise
    //Note : direction reversed due to incorrect motor connection, ideally CW=0x70 & CCW=0x71

    kMotorLoadConfiguration       = 0x80,  // Load the configuration for the motor
    kMotorSetNonResetState        = 0x01,  // Set cmd_nRST flag to 1(Nonreset state)
    kMotorClrUnderOverTemperature = 0xC0,  // Clear undervoltage/overtemperature event buffer
    kMotorDisablePowerDriver      = 0x00,  // Disable the power driver
    kMotorConfigureTiltChannel    = 0x10,  // Adding 0x10 in command for pan channel
    kMotorCheckRunningFlag        = 0x08,  // Check Motor running state; 1:running, 0:stopped
    kMotorSetCurrentAmplitude     = 0x84   // Set amplitude (1/8 step, 100% current)
};

typedef enum {
    kMotorAction_Stop      = 0x01,  // Stop the motor
    kMotorAction_Reset     = 0x02,  // Reset the motor
    kMotorAction_Move      = 0x03,  // Rotate the motor
    kMotorAction_GetStatus = 0x04,  // Get motor status
    kMotorAction_MotorSpeed = 0x05,  // Motor speed
} MotorAction;

typedef enum {
    kMotorAngle_XMax = 350,  // Maximum X-axis angle
    kMotorAngle_YMax = 135,  // Maximum Y-axis angle
    kMotorAngle_XYMin = 0,   // Minimum angle for both X and Y
} MotorAngle;

enum {
    kMinimumMotorSpeed = 500,
    kMaximumMotorSpeed = 1500,
    kDefaultMotorSpeed = 1000,
};

enum {
    kMS320008NRegisterForceStop = 0x80,
    kMS320008NRegisterHighByte  = 0x100,
    kMS320008NPulseByteDivider  = 256,
    kMS320008NPulseThreshold    = 0xF000,  // Maximum pulse difference threshold
    kMS320008NPulseMaxValue     = 0xFFFF   // Maximum pulse value
};



void *MotorOpen(void);
void MotorClose(void);
bool IrcutOpen(void);
bool IrcutClose(void);
bool IrcutReset(void);

#endif /* LT_SOURCE_LT_DRIVER_MOTOR_LTDRIVERMOTORPANTILTMS320008N_PRIV_H */
