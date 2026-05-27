/*******************************************************************************
 *
 * TestWiFiDisconnect
 *
 * Purpose: cause the STA to disconnect from the AP.
 *
 * Use -f for fake disconnect which simulates AP disconnect outside of the
 * WiFi state machine. This is a good test for automatic rejoin.
 *
 * Otherwise, just do a normal disconnect. This tells the state machine to
 * not attempt to rejoin.
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/wifi/LTDeviceWiFi.h>

static int TestWiFiDisconnect_Main(int argc, const char **argv) {
    bool unexpected = false;
    if (argc > 2) {
        if (lt_strcmp(argv[2], "-u") == 0) unexpected = true;
        if (lt_strcmp(argv[2], "?") == 0) {
            lt_consoleprint("Use -u for unexpected disconnect\n");
            return 0;
        }
    }
    LTDeviceWiFi *iWifi = lt_openlibrary(LTDeviceWiFi);
    if (!iWifi) return -1;
    lt_consoleprint("AP disconnect %s\n", unexpected ? "(unexpected)" : "");
    if (unexpected) iWifi->SetOption("disconnect", 0);
    else iWifi->Disconnect();
    // lt_closelibrary(iWifi); leave it open in case this is the only instance
    return 0;
}

define_LTLIBRARY_APPLICATION(TestWiFiDisconnect, 1, 1024);
