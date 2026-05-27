/*******************************************************************************
 * Potentiometer Unit Test
 *
 * Test LTDevicePotentiometer and <platform>DriverPotentiometer.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>

#include <lt/device/potentiometer/LTDevicePotentiometer.h>

DEFINE_LTLOG_SECTION("unitest.Potentiometer");

/*******************************************************************************
 * libraries
*******************************************************************************/
LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(UnitTestLTDevicePotentiometer, (LTDevicePotentiometer, LTDevicePotentiometer));

/*******************************************************************************
 * Consts
*******************************************************************************/

/*******************************************************************************
 * Banners
*******************************************************************************/
static const char * kFailedBanner                       = {
    " _________________________________________________ \n"
    "/       _________    ______    __________          \n"
    "/      / ____/   |  /  _/ /   / ____/ __ \\        \n"
    "/     / /_  / /| |  / // /   / __/ / / / /         \n"
    "/    / __/ / ___ |_/ // /___/ /___/ /_/ /          \n"
    "/   /_/   /_/  |_/___/_____/_____/_____/           \n"
    " _________________________________________________ \n"
};
static const char * kSuccessBanner                      = {
    " __________________________________________________\n"
    "/      _____ __  ______________________________    \n"
    "/     / ___// / / / ____/ ____/ ____/ ___/ ___/    \n"
    "/     \\__ \\/ / / / /   / /   / __/  \\__ \\__ \\ \n"
    "/    ___/ / /_/ / /___/ /___/ /___ ___/ /__/ /     \n"
    "/   /____/\\____/\\____/\\____/_____//____/____/   \n"
    " __________________________________________________\n"
};

/*******************************************************************************
 * Initialize the unit test
*******************************************************************************/
static bool UnitTestLTDevicePotentiometerImpl_LibInit(void) {
    return true;
}

/*******************************************************************************
 * Finalize the unit test
*******************************************************************************/
static void UnitTestLTDevicePotentiometerImpl_LibFini(void) {
}

/*******************************************************************************
 * Unit test run function
*******************************************************************************/
static int UnitTestLTDevicePotentiometerImpl_Run(int argc, char const ** argv) {
    u32 nNumDeviceUnits = LT_GetLTDevicePotentiometer()->GetNumDeviceUnits();
    u32 failedDevices = 0;
    if (argc <= 2) {
        LTLOG("cmd.line.start", "***************************************************************");
        LTLOG("cmd.line.usage", "Usage:");
        LTLOG("cmd.line.help", "    UnitTestLTDevicePotentiometer [List of DPOT hex values for each device unit]");
        LTLOG("cmd.line.help", "    OR");
        LTLOG("cmd.line.help", "    UnitTestLTDevicePotentiometer read");
        LTLOG("cmd.line.deviceunits", "    (there are %d device units)", (int)nNumDeviceUnits);
        LTLOG("cmd.line.end", "  ***************************************************************");
    } else {
        bool bRead = (lt_strcasecmp(argv[2], "read") == 0);
        for (u32 i = 0; i < nNumDeviceUnits; ++i) {
            int argIndex = i + 2;
            LTDeviceUnit hDeviceUnit = LT_GetLTDevicePotentiometer()->CreateDeviceUnitHandle(i);
            ILTDriverPotentiometer * iPotentiometer = NULL;
            if (hDeviceUnit == 0) {
                LTLOG_REDALERT("fail.device.unit.create", "Failed to created device unit for index %d", (int)i);
            } else {
                iPotentiometer = lt_gethandleinterface(ILTDriverPotentiometer, hDeviceUnit);
                if (iPotentiometer == NULL) {
                    LTLOG_REDALERT("fail.load.interface.potentiometer", "failed to get the potentiometer driver interface");
                }
            }
            if (iPotentiometer != NULL) {
                if (bRead) {
                    u32 value = iPotentiometer->GetDPOTValue(hDeviceUnit);
                    LTLOG("dpot.value", "DPOT value for Device %lu = 0x%lx", LT_Pu32(i), LT_Pu32(value));
                } else {
                    if (argc > argIndex) {
                        u32 value = lt_strtou32(argv[argIndex], NULL, 16);
                        LTLOG("port.val", "Setting DeviceUnit %d DPOT to 0x%lx", (int)i, LT_Pu32(value));
                        LTDevicePotentiometerResult res = kLTDevicePotentiometer_DeviceBusy;
                        for (u8 x = 0; x < 3 && res == kLTDevicePotentiometer_DeviceBusy; ++x) {
                            res = iPotentiometer->SetDPOTValue(hDeviceUnit, value);
                            switch (res) {
                                case kLTDevicePotentiometer_DeviceBusy:
                                    LTLOG_REDALERT("fail.DPOT.set.busy", "Device is busy; try again");
                                    failedDevices |= (0x01 << i);
                                    break;
                                case kLTDevicePotentiometer_InvalidHandle:
                                    LTLOG_REDALERT("fail.DPOT.set.handle", "Invalid Device handle");
                                    failedDevices |= (0x01 << i);
                                    break;
                                case kLTDevicePotentiometer_ReadError:
                                    LTLOG_REDALERT("fail.DPOT.set.read", "Read error");
                                    failedDevices |= (0x01 << i);
                                    break;
                                case kLTDevicePotentiometer_WriteError:
                                    LTLOG_REDALERT("fail.DPOT.set.write", "Write error");
                                    failedDevices |= (0x01 << i);
                                    break;
                                case kLTDevicePotentiometer_Success:
                                default:
                                    break;
                            }
                        }
                    }
                }
                if (hDeviceUnit != 0) {
                    iPotentiometer->Destroy(hDeviceUnit);
                }
            }
        }

        if (failedDevices != 0) {
            LT_GetCore()->ConsolePutString(kFailedBanner);
            LTLOG("devices.fail", "The following devices failed to be set");
            for (u32 i = 0; i < nNumDeviceUnits; ++i) {
                if (failedDevices & (0x01 << i)) {
                    LTLOG("device.fail", "  Device index %lu", LT_Pu32(i));
                }
            }
        } else {
            LT_GetCore()->ConsolePutString(kSuccessBanner);
        }
    }

    // returns a bitmask for failed device where failed devices have a value of 1 in their bit position if failed
    return failedDevices;
}

/*******************************************************************************
 * Interface defs
*******************************************************************************/
typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDevicePotentiometer, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDevicePotentiometer, UnitTestLTDevicePotentiometerImpl_Run, 1024) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  21-Jan-22   vitellius   created
 */
