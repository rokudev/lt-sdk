/*******************************************************************************
 * <lt/utility/LTUtilityJsonParser.h>                  Json parser handle and interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYJSONPARSER_H
#define ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYJSONPARSER_H

/**
 * @defgroup ltutility_jsonparser LTUtilityJsonParser
 * @ingroup  ltutility_jsonparer
 * @{
 *
 * @brief blindingly fast json parser that uses
 */

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

/*___________________________________________
  LTUtilityJsonParser forward declarations */
typedef struct LTUtilityJsonParser_Value        LTUtilityJsonParser_Value;
typedef   enum LTUtilityJsonParser_ValueType    LTUtilityJsonParser_ValueType;
typedef struct LTUtilityJsonParser_ParseStatus  LTUtilityJsonParser_ParseStatus;
typedef   enum LTUtilityJsonParser_ParseResult  LTUtilityJsonParser_ParseResult;
typedef struct LTUtilityJsonParser_ValueOffsets LTUtilityJsonParser_ValueOffsets;

/*________________________
  LTUtilityJsonParser typedefs */
typedef bool (LTUtilityJsonParser_ParseCallback)(const char *key, LTUtilityJsonParser_Value *value, void *clientData);
    /**< the callback type for Json values encountered during parsing
      *
      * @param key the key of the value encountered
      * @param value the value encountered, only valid for duration of callback
      * @param clientData user supplied client data
      * @param true to continue parsing, false to abort
      */

typedef bool (LTUtilityJsonParser_FoundIncludeDirectiveCallback)(const char * pIncludeDirective, u32 nStartOffset, u32 nEndOffset, void *clientData);
    /**< the callback type for json include directives found with %FindIncludeDirectives()
      *
      * LTUtilityJsonParser can find include directives which are text elements of the form:
      *   "...": "path_to_json_for_inclusion".  The key of include directives is always "..." and the value
      *   can be any type of url or path or id that is meaningful to the parsing client.  LTUtilityJsonParser reports
      *   to the callback the path, and the start and end offset of the entire include directive from key start quote to value end quote.
      *
      * @param pIncludeDirective the identifier (e.g. path) of the json snippet to include
      * @param nStartOffset the offset in the original jsonText where the json snippet should replace the directive
      * @param nEndOffset the offset of the end of the identifier to be replaced
      * @param true to continue looking for additional directives, false to abort
      * @see   FindIncludeDirectives
      */

/*__________________________
  ILTUtilityJsonParser interface */
typedef_LTObject(LTUtilityJsonParser, 1) {

    void (*GetValue)(LTUtilityJsonParser *parser, const char *jsonText, const char *absoluteKeyName, LTUtilityJsonParser_Value *value);
        /**< finds the value of the key in json text given the key's fully qualified name
          * @param parser the parser instance
          * @param jsonText the jsonText to parse or NULL to initiate parsing in feeder mode
          * @param absoluteKeyName a forward-slash ('/') separated fully qualified path of the key for the value sought
          * @param value The value that will contin the result if found, and a type indicating not found if not found
          * @note  When GetValue returns, *value contains the value, found or not.  if value->type
          *        is kLTUtilityJsonParser_ValueType_String, a string value was found and is pointed to by value->string.
          *        This string pointer is only valid until the parser object is destroyed *or* until the next call to
          *        ParseJson, GetValue or ValidateJson (even if a distinct value struct is used for subsequent calls).
          *
          * <b>Example usage:</b><pre>
          *    static u32 GetFutureSpaHighScore(const char * gameJSON) {
          *        / * a function to retrieve the Future Spa high score out of game JSON * /
          *
          *        LTUtilityJsonParser *parser = lt_createobject(LTUtilityJsonParser);
          *        LTUtilityJsonParser_Value    value = { .integer = 0 };
          *        if (parser) {
          *            parser->API->GetValue(parser, gameJSON, "/games/pinball/future spa/high score", &value);
          *            if (value.type != kLTUtilityJsonParser_ValueType_Integer || value.integer < 0) value.integer = 0;
          *            lt_destroyobject(parser);
          *        }
          *        return (u32)value.integer;
          *    }
          *    </pre>
          */

    u32 (*FindArray)(LTUtilityJsonParser *parser, const char *jsonText, const char *arrayKeyName);
        /**< returns the byte-offset from start of jsonText where a named array is found
          * @param parser the parser instance
          * @param jsonText the jsonText to parse or NULL to initiate parsing in feeder mode
          * @param arrayKeyName a forward-slash ('/') separated fully qualified path of the key for the array sought
          * @return the 0-based character offset from the start of jsonText of the '[' character that starts the array or
          *         zero if no array with requested name is found or if parse error occurs
          * @note  A zero return value may be distinguished between not-found and parse-error by examining the ParseResult obtained from %GetParserStatus
          *        The parse-result of kLTUtilityJsonParser_ParseResult_ParsingComplete means parsing completed without error and the array wasn't found.
          * @note FindArray is useful for:
          *        (a) locating the array and then parsing only the array: ParseJson(parser, jSonText + offset, ...)
          *        (b) retrieving array elements by iteration instead of callback enumeration: <pre>
          *               u32 itemIndex = 0; char stringIndex[12]; LTUtilityJsonParser_Value value;
          *               u32 arrayOffset = parser->API->FindArray(parser, jsonText, "/config/build/libs");
          *               if (arrayOffset) do {  / * found the array, iterate over items by calling GetValue() in succession * /
          *                    lt_u32toString(itemIndex, stringIndex, sizeof(stringIndex) -1);  / * turn numeric into string, more efficient than lt_snprintf * /
          *                    parser->API->GetValue(parser, jsonText + arrayOffset, stringIndex, &value);
          *                    if (value.type == kLTUtilityJsonParser_ValueType_String) { ProcessItem(itemIndex++, value.string); continue; }
          *                  } while (false);
          *               lt_consoleprint("Found %lu items in the array\n", LT_Pu32(itemIndex));
          *               </pre>
          */

    u32 (*FindObject)(LTUtilityJsonParser *parser, const char *jsonText, const char *objectKeyName);
        /**< returns the byte-offset from start of jsonText where a named object is found
          * @param parser the parser instance
          * @param jsonText the jsonText to parse or NULL to initiate parsing in feeder mode
          * @param objectKeyName a forward-slash ('/') separated fully qualified path of the key for the object sought
          * @return the 0-based character offset from the start of jsonText of the '{' character that starts the object
          *         zero if no object with requested name is found or if parse error occurs
          * @note  A zero return value may be distinguished between not-found and parse-error by examining the ParseResult obtained from %GetParserStatus
          *        The parse-result of kLTUtilityJsonParser_ParseResult_ParsingComplete means parsing completed without error and the object wasn't found.
          * @note FindObject is useful for: <pre>
          *        (a) easily parsing a subtree object without having to manually parse for its beginning and manually detect when the end is reached
          *        (b) optimizing performance of calling GetValue() in succession by narrowing the search-space to a specific object: <pre>
          *            LT_INLINE void AssignString(LTUtilityJsonParser_Value *v, char *string, u32 size) {
          *               if (v->type == kLTUtilityJsonParser_ValueType_String) lt_strncpyTerm(string, v->string, size); else *string = 0;
          *            }
          *            LTUtilityJsonParser_Value v; char city[24], state[3], zip[6];
          *            u32 nOffset = parser->API->FindObject(parser, jsonText, "/employee/32/address");
          *            if (nOffset) {
          *               parser->API->GetValue(parser, jsonText + arrayOffset, "city",  &v);  CheckString(&v, city);
          *               parser->API->GetValue(parser, jsonText + arrayOffset, "state", &v);  CheckString(&v, state);
          *               parser->API->GetValue(parser, jsonText + arrayOffset, "zip",   &v);  CheckString(&v, zip);
          *            }
          *       </pre>
          */

    bool (*ParseJson)(LTUtilityJsonParser *parser, const char *jsonText, LTUtilityJsonParser_ParseCallback *parseCallback, void *clientData);
        /**< parses json text using a depth-first pre-order tree traversal
          *
          * @param parser the parser instance
          * @param jsonText the jsonText to parse or NULL to initiate parsing in feeder mode
          * @param parseCallback the callback that will be called back with json values encountered
          * @param clientData user client data passed back to the parseCallback
          * @return whether or not the parsing continued through completion successfully
          *
          * <b> Example: Using %ParseJson() to getting the Future Spa game json high score</b><pre>
          *
          * / * struct GetValueParseClientData and GetValueParseCallback forward declaration * /
          * struct GetValueParseClientData { const char ** keys, u32 index, LTUtilityJsonParser_Value value };
          * static bool GetValueParseCallback(const char *key, LTUtilityJsonParser_Value *value, void *clientData);
          *
          * / * Use ParseJson() to find the value of "/games/pinball/future spa/high score" * /
          * static u32 ParseForFutureSpaHighScore(const char *gameJSON) {
          *     struct GetValueParseClientData cd = {
          *         .keys = { "games", "pinball", "future spa", "high score", NULL },
          *         .index = 0, .value = { .integer = 0 }
          *     };
          *     LTUtilityJsonParser *parser = lt_createobject(LTUtilityJsonParser);
          *     if (parser) {
          *         parser->API->ParseJson(parser, gameJSON, GetValueParseCallback, &cd);
          *         lt_destroyobject(parser);
          *     }
          *     return (u32)cd.value.integer;
          * }
          *
          * / * GetValueParseCallback callback function for example implementation of GetValue using ParseJson  * /
          * static bool GetValueParseCallback(const char *key, LTUtilityJsonParser_Value *value, void *clientData) {
          *     struct GetValueParseClientData *cd = (struct GetValueParseClientData *)clientData;
          *     bool lookingForFinalKey = (cd->keys[index+1] == NULL);
          *     bool matchedThisKey     = (0 == lt_strcmp(key, cd->keys[cd->index]));
          *
          *     if (lookingForFinalKey) {
          *         if (matchedThisKey) {
          *             if (value.type == kLTUtilityJsonParser_ValueType_Integer && value.integer > 0) cd->highScore = (u32)value.integer;
          *             return false; / * abort parsing - we found the final key
          *         }
          *         / * still looking for the final key, instruct the parser not to enter/descend an object or array
          *            since we know the key we are looking for is at this level * /
          *         LTUtilityJsonParser_Value_DoNotEnter(value);
          *         return true; / * keep parsing * /
          *     }
          *     / * we are at an intermediate level, descend to the next level if we match keys * /
          *     if (matchedThisKey) {
          *         / * we matched but is it descendable? only if value type is an object * /
          *         if (value.type == kLTUtilityJsonParser_ValueType_Object) {
          *             LTUtilityJsonParser_Value_DoNotExit(value); / * we will enter this object, but won't exit, because we know key is inside this object somewhere * /
          *             cd->index++; / * now we get to look for the next key part* /
          *             return true; / * continue parsing * /
          *         }
          *         return false; / * matched the key but the value is the wrong type (not object so not descendable), abort parsing * /
          *     }
          *     / * this key is not a match * /
          *     LTUtilityJsonParser_Value_DoNotEnter(value); / * don't descend into an object (or array) that didn't match * /
          *     return true; / * keep looking at this level for a match * /
          * }
          *
          * </pre>
          */

    bool (*ValidateJson)(LTUtilityJsonParser *parser, const char *jsonText);
        /**< determines whether or not json is valid
          *
          * @param parser the parser instance
          * @param jsonText the jsonText to validate or NULL to initiate validation in feeder mode
          * @return true when jsonText is non-NULL and valid json, false when jsonText is non-NULL and invalid json, and
          *         and true when jsonText is NULL (feeder mode entered)
          * @note When this function returns false (jsonText is non-NULL and is invalid json), call %GetParseStatus()
          *       to obtain more information about why/how the validation failed.
          *       If jsonText is NULL (feeder mode) then %GetParseStatus() should be called:
          *         a. whenever any call to FeedParserJsonChars() returns false, or
          *         b. after FeedComplete() has been called.
          */

    bool (*FindIncludeDirectives)(LTUtilityJsonParser *parser, const char *jsonText, LTUtilityJsonParser_FoundIncludeDirectiveCallback *foundIncludeDirectiveCallback, void *clientData);
        /**< finds json include directives of the form: (a) "...": "include_path_directive" or (b) "...:include_path_directive" and reports include_path_directive and replacement offsets
         *
         * FindIncludeDirectives() parses jsonText and calls back the foundIncludeDirectiveCallback for each directive found, delivering
         * the path_spec/url/id of the json snipped to be included and the start and end offsets of the text that the snippet should replace.
         *
         * The typical use case for %FindIncludeDirectives() is to build a list of paths and offsets to then be used to reconstruct jsonText with
         * the json snippets replacing their directives, before parsing with parseJson() or GetValue()
         *
         * @param parser the parser instance
         * @param jsonText the jsonText to find include directives in or NULL to initiate finding in feeder mode
         * @param foundIncludeDirectiveCallback the callback that will receive the found include directive information
         * @param clientData user client data passed back to the findCallback
         * @note  Typical use case for FindIncludeDirectives() is to build a list of includes with replacement offsets, then use the list to reconstruct jsonText
         *        with the included snippets replacing the directives and then parse the result
         */

    bool (*FeedJsonChars)(LTUtilityJsonParser *parser, const char * pJsonChars, u32 numChars);
        /**< feeds characters into the json parser when in feeder mode
          * @param parser the parser instance
          * @param pJsonChars the json characters to feed into the parser
          * @param numChars the number of characters starting at pJsonChars to feed into the parser
          * @return true if parse will accept more characters, false if it will not (because an error occurred or key value was found)
          * @note Feeder mode is entered whenever NULL jsonText was specified to GetValue, ParseJson or ValidateJson
          *       FeedJsonChars should be called repeatedly until:
          *          a. You don't want to
          *          b  It returns false
          *       FeedParserJsonChars returns false to indicate it will not accept being fed more characters because:
          *          a. Parsing has not been initiated
          *          b. The parser is not in feeder mode
          *          c. A parse error occurred while feeding and the parser has aborted
          *          d. The value sought by your call to get value was found, or determined unfindable, and the parser has stopped.
          *          e. You called FeedComplete, signifying the json input was complete; the parser was finalized and stopped.
          *
          *       GetParseStatus may be called to determine the status of the parser.
          *
          *       FeedComplete does not need to be called when FeedJsonChars returns false as parsing has aborted.
          *       While FeedParserJsonChars returns true, continue calling it when you have characters to feed until all characters
          *       have been fed into the parser successfully.  Then call FeedComplete to finalize the parsing,
          *       and then GetParseStatus may be called depending on the return value from FeedComplete.
          * @see FeedComplete
          */

    bool (*FeedComplete)(LTUtilityJsonParser *parser);
        /**< finalizes parsing when in feeder mode
          * @param parser the parser instance
          * @return true if final parsing status after feeding was successful, false if there was a parsing error
          * @note when in feeder mode %FeedComplete must be called to determine final parsing status after all jsonText
          *       characters have been fed with (possibly multiple) calls
          *       to FeedJsonChars, and each and every one of those calls returned true.
          * @see FeedJsonChars
          */

    void (*GetParseStatus)(LTUtilityJsonParser *parser, LTUtilityJsonParser_ParseStatus *parseStatus);
        /**< gives extended information about unfound values, validation errors, and parsing errors
          *
          * Call %GetParseStatus() to get the parser status after calling status of the parser.
          * %ParseJson(), %ValidateJson, %GetValue, %FindArray(), FeedJsonChars() or FeedComplete().
          *   If
          * supplied to .  the ending result status of the most recent call to %ValidateJson()
          * %ParseJson(),  %GetValue(), or find subtree when not in feeder mode.
          * When in feeder mode (NULL jsonText fed in to GetValue, ParseJson, ValidateJson, or FindSubtreeOffset),
          * call %GetParseStatus() to get the parser status after calling FeedJsonChars() or FeedComplete().
          * When the parser status indicates a parse failure, the line number and character offset where the
          * error occurred is given in the parser status.
          *
          * @param parseStatus a pointer to an LTUtilityJsonParser_ParseStatus struct that will receive the parse status
          * @see ParseJson
          * @see ValidateJson
          * @see GetValue
          * @see FeedJsonChars
          * @see FeedComplete
          * @see ParseResultToString
          */

    void (*GetValueOffsets)(LTUtilityJsonParser *parser, LTUtilityJsonParser_Value *value, LTUtilityJsonParser_ValueOffsets *offsetsToSet);
        /**< gets the 0 based byte offset of certain json elements relative to the beginning of the parser run
         *
         * %GetValueOffsets supplies 0 based byte offsets for the location of elements relative to the start of the current parsing run.
         * It supplies offsets when called (a) during a parsing run from within a parse callback, using the value passed into the callback and (b) after
         * a value has been found with GetValue(), before another parsing operation is initiated.
         *
         * %GetValueOffsets provides 0 based byte offsets representing positions relative to the start of the current parsing run.
         * Offsets are provided for:
         *    string values (4): for the start and end quote char delimiters of both the value and its key, i.e., {"foo":"bar"} would give 1,5,7,11 for "/foo"
         *    object values (3): for the start curly brace '{' and the start and end quote char delimiters of its key, e.g. {"baz":{"foo":"bar"}} would give 1,5,7 for "/baz"
         *    array  values (3): for the start square bracket: '[' and the start and end quote char delimiters of its key, e.g. {"baz":["foo":"bar"]} would give 1,5,7 for "/baz"
         *
         * @param parser the parser instance
         * @param value the value to get the offsets for
         * @param offsetsToSet pointer to the struct that will receive the offsets
         *
         * Use cases:
         *  1. string values: for easy element substitution, e.g. replace  "...":"$(LT_PRODUCT_ROOT)/build/product/common/CommonProductConfig.json" with contents of file
         *  2. object values: locate subtree offset to accelerate multiple GetValue() retrievals by starting jsonText at the subtree of interest
         *  3. object values: locate subtree before calling ParseJson to parse discrete subtree without having to manually parse to arrive there and then manually detect when to stop
         *  4. array  values: enable easy and subroutine iteration over arrays by locating array and looping with GetValue() calls using incrementing keynames - "0", "1", "2", etc.  until valueNotFound
         *
         * @note zero offsets are given for keys of values that are array elements and consequently have an implicit key (their index number in the array):
         *        e.g. [{"name":"Don","cities":["London","Paris","Munich","Kabul"]},{"name":"Lenny","cities":["Toronto","Ontario","Summer Beaver"]}] would give 0,0,87,95 for "/1/cities/0" (quotes of "Toronto")
         *
         * @note When json 5 unquoted key names are supported, key names that are in quotes will still have offsets that point to the quote characters
         *       and unquoted key names will have offsets that point to the first and last chars of the key name,
         *       e.g. {foo:"bar"} would give 1,3,5,9 and {"foo":"bar"} would remain 1,5,7,11 for "/foo"
         *
         * @note For any value->type other than String, Array or Object, all offsets will be set to 0.
         *
         * @see LTUtilityJsonParser_ValueOffsets, LTUtilityJsonParser_ParseCallback, GetValue
         */

    const char *(*ValueTypeToString)(LTUtilityJsonParser_ValueType valueType);
        /**< converts a value type to a printable string
          *
          * Call %ValueTypeToString() to convert an LTUtilityJsonParser_ValueType to
          * a printable string.  Useful for debugging json parser callback invocations.
          *
          * @param valueType the value type to convert to a printable string
          * @return the printable string identifying the value type
          * @note when finished debugging your parser callback, #if 0 all of your
          * debugging code to preserve smallness of build.
          *
          */

    const char *(*ParseResultToString)(LTUtilityJsonParser_ParseResult parseResult);
        /**< converts a parse result type to a printable string
          *
          * Call %ParseResultToString() to convert an LTUtilityJsonParser_ParseResult to
          * a printable string.  Useful for reporting parsing failures
          *
          * @param parseResult the parse result to convert to a printable string
          * @return the printable string identifying the parse result
          *
          */

} LTLIBRARY_INTERFACE;

/*_____________________________________
  LTUtilityJsonParser_ValueType enumeration */
typedef enum LTUtilityJsonParser_ValueType {
    /* IMPORTANT: do not reorder or renumber, for performance these match an internal state enum; any change will break the system */
    kLTUtilityJsonParser_ValueType_KeyNotFound =   0,                                     /**< key searched with %GetValue() not found */
    kLTUtilityJsonParser_ValueType_Array       =   1,                                     /**< JSON array   value */
    kLTUtilityJsonParser_ValueType_ArrayEntry  =   kLTUtilityJsonParser_ValueType_Array,  /**< JSON array  entry (descent) during parse */
    kLTUtilityJsonParser_ValueType_ArrayExit   =   2,                                     /**< JSON array   exit (ascent)  during parse */
    kLTUtilityJsonParser_ValueType_Object      =   3,                                     /**< JSON object  value */
    kLTUtilityJsonParser_ValueType_ObjectEntry =   kLTUtilityJsonParser_ValueType_Object, /**< JSON object entry (descent) during parse */
    kLTUtilityJsonParser_ValueType_ObjectExit  =   4,                                     /**< JSON object  exit (ascent)  during parse */
    kLTUtilityJsonParser_ValueType_Integer     =   5,                                     /**< JSON Integer value */
    kLTUtilityJsonParser_ValueType_Real        =   6,                                     /**< JSON float   value */
    kLTUtilityJsonParser_ValueType_null        =   7,                                     /**< JSON null    value */
    kLTUtilityJsonParser_ValueType_True        =   8,                                     /**< JSON True    value */
    kLTUtilityJsonParser_ValueType_False       =   9,                                     /**< JSON False   value */
    kLTUtilityJsonParser_ValueType_String      =  10,                                     /**< JSON String  value */

    kLTUtilityJsonParser_ValueType_DoNotEnter  = 254,                                     /**< instructs parser to skip over object or array  */
    kLTUtilityJsonParser_ValueType_DoNotExit   = 255                                      /**< instructs parser to end parsing when exiting object or array */
} LTUtilityJsonParser_ValueType;

/*____________________________
  LTUtilityJsonParser_Value struct */
typedef struct LTUtilityJsonParser_Value {
    union {
        s64          integer;  /**< the integer value when type is kLTUtilityJsonParser_ValueType_Integer */
        double       real;     /**< the floating point value when type is kLTUtilityJsonParser_ValueType_Real */
        const char * string;   /**< the string value when type is kLTUtilityJsonParser_ValueType_String */
    };
    LTUtilityJsonParser_ValueType  type;     /**< the LTUtilityJsonParser_ValueType of the value */
} LTUtilityJsonParser_Value;

/*_____________________________________________
  LTUtilityJsonParser_Value inline helper functions */
LT_INLINE bool LTUtilityJsonParser_Value_Found(LTUtilityJsonParser_Value * value)          { return value->type != kLTUtilityJsonParser_ValueType_KeyNotFound;  }   /**< indicates the value requested by GetValue() was found */
LT_INLINE bool LTUtilityJsonParser_Value_NotFound(LTUtilityJsonParser_Value * value)       { return value->type == kLTUtilityJsonParser_ValueType_KeyNotFound;  }   /**< indicates the value requested by GetValue() was not found */

LT_INLINE bool LTUtilityJsonParser_Value_IsArray(LTUtilityJsonParser_Value * value)        { return value->type == kLTUtilityJsonParser_ValueType_Array;        }   /**< indicates the value requested by GetValue() was found and is an array */
LT_INLINE bool LTUtilityJsonParser_Value_IsArrayEntry(LTUtilityJsonParser_Value * value)   { return value->type == kLTUtilityJsonParser_ValueType_ArrayEntry;   }   /**< indicates during parse callback that an array is being entered */
LT_INLINE bool LTUtilityJsonParser_Value_IsArrayExit(LTUtilityJsonParser_Value * value)    { return value->type == kLTUtilityJsonParser_ValueType_ArrayExit;    }   /**< indicates during parse callback that an array is being exited */
LT_INLINE bool LTUtilityJsonParser_Value_IsObject(LTUtilityJsonParser_Value * value)       { return value->type == kLTUtilityJsonParser_ValueType_Object;       }   /**< indicates the value requested by GetValue() was found and is an object */
LT_INLINE bool LTUtilityJsonParser_Value_IsObjectEntry(LTUtilityJsonParser_Value * value)  { return value->type == kLTUtilityJsonParser_ValueType_ObjectEntry;  }   /**< indicates during parse callback that an object is being entered */
LT_INLINE bool LTUtilityJsonParser_Value_IsObjectExit(LTUtilityJsonParser_Value * value)   { return value->type == kLTUtilityJsonParser_ValueType_ObjectExit;   }   /**< indicates during parse callback that an object is being exited */
LT_INLINE bool LTUtilityJsonParser_Value_IsInteger(LTUtilityJsonParser_Value * value)      { return value->type == kLTUtilityJsonParser_ValueType_Integer;      }   /**< indicates an integer value encountered in parse callback or found by GetValue() */
LT_INLINE bool LTUtilityJsonParser_Value_IsReal(LTUtilityJsonParser_Value * value)         { return value->type == kLTUtilityJsonParser_ValueType_Real;         }   /**< indicates a floating point value encountered in parse callback or found by GetValue()  */
LT_INLINE bool LTUtilityJsonParser_Value_IsNull(LTUtilityJsonParser_Value * value)         { return value->type == kLTUtilityJsonParser_ValueType_null;         }   /**< indicates an null value encountered in parse callback or found by GetValue() */
LT_INLINE bool LTUtilityJsonParser_Value_IsTrue(LTUtilityJsonParser_Value * value)         { return value->type == kLTUtilityJsonParser_ValueType_True;         }   /**< indicates an true value encountered in parse callback or found by GetValue() */
LT_INLINE bool LTUtilityJsonParser_Value_IsFalse(LTUtilityJsonParser_Value * value)        { return value->type == kLTUtilityJsonParser_ValueType_False;        }   /**< indicates an false value encountered in parse callback or found by GetValue() */
LT_INLINE bool LTUtilityJsonParser_Value_IsString(LTUtilityJsonParser_Value * value)       { return value->type == kLTUtilityJsonParser_ValueType_String;       }   /**< indicates an string value encountered in parse callback or found by GetValue() */

LT_INLINE void LTUtilityJsonParser_Value_DoNotEnter(LTUtilityJsonParser_Value * value)     { value->type = kLTUtilityJsonParser_ValueType_DoNotEnter;           }           /**< when called on value passed into parse callback of type ObjectEntry or ArrayEntry,
                                                                                                                                                                                 instructs parser to skip over object or array */
LT_INLINE void LTUtilityJsonParser_Value_DoNotExit(LTUtilityJsonParser_Value * value)      { value->type = kLTUtilityJsonParser_ValueType_DoNotExit;            }           /**< when called on value passed into parse callback of type ObjectEntry or ArrayEntry,
                                                                                                                                                                                 instructs parser to stop parsing when object or array parsing is complete. */
LT_EXTERN_C_END

/*_____________________________________
  LTUtilityJsonParser_Result enumeration */
typedef enum LTUtilityJsonParser_ParseResult {
    /* IMPORTANT: do not reorder or renumber, for performance these match an internal state enum; any change will break the system */
    kLTUtilityJsonParser_ParseResult_ParsingComplete           =   0, /**< parsing has continued to completion without error       */

    kLTUtilityJsonParser_ParseResult_InvalidCharacter          =   1, /**< an invalid character was encountered                    */
    kLTUtilityJsonParser_ParseResult_InvalidKeyword            =   2, /**< an invalid keyword was encountered                      */
    kLTUtilityJsonParser_ParseResult_InvalidEscapeSequence     =   3, /**< an invalid escape sequence was encountered              */
    kLTUtilityJsonParser_ParseResult_InvalidUnicodeSequence    =   4, /**< an invalid unicode sequence was encountered             */
    kLTUtilityJsonParser_ParseResult_InvalidNumber             =   5, /**< an invalid number was encountered                       */
    kLTUtilityJsonParser_ParseResult_MaxDepthExceeded          =   6, /**< Json tree was too deep to parse                         */
    kLTUtilityJsonParser_ParseResult_UbalancedCollection       =   7, /**< a brace mismatch of object or array was encountered     */
    kLTUtilityJsonParser_ParseResult_ExpectedKey               =   8, /**< a key was expected to be found and was not              */
    kLTUtilityJsonParser_ParseResult_ExpectedColon             =   9, /**< a colon was expected to be found and was not            */
    kLTUtilityJsonParser_ParseResult_OutOfMemory               =  10, /**< system low on memory; parse aborted incomplete          */
    kLTUtilityJsonParser_ParseResult_InternalError             =  99, /**< parser state inconsistent                               */

    kLTUtilityJsonParser_ParseResult_ReadyForFeeding           = 200, /**< parser is ready to be fed input characters              */
    kLTUtilityJsonParser_ParseResult_ParserIdle                = 255,  /**< the parser has not yet been engaged                    */

    kLTUtilityJsonParser_ParseResult_FirstError                = kLTUtilityJsonParser_ParseResult_InvalidCharacter, /**< error range start */
    kLTUtilityJsonParser_ParseResult_LastError                 = kLTUtilityJsonParser_ParseResult_InternalError     /**< error range end */

} LTUtilityJsonParser_ParseResult;

typedef struct LTUtilityJsonParser_ParseStatus {
    u32 linePos;                                              /**< the line number of the line where the parsing error occurred         */
    u32 charPosInLine;                                        /**< the character position on the line where the parsing error occurred  */
    LTUtilityJsonParser_ParseResult parseResult;              /**< the result of the last initiated parse or validation run             */
} LTUtilityJsonParser_ParseStatus;
    /**< represents the outcome status of the last parse or validation run */

typedef struct LTUtilityJsonParser_ValueOffsets {
    u32 keyOffsetStart;                                       /**< offset of opening quote of quoted key string; offset of first letter of non-quoted key string */
    u32 keyOffsetEnd;                                         /**< offset of  ending quote of quoted key strings offset of  last letter of non-quoted key string */
    u32 valueOffsetStart;                                     /**< offset of a string value's opening quote, or offset of object or array's opening { or [ character */
    u32 valueOffsetEnd;                                       /**< offset of a string value's  ending quote, or offset of object or array's closing { or [ character */
} LTUtilityJsonParser_ValueOffsets;
    /**< used to retrieve the byte offsets of the start and end character positions of a key and its values
     * %LTUtilityJsonParser_ValueOffsets is the struct type passed into %GetValueOffsets
     */

LT_INLINE bool LTUtilityJsonParser_ParseStatus_IsErrorStatus(LTUtilityJsonParser_ParseStatus * status)  { return status->parseResult >= kLTUtilityJsonParser_ParseResult_FirstError && status->parseResult <= kLTUtilityJsonParser_ParseResult_LastError; }
  /**< tests whether or not the parse status is a parser error status */

#endif /* #ifndef ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYJSONPARSER_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  20-Dec-22   augustus    created
 */
