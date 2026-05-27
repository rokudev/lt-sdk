/******************************************************************************
 * source/lt/driver/ota/proxy/LTOtaProxy.h - LTDriverOta proxy protocol
 *
 * Common definitions for the LTDriverOta proxy client/server
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_OTA_PROXY_LTOTAPROXY_H
#define ROKU_LT_SOURCE_LT_OTA_PROXY_LTOTAPROXY_H

typedef enum {
    kLTOtaProxy_RequestType_GetVersion,
    kLTOtaProxy_RequestType_InitDownload,
    kLTOtaProxy_RequestType_SaveBlock,
    kLTOtaProxy_RequestType_Finalize,

    kLTOtaProxy_ResponseType_GetVersion,
    kLTOtaProxy_ResponseType_InitDownload,
    kLTOtaProxy_ResponseType_SaveBlock,
    kLTOtaProxy_ResponseType_Finalize,
} LTOtaProxy_PacketType;

#endif /* #ifndef ROKU_LT_SOURCE_LT_OTA_PROXY_LTOTAPROXY_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  16-Jan-25   trajan      created
 */
