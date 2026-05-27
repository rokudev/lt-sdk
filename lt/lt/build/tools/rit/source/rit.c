/******************************************************************************
 * rit.c                                  Roku Image Tool for Flash Programming
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>

#include "lt/LTTypes.h"

#include "Image.h"
#include "FlashDevice.h"

enum {
    kMinimumArgumentCount  = 1,
    kMaximumArgumentCount  = 10,
};

#define countof(x)   (sizeof(x) / sizeof(x[0]))
#define _stringify(s) #s
#define stringify(s) _stringify(s)

typedef u8 Command;
enum Command {
    kCommand_Init,
    kCommand_Info,
    kCommand_Erase,
    kCommand_Read,
    kCommand_Program,
    kCommand_Reboot
};

typedef struct {
    Command   commands[kMaximumArgumentCount];
    int       nCommandCount;
    char    * pAreaName;
    char    * pPaths;
    char    * pConfigFilename;
    char    * pFilename;
    char    * pDeviceName;
    char    * pPlatformArgs;
    bool      bPad;
    bool      bAutoProgram;
    bool      bAutoReboot;
    bool      bRequiredFile;
} Arguments;

typedef struct {
    char    * pName;
    Command   cmd;
    bool      bRequiredFile;
} CommandMap;

static CommandMap s_commandMap[] = {
    { "init",     kCommand_Init,       false },
    { "info",     kCommand_Info,       false },
    { "erase",    kCommand_Erase,      false },
    { "read",     kCommand_Read,       true  },
    { "program",  kCommand_Program,    true  },
    { "reboot",   kCommand_Reboot,     false }
};

static const struct option s_options[] = {
    /* long    argument          n/a short */
    { "area",  required_argument, 0, 'a' },
    { "paths", required_argument, 0, 'p' },
    { "cfg",   required_argument, 0, 'c' },
    { "file",  required_argument, 0, 'f' },
    { "dev",   required_argument, 0, 'D' },
    { "xargs", required_argument, 0, 'x' },
    { "pad",   no_argument,       0, 'P' },
    { "dap",   no_argument,       0,  1  },
    { "dar",   no_argument,       0,  2  },
    { "help",  no_argument,       0, '?' },
    { "usage", no_argument,       0, 10  }
};

static const struct optionsHelp {
    char const * pArgType;
    char const * pHelpText;
} s_optionsHelp[] = {    /* must match s_options[] above */
        { "NAME",  "flash area name (*)"                             },
        { "PATHS", ": delimited search paths for input files"        },
        { "FILE",  "configuration file, default: LTFlashConfig.json" },
        { "FILE",  "binary file"                                     },
        { "NAME",  "serial device name"                              },
        { "ARGS",  "platform-specific arguments"                     },
        {  NULL,   "pad with 0xff to end of area"                    },
        {  NULL,   "disable auto-programming features"               },
        {  NULL,   "disable auto-reboot after program"               },
        {  NULL,   "Give this help text"                             },
        {  NULL,   "Give a short usage message"                      }
};

/* Output a list of available commands, separated by a vertical bar: */
static void PrintCommandList(void) {
    CommandMap const * pCmd = s_commandMap;
    for (int nIdx = countof(s_commandMap); nIdx; --nIdx, ++pCmd) {
        printf("%s%s", pCmd->pName, nIdx > 1 ? "|" : "");
    }
}

/* Print the long usage message: */
static void Usage(void) {
    printf("Usage: rit [OPTION...] ");
    PrintCommandList();
    puts("\n");
    struct option const       * pOpt  = s_options;
    struct optionsHelp const  * pHelp = s_optionsHelp;
    for (int nIdx = countof(s_options); nIdx; --nIdx, ++pOpt, ++pHelp) {
        int n = pOpt->val >= '?' ? printf("  -%c, ", pOpt->val)
                                 : printf("      ");
        n += printf("--%s", pOpt->name);
        if (pOpt->has_arg != no_argument) {
            n += printf("=%s", pHelp->pArgType);
        }
        for (n = 30 - n; n; --n) {
            putchar(' ');
        }
        printf("%s\n", pHelp->pHelpText);
    }
    printf("\n(*) Areas are contiguous regions of flash."
           " Partitions are areas that do not overlap each other.\n");
    printf("\nMandatory or optional arguments to long"
           " options are also mandatory or optional"
           "\nfor any corresponding short options.\n\n");
}

/* Print the short usage message: */
static void ShortUsage(void) {
    int nIndent = printf(                  "Usage: rit");
    static const char * pIndentString = "\n          ";
    struct option const       * pOpt  = s_options;
    struct optionsHelp  const * pHelp = s_optionsHelp;
    int nColumn = nIndent;
    for (int nIdx = countof(s_options); nIdx; --nIdx, ++pOpt, ++pHelp) {
        if (pOpt->val > '?') {
            nColumn += printf(" [-%c", pOpt->val);
            if (pOpt->has_arg != no_argument) {
                nColumn += printf(" %s", pHelp->pArgType);
            }
            nColumn += printf("]");
        }
        if (nColumn > 65) {
            printf("%s", pIndentString);
            nColumn = nIndent;
        }
    }
    pOpt  = s_options;
    pHelp = s_optionsHelp;
    for (int nIdx = countof(s_options); nIdx; --nIdx, ++pOpt, ++pHelp) {
        if (pOpt->name) {
            nColumn += printf(" [--%s", pOpt->name);
            if (pOpt->has_arg != no_argument) {
                nColumn += printf("=%s", pHelp->pArgType);
            }
            nColumn += printf("]");
        }
        if (nColumn > 65) {
            printf("%s", pIndentString);
            nColumn = nIndent;
        }
    }
    printf("%s ", pIndentString);
    PrintCommandList();
    putchar('\n');
}

/* Process the command line, gathering operating parameters into the
   arguments structure: */
static int ParseOptions(int argc, char * const * argv, Arguments * pArgs) {
    /* Generate the getopt() short-options list.  This is an array
       of chars listing the short option characters, each followed
       by a colon if the option requires an argument: */
    char optChars[countof(s_options) * 2 + 1];
    {
        char * pOptChar = optChars;
        struct option const * pOpt = s_options;
        for (int nIdx = countof(s_options); nIdx; --nIdx, ++pOpt) {
            if (pOpt->val > 10) {
                *pOptChar++ = pOpt->val;
                if (pOpt->has_arg) {
                    *pOptChar++ = ':';
                }
            }
        }
        *pOptChar = '\0';
    }

    /* Use getopt_long() to parse out command-line options and
       their operands: */
    int nOptionIndex = 0;
    int nOptionResult = 0;
    do {
         nOptionResult = getopt_long(argc, argv, optChars, s_options, &nOptionIndex);
         switch (nOptionResult) {
         case -1: /* Ignore.  This value terminates the loop. */
                  /* fall through */

         case 0: /* Ignore.  None of the options set flags. */
             break;

         case '?':
            Usage();
            return 1;

         case 10:
            ShortUsage();
            return 1;

         default:
            break;

        case 'a':
            pArgs->pAreaName = optarg;
            break;

        case 'p':
            pArgs->pPaths = optarg;
            break;

        case 'c':
            pArgs->pConfigFilename = optarg;
            break;

        case 'f':
            pArgs->pFilename = optarg;
            break;

        case 'D':
            if (*optarg == '/') {
                pArgs->pDeviceName = optarg;
            } else {
                static char deviceName[30] = "";
                snprintf(deviceName, 30, "/dev/tty%s", optarg);
                pArgs->pDeviceName = deviceName;
            }
            break;

        case 'x':
            pArgs->pPlatformArgs = optarg;
            break;

        case 'P':
            pArgs->bPad = true;
            break;

        case 1:
            pArgs->bAutoProgram = false;
            pArgs->bAutoReboot  = false;
            break;

        case 2:
            pArgs->bAutoReboot = false;
            break;
        }
    } while (nOptionResult != -1);
    return 0;
}

static const FlashDeviceInterface * OpenDevice(Arguments * pArgs) {
    static const FlashDeviceInterface * pInterface = NULL;
    if (!pInterface) {
        pInterface = GetFlashDeviceInterface(ImageGetDeviceType(), ImageGetDeviceFamily());
        if (!pInterface) {
            printf("Device type %s and/or family %s unsupported\n", ImageGetDeviceType(), ImageGetDeviceFamily());
            return NULL;
        }
        printf("Initializing %s (%s)\n", ImageGetDeviceType(), pArgs->pConfigFilename);
        int nRtn = pInterface->Open(pArgs->pDeviceName, pArgs->bAutoProgram, pArgs->pPlatformArgs);
        if (nRtn < 0) {
            // Negative return value is -errno
            switch (-nRtn) {
            case EBUSY:
                printf("Initialization error, serial port already open!\n");
                break;
            case ETIMEDOUT:
                printf("Timeout error, is device in programming mode?\n");
                break;
            case EPROTO:
                printf("Protocol error, is device in programming mode?\n");
                break;
            case EINVAL:
                printf("Invalid argument or response, is device in programming mode?\n");
                break;
            default:
                printf("Initialization error, %s!\n", strerror(-nRtn));
                break;
            }
            pInterface = NULL;
            return NULL;
        }
    }
    return pInterface;
}

int main(int argc, char **argv) {
    /* Provide default values for some of the arguments: */
    Arguments args = {
        .pAreaName       = ImageGetDefaultAreaName(),
        .pPaths          = NULL,
        .pConfigFilename = "LTFlashConfig.json",
        .pFilename       = NULL,
        .pDeviceName     = "/dev/ttyUSB0",
        .pPlatformArgs   = "",
        .bPad            = false,
        .bAutoProgram    = true,
        .bAutoReboot     = true
    };

    /* Process the command-line arguments.  ParseOptions() will print a usage message
       to standard output if it returns nonzero: */
    if (ParseOptions(argc, argv, &args) != 0) {
        return -1;
    }

    /* ParseOptions() (getopt_long(), actually) permutes the argv array to move
       all the non-command-line-option arguments to the end of the list.  Advance
       argv to that point in the array and process any loose command-line arguments
       as potential commands.  Legitimate commands get put into the commands array: */
    for (argv += optind; *argv != NULL; ++argv) {
        CommandMap const * pCmd = s_commandMap;
        int nCmdIdx = 0;
        for (; nCmdIdx < countof(s_commandMap); ++nCmdIdx, ++pCmd) {
            if (strcmp(*argv, pCmd->pName) == 0) {                   /* found a command! */
                if (args.nCommandCount == kMaximumArgumentCount) {   /* no more space in the array */
                    fprintf(stderr, "rit: too many commands (max %d)\n", kMaximumArgumentCount);
                    Usage();
                    return -1;
                }
                args.commands[args.nCommandCount++] = pCmd->cmd;
                if (pCmd->bRequiredFile) {
                    args.bRequiredFile = true;
                }
                break;
            }
        }
        if (nCmdIdx == countof(s_commandMap)) {
            Usage();        /* advanced through the entire command */
            return -1;      /* list without finding a match.       */
        }
    }

    if (args.nCommandCount < kMinimumArgumentCount) {
        fprintf(stderr, "rit: must specify at least one command\n");
        Usage();
        return -1;
    }

    if (args.bRequiredFile && args.pFilename == NULL) {
        fprintf(stderr, "rit: requires file argument\n");
        Usage();
        return -1;
    }

    if (ImageInit(args.pConfigFilename, args.pPaths) < 0) {
        printf("[ERR] aborting...\n");
        return -1;
    }

    int nAreaIdx = ImageGetAreaIndexByName(args.pAreaName);
    if (nAreaIdx < 0) {
        printf("Area %s does not exist.\n", args.pAreaName);
        printf("[ERR] aborting...\n");
        ImageFini();
        return -1;
    }

    const FlashDeviceInterface * pInterface = NULL;

    int nRtn = -1;
    for (int nCmdIdx = 0; nCmdIdx < args.nCommandCount; nCmdIdx++) {
        nRtn = -1;
        switch (args.commands[nCmdIdx]) {
        case kCommand_Init:
            pInterface = OpenDevice(&args);
            if (pInterface) nRtn = 0;
            break;

        case kCommand_Info:
            nRtn = ImageList();
            break;

        case kCommand_Erase:
            pInterface = OpenDevice(&args);
            if (pInterface)
                nRtn = pInterface->Erase(nAreaIdx);
            break;

        case kCommand_Read:
            pInterface = OpenDevice(&args);
            if (pInterface)
                nRtn = pInterface->Read(nAreaIdx, args.pFilename, args.bAutoReboot);
            break;

        case kCommand_Program:
            pInterface = OpenDevice(&args);
            if (pInterface)
                nRtn = pInterface->Program(nAreaIdx, args.pFilename, args.bPad, args.bAutoReboot);
            break;

        case kCommand_Reboot:
            pInterface = OpenDevice(&args);
            if (pInterface) nRtn = pInterface->Reboot();
            break;

        default:
            break;
        }
        if (nRtn == 0) {
            printf("[OK]\n");
        } else {
            switch (-nRtn) {
            case EINVAL:
                printf("Invalid argument or response\n");
                break;
            case EMSGSIZE:
                printf("Unexpected message size\n");
                break;
            case EPROTO:
                printf("Protocol error\n");
                break;
            case ETIMEDOUT:
                printf("Timeout Error\n");
                break;
            default:
                break;
            }
            printf("[ERR] aborting...\n");
            break;
        }
    }

    if (pInterface) pInterface->Close();
    ImageFini();
    return nRtn;
}
