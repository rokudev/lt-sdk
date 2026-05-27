/******************************************************************************
 * <lt/device/battery/LTDeviceBattery.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

/**
 * @defgroup ltdevice_battery LTDeviceBattery
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for battery management.
 */

#ifndef LT_INCLUDE_LT_DEVICE_BATTERY_LTDEVICEBATTERY_H
#define LT_INCLUDE_LT_DEVICE_BATTERY_LTDEVICEBATTERY_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

typedef enum {
    kLTDeviceBatteryStatus_Unknown = 0,     /**< Battery status is unknown */
    kLTDeviceBatteryStatus_NotPresent,      /**< Battery is absent */
    kLTDeviceBatteryStatus_Idle,            /**< Battery is neither charging or discharging */
    kLTDeviceBatteryStatus_Charging,        /**< Battery is charging */
    kLTDeviceBatteryStatus_Discharging,     /**< Battery is discharging */
} LTDeviceBatteryStatus;

typedef enum LTDeviceBattery_OperatingMode {
    kLTDeviceBattery_OperatingMode_Unknown = 0, /**< Battery operating mode is unknown */
    kLTDeviceBattery_OperatingMode_Normal,      /**< Battery is in normal operating mode */
    kLTDeviceBattery_OperatingMode_Shipping,    /**< Battery is in shipping mode */
    kLTDeviceBattery_OperatingMode_Shutdown,    /**< Battery is in shutdown mode */
} LTDeviceBattery_OperatingMode;

enum {
    kLTDeviceBattery_MinimumInputCurrentLimit = 100,  /**< Minimum input current limit in mA */
    kLTDeviceBattery_DefaultInputCurrentLimit = 3200, /**< Default input current limit in mA */
};

typedef enum {
    kLTDeviceBattery_Charging_Idle = 0,             /**< Idle charging state */
    kLTDeviceBattery_Charging_Trickle,              /**< Trickle charging state */
    kLTDeviceBattery_Charging_PreCharge,            /**< Pre-charge state */
    kLTDeviceBattery_Charging_Constant_Current,     /**< Fast charging state */
    kLTDeviceBattery_Charging_Constant_Voltage,     /**< Constant voltage state */
    kLTDeviceBattery_Charging_Charge_Complete,      /**< Charge complete */
} LTDeviceBattery_ChargingMode;

typedef struct {
    LTDeviceBatteryStatus status;           /**< Battery system status */
    struct {
        u32 designCapacity;                 /**< Battery design capacity in mAh */
        u32 stateOfCharge;                  /**< Battery capacity in percentage */
        u16 voltage;                        /**< Battery voltage in mV */
        s16 current;                        /**< Battery current in mA */
        s16 temperature;                    /**< Battery temperature in 0.1 degree Celsius */
    } battery;
    struct {
        u16 batteryVoltage;                 /**< battery voltage at charger in mV */
        s16 batteryCurrent;                 /**< battery current at charger in mA */
        u16 adaptorVoltage;                 /**< adaptor voltage in mV */
        s16 adaptorCurrent;                 /**< adaptor current in mA */
        u16 systemVoltage;                  /**< system voltage in mV */
        s16 temperature;                    /**< Charger temperature in 0.1 degree Celsius */
        u16 inputCurrentLimit;              /**< Input current limit in mA */
        LTDeviceBattery_ChargingMode mode;  /**< Charging mode */
    } charger;
} LTDeviceBatteryInfo;

typedef void (* LTDeviceBattery_StatusChangeEventProc)(LTDeviceBatteryInfo info, void * pClientData);
    /**< Battery status change callback function
      *  @param[in] pClientData Client data for the callback. */

typedef_LTObject(LTBatterySystem, 1) {

    bool (* GetProperty)(LTBatterySystem *bms, const char *name, void *value);
        /**< gets a property by name.
         *
         *   @param bms  the battery system object.
         *   @param name the property to be set.
         *   @param value the property value to be set.
         *   @return true if the driver supports the property, else false.
         */

    bool (* SetProperty)(LTBatterySystem *bms, const char *name, const void *value);
        /**< sets a property by name.
         *
         *   @param bms  the battery system object.
         *   @param name the property to be set.
         *   @param args the property value to be set.
         *   @return true if the driver supports the property, else false.
         */

    bool (* SetOperatingMode)(LTBatterySystem *bms, LTDeviceBattery_OperatingMode mode);
        /**< Requests battery operating mode.
         *   @param bms the battery system object.
         *   @param[in] mode The operating mode to enter.
         *   @return    true if operating mode request was successful, fail otherwise */

    bool (* EnableCharging)(LTBatterySystem *bms, bool enable);
        /**< Enables or disables battery charging.
         *   @param bms the battery system object.
         *   @param[in] enable true to enable charging, false to disable.
         *   @return    true if charging is enabled or disabled, false if not. */

    bool (* DisableInputPower)(LTBatterySystem *bms, bool disable);
        /**< Disables input power to allow battery to discharge while charger is still connected.
         *   @param bms the battery system object.
         *   @param[in] disable true to disable input power, false to enable.
         *   @return    true if successful, fail otherwise. */

    bool (* SetInputCurrentLimit)(LTBatterySystem *bms, u16 currentLimitMa);
        /**< Sets the input current limit for the battery charger.
         *
         * The limit is in mA and must be within the range of kLTDeviceBattery_MinimumInputCurrentLimit
         * to kLTDeviceBattery_DefaultInputCurrentLimit.
         * It will be rounded down to 20mA steps.
         *
         *   @param bms the battery system object.
         *   @param[in] currentLimitMa Input current limit in mA).
         *   @return    true if successful, false if not. */

    bool (* GetInfo)(LTBatterySystem *bms, LTDeviceBatteryInfo *pInfo);
        /**< Obtains battery state.
         *   @param bms the battery system object.
         *   @param[out] pInfo Pointer to the LTDeviceBatteryInfo structure from the caller.
         *   @return     true if state is read, false if not. */

    bool (* IsExternalPowerPresent)(LTBatterySystem *bms, LTDeviceBatteryInfo *pInfo);
         /**< Obtains information about external input power, such as USB.
          *   @param bms the battery system object.
          *   @param pInfo optional LTDeviceBatteryInfo to check instead of the current one.
          *   @return     true if external power is present, false if not. */

    bool (* PowerCycle)(LTBatterySystem *bms);
        /**< Resets system power. This causes a power cycle of the system.
         *   @param bms the battery system object.
         *   @return    true if power reset was successful, fail otherwise */

    bool (* GetBatteryVoltageMinMax)(LTBatterySystem *bms, u32 *pMinVoltage, u32 *pMaxVoltage);
        /**< Obtains the minimum and maximum battery voltage.
         *   @param bms the battery system object.
         *   @param[out] pMinVoltage Pointer to store minimum battery voltage in mV.
         *   @param[out] pMaxVoltage Pointer to store maximum battery voltage in mV.
         *   @return    true if values are successfully obtained, false otherwise. */

    bool (* OnStatusChangeCallback)(LTBatterySystem *bms, LTDeviceBattery_StatusChangeEventProc pEventHandler, void * pClientData);
        /**< Registers battery status change event handler.
         *   @param bms the battery system object.
         *   @param[in] pEventHandler Status change event handler pointer
         *   @param[in] pClientData Client data for the handler.
         *   @return    true if handler is registered. */

    bool (* NoStatusChangeCallback)(LTBatterySystem *bms, LTDeviceBattery_StatusChangeEventProc pEventHandler);
        /**< Unregisters battery status change event handler.
         *   @param bms the battery system object.
         *   @param[in] pEventHandler Status change event handler pointer
         *   @return    true if handler is unregistered. */

} LTOBJECT_API;

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceBattery, 1);

/**
 * @brief The API for controlling I2C devices.
 */
struct LTDeviceBattery {

    INHERIT_DEVICE_LIBRARY_BASE

    LTBatterySystem *(* GetBatterySystemByName)(const char *name);
        /**< Finds battery device unit index for corresponding to the given name.
         *   @param[in]  name The name of the desired battery device unit.
         *  @return     the device unit index if found else LT_U32_MAX. */

    LTBatterySystem *(* GetBatterySystem)(u32 index);
        /**< Finds battery device unit name for corresponding to the given index.
         *   @param[in]  index The index of the desired battery device unit.
         *  @return     the device unit name if found else NULL. */

    const char *(* GetNameFromIndex)(u32 index);
        /**< Finds battery system device name from index.
         *  @param[in]  index The index of the desired battery device unit.
         *  @return     the device unit name if found else NULL. */
};

#ifndef DOXY_SKIP // [
typedef_LTLIBRARY_INTERFACE(ILTDriverBattery, 1) {
    /**< LT Battery Driver interface - only to be used internally by LTDeviceBattery */
    LTBatterySystem *(* GetBatterySystemByName)(const char *name);
        /**< Finds battery device unit index for corresponding to the given name.
         *   @param[in]  name The name of the desired battery device unit.
         *  @return     the device unit index if found else LT_U32_MAX. */

    LTBatterySystem *(* GetBatterySystem)(u32 index);
        /**< Finds battery device unit name for corresponding to the given index.
         *   @param[in]  index The index of the desired battery device unit.
         *  @return     the device unit name if found else NULL. */

    const char *(* GetNameFromIndex)(u32 index);
        /**< Finds battery system device name from index.
         *  @param[in]  index The index of the desired battery device unit.
         *  @return     the device unit name if found else NULL. */
} LTLIBRARY_INTERFACE;
#endif // DOXY_SKIP ]

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_BATTERY_LTDEVICEBATTERY_H

/** @} */  /* End of group ltdevice_battery */
