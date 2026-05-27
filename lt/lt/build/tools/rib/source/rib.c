/******************************************************************************
 * rib.c                                                     Roku Image Builder
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>

#include "lt/LTTypes.h"

#include "Image.h"
#include "Builder.h"
#include "Assets.h"

enum {
    kMinimumArgumentCount  = 1,
    kMaximumArgumentCount  = 10,
};

#define countof(x)   (sizeof(x) / sizeof(x[0]))
#define _stringify(s) #s
#define stringify(s) _stringify(s)

typedef u8 Command;
enum Command {
    kCommand_Info,
    kCommand_CheckImage,
    kCommand_BuildImage,
    kCommand_BuildPartition,
    kCommand_BuildUpdate,
    kCommand_BuildOTA,
    kCommand_BuildAssets,
};

// clang-format off
typedef struct {
    Command   commands[kMaximumArgumentCount];
    int       nCommandCount;
    char    * pAreaName;
    char    * pPaths;
    char    * pConfigFilename;
    char    * pKeyFilename;
    char    * pIFilename;
    char    * pOFilename;
    char    * pVersion;
    char    * pPlatformArgs;
    s32       nMaxSize;
    u32       nBlockSize;
    bool      bRequiredFile;
    bool      bRequiredKey;
} Arguments;

// clang-format on

typedef struct {
    char    * pName;
    Command   cmd;
    bool      bRequiredFile;
    bool      bRequiredKey;
} CommandMap;

static CommandMap s_commandMap[] = {
    { "info",      kCommand_Info,           false, false },
    { "check",     kCommand_CheckImage,     true,  false },
    { "build",     kCommand_BuildImage,     false, false },
    { "partition", kCommand_BuildPartition, false, false },
    { "update",    kCommand_BuildUpdate,    true,  true  },
    { "ota",       kCommand_BuildOTA,       false, false },
    { "assets",    kCommand_BuildAssets,    true,  false },
};

static const struct option s_options[] = {
  /* long      argument           n/a short */
    { "area",   required_argument, 0, 'a' },
    { "blksize",required_argument, 0, 'b' },
    { "paths",  required_argument, 0, 'p' },
    { "cfg",    required_argument, 0, 'c' },
    { "key",    required_argument, 0, 'k' },
    { "file",   required_argument, 0, 'f' },
    { "ofile",  required_argument, 0, 'o' },
    { "ver",    required_argument, 0, 'v' },
    { "xargs",  required_argument, 0, 'x' },
    { "help",   no_argument,       0, '?' },
    { "usage",  no_argument,       0, 10  }
};

static const struct optionsHelp {
    char const * pArgType;
    char const * pHelpText;
} s_optionsHelp[] = {
  /* must match options[] above */
    { "NAME",    "flash area name (*)"                              },
    { "BLKSIZE", "encrypted block size"                             },
    { "PATHS",   ": delimited search paths for input files"         },
    { "FILE",    "configuration file, default: LTFlashConfig.json"  },
    { "FILE",    "root firmware encryption key file"                },
    { "FILE",    "input file"                                       },
    { "FILE",    "output file"                                      },
    { "VER",     "version string"                                   },
    { "ARGS",    "platform-specific arguments"                      },
    { NULL,      "Give this help text"                              },
    { NULL,      "Give a short usage message"                       }
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
    printf("Usage: rib [OPTION...] ");
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
    int nIndent = printf(                  "Usage: rib");
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

        case 'b':
            pArgs->nBlockSize = atoi(optarg);
            break;

        case 'p':
            pArgs->pPaths = optarg;
            break;

        case 'c':
            pArgs->pConfigFilename = optarg;
            break;

        case 'k':
            pArgs->pKeyFilename = optarg;
            break;

        case 'f':
            pArgs->pIFilename = optarg;
            break;

        case 'o':
            pArgs->pOFilename = optarg;
            break;

        case 'v':
            pArgs->pVersion = optarg;
            break;

        case 'x':
            pArgs->pPlatformArgs = optarg;
            break;
        }
    } while (nOptionResult != -1);
    return 0;
}

int main(int argc, char **argv) {
    /* Provide default values for some of the arguments: */
    Arguments args = {
        .pAreaName       = NULL,
        .pPaths          = NULL,
        .pConfigFilename = "LTFlashConfig.json",
        .pIFilename      = NULL,
        .pOFilename      = NULL,
        .pVersion        = "",
        .pPlatformArgs   = "",
        .nBlockSize      = 0,
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
                    fprintf(stderr, "rib: too many commands (max %d)\n", kMaximumArgumentCount);
                    Usage();
                    return -1;
                }
                args.commands[args.nCommandCount++] = pCmd->cmd;
                if (pCmd->bRequiredFile) {
                    args.bRequiredFile = true;
                }
                if (pCmd->bRequiredKey) {
                    args.bRequiredKey = true;
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
        fprintf(stderr, "rib: must specify at least one command\n");
        Usage();
        return -1;
    }

    if (args.bRequiredFile && args.pIFilename == NULL) {
        fprintf(stderr, "rib: requires file argument\n");
        Usage();
        return -1;
    }

    if (args.bRequiredKey && args.pKeyFilename == NULL) {
        fprintf(stderr, "rib: requires key argument\n");
        Usage();
        return -1;
    }

    if (ImageInit(args.pConfigFilename, args.pPaths) < 0) {
        printf("[ERR] aborting...\n");
        return -1;
    }

    int nAreaIdx = -1;
    if (args.pAreaName) {
        nAreaIdx = ImageGetAreaIndexByName(args.pAreaName);
        if (nAreaIdx < 0) {
            printf("Area %s does not exist.\n", args.pAreaName);
            printf("[ERR] aborting...\n");
            ImageFini();
            return -1;
        }
    }

    int nRtn = -1;
    for (unsigned nCmdIdx = 0; nCmdIdx < args.nCommandCount; nCmdIdx++) {
        nRtn = -1;
        switch (args.commands[nCmdIdx]) {
        case kCommand_Info:
            nRtn = ImageList();
            break;

        case kCommand_CheckImage:
            nRtn = BuilderCheckImage(nAreaIdx, args.pIFilename);
            break;

        case kCommand_BuildImage:
            nRtn = BuilderCreateFullImage(args.pOFilename);
            break;

        case kCommand_BuildPartition:
            nRtn = BuilderCreatePartitionTable(args.pOFilename);
            break;

        case kCommand_BuildUpdate:
            nRtn = BuilderCreateUpdateImage(args.pOFilename,
                                            args.pIFilename,
                                            args.pKeyFilename,
                                            args.pVersion,
                                            args.pPlatformArgs,
                                            args.nBlockSize);
            break;

        case kCommand_BuildOTA:
            nRtn = BuilderCreateOTAData(args.pOFilename, true, false);
            break;

        case kCommand_BuildAssets:
            nRtn = AssetsCreateImage(nAreaIdx, args.pIFilename, args.pOFilename);
            break;

        default:
            break;
        }
        if (nRtn == 0) {
            printf("[OK]\n");
        } else {
            printf("[ERR] aborting...\n");
            break;
        }
    }

    ImageFini();
    return nRtn;
}
