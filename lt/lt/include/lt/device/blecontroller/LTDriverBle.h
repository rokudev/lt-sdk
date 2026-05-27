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

#ifndef LTDRIVERBLE_H
#define LTDRIVERBLE_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN
/*******************************************************************************
 *
 * BLE Driver Abstract Interface
 * ------------------------------
 * These APIs will be called by the BLE controller driver when it needs to
 * call into the BLE controller which is integrated with the WiFi Driver.
 * The Integrated WiFi Driver will implement these APIs.
 *
 ******************************************************************************/
TYPEDEF_LTLIBRARY_INTERFACE(LTDriverBle, 1);
typedef void (*BleControllerRxCallback_t)(u8 *data);
/** DOCUMENTATION_NEEDED */
struct LTDriverBleApi {
    INHERIT_INTERFACE_BASE
    /**<
     * @brief API to start the BLE controller.
     * 
     * Pass a data buffer in case the controller driver needs
     * to store some context for the application.
     * This APi will be used by those platforms which do not have a separate BLE controller.
     * 
     * @param[in] data:: data buffer
     * @return 0 on success, 1 on failure
     * 
     */
    int (*BleControllerStart)(u8 *data);

    /**<
     * @brief API to stop the BLE controller.
     * 
     * Pass a data buffer in case the controller driver needs
     * to store some context for the application.
     * This API will be used by those platforms which do not have a separate BLE controller.
     * 
     * @param[in] data:: data buffer
     * @return 0 on success, 1 on failure
     * 
     */
    int (*BleControllerStop)(u8 *data);

    /**<
     * @brief API to transmit the BLE data to the BLE controller.
     * 
     * API to transmit data to BLE HCI data to controller. This API will
     * massage the buffer based on the driver requirements and send it to the BLE controller.
     */
    int (*BleControllerSend)(u8 *data, u32 len);

    /**<
     * @brief API to register the BLE controller callback.
     * 
     * API to register BLE Controller callback
     * for RX Packet proicessing
     */
    bool (*BleControllerRegisterRxCallbacks)(BleControllerRxCallback_t rx_cb);
};

LT_EXTERN_C_END
#endif /* LTDRIVERBLE_H */