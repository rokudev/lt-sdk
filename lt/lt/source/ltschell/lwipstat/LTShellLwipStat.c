/*******************************************************************************
 *
 * LTShellLwipStat - Shell command for dumping lwIP pool statistics
 * -----------------------------------------------------------------------------
 *
 * Copyright 2026, Roku, Inc.  All Rights Reserved.
 *
 * Products that use the LTSystemSchell console cannot link LTShellNet (it
 * targets the LTSystemShell console instead), so this library exposes just its
 * `lwipstat` command.  It reports how close the lwIP memp pools have come to
 * exhaustion, which is what sizing PBUF_POOL and the mem pools depends on.
 *
 * The counters only exist when the lwIP port is built with LT_LWIP_MEMP_STAT=1
 * (config/product/lwip/LT_LWIP_MEMP_STAT in LTProductConfig.json); without it
 * the command runs but prints nothing.
 *
 ******************************************************************************/

#include <lt/LT.h>
#include <lt/device/config/LTDeviceConfig.h>
#include <lt/net/core/LTNetCoreDriver.h>
#include <lt/system/schell/LTSystemSchell.h>

DEFINE_LTLOG_SECTION("shell.lwipstat");

static struct Statics {
    LTCore *core;
} S;

/** Command *******************************************************************/

static int LwipStatCommand(LTSystemSchell *shell, int argc, const char **argv) {
    LT_UNUSED(argc), LT_UNUSED(argv);

    LTDeviceConfig *config = lt_openlibrary(LTDeviceConfig);
    if (!config) {
        shell->API->Print(shell, "no LTDeviceConfig\n");
        return -1;
    }
    const char *transportName = config->GetDefaultNetTransport();
    lt_closelibrary(config);

    LTLibrary *transport = transportName ? S.core->OpenLibrary(transportName) : NULL;
    if (!transport) {
        shell->API->Print(shell, "no transport '%s'\n", transportName ? transportName : "(none)");
        return -1;
    }

    /* The driver writes the report to the log, not to the shell. */
    LTNetDriver *driver = lt_getlibraryinterface(LTNetDriver, transport);
    if (driver && driver->ShowLwipStat) {
        driver->ShowLwipStat(NULL, false);
    } else {
        shell->API->Print(shell, "transport '%s' has no lwip stats\n", transportName);
    }
    lt_closelibrary(transport);
    return 0;
}

static const LTSystemShell_CommandDesc s_LwipStatCommands[] = {
    { "lwipstat", LwipStatCommand, "dump lwIP pool statistics to the log", NULL }
};

/** Library Lifecycle *********************************************************/

static void LTShellLwipStatImpl_LibFini(void) {
    LTSystemSchell *shell = LTSystemSchellConsole_GetConsoleShell();
    if (shell) {
        LTSystemShell_CommandTable table = {
            .commands    = s_LwipStatCommands,
            .numCommands = sizeof(s_LwipStatCommands) / sizeof(s_LwipStatCommands[0])
        };
        shell->API->UnregisterCommands(shell, &table);
    }
    S = (struct Statics){};
}

static bool LTShellLwipStatImpl_LibInit(void) {
    S.core = LT_GetCore();

    LTSystemSchell *shell = LTSystemSchellConsole_GetConsoleShell();
    if (!shell) {
        LTLOG_YELLOWALERT("init.fail", "init failed: no console shell");
        return false;
    }
    LTSystemShell_CommandTable table = {
        .commands    = s_LwipStatCommands,
        .numCommands = sizeof(s_LwipStatCommands) / sizeof(s_LwipStatCommands[0])
    };
    shell->API->RegisterCommands(shell, &table);
    return true;
}

/*******************************************************************************
 * Library Interfaces
 ******************************************************************************/

typedef_LTLIBRARY_ROOT_INTERFACE(LTShellLwipStat, 1) LTLIBRARY_EMPTY_INTERFACE;
define_LTLIBRARY_ROOT_INTERFACE(LTShellLwipStat) LTLIBRARY_DEFINITION;
