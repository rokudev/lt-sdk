# LTUtilityJsonParser: Benefits and Differentiators

## Overview

`LTUtilityJsonParser` is LT OS's single-pass, zero-DOM, streaming JSON parser. It provides direct path-based value lookup, full-document event-driven traversal, subtree offset navigation, streaming feeder-mode parsing of chunked input, JSON include-directive resolution, and syntax validation — all without building a heap-allocated tree, without external dependencies, and with identical behavior across every LT platform.

---

## Key Differentiators

### Zero-DOM, Constant Heap Overhead

Most embedded and desktop JSON libraries — cJSON, jansson, json-c on Linux; cJSON or ArduinoJSON ports on FreeRTOS — build a complete DOM tree from the input: every key, value, and container becomes a heap-allocated node. Parsing a 10 KB JSON document can allocate dozens or hundreds of nodes, each requiring a separate `malloc`, and the whole structure must be freed afterward.

`LTUtilityJsonParser` builds **no DOM**. Its heap footprint is a single fixed-size `JSON_parser` state machine plus a small key copy buffer (reused across calls). Parsing a 10 MB JSON document costs the same heap as parsing a 10-byte one. Strings are returned as pointers directly into the original input buffer — no copy, no allocation.

**FreeRTOS:** The most common alternative is JSMN, a tokenizer that returns a flat array of position tokens with no types, no values, and no path support. Path lookup requires the application to implement its own tree walk. ArduinoJSON builds a DOM with a fixed-size allocator the application must size in advance.

**Linux:** `cJSON_Parse` / `json_object_new_*` allocate a complete object graph. Freeing it requires `cJSON_Delete` / `json_object_put`. There is no path-based single-value retrieval; applications must walk the tree manually.

### Direct Path-Based Value Retrieval — `GetValue`

`GetValue` takes a `/`-separated absolute key path like `"/games/pinball/future spa/high score"` and returns a fully typed `LTUtilityJsonParser_Value` — integer (`s64`), real (`double`), string (pointer into source), boolean, null, object, or array — in a single call. No DOM, no intermediate allocation, no application-written tree walk required.

The value struct union carries the native C type: `value.integer` for integers, `value.real` for floats, `value.string` for strings. A set of `LTUtilityJsonParser_Value_Is*()` inline helpers makes type dispatch one line of code.

### Parser-Guided Traversal — `DoNotEnter` / `DoNotExit`

During `ParseJson` callbacks, the callback can call `LTUtilityJsonParser_Value_DoNotEnter(value)` on any `ObjectEntry` or `ArrayEntry` value to instruct the parser to skip the entire subtree without visiting it. This is **not** the application filtering results after the fact — it is the parser literally not entering the subtree, saving all the character scanning work inside it. Similarly, `DoNotExit` tells the parser to stop when the current container closes, eliminating unnecessary scanning of the rest of the document.

No mainstream embedded JSON library exposes this level of traversal control. JSMN tokenizes the entire document regardless. cJSON parses the entire input before the application can begin filtering.

### Subtree Navigation with Byte Offsets — `FindArray` / `FindObject` / `GetValueOffsets`

`FindArray` and `FindObject` return the byte offset of a named array `[` or object `{` within the source text. Passing `jsonText + offset` to a subsequent `GetValue` or `ParseJson` call narrows parsing to that subtree alone, with no re-scanning of the prefix. `GetValueOffsets` reports the opening and closing quote byte positions of both the key and value for String, Object, and Array values — enabling precise in-place text substitution without reserializing the document.

This is directly useful for multi-field extraction from a large object (locate it once with `FindObject`, then call `GetValue` multiple times on the narrow subtree), and for implementing JSON template expansion (locate string values, splice in replacements at their exact offsets).

### Streaming Feeder Mode — `FeedJsonChars` / `FeedComplete`

Passing `NULL` as `jsonText` to `GetValue`, `ParseJson`, or `ValidateJson` enters feeder mode. The application then calls `FeedJsonChars(parser, chunk, len)` as each buffer of bytes arrives — from a network socket, a flash read, or any other incremental source — and the parser processes each chunk incrementally. `FeedComplete()` finalizes parsing when the last byte has been fed.

**FreeRTOS / Linux streaming:** JSMN requires the complete input in memory before tokenizing. cJSON requires a complete null-terminated string. Implementing incremental JSON parsing from a socket on either platform requires the application to accumulate the full response before parsing can begin, using RAM proportional to the response size.

### Include Directive Resolution — `FindIncludeDirectives`

`FindIncludeDirectives` scans a JSON document for entries of the form `"...": "path/to/include"` or values beginning with `"...:path"` and delivers the path and its exact start/end byte offsets to a callback. The typical use is to collect all include paths, fetch each referenced snippet, and splice them into the source text at the given offsets before a final parse. This enables a JSON composition / template-inclusion pattern with no application-written scanning code.

Neither Linux JSON libraries nor FreeRTOS alternatives provide any include-directive facility.

### Precise Error Reporting — Line and Column on Failure

`GetParseStatus` returns the `linePos` and `charPosInLine` of the first syntax error, along with a `ParseResult` enum identifying the error category (invalid character, invalid escape, unbalanced collection, etc.). `ParseResultToString` converts any result code to a human-readable string for logging. This makes diagnosing malformed JSON from remote endpoints or configuration files practical on a device with no interactive debugger.

---

## Usage Models with Example Code

### 1. `GetValue` — Direct Path-Based Single Value Retrieval

```c
LTUtilityJsonParser *parser = lt_createobject(LTUtilityJsonParser);
LTUtilityJsonParser_Value value;

/* Retrieve a deeply nested integer */
parser->API->GetValue(parser, jsonText, "/games/pinball/future spa/high score", &value);
if (LTUtilityJsonParser_Value_IsInteger(&value) && value.integer > 0) {
    lt_consoleprint("High score: %ld\n", LT_Ps64(value.integer));
}

/* Retrieve a string — pointer valid until next parser call or destroy */
parser->API->GetValue(parser, jsonText, "/config/server/host", &value);
if (LTUtilityJsonParser_Value_IsString(&value)) {
    lt_strncpyTerm(hostBuf, value.string, sizeof(hostBuf));
}

/* Retrieve a boolean */
parser->API->GetValue(parser, jsonText, "/config/tls/enabled", &value);
bool tlsEnabled = LTUtilityJsonParser_Value_IsTrue(&value);

/* Check not-found explicitly */
if (LTUtilityJsonParser_Value_NotFound(&value)) { /* key absent */ }

lt_destroyobject(parser);
```

### 2. `FindObject` + Multiple `GetValue` Calls — Narrow the Search Space

```c
/* Locate an object subtree once, then extract multiple fields efficiently */
u32 offset = parser->API->FindObject(parser, jsonText, "/employee/32/address");
if (offset) {
    const char *subtree = jsonText + offset;
    LTUtilityJsonParser_Value v;

    char city[32], state[3], zip[6];
    parser->API->GetValue(parser, subtree, "city",  &v);
    if (LTUtilityJsonParser_Value_IsString(&v)) lt_strncpyTerm(city,  v.string, sizeof(city));
    parser->API->GetValue(parser, subtree, "state", &v);
    if (LTUtilityJsonParser_Value_IsString(&v)) lt_strncpyTerm(state, v.string, sizeof(state));
    parser->API->GetValue(parser, subtree, "zip",   &v);
    if (LTUtilityJsonParser_Value_IsString(&v)) lt_strncpyTerm(zip,   v.string, sizeof(zip));
}
```

### 3. `FindArray` + Indexed Iteration — Walk an Array Without a Callback

```c
/* Iterate array items by synthetic index key: "0", "1", "2", ... */
char indexKey[12];
LTUtilityJsonParser_Value value;
u32 arrayOffset = parser->API->FindArray(parser, jsonText, "/config/build/libs");
if (arrayOffset) {
    for (u32 i = 0; ; i++) {
        lt_u32toString(i, indexKey, sizeof(indexKey) - 1);
        parser->API->GetValue(parser, jsonText + arrayOffset, indexKey, &value);
        if (LTUtilityJsonParser_Value_NotFound(&value)) break;
        if (LTUtilityJsonParser_Value_IsString(&value)) ProcessLib(i, value.string);
    }
}
```

### 4. `ParseJson` with `DoNotEnter` / `DoNotExit` — Efficient Filtered Traversal

```c
/* Collect all string values under "/net/" while skipping all other top-level keys */
static bool OnNetValue(const char *key, LTUtilityJsonParser_Value *value, void *cd) {
    if (LTUtilityJsonParser_Value_IsObjectEntry(value)) {
        if (key && lt_strcmp(key, "net") == 0)
            LTUtilityJsonParser_Value_DoNotExit(value);  /* descend and stop at exit */
        else
            LTUtilityJsonParser_Value_DoNotEnter(value); /* skip everything else */
        return true;
    }
    if (LTUtilityJsonParser_Value_IsString(value) && key) {
        StoreNetSetting(key, value->string);
        return true;
    }
    return true;
}

parser->API->ParseJson(parser, jsonText, OnNetValue, NULL);
```

### 5. `ValidateJson` + `GetParseStatus` — Error Reporting

```c
if (!parser->API->ValidateJson(parser, jsonText)) {
    LTUtilityJsonParser_ParseStatus status;
    parser->API->GetParseStatus(parser, &status);
    lt_consoleprint("JSON error: %s at line %lu col %lu\n",
        parser->API->ParseResultToString(status.parseResult),
        LT_Pu32(status.linePos),
        LT_Pu32(status.charPosInLine));
}
```

### 6. Feeder Mode — Streaming Incremental Parse from a Socket

```c
/* Enter feeder mode by passing NULL jsonText */
parser->API->ParseJson(parser, NULL, OnValue, clientData);

/* Feed chunks as they arrive from a network read loop */
const char *chunk;
u32 chunkLen;
while (GetNextChunk(&chunk, &chunkLen)) {
    if (!parser->API->FeedJsonChars(parser, chunk, chunkLen)) break;
}

/* Finalize — returns true if all fed input was valid JSON */
bool ok = parser->API->FeedComplete(parser);
if (!ok) {
    LTUtilityJsonParser_ParseStatus status;
    parser->API->GetParseStatus(parser, &status);
    lt_consoleprint("Feed error: %s\n", parser->API->ParseResultToString(status.parseResult));
}
```

### 7. `GetValueOffsets` — Byte-Level Offset Reporting for Text Substitution

```c
/* Find a string value and determine its exact character positions */
LTUtilityJsonParser_Value value;
LTUtilityJsonParser_ValueOffsets offsets;

parser->API->GetValue(parser, jsonText, "/config/template/rootPath", &value);
if (LTUtilityJsonParser_Value_IsString(&value)) {
    parser->API->GetValueOffsets(parser, &value, &offsets);
    /* offsets.valueOffsetStart == byte index of opening quote of the string value */
    /* offsets.valueOffsetEnd   == byte index of closing quote of the string value */
    SpliceReplacement(jsonText, offsets.valueOffsetStart, offsets.valueOffsetEnd, resolvedPath);
}
```

### 8. `FindIncludeDirectives` — JSON Composition

```c
typedef struct { const char *path; u32 start; u32 end; } IncludeEntry;
typedef struct { IncludeEntry entries[8]; u32 count; } IncludeList;

static bool OnInclude(const char *directive, u32 start, u32 end, void *cd) {
    IncludeList *list = (IncludeList *)cd;
    if (list->count < 8) {
        list->entries[list->count++] = (IncludeEntry){ directive, start, end };
    }
    return true;
}

IncludeList includes = { 0 };
parser->API->FindIncludeDirectives(parser, jsonText, OnInclude, &includes);
for (u32 i = 0; i < includes.count; i++) {
    const char *snippet = LoadJsonSnippet(includes.entries[i].path);
    SpliceReplacement(jsonText, includes.entries[i].start,
                      includes.entries[i].end, snippet);
}
/* Then parse the fully composed document */
parser->API->ParseJson(parser, composedJson, OnValue, NULL);
```

---

## Design Considerations

String values returned from `GetValue` and parse callbacks are pointers into the internal key buffer or directly into the source text; they are valid only until the next call to `ParseJson`, `GetValue`, `ValidateJson`, or `FindArray`/`FindObject`, or until the parser object is destroyed. Callers that need to retain a string past the next parser call must copy it with `lt_strncpyTerm`. The parser object itself is not thread-safe; each concurrent parsing context requires its own `lt_createobject(LTUtilityJsonParser)` instance. `GetValueOffsets` returns meaningful offsets only for `String`, `Object`, and `Array` value types; all other types return zero offsets.
