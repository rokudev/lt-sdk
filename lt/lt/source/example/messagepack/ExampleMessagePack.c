/*******************************************************************************
 *
 * MessagePack Example
 *
 * This is an example of how to write out and read back example settings
 * as message pack data.
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/utility/messagepack/LTUtilityMessagePack.h>

#define P LT_GetCore()->ConsolePrint

static LTUtilityMessagePack *s_mpLib;

enum ExampleSettings {
    SettingsWifi             = 3,  // Key used for WiFi settings in top map

    SettingsWifi_Ssid        = 1,  // Keys used for wifi values
    SettingsWifi_Pass        = 2,
    SettingsWifi_Channel     = 3,
    SettingsWifi_EnableJoin  = 4,
};

static void ExampleSaveWifiSettings(LTMessagePack_Obj *mp) {

    s_mpLib->PutMap(mp, 1); // Top level settings map:

    // Wifi settings are a nested map:
    s_mpLib->PutIntU32(mp, SettingsWifi);
    s_mpLib->PutMap(mp, 4);

    // Output Wifi settings key-values:
    s_mpLib->PutIntU32(mp, SettingsWifi_Ssid);
    s_mpLib->PutCString(mp, "My-AP");

    s_mpLib->PutIntU32(mp, SettingsWifi_Pass);
    s_mpLib->PutCString(mp, "password");

    s_mpLib->PutIntU32(mp, SettingsWifi_Channel);
    s_mpLib->PutIntU32(mp, 6);

    s_mpLib->PutIntU32(mp, SettingsWifi_EnableJoin);
    s_mpLib->PutBoolean(mp, true);

    P("length of SettingsWifi: %lu\n", LT_Ps32(s_mpLib->GetPosition(mp)));
}

static bool ExampleLoadWifiSettings(LTMessagePack_Obj *mp) {
    LTMessagePack_Value settingMap;
    LTMessagePack_Value value;

    // Verify top level is the settings map:
    if (s_mpLib->GetValue(mp, &settingMap) != LTMessagePack_Type_Map) return false;
    P("General settings map length: %ld\n", LT_Ps32(settingMap.size));

    // Find the wifi settings key-value in the map:
    if (!s_mpLib->FindInteger(mp, &settingMap, SettingsWifi)) return false;

    // Now we have the wifi settings map as the current value:
    if (s_mpLib->GetValue(mp, &value) != LTMessagePack_Type_Map) return false;
    P("Wifi settings map length: %ld\n", LT_Ps32(value.size));

    // Walk thru wifi key-values in encoded order and handle each one:
    for (u8 n = value.size; n > 0; n--) {
        if (s_mpLib->GetValue(mp, &value) != LTMessagePack_Type_Integer) return false;
        switch (value.integer) {
            case SettingsWifi_Ssid:
                if (s_mpLib->GetValue(mp, &value) != LTMessagePack_Type_String) return false;
                P("Wifi SSID: %s\n", value.data);
                break;
            case SettingsWifi_Pass:
                if (s_mpLib->GetValue(mp, &value) != LTMessagePack_Type_String) return false;
                P("Wifi password: %s\n", value.data);
                break;
            case SettingsWifi_Channel:
                if (s_mpLib->GetValue(mp, &value) != LTMessagePack_Type_Integer) return false;
                P("Wifi channel: %ld\n", LT_Ps32(value.integer));
                break;
            case SettingsWifi_EnableJoin:
                if (s_mpLib->GetValue(mp, &value) != LTMessagePack_Type_Boolean) return false;
                P("Wifi rejoin: %s\n", value.boolean ? "true" : "false");
                break;
            default:
                return false;
        }
    }
    return true;
}

static int ExampleMessagePack_Main(int argc, const char **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    LTMessagePack_Obj mp;

    s_mpLib = lt_openlibrary(LTUtilityMessagePack);
    if (!s_mpLib) {
        P("Cannot open LTUtilityMessagePack\n");
        return -1;
    }

    if (!s_mpLib->Init(&mp, 0, 300))  {
        P("Init failed\n");
        lt_closelibrary(s_mpLib);
        return -1;
    }

    ExampleSaveWifiSettings(&mp);

    s_mpLib->SetPosition(&mp, 0); // rewind to head

    if (!ExampleLoadWifiSettings(&mp)) {
        P("Malformed message near: %lu\n", LT_Pu32(s_mpLib->GetPosition(&mp)));
    }

    lt_closelibrary(s_mpLib);
    s_mpLib = NULL;
    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleMessagePack, 1, 1024);
