/*******************************************************************************
 * source/lt/utility/jsonparser/LTUtilityJsonParser.c - fast low mem json parser
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <lt/utility/jsonparser/LTUtilityJsonParser.h>
#include "JSON_parser.h"

#define DUMP_VALIDATION_PARSE 0

DEFINE_LTLOG_SECTION("ltutilityjsonparser");

/*______________________________________
  LTUtilityJsonParserImpl.c constants */
enum { kArrayIndexKeyBufferSize = 8 };

/*_______________________________________________
  LTUtilityJsonParserImpl private data members */
typedef_LTObjectImpl(LTUtilityJsonParser, LTUtilityJsonParserImpl) {
	JSON_parser                         pJSONParser;
    LTUtilityJsonParser_ParseCallback  *pParseCallback;
    void                               *parseClientData;
    LTUtilityJsonParser_ValueOffsets    valueOffsets;
    char                               *currentKey;
    char                               *arrayIndexKeyBuffer;
    char                               *keyBuf;
    u16                                 keyBufLen;
    u16                                 currentLevel;
    u16                                 exitLevel;
    u16                                 skipLevel;
    LTUtilityJsonParser_Value           value;
    LTUtilityJsonParser_ParseStatus     parseStatus;
    char                                lastFedChar;
    bool                                bGracefulSubtreeExit;
} LTOBJECT_API;

typedef struct LTUtilityJsonParserImpl_GetValueClientData {
    LTUtilityJsonParserImpl     *parser;
    LTUtilityJsonParser_Value   *value;
    const char                  *currKey;
    u32                          currKeyNum;
    u32                          currKeyLen;
    u32                          numKeys;
    u32                          itemIndex;
} LTUtilityJsonParserImpl_GetValueClientData;

/*_____________________________________________
  LTUtilityJsonParser.c forward declarations */
static bool LTUtilityJsonParserImpl_ParseJson(LTUtilityJsonParserImpl *parser, const char *jsonText, LTUtilityJsonParser_ParseCallback *parseCallback, void *clientData);
static bool LTUtilityJsonParserImpl_GetValueParseCallback(const char *key, LTUtilityJsonParser_Value *value, void *clientData);

/*_______________________________________
  LTUtilityJsonParserImpl constructors */
static bool LTUtilityJsonParserImpl_ConstructObject(LTUtilityJsonParserImpl *parser) {
    if (NULL == (parser->pJSONParser = new_JSON_parser_ForLT())) { LTLOG_YELLOWALERT("nomem", NULL); return false; }
    return true;
}

static void LTUtilityJsonParserImpl_DestructObject(LTUtilityJsonParserImpl *parser) {
    if (parser->keyBuf) lt_free(parser->keyBuf);
    if (parser->arrayIndexKeyBuffer) lt_free(parser->arrayIndexKeyBuffer);
    if (parser->pJSONParser) delete_JSON_parser(parser->pJSONParser);
    if (parser->parseClientData && parser->pParseCallback == &LTUtilityJsonParserImpl_GetValueParseCallback) lt_free(parser->parseClientData);
}

/*_________________________________________
  LTUtilityJsonParser.c helper functions */
static void LTUtilityJsonParserImpl_ResetParserClientData(LTUtilityJsonParserImpl *parser) {
    if (parser->parseClientData && parser->pParseCallback == &LTUtilityJsonParserImpl_GetValueParseCallback) lt_free(parser->parseClientData);
    parser->pParseCallback = NULL;
    parser->parseClientData = NULL;
}

static void
LTUtilityJsonParserImpl_ResetParser(LTUtilityJsonParserImpl *parser) {

    /* reset the JSON_parser and clear our state*/
    JSON_parser_reset(parser->pJSONParser);
    parser->valueOffsets = (LTUtilityJsonParser_ValueOffsets){ 0, 0, 0, 0 };
    parser->currentLevel = parser->exitLevel = parser->skipLevel = 0;
    parser->value.type = kLTUtilityJsonParser_ValueType_KeyNotFound;
    parser->value.integer = 0;
    parser->lastFedChar = 0;
    parser->bGracefulSubtreeExit = false;

    /* clear current key; free key buffer if large-ish */
    parser->currentKey = NULL;
    if (parser->keyBufLen > 63) {
        lt_free(parser->keyBuf);
        parser->keyBuf = NULL;
        parser->keyBufLen = 0;
    }

    /* free temp array index key buffer */
    if (parser->arrayIndexKeyBuffer) {
        lt_free(parser->arrayIndexKeyBuffer);
        parser->arrayIndexKeyBuffer = NULL;
    }

    /* reset the parser clientdata which will also free the GetParserClientData if necessary */
    LTUtilityJsonParserImpl_ResetParserClientData(parser);

    /* reset the status */
    parser->parseStatus.linePos = 1;
    parser->parseStatus.charPosInLine = 1;
    parser->parseStatus.parseResult = kLTUtilityJsonParser_ParseResult_ParserIdle;

}

static u32 LTUtilityJsonParserImpl_CountNumKeys(const char *keys) {
    u32 numKeys = 0;
    while (*keys) { while (*keys == '/') keys++; if (*keys) { numKeys++; while (*keys && *keys != '/') keys++; } }
    return numKeys;
}

static bool LTUtilityJsonParserImpl_AdvanceToNextKey(LTUtilityJsonParserImpl_GetValueClientData *cd) {
    if (cd->currKeyNum == cd->numKeys) return false;
    cd->currKey += cd->currKeyLen;
    cd->currKeyLen = 0;
    while (*cd->currKey == '/') cd->currKey++;
    while (cd->currKey[cd->currKeyLen] && cd->currKey[cd->currKeyLen] != '/') cd->currKeyLen++;
    return (cd->currKeyLen ? cd->currKeyNum++, true : false);
}

static int
LTUtilityJsonParserImpl_SetCurrentKeyAndKeyOffsets(LTUtilityJsonParserImpl * parser, const JSON_value * value) {
    if (value->vu.str.length >= parser->keyBufLen) {
        if (parser->keyBuf) lt_free(parser->keyBuf);
        parser->keyBufLen = value->vu.str.length+1;
        if (NULL == (parser->keyBuf = lt_malloc(parser->keyBufLen))) {
            LTLOG_YELLOWALERT("parsecb.nokeymem", NULL);
            parser->keyBufLen = 0;
            parser->currentKey = NULL;
            parser->valueOffsets.keyOffsetStart = parser->valueOffsets.keyOffsetEnd = 0;
            return 0;
        }
    }
    lt_strncpyTerm(parser->keyBuf, value->vu.str.value, parser->keyBufLen);
    parser->currentKey = parser->keyBuf;
    parser->valueOffsets.keyOffsetStart = value->elementStartOffset;
    parser->valueOffsets.keyOffsetEnd = value->elementEndOffset;
    return 1;
}

static void LTUtilityJsonParserImpl_ClearCurrentKeyAndKeyOffsets(LTUtilityJsonParserImpl * parser) {
    if (parser->currentKey) {
        parser->currentKey = NULL;
        parser->valueOffsets.keyOffsetStart = parser->valueOffsets.keyOffsetEnd = 0;
    }
}

LT_INLINE void LTUtilityJsonParserImpl_ClearCurrentValueOffsets(LTUtilityJsonParserImpl * parser) {
    parser->valueOffsets.keyOffsetStart = parser->valueOffsets.keyOffsetEnd = 0;
}

/*________________________________________________________
  LTUtilityJsonParser.c implementation parser callbacks */
static bool LTUtilityJsonParserImpl_GetValueParseCallback(const char *key, LTUtilityJsonParser_Value *value, void *clientData) {
    LTUtilityJsonParserImpl_GetValueClientData *cd = (LTUtilityJsonParserImpl_GetValueClientData *)clientData;
    if (!key) {
        if (cd->parser->currentLevel == 1) {
            /* don't manufacture an array index for the root object or array, just continue */
            return true;
        }
        if (NULL == cd->parser->arrayIndexKeyBuffer) {
            if (NULL == (cd->parser->arrayIndexKeyBuffer = lt_malloc(kArrayIndexKeyBufferSize))) {
                LTLOG_YELLOWALERT("getvalue.indexkey.nomem", NULL);
                cd->value->integer = 0;
                cd->value->type = kLTUtilityJsonParser_ValueType_KeyNotFound;
                return false;
            }
        }
        lt_u32toString(cd->itemIndex, cd->parser->arrayIndexKeyBuffer, kLTStdlib_FormatFlags_LeftJustify | (kArrayIndexKeyBufferSize - 1));
        key = cd->parser->arrayIndexKeyBuffer;
    }
    bool bKeyMatch = ((0 == lt_strncmp(key, cd->currKey, cd->currKeyLen)) && (0 == key[cd->currKeyLen]));
    if (cd->currKeyNum == cd->numKeys) {
        /* we are looking for the final key */
        if (bKeyMatch) {
            /* we found the final key - copy the value into the user's value struct */
            *cd->value = *value;
            /* if it's a string we have to cache a copy - value->string is only valid for the duration of this callback */
            if (value->type == kLTUtilityJsonParser_ValueType_String) {
               /* It *is* a string.  Let's reuse the parser's key buffer to hold it since we're done parsing and aren't using the key buffer anymore.
                  Also, the key buffer is auto-freed or recycled when the parser is destroyed or restarts.  Perfect! */
                cd->value->integer = lt_strlen(value->string);
                if (cd->parser->keyBufLen <= cd->value->integer) {
                    if (cd->parser->keyBufLen) lt_free(cd->parser->keyBuf);
                    cd->parser->keyBufLen = cd->value->integer + 1;
                    if (NULL == (cd->parser->keyBuf = lt_malloc(cd->parser->keyBufLen))) cd->parser->keyBufLen = 0;
                }
                if (cd->parser->keyBuf) {
                    lt_strncpyTerm(cd->parser->keyBuf, value->string, cd->parser->keyBufLen);
                    cd->value->string = cd->parser->keyBuf;
                }
                else {
                    LTLOG_YELLOWALERT("getvalue.stringfound.nomem", NULL);
                    cd->value->string = NULL;
                    cd->value->type = kLTUtilityJsonParser_ValueType_KeyNotFound;
                }
            }
            return false; /* done parsing, return false to stop */
        }
        /* this wasn't the key we're looking for; we know we must find it at this level so don't descend */
        if (1 != cd->parser->currentLevel) LTUtilityJsonParser_Value_DoNotEnter(value);  /* descend the first level always (the opening json { or [) */
        ++cd->itemIndex;
        return true;
    }
    /* intermediate level; descend if we matched keys */
    if (bKeyMatch) {
        if ((value->type == kLTUtilityJsonParser_ValueType_Object) ||
            (value->type == kLTUtilityJsonParser_ValueType_Array)) {
            cd->itemIndex = 0;
            LTUtilityJsonParser_Value_DoNotExit(value); /* we're going to descend because the key we're looking for is inside this object; no need to exit object if we don't find it inside */
            if (LTUtilityJsonParserImpl_AdvanceToNextKey(cd)) return true;
            LTLOG_YELLOWALERT("getvalue.keyadvance.fail", NULL);
            return false;
        }
        /* matched at intermediate level, but not an object/not descendable, not going to find it */
        return false;
    }
    /* not a match, don't descend and continue parsing this level */
    if (1 != cd->parser->currentLevel) LTUtilityJsonParser_Value_DoNotEnter(value); /* descend the first level always (the opening json { or [) */
    ++cd->itemIndex;
    return true;
}

static int LTUtilityJsonParserImpl_ParseJsonParseCallback(void *context, int type, const JSON_value * value) {
    LTUtilityJsonParserImpl * parser =  (LTUtilityJsonParserImpl *)context;

    parser->value.type = (LTUtilityJsonParser_ValueType)type;
    switch (type) {
        case JSON_T_ARRAY_BEGIN:
        case JSON_T_OBJECT_BEGIN:
            parser->currentLevel++;
            parser->value.integer = 0;
            if (0 == parser->skipLevel) {
                parser->valueOffsets.valueOffsetStart = value->elementStartOffset;
                parser->valueOffsets.valueOffsetEnd = 0;
                if (! (parser->pParseCallback)(parser->currentKey, &parser->value, parser->parseClientData)) return 0;
                if (parser->value.type == kLTUtilityJsonParser_ValueType_DoNotEnter) parser->skipLevel = parser->currentLevel;
                else if (parser->value.type == kLTUtilityJsonParser_ValueType_DoNotExit) parser->exitLevel = parser->currentLevel;
            }
            LTUtilityJsonParserImpl_ClearCurrentKeyAndKeyOffsets(parser);
            return 1;
        case JSON_T_ARRAY_END:
        case JSON_T_OBJECT_END:
            parser->value.integer = 0;
            if (parser->exitLevel && parser->exitLevel == parser->currentLevel) parser->bGracefulSubtreeExit = true;
            if (parser->skipLevel) {
                if (parser->skipLevel == parser->currentLevel) parser->skipLevel = 0;
            }
            else {
                parser->valueOffsets.valueOffsetStart = 0;
                parser->valueOffsets.valueOffsetEnd = value->elementEndOffset;
                if (! (parser->pParseCallback)(parser->currentKey, &parser->value, parser->parseClientData)) return 0;
            }
            parser->currentLevel--;
            LTUtilityJsonParserImpl_ClearCurrentKeyAndKeyOffsets(parser);
            LTUtilityJsonParserImpl_ClearCurrentValueOffsets(parser);
            return 1;
        case JSON_T_INTEGER:
            parser->value.integer = value->vu.integer_value;
            break;
        case JSON_T_FLOAT:
            parser->value.real = value->vu.float_value;
            break;
        case JSON_T_NULL:   /* pass through */
        case JSON_T_TRUE:   /* pass through */
        case JSON_T_FALSE:
            parser->value.integer = 0;
            break;
        case JSON_T_STRING:
            parser->value.string = value->vu.str.value;
            parser->valueOffsets.valueOffsetStart = value->elementStartOffset;
            parser->valueOffsets.valueOffsetEnd = value->elementEndOffset;
            break;
        case JSON_T_KEY:
            return LTUtilityJsonParserImpl_SetCurrentKeyAndKeyOffsets(parser, value);
        default:
            LT_ASSERT(0);
            return 0;
    }

    if (0 == parser->skipLevel) {
        if (JSON_T_STRING != type) LTUtilityJsonParserImpl_ClearCurrentValueOffsets(parser);
        if (! (parser->pParseCallback)(parser->currentKey, &parser->value, parser->parseClientData)) return 0;
    }
    LTUtilityJsonParserImpl_ClearCurrentKeyAndKeyOffsets(parser);

    return 1;
}

static int LTUtilityJsonParserImpl_ValidateJsonParseCallback(void *context, int type, const JSON_value * value) { LT_UNUSED(context); LT_UNUSED(type); LT_UNUSED(value);
    #if DUMP_VALIDATION_PARSE
        LTUtilityJsonParserImpl * parser = (LTUtilityJsonParserImpl *)context;
        if (type == JSON_T_KEY) {
            lt_consoleputstring("JSON_T_KEY: "); lt_consoleputstring(value->vu.str.value); lt_consoleputstring("\n");
            return LTUtilityJsonParserImpl_SetCurrentKeyAndKeyOffsets(parser, value);
        }
        lt_consoleputstring("\""); if (parser->currentKey) lt_consoleputstring(parser->currentKey);
        lt_consoleputstring("\": ");   lt_consoleputstring(LTUtilityJsonParser_ValueTypeToString((LTUtilityJsonParser_ValueType)type));
        lt_consoleputstring("\n");
        LTUtilityJsonParserImpl_ClearCurrentKeyAndKeyOffsets(parser);
    #endif
    return 1;
}

/* ____________________________________
   LTUtilityJsonParser api functions */
static void
LTUtilityJsonParserImpl_GetValue(LTUtilityJsonParserImpl *parser, const char *jsonText, const char *fullyQualifiedKeyName, LTUtilityJsonParser_Value *value) {
    if (value) {
        value->type = kLTUtilityJsonParser_ValueType_KeyNotFound; /* The key isn't found until it is */
        LTUtilityJsonParserImpl_GetValueClientData *cd =  (parser && fullyQualifiedKeyName && *fullyQualifiedKeyName) ? lt_malloc(sizeof(*cd)) : NULL;
        if (cd) {
            cd->parser = parser;
            cd->value = value;
            cd->currKey = fullyQualifiedKeyName;
            cd->currKeyNum = cd->currKeyLen = 0;
            cd->numKeys = LTUtilityJsonParserImpl_CountNumKeys(fullyQualifiedKeyName);
            cd->itemIndex = 0;
            if (LTUtilityJsonParserImpl_AdvanceToNextKey(cd)) LTUtilityJsonParserImpl_ParseJson(parser, jsonText, &LTUtilityJsonParserImpl_GetValueParseCallback, cd);
        }
    }
}

static LTUtilityJsonParser_ParseResult LTUtilityJsonParserImpl_JsonErrorToParseResult(int jsonError) {
    return (jsonError >= JSON_E_NONE && jsonError <= JSON_E_OUT_OF_MEMORY) ? (LTUtilityJsonParser_ParseResult)jsonError : kLTUtilityJsonParser_ParseResult_InternalError;
}

static void
LTUtilityJsonParserImpl_AdvanceParsePosNextLine(LTUtilityJsonParserImpl *parser) {
    parser->parseStatus.linePos++;
    parser->parseStatus.charPosInLine = 1;
}

static void
LTUtilityJsonParserImpl_AdvanceParsePosNextChar(LTUtilityJsonParserImpl *parser) {
    parser->parseStatus.charPosInLine++;
}

static u32
LTUtilityJsonParserImpl_FindArray(LTUtilityJsonParserImpl *parser, const char *jsonText, const char *arrayKeyName) {
    LTUtilityJsonParser_Value value;
    LTUtilityJsonParserImpl_GetValue(parser, jsonText, arrayKeyName, &value);
    return (value.type == kLTUtilityJsonParser_ValueType_Array ? parser->valueOffsets.valueOffsetStart : 0);
}

static u32
LTUtilityJsonParserImpl_FindObject(LTUtilityJsonParserImpl *parser, const char *jsonText, const char *objectKeyName) {
    LTUtilityJsonParser_Value value;
    LTUtilityJsonParserImpl_GetValue(parser, jsonText, objectKeyName, &value);
    return (value.type == kLTUtilityJsonParser_ValueType_Object ? parser->valueOffsets.valueOffsetStart : 0);
}

static bool
LTUtilityJsonParserImpl_ParseJson(LTUtilityJsonParserImpl *parser, const char *jsonText, LTUtilityJsonParser_ParseCallback *parseCallback, void *clientData) {

    if (NULL == parser) return false;

    /* reset the parser */
    LTUtilityJsonParserImpl_ResetParser(parser);

    /* set the client's callback and client data */
    parser->pParseCallback = parseCallback;
    parser->parseClientData = clientData;

    /* set the JSON_parser callback and context to our internal callback with our parser as context */
    JSON_parser_setCallbackAndContext(parser->pJSONParser, parseCallback ? &LTUtilityJsonParserImpl_ParseJsonParseCallback : &LTUtilityJsonParserImpl_ValidateJsonParseCallback, (void *)parser);

    /* bail early if we're in feeder mode */
    if (NULL == jsonText) {
        parser->parseStatus.parseResult = kLTUtilityJsonParser_ParseResult_ReadyForFeeding;
        return true;
    }

    /* if our first character is a { or a [ we will presume we are starting there from the user having obtained
       an offset from FindArray or FindObject, so we'll "balance the collection" so we don't try and parse off the end of it */
    if (jsonText && ((*jsonText == '{') || (*jsonText == '['))) parser->exitLevel = 1;

    /* parse */
    bool bSuccess = false;
    while (*jsonText) {
        if (false == (bSuccess = JSON_parser_char(parser->pJSONParser, (int)((unsigned char)*jsonText)))) {
            bSuccess = parser->bGracefulSubtreeExit;
            break;
        }
        if ((*jsonText == '\n') || ((*jsonText == '\r') && (*(jsonText+1) != '\n'))) LTUtilityJsonParserImpl_AdvanceParsePosNextLine(parser);
        else LTUtilityJsonParserImpl_AdvanceParsePosNextChar(parser);
        jsonText++;
    }
    bSuccess = bSuccess && JSON_parser_done(parser->pJSONParser);
    parser->parseStatus.parseResult = bSuccess ? kLTUtilityJsonParser_ParseResult_ParsingComplete : LTUtilityJsonParserImpl_JsonErrorToParseResult(JSON_parser_get_last_error(parser->pJSONParser));
    LTUtilityJsonParserImpl_ResetParserClientData(parser);
    return bSuccess;
}

static bool
LTUtilityJsonParserImpl_ValidateJson(LTUtilityJsonParserImpl *parser, const char *jsonText) {
    return LTUtilityJsonParserImpl_ParseJson(parser, jsonText, NULL, NULL);
}

typedef struct FindIncludeDirectivesClientData {
    LTUtilityJsonParserImpl *parser;
    LTUtilityJsonParser_FoundIncludeDirectiveCallback *callback;
    void *clientData;
} FindIncludeDirectivesClientData;

static bool LTUtilityJsonParserImpl_FindIncludeDirectivesParseCallback(const char *key, LTUtilityJsonParser_Value *value, void *clientData) {
    if (value->type == kLTUtilityJsonParser_ValueType_String) {
        FindIncludeDirectivesClientData *cd = (FindIncludeDirectivesClientData *)clientData;
        if (key && key[0] == '.' && key[1] == '.' && key[2] == '.' && key[3] == 0) {
            return cd->callback(value->string, cd->parser->valueOffsets.keyOffsetStart, cd->parser->valueOffsets.valueOffsetEnd, cd->clientData);
        }
        else if (value->string && value->string[0] == '.' && value->string[1] == '.' && value->string[2] == '.' && value->string[3] == ':' && value->string[4] != 0) {
            return cd->callback(value->string+4, cd->parser->valueOffsets.valueOffsetStart, cd->parser->valueOffsets.valueOffsetEnd, cd->clientData);
        }
    }
    return true;
}

static bool
LTUtilityJsonParserImpl_FindIncludeDirectives(LTUtilityJsonParserImpl *parser, const char *jsonText, LTUtilityJsonParser_FoundIncludeDirectiveCallback *foundIncludesCallback, void *clientData) {
    FindIncludeDirectivesClientData findClientData = { parser, foundIncludesCallback, clientData };
    return LTUtilityJsonParserImpl_ParseJson(parser, jsonText, &LTUtilityJsonParserImpl_FindIncludeDirectivesParseCallback, &findClientData);
}

static bool LTUtilityJsonParserImpl_FeedJsonChars(LTUtilityJsonParserImpl *parser, const char * pJsonChars, u32 numChars) {
    bool bSuccess = false;
    if (parser && parser->parseStatus.parseResult == kLTUtilityJsonParser_ParseResult_ReadyForFeeding && pJsonChars && numChars) {
        if (parser->lastFedChar == '\r' && (*pJsonChars != '\n')) LTUtilityJsonParserImpl_AdvanceParsePosNextLine(parser);
        parser->lastFedChar = 0;
        while (numChars--) {
            if (false == (bSuccess = JSON_parser_char(parser->pJSONParser, (int)((unsigned char)*pJsonChars)))) {
                bSuccess = parser->bGracefulSubtreeExit;
                break;
            }
            parser->lastFedChar = *pJsonChars++;
            if (parser->lastFedChar == '\n') LTUtilityJsonParserImpl_AdvanceParsePosNextLine(parser);
            else if (parser->lastFedChar == '\r') {
                if (numChars) {
                    parser->lastFedChar = 0;
                    if (*pJsonChars != '\n') LTUtilityJsonParserImpl_AdvanceParsePosNextLine(parser);
                }
            }
            else LTUtilityJsonParserImpl_AdvanceParsePosNextChar(parser);
        }
        parser->parseStatus.parseResult = bSuccess ? kLTUtilityJsonParser_ParseResult_ReadyForFeeding : LTUtilityJsonParserImpl_JsonErrorToParseResult(JSON_parser_get_last_error(parser->pJSONParser));
    }
    return bSuccess;
}

static bool LTUtilityJsonParserImpl_FeedComplete(LTUtilityJsonParserImpl *parser) {
    bool bSuccess = false;
    if (parser && parser->parseStatus.parseResult == kLTUtilityJsonParser_ParseResult_ReadyForFeeding) bSuccess = JSON_parser_done(parser->pJSONParser);
    parser->parseStatus.parseResult = bSuccess ? kLTUtilityJsonParser_ParseResult_ParsingComplete : LTUtilityJsonParserImpl_JsonErrorToParseResult(JSON_parser_get_last_error(parser->pJSONParser));
    LTUtilityJsonParserImpl_ResetParserClientData(parser);
    return bSuccess;
}

static void
LTUtilityJsonParserImpl_GetParseStatus(LTUtilityJsonParserImpl *parser, LTUtilityJsonParser_ParseStatus *parseStatus) {
    if (parser && parseStatus) *parseStatus = parser->parseStatus;
}

static void LTUtilityJsonParserImpl_GetValueOffsets(LTUtilityJsonParserImpl *parser, LTUtilityJsonParser_Value *value, LTUtilityJsonParser_ValueOffsets *offsetsToSet) {
    if (!offsetsToSet) return;
    if (value) {
        switch (parser->parseStatus.parseResult) {
            case kLTUtilityJsonParser_ParseResult_ParserIdle:
            case kLTUtilityJsonParser_ParseResult_ReadyForFeeding:
            case kLTUtilityJsonParser_ParseResult_ParsingComplete:
                switch (value->type) {
                    case kLTUtilityJsonParser_ValueType_Array:
                    case kLTUtilityJsonParser_ValueType_Object:
                    case kLTUtilityJsonParser_ValueType_String:
                        *offsetsToSet = parser->valueOffsets;
                        break;
                    default:
                        value = NULL;
                        break;
                }
                break;
            default:
                value = NULL;
                break;
        }
    }
    if (! value) *offsetsToSet = (LTUtilityJsonParser_ValueOffsets){ 0,0,0,0};
}

static const char *
LTUtilityJsonParserImpl_ValueTypeToString(LTUtilityJsonParser_ValueType valueType) {
    const char * typeString;
    switch (valueType) {
        case kLTUtilityJsonParser_ValueType_KeyNotFound:   typeString = "KeyNotFound";            break;
        case kLTUtilityJsonParser_ValueType_ArrayEntry:    typeString = "ArrayEntry";             break;
        case kLTUtilityJsonParser_ValueType_ArrayExit:     typeString = "ArrayExit";              break;
        case kLTUtilityJsonParser_ValueType_ObjectEntry:   typeString = "ObjectEntry";            break;
        case kLTUtilityJsonParser_ValueType_ObjectExit:    typeString = "ObjectExit";             break;
        case kLTUtilityJsonParser_ValueType_Integer:       typeString = "Integer";                break;
        case kLTUtilityJsonParser_ValueType_Real:          typeString = "Real";                   break;
        case kLTUtilityJsonParser_ValueType_null:          typeString = "null";                   break;
        case kLTUtilityJsonParser_ValueType_True:          typeString = "True";                   break;
        case kLTUtilityJsonParser_ValueType_False:         typeString = "False";                  break;
        case kLTUtilityJsonParser_ValueType_String:        typeString = "String";                 break;
        case kLTUtilityJsonParser_ValueType_DoNotEnter:    typeString = "DoNotEnter";             break;
        case kLTUtilityJsonParser_ValueType_DoNotExit:     typeString = "DoNotExit";              break;
        default:                                           typeString = "<invalid value type>";   break;
    }
    return typeString;
}

static const char * LTUtilityJsonParserImpl_ParseResultToString(LTUtilityJsonParser_ParseResult parseResult) {
    const char * resultString;
    switch (parseResult) {
        case kLTUtilityJsonParser_ParseResult_ParsingComplete:         resultString = "ParsingComplete";           break;
        case kLTUtilityJsonParser_ParseResult_InvalidCharacter:        resultString = "InvalidCharacter";          break;
        case kLTUtilityJsonParser_ParseResult_InvalidKeyword:          resultString = "InvalidKeyword";            break;
        case kLTUtilityJsonParser_ParseResult_InvalidEscapeSequence:   resultString = "InvalidEscapeSequence";     break;
        case kLTUtilityJsonParser_ParseResult_InvalidUnicodeSequence:  resultString = "InvalidUnicodeSequence";    break;
        case kLTUtilityJsonParser_ParseResult_InvalidNumber:           resultString = "InvalidNumber";             break;
        case kLTUtilityJsonParser_ParseResult_MaxDepthExceeded:        resultString = "MaxDepthExceeded";          break;
        case kLTUtilityJsonParser_ParseResult_UbalancedCollection:     resultString = "UnbalancedCollection";      break;
        case kLTUtilityJsonParser_ParseResult_ExpectedKey:             resultString = "ExpectedKey";               break;
        case kLTUtilityJsonParser_ParseResult_ExpectedColon:           resultString = "ExpectedColon";             break;
        case kLTUtilityJsonParser_ParseResult_OutOfMemory:             resultString = "OutOfMemory";               break;
        case kLTUtilityJsonParser_ParseResult_InternalError:           resultString = "InvalidArguments";          break;
        case kLTUtilityJsonParser_ParseResult_ParserIdle:              resultString = "ParserIdle";                break;
        default:                                                       resultString = "???";                       break;
    }
    return resultString;
}

/*______________________________________
  LTUtilityJsonParserImpl  definition */
define_LTObjectImplPublic(LTUtilityJsonParser, LTUtilityJsonParserImpl,
    GetValue,
    FindArray,
    FindObject,
    ParseJson,
    ValidateJson,
    FindIncludeDirectives,
    FeedJsonChars,
    FeedComplete,
    GetParseStatus,
    GetValueOffsets,
    ValueTypeToString,
    ParseResultToString
);

/*____________________
  LTLibrary binding */
define_LTObjectLibrary(1, NULL, NULL);

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  21-Dec-22   augustus    created
 *  20-Jun-24   augustus    converted to object; added feed mode
 */
