/******************************************************************************
 * esp32s3/Esp32_Console.c                                         ESP32-S3 BSP
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
#include "Esp32_Clock.h"
#include "Esp32_Console.h"

/*
 * The console here is the USB serial/JTAG device, not UART0.
 *
 * The esp32s3 has a USB full speed PHY on GPIO19 and GPIO20 with a CDC-ACM
 * device permanently wired behind it, so a board can present a console over the
 * one cable that also powers and flashes it, and most do: the board this port
 * targets brings D+/D- out to its only connector and fits no USB to UART bridge
 * at all, so USB is the only console it has without soldering.  UART0 on
 * GPIO43/44 is not dead - both pads are brought out to the board's reserved
 * debug pads - but reaching it costs two wires and an adapter, so nothing that
 * has to work on an untouched board can depend on it.  The ROM bootloader
 * already prints its banner over USB, and this file continues where it left off.
 *
 * Two consequences follow from the far end being a USB host rather than a wire.
 *
 * The first is that transmission is packet at a time, not byte at a time.  The
 * IN endpoint holds exactly one 64 byte bulk packet; bytes are pushed into it
 * for as long as IN_DATA_FREE says there is room, and WR_DONE then offers the
 * packet to the host.
 *
 * The second is that the far end may simply not be there.  An absent host, or a
 * host that has enumerated the device but not opened the port, never collects
 * that packet, so IN_DATA_FREE goes to zero on the 64th byte and stays there for
 * as long as the board is powered.  A console that waited on it would hang the
 * system on its first LTLOG line, which is exactly the sort of failure a console
 * exists to diagnose - so this one drops output instead.  See below.
 *
 * The receive path has the mirror image of that problem, and it is the host that
 * pays for it.  The OUT endpoint does not drain itself: bytes from the host sit
 * in the RX FIFO until software reads them, and while they sit there the endpoint
 * NAKs every further OUT packet, so the *host's* write never completes.  Reading
 * the FIFO empty is what re-arms it.  On Linux a terminal caught that way wedges
 * inside tty_wait_until_sent() - which waits forever - while holding the port's
 * exclusive lock, so it takes the console away from every later attempt too, and
 * one stray keystroke costs the log until the process is killed by hand.
 *
 * Esp32_ConsoleISR() is what normally keeps the endpoint moving, but nothing here
 * relies on it being the only thing that does: Esp32_ConsolePutChars() runs the
 * same drain, so output alone is enough to keep the endpoint - and therefore the
 * console - alive.  See Esp32_ConsolePumpRx().
 */

/*___________________
  constants        */
enum {
    /* Both endpoints move one USB full speed bulk packet at a time */
    kEsp32_ConsolePacketSize    = kEsp32_RegisterUSB_SERIAL_JTAG_PACKET_SIZE,

    /*
     * How long a write waits for the host to collect a packet before writing the
     * host off.  A host that has the port open polls a bulk endpoint at least
     * once per 1ms USB frame, and each iteration below is an APB register read,
     * so this is some tens of milliseconds - long enough to ride out a host that
     * is merely busy, and paid at most once per detach thanks to the latch.
     */
    kEsp32_ConsoleTxSpinLimit   = 500000,
};

/*___________________
  static variables */

static const LTCoreBSP_LTCoreCallbacks *    s_pCoreCallbacks = NULL;

static const u32 s_rxInterruptMask = 1u << ESP32_REG_SHIFT(USB_SERIAL_JTAG_INT, OUT_RECV_PKT);

/*
 * Whether anything is collecting what we write.
 *
 * This starts optimistic, because output produced before a host has had time to
 * open the port is the output most worth keeping - it sits in the endpoint and
 * is delivered the moment somebody attaches.  The first write that fills the
 * endpoint and then waits out kEsp32_ConsoleTxSpinLimit for nothing clears the
 * latch, and from then on writes that find the endpoint full give up after a
 * single register read rather than stalling the caller.  Any write that does get
 * a byte in sets it again, so attaching a host mid-run brings the console back.
 */
static bool s_bHostDraining = true;

/*
 * Whether LT is ready to be handed received characters.
 *
 * Set as the last act of Esp32_ConsoleInitialize(), at the same moment the
 * interrupt is armed, because that is the point from which the ISR may deliver -
 * so it is also the earliest the output path may.  Before it, received bytes are
 * still drained, just discarded: they were typed at the ROM or the bootloader,
 * and draining them is about not wedging the host rather than about input.
 */
static bool s_bDeliverRx = false;

/*_______________________________________
  discard whatever the host has sent us */

/*
 * Called where Esp32_ConsoleISR() cannot run, to keep a host that writes to us
 * from hanging on an endpoint nobody is emptying.  Two such windows exist:
 *
 *  - Before Esp32_ConsoleInitialize().  Esp32_ConsolePutChars() is documented as
 *    usable this early and the bring-up path uses it, so the first output an
 *    operator sees can precede the interrupt by a long way.  Worse, the FIFO may
 *    already be occupied on entry: the ROM and the second-stage bootloader print
 *    over this same device, an attached terminal answers, and with the
 *    RTC_CNTL_USB_CONF reset hold in place that state now survives a reset into
 *    the application.
 *
 *  - At the end of initialization, because clearing the stale interrupt status
 *    there would otherwise strand any packet already waiting: the ISR only drains
 *    when it sees OUT_RECV_PKT, and nothing sets that bit again for bytes that
 *    have already arrived.  That is a permanent jam from the first boot onwards,
 *    not a window.
 *
 * Discarding is right in both places rather than merely convenient.  Anything in
 * the FIFO at that point was typed at the ROM or the bootloader, before this
 * console - and before the shell behind it - existed to receive it.  It also
 * keeps this off ProcessISRConsoleInputChars(), which the ISR is the only thing
 * that should be calling.
 *
 * The status bit is cleared *before* the FIFO is emptied, not after.  A packet
 * that lands mid-drain then either gets read here or leaves OUT_RECV_PKT set for
 * the ISR to pick up; the other order can clear a live notification with bytes
 * still unread, which is the jam this function exists to prevent.
 */
static void LT_ISR_SAFE
Esp32_ConsoleDrainRx(void) {
    ESP32_REG(USB_SERIAL_JTAG_INT_CLR) = s_rxInterruptMask;
    while (ESP32_REG(USB_SERIAL_JTAG_EP1_CONF) &
           ESP32_REG_MASK(USB_SERIAL_JTAG_EP1_CONF, OUT_DATA_AVAIL)) {
        (void)ESP32_REG(USB_SERIAL_JTAG_EP1);
    }
}

/*_________________________________________________
  drain the endpoint and hand what was in it to LT */

/*
 * The receive path proper, factored out of the ISR because the ISR must not be
 * the only thing that can run it.
 *
 * An interrupt that never arrives is not a hypothetical here: it is a whole
 * class of bring-up fault - an unrouted multiplexer, a masked line, a peripheral
 * that never asserts - and the previous arrangement turned every member of that
 * class into the worst failure the console has, because arming the interrupt
 * also switched off the polled drain that had been covering for it.  The first
 * keystroke then had nothing to empty the endpoint, and an unemptied endpoint
 * does not merely lose input: it NAKs the host forever and takes the *output*
 * with it, so the one tool that could diagnose the fault is destroyed by it.
 *
 * Calling this from the output path as well costs one register read per write
 * and removes that failure mode outright.  Input still works, at the granularity
 * of whatever the board prints, however badly the interrupt is wired.
 *
 * Runs with interrupts disabled, which is the context ProcessISRConsoleInputChars()
 * is written for and also what makes the ISR and the output path safe against each
 * other - neither can interleave with the other's drain and steal half a packet.
 * The one re-entry that survives that is the callback itself logging something,
 * which comes back round through Esp32_ConsolePutChars(); s_bPumping turns the
 * inner call into a no-op and the outer loop picks up anything it left.
 */
static void LT_ISR_SAFE
Esp32_ConsolePumpRx(void) {
    static char s_rxBuff[kEsp32_ConsolePacketSize];
    static bool s_bPumping = false;

    u32 nMask = Esp32DisableInterrupts();

    if (!s_bPumping) {
        u32 nCount = 0, bSent = 0;
        s_bPumping = true;

        ESP32_REG(USB_SERIAL_JTAG_INT_CLR) = s_rxInterruptMask;
        while (1) {
            while ((nCount < kEsp32_ConsolePacketSize) &&
                   (ESP32_REG(USB_SERIAL_JTAG_EP1_CONF) &
                    ESP32_REG_MASK(USB_SERIAL_JTAG_EP1_CONF, OUT_DATA_AVAIL))) {
                s_rxBuff[nCount++] = (char)(ESP32_REG(USB_SERIAL_JTAG_EP1) & 0xFF);
            }
                 if (nCount) { s_pCoreCallbacks->ProcessISRConsoleInputChars(s_rxBuff, nCount); bSent = 1; }
            else if (bSent) { s_pCoreCallbacks->ProcessISRConsoleInputChars(NULL, 0);           bSent = 0; }
            else break;
            nCount = 0;
        }

        s_bPumping = false;
    }

    Esp32EnableInterrupts(nMask);
}

/*______________________________
  unbuffered console putchars */
/*
 * Undeliverable output is dropped, and the loss that hurts most is the boot log
 * after a reboot: the reset takes the USB device with it, the host needs seconds
 * to re-enumerate and re-open the port, and the board has said everything it is
 * going to say within milliseconds of coming back.
 *
 * Holding that output in a 4KB buffer and replaying it to a host that turns up
 * later was built, driven off IN_EMPTY - "the host collected a packet" - and
 * tested on hardware, and the log still did not arrive.  It was reverted for the
 * static RAM.  Do not rebuild it without a theory that explains that result; the
 * mechanism, the reasoning and what was ruled out are in ESP32S3_Port_Notes.md.
 * Capturing a boot log reliably wants UART0 on the debug pads instead - a console
 * on a separate USB device that does not reset when the esp32s3 does.
 */
void LT_ISR_SAFE Esp32_ConsolePutChars(const char * pChars, u32 nChars) {

    /*
     * Piggyback the receive path on output, always, not just before the ISR is
     * live - see Esp32_ConsolePumpRx() for why this is not merely belt and braces.
     * It costs one register read when the FIFO is empty.  Before LT has given us
     * anywhere to deliver to there is nothing to do but discard.
     */
    if (s_bDeliverRx) Esp32_ConsolePumpRx();
    else              Esp32_ConsoleDrainRx();

    while (nChars) {
        u32 nWritten = 0;

        /* fill the packet, or as much of it as is still free */
        while (nChars && (ESP32_REG(USB_SERIAL_JTAG_EP1_CONF) &
                          ESP32_REG_MASK(USB_SERIAL_JTAG_EP1_CONF, IN_DATA_FREE))) {
            ESP32_REG(USB_SERIAL_JTAG_EP1) = (u8)*pChars++;
            nChars--;
            nWritten++;
        }

        if (nWritten) {
            /* offer what we wrote to the host - a no-op if the packet just filled */
            ESP32_REG(USB_SERIAL_JTAG_EP1_CONF) = ESP32_REG_MASK(USB_SERIAL_JTAG_EP1_CONF, WR_DONE);
            s_bHostDraining = true;
        }

        if (nChars) {
            /* more to say than fits, so wait for the packet we just handed over */
            u32 nSpins = s_bHostDraining ? kEsp32_ConsoleTxSpinLimit : 1;
            while (nSpins && !(ESP32_REG(USB_SERIAL_JTAG_EP1_CONF) &
                               ESP32_REG_MASK(USB_SERIAL_JTAG_EP1_CONF, IN_DATA_FREE))) {
                nSpins--;
            }
            if (!nSpins) {
                s_bHostDraining = false;
                return;                     /* nobody is listening - drop the rest */
            }
        }
    }
}

/* ____________________________________________________________________________
   USB serial/JTAG interrupt service routine - the console's receive path */
/*
 * Note what this does before it does anything else, and unconditionally.
 *
 * CPU interrupt 13 is XTHAL_INTTYPE_EXTERN_LEVEL, so the core's INTERRUPT bit is
 * not a latch this routine may choose to leave alone - it is a live wire that
 * follows INT_ST for as long as the peripheral holds it up.  _LTKDispatcher()
 * re-reads INTERRUPT after every handler it calls and keeps going while anything
 * is set, so a handler that returns without deasserting its source does not
 * simply get called again: it is called forever, and the board dies by watchdog
 * with no output and no clue as to why.  Returning early on an unrecognised
 * status - which is what the guard here used to do - is exactly that bug.
 */
static void LT_ISR_SAFE
Esp32_ConsoleISR(void) {
    ESP32_REG(USB_SERIAL_JTAG_INT_CLR) = ESP32_REG(USB_SERIAL_JTAG_INT_ST);
    Esp32_ConsolePumpRx();
}

/*_________________________
  console initialization */
void Esp32_ConsoleInitialize(const LTCoreBSP_LTCoreCallbacks * pCallbacks) {

    /* hook up the char receive interrupt after setting s_pCoreCallbacks! */
    s_pCoreCallbacks = pCallbacks;

    /*
     * Keep the bus clock on now that the application owns the SYSTEM registers.
     *
     * Deliberately nothing else: the ROM has already enabled the PHY pads and
     * been enumerated, and both pulsing the module reset and clearing
     * CONF0.PAD_ENABLE would drop the device off the bus, taking the host's open
     * port - and the boot log the user is watching - with it.
     */
    Esp32_ClockEnableModuleClock(kEsp32_ClockModule_USB_DEVICE);

    ESP32_REG(USB_SERIAL_JTAG_INT_ENA) = 0;              /* disable all USB serial/JTAG interrupts */
    ESP32_REG(USB_SERIAL_JTAG_INT_CLR) = LT_U32_MAX;

    /*
     * Clearing the status above just discarded the notification for anything the
     * host sent while the bootloader was talking, so empty the FIFO to match.
     * Left alone those bytes would sit there unreadable and unread - no interrupt
     * is raised for them again - NAKing the host forever.
     *
     * Safe against the ISR either way round: the interrupt is masked here, and a
     * packet arriving between this call and the unmask below leaves its sticky
     * raw bit set, so INT_ST reads it the moment the mask lifts.
     */
    Esp32_ConsoleDrainRx();

    Esp32MapExternalToCPUIrq(kEsp32_CPU0, kEsp32_ExternalIrq_USBSerialJTAG, kEsp32_IrqNumber_USBSerialJTAG);
    pCallbacks->SetInterruptVector(kEsp32_IrqNumber_USBSerialJTAG, Esp32_ConsoleISR, kEsp32_IrqPriority_USBSerialJTAG);
    ESP32_REG(USB_SERIAL_JTAG_INT_ENA) = s_rxInterruptMask;  /* enable receive interrupt */

    /* both the ISR and the output path may deliver to LT from here */
    s_bDeliverRx = true;
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Jul-26   claudius    created
 *  03-Aug-26   claudius    drain the OUT endpoint where the ISR cannot reach it
 *  03-Aug-26   claudius    never let the ISR be the only drain; always deassert
 *                          the level-triggered line
 *  10-Aug-26   claudius    hold undeliverable output and replay it to a host
 *                          that turns up later, so a reboot keeps its boot log
 *  10-Aug-26   claudius    UART0 is reachable on the debug pads after all
 *  11-Aug-26   claudius    drain the replay from IN_EMPTY; checking for a host
 *                          on the output path never runs on an idle board
 *  11-Aug-26   claudius    reverted both of the above: the boot log still does
 *                          not arrive on hardware, and 4KB of static RAM is too
 *                          much to pay for a mechanism that does not work.  Back
 *                          to dropping what the endpoint will not take.  See the
 *                          note above Esp32_ConsolePutChars() before trying it
 *                          again, and ESP32S3_Port_Notes.md 10.
 */
