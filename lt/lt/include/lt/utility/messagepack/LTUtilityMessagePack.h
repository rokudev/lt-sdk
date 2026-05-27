/*******************************************************************************
 *
 * LTUtilityMessagePack.h - LT MessagePack Library
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 ******************************************************************************/

/**
 * @defgroup ltutility_messagepack LTUtilityMessagePack
 * @ingroup ltutility
 * @brief An LT MessagePack encoder and decoder
 *
 * Add more description here.
 *
 * MessagePack Specification: https://github.com/msgpack/msgpack/blob/master/spec.md
 */

#ifndef ROKU_LT_INCLUDE_LT_MESSAGEPACK_LTUTILITYMESSAGEPACK_H
#define ROKU_LT_INCLUDE_LT_MESSAGEPACK_LTUTILITYMESSAGEPACK_H

#include <lt/LTTypes.h>
#include <lt/utility/buffer/LTBuffer.h>

LT_EXTERN_C_BEGIN

typedef enum LTMessagePack_Type {
    LTMessagePack_Type_Error,        ///< Error, including end of stream or out of memory
    LTMessagePack_Type_Nil,          ///< Nil
    LTMessagePack_Type_Boolean,      ///< True and false
    LTMessagePack_Type_Integer,      ///< Integers (all sizes signed and unsigned)
    LTMessagePack_Type_Float32,      ///< 32 bit IEEE float (distinguished from 64 bit)
    LTMessagePack_Type_Float64,      ///< 64 bit IEEE float
    LTMessagePack_Type_String,       ///< String, keep them zero terminated for direct use
    LTMessagePack_Type_Binary,       ///< Binary data
    LTMessagePack_Type_Array,        ///< Array of elements
    LTMessagePack_Type_Map,          ///< Map of key-values
    LTMessagePack_Type_Extended,     ///< User defined elements, typed and fixed or variable length
} LTMessagePack_Type;

#define MP_NIL_SIZE       (1)
#define MP_BOOL_SIZE      (1)
#define MP_U32_SIZE(n)    (((n) <= 0x7F) ? 1 : (((n + 0L) <= 0xFF) ? 2 : (((n + 0L) <= 0xFFFF) ? 3 : 5)))
#define MP_U64_SIZE(n)    (((n + 0LL) < 0x100000000LL) ? MP_U32_SIZE(n) : 9)
#define MP_S32_SIZE(n)    (((n) >= 0x80) ? MP_U32_SIZE(n) : (((n) >= -0x20) ? 1 : (((n) >= -0x80) ? 2 : (((n) >= -0x8000) ? 3 : 5))))
#define MP_S64_SIZE(n)    ((((n) < 0x100000000LL) && ((n) > -0x100000000LL)) ? MP_S32_SIZE(n) : 9)
#define MP_FLOAT32_SIZE   (5)
#define MP_FLOAT64_SIZE   (9)
#define MP_STR_SIZE(n)    ((((n) <= 0x1F) ? 1 : (((n) <= 0xFF) ? 2 : (((n + 0) <= 0xFFFF) ? 3 : 5))) + (n))
#define MP_BIN_SIZE(n)    ((((n) <= 0xFF) ? 2 : (((n + 0) <= 0xFFFF) ? 3 : 5)) + (n))
#define MP_ARRAY_SIZE(n)  (((n) <= 0xF) ? 1 : (((n) <= 0xFFFF) ? 3 : 5))
#define MP_MAP_SIZE(n)    (((n) <= 0xF) ? 1 : (((n) <= 0xFFFF) ? 3 : 5))
#define MP_EXT_SIZE(n)                                                                                             \
    (((n) == 1)            ? 2                                                                                     \
        : ((n) == 2)          ? 2                                                                                     \
        : ((n) == 4)          ? 2                                                                                     \
        : ((n) == 8)          ? 2                                                                                     \
        : ((n) == 16)         ? 2                                                                                     \
        : ((n) <= LT_U8_MAX)  ? 3                                                                                     \
        : ((n) <= LT_U16_MAX) ? 4                                                                                     \
                            : 6)

struct LTMessagePack_Obj;

typedef u8 *(*LTMessagePackRealloc)(struct LTMessagePack_Obj *obj, LT_SIZE requested);
/**< Callback for reallocating buffer space */

typedef struct LTMessagePack_Obj {
    u8  *head;                       ///< Base of bytes for decoding or encoding
    u8  *next;                       ///< Current position pointer of decode or encode
    u8  *end;                        ///< Used for end position checking (head + size)
    LTMessagePackRealloc allocator;  ///< Reallocation function when more memory is needed
} LTMessagePack_Obj;

typedef struct LTMessagePack_Value {
    union {
        bool    boolean;             ///< Boolean true or false
        s64     integer;             ///< For signed integer access
        u64     uinteger;            ///< For unsigned integer access
        float   float32;             ///< For 32 bit float access
        double  float64;             ///< For 64 bit float access
        u8     *data;                ///< For string and binary access
    };
    u32 size;                        ///< Size for various types (strings, binary, array, etc.)
    LTMessagePack_Type type;         ///< Type of data
    s8  extType;                     ///< User defined extended type
} LTMessagePack_Value;

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTUtilityMessagePack, 1);
struct LTUtilityMessagePackApi {
    INHERIT_LIBRARY_BASE

    bool (*Init)(LTMessagePack_Obj *mp, u8 *data, u32 dataSize);
        /**<
         * @brief Initialize a MessagePack object
         *
         * This function is used to initialize an object used for encoding and decoding
         * MessagePack content.
         *
         * For encoding: you can provide your own memory for storing the encoded output, or
         * optionally, you can provide a NULL and this function will allocate memory of the
         * size given. If you provide a NULL, memory will auto-expanded (using realloc) if
         * necessary, but of course for best performance, try to make it large enough to hold
         * the fully encoded MessagePack data.
         *
         * An easy way to estimate the required memory size is to make a test run where you
         * allocate more memory than you need, then call GetPosition() and print that to tell
         * you how much memory was needed.
         *
         * For decoding: provide valid MessagePack encoded data and specify its size. As you call
         * the decoder functions, if you run past the end of the data, an error will be returned.
         *
         * @param[in] mp is a LTMessagePack_Obj to initialize
         * @param[in] data is memory used for encoding or message pack data for decoding
         * @param[in] dataSize indicates the size of the above data in bytes
         * @return false if out of memory for allocating data (when NULL is passed for it). True otherwise.
         */

    void (*Free)(LTMessagePack_Obj *mp);
        /**<
         * @brief Frees data used for a MessagePack object
         *
         * If Init() was called with a NULL data pointer, then this function can be used to
         * free the memory allocated by Init(). If a data pointer was provided to Init(),
         * then it is not freed. (Caller must handle their own data.)
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         */

    u32 (*GetPosition)(LTMessagePack_Obj *mp);
        /**<
         * @brief Return the current position (offset) within the encoded data
         *
         * The result is just a byte offset from the head of the MessagePack data.
         * This function works for both decoding and encoding purposes.
         *
         * It is useful for cases where you need to rewind back to a known point, such as at the start
         * of an array or map to perform multiple searches.
         *
         * This function is also useful to determine the current size of encoded data. For example,
         * when you've finished encoding your data, call this function to get the total length in bytes.
         *
         * Note that when decoder functions return an error, the position does not advance. For example,
         * if you use GetString() and the data at the current position is an integer, GetString() will return
         * LTMessagePack_Type_Error and the position will not change. You can then use GetInteger() instead.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @return the current position in the data
         */

    void (*SetPosition)(LTMessagePack_Obj *mp, u32 position);
        /**<
         * @brief Set the current position within the encoded data.
         *
         * This function is useful if you need to rewind back to a known point, such as at the start
         * of an array or map to perform multiple searches. Use GetPosition() to get the current
         * position, then call this function to return to it later.
         *
         * You can also provide a zero position to rewind to the head of the encoded data.
         *
         * Note that you can set the position anywhere, even to an invalid spot within the data.
         * For example, if the position is within the contents of a string or binary, that data
         * will be treated as data to be decoded. Generally, that should be avoided.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] the desired position in the data
         */

    void (*Skip)(LTMessagePack_Obj *mp, u32 count);
        /**<
         * @brief Skip a number of elements
         *
         * Skip over the number of elements specified by the count. If the end of data is reached
         * do not go past that.
         *
         * Arrays and maps are not treated specially. Each of their elements is part of the skip count.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] count is the number of elements to skip
         */

    void (*SkipWithin)(LTMessagePack_Obj *mp, const LTMessagePack_Value *container, u32 count);
        /**<
         * @brief Skip a number of elements limited within a map or array
         *
         * Skip over the number of elements specified by the count, but not past the end of the
         * array or map provided by the specified value.
         *
         * The current position is updated. Calling this function again with the same container
         * will continue the skip from the current position (not from the start of the container).
         *
         * The normal usage pattern is to call GetValue() to obtain the array or map value, then
         * call this function with that value.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] container used to limit the skip
         * @param[in] count is the number of elements to skip
         */

    bool (*SkipContainer)(LTMessagePack_Obj *mp, const LTMessagePack_Value *container);
        /**<
         * @brief Skip over the all elements of an array or map
         *
         * The current position is updated to just past the last element of the array or map.
         *
         * The normal usage pattern is to call GetValue() to obtain the array or map value, then
         * call this function with that value.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] count is the number of elements to skip
         * @param[in] container used to limit the skip
         * @return True if arguments are valid
         */

    bool (*FindCString)(LTMessagePack_Obj *mp, const LTMessagePack_Value *container, char *targetString);
        /**<
         * @brief Find a CString within an optional container
         *
         * Searches for a matching CString. An array or map container may be provided.
         *
         * There are three container options:
         *
         * - Array: the search will occur within the array, and if the string is found, true
         * will be returned and the current position will be where the string was found.
         *
         * - Map: the search will occur on the keys only, and if the string is found, true
         * will be returned and the current position will be the value that follows the key.

         * - Null: the search will occur within the entire encoded data, and if the string is found,
         * true will be returned and the current position will be where the string was found.
         *
         * In all the above cases, if the string was not found, then the current position will
         * remain unchanged and false will be returned.
         *
         * Calling this function again with the same container will continue the skip from the current
         * position (not from the start of the container).
         *
         * The search will be limited to the bounds of the array or map provided by the container argument.
         * The normal usage pattern is to call GetValue() to obtain the array or map value, then call this
         * function with that value.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] container to search, or NULL for entire MessagePack data
         * @param[in] target C-string to find
         * @return True if value was found. False otherwise.
         */

    bool (*FindInteger)(LTMessagePack_Obj *mp, const LTMessagePack_Value *container, s64 targetInteger);
        /**<
         * @brief Find an integer within an optional container
         *
         * Searches for a matching integer. An array or map container may be provided.
         *
         * There are three container options:
         *
         * - Array: the search will occur within the array, and if the integer is found, true
         * will be returned and the current position will be where the integer was found.
         *
         * - Map: the search will occur only on the keys, and if the integer is found, true
         * will be returned and the current position will be the value that follows the key.

         * - Null: the search will occur within the entire encoded data, and if the integer is found,
         * true will be returned and the current position will be where the integer was found.
         *
         * In all the above cases, if the integer was not found, then the current position will
         * remain unchanged and false will be returned.
         *
         * The search will be limited to the bounds of the array or map provided by the container argument.
         * The normal usage pattern is to call GetValue() to obtain the array or map value, then call this
         * function with that value.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] container to search, or NULL for entire MessagePack data
         * @param[in] target integer to find
         * @return True if value was found. False otherwise.
         */

    bool (*PutNil)(LTMessagePack_Obj *mp);
        /**<
         * @brief Output a NIL at the current position
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutBoolean)(LTMessagePack_Obj *mp, bool isTrue);
        /**<
         * @brief Output a boolean true or false at the current position
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] boolean true or false to output
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutIntS32)(LTMessagePack_Obj *mp, s32 integer);
        /**<
         * @brief Output a signed integer at the current position
         *
         * Within the encoded output the minimal amount of space will be used to store
         * the integer depending on its value.
         *
         * To store an integer larger than 32 bits, use the PutIntS64/U64 functions.
         * (Rarely needed, which is why they are separate functions.)
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] integer value to output, 32-bit maximum
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutIntU32)(LTMessagePack_Obj *mp, u32 integer);
        /**<
         * @brief Output an unsigned integer at the current position
         *
         * See PutIntS32 for more information.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] integer value to output, 32-bit maximum
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutIntS64)(LTMessagePack_Obj *mp, s64 integer);
        /**<
         * @brief Output a large signed integer at the current position
         *
         * See PutIntS32 for more information. Note that if the integer can be stored
         * in a more compact encoded format, it will be.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] integer value to output, 64-bit maximum
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutIntU64)(LTMessagePack_Obj *mp, u64 integer);
        /**<
         * @brief Output a large unsigned integer at the current position
         *
         * See PutIntS32 for more information. Note that if the integer can be stored
         * in a more compact encoded format, it will be.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] integer value to output, 32-bit maximum
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutFloat32)(LTMessagePack_Obj *mp, float value);
        /**<
         * @brief Output a 32 bit floating point number at the current position
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] float value to output, 32-bit maximum
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutFloat64)(LTMessagePack_Obj *mp, double value);
        /**<
         * @brief Output a 64 bit floating point number at the current position
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] float value to output, 32-bit maximum
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    u32 (*PutString)(LTMessagePack_Obj *mp, const char *data, u32 size);
       /**<
         * @brief Output or reserve space for a string at the current position
         *
         * The string will be copied into the encoded output if provided. If you use this
         * function for NULL-terminated C strings, please included the NULL termination.
         * This allows the string to be used directly in-place without copying.
         *
         * The maximum size of a string is limited to 2^32-1.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] data (optional) points to a string or NULL to only reserve space
         * @param[in] size indicates the number of bytes of data. If you
         * provide a size for the string, be sure to include the null terminator. This
         * makes it possible for apps to directly use the string without copying it.
         * @return The position of the data section of the inserted binary field or 0 if encode
         * buffer ran out of memory and could not be expanded
         */

    bool (*PutCString)(LTMessagePack_Obj *mp, const char *data);
       /**<
         * @brief Output a C null-terminated string at the current position
         *
         * The string and its terminator will be copied into the encoded output.
         *
         * The maximum size of a string is limited to 2^32-1.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] data points to a string
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    u32 (*PutBinary)(LTMessagePack_Obj *mp, const u8 *data, u32 size);
       /**<
         * @brief Output or reserve space for binary at the current position
         *
         * The binary data will be copied into the encoded output if provided.
         *
         * The size field indicates how many bytes to copy/reserve.
         *
         * The maximum size of data is limited to 2^32-1.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] data (optional) points to binary data to be copied or NULL to only reserve space
         * @param[in] size indicates the number of bytes of data. For strings, if the size
         * is zero, the entire string will be copied, including its terminator. If you
         * provide a size for the string, be sure to include the null terminator. This
         * makes it possible for decoding apps to directly use the string without copying it.
         * @return The position of the data section of the inserted binary field or 0 if encode
         * buffer ran out of memory and could not be expanded
         */

    bool (*PutArray)(LTMessagePack_Obj *mp, u32 size);
       /**<
         * @brief Start output of a MessagePack array
         *
         * This function indicates that the data to follow is part of an array
         * structure. You must provide the number of elements of the array.
         *
         * After calling this function, output each of the elements of the array
         * in the normal way. Elements can be of any type and can be mixed with
         * the array depending on the structure intended by the user.
         *
         * The maximum number of elements is limited to 2^32-1.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] size indicates the number of elements of the array
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutMap)(LTMessagePack_Obj *mp, u32 size);
       /**<
         * @brief Start output of a MessagePack map
         *
         * This function indicates that the data to follow is part of the map
         * structure. You must provide the number of elements (key-value pairs)
         * of the map.
         *
         * After calling this function, output each of the elements of the map
         * in the normal way: key-value. Keys and values can be of any type, and
         * can be mixed depending on the semantics intended by the user.
         *
         * The maximum number of elements is limited to 2^32-1.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] size indicates the number of elements (key-value pairs) of the map
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutExtended)(LTMessagePack_Obj *mp, s8 type, const u8 *data, u32 size);
       /**<
         * @brief Output a user-defined extended MessagePack datatype
         *
         * This is useful if you want to output typed values in a user-defined format.
         * For example, you could output MAC or TCP network addresses that get "tagged"
         * with your own type constant to identify them. The format of the data is entirely
         * up to you, and the data get copied into the encoded output.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[in] type can be any 8-bit integer value, with -1 reserved for timestamps
         * @param[in] data points to the data to copy into the encoded output
         * @param[in] size is the number of bytes of data
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    bool (*PutMessagePack)(LTMessagePack_Obj *mp, const LTMessagePack_Obj *source);
        /**<
         * @brief Add the entire contents of one MessagePack to the end of another
         *
         * This is useful when you have already built up a MessagePack, gathering metadata
         * along the way, and need to use that metadata to generate a MessagePack structure
         * (e.g., a map) enclosing the MessagePack data you've built up.
         * For example, you don't know ahead of time how many items a map will contain
         * until you actually go through the process of generating the MessagePack of the
         * map.  You can generate a secondary MessagePack with just the items in the map,
         * counting the items as you go, then add the map structure to the primary
         * MessagePack (with PutMap()), then use PutMessagePack() to splice in all the
         * items in the secondary MessagePack at once.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj, to receive the MessagePack
         * @param[in] source is the source MessagePack to be written to mp
         * @return false if encode buffer ran out of memory and could not be expanded
         */

    LTMessagePack_Type (*GetValue)(LTMessagePack_Obj *mp, LTMessagePack_Value *value);
       /**<
         * @brief Get all types of data from the current position
         *
         * This is a general purpose function to decode any type of MessagePack data.
         * You can use this function alone or in conjunction with the other "Get" functions
         * of this API.
         *
         * As data is decoded, the current position is advanced accordingly.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[out] value provides access to the decoded data
         * @return The datatype of the MessagePack data. Check this result for
         * LTMessagePack_Type_Error (zero) to detect the end of data or bad data.
         */

    s8 (*GetInteger)(LTMessagePack_Obj *mp, u64 *integer);
       /**<
         * @brief Get an integer at the current position
         *
         * The integer is stored in the integer argument and the return value is used
         * to tell you if the integer is positive and unsigned or negative and signed.
         * In addition, the return will tell you how many bytes were needed to store
         * the encoded value. This is useful to tell you if the returned integer
         * requires 32-bits or 64-bits. For signed integers less than 32-bits, use your
         * own range test. (The encoded size is not symmetrical around zero.)
         *
         * As data is decoded, the current position is advanced accordingly.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[out] integer gets set on successful return
         * @return Indicates if the value is signed or unsigned and what the size of
         * the encoded value was. A zero indicates that the next value is not an integer
         * or that the end of encoded data was reached.
         */

    LTMessagePack_Type (*GetString)(LTMessagePack_Obj *mp, u8 **string, u32 *length);
       /**<
         * @brief Get a string or binary at the current position
         *
         * The return value tells you if the value is a string or binary.
         * Strings should always include their zero termination to allow them
         * to be used in place without copying. However, that convention is not
         * enforced, and if you don't know the encoding source, it is wise to
         * check that the string has been terminated by using the returned length.
         *
         * As data is decoded, the current position is advanced accordingly.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[out] string points to the start of the string or binary
         * @param[out] length tells you the size of the string or binary
         * @return Indicates if the data is a string or binary, or that the next
         * value is neither or that the end of encoded data was reached.
         */

    LTMessagePack_Type (*GetArray)(LTMessagePack_Obj *mp, u32 *length);
       /**<
         * @brief Get the size of an array or map
         *
         * The return value tells you if the value is an array or map.
         * The length tells you the number of elements that are included.
         *
         * As data is decoded, the current position is advanced accordingly.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[out] length tells you the number of elements
         * @return Indicates if its an array or a map, or that the next
         * value is neither or that the end of encoded data was reached.
         */

    s8 (*GetExtended)(LTMessagePack_Obj *mp, u8 **data, u32 *length);
       /**<
         * @brief Get a user-defined extended type
         *
         * This function returns the user-defined type of the data that
         * is pointed to by the data parameter. Note that it is a signed
         * 8-bit value. The length of the data is also set.
         *
         * As data is decoded, the current position is advanced accordingly.
         *
         * @param[in] mp is an initialized LTMessagePack_Obj
         * @param[out] data points to the start of the data
         * @param[out] length tells you the length of the data
         * @return The user defined type is returned. A zero indicates
         * that the next value is not an extended type or that the end
         * of the encoded data has been reached.
         */

    bool (*WriteNil)(LTBuffer *buf);
    /**< Write nil value to the buffer */

    bool (*WriteBoolean)(LTBuffer *buf, bool value);
    /**< Write boolean value to the buffer */

    bool (*WriteIntU32)(LTBuffer *buf, u32 value);
    /**< Write unsigned 32-bit integer value to the buffer with most compact encoding */

    bool (*WriteIntU64)(LTBuffer *buf, u64 value);
    /**< Write unsigned 64-bit integer value to the buffer with most compact encoding  */

    bool (*WriteIntS32)(LTBuffer *buf, s32 value);
    /**< Write signed 32-bit integer value to the buffer with most compact encoding */

    bool (*WriteIntS64)(LTBuffer *buf, s64 value);
    /**< Write signed 64-bit integer value to the buffer with most compact encoding */

    bool (*WriteFloat32)(LTBuffer *buf, float value);
    /**< Write 32-bit floating point value to the buffer */

    bool (*WriteFloat64)(LTBuffer *buf, double value);
    /**< Write 64-bit floating point value to the buffer */

    bool  (*WriteString)(LTBuffer *buf, const char *data, u32 size);
    /**< Write string to the buffer without termination
     *
     * @param data String data to write or NULL to reserve space
     * @param size Number of string characters to write
     */

    bool (*WriteCString)(LTBuffer *buf, const char *data, bool terminated);
    /**< Write a C-string to the buffer
     *
     * @param data Null-terminated string to write or NULL to reserve space
     * @param terminated Whether the null terminator will be included in the string
     */

    u32 (*PrintString)(LTBuffer *buf, const char *format, ...);
    /**< Print a formatted string into the buffer as with sprintf */

    u32 (*VPrintString)(LTBuffer *buf, const char *format, lt_va_list args);
    /**< Print a formatted string into the buffer as with vsprintf */

    bool  (*WriteBinary)(LTBuffer *buf, const u8 *data, u32 size);
    /**< Write a binary blob to the buffer
     *
     * @param data Pointer to data to write
     * @param size Number of bytes to write
     */

    bool (*WriteArray)(LTBuffer *buf, u32 size);
    /**< Write an array descriptor to the buffer
     *
     * @param size Number of array elements to follow
     *
     * This call must be followed by size additional Write calls to the same buffer providing the array contents.
     */

    bool (*WriteMap)(LTBuffer *buf, u32 size);
    /**< Write a map descriptor to the buffer
     *
     * @param size Number of key-value pairs to follow
     *
     * This call must be followed by size * 2 additional Write calls to the same buffer providing the map key-value
     * pairs.
     */
    bool (*WriteExtended)(LTBuffer *buf, s8 type, u32 size);

    bool (*WriteTable)(LTBuffer *buf, u32 headerRows, u32 headerColumns, s32 size);
    /**< Write a table extension type descriptor to the buffer
     *
     * @param headerRows Number of column headers in the table
     * @param headerColumns Number of row headers in the table
     * @param size Number of rows in the table or -1 for a Nil terminated table
     */

    bool (*ReadNil)(LTBuffer *buf);
    /**< Consume a nil value */

    bool (*ReadBoolean)(LTBuffer *buf, bool *value);
    /**< Read a boolean from the buffer */

    bool (*ReadIntU8)(LTBuffer *buf, u8 *value);
    /**< Read an 8-bit unsigned integer from the buffer */

    bool (*ReadIntU16)(LTBuffer *buf, u16 *value);
    /**< Read a 16-bit unsigned integer from the buffer */

    bool (*ReadIntU32)(LTBuffer *buf, u32 *value);
    /**< Read a 32-bit unsigned integer from the buffer */

    bool (*ReadIntU64)(LTBuffer *buf, u64 *value);
    /**< Read a 64-bit unsigned integer from the buffer */

    bool (*ReadIntS8)(LTBuffer *buf, s8 *value);
    /**< Read an 8-bit signed integer from the buffer */

    bool (*ReadIntS16)(LTBuffer *buf, s16 *value);
    /**< Read n 16-bit signed integer from the buffer */

    bool (*ReadIntS32)(LTBuffer *buf, s32 *value);
    /**< Read n 32-bit signed integer from the buffer */

    bool (*ReadIntS64)(LTBuffer *buf, s64 *value);
    /**< Read n 64-bit signed integer from the buffer */

    bool (*ReadFloat32)(LTBuffer *buf, float *value);
    /**< Read a 32-bit floating point value */

    bool (*ReadFloat64)(LTBuffer *buf, double *value);
    /**< Read a 64-bit floating point value */

    bool (*ReadString)(LTBuffer *buf, u32 *length);
    /**< Read a string header from the buffer
     *
     * @param[out] length Filled with the string length that follows
     *
     * Exactly length bytes of string data must be read directly from the buffer by any method after this call
     * succeeds.
     */

    bool (*ReadArray)(LTBuffer *buf, u32 *length);
    /**< Read an array header from the buffer
     *
     * @param[out] length Filled with the number of array elements which follow
     */

    bool (*ReadMap)(LTBuffer *buf, u32 *length);
    /**< Read a map header from the buffer
     *
     * @param[out] length Number of key-value pairs which follow
     */

    bool (*ReadExtended)(LTBuffer *buf, u8 *type, u32 *length);
    /**< Read an extended type header from the buffer
     *
     * @param[out] type Extended type identifier
     * @param[out] length Length of extended payload
     *
     * Exactly length bytes of extended data must be read directly from the buffer by any method after this call
     * succeeds.
     */

    bool (*ReadTable)(LTBuffer *buf, u32 *rowHeaders, u32 *colHeaders, s32 *size);
    /**< Read a table descriptor */

    bool (*ReadTableHeader)(LTBuffer *buf, u32 *rowHeaders, u32 *colHeaders, s32 *size);
    /**< Read a table descriptor if the extended type wrapper has already been decoded */

    u32 (*PeekAt)(LTBuffer *buf, LTMessagePack_Value *value, u32 offset);
    /**< Peek the element at a byte offset within the buffer */

    bool (*ReadNext)(LTBuffer *buf, LTMessagePack_Value *value);
    /**< Read the next element descriptor */

    const char* (*CopyString)(LTBuffer *buf);
    /**< Copy a string from the buffer as a zero-terminated string
     *
     * @returns Pointer to a string guaranteed to be zero-terminated
     *
     * FreeString must always be called after the returned string is no longer being used.
     */

    void (*FreeString)(LTBuffer *buf, const char* string);
    /**< Free a string previously returned by CopyString */

    bool (*ReadBinary)(LTBuffer *buf, u32 *length);
    /**< Read a binary blob from the buffer
     *
     * @param[out] length of the binary payload
     *
     * Exactly length bytes of binary data must be read directly from the buffer by any method after this call
     * succeeds.
     */
};

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_MESSAGEPACK_LTUTILITYMESSAGEPACK_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  16-Mar-23   hadrian     created
 */
