/*******************************************************************************
 * <lt/system/shell/LTSystemShellCommands.h>            LT System Shell Commands
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/* LTSystemShellCommands
 *
 * Guidelines For Writing Commands:
 *  1. Do not perform logging in commands. Instead use the provided shell Print()
 *     facilities and timestamp formatting functions in LTCore (if needed).
 *  2. Separate manufacturing commands from developer commands by implementing them
 *     in product-specific areas. This prevents unnecessary dependencies in unrelated
 *     contexts.
 *  3. Commands consume more code space than one would expect. Keep them concise,
 *     especially when placed in shared areas of LT.
 *  4. Commands are NOT a substitute for unit tests.
 *  5. Organize commands in LTSystemShellCommand-derived objects. This allows them
 *     to be excluded from builds, helping to reduce code size.
 *
 * Command Tables
 *   Command tables must be registered with LTSystemShell before their associated
 *   commands can be executed. Commands can either be defined within an object or
 *   encapsulated in LTSystemShellCommands container objects. Using container objects
 *   offers the advantage of reducing code bloat, as unused commands can easily be
 *   excluded from the build.
 *
 *   LTSystemShellCommands and its various implementations are singleton objects that
 *   exclusively contain commands. The following example demonstrates how to register
 *   a table of commands from an LTSystemShellCommands container object: <pre>
 *
 *   LTSystemShellCommands *myCommands = lt_createobject_typed(LTSystemShellCommands, MyCommands);
 *   if (myCommands) {
 *       LTSystemShell_CommandTable table;
 *       myCommands->API->GetCommands(&table);
 *       shell->API->RegisterCommands(shell, &table);
 *   }</pre>
 */

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELLCOMMANDS_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELLCOMMANDS_H

#include <lt/system/schell/LTSystemSchell.h>
LT_EXTERN_C_BEGIN

typedef struct LTSystemSchell LTSystemSchell;

/*_______________________________
_/ LTSystemShellCommands Procs */

typedef int (LTSystemShell_CommandProc)(LTSystemSchell *shell, int argc, const char **argv);
/**< The type of the callback for command execution.
 *  TBR: CommandProc callbacks are executed in the thread context
 *    of the shell from which the command was invoked.
 *
 * @param shell Pointer to the shell instance.
 * @param argc The count of arguments present in the argv array.
 * @param argv The command argument array. Valid index values are [0..argc-1].
 *             argv[0] holds the command itself.
 * @return the result of the command. Zero should be returned to indicate success; non-zero
 *         to indicate failure. If the result is zero, the shell does nothing and prints
 *         the shell prompt for the next command. If the result is non-zero the shell prints
 *         out "<command>: returned <num>" before printing the shell prompt for the next
 *         command. */

typedef void (LTSystemShell_HelpProc)(LTSystemSchell *shell, int argc, const char **argv);
/**< The type of the callback for providing extended command specific help.
 *  TBR: CommandProc callbacks are executed in the thread context
 *    of the shell from which the command was invoked.
 *
 * @param shell Pointer to the shell instance.
 * @param argc The count of arguments present in the argv array.
 * @param argv The help request argument array. Valid index values are [0..argc-1].
 *             argv[0] holds the command for which help is requested.  */

/*_____________________________
_/ LTSystemShell_CommandDesc */

typedef struct LTSystemShell_CommandDesc {
    const char                 *name;      /**< command name; must be non-null and non-empty */
    LTSystemShell_CommandProc  *proc;      /**< command procedure; must be non-NULL */
    const char                 *desc;      /**< command description or NULL for no listing */
    LTSystemShell_HelpProc     *helpProc;  /**< extended help procedure or NULL for no extended help */
} LTSystemShell_CommandDesc;

/*______________________________
_/ LTSystemShell_CommandTable */

typedef struct LTSystemShell_CommandTable {
    const LTSystemShell_CommandDesc *commands;     /**< pointer to array of commands (the table) */
    u32                              numCommands;  /**< number of commands in array */
} LTSystemShell_CommandTable;

/*________________________________
_/ LTSystemShellCommands Object */

typedef_LTObject(LTSystemShellCommands, 1) {

    void (* GetCommands)(LTSystemShell_CommandTable *table);
    /**< Obtain command table.
     *
     * @param table pointer to the command table to set. */

} LTOBJECT_API;

LT_EXTERN_C_END
#endif /* ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELLCOMMANDS_H */
