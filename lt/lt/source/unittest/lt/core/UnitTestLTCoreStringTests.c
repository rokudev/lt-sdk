/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreStringTests.c>
 *    __  __      _ __ ______          __  __  ____________
 *   / / / /___  (_) //_  __/__  _____/ /_/ / /_  __/ ____/___  ________
 *  / / / / __ \/ / __// / / _ \/ ___/ __/ /   / / / /   / __ \/ ___/ _ \
 * / /_/ / / / / / /_ / / /  __(__  ) /_/ /___/ / / /___/ /_/ / /  /  __/
 * \____/_/ /_/_/\__//_/  \___/____/\__/_____/_/  \____/\____/_/   \___/
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <lt/core/LTCore.h>
#include <tilt/JiltEngine.h>
#include "UnitTestLTCoreHelpers.h"

/*******************************************************************************
 * Test LTStdlibImpl_isalnum via lt_isalnum.
 * Verify return value that is one for alphanumeric characters and
 * zero in cases where the argument is anything else */
void Testisalnum(Tilt * tilt) {
    TILT_EXPECT_TRUE(tilt, lt_isalnum('a'), "isalnum.a");
    TILT_EXPECT_TRUE(tilt, lt_isalnum('z'), "isalnum.z");
    TILT_EXPECT_TRUE(tilt, lt_isalnum('A'), "isalnum.A");
    TILT_EXPECT_TRUE(tilt, lt_isalnum('Z'), "isxalnum.Z");
    TILT_EXPECT_TRUE(tilt, lt_isalnum('0'), "isalnum.0");
    TILT_EXPECT_TRUE(tilt, lt_isalnum('9'), "isalnum.9");
    TILT_EXPECT_FALSE(tilt, lt_isalnum(' '), "isalnum.spc");
    TILT_EXPECT_FALSE(tilt, lt_isalnum('+'), "isalnum.sym");
}

/* TODO: isalpha */
/* TODO: isdigit */

/*******************************************************************************
 * Test LTStdlibImpl_isxdigit via lt_isxdigit.
 * Verify return value that is one for hex characters (A-F, a-f, 0-9) and
 * zero in cases where the argument is anything else */
void Testisxdigit(Tilt * tilt) {
    TILT_EXPECT_TRUE(tilt, lt_isxdigit('f'), "isxdigit.f");
    TILT_EXPECT_TRUE(tilt, lt_isxdigit('A'), "isxdigit.A");
    TILT_EXPECT_TRUE(tilt, lt_isxdigit('F'), "isxdigit.F");
    TILT_EXPECT_TRUE(tilt, lt_isxdigit('0'), "isxdigit.0");
    TILT_EXPECT_TRUE(tilt, lt_isxdigit('9'), "isxdigit.9");
    TILT_EXPECT_FALSE(tilt, lt_isxdigit('g'), "isxdigit.g");
    TILT_EXPECT_FALSE(tilt, lt_isxdigit('z'), "isxdigit.z");
    TILT_EXPECT_FALSE(tilt, lt_isxdigit('G'), "isxdigit.G");
    TILT_EXPECT_FALSE(tilt, lt_isxdigit('Z'), "isxdigit.Z");
    TILT_EXPECT_FALSE(tilt, lt_isxdigit('+'), "isxdigit.sym");
}

/*******************************************************************************
 * Test LTStdlibImpl_isspace via lt_isspace.
 * Verify return value that is one for white-space character and
 * zero in cases where the argument is anything else */
void Testisspace(Tilt * tilt) {
    TILT_EXPECT_TRUE(tilt, lt_isspace(' '), "isspace.space");
    TILT_EXPECT_TRUE(tilt, lt_isspace('\t'),"isspace.tab");
    TILT_EXPECT_TRUE(tilt, lt_isspace('\f'),"isspace.feed");
    TILT_EXPECT_TRUE(tilt, lt_isspace('\n'),"isspace.newline");
    TILT_EXPECT_TRUE(tilt, lt_isspace('\v'),"isspace.vertical");
    TILT_EXPECT_TRUE(tilt, lt_isspace('\r'),"isspace.carraige");
    TILT_EXPECT_FALSE(tilt, lt_isspace('\b'),"isspace.backspace");
    TILT_EXPECT_FALSE(tilt, lt_isspace('-'), "isspace.dash");
    TILT_EXPECT_FALSE(tilt, lt_isspace('_'), "isspace.ldash");
    TILT_EXPECT_FALSE(tilt, lt_isspace('a'), "isspace.char");
}

/*******************************************************************************
 * Test LTStdlibImpl_isupper via lt_isupper.
 * Verify return value that is one for uppercase letters and zero in cases
 * where the argument is not uppercase or non-alphabetic */
void Testisupper(Tilt * tilt) {
    TILT_EXPECT_TRUE(tilt, lt_isupper('A'), "isupper.ucase1");
    TILT_EXPECT_TRUE(tilt, lt_isupper('Z'), "isupper.ucase2");
    TILT_EXPECT_FALSE(tilt, lt_isupper('a'), "isupper.lcase1");
    TILT_EXPECT_FALSE(tilt, lt_isupper('z'), "isupper.lcase2");
    TILT_EXPECT_FALSE(tilt, lt_isupper('3'), "isupper.int");
    TILT_EXPECT_FALSE(tilt, lt_isupper('+'), "isupper.sym");
}

/*******************************************************************************
 * Test LTStdlibImpl_islower via lt_islower.
 * Verify return value that is one for lower case letters and zero in cases
 * where the argument is uppercase or non-alphabetic */
void Testislower(Tilt const * tilt) {
    TILT_EXPECT_TRUE(tilt, lt_islower('a'), "islower.lcase1");
    TILT_EXPECT_TRUE(tilt, lt_islower('z'), "islower.lcase2");
    TILT_EXPECT_FALSE(tilt, lt_islower('A'), "islower.ucase1");
    TILT_EXPECT_FALSE(tilt, lt_islower('Z'), "islower.ucase2");
    TILT_EXPECT_FALSE(tilt, lt_islower('3'), "islower.int");
    TILT_EXPECT_FALSE(tilt, lt_islower('+'), "islower.sym");
}

/* TODO: toupper */
/* TODO: toupper */

/*****************
 * lt_hextobyte */
void Testhextobyte(Tilt const * tilt) {
    static const char test[] = "a95bA1F0x";
    u8 temp;
    TILT_EXPECT_TRUE(tilt, lt_hextobyte(test, 2, &temp), "error");
    TILT_EXPECT_TRUE(tilt, temp == 0xa9, "error");
    TILT_EXPECT_TRUE(tilt, lt_hextobyte(test + 2, 2, &temp), "error");
    TILT_EXPECT_TRUE(tilt, temp == 0x5b, "error");
    TILT_EXPECT_TRUE(tilt, lt_hextobyte(test + 4, 2, &temp), "error");
    TILT_EXPECT_TRUE(tilt, temp == 0xa1, "error");
    TILT_EXPECT_TRUE(tilt, lt_hextobyte(test + 6, 2, &temp), "error");
    TILT_EXPECT_TRUE(tilt, temp == 0xf0, "error");
    TILT_EXPECT_TRUE(tilt, lt_hextobyte(test + 1, 1, &temp), "error");
    TILT_EXPECT_TRUE(tilt, temp == 0x9, "error");
    TILT_EXPECT_FALSE(tilt, lt_hextobyte(test + 1, 3, &temp), "error");
    TILT_EXPECT_FALSE(tilt, lt_hextobyte(test + 7, 2, &temp), "error");
    TILT_EXPECT_FALSE(tilt, lt_hextobyte(NULL, 2, &temp), "error");
}

/**************************************
 * ltstring_empty / ltstring_isempty */
void Teststrempty(Tilt const * tilt) {
    LTString str = NULL;
    TILT_EXPECT_TRUE(tilt, ltstring_isempty(str), "error");
    str = ltstring_create("");
    TILT_EXPECT_TRUE(tilt, ltstring_isempty(str), "error");
    ltstring_empty(str);
    TILT_EXPECT_TRUE(tilt, ltstring_isempty(str), "error");
    ltstring_appendchar(&str, 'x');
    TILT_EXPECT_FALSE(tilt, ltstring_isempty(str), "error");
    ltstring_empty(str);
    TILT_EXPECT_TRUE(tilt, ltstring_isempty(str), "error");
    ltstring_destroy(str);
}

/*******************************************************************************
 * Test strlen().
 * Verify return value (bytes from beginning to null terminator) for empty and
 * nonempty string. */
void Teststrlen(Tilt const * tilt) {
    static char const * const original = "Negative Operational Failures \0"
                                         "Achieved via Unit Testing of LT";
    static char const * const empty=     "";
    enum { kLen = 30 };
    s32 r = lt_strlen(original);
    if (r != kLen) {
        TILT_REPORT_FAILURE(tilt, "strlen: length=%ld", r);
    }
    r = lt_strlen(empty);
    if (r) {
        TILT_REPORT_FAILURE(tilt, "strlen.empty: length=%ld", r);
    }
}

/*******************************************************************************
 * Test strcmp().
 * Verify return value for comparison of strings that are equal, "greater than"
 * and "less than" the original. */
void Teststrcmp(Tilt const * tilt) {
    static char const * const original = "The quick brown fox jumped over the lazy dog's back.";
    static char const * const same =     "The quick brown fox jumped over the lazy dog's back.";
    static char const * const less =     "The quick brown fox jumped ever the lazy dog's back.";
    static char const * const greater =  "The quick brown fox jumped over the mazy dog's back.";
    TILT_EXPECT_TRUE(tilt, lt_strcmp(original, same)   == 0, "strcmp.nonzero");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(original, greater) < 0, "strcmp.lessthan");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(original, less)    > 0, "strcmp.greaterthan");
}

/*******************************************************************************
 * Test strcasecmp().
 * Compare several variations of the same string, with varying case and value.
 * Verify return values. */
void Teststrcasecmp(Tilt const * tilt) {
    static char const * const original = "The quick brown fox jumped over the lazy dog's back.";
    static char const * const same1 =    "tHE QUICK BROWN FOX JUMPED OVER THE LAZY DOG'S BACK.";
    static char const * const same2 =    "The quick brown fox jumped over the lazy dog's back.";
    static char const * const greater1 = "The quick brown fox jumped over the Mazy dog's back.";
    static char const * const greater2 = "The quick brown fox yumped over the lazy dog's back.";
    static char const * const less1 =    "The quick brown fox bumped over the lazy dog's back.";
    static char const * const less2 =    "The quick brown fox jumped Ever the lazy dog's back.";
    s32 r = 0;
    if ((r = lt_strcasecmp(original, same1))) {
        TILT_REPORT_FAILURE(tilt, "strcasecmp.equal.1: return=%ld", r);
    }
    if ((r = lt_strcasecmp(original, same2))) {
        TILT_REPORT_FAILURE(tilt, "strcasecmp.equal.2: return=%ld", r);
    }
    if ((r = lt_strcasecmp(original, greater1)) >= 0) {
        TILT_REPORT_FAILURE(tilt, "strcasecmp.less.1: return=%ld", r);
    }
    if ((r = lt_strcasecmp(original, greater2)) >= 0) {
        TILT_REPORT_FAILURE(tilt, "strcasecmp.less.2: return=%ld", r);
    }
    if ((r = lt_strcasecmp(original, less1)) <= 0) {
        TILT_REPORT_FAILURE(tilt, "strcasecmp.greater.1: return=%ld", r);
    }
    if ((r = lt_strcasecmp(original, less2)) <= 0) {
        TILT_REPORT_FAILURE(tilt, "strcasecmp.greater.2: return=%ld", r);
    }
}

/*******************************************************************************
 * Test strncmp().
 * Verify return value for fixed-length comparison of strings that are equal,
 * "greater than" and "less than" the original. */
void Teststrncmp(Tilt const * tilt) {
    static char const * const original = "The quick brown fox jumped over the lazy dog's back.";
    static char const * const same =     "The quick brown fox jumped over the lazy dog's back.";
    static char const * const less =     "The quick brown fox jumped ever the lazy dog's back.";
    static char const * const greater =  "The quick brown fox jumped over the mazy dog's back.";
    enum { kLen = 52 };
    s32 r = 0;
    if ((r = lt_strncmp(original, same, kLen))) {
        TILT_REPORT_FAILURE(tilt, "strncmp.equal.1: return=%ld", r);
    }
    if ((r = lt_strncmp(original, greater, 20))) {
        TILT_REPORT_FAILURE(tilt, "strncmp.equal.2: return=%ld", r);
    }
    if ((r = lt_strncmp(original, greater, 0))) { /* "Empty" strings ought to be equal. */
        TILT_REPORT_FAILURE(tilt, "strncmp.empty: return=%ld", r);
    }
    if ((r = lt_strncmp(original, greater, kLen)) >= 0) {
        TILT_REPORT_FAILURE(tilt, "strncmp.lessthan: return=%ld", r);
    }
    if ((r = lt_strncmp(original, less, kLen)) <= 0) {
        TILT_REPORT_FAILURE(tilt, "strncmp.greaterthan: return=%ld", r);
    }
}

/*******************************************************************************
 * Test strncasecmp().
 * Compare several variations of the same string, with varying case and value.
 * Verify return values. */
void Teststrncasecmp(Tilt const * tilt) {
    static char const * const original = "The quick brown fox jumped over the lazy dog's back.";
    static char const * const same1 =    "tHE QUICK BROWN FOX JUMPED OVER THE LAZY DOG'S xxxx.";
    static char const * const same2 =    "The quick brown fox jumped over the lazy dog's yyyy.";
    static char const * const greater1 = "The quick brown fox jumped over the Mazy dog's zzzz.";
    static char const * const greater2 = "The quick brown fox yumped over the lazy dog's tttt.";
    static char const * const less1 =    "The quick brown fox bumped over the lazy dog's aaaa.";
    static char const * const less2 =    "The quick brown fox jumped Ever the lazy dog's qqqq.";
    enum { kLen = 40 };
    s32 r = 0;
    if ((r = lt_strncasecmp(original, same1, kLen))) {
        TILT_REPORT_FAILURE(tilt, "strncasecmp.equal.1: return=%ld", r);
    }
    if ((r = lt_strncasecmp(original, same2, kLen))) {
        TILT_REPORT_FAILURE(tilt, "strncasecmp.equal.2: return=%ld", r);
    }
    if ((r = lt_strncasecmp(original, greater1, kLen)) >= 0) {
        TILT_REPORT_FAILURE(tilt, "strncasecmp.lessthan.1: return=%ld", r);
    }
    if ((r = lt_strncasecmp(original, greater2, kLen)) >= 0) {
        TILT_REPORT_FAILURE(tilt, "strncasecmp.lessthan.2: return=%ld", r);
    }
    if ((r = lt_strncasecmp(original, less1, kLen)) <= 0) {
        TILT_REPORT_FAILURE(tilt, "strncasecmp.greaterthan.1: return=%ld", r);
    }
    if ((r = lt_strncasecmp(original, less2, kLen)) <= 0) {
        TILT_REPORT_FAILURE(tilt, "strncasecmp.greaterthan.2: return=%ld", r);
    }
}

/*******************************************************************************
 * Test strchr().
 * Verify return value for a char present in the target char array, and for a
 * char absent from the array. */
void Teststrchr(Tilt const * tilt) {
    static char const * const test = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
    TILT_EXPECT_TRUE(tilt, lt_strchr(test, 'f') == test + 5, "strchr.6");
    TILT_EXPECT_TRUE(tilt, lt_strchr(test, '9') == NULL,     "strchr.NULL");
}

/*******************************************************************************
 * Test strrchr().
 * Verify return value for a char present in the target char array, and for a
 * char absent from the array. */
void Teststrrchr(Tilt const * tilt) {
    static char const * const test = "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz";
    TILT_EXPECT_TRUE(tilt, lt_strrchr(test, 'f') == test + 31, "strrchr.6");
    TILT_EXPECT_TRUE(tilt, lt_strrchr(test, '9') == NULL,      "strrchr.NULL");
}

/*******************************************************************************
 * Test strstr().
 * Verify return value for a string present in the target char array, and for a
 * string absent from the array. */
void Teststrstr(Tilt const * tilt) {
    static char const * const test = "abcdefghijklmnopqrstuvwxyzabc";
    TILT_EXPECT_TRUE(tilt, lt_strstr(test, "abc") == test,       "strstr.1");
    TILT_EXPECT_TRUE(tilt, lt_strstr(test, "zabc") == test + 25, "strstr.2");
    TILT_EXPECT_TRUE(tilt, lt_strstr(test, "zabcz") == NULL,     "strstr.NULL");
}

/*******************
 * lt_strendswith */
void Teststrendswith(Tilt const * tilt) {
    TILT_EXPECT_TRUE(tilt, lt_strendswith("fun", "n"), "error");
    TILT_EXPECT_TRUE(tilt, lt_strendswith("fun", ""), "error");
    TILT_EXPECT_TRUE(tilt, lt_strendswith("fun", "fun"), "error");
    TILT_EXPECT_FALSE(tilt, lt_strendswith("fun", "u"), "error");
    TILT_EXPECT_FALSE(tilt, lt_strendswith("fun", "fung"), "error");
    TILT_EXPECT_FALSE(tilt, lt_strendswith("fun", "fungus"), "error");
}

/*******************************************************************************
 * Test strdup().
 * Verify that the returned string is the same as the original and is at a
 * different address. Check that passing NULL returns NULL */
void Teststrdup(Tilt const * tilt) {
    static char const * const original = "The quick brown fox jumped over the lazy dog's back.";
    static char const * const empty = "";
    char * original_dup = lt_strdup(original);
    char * empty_dup = lt_strdup(empty);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(original_dup, original) == 0, "strdup.equal.1");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(empty_dup, empty) == 0,       "strdup.equal.empty");
    TILT_EXPECT_TRUE(tilt, original != original_dup,               "strdup.pointers");
    TILT_EXPECT_TRUE(tilt, lt_strdup(NULL) == NULL,                "strdup.NULL");
    lt_free(original_dup);
    lt_free(empty_dup);
}

/**********************
 * lt_strupper_lower */
void Teststrupper_lower(Tilt const * tilt) {
    LTString str = lt_strupper(ltstring_create("\t33This is Fun&\n"));
    TILT_EXPECT_TRUE(tilt, lt_strcmp(str, "\t33THIS IS FUN&\n") == 0, "error");
    str = lt_strlower(str);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(str, "\t33this is fun&\n") == 0, "error");
    ltstring_destroy(str);
}

/*******************************************************************************
 * Test strncpyTerm().
 * Fill the target array, then copy a string to it, verifying accuracy of
 * strings.  Ensure that strncpyTerm() always writes a terminator. */
void TeststrncpyTerm(Tilt const * tilt) {
#define ORIGINAL_STRING  "The frequency of the emitted wave"
#define INTENDED_STRING  "The frequency of "
    static char const * const original = ORIGINAL_STRING;
    static char const * const intended = INTENDED_STRING;
    enum {
        kOriginalSize = sizeof(ORIGINAL_STRING),  /* Size includes NULL */
        kIntendedSize = sizeof(INTENDED_STRING),
        kBufLen       = 40
    };
    char area[kBufLen];
    /* Fill the buffer to ensure that strncpyTerm() always writes a terminator. */
    fill((u8 *) area, (u8) 'y', kBufLen);
    TILT_EXPECT_TRUE(tilt, lt_strncpyTerm(area, original, kBufLen) == (kOriginalSize - 1), "strncpyTerm.%s: bad rtnval", "1");
    if (!strequ(area, original)) {
        TILT_REPORT_FAILURE(tilt, "strncpyTerm.1: result='%s', original='%s'",
                            area, original);
    }
    fill((u8 *) area, (u8) 'z', kBufLen);
    TILT_EXPECT_TRUE(tilt, lt_strncpyTerm(area, original, kIntendedSize) == (kIntendedSize - 1), "strncpyTerm.%s: bad rtnval", "2");
    if (!strequ(area, intended)) {
        TILT_REPORT_FAILURE(tilt, "strncpyTerm.2: result='%s', intended='%s'",
                            area, intended);
    }
    TILT_EXPECT_TRUE(tilt, lt_strncpyTerm(area, original + 1, 0) == 0, "strncpyTerm.%s: bad rtnval", "3");
    if (!strequ(area, intended)) {
        TILT_REPORT_FAILURE(tilt, "strncpyTerm.3: result='%s', intended='%s'",
                            area, intended);
    }
}

/*******************************************************************************
 * Test strncatTerm().
 * Fill the target array, then cat a string to it, verifying accuracy of
 * strings.  Ensure that strncatTerm() always writes a terminator. */
void TeststrncatTerm(Tilt const * tilt) {
    #define BASE_STRING         "You have no chance to survive."
    #define CAT_STRING          "  Make your time."
    #define TOTAL_STRING                       BASE_STRING \
                                                CAT_STRING
    static char const * const   baseString  =  BASE_STRING;
    static char const * const   catString   =   CAT_STRING;
    static char const * const   totalString = TOTAL_STRING;
    enum {
        kBaseSize   = sizeof(BASE_STRING),  /* Size includes NULL */
        kCatSize    = sizeof(CAT_STRING),
        kTotalSize  = sizeof(TOTAL_STRING),
        kBufLen    = kTotalSize
    };
    char area[kBufLen];

    /* Fill the buffer to ensure that strncatTerm() always writes a terminator; */
    fill((u8 *) area, (u8) 'y', kBufLen); area[0] = 0; /* also make sure lt_strncatTerm initially sees the string as empty */

    // cat the two strings and ensure they equal the total string
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, baseString, kBufLen) == (kBaseSize - 1), "strncatTerm.%s: bad rtnval", "1");
    if (!strequ(area, baseString)) TILT_REPORT_FAILURE(tilt, "strncatTerm.2: result='%s', expected='%s'", area, baseString);
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, catString, kBufLen) == (kCatSize - 1), "strncatTerm.%s: bad rtnval", "3");
    if (!strequ(area, totalString)) TILT_REPORT_FAILURE(tilt, "strncatTerm.4: result='%s', expected='%s'", area, totalString);

    // test boundaries
    fill((u8 *) area, (u8) 'z', kBufLen); area[0] = 0;
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, baseString, kBufLen) == (kBaseSize - 1), "strncatTerm.%s: bad rtnval", "5");
    if (!strequ(area, baseString)) TILT_REPORT_FAILURE(tilt, "strncatTerm.6: result='%s', expected='%s'", area, baseString);
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, catString, kBaseSize-1) == 0, "strncatTerm.%s: bad rtnval", "7");
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, catString, kBaseSize) == 0, "strncatTerm.%s: bad rtnval", "8");
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, catString, 0) == 0, "strncatTerm.%s: bad rtnval", "9");
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, catString, 2) == 0, "strncatTerm.%s: bad rtnval", "10");
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, catString, kBaseSize+1) == 1, "strncatTerm.%s: bad rtnval", "11");
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, catString, kBaseSize+2) == 1, "strncatTerm.%s: bad rtnval", "12");
    TILT_EXPECT_TRUE(tilt, lt_strncatTerm(area, catString+2, kBufLen) == (kCatSize - 3), "strncatTerm.%s: bad rtnval", "13");
    if (!strequ(area, totalString)) TILT_REPORT_FAILURE(tilt, "strncatTerm.14: result='%s', expected='%s'", area, totalString);
}

/*******************************************************************************
 * lt_snprintf() / lt_vsnprintf.
 * (This is by no means an exhaustive test) */
void Testsnprintf(Tilt const * tilt) {
    static char const * const jenny = "8675309";
        /* the string that ought to be written */
    static u32 const tommy = 867530912;
    enum { kSize = 10 };
    char area[kSize];
    fill((u8 *) area, (u8) 'x', kSize);
    s32 n = lt_snprintf(area, 8, "%lu", LT_Pu32(tommy));
        /* 8: only write jenny plus terminator */
    TILT_EXPECT_TRUE(tilt, n == 9, "snprintf.return.9");
    /* (bytes that would have been written if not truncated) */
    TILT_EXPECT_TRUE(tilt, strequ(area, jenny), "snprintf.jenny");
    /* I need to make you mine. */
    /* rounding tests */
    /* round up */
    float roundup = 3.9851;
    lt_snprintf(area, sizeof(area), "%0.0f", roundup);
    /* string should be "4" */
    if (!strequ(area, "4")) {
        TILT_REPORT_FAILURE(tilt, "snprintf.roundup.1: area = '%s'", area);
    }
    lt_snprintf(area, sizeof(area), "%0.2f", roundup);
    /* string should be "3.99" */
    if (!strequ(area, "3.99")) {
        TILT_REPORT_FAILURE(tilt, "snprintf.roundup.2: area = '%s'", area);
    }
    roundup = 0.999999;
    lt_snprintf(area, sizeof(area), "%0.0f", roundup);
    /* string should be "1" */
    if (!strequ(area, "1")) {
        TILT_REPORT_FAILURE(tilt, "snprintf.roundup.3: area = '%s'", area);
    }
    roundup = 0.501;
    lt_snprintf(area, sizeof(area), "%0.0f", roundup);
    /* string should be "1" */
    if (!strequ(area, "1")) {
        TILT_REPORT_FAILURE(tilt, "snprintf.roundup.4: area = '%s'", area);
    }
    /* round down */
    float rounddown = 3.3341;
    lt_snprintf(area, sizeof(area), "%0.0f", rounddown);
    /* string should be "3" */
    if (!strequ(area, "3")) {
        TILT_REPORT_FAILURE(tilt, "snprintf.rounddown.1: area = '%s'", area);
    }
    lt_snprintf(area, sizeof(area), "%0.2f", rounddown);
    /* string should be "3.33" */
    if (!strequ(area, "3.33")) {
        TILT_REPORT_FAILURE(tilt, "snprintf.rounddown.2: area = '%s'", area);
    }
    static char zone[21];
    /* Capacity */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(NULL, 0, "%u %s", 3, "hello") == 7, "error");
    /* 64-bit integer */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%llu", LT_U64_MAX) == 20, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "18446744073709551615"), "error");
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%lld", LT_U64_MAX) == 2, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "-1"), "error");
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%lld", LT_S64_MAX) == 19, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "9223372036854775807"), "error");
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%lld", LT_S64_MIN) == 20, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "-9223372036854775808"), "error");
    /* String */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%7.4s", "this is a great test") == 7, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "   this"), "error");
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%*.4s", -10, "this is a great test") == 10, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "this      "), "error");
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%*.*s", 3, 6, "fungus47") == 6, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "fungus"), "error");
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%*.*s", 8, 6, "fungus47") == 8, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "  fungus"), "error");
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%s", (char *)NULL) == 0, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, ""), "error");
    /* Floating point */
    u64 raw_double = 0x3fd5555555555555;  /* 1/3 */
    double *ptr = (double *)&raw_double;
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%.7e", *ptr) == 13, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "3.3333333e-01"), "error");
    raw_double = 0xfff0000000000000;      /* -inf */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%e", *ptr) == 4, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "-inf"), "error");
    raw_double = 0x7ff8000000000001;      /* nan */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%f", *ptr) == 3, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "nan"), "error");
    raw_double = 0x0000000000000000;      /* 0 */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%f", *ptr) == 8, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "0.000000"), "error");
    raw_double = 0x8000000000000000;      /* -0 */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%f", *ptr) == 9, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "-0.000000"), "error");
    raw_double = 0xffefffffffffffff;      /* -ovf (%f) */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%f", *ptr) == 4, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "-ovf"), "error");
    raw_double = 0x7fefffffffffffff;      /* ovf (%f) */
    TILT_EXPECT_TRUE(tilt, lt_snprintf(zone, sizeof(zone), "%f", *ptr) == 3, "error");
    TILT_EXPECT_TRUE(tilt, strequ(zone, "ovf"), "error");
}

/******************************************************************************/

/* lt_u32toString */
void Testu32toString(Tilt const * tilt) {
    u32 year = 2024;
    enum {kYearBufferLen = 5};
    char buffer[kYearBufferLen + 2];

    /* The exact width requested, no padding */
    u32 written = lt_u32toString(year, buffer, kYearBufferLen - 1);
    TILT_EXPECT_TRUE(tilt, written == kYearBufferLen - 1, "error");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((const char*)buffer ,"2024") == 0, "error");

    /* The exact width requested, supplied padding and justification are ignored */
    written = lt_u32toString(year, buffer, (kYearBufferLen - 1) |
                                            kLTStdlib_FormatFlags_RightJustify |
                                            kLTStdlib_FormatFlags_PadWithZeros);
    TILT_EXPECT_TRUE(tilt, written == kYearBufferLen - 1, "error");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((const char*)buffer ,"2024") == 0, "error");

    /* A wider width requested, but left justified with no padding */
    written = lt_u32toString(year, buffer, (kYearBufferLen + 1) | kLTStdlib_FormatFlags_LeftJustify);
    TILT_EXPECT_TRUE(tilt, written == kYearBufferLen - 1, "error");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((const char*)buffer ,"2024") == 0, "error");

    /* Not enough width, the field set to zeros */
    written = lt_u32toString(year, buffer, kYearBufferLen - 2);
    TILT_EXPECT_TRUE(tilt, written == 0, "error");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((const char*)buffer ,"") == 0, "error");
    for (u32 i = 0; i < kYearBufferLen - 2; i++) {
        TILT_EXPECT_TRUE(tilt, buffer[i] == 0, "error");
    }

    /* A wider width padded with zeroes on the left */
    written = lt_u32toString(year, buffer, (kYearBufferLen + 1) |
                                            kLTStdlib_FormatFlags_RightJustify |
                                            kLTStdlib_FormatFlags_PadWithZeros);
    TILT_EXPECT_TRUE(tilt, written == kYearBufferLen - 1, "error");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((const char*)buffer ,"002024") == 0, "error");

    /* A wider width padded with spaces on the left */
    written = lt_u32toString(year, buffer, (kYearBufferLen + 1) |
                                            kLTStdlib_FormatFlags_RightJustify |
                                            kLTStdlib_FormatFlags_PadWithSpaces);
    TILT_EXPECT_TRUE(tilt, written == kYearBufferLen - 1, "error");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((const char*)buffer ,"  2024") == 0, "error");

    /* A wider width padded with spaces on the right */
    written = lt_u32toString(year, buffer, (kYearBufferLen + 1) |
                                            kLTStdlib_FormatFlags_LeftJustify |
                                            kLTStdlib_FormatFlags_PadWithSpaces);
    TILT_EXPECT_TRUE(tilt, written == kYearBufferLen - 1, "error");
    TILT_EXPECT_TRUE(tilt, lt_strcmp((const char*)buffer ,"2024  ") == 0, "error");
}

/*******************************************************************************
 * LT String tests */
void TestLTString(Tilt const * tilt) {
    /* create / append / destroy */
    LTString string = ltstring_create("my string");
    ltstring_append(&string, " is now a whole lot bigger");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "my string is now a whole lot bigger") == 0, "error");
    ltstring_destroy(string);
    string = NULL;
    ltstring_append(&string, "now has something in it");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "now has something in it") == 0, "error");
    ltstring_destroy(string);
    string = ltstring_create("my other string");
    ltstring_append(&string, " is now bigger");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "my other string is now bigger") == 0, "error");
    /* set */
    ltstring_set(&string, "fun");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "fun") == 0, "error");
    ltstring_destroy(string);
    /* format (and by extension vformat) */
    string = NULL;
    ltstring_format(&string, "%s %u fun", "this", 100);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "this 100 fun") == 0, "error");
    /* appendbytes */
    ltstring_appendbytes(&string, " because it is", 8);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "this 100 fun because") == 0, "error");
    ltstring_destroy(string);
    /* appendformat (and by extension vformat) */
    string = lt_strdup("this string is ");
    ltstring_appendformat(&string, "%s fun %u", "great", 37);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "this string is great fun 37") == 0, "error");
    /* trim */
    string[8] = '\0';
    ltstring_trim(&string);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "this str") == 0, "error");
    /* setcapacity */
    ltstring_setcapacity(&string, 9, true);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "this str") == 0, "error");
    ltstring_set(&string, "this is the new value");
    ltstring_setcapacity(&string, 5, true);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "this") == 0, "error");
    ltstring_destroy(string);
    /* createsubstring */
    string = ltstring_createsubstring("my string", 3, 6);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "string") == 0, "error");
    ltstring_destroy(string);
    string = ltstring_createsubstring("my string", 3, 0);
    TILT_EXPECT_TRUE(tilt, ltstring_isempty(string), "error");
    ltstring_destroy(string);
    string = ltstring_createsubstring("my string", 9, 0);
    TILT_EXPECT_TRUE(tilt, ltstring_isempty(string), "error");
    ltstring_destroy(string);
    /* createempty */
    string = ltstring_createempty(17);
    TILT_EXPECT_TRUE(tilt, ltstring_isempty(string), "error");
    lt_memset(string, 'a', 16);
    string[16] = '\0';
    TILT_EXPECT_FALSE(tilt, ltstring_isempty(string), "error");
    ltstring_destroy(string);
    /* setcapacity 2 */
    string = NULL;
    ltstring_setcapacity(&string, 10, false);
    TILT_EXPECT_TRUE(tilt, string[0] == '\0', "error");
    ltstring_destroy(string);
    /* appendformat 2 */
    string = NULL;
    ltstring_appendformat(&string, "%u was %s fungus", 43, "great");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "43 was great fungus") == 0, "error");
    ltstring_destroy(string);
    /* appendchar */
    string = NULL;
    ltstring_appendchar(&string, 'a');
    ltstring_appendchar(&string, 'b');
    ltstring_appendchar(&string, 'c');
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "abc") == 0, "error");
    ltstring_destroy(string);
    /* removechars */
    string = ltstring_create("let's delete the xxxxxxjunk");
    ltstring_removechars(&string, 17, 6);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "let's delete the junk") == 0, "error");
    ltstring_removechars(&string, (u32)-1, 5);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "let's delete the") == 0, "error");
    ltstring_destroy(string);
    /* insert */
    string = ltstring_create("this is a string");
    ltstring_insert(&string, 9, " great");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "this is a great string") == 0, "error");
    ltstring_insert(&string, lt_strlen(string), " too");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "this is a great string too") == 0, "error");
    ltstring_insert(&string, 0, "my ");
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "my this is a great string too") == 0, "error");
    ltstring_destroy(string);
    /* stripwhitespace */
    string = ltstring_create("\n\n \t funky town\t \t\t\n\r");
    ltstring_stripwhitespace(&string, false, true);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "\n\n \t funky town") == 0, "error");
    ltstring_stripwhitespace(&string, true, false);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "funky town") == 0, "error");
    ltstring_stripwhitespace(&string, true, true);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "funky town") == 0, "error");
    ltstring_set(&string, "\n\r \n\nxyz\t\t\t ");
    ltstring_stripwhitespace(&string, true, true);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "xyz") == 0, "error");
    ltstring_setcapacity(&string, 0, true);
    ltstring_stripwhitespace(&string, true, true);
    TILT_EXPECT_TRUE(tilt, lt_strcmp(string, "") == 0, "error");
    ltstring_destroy(string);
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  18-Jul-20   constantine converted to C
 *  06-Dec-20   constantine reworked printf formatting
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  02-Aug-22   constantine reworked for TILT
 *  31-Mar-24   augustus    added test for strncatTerm
*/
