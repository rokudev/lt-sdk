/*******************************************************************************
 * source/iot/service/ble/IotServiceBleRvR.c
 *
 *   RvR App for BLE
 *
 * Copyright 2024-2026, Roku, Inc.  All Rights Reserved.
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/device/ble/LTDeviceBle.h>
#include <lt/system/schell/LTSystemSchell.h>

/******************************************************************************
 * Debug Macros
 ******************************************************************************/
#define PLOG(...) LTLOG(__VA_ARGS__)
#define P(...) //LTLOG(__VA_ARGS__)
#define PCONSOLE(...) LTLOG_STOMP(__VA_ARGS__)

/******************************************************************************
 * Size Macros
 ******************************************************************************/
#define LT_BLE_DEVICE_NAME_MAX_LEN      (21)
#define LT_BLE_IPG                      (5) //msec
#define LT_BLE_RVR_MAX_PKT_SIZE         (230)

/******************************************************************************
 * Static Definitions
 ******************************************************************************/
DEFINE_LTLOG_SECTION("ble.rvr");
#define ADVNAME_PREFIX "Roku_Ble_RvR"
typedef enum LTBleTestCommand {
    kLTBleStartTestTx       = 1,
    kLTBleStartTestRx       = 2,
    kLTBleCommandRxAck      = 3,
    kLTBleStopTxTest        = 4,
    kLTBleStopRxTest        = 5,
    kBleGetTxTestStats      = 6,
    kBleGetRxTestStats      = 7,
    kBleRxTestData          = 8,
    kBleStartTestLatency    = 9,
    kBleStopTestLatency     = 10,
    kBleGetLatencyStats     = 11,
    kBleLatencyTestData     = 12,
    kLTBleStartTestTxData   = 13
} LTBleTestCommand;

typedef struct LTBleRvrParams {
    u32           numPkts;
    u16           pktSize;
    u8            indicate;
    u16           ipg;
} LTBleRvrParams;

typedef struct LTBleRvrTxStats {
    u32           numPktsTx;
    u32           numPktsTxErr;
    u8            txPktStatBmapLen;
    u8*           txPktStatBmap; // Bit map of packets sent
    s64           txTime;
    bool          complete;
} LTBleRvrTxStats;

typedef struct LTBleRvrRxStats {
    u32           numPktsRx;
    u8            rxPktStatBmapLen;
    u8*           rxPktStatBmap; // Bit map of packets received
} LTBleRvrRxStats;

typedef LTBleRvrRxStats LTBleRvrLatencyStats;

typedef struct RvRPktHdr {
    u8            cmdType;
    u32           pktNum;
    u16           pktSize;
}__attribute__((packed)) RvRPktHdr;
#define LEAVE_HDR_SPACE(ptr) ptr += sizeof(RvRPktHdr)

static struct Statics {
    LTSystemSchell      *shell;
    LTCore              *core;
    ILTEvent            *iEvent;
    LTBleDeviceHandle    devHandle;
    LTBleSvcHandle       svcHandle;
    LTBleChrHandle       tputTestCharHandle;
    ILTThread           *iThread;
    LTDeviceBle         *devBle;
    LTThread             testThread;
    u8                   sendbuffer[LT_BLE_RVR_MAX_PKT_SIZE];
    u8                   cmdbuffer[LT_BLE_RVR_MAX_PKT_SIZE];
    char                 deviceName[LT_BLE_DEVICE_NAME_MAX_LEN];
    LTBleRvrTxStats      txStats;
    LTBleRvrRxStats      rxStats;
    LTBleRvrLatencyStats latencyStats;
    u8                   indnot_subscribe;
    u8                   bleHostDefPriority;
    s32                  txPwr;
    char                 adv_name_prefix[LT_BLE_DEVICE_NAME_MAX_LEN];
} S;

#define LT_BLE_RVR_SERVICE_UUID128 { 0x16, 0x3a, 0x51, 0x0b, 0x15, 0x7e, 0x46, 0x6c, 0xa2, 0xcd, 0x3c, 0x59, 0xe0, 0x94, 0x49, 0xda }
#define LT_BLE_RX_RVR_CHAR_UUID128 { 0xd4, 0x76, 0x86, 0x5b, 0x16, 0x6f, 0x46, 0x4e, 0x96, 0x3a, 0x7f, 0x79, 0xd4, 0x92, 0x9f, 0x54 }

LTBleUuidAny         RvRSvcUuid    = { .type = kLTBleUuidType_128, .value.uuid128 =  { .value = LT_BLE_RVR_SERVICE_UUID128 }};
LTBleUuidAny         testCharUuid = { .type = kLTBleUuidType_128, .value.uuid128 =  { .value = LT_BLE_RX_RVR_CHAR_UUID128 }};

/*******************************************************************************
 * RvR Application Functions
 ******************************************************************************/
static const LTArgsDescriptor EventArgs = {3, { kLTArgType_u32, kLTArgType_u32, kLTArgType_pointer }};
static void DispatchEvent(LTEvent event, void *proc, LTArgs *args, void *data) {
    LT_UNUSED(event);
    P("debug", "DispatchEvent");
    if (!proc) return;
    (*(LTBleEventProc)proc)(LTArgs_u32At(0, args), LTArgs_u32At(1, args), LTArgs_pointerAt(2, args), data);
}

static void DispatchCompleteProc(LTHandle hEvent, LTArgs *args) {
    LT_UNUSED(hEvent);
    void* bleEvent = (void*)LTArgs_pointerAt(2, args);
    if (bleEvent) {
        lt_free(bleEvent);
    }
}

static void OnDeviceEvent(LTBleDeviceHandle hDevice, u32 event, void *bleEvtData, void *data) {
    LT_UNUSED(data);
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) return;

    if (hDevice == S.devHandle) {
        if (event == kLTDeviceBle_Event_Started || event==kLTDeviceBle_Event_Disconnected) {
            if (event==kLTDeviceBle_Event_Disconnected) {
                P(__FUNCTION__, "Ble Device Disconnected, Advertising again");
            } else {
                P(__FUNCTION__, "Ble Device Started");
            }
            LTBleAdvFields adv_fields = {0};
            LTBleAdvFields rsp_fields = {0};
            LTBleAdvParams adv_params = {0};
            char name[LT_BLE_DEVICE_NAME_MAX_LEN];
            LTMacAddress own_mac;
            S.devBle->GetOwnAddress(&own_mac);
            lt_snprintf(name, LT_BLE_DEVICE_NAME_MAX_LEN, "%s_%02x%02x", S.adv_name_prefix, own_mac.octet[4], own_mac.octet[5]);
            adv_fields.flags = LT_BLE_ADV_F_DISC_GEN|LT_BLE_ADV_F_BREDR_UNSUP;
            adv_fields.name = name;
            adv_params.conn_mode = LT_BLE_CONN_MODE_UND;
            adv_params.disc_mode = LT_BLE_DISC_MODE_GEN;
            adv_params.duration_ms = LT_BLE_ADV_FOREVER;
            if(!S.devBle->StartAdvertise(hDevice, &adv_fields, &rsp_fields, &adv_params)) {
                LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to start advertising %s", name);
            }
        } else if (event == kLTDeviceBle_Event_Subscribe) {
            P(__FUNCTION__, "Ble Device Subscribed");
            // Set max Data length and Transmit time params
            S.devBle->SetMaxTransmitParams(devCtx, 251, 2120);
            LTBleEventData *evt = (LTBleEventData*)bleEvtData;
            if (evt) {
                if (evt->subscribe.cur_indicate == 1) {
                    P(__FUNCTION__, "Indicate is enabled");
                    S.indnot_subscribe |= 0x1;
                }
                if (evt->subscribe.cur_notify == 1) {
                    P(__FUNCTION__, "Notify is enabled");
                    S.indnot_subscribe |= 0x2;
                }
                LTBleUpdConnParams upd_conn_params;
                lt_memset(&upd_conn_params, 0x00, sizeof(LTBleUpdConnParams));
                upd_conn_params.itvl_min = 6;
                upd_conn_params.itvl_max = 7;
                upd_conn_params.latency = 0;
                upd_conn_params.supervision_timeout = 500;
                upd_conn_params.min_ce_len = 100;
                upd_conn_params.max_ce_len = 200;
                P("conn.param.upd", "requesting faster connection interval for RvR");
                if(!S.devBle->UpdateConnectionParams(hDevice, &upd_conn_params)) {
                    PLOG("conn.param.upd.err", "Failed to update connection parameters");
                }
            } else {
                P(__FUNCTION__, "Event data is Empty or Disconnected");
            }
        }
    }
    P(__FUNCTION__, "hDevice: %lx(S.devHandle: %lx), Event: %lx", LT_Pu32(hDevice), LT_Pu32(S.devHandle), LT_Pu32(event));
    S.core->ReleaseHandlePrivateData(hDevice, devCtx);
}

static void OnSvcEvent(LTBleSvcHandle hSvc, u32 event, void *bleEvtData, void *data) {
    LT_UNUSED(hSvc);
    LT_UNUSED(event);
    LT_UNUSED(bleEvtData);
    LT_UNUSED(data);
    P(__FUNCTION__, "hSvc: %lx, S.svcHandle: %lx Event: %lx", LT_Pu32(hSvc), LT_Pu32(S.svcHandle), LT_Pu32(event));
}

u8* ParseTestParams(u8 *buffer, u16 length, LTBleRvrParams *testParams) {
    if (length < sizeof(testParams->numPkts) + sizeof(testParams->pktSize)) {
        PLOG("inval.test.param", "No Test Parameters");
        return NULL;
    }
    P(__FUNCTION__, "Parsing Test Parameters, ptr %p len %lx", buffer, LT_Pu32(length));
    u8* ptr = buffer;
    s16 len = length;
    lt_memcpy(&testParams->numPkts, ptr, sizeof(testParams->numPkts));
    ptr += sizeof(testParams->numPkts);
    len -= sizeof(testParams->numPkts);
    lt_memcpy(&testParams->pktSize, ptr, sizeof(testParams->pktSize));
    ptr += sizeof(testParams->pktSize);
    len -= sizeof(testParams->pktSize);
    testParams->indicate = 0;
    if (len > 0) {
        lt_memcpy(&testParams->indicate, ptr, sizeof(testParams->indicate));
        ptr += sizeof(testParams->indicate);
        len -= sizeof(testParams->indicate);
    }
    testParams->ipg = LT_BLE_IPG;
    if (len > 0) {
        lt_memcpy(&testParams->ipg, ptr, sizeof(testParams->ipg));
        ptr += sizeof(testParams->ipg);
        len -= sizeof(testParams->ipg);
    }
    
    P(__FUNCTION__, "Number of Packets: %lx, Packet Size: %lx ipg: %lxms len: %ld",
        LT_Pu32(testParams->numPkts), LT_Pu32(testParams->pktSize), LT_Pu32(testParams->ipg), LT_Ps32(len));
    
    return ptr;
}


u16 SlapHeader(u8 *buffer, u8 cmdType, u32 pktNum, u16 pktSize) {
    u8 * ptr = buffer;
    lt_memset(ptr, 0x00, sizeof(RvRPktHdr));
    lt_memcpy(ptr, &cmdType, sizeof(cmdType));
    ptr += sizeof(cmdType);
    lt_memcpy(ptr, &pktNum, sizeof(pktNum));
    ptr += sizeof(pktNum);
    lt_memcpy(ptr, &pktSize, sizeof(pktSize));
    ptr += sizeof(pktSize);
    return ptr - buffer;
}

bool TestStop(LTBleChrHandle hChr) {
    P(__FUNCTION__, "Send Stop Test");
    RvRPktHdr pktHdr = { .cmdType = kLTBleStopTxTest, .pktNum = 0, .pktSize = 0 };
    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) {
        PLOG("inval.chr.hdl", "Invalid Test Handle for Abort hChr %lx", LT_Pu32(hChr));
        return false;
    }
    if (!(S.indnot_subscribe & 0x1)) {
        PLOG("inval.stop.param", "Indicate is not subscribed");
        return false;
    }
    u8 * ptr = charCtx->readBuf;
    charCtx->readLen = SlapHeader(ptr, pktHdr.cmdType, pktHdr.pktNum, pktHdr.pktSize);
    S.core->ReleaseHandlePrivateData(hChr, charCtx);
    u8 retry = 3;
    bool ret = false;
    //Add a retry here for indicate failure
    while (--retry) {
        ret = S.devBle->Indicate(S.devHandle, hChr);
        if (ret) {
            P(__FUNCTION__, "Send Stop Test Successful");
            break;
        }
        S.iThread->Sleep(LTTime_Milliseconds(500));
    }
    if (!ret) {
        PLOG("tx.test.stop.fail", "Unable to stop the test");
    }
    charCtx->readLen = 0;
    return ret;
}

bool  SendBuffer(LTBleChrHandle hChr, u8 indicate) {
    if (!indicate) {
        if (!(S.indnot_subscribe & 0x2)) {
            PLOG("inval.tx.test.param", "Notify is not subscribed");
            return false;
        }
        P(__FUNCTION__, "Sending Notification");
        return S.devBle->Notify(S.devHandle, hChr);
    } else {
        if (!(S.indnot_subscribe & 0x1)) {
            PLOG("inval.tx.test.param", "Indicate is not subscribed");
            return false;
        }
        P(__FUNCTION__, "Sending Indication");
        return S.devBle->Indicate(S.devHandle, hChr);
    }
}

bool PerformTxTest(LTBleChrHandle hChr, u8* optr, u16 payload_len) {
    if (!optr) {
        PLOG("inval.tx.test.param", "No Test Parameters");
        return false;
    }
    LTBleRvrParams testParams = {0};
    optr = ParseTestParams(optr, payload_len, &testParams);
    if (!optr) {
        PLOG("inval.tx.test.param", "Invalid Test Parameters");
        return false;
    }
    P(__FUNCTION__, "Performing Tx Test for hChr: %lx Number of Packets: %lx Packets Size: %lx",
        LT_Pu32(hChr), LT_Pu32(testParams.numPkts), LT_Pu32(testParams.pktSize));
    //Start the test now.
    if (sizeof(RvRPktHdr) + testParams.pktSize > sizeof(S.cmdbuffer)) {
        PLOG("inval.tx.test.param", "Invalid Test Parameters");
        return TestStop(hChr);
    }

    if (!(S.indnot_subscribe & 0x1)) {
        PLOG("inval.tx.test.param", "Indicate is not subscribed, required for control messages");
        return TestStop(hChr);
    }

    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(S.devHandle);
    if (!devCtx) {
        PLOG("inval.dev.hdl", "Invalid Device Handle for Tx hChr %lx", LT_Pu32(hChr));
        return TestStop(hChr);
    }
    S.bleHostDefPriority = S.iThread->GetPriority(devCtx->bleHostThr);
    S.iThread->SetPriority(devCtx->bleHostThr, kLTThread_PriorityDefault);
    S.core->ReleaseHandlePrivateData(S.devHandle, devCtx);

    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) {
        PLOG("inval.chr.hdl", "Invalid Test Handle for Tx hChr %lx", LT_Pu32(hChr));
        return TestStop(hChr);
    }
    u8* ptr = charCtx->readBuf;
    LEAVE_HDR_SPACE(ptr);
    lt_memset(ptr, 0xab, testParams.pktSize);
    // Calculate the sizeof bitmap
    u32 numBytes = testParams.numPkts/8;
    if (testParams.numPkts % 8) {
        numBytes++;
    }
    if (S.txStats.txPktStatBmap) {
        lt_free(S.txStats.txPktStatBmap);
    }
    S.txStats = (LTBleRvrTxStats){};

    S.txStats.txPktStatBmapLen = numBytes;
    S.txStats.txPktStatBmap = lt_malloc(numBytes);
    if (!S.txStats.txPktStatBmap) {
        PLOG("tx.test.alloc.fail", "Unable to allocate memory for packet status bitmap");
        return TestStop(hChr);
    }
    lt_memset(S.txStats.txPktStatBmap, 0x00, numBytes);
    LTTime start_time = S.core->GetKernelTime();
    for (u32 cnt = 0; cnt < testParams.numPkts; cnt++) {
        RvRPktHdr pktHdr = { .cmdType = kLTBleStartTestTxData, .pktNum = cnt, .pktSize = testParams.pktSize };
        u8 * ptr = charCtx->readBuf;
        u16 hdrLen = SlapHeader(ptr, pktHdr.cmdType, pktHdr.pktNum, pktHdr.pktSize);
        charCtx->readLen = hdrLen + testParams.pktSize;
        while (!SendBuffer(hChr, testParams.indicate)) {
            u8 bit_idx = cnt % 8;
            u8 byte_idx = cnt / 8;
            if (!(S.txStats.txPktStatBmap[byte_idx] & (1 << bit_idx))) {
                S.txStats.numPktsTxErr++;
                S.txStats.txPktStatBmap[byte_idx] |= (1 << bit_idx);
            }
            S.iThread->Sleep(LTTime_Milliseconds(testParams.ipg));
        }
        S.txStats.numPktsTx++;
        charCtx->readLen = 0;
        S.iThread->Sleep(LTTime_Milliseconds(testParams.ipg));
    }
    S.txStats.complete = TestStop(hChr);
    if (S.txStats.complete == false) {
        PLOG("tx.test.stop.fail", "Unable to stop the test");
    }

    S.txStats.txTime = LTTime_GetMilliseconds(LTTime_Subtract(S.core->GetKernelTime(), start_time));

    S.core->ReleaseHandlePrivateData(hChr, charCtx);
    return true;
}

void PerformStopTxTest(LTBleChrHandle hChr) {
    LT_UNUSED(hChr);
    P(__FUNCTION__, "Stopping Tx Test for hChr: %lx", LT_Pu32(hChr));
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(S.devHandle);
    if (devCtx) {
        S.iThread->SetPriority(devCtx->bleHostThr, S.bleHostDefPriority);
        S.core->ReleaseHandlePrivateData(S.devHandle, devCtx);
    } else {
        PLOG("inval.dev.hdl", "Invalid Device Handle for Tx hChr %lx", LT_Pu32(S.devHandle));
        PLOG("blehost.thr.pri.bad", "Unable to set the priority back to default");
    }

    if (S.txStats.txPktStatBmap) {
        lt_free(S.txStats.txPktStatBmap);
    }
    S.txStats = (LTBleRvrTxStats){};
}

bool PerformRxTest(LTBleChrHandle hChr, u8* optr, u16 payload_len) {
    LTBleRvrParams testParams = {0};
    optr = ParseTestParams(optr, payload_len, &testParams);
    if (!optr) {
        PLOG("inval.tx.test.param", "Invalid Test Parameters");
        return false;
    }
    P(__FUNCTION__, "Performing Rx Test for hChr: %lx", LT_Pu32(hChr));

    // Calculate the sizeof bitmap
    u32 numBytes = testParams.numPkts/8;
    if (testParams.numPkts % 8) {
        numBytes++;
    }
    if (S.rxStats.rxPktStatBmap) {
        lt_free(S.rxStats.rxPktStatBmap);
    }
    S.rxStats = (LTBleRvrRxStats){};

    S.rxStats.rxPktStatBmapLen = numBytes;
    S.rxStats.rxPktStatBmap = lt_malloc(numBytes);
    if (!S.rxStats.rxPktStatBmap) {
        PLOG("rx.test.alloc.fail", "Unable to allocate memory for packet status bitmap");
        return TestStop(hChr);
    }
    lt_memset(S.rxStats.rxPktStatBmap, 0x00, numBytes);

    //Send the ack to start the test
    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) {
        PLOG("inval.chr.hdl", "Invalid Test Handle for Tx hChr %lx", LT_Pu32(hChr));
        return TestStop(hChr);
    }
    RvRPktHdr pktHdr = { .cmdType = kLTBleCommandRxAck, .pktNum = 0, .pktSize = 0 };
    u8 * ptr = charCtx->readBuf;
    charCtx->readLen = SlapHeader(ptr, pktHdr.cmdType, pktHdr.pktNum, pktHdr.pktSize);

    if (!SendBuffer(hChr, 0x1)) {
        PLOG("rx.test.start.fail", "Unable to start the test");
        return false;
    }
    charCtx->readLen = 0;
    S.core->ReleaseHandlePrivateData(hChr, charCtx);
    return true;
}

void PerformStopRxTest(LTBleChrHandle hChr) {
    LT_UNUSED(hChr);
    P(__FUNCTION__, "Stopping Rx Test for hChr: %lx", LT_Pu32(hChr));
    if (S.rxStats.rxPktStatBmap) {
        lt_free(S.rxStats.rxPktStatBmap);
    }
    S.rxStats = (LTBleRvrRxStats){};
    S.iThread->SetPriority(S.testThread, (kLTThread_PriorityDefault)); 
}

void PerformGetTxTestStats(LTBleChrHandle hChr) {
    P(__FUNCTION__, "Getting TX Test Stats for hChr: %lx", LT_Pu32(hChr));
    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) {
        PLOG("inval.chr.hdl", "Invalid Test Handle for Stats hChr %lx", LT_Pu32(hChr));
        return;
    }
    if (!(S.indnot_subscribe & 0x1)) {
        PLOG("inval.tx.test.param", "Indicate is not subscribed, required for control messages");
        TestStop(hChr);
        return;
    }

    u8* ptr = charCtx->readBuf;
    LEAVE_HDR_SPACE(ptr);
    lt_memcpy(ptr, &S.txStats.numPktsTx, sizeof(S.txStats.numPktsTx));
    ptr += sizeof(S.txStats.numPktsTx);
    lt_memcpy(ptr, &S.txStats.numPktsTxErr, sizeof(S.txStats.numPktsTxErr));
    ptr += sizeof(S.txStats.numPktsTxErr);
    lt_memcpy(ptr, &S.txStats.txTime, sizeof(S.txStats.txTime));
    ptr += sizeof(S.txStats.txTime);
    lt_memcpy(ptr, &S.txStats.txPktStatBmapLen, sizeof(S.txStats.txPktStatBmapLen));
    ptr += sizeof(S.txStats.txPktStatBmapLen);
    lt_memcpy(ptr, S.txStats.txPktStatBmap, S.txStats.txPktStatBmapLen);
    ptr += S.txStats.txPktStatBmapLen;
    lt_memcpy(ptr, &S.txStats.complete, sizeof(S.txStats.complete));
    ptr += sizeof(S.txStats.complete);
    u32 payload_len = sizeof(S.txStats.numPktsTx) + sizeof(S.txStats.numPktsTxErr) 
                    + sizeof(S.txStats.txTime) + sizeof(S.txStats.txPktStatBmapLen)
                    + S.txStats.txPktStatBmapLen + sizeof(S.txStats.complete);
    u16 hdlen = SlapHeader(charCtx->readBuf, kBleGetTxTestStats, 0, payload_len);
    charCtx->readLen = hdlen + payload_len;
    P(__FUNCTION__, "numPktsTx: 0x%02lx numPktsTxErr: 0x%02lx TXTime: 0x%02llxmsec Complete: %x",
                    LT_Pu32(S.txStats.numPktsTx), 
                    LT_Pu32(S.txStats.numPktsTxErr),
                    LT_Pu64(S.txStats.txTime), S.txStats.complete);
    if (!SendBuffer(hChr, 0x1)) {
        PLOG("tx.test.stats.fail", "Unable to send stats");
    }
    charCtx->readLen = 0;
    S.core->ReleaseHandlePrivateData(hChr, charCtx);
}

void PerformGetRxTestStats(LTBleChrHandle hChr) {
    P(__FUNCTION__, "Getting RX Test Stats for hChr: %lx", LT_Pu32(hChr));
    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) {
        PLOG("inval.chr.hdl", "Invalid Test Handle for Stats hChr %lx", LT_Pu32(hChr));
        return;
    }
    if (!(S.indnot_subscribe & 0x1)) {
        PLOG("inval.rx.test.param", "Indicate is not subscribed, required for control messages");
        TestStop(hChr);
        return;
    }

    u8* ptr = charCtx->readBuf;
    LEAVE_HDR_SPACE(ptr);
    lt_memcpy(ptr, &S.rxStats.numPktsRx, sizeof(S.rxStats.numPktsRx));
    ptr += sizeof(S.rxStats.numPktsRx);
    lt_memcpy(ptr, &S.rxStats.rxPktStatBmapLen, sizeof(S.rxStats.rxPktStatBmapLen));
    ptr += sizeof(S.rxStats.rxPktStatBmapLen);
    lt_memcpy(ptr, S.rxStats.rxPktStatBmap, S.rxStats.rxPktStatBmapLen);
    ptr += S.rxStats.rxPktStatBmapLen;
    u32 payload_len = sizeof(S.rxStats.numPktsRx)
                    + sizeof(S.rxStats.rxPktStatBmapLen)
                    + S.rxStats.rxPktStatBmapLen;
    u16 hdlen = SlapHeader(charCtx->readBuf, kBleGetRxTestStats, 0, payload_len);
    charCtx->readLen = hdlen + payload_len;
    if (!SendBuffer(hChr, 0x1)) {
        PLOG("rx.test.stats.fail", "Unable to send stats");
    }
    charCtx->readLen = 0;
    S.core->ReleaseHandlePrivateData(hChr, charCtx);
}

void ProcessRxTestData(LTBleChrHandle hChr, u8* ptr, u16 payload_len) {
    LT_UNUSED(hChr);
    if (!ptr) {
        PLOG("inval.rx.test.pkt", "Invalid Test Packet");
        return;
    }
    if (S.rxStats.rxPktStatBmap == NULL) {
        PLOG("inval.rx.test.pkt", "RX Packet hasnt started");
        TestStop(hChr);
        return;
    }
    u32 pkt_idx = 0;
    u16 pkt_size = 0;
    lt_memcpy(&pkt_idx, ptr, sizeof(pkt_idx));
    ptr += sizeof(pkt_idx);
    lt_memcpy(&pkt_size, ptr, sizeof(pkt_size));
    ptr += sizeof(pkt_size);
    if (payload_len != (sizeof(pkt_idx) + sizeof(pkt_size) + pkt_size)) {
        PLOG("inval.rx.test.pkt", "Invalid Packet Size payload_len=%lx pkt_size=%lx",
            LT_Pu32(payload_len), LT_Pu32(pkt_size));
        return;
    }
    u8 idx = pkt_idx / 8;
    u8 bit = pkt_idx % 8;
    if (idx >= S.rxStats.rxPktStatBmapLen) {
        PLOG("inval.rx.test.pkt", "Invalid Packet Number");
        TestStop(hChr);
        return;
    }
    S.rxStats.rxPktStatBmap[idx] |= (1 << bit);
    S.rxStats.numPktsRx++;
    P("rcv.pkt", "Successfully Recieved Packet id:%lx", LT_Pu32(pkt_idx));
}

void ProcessLatencyTestData(LTBleChrHandle hChr, u8* ptr, u16 payload_len) {
    LT_UNUSED(hChr);
    if (!ptr) {
        PLOG("inval.rx.test.pkt", "Invalid Test Packet");
        return;
    }
    if (S.latencyStats.rxPktStatBmap == NULL) {
        PLOG("inval.latency.test.pkt", "Latency Test hasnt started");
        TestStop(hChr);
        return;
    }
    u32 pkt_idx = 0;
    u16 pkt_size = 0;
    lt_memcpy(&pkt_idx, ptr, sizeof(pkt_idx));
    ptr += sizeof(pkt_idx);
    lt_memcpy(&pkt_size, ptr, sizeof(pkt_size));
    ptr += sizeof(pkt_size);
    if (payload_len != (sizeof(pkt_idx) + sizeof(pkt_size) + pkt_size)) {
        PLOG("inval.lat.test.pkt", "Invalid Packet Size payload_len=%lx pkt_size=%lx",
            LT_Pu32(payload_len), LT_Pu32(pkt_size));
        TestStop(hChr);
        return;
    }
    u8 idx = pkt_idx / 8;
    u8 bit = pkt_idx % 8;
    if (idx >= S.latencyStats.rxPktStatBmapLen) {
        PLOG("inval.rx.test.pkt", "Invalid Packet Number");
        TestStop(hChr);
        return;
    }

    //Send the response back
    //Sending the same packet back
    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) {
        PLOG("inval.chr.hdl", "Invalid Test Handle for Tx hChr %lx", LT_Pu32(hChr));
        TestStop(hChr);
        return;
    }
    payload_len -= sizeof(pkt_idx) + sizeof(pkt_size);
    u8* tx_ptr = charCtx->readBuf;
    LEAVE_HDR_SPACE(tx_ptr);
    RvRPktHdr pktHdr = { .cmdType = kBleLatencyTestData, .pktNum = pkt_idx, .pktSize = payload_len };
    lt_memcpy(tx_ptr, ptr, payload_len);
    u16 hdlen = SlapHeader(charCtx->readBuf, pktHdr.cmdType, pktHdr.pktNum, pktHdr.pktSize);
    charCtx->readLen = hdlen + payload_len;
    if (SendBuffer(hChr, 0)) {
        S.latencyStats.rxPktStatBmap[idx] |= (1 << bit);
        S.latencyStats.numPktsRx++;
        P("rcv.pkt", "Successfully Recieved Packet id:%lx", LT_Pu32(pkt_idx));
    } else {
        PLOG("lat.test.rx.fail", "Unable to send the response");
    }
}

void PerformStopLatencyTest(LTBleChrHandle hChr) {
    LT_UNUSED(hChr);
    P(__FUNCTION__, "Stopping Latency Test for hChr: %lx", LT_Pu32(hChr));
    if (S.latencyStats.rxPktStatBmap) {
        lt_free(S.latencyStats.rxPktStatBmap);
    }
    S.latencyStats = (LTBleRvrLatencyStats){};
}

bool  PerformLatencyTest(LTBleChrHandle hChr, u8* optr, u16 payload_len) {
    LT_UNUSED(hChr);
    LT_UNUSED(optr);
    LT_UNUSED(payload_len);
    P(__FUNCTION__, "Performing Latency Test for hChr: %lx Payload Len: %lx", LT_Pu32(hChr), LT_Pu32(payload_len));
    LTBleRvrParams testParams = {0};
    optr = ParseTestParams(optr, payload_len, &testParams);
    if (!optr) {
        PLOG("inval.lat.test.param", "Invalid Test Parameters");
        return false;
    }
    P("lat.test.param", "Number of Packets: %lx, Packet Size: %lx ipg: %lxms", LT_Pu32(testParams.numPkts), LT_Pu32(testParams.pktSize), LT_Pu32(testParams.ipg));

    // Calculate the sizeof bitmap
    u32 numBytes = testParams.numPkts/8;
    if (testParams.numPkts % 8) {
        numBytes++;
    }
    if (S.latencyStats.rxPktStatBmap) {
        lt_free(S.latencyStats.rxPktStatBmap);
    }
    S.latencyStats = (LTBleRvrLatencyStats){};

    S.latencyStats.rxPktStatBmapLen = numBytes;
    S.latencyStats.rxPktStatBmap = lt_malloc(numBytes);
    if (!S.latencyStats.rxPktStatBmap) {
        PLOG("lat.test.alloc.fail", "Unable to allocate memory for packet status bitmap");
        return TestStop(hChr);
    }
    lt_memset(S.latencyStats.rxPktStatBmap, 0x00, numBytes);


    //Send the ack to start the test
    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) {
        PLOG("inval.chr.hdl", "Invalid Test Handle for Tx hChr %lx", LT_Pu32(hChr));
        return TestStop(hChr);
    }
    RvRPktHdr pktHdr = { .cmdType = kLTBleCommandRxAck, .pktNum = 0, .pktSize = 0 };
    u8 * ptr = charCtx->readBuf;
    charCtx->readLen = SlapHeader(ptr, pktHdr.cmdType, pktHdr.pktNum, pktHdr.pktSize);
    if (!SendBuffer(hChr, 0x1)) {
        PLOG("lat.test.start.fail", "Unable to start the test");
        return false;
    }
    charCtx->readLen = 0;
    S.core->ReleaseHandlePrivateData(hChr, charCtx);
    return true;
}

void PerformGetLatencyTestStats(LTBleChrHandle hChr) {
    P(__FUNCTION__, "Getting RX Test Stats for hChr: %lx", LT_Pu32(hChr));
    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) {
        PLOG("inval.chr.hdl", "Invalid Test Handle for Stats hChr %lx", LT_Pu32(hChr));
        return;
    }
    if (!(S.indnot_subscribe & 0x1)) {
        PLOG("inval.lat.test.param", "Indicate is not subscribed, required for control messages");
        TestStop(hChr);
        return;
    }

    u8* ptr = charCtx->readBuf;
    LEAVE_HDR_SPACE(ptr);
    lt_memcpy(ptr, &S.latencyStats.numPktsRx, sizeof(S.latencyStats.numPktsRx));
    ptr += sizeof(S.latencyStats.numPktsRx);
    lt_memcpy(ptr, &S.latencyStats.rxPktStatBmapLen, sizeof(S.latencyStats.rxPktStatBmapLen));
    ptr += sizeof(S.latencyStats.rxPktStatBmapLen);
    lt_memcpy(ptr, S.latencyStats.rxPktStatBmap, S.latencyStats.rxPktStatBmapLen);
    ptr += S.latencyStats.rxPktStatBmapLen;
    u32 payload_len = sizeof(S.latencyStats.numPktsRx)
                    + sizeof(S.latencyStats.rxPktStatBmapLen)
                    + S.latencyStats.rxPktStatBmapLen;
    u16 hdlen = SlapHeader(charCtx->readBuf, kBleGetLatencyStats, 0, payload_len);
    charCtx->readLen = hdlen + payload_len;
    if (!SendBuffer(hChr, 0x1)) {
        PLOG("rx.test.stats.fail", "Unable to send stats");
    }
    charCtx->readLen = 0;
    S.core->ReleaseHandlePrivateData(hChr, charCtx);
}

static void ProcessCommandBuffer(u8 *buffer, LTBleChrHandle hChr) {
    if (buffer == NULL) {
        P(__FUNCTION__, "Buffer is NULL");
        return;
    }
    u8 cmd_type = 0;
    u8* ptr = buffer;
    lt_memcpy(&cmd_type, ptr, sizeof(cmd_type));
    ptr += sizeof(cmd_type);
    u16 payload_len = 0;
    lt_memcpy(&payload_len, ptr, sizeof(payload_len));
    ptr += sizeof(payload_len);
    P(__FUNCTION__, "Command Type: %lx, Payload Length: %lx", LT_Pu32(cmd_type), LT_Pu32(payload_len));
    switch (cmd_type) {
        case kLTBleStartTestTx:
            P(__FUNCTION__, "Start Test Tx");
            if(PerformTxTest(hChr, ptr, payload_len)==false) {
                PLOG("tx.test.start.fail", "Unable to start the test");
            }
            break;
        case kLTBleStartTestRx:
            P(__FUNCTION__, "Start Test Rx");
            PerformRxTest(hChr, ptr, payload_len);
            break;
        case kLTBleStopTxTest:
            P(__FUNCTION__, "Stop Test");
            PerformStopTxTest(hChr);
            break;
        case kLTBleStopRxTest:
            P(__FUNCTION__, "Stop Test");
            PerformStopRxTest(hChr);
            break;
        case kBleGetTxTestStats:
            P(__FUNCTION__, "Get Test Stats");
            PerformGetTxTestStats(hChr);
            break;
        case kBleGetRxTestStats:
            P(__FUNCTION__, "Get Test Stats");
            PerformGetRxTestStats(hChr);
            break;
        case kBleRxTestData:
            P(__FUNCTION__, "Rx Test Data");
            ProcessRxTestData(hChr, ptr, payload_len);
            break;
        case kBleStartTestLatency:
            P(__FUNCTION__, "Latency Test");
            PerformLatencyTest(hChr, ptr, payload_len);
            break;
        case kBleStopTestLatency:
            P(__FUNCTION__, "Stop Latency Test");
            PerformStopLatencyTest(hChr);
            break;
        case kBleGetLatencyStats:
            P(__FUNCTION__, "Get Latency Stats");
            PerformGetLatencyTestStats(hChr);
            break;
        case kBleLatencyTestData:
            P(__FUNCTION__, "Latency Test Data");
            ProcessLatencyTestData(hChr, ptr, payload_len);
            break;
        default:
            P(__FUNCTION__, "Invalid Command");
            break;
    }
}

static void OnChrEvent(LTBleChrHandle hChr, u32 event, void *bleEvtData, void *data) {
    LT_UNUSED(bleEvtData);
    LT_UNUSED(data);

    LTBleChrCtx *charCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!charCtx) return;
    if (hChr == S.tputTestCharHandle) {
        if (event == kLTDeviceBle_Event_Write_Done) {
            P(__FUNCTION__, "Write Done");
            ProcessCommandBuffer(S.cmdbuffer, hChr);
        }
    }
    P(__FUNCTION__, "hChr: %lx, S.tputTestCharHandle: %lx Event: %lx", LT_Pu32(hChr), LT_Pu32(S.tputTestCharHandle), LT_Pu32(event));
    S.core->ReleaseHandlePrivateData(hChr, charCtx);
}

static void DestroyChrHandle(LTBleChrHandle hChr) {
    LTBleChrCtx *chrCtx = S.core->ReserveHandlePrivateData(hChr);
    if (!chrCtx) return;
    S.iEvent->UnregisterFromEvent(chrCtx->hEvent, OnChrEvent);
    lt_destroyhandle(chrCtx->hEvent);
    *chrCtx = (LTBleChrCtx){};
    S.core->ReleaseHandlePrivateData(hChr, chrCtx);
}

static void DestroySvcHandle(LTBleSvcHandle hSvc) {
    LTBleSvcCtx *svcCtx = S.core->ReserveHandlePrivateData(hSvc);
    if (!svcCtx) return;
    S.iEvent->UnregisterFromEvent(svcCtx->hEvent, OnSvcEvent);
    lt_destroyhandle(svcCtx->hEvent);
    for (int i = 0; i < svcCtx->numCharacteristics; i++) {
        lt_destroyhandle(svcCtx->chr_hdl[i]);
    }
    S.core->ReleaseHandlePrivateData(hSvc, svcCtx);
}

static void DestroyDeviceHandle(LTBleDeviceHandle hDevice) {
    LTBleDeviceCtx *devCtx = S.core->ReserveHandlePrivateData(hDevice);
    if (!devCtx) return;
    S.iEvent->UnregisterFromEvent(devCtx->hEvent, OnDeviceEvent);
    lt_destroyhandle(devCtx->hEvent);
    for (int i = 0; i < devCtx->numServices; i++) {
        lt_destroyhandle(devCtx->svc_hdl[i]);
    }
    S.core->ReleaseHandlePrivateData(hDevice, devCtx);
}

typedef_LTLIBRARY_INTERFACE(IBleChrHandle, 1) {}  LTLIBRARY_INTERFACE;
define_LTLIBRARY_INTERFACE(IBleChrHandle, DestroyChrHandle) {} LTLIBRARY_DEFINITION;
typedef_LTLIBRARY_INTERFACE(IBleHSvc, 1) {}  LTLIBRARY_INTERFACE;
define_LTLIBRARY_INTERFACE(IBleHSvc, DestroySvcHandle) {} LTLIBRARY_DEFINITION;
typedef_LTLIBRARY_INTERFACE(IBleHDevice, 1) {}  LTLIBRARY_INTERFACE;
define_LTLIBRARY_INTERFACE(IBleHDevice, DestroyDeviceHandle) {} LTLIBRARY_DEFINITION;

static bool BleControlWriteCb(LTBleChrHandle hChr) {
    LT_UNUSED(hChr);
    P("debug", "BleControlWriteCb");
    return true;
}

static bool BleControlReadCb(LTBleChrHandle hChr) {
    LT_UNUSED(hChr);
    P("debug", "BleControlReadCb");
    return true;
}

static bool OnMainThreadStart(void) {
    // Setup Device
    LTBleDeviceCtx *deviceData = NULL;
    LTBleSvcCtx *svcData = NULL;
    LTBleChrCtx *testChrCtx = NULL;

    bool return_code = true;
    S.devHandle = S.core->CreateHandle((LTInterface *)&s_IBleHDevice, sizeof(LTBleDeviceCtx));
    if (!S.devHandle) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to create device handle");
        return_code = false;
        goto done;
    }
    deviceData = S.core->ReserveHandlePrivateData(S.devHandle);
    if (!deviceData) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to reserve private data for device handle");
        return_code = false;
        goto done;
    }
    *deviceData = (LTBleDeviceCtx){};
    deviceData->hDev = S.devHandle;
    deviceData->securePairing = false;
    deviceData->numServices = 1;
    deviceData->preferred_mtu = 527;
    deviceData->ble_host_thread_prio = (kLTThread_PriorityHighest - 4);    
    deviceData->hEvent = S.core->CreateEvent(&EventArgs, DispatchEvent, DispatchCompleteProc, NULL, NULL);
    if (!deviceData->hEvent) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to create event for device handle");
        return_code = false;
        goto done;
    }
    S.iEvent->RegisterForEvent(deviceData->hEvent, OnDeviceEvent, NULL, NULL, false);
    if (!S.devBle->Start(S.devHandle)) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to start BLE device");
        return_code = false;
        goto done;
    }
    P("device.done", "Device Configuration is Done");

    // Create BLE RvR Service
    S.svcHandle = S.core->CreateHandle((LTInterface *)&s_IBleHSvc, sizeof(LTBleSvcCtx));
    if (!S.svcHandle) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to create service handle");
        return_code = false;
        goto done;
    }
    deviceData->svc_hdl[0] = S.svcHandle;
    svcData = S.core->ReserveHandlePrivateData(S.svcHandle);
    if (!svcData) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to reserve private data for service handle");
        return_code = false;
        goto done;
    }
    *svcData = (LTBleSvcCtx){};
    svcData->hSvc = S.svcHandle;
    svcData->numCharacteristics = 1;
    svcData->uuid = &RvRSvcUuid;
    svcData->serviceType = LT_BLE_GATT_SVC_TYPE_PRIMARY;
    svcData->hEvent = S.core->CreateEvent(&EventArgs, DispatchEvent, NULL, NULL, NULL);
    if (!svcData->hEvent) {
        return_code = false;
        goto done;
    }
    S.iEvent->RegisterForEvent(svcData->hEvent, OnSvcEvent, NULL, NULL, false);
    P("service.done", "Service Configuration is Done");

    // Create BLE RvR Characteristic
    S.tputTestCharHandle = S.core->CreateHandle((LTInterface *)&s_IBleChrHandle, sizeof(LTBleChrCtx));
    if (!S.tputTestCharHandle) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to create RvR characteristic handle");
        return_code = false;
        goto done;
    }
    svcData->chr_hdl[0] = S.tputTestCharHandle;
    testChrCtx = S.core->ReserveHandlePrivateData(S.tputTestCharHandle);
    if (!testChrCtx) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to reserve private data for Rx RvR characteristic handle");
        return_code = false;
        goto done;
    }
    *testChrCtx = (LTBleChrCtx){};
    testChrCtx->hChr = S.tputTestCharHandle;
    testChrCtx->uuid = &testCharUuid;
    testChrCtx->chrFlags = LT_BLE_GATT_CHR_F_READ | LT_BLE_GATT_CHR_F_WRITE | LT_BLE_GATT_CHR_F_NOTIFY | LT_BLE_GATT_CHR_F_INDICATE | LT_BLE_GATT_CHR_F_WRITE_NO_RSP;
    testChrCtx->hEvent = S.core->CreateEvent(&EventArgs, DispatchEvent, NULL, NULL, NULL);
    if (!testChrCtx->hEvent) {
        return_code = false;
        goto done;
    }
    S.iEvent->RegisterForEvent(testChrCtx->hEvent, OnChrEvent, NULL, NULL, false);
    testChrCtx->readBuf = S.sendbuffer;
    testChrCtx->writeBuf = S.cmdbuffer;
    testChrCtx->readBufSize = sizeof(S.sendbuffer);
    testChrCtx->writeBufSize = sizeof(S.cmdbuffer);
    testChrCtx->readCb = &BleControlReadCb;
    testChrCtx->writeCb = &BleControlWriteCb;
    P("characteristic.done", "Characteristic Configuration is Done");

    // Set Service
    if(!S.devBle->SetService(S.svcHandle)) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to set service");
        return_code = false;
        goto done;
    }

    if (S.txPwr) {
        char cmd[20];
        lt_snprintf(cmd, sizeof(cmd), "pwr-set tx_pwr=%d", S.txPwr);
        if (!S.devBle->SendPrivVenCommand(cmd, NULL, 0)) {
            LTLOG_YELLOWALERT("ble.txpwr.error", "failed to set tx power");
        } else {
            PCONSOLE("ble.txpwr.set", "Set Tx Power to %lu", LT_Pu32(S.txPwr));
        }
    }

done:
    //Error Handeling
    if (deviceData) {
        S.core->ReleaseHandlePrivateData(S.devHandle, deviceData);
    }
    if (svcData) {
        S.core->ReleaseHandlePrivateData(S.svcHandle, svcData);
    }
    if (testChrCtx) {
        S.core->ReleaseHandlePrivateData(S.tputTestCharHandle, testChrCtx);
    }
    return return_code;
}

static void OnMainThreadStop(void) {
    S.devBle->Disconnect(S.devHandle);
    S.devBle->Stop();
    lt_destroyhandle(S.devHandle);
}

static int StartBleRvRApplication(LTSystemSchell *shell, int argc, const char ** argv) {
    LT_UNUSED(shell);
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    S.testThread = S.core->CreateThread("BleRvRMain");
    if (!S.testThread) {
        LTLOG_YELLOWALERT("ble.rvr.main.thread", "Failed to create main thread");
        return -1;
    }
    S.iThread->SetStackSize(S.testThread, 2048);
    S.iThread->Start(S.testThread, OnMainThreadStart, OnMainThreadStop);
    return 0;
}

static int SetAdvertisementPrefix(LTSystemSchell *shell, int argc, const char ** argv) {
    LT_UNUSED(shell);
    if (argc < 2) {
        PLOG("inval.adv.prefix", "Invalid Advertisement Prefix");
        return -1;
    }
    lt_strncpyTerm(S.adv_name_prefix, argv[1], sizeof(S.adv_name_prefix));
    return 0;
}

static int StopBleRvRApplication(LTSystemSchell *shell, int argc, const char ** argv) {
    LT_UNUSED(shell);
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    if (!S.testThread) return 0;
    S.iThread->Terminate(S.testThread);
    S.iThread->WaitUntilFinished(S.testThread, LTTime_Infinite());
    lt_destroyhandle(S.testThread);
    S.testThread = LTHANDLE_INVALID;
    return 0;
}

/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static const LTSystemShell_CommandDesc s_LTSchellCommands[] = {
    { "blervr-advpfix",     SetAdvertisementPrefix,    "Set the dvertisement prefix", NULL },
    { "blervr-start",             StartBleRvRApplication,    "Start BLE RvR Application", NULL },
    { "blervr-stop",              StopBleRvRApplication,    "Stop BLE RvR Application", NULL }
};

static LTSystemShell_CommandTable s_LTSchellCommandTable = {
    .commands    = s_LTSchellCommands,
    .numCommands = sizeof(s_LTSchellCommands) / sizeof(s_LTSchellCommands[0])
};

static void IotServiceBleRvRImpl_LibFini(void) {
    if (S.testThread) {
        S.iThread->Terminate(S.testThread);
        S.iThread->WaitUntilFinished(S.testThread, LTTime_Infinite());
        lt_destroyhandle(S.testThread);
    }
    lt_closelibrary(S.devBle);  // null ok
    if (S.shell) S.shell->API->UnregisterCommands(S.shell, &s_LTSchellCommandTable);
    S = (struct Statics){};
}

static bool IotServiceBleRvRImpl_LibInit(void) {
    const char *msg;
    do {
        S = (struct Statics){};
        msg = "Failed to get LT Core";
        S.core = LT_GetCore();
        if (!S.core) break;
        msg = "Failed to get Console Shell";
        S.shell = LTSystemSchellConsole_GetConsoleShell();
        if (!S.shell) break;
        msg =  "Failed to get Thread Interface";
        S.iThread = lt_getlibraryinterface(ILTThread, S.core);
        if (!S.iThread) break;
        msg = "Failed to get Event Interface";
        S.iEvent = lt_getlibraryinterface(ILTEvent, S.core);
        if(!S.iEvent) break;
        msg = "Failed to get BLE Interface";
        S.devBle = lt_openlibrary(LTDeviceBle);
        if(!S.devBle) break;
        S.txPwr = 15;
        lt_strncpyTerm(S.adv_name_prefix, ADVNAME_PREFIX, sizeof(S.adv_name_prefix));
        S.shell->API->RegisterCommands(S.shell, &s_LTSchellCommandTable);
        msg = NULL;
    } while(0);
    if (msg) {
        LTLOG_REDALERT("fatal", "Init Failure: %s", msg);
        IotServiceBleRvRImpl_LibFini();
        return false;
    }
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

static int IotServiceBleRvRImpl_Run(int argc, const char **argv) {
    LT_UNUSED(argc);
    LT_UNUSED(argv);
    int ret = StartBleRvRApplication(NULL, 0, NULL);
    if (ret != 0) return ret;
    S.iThread->WaitUntilFinished(S.testThread, LTTime_Infinite());
    StopBleRvRApplication(NULL, 0, NULL);
    return 0;
}
typedef_LTLIBRARY_ROOT_INTERFACE(IotServiceBleRvR, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(IotServiceBleRvR, IotServiceBleRvRImpl_Run, 2048) LTLIBRARY_DEFINITION;

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  1-May-24   snandi       created
 
*/
