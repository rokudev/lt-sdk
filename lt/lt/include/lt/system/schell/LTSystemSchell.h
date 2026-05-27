/*******************************************************************************
 * <lt/system/shell/LTSystemShell.h>                             LT System Shell
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

/* LTSystemShell
 *   An LTSystemShell object is instantiated for each active shell within a
 *   product. Shells can operate in one of three modes: interactive,
 *   non-interactive, or mixed. The ExecuteCommand() function is used to execute
 *   commands in non-interactive or mixed modes. By default, shells start in
 *   non-interactive mode. To switch a shell to interactive mode, call
 *   GoInteractive().
 *
 * LTSystemShellConsole
 *   The Console Shell serves as the default shell for LT. LTSystemShellConsole
 *   is a singleton object that provides external objects access to the Console
 *   Shell instance, thereby enabling the registration of commands.
 */

#ifndef ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELL_H
#define ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELL_H

#include <lt/core/LTCore.h>
#include <lt/system/schell/LTSystemSchellCommands.h>
LT_EXTERN_C_BEGIN

typedef struct LTSystemSchell             LTSystemSchell;
typedef struct LTSystemShell_CommandDesc  LTSystemShell_CommandDesc;
typedef struct LTSystemShell_CommandTable LTSystemShell_CommandTable;

/*_______________________
_/ LTSystemShell Procs */

typedef bool (LTSystemShell_EnumerateCommandsProc)(LTSystemSchell *shell,
                                                       const LTSystemShell_CommandDesc *desc, void *clientData);
/**< The proc for enumerating the commands registered with the shell.
 *
 * @param shell pointer to the shell instance.
 * @param desc pointer to command descriptor.
 * @param clientData pointer to private client data.
 * @return true to continue enumerating, false to stop.  */

typedef void (LTSystemShell_OutputCharsProc)(const char *chars, u32 numChars);
/**< The proc for transmitting characters to an underlying shell device.
 *
 * @param chars pointer to the characters to send to the shell output.
 * @param numChars number of characters to send.  */

/*________________________
_/ LTSystemShell Object */

typedef_LTObject(LTSystemSchell, 1) {

    /*********  Shell Command Output  *********/

    void    (* Print)(LTSystemSchell *shell, const char *format, ...) LT_PRINTF_FORMAT_FUNCTION(2);
    /** Write a formatted string to the shell output.
     *
     * @param shell pointer to the shell instance.
     * @param format the format string.
     * @param ...    the format string parameters. */

    void    (* PrintV)(LTSystemSchell *shell, const char *format, lt_va_list args);
    /** Write a formatted string to the shell output (variable argument list).
     *
     * @param shell pointer to the shell instance.
     * @param format the format string.
     * @param args the format string argument list. */

    void    (* PutString)(LTSystemSchell *shell, const char *string);
    /** Write a C string to the shell output.
     *
     * @param shell pointer to the shell instance.
     * @param string the null-terminated C string to print. */

    void    (* PutChars)(LTSystemSchell *shell, const char *chars, u32 numChars);
    /** Write one or more characters to the shell output.
     *
     * @param shell pointer to the shell instance.
     * @param chars pointer to the characters to write.
     * @param numChars number of characters to write.  */

    /*** External Command Registration and Execution ***/

    void    (* RegisterCommands)(LTSystemSchell *shell, LTSystemShell_CommandTable *table);
    /**< Register a command object with the shell.
     *
     * @param shell pointer to the shell instance.
     * @param table pointer to table of commands to add.  */

    void    (* UnregisterCommands)(LTSystemSchell *shell, LTSystemShell_CommandTable *table);
    /**< Unregister a command object with the shell.
     *
     * @param shell pointer to the shell instance.
     * @param table pointer to table of commands to remove.  */

    bool    (* EnumerateCommands)(LTSystemSchell *shell, LTSystemShell_EnumerateCommandsProc proc,
                                       void *clientData);
    /**< Invoke the given proc callback for each registered command.
     * @note this may be used to implement help functionality.
     *
     * @param shell pointer to the shell instance.
     * @param proc proc to invoke for each registered command. If this proc returns false then
     *             further enumeration will be aborted and EnumerateCommands() will return false.
     * @param clientData pointer to private client data.
     * @return false if any iteration of the enumeration proc returns false. */

    int     (* ExecuteCommand)(LTSystemSchell *shell, const char *command);
    /**< Execute a command string in the shell synchronously in the caller's thread context.
     * @note ExecuteCommand() is used to deploy commands in non-interactive or mixed shell modes.
     * @note All calls to ExecuteCommand() or GoInteractive() must originate from the same thread.
     *
     * @param shell pointer to the shell instance.
     * @param command command string to execute in the shell, e.g.: "echo Hello World!".
     * @return command integer return value.  */

    /*******  Shell Configuration and Modes *******/

    void    (* SetPrompt)(LTSystemSchell *shell, const char *prompt);
    /**< Set the prompt for an interactive shell.
     *
     * @param shell pointer to the shell instance.
     * @param prompt pointer to null-terminated string to use. */

    void    (* EnableOutput)(LTSystemSchell *shell, bool enable);
    /** Set the output enable mode for a shell.
     * @note If output is disabled via EnableOutput(), then interactive mode will ALSO be disabled.
     * @note Output mode is enabled by default if EnableOutput() is not called.
     *
     * @param shell pointer to the shell instance.
     * @param enable true to enable character output, false to disable. */

    void    (* SetPrintBufferSize)(LTSystemSchell *shell, u32 printBufferSize);
    /**< Sets the size of the shell's print buffer.
     * If the printed output exceeds this buffer size it won't be sent to the output.
     * @note the default size is 512 bytes. This size may be reduced to reduce memory footprint.
     *
     * @param shell pointer to the shell instance.
     * @param printBufferSize the size in bytes of the internal print buffer used by shell->API->Print(). */

    bool    (* GoInteractive)(LTSystemSchell *shell);
    /**< Enable interactive mode in the shell using the callers thread context.
     *  @note All calls to ExecuteCommand() or GoInteractive() must originate from the same thread.
     *
     * @param shell pointer to the shell instance.
     * @return true if the shell successfully entered interactive mode. */

    void    (* SetOutputCharsProc)(LTSystemSchell *shell, LTSystemShell_OutputCharsProc *outputCharsProc);
    /**< Sets the procedure used for character output by this shell.
     *
     * @param shell pointer to the shell instance.
     * @param outputCharsProc the proc to receive output characters from shell->API->Print() statements.
     *                        Use NULL to specify LTCore->ConsolePutChars() as the outputCharsProc.
     *
     * @note If SetOutputCharsProc is never called, the shell will use LT_GetCore()->ConsolePutChars().
     * @note Use SetOutputEnable() to disable or re-enable shell output. */

    void    (* PutInputChars)(LTSystemSchell *shell, const char *chars, u32 numChars);
    /**< Supply input characters to the shell.
     * @note Input characters are ignored unless shell is in interactive mode.
     *
     * @param shell pointer to the shell instance.
     * @param chars pointer to the characters to input to shell.
     * @param numChars number of characters to input to shell.  */

} LTOBJECT_API;

/*_______________________________
_/ LTSystemShellConsole Object */

typedef_LTObject(LTSystemSchellConsole, 1) {

    LTSystemSchell *  (* GetConsoleShell)(void);
    /**< Obtain a pointer to the console shell object.
     * The primary use for this singleton object is to register commands with the console shell.
     *
     * @return pointer to the Console Shell object, NULL if console is not present. */

} LTOBJECT_API;

/** Helper function for obtaining the console shell */
LT_INLINE LTSystemSchell *LTSystemSchellConsole_GetConsoleShell(void) {
    LTSystemSchellConsole *console = lt_createobject(LTSystemSchellConsole);
    LTSystemSchell        *shell   = console->API->GetConsoleShell();
    lt_destroyobject(console);
    return shell;
}

LT_EXTERN_C_END
#endif /* ROKU_LT_INCLUDE_LT_SYSTEM_SHELL_LTSYSTEMSHELL_H */
