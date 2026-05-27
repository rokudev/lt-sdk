/******************************************************************************
 * LTCoreBSP for ESP32
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/bsp/LTCoreBSP.h>

#include "esp32/Esp32_Irq.h"
#include "esp32/Esp32_Registers.h"
#include "esp32/Esp32_SoC.h"

#include <setjmp.h>

/*_______________________
  forward declarations */
static const LTCoreBSP s_bsp;

/*___________________
  static variables */

static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;
static LTAtomic                             s_LTCoreBSPInitialized = { 0 };

static const u32 s_rxInterruptMask =   1 << ESP32_REG_SHIFT(UART_INT, RXFIFO_FULL)
                                     | 1 << ESP32_REG_SHIFT(UART_INT, RXFIFO_TOUT);
enum { kESP_UART_FifoLength = 128 };

#define ESP32_UART_BIT_VALUE(r, f) ((ESP32_UART_REG(0, r) & ESP32_REG_MASK(UART_ ## r, f)) >> ESP32_REG_SHIFT(UART_ ## r, f))

/*___________________
   Heap definition  */

extern int _heap0_start;
extern int _heap0_end;
extern int _heap1_start;
extern int _heap1_end;
extern int _heap2_start;
extern int _heap2_end;
extern int _heap3_start;
extern int _heap3_end;

#define ESP32_NUM_HEAP_REGIONS      4
static LTCoreBSP_HeapRegion s_heapRegions[ESP32_NUM_HEAP_REGIONS];
static const LTCoreBSP_LTHeapConfig LTHeapConfig = { ESP32_NUM_HEAP_REGIONS, s_heapRegions };
#define HEAP_REGION_SIZE(n) (u32)(((u8*)&_heap##n##_end) - ((u8*)&_heap##n##_start))

/*___________________
  BSP configuration */
static const LTCoreBSP_Xtensa_SystemConfig LTSystemConfig = {
    .nClockSpeedHz      = 240000000
};

/*______________________________
  unbuffered console putchars */
static void LT_ISR_SAFE LTCoreBSP_PutCharsToConsole(const char * pChars, u32 nChars) {
    u32 nCharsInFifo;
    while (nChars) {
        while ((nCharsInFifo = (ESP32_UART_REG(0, STATUS) >> ESP32_REG_SHIFT(UART_STATUS, TXFIFO_CNT) & 0xFF)) >= kESP_UART_FifoLength);
        nCharsInFifo = kESP_UART_FifoLength - nCharsInFifo;             /* now nCharsInFifo is nChars available in fifo */
        if (nCharsInFifo > nChars) nCharsInFifo = nChars;               /* now nCharsInFiFo is how many we're going to write */
        nChars -= nCharsInFifo;                                         /* mark we wrote them */
        while (nCharsInFifo--) ESP32_UART_REG(0, FIFO) = *pChars++;     /* write them */
    }
}

/* ____________________________________________________________________________________________________________
   UART interrupt service routine - handles all UART interrupts and dispatches according to interrupt source */
static void LT_ISR_SAFE
LTCoreBSP_UARTISR(void) {
    static char s_tempBuff[kESP_UART_FifoLength];
    if (ESP32_UART_REG(0, INT_ST) & s_rxInterruptMask) {
        u32 nCount = 0, sent = 0;
        ESP32_UART_REG(0, INT_CLR) |= s_rxInterruptMask;
        while (1) {
            while ((nCount < kESP_UART_FifoLength) && (ESP32_UART_BIT_VALUE(STATUS, RXFIFO_CNT) || ESP32_UART_BIT_VALUE(MEM_CNT_STATUS, RX_MEM_CNT))) {
                s_tempBuff[nCount++] = (ESP32_UART_REG(0, FIFO) & 0xFF);
            }
                 if (nCount) { s_pCoreCallbacks->ProcessISRConsoleInputChars(s_tempBuff, nCount); sent = 1; }
            else if (sent)   { s_pCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);         sent = 0; }
            else break;
            nCount = 0;
        }
    }
}

/*_____________________
  BSP initialization */
const LTCoreBSP *
LTCoreBSP_Initialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    if (LTAtomic_Load(&s_LTCoreBSPInitialized)) return NULL; /* don't let anyone come in here except LTCore the first time */
    LTAtomic_Store(&s_LTCoreBSPInitialized, 1); /* don't need CompareAndExchange, LTCore calls this before any threads are running */

    s_pCoreCallbacks = pCallbacks;
    Esp32MapExternalToCPUIrq(0, kEsp32_ExternalIrq_EMAC, kEsp32_IrqNumber_EMAC);
    /* hook up the UART char receive interrupt after setting s_pCoreCallbacks! */
    ESP32_UART_REG(0, INT_ENA) = 0;        /* disable all UART0 interrupts */
    Esp32MapExternalToCPUIrq(0, kEsp32_ExternalIrq_UART0, kEsp32_IrqNumber_UART0);
    pCallbacks->SetInterruptVector(kEsp32_IrqNumber_UART0, LTCoreBSP_UARTISR, kEsp32_IrqPriority_UART0);
    ESP32_UART_REG(0, INT_ENA) |=  s_rxInterruptMask; // enable receive interrupt

    s_heapRegions[0] = (LTCoreBSP_HeapRegion) { (u8*)&_heap0_start, HEAP_REGION_SIZE(0), false };
    s_heapRegions[1] = (LTCoreBSP_HeapRegion) { (u8*)&_heap1_start, HEAP_REGION_SIZE(1), false };
    s_heapRegions[2] = (LTCoreBSP_HeapRegion) { (u8*)&_heap2_start, HEAP_REGION_SIZE(2), false };
    s_heapRegions[3] = (LTCoreBSP_HeapRegion) { (u8*)&_heap3_start, HEAP_REGION_SIZE(3), false };

    return &s_bsp;
}

void
LTCoreBSP_Finalize(const LTCoreBSP * pBSP) {
    if ((! LTAtomic_Load(&s_LTCoreBSPInitialized)) || (pBSP != &s_bsp)) return; /* don't let anyone except LTCore in here */
    s_pCoreCallbacks = NULL;
    LTAtomic_Store(&s_LTCoreBSPInitialized, 0);
}

/*____________
  debugging */
static bool LT_ISR_SAFE
LTCoreBSP_DebugAssertFailed(const char * pFile, int nLine, const char * pTest) {
    LT_UNUSED(pFile);
    LT_UNUSED(nLine);
    LT_UNUSED(pTest);
    #if 1
        return true;   /* DRW 07-Feb-23 : always do asserts, even in release mode now */
    #else
        #ifdef LT_DEBUG
            /* return true to trap to debugger on assert - may be used to implement abort/continue prompt */
            return true;
        #else
            return false;
        #endif
    #endif
}

/*____________
  wrappers  */
void __wrap_longjmp(jmp_buf buf, int nVal) {
    LT_UNUSED(buf);
    LT_UNUSED(nVal);
    /* longjmp shouldn't be needed, let's assert instead */
    esp_rom_printf("longjmp not supported.\n");
    while (1);
}


/*_____________________________
  LTCoreBSP interface struct */
static const LTCoreBSP s_bsp = {

    /* LT Configuration */
    .pLTSystemConfig = &LTSystemConfig,
    .pLTHeapConfig   = &LTHeapConfig,

    /* BSP Functions */
    .PutCharsToConsole  = LTCoreBSP_PutCharsToConsole,
    .DebugAssertFailed  = LTCoreBSP_DebugAssertFailed,

};

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  25-Mar-22   tiberius    created
 */
