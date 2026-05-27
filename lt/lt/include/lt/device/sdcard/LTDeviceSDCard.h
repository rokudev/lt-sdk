/*******************************************************************************
 * <lt/device/sdcard/LTDeviceSDCard.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_DEVICE_SDCARD_LTDEVICESDCARD_H
#define ROKU_LT_INCLUDE_LT_DEVICE_SDCARD_LTDEVICESDCARD_H

#include <lt/LT.h>

TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceSDCard, 1);

struct LTDeviceSDCard {
    INHERIT_DEVICE_LIBRARY_BASE
};

typedef enum LTDeviceSDCard_Format {
    LTDeviceSDCard_Format_UNKNOWN,
    LTDeviceSDCard_Format_FAT32,
    LTDeviceSDCard_Format_EXFAT,
} LTDeviceSDCard_Format;

typedef struct {
    bool present;
    bool mounted;
    LTDeviceSDCard_Format fstype;
    u64 capacity; // in bytes
    u64 available; // in bytes
} LTDeviceSDCard_SDCardInfo;

typedef struct {
    LTDeviceSDCard_Format format;
} LTDeviceSDCard_FormatOptions;

typedef void (LTDeviceSDCard_OnSDCardEventProc)(LTDeviceSDCard_SDCardInfo *info, void * pClientData);

TYPEDEF_LTLIBRARY_INTERFACE(ILTSDCardDeviceUnit, 1);
struct ILTSDCardDeviceUnitApi {

    INHERIT_INTERFACE_BASE

    void (*OnSDCardEvent)(LTDeviceUnit hUnit,
                          LTDeviceSDCard_OnSDCardEventProc * pCallback,
                          void * pClientData);

    void (*NoSDCardEvent)(LTDeviceUnit hUnit,
                          LTDeviceSDCard_OnSDCardEventProc * pCallback);

    LTDeviceSDCard_SDCardInfo (*GetSDCardInfo)(LTDeviceUnit hUnit);

    bool (*Mount)(LTDeviceUnit hUnit);

    void (*Unmount)(LTDeviceUnit hUnit);

    bool (*Format)(LTDeviceUnit hUnit, const LTDeviceSDCard_FormatOptions options);
};

#endif /* #ifndef ROKU_LT_INCLUDE_LT_DEVICE_SDCARD_LTDEVICESDCARD_H */
