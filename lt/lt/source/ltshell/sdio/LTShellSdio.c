/*******************************************************************************
 *
 * LTShellSdio: Shell handler for LTDeviceSdio library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/
#include <lt/LT.h>
#include <lt/system/ipc/buffermanager/LTIPCBufferManager.h>
#include <lt/system/shell/LTSystemShell.h>
#include <lt/device/sdio/LTDeviceSdioPort.h>

DEFINE_LTLOG_SECTION("ltshell.sdio");


/*******************************************************************************
 * Static Variables & Types
 ******************************************************************************/

enum {
    // There's currently no cetralized place to define these values so be careful to not overlap
    kSDIOPortNumber_ShellSdio   = 15,
    kSDIOStressTest_BlockSize   = 251,  // how often the buffer contents repeat; last prime number before 255
    kSDIOStressTest_BufferSize  = (kLTDeviceSdioPort_BlockSize * 10),
    kSDIOStressTest_ReportTime  = 2,    // seconds
};

/* Static references to LT libraries and interfaces, opened for long periods of time */
static struct Statics {
    LTCore *core;
    LTSystemShell *shell;
    LTDeviceSdioPort *sdioPort;
    LTOThread *sdioReadThread;
    LTOThread *sdioWriteThread;

    LTTime rxStartTime;
    u8 nextRxTestByte;
    u8 rxMissingReports;
    u32 cumulativeRxBytes;
    u32 cumulativeRxErrors;
    u32 lastReportedRxBytes;

    LTTime txStartTime;
    u8 nextTxTestByte;
    u32 cumulativeTxBytes;
    u32 sdioFailures;
    u32 prevSdioFailures;
    bool txStarted;

    ILTShell *iShell;

    LTIPCBufferManager *bufMgr;
} S;

static u8 rxBuffer[kSDIOStressTest_BufferSize];
static u8 txBuffer[kSDIOStressTest_BufferSize];


/*******************************************************************************
 * Shell thread to handle SDIO read operations
 ******************************************************************************/

static void ReadReportProc(void *pClientData) {
    LT_UNUSED(pClientData);

    // The timer shouldn't start until the first packet is received, but anyway
    // return early if there are no bytes logged
    if (!S.cumulativeRxBytes) {
        return;
    }

    // Log ongoing test status. Stop reporting if no new data received after several reporting periods
    if (S.cumulativeRxBytes == S.lastReportedRxBytes) {
        S.rxMissingReports++;
        if (S.rxMissingReports > 5) {
            S.sdioReadThread->API->KillTimer(S.sdioReadThread, ReadReportProc, NULL);
            S.rxStartTime = LTTimeInitializer_Zero();
            LTLOG("rx.report.done", "No new data received for 5 reporting periods, stopping test");
            return;
        }
    } else {
        S.lastReportedRxBytes = S.cumulativeRxBytes;
    }

    LTTime now = S.core->GetKernelTime();
    LTTime delta = LTTime_Subtract(now, S.rxStartTime);
    u32 bitsPerSec = (S.cumulativeRxBytes / LTTime_GetSeconds(delta)) * 8;
    u32 errorRate = (S.cumulativeRxErrors * 100000) / S.cumulativeRxBytes;
    u32 errorsInt = errorRate / 1000;
    u32 errorsFrac = errorRate % 1000;
    LTLOG("rx.report", "Average: %lu bps -- BER: %02lu.%03lu", LT_Pu32(bitsPerSec), LT_Pu32(errorsInt), LT_Pu32(errorsFrac));
}

static u32 validateRxBuffer(void) {
    u32 failCount = 0;

    for (u32 i = 0; i < sizeof(rxBuffer); i++) {
        if (rxBuffer[i] != S.nextRxTestByte++) {
            failCount++;
        }
        if (S.nextRxTestByte >= kSDIOStressTest_BlockSize) {
            S.nextRxTestByte = 0;
        }
    }
    return failCount;
}

static void OnSdioPerformReadCallback(LTDeviceSdioPort *port, u32 nBytes, void *clientData) {
    LT_UNUSED(clientData);

    if (nBytes > sizeof(rxBuffer)) {
        LTLOG_YELLOWALERT("sdio.read.fail.size", "Received more bytes than expected");
        return;
    }

    if (!port->API->Read(port, rxBuffer)) {
        LTLOG_YELLOWALERT("sdio.read.fail", "Failed to read RX data");
    }

    if (LTTime_IsZero(S.rxStartTime)) {
        // Reset start time, and sync expected byte to first byte of buffer
        S.rxStartTime = S.core->GetKernelTime();
        S.nextRxTestByte = rxBuffer[0];
        S.rxMissingReports = 0;
        S.cumulativeRxBytes = 0;
        S.cumulativeRxErrors = 0;
        S.lastReportedRxBytes = 0;
        S.sdioReadThread->API->SetTimer(S.sdioReadThread, LTTime_Seconds(kSDIOStressTest_ReportTime), ReadReportProc, NULL, NULL);
    }

    // Validate buffer contents
    S.cumulativeRxBytes += nBytes;
    S.cumulativeRxErrors += validateRxBuffer();
}

static bool LTShellSdio_ReadThreadInit(void) {
    // Read thread owns the SDIO port
    if (!(S.sdioPort = lt_createobject(LTDeviceSdioPort))) {
        LTLOG("err.sdio.create", "Failed to create LTDeviceSdioPort");
        return false;
    }
    if (!S.sdioPort->API->Bind(S.sdioPort, kSDIOPortNumber_ShellSdio, OnSdioPerformReadCallback, NULL)) {
        LTLOG("err.sdio.bind", "Failed to bind to SDIO port %d", kSDIOPortNumber_ShellSdio);
        return false;
    }

    // NOTE: Read testing is auto-start and auto-stop after shell is loaded

    return true;
}

static void LTShellSdio_ReadThreadExit(void) {
    S.sdioReadThread->API->KillTimer(S.sdioReadThread, ReadReportProc, NULL);
    lt_destroyobject(S.sdioPort);
    S.sdioPort = NULL;
}

/*******************************************************************************
 * Shell thread to handle SDIO write operations
 ******************************************************************************/

static void buildTxBuffer(void) {
    for (u32 i = 0; i < sizeof(txBuffer); i++) {
        txBuffer[i] = S.nextTxTestByte++;
        if (S.nextTxTestByte >= kSDIOStressTest_BlockSize) {
            S.nextTxTestByte = 0;
        }
    }
}

static void WriteTestProc(void *pClientData) {
    LT_UNUSED(pClientData);

    // Periodically send a chunk of data over the SDIO port
    buildTxBuffer();
    if (!S.sdioPort->API->Write(S.sdioPort, txBuffer, sizeof(txBuffer))) {
        // Don't log here to avoid spamming the console
        S.sdioFailures++;
    } else {
        S.cumulativeTxBytes += sizeof(txBuffer);
    }
}

static void StressTxStopProc(void *pClientData);    // Forward-decl

static void WriteReportProc(void *pClientData) {
    LT_UNUSED(pClientData);

    LTTime now = S.core->GetKernelTime();
    LTTime delta = LTTime_Subtract(now, S.txStartTime);
    u32 bitsPerSec = (S.cumulativeTxBytes / LTTime_GetSeconds(delta)) * 8;
    LTLOG("tx.report", "Average: %lu bps", LT_Pu32(bitsPerSec));

    if (S.sdioFailures != S.prevSdioFailures) {
        LTLOG("tx.report.fail", "SDIO write failures: %lu", LT_Pu32(S.sdioFailures));
        if (S.sdioFailures > 100) {
            // Abort the test if too many failures
            LTLOG("tx.abort", "Aborting due to too many SDIO failures");
            StressTxStopProc(NULL);
        }
        S.prevSdioFailures = S.sdioFailures;
    }
}

static void StressTxStartProc(void *pClientData) {
    u32 gap = (u32)pClientData;

    S.txStartTime = S.core->GetKernelTime();
    S.nextTxTestByte = 0;
    S.cumulativeTxBytes = 0;
    S.sdioFailures = 0;
    S.prevSdioFailures = 0;

    S.sdioWriteThread->API->SetTimer(S.sdioWriteThread, LTTime_Milliseconds(gap), WriteTestProc, NULL, NULL);
    S.sdioWriteThread->API->SetTimer(S.sdioWriteThread, LTTime_Seconds(kSDIOStressTest_ReportTime), WriteReportProc, NULL, NULL);

    gap = kSDIOStressTest_BufferSize * 8 * 1000 / gap;
    LTLOG("start.ok", "TX stress test started: nominal rate: %lu bps", LT_Pu32(gap));
    S.txStarted = true;
}

static void StressTxStopProc(void *pClientData) {
    LT_UNUSED(pClientData);

    S.sdioWriteThread->API->KillTimer(S.sdioWriteThread, WriteTestProc, NULL);
    S.sdioWriteThread->API->KillTimer(S.sdioWriteThread, WriteReportProc, NULL);

    // Trigger one last TX report before we finish
    WriteReportProc(NULL);
    LTLOG("stop.ok", "TX stress test stopped");
    S.txStarted = false;
}

void OnBufferStatus(LTBufferID bufferID, LTBuffer *ipcBuf, LTIPCBufferManagerStatus status, void *clientData) {
    LT_UNUSED(ipcBuf);
    LT_UNUSED(clientData);
    switch(status) {
        case LTIPCBufferManagerStatus_MoreData:
            LTLOG("moredata", "Buffer 0x%08lx has more data", LT_Pu32(bufferID));
            break;
        case LTIPCBufferManagerStatus_DataConsumed:
            LTLOG("dataconsumed", "Buffer 0x%08lx has data consumed", LT_Pu32(bufferID));
            break;
        case LTIPCBufferManagerStatus_BufferLoss:
            LTLOG("bufferloss", "Buffer 0x%08lx has been lost", LT_Pu32(bufferID));
            break;
    }
}

static void BufEventLogging(void *pClientData) {
    LTBuffer *ipcBuf = pClientData;

    // Request data events for the buffer
    S.bufMgr->API->OnStatusChange(ipcBuf, OnBufferStatus, NULL, NULL);
}

static void BufDestroyProc(LTThread_ReleaseReason releaseReason, void *pClientData) {
    LT_UNUSED(releaseReason);
    lt_destroyobject(pClientData);
    LTLOG("buftest.cleanup", "Destroyed IPC buffer object");
}

static void BufDestroyTimeoutProc(void *pClientData) {
    // Just kill the timer - this will trigger the client data release proc above, which will destroy the buffer
    S.sdioReadThread->API->KillTimer(S.sdioReadThread, BufDestroyTimeoutProc, pClientData);
}

static void BufTestProc(void *pClientData) {
    u32 size = (u32)pClientData;

    LTLOG("buftest.create", "---------- Creating remote buffer of size %lu ----------", LT_Pu32(size));
    LTBufferID bufferID = S.bufMgr->API->CreateRemoteSourceBuffer(size);
    if (bufferID == LT_BUFFER_ID_INVALID) {
        LTLOG("buftest.fail", "Failed to create remote buffer");
        return;
    }

    LTLOG("buftest.hnd", "Created remote buffer ID: 0x08%lx", LT_Pu32(bufferID));

    LTBuffer *buffer = S.bufMgr->API->AccessBuffer(bufferID);
    if (!buffer) {
        LTLOG("buftest.fail", "Failed to access buffer");
        S.bufMgr->API->DestroySourceBuffer(bufferID);
        return;
    }

    // This thread will hang until the entire transfer completes, so we need to move
    // the buffer event logging to another thread
    S.sdioReadThread->API->QueueTaskProcIfRequired(S.sdioReadThread, BufEventLogging, NULL, buffer);

    // Fill the remote buffer as fast as possible, then read it back and check for accuracy
    LTLOG("buftest.write", "---------- Writing to remote buffer ----------");
    const u32 FRAG_SIZE = (1024 - 8);    // hard-coded transport block size, not sure if there's a non-test need for this

    // For testing with fixed buffer contents pre-fill the buffer
    u8 nextTestByte = 0;
    for (u32 i = 0; i < FRAG_SIZE; i++) {
        txBuffer[i] = nextTestByte++;
        if (nextTestByte >= kSDIOStressTest_BlockSize) {
            nextTestByte = 0;
        }
    }

    LTTime startTime = S.core->GetKernelTime();
    u32 remaining = size;
    nextTestByte = 0;
    while (remaining) {
        u32 fragSize = LT_MIN(remaining, FRAG_SIZE);
        LTBufferSpan span = { .data = txBuffer, .size = fragSize };
        buffer->API->Write(buffer, span);
        remaining -= fragSize;
    }
    LTTime writeTime = S.core->GetKernelTime();

    LTLOG("buftest.read", "---------- Reading from remote buffer ----------");
    remaining = size;
    nextTestByte = 0;
    while (remaining) {
        u32 fragSize = LT_MIN(remaining, FRAG_SIZE);
        LTBufferSpan span = { .data = txBuffer, .size = fragSize };
        span = buffer->API->Read(buffer, span);
        if (span.size == 0) {
            LTLOG("buftest.fail", "Failed to read any bytes - possible buffer loss");
            break;
        }
        remaining -= span.size;
    }
    LTTime readTime = S.core->GetKernelTime();

    LTTime delta = LTTime_Subtract(writeTime, startTime);
    u64 bitsPerSec = (u64)size * 8000 / LTTime_GetMilliseconds(delta);
    LTLOG("write.report", "Duration: %lu.%06lu.%03lu secs. Average: %lu bps",
        LT_Pu32(LTTime_GetSeconds(delta)),
        LT_Pu32(LTTime_GetMicroseconds(LTTime_FractionalSeconds(delta))),
        LT_Pu32(LTTime_GetNanoseconds(LTTime_FractionalMicroseconds(delta))),
        LT_Pu32(bitsPerSec));

    delta = LTTime_Subtract(readTime, writeTime);
    bitsPerSec = (u64)size * 8000 / LTTime_GetMilliseconds(delta);
    LTLOG("read.report", " Duration: %lu.%06lu.%03lu secs. Average: %lu bps",
        LT_Pu32(LTTime_GetSeconds(delta)),
        LT_Pu32(LTTime_GetMicroseconds(LTTime_FractionalSeconds(delta))),
        LT_Pu32(LTTime_GetNanoseconds(LTTime_FractionalMicroseconds(delta))),
        LT_Pu32(bitsPerSec));

    if (!S.bufMgr->API->DestroySourceBuffer(bufferID)) {
        LTLOG("buftest.cleanup", "Failed to destroy remote buffer");
    }

    // Delay destroying the IPC access buffer to ensure the buffer loss event can be received and logged.
    //
    // NOTE: This pattern is only used here for the purposes of testing the BufferLoss event can be
    // received after the underlying source buffer has been destroyed. In a typical application there is
    // no need to push this via a timer or queued proc - you would just directly call lt_destroyobject().
    //
    S.sdioReadThread->API->SetTimer(S.sdioReadThread, LTTime_Seconds(1), BufDestroyTimeoutProc, BufDestroyProc, buffer);
}

static bool LTShellSdio_WriteThreadInit(void) {
    return true;
}

static void LTShellSdio_WriteThreadExit(void) {
    StressTxStopProc(NULL);
}


/*******************************************************************************
 * Shell Interface
 ******************************************************************************/

static int LTShellSdio_Stress(LTShell hShell, int argc, const char *argv[]) {
    if (argc < 2 || argc > 3) {
        S.iShell->Print(hShell, "stress [start gap_msec | stop]\n");
        return -1;
    }

    if (!S.sdioReadThread || !S.sdioWriteThread) {
        S.iShell->Print(hShell, "SDIO threads not initialized\n");
        return -1;
    }

    if (!lt_strcasecmp(argv[1], "start")) {
        if (argc != 3) {
            S.iShell->Print(hShell, "stress start requires a gap in milliseconds\n");
            return -1;
        }

        u32 gap = lt_strtou32(argv[2], NULL, 0);

        if (!S.txStarted) {
            S.sdioWriteThread->API->QueueTaskProcIfRequired(S.sdioWriteThread, StressTxStartProc, NULL, (void*)gap);
        } else {
            S.iShell->Print(hShell, "Stress test already running\n");
        }

    } else if (!lt_strcasecmp(argv[1], "stop")) {
        if (S.txStarted) {
            S.sdioWriteThread->API->QueueTaskProcIfRequired(S.sdioWriteThread, StressTxStopProc, NULL, NULL);
        } else {
            S.iShell->Print(hShell, "Stress test not running\n");
        }

    } else {
        S.iShell->Print(hShell, "stress [start|stop]\n");
        return -1;
    }

    return 0;
}

static int LTShellSdio_BufTest(LTShell hShell, int argc, const char *argv[]) {
    if (argc != 2) {
        S.iShell->Print(hShell, "buftest <bufsize>\n");
        return -1;
    }

    if (!S.sdioWriteThread) {
        S.iShell->Print(hShell, "SDIO threads not initialized\n");
        return -1;
    }

    if (!S.bufMgr) {
        S.iShell->Print(hShell, "ERROR: LTIPCBufferManager not available\n");
        return -1;
    }

    u32 size = lt_strtou32(argv[1], NULL, 0);
    S.sdioWriteThread->API->QueueTaskProcIfRequired(S.sdioWriteThread, BufTestProc, NULL, (void*)size);

    return 0;
}

/* Forward-decl for SdioCommands array due to circular references */
static int LTShellSdio_Help(LTShell hShell, int argc, const char *argv[]);

static const LTSystemShell_CommandDesc SdioCommands[] = {
    { "help",       LTShellSdio_Help,       "- list supported commands", NULL },
    { "stress",     LTShellSdio_Stress,     "- run a stress test on the SDIO transport", NULL },
    { "buftest",    LTShellSdio_BufTest,    "- LTIPCBufferManager testing", NULL },
    { }
};

static int LTShellSdio_Help(LTShell hShell, int argc, const char *argv[]) {
    LT_UNUSED(argv);

    for (int n = 0; SdioCommands[n].pCommand; n++) {
        S.iShell->Print(hShell, "  %8s %s\n", SdioCommands[n].pCommand, SdioCommands[n].pDescription);
    }
    if (argc < 2) {
        return 0;
    } else {
        return -1;
    }
}

static int LTShellSdio_Handler(LTShell hShell, int argc, const char *argv[]) {
    if (argc > 1) {
        for (int n = 0; SdioCommands[n].pCommand; n++) {
            if (0 == lt_strcasecmp(SdioCommands[n].pCommand, argv[1])) {
                return SdioCommands[n].pCommandProc(hShell, argc-1, argv+1);
            }
        }
    }

    // Command not found so just display help
    return LTShellSdio_Help(hShell, 0, NULL);
}

static void LTShell_Help(LTShell hShell, int argc, const char ** argv) {
    /* This LTShell Help proc is so "help sdio" works in addition to "sdio help".
    * It prints usage and calls the regular LTShellSdio_Help
    */
    S.iShell->Print(hShell, "usage: sdio <command> [args]\nCommands:\n");
    (void)LTShellSdio_Help(hShell, argc, argv);
}

static const LTSystemShell_CommandDesc LTShellSdio_Commands[] = {
    { "sdio", LTShellSdio_Handler, "Shell commands related to the SDIO device", LTShell_Help },
};


/*******************************************************************************
 * Library Standard Functions
 ******************************************************************************/
static void LTShellSdioImpl_LibFini(void) {
    lt_destroyobject(S.bufMgr);
    // Destroy write thread before read thread, so it can't use the port owned by reader
    lt_destroyobject(S.sdioWriteThread);
    lt_destroyobject(S.sdioReadThread);

    if (S.shell) {
        S.shell->UnregisterCommands(LTShellSdio_Commands);
        lt_closelibrary(S.shell);
    }
    S = (struct Statics) {};
}

static bool LTShellSdioImpl_LibInit(void) {
    S = (struct Statics) {
        .core = LT_GetCore(),
    };

    const char *reason;
    do {
        reason = "no shell";
        if (!(S.shell = lt_openlibrary(LTSystemShell))) break;
        S.shell->RegisterCommands(LTShellSdio_Commands, sizeof(LTShellSdio_Commands) / sizeof(LTShellSdio_Commands[0]));
        S.iShell = lt_getlibraryinterface(ILTShell, S.shell);

        reason = "read thread failed";
        if (!(S.sdioReadThread = lt_createobject(LTOThread))) break;
        S.sdioReadThread->API->SetStackSize(S.sdioReadThread, 1024);
        S.sdioReadThread->API->Start(S.sdioReadThread, "shellSdioRead", LTShellSdio_ReadThreadInit, LTShellSdio_ReadThreadExit);

        reason = "write thread failed";
        if (!(S.sdioWriteThread = lt_createobject(LTOThread))) break;
        S.sdioWriteThread->API->SetStackSize(S.sdioWriteThread, 1024);
        S.sdioWriteThread->API->Start(S.sdioWriteThread, "shellSdioWrite", LTShellSdio_WriteThreadInit, LTShellSdio_WriteThreadExit);

        reason = "buffer manager failed";
        if (!(S.bufMgr = lt_createobject(LTIPCBufferManager))) break;

        return true;
    } while (false);

    LTLOG_YELLOWALERT("init.fail", "init failed: %s", reason);
    LTShellSdioImpl_LibFini();
    return false;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellSdio, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellSdio) LTLIBRARY_DEFINITION;
