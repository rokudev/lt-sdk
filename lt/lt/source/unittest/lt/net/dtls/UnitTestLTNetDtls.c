/*******************************************************************************
 * UnitTestLTNetDtls
 *
 * Plain DTLS 1.2 client: handshake, establish session, send encrypted request
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/driver/crypto/LTDriverCrypto.h>
#include <lt/net/core/LTNetCore.h>
#include <lt/net/monitor/LTNetMonitor.h>
#include <lt/system/crashdump/LTSystemCrashdump.h>
#include <lt/system/crypto/LTSystemCrypto.h>
#include <lt/system/settings/LTSystemSettings.h>
#include <tilt/JiltEngine.h>

#define DEFAULTHOST 1

// Dtls
#if DEFAULTHOST == 1
#define remote_ip "192.168.128.9"
static const char *s_SockSpec = "dtls udp host: " remote_ip " port: 4433";
#endif

static u8 s_inBuf[3000];   // buffer to dump data
static bool s_bSocketError = false;
static bool s_bReceived    = false;

static JiltEngine      * s_engine;

// library handlers
static LTCore           *s_core         = NULL;
static LTNetCore        *s_netCore      = NULL;
static LTNetMonitor     *s_netMonitor   = NULL;
static ILTThread        *s_Thread       = NULL;
static LTSocket          s_hSocket      = 0;
static bool              s_bRequestSent = false;
static LTSystemCrypto   *s_crypto       = NULL;
static LTSystemSettings *s_settings     = NULL;

static bool
StringToIPAddress(const char *str, u8 *ip) {
    int  n             = 0;
    int  len           = lt_strlen(str);
    bool octet_started = false;
    ip[0]              = 0;
    ip[1]              = 0;
    ip[2]              = 0;
    ip[3]              = 0;
    for (int i = 0; i < len && n < 4; i++) {
        char c = str[i];
        if (c >= '0' && c <= '9') {
            octet_started = true;
            ip[n] *= 10;
            ip[n] += (c - '0');
        } else if (c == '.') {
            if (!octet_started) {
                return false;
            }
            octet_started = false;
            n++;
        } else {
            return false;
        }
    }
    if (n == 3 && octet_started) return true;
    return false;
}

static void OnSocketEvent(LTSocket hSocket, LTSocket_Event event, void * data) {
    LT_UNUSED(data);
    ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, hSocket);

    s32 len = 0; LT_UNUSED(len);
    switch (event) {
        case kLTSocket_Event_WriteReady:
            if (s_bRequestSent) break;
            s_bRequestSent = true;
            u32 addr = 0;
            StringToIPAddress(remote_ip, (u8*)&addr);
            pSocket->ConnectSocket(hSocket);
            break;

        case kLTSocket_Event_ReadReady:
            /* Important: (1) App should always read all data from TLS read buffer to clear the buffer.
             *                Otherwise, unread data in ready buffer will be overwritten by the next TLS data packet.
             *            (2) LTNetTls limits incoming TLS packet size to 2048 B.
             *                So, do not expect any data more than 2 KB per read event.
             */
            len = pSocket->ReadSocket(hSocket, s_inBuf, sizeof(s_inBuf) - 1);
            s_inBuf[len] = 0;
            s_bReceived = true;
            s_Thread->Terminate(s_Thread->GetCurrentThread());
            break;

        case kLTSocket_Event_ConnectTimeout:
        case kLTSocket_Event_Disconnected:
        case kLTSocket_Event_ReadError:
            pSocket->DisconnectSocket(hSocket);
            s_Thread->Terminate(s_Thread->GetCurrentThread());
            break;

        default:
            break;
    }
}

static void OnNetStatus(LTNetMonitor_Status status, void *clientData) {
    LT_UNUSED(clientData);
    if (status == kLTNetMonitor_Status_NetworkUp && !s_hSocket) {
        s_hSocket = s_netCore->OpenSocket(0, s_SockSpec, OnSocketEvent, NULL);
        if (!s_hSocket) {
            s_bSocketError = true;
        }
        ILTSocket *pSocket = lt_gethandleinterface(ILTSocket, s_hSocket);
        pSocket->ConnectSocket(s_hSocket);
    }
}

static bool OnThreadStart(void) {
    s_netMonitor->OnStatusChange(OnNetStatus, NULL, NULL);
    return true;
}

static void OnThreadExit(void) {
    s_netMonitor->NoStatusChange(OnNetStatus);
}

static void StartTest(void) {
    s_hSocket = 0;
    s_bSocketError = false;
    s_bRequestSent = false;
    s_bReceived = false;

    LTThread mainThread = s_core->CreateThread("dtls_main");
    if (!mainThread) return;
    s_Thread = lt_getlibraryinterface(ILTThread, s_core);
    s_Thread->SetStackSize(mainThread, 2560);
    s_Thread->Start(mainThread, OnThreadStart, OnThreadExit);
    s_Thread->WaitUntilFinished(mainThread, LTTime_Seconds(20)); // LTTime_Infinite()
    lt_destroyhandle(mainThread);
    s_Thread = NULL;
    lt_destroyhandle(s_hSocket);
    s_hSocket = 0;
    s_bRequestSent = false;
}

static void TestDtls(Tilt * tilt) {
    TILT_INFO(tilt, "dtls start");
    StartTest();
    TILT_EXPECT_FALSE(tilt, s_bSocketError, "socket error");
    TILT_EXPECT_TRUE(tilt, s_bReceived, "dtls done");
}

static void BeforeAllTests(Tilt * tilt) {
    // Keep these two libs open to avoid init/fini in underlying crypto and flash drivers.
    s_crypto = lt_openlibrary(LTSystemCrypto);
    s_settings = lt_openlibrary(LTSystemSettings);

    s_core    = LT_GetCore();
    s_netCore = lt_openlibrary(LTNetCore);
    TILT_EXPECT_TRUE(tilt, s_netCore != NULL, "Cannot open LTNetCore");
    s_netMonitor = lt_openlibrary(LTNetMonitor);
    TILT_EXPECT_TRUE(tilt, s_netMonitor != NULL, "Cannot open LTNetMonitor");

    // Set CA public keys
    LTPublicKey s_RokuCaKey = {
        .type     = SIGNATURE_ECDSA_SECP256R1_SHA256,
        .keyLen   = ECDSA_P256_PUBLICKEY_LENGTH,
        .key      = (u8 *)"\x3f\x0f\x32\xdf\xac\x51\x68\xd8\x38\xaa\x5c\xcf\xd4\x93\x30\x71\xb4\x19\xdf\x74\xf0\x57\xd0\x4b\xd0\x84\x6a\x32\x70\xef\x1a\x04\x4c\xa9\x83\x05\xb4\xc0\xa7\xc3\xa1\xb4\xda\x5c\xcd\x99\xd9\x47\x25\xca\xf9\x7c\x1c\x35\x15\x6c\x25\x3a\xaf\xc7\xc1\x5b\xed\x83",
        .keyIdLen = SHA1_HASH_LENGTH,
        .keyId    = (u8 *)"\x8F\xBE\x3B\x01\x56\x02\xA5\x24\x14\xD1\x18\xF4\x34\x84\x34\x1C\x81\x85\x6D\xA2",
    };
    LTDriverCryptoKeyManager *keyManager = lt_createobject(LTDriverCryptoKeyManager);
    keyManager->API->SetCaPublicKey("ROKUCA", &s_RokuCaKey, false);
    lt_destroyobject(keyManager);

    // Obtain time and switch to UTC
    LTTimeBase timebase = (LTTimeBase) {
        .primaryClockTime = LT_GetCore()->GetKernelTime(),
        .secondaryClockTime = LT_GetCore()->GetBuildTime(),
    };
    s_core->SetClockTimeBaseUTC(&timebase);
}

static void AfterAllTests(Tilt * tilt) {
    LT_UNUSED(tilt);
    lt_closelibrary(s_netMonitor);
    lt_closelibrary(s_netCore);
    lt_closelibrary(s_settings);
    lt_closelibrary(s_crypto);
}

static const TiltEngineTestHooks s_hooks = {
    .BeforeAllTests = BeforeAllTests,
    .AfterAllTests  = AfterAllTests
};

static const TiltEngineTest s_tests[] = {
    { TestDtls,      "dtls",     "Test DtLS",      0 },
};

static int UnitTestLTNetDtlsImpl_Run(int argc, const char **argv) {
    /* Set test properties */
    s_engine->API->ConfigureTestSuite(s_engine, s_tests, sizeof(s_tests)/sizeof(s_tests[0]), &s_hooks);

    /* Invoke testing */
    return s_engine->API->RunTestSuite(s_engine, argc, argv);
}

static bool UnitTestLTNetDtlsImpl_LibInit(void) {
    s_engine = lt_createobject(JiltEngine);
    return s_engine != NULL;
}

static void UnitTestLTNetDtlsImpl_LibFini(void) {
    lt_destroyobject(s_engine);
}

typedef_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetDtls, 1) LTLIBRARY_EMPTY_INTERFACE;

define_LTLIBRARY_ROOT_INTERFACE(UnitTestLTNetDtls, UnitTestLTNetDtlsImpl_Run, 1536) LTLIBRARY_DEFINITION;
