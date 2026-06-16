/*******************************************************************************
 * source/example/object/ExampleObject.c - Example LT Object Library with ExampleObject
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************/

#include <lt/core/LTCore.h>
#include <example/object/ExampleObject.h>

/*___________________________________________________
  Declare ExampleObjectImpl private instance data. */
typedef_LTObjectImpl(ExampleObject, ExampleObjectImpl) {
    LTTime bornOnTime;
} LTOBJECT_API;

/*________________________________
  ExampleObjectImpl constructor */
static bool ExampleObjectImpl_ConstructObject(ExampleObjectImpl * object) {
    object->bornOnTime = LT_GetCore()->GetKernelTime();
    return true;
}

/*_______________________________
  ExampleObjectImpl destructor */
static void ExampleObjectImpl_DestructObject(ExampleObjectImpl * object) {
    LT_UNUSED(object); /* prevent compiler warning on unused parameter */
    /* nothing to clean up */
}

/*__________________________________
  ExampleObjectImpl API functions */
static void ExampleObjectImpl_StateYourAge(ExampleObjectImpl * object) {
    char ageBuff[24];
    LTTime age = LTTime_Subtract(LT_GetCore()->GetKernelTime(), object->bornOnTime);

    LT_GetCore()->FormatCanonicalTimeString(age, ageBuff, sizeof(ageBuff), false);
    lt_consoleprint("I am %s seconds old\n", ageBuff);
}

static void ExampleObjectImpl_RebirthYourself(ExampleObjectImpl * object) {
    object->bornOnTime = LT_GetCore()->GetKernelTime();
}

/*_______________________________________________________________________________
  make ExampleObjectImpl public and bind the ExampleObject API functions to it */
define_LTObjectImplPublic(ExampleObject, ExampleObjectImpl,
    StateYourAge,
    RebirthYourself
);

/* __________________________________________________________________________________________
   Now contain our object in a loadable LTObjectLibrary with LibInit and LibFini functions */
static bool ExampleObject_LibInit(void) {
    lt_consoleprint("ExampleObject LibInit() !!\n");
    return true;
}

static void ExampleObject_LibFini(void) {
    lt_consoleprint("ExampleObject LibFini() !!\n");
}

define_LTObjectLibrary(1, ExampleObject_LibInit, ExampleObject_LibFini);

/* ___
   LOG\_______________________________________________
   29-Jan-26   augustus       created
*/
