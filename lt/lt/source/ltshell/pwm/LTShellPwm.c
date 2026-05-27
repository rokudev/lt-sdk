/*******************************************************************************
 *
 * LTShellPwm - Shell commands for exercising PWM output
 * -----------------------------------------------------------------------------
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/gpio/LTDeviceGpio.h>
#include <lt/device/pwm/LTDevicePwm.h>
#include <lt/system/schell/LTSystemSchell.h>

static struct Statics {
    LTDevicePwm             *pPwm;
    LTDeviceUnit             hPwm;
    ILTDriverPwmDeviceUnit  *iPwm;
    LTDeviceGpio            *pGpio;
} S;

#include "PwmNotes.h"

static void PwmHelp(LTSystemSchell *shell, int argc, const char *argv[]) {
    LT_UNUSED(argc), LT_UNUSED(argv);
    enum { kSongListSize = 64 };
    char *songList = lt_malloc(kSongListSize);
    if (songList) {
        int totalSongs = GetTotalSongs();
        char *nextSong = songList;
        s32 songListCharsRemaining = kSongListSize;
        for (int i = 0; i < totalSongs; ++i) {
            SongElement const *songElem = GetSong(i);
            if (lt_snprintf(NULL, 0, ",%d=%s", i + 4, songElem->title) >= songListCharsRemaining) {
                /* out of room */
                if (songListCharsRemaining >= 4) lt_strncpyTerm(nextSong, "...", songListCharsRemaining);
                break;
            }
            u32 charsThisSong = lt_snprintf(nextSong, songListCharsRemaining, ",%d=%s", i + 4, songElem->title);
            songListCharsRemaining -= charsThisSong;
            nextSong += charsThisSong;
        }
    }
#ifdef __cplusplus
    auto p = shell->API->Print;
#else
    typedef void (*p_type)(LTSystemSchell *shell, const char *format, ...);
    p_type p = shell->API->Print;
#endif
    p(shell, "usage: pwm [pin# | @name], freq, duty%%(or permil*) - Set freq=0 to stop, or pin=-pin (or -@name) for active low\n");
    p(shell, "       pwm alt [pin# | @name] [0|1|2|3]            - Set alt clock output (0=stop,1=pri,2=sec,3=tert)\n");
    p(shell, "       pwm pat [pin# | @name] test# [freq]         - Play test pattern on pin (1=Whistle,2=Breath(freq),3=Sonar(freq)%s)\n", songList ? songList : "");
    p(shell, "\n");
    p(shell, "Note: list of values usable in @name, can be listed with 'gpio names' command");
    p(shell, "\n");
    lt_free(songList);
}

void FreqSustain(ILTThread *iThread, int pin, int freq, int duration_ms, int fadeFactor) {
    int delta = 10;
    int loop_duration_us = fadeFactor * 1000 / 500;
    int loop_accum = 0;
    int duration_us = duration_ms * 1000;

    // Start the duty cycle at 50%
    int p = 500;

    // Walk the duty cycle down to 0 at the fadeFactor rate, exit when it hits
    // zero, or the requested duration has been met
    while (p >= 0 && loop_accum < duration_us) {
        p -= delta;

        // Change duty cycle
        S.iPwm->InitPwmPin(pin, true, freq, p, true);

        // Delay at this duty cycle for a while
        iThread->Sleep(LTTime_Microseconds(loop_duration_us));

        // Accumulate the small durations
        loop_accum += loop_duration_us;
    }

    if (loop_accum < duration_us) {
        // Ensure that the sound is off
        S.iPwm->Stop(pin);

        // Perform the remaining duration here
        iThread->Sleep(LTTime_Microseconds(duration_us - loop_accum));
    }
}

static void TestPwmPattern(LTSystemSchell *shell, u8 pin, int which, int xtra) {
    ILTThread *iThread = lt_getlibraryinterface(ILTThread, LT_GetCore());

    int totalSongs = GetTotalSongs();

    if (which == 1) {
        static const int delay_us = 3000;
        // Play "come here" whistle for FMR
        shell->API->Print(shell, "pat.whistle.begin: Testing pattern: Whistle");

        // Initialize this PWM block and pin
        S.iPwm->Stop(pin);
        S.iPwm->InitPwmPin(pin, true, 1000, 500, true);

        int delta = 150;

        for (int f = 1000; f < 6000; f += delta) {
            // Change Frequency
            S.iPwm->InitPwmPin(pin, true, f, 500, true);

            iThread->Sleep(LTTime_Microseconds(delay_us));
        }

        delta = 100;

        for (int f = 6000; f >= 1000 ; f -= delta) {
            // Change Frequency
            S.iPwm->InitPwmPin(pin, true, f, 500, true);

            iThread->Sleep(LTTime_Microseconds(delay_us));
        }

        delta = 200;

        for (int f = 1000; f < 9000; f += delta) {
            // Change Frequency
            S.iPwm->InitPwmPin(pin, true, f, 500, true);

            iThread->Sleep(LTTime_Microseconds(delay_us));
        }

        S.iPwm->Stop(pin);

        shell->API->Print(shell, "pat.whistle.done: Pattern Whistle Done");
    }
    else if (which == 2) {
        static const int delay_us = 500;
        int freq = (xtra ? xtra : 4000);
        // Play "Breath" pattern for LEDs
        shell->API->Print(shell, "pat.breath.begin: Testing pattern: Breath @%d Hz", freq);

        S.iPwm->Stop(pin);
        S.iPwm->InitPwmPin(pin, true, freq, 0, true);

        int delta = 1;

        for (int count = 0; count < 2; ++count) {
            for (int p = 0; p <= 1000; p += delta) {
                // Change duty cycle
                S.iPwm->InitPwmPin(pin, true, freq, p, true);

                iThread->Sleep(LTTime_Microseconds(delay_us * 2));
            }

            // Delay at full on for 500ms
            iThread->Sleep(LTTime_Milliseconds(500));

            for (int p = 1000; p >= 0; p -= delta) {
                // Change duty cycle
                S.iPwm->InitPwmPin(pin, true, freq, p, true);

                iThread->Sleep(LTTime_Microseconds(delay_us));
            }

            // Delay at full off for 500ms
            iThread->Sleep(LTTime_Milliseconds(500));
        }

        S.iPwm->Stop(pin);

        shell->API->Print(shell, "pat.breath.done: Pattern Breath Done");
    }
    else if (which == 3) {
        static const int delay_us = 450;
        int freq = xtra ? xtra : 3800;

        static const int cycles = 2;
        // Play "Sonar" pattern for FMR
        shell->API->Print(shell, "pat.sonar.begin: Testing pattern: Sonar @%d Hz", freq);

        S.iPwm->Stop(pin);
        S.iPwm->InitPwmPin(pin, true, freq, 0, true);

        static const int ramp_delta = 20;
        static const int warble_depth = 50;
        static const int warble_step = 1;
        static const int warble_delay = delay_us * 5 / 2;

        // Repeat the whole pattern for this many cycles
        for (int count = 0; count < cycles; ++count) {
            // Each pattern starts at full loudness (50% duty cycle)
            // and iteratively ramps down to 0% (to attenuate the output). Note:
            // This ramp is linear for the ease of coding, but probably should
            // have a logarithmic "audio" taper instead.
            for (int p = 500; p >= warble_depth; p -= ramp_delta) {
                // But to make the sonar-ish sound, add a slight warble of
                // amplitude around the current loudness Note: this is
                // implemented as a triangle wave on top of the current signal's
                // amplitude.  It really should be a sine wave, but triangle was
                // easier to code up for now.
                for (int w = p; w >= (p - warble_depth); w -= warble_step) {
                    // Half the Warble is down
                    S.iPwm->InitPwmPin(pin, true, freq, w, true);
                    iThread->Sleep(LTTime_Microseconds(warble_delay));
                }
                for (int w = p - warble_depth; w >= p; w += warble_step) {
                    // Half the Warble ie back up
                    S.iPwm->InitPwmPin(pin, true, freq, w, true);
                    iThread->Sleep(LTTime_Microseconds(warble_delay));
                }
            }

            // At the end of the ramp, stop the warble and just ramp down by 1
            for (int p = warble_depth; p >= 0; p -= 1) {
                S.iPwm->InitPwmPin(pin, true, freq, p, true);
                iThread->Sleep(LTTime_Microseconds(delay_us / ramp_delta ));
            }

            // Delay at full off for a short while
            iThread->Sleep(LTTime_Milliseconds(600));
        }

        S.iPwm->Stop(pin);

        shell->API->Print(shell, "pat.sonar.done: Pattern Sonar Done");
    }
    else if ((which >= 4) && (which <= (totalSongs + 3))) {
        // Play "Song" pattern for FMR
        unsigned int songIndex = which - 4;

        if (songIndex >= GetTotalSongs()) {
            shell->API->Print(shell, "Error: Unknown song #: %d\n", which);
            return;
        };

        SongElement const *songElem = GetSong(songIndex);
        unsigned int songCount = songElem->elementCount;
        MusicElement const *songBase = songElem->songBase;
        char const *songTitle = songElem->title;
        int postNoteSilence = songElem->postNoteSilence;
        int tempoMultiplier = songElem->tempoMultiplier;

        shell->API->Print(shell, "pat.song.begin: Testing pattern: Song '%s'", songTitle);

        S.iPwm->Stop(pin);

        for (unsigned int idx = 0; idx < songCount; ++idx) {
            MusicElement const *elem = GetNextNote(songBase, songCount, idx);
            if (elem) {
                char const *name = "";

                // Since the Piezo does better at higher frequencies, double
                // the frequency to shift everything up an octave
                int freq = GetNoteFrequency(elem->note, &name) * 2;

                int dur = elem->duration * tempoMultiplier;
                int sustain = elem->sustain ? (elem->sustain * songElem->sustainDuration) : songElem->sustainDuration / 2;

#if 0 // Don't show the note and timing info unless we're debugging
                shell->API->Print(shell, " %-3s (%4d) {%4d} <%4d>\n", name, freq, dur, sustain);
#endif

                if (freq) {
                    FreqSustain(iThread, pin, freq, dur, sustain);
                }
                else {
                    S.iPwm->Stop(pin);
                    iThread->Sleep(LTTime_Milliseconds(dur));
                }
            }
            else {
                shell->API->Print(shell, "\n{end}\n");
                // Oops hit the end, so stop
                break;
            }

            S.iPwm->Stop(pin);

            // Delay at end of each note
            iThread->Sleep(LTTime_Milliseconds(postNoteSilence));
        }

        S.iPwm->Stop(pin);

        shell->API->Print(shell, "\npat.song.done: Pattern Song '%s' Done", songTitle);
    }
    else {
        shell->API->Print(shell, "pat.unk: Unknown pattern number: %d", which);
    }
}

static int GetPinValue(char const *arg) {
    int ret = -1;
    bool invert = false;

    if (arg && arg[0]) {
        // First see if this is a named or numeric pin
        if (arg[0] == '-' && arg[1] == '@') {
            invert = true;
            ++arg;
        }

        if (arg[0] == '@') {
            // It's a named one, so translate that to its value
            /* Temporary: only support named pins on platforms that include LTDeviceGpio */
            ret = S.pGpio ? S.pGpio->API->GetNamedPinValueFromName(S.pGpio, &arg[1]) : -1;
        }
        else {
            // It's a number, so convert it
            ret = lt_strtou32(arg, NULL, 0);
        }
    }

    if (invert)
        ret = -ret;

    return ret;
}

static int PwmCommand(LTSystemSchell *shell, int argc, const char *argv[]) {
    if (argc <= 1) {
        PwmHelp(shell, 0, NULL);
        return -1;
    }

    S.hPwm = S.pPwm->CreateDeviceUnitHandle(0);
    S.iPwm = lt_gethandleinterface(ILTDriverPwmDeviceUnit, S.pPwm->CreateDeviceUnitHandle(0));

    int pin = 0;

    if ((argc >= 3) && !lt_strcmp(argv[1], "pat")) {
        if (((pin = GetPinValue(argv[2]))) == -1) {
            shell->API->Print(shell, "unknown pin \"%s\"\n", argv[2]);
            return 1;
        }
        int which = lt_strtou32(argv[3], NULL, 0);
        int xtra = (argc >= 4) ? lt_strtou32(argv[4], NULL, 0) : 0;
        TestPwmPattern(shell, pin, which, xtra);
        return 0;
    }
    else if ((argc >= 3) && !lt_strcmp(argv[1], "alt")) {
        if (((pin = GetPinValue(argv[2])) == -1)) {
            shell->API->Print(shell, "unknown pin \"%s\"\n", argv[2]);
            return 1;
        }
        u32 value = lt_strtou32(argv[3], NULL, 0);
        bool enable = (value != 0);
        if (value < kLTDevicePwm_ClockType_TOTAL) {
            // Alternate set of clk out
            S.iPwm->SetClockOutputPin(pin, enable, (LTDevicePwm_ClockType)value);
            return 0;
        }
        else {
            shell->API->Print(shell, "Error, unknown clock type: %lu\n\n", LT_Pu32(value));
        }
    }
    else if (argc >= 4) {
        int p = GetPinValue(argv[1]);
        if (p == -1) {
            shell->API->Print(shell, "unknown pin \"%s\"\n", argv[1]);
            return 1;
        }
        bool activeHigh = p > 0;
        pin = activeHigh ? p : -p;

        char *endptr = NULL;

        u32 freq = lt_strtou32(argv[2], NULL, 0);
        u16 duty = lt_strtou32(argv[3], &endptr, 0);
        if (endptr && *endptr) {
            // Check for duty cycle unit suffix: none = %, % = 0-100, * = 0 - 1000 (permil)
            if (endptr[0] == '%' || *endptr == ' ') {
                duty *= 10;
            }
            else {
                // Assume full permil range for other chars (including *)
            }
        }
        else {
            duty *= 10;
        }

        if (freq == 0) {
            S.iPwm->Stop(pin);
            shell->API->Print(shell, "pwm stop: pin=%d\n", pin);
        }
        else {
            S.iPwm->InitPwmPin(pin, activeHigh, freq, duty, true);

            shell->API->Print(shell, "pwm start: pin=%d, freq=%lu Hz, duty=%u permil, active=%s\n",
                                    pin, LT_Pu32(freq), duty, activeHigh ? "high" : "low");
        }

        return 0;
    }

    PwmHelp(shell, 0, NULL);
    return -1;
}

static const LTSystemShell_CommandDesc s_PWMCommands[] = {
    { "pwm", PwmCommand, "Drive a PWM channel", PwmHelp }
};

static bool ShellInit(void) {
    LTSystemSchell *shell = LTSystemSchellConsole_GetConsoleShell();
    if (!shell) return false;
    LTSystemShell_CommandTable table = {
        .commands    = s_PWMCommands,
        .numCommands = sizeof(s_PWMCommands) / sizeof(s_PWMCommands[0])
    };
    shell->API->RegisterCommands(shell, &table);
    return true;
}

static void LTShellPwmImpl_LibFini(void) {
    LTSystemSchell *shell = LTSystemSchellConsole_GetConsoleShell();
    LTSystemShell_CommandTable table = {
        .commands    = s_PWMCommands,
        .numCommands = sizeof(s_PWMCommands) / sizeof(s_PWMCommands[0])
    };
    shell->API->UnregisterCommands(shell, &table);
    lt_destroyobject(S.pGpio);
    lt_closelibrary(S.pPwm);
    S = (struct Statics){};
}

static bool LTShellPwmImpl_LibInit(void) {
    S.pGpio = lt_createdeviceobject(LTDeviceGpio);
    if (   !(S.pPwm  = lt_openlibrary(LTDevicePwm))
        || !ShellInit()) { LTShellPwmImpl_LibFini(); return false; }
    return true;
}

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellPwm, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellPwm) LTLIBRARY_DEFINITION;
