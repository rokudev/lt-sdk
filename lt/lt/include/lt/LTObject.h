/******************************************************************************
 * <lt/LTObject.h>                       prototype macros for new object system
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *
 * This file defines the macros that are used to create LTObjects.
 * _____________________________________________________________________________________
 * CREATING a new LTObject in THREE steps (using macros 1-3 defined in this file)
 *    1.  Use macro 1 in lib public  .h    file] to define a new LTObject.
 *    2.  Use macro 2 in lib private .h/.c file] to create a specialization with private instance data
 *    3.  Use macro 3 in lib private    .c file] to bind implementation to specialization and export for
 *                                                public or private creation
 *
 * USING an LTObject from ANYWHERE(!) in THREE steps:
 *    1.  Create the object:  LTUtilityJsonParser * parser = lt_createobject(LTUtilityJsonParser);
 *    2.     Use the object:  bool bValid = parser->API->ValidateJson(parser, jsonText);
 *    3. Destroy the object:  lt_destroyobject(parser);
 *
 * USING an LTObject by specifying a specific specialization to create:
 *    _____________________________________________
 *    1.  LTDriverFS *driverFat32 = lt_createobject(LTDriverFS, LTDriverFSFat32); // specifhy LTDriverFSFat32 as the desired specialization
 *    2.  driverFat32->API->SomeFunction(driverFat32);
 *    3.  lt_destroyobject(driverFat32);
 *
 ******************************************************************************/

#ifndef LT_LTOBJECT_H
#define LT_LTOBJECT_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN

/*_____________________________________________________________________________________
  ____________________________________________________________________________________/
  MACRO 1: typedef_LTObject _________________________________________________________/
                            for defining new LTObject types in lib public .h files  /
Example:                                                                           /
       typedef_LTObject(Foo, 1) {          // (objectName, objectVersion)         /
           void (*FooFunction)(Foo *foo);  // <-- object api function            /
           void (*BarFunction)(Foo *foo);  // <-- pointers go here              /
       } LTOBJECT_API;                     // terminate with } LTOBJECT_API;   /
________.---------------.____________________________________________________*/
#define typedef_LTObject(objectName, objectVersion)                                                     \
    typedef enum objectName##_Version { k##objectName##_Version = objectVersion } objectName##_Version; \
    typedef struct objectName##Api objectName##Api;                                                     \
    typedef struct objectName objectName;                                                               \
    struct objectName {                                                                                 \
        LTOBJECT_INHERIT_BASE_DATA(objectName)                                                          \
    };                                                                                                  \
    struct objectName##Api {                                                                            \
        LTOBJECT_INHERIT_BASE_API(objectName)                                                           \
        struct
#define LTOBJECT_API ; }


/*________________________________________________________________________________________
  _______________________________________________________________________________________/
  MACRO 2: typedef_LTObjectImpl ________________________________________________________/
                                for declaring the private instance data struct of an   /
                                object implementation (aka specialization) in a       /
                                library private header file or source file.          /
Example:                                                                            /
       typedef_LTObjectImpl(Foo, FooImpl) { // (objectName, specialization)        /
           LTString *progress;              // <-- implementation private data    /
           u32 retryCount;                  // <-- members here                  /
       } LTOBJECT_API;                      // terminate with } LTOBJECT_API;   /
________.-------------------._________________________________________________*/
#define typedef_LTObjectImpl(objectName, specializationName)                                            \
    typedef struct specializationName specializationName;                                               \
    typedef struct specializationName##Api specializationName##Api;                                     \
    struct specializationName##Api { objectName##Api interface; };                                      \
    struct specializationName {                                                                         \
        LTOBJECT_INHERIT_BASE_DATA(objectName)                                                          \
        struct

/*_______________________________________________________________________________________________
  ______________________________________________________________________________________________/
  MACRO 3: define_LTObjectImplPublic  _________________________________________________________/
        or define_LTObjectImplPrivate ________________________________________________________/
                                      for binding, in a library private source file,         /
                                      api implementation to object specialization and       /
                                      exporting for public creation or private             /
                                      (library internal) creation only.                   /
Example:                                                                                 /
       define_LTObjectImplPublic(Foo, FooImpl, // (objectName, specializationName,      /
           FooFunction,                        //  comma separated list of functions   /
           BarFunction );                      //  terminate with );                  /
________.------------------------.__________________________________________________*/
#define define_LTObjectImplPublic(objectName, specializationName, ...)                                  \
    DEFINE_LTOBJECT_IMPL(objectName, specializationName, __VA_ARGS__)                                   \
    static LT_USED LTLibrary_LTObjectApiListEntry s_entry_##specializationName##Api = {                 \
        .objectApi = (const struct LTObjectApi *)&s_##specializationName##Api,                          \
        .pNextEntry = NULL                                                                              \
    };                                                                                                  \
    LTOBJECT_MARK_EXPORTED(objectName, specializationName);                                             \
    static LT_USED_CONSTRUCTOR void LTObject_RegisterLTObject##specializationName##Api(void) {          \
        extern LTLibrary_LTObjectApiListEntry * LTLIBRARY_EXPORTED_OBJECT_LIST;                         \
        s_entry_##specializationName##Api.pNextEntry = LTLIBRARY_EXPORTED_OBJECT_LIST;                  \
        LTLIBRARY_EXPORTED_OBJECT_LIST = &s_entry_##specializationName##Api;                            \
    }                                                                                                   \

#define define_LTObjectImplPrivate(objectName, specializationName, ...)                                 \
    DEFINE_LTOBJECT_IMPL(objectName, specializationName, __VA_ARGS__)                                   \
    static LT_USED LTLibrary_LTObjectApiListEntry s_entry_##specializationName##Api = {                 \
        .objectApi = (const struct LTObjectApi *)&s_##specializationName##Api,                          \
        .pNextEntry = NULL                                                                              \
    };                                                                                                  \
    static LT_USED_CONSTRUCTOR void LTObject_RegisterLTObject##specializationName##Api(void) {          \
        extern LTLibrary_LTObjectApiListEntry * LTLIBRARY_INTERNAL_OBJECT_LIST;                         \
        s_entry_##specializationName##Api.pNextEntry = LTLIBRARY_INTERNAL_OBJECT_LIST;                  \
        LTLIBRARY_INTERNAL_OBJECT_LIST = &s_entry_##specializationName##Api;                            \
    }                                                                                                   \

/*     ______________________________________
      ______________________________________
     ______________________________________
    ______________________________________
   ______________________________________
  ______________________________________
 / HELPER MACROS: DO NOT USE DIRECTLY  /
/____________________________________*/
#define LTOBJECT_INHERIT_BASE_API(objectName)                                           \
    struct {                                                                            \
        /* Note that these four function signatures of the LTObject API must match */   \
        /* the first four function signatures of LTInterface.  This is important.  */   \
                                                                                        \
                      const char * (* GetObjectApiName)(void);                          \
                               u32 (* GetObjectApiVersion)(void);                       \
                   LTInterfaceType (* GetObjectInterfaceType)(void);                    \
                   LTLibrary *     (* GetObjectLibrary)(void);                          \
                      const char * (* GetObjectImplName)(void);                         \
                               u32 (* GetObjectImplSize)(void);                         \
                              bool (* ConstructObject)(objectName *object);             \
                              void (* DestructObject)(objectName *object);              \
                              void (* AddRef)(LTObject *object);                        \
                              void (* RemoveRef)(LTObject *object);                     \
        const struct LTObjectExt * (* GetLTObjectExt)(void);                            \
       };
#define LTOBJECT_GETOBJECTAPI_FUNCTION(cblName, specializationName)                               \
        LTOBJECT_GETOBJECTAPI_FUNCTION_FOR(cblName, specializationName)
#define LTOBJECT_GETOBJECTAPI_FUNCTION_FOR(cblName, specializationName)                           \
        const struct specializationName##Api * cblName##Impl_Get##specializationName(void) { return (specializationName##Api*)&s_##specializationName##Api; }
#define LTOBJECT_INHERIT_BASE_DATA(objectName) \
        const objectName##Api *API;     \
        const u32 reserved;             \
        const LTAtomic refCount;
#define LTOBJECT_DEFINE_FUNCTIONS(specializationName, ...)                          LTTYPES_EVAL(LTOBJECT_EXPAND_FUNCTIONS(specializationName, __VA_ARGS__))
#define LTOBJECT_EXPAND_FUNCTIONS(specializationName, ...)                          LTOBJECT_EXPAND_FUNCTIONS_TEST_EMPTY(specializationName, LTTYPES_IS_VARIADIC_LIST_EMPTY(__VA_ARGS__), __VA_ARGS__)
#define LTOBJECT_EXPAND_FUNCTIONS_TEST_EMPTY(specializationName, empty, ...)        LTOBJECT_EXPAND_FUNCTIONS_TEST_EMPTY_VALUE(specializationName, empty, __VA_ARGS__)
#define LTOBJECT_EXPAND_FUNCTIONS_TEST_EMPTY_VALUE(specializationName, empty, ...)  LTTYPES_FORCE_VARIADIC_EXPANSION(LTOBJECT_EXPAND_FUNCTIONS_TEST_EMPTY_RESULT_##empty(specializationName, __VA_ARGS__))
#define LTOBJECT_EXPAND_FUNCTIONS_TEST_EMPTY_RESULT_1(specializationName, ...)
#define LTOBJECT_EXPAND_FUNCTIONS_TEST_EMPTY_RESULT_0(specializationName, ...)      LTOBJECT_EXPAND_FUNCTION(specializationName, LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_FIRST_ARG(__VA_ARGS__))) LTTYPES_DEFERRED_COMMA_EXPANSION LTTYPES_DEFER_EXPANSION_TWICE(LTOBJECT_RECURSE_FUNCTION_LIST)()(specializationName, LTTYPES_DISCARD_FIRST_ARG(__VA_ARGS__))
#define LTOBJECT_EXPAND_FUNCTION(specializationName, functionName)                  LTOBJECT_EXPAND_FUNCTION_OUT(specializationName, functionName)
#define LTOBJECT_EXPAND_FUNCTION_OUT(specializationName, functionName)              .functionName = (void *)specializationName##_##functionName
#define LTOBJECT_RECURSE_FUNCTION_LIST()                                            LTOBJECT_EXPAND_FUNCTIONS

#define LTLIBRARY_EXPORTED_OBJECT_LIST                                              LTLIBRARY_EXPORTED_OBJECT_LIST_1(CURRENTLY_BUILDING_LTLIBRARY)
#define LTLIBRARY_EXPORTED_OBJECT_LIST_1(cblName)                                   LTLIBRARY_EXPORTED_OBJECT_LIST_2(cblName)
#define LTLIBRARY_EXPORTED_OBJECT_LIST_2(cblName)                                   g_p_##cblName##_LTObjectEntriesExported

#define LTLIBRARY_INTERNAL_OBJECT_LIST                                              LTLIBRARY_INTERNAL_OBJECT_LIST_1(CURRENTLY_BUILDING_LTLIBRARY)
#define LTLIBRARY_INTERNAL_OBJECT_LIST_1(cblName)                                   LTLIBRARY_INTERNAL_OBJECT_LIST_2(cblName)
#define LTLIBRARY_INTERNAL_OBJECT_LIST_2(cblName)                                   g_p_##cblName##_LTObjectEntriesInternal

#define LTLIBRARY_MACROCREATEOBJECT_PARMS                                           LTLIBRARY_MACROCREATEOBJECT_PARMS_1(CURRENTLY_BUILDING_LTLIBRARY)
#define LTLIBRARY_MACROCREATEOBJECT_PARMS_1(cblName)                                LTLIBRARY_MACROCREATEOBJECT_PARMS_2(cblName)
#define LTLIBRARY_MACROCREATEOBJECT_PARMS_2(cblName)                                g_p_##cblName##_MacroCreateObjectParms

#if defined(CURRENTLY_BUILDING_LTLIBRARY)
    #define GET_LTOBJECT_MACROCREATEOBJECT_PARMS()                                  GET_LTOBJECT_MACROCREATEOBJECT_PARMS_FUNCTION(CURRENTLY_BUILDING_LTLIBRARY)()
    #define GET_LTOBJECT_MACROCREATEOBJECT_PARMS_FUNCTION(cblName)                  GET_LTOBJECT_MACROCREATEOBJECT_PARMS_FUNCTION_1(cblName)
    #define GET_LTOBJECT_MACROCREATEOBJECT_PARMS_FUNCTION_1(cblName)                LTLibrary_##cblName##_GetMacroCreateObjectParms

    #define DEFINE_GET_LTOBJECT_MACROCREATEOBJECT_PARMS_FUNCTION(cblName)           \
      LT_INLINE LTLibrary_MacroCreateObjectParms *                                  \
      GET_LTOBJECT_MACROCREATEOBJECT_PARMS_FUNCTION(cblName)(void) {                \
        extern LTLibrary_MacroCreateObjectParms LTLIBRARY_MACROCREATEOBJECT_PARMS;  \
        return &LTLIBRARY_MACROCREATEOBJECT_PARMS;                                  \
      }

    DEFINE_GET_LTOBJECT_MACROCREATEOBJECT_PARMS_FUNCTION(CURRENTLY_BUILDING_LTLIBRARY)
#else
    #define GET_LTOBJECT_MACROCREATEOBJECT_PARMS()                                  NULL
#endif

#ifdef LT_NO_DYNAMIC_LOADER
    #define LTOBJECT_MARK_EXPORTED(objectName, specializationName) \
        static LT_USED LTLibrary_LTObjectMapEntry s_entry_##specializationName##ObjectMapEntry = {              \
            #specializationName, #objectName, LT_STRINGIFY(CURRENTLY_BUILDING_LTLIBRARY), NULL                  \
        };                                                                                                      \
        static LT_USED_CONSTRUCTOR void LTObject_RegisterLTObject##specializationName##ObjectMapEntry(void) {   \
            extern LTLibrary_LTObjectMapEntry * g_pStaticallyBoundLTObjectMapEntries;                           \
            s_entry_##specializationName##ObjectMapEntry.pNextEntry = g_pStaticallyBoundLTObjectMapEntries;     \
            g_pStaticallyBoundLTObjectMapEntries = &s_entry_##specializationName##ObjectMapEntry;               \
        }
#else
    #define LTOBJECT_MARK_EXPORTED(objectName, specializationName)                      DEFINE_LTOBJECT_EXPORT_STRING_1(CURRENTLY_BUILDING_LTLIBRARY, objectName, specializationName)
    #define DEFINE_LTOBJECT_EXPORT_STRING_1(cblName, objectName, specializationName)    DEFINE_LTOBJECT_EXPORT_STRING_2(cblName, objectName, specializationName)
    #define DEFINE_LTOBJECT_EXPORT_STRING_2(cblName, objectName, specializationName)    \
         LT_USED const char *                                                           \
           s_ltObjectExportString##specializationName##_##objectName##_##cblName =      \
             "ltobject_export_string:" #specializationName ":" #objectName ":" #cblName ;
#endif

#define define_LTObjectLibrary(version, libInit, libFini)               define_LTObjectLibrary_1(CURRENTLY_BUILDING_LTLIBRARY, version, libInit, libFini)
#define define_LTObjectLibrary_1(cblName, version, libInit, libFini)    define_LTObjectLibrary_2(cblName, version, libInit, libFini)
#define define_LTObjectLibrary_2(cblName, version, libInit, libFini)                          \
    static bool cblName##LibImpl_LibInit(void) {                                               \
        bool (*f)(void) = (bool (*)(void))libInit; return f ? f() : true; }                     \
    static void cblName##LibImpl_LibFini(void) {                                                 \
        void (*f)(void) = (void (*)(void))libFini; if (f) f(); }                                  \
    typedef_LTLIBRARY_ROOT_INTERFACE(cblName##Lib, version)             LTLIBRARY_EMPTY_INTERFACE; \
    define_LTLIBRARY_ROOT_INTERFACE(cblName##Lib)                       LTLIBRARY_DEFINITION;

#define define_LTOBJECT_EXPORTLIBRARY(libName, version, ...)                              \
    static bool libName##LibImpl_LibInit(void) { return true; }                            \
    static void libName##LibImpl_LibFini(void) { }                                          \
    typedef_LTLIBRARY_ROOT_INTERFACE(libName##Lib, version) LTLIBRARY_EMPTY_INTERFACE;       \
    define_LTLIBRARY_ROOT_INTERFACE(libName##Lib) LTLIBRARY_DEFINITION;                       \
    LTLIBRARY_EXPORT_INTERFACES(libName##Lib, LTOBJECT_SEQUENCE_EXPORTED_OBJECTS(__VA_ARGS__) );

#define LTOBJECT_SEQUENCE_EXPORTED_OBJECTS(...)                             LTTYPES_EVAL(LTOBJECT_EXPAND_SEQUENCE(__VA_ARGS__))
#define LTOBJECT_EXPAND_SEQUENCE(...)                                       LTOBJECT_EXPAND_SEQUENCE_TEST_EMPTY(LTTYPES_IS_VARIADIC_LIST_EMPTY(__VA_ARGS__), __VA_ARGS__)
#define LTOBJECT_EXPAND_SEQUENCE_TEST_EMPTY(empty, ...)                     LTOBJECT_EXPAND_SEQUENCE_TEST_EMPTY_VALUE(empty, __VA_ARGS__)
#define LTOBJECT_EXPAND_SEQUENCE_TEST_EMPTY_VALUE(empty, ...)               LTTYPES_FORCE_VARIADIC_EXPANSION(LTOBJECT_EXPAND_SEQUENCE_TEST_EMPTY_RESULT_##empty(__VA_ARGS__))
#define LTOBJECT_EXPAND_SEQUENCE_TEST_EMPTY_RESULT_1(...)
#define LTOBJECT_EXPAND_SEQUENCE_TEST_EMPTY_RESULT_0(...)                   LTOBJECT_EXPAND_OBJECT(LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_FIRST_ARG(__VA_ARGS__))) LTTYPES_DEFER_EXPANSION_TWICE(LTOBJECT_RECURSE_OBJECT_LIST)()(LTTYPES_DISCARD_FIRST_ARG(__VA_ARGS__))
#define LTOBJECT_EXPAND_OBJECT(objectName)                                  ( objectName )
#define LTOBJECT_RECURSE_OBJECT_LIST()                                      LTOBJECT_EXPAND_SEQUENCE

#define DEFINE_LTOBJECT_IMPL(objectName, specializationName, ...)                                                               \
    static const                objectName##Api s_##specializationName##Api;                                                    \
    LTOBJECT_GETOBJECTAPI_FUNCTION(CURRENTLY_BUILDING_LTLIBRARY, specializationName)                                            \
    LTLibrary *                 LTLIBRARY_SHARED_PROTOTYPE(GetLibrary)(void);                                                   \
    LTInterfaceType             LTLIBRARY_SHARED_PROTOTYPE(GetObjectInterfaceType)(void);                                       \
    static bool                 specializationName##_ConstructObject(specializationName *object);                               \
    static void                 specializationName##_DestructObject(specializationName *object);                                \
                                                                                                                                \
    static const char *         specializationName##_GetObjectApiName(void)      { return #objectName ; }                       \
    static u32                  specializationName##_GetObjectApiVersion(void)   { return k##objectName##_Version; }            \
    static const char *         specializationName##_GetObjectImplName(void)     { return #specializationName ; }               \
    static u32                  specializationName##_GetObjectImplSize(void)     { return (u32)(sizeof(specializationName)); }  \
    void                        LTLIBRARY_SHARED_PROTOTYPE(AddRef)(LTObject *object);                                           \
    void                        LTLIBRARY_SHARED_PROTOTYPE(RemoveRef)(LTObject *object);                                        \
    static                                                                                                                      \
    const struct LTObjectExt * specializationName##_GetLTObjectExt(void)        { return NULL; }                                \
                                                                                                                                \
    static const                objectName##Api s_##specializationName##Api = {                                                 \
        .GetObjectApiName       = specializationName##_GetObjectApiName,                                                        \
        .GetObjectApiVersion    = specializationName##_GetObjectApiVersion,                                                     \
        .GetObjectInterfaceType = LTLIBRARY_SHARED_PROTOTYPE(GetObjectInterfaceType),                                           \
        .GetObjectLibrary       = LTLIBRARY_SHARED_PROTOTYPE(GetLibrary),                                                       \
        .GetObjectImplName      = specializationName##_GetObjectImplName,                                                       \
        .GetObjectImplSize      = specializationName##_GetObjectImplSize,                                                       \
        .ConstructObject        = (void *)specializationName##_ConstructObject,                                                 \
        .DestructObject         = (void *)specializationName##_DestructObject,                                                  \
        .AddRef                 = LTLIBRARY_SHARED_PROTOTYPE(AddRef),                                                           \
        .RemoveRef              = LTLIBRARY_SHARED_PROTOTYPE(RemoveRef),                                                        \
        .GetLTObjectExt         = specializationName##_GetLTObjectExt,                                                          \
                                                                                                                                \
        LTOBJECT_DEFINE_FUNCTIONS(specializationName, __VA_ARGS__)                                                              \
    };

/* ______________________________________________________
  / LTObject & LTObjectApi PROTOTYPES - USED BY LTCORE  /
 /____________________________________________________*/
typedef struct LTObject LTObject;
typedef struct LTObjectApi LTObjectApi;
struct LTObject    { const LTObjectApi *API; const u32 reserved; const LTAtomic refCount; };
struct LTObjectApi { LTOBJECT_INHERIT_BASE_API(LTObject)                    };
enum               { kLTObject_Version = 1 };

LT_EXTERN_C_END
#endif /* #ifndef LT_LTOBJECT_H */

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  29-Jun-23   augustus    created
 */
