/*******************************************************************************
 * <lt/utility/tokenizer/LTUtilityTokenizer.h>                  Text tokenizer
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

/**
 * @defgroup ltutility_tokenizer LTUtilityTokenizer
 * @ingroup ltutility
 *
 * @brief A library for text tokenization.
 *
 * LTTokenizer provides a simple interface for parsing raw text into tokens. The tokenizer
 * is configured one or more 'sentinel' characters which are used to delineate tokens. The
 * tokenizer will append characters until it reaches any one of the specified sentinel characters
 * and then emit the token to the user for processing. The sentinel characters can be changed
 * between tokens to allow parsing text which utilizes different sentinel characters for
 * different sections of the text structure.
 */

#ifndef ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYTOKENIZER_H
#define ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYTOKENIZER_H

#include <lt/LTTypes.h>
LT_EXTERN_C_BEGIN

typedef struct sLTTokenizer LTTokenizer;

typedef bool (LTTokenizer_ProcessTokenProc)(LTTokenizer * pTokenizer, const char * pToken, char nMatchedSentinel, void * pClientData);
    /**< Callback for the tokenizer to process one token at a time
     *
     *   @param pTokenizer pointer to the tokenizer instance in use
     *   @param pToken pointer to the token just found by the tokenizer
     *   @param nMatchedSentinel the sentinel that terminated the token
     *   @param pClientData client data pointer passed through CreateTokenizer()
     */

/*  _________________________
    Library Root Interface */

TYPEDEF_LTLIBRARY_ROOT_INTERFACE(LTUtilityTokenizer, 1);

/**
 * @ingroup ltutility_tokenizer
 */
struct LTUtilityTokenizerApi {

    INHERIT_LIBRARY_BASE

    LTTokenizer * (* CreateTokenizer)(const char * pSentinels, LTTokenizer_ProcessTokenProc * pProcessToken, void * pClientData);
        /**< creates a new tokenizer instance
         *
         *   @param pSentinels the character(s) to use to detect the end of the next token.
         *   @param pProcessToken the callback that will be called when a new token has been parsed
         *   @param pClientData client callback data passed to the pProcessToken callback
         *   @return the LTTokenizer instance
         */
    void          (* DestroyTokenizer)(LTTokenizer * pTokenizer);
        /**< destroys a tokenizer instance
         *
         *   @param pTokenizer the tokenizer instance to be destroyed
         */
    void          (* SetSentinels)(LTTokenizer * pTokenizer, const char * pSentinels);
        /**< sets the sentinel character(s)
         *
         *   @param pTokenizer the tokenizer instance
         *   @param pSentinels the character(s) to use to detect the end of the next token
         */
    u32           (* ProcessData)(LTTokenizer * pTokenizer, const void * pData, u32 nDataLen, bool bMoreData);
        /**< processes raw data, emitting tokens using the callback provided to %CreateTokenizer()
         *
         *   @param pTokenizer the tokenizer instance
         *   @param pData the buffer containing the raw data to be parsed
         *   @param nDataLen the size of pData in bytes
         *   @param bMoreData indicates that there is more data to come, which means anything after
         *          the last sentinel will not be included in the parsed tokens
         *   @return the number of bytes of the buffer at pData consumed by the tokenizer instance
         */

};

LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_UTILITY_LTUTILITYTOKENIZER_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  25-Feb-22   trajan      created
 */
