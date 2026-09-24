/******************************************************************************
 * Esp32_Console.c                                                 ESP32-P4 BSP
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <lt/core/bsp/LTCoreBSP.h>

#include "Esp32_Irq.h"
#include "Esp32_Registers.h"
#include "Esp32_SoC.h"
#include "Esp32_Console.h"

/*
 * UART0 on GPIO37 and GPIO38, at whatever baud rate the ROM left configured.
 * The board brings those two pads out through a USB-UART bridge, so the console
 * is already up and running by the time this code sees it and there is nothing
 * to configure but the receive interrupt.
 *
 * This is the esp32's console rather than the esp32c3's: the esp32p4 does have a
 * USB serial/JTAG peripheral, but nothing on this board's console path uses it.
 */

/*___________________
  static variables */

static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;

static const u32 s_rxInterruptMask =   1 << ESP32_REG_SHIFT(UART_INT, RXFIFO_FULL)
                                     | 1 << ESP32_REG_SHIFT(UART_INT, RXFIFO_TOUT);

enum { kESP_UART_FifoLength = kEsp32_RegisterUART_FIFO_LENGTH };

#define ESP32_UART_BIT_VALUE(r, f) ((ESP32_UART_REG(0, r) & ESP32_REG_MASK(UART_ ## r, f)) >> ESP32_REG_SHIFT(UART_ ## r, f))

/*______________________________
  unbuffered console putchars */
void LT_ISR_SAFE Esp32_ConsolePutChars(const char * pChars, u32 nChars) {
    u32 nCharsInFifo;
    while (nChars) {
        while ((nCharsInFifo = ESP32_UART_BIT_VALUE(STATUS, TXFIFO_CNT)) >= kESP_UART_FifoLength);
        nCharsInFifo = kESP_UART_FifoLength - nCharsInFifo;             /* now nCharsInFifo is nChars available in fifo */
        if (nCharsInFifo > nChars) nCharsInFifo = nChars;               /* now nCharsInFiFo is how many we're going to write */
        nChars -= nCharsInFifo;                                         /* mark we wrote them */
        while (nCharsInFifo--) ESP32_UART_REG(0, FIFO) = *pChars++;     /* write them */
    }
}

/* ____________________________________________________________________________________________________________
   UART interrupt service routine - handles all UART interrupts and dispatches according to interrupt source */
static u32 LT_ISR_SAFE
Esp32_ConsoleISR(void) {
    static char s_tempBuff[kESP_UART_FifoLength];
    if (ESP32_UART_REG(0, INT_ST) & s_rxInterruptMask) {
        u32 nCount = 0, sent = 0;
        ESP32_UART_REG(0, INT_CLR) |= s_rxInterruptMask;
        while (1) {
            /*
             * STATUS.RXFIFO_CNT is the whole occupancy here.  The esp32 file
             * beside this one has to consult a second register because its
             * counter is narrower than its FIFO; this one's is eight bits for a
             * 128 byte FIFO.
             */
            while ((nCount < kESP_UART_FifoLength) && ESP32_UART_BIT_VALUE(STATUS, RXFIFO_CNT)) {
                s_tempBuff[nCount++] = (ESP32_UART_REG(0, FIFO) & 0xFF);
            }
                 if (nCount) { s_pCoreCallbacks->ProcessISRConsoleInputChars(s_tempBuff, nCount); sent = 1; }
            else if (sent)   { s_pCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);         sent = 0; }
            else break;
            nCount = 0;
        }
    }
    return 0;
}

/*_________________________
  console initialization */
void Esp32_ConsoleInitialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    /* hook up the UART char receive interrupt after setting s_pCoreCallbacks! */
    s_pCoreCallbacks = pCallbacks;

    ESP32_UART_REG(0, INT_ENA) = 0;        /* disable all UART0 interrupts */
    ESP32_UART_REG(0, INT_CLR) = 0xffffffff;

    /*
     * Attached through the BSP's own table rather than through
     * pCallbacks->SetInterruptVector().  LTKSetInterruptVector() is a no-op in
     * the RISC-V port - the whole notion of a per-source vector lives here.
     */
    Esp32_AttachInterrupt(kEsp32_ExternalIrq_UART0, kEsp32_IrqNumber_UART0,
                          kEsp32_IrqType_Level, kEsp32_IrqPriority_UART0, Esp32_ConsoleISR);

    ESP32_UART_REG(0, INT_ENA) |=  s_rxInterruptMask; /* enable receive interrupt */
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  23-Sep-26   claudius    created
 */
