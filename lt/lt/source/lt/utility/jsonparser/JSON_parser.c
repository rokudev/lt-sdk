/*
Copyright (c) 2005 JSON.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

The Software shall be used for Good, not Evil.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
    Callbacks, comments, Unicode handling by Jean Gressmann (jean@0x42.de), 2007-2011.
    For my modifications the above license applies as well.

    Changelog:
        2010-11-25
            Support for custom memory allocation (sgbeal@googlemail.com).

        2010-05-07
            Added error handling for memory allocation failure (sgbeal@googlemail.com).
            Added diagnosis errors for invalid JSON.

        2010-03-25
            Fixed buffer overrun in grow_parse_buffer & cleaned up code.

        2009-10-19
            Replaced long double in JSON_value_struct with double after reports
            of strtold being broken on some platforms (charles@transmissionbt.com).

        2009-05-17
            Incorporated benrudiak@googlemail.com fix for UTF16 decoding.

        2009-05-14
            Fixed float parsing bug related to a locale being set that didn't
            use '.' as decimal point character (charles@transmissionbt.com).

        2008-10-14
            Renamed states.IN to states.IT to avoid name clash which IN macro
            defined in windef.h (alexey.pelykh@gmail.com)

        2008-07-19
            Removed some duplicate code & debugging variable (charles@transmissionbt.com)

        2008-05-28
            Made JSON_value structure ansi C compliant. This bug was report by
            trisk@acm.jhu.edu

        2008-05-20
            Fixed bug reported by charles@transmissionbt.com where the switching
            from static to dynamic parse buffer did not copy the static parse
            buffer's content.
*/



#include <lt/core/LTCore.h>
#include "JSON_parser.h"

#define __   -1     /* the universal error code */

/* values chosen so that the object size is approx equal to one page (4K) */
#ifndef JSON_PARSER_STACK_SIZE
//#   define JSON_PARSER_STACK_SIZE 128
#   define JSON_PARSER_STACK_SIZE 64
#endif

#ifndef JSON_PARSER_PARSE_BUFFER_SIZE
//#   define JSON_PARSER_PARSE_BUFFER_SIZE 3496
#   define JSON_PARSER_PARSE_BUFFER_SIZE 1760
#endif

typedef u16 UTF16;

struct JSON_parser_struct {
    JSON_parser_callback    callback;                                           /*    4,    8 :     4,    8 */
    void                   *ctx;                                                /*    4,    8 :     8,   16 */
    signed char            *stack;                                              /*    4,    8 :    12,   24 */
    char                   *parse_buffer;                                       /*    4,    8 :    16,   32 */
    int                     current_char;                                       /*    4,    4 :    20,   36 */
    int                     depth;                                              /*    4,    4 :    24,   40 */
    int                     top;                                                /*    4,    4 :    28,   44 */
    int                     stack_capacity;                                     /*    4,    4 :    32,   48 */
    u32                     currentParseOffset;            /* new: how many characters have been fed us, 0 based */
    u32                     elementStartOffset;            /* new: remember the start offset of the current key or string value */
    u32                     parse_buffer_capacity;                              /*    4,    4 :    36,   52 */
    u32                     parse_buffer_count;                                 /*    4,    4 :    40,   56 */
    signed char             static_stack[JSON_PARSER_STACK_SIZE];               /*  128,  128 :   168,  184 */
    char                    static_parse_buffer[JSON_PARSER_PARSE_BUFFER_SIZE]; /* 3496, 3496 :  3664, 3680 */
    UTF16                   utf16_high_surrogate ;                              /*    2,    2 :  3666, 3682 */
    u16                     intBase;                     /* new: base (10 or 16) for integer conversion; u16 for padding reasons */
    u16                     padding[3];                                         /*    6,    6 :  3672, 3688 */
    signed char             state;                                              /*    1,    1 :  3673, 3689 */
    signed char             before_comment_state;                               /*    1,    1 :  3674, 3690 */
    signed char             type;                                               /*    1,    1 :  3675, 3691 */
    signed char             escaped;                                            /*    1,    1 :  3676, 3692 */
    signed char             comment;                                            /*    1,    1 :  3677, 3693 */
    signed char             allow_comments;                                     /*    1,    1 :  3678, 3694 */
    signed char             error;                                              /*    1,    1 :  3679, 3695 */
           char             decimal_point;                                      /*    1,    1 :  3680, 3696 */
};

//LT_STATIC_ASSERT_SIZE_32_64(struct JSON_parser_struct, 3680, 3696);

#define COUNTOF(x) (sizeof(x)/sizeof(x[0]))

/*
    Characters are mapped into these character classes. This allows for
    a significant reduction in the size of the state transition table.
*/

enum classes {
    C_SPACE,  /* space */
    C_LF,     /* line feed */
    C_CR,     /* carriage return */
    C_WHITE,  /* other whitespace */
    C_LCURB,  /* {  */
    C_RCURB,  /* } */
    C_LSQRB,  /* [ */
    C_RSQRB,  /* ] */
    C_COLON,  /* : */
    C_COMMA,  /* , */
    C_QUOTE,  /* " */
    C_BACKS,  /* \ */
    C_SLASH,  /* / */
    C_PLUS,   /* + */
    C_MINUS,  /* - */
    C_POINT,  /* . */
    C_ZERO ,  /* 0 */
    C_DIGIT,  /* 123456789 */
    C_LOW_A,  /* a */
    C_LOW_B,  /* b */
    C_LOW_C,  /* c */
    C_LOW_D,  /* d */
    C_LOW_E,  /* e */
    C_LOW_F,  /* f */
    C_LOW_L,  /* l */
    C_LOW_N,  /* n */
    C_LOW_R,  /* r */
    C_LOW_S,  /* s */
    C_LOW_T,  /* t */
    C_LOW_U,  /* u */
    C_ABCDF,  /* ABCDF */
    C_E,      /* E */
    C_XX,     /* Xx */
    C_ETC,    /* everything else */
    C_STAR,   /* * */
    NR_CLASSES
};

static const signed char ascii_class[128] = {
/*
    This array maps the 128 ASCII characters into character classes.
    The remaining Unicode characters should be mapped to C_ETC.
    Non-whitespace control characters are errors.
*/
    __,      __,      __,      __,      __,      __,      __,      __,
    __,      C_WHITE, C_LF,    __,      __,      C_CR,    __,      __,
    __,      __,      __,      __,      __,      __,      __,      __,
    __,      __,      __,      __,      __,      __,      __,      __,

    C_SPACE, C_ETC,   C_QUOTE, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_STAR,  C_PLUS,  C_COMMA, C_MINUS, C_POINT, C_SLASH,
    C_ZERO,  C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT, C_DIGIT,
    C_DIGIT, C_DIGIT, C_COLON, C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,

    C_ETC,   C_ABCDF, C_ABCDF, C_ABCDF, C_ABCDF, C_E,     C_ABCDF, C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_ETC,
    C_XX,    C_ETC,   C_ETC,   C_LSQRB, C_BACKS, C_RSQRB, C_ETC,   C_ETC,

    C_ETC,   C_LOW_A, C_LOW_B, C_LOW_C, C_LOW_D, C_LOW_E, C_LOW_F, C_ETC,
    C_ETC,   C_ETC,   C_ETC,   C_ETC,   C_LOW_L, C_ETC,   C_LOW_N, C_ETC,
    C_ETC,   C_ETC,   C_LOW_R, C_LOW_S, C_LOW_T, C_LOW_U, C_ETC,   C_ETC,
    C_XX,    C_ETC,   C_ETC,   C_LCURB, C_ETC,   C_RCURB, C_ETC,   C_ETC
};

/*
    The state codes.
*/
enum states {
    GO,  /* start    */
    OK,  /* ok       */
    OB,  /* object   */
    KE,  /* key      */
    CO,  /* colon    */
    VA,  /* value    */
    AR,  /* array    */
    ST,  /* string   */
    ES,  /* escape   */
    U1,  /* u1       */
    U2,  /* u2       */
    U3,  /* u3       */
    U4,  /* u4       */
    MI,  /* minus    */
    ZE,  /* zero     */
    IT,  /* integer  */
    IH,  /* hex integer  */
    FR,  /* fraction */
    E1,  /* e        */
    E2,  /* ex       */
    E3,  /* exp      */
    T1,  /* tr       */
    T2,  /* tru      */
    T3,  /* true     */
    F1,  /* fa       */
    F2,  /* fal      */
    F3,  /* fals     */
    F4,  /* false    */
    N1,  /* nu       */
    N2,  /* nul      */
    N3,  /* null     */
    C1,  /* /        */
    C2,  /* / *      */
    C3,  /* *        */
    C4,  /* / /      */
    FX,  /* *.* *eE* */
    D1,  /* second UTF-16 character decoding started by \ */
    D2,  /* second UTF-16 character proceeded by u */
    NR_STATES
};

enum actions
{
    CB = -10, /* comment begin */
    CE = -11, /* comment end */
    FA = -12, /* false */
    TR = -13, /* false */
    NU = -14, /* null */
    DE = -15, /* double detected by exponent e E */
    DF = -16, /* double detected by fraction . */
    SB = -17, /* string begin */
    MX = -18, /* integer detected by minus */
    ZX = -19, /* integer detected by zero */
    IX = -20, /* integer detected by 1-9 */
    HX = -21, /* integer converting to hex */
    EX = -22, /* next char is escaped */
    UC = -23  /* Unicode character read */
};


static const signed char state_transition_table[NR_STATES][NR_CLASSES] = {
/*
    The state transition table takes the current state and the current symbol,
    and returns either a new state or an action. An action is represented as a
    negative number. A JSON text is accepted if at the end of the text the
    state is OK and if the mode is MODE_DONE.

                  LF CR white                                     1-9                                   ABCDF  Xx etc
             space |  |  |  {  }  [  ]  :  ,  "  \  /  +  -  .  0  |  a  b  c  d  e  f  l  n  r  s  t  u  |  E  |  |  * */
/*start  GO*/ {GO,GO,GO,GO,-6,__,-5,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*ok     OK*/ {OK,OK,OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*object OB*/ {OB,OB,OB,OB,__,-9,__,__,__,__,SB,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*key    KE*/ {KE,KE,KE,KE,__,__,__,__,__,__,SB,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*colon  CO*/ {CO,CO,CO,CO,__,__,__,__,-2,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*value  VA*/ {VA,VA,VA,VA,-6,__,-5,__,__,__,SB,__,CB,__,MX,__,ZX,IX,__,__,__,__,__,FA,__,NU,__,__,TR,__,__,__,__,__,__},
/*array  AR*/ {AR,AR,AR,AR,-6,__,-5,-7,__,__,SB,__,CB,__,MX,__,ZX,IX,__,__,__,__,__,FA,__,NU,__,__,TR,__,__,__,__,__,__},
/*string ST*/ {ST,__,__,__,ST,ST,ST,ST,ST,ST,-4,EX,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST,ST},
/*escape ES*/ {__,__,__,__,__,__,__,__,__,__,ST,ST,ST,__,__,__,__,__,__,ST,__,__,__,ST,__,ST,ST,__,ST,U1,__,__,__,__,__},
/*u1     U1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U2,U2,U2,U2,U2,U2,U2,U2,__,__,__,__,__,__,U2,U2,__,__,__},
/*u2     U2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U3,U3,U3,U3,U3,U3,U3,U3,__,__,__,__,__,__,U3,U3,__,__,__},
/*u3     U3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U4,U4,U4,U4,U4,U4,U4,U4,__,__,__,__,__,__,U4,U4,__,__,__},
/*u4     U4*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,UC,UC,UC,UC,UC,UC,UC,UC,__,__,__,__,__,__,UC,UC,__,__,__},
/*minus  MI*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,ZE,IT,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*zero   ZE*/ {OK,OK,OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,DF,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,HX,__,__},
/*int    IT*/ {OK,OK,OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,DF,IT,IT,__,__,__,__,DE,__,__,__,__,__,__,__,__,DE,__,__,__},
/*hexint IH*/ {OK,OK,OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,__,IH,IH,IH,IH,IH,IH,IH,IH,__,__,__,__,__,__,IH,IH,__,__,__},
/*frac   FR*/ {OK,OK,OK,OK,__,-8,__,-7,__,-3,__,__,CB,__,__,__,FR,FR,__,__,__,__,E1,__,__,__,__,__,__,__,__,E1,__,__,__},
/*e      E1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,E2,E2,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*ex     E2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*exp    E3*/ {OK,OK,OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,E3,E3,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*tr     T1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T2,__,__,__,__,__,__,__,__},
/*tru    T2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,T3,__,__,__,__,__},
/*true   T3*/ {__,__,__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__,__,__},
/*fa     F1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*fal    F2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F3,__,__,__,__,__,__,__,__,__,__},
/*fals   F3*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,F4,__,__,__,__,__,__,__},
/*false  F4*/ {__,__,__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__,__,__},
/*nu     N1*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N2,__,__,__,__,__},
/*nul    N2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,N3,__,__,__,__,__,__,__,__,__,__},
/*null   N3*/ {__,__,__,__,__,__,__,__,__,__,__,__,CB,__,__,__,__,__,__,__,__,__,__,__,OK,__,__,__,__,__,__,__,__,__,__},
/*/      C1*/ {__,__,__,__,__,__,__,__,__,__,__,__,C4,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,C2},
/*/*     C2*/ {C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C3},
/**      C3*/ {C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,CE,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C2,C3},
/*//     C4*/ {C4,CE,CE,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4,C4},
/*_.     FX*/ {OK,OK,OK,OK,__,-8,__,-7,__,-3,__,__,__,__,__,__,FR,FR,__,__,__,__,E1,__,__,__,__,__,__,__,__,E1,__,__,__},
/*\      D1*/ {__,__,__,__,__,__,__,__,__,__,__,D2,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__},
/*\      D2*/ {__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,__,U1,__,__,__,__,__},
};


/*
    These modes can be pushed on the stack.
*/
enum modes {
    MODE_ARRAY = 1,
    MODE_DONE = 2,
    MODE_KEY = 3,
    MODE_OBJECT = 4
};

static void set_error(JSON_parser jc)
{
    switch (jc->state) {
        case GO:
            switch (jc->current_char) {
            case '{': case '}': case '[': case ']':
                jc->error = JSON_E_UNBALANCED_COLLECTION;
                break;
            default:
                jc->error = JSON_E_INVALID_CHAR;
                break;
            }
            break;
        case OB:
            jc->error = JSON_E_EXPECTED_KEY;
            break;
        case AR:
            jc->error = JSON_E_UNBALANCED_COLLECTION;
            break;
        case CO:
            jc->error = JSON_E_EXPECTED_COLON;
            break;
        case KE:
            jc->error = JSON_E_EXPECTED_KEY;
            break;
        /* \uXXXX\uYYYY */
        case U1: case U2: case U3: case U4: case D1: case D2:
            jc->error = JSON_E_INVALID_UNICODE_SEQUENCE;
            break;
        /* true, false, null */
        case T1: case T2: case T3: case F1: case F2: case F3: case F4: case N1: case N2: case N3:
            jc->error = JSON_E_INVALID_KEYWORD;
            break;
        /* minus, integer, fraction, exponent */
        case MI: case ZE: case IT: case IX: case FR: case E1: case E2: case E3:
            jc->error = JSON_E_INVALID_NUMBER;
            break;
        default:
            jc->error = JSON_E_INVALID_CHAR;
            break;
    }
}

static int
push(JSON_parser jc, int mode)
{
/*
    Push a mode onto the stack. Return false if there is overflow.
*/
    LT_ASSERT(jc->top <= jc->stack_capacity);
    if (jc->depth < 0) {
        if (jc->top == jc->stack_capacity) {
            void* mem = lt_malloc((jc->stack_capacity << 1) * sizeof(jc->stack[0]));
            if (!mem) {
                jc->error = JSON_E_OUT_OF_MEMORY;
                return false;
            }
            lt_memcpy(mem, jc->stack, jc->stack_capacity * sizeof(jc->stack[0]));
            jc->stack_capacity <<= 1;
            if (jc->stack != &jc->static_stack[0]) {
                lt_free(jc->stack);
            }
            jc->stack = (signed char*)mem;
        }
    }
    else if (jc->top == jc->depth) { jc->error = JSON_E_NESTING_DEPTH_REACHED; return false; }

    jc->stack[++jc->top] = (signed char)mode;
    return true;
}


static int
pop(JSON_parser jc, int mode)
{
/*
    Pop the stack, assuring that the current mode matches the expectation.
    Return false if there is underflow or if the modes mismatch.
*/
    if (jc->top < 0 || jc->stack[jc->top] != mode) {
        return false;
    }
    jc->top -= 1;
    return true;
}


#define parse_buffer_clear(jc) \
    do {\
        jc->parse_buffer_count = 0;\
        jc->parse_buffer[0] = 0;\
    } while (0)

#define parse_buffer_pop_back_char(jc)\
    do {\
        LT_ASSERT(jc->parse_buffer_count >= 1);\
        --jc->parse_buffer_count;\
        jc->parse_buffer[jc->parse_buffer_count] = 0;\
    } while (0)



void delete_JSON_parser(JSON_parser jc)
{
    if (jc) {
        if (jc->stack != &jc->static_stack[0]) {
            lt_free((void*)jc->stack);
        }
        if (jc->parse_buffer != &jc->static_parse_buffer[0]) {
            lt_free((void*)jc->parse_buffer);
        }
        lt_free((void*)jc);
     }
}

int JSON_parser_reset(JSON_parser jc)
{
    if (NULL == jc) {
        return false;
    }

    jc->state = GO;
    jc->top = -1;

    /* parser has been used previously? */
    if (NULL == jc->parse_buffer) {

        /* Do we want non-bound stack? */
        if (jc->depth > 0) {
            jc->stack_capacity = jc->depth;
            if (jc->depth <= (int)COUNTOF(jc->static_stack)) {
                jc->stack = &jc->static_stack[0];
            } else {
                const u32 bytes_to_alloc = jc->stack_capacity * sizeof(jc->stack[0]);
                jc->stack = (signed char*)lt_malloc(bytes_to_alloc);
                if (jc->stack == NULL) {
                    return false;
                }
            }
        } else {
            jc->stack_capacity = (int)COUNTOF(jc->static_stack);
            jc->depth = -1;
            jc->stack = &jc->static_stack[0];
        }

        /* set up the parse buffer */
        jc->parse_buffer = &jc->static_parse_buffer[0];
        jc->parse_buffer_capacity = COUNTOF(jc->static_parse_buffer);
    }

    /* set parser to start */
    jc->type = JSON_T_NONE;
    jc->escaped = 0;
    jc->before_comment_state = 0;
    jc->error = 0;
    push(jc, MODE_DONE);
    parse_buffer_clear(jc);

    /* reset the byte offset counts */
    jc->currentParseOffset = jc->elementStartOffset = 0;
    return true;
}

JSON_parser
new_JSON_parser(JSON_config const * config)
{
/*
    new_JSON_parser starts the checking process by constructing a JSON_parser
    object. It takes a depth parameter that restricts the level of maximum
    nesting.

    To continue the process, call JSON_parser_char for each character in the
    JSON text, and then call JSON_parser_done to obtain the final result.
    These functions are fully reentrant.
*/

    JSON_parser jc;

    /* bail if no configuration provided */
    if (NULL == config) return NULL;

    jc = (JSON_parser)lt_malloc(sizeof(*jc));

    if (NULL == jc) return NULL;

    /* configure the parser */
    lt_memset(jc, 0, sizeof(*jc));
    jc->callback = config->callback;
    jc->ctx = config->callback_ctx;
    jc->allow_comments = (signed char)(config->allow_comments != 0);
    jc->decimal_point = '.';
    /* We need to be able to push at least one object */
    jc->depth = config->depth == 0 ? 1 : config->depth;

    /* reset the parser */
    if (!JSON_parser_reset(jc)) {
        lt_free(jc);
        return NULL;
    }

    return jc;
}

JSON_parser
new_JSON_parser_ForLT(void)
{
    /* special new function for LT, doesn't require config or params so as to be economical of stack usage */
    JSON_parser jc = (JSON_parser)lt_malloc(sizeof(*jc));
    if (jc) {
        lt_memset(jc, 0, sizeof(*jc));
        jc->decimal_point = '.';
        jc->depth = JSON_PARSER_STACK_SIZE - 1;
        jc->allow_comments = 1;
        if (! JSON_parser_reset(jc)) { lt_free(jc); jc = NULL; }
    }
    return jc;
}

void
JSON_parser_setCallbackAndContext(JSON_parser jc, JSON_parser_callback callback, void *callbackContext) {
    jc->callback = callback;
    jc->ctx = callbackContext;
}

static int parse_buffer_grow(JSON_parser jc)
{
    const u32 bytes_to_copy = jc->parse_buffer_count * sizeof(jc->parse_buffer[0]);
    const u32 new_capacity = jc->parse_buffer_capacity * 2;
    const u32 bytes_to_allocate = new_capacity * sizeof(jc->parse_buffer[0]);
    void* mem = lt_malloc(bytes_to_allocate);

    if (mem == NULL) {
        jc->error = JSON_E_OUT_OF_MEMORY;
        return false;
    }

    LT_ASSERT(new_capacity > 0);
    lt_memcpy(mem, jc->parse_buffer, bytes_to_copy);

    if (jc->parse_buffer != &jc->static_parse_buffer[0]) {
        lt_free(jc->parse_buffer);
    }

    jc->parse_buffer = (char*)mem;
    jc->parse_buffer_capacity = new_capacity;

    return true;
}

static int parse_buffer_reserve_for(JSON_parser jc, unsigned chars)
{
    while (jc->parse_buffer_count + chars + 1 > jc->parse_buffer_capacity) {
        if (!parse_buffer_grow(jc)) {
            LT_ASSERT(jc->error == JSON_E_OUT_OF_MEMORY);
            return false;
        }
    }

    return true;
}

#define parse_buffer_has_space_for(jc, count) \
    (jc->parse_buffer_count + (count) + 1 <= jc->parse_buffer_capacity)

#define parse_buffer_push_back_char(jc, c)\
    do {\
        LT_ASSERT(parse_buffer_has_space_for(jc, 1)); \
        jc->parse_buffer[jc->parse_buffer_count++] = c;\
        jc->parse_buffer[jc->parse_buffer_count]   = 0;\
    } while (0)

#define assert_is_non_container_type(jc) \
    LT_ASSERT( \
        jc->type == JSON_T_NULL || \
        jc->type == JSON_T_FALSE || \
        jc->type == JSON_T_TRUE || \
        jc->type == JSON_T_FLOAT || \
        jc->type == JSON_T_INTEGER || \
        jc->type == JSON_T_STRING)


static int parse_parse_buffer(JSON_parser jc)
{
    if (jc->callback) {
        JSON_value value, *arg = NULL;

        if (jc->type != JSON_T_NONE) {
            assert_is_non_container_type(jc);

            switch(jc->type) {
                case JSON_T_FLOAT:
                    arg = &value;
                    /* not checking with end pointer b/c there may be trailing ws */
                    value.vu.float_value = lt_strtod(jc->parse_buffer, NULL);
                    value.elementStartOffset = value.elementEndOffset = 0;
                    break;
                case JSON_T_INTEGER:
                    arg = &value;
                    //sscanf(jc->parse_buffer, JSON_PARSER_INTEGER_SSCANF_TOKEN, &value.vu.integer_value);
                    value.vu.integer_value = lt_strtos64(jc->parse_buffer, NULL, (int)jc->intBase);
                    value.elementStartOffset = value.elementEndOffset = 0;
                    break;
                case JSON_T_STRING:
                    arg = &value;
                    value.vu.str.value = jc->parse_buffer;
                    value.vu.str.length = jc->parse_buffer_count;
                    value.elementStartOffset = jc->elementStartOffset;
                    value.elementEndOffset = jc->currentParseOffset;
                    break;
            }

            if (!(*jc->callback)(jc->ctx, jc->type, arg)) {
                jc->elementStartOffset = 0;
                return false;
            }
        }
    }

    if (jc->type != JSON_T_NONE) jc->elementStartOffset = 0;
    parse_buffer_clear(jc);

    return true;
}

#define IS_HIGH_SURROGATE(uc) (((uc) & 0xFC00) == 0xD800)
#define IS_LOW_SURROGATE(uc)  (((uc) & 0xFC00) == 0xDC00)
#define DECODE_SURROGATE_PAIR(hi,lo) ((((hi) & 0x3FF) << 10) + ((lo) & 0x3FF) + 0x10000)
static const unsigned char utf8_lead_bits[4] = { 0x00, 0xC0, 0xE0, 0xF0 };

static int decode_unicode_char(JSON_parser jc)
{
    int i;
    unsigned uc = 0;
    char* p;
    int trail_bytes;

    LT_ASSERT(jc->parse_buffer_count >= 6);

    p = &jc->parse_buffer[jc->parse_buffer_count - 4];

    for (i = 12; i >= 0; i -= 4, ++p) {
        unsigned x = *p;

        if (x >= 'a') {
            x -= ('a' - 10);
        } else if (x >= 'A') {
            x -= ('A' - 10);
        } else {
            x &= ~0x30u;
        }

        LT_ASSERT(x < 16);

        uc |= x << i;
    }

    /* clear UTF-16 char from buffer */
    jc->parse_buffer_count -= 6;
    jc->parse_buffer[jc->parse_buffer_count] = 0;

    /* attempt decoding ... */
    if (jc->utf16_high_surrogate) {
        if (IS_LOW_SURROGATE(uc)) {
            uc = DECODE_SURROGATE_PAIR(jc->utf16_high_surrogate, uc);
            trail_bytes = 3;
            jc->utf16_high_surrogate = 0;
        } else {
            /* high surrogate without a following low surrogate */
            return false;
        }
    } else {
        if (uc < 0x80) {
            trail_bytes = 0;
        } else if (uc < 0x800) {
            trail_bytes = 1;
        } else if (IS_HIGH_SURROGATE(uc)) {
            /* save the high surrogate and wait for the low surrogate */
            jc->utf16_high_surrogate = (UTF16)uc;
            return true;
        } else if (IS_LOW_SURROGATE(uc)) {
            /* low surrogate without a preceding high surrogate */
            return false;
        } else {
            trail_bytes = 2;
        }
    }

    jc->parse_buffer[jc->parse_buffer_count++] = (char) ((uc >> (trail_bytes * 6)) | utf8_lead_bits[trail_bytes]);

    for (i = trail_bytes * 6 - 6; i >= 0; i -= 6) {
        jc->parse_buffer[jc->parse_buffer_count++] = (char) (((uc >> i) & 0x3F) | 0x80);
    }

    jc->parse_buffer[jc->parse_buffer_count] = 0;

    return true;
}

static int add_escaped_char_to_parse_buffer(JSON_parser jc, int next_char)
{
    LT_ASSERT(parse_buffer_has_space_for(jc, 1));

    jc->escaped = 0;
    /* remove the backslash */
    parse_buffer_pop_back_char(jc);
    switch(next_char) {
        case 'b':
            parse_buffer_push_back_char(jc, '\b');
            break;
        case 'f':
            parse_buffer_push_back_char(jc, '\f');
            break;
        case 'n':
            parse_buffer_push_back_char(jc, '\n');
            break;
        case 'r':
            parse_buffer_push_back_char(jc, '\r');
            break;
        case 't':
            parse_buffer_push_back_char(jc, '\t');
            break;
        case '"':
            parse_buffer_push_back_char(jc, '"');
            break;
        case '\\':
            parse_buffer_push_back_char(jc, '\\');
            break;
        case '/':
            parse_buffer_push_back_char(jc, '/');
            break;
        case 'u':
            parse_buffer_push_back_char(jc, '\\');
            parse_buffer_push_back_char(jc, 'u');
            break;
        default:
            return false;
    }

    return true;
}

static int add_char_to_parse_buffer(JSON_parser jc, int next_char, int next_class)
{
    if (!parse_buffer_reserve_for(jc, 1)) {
        LT_ASSERT(JSON_E_OUT_OF_MEMORY == jc->error);
        return false;
    }

    if (jc->escaped) {
        if (!add_escaped_char_to_parse_buffer(jc, next_char)) {
            jc->error = JSON_E_INVALID_ESCAPE_SEQUENCE;
            return false;
        }
    } else if (!jc->comment) {
        if ((jc->type != JSON_T_NONE) | !((next_class == C_SPACE) | (next_class == C_WHITE)) /* non-white-space */) {
            parse_buffer_push_back_char(jc, (char)next_char);
        }
    }

    return true;
}

#define assert_type_isnt_string_null_or_bool(jc) \
    LT_ASSERT(jc->type != JSON_T_FALSE); \
    LT_ASSERT(jc->type != JSON_T_TRUE); \
    LT_ASSERT(jc->type != JSON_T_NULL); \
    LT_ASSERT(jc->type != JSON_T_STRING)


int
JSON_parser_char(JSON_parser jc, int next_char)
{
/*
    After calling new_JSON_parser, call this function for each character (or
    partial character) in your JSON text. It can accept UTF-8, UTF-16, or
    UTF-32. It returns true if things are looking ok so far. If it rejects the
    text, it returns false.
*/
#if 0
    int next_class, next_state;

/*
    Store the current char for error handling
*/
    jc->current_char = next_char;

/*
    Determine the character's class.
*/
    if (next_char < 0) {
        jc->error = JSON_E_INVALID_CHAR;
        return false;
    }
    if (next_char >= 128) {
        next_class = C_ETC;
    } else {
        next_class = ascii_class[next_char];
        if (next_class <= __) {
            set_error(jc);
            return false;
        }
    }

    if (!add_char_to_parse_buffer(jc, next_char, next_class)) {
        return false;
    }

/*
    Get the next state from the state transition table.
*/
    next_state = state_transition_table[jc->state][next_class];
#else
    /* rewrite the above to reuse next_char to mean next_class, saving 4 bytes of stack space */
    int next_state;

/*
    Store the current char for error handling
*/
    jc->current_char = next_char;

/*
    Determine the character's class.
*/
    if (jc->current_char < 0) {
        jc->error = JSON_E_INVALID_CHAR;
        return false;
    }
    if (jc->current_char >= 128) {
        next_char = C_ETC;
    } else {
        next_char = ascii_class[jc->current_char];
        if (next_char <= __) {
            set_error(jc);
            return false;
        }
    }

    if (!add_char_to_parse_buffer(jc, jc->current_char, next_char)) {
        return false;
    }

/*
    Get the next state from the state transition table.
*/
    next_state = state_transition_table[jc->state][next_char];

#endif
    if (next_state >= 0) {
/*
    Change the state.
*/
        jc->state = (signed char)next_state;
    } else {
/*
    Or perform one of the actions.
*/
        switch (next_state) {
/* Unicode character */
        case UC:
            if(!decode_unicode_char(jc)) {
                jc->error = JSON_E_INVALID_UNICODE_SEQUENCE;
                return false;
            }
            /* check if we need to read a second UTF-16 char */
            if (jc->utf16_high_surrogate) {
                jc->state = D1;
            } else {
                jc->state = ST;
            }
            break;
/* escaped char */
        case EX:
            jc->escaped = 1;
            jc->state = ES;
            break;
/* integer detected by minus */
        case MX:
            jc->type = JSON_T_INTEGER;
            jc->state = MI;
            jc->intBase = 10;
            break;
/* integer detected by zero */
        case ZX:
            jc->type = JSON_T_INTEGER;
            jc->state = ZE;
            jc->intBase = 10;
            break;
/* integer detected by 1-9 */
        case IX:
            jc->type = JSON_T_INTEGER;
            jc->state = IT;
            jc->intBase = 10;
            break;
/* integer converting to hex */
        case HX:
            LT_ASSERT(jc->type == JSON_T_INTEGER);
            jc->type = JSON_T_INTEGER;
            jc->state = IH;
            jc->intBase = 16;
            break;
/* floating point number detected by exponent*/
        case DE:
            assert_type_isnt_string_null_or_bool(jc);
            jc->type = JSON_T_FLOAT;
            jc->state = E1;
            break;

/* floating point number detected by fraction */
        case DF:
            assert_type_isnt_string_null_or_bool(jc);
        /*******/
            /*
                Some versions of strtod (which underlies sscanf) don't support converting
                C-locale formated floating point values.
            */
            LT_ASSERT(jc->parse_buffer[jc->parse_buffer_count-1] == '.');
            jc->parse_buffer[jc->parse_buffer_count-1] = jc->decimal_point;
        /*******/
            jc->type = JSON_T_FLOAT;
            jc->state = FX;
            break;
/* string begin " */
        case SB:
            parse_buffer_clear(jc);
            LT_ASSERT(jc->type == JSON_T_NONE);
            jc->type = JSON_T_STRING;
            jc->state = ST;
            jc->elementStartOffset = jc->currentParseOffset;
            break;

/* n */
        case NU:
            LT_ASSERT(jc->type == JSON_T_NONE);
            jc->type = JSON_T_NULL;
            jc->state = N1;
            break;
/* f */
        case FA:
            LT_ASSERT(jc->type == JSON_T_NONE);
            jc->type = JSON_T_FALSE;
            jc->state = F1;
            break;
/* t */
        case TR:
            LT_ASSERT(jc->type == JSON_T_NONE);
            jc->type = JSON_T_TRUE;
            jc->state = T1;
            break;

/* closing comment */
        case CE:
            jc->comment = 0;
            LT_ASSERT(jc->parse_buffer_count == 0);
            LT_ASSERT(jc->type == JSON_T_NONE);
            jc->state = jc->before_comment_state;
            break;

/* opening comment  */
        case CB:
            if (!jc->allow_comments) {
                jc->error = JSON_E_INVALID_CHAR;
                return false;
            }
            parse_buffer_pop_back_char(jc);
            if (!parse_parse_buffer(jc)) {
                return false;
            }
            LT_ASSERT(jc->parse_buffer_count == 0);
            LT_ASSERT(jc->type != JSON_T_STRING);
            switch (jc->stack[jc->top]) {
            case MODE_ARRAY:
            case MODE_OBJECT:
                switch(jc->state) {
                case VA:
                case AR:
                    jc->before_comment_state = jc->state;
                    break;
                default:
                    jc->before_comment_state = OK;
                    break;
                }
                break;
            default:
                jc->before_comment_state = jc->state;
                break;
            }
            jc->type = JSON_T_NONE;
            jc->state = C1;
            jc->comment = 1;
            break;
/* empty } */
        case -9:
            parse_buffer_clear(jc);
            if (jc->callback) {
                JSON_value value;
                value.vu.integer_value = 0;
                value.elementStartOffset = 0;
                value.elementEndOffset = jc->currentParseOffset;
                if (!(*jc->callback)(jc->ctx, JSON_T_OBJECT_END, &value)) {
                   return false;
                }
            }
            if (!pop(jc, MODE_KEY)) {
                return false;
            }
            jc->state = OK;
            break;

/* } */ case -8:
            parse_buffer_pop_back_char(jc);
            if (!parse_parse_buffer(jc)) {
                return false;
            }
            if (jc->callback) {
                JSON_value value;
                value.vu.integer_value = 0;
                value.elementStartOffset = 0;
                value.elementEndOffset = jc->currentParseOffset;
                if (!(*jc->callback)(jc->ctx, JSON_T_OBJECT_END, &value)) {
                   return false;
                }
            }
            if (!pop(jc, MODE_OBJECT)) {
                jc->error = JSON_E_UNBALANCED_COLLECTION;
                return false;
            }
            jc->type = JSON_T_NONE;
            jc->state = OK;
            break;

/* ] */ case -7:
            parse_buffer_pop_back_char(jc);
            if (!parse_parse_buffer(jc)) {
                return false;
            }
            if (jc->callback) {
                JSON_value value;
                value.vu.integer_value = 0;
                value.elementStartOffset = 0;
                value.elementEndOffset = jc->currentParseOffset;
                if (!(*jc->callback)(jc->ctx, JSON_T_ARRAY_END, &value)) {
                    return false;
                }
            }
            if (!pop(jc, MODE_ARRAY)) {
                jc->error = JSON_E_UNBALANCED_COLLECTION;
                return false;
            }

            jc->type = JSON_T_NONE;
            jc->state = OK;
            break;

/* { */ case -6:
            parse_buffer_pop_back_char(jc);
            if (jc->callback) {
                JSON_value value;
                value.vu.integer_value = 0;
                value.elementStartOffset = jc->currentParseOffset;
                value.elementEndOffset = 0;
                if (!(*jc->callback)(jc->ctx, JSON_T_OBJECT_BEGIN, &value)) {
                    return false;
                }
            }
            if (!push(jc, MODE_KEY)) {
                return false;
            }
            LT_ASSERT(jc->type == JSON_T_NONE);
            jc->state = OB;
            break;

/* [ */ case -5:
            parse_buffer_pop_back_char(jc);

            if (jc->callback) {
                JSON_value value;
                value.vu.integer_value = 0;
                value.elementStartOffset = jc->currentParseOffset;
                value.elementEndOffset = 0;
                if (!(*jc->callback)(jc->ctx, JSON_T_ARRAY_BEGIN, &value)) {
                    return false;
                }
            }
            if (!push(jc, MODE_ARRAY)) {
                return false;
            }
            LT_ASSERT(jc->type == JSON_T_NONE);
            jc->state = AR;
            break;

/* string end " */ case -4:
            parse_buffer_pop_back_char(jc);
            switch (jc->stack[jc->top]) {
            case MODE_KEY:
                LT_ASSERT(jc->type == JSON_T_STRING);
                jc->type = JSON_T_NONE;
                jc->state = CO;

                if (jc->callback) {
                    JSON_value value;
                    value.vu.str.value = jc->parse_buffer;
                    value.vu.str.length = jc->parse_buffer_count;
                    value.elementStartOffset = jc->elementStartOffset;
                    value.elementEndOffset = jc->currentParseOffset;
                    jc->elementStartOffset = 0;
                    if (!(*jc->callback)(jc->ctx, JSON_T_KEY, &value)) {
                        return false;
                    }
                }
                parse_buffer_clear(jc);
                break;
            case MODE_ARRAY:
            case MODE_OBJECT:
                LT_ASSERT(jc->type == JSON_T_STRING);
                if (!parse_parse_buffer(jc)) {
                    return false;
                }
                jc->type = JSON_T_NONE;
                jc->state = OK;
                break;
            default:
                return false;
            }
            break;

/* , */ case -3:
            parse_buffer_pop_back_char(jc);
            if (!parse_parse_buffer(jc)) {
                return false;
            }
            switch (jc->stack[jc->top]) {
            case MODE_OBJECT:
/*
    A comma causes a flip from object mode to key mode.
*/
                if (!pop(jc, MODE_OBJECT) || !push(jc, MODE_KEY)) {
                    return false;
                }
                LT_ASSERT(jc->type != JSON_T_STRING);
                jc->type = JSON_T_NONE;
                jc->state = KE;
                break;
            case MODE_ARRAY:
                LT_ASSERT(jc->type != JSON_T_STRING);
                jc->type = JSON_T_NONE;
                jc->state = VA;
                break;
            default:
                return false;
            }
            break;

/* : */ case -2:
/*
    A colon causes a flip from key mode to object mode.
*/
            parse_buffer_pop_back_char(jc);
            if (!pop(jc, MODE_KEY) || !push(jc, MODE_OBJECT)) {
                return false;
            }
            LT_ASSERT(jc->type == JSON_T_NONE);
            jc->state = VA;
            break;
/*
    Bad action.
*/
        default:
            set_error(jc);
            return false;
        }
    }
    jc->currentParseOffset++;
    return true;
}

int
JSON_parser_done(JSON_parser jc)
{
    if ((jc->state == OK || jc->state == GO) && pop(jc, MODE_DONE))
    {
        return true;
    }

    jc->error = JSON_E_UNBALANCED_COLLECTION;
    return false;
}


int JSON_parser_is_legal_white_space_string(const char* s)
{
    int c, char_class;

    if (s == NULL) {
        return false;
    }

    for (; *s; ++s) {
        c = *s;

        if (c < 0 || c >= 128) {
            return false;
        }

        char_class = ascii_class[c];

        if (char_class != C_SPACE && char_class != C_WHITE) {
            return false;
        }
    }

    return true;
}

int JSON_parser_get_last_error(JSON_parser jc)
{
    return jc->error;
}


void init_JSON_config(JSON_config* config)
{
    if (config) {
        lt_memset(config, 0, sizeof(*config));

        config->depth = JSON_PARSER_STACK_SIZE - 1;
    }
}
