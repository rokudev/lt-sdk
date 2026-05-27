/*******************************************************************************
 *
 * UnitTestLTDeviceBleControllerImpl.c
 * -----------------------------------
 *
 * See LTDeviceBleController.h for interface documentation.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/device/blecontroller/LTDeviceBleController.h>

#include <tilt/JiltEngine.h>

/** Standard LT Interfaces ****************************************************/

static JiltEngine             * s_engine;
static Tilt                   * s_tilt;
static LTDeviceBleController  * s_DevBleController  = NULL;
static LTDeviceUnit             s_hDevBleController = 0;
static ILTBleController       * s_IControllerBle     = NULL;
static u8 hciCmdBuf[128];

/* Device address */
typedef u8 bd_addr_t[BD_ADDR_LEN];

static u16 UnitTestLTDeviceBleCmdReset(u8 * pBuf) {
    UINT8_TO_STREAM(pBuf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(pBuf, HCI_RESET);
    UINT8_TO_STREAM(pBuf, 0);
    return HCI_H4_CMD_PREAMBLE_SIZE;
}

static u16 UnitTestLTDeviceBleCmdSetEvtMask(u8 * pBuf, u8 * pEvtMask) {
    UINT8_TO_STREAM(pBuf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(pBuf, HCI_SET_EVT_MASK);
    UINT8_TO_STREAM(pBuf, HCIC_PARAM_SIZE_SET_EVENT_MASK);
    lt_memcpy(pBuf, pEvtMask, HCIC_PARAM_SIZE_SET_EVENT_MASK);
    return HCI_H4_CMD_PREAMBLE_SIZE + HCIC_PARAM_SIZE_SET_EVENT_MASK;
}

static u16 UnitTestLTDeviceBleCmdBleSetAdvParam(u8 * pBuf, u16 advIntMin, u16 advIntMax,
                                     u8 advType, u8 ownAddrType,
                                     u8 dirAddrType, bd_addr_t directBda,
                                     u8 channelMap, u8 advFilterPolicy) {
    UINT8_TO_STREAM(pBuf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(pBuf, HCI_BLE_WRITE_ADV_PARAMS);
    UINT8_TO_STREAM(pBuf, HCIC_PARAM_SIZE_BLE_WRITE_ADV_PARAMS );

    UINT16_TO_STREAM(pBuf, advIntMin);
    UINT16_TO_STREAM(pBuf, advIntMax);
    UINT8_TO_STREAM(pBuf, advType);
    UINT8_TO_STREAM(pBuf, ownAddrType);
    UINT8_TO_STREAM(pBuf, dirAddrType);
    BDADDR_TO_STREAM(pBuf, directBda);
    UINT8_TO_STREAM(pBuf, channelMap);
    UINT8_TO_STREAM(pBuf, advFilterPolicy);
    return HCI_H4_CMD_PREAMBLE_SIZE + HCIC_PARAM_SIZE_BLE_WRITE_ADV_PARAMS;
}

static void UnitTestLTDeviceBleCmdSendReset(void) {
    u16 sz = UnitTestLTDeviceBleCmdReset(hciCmdBuf);
    s_IControllerBle->Send(hciCmdBuf, sz);
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(50));
}

static void UnitTestLTDeviceBleCmdSendSetEvtMask(void) {
    /* Set bit 61 in event mask to enable LE Meta events. */
    u8  evtMask[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20};
    u16 sz         = UnitTestLTDeviceBleCmdSetEvtMask(hciCmdBuf, evtMask);
    s_IControllerBle->Send(hciCmdBuf, sz);
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(50));
}

static void UnitTestLTDeviceBleCmdSendBleSetAdvParam(void) {
    /* Minimum and maximum Advertising interval are set in terms of slots. Each slot is of 625 microseconds. */
    u16 advIntvMin = 0x100;
    u16 advIntvMax = 0x100;

    /* Connectable undirected advertising (ADV_IND). */
    u8 advType = 0;

    /* Own address is public address. */
    u8 ownAddrType = 0;

    /* Public Device Address */
    u8 peerAddrType = 0;
    u8 peerAddr[6] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85};

    /* Channel 37, 38 and 39 for advertising. */
    u8 advChnMap = 0x07;

    /* Process scan and connection requests from all devices (i.e., the White List is not in use). */
    u8 advFilterPolicy = 0;

    u16 sz = UnitTestLTDeviceBleCmdBleSetAdvParam(hciCmdBuf,
                  advIntvMin,
                  advIntvMax,
                  advType,
                  ownAddrType,
                  peerAddrType,
                  peerAddr,
                  advChnMap,
                  advFilterPolicy);

    s_IControllerBle->Send(hciCmdBuf, sz);
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(50));
}

static u16 UnitTestLTDeviceBleCmdBleSetAdvData(u8 * pBuf, u8 dataLen, const u8 * pData) {
    UINT8_TO_STREAM(pBuf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(pBuf, HCI_BLE_WRITE_ADV_DATA);
    UINT8_TO_STREAM(pBuf, HCIC_PARAM_SIZE_BLE_WRITE_ADV_DATA + 1);

    lt_memset(pBuf, 0, HCIC_PARAM_SIZE_BLE_WRITE_ADV_DATA);

    if (pData != NULL && dataLen > 0) {
        if (dataLen > HCIC_PARAM_SIZE_BLE_WRITE_ADV_DATA) {
            dataLen = HCIC_PARAM_SIZE_BLE_WRITE_ADV_DATA;
        }

        UINT8_TO_STREAM(pBuf, dataLen);
        lt_memcpy(pBuf, pData, dataLen);
    }
    return HCI_H4_CMD_PREAMBLE_SIZE + HCIC_PARAM_SIZE_BLE_WRITE_ADV_DATA + 1;
}

static u16 UnitTestLTDeviceBleCmdBleSetAdvEnable(u8 * pBuf, u8 advEnable) {
    UINT8_TO_STREAM(pBuf, H4_TYPE_COMMAND);
    UINT16_TO_STREAM(pBuf, HCI_BLE_WRITE_ADV_ENABLE);
    UINT8_TO_STREAM(pBuf, HCIC_PARAM_SIZE_WRITE_ADV_ENABLE);
    UINT8_TO_STREAM(pBuf, advEnable);
    return HCI_H4_CMD_PREAMBLE_SIZE + HCIC_PARAM_SIZE_WRITE_ADV_ENABLE;
}

static void UnitTestLTDeviceBleCmdSendBleSetAdvData(void) {
    char advName[] = "LT_CONTROLLER";
    u8 nameLen     = (u8)lt_strlen(advName);
    u8 advData[31] = {0x02, 0x01, 0x06, 0x0, 0x09};
    u8 advDataLen;

    advData[3] = nameLen + 1;
    for (int i = 0; i < nameLen; i++) {
        advData[5 + i] = (u8)advName[i];
    }
    advDataLen = 5 + nameLen;

    u16 sz = UnitTestLTDeviceBleCmdBleSetAdvData(hciCmdBuf, advDataLen, (u8 *)advData);
    TILT_INFO(s_tilt, "Starting BLE advertising with name \"%s\"", advName);
    s_IControllerBle->Send(hciCmdBuf, sz);
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(50));
}

static void UnitTestLTDeviceBleCmdSendBleAdvStart(void) {
    u16 sz = UnitTestLTDeviceBleCmdBleSetAdvEnable(hciCmdBuf, 1);
    TILT_INFO(s_tilt, "BLE Advertising started..");
    s_IControllerBle->Send(hciCmdBuf, sz);
    lt_getlibraryinterface(ILTThread, LT_GetCore())->Sleep(LTTime_Milliseconds(50));
}

static void ControllerSendReady(void) {}
static int HostRecvReady(const u8 *data, u16 len) {
    LT_UNUSED(data);
    LT_UNUSED(len);
    TILT_INFO(s_tilt, "BLE packet received..");
    return 0;
}

static const LTBleController_VhciHostCallback VhciHostCallback = {
    .NotifySendReady = &ControllerSendReady,
    .NotifyRecvReady = &HostRecvReady,
};

static void StartBleControllerDeviceTest(Tilt * tilt) {
    s_tilt = tilt;
    UnitTestLTDeviceBleCmdSendReset();
    UnitTestLTDeviceBleCmdSendSetEvtMask();
    UnitTestLTDeviceBleCmdSendBleSetAdvParam();
    UnitTestLTDeviceBleCmdSendBleSetAdvData();
    UnitTestLTDeviceBleCmdSendBleAdvStart();
}

static void BeforeAllTests(Tilt * tilt) {
    s_DevBleController = lt_openlibrary(LTDeviceBleController);
    TILT_EXPECT_TRUE(tilt, s_DevBleController != NULL, "Cannot open LTDeviceBleController");
    if (!s_DevBleController) return;

    u32 numDev = s_DevBleController->GetNumDeviceUnits();
    s_hDevBleController = s_DevBleController->CreateDeviceUnitHandle(numDev - 1);
    TILT_EXPECT_TRUE(tilt, s_hDevBleController != 0, "Cannot create s_hDevBleController");
    if (!s_hDevBleController) return;

    s_IControllerBle = lt_gethandleinterface(ILTBleController, s_hDevBleController);
    TILT_EXPECT_TRUE(tilt, s_IControllerBle != NULL, "Cannot get s_IControllerBle");
    if (!s_IControllerBle) return;

    s_IControllerBle->Enable(true);
    s_IControllerBle->RegisterCallbacks(&VhciHostCallback);
}

static void AfterAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    if (s_IControllerBle) {
        s_IControllerBle->Enable(false);
        s_IControllerBle = NULL;
    }
    lt_destroyhandle(s_hDevBleController);
    s_hDevBleController = 0;
    lt_closelibrary(s_DevBleController);
    s_DevBleController = NULL;
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests
};

static const TiltEngineTest s_tests[] = {
    { StartBleControllerDeviceTest, "bleprphr", "Test LTDeviceBleController interface - start peripheral mode", 0 },
};

static int UnitTestLTDeviceBleControllerImplImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTDeviceBleControllerImplImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceBleControllerImplImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceBleControllerImpl, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceBleControllerImpl, UnitTestLTDeviceBleControllerImplImpl_Run, 1536) LTLIBRARY_DEFINITION;
