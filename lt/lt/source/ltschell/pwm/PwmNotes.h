/*******************************************************************************
 * lt/source/ltshell/pwm/PwmNotes.h
 *
 * Simple Piano notes/frequencies, and various defined songs
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_SOURCE_LTSHELL_PWM_PWMNOTES_H
#define ROKU_LT_SOURCE_LTSHELL_PWM_PWMNOTES_H

typedef u8 NoteOctave;

typedef struct {
    unsigned char   sustain;      // Add extra sustain or not
    unsigned short  note;         // NoteOctave
    unsigned short  duration;     // Duration factor
} __attribute__((packed)) MusicElement;

typedef struct {
    char const *title;
    MusicElement const *songBase;
    unsigned int elementCount;
    short tempoMultiplier;
    short sustainDuration;
    short postNoteSilence;
} SongElement;

MusicElement const *GetNextNote(MusicElement const *song, int count, int index);
unsigned int GetTotalSongs(void);
int GetNoteFrequency(NoteOctave note, char const **name);
SongElement const *GetSong(unsigned int index);

#endif /* ROKU_LT_SOURCE_LTSHELL_PWM_PWMNOTES_H */
