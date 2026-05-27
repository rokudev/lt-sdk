/******************************************************************************
 * <lt/device/mipicsi/LTDeviceMipiCsi.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

/**
 * @defgroup ltdevice_mipicsi LTDeviceMipiCsi
 * @ingroup ltdevice
 * @{
 *
 * @brief LT Device Library for MIPI-CSI devices.
 */

#ifndef LT_INCLUDE_LT_DEVICE_MIPICSI_LTDEVICEMIPICSI_H
#define LT_INCLUDE_LT_DEVICE_MIPICSI_LTDEVICEMIPICSI_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN


TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceMipiCsi, 1);

/**
 * @brief The API for controlling MIPI-CSI devices.
 */
struct LTDeviceMipiCsi {

    INHERIT_DEVICE_LIBRARY_BASE

    u32  (* GetBusIndexFromName)(const char *busName);
        /**< Finds MIPI-CSI bus device unit index for corresponding to the given name.
         *   @param[in]  busName The name of the desired MIPI-CSI bus device unit.
         *   @return     the device unit index if found else LT_U32_MAX. */

    void (* EnableExtclk)(LTDeviceUnit hUnit, bool enable);
        /**< Enables or disables MIPI-CSI EXTCLK output to image sensor.
         *   @param[in]  hUnit The device unit handle.
         *   @param[in]  enable Whether to enable or disable the clock. */

    void (* EnableOutput)(LTDeviceUnit hUnit, bool enable);
        /**< Enables or disables MIPI-CSI stream output.
         *   @param[in]  hUnit The device unit handle.
         *   @param[in]  enable Whether to enable or disable the output. */

};

#ifndef DOXY_SKIP // [
typedef_LTLIBRARY_INTERFACE(ILTDriverMipiCsi, 1) {
    /**< MIPI-CSI Driver private interface - only to be used internally by LTDeviceMipiCsi */

    u32  (* GetBusIndexFromName)(const char *busName);
        /**< Finds MIPI-CSI bus device unit index for corresponding to the given name.
         *   @param[in]  busName The name of the desired MIPI-CSI bus device unit.
         *   @return     the device unit index if found else LT_U32_MAX. */

    void (* EnableExtclk)(LTDeviceUnit hUnit, bool enable);
        /**< Enables or disables MIPI-CSI EXTCLK output to image sensor.
         *   @param[in]  hUnit The device unit handle.
         *   @param[in]  enable Whether to enable or disable the clock. */

    void (* EnableOutput)(LTDeviceUnit hUnit, bool enable);
        /**< Enables or disables MIPI-CSI stream output.
         *   @param[in]  hUnit The device unit handle.
         *   @param[in]  enable Whether to enable or disable the output. */

} LTLIBRARY_INTERFACE;
#endif // DOXY_SKIP ]

LT_EXTERN_C_END
#endif // #ifndef LT_INCLUDE_LT_DEVICE_MIPICSI_LTDEVICEMIPICSI_H

/** @} */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  17-Jan-24   trajan      created
 */
