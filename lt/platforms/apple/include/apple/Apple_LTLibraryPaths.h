/******************************************************************************
 * platforms/apple/include/apple/Apple_LTLibraryPaths.h
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026 Roku Inc. All rights reserved.
 ******************************************************************************/

#include <apple/Apple_LTPlatformDefs.h>
#import <Foundation/Foundation.h>

#define APPLE_LTLIBRARY_PREFIX                 	  "lib"
#define APPLE_LTLIBRARY_EXTENSION                 "so"
#define APPLE_LTLIBRARY_RESOURCEDIR_IOS           "LTLibraries"

LT_INLINE NSString *
Apple_GetLTLibraryFilePath(const char * pLibraryName) {
    NSString * path = [NSString stringWithFormat: @"%s%s", APPLE_LTLIBRARY_PREFIX, pLibraryName];
    #ifdef LT_APPLE_PLATFORM_IOS
        return [[NSBundle mainBundle] pathForResource: path ofType: @APPLE_LTLIBRARY_EXTENSION inDirectory: @APPLE_LTLIBRARY_RESOURCEDIR_IOS ];
    #else
        return [path stringByAppendingPathExtension: @APPLE_LTLIBRARY_EXTENSION ];
    #endif
}

LT_INLINE NSString *
Apple_GetLTLibraryDirectory(void) {
    #ifdef LT_APPLE_PLATFORM_IOS
        return [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent: @APPLE_LTLIBRARY_RESOURCEDIR_IOS ];
    #else
        return [[[NSBundle mainBundle] executablePath] stringByDeletingLastPathComponent];
    #endif
}

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  06-Nov-21   augustus    created
 */
