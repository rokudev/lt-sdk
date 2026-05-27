/*******************************************************************************
 * lt/source/ltshell/pwm/PwmNotes.c
 *
 * Simple Piano notes/frequencies, and various defined songs
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/LTTypes.h>
#include "./PwmNotes.h"

//#define USE_STDLIB_BSEARCH

// When USE_EXTENDED_OCTAVES is not defined we will exclude entries (notes) in the
// notes table, that we're not using at the moment, to shrink its size.
//
//#define USE_EXTENDED_OCTAVES
//

//#define INCLUDE_SONG_MARY
//#define INCLUDE_SONG_FUR_ELISE
//#define INCLUDE_SONG_FUGUE
//#define INCLUDE_SONG_MINUET
//#define INCLUDE_SONG_SPENCER

#if defined(INCLUDE_SONG_MARY)      || \
    defined(INCLUDE_SONG_FUR_ELISE) || \
    defined(INCLUDE_SONG_FUGUE)     || \
    defined(INCLUDE_SONG_MINUET)    || \
    defined(INCLUDE_SONG_MINUET)
#define USE_NOTE_DATA
#endif

// Enumeration of all possible musical notes that can be played on this system.
// The range of octaves is arbitrary, but octaves 3 through 7 seem reasonable.
//
enum NoteOctave {
    S,    // Silence

    C3,
    C3s,
    D3,
    D3s,
    E3,
    F3,
    F3s,
    G3,
    G3s,
    A3,
    A3s,
    B3,

    C4,
    C4s,
    D4,
    D4s,
    E4,
    F4,
    F4s,
    G4,
    G4s,
    A4,
    A4s,
    B4,

    C5,
    C5s,
    D5,
    D5s,
    E5,
    F5,
    F5s,
    G5,
    G5s,
    A5,
    A5s,
    B5,

    C6,
    C6s,
    D6,
    D6s,
    E6,
    F6,
    F6s,
    G6,
    G6s,
    A6,
    A6s,
    B6,

    C7,
    C7s,
    D7,
    D7s,
    E7,
    F7,
    F7s,
    G7,
    G7s,
    A7,
    A7s,
    B7,

};

typedef struct {
    char const *noteString;
    unsigned short noteOctave;
    unsigned short freq;
} __attribute__((packed)) Note;

#define FREQ(x) ((unsigned short)(x + 0.5))

static const Note NoteData[] = {
#ifdef USE_NOTE_DATA
#ifdef USE_EXTENDED_OCTAVES
    { "C3",   C3,  FREQ(130.8)  },
#endif
    { "C3#",  C3s, FREQ(138.5)  },
    { "D3",   D3,  FREQ(146.8)  },
    { "D3#",  D3s, FREQ(155.5)  },
    { "E3",   E3,  FREQ(164.8)  },
    { "F3",   F3,  FREQ(174.6)  },
    { "F3#",  F3s, FREQ(184.9)  },
    { "G3",   G3,  FREQ(195.9)  },
    { "G3#",  G3s, FREQ(207.6)  },
    { "A3",   A3,  FREQ(220.0)  },
    { "A3#",  A3s, FREQ(233.0)  },
    { "B3",   B3,  FREQ(246.9)  },
    { "C4",   C4,  FREQ(261.6)  },
    { "C4#",  C4s, FREQ(277.1)  },
    { "D4",   D4,  FREQ(293.6)  },
    { "D4#",  D4s, FREQ(311.1)  },
    { "E4",   E4,  FREQ(329.6)  },
    { "F4",   F4,  FREQ(349.2)  },
    { "F4#",  F4s, FREQ(369.9)  },
    { "G4",   G4,  FREQ(391.9)  },
    { "G4#",  G4s, FREQ(415.3)  },
    { "A4",   A4,  FREQ(440.0)  },
    { "A4#",  A4s, FREQ(466.1)  },
    { "B4",   B4,  FREQ(493.8)  },
    { "C5",   C5,  FREQ(523.2)  },
    { "C5#",  C5s, FREQ(554.3)  },
    { "D5",   D5,  FREQ(587.3)  },
    { "D5#",  D5s, FREQ(622.2)  },
    { "E5",   E5,  FREQ(659.2)  },
    { "F5",   F5,  FREQ(698.4)  },
    { "F5#",  F5s, FREQ(739.9)  },
    { "G5",   G5,  FREQ(783.9)  },
    { "G5#",  G5s, FREQ(830.6)  },
    { "A5",   A5,  FREQ(880.0)  },
    { "A5#",  A5s, FREQ(932.3)  },
    { "B5",   B5,  FREQ(987.7)  },
    { "C6",   C6,  FREQ(1046.5) },

#ifdef USE_EXTENDED_OCTAVES
    { "C6#",  C6s, FREQ(1108.7) },
    { "D6",   D6,  FREQ(1174.6) },
    { "D6#",  D6s, FREQ(1244.5) },
    { "E6",   E6,  FREQ(1318.5) },
    { "F6",   F6,  FREQ(1396.9) },
    { "F6#",  F6s, FREQ(1479.9) },
    { "G6",   G6,  FREQ(1567.9) },
    { "G6#",  G6s, FREQ(1661.2) },
    { "A6",   A6,  FREQ(1760.0) },
    { "A6#",  A6s, FREQ(1864.6) },
    { "B6",   B6,  FREQ(1975.5) },
    { "C7",   C7,  FREQ(2093.0) },
    { "C7#",  C7s, FREQ(2217.4) },
    { "D7",   D7,  FREQ(2349.3) },
    { "D7#",  D7s, FREQ(2489.0) },
    { "E7",   E7,  FREQ(2637.0) },
    { "F7",   F7,  FREQ(2793.8) },
    { "F7#",  F7s, FREQ(2959.9) },
    { "G7",   G7,  FREQ(3135.9) },
    { "G7#",  G7s, FREQ(3322.4) },
    { "A7",   A7,  FREQ(3520.0) },
    { "A7#",  A7s, FREQ(3729.3) },
    { "B7",   B7,  FREQ(3951.0) },
#endif // USE_EXTENDED_OCTAVES
#endif // USE_NOTE_DATA
};

//#define NoteDataCount (sizeof(NoteData)? (sizeof(NoteData)/sizeof(NoteData[0])) : 0)
#define NoteDataCount (sizeof(NoteData)/sizeof(NoteData[0]))

#ifdef USE_STDLIB_BSEARCH
static int noteCmpFunc(const void * a, const void * b) {
   return ((Note const *)a)->noteOctave - ((Note const *)b)->noteOctave;
}
#endif // USE_STDLIB_BSEARCH

int GetNoteFrequency(NoteOctave note, char const **name) {
    int freq = -1;

    // Check for and handle the "silence" note.
    if (note == S)
        return 0;

#ifdef USE_STDLIB_BSEARCH
    // bsearch() isn't available in LT, so don't use it here
    Note key = { NULL, note, 0 };

    Note const *elem = (Note const*) bsearch(
        &key,
        NoteData,
        NoteDataCount,
        sizeof(NoteData),
        noteCmpFunc
    );
#else
    // Cheap, linear, sequential search (still seems fast enough, though)
    Note const *elem = NULL;

    for (int j = 0; j < (int)NoteDataCount; ++j) {
        if (NoteData[j].noteOctave == note) {
            elem = &NoteData[j];
            break;
        }
    }
#endif
    if( elem != NULL ) {
        freq = elem->freq;
        if (name)
            *name = elem->noteString;
    }

    return freq;
}

#define TEMPO (200)      // Base tempo in milliseconds

// Whole, Half, Quarter, Dotted Quarter, Eighth, Sixteenth, and Thirtysecond notes with normal sustain
#define W(n)  { 1, n, TEMPO }
#define H(n)  { 1, n, TEMPO / 2 }
#define Q(n)  { 1, n, TEMPO / 4 }
#define Qd(n) { 1, n, (TEMPO * 3) / 8 }
#define E(n)  { 1, n, TEMPO / 8 }
#define S(n)  { 1, n, TEMPO / 16 }
#define T(n)  { 1, n, TEMPO / 32 }

// Whole, Half, Quarter, Dotted Quarter, Eighth, Sixteenth, and Thirtysecond notes with double sustain
#define W_(n)  { 2, n, TEMPO }
#define H_(n)  { 2, n, TEMPO / 2 }
#define Q_(n)  { 2, n, TEMPO / 4 }
#define Qd_(n) { 2, n, (TEMPO * 3) / 8 }
#define E_(n)  { 2, n, TEMPO / 8 }
#define S_(n)  { 2, n, TEMPO / 16 }
#define T_(n)  { 2, n, TEMPO / 32 }

// Whole, Half, Quarter, Dotted Quarter, Eighth, Sixteenth, and Thirtysecond notes with quadruple sustain
#define _W_(n)  { 4, n, TEMPO }
#define _H_(n)  { 4, n, TEMPO / 2 }
#define _Q_(n)  { 4, n, TEMPO / 4 }
#define _Qd_(n) { 4, n, (TEMPO * 3) / 8 }
#define _E_(n)  { 4, n, TEMPO / 8 }
#define _S_(n)  { 4, n, TEMPO / 16 }
#define _T_(n)  { 4, n, TEMPO / 32 }

// Whole note with octuple sustain
#define __W__(n)  { 8, n, TEMPO }

// Double Whole note with double sustain
#define DW_(n)    { 2, n, TEMPO * 2 }

// Double Whole note with quadruple sustain
#define _DW_(n)   { 4, n, TEMPO * 2 }

// Double Whole note with octuple sustain
#define __DW__(n) { 8, n, TEMPO * 2 }

// Quadruple Whole note with double sustain
#define QW_(n)    { 2, n, TEMPO * 4 }

// Quadruple Whole note with quadruple sustain
#define _QW_(n)   { 4, n, TEMPO * 4 }

// Quadruple Whole note with octuple sustain
#define __QW__(n) { 8, n, TEMPO * 4 }

#ifdef INCLUDE_SONG_MARY
static const MusicElement SongMary[] = {
    // Mary had a little lamb

    Qd_(E5), S(D5), Q(C5), Q(D5),
    Q(E5), Q(E5), _H_(E5),
    Q(D5), Q(D5), _H_(D5),
    Q(E5), Q(G5), _H_(G5),
    Qd_(E5), S(D5), Q(C5), Q(D5),
    Q(E5), Q(E5), Q(E5), Q(E5),
    Q(D5), Q(D5), Q(E5), Q(D5),
   _W_(C5)
};
#define SongMaryElementCount    (sizeof(SongMary)/sizeof(SongMary[0]))
#endif // INCLUDE_SONG_MARY

#ifdef INCLUDE_SONG_FUR_ELISE
static const MusicElement SongFurElise[] = {
    // Beethoven's Fur Elise

    H(S),                        E(E5), E(D5s),
    E(E5),  E(D5s),E(E5), E(B4), E(D5), E(C5),
    _H_(A4), E(S),  E(C4), E(E4), E(A4),
    _H_(B4), E(S),  E(E4), E(G4s),E(B4),
    _H_(C5), E(S),  E(E4), E(E5), E(D5s),
    E(E5),  E(D5s),E(E5), E(B4), E(D5), E(C5),
    _H_(A4), E(S),  E(C4), E(E4), E(A4),
    _H_(B4), E(S),  E(E4), E(C5), E(B4),
    __DW__(A4)
};
#define SongFurEliseElementCount    (sizeof(SongFurElise)/sizeof(SongFurElise[0]))
#endif // INCLUDE_SONG_FUR_ELISE

#ifdef INCLUDE_SONG_FUGUE
static const MusicElement SongFugue[] = {
    // Bach's Toccata and Fugue in D minor

    T(A5), T(G5), _E_(A5), S(S), T(G5), T(F5), T(E5), T(D5), E_(C5s), _H_(D5),
    T(A3), T(G3), E_(A3), S(S), S(S), E_(E4), E_(F4), _E_(C4s), __W__(D4),

};
#define SongFugueElementCount    (sizeof(SongFugue)/sizeof(SongFugue[0]))
#endif // INCLUDE_SONG_FUGUE

#ifdef INCLUDE_SONG_MINUET
static const MusicElement SongMinuet[] = {
    // Bach's Minuet in G (transposed into C)

    Qd(G5),  E(C5),  E(D5),  E(E5),  E(F5),
    Qd(G5), Qd(C5), Qd(C5),
    Qd(A5),  E(F5),  E(G5),  E(A5),  E(B5),
    Qd(C6), Qd(C5), Qd(C5),

    Qd(F5),  E(G5),  E(F5),  E(E5),  E(D5),
    Qd(E5),  E(F5),  E(E5),  E(D5),  E(C5),
    Qd(B4),  E(C5),  E(D5),  E(E5),  E(C5),
    _W_(D5),
};
#define SongMinuetElementCount    (sizeof(SongMinuet)/sizeof(SongMinuet[0]))
#endif // INCLUDE_SONG_MINUET


#ifdef INCLUDE_SONG_SPENCER
static const MusicElement SongSpencer[] = {
    // Stephen Spencer's musical sound effect

    E(E4),  E(C4s),  DW_(B4), W(S),
    E(E3),  E(C3s),  DW_(B3), W(S),

    DW_(S),

    E(E4),  E(C4s),  DW_(B4), W(S),
    E(E3),  E(C3s),  DW_(B3), W(S),

};
#define SongSpencerElementCount    (sizeof(SongSpencer)/sizeof(SongSpencer[0]))
#endif // INCLUDE_SONG_SPENCER

static const SongElement SongList[] = {
#ifdef INCLUDE_SONG_MARY
    { "Mary",       SongMary,       SongMaryElementCount,      4,  2000, 100 },
#endif

#ifdef INCLUDE_SONG_FUR_ELISE
    { "FurElise",   SongFurElise,   SongFurEliseElementCount,  4,  1000, 75  },
#endif

#ifdef INCLUDE_SONG_FUGUE
    { "Fugue",      SongFugue,      SongFugueElementCount,     8,  1000, 100 },
#endif

#ifdef INCLUDE_SONG_MINUET
    { "Minuet",     SongMinuet,     SongMinuetElementCount,    3,  2000, 100  },
#endif

#ifdef INCLUDE_SONG_SPENCER
    { "Spencer",    SongSpencer,    SongSpencerElementCount,   1,  4000, 25  },
#endif
};
#define SongListCount  (sizeof(SongList) / sizeof(SongList[0]))

MusicElement const *GetNextNote(MusicElement const *song, int count, int index) {
    if (index >= count) {
        return NULL;
    }

    return &song[index];
}

unsigned int GetTotalSongs(void) {
    return SongListCount;
}

SongElement const *GetSong(unsigned int index) {
    SongElement const *ret = NULL;
    if (index < GetTotalSongs()) {
        ret = &SongList[index];
    }

    return ret;
}
