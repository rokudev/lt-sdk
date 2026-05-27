/******************************************************************************
 * <lt/system/shell/LTShell.h>        LTShell handle and the ILTShell interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSHELL_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSHELL_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/*____________________
 / LTShell TYPEDEFS */

/**********************
 * ILTShell Interface *
 **********************
 * LTSystemShell allows one serial and multiple network connections.
 * The ILTShell interface is used in the callback functions to output
 * text to one of the connected shells.
 */

typedef LTHandle LTShell;

/** ILTShell LTLIBRARY_INTERFACE
* Writes a string in the format specified by pFormat to the console
*/
typedef_LTLIBRARY_INTERFACE(ILTShell, 1) {

    void (* Print)(LTShell hShell, const char * pFormat, ...) LT_PRINTF_FORMAT_FUNCTION(2);
        /**
         * Writes a string in the format specified by pFormat to the console
         * output. This function may be called to output the response of the
         * command. The string is not modified prior to output.
         *
         * @param    hShell     The LTShell handle to which output should
         *                      be directed.
         *
         * @param    pFormat    The format that should be applied when
         *                      writing the output.
         *
         * @param    ...        Zero or more parameters as required by
         *                      the format that is passed in.
         */
    void (* VPrint)(LTShell hShell, const char * pFormat, lt_va_list args);
    void (* PutChar)(LTShell hShell, char ch);
    void (* PutString)(LTShell hShell, const char * pString);
    void (* PrintPrompt)(LTShell hShell);

} LTLIBRARY_INTERFACE;


LT_EXTERN_C_END
#endif /* #ifndef ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSHELL_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  06-Nov-20   augustus    created from .../include/soundbridge/os/Shell.h
 *  17-Dec-20   augustus    added VPrint, PutChar and PutString
 *  04-Jun-22   augustus    added PrintPrompt
 */
