/******************************************************************************
 * peekjson.c
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>

#include <lt/core/LTCore.h>
#include <lt/utility/jsonparser/LTUtilityJsonParser.h>

/*___________
  #defines */
#define PROGRAM_NAME "peekjson"

/*____________________
  utility functions */
static int Usage(void) {
    printf("usage: %s /fully/qualified/json/key [jsonInputFile] \n", PROGRAM_NAME);
    return 1;
}

/*________________
  function main */
int main(int argc, const char ** argv) {

    // parse command line.  usage: peekjson /fully/qualified/json/key [jsonInputFile]
    if (argc < 2|| argc > 3) return Usage();
    const char * pJsonVarname = argv[1];
    const char * pJsonInputFilename = (argc == 3) ? argv[2] : NULL;
    if ((NULL == pJsonVarname) || 0 == *pJsonVarname) return Usage();

    // open the input file
    FILE * pFile = stdin;
    if (pJsonInputFilename) {
        if (NULL == (pFile = fopen(pJsonInputFilename, "rb"))) return printf(PROGRAM_NAME ": failed to open %s for reading\n", pJsonInputFilename), -1;
    }
    else pJsonInputFilename = "stdin";

    // create the parser object and the value struct to hold the value when found
    LTUtilityJsonParser_Value value;
    LTUtilityJsonParser * parser = lt_createobject(LTUtilityJsonParser);
    if (NULL == parser) return fclose(pFile), printf(PROGRAM_NAME ": failed to create LTUtilityJsonParser\n"), -1;

    // conduct search with GetValue in parser feeder mode (by specifying NULL for the json parse text)
    parser->API->GetValue(parser, NULL, pJsonVarname, &value);

    // feed the parser
    int c;
    while ((c = fgetc(pFile)) != EOF) {
        char ch = c & 0xFF;
        if (! parser->API->FeedJsonChars(parser, &ch, 1)) break;
    }
    fclose(pFile);

    if (LTUtilityJsonParser_Value_Found(&value)) {
        /* as long as we found the value we're looking for we don't care about the parser status, just print the value */
        switch (value.type) {
            case kLTUtilityJsonParser_ValueType_Real   : printf("%f\n", value.real);                break;
            case kLTUtilityJsonParser_ValueType_Integer: printf("%lld\n", LT_Ps64(value.integer));  break;
            case kLTUtilityJsonParser_ValueType_String : printf("%s\n", value.string);              break;
            case kLTUtilityJsonParser_ValueType_null   : printf("null\n");                          break;
            case kLTUtilityJsonParser_ValueType_True   : printf("true\n");                          break;
            case kLTUtilityJsonParser_ValueType_False  : printf("false\n");                         break;
            case kLTUtilityJsonParser_ValueType_Array  : printf("[Array]\n");                       break;
            case kLTUtilityJsonParser_ValueType_Object : printf("{Object}\n");                      break;
            default: break;
        }
        value.integer = 0; /* borrow value.integer for the return value; 0 == found */
    }
    else {
       /* didn't find the value we're looking for; was there an error? */
       LTUtilityJsonParser_ParseStatus status;
       parser->API->GetParseStatus(parser, &status);
       if (LTUtilityJsonParser_ParseStatus_IsErrorStatus(&status)) {
            /* print the error to stderr */
            fprintf(stderr, PROGRAM_NAME ": %s:%d:%d: parse error: %s\n", pJsonInputFilename, (int)status.linePos, (int)status.charPosInLine, parser->API->ParseResultToString(status.parseResult));
            value.integer = 1; /* borrow value.integer for the return value; 1 == parse error */
       }
       else value.integer = 0; /* not found is a success case */
    }
    lt_destroyobject(parser);
    return (int)value.integer;
}

 /*******************************************************************************
 *  LOG
 *******************************************************************************
 *  20-Jun-24   augustus    created
 */
