/*******************************************************************************
 * <unittest/lt/core/UnitTestLTCoreStrtoTests.c>
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
#include <tilt/TiltImpl.h>

/* clang-format off */

/*******************************************************************************
 * String conversion tests
 * A test for a string-conversion library function has a table of tests to
 * perform, involving various combinations of string to convert, expected length
 * (for testing the "endp" parameter), and the expected value.  Each entry in
 * the test table is run through the test twice:
 * - one (test "a") with a value of 0 for endp, and
 * - the other (test "b") with a valid pointer for endp.
 */

/* Comparing floating point values is hard. This comparison logic
   was adapted from: https://bitbashing.io/comparing-floats.html */
static s64 UlpsDistance(const double a, const double b) {
    s64 ia, ib;
    lt_memcpy(&ia, &a, sizeof(double));
    lt_memcpy(&ib, &b, sizeof(double));
    /* Don't compare differently-signed floats. */
    if ((ia < 0) != (ib < 0)) return LT_S64_MAX;
    /* Return the absolute value of the distance in ULPs. */
    s64 distance = ia - ib;
    if (distance < 0) distance = -distance;
    return distance;
}

static bool NearlyEqual(double a, double b, double fixedEpsilon, int ulpsEpsilon) {
    /* Save work if the floats are equal. Also handles +0 == -0 */
    if (a == b) return true;
    /* Return if either or both values are NaNs. If both
       are NaN, they are considered equal here. */
    if (lt_isnan(a) || lt_isnan(b)) return lt_isnan(a) && lt_isnan(b);
    /* One's infinite and they're not equal. */
    if (lt_isinf(a) || lt_isinf(b)) return false;
    /* Handle the near-zero case. */
    const double difference = (a > b) ? (a - b) : (b - a);
    if (difference <= fixedEpsilon) return true;
    /* Finally resort to comparing ULPs */
    return UlpsDistance(a, b) <= ulpsEpsilon;
}

void Teststrtod(TiltImplReportingCallbacks const * pTRC) {
    struct strtodTestElement {
        char const * rep;        /* what to parse for the test */
        s32 len;                 /* the length of the legitimate part of the string
                                  * (if the end pointer is supplied, it should get a
                                  * value equal to the pointer to the beginning of
                                  * the string plus this */
        double expectedValue;    /* the expected return value */
    };

    static struct strtodTestElement const tests[] = {
        /* 1. Test decimal conversion with and without trailing chars */
        { "3.14159265358979",         16, 3.14159265358979         },
        { " 2.71828182845904",        17, 2.71828182845904         },
        { "299792458q334891",          9, 299792458                },
        /* 2. Test exponential notation with and without trailing chars */
        { "1.87554603778e-18",        17, 1.87554603778e-18        },
        { " -5.391245E44",            13, -5.391245e44             },
        { "187554.603778e-235",       18, 1.87554603778e-230       },
        { "+1.416785e+3e2",           12, 1416.785                 },
        /* 3. Test hexadecimal conversion with and without trailing chars */
        { "0x440FC781D3.056",         16, 0x440FC781D3.056P0       },
        { "   0xE456F103.   ",        14, 0xE456F103p0             },
        { "0xDEADGBEEF",               6, 0xDEADp0                 },
        { "-0xFACE5064. ",            12, -0xFACE5064P0            },
        { "0X55378008P2P",            12, 0X55378008P2             },
        { "0xf00bA2000P-12",          15, 0xF00BA2p0               },
        /* 4. Test INFINITY, infinity, INF, and inf */
        { "INFINITY",                  8,  LT_INFINITY             },
        { "-INFINITY",                 9, -LT_INFINITY             },
        { "infinity",                  8,  LT_INFINITY             },
        { "-infinity",                 9, -LT_INFINITY             },
        { "INF",                       3,  LT_INFINITY             },
        { "-INF",                      4, -LT_INFINITY             },
        { "inf",                       3,  LT_INFINITY             },
        { "-inf",                      4, -LT_INFINITY             },
        /* 5. Test NAN and nan */
        { "NAN",                       3, LT_NAN                   },
        { "nan",                       3, LT_NAN                   },
        /* 6. Test huge positive and negative values */
        { "1.7976931348623157E+308",  23,  1.7976931348623157E+308 },
        { "-1.7976931348623157E+308", 24, -1.7976931348623157E+308 },
        { "1.8E308",                   7,  LT_HUGE_VAL             },
        { "-1.8E308",                  8, -LT_HUGE_VAL             },
        { "1.7976931348623157E+309",  23,  LT_HUGE_VAL             },
        { "-1.7976931348623157E+309", 24, -LT_HUGE_VAL             },
    };

    struct strtodTestElement const * pTest = tests;

    for (u32 n = 1; n <= sizeof tests / sizeof *pTest; ++n, ++pTest) {
        double value = lt_strtod(pTest->rep, 0);
        if (!NearlyEqual(value, pTest->expectedValue, 0.000000000000005, 3)) {
            TILT_REPORT_FAILURE(pTRC, "strtod.result.a: n=%lu", n);
        }
        char *end = 0;
        value = lt_strtod(pTest->rep, &end);
        if (!NearlyEqual(value, pTest->expectedValue, 0.000000000000005, 3)) {
            TILT_REPORT_FAILURE(pTRC, "strtod.result.b: n=%lu", n);
        }
        char const * const expectedEnd = pTest->rep + pTest->len;
        if (end != expectedEnd) {
            TILT_REPORT_FAILURE(pTRC, "strtod.parse-end: n=%lu", n);
        }
    }
}

void Teststrtos32(TiltImplReportingCallbacks const * pTRC) {
    struct strtos32TestElement {
        char const * rep;        /* what to parse for the test */
        s32 len;                 /* the length of the legitimate part of the string
                                  * (if the end pointer is supplied, it should get a
                                  * value equal to the pointer to the beginning of
                                  * the string plus this */
        int base;
        s32 expectedValue;       /* the expected return value */
    };

    static struct strtos32TestElement const tests[] = {
        /* 1. Test decimal conversion with and without leading
         *    and trailing chars */
        { "1276056247",                          10,  0, 1276056247        },
        { "+1694009734",                         11,  0, 1694009734        },
        { "-888021607",                          10,  0, -888021607        },
        { "   1077581042",                       13,  0, 1077581042        },
        { " +235041654",                         11,  0, 235041654         },
        { "   -353899423",                       13,  0, -353899423        },
        { "1151995156 ",                         10,  0, 1151995156        },
        { "+1654667480 ",                        11,  0, 1654667480        },
        { "-2085996316 ",                        11,  0, -2085996316       },
        { " 380548369 ",                         10,  0, 380548369         },
        { "   +1692q39772 ",                      8,  0, 1692              },
        { " -533821973 ",                        11,  0, -533821973        },
        { " 2147483647 ",                        11,  0, 2147483647        },
        { " 2147483648 ",                        11,  0, 2147483647        },
        { " -2147483648 ",                       12,  0, -2147483648       },
        { " -2147483649 ",                       12,  0, -2147483648       },
        /* 2. Test hexadecimal conversion with and without leading
         *    and trailing chars */
        { "0x6b89fa78",                          10,  0, 0x6B89FA78        },
        { "+0Xb34f26",                            9,  0, 0xB34F26          },
        { "-0x16eb0e5e",                         11,  0, -0x16EB0E5E       },
        { " 0x62D20B42",                         11,  0, 0x62D20B42        },
        { "  +0x9fbc918",                        12,  0, 0x9FBC918         },
        { "   -0x2d7cd594",                      14,  0, -0x2D7CD594       },
        { "0x79ef2c4b ",                         10,  0, 0x79EF2C4B        },
        { "+0x3aeeaa24 ",                        11,  0, 0x3AEEAA24        },
        { "-0x3c676db0 ",                        11,  0, -0x3C676DB0       },
        { "   0x77049610 ",                      13,  0, 0x77049610        },
        { " +0x28426868 ",                       12,  0, 0x28426868        },
        { "   -0x37dee0e1 ",                     14,  0, -0x37DEE0E1       },
        { " 0x7fffffff ",                        11,  0, 0x7FFFFFFF        },
        { " 0x80000000 ",                        11,  0, 0x7FFFFFFF        },
        { " -0x80000000 ",                       12,  0, -0x80000000       },
        { " -0x80000001 ",                       12,  0, -0x80000000       },
        /* 3. Test octal conversion with and without trailing chars */
        { "017777777777",                        12,  0, 017777777777      },
        { "020000000000",                        12,  0, 017777777777      },
        { "-020000000000",                       13,  0, -017777777777 - 1 },
        { "-020000000001",                       13,  0, -017777777777 - 1 },
        /* 4. Test binary conversion */
        { "0b00000000000000000000000000000000",  34,  0, 0x00000000        },
        { "0b00101010101010101010101010101010",  34,  0, 0x2AAAAAAA        },
        { "0b01111111111111111111111111111111",  34,  0, 0x7FFFFFFF        },
        { "-0b00101010101010101010101010101010", 35,  0, -0x2AAAAAAA       },
        { "-0b10000000000000000000000000000000", 35,  0, -0x80000000       },
        { "-0b10000000000000000000000000000001", 35,  0, -0x80000000       },
        /* 5. Base confusion */
        { "0b",                                   2,  0, 0x00              },
        { "0b",                                   2,  2, 0x00              },
        { "0b",                                   2, 16, 0x0b              },
        { "0x1234",                               6,  0, 0x1234            },
        { "0x1234",                               1,  2, 0                 },
        { "0x1234",                               6, 16, 0x1234            },
    };

    struct strtos32TestElement const * pTest = tests;

    for (u32 n = 1; n <= sizeof tests / sizeof *pTest; ++n, ++pTest) {
        s32 value = lt_strtos32(pTest->rep, 0, pTest->base);
        if (value != pTest->expectedValue) {
            TILT_REPORT_FAILURE(pTRC, "strtos32.result.a: n=%lu", n);
        }
        char *end = 0;
        value = lt_strtos32(pTest->rep, &end, pTest->base);
        if (value != pTest->expectedValue) {
            TILT_REPORT_FAILURE(pTRC, "strtos32.result.b: n=%lu", n);
        }
        char const * const expectedEnd = pTest->rep + pTest->len;
        if (end != expectedEnd) {
            TILT_REPORT_FAILURE(pTRC, "strtos32.parse-end: n=%lu", n);
        }
    }
}

void Teststrtos64(TiltImplReportingCallbacks const * pTRC) {
    struct strtos64TestElement {
        char const * rep;        /* what to parse for the test */
        s32 len;                 /* the length of the legitimate part of the string
                                  * (if the end pointer is supplied, it should get a
                                  * value equal to the pointer to the beginning of
                                  * the string plus this */
        s64 expectedValue;       /* the expected return value */
    };

    static struct strtos64TestElement const tests[] = {
        /* 1. Test decimal conversion with and without trailing chars */
        { "4203846146547558775",                 19, LT_CONSTS64(4203846146547558775)         },
        { "+7022961019171052169",                20, LT_CONSTS64(7022961019171052169)         },
        { "-4289164947016122184",                20, LT_CONSTS64(-4289164947016122184)        },
        { "   2938904800649265252",              22, LT_CONSTS64(2938904800649265252)         },
        { "  +9134782070183591512",              22, LT_CONSTS64(9134782070183591512)         },
        { "   -7345256814852500853",             23, LT_CONSTS64(-7345256814852500853)        },
        { "6921827637350485364 ",                19, LT_CONSTS64(6921827637350485364)         },
        { "+8260415484299388530 ",               20, LT_CONSTS64(8260415484299388530)         },
        { "-5963687711147179850 ",               20, LT_CONSTS64(-5963687711147179850)        },
        { " 4591913700202716204 ",               20, LT_CONSTS64(4591913700202716204)         },
        { "   +6698814402709343069 ",            23, LT_CONSTS64(6698814402709343069)         },
        { " -522572838656589862 ",               20, LT_CONSTS64(-522572838656589862)         },
        { " -522572838656589862 ",               20, LT_CONSTS64(-522572838656589862)         },
        { " 9223372036854775807 ",               20, LT_CONSTS64(9223372036854775807)         },
        { " 9223372036854775808 ",               20, LT_CONSTS64(9223372036854775807)         },
        { " -9223372036854775808 ",              21, LT_CONSTS64(-9223372036854775807 - 1)    },
        { " -9223372036854775809 ",              21, LT_CONSTS64(-9223372036854775807 - 1)    },
        /* 2. Test hexadecimal conversion with and without trailing chars */
        { "0x6120b9ca7b0498f1",                  18, LT_CONSTS64(0x6120B9CA7B0498F1)          },
        { "+0xa54a60f00362a1e",                  18, LT_CONSTS64(0xA54A60F00362A1E)           },
        { "-0x1823a53840b8041d",                 19, LT_CONSTS64(-0x1823A53840B8041D)         },
        { "  0x306f895269a85309",                20, LT_CONSTS64(0x306F895269A85309)          },
        { "  +0x656838a40d4833ed",               21, LT_CONSTS64(0x656838A40D4833ED)          },
        { "   -0x5974ff3930a0fb3c",              22, LT_CONSTS64(-0x5974FF3930A0FB3C)         },
        { "0x512c3ba700be4e1b ",                 18, LT_CONSTS64(0x512C3BA700BE4E1B)          },
        { "+0x37e2ceaf18a06407 ",                19, LT_CONSTS64(0x37E2CEAF18A06407)          },
        { "-0x7120871f0dc1df57 ",                19, LT_CONSTS64(-0x7120871F0DC1DF57)         },
        { "   0x62a7ddef297f245b ",              21, LT_CONSTS64(0x62A7DDEF297F245B)          },
        { "+0x3d981b760af032e0 ",                19, LT_CONSTS64(0x3D981B760AF032E0)          },
        { "   -0x3f55e48a2dddbdf7 ",             22, LT_CONSTS64(-0x3F55E48A2DDDBDF7)         },
        { "   0x7FFFFFFFFFFFFFFF ",              21, LT_CONSTS64(0x7FFFFFFFFFFFFFFF)          },
        { "   0x10000000000000000 ",             22, LT_CONSTS64(0x7FFFFFFFFFFFFFFF)          },
        { "   -0x8000000000000000 ",             22, LT_CONSTS64(-0x7FFFFFFFFFFFFFFF - 1)     },
        { "   -0x8000000000000001 ",             22, LT_CONSTS64(-0x7FFFFFFFFFFFFFFF - 1)     },
        /* 3. Test octal conversion with and without trailing chars */
        { "076232.asdf",                          6, LT_CONSTS64(076232)                      },
        { "+023476246232453352",                 19, LT_CONSTS64(023476246232453352)          },
        { "-023476246232453352",                 19, LT_CONSTS64(-023476246232453352)         },
        { "012345678",                            8, LT_CONSTS64(001234567)                   },
        { "  +07532152143256125",                20, LT_CONSTS64(07532152143256125)           },
        { "  -07532152143256125",                20, LT_CONSTS64(-07532152143256125)          },
        { "056732123 ",                           9, LT_CONSTS64(056732123)                   },
        { "+021345254 ",                         10, LT_CONSTS64(021345254)                   },
        { "-021345254 ",                         10, LT_CONSTS64(-021345254)                  },
        { "035451234 ",                           9, LT_CONSTS64(035451234)                   },
        { "  +0522521 ",                         10, LT_CONSTS64(0522521)                     },
        { "0777777777777777777777",              22, LT_CONSTS64(0777777777777777777777)      },
        { "01000000000000000000000",             23, LT_CONSTS64(0777777777777777777777)      },
        { "-01000000000000000000000",            24, LT_CONSTS64(-0777777777777777777777 - 1) },
        { "-01000000000000000000001",            24, LT_CONSTS64(-0777777777777777777777 - 1) },
        /* 4. Test binary conversion */
        { "0b00000000000000000000000000000000",  34, LT_CONSTS64(0x00000000)                  },
        { "0b00101010101010101010101010101010",  34, LT_CONSTS64(0x2AAAAAAA)                  },
        { "0b01111111111111111111111111111111",  34, LT_CONSTS64(0x7FFFFFFF)                  },
        { "-0b00101010101010101010101010101010", 35, LT_CONSTS64(-0x2AAAAAAA)                 },
        { "-0b10000000000000000000000000000000", 35, LT_CONSTS64(-0x80000000)                 },
        { "-0b10000000000000000000000000000001", 35, LT_CONSTS64(-0x80000001)                 },
    };

    struct strtos64TestElement const * pTest = tests;

    for (u32 n = 1; n <= sizeof tests / sizeof *pTest; ++n, ++pTest) {
        s64 value = lt_strtos64(pTest->rep, 0, 0);
        if (value != pTest->expectedValue) {
            TILT_REPORT_FAILURE(pTRC, "strtos64.result.a: n=%lu", n);
        }
        char *end = 0;
        value = lt_strtos64(pTest->rep, &end, 0);
        if (value != pTest->expectedValue) {
            TILT_REPORT_FAILURE(pTRC, "strtos64.result.b: n=%lu", n);
        }
        char const * const expectedEnd = pTest->rep + pTest->len;
        if (end != expectedEnd) {
            TILT_REPORT_FAILURE(pTRC, "strtos64.parse-end: n=%lu", n);
        }
    }
}

void Teststrtou32(TiltImplReportingCallbacks const * pTRC) {
    struct strtou32TestElement {
        char const * rep;        /* what to parse for the test */
        s32 len;                 /* the length of the legitimate part of the string
                                  * (if the end pointer is supplied, it should get a
                                  * value equal to the pointer to the beginning of
                                  * the string plus this */
        u32 expectedValue;       /* the expected value */
    };

    static struct strtou32TestElement const tests[] = {
        /* 1. Test decimal conversion with and without leading
         *    and trailing chars */
        { "3753203638",                          10, 3753203638u   },
        { "+1009114548",                         11, 1009114548u   },
        { "  415925041",                         11, 415925041u    },
        { " +2012883765",                        12, 2012883765u   },
        { "99786358 ",                            8, 99786358u     },
        { "+526684766 ",                         10, 526684766u    },
        { "   740560102 ",                       12, 740560102u    },
        { " +1716260722 ",                       12, 1716260722u   },
        { " +4294967295 ",                       12, 4294967295u   },
        { " +4294967296 ",                       12, 4294967295u   },
        { "999999999999 ",                       12, 4294967295u   },
        /* 2. Test hexadecimal conversion with and without leading
         *    and trailing chars */
        { "0xc4b.a8d",                            5, 0xC4Bu        },
        { "+0x65e5667b",                         11, 0x65E5667Bu   },
        { "0x2EC1164C",                          10, 0x2EC1164Cu   },
        { "  +0X7ac855ad",                       13, 0x7AC855ADu   },
        { "0x56bae86a ",                         10, 0x56BAE86Au   },
        { "+0xE03d4b4b ",                        11, 0xE03D4B4Bu   },
        { "0x358ec105 ",                         10, 0x358EC105u   },
        { "  +0x1a6f9c50 ",                      13, 0x1A6F9C50u   },
        { " +0xFFFFFFFF ",                       12, 0xFFFFFFFFu   },
        { " +0x100000000 ",                      13, 0xFFFFFFFFu   },
        { "0x10000000000 ",                      13, 0xFFFFFFFFu   },
        /* 3. Test octal conversion with and without leading
         *    and trailing chars */
        { "076232.asdf",                          6, 076232u       },
        { "+023453352",                          10, 023453352u    },
        { "-023453352",                          10, -023453352u   },
        { "012345678",                            8, 001234567u    },
        { "  +0753215214",                       13, 0753215214u   },
        { "  -0753215214",                       13, -0753215214u  },
        { "056732123 ",                           9, 056732123u    },
        { "+021345254 ",                         10, 021345254u    },
        { "-021345254 ",                         10, -021345254u   },
        { "035451234 ",                           9, 035451234u    },
        { "  +0522521 ",                         10, 0522521u      },
        { "  +037777777777 ",                    15, 037777777777u },
        { "  +040000000000 ",                    15, 037777777777u },
        { "077777777777777 ",                    15, 037777777777u },
        /* 4. Test binary conversion */
        { "0b00000000000000000000000000000000",  34, 0x00000000u   },
        { "0b00101010101010101010101010101010",  34, 0x2AAAAAAAu   },
        { "0b01111111111111111111111111111111",  34, 0x7FFFFFFFu   },
        { "-0b00101010101010101010101010101010", 35, -0x2AAAAAAAu  },
        { "-0b10000000000000000000000000000000", 35, -0x80000000u  },
        { "-0b10000000000000000000000000000001", 35, -0x80000001u  },
    };

    struct strtou32TestElement const *pTest = tests;

    for (u32 n = 1; n <= sizeof tests / sizeof *pTest; ++n, ++pTest) {
        u32 value = lt_strtou32(pTest->rep, 0, 0);
        if (value != pTest->expectedValue) {
            TILT_REPORT_FAILURE(pTRC, "strtou32.result.a: n=%lu", n);
        }
        char *end = 0;
        value = lt_strtou32(pTest->rep, &end, 0);
        if (value != pTest->expectedValue) {
            TILT_REPORT_FAILURE(pTRC, "strtou32.result.b: n=%lu", n);
        }
        char const * const expectedEnd = pTest->rep + pTest->len;
        if (end != expectedEnd) {
            TILT_REPORT_FAILURE(pTRC, "strtou32.parse-end: n=%lu", n);
        }
    }
}

void Teststrtou64(TiltImplReportingCallbacks const * pTRC) {
    struct strtou64TestElement {
        char const * rep;        /* what to parse for the test */
        s32 len;                 /* the length of the legitimate part of the string
                                  * (if the end pointer is supplied, it should get a
                                  * value equal to the pointer to the beginning of
                                  * the string plus this */
        u64 expectedValue;       /* the expected return value */
    };

    static struct strtou64TestElement const tests[] = {
        /* 1. Test decimal conversion with and without leading
         *    and trailing chars */
        { "17970561580968542816",                20, LT_CONSTU64(17970561580968542816) },
        { "+15473129008534791995",               21, LT_CONSTU64(15473129008534791995) },
        { "  11022154269323778126",              22, LT_CONSTU64(11022154269323778126) },
        { "+67580990,5812791193",                 9, LT_CONSTU64(67580990)             },
        { "16918587722375395428 ",               20, LT_CONSTU64(16918587722375395428) },
        { "+3604135840929671005 ",               20, LT_CONSTU64(3604135840929671005)  },
        { " 12871440621426801140 ",              21, LT_CONSTU64(12871440621426801140) },
        { "   +9765577871863994198 ",            23, LT_CONSTU64(9765577871863994198)  },
        { "  +18446744073709551615 ",            23, LT_CONSTU64(18446744073709551615) },
        { "  +18446744073709551616 ",            23, LT_CONSTU64(18446744073709551615) },
        /* 2. Test hexadecimal conversion with and without leading
         *    and trailing chars */
        { "0X78febeee37a83fe4",                  18, LT_CONSTU64(0x78FEBEEE37A83FE4)   },
        { "+0x11f02d7df1117495",                 19, LT_CONSTU64(0x11F02D7DF1117495)   },
        { " 0x702809da109bc007",                 19, LT_CONSTU64(0x702809DA109BC007)   },
        { "+0x25FF0667002E4162",                 19, LT_CONSTU64(0x25FF0667002E4162)   },
        { "0x1aa3a8b6ad30b0e2 ",                 18, LT_CONSTU64(0x1AA3A8B6AD30B0E2)   },
        { "+0xb5b6212aa7e50ac9 ",                19, LT_CONSTU64(0xB5B6212AA7E50AC9)   },
        { "0x73bd3beb3b028b1c ",                 18, LT_CONSTU64(0x73BD3BEB3B028B1C)   },
        { "+0x8374a1ccc29ccc87 ",                19, LT_CONSTU64(0x8374A1CCC29CCC87)   },
        { "+0xFFFFFFFFFFFFFFFF ",                19, LT_CONSTU64(0xFFFFFFFFFFFFFFFF)   },
        { "+0x10000000000000000 ",               20, LT_CONSTU64(0xFFFFFFFFFFFFFFFF)   },
        /* 3. Test octal conversion with and without leading
         *    and trailing chars */
        { "076232.asdf",                          6, LT_CONSTU64(076232)               },
        { "+023453352",                          10, LT_CONSTU64(023453352)            },
        { "-023453352",                          10, LT_CONSTU64(-023453352)           },
        { "012345678",                            8, LT_CONSTU64(001234567)            },
        { "  +0753215214",                       13, LT_CONSTU64(0753215214)           },
        { "  -0753215214",                       13, LT_CONSTU64(-0753215214)          },
        { "056732123 ",                           9, LT_CONSTU64(056732123)            },
        { "+021345254 ",                         10, LT_CONSTU64(021345254)            },
        { "-021345254 ",                         10, LT_CONSTU64(-021345254)           },
        { "035451234 ",                           9, LT_CONSTU64(035451234)            },
        { "  +0522521 ",                         10, LT_CONSTU64(0522521)              },
        { "  +037777777777 ",                    15, LT_CONSTU64(037777777777)         },
        { "  +040000000000 ",                    15, LT_CONSTU64(040000000000)         },
        { "077777777777777 ",                    15, LT_CONSTU64(077777777777777)      },
        /* 4. Test binary conversion */
        { "0b00000000000000000000000000000000",  34, LT_CONSTU64(0x00000000)           },
        { "0b00101010101010101010101010101010",  34, LT_CONSTU64(0x2AAAAAAA)           },
        { "0b01111111111111111111111111111111",  34, LT_CONSTU64(0x7FFFFFFF)           },
        { "-0b00101010101010101010101010101010", 35, LT_CONSTU64(-0x2AAAAAAA)          },
        { "-0b10000000000000000000000000000000", 35, LT_CONSTU64(-0x80000000)          },
        { "-0b10000000000000000000000000000001", 35, LT_CONSTU64(-0x80000001)          },
    };

    struct strtou64TestElement const * pTest = tests;

    for (u32 n = 1; n <= sizeof tests / sizeof *pTest; ++n, ++pTest) {
        u64 value = lt_strtou64(pTest->rep, 0, 0);
        if (value != pTest->expectedValue) {
            TILT_REPORT_FAILURE(pTRC, "strtou64.result.a: n=%lu", n);
        }
        char *end = 0;
        value = lt_strtou64(pTest->rep, &end, 0);
        if (value != pTest->expectedValue) {
            TILT_REPORT_FAILURE(pTRC, "strtou64.result.b: n=%lu", n);
        }
        char const * const expectedEnd = pTest->rep + pTest->len;
        if (end != expectedEnd) {
            TILT_REPORT_FAILURE(pTRC, "strtou64.parse-end: n=%lu", n);
        }
    }
}

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  06-Aug-19   constantine created
 *  23-Dec-19   constantine changed name from LTTestNoFault to LTUnitTestCore
 *  18-Jul-20   constantine converted to C
 *  26-Sep-20   constantine ensured failure return values are 10 or higher
 *  06-Dec-20   constantine reworked printf formatting
 *  19-Oct-21   constantine moved to UnitTestLTCore
 *  02-Aug-22   constantine reworked for TILT
 */
