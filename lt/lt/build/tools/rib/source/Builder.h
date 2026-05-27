/******************************************************************************
 * Builder.h                                                      Image Builder
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef _BUILDER_H
#define _BUILDER_H

int BuilderCheckImage(int nAreaIdx, char * pIFilename);
int BuilderCreateFullImage(char * pOFilename);
int BuilderCreateUpdateImage(char       *pOFilename,
                             char       *pIFilename,
                             char       *pKeyFilename,
                             char       *pVersion,
                             const char *pPlatformArgs,
                             u32         nBlockSize);
int BuilderCreatePartitionTable(char * pOFilename);
int BuilderCreateOTAData(char * pOFilename, bool bIsInitial, bool bIsGood);

#endif
