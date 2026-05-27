/******************************************************************************
 * Arbolate.c
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>

#include <lt/core/LTCore.h>
#include <lt/core/LTArray.h>
#include <lt/utility/jsonparser/LTUtilityJsonParser.h>
#include <lt/utility/byteops/LTUtilityByteOps.h>

#include "ArbolatorAmbles.h"

/*___________
  #defines */
#define ParseBail(f, ...) { ReportParseAbort(f, __VA_ARGS__); goto bailure; }
#define ErrorBail(f, ...) { ReportAbort(f, __VA_ARGS__); goto bailure; }

DEFINE_LTLOG_SECTION("arbolator")
#define USE_DLOG    0
#if USE_DLOG
  #define DLOG LTLOG
#else
  #define DLOG LTLOG_LOGNULL
#endif

/*____________
  constants */
enum {
    kJsonIncludeMaxDepth        = 3,
    kArbolatedMaxDepth          = 32,
    kArbolatedTreeBytesPerLine  = 12,
    kArbolatedTreeMaxBytes      = 16384
        //  max byte size of arbolated trees shall not exceed kArbolateedTreeMaxBytes (arbitrary choice, expected not to exceed).
        //  if we end up sticking large binary data in arbolated trees then we'll parce the json in a first pss counting the size of everythign
        //  and do away with this content
};

/*___________
  typedefs */
typedef struct ArbolWriter {
    u32 nCurrentOffset;
    u32 nMaxSize;
    u8 *pBytes;
    u32 entryIndex[kArbolatedMaxDepth];
    u32 parentOffset[kArbolatedMaxDepth];
    u32 currentParentIndex;
    u32 previousSiblingOffset;
} ArbolWriter;

typedef struct IncludeSegment {
    u32 nInputPreambleOffset;
    u32 nInputPreambleLength;
    u32 nInputPreambleSkip;
    u32 nJsonPreambleLength;
    u32 nJsonSegmentLength;
    u32 nJsonPostambleLength;
    char jsonIncludeDirectivePath[PATH_MAX];
    char fsIncludeFilePath[PATH_MAX];
    char * includeFileText;
} IncludeSegment;

typedef struct ExpandIncludesClientData {
    char inputFileDocRoot[PATH_MAX];
    char inputFileBasename[256];
    u32  inputPass;
    u32  outputPass;
    LTArray *segmentsOfPass[kJsonIncludeMaxDepth+1];
    LTArray *segmentsOfInputFile;
    LTArray *segmentsOfOutputFile;
} ExpandIncludesClientData;

typedef struct WorkingData {
    const char              *parsePath;         /* for ReportParseAbort to report the proper file */
    s32                      parseLineOffset;   /* and line of the file being parsed during inclusion processing */
    u32                      inputFileLength;
    char                    *inputFileMem;
    FILE                    *inputFile;
    FILE                    *outputFile;
    char                     inputFilePath[PATH_MAX];
    const char              *outputFilePath;
    const char              *jsonIntermediateSourceDir;
    LTUtilityByteOps        *utilityByteOps;
    LTUtilityJsonParser     *parser;
    bool                     preserveOutputFile;
} WorkingData;

/*___________________
  static variables */
WorkingData s_workingData;

/*_______________________
  Forward Declarations */
static bool ProcessJsonIncludes(void);
static bool GenerateProductConfigMakefileLibraryList(void);
static bool GenerateDeviceConfigMakefileLibraryList(void);
static bool GenerateArbolatedResourceTree(void);

static void ReportAbort(const char *format, ...);
static void ReportParseAbort(const char *format, ...);

static void WriteHeaderComment(FILE * pFile, const char * pFilePath, bool useMakefileComment);
static void WriteTrailerComment(FILE * pFile, bool useMakefileComment);

    //                   // Including from a library's private source tree like this
   //___________________// is illegal and only permitted here until we figure out where
  // Illegal include   // to locate common ResourceTree internal defs
  /*\\\\\\\\\\\\\\\\\*/#include "../../../../source/lt/core/LTResourceTreeImpl.h"
 /* ............... */
/*_________________*/

/*_____________________
   function main() ! */
int main(int argc, const char ** argv) {
    bool bGenProductConfig = (argc == 5) && (0 == lt_strcmp(argv[1], "--genproductconfig"));
    bool bGenDeviceConfig  = (argc == 5) && (0 == lt_strcmp(argv[1], "--gendeviceconfig"));
    bool bGenResourceTree  = (argc == 5) && (0 == lt_strcmp(argv[1], "--genresourcetree"));

    if (! (bGenDeviceConfig || bGenProductConfig || bGenResourceTree)) { lt_consoleputstring("\n");

        lt_consoleputstring("usage: arbolator --gen<deviceconfig|productconfig|resourcetree> <inputfile.json> <outputfile.[mk|.h]> <json-tmp-obj-dir>\n\n");

        lt_consoleputstring("examples:\n");
        lt_consoleputstring("       arbolator --gendeviceconfig   <platform_root>/build/platform/<variant>/LTDeviceConfig.json\n");
        lt_consoleputstring("                                     <target_objdir>/LTDeviceConfig/arbolated/build/LTDeviceConfig-LibraryIncludes.mk\n");
        lt_consoleputstring("                                     <target_objdir>/LTDeviceConfig/arbolated/json-source\n\n");

        lt_consoleputstring("       arbolator --genproductconfig  <product__root>/build/product/<variant>/LTProductConfig.json\n");
        lt_consoleputstring("                                     <target_objdir>/LTProductConfig/arbolated/build/LTProductConfig-LibraryIncludes.mk\n");
        lt_consoleputstring("                                     <target_objdir>/LTProductConfig/arbolated/json-source\n\n");

        lt_consoleputstring("       arbolator --genresourcetree   <product__root>/source/mylibrary/resources/ResourceTree.json\n");
        lt_consoleputstring("                                     <target_objdir>/mylibrary/arbolated/include/MyLibraryResourceTree.h\n");
        lt_consoleputstring("                                     <target_objdir>/mylibrary/arbolated/json-source\n\n");
        return -1;
    }

    lt_memset(&s_workingData, 0, sizeof(s_workingData));
    lt_strncpyTerm(s_workingData.inputFilePath, argv[2], sizeof(s_workingData.inputFilePath));
    s_workingData.outputFilePath = argv[3];
    s_workingData.jsonIntermediateSourceDir = argv[4];

    // open our libraries and create our objects
    s_workingData.utilityByteOps = lt_openlibrary(LTUtilityByteOps);
    s_workingData.parser = lt_createobject(LTUtilityJsonParser);

    // FIRST: ProcessJsonIncludes - takes original file and expands its includes in multiple passes and adjusts s_workingData.inputFilePath as necessary
    //                              ProcessJsonIncludes will print the error so we can just goto bailure
    if (! ProcessJsonIncludes()) goto bailure;

    // open input and output files and stat the input file to get it's size and mallocate a buffer of that size and read input file contents into the buffer
    struct stat file_stat;
    if (0 != stat(s_workingData.inputFilePath, &file_stat))                                     ErrorBail("failed to stat input file %s", s_workingData.inputFilePath);
    if (0 == (s_workingData.inputFileLength = (u32)file_stat.st_size))                          ErrorBail("empty input file %s", s_workingData.inputFilePath);
    if (NULL == (s_workingData.inputFileMem = lt_malloc(s_workingData.inputFileLength + 1)))    ErrorBail("failed to allocate %d bytes for input file contents", (int)(u32)file_stat.st_size);
    if (NULL == (s_workingData.inputFile = fopen(s_workingData.inputFilePath,  "rb")))          ErrorBail("failed to open input file %s", s_workingData.inputFilePath);
    if (s_workingData.inputFileLength != fread(s_workingData.inputFileMem, 1,
        s_workingData.inputFileLength, s_workingData.inputFile))                                ErrorBail("failed to read input file %s", s_workingData.inputFilePath);
    s_workingData.inputFileMem[s_workingData.inputFileLength] = 0;
    if (NULL == (s_workingData.outputFile = fopen(s_workingData.outputFilePath, "wb")))         ErrorBail("failed to open output file %s", s_workingData.outputFilePath);
    s_workingData.parsePath = s_workingData.inputFilePath;
    s_workingData.parseLineOffset = 0;

    // SECOND: VALIDATE THE INPUT to check for and notify the user of helpful parsing error information (this will ensure the json is structurally sound before we attempt arbolation
    if (! s_workingData.parser->API->ValidateJson(s_workingData.parser, s_workingData.inputFileMem)) ParseBail(NULL, NULL);

    // THIRD: DO THE WORK
    // write a comment header to the output file, Perform the selected operation to generate and write output file contents, write a comment footer to the output file
    WriteHeaderComment(s_workingData.outputFile, s_workingData.outputFilePath, !bGenResourceTree);
    s_workingData.preserveOutputFile = bGenDeviceConfig ? GenerateDeviceConfigMakefileLibraryList() : (bGenProductConfig ? GenerateProductConfigMakefileLibraryList() : GenerateArbolatedResourceTree());
    if (s_workingData.preserveOutputFile) WriteTrailerComment(s_workingData.outputFile, !bGenResourceTree);

bailure:
    if (s_workingData.inputFileMem)     lt_free(s_workingData.inputFileMem);
    if (s_workingData.inputFile)        fclose(s_workingData.inputFile);
    if (s_workingData.outputFile)       { fclose(s_workingData.outputFile); if (!s_workingData.preserveOutputFile) unlink(s_workingData.outputFilePath); }
    if (s_workingData.parser)           lt_destroyobject(s_workingData.parser);
    if (s_workingData.utilityByteOps)   lt_closelibrary(s_workingData.utilityByteOps);
    return s_workingData.preserveOutputFile ? 0 : -1;
}


/* _______________________________________________________________________________________________________
 ' OPERATION ONE: arbolator --genproductconfig                                                '          /
 '                                                                                            '         /
 '   ENTRY POINT: GenerateProductConfigMakefileLibraryList()                                  '        /
 '       MISSION: Parse the currently building product variant's LTProductConfig.json file    '       /
 '                to determine the build libraries for this product variant and write them    '      /
 '                into an intermediate makefile, LTProductConfig_LibraryIncludes.mk,  that    '     /
 '                parameterizes the current product variant build.                            '    /
 '                                                                                            '   /
 '         PARSE: LTProductConfig.json                                                        '  /
 '      GENERATE: LTProductConfig_LibraryIncludes.mk                                          ' /
 '                                                                                            ;/
 `'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''*/
static bool AddProductBuildLibs_ParseCallback(const char *key, LTUtilityJsonParser_Value *value, void *cd) {
    if (value->type == kLTUtilityJsonParser_ValueType_String) {
        *((u32 *)cd) = *((u32 *)cd) + 1;
        fprintf(s_workingData.outputFile, "  LT_BUILD_PRODUCT_LIBRARIES += %s\n", value->string);
    }
    return true;
}

static bool GenerateProductConfigMakefileLibraryList(void) {
// {"config":{"product":{"genesis":{"lib":"LTSystemShell"},
//                         "build":{"libs":["LTNetCore","LTNetEcho"]}
//                      }}}
    // 1. FindArray /config/product/build/libs, then parse it, writing lib names encountered in callback to .mk file being generated
    u32 nLibCount = 0;
    u32 nOffset = s_workingData.parser->API->FindArray(s_workingData.parser, s_workingData.inputFileMem, "/config/product/build/libs");
    if (nOffset)  s_workingData.parser->API->ParseJson(s_workingData.parser, s_workingData.inputFileMem + nOffset, &AddProductBuildLibs_ParseCallback, &nLibCount);
    // if (! nLibCount) ParseBail("no /config/product/build/libs", NULL); /* DRW 05-Mar-2026 : allow 0 build libs (allow for only genesis lib) */

    // 2. get the genesis library name from /config/product/genesis/lib and write it to the .mk file being generated
    LTUtilityJsonParser_Value value;
    s_workingData.parser->API->GetValue(s_workingData.parser, s_workingData.inputFileMem, "/config/product/genesis/lib", &value);
    if (value.type != kLTUtilityJsonParser_ValueType_String) ErrorBail("No /config/product/genesis/lib found in %s", s_workingData.inputFilePath);

    fprintf(s_workingData.outputFile, "\n  LT_PRODUCT_GENESIS_LIBRARY := %s\n", value.string);
    return true;

bailure:
    return false;
}

/* _______________________________________________________________________________________________________
 ' OPERATION TWO: arbolator --gendeviceconfig                                                '           /
 '                                                                                           '          /
 '   ENTRY POINT: GenerateDeviceConfigMakefileLibraryList()                                  '         /
 '       MISSION: Parse the currently building platform variant's LTDeviceConfig.json file   '        /
 '                to determine the device and driver libraries included in this platform     '       /
 '                variant and write the list of libraries into an intermediate makefile,     '      /
 '                LTDeviceConfig_LibraryIncludes.mk, that parameterizes the current          '     /
 '                platform variant build.                                                    '    /
 '                                                                                           '   /
 '         PARSE: LTDeviceConfig.json                                                        '  /
 '      GENERATE: LTDeviceConfig_LibraryIncludes.mk                                          ' /
 '                                                                                           '/
 `'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''`
*/
static bool ArrayInsertUnique(LTArray *array, const char *string) {
    bool bRetVal = true;
    if (-1 == array->API->Find(array, array->API->CompareCString, string, (void*)kLTArrayCompare_Ascending)) {
        char *unique = lt_strdup(string);
        if (unique) {
           if (-1 == array->API->InsertSorted(array, array->API->CompareCString, unique, (void*)kLTArrayCompare_Ascending)) {
               bRetVal = false;
               lt_free(unique);
           }
        }
        else bRetVal = false;
    }
    return bRetVal;
}

static bool GenerateDeviceConfigMakefileLibraryList(void) {
//
//      old method:
//      {"config":{"device":[     {"lib":"LTDeviceFlash", "driver":[{"lib":"LinuxDriverFlash"}]},
//                                {"lib":"LTDeviceWiFi",  "driver":[{"lib":"LinuxDriverWiFi"}]}],
//                    "net":{"transport":[{"lib":"LinuxDriverNetIp"}]} }},
//      new method:
/* -- File: DeviceConfig.json
 *     { -- TOP LEVEL: 1 required element, "device": [ARRAY of OBJECTS {}, {}, {} ,...]; one optional element, "extra-libs": [ARRAY of STRINGS "", "", ...]
 *         "device": [ -- required array of device specification objects, each with 2 required elements { class: "STRING", "unit": [ARRAY of OBJECTS {}, {}, ...] }
 *             { -- device 0
 *                 "class"      : "<device-class>",                -- required device class name (aka object/api name), e.g.  "LTDeviceWatchdog"; note: must contain substring "Device" in name
 *                *"api"        : "passthru",                      -- optional, if the device api is a passthru to the driver; will elide device class from being built
 *                *"device-lib" : "<device-lib-name>"              -- optional name of lib containing device object impl; RECOMMENDATION: do not specify; will use device-class as lib name
 *                *"driver-api" : "<driver-api-name>",             -- optional driver api name; use device-class to bypass device object and go driver direct; if unspecified, replaces device class substring "Device" with "Driver" for driver api name, e.g. LTDeviceWatchdog -> LTDriverWatchdog
 *                *"config"     : {<class specific config data>}   -- optional device class configuration specific to device class operation, not specific nor relevant to device driver unit configuration
 *                 "unit        : [                                -- required array of [at least 1] device unit objects, each with 2 required elements { name: "STRING", "driver": "STRING" }
 *                     { -- unit 0 (default unit)
 *                         "name"       : "<unit-name>",           -- short unit name, e.g. Wlan0, max 15 characters
 *                         "driver"     : "<driver-object-name>",  -- the name of the driver object specialization that operates this unit, e.g. "s91xDriverWiFi"
 *                        *"driver-lib" : "<driver-lib-name>",     -- optional name of library that contains driver object implementation, if unspecified, same as driver, e.g. "s91xDriverWiFi"
 *                        *"config"     : {<unit specific data>}   -- optional unit specific configuration data
 *                     },
 *                    *{ -- unit 1, (additional units optional)
 *                         "name"       : "<unit-name>",           -- short unit name, e.g., Wlan1, max 15 characters
 *                         "driver"     : "<driver-object-name>",  -- may be same driver object used for other units (e.g. "s91xDriverWiFi"), or may be different object
 *                        *"driver-lib" : "<driver-lib-name>",     -- optional name of library that contains driver object implementation, if unspecified, same as driver, e.g. "s91xDriverWiFi"
 *                        *"config"     : {<unit specific data>}   -- optional unit specific configuration data
 *                     }
 *                 ]
 *             }
 *         ],
 *
 *        *"extra-libs": [     -- an array of names of libraries for build inclusion required by driver libaries specified explicitly or implitly in the preceeding devices array, e.g. LTThirdPartLWIP.
 *                             -- Note: do not put platform mastering nor vendor sdk libraries here.  Those go in LT_PLATFORM_MASTERING_PROJECTS in Makefile.config.
 *                             -- Do not put libraries used by drivers specified herein in LTProductConfig.json.  e.g. Don't put LTThirdPartyLWIP in an LTProductConfig.json, put it in "extra-libs" here.
 *             "<lib name 1>",
 *            *"<lib name 2>",
 *            *"<lib name 3>"
 *         ],
 *     }
 * ____________________________________________________________________________
 */

    // make 2 arrays, one for device libraries and one for driver libraries, put net transport libraries in driver libraries, put extra libs in driver libraries
    bool bRetVal = false;
    LTArray *deviceLibraries = lt_createobject(LTArray);
    LTArray *driverLibraries = lt_createobject(LTArray);
    if (NULL == deviceLibraries || NULL == driverLibraries) ErrorBail("GenerateDeviceConfigMakefileLibraryList failed to create LTArray objects", NULL);

    // 1.  find the offset of the /config/device array and iterate over /%d/lib for device libraries and /%d/driver/%d/lib for driver libraries
    u32 deviceArrayOffset = s_workingData.parser->API->FindArray(s_workingData.parser, s_workingData.inputFileMem, "/config/device");
    if (0 == deviceArrayOffset) ParseBail("No /config/device array found", NULL);
    const char * pDeviceArray = s_workingData.inputFileMem + deviceArrayOffset;
    int deviceIndex, driverIndex; char key[48]; LTUtilityJsonParser_Value value; enum { kIndexUpperBound = 1024 };
    for (deviceIndex = 0; deviceIndex <= kIndexUpperBound; deviceIndex++) {
        lt_snprintf(key, sizeof(key), "/%d/lib", deviceIndex);
        s_workingData.parser->API->GetValue(s_workingData.parser, pDeviceArray, key, &value);
        if (value.type != kLTUtilityJsonParser_ValueType_String) break;
        if (! ArrayInsertUnique(deviceLibraries, value.string)) ErrorBail("GenerateDeviceConfigMakefileLibraryList failed to add device library array element", NULL);
        for (driverIndex = 0; driverIndex <= kIndexUpperBound; driverIndex++) {
            lt_snprintf(key, sizeof(key), "/%d/driver/%d/lib", deviceIndex, driverIndex);
            s_workingData.parser->API->GetValue(s_workingData.parser, pDeviceArray, key, &value);
            if (value.type != kLTUtilityJsonParser_ValueType_String) break;
            if (! ArrayInsertUnique(driverLibraries, value.string)) ErrorBail("GenerateDeviceConfigMakefileLibraryList failed to add driver library array element", NULL);
        }
    }

    // 2. find the offset of the /device array and iterate over /%d/device-lib then /%d/class for device libraries and /%d/unit/%d/driver-lib then /%d/unit/%d/driver for driver libraries
    deviceArrayOffset = s_workingData.parser->API->FindArray(s_workingData.parser, s_workingData.inputFileMem, "/device");
    pDeviceArray = deviceArrayOffset ? s_workingData.inputFileMem + deviceArrayOffset : NULL;
    for (deviceIndex = 0; deviceIndex <= kIndexUpperBound; deviceIndex++) {
        lt_snprintf(key, sizeof(key), "/%d/device-lib", deviceIndex);
        s_workingData.parser->API->GetValue(s_workingData.parser, pDeviceArray, key, &value);
        if (value.type != kLTUtilityJsonParser_ValueType_String) {
            lt_snprintf(key, sizeof(key), "/%d/class", deviceIndex);
            s_workingData.parser->API->GetValue(s_workingData.parser, pDeviceArray, key, &value);
            if (value.type != kLTUtilityJsonParser_ValueType_String) break;
            char * pDeviceName = lt_strdup(value.string);
            lt_snprintf(key, sizeof(key), "/%d/api", deviceIndex);
            s_workingData.parser->API->GetValue(s_workingData.parser, pDeviceArray, key, &value);
            if (value.type != kLTUtilityJsonParser_ValueType_String || (0 != lt_strcmp(value.string, "passthru"))) {
                if (! ArrayInsertUnique(deviceLibraries, pDeviceName)) {
                    lt_free(pDeviceName);
                    ErrorBail("GenerateDeviceConfigMakefileLibraryList failed to add device library array element", NULL);
                }
            }
            lt_free(pDeviceName);
        }
        else {
            if (! ArrayInsertUnique(deviceLibraries, value.string)) ErrorBail("GenerateDeviceConfigMakefileLibraryList failed to add device library array element", NULL);
        }
        for (driverIndex = 0; driverIndex <= kIndexUpperBound; driverIndex++) {
            lt_snprintf(key, sizeof(key), "/%d/unit/%d/driver-lib", deviceIndex, driverIndex);
            s_workingData.parser->API->GetValue(s_workingData.parser, pDeviceArray, key, &value);
            if (value.type != kLTUtilityJsonParser_ValueType_String) {
                lt_snprintf(key, sizeof(key), "/%d/unit/%d/driver", deviceIndex, driverIndex);
                s_workingData.parser->API->GetValue(s_workingData.parser, pDeviceArray, key, &value);
                if (value.type != kLTUtilityJsonParser_ValueType_String) break;
            }
            if (! ArrayInsertUnique(driverLibraries, value.string)) ErrorBail("GenerateDeviceConfigMakefileLibraryList failed to add driver library array element", NULL);
        }
    }

    // 3. find the transport drivers in /config/net/transport/%d/lib
    for (driverIndex = 0; driverIndex < kIndexUpperBound; driverIndex++) {
        lt_snprintf(key, sizeof(key), "/config/net/transport/%d/lib", driverIndex);
        s_workingData.parser->API->GetValue(s_workingData.parser, s_workingData.inputFileMem, key, &value);
        if (value.type != kLTUtilityJsonParser_ValueType_String) break;
        if (! ArrayInsertUnique(driverLibraries, value.string)) ErrorBail("GenerateDeviceConfigMakefileLibraryList failed to add net transport library array element", NULL);
    }

    // 4. find the extra libs in /config/net/transport/%d/lib
    for (driverIndex = 0; driverIndex < kIndexUpperBound; driverIndex++) {
        lt_snprintf(key, sizeof(key), "/extra-libs/%d", driverIndex);
        s_workingData.parser->API->GetValue(s_workingData.parser, s_workingData.inputFileMem, key, &value);
        if (value.type != kLTUtilityJsonParser_ValueType_String) break;
        if (! ArrayInsertUnique(driverLibraries, value.string)) ErrorBail("GenerateDeviceConfigMakefileLibraryList failed to add extra-libs library array element", NULL);
    }

    // 5. write out the libraries
    u32 numDevices = deviceLibraries->API->GetCount(deviceLibraries), numDrivers = driverLibraries->API->GetCount(driverLibraries);
    if (numDevices) {
        fprintf(s_workingData.outputFile, "%s", "# DEVICE LIBRARIES\n");
        for (u32 libIndex = 0; libIndex < numDevices; libIndex++) fprintf(s_workingData.outputFile, "  LT_BUILD_DEVICE_LIBRARIES %c= %s\n", libIndex ? '+' : ':', (const char *)deviceLibraries->API->Get(deviceLibraries, libIndex, NULL));
    }
    if (numDrivers) {
        fprintf(s_workingData.outputFile, "%s", "\n# DRIVER LIBRARIES\n");
        for (u32 libIndex = 0; libIndex < numDrivers; libIndex++) fprintf(s_workingData.outputFile, "  LT_BUILD_DRIVER_LIBRARIES %c= %s\n", libIndex ? '+' : ':', (const char *)driverLibraries->API->Get(driverLibraries, libIndex, NULL));
    }

    bRetVal = true;
bailure:
    if (deviceLibraries) { LTArray_RemoveAndFreeAll(deviceLibraries); lt_destroyobject(deviceLibraries); }
    if (driverLibraries) { LTArray_RemoveAndFreeAll(driverLibraries); lt_destroyobject(driverLibraries); }
    return bRetVal;
}

/* _________________________________________________________________________________________________________________
 ' OPERATION THREE: arbolator --genresourcetre                                              '                      /
 '                                                                                          '                     /
 ' ENTRY POINT: GenerateArbolatedResourceTree()                                             '                    /
 '     MISSION: Parse a library's <lib_source_dir>/resources/ResourceTree.json file         '                   /
 '              and turn the entire json contents into a binary "arbolated tree",           '                  /
 '              that is to say a compact tree whose nodes are arranged in the same          '                 /
 '              structure as the json, but with binary relative pointers linking them       '                /
 '              (actually size offsets from 0, from container, or from self),               '               /
 '              with size and type of each value identified with a compact binary encoding. '              /
 '              Supported Json types for arbolation are strings, s64 integers, objects,     '             /
 '              and arrays. A binary data type may be encoded in a Json text value if the   '            /
 '              text value begins with '\b' as its 1st character, with the rest being ascii '           /
 '              hex character values, e.g. the json text: "meal": "\bDEADBEEFF0D199E4"      '          /
 '              would be converted from its string form into the binary bytes               '         /
 '              DEADBEEFF0D199E4 keyed by the same key, "meal" in the arbolated tree.       '        /
 '              A notable feature of arbolated trees is that they are placed in read-only   '       /
 '              memory (flash) and the LTCore access functions that operate on them         '      /
 '              return pointers to the strings and binary data directly therein, so as not  '     /
 '              consume memory or processor cycles by copying them into RAM when accessing. '    /
 '                                                                                          '   /
 '       PARSE: <your lib source dir>/resources/ResourceTree.json                           '  /
 '    GENERATE: YourLibName-ResourceTree.h  (arbolated tree in static const byte array)     ' /
 '                                                                                          '/
 `''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''`
*/
static void OutputArbolatedLine(u8 * pBytes, u32 nNumBytes, bool bMoreComing) {
    if (nNumBytes) {
        fprintf(s_workingData.outputFile, "    0x%02x", *pBytes++); nNumBytes--;
        while (nNumBytes--) fprintf(s_workingData.outputFile, ", 0x%02x", *pBytes++);
        fprintf(s_workingData.outputFile, bMoreComing ? ",\n" : "\n");
    }
}

static bool WriteBytes(ArbolWriter *writer, u8 * pBytes, u32 nNumBytes) {
    if (writer->nCurrentOffset + nNumBytes <= writer->nMaxSize) {
        if (nNumBytes < 8) while (nNumBytes--) writer->pBytes[writer->nCurrentOffset++] = *pBytes++;
        else {
            lt_memcpy(writer->pBytes + writer->nCurrentOffset, pBytes, nNumBytes);
            writer->nCurrentOffset += nNumBytes;
        }
        return true;
    }
    else return false;
}

static bool Write16(ArbolWriter *writer, u16 value) {
    DLOG("Write16", "offset(%d) = 0x%04X\n", (int)writer->nCurrentOffset, (int)value);
    return WriteBytes(writer, (u8 *)&value, 2);
}

static bool Write8(ArbolWriter *writer, u8 value) {
    DLOG("Write08", "offset(%d) = 0x%02X\n", (int)writer->nCurrentOffset, (int)value);
    return WriteBytes(writer, &value, 1);
}
static void ReplaceBytes(ArbolWriter *writer, u32 offset, u8 * pBytes, u32 nNumBytes) {
    while (nNumBytes--) writer->pBytes[offset++] = *pBytes++;
}
static void Replace16(ArbolWriter *writer, u32 offset, u16 value) {
    DLOG("Replace16", "offset(%d) = 0x%04X\n", (int)offset, (int)value);
    ReplaceBytes(writer, offset, (u8 *)&value, 2);
}

static bool AddResourceTreeEntry(ArbolWriter *writer, const char * key, u8 type) {
    // array elements don't have a key - tag them with their index instead
    if (key == NULL) {
        static char keyIndexBuf[10];
        lt_u32toString(writer->entryIndex[writer->currentParentIndex], keyIndexBuf, kLTStdlib_FormatFlags_LeftJustify | (sizeof(keyIndexBuf) - 1));
        key = keyIndexBuf;
    }
    DLOG("add", "AddResourceTreeEntry(key: %p \"%s\")\n", key, key);
    if (lt_strlen(key) > 254)                           ParseBail("illegal key length > 254 characters", NULL);
    u32 currentEntryOffset = writer->nCurrentOffset;
    u32 offset = kResourceTree_NoSuchEntry;
    u8  nameSize = lt_strlen(key) + 1;

    // write the fixed part of the ResourceTreeEntry header
    const char * pError = "Exceeded max arbolation size";
    if (! Write16(writer, kResourceTree_NoSuchEntry))   ParseBail(pError, NULL);
    if (! Write8(writer, type))                         ParseBail(pError, NULL);
    if (! Write8(writer, nameSize))                     ParseBail(pError, NULL);

    /* Write the variable part of the ResourceEntry header */
    if ((type == kResourceTreeEntryType_Object) ||
        (type == kResourceTreeEntryType_Array)) {
        /* this is a container, add the offset for its first child, placeheld with kResourceTree_NoSuchEntry */
        if (! Write16(writer, kResourceTree_NoSuchEntry)) ParseBail(pError, NULL);
    }
    DLOG("WriteKey", "offset(%d), size(%d), key(\"%s\")\n", (int)writer->nCurrentOffset, (int)nameSize, key);
    if (! WriteBytes(writer, (u8 *)key, nameSize)) ParseBail(pError, NULL);

    /* mark the offset of this resoruce tree entry relative to its previous sibling if it has one, or relative
       to its parent entry if it has one */
    /* if we have a previous sibling, set it's next offset to the difference between our offset and his offset */
    if (writer->previousSiblingOffset != kResourceTree_NoSuchEntry) {
        offset = currentEntryOffset - writer->previousSiblingOffset;
        Replace16(writer, writer->previousSiblingOffset, (u16)offset);
    }
    else if (writer->currentParentIndex) {
        offset = writer->parentOffset[writer->currentParentIndex] + 2 + 1 + 1;
        u16 val = Read16(writer->pBytes, offset);
        //LT_ASSERT(Read16(writer->pBytes, offset) == kResourceTree_NoSuchEntry);
        writer->previousSiblingOffset = currentEntryOffset - offset; /* borrow writer->previousSiblingOffset */
        Replace16(writer, offset, (u16)writer->previousSiblingOffset);
        LT_ASSERT(val == kResourceTree_NoSuchEntry); LT_UNUSED(val);
    }
    ++writer->entryIndex[writer->currentParentIndex];
    writer->previousSiblingOffset = currentEntryOffset;
    return true;
bailure:
    return false;
}

static bool MakeArbolatedTreeParseCallback(const char *key, LTUtilityJsonParser_Value *value, void *clientData) {
    bool bRetVal = false;
    u8 *pBytes = NULL;
    ArbolWriter *writer = (ArbolWriter *)clientData;
    DLOG("ParseCallback", "currentOffset(%d), parentIndex(%d), parentOffset[0](%d), parentOffset[index](%d)\n",
        (int)writer->nCurrentOffset, (int)writer->currentParentIndex, (int)writer->parentOffset[0], (int)writer->parentOffset[writer->currentParentIndex]);
    DLOG("ParseCallback", "key(\"%s\"), value.type = %s\n", key ? key : "<NULL>", s_workingData.parser->API->ValueTypeToString(value->type));
    switch (value->type) {
        case kLTUtilityJsonParser_ValueType_ArrayEntry:
        case kLTUtilityJsonParser_ValueType_ObjectEntry:
            if (writer->currentParentIndex == (kArbolatedMaxDepth-1)) ParseBail("arbolation exceeds max depth of: %d", (int)kArbolatedMaxDepth);
            // don't note the entry of the first object
            if (writer->parentOffset[0] == 0) {
                DLOG("ParseCallback", "First %s\n", s_workingData.parser->API->ValueTypeToString(value->type));
                writer->parentOffset[0] = sizeof(ResourceTreeHeader);
            }
            else {
                /* entrez vouz */
                u16 currentOffset = writer->nCurrentOffset;
                u8 nodeType = (value->type == kLTUtilityJsonParser_ValueType_ArrayEntry) ? kResourceTreeEntryType_Array : kResourceTreeEntryType_Object;
                DLOG("ParseCallback", "Write depth %lu\n", LT_Pu32(writer->currentParentIndex + 1));
                if (! AddResourceTreeEntry(writer, key, nodeType)) goto bailure;  // AddResourceTreeEntry prints the error
                writer->currentParentIndex++;
                writer->parentOffset[writer->currentParentIndex] = currentOffset;
                writer->entryIndex[writer->currentParentIndex] = 0;
                writer->previousSiblingOffset = kResourceTree_NoSuchEntry;
            }
            break;
        case kLTUtilityJsonParser_ValueType_ArrayExit:
        case kLTUtilityJsonParser_ValueType_ObjectExit:
            DLOG("ParseCallback", "Exit  depth %lu\n", LT_Pu32(writer->currentParentIndex));
            if (writer->currentParentIndex > 0) {
                writer->previousSiblingOffset = writer->parentOffset[writer->currentParentIndex];
                writer->currentParentIndex--;
            }
            else {
                /* only come out of object 0 one time */
                if (writer->parentOffset[0]) writer->parentOffset[0] = 0;
                else ParseBail("unbalanced json", NULL);
            }
            break;
        case kLTUtilityJsonParser_ValueType_Integer:
            {   DLOG("ParseCallback", "value.integer = %lld\n", LT_Pu64(value->integer));
                if (value->integer < 0) {
                    value->integer = 0 - value->integer;
                    value->type = kLTUtilityJsonParser_ValueType_True; /* to mark we were negative */
                }
                u8 type = kResourceTreeEntryType_Integer32;
                if (value->integer < 0x10000) type = (value->integer < 0x100) ? kResourceTreeEntryType_Integer8 : kResourceTreeEntryType_Integer16;
                else if (value->integer >= 0x100000000) type = kResourceTreeEntryType_Integer64;
                if (value->type == kLTUtilityJsonParser_ValueType_True) type |= kResourceTreeEntryType_NegationMask;
                if (! AddResourceTreeEntry(writer, key, type)) goto bailure;  // AddResourceTreeEntry prints the error
                DLOG("ParseCallback.WriteIntegerBytes", "bytes(%d), sign(%s), offset(%d) = %lld\n",
                    (int)(type & ~kResourceTreeEntryType_NegationMask), (type & kResourceTreeEntryType_NegationMask) ? "-" : "+",
                    (int)writer->nCurrentOffset, LT_Pu64(value->integer));
                if (! WriteBytes(writer, (u8 *)&value->integer, (type & ~kResourceTreeEntryType_NegationMask))) ParseBail("Exceeded max arbolation size", NULL);
            }
            break;
        case kLTUtilityJsonParser_ValueType_String:
            {   DLOG("ParseCallback", "value.string = \"%s\"\n", value->string ? value->string : NULL);
                ResourceTreeEntryType type;
                u32 nSize;
                if (value->string[0] == '\b') {
                    type = kResourceTreeEntryType_Binary;
                    const char *pHexStr = value->string + 1;
                    u32 nHexLen = lt_strlen(pHexStr);
                    if (nHexLen & 1) ParseBail("illegal binary string length (%d) - must be multiple of 2", (int)nHexLen);
                    nSize = nHexLen / 2;
                    pBytes = lt_malloc(nSize);
                    if (!pBytes) ParseBail("allocation failure of %d bytes for hex string to binary conversion", (int)nSize);
                    u32 nConverted = s_workingData.utilityByteOps->HexDecode(pHexStr, nHexLen, pBytes, nSize);
                    if (nConverted != nSize) ParseBail("failed to convert hex string to bytes", NULL);
                } else {
                    type   = kResourceTreeEntryType_String;
                    nSize  = lt_strlen(value->string) + 1;
                    pBytes = (u8 *)lt_strdup(value->string);
                    if (! pBytes) ParseBail("allocation failure arbolating string value", NULL);
                }
                if (nSize > 65534) ParseBail("illegal string value length > 65534 bytes", NULL);
                u16 size = (u16)nSize;
                if (! AddResourceTreeEntry(writer, key, type)) goto bailure;  // AddResourceTreeEntry prints the error
                if (! Write16(writer, size)) ParseBail("Exceeded max arbolation size", NULL);
                DLOG("ParseCallback.WriteStringBytes", "bytes(%d),  offset(%d) = %s\n", (int)size, (int)writer->nCurrentOffset, pBytes);
                if (! WriteBytes(writer, pBytes, size)) ParseBail("Exceeded max arbolation size", NULL);
            }
            break;
        default:
            DLOG("ParseCallback.IllegalArbolationType", "value.type(%d)\n", (int)value->type);
            ParseBail("illegal arbolation type: %s", s_workingData.parser->API->ValueTypeToString(value->type));
            break;
    }
    bRetVal = true;
bailure:
    if (pBytes) lt_free(pBytes);
    return bRetVal;
}

static u32 MakeArbolatedTreeFromJson( u8 * pBytes, u32 nSize) {
    ResourceTreeHeader header = { .nResourceTreeIDStamp = kResourceTree_HeaderStampARB0, .nTotalTreeSize = 0 };
    ArbolWriter *writer = lt_malloc(sizeof(*writer));
    if (writer) {
        lt_memset(writer, 0, sizeof(*writer));
        writer->pBytes = pBytes; writer->nMaxSize = nSize;
        // write the header
        WriteBytes(writer, (u8 *)&header, sizeof(header));
        // parse and write arbolated content
        nSize = s_workingData.parser->API->ParseJson(s_workingData.parser, s_workingData.inputFileMem, &MakeArbolatedTreeParseCallback, writer) ? writer->nCurrentOffset : 0;
        if (nSize) {
            // rewrite the header with the total arbolated tree size
            header.nTotalTreeSize = nSize;
            ReplaceBytes(writer, 0, (u8 *)&header, sizeof(header));
        }
        lt_free(writer);
    }
    else nSize = 0;

    return nSize;
}

static bool GenerateArbolatedResourceTree(void) {
    bool bRetVal = false;
    char *pLibPath = NULL;
    /* allocate memory for the arbolated tree */
    u32 nSize = kArbolatedTreeMaxBytes;
    u8 *pBytes = lt_malloc(nSize);
    if (NULL == pBytes) ErrorBail("failed to allocate arbolation memory", NULL);

    /* turn json into an arbolated tree */
    if (0 == (nSize = MakeArbolatedTreeFromJson(pBytes, nSize))) ErrorBail("MakeArbolatedTreeFromJson() failed", NULL);

    /* determine the libraryName based on the output file name because the output file name
      is always <libraryName>ResourceTree.h */
    pLibPath = lt_strdup(s_workingData.outputFilePath); /* make a copy for basename to mangle */
    char * pLibrary = basename(pLibPath);
    int nLen = pLibrary && *pLibrary ? (int)lt_strlen(pLibrary) : 0;
    int nLenResourceTree = sizeof("ResourceTree.h") - 1;
    char * pEnd = (nLenResourceTree < nLen) ? pLibrary + nLen - nLenResourceTree : NULL;
    if (pEnd && (0 == lt_strcmp(pEnd, "ResourceTree.h"))) *pEnd = 0;
    else ErrorBail("failed to extract library name from: %s", pLibrary);

    /* write the preamble */
    fprintf(s_workingData.outputFile, s_arbolatedTreePreamble, pLibrary, pLibrary, LT_Pu32(((ResourceTreeHeader *)pBytes)->nTotalTreeSize));

    /* write arbolated tree bytes out line by line, all but the last line */
    u8 *pOutBytes = pBytes;
    for (; nSize > kArbolatedTreeBytesPerLine; pOutBytes += kArbolatedTreeBytesPerLine, nSize -= kArbolatedTreeBytesPerLine) OutputArbolatedLine(pOutBytes, kArbolatedTreeBytesPerLine, true);
    /* write the last line */
    if (nSize) OutputArbolatedLine(pOutBytes, nSize, false);

    /* write the postamble */
    fprintf(s_workingData.outputFile, "%s", s_arbolatedTreePostamble);

    bRetVal = true;
bailure:
    if (pLibPath) lt_free(pLibPath);
    if (pBytes) lt_free(pBytes);
    return bRetVal;
}

/* ________________________________________________________________________________________________________________________
 '      ALL OPS                                                                                   '                       /
 ' PREREQUISITE: EXPAND INCLUDE DIRECTIVES                                                        '                      /
 '                                                                                                '                     /
 '  ENTRY POINT: ProcessJsonIncludes()                                                            '                    /
 '      MISSION: Parse the original json source file supplied on the command line, and            '                   /
 '               expand any arbolator include directives found therein and write the result       '                  /
 '               to an intermediate file named <basename>_Pass_N, where N is the pass number.     '                 /
 '               Then perform the same exercise again (next pass) because an included file        '                /
 '               could itself have includes in it.  Continue this a until a pass completes        '               /
 '               with no further inclusions or until the max passes is reached (currently 3)      '              /
 '               which means, e.g., an LTProductConfig could include file 1, which might          '             /
 '               have an include (depth 2), which might in-turn include another file              '            /
 '               that has includes (depth 3) and after expanding phase 3, if there are still      '           /
 '               include directives becuase the latest include pass added more, then the          '          /
 '               arbolator will error out with an appropriate message.  The max depth             '         /
 '               is configured by modifying enum value: kJsonIncludeMaxDepth in this file.        '        /
 '                                                                                                '       /
 '               The json include directive may specify the file to include as a path relative    '      /
 '               to itself, an absolute path, or may use environment variables expressed as       '     /
 '               either $(VARIABLE) or ${VARIABLE}.                                               '    /
 '                                                                                                '   /
 '        PARSE: e.g. <product_root>/build/product/<product_variant>/LTProductConfig.json         '  /
 '     GENERATE: e.g. <target_obj_dir>/LTProductConfig/arbolated/json-source/LTProductCnfig.json  ' /
 '                                                                                                '/
 `''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''`
*/
static bool SystemExec(const char *command) {
    int status = system(command);
    if (WIFEXITED(status) && (0 == WEXITSTATUS(status))) return true;
    ReportAbort("failed to execute system command: %s", command);
    return false;
}

static bool DeleteAndRecreateJsonIntermediateSourceDir(void) {
    char command[PATH_MAX+24];
    if (lt_strlen(s_workingData.jsonIntermediateSourceDir) < 24) return false;
    lt_snprintf(command, sizeof(command), "rm -rf \"%s\"", s_workingData.jsonIntermediateSourceDir);
    if (! SystemExec(command)) return false;
    lt_snprintf(command, sizeof(command), "mkdir -p \"%s/passes\"", s_workingData.jsonIntermediateSourceDir);
    if (! SystemExec(command)) return false;
    return true;
}

static bool ExpandIncludeDirectiveEnvironmentVariables(const char *pIncludeDirective, char * destPath) {
    char envVar[256];
    u32 envIndex = 0;
    u32 destIndex = 0;
    while (*pIncludeDirective) {
        if (*pIncludeDirective == '$') {
            char nextCh = *(pIncludeDirective + 1);
            char lastCh = ((nextCh == '(') ? ')' : ((nextCh == '{') ? '}' : 0));
            if (lastCh) {
                envVar[0] = 0;
                envIndex = 0;
                pIncludeDirective += 2;
                while (*pIncludeDirective && *pIncludeDirective != lastCh && (envIndex < (sizeof(envVar)-1))) {
                    envVar[envIndex++] = *pIncludeDirective++;
                }
                if (*pIncludeDirective == lastCh) pIncludeDirective++; // landed on the end character, advance past it
                else ParseBail("malformed environment variable in json include directive", NULL);
                envVar[envIndex] = 0;
                char * envValue = getenv(envVar);
                if (NULL == envValue) ParseBail("unknown environment variable '%s' in json include directive", envVar);
                while ((destPath[destIndex++] = *envValue++));
                destIndex--; /* erase the null term we just put in destPath */
                continue;
            }
            else {
                // got $something but not $( or ${
                // allow 1 $ in
                destPath[destIndex++] = *pIncludeDirective++;
                // if nextch is $, i.e $$, skip over the second one
                if (nextCh == '$') pIncludeDirective++;
                continue;
            }
        }
        destPath[destIndex++] = *pIncludeDirective++;
    }
    destPath[destIndex] = 0;
    return true;
bailure:
    return false;
}

#if 0
static void DumpIncludeSegment(u32 nPass, u32 nSegment, const char * pMsg, IncludeSegment *segment) {
    lt_consoleprint("### Pass %d, Segment %d: %s\n", (int)nPass, (int)nSegment, pMsg);
    lt_consoleprint("                  Directive: %s\n", segment->jsonIncludeDirectivePath);
    lt_consoleprint("               Include Path: %s\n", segment->fsIncludeFilePath);
    lt_consoleprint("      Preamble Input Offset: %4d,  Preamble length: %4d, Preamble Post  Skip: %4d\n", (int)segment->nInputPreambleOffset, (int)segment->nInputPreambleLength, (int)segment->nInputPreambleSkip);
    lt_consoleprint("       Inclusion Pre-Length: %4d, Inclusion Length: %4d Iclusion Post-length: %4d\n", (int)segment->nJsonPreambleLength, (int)segment->nJsonSegmentLength, (int)segment->nJsonPostambleLength);
}
#endif

static IncludeSegment * FindSegmentWithInclusionOffset(u32 nOffset, LTArray *segments) {
    IncludeSegment *segment = NULL;
    u32 inclusionOffsetStart, inclusionOffsetEnd, totalOffset = 0;
    u32 count = segments->API->GetCount(segments);
    u32 i = 0;
    for (; i < count; i++) {
        segment = segments->API->Get(segments, i, NULL);
        inclusionOffsetStart = totalOffset + segment->nInputPreambleLength + segment->nJsonPreambleLength;
        inclusionOffsetEnd = inclusionOffsetStart + segment->nJsonSegmentLength - 1;
        if (nOffset >= inclusionOffsetStart && nOffset <= inclusionOffsetEnd) break;
        totalOffset += (segment->nInputPreambleLength + segment->nJsonPreambleLength + segment->nJsonSegmentLength + segment->nJsonPostambleLength);
    }
    return (i == count) ? NULL : segment;
}

static bool MyFoundIncludesCallback(const char * pIncludeDirective, u32 nStartOffset, u32 nEndOffset, void *clientData) {
    ExpandIncludesClientData *cd = (ExpandIncludesClientData *)clientData;
    if (cd->outputPass == 0) {
        // if outputPass is zero we are doing a final parse of our kJsonIncludeMaxDepth pass to check for includes
        // if we hit one, we bail and report max depth reached
        cd->outputPass = kJsonIncludeMaxDepth + 1; /* mark outputPass so we can tell the difference between maxDepthReached and general parser error */
        return false;
    }
    u32 nCount = cd->segmentsOfOutputFile->API->GetCount(cd->segmentsOfOutputFile);
    cd->segmentsOfOutputFile->API->SetCount(cd->segmentsOfOutputFile, nCount+1);
    IncludeSegment *  newSegment = cd->segmentsOfOutputFile->API->Get(cd->segmentsOfOutputFile, nCount, NULL);
    IncludeSegment * prevSegment = nCount > 0 ? cd->segmentsOfOutputFile->API->Get(cd->segmentsOfOutputFile, nCount-1, NULL) : NULL;
    newSegment->nInputPreambleOffset = (prevSegment == NULL) ? 0 : prevSegment->nInputPreambleOffset + prevSegment->nInputPreambleLength + prevSegment->nInputPreambleSkip;
    LT_ASSERT(nStartOffset > newSegment->nInputPreambleOffset);
    LT_ASSERT(nStartOffset < nEndOffset);
    newSegment->nInputPreambleLength = nStartOffset - newSegment->nInputPreambleOffset;
    newSegment->nInputPreambleSkip = (nEndOffset - nStartOffset) + 1;

    // setup the fsIncludeFilePath
    char workingPath[PATH_MAX]; workingPath[0] = 0;
    if (! ExpandIncludeDirectiveEnvironmentVariables(pIncludeDirective, workingPath)) goto bailure; /* bailure error already printed */
    if (workingPath[0] != '/') {
        // the include directive after env var expansion is not an absolute directory
        // we have to find the doc root (containing path) of the json file that the snippet this directive is in came from
        // by searching for the offset of the directive in the previous pass' segments (cd->segmentsOfInputFile)
        char docRoot[PATH_MAX];     docRoot[0] = 0;
        char *pDocRoot = docRoot;
        if (NULL == cd->segmentsOfInputFile) {
            // this is pass 1, no previous path, use doc root of main arbolation source file
            lt_strncpyTerm(docRoot, cd->inputFileDocRoot, sizeof(docRoot));
        }
        else {
            IncludeSegment *sourceSegment = FindSegmentWithInclusionOffset(nStartOffset, cd->segmentsOfInputFile);
            if (NULL == sourceSegment) ErrorBail("logic error: couldn't find segment providing include directive for relative path expansion:", NULL);
            lt_strncpyTerm(docRoot, sourceSegment->fsIncludeFilePath, sizeof(docRoot));
            pDocRoot = dirname(docRoot); LT_ASSERT(pDocRoot != NULL);
        }
        lt_strncpyTerm(newSegment->fsIncludeFilePath, workingPath, sizeof(newSegment->fsIncludeFilePath));
        lt_snprintf(workingPath, sizeof(workingPath), "%s/%s", pDocRoot, newSegment->fsIncludeFilePath);
    }


    if (NULL == realpath(workingPath, newSegment->fsIncludeFilePath)) ParseBail("include path %s is not a realpath", workingPath);
    lt_strncpyTerm(newSegment->jsonIncludeDirectivePath, pIncludeDirective, sizeof(newSegment->jsonIncludeDirectivePath));

    // ok got the path of the thing to include, read it's contents for later assembly
    FILE *outputFile = NULL;
    struct stat file_stat;
    if (0 != stat(newSegment->fsIncludeFilePath, &file_stat))                                   ParseBail("failed to stat include file %s", newSegment->fsIncludeFilePath);
    if (0 == (newSegment->nJsonSegmentLength = (u32)file_stat.st_size))                         ParseBail("empty include file %s", newSegment->fsIncludeFilePath);
    if (NULL == (newSegment->includeFileText = lt_malloc(newSegment->nJsonSegmentLength + 1)))  ErrorBail("failed to allocate %d bytes for include file contents", (int)newSegment->nJsonSegmentLength+1);
    if (NULL == (outputFile = fopen(newSegment->fsIncludeFilePath,  "rb")))                      ParseBail("failed to open include file %s", newSegment->fsIncludeFilePath);
    if (newSegment->nJsonSegmentLength != fread(newSegment->includeFileText, 1, newSegment->nJsonSegmentLength, outputFile)) ParseBail("failed to read include file %s", newSegment->fsIncludeFilePath);
    newSegment->includeFileText[newSegment->nJsonSegmentLength] = 0;

    return true;
bailure:
    return false;
}

static void ClearSegmentArrays(ExpandIncludesClientData *cd) {
    for (u32 i = 0; i <= kJsonIncludeMaxDepth; i++) {
        LTArray *segmentArray = cd->segmentsOfPass[i];
        if (segmentArray) {
            u32 count = segmentArray->API->GetCount(segmentArray);
            for (u32 j = 0; j < count; j++) {
                IncludeSegment *segment = segmentArray->API->Get(segmentArray, j, NULL);
                if (segment && segment->includeFileText) lt_free(segment->includeFileText);
            }
            lt_destroyobject(segmentArray);
            cd->segmentsOfPass[i] = NULL;
        }
    }
    cd->segmentsOfInputFile = cd->segmentsOfOutputFile = NULL;
}

static bool ExpandIncludes(bool *pbContinue, ExpandIncludesClientData *cd) {
    /* we have to parse the source, and if we have includes, write to the dest
       if the include list is empty, then copy source file to working master,
       setup the input file name and set *pbContinue to false;
     */
    bool bRetVal = false;
    char inputPath[PATH_MAX]; char outputPath[PATH_MAX]; char scratch[PATH_MAX*3 + 24];
    const char * pFilename;
    char *inputText = NULL; FILE *inputFile = NULL; FILE *outputFile = NULL;
    if (cd->inputPass == 0) {
        // first time through, use actual source file, set pFilename and calcuate the inputFileBasename sans extension
        pFilename = s_workingData.inputFilePath;
        // calculate baseName from pFilename;
        const char * dot = NULL;
        const char *slash = pFilename;
        const char *curr = pFilename;
        while (*curr) { if (*curr == '/') slash = curr; curr++; }
        curr = slash+1;
        if (*curr) curr++; /* advance over the first char in case it's a dot so we dont cut /.foo */
        while (*curr) { if (*curr == '.') dot = curr; curr++; }
        u32 baseNameBytes = dot ? (u32)((LT_SIZE)dot - (LT_SIZE)(slash+1)) : (u32)((LT_SIZE)curr - (LT_SIZE)(slash+1));
        if (baseNameBytes > (sizeof(cd->inputFileBasename)-1)) baseNameBytes = sizeof(cd->inputFileBasename)-1;
        lt_memcpy(cd->inputFileBasename, slash+1, baseNameBytes);
        cd->inputFileBasename[baseNameBytes] = 0;
        baseNameBytes = (u32)(((LT_SIZE)slash) - ((LT_SIZE)pFilename));
        lt_memcpy(cd->inputFileDocRoot, pFilename, baseNameBytes);
        LT_ASSERT(cd->outputPass == 1);
        cd->segmentsOfInputFile = NULL;
        cd->segmentsOfOutputFile = cd->segmentsOfPass[cd->outputPass] = LTArray_CreateStructArray(sizeof(IncludeSegment));
        // copy the actual source file to the passes directory as pass 0 so a reference lives there with inclusion source
        lt_snprintf(inputPath, sizeof(inputPath), "%s/passes/%s_Pass_%d.json", s_workingData.jsonIntermediateSourceDir, cd->inputFileBasename, (int)cd->inputPass);
        //lt_snprintf(scratch, sizeof(scratch), "echo \"/* initial source json: %s */\" > \"%s\"", pFilename, inputPath);
        lt_snprintf(scratch, sizeof(scratch), "echo \"%s%s%s\" > \"%s\"", s_includePassZeroPreamble, pFilename, s_includePassZeroPostamble, inputPath);
        if (! SystemExec(scratch)) ErrorBail("failed to echo initial source banner into %s", inputPath);
        lt_snprintf(scratch, sizeof(scratch), "cat \"%s\" >> \"%s\"", pFilename, inputPath);
        if (! SystemExec(scratch)) ErrorBail("failed to cat %s into %s", pFilename, inputPath);
        pFilename = inputPath;
    }
    else {
        // source file is a previously output BaseName_Pass_N.json file
        lt_snprintf(inputPath, sizeof(inputPath), "%s/passes/%s_Pass_%d.json", s_workingData.jsonIntermediateSourceDir, cd->inputFileBasename, (int)cd->inputPass);
        pFilename = inputPath;
        cd->segmentsOfInputFile = cd->segmentsOfPass[cd->inputPass]; LT_ASSERT(cd->segmentsOfInputFile != NULL);
        cd->segmentsOfOutputFile = cd->segmentsOfPass[cd->outputPass] = LTArray_CreateStructArray(sizeof(IncludeSegment));
    }

    u32 nInputSize = 0;
    struct stat file_stat;
    if (0 != stat(pFilename, &file_stat))                         ErrorBail("failed to stat input file %s", pFilename);
    if (0 == (nInputSize = (u32)file_stat.st_size))               ErrorBail("empty input file %s", pFilename);
    if (NULL == (inputText = lt_malloc(nInputSize + 1)))          ErrorBail("failed to allocate %d bytes for input file contents", (int)nInputSize);
    if (NULL == (inputFile = fopen(pFilename,  "rb")))            ErrorBail("failed to open input file %s", pFilename);
    if (nInputSize != fread(inputText, 1, nInputSize, inputFile)) ErrorBail("failed to read input file %s", pFilename);
    inputText[nInputSize] = 0;

    // for parse error reporting use the actual file we're parsing, unless (cd->inputPass == 0) and we'll use the input file
    // and set the line offset to 3, the numLines we added to the actual file we're parsing for the pass 0 paper trail
    if (cd->inputPass == 0) {
        s_workingData.parsePath = s_workingData.inputFilePath;
        s_workingData.parseLineOffset = -3; /* we put 3 lines in at the top of the pass 0 file we made */
    }
    else {
        s_workingData.parsePath = pFilename;
        s_workingData.parseLineOffset = 0;
    }

    if (! s_workingData.parser->API->FindIncludeDirectives(s_workingData.parser, inputText, &MyFoundIncludesCallback, cd)) {
        if (cd->outputPass == (kJsonIncludeMaxDepth + 1)) { ParseBail("max include depth exceeded", NULL); }
        else { ParseBail(NULL, NULL); }
    }

    u32 nCount = cd->segmentsOfOutputFile->API->GetCount(cd->segmentsOfOutputFile);
    if (nCount == 0) {
        /* input file had no include directives;  mark that we're done */
        *pbContinue = false;
        /* copy the final file to working master */
        lt_snprintf(outputPath, sizeof(outputPath), "%s/%s.json", s_workingData.jsonIntermediateSourceDir, cd->inputFileBasename);
        lt_snprintf(scratch, sizeof(scratch), "cp \"%s\" \"%s\"", pFilename, outputPath);
        if (! SystemExec(scratch)) ErrorBail("failed to copy %s to %s", pFilename, outputPath);
        /* make working master the input file for arbolation */
        lt_strncpyTerm(s_workingData.inputFilePath, outputPath, sizeof(s_workingData.inputFilePath));
        bRetVal = true;
        goto bailure;
    }

    if (cd->outputPass == 0)  ParseBail("max include depth exceeded", NULL);

    /* now we write out our output file from the sections we built */
    lt_snprintf(outputPath, sizeof(outputPath), "%s/passes/%s_Pass_%d.json", s_workingData.jsonIntermediateSourceDir, cd->inputFileBasename, (int)cd->outputPass);
    outputFile = fopen(outputPath, "w");
    if (NULL == outputFile) ErrorBail("failed to create output file %s", outputPath);

    u32 inputTextTotalCopied = 0;
    u32 bytesWritten = 0;
    char * pInput = inputText;
    for (u32 i = 0; i < nCount; i++) {
        IncludeSegment *segment = cd->segmentsOfOutputFile->API->Get(cd->segmentsOfOutputFile, i, NULL);
        if (inputTextTotalCopied >= nInputSize) ErrorBail("logic error, miscalculated segment input text sizes (copied(%d) >= nInputSize(%d)", inputTextTotalCopied, nInputSize);
        bytesWritten = fwrite(pInput, 1, segment->nInputPreambleLength, outputFile);
        if (bytesWritten != segment->nInputPreambleLength) ErrorBail("failed to write segment input text to arbolated json expansion", NULL);
        pInput += (bytesWritten + segment->nInputPreambleSkip);
        inputTextTotalCopied += (bytesWritten + segment->nInputPreambleSkip);

        /* json inclusion preamble */
        lt_snprintf(scratch, sizeof(scratch), s_arbolatedIncludePreamble, cd->outputPass, i+1, segment->jsonIncludeDirectivePath, segment->fsIncludeFilePath);
        segment->nJsonPreambleLength = lt_strlen(scratch);
        bytesWritten = fwrite(scratch, 1, segment->nJsonPreambleLength, outputFile);
        if (bytesWritten != segment->nJsonPreambleLength) ErrorBail("failed to write segment json preamble to arbolated json expansion", NULL);

        /* json inclusion text */
        bytesWritten = fwrite(segment->includeFileText, 1, segment->nJsonSegmentLength, outputFile);
        if (bytesWritten != segment->nJsonSegmentLength) ErrorBail("failed to write segment json text to arbolated json expansion", NULL);

        /* json inclusion postamble */
        lt_snprintf(scratch, sizeof(scratch), s_arbolatedIncludePostamble, cd->outputPass, i+1);
        segment->nJsonPostambleLength = lt_strlen(scratch);
        bytesWritten = fwrite(scratch, 1, segment->nJsonPostambleLength, outputFile);
        if (bytesWritten != segment->nJsonPostambleLength) ErrorBail("failed to write segment json postamble to arbolated json expansion", NULL);
    }

    if (inputTextTotalCopied < nInputSize) {
        nInputSize -= inputTextTotalCopied;
        bytesWritten = fwrite(pInput, 1, nInputSize, outputFile);
        if (bytesWritten != nInputSize) ErrorBail("failed to write final input text on output pass %d", cd->outputPass);
    }
    fflush(outputFile);
    fclose(outputFile);
    outputFile = NULL;

    bRetVal = true;

bailure:
    if (outputFile) fclose(outputFile);
    if (inputFile) fclose(inputFile);
    if (inputText) lt_free(inputText);
    return bRetVal;
}

static bool ProcessJsonIncludes(void) {
    bool bRetVal = false;

    ExpandIncludesClientData cd;
    lt_memset(&cd, 0, sizeof(cd));

    /* first blammo the intermediate source directory and re-create it.  This is easier than mucking with the makefile dependences across scattered json includes */
    DeleteAndRecreateJsonIntermediateSourceDir();
    ClearSegmentArrays(&cd);

    bool bContinue = true;
    u32 pass;
    for (pass = 0; pass < kJsonIncludeMaxDepth && bContinue; pass++) {
        cd.inputPass = pass; cd.outputPass = pass + 1;
        if (! ExpandIncludes(&bContinue, &cd)) goto bailure;
    }

    if (bContinue) {
        // we wrote the final output pass, kJsonIncludeMaxDepth.  run the final pass through ExpandExcludes as input,
        // if it finds no further includes in th efinal pass, it will finalize it; if the final pass has includes, ExpandExcludes will report maxdepth error and return false
        cd.inputPass = pass; cd.outputPass = 0;
        if (! ExpandIncludes(&bContinue, &cd)) goto bailure;
    }

    bRetVal = true;

bailure:
    ClearSegmentArrays(&cd);
    return bRetVal;
}

/*____________________
  Utility Functions */

/*________________________________________________________________________
  ReportAbort:  formats and prints an error string in the standard format.
  It is used for the reporting of generic errors that aren't related to parsing or json file contents. */
static void ReportAbort(const char *format, ...) {
    /* standard generic abort report, <error string> defaults to "aborting" if format NULL.  one form:
     *    arbolator: <error string>
     */
    char buff[1024]; const char * pAbortReport = "aborting";
    if (format) { lt_va_list args; lt_va_start(args, format); lt_vsnprintf(buff, sizeof(buff), format, args); lt_va_end(args); pAbortReport = buff; }
    lt_consoleprint("arbolator: %s\n", pAbortReport);
}

/*________________________________________________________________________
  ReportParseAbort:  formats and prints an error string related to parsing
               or to json content, reporting error location in json source. */
  static void ReportParseAbort(const char *format, ...) {
    /* standard parse position error report, reports s_workingData.parsePath for file, must be set; <error string> defaults to "aborting" if format NULL
     * Two forms, depending on presence of parsing error:
     *    <file>:<line>:<col>: parse error: <parse_error_name>, <error string>
     *    <file>:<line>:<col>: <error string>
     */
    char buff[1024]; const char * pAbortReport = "aborting";
    if (format) { lt_va_list args; lt_va_start(args, format); lt_vsnprintf(buff, sizeof(buff), format, args); lt_va_end(args); pAbortReport = buff; }
    if (s_workingData.parser) {
        const char * parsePath = s_workingData.parsePath ? s_workingData.parsePath : ""; const char * colon = s_workingData.parsePath ? ":" : "";
        LTUtilityJsonParser_ParseStatus parseStatus; s_workingData.parser->API->GetParseStatus(s_workingData.parser, &parseStatus);
        if (LTUtilityJsonParser_ParseStatus_IsErrorStatus(&parseStatus)) { lt_consoleprint("%s%s%d:%d: parse error: %s, %s\n", parsePath, colon, (int)parseStatus.linePos + (int)s_workingData.parseLineOffset, (int)parseStatus.charPosInLine, s_workingData.parser->API->ParseResultToString(parseStatus.parseResult), pAbortReport); }
        else { lt_consoleprint("%s%s%d:%d: %s\n", parsePath, colon, (int)parseStatus.linePos + (int)s_workingData.parseLineOffset, (int)parseStatus.charPosInLine, pAbortReport); }
    }
    else lt_consoleprint("arbolator: %s\n", pAbortReport);
}

/*______________________________________________________________________________________
  WriteHeaderComment writes the copyright header at the top of a Makefile/Source file */
static void WriteHeaderComment(FILE * pFile, const char * pFilePath, bool useMakefileComment) {
    time_t seconds = time(NULL); struct tm *current_time = localtime(&seconds);
    fprintf(pFile, useMakefileComment ? s_makefileHeaderPreamble : s_sourceFileHeaderPreamble, pFilePath, s_workingData.inputFilePath, current_time->tm_year + 1900);
}

/*________________________________________________________________________________
  WriteTrailerComment Writes the log blurb at the end of a Makefile/Source file */
static void WriteTrailerComment(FILE * pFile, bool useMakefileComment){
    time_t seconds = time(NULL); struct tm *current_time = localtime(&seconds); char pDate[10] = "01-Jan-20"; strftime(pDate, 10, "%d-%b-%y", current_time);
    fprintf(pFile, useMakefileComment ? s_makeFileFooterPostamble : s_sourceFileFooterPostamble, pDate, pDate);
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  13-Feb-25   augustus    added detection and expansion of include directives
 *                          in json source.  Takes a muli-pass approach where the
 *                          the orginal source file is scanned, all include directives
 *                          are resolved and the directives are replaced with the contents
 *                          of the file they referred to and written out to a new file
 *                          in an intermediate folder.  This file is scanned for
 *                          directives that may have been added from the included segments
 *                          of the first pass, and this continues until a file is produced
 *                          that has no directives in it, and then arbolation proceeds as normal.
 *                          The total number of passes allowed (include depth) is controlled
 *                          by the value of kJsonIncludeMaxDepth.
 *  14-Feb-25   augustus    simplified --gendeviceconfig and --genresourcetree parsing
 *
 */
