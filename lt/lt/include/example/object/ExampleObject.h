/* ___________________________________________________
   FILE: include/example/object/ExampleObject.h
         include as:
        #include <example/object/ExampleObject.h>


   This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
   If a copy of the MPL was not distributed with this file, you can obtain one at
   https://mozilla.org/MPL/2.0/.

   Copyright 2026, Roku, Inc.  All rights reserved.
______________________________________________________ */
#ifndef ROKU_LT_INCLUDE_EXAMPLE_OBJECT_EXAMPLEOBJECT_H
#define ROKU_LT_INCLUDE_EXAMPLE_OBJECT_EXAMPLEOBJECT_H

#include <lt/LTTypes.h>

/* Create ExampleObject by declaring ExampleObject abstract API (version 1)
   using (* FunctionPointer)(WithObjectInstanceAsFirstParameter * object) syntax. */

typedef_LTObject(ExampleObject, 1) {

    void (* StateYourAge)(ExampleObject * object);

    void (* RebirthYourself)(ExampleObject * object);

} LTOBJECT_API;

#endif

/* ___
   LOG\_______________________________________________
   29-Jan-26   augustus       created
*/
