/******************************************************************************
 * amebaPro.c                                            AmebaPro Flash Support
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "lt/LTTypes.h"

#include "Image.h"
#include "FlashDevice.h"
#include "Serial.h"
#include "ameba.h"

static bool bRevertToLowSpeed = true;

static int TargetInit(void) {
    AmebaSetModeOfMystery(CTHULHU_SLUMBERS);
    if (AmebaPing() < 0)
        return -1;
    printf("Received ping\n");
    if (AmebaSendUCFGandSetSpeed(kFastBaudRate) < 0)
        return -1;
    printf("Speed set successfully\n");
    return 0;
}

static int Open(const char * pDeviceName, bool bAutoProgram, const char * pPlatformArgs) {
    if (!bAutoProgram) printf("Auto-programming features not supported on this platform\n");
    if (pPlatformArgs && pPlatformArgs[0] != '\0') {
        printf("Platform-specific flags do not exist for this device\n");
        return -1;
    }
    bRevertToLowSpeed = true;
    int err = SerialOpen(pDeviceName, kInitialBaudRate, kDefaultTimeoutMS);
    if (err < 0) return err;
    return TargetInit();
}

static void Close() {
    if (bRevertToLowSpeed) AmebaSendUCFGandSetSpeed(kInitialBaudRate);
    SerialClose();
}

static int Program(u32 nAreaIdx, char * pFilename, bool bPad, bool bAutoReboot) {
    if (!bAutoReboot) printf("Auto-reboot feature not supported on this platform\n");
    int ret = -1;
    Image image;
    if (ImageCreateFromFile(&image, pFilename) < 0) {
        printf("Does file exist?\n");
        return -1;
    }
    if (bPad && ImagePadToEnd(&image, nAreaIdx) < 0) {
        printf("Error padding image\n");
        return -1;
    }
    Area * pArea = ImageGetArea(nAreaIdx);
    if (pArea) {
        if (pArea->bMustFlashAll && image.nSize != pArea->nMaxSize) {
            printf("Error, image must be exact size of area\n");
            pArea = NULL;
        } else if (image.nSize > pArea->nMaxSize) {
            printf("Error, image will overflow area\n");
            pArea = NULL;
        }
    }
    if (pArea) {
        printf("Programming %s [%08X][%08X]\n", pFilename, pArea->nOffset, image.nSize);
        ret = AmebaProgram(pArea->nOffset, &image);
        if (ret == 0) {
            ret = AmebaVerifyChecksum(&image);
        }
    }
    ImageFree(&image);
    return ret;
}

static int Read(u32 nAreaIdx, char * pFilename, bool bAutoReboot) {
    printf("Flash read is not supported on this device\n");
    return -1;
}

static int Erase(u32 nAreaIdx) {
    Area * pArea = ImageGetArea(nAreaIdx);
    if (pArea) {
        printf("Erasing %s [%08X][%08X]\n", pArea->name, pArea->nOffset, pArea->nMaxSize);
        return AmebaErase(pArea->nOffset, pArea->nMaxSize / ImageGetBlockSize());
    }
    return -1;
}

static int Reboot(void) {
    bRevertToLowSpeed = false;
    return AmebaRebootTarget();
}

FlashDeviceInterface FlashDeviceInterface_amebaPro = {
    &Open,
    &Close,
    &Program,
    &Read,
    &Erase,
    &Reboot,
};

const FlashDeviceInterfaceMapEntry FlashDeviceMapEntry_amebaPro = {
    .pFamily = "ameba",
    .pDevice = "amebaPro",
    .pLegacyDevice = "AmebaPro",
    .pInterface = &FlashDeviceInterface_amebaPro
};

static void LT_USED_CONSTRUCTOR FlashDevice_RegisterSelf_amebaPro(void) {
    (void)FlashDevice_Register(&FlashDeviceMapEntry_amebaPro);
}
