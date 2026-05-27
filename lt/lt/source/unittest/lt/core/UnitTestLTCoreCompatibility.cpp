/******************************************************************************
 * UnitTestLTCoreCompatibility.cpp
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include <lt/LT.h>
#include <lt/LTObject.h>
#include <lt/LTTypes.h>
#include <lt/core/LTCore.h>
#include <lt/core/LTEvent.h>
#include <lt/core/LTMutex.h>
#include <lt/core/LTStdlib.h>
#include <lt/core/LTThread.h>
#include <lt/core/LTTime.h>

/* !!! THIS UNIT TEST ENSURES THAT C++ COMPATIBILITY HAS NOT BEEN BROKEN IN CORE INTERFACES */
