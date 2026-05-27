/*******************************************************************************
 * <lt/system/usbconsole/LTSystemUsbConsole.h>   LTSystemUsbConsole lib root interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_USB_LTSYSTEMUSBCONSOLE_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_USB_LTSYSTEMUSBCONSOLE_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/**
 * @struct LTSystemUsbConsole
 * @brief The API for enabling USB console
 *
 * This LTObject provides an interface to enable and disable USB console functionality.
 * When enabled, it redirects console I/O to a USB CDC device, allowing interaction
 * with the system via USB.
 */

typedef_LTObject(LTSystemUsbConsole, 1) {}

LTOBJECT_API;

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_SYSTEM_USB_LTSYSTEMUSBCONSOLE_H */