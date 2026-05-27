/******************************************************************************
 * <lt/system/shell/LTSystemShell.h>            LTSystemShell library interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELL_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELL_H

#include <lt/system/shell/LTShell.h>

LT_EXTERN_C_BEGIN

/*___________________________
_/ LTSystemShell Introduction
   _________________________
   The LTSystemShell Library provides an interactive shell on the console and
   over network connections.  The console shell is automatically started the
   first time the LTSystemShell library is opened, typically from a system's
   genesis library during system startup.  On some systems LTSystemShell
   *is* the genesis library.  Isn't that neat?  Yes it is.

   LTSystemShell also provides the facility for other libraries to register
   and unregister their own shell commands.  The user may examine the list of
   registered commands by entering '?' or 'help' at the shell prompt.
*/

/*_____________________________
_/ LTSystemShell_CommandProc */
typedef int (LTSystemShell_CommandProc)(LTShell hShell, int argc, const char ** argv);
    /**<
     * The type of the callback for command execution.  CommandProc callbacks are executed
     * in the thread context of the shell from which the command was invoked.  Shell threads
     * provide 320 bytes of stack space to CommandProc callbacks so plan accordingly.
     *
     * @param   hShell   The LTShell that the command was invoked within.  All
     *                   shell interaction (e.g. Print) must use this shell handle.
     *
     * @param   argc     The count of arguments present in the argv array.
     *
     * @param   argv     The command argument array.  Valid index values are [0..argc-1].
     *                   argv[0] holds the command itself.
     *
     * @return the result of the command. Zero should be returned to indicate success; non-zero to indicate failure.
     *         If the result is zero, the shell does nothing and prints the shell prompt for the next command.
     *         If the result is non-zero the shell prints out
     *         "'<command>': returned <num>"
     *         before printing the shell prompt for the next command.
     */

/*__________________________
_/ LTSystemShell_HelpProc */
typedef void (LTSystemShell_HelpProc)(LTShell hShell, int argc, const char ** argv);
    /**<
     * The type of the callback for providing extended command specific help.
     * HelpProc callbacks are executed in the thread context of the shell from which
     * the help request was invoked.  Shell threads provide 320 bytes of stack space
     * to HelpProc callbacks so plan accordingly.
     *
     * @param   hShell   The LTShell that the help request was invoked within.  All
     *                   shell interaction (e.g. Print) must use this shell handle.
     *
     * @param   argc     The count of arguments present in the argv array.
     *
     * @param   argv     The help request argument array.  Valid index values are [0..argc-1].
     *                   argv[0] holds the command for which help is requested.
     */

/*_____________________________
_/ LTSystemShell_CommandDesc */
typedef struct {
    const char                          *pCommand;               /**< command string; must be non-null and non-empty */
    LTSystemShell_CommandProc           *pCommandProc;           /**< command procedure; must be non-null */
    const char                          *pDescription;           /**< command description or NULL for no listing or kLTSystemShell_HiddenCommand to hide command from help listing */
    LTSystemShell_HelpProc              *pHelpProc;              /**< extended help procedure or NULL for no extended help */
} LTSystemShell_CommandDesc;


#define kLTSystemShell_HiddenCommand        ((const char *)LT_SIZE_MAX)
    /**< set m_pDescription to this value to make the command available but hidden from the help listing */

/*___________________________________
_/ LTSystemShell Library Interface */
typedef_LTLIBRARY_ROOT_INTERFACE(LTSystemShell, 1) {
    void (* RegisterCommands)(const LTSystemShell_CommandDesc commandDescs[], u32 nNumCommandDescs);
       /**< registers shell command descriptors with the Shell.
         *
         * When a command is invoked in the shell, registered command descriptors
         * are searched and the shell invokes the matching descriptor's CommandProc.
         * When help is requested for a command, the matching descriptor's HelpProc is called.
         * CommandProcs and HelpProcs are called in the thread context of the shell
         * from which the command was invoked.
         *
         * The entire commandDescs array must remain fixed in memory while
         * registered with the shell.  It is recommended that only static const
         * arrays of commands be registered and unregistered so that descriptor
         * arrays may be located in Flash Memory without consuming system RAM.
         *
         *      static const LTSystemShell_CommandDesc s_myCommandDescs[] = {
         *          { ... },
         *          { ... },
         *          { ... }
         *      };
         *
         *      static void RegisterMyCommands() {
         *          s_pSystemShell->RegisterCommands(s_myCommandDescs, sizeof(s_myCommandDescs) / sizeof(s_myCommandDescs[0]);
         *      };
         *
         * @param  commandDescs     The C array of commandDescs to register.
         * @param  nNumCommandDescs The number of commandDescs in the array.
         */

    void (* UnregisterCommands)(const LTSystemShell_CommandDesc commandDescs[]);
        /**<
         * Unregisters a previously registered list of commands.
         *
         * @param  commandDescs   The C array of commandDescs to unregister.  The commandDescs array
         *                        must be identical to the one registered - fixed in content and location:
         *                        i.e. The address of commandDescs (&commandDescs[0]) used in both the calls
         *                        to RegisterCommands and UnregisterCommands must be the same.
         */

} LTLIBRARY_INTERFACE;

LT_EXTERN_C_END

#endif /* #ifndef ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELL_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  06-Nov-20   augustus    created
 *  16-Nov-20   caligula    initial definition
 *  07-May-24   augustus    added auxiliary usb console functions
 */
