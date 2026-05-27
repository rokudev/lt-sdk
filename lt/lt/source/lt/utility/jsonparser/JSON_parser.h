/* See JSON_parser.c for copyright information and licensing. */

#ifndef JSON_PARSER_H
#define JSON_PARSER_H

typedef s64 JSON_int_t;

/* JSON_parser.h */
typedef enum
{
    JSON_E_NONE = 0,
    JSON_E_INVALID_CHAR,
    JSON_E_INVALID_KEYWORD,
    JSON_E_INVALID_ESCAPE_SEQUENCE,
    JSON_E_INVALID_UNICODE_SEQUENCE,
    JSON_E_INVALID_NUMBER,
    JSON_E_NESTING_DEPTH_REACHED,
    JSON_E_UNBALANCED_COLLECTION,
    JSON_E_EXPECTED_KEY,
    JSON_E_EXPECTED_COLON,
    JSON_E_OUT_OF_MEMORY
} JSON_error;

typedef enum
{
    JSON_T_NONE = 0,
    JSON_T_ARRAY_BEGIN,
    JSON_T_ARRAY_END,
    JSON_T_OBJECT_BEGIN,
    JSON_T_OBJECT_END,
    JSON_T_INTEGER,
    JSON_T_FLOAT,
    JSON_T_NULL,
    JSON_T_TRUE,
    JSON_T_FALSE,
    JSON_T_STRING,
    JSON_T_KEY,
    JSON_T_MAX
} JSON_type;

typedef struct JSON_value_struct {
    union {
        JSON_int_t integer_value;

        double float_value;

        struct {
            const char* value;
            LT_SIZE length;
        } str;
    } vu;

    u32 elementStartOffset;  /**< parse offset of element start where forType:pointsTo - key|stringVal:opening quote, objectBegin:'{', arrayBegin:'[', all_other:0 */
    u32 elementEndOffset;    /**< parse offset of element end   where forType:pointsTo - key|stringVal:closing quote,   objectEnd:'}',   arrayEnd:']', all_other:0 */

} JSON_value;

typedef struct JSON_parser_struct* JSON_parser;

/*! \brief JSON parser callback

    \param ctx The pointer passed to new_JSON_parser.
    \param type An element of JSON_type but not JSON_T_NONE.
    \param value A representation of the parsed value. This parameter is NULL for
        JSON_T_ARRAY_BEGIN, JSON_T_ARRAY_END, JSON_T_OBJECT_BEGIN, JSON_T_OBJECT_END,
        JSON_T_NULL, JSON_T_TRUE, and JSON_T_FALSE. String values are always returned
        as zero-terminated C strings.

    \return Non-zero if parsing should continue, else zero.
*/
typedef int (*JSON_parser_callback)(void* ctx, int type, const JSON_value* value);

/**
   A typedef for allocator functions semantically compatible with malloc().
*/
typedef void* (*JSON_malloc_t)(LT_SIZE n);
/**
   A typedef for deallocator functions semantically compatible with free().
*/
typedef void (*JSON_free_t)(void* mem);

/*! \brief The structure used to configure a JSON parser object
*/
typedef struct {
    /** Pointer to a callback, called when the parser has something to tell
        the user. This parameter may be NULL. In this case the input is
        merely checked for validity.
    */
    JSON_parser_callback    callback;
    /**
       Callback context - client-specified data to pass to the
       callback function. This parameter may be NULL.
    */
    void*                   callback_ctx;
    /** Specifies the levels of nested JSON to allow. Negative numbers yield unlimited nesting.
        If negative, the parser can parse arbitrary levels of JSON, otherwise
        the depth is the limit.
    */
    int                     depth;
    /**
       To allow C style comments in JSON, set to non-zero.
    */
    int                     allow_comments;
} JSON_config;

/*! \brief Initializes the JSON parser configuration structure to default values.

    The default configuration is
    - 127 levels of nested JSON (depends on JSON_PARSER_STACK_SIZE, see json_parser.c)
    - no parsing, just checking for JSON syntax
    - no comments
    - Uses realloc() for memory de/allocation.

    \param config. Used to configure the parser.
*/
void init_JSON_config(JSON_config * config);

/*! \brief Create a JSON parser object

    \param config. Used to configure the parser. Set to NULL to use
        the default configuration. See init_JSON_config.  Its contents are
        copied by this function, so it need not outlive the returned
        object.

    \return The parser object, which is owned by the caller and must eventually
    be freed by calling delete_JSON_parser().
*/
JSON_parser new_JSON_parser(JSON_config const* config);

/*! \brief LT: Create a JSON parser object without requiring config

    \return The parser object, which is owned by the caller and must eventually
    be freed by calling delete_JSON_parser().
*/
JSON_parser new_JSON_parser_ForLT(void);


/*! \brief LT: Set the callback and client data

*/
void JSON_parser_setCallbackAndContext(JSON_parser jc, JSON_parser_callback callback, void *callbackContext);

/*! \brief Destroy a previously created JSON parser object. */
void delete_JSON_parser(JSON_parser jc);

/*! \brief Parse a character.

    \return Non-zero, if all characters passed to this function are part of are valid JSON.
*/
int JSON_parser_char(JSON_parser jc, int next_char);

/*! \brief Finalize parsing.

    Call this method once after all input characters have been consumed.

    \return Non-zero, if all parsed characters are valid JSON, zero otherwise.
*/
int JSON_parser_done(JSON_parser jc);

/*! \brief Determine if a given string is valid JSON white space

    \return Non-zero if the string is valid, zero otherwise.
*/
int JSON_parser_is_legal_white_space_string(const char* s);

/*! \brief Gets the last error that occurred during the use of JSON_parser.

    \return A value from the JSON_error enum.
*/
int JSON_parser_get_last_error(JSON_parser jc);

/*! \brief Re-sets the parser to prepare it for another parse run.

    \return True (non-zero) on success, 0 on error (e.g. !jc).
*/
int JSON_parser_reset(JSON_parser jc);

#endif /* JSON_PARSER_H */
