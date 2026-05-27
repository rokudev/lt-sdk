/*******************************************************************************
 * example/parsejson/ExampleParseJson.c example of how to parse json in LT
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/utility/jsonparser/LTUtilityJsonParser.h>
#include <lt/core/LTCore.h>

/*____________________++____________________
  ExampleParser run on sample json string */
#define JSONIFY(x...) #x
static const char * s_deviceJson = JSONIFY(
    { "config": { "device": {
                         "0": {
                           "device class": "LTDevicePushbutton",
                                 "driver": { "0": {
                                               "name": "Esp32DriverPushButton",
                                               "unit": { "0": { "device class": "LTDevicePins",
                                                                 "device unit": 2,
                                                                        "name": "sidebutton"
                                            }     }    }      }
                              },
                         "1": {
                           "device class": "LTDevicePins",
                                 "driver": { "0": {
                                               "name": "Esp32DriverPins",
                                               "unit": { "0": { "gpio": 19, "name": "led"    },
                                                         "1": { "gpio": 16, "name": "relay"  },
                                                         "2": { "gpio": 15, "name": "button" }
    }           }           } }            }       }   }
);

/*________________________________________
  ExampleParseJson Application Main() */
static int
ExampleParseJson_Main(int argc, const char **argv) { LT_UNUSED(argc); LT_UNUSED(argv);

    LTCore              *core   = LT_GetCore();
    LTUtilityJsonParser *parser = lt_createobject(LTUtilityJsonParser);

    if (parser) {
        /* example of using the parser to validate json */
        core->ConsolePrint("__________________\nThe example json string:\n  ");
        core->ConsolePutString(s_deviceJson);
        core->ConsolePrint("\nIS %s JSON!!!\n",
           parser->API->ValidateJson(parser, s_deviceJson) ? "VALID" : "NOT VALID");

        /* example of using the parser to get key values from the json */
        enum { keyBuffSize = 64 };
        char *key = lt_malloc(keyBuffSize);
        LTUtilityJsonParser_Value value;
        core->ConsolePrint("__________________\nTExampleParseJson\nEnumerating devices:\n");
        if (key) {
            for (int i = 0; i < 99; i++) {
                lt_snprintf(key, keyBuffSize, "/config/device/%d/device class", i);
                parser->API->GetValue(parser, s_deviceJson, key, &value);
                if (value.type == kLTUtilityJsonParser_ValueType_String) {
                    core->ConsolePrint("  Device %d: %s", i, value.string);
                    lt_snprintf(key, keyBuffSize, "/config/device/%d/driver/0/name", i);
                    parser->API->GetValue(parser, s_deviceJson, key, &value);
                    if (value.type == kLTUtilityJsonParser_ValueType_String) core->ConsolePrint(" uses driver %s\n", value.string);
                    else core->ConsolePrint("\n");
                }
                else break;
            }
            lt_free(key);
            core->ConsolePrint("Enumeration Complete, Captain.\n");
        }
        lt_destroyobject(parser);
    }

    return 0;
}

define_LTLIBRARY_APPLICATION(ExampleParseJson, 1, 0); /* (appName, version, stackSize 0=default) */

/* __________________
 * Application Output
 *   LT> ltrun ExampleParseJson
 *   ________________
 *   ExampleParseJson
 *   The example json string:
 *     { "config": { "device": { "0": { "device class": "LTDevicePushbutton", "driver": { "0": { "name": "Esp32DriverPushButton", "unit": { "0": { "device class": "LTDevicePins", "device unit": 2, "name": "sidebutton" } } } } }, "1": { "device class": "LTDevicePins", "driver": { "0": { "name": "Esp32DriverPins", "unit": { "0": { "gpio": 19, "name": "led" }, "1": { "gpio": 16, "name": "relay" }, "2": { "gpio": 15, "name": "button" } } } } } } } }
 *   IS VALID JSON!!!
 *   ________________
 *   ExampleParseJson
 *   Enumerating devices:
 *     Device 0: LTDevicePushbutton uses driver Esp32DriverPushButton
 *     Device 1: LTDevicePins uses driver Esp32DriverPins
 *   Enumeration Complete, Captain.
 *   ltrun: ExampleParseJson exited with code 0
 *   LT>
 */

 /*******************************************************************************
 *  LOG
 *******************************************************************************
 *  16-Jan-23   augustus    created
 */
