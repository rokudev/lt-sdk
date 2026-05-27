/******************************************************************************
 * platforms/apple/source/apple/liblthost/liblthost.m - static lib
 * for linking against programs that use LT in hosted mode, e.g. iOS Apps.
 *
 * Three functions are supplied: LT_GetCore(), LT_GetStdlib() and LT_Run().
 * They all lazily instantiate LTCore in hosted mode.
 *
 * LTRun(argc, argv) expects "ltrun command [command_args]" and operates
 * just like the "ltrun" command in the shell.
 *
 * Copyright 2021, Roku, Inc.  All Rights Reserved.
 ******************************************************************************/


#include <dlfcn.h>
#include <stdio.h>
#include <time.h>

#include <lt/LT.h>
#undef va_list
#include <apple/Apple_LTLibraryPaths.h>

/*********************
 * liblthost #defines */
#define LTCOREEXPORTED_LTRUN            "LTCoreExported_LTRun"
#define LTCOREEXPORTED_LTGETCORE        "LTCoreExported_LTGetCore"
#define LTCORE_LIBRARYNAME           	"LTCore"
#define ATOMIC_SLEEP_MS                 (5)
#define ATOMIC_SLEEP_NS                 (ATOMIC_SLEEP_MS * 1000000)
#define LT_RUN                          "LT_Run(argc, argv): "

/********************
 * liblthost types */
typedef int         (LTRunProc)(int argc, const char ** argv);
typedef LTCore *    (LTGetCoreProc)(void);
struct  LTRunClientData {
            LTLibrary * m_pLibrary;
            const char ** m_argv;
            int m_argc;
            int m_nRetVal;
        };

/*******************************
 * liblthost static variables */
static void *   s_pLTCoreLibrary = NULL;
static LTCore * s_pCore          = NULL;
static LTAtomic s_atomicRunOnce  = 0;
static const char * s_argv[2]    = { "lthost", LT_APPLE_PLATFORM_NAME };

/******************************
 * liblthost helperfunctions */
static void LazyLoadLTCoreLibrary(void) {

    bool              bSuccess = false;
    LTGetCoreProc   * pLTGetCoreProc = NULL;
    LTRunProc       * pLTRunProc = NULL;

    NSString * pNSLibraryPath = Apple_GetLTLibraryFilePath(LTCORE_LIBRARYNAME);
    const char * pLTLibraryPath = [pNSLibraryPath UTF8String];
    do
    {
        if (NULL == (s_pLTCoreLibrary = dlopen(pLTLibraryPath, RTLD_NOW | RTLD_LOCAL))) {
            printf("Unable to find %s\n", pLTLibraryPath);
            break;
        }
        if (NULL == (pLTRunProc = (LTRunProc *)dlsym(s_pLTCoreLibrary, LTCOREEXPORTED_LTRUN))) {
            printf("Unable to find function " LTCOREEXPORTED_LTRUN " in %s\n", pLTLibraryPath);
            break;
        }
        if (NULL == (pLTGetCoreProc = (LTGetCoreProc *)dlsym(s_pLTCoreLibrary, LTCOREEXPORTED_LTGETCORE))) {
            printf("Unable to find function " LTCOREEXPORTED_LTGETCORE " in %s\n", pLTLibraryPath);
            break;
        }
        bSuccess = true;
    }
    while (false);

    if (! bSuccess) {
        pLTLibraryPath = dlerror();
        if (pLTLibraryPath) printf("liblthost: %s\n", pLTLibraryPath);
        if (s_pLTCoreLibrary) { dlclose(s_pLTCoreLibrary); s_pLTCoreLibrary = NULL; }
        return;
    }

    bSuccess = false;
    do
    {
        if (0 != (*pLTRunProc)(sizeof(s_argv)/sizeof(s_argv[0]), s_argv)) {
            printf("%s:" LTCOREEXPORTED_LTRUN "() failed.\n", pLTLibraryPath);
            break;
        }
        if (NULL == (s_pCore = (*pLTGetCoreProc)())) {
            printf("%s:" LTCOREEXPORTED_LTGETCORE "() returned NULL\n", pLTLibraryPath);
            break;
        }
        bSuccess = true;
    }
    while (false);

    if (! bSuccess) {
        dlclose(s_pLTCoreLibrary);
        s_pLTCoreLibrary = NULL;
    }
}

static void AtomicSleep(void) {
    struct timespec remaining, delay = { 0, (long)ATOMIC_SLEEP_NS };
    while (0 != nanosleep(&delay, &remaining)) delay = remaining;
}

static void LTRunTaskProc(LTThread hThread, void * pClientData) {
    struct LTRunClientData * pCD = (struct LTRunClientData *)pClientData;
    pCD->m_nRetVal = pCD->m_pLibrary->Run(pCD->m_argc, pCD->m_argv);
    lt_gethandleinterface(ILTThread, hThread)->Terminate(hThread);
}

/**********************************************************************************
 * LT_Run() implementation - in hosted mode, make it work like the ltrun command */
int LT_Run(int argc, const char ** argv) {
    LTCore * pCore = LT_GetCore();
    if (NULL == pCore) return - 1;

    struct LTRunClientData clientData;
    clientData.m_argc = argc;
    clientData.m_argv = argv;
    clientData.m_nRetVal = -1;
    clientData.m_pLibrary = NULL;

    ILTThread * iThread;
    LTThread hThread = 0;
    u32 nStackSize = 0;

    do {
        if (argc < 2 || (0 == argv[1][0])) { printf("LTRun(argc, argv): argv[0..argc) must be of the form: \"ltrun libraryName [args]\"\n"); break; }
        if (NULL == (clientData.m_pLibrary = pCore->OpenLibrary(argv[1]))) { printf("LTRun(%s): failed to open library\n", argv[1]); break; }
        if (0 == (hThread = pCore->CreateThread(argv[1]))) { printf("LTRun(%s): failed to create run thread\n", argv[1]); break; }
        iThread = (ILTThread *)pCore->GetHandleInterface(hThread);
        if (NULL == clientData.m_pLibrary->Run) { printf("LTRun(%s): library has no Run() function\n", argv[1]); break; }
        if (0 != (nStackSize = clientData.m_pLibrary->GetRunFunctionStacksizeRequirement())) iThread->SetStackSize(hThread, nStackSize);
        clientData.m_nRetVal = 0;
        iThread->Start(hThread, NULL, NULL);
        iThread->QueueTaskProc(hThread, LTRunTaskProc, NULL, &clientData);
        iThread->WaitUntilFinished(hThread, LTTime_Infinite());
    }
    while (false);

    return clientData.m_nRetVal;
}

/*****************************************************************************************
 * LT_GetCore() implementation - in hosted mode LT_GetCore() lazily instantiates LTCore */
LTCore * LT_GetCore(void) {
    while (! LTAtomic_TestAndSet(&s_atomicRunOnce, 0, 1)) AtomicSleep();
    if (NULL == s_pCore) LazyLoadLTCoreLibrary();
    s_atomicRunOnce = 0;
    return s_pCore;
}

/*****************************************************************************************
 * LT_GetStdlib() implementation - in hosted mode LT_GetStdlib() lazily instantiates LTCore */
LTStdlib * LT_GetStdlib(void) {
    return LT_GetCore()->GetLTStdlib();
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  18-Jun-21   augustus    created from libltrun.c for hosted mode
 */
