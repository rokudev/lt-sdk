/*******************************************************************************
 *
 * LTDeviceWiFi: WiFi Device Interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

/**
 * @defgroup ltdevice_wifi LTDeviceWiFi
 * @ingroup ltdevice
 * @brief Provides the primary STA-AP functions for link-layer networking.
 *
 * All platform-dependent features and functions are hidden below this layer and
 * shall not be called or otherwise referenced directly from higher levels.
 * Application code must pass through the this layer to properly sequence
 * states, minimize locking, and avoid conflicts within the driver.
 */

#ifndef LTDEVICEWIFI_H
#define LTDEVICEWIFI_H
/**
 * @defgroup ltdevice_wifi_cb Callbacks
 * @ingroup ltdevice_wifi
  */

/**
 * @defgroup ltdevice_wifi_enum Enumerations
 * @ingroup ltdevice_wifi
 */

/**
 * @defgroup ltdevice_wifi_struct Structs
 * @ingroup ltdevice_wifi
 */

#include <lt/LTTypes.h>
#include <lt/device/wifi/LTWiFi.h>
#include <lt/net/core/LTNetCore.h>

LT_EXTERN_C_BEGIN

/*******************************************************************************
** WiFi Device Constants
*******************************************************************************/

typedef u8 LTDeviceWiFi_Status;
/**
 * WiFi top-level device status indicators.
 * These will be reported by LTDeviceWiFi_StatusCallback() as WiFi starts up
 * or if it gets reset or otherwise brought up or down.
 * Note: These are grouped for greater-than/less-than status comparisons
 *       for failure vs success. Keep that relationship for new constants.
 * @ingroup ltdevice_wifi_enum
 */
enum LTDeviceWiFi_Status {
    kLTDeviceWiFi_Status_Unknown = 0,
    kLTDeviceWiFi_Status_Reset,             ///< In reset. Wait for up before continuing.
    kLTDeviceWiFi_Status_Down,              ///< Chip/driver down. Don't attempt commands.
    kLTDeviceWiFi_Status_Start,             ///< State machine is started
    kLTDeviceWiFi_Status_Up,                ///< chip/driver up. Commands accepted.
    kLTDeviceWiFi_Status_ScanStart,         ///< A scan has been started.
    kLTDeviceWiFi_Status_ScanResult,        ///< New scan results ready to read.
    kLTDeviceWiFi_Status_ScanDone,          ///< Scan has finished.
    kLTDeviceWiFi_Status_JoinStart,         ///< AP join has started.
    kLTDeviceWiFi_Status_JoinAssociated,    ///< STA has associated to AP.
    kLTDeviceWiFi_Status_JoinAuthenticated, ///< STA has been authenticated by AP.
    kLTDeviceWiFi_Status_JoinDone,          ///< The join done (meaning AP is linked).
    kLTDeviceWiFi_Status_JoinFailed,        ///< The join failed.
    kLTDeviceWiFi_Status_LinkConnected,     ///< Link to AP is up
    kLTDeviceWiFi_Status_LinkDisconnected,  ///< Link to AP is (temporarily) down
    kLTDeviceWiFi_Status_Connected,         ///< Connected to AP; the main event to monitor.
    kLTDeviceWiFi_Status_Disconnected,      ///< Disconnected from AP.
    kLTDeviceWiFi_Status_Rejoining,         ///< Indicate attempt to rejoin the AP.
    kLTDeviceWiFi_Status_Max
};

typedef enum {
    kLTDeviceWiFiPowerState_Sleep = 0,
    kLTDeviceWiFiPowerState_Awake = 1,
    kLTDeviceWiFiPowerState_Invalid = 255
} LTDeviceWiFiPowerState;

/*******************************************************************************
** WiFi Callback Definitions
*******************************************************************************/

/**
 * @brief The callback function used for WiFi system status changes.
 *
 * Call OnStatusChange to register this callback.
 *
 * Call NoStatusChange to unregister this callback.
 *
 * @param[in] status: The new WiFi system status.
 * @param[in] unit:  The driver unit instance.
 * @param[in] clientData:  The client's data supplied to OnStatus.
 * @ingroup ltdevice_wifi_cb
 */
typedef void (LTDeviceWiFi_StatusCallback)(LTDeviceWiFi_Status status, LTDeviceUnit unit, void *clientData);

/**
 * @brief The callback function used for AP scans.
 *
 * A pointer to the current AP information structure is returned.
 *
 * @param[in] ap AP information, or NULL when no more APs are found.
 * @param[in] callback_data: Client data provided to callback.
 * @ingroup ltdevice_wifi_cb
 */
typedef void (LTDeviceWiFi_ScanCallback)(LTWiFi_ApInfo *ap, void *callback_data);

/**
 * @brief The callback function used for AP joins.
 *
 * @param[in] status The join status.
 * @param[in] callback_data: Client data provided to callback.
 * @ingroup ltdevice_wifi_cb
 */
typedef void (LTDeviceWiFi_JoinCallback)(LTWiFi_JoinStatus status, void *callback_data);

/*******************************************************************************
** WiFi Device API
*******************************************************************************/

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceWiFi, 1);

/**
 * WiFi Device Library
 *
 * Provides the primary STA-AP functions for link-layer WiFi networking. All
 * platform-dependent features and functions are hidden below this layer and
 * shall not be called or otherwise referenced directly from higher levels.
 *
 * @internal
 *
 * Application code must pass through the this layer to properly sequence
 * states, minimize locking, and avoid conflicts within the driver.
 *
 * If you add functions here, consider whether they need to pass through the
 * WiFi state machine to properly deal with driver state (e.g. single-threaded
 * access to driver commands), or whether they can pass directly to the driver
 * layer because they are independent of state management.
 *
 * @endinternal
 *
 * @ingroup ltdevice_wifi
 */
struct LTDeviceWiFi {
    INHERIT_DEVICE_LIBRARY_BASE

    bool (*GetDeviceInfo)(LTWiFi_DriverInfo *info);
        /**<
         * @brief Return general information about the device / driver.
         *
         * Normally this info is just logged for the record.
         * @param[out] info The device info requested.
         * @return false if WiFi is not up or configured.
         */

    bool (*GetMacAddress)(LTMacAddress *mac);
        /**<
         * @brief Return the MAC address of the local (STA) device.
         *
         * To get the MAC address of the AP, see GetAPInfo().
         * @param[out] mac The MAC address requested.
         * @return false if WiFi is not up or configured.
         */

    bool (*SetMacAddress)(LTMacAddress *mac);
        /**<
         * @brief Set the MAC address of the local (STA) device.
         *
         * @param[in] mac The new MAC address.
         * @return false if WiFi is not up or configured.
         */

#ifndef DOXY_SKIP // [
    void (*SetOption)(char const *option, s32 value);
        /**<
         * @brief Experimental config idea - TBD and pending
         */

    u32  (*GetOption)(char const *option);
        /**<
         * @brief Experimental config idea - TBD and pending
         */
#endif  // DOXY_SKIP ]

    void (*OnStatusChange)(LTDeviceWiFi_StatusCallback cb, LTThread_ClientDataReleaseProc *clientDataReleaseProc, void * clientData);
        /**<
         * @brief Set the callback for WiFi device status changes.
         *
         * @param[in] cb The function to be called each time the device status changes.
         * @param[in] clientDataReleaseProc The function to be called when the client data is released (when NoStatusChange is called).
         * @param[in] clientData The client data passed back to the event callback.
         */

    void (*NoStatusChange)(LTDeviceWiFi_StatusCallback cb);
        /**<
         * @brief Disable a prior OnStatusChange callback.
         *
         * @param[in] cb The function that should no longer be called when device status changes.
         * It must be the same as the function passed to OnStatusChange.
         */

    void (*Reset)(void);
        /**<
         * @brief Reset and reinitialize the full WiFi stack.
         *
         * This will temporarily bring down the WiFi device.
         * If the device is connected, the connection will be abruptly terminated.
         * All variables will be cleared and the state machine will be restarted.
         * Monitor the OnStatusChange callback to track the device state.
         */

    void (*EnableRadio)(bool state);
        /**<
         * @brief Enable or disable the WiFi radio(s).
         *
         * This can be done to save
         * power or to otherwise disable/enable on-air signals.
         * If the device is connected, the connection will be abruptly terminated.
         * Monitor the OnStatusChange callback to track the device state.
         *
         * @param[in] state Set true to enable, false to disable.
         */

    void (*GetMetrics)(LTWiFi_Metrics *metrics, LT_SIZE sizeOfMetrics);
        /**<
         * @brief Get various metrics.
         *
         * Returns counters for total number of frames, retries, drops, and various
         * signal quality statistics.
         *
         * Metric fields are cleared on WiFi reset or if NULL metrics pointer is passed.
         *
         * @param[out] metrics: The requested metrics.
         * @param[in] sizeOfMetrics: Size of metrics struct passed.
         * Pass NULL to clear metrics instead.
         */

    bool (*LoadApSettings)(LTWiFi_ApInfo *spec);
        /**<
         * @brief Load AP credentials from LTSystemSettings.
         *
         * @param[out] spec AP info that can be passed to JoinAp. All
         * fields and options are reset to defaults.
         * @return true if settings were found.
         */

    bool (*SaveApSettings)(LTWiFi_ApInfo *spec);
        /**<
         * @brief Save or clear AP credentials and wifi settings in LTSystemSettings.
         *
         * @param[in] spec that has AP settings to be saved, spec == NULL to clear
         * all wifi settings or spec->ssid == "" to clear only credentials.
         * @return true if settings can be saved.
         */

    void (*ScanAps)(LTWiFi_ScanSpec *spec, LTDeviceWiFi_ScanCallback *cb, void *callback_data);
        /**<
         * @brief Scan for APs with the given scan specification.
         *
         * @param[in] spec Optionally indicates how to conduct the scan. Passing
         * a NULL will use the default and most common scan behavior.
         * @param[in] cb: The function that will be called for each AP found that matches the
         * given specification. It is also called when no more APs were found.
         * @param[in] callback_data: Data passed to callback when it's invoked.
         */

    void (*JoinAp)(LTWiFi_ApInfo *ap, LTDeviceWiFi_JoinCallback *cb, void *callback_data);
        /**<
         * @brief Join an AP BSS using the given specification.
         *
         * @param[in] ap The AP credentials and options.
         * @param[in] cb The function called to indicate progress of the operation.
         * @param[in] callback_data: Data passed to callback when it's invoked.
         */

    bool (*IsConnected)(void);
       /**<
         * @brief Return true if link to AP is connected.
         *
         * @return false if AP is not connected
         */

    bool (*GetApInfo)(LTWiFi_ApInfo *ap);
       /**<
         * @brief Get AP information.
         *
         * Get info about the AP used for the latest BSS join including
         * signal strength, operating channel, security mode, etc.
         * @param[out] ap The requested data.
         * Fields will be cleared, then set accordingly.
         * Undefined behavior results if this pointer is NULL or otherwise invalid.
         * @return false if AP is not connected
         */

    void (*Disconnect)(void);
       /**<
         * @brief Terminate link to AP.
         *
         * If there is no current link, then this is a no-op.
         * If a join is in progress, it will be terminated.
         */

    bool (*StartAp)(LTWiFi_ApInfo *spec);
        /**<
         * @brief Setup a soft AP BSS using the given specification.
         *
         * @param[in] spec The AP credentials and options.
         */

    bool (*ReceiveFrame)(LTDeviceUnit unit, LTWiFi_FrameRxCallback *pCallback, void *pClientData);
        /**<
         * @brief Receive WiFi frames
         *
         * Operates by setting-up a received frame processing callback.
         *
         * Intended to be used by networking stack or other MAC-layer processing scheme.
         *
         * For performance, this function operates directly at the WiFi driver
         * layer, outside of the WiFi state machine.
         *
         * pCallback gets executed in WiFi RX processing context, and depends on
         * driver archirecture, may even be a top half IRQ.
         *
         * @param[in] unit: The driver unit instance.
         * @param[in] pCallback: The callback function.
         * @param[in] pClientData: A pointer passed to the callback (upper-layer processing context).
         * @return true when callback is set successfully.
         */

    bool (*TransmitFrames)(LTDeviceUnit unit, const LTBufferChain *bufferChain);
        /**<
         * @brief Transmit one or more WiFi frames
         *
         * Pass one or more frames for transmission by the WiFi device.
         *
         * @internal
         * For performance, this function operates directly at the WiFi driver
         * layer, outside of the WiFi state machine.
         * @endinternal
         *
         * @param[in] unit: The driver unit instance.
         * @param[in] bufferChain: SG buffers to send.
         * @param[in/out] priv: application private data
         * @return true when frames are successfully queued for transmission.
         */
    int (*IwPriv)(int argc, const char **argv, void *priv);
        /**<
         * @brief Run a vendor specific commands like iwpriv
         *
         * @param[in] argc: number of arguments.
         * @param[in] argv: arguments
         * @return 0 when the command is run successfully
         */
    LTWiFi_DisconnectReason (*GetDiscReason)(void);
        /**<
         * @brief Get the disconnect reason code
         *
         * @return disconnect reason code
         */
    LTWiFi_JoinStatus (*GetLastJoinStatus)(void);
        /**<
         * @brief Get Wifi last updated join status
         *
         * @return Wifi last updated join status
         */

    void (*GetWiFiDrvTxQStats)(u8 ac, LTWiFi_DrvTxQStats *stats);
         /**<
         * @brief Get Wifi driver tx queue stats
         *
         * @param[in] ac: Access category from 0 to 3. 0:V0, 1:VI, 2:BE, 3:BK
         * @param[out] stats: Stats result
         */
};
LT_EXTERN_C_END
#endif /* #ifndef LTDEVICEWIFI_H */
