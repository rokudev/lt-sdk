/*******************************************************************************
* <lt/device/sleepcontrol/LTDeviceSleepControl.h>
*
* LTDevice Sleep Control Device Library
*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
*******************************************************************************/

/**
 * @defgroup LTDeviceSleepControl LTDeviceSleepControl
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for initiating Sleep Mode.
 *
 * This Library provides access to System Sleep Mode.
 * The API provides functions to initialize and control sleep behavior with configurable
 * wake-up sources and callbacks for operations before sleep and after wake-up.
 * The implementation relies on LTCore Low Power Sleep Mode to enter sleep mode.
 * @see SetEnterSleepModeProc
 * @see SetEnterSleepModeIdleDelay
 */

#ifndef LT_INCLUDE_LT_DEVICE_SLEEPCONTROL_LTDEVICESLEEPCONTROL_H
#define LT_INCLUDE_LT_DEVICE_SLEEPCONTROL_LTDEVICESLEEPCONTROL_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN
/*
 * How to use:
 *
 * 1. Initialize sleep control by calling the Init() function with:
 *    - wakeupSources: A bitmask of LTDeviceSleepControl_WakeupSources indicating which wake-up
 *                     sources to enable (e.g., kLTDeviceSleepControl_WakeupSource_RTC,
 *                     kLTDeviceSleepControl_WakeupSource_UARTRX, etc.). To enable all, you can use
 *                     kLTDeviceSleepControl_WakeupSource_Any.
 *    - notificationThread: A pointer to the thread that will handle sleep/wake notifications.
 *    - pBeforeSleepTask: A callback function to execute tasks before entering sleep.
 *    - pAfterSleepTask: A callback function to execute tasks after waking up.
 *    - pClientData: User data to pass to the callbacks.
 *
 *    Example:
 *      bool initResult = pSleepControl->Init(
 *           (kLTDeviceSleepControl_WakeupSource_RTC | kLTDeviceSleepControl_WakeupSource_UARTRX),
 *           notificationThread,
 *           beforeSleepCallback,
 *           afterSleepCallback,
 *           pClientData);
 * 2. Trigger sleep mode by calling AllowSleep().
 *    - AllowSleep() / PreventSleep(): control whether the device is allowed to sleep.
 * 3. Optionally, use other API functions:
 *    - SetSleepDurationLimit(sleepDurationLimit): to set a maximum sleep duration.
 *    - GetSleepDurationLimit(): returns the current sleep duration limit.
 *    - GetLastActualSleepDuration(): returns how long the device actually slept.
 *    - GetWakeTime(): returns the time at which the device woke.
 *    - GetWakeReason(): returns the cause for the wake-up (RTC, GPIO, Key, etc.).
 */
typedef enum LTDeviceSleepControl_WakeReason {
    kLTDeviceSleepControl_WakeReason_RTC = 1,   // real-time clock wake
    kLTDeviceSleepControl_WakeReason_GPIO,      // GPIO wake
    kLTDeviceSleepControl_WakeReason_Key,       // key press wake
    kLTDeviceSleepControl_WakeReason_Insomnia,  // device refused to sleep due to some internal condition
    kLTDeviceSleepControl_WakeReason_Unknown,
} LTDeviceSleepControl_WakeReason;

typedef enum LTDeviceSleepControl_WakeupSources {
    kLTDeviceSleepControl_WakeupSource_RTC  = 1 << 0,
    kLTDeviceSleepControl_WakeupSource_UARTRX = 1 << 1,
    kLTDeviceSleepControl_WakeupSource_Key  = 1 << 2,
    kLTDeviceSleepControl_WakeupSource_Captouch  = 1 << 3,
    /* (others, TBD) */
    kLTDeviceSleepControl_WakeupSource_Any = LT_U32_MAX,
} LTDeviceSleepControl_WakeupSources;

typedef enum LTDeviceSleepControl_SleepMode {
    kLTDeviceSleepControl_SleepMode_Sleep = 0,
    kLTDeviceSleepControl_SleepMode_Ship,
} LTDeviceSleepControl_SleepMode;

/**
 * @brief Sleep Control Device Library Root Interface.
 */
TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSleepControl, 1);

typedef bool (LTDeviceSleepControl_BeforeSleepProc)(void *pClientData);

struct LTDeviceSleepControl {
    INHERIT_DEVICE_LIBRARY_BASE
    bool (*Init)(LTDeviceSleepControl_WakeupSources    wakeupSources,
                 const LTOThread                      *notificationThread,
                 LTDeviceSleepControl_BeforeSleepProc *pBeforeSleepTask,
                 LTThread_TaskProc                    *pAfterSleepTask,
                 void                                 *pClientData);
    /**< @brief Initialize the sleep control device
     *
     * @param[in] wakeupSources Which wakeup sources are enabled
     * @param[in] notificationThread Thread for notifications
     * @param[in] pBeforeSleepTask Task to run before sleep. @note The task will be called in the context of a special
     *            LTCore thread of a priority higher than any other thread in the system.
     * @param[in] pAfterSleepTask Task to run after sleep. @note The task will be run on the notification thread.
     * @param[in] pClientData Data for callbacks
     * @retval true if initialization was successful, false otherwise
     */

    bool                        (*SetSleepDurationLimit)(const LTTime sleepDurationLimit);
    /**< @brief Set the sleep duration limit
     *
     * @param[in] sleepDurationLimit Maximum duration to sleep
     * @retval true if the limit was set successfully, false otherwise
     */

    LTTime                      (*GetSleepDurationLimit)(void);
    /**< @brief Get the current sleep duration limit
     *
     * @retval The current sleep duration limit
     */

    LTTime                      (*GetLastActualSleepDuration)(void);
    /**< @brief Get the actual duration of the last sleep
     *
     * @retval The actual duration of the last sleep
     */

    LTTime                      (*GetWakeTime)(void);
    /**< @brief Get the wake-up time
     *
     * @retval The time at which the device woke up
     */

    LTDeviceSleepControl_WakeReason (*GetWakeReason)(void);
    /**< @brief Get the reason for waking up
     *
     * @retval The reason the device woke up
     */

    bool                        (*AllowSleep)(LTDeviceSleepControl_SleepMode sleepMode);
    /**< @brief Allow the device to sleep
     *
     * @param[in] sleepMode The sleep mode to enter
     * @retval true if the device is allowed to sleep, false otherwise
     */

    bool                        (*PreventSleep)(void);
    /**< @brief Prevent the device from sleeping
     *
     * @retval true if the device is prevented from sleeping, false otherwise
     */
};

typedef_LTLIBRARY_INTERFACE(ILTDriverSleepControl, 1) {
    bool                            (*Init)(LTDeviceSleepControl_WakeupSources wakeupSources,
                                        const LTOThread                      *notificationThread,
                                        LTDeviceSleepControl_BeforeSleepProc *pBeforeSleepTask,
                                        LTThread_TaskProc                    *pAfterSleepTask,
                                        void                                 *pClientData);
    bool                            (*SetSleepDurationLimit)(const LTTime sleepDurationLimit);
    LTTime                          (*GetSleepDurationLimit)(void);
    LTTime                          (*GetLastActualSleepDuration)(void);
    LTTime                          (*GetWakeTime)(void);
    LTDeviceSleepControl_WakeReason (*GetWakeReason)(void);
    bool                            (*AllowSleep)(LTDeviceSleepControl_SleepMode sleepMode);
    bool                            (*PreventSleep)(void);
}

LTLIBRARY_INTERFACE;

LT_EXTERN_C_END
#endif  // #ifndef LT_INCLUDE_LT_DEVICE_SLEEPCONTROL_LTDEVICESLEEPCONTROL_H
