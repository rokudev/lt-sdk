/*******************************************************************************
 * lt/source/ltshell/gpio/LTShellGpio.c
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 *
 *******************************************************************************
 * LT Library registering a "gpio" shell command that exercises LTDeviceGpio.
 *
 * Every subcommand is a thin call into the device api, so what this reports is
 * what the driver and the BSP beneath it actually did to the hardware.  "gpio
 * selftest" is the useful one: it walks a pad through the api and checks the
 * readbacks, including driving the pad against its own input receiver and
 * against each internal pull, so it needs nothing wired to the board.
 *******************************************************************************/

#include <lt/LT.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTStdlib.h>
#include <lt/device/gpio/LTDeviceGpio.h>
#include <lt/system/shell/LTSystemShell.h>

DEFINE_LTLOG_SECTION("ltshell.gpio");

/*_________________________
_/ #forward declarations */
static int  ShellCommandGpio(LTShell hShell, int argc, const char ** argv);
static void ShellHelpGpio(LTShell hShell, int argc, const char ** argv);

/*____________________
_/ static constants */
static const LTSystemShell_CommandDesc s_gpioShellCommands[] = {
    { "gpio", ShellCommandGpio, "exercise the LTDeviceGpio driver", ShellHelpGpio },
};

/*____________________
_/ static variables */
static LTSystemShell    * s_pLTSystemShell = NULL;
static LTDeviceGpio     * s_pGpio          = NULL;

/* Set by the selftest's interrupt handler.  Volatile because the ISR and the
   shell thread are the two ends of this. */
static volatile int       s_nIsrCount      = 0;

/*____________________
_/ helpers */
static const char * ModeName(LTDeviceGpio_ModeType mode) {
    switch (mode) {
        case kLTDeviceGpio_ModeType_Input:             return "input";
        case kLTDeviceGpio_ModeType_Output:            return "output";
        case kLTDeviceGpio_ModeType_Both:              return "both";
        case kLTDeviceGpio_ModeType_HighZ:             return "highz";
        case kLTDeviceGpio_ModeType_AlternateFunction: return "altfunc";
        default:                                       return "error";
    }
}

static const char * PullName(LTDeviceGpio_PullType pull) {
    switch (pull) {
        case kLTDeviceGpio_PullType_NoPull:   return "none";
        case kLTDeviceGpio_PullType_PullUp:   return "up";
        case kLTDeviceGpio_PullType_PullDown: return "down";
        default:                              return "error";
    }
}

/* Accepts a pad number or a name from the config's pins array, so the commands
   below take "gpio read 2" and "gpio read d0" alike.  Returns -1 if neither. */
static int ResolvePin(const char * pArg) {
    if (pArg == NULL) return -1;

    if (pArg[0] >= '0' && pArg[0] <= '9') {
        char * pEnd = NULL;
        s32 n = lt_strtos32(pArg, &pEnd, 0);
        if (pEnd && *pEnd == '\0' && n >= 0 && n < (s32)s_pGpio->API->GetNumberOfGpios(s_pGpio)) return (int)n;
        return -1;
    }

    return s_pGpio->API->GetNamedPinValueFromName(s_pGpio, pArg);
}

static LTDeviceGpio_PullType ParsePull(const char * pArg) {
    if (pArg == NULL)                       return kLTDeviceGpio_PullType_Error;
    if (0 == lt_strcmp(pArg, "up"))         return kLTDeviceGpio_PullType_PullUp;
    if (0 == lt_strcmp(pArg, "down"))       return kLTDeviceGpio_PullType_PullDown;
    if (0 == lt_strcmp(pArg, "none"))       return kLTDeviceGpio_PullType_NoPull;
    return kLTDeviceGpio_PullType_Error;
}

static LTDeviceGpio_ModeType ParseMode(const char * pArg) {
    if (pArg == NULL)                       return kLTDeviceGpio_ModeType_Error;
    if (0 == lt_strcmp(pArg, "input"))      return kLTDeviceGpio_ModeType_Input;
    if (0 == lt_strcmp(pArg, "output"))     return kLTDeviceGpio_ModeType_Output;
    if (0 == lt_strcmp(pArg, "both"))       return kLTDeviceGpio_ModeType_Both;
    if (0 == lt_strcmp(pArg, "highz"))      return kLTDeviceGpio_ModeType_HighZ;
    return kLTDeviceGpio_ModeType_Error;
}

/*____________________
_/ subcommands */
static void GpioList(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    u16 nGpios = s_pGpio->API->GetNumberOfGpios(s_pGpio);

    iShell->Print(hShell, "%u pads\n", (unsigned)nGpios);
    iShell->Print(hShell, "%-4s %-9s %-8s %-6s %-5s %s\n", "idx", "name", "mode", "pull", "value", "func");
    for (u16 i = 0; i < nGpios; ++i) {
        int nFunc = s_pGpio->API->GetGpioAlternateFunctionFromIndex(s_pGpio, i);
        iShell->Print(hShell, "%-4u %-9s %-8s %-6s %-5d %s\n",
                      (unsigned)i,
                      s_pGpio->API->GetGpioNameFromIndex(s_pGpio, i),
                      ModeName(s_pGpio->API->GetGpioModeFromIndex(s_pGpio, i)),
                      PullName(s_pGpio->API->GetGpioPullFromIndex(s_pGpio, i)),
                      (int)s_pGpio->API->GetInputValue(s_pGpio, i),
                      (nFunc < 0) ? "?" : s_pGpio->API->GetAlternateFunctionNameFromIndex(s_pGpio, (u16)nFunc));
    }
}

static void GpioNames(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    u16 nNamed = s_pGpio->API->GetNumberOfNamedPins(s_pGpio);

    iShell->Print(hShell, "%u named pins\n", (unsigned)nNamed);
    for (u16 i = 0; i < nNamed; ++i) {
        const char * pName = s_pGpio->API->GetNamedPinFromIndex(s_pGpio, i);
        int nByIndex = s_pGpio->API->GetNamedPinValueFromIndex(s_pGpio, i);
        int nByName  = pName ? s_pGpio->API->GetNamedPinValueFromName(s_pGpio, pName) : -1;
        /* the two lookups have to agree, or the name to pad mapping is broken */
        iShell->Print(hShell, "  %-12s pad %-3d %s\n", pName ? pName : "(null)", nByIndex,
                      (nByIndex == nByName) ? "" : "MISMATCH by name!");
    }
}

static void GpioFuncs(LTShell hShell) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    u16 nFuncs = s_pGpio->API->GetNumberOfAlternateFunctions(s_pGpio);

    iShell->Print(hShell, "%u alternate functions\n", (unsigned)nFuncs);
    for (u16 i = 0; i < nFuncs; ++i) {
        iShell->Print(hShell, "  %u %s\n", (unsigned)i,
                      s_pGpio->API->GetAlternateFunctionNameFromIndex(s_pGpio, i));
    }
}

static void GpioSelftestIsr(void * pClientData) LT_ISR_SAFE {
    LT_UNUSED(pClientData);
    ++s_nIsrCount;
}

/*
 * Drives one pad through the whole api and checks every readback.  The pad is
 * put in "both" mode - driven and received at once - so the input register sees
 * what the output driver is doing and no external wiring is needed.  The pulls
 * are then checked with the driver off, where the only thing holding the pad is
 * the internal resistor.
 */
static int GpioSelftest(LTShell hShell, int nPin) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    LTOThread * pThread = LT_GetCore()->GetCurrentThreadObject();
    int nFailures = 0;

    #define CHECK(cond, ...) do {                                    \
            bool bOk = (cond);                                       \
            if (!bOk) ++nFailures;                                   \
            iShell->Print(hShell, "  [%s] ", bOk ? "pass" : "FAIL"); \
            iShell->Print(hShell, __VA_ARGS__);                      \
            iShell->Print(hShell, "\n");                             \
        } while (0)

    iShell->Print(hShell, "selftest on pad %d (%s)\n", nPin,
                  s_pGpio->API->GetGpioNameFromIndex(s_pGpio, (u16)nPin));

    /* out of range indices must be refused rather than aliased onto a real pad */
    u16 nGpios = s_pGpio->API->GetNumberOfGpios(s_pGpio);
    CHECK(s_pGpio->API->GetGpioNameFromIndex(s_pGpio, nGpios) == NULL, "index %u rejected", (unsigned)nGpios);
    CHECK(s_pGpio->API->GetGpioModeFromIndex(s_pGpio, nGpios) == kLTDeviceGpio_ModeType_Error,
          "mode of index %u is error", (unsigned)nGpios);
    CHECK(!s_pGpio->API->SetOutputValue(s_pGpio, nGpios, true), "write to index %u refused", (unsigned)nGpios);

    /* mode set and readback */
    CHECK(s_pGpio->API->SetGpioModeFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_ModeType_Output), "set output");
    CHECK(s_pGpio->API->GetGpioModeFromIndex(s_pGpio, (u16)nPin) == kLTDeviceGpio_ModeType_Output, "mode reads output");
    CHECK(s_pGpio->API->SetGpioModeFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_ModeType_Input), "set input");
    CHECK(s_pGpio->API->GetGpioModeFromIndex(s_pGpio, (u16)nPin) == kLTDeviceGpio_ModeType_Input, "mode reads input");
    CHECK(s_pGpio->API->SetGpioModeFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_ModeType_HighZ), "set highz");
    CHECK(s_pGpio->API->GetGpioModeFromIndex(s_pGpio, (u16)nPin) == kLTDeviceGpio_ModeType_HighZ, "mode reads highz");

    /* the pad stays on the GPIO function throughout the above */
    CHECK(s_pGpio->API->GetGpioAlternateFunctionFromIndex(s_pGpio, (u16)nPin) == 1, "func is GPIO (1)");

    /* pull set and readback, as an input so the pull is actually connected */
    CHECK(s_pGpio->API->SetGpioModeFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_ModeType_Input), "back to input");
    CHECK(s_pGpio->API->SetGpioPullFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_PullType_PullUp), "set pull up");
    CHECK(s_pGpio->API->GetGpioPullFromIndex(s_pGpio, (u16)nPin) == kLTDeviceGpio_PullType_PullUp, "pull reads up");
    pThread->API->Sleep(LTTime_Milliseconds(5));
    CHECK(s_pGpio->API->GetInputValue(s_pGpio, (u16)nPin), "pad floats high on pull up");

    CHECK(s_pGpio->API->SetGpioPullFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_PullType_PullDown), "set pull down");
    CHECK(s_pGpio->API->GetGpioPullFromIndex(s_pGpio, (u16)nPin) == kLTDeviceGpio_PullType_PullDown, "pull reads down");
    pThread->API->Sleep(LTTime_Milliseconds(5));
    CHECK(!s_pGpio->API->GetInputValue(s_pGpio, (u16)nPin), "pad floats low on pull down");

    CHECK(s_pGpio->API->SetGpioPullFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_PullType_NoPull), "set pull none");
    CHECK(s_pGpio->API->GetGpioPullFromIndex(s_pGpio, (u16)nPin) == kLTDeviceGpio_PullType_NoPull, "pull reads none");

    /* drive the pad and read it back through its own receiver */
    CHECK(s_pGpio->API->SetGpioModeFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_ModeType_Both), "set both");
    CHECK(s_pGpio->API->GetGpioModeFromIndex(s_pGpio, (u16)nPin) == kLTDeviceGpio_ModeType_Both, "mode reads both");
    CHECK(s_pGpio->API->SetOutputValue(s_pGpio, (u16)nPin, true), "drive high");
    pThread->API->Sleep(LTTime_Milliseconds(5));
    CHECK(s_pGpio->API->GetInputValue(s_pGpio, (u16)nPin), "reads back high");
    CHECK(s_pGpio->API->SetOutputValue(s_pGpio, (u16)nPin, false), "drive low");
    pThread->API->Sleep(LTTime_Milliseconds(5));
    CHECK(!s_pGpio->API->GetInputValue(s_pGpio, (u16)nPin), "reads back low");

    /* the interrupt path: the pad is still driven, so toggling it is the edge */
    s_nIsrCount = 0;
    CHECK(s_pGpio->API->SetISR(s_pGpio, (u16)nPin, GpioSelftestIsr,
                               kLTDeviceGPIO_TriggerType_RisingEdge, NULL), "attach rising isr");
    s_pGpio->API->SetOutputValue(s_pGpio, (u16)nPin, true);
    pThread->API->Sleep(LTTime_Milliseconds(20));
    CHECK(s_nIsrCount > 0, "isr fired on rising edge (count %d)", s_nIsrCount);

    int nAtDetach = s_nIsrCount;
    CHECK(s_pGpio->API->SetISR(s_pGpio, (u16)nPin, NULL,
                               kLTDeviceGPIO_TriggerType_RisingEdge, NULL), "detach isr");
    s_pGpio->API->SetOutputValue(s_pGpio, (u16)nPin, false);
    pThread->API->Sleep(LTTime_Milliseconds(5));
    s_pGpio->API->SetOutputValue(s_pGpio, (u16)nPin, true);
    pThread->API->Sleep(LTTime_Milliseconds(20));
    CHECK(s_nIsrCount == nAtDetach, "isr silent after detach (count %d)", s_nIsrCount);

    /* leave the pad as we would rather find it */
    s_pGpio->API->SetOutputValue(s_pGpio, (u16)nPin, false);
    s_pGpio->API->SetGpioModeFromIndex(s_pGpio, (u16)nPin, kLTDeviceGpio_ModeType_HighZ);

    #undef CHECK

    iShell->Print(hShell, "%s\n", nFailures ? "SELFTEST FAILED" : "selftest passed");
    return nFailures;
}

/*____________________
_/ gpio help proc */
static void ShellHelpGpio(LTShell hShell, int argc, const char ** argv) { LT_UNUSED(argc); LT_UNUSED(argv);
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);
    iShell->PutString(hShell, "usage: gpio list                     - every pad with its mode, pull, value and function\n");
    iShell->PutString(hShell, "       gpio names                    - the named pins from LTDeviceConfig.json\n");
    iShell->PutString(hShell, "       gpio funcs                    - the IO_MUX functions this chip has\n");
    iShell->PutString(hShell, "       gpio read <pin>               - read a pad\n");
    iShell->PutString(hShell, "       gpio write <pin> <0|1>        - drive a pad\n");
    iShell->PutString(hShell, "       gpio mode <pin> <input|output|both|highz>\n");
    iShell->PutString(hShell, "       gpio pull <pin> <up|down|none>\n");
    iShell->PutString(hShell, "       gpio selftest <pin>           - exercise the api against one pad\n");
    iShell->PutString(hShell, "\n");
    iShell->PutString(hShell, "<pin> is a pad number or a name from 'gpio names'.\n");
}

/*____________________
_/ gpio command proc */
static int ShellCommandGpio(LTShell hShell, int argc, const char ** argv) {
    ILTShell * iShell = (ILTShell *)LT_GetCore()->GetHandleInterface(hShell);

    if (s_pGpio == NULL) {
        iShell->Print(hShell, "no LTDeviceGpio device on this platform\n");
        return -1;
    }

    if (argc < 2) {
        ShellHelpGpio(hShell, argc, argv);
        return -1;
    }

    if (0 == lt_strcmp(argv[1], "list"))  { GpioList(hShell);  return 0; }
    if (0 == lt_strcmp(argv[1], "names")) { GpioNames(hShell); return 0; }
    if (0 == lt_strcmp(argv[1], "funcs")) { GpioFuncs(hShell); return 0; }

    /* everything below takes a pad */
    if (argc < 3) {
        ShellHelpGpio(hShell, argc, argv);
        return -1;
    }

    int nPin = ResolvePin(argv[2]);
    if (nPin < 0) {
        iShell->Print(hShell, "no such pad or named pin: %s\n", argv[2]);
        return -1;
    }

    if (0 == lt_strcmp(argv[1], "read")) {
        iShell->Print(hShell, "pad %d = %d\n", nPin, (int)s_pGpio->API->GetInputValue(s_pGpio, (u16)nPin));
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "selftest")) {
        return GpioSelftest(hShell, nPin);
    }

    /* and everything below that takes a value too */
    if (argc < 4) {
        ShellHelpGpio(hShell, argc, argv);
        return -1;
    }

    if (0 == lt_strcmp(argv[1], "write")) {
        bool bValue = (argv[3][0] != '0');
        if (!s_pGpio->API->SetOutputValue(s_pGpio, (u16)nPin, bValue)) {
            iShell->Print(hShell, "write to pad %d failed\n", nPin);
            return -1;
        }
        iShell->Print(hShell, "pad %d <- %d\n", nPin, (int)bValue);
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "mode")) {
        LTDeviceGpio_ModeType mode = ParseMode(argv[3]);
        if (mode == kLTDeviceGpio_ModeType_Error || !s_pGpio->API->SetGpioModeFromIndex(s_pGpio, (u16)nPin, mode)) {
            iShell->Print(hShell, "cannot set pad %d to mode %s\n", nPin, argv[3]);
            return -1;
        }
        iShell->Print(hShell, "pad %d mode %s\n", nPin, ModeName(s_pGpio->API->GetGpioModeFromIndex(s_pGpio, (u16)nPin)));
        return 0;
    }

    if (0 == lt_strcmp(argv[1], "pull")) {
        LTDeviceGpio_PullType pull = ParsePull(argv[3]);
        if (pull == kLTDeviceGpio_PullType_Error || !s_pGpio->API->SetGpioPullFromIndex(s_pGpio, (u16)nPin, pull)) {
            iShell->Print(hShell, "cannot set pad %d to pull %s\n", nPin, argv[3]);
            return -1;
        }
        iShell->Print(hShell, "pad %d pull %s\n", nPin, PullName(s_pGpio->API->GetGpioPullFromIndex(s_pGpio, (u16)nPin)));
        return 0;
    }

    ShellHelpGpio(hShell, argc, argv);
    return -1;
}

/*__________________________
_/ library initialization */
static bool LTShellGpioImpl_LibInit(void) {
    s_pLTSystemShell = lt_openlibrary(LTSystemShell);
    if (s_pLTSystemShell == NULL) return false;

    /* the device is optional - the command reports its absence rather than
       keeping the whole library from loading */
    s_pGpio = lt_createdeviceobject(LTDeviceGpio);
    if (s_pGpio == NULL) LTLOG_YELLOWALERT("no.gpio.device", "no LTDeviceGpio in the device config");

    s_pLTSystemShell->RegisterCommands(s_gpioShellCommands, sizeof s_gpioShellCommands / sizeof s_gpioShellCommands[0]);
    return true;
}

static void LTShellGpioImpl_LibFini(void) {
    if (s_pLTSystemShell) {
        s_pLTSystemShell->UnregisterCommands(s_gpioShellCommands);
        lt_closelibrary(s_pLTSystemShell);
        s_pLTSystemShell = NULL;
    }
    if (s_pGpio) {
        lt_destroyobject(s_pGpio);
        s_pGpio = NULL;
    }
}

/*_______________________________
_/ LTShellGpio library binding   */
typedef_LTLIBRARY_ROOT_INTERFACE(LTShellGpio, 1) LTLIBRARY_EMPTY_INTERFACE;
 define_LTLIBRARY_ROOT_INTERFACE(LTShellGpio)    LTLIBRARY_DEFINITION;

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  22-Sep-26   claudius    created
 */
