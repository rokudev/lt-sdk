/*******************************************************************************
 *
 * UnitTestLTDeviceBleImpl.c
 * -----------------------------------
 *
 * See LTDeviceBle.h for interface documentation.
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
#include <lt/device/ble/LTDeviceBle.h>
#include <lt/system/crashdump/LTSystemCrashdump.h>

#include <tilt/JiltEngine.h>

/******************************************************************************
 * Static Definitions
 ******************************************************************************/

typedef struct TestDataStruct {
    u8 type;
    u16 len;
    u8 idx;
    u8 payload[0];
} __attribute__((packed)) TestDataStruct;

#define LT_BLE_IOT_SERVICE_UUID128      0x27,0x17,0x43,0x83,0x8f,0x6b,0xc4,0x4c,0xc1,0x12,0xaf,0x84,0x01,0x50,0x00,0x00
#define LT_BLE_BUF_MAX_LEN              (250)
#define LT_BLE_DEVICE_NAME_MAX_LEN      (21)
#define LT_BLE_MANUFACTURER_DATA_LEN    (24)
#define LT_BLE_ADV_NAME                 "Roku_ble_test"

static struct Statics {
    Tilt               *tilt;
    LTBleDeviceHandle   hDevice;
    LTBleSvcHandle      hSvc;
    LTCore             *core;
    ILTEvent           *event;
    ILTThread          *thread;
    LTDeviceBle        *devBle;
    LTSystemCrashdump  *pCrashdump;
    bool                bleStartStatus;
    LTThread            bleThread;
    u32                 bleEvent;
    void*               bleData;
    u8                  readbuf[LT_BLE_BUF_MAX_LEN];
    u8                  writebuf[LT_BLE_BUF_MAX_LEN];
    char                deviceName[LT_BLE_DEVICE_NAME_MAX_LEN];
    bool                ctrl_sync;
    bool                dont_bail_out;
} S;

static JiltEngine *s_engine;

static bool ble_contr_start_test_done = false;
static bool adv_start_test_done       = false;
static bool conection_test_done       = false;
static bool char_read_test_done       = false;
static bool write_test_done           = false;
static bool notify_test_done          = false;
static bool indicate_test_done        = false;
static bool disconnection_test_done   = false;

struct BleMfgData {
    u64 mid     : 16;    // manufacture id, 0x07a2
    u64 version : 4;     // setup protocol version
    u64 state   : 12;    // device setup state
    u64 r0      : 32;    // r0
};

#define LT_BLE_IOT_CHAR_CONTROL_UUID128   0x27,0x17,0x43,0x83,0x8f,0x6b,0xc4,0x4c,0xc1,0x12,0xaf,0x84,0x02,0x50,0x00,0x00
static  LTBleUuidAny CHARS_UUID =   { .type = kLTBleUuidType_128, .value.uuid128.value = { LT_BLE_IOT_CHAR_CONTROL_UUID128 } };
static  LTBleUuidAny SERVICE_UUID = { .type = kLTBleUuidType_128, .value.uuid128.value = { LT_BLE_IOT_SERVICE_UUID128 } };

static const LTArgsDescriptor EventArgs =  { 3, { kLTArgType_u32, kLTArgType_u32, kLTArgType_pointer } };

static void DispatchEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    if (!proc) return;
    (*(LTBleEventProc)proc)((int)LTArgs_u32At(0, args),(int)LTArgs_u32At(1, args), (void*)LTArgs_pointerAt(2, args), data);
}

static void DispatchCompleteProc(LTHandle hEvent, LTArgs *args) {
    LT_UNUSED(hEvent);
    void* bleEvent = (void*)LTArgs_pointerAt(2, args);
    if (bleEvent) {
        lt_free(bleEvent);
    }
}

static bool BleControlReadCb(LTBleChrHandle hChr) {
    LTBleChrCtx *ctx = S.core->ReserveHandlePrivateData(hChr);
    if (!ctx) return false;
    const u8 *v = ctx->uuid->value.uuid128.value;
    S.core->ReleaseHandlePrivateData(hChr, ctx);
    bool ret = (lt_memcmp(v, CHARS_UUID.value.uuid128.value, LT_UUID_BYTE_LEN) == 0);
    TILT_EXPECT_TRUE(S.tilt, ret, "Indicate Failure due to invalid chrUuid");
    S.readbuf[sizeof(S.readbuf)-1] = 0xFF;
    return ret;
}

static bool BleControlWriteCb(LTBleChrHandle hChr) {
    LT_UNUSED(hChr);
    return true;
}

static void OnChrEvent(LTBleChrHandle hChr, u32 event, void *data) {
    LT_UNUSED(event);
    LTBleChrCtx *chrCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!chrCtx) return;
    if (lt_memcmp(chrCtx->uuid->value.uuid128.value,
            CHARS_UUID.value.uuid128.value, LT_UUID_BYTE_LEN) == 0) {
        S.bleData = data;
        S.bleEvent = LT_Pu32(event);
    }  else {
        goto error;
    }
    if (event == kLTDeviceBle_Event_Read_Done) {
        char_read_test_done = true;
    } else if (event == kLTDeviceBle_Event_Write_Done) {
        TestDataStruct *recvData = (TestDataStruct*)S.writebuf;
        TILT_EXPECT_TRUE(S.tilt, recvData->type == 0x01, "Write Failure, type mismatch");
        TILT_EXPECT_TRUE(S.tilt, recvData->len == 100, "Write Failure, length mismatch");
        u8 idx = recvData->idx;
        TILT_EXPECT_TRUE(S.tilt, recvData->payload[10]==0xAB, "Write Failure, payload mismatch");
        lt_memcpy(S.readbuf, S.writebuf, recvData->len+sizeof(TestDataStruct));
        chrCtx->readLen = recvData->len+sizeof(TestDataStruct);
        TILT_EXPECT_TRUE(S.tilt, S.devBle->Indicate(S.hDevice, hChr), "Indicate Failure");
        if (idx == 3) {
            write_test_done = true;
        } else {
            write_test_done = false;
        }
    }
error:
    S.core->ReleaseHandlePrivateData(hChr, chrCtx);
}

static void OnSvcEvent(LTBleSvcHandle hSvc, u32 event, void *data) {
    LT_UNUSED(data);
    TILT_DEBUG(S.tilt, "OnSvcEvent: %lx svcHdl: %lx", LT_Pu32(event), LT_Pu32(hSvc));
}

static void OnDeviceEvent(LTBleDeviceHandle hDevice, u32 event, void *bleEvtData, void *data) {
    LTBleEventData *ble_evt_data = (LTBleEventData*)bleEvtData;
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) return;
    if (devCtx->hDev == S.hDevice) {
        S.bleData = data;
        S.bleEvent = LT_Pu32(event);
    } else {
        goto error;
    }
    do {
        if (event == kLTDeviceBle_Event_Connected) {
            if (!S.devBle->SetMaxTransmitParams(devCtx, 251, 2120)) {
                TILT_DEBUG(S.tilt, "Failed to set max transmit params");
            }
            conection_test_done = true;
            const char* dev_name = S.devBle->GetDeviceName();
            if (!dev_name || lt_strncmp(dev_name, S.deviceName, lt_strlen(S.deviceName))) {
                if (!dev_name) dev_name = "(null)";
                TILT_DEBUG(S.tilt, "Device name mismatch: %s != %s", dev_name, S.deviceName);
                goto error;
            }
            adv_start_test_done = true;
            LTBleUpdConnParams upd_conn_params;
            lt_memset(&upd_conn_params, 0x00, sizeof(LTBleUpdConnParams));
            upd_conn_params.itvl_min = 6;
            upd_conn_params.itvl_max = 7;
            upd_conn_params.latency = 0;
            upd_conn_params.supervision_timeout = 500;
            upd_conn_params.min_ce_len = 100;
            upd_conn_params.max_ce_len = 200;
            if (!S.devBle->UpdateConnectionParams(hDevice, &upd_conn_params)) {
                TILT_DEBUG(S.tilt, "Failed to update connection parameters");
            }
        } else if (event == kLTDeviceBle_Event_Disconnected) {
            disconnection_test_done = true;
            S.dont_bail_out = true;
        } else if (event == kLTDeviceBle_Event_Subscribe) {
            if(!ble_evt_data) goto error;
            if (ble_evt_data->subscribe.cur_notify == 1) {
                notify_test_done = true;
            }
            if (ble_evt_data->subscribe.cur_indicate == 1) {
                indicate_test_done = true;
            }
        }
    } while(0);

error:
    S.core->ReleaseHandlePrivateData(hDevice, devCtx);
}

/*******************************************************************************
 * Unit Tests
 *******************************************************************************/

static u16  s_ble_start_test_sleep_time = 2000;
static u16  s_connect_test_sleep_time   = 5000;
static u16  s_adv_stop_test_sleep_time  = 100;

static void StartBleDeviceTest(Tilt *tilt) {
    S.thread->Sleep(LTTime_Milliseconds(s_ble_start_test_sleep_time));
    TILT_EXPECT_TRUE(tilt, S.bleStartStatus, "Start Failure");
    ble_contr_start_test_done = (S.bleEvent == kLTDeviceBle_Event_Started);
    TILT_EXPECT_TRUE(tilt, ble_contr_start_test_done, "Start Status Failure");
}

static void AdvertiseDeviceTest(Tilt *tilt) {
    LTBleAdvFields adv_fields = {0};
    LTBleAdvFields rsp_fields = {0};
    LTBleAdvParams adv_params = {0};
    if (S.bleEvent == kLTDeviceBle_Event_Started) {
        // Starting BLE device
        u8 advMfgData[4] = {0xAB, 0xAB, 0xA0, 0xA0};
        adv_fields.flags = LT_BLE_ADV_F_DISC_GEN|LT_BLE_ADV_F_BREDR_UNSUP;
        adv_fields.name = S.deviceName;
        LTBLeUuid16 advuuids16[2];
        advuuids16[0].value = 0xBBB0;
        advuuids16[1].value = 0xBBB1;
        adv_fields.uuids16 = advuuids16;
        adv_fields.num_uuids16 = 2;
        adv_fields.mfg_data_len = sizeof(advMfgData);
        adv_fields.mfg_data = advMfgData;

        u8 resMfgData[4] = {0xff, 0xcc, 0xdd, 0xdd};
        LTBLeUuid16 resuuids16[2];
        resuuids16[0].value = 0xCCC0;
        resuuids16[1].value = 0xDDD0;
        rsp_fields.uuids16 = resuuids16;
        rsp_fields.num_uuids16 = 2;
        rsp_fields.mfg_data_len = sizeof(resMfgData);
        rsp_fields.mfg_data = resMfgData;
        adv_params.conn_mode = LT_BLE_CONN_MODE_UND;
        adv_params.disc_mode = LT_BLE_DISC_MODE_GEN;
        adv_params.duration_ms = LT_BLE_ADV_FOREVER;
        S.devBle->StartAdvertise(S.hDevice, &adv_fields, &rsp_fields, &adv_params);
    } else {
        TILT_EXPECT_TRUE(tilt, false, "Adv Status Failure");
        return;
    }
}

static void ConnectDeviceTest(Tilt *tilt) {
    LTTime start_time = S.core->GetKernelTime();
    while (!S.dont_bail_out) {
        LTTime diff_time = LTTime_Subtract(S.core->GetKernelTime(), start_time);
        if (LTTime_GetSeconds(diff_time) > 60) {
            break;
        }
        S.thread->Sleep(LTTime_Milliseconds(s_connect_test_sleep_time));
    }
    TILT_EXPECT_TRUE(tilt, ble_contr_start_test_done, "Ble Controller Test Failure");
    TILT_EXPECT_TRUE(tilt, adv_start_test_done, "StartDone Ble Controller Test Failure");
    TILT_EXPECT_TRUE(tilt, conection_test_done, "Ble Advertisement Test Failure");
    TILT_EXPECT_TRUE(tilt, char_read_test_done, "Ble Char Read Test Failure");
    TILT_EXPECT_TRUE(tilt, indicate_test_done, "Ble Indicate Test Failure");
    TILT_EXPECT_TRUE(tilt, notify_test_done, "Ble Notify Test Failure");
    TILT_EXPECT_TRUE(tilt, write_test_done, "Ble Data Transfer Test Failure");
    TILT_EXPECT_TRUE(tilt, disconnection_test_done, "Ble Controller Stop Test Failure");
}

static void StopBleAdvertiseTest(Tilt *tilt) {
    TILT_EXPECT_TRUE(tilt, S.devBle->Disconnect(S.hDevice), "Disconnection Test Failure");
    S.devBle->StopAdvertise();
    S.thread->Sleep(LTTime_Milliseconds(s_adv_stop_test_sleep_time));
    LTDeviceBle_State state = S.devBle->GetState();
    // This condition will change if the ConnectDeviceTest and ReadCharTest
    // are enabled.
    TILT_EXPECT_TRUE(tilt, state==kLTDeviceBle_State_Started, "Stop Adv Failure");
}

/*******************************************************************************
 * Test Hook Functions
 ******************************************************************************/
static void DestroyChrHandle(LTBleChrHandle hChr) {
    LTBleChrCtx *chrCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!chrCtx) {
        return;
    }
    S.event->UnregisterFromEvent(chrCtx->hEvent, OnChrEvent);
    lt_destroyhandle(chrCtx->hEvent);
    *chrCtx = (LTBleChrCtx){};
    S.core->ReleaseHandlePrivateData(hChr, chrCtx);
}

static void DestroySvcHandle(LTBleSvcHandle hSvc) {
    LTBleSvcCtx *svcCtx = S.core->ReserveHandlePrivateData(hSvc);
    if (!svcCtx) {
        return;
    }
    S.event->UnregisterFromEvent(svcCtx->hEvent, OnSvcEvent);
    lt_destroyhandle(svcCtx->hEvent);
    for (int i = 0; i < svcCtx->numCharacteristics; i++) {
        lt_destroyhandle(svcCtx->chr_hdl[i]);
    }
    S.core->ReleaseHandlePrivateData(hSvc, svcCtx);
}

static void DestroyDeviceHandle(LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) {
        return;
    }
    S.event->UnregisterFromEvent(devCtx->hEvent, OnDeviceEvent);
    lt_destroyhandle(devCtx->hEvent);
    for (int i = 0; i < devCtx->numServices; i++) {
        lt_destroyhandle(devCtx->svc_hdl[i]);
    }
    S.core->ReleaseHandlePrivateData(hDevice, devCtx);
}

typedef_LTLIBRARY_INTERFACE(IBleHChr, 1) {}  LTLIBRARY_INTERFACE;
define_LTLIBRARY_INTERFACE(IBleHChr, DestroyChrHandle) {} LTLIBRARY_DEFINITION;
typedef_LTLIBRARY_INTERFACE(IBleHSvc, 1) {}  LTLIBRARY_INTERFACE;
define_LTLIBRARY_INTERFACE(IBleHSvc, DestroySvcHandle) {} LTLIBRARY_DEFINITION;
typedef_LTLIBRARY_INTERFACE(IBleHDevice, 1) {}  LTLIBRARY_INTERFACE;
define_LTLIBRARY_INTERFACE(IBleHDevice, DestroyDeviceHandle) {} LTLIBRARY_DEFINITION;

static bool OnThreadStart(void) {
    //Setup Device Handle
    LTBleDeviceCtx *deviceData = NULL;
    LTBleSvcCtx *svcData = NULL;
    LTBleChrCtx *chrCtx = NULL;

    S.hDevice = S.core->CreateHandle((LTInterface *)&s_IBleHDevice, sizeof(LTBleDeviceCtx));
    if (!S.hDevice) return false;
    deviceData = S.core->ReserveHandlePrivateData(S.hDevice);
    if (!deviceData) {
        S.bleStartStatus = false;
        goto done;
    }
    *deviceData = (LTBleDeviceCtx){};
    deviceData->hEvent = S.core->CreateEvent(&EventArgs, DispatchEvent, DispatchCompleteProc, NULL, NULL);
    if (!deviceData->hEvent) {
        S.bleStartStatus = false;
        goto done;
    }
    deviceData->hDev = S.hDevice;
    deviceData->securePairing = false;
    deviceData->ble_host_thread_prio = (kLTThread_PriorityHighest - 4);
    S.event->RegisterForEvent(deviceData->hEvent, OnDeviceEvent, NULL, NULL, false);
    deviceData->numServices = 1;

    //Setup Service Handle
    S.hSvc = deviceData->svc_hdl[0] =
                    S.core->CreateHandle((LTInterface *)&s_IBleHSvc, sizeof(LTBleSvcCtx));
    svcData = S.core->ReserveHandlePrivateData(S.hSvc);
    if (!svcData) {
        S.bleStartStatus = false;
        goto done;
    }
    *svcData = (LTBleSvcCtx){};
    svcData->uuid = &SERVICE_UUID;
    svcData->serviceType = LT_BLE_GATT_SVC_TYPE_PRIMARY;
    svcData->hEvent = S.core->CreateEvent(&EventArgs, DispatchEvent, NULL, NULL, NULL);
    if (!svcData->hEvent) {
        S.bleStartStatus = false;
        goto done;
    }
    S.event->RegisterForEvent(svcData->hEvent, OnSvcEvent, NULL, NULL, false);

    //Setup Characteristic Handle
    LTBleChrHandle hChr = S.core->CreateHandle((LTInterface *)&s_IBleHChr, sizeof(LTBleChrCtx));
    svcData->chr_hdl[0] = hChr;
    svcData->numCharacteristics = 1;
    chrCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!chrCtx) {
        S.bleStartStatus = false;
        goto done;
    }
    *chrCtx = (LTBleChrCtx){};
    chrCtx->hChr = hChr;
    chrCtx->uuid = &CHARS_UUID;
    chrCtx->chrFlags = LT_BLE_GATT_CHR_F_READ | LT_BLE_GATT_CHR_F_WRITE | LT_BLE_GATT_CHR_F_NOTIFY | LT_BLE_GATT_CHR_F_INDICATE;
    chrCtx->hEvent = S.core->CreateEvent(&EventArgs, DispatchEvent, NULL, NULL, NULL);
    if (!chrCtx->hEvent) {
        S.bleStartStatus = false;
        goto done;
    }
    S.event->RegisterForEvent(chrCtx->hEvent, OnChrEvent, NULL, NULL, false);
    chrCtx->readBuf = S.readbuf;
    chrCtx->readBufSize = LT_BLE_BUF_MAX_LEN;
    chrCtx->readLen = LT_BLE_BUF_MAX_LEN;
    chrCtx->writeBuf = S.writebuf;
    chrCtx->writeBufSize = LT_BLE_BUF_MAX_LEN;
    chrCtx->readCb = &BleControlReadCb;
    chrCtx->writeCb = &BleControlWriteCb;

    S.bleStartStatus = S.devBle->Start(S.hDevice);
    if (!S.bleStartStatus) {
        TILT_DEBUG(S.tilt, "BLE device start failed");
    }
    S.devBle->SetService(S.hSvc);
done:
    //Release service handle
    if (svcData) S.core->ReleaseHandlePrivateData(S.hSvc, svcData);
    if (chrCtx) S.core->ReleaseHandlePrivateData(hChr, chrCtx);
    if (deviceData) S.core->ReleaseHandlePrivateData(S.hDevice, deviceData);
    return S.bleStartStatus;
}

static void OnThreadStop(void) {
    S.devBle->Stop();
    lt_destroyhandle(S.hDevice);
}

static void BeforeAllTests(Tilt *tilt) {
    S = (struct Statics){};
    S.tilt   = tilt;
    S.core   = LT_GetCore();
    S.event  = lt_getlibraryinterface(ILTEvent, S.core);
    S.thread = lt_getlibraryinterface(ILTThread, S.core);
    S.devBle = lt_openlibrary(LTDeviceBle);
    TILT_EXPECT_TRUE(tilt, S.devBle != NULL, "Cannot open LTDeviceBle");

    const char *ble_advname = tilt->API->GetProperty("AP_SSID", LT_BLE_ADV_NAME);
    lt_strncpyTerm(S.deviceName, ble_advname, lt_strlen(ble_advname)+1);

    S.bleThread = S.core->CreateThread("TestBleThread");
    TILT_EXPECT_TRUE(tilt, S.bleThread != 0, "Cannot create BLE thread");
    if (!S.bleThread) return;

    S.thread->SetStackSize(S.bleThread, 2048);
    S.thread->Start(S.bleThread, OnThreadStart, OnThreadStop);
}

static void AfterAllTests(Tilt *tilt) {
    LT_UNUSED(tilt);
    if (S.bleThread) {
        S.thread->Terminate(S.bleThread);
        S.thread->WaitUntilFinished(S.bleThread, LTTime_Milliseconds(10000));
        lt_destroyhandle(S.bleThread);
    }
    lt_closelibrary(S.devBle);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests,
};

/*******************************************************************************
 * Test list
 ******************************************************************************/

static const TiltEngineTest s_tests[] = {
    { StartBleDeviceTest,   "BleDeviceStartTest", "Test LTDeviceBle interface", 0 },
    { AdvertiseDeviceTest,  "AdvTest",            "Test Start Advertisement",   0 },
    { ConnectDeviceTest,    "ConnectTest",        "Test to connect",            0 },
    { StopBleAdvertiseTest, "StopAdvTest",        "Test to stop advertisement", 0 },
};

/*******************************************************************************
 * Test executive
 ******************************************************************************/

static int UnitTestLTDeviceBleImplImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

/*******************************************************************************
 * Library binding
 ******************************************************************************/

static bool UnitTestLTDeviceBleImplImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTDeviceBleImplImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceBleImpl, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTDeviceBleImpl, UnitTestLTDeviceBleImplImpl_Run, 1536) LTLIBRARY_DEFINITION;
