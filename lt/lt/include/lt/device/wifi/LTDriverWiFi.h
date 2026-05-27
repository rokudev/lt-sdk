/*******************************************************************************
 *
 * LTDriverWiFi: WiFi Driver Interface
 * -----------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#ifndef LTDRIVERWIFI_H
#define LTDRIVERWIFI_H

#include <lt/LTTypes.h>
#include <lt/device/wifi/LTWiFi.h>
#include <lt/net/core/LTNetCore.h>

LT_EXTERN_C_BEGIN
/*******************************************************************************
 *
 * WiFi Driver Abstract Interface
 * ------------------------------
 *
 * This API is called by the Roku LT WiFi state machine. The state machine
 * will serialize all calls to this driver code. Commands like scan and
 * join will never overlap. This eliminates the need to do locking at this
 * API level.
 *
 * In addition, the WiFi state machine will verify all arguments before passing
 * them to the API. The API does not need to validate arguments again.
 *
 * The WiFi state machine will also handle timing (like timeouts) and also
 * handle special recovery conditions (like auto-reconnect).
 *
 * The API functions are split into those that immediately set values or get
 * values or information, and those that operate asynchronously (non-blocking)
 * and indicate state change or results with a callback function. Such
 * callback functions can run in the context of a driver thread. They will
 * be processed quickly and will call only simple functions and not call
 * any blocking functions. (Usually they will just set state variables and
 * copy information into a different context.)
 *
 ******************************************************************************/

TYPEDEF_LTLIBRARY_INTERFACE(LTDriverWiFi, 1);

/** DOCUMENTATION_NEEDED */
struct LTDriverWiFiApi {
    INHERIT_INTERFACE_BASE

    bool (*GetDriverInfo)(LTDeviceUnit unit, LTWiFi_DriverInfo *info);
    /**<
     * @brief Get general information about the WiFi driver.
     *
     * Returns vendor id, product id, chip id, version, date, etc.
     * Used to print info to console or log to server for reference.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[out] info: returns device info on success
     * @return TRUE if info has valid information, otherwise return FALSE
     */

    bool (*SetDriverState)(LTDeviceUnit unit, LTWiFi_DriverState state);
    /**<
     * @brief Set driver state to be up, down, or reset.
     *
     * Tell driver to enable, disable, or reset radios and interfaces.
     *
     * On the reset states, all pending operations are immediately halted.
     *
     * The reset states are necessary if application detects driver hang or malfunction.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[in] state: see above enum for details
     * @return FALSE if state cannot be set (like hardware failure)
     * @note Non-blocking. Returns immediately. Poll GetDriverState to determine new state.
     */

    LTWiFi_DriverState (*GetDriverState)(LTDeviceUnit unit);
    /**<
     * @brief Return WiFi driver operational state.
     *
     * @param[in] unit: specifies the driver unit instance
     * @return Current driver state (up, down, RF_down, reset, init, etc.)
     */

#if 0
    void (*SetApMode)(LTDeviceUnit unit, LTWiFi_ApInfo *ap);
    /**<
     * @brief Settings for AP mode (soft-AP)
     *
     * If the STA mode has already been started with SetDriverState(up), then
     * then concurrent STA+AP operation will occur. If STA is not started, then
     * only AP mode will occur.
     *
     * The parameter provides the AP settings (SSID, password, security mode,
     * channel, and bandwidth.) Any null fields will use default settings.
     * Note that the channel field may get ignored if the STA is already
     * operating on a specific channel.
     *
     * Setting AP mode may cause a WiFi reset and the state machine must call
     * GetDriverState() to determine when WiFi is operational again.
     *
     * A NULL ap pointer disables the AP mode.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[in] ap: AP settings if required for the specified mode
     * @return Current driver state (up, down, RF_down, reset, init, etc.)
     */
#endif

    bool (*SetOption)(LTDeviceUnit unit, char const *option, void *value);
    /**<
     * @brief Set a WiFi option.
     *
     * Set a specific wifi option like MAC address, country code, PHY bandwidth,
     * power modes, power levels and timing, promiscuous settings, etc.
     *
     * Options are specified as strings for flexible expansion and compatibility.
     *
     * @code
     * LTMacAddress *host_address = get_host_address();
     * wifi->SetOption("mac_address", host_address);
     * char *country_code = "US";
     * wifi->SetOption("country_code", country_code);
     * @endcode
     *
     * Some options may require that SetDriverState(up) is called again.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[in] option: specifies the option as a string
     * @param[in] value: can be int *, char *, or struct *, depending on the option
     * @return FALSE if the option is not implemented or otherwise invalid.
     */

    bool (*GetOption)(LTDeviceUnit unit, char const *option, void *value);
    /**<
     * @brief Get a WiFi option.
     *
     * Get a specific wifi option like MAC address, country code, PHY bandwidth,
     * power modes, power levels and timing, promiscuous settings, etc.
     *
     * Options are specified as strings for flexible expansion and compatibility.
     *
     * @code
     * LTMacAddress my_address;
     * wifi->GetOption("mac_address", &my_address);
     * char country_code[4];
     * wifi->GetOption("country_code", &country_code);
     * @endcode
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[in] option: specifies the option as a string
     * @param[out] value: can be int *, char *, or struct *, depending on the option
     * @return FALSE if the option is not implemented or otherwise invalid.
     */

    void (*GetMetrics)(LTDeviceUnit unit, LTWiFi_Metrics *metrics, LT_SIZE sizeOfMetrics);
    /**<
     * @brief Get link and driver metrics.
     *
     * Returns counters for total number of frames, retries, drops, and various
     * signal quality statistics.
     *
     * Metric fields are cleared on WiFi reset or if NULL metrics pointer is passed.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[out] metrics: current metrics or NULL to clear metrics
     * @param[in] sizeOfMetrics: Size of metrics struct passed.
     */

    void (*ScanStart)(LTDeviceUnit unit, LTWiFi_ScanSpec *spec, LTWiFi_ScanResults_CB callback);
    /**<
     * @brief Start a scan for APs.
     *
     * The scan is controlled by the scan specification struct (spec).
     *
     * If the spec struct is cleared to zero a default scan will be performed.
     *
     * The callback will be invoked for each AP found. When the callback ap
     * pointer is null, then scan the has finished and there are no more results.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[in] spec: specifies the scan parameters
     * @note Non-blocking. Returns immediately.
     */

    void (*ScanCheck)(LTDeviceUnit unit);
    /**<
     * @brief Nudge scan to indicate results
     *
     * On systems that do not provide an async scan callback, this function
     * can be called by the state machine to poll for results.
     *
     * When this function is called, it will force the above ScanResults callback
     * to be invoked to report new scan results. This method makes it possible
     * to use the same approach for both async and blocking lower layers.
     *
     * @param[in] unit: specifies the driver unit instance
     */

    void (*ScanAbort)(LTDeviceUnit unit);
    /**<
     * @brief Terminate the current scan.
     *
     * Sometime it is necessary to terminate a scan when higher priority
     * actions are necessary. The ScanResults callback will return NULL
     * to indicate that the scan is done.
     *
     * If the scan is already done, this is a no-op.
     *
     * @param[in] unit: specifies the driver unit instance
     * @note Non-blocking. Returns immediately.
     */

    void (*JoinStart)(LTDeviceUnit unit, LTWiFi_ApInfo *ap_spec, LTWiFi_JoinStatus_CB callback);
    /**<
     * @brief Start a join to an AP or peer.
     *
     * A BSS join operation is started using the credentials and parameters
     * specified by the ap argument. Empty or clear fields will use reasonable
     * default values.
     *
     * The progress of the join operation can be monitored by the JoinStatus
     * callback, including association and authentication, as well as related
     * failure conditions.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[in] ap: specifies the SSID, password, BSSID, and optional params
     * @param[in] callback: called when join status changes
     * @return TRUE if join can be started
     * @note Non-blocking. Returns immediately. Callback will indicate status.
     */

    void (*JoinCheck)(LTDeviceUnit unit);
    /**<
     * @brief Nudge join to indicate its status
     *
     * For drivers that support a true callback, this function is a no-op and
     * would simply return. However, if driver has no async callback, this
     * function can be used to check the driver status and then call the
     * JoinStatus callback in the above function. This method makes it possible
     * to use the same approach for both async and blocking drivers.
     *
     * @param[in] unit: specifies the driver unit instance
     */

    void (*JoinAbort)(LTDeviceUnit unit);
    /**<
     * @brief Terminate the current join.
     *
     * The current join will be terminated if possible.
     *
     * This may be necessary if higher priority actions need to happen, rather than
     * wait several seconds for the join to complete.
     *
     * If the join is already done, this is a no-op.
     *
     * @param[in] unit: specifies the driver unit instance
     * @note Non-blocking. Returns immediately. The JoinStart callback will indicate when done.
     */

    bool (*GetApInfo)(LTDeviceUnit unit, LTWiFi_ApInfo *ap);
    /**<
     * @brief Get information about the connected AP.
     *
     * If the AP is no longer connected, the information is still valid from
     * when it was last connected (allows getting info about prior connection.)
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[out] ap: returns the AP information
     * @return TRUE when AP is connected, otherwise FALSE
     */

    bool (*GetLinkState)(LTDeviceUnit unit);
    /**<
     * @brief Get the link state.
     *
     * Return true if STA is connected to the AP.
     *
     * @param[in] unit: specifies the driver unit instance
     * @return TRUE when AP is connected
     */

    void (*Disconnect)(LTDeviceUnit unit);
    /**<
     * @brief Disconnect from the AP.
     *
     * If AP is linked, terminate the link.
     *
     * If AP JOIN is in progress, terminate the join operation.
     *
     * @param[in] unit: specifies the driver unit instance
     * @note Non-blocking. Returns immediately. Call GetLinkState to determine status.
     */

    bool (*ApStart)(LTDeviceUnit unit, LTWiFi_ApInfo *ap_spec /* callback? */);
    /**<
     * @brief Startup a soft AP.
     *
     * Brings up the soft AP with the given SSID, password, security, and
     * optional channel (if STA isn't connected).
     *
     * Be sure to clear or properly set all fields of the ap_spec as those
     * fields may be used for future features. (e.g. options)
     *
     * To disable the soft AP, set the ap_spec to NULL.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[out] ap_spec: specifies the parameters and options of the SoftAP.
     * @return TRUE if successful
     */

    bool (*ReceiveFrame)(LTDeviceUnit unit, LTWiFi_FrameRxCallback *pCallback, void *pClientData);
    /**<
     * @brief Receive WiFi frames
     *
     * Operates by setting-up a received frame processing callback.
     *
     * Intended to be used by networking stack or other MAC-layer processing scheme.
     *
     * pCallback gets executed in WiFi RX processing context, and depends on
     * driver archirecture, may even be a top half IRQ.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[in] pCallback: specifies the callback function
     * @param]in] pClientData: passed to the callback (upper-layer processing context)
     * @return true when callback is set successfully
     */

    bool (*TransmitFrames)(LTDeviceUnit unit, const LTBufferChain *bufferChain);
    /**<
     * @brief Transmit one or more WiFi frames
     *
     * Pass one or more frames for transmission by the WiFi device.
     *
     * @param[in] unit: specifies the driver unit instance
     * @param[in] bufferChain: SG buffers to send
     * @return true when frames are successfully queued for transmission
     */

    int  (*IwPriv)(int argc, const char **argv, void *priv);
    /**<
     * @brief Run a vendor specific commands like iwpriv
     *
     * @param[in] argc: number of arguments.
     * @param[in] argv: arguments
     * @return 0 when the command is run successfully
     */

    LTWiFi_DisconnectReason (*GetDiscReasonCode)(LTDeviceUnit unit);
    /**<
    * @brief Get the disconnect reason code
    * @param[in] unit: specifies the driver unit instance
    * @return LT disconnect reason code
    */

    LTWiFi_JoinStatus (*GetLastJoinStatus)(LTDeviceUnit unit);
    /**<
    * @brief Get Wifi last updated join status
    *
    * @return Wifi last updated join status
    */

    void (*GetWiFiDrvTxQStats)(LTDeviceUnit unit, u8 ac, LTWiFi_DrvTxQStats *stats);
    /**<
     * @brief Get Wifi driver tx queue stats
     *
     * @param[in] ac: Access category from 0 to 3. 0:V0, 1:VI, 2:BE, 3:BK
     * @param[out] stats: Stats result
     */

    // !!! Needs doc if actually added
    void (*SniffFrames)(LTDeviceUnit unit, LTWiFi_SniffCallback callback);  ///< DOCUMENTATION_NEEDED
    int  (*SendFrame)(LTDeviceUnit unit, LTWiFi_Frame *frame);              ///< DOCUMENTATION_NEEDED
};
LT_EXTERN_C_END
#endif /* LTDRIVERWIFI_H */
