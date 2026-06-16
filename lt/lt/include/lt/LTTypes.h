/******************************************************************************
 * <lt/LTTypes.h>                                          LT Fundamental Types
 *
 * This file defines the core types used in LT for:
 *   a. the core LT operating system
 *   b. the method of organizing and packaging code into LT Libraries
 *   c. the Public API and Private Implementations for all LT Libraries
 *
 * Further info on the LT Type System, including requirements, goals, design,
 * usage, policies, and policy exceptions may be found in the comprehensive
 * overview at the end of this file.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 ******************************************************************************/

#ifndef ROKU_LT_INCLUDE_LT_LTTYPES_H
#define ROKU_LT_INCLUDE_LT_LTTYPES_H

#if defined(__cplusplus)
  extern "C" {
#endif

/******************
 * 1. BASIC TYPES */
#if defined (_MSC_VER)                  /* Microsoft C/C++ Compiler */

    typedef unsigned char               u8;
    typedef unsigned short              u16;
    typedef unsigned long               u32;
    typedef unsigned __int64            u64;
    typedef signed   char               s8;
    typedef signed   short              s16;
    typedef signed   long               s32;
    typedef signed   __int64            s64;

    #ifdef _WIN64
        typedef unsigned long long      LT_SIZE;
        typedef   signed long long      LT_SSIZE;
        #define LT_ARCHITECTURE_BITS    64
    #else
        typedef unsigned int            LT_SIZE;
        typedef   signed int            LT_SSIZE;
        #define LT_ARCHITECTURE_BITS    32
    #endif

#elif defined (__GNUC__)         /* gcc compiler */

    #if defined (__UINT8_TYPE__)    /* what stdint.h uses */
        typedef __UINT8_TYPE__      u8;
        typedef __UINT16_TYPE__     u16;
        typedef __UINT32_TYPE__     u32;
        typedef __UINT64_TYPE__     u64;
        typedef __INT8_TYPE__       s8;
        typedef __INT16_TYPE__      s16;
        typedef __INT32_TYPE__      s32;
        typedef __INT64_TYPE__      s64;
        typedef __SIZE_TYPE__       LT_SIZE;
        typedef __PTRDIFF_TYPE__    LT_PTRDIFF;
        #if defined(__SIZEOF_SIZE_T__)
            #if (__SIZEOF_SIZE_T__ == 8)
                #define LT_ARCHITECTURE_BITS 64
                typedef __INT64_TYPE__ LT_SSIZE;
            #elif (__SIZEOF_SIZE_T__ == 4)
                #define LT_ARCHITECTURE_BITS 32
                typedef __INT32_TYPE__ LT_SSIZE;
            #else
                #error "unsupported gcc __SIZEOF_SIZE_T__ value"
            #endif
        #endif
    #else
        typedef unsigned char       u8;
        typedef unsigned short      u16;
        typedef unsigned long long  u64;
        typedef signed   char       s8;
        typedef signed   short      s16;
        typedef signed   long long  s64;
        #if __LP64__ == 1
            typedef unsigned int    u32;
            typedef signed   int    s32;
            typedef unsigned long   LT_SIZE;
            typedef signed long     LT_SSIZE;
            typedef __int64         LT_PTRDIFF;
            #define LT_ARCHITECTURE_BITS 64
        #else
            typedef unsigned long   u32;
            typedef signed   long   s32;
            typedef unsigned int    LT_SIZE;
            typedef signed int      LT_SSIZE;
            typedef int             LT_PTRDIFF;
            #define LT_ARCHITECTURE_BITS 32
        #endif
    #endif
#else
    #error "Unsupported Compiler"
#endif

    /* sanity check size of basic types */
    #ifdef LT_DEBUG
        typedef char ltdebug__u8Check[sizeof( u8) == 1 ? 1 : -1];
        typedef char ltdebug_u16Check[sizeof(u16) == 2 ? 1 : -1];
        typedef char ltdebug_u32Check[sizeof(u32) == 4 ? 1 : -1];
        typedef char ltdebug_u64Check[sizeof(u64) == 8 ? 1 : -1];
        typedef char ltdebug__s8Check[sizeof( s8) == 1 ? 1 : -1];
        typedef char ltdebug_s16Check[sizeof(s16) == 2 ? 1 : -1];
        typedef char ltdebug_s32Check[sizeof(s32) == 4 ? 1 : -1];
        typedef char ltdebug_s64Check[sizeof(s64) == 8 ? 1 : -1];
        typedef char ltdebug_sz1Check[sizeof(LT_SIZE) == sizeof(void *) ? 1 : -1];
        typedef char ltdebug_sz2Check[sizeof(LT_SIZE) == sizeof(LT_SSIZE) ? 1 : -1];
        #if (LT_ARCHITECTURE_BITS == 64)
            typedef char ltdebug_sz3Check[sizeof(LT_SIZE) == 8 ? 1 : -1];
        #elif (LT_ARCHITECTURE_BITS == 32)
            typedef char ltdebug_sz3Chuck[sizeof(LT_SIZE) == 4 ? 1 : -1];
        #else
            #error "LT_ARCHITECTURE_BITS not determined"
        #endif
    #endif

/*************************************************
 * 2. C/C++ TYPE INTEROPERABILITY and LT_INLINE */
#undef NULL
#if defined(__cplusplus)
    #define NULL nullptr
    #define LT_EXTERN_C_BEGIN   extern "C" {
    #define LT_EXTERN_C_END     }
    #define LT_EXTERN_C         extern "C"
    #define declare_LTENUM_SIZED(name_, type_) enum name_ : type_;
    #define typedef_LTENUM_SIZED(name_, type_) enum name_ : type_
#else
    #define NULL ((void *)0)
    #define bool _Bool
    #define true 1
    #define false 0
    #define LT_EXTERN_C_BEGIN
    #define LT_EXTERN_C_END
    #define LT_EXTERN_C
    #define declare_LTENUM_SIZED(name_, type_) typedef type_ name_;
    #define typedef_LTENUM_SIZED(name_, type_) \
        typedef type_ name_; \
        enum name_
#endif
#if defined (_MSC_VER)          /* Microsoft C/C++ Compiler */
    #define LT_INLINE           static __forceinline
    #define LT_NOINLINE
    #define LT_NOINLINE_WEAK
#elif defined (__GNUC__)        /* gcc compiler */
    #define LT_INLINE           __attribute__((always_inline)) static inline
    #define LT_NOINLINE         __attribute__((noinline))
    #define LT_NOINLINE_WEAK    __attribute__((noinline, weak))
#else
    #error "Unsupported Compiler"
#endif



/***********************************************************************************
 * 3. VARARGS - LT defines lt_va_list, lt_va_start(), lt_va_end(), lt_va_arg(), and lt_va_copy()
 *              for use instead of the standard va_list, va_start, va_end, var_arg and va_copy
 *              to assure the lt_ set is always available in standalone and hosted environments
 *              whether or not the compiler supplies them as intrinisics or requires header file
 *              inclusion.  The va_ set is also redefined if building LT libraries in order to
 *              generate compiler message on unintended mixing.
 * RETURNADDR - void * lt_returnaddress(void); is defined as an inline function that returns the address
 *              the current function will return to (address of the current function's call site)
 */

#if defined(LT_RELEASE_IDENTIFIER)
    /* Prevent inadvertent mixing of lt_va_list with va_list */
    #define VALIST_ERROR_MESSAGE(symbol) "\n" __FILE__ "(" LT_STRINGIFY(__LINE__) "):\n\n\
      error: use lt_va_" #symbol " instead of va_" #symbol "\n"
    #define VA_LIST_ERROR(symbol) LTTYPES_CPP_PRAGMA_MESSAGE(VALIST_ERROR_MESSAGE(symbol))

    #undef  va_list
    #define va_list     VA_LIST_ERROR(list)
    #undef  va_start
    #define va_start    VA_LIST_ERROR(start)
    #undef  va_arg
    #define va_arg      VA_LIST_ERROR(arg)
    #undef  va_end
    #define va_end      VA_LIST_ERROR(end)
    #undef  va_copy
    #define va_copy     VA_LIST_ERROR(copy)
#endif

#if defined (_MSC_VER)  /* Microsoft C/C++ Compiler */

    /* definitions that are common across win32 and win64 */
    typedef char *  lt_va_list;
    #define         lt_va_end(ap)           ((void)(ap = (lt_va_list)0))
    #define         lt_va_copy(dst, src)    ((dst) = (src))

    #if defined _M_IX86 && !defined _M_HYBRID_X86_ARM64
        /* definitions that are win32 only */
        #define         LT_VARARGS_INTSIZEOF(n) ((sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))
        #ifdef __cplusplus
            #define     LT_VARARGS_ADDRESSOF(v) (&const_cast<char&>(reinterpret_cast<const volatile char&>(v)))
        #else
            #define     LT_VARARGS_ADDRESSOF(v) (&(v))
        #endif
        #define         lt_va_start(ap, v)      ((void)(ap = (lt_va_list)LT_VARARGS_ADDRESSOF(v) + LT_VARARGS_INTSIZEOF(v)))
        #define         lt_va_arg(ap, t)        (*(t*)((ap += LT_VARARGS_INTSIZEOF(t)) - LT_VARARGS_INTSIZEOF(t)))
    #elif defined _M_X64
        /* definitions that are win64 only */
        extern void __cdecl __va_start(lt_va_list * , ...);
        #pragma intrinsic  (__va_start)
        #define            lt_va_start(ap, x)     ((void)(__va_start(&ap, x)))
        #define            lt_va_arg(ap, t)       ((sizeof(t) > sizeof(__int64) || (sizeof(t) & (sizeof(t) - 1)) != 0) ? **(t**)((ap += sizeof(__int64)) - sizeof(__int64)) : *(t* )((ap += sizeof(__int64)) - sizeof(__int64)))
    #else
        #error "Unsupported win32 varargs platform"
    #endif

    /* Windows return address */
    extern void * __cdecl _ReturnAddress(void);
    #pragma intrinsic (_ReturnAddress)
    LT_INLINE void * lt_returnaddress(void) { return _ReturnAddress(); }

#elif defined (__GNUC__)         /* gcc compiler */
        typedef __builtin_va_list       lt_va_list;
        #define                         lt_va_start(v,l)    __builtin_va_start(v,l)
        #define                         lt_va_end(v)        __builtin_va_end(v)
        #define                         lt_va_arg(v,l)      __builtin_va_arg(v,l)
        #define                         lt_va_copy(d,s)     __builtin_va_copy(d,s)

        LT_INLINE void * lt_returnaddress(void) { return __builtin_extract_return_addr(__builtin_return_address(0)); }
#else
    #error "Unsupported Compiler"
#endif

/* _________________________________________________________________________
 * Types for debugging the source of allocations and creations of objects */

// LTCallSite - this sends file, line, and return address into the allocation
//              functions and causes all functions that use them to have increased
//              stack requirements; it should not be used in release mode
#define ENABLE_LT_CALLSITE_IN_RELEASE_MODE 0
    /* ENABLE_LT_CALLSITE_IN_RELEASE_MODE is the only configurable parameter
       for LTCallSite; always check it in with it set to 0 */

typedef struct {
    const char *file;             /**< Null-terminated name of source file. */
    LT_SIZE     line;             /**< Line number of call. */
    void       *returnAddress;    /**< Return address at callsite. */
} LTCallSite;

#define LT_NULL_CALLSITE ((LTCallSite) { NULL, 0, NULL })

#ifdef LT_DEBUG
    #define DISABLE_LT_CALLSITE 0
#else
    #if ENABLE_LT_CALLSITE_IN_RELEASE_MODE
        #define DISABLE_LT_CALLSITE 0
    #else
        #define DISABLE_LT_CALLSITE 1
    #endif
#endif

#if DISABLE_LT_CALLSITE
    #define LT_CALLSITE_FUNCTION_PARAMETER
    #define LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU
    #define comma_lt_callsite()
#else
    #define LT_CALLSITE_FUNCTION_PARAMETER          , LTCallSite callsite
    #define LT_CALLSITE_FUNCTION_PARAMETER_PASSTHRU , callsite
    #define comma_lt_callsite()                     , (LTCallSite){__FILE__, __LINE__, lt_returnaddress()}
#endif

/****************************************************************************
 * 4. ATOMICS                                                               */

#if defined(__cplusplus)
typedef          struct { volatile u32 atomicValue; } LTAtomic;
#elif defined (__clang__)
typedef          struct { _Atomic  u32 atomicValue; } LTAtomic;
#else
typedef volatile struct { volatile u32 atomicValue; } LTAtomic;
#endif

#if defined (__clang__)
   LT_INLINE  u32 LTAtomic_Load(const LTAtomic *atomic)                                               { return  (u32)__c11_atomic_load((_Atomic(u32)*)&atomic->atomicValue,               __ATOMIC_SEQ_CST); }
   LT_INLINE void LTAtomic_Store(LTAtomic *atomic, u32 value)                                         {              __c11_atomic_store((_Atomic(u32)*)&atomic->atomicValue,     value,   __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_FetchAdd(LTAtomic *atomic, u32 operand)                                    { return  (u32)__c11_atomic_fetch_add((_Atomic(u32)*)&atomic->atomicValue, operand, __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_FetchSubtract(LTAtomic *atomic, u32 operand)                               { return  (u32)__c11_atomic_fetch_sub((_Atomic(u32)*)&atomic->atomicValue, operand, __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_FetchAnd(LTAtomic *atomic, u32 operand)                                    { return  (u32)__c11_atomic_fetch_and((_Atomic(u32)*)&atomic->atomicValue, operand, __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_FetchOr(LTAtomic *atomic, u32 operand)                                     { return  (u32)__c11_atomic_fetch_or((_Atomic(u32)*)&atomic->atomicValue,  operand, __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_Exchange(LTAtomic *atomic, u32 value)                                      { return  (u32)__c11_atomic_exchange((_Atomic(u32)*)&atomic->atomicValue,  value,   __ATOMIC_SEQ_CST); }
   LT_NOINLINE_WEAK bool LTAtomic_CompareAndExchange(LTAtomic *atomic, u32 oldValue, u32 newValue)  { u32 ov = oldValue; return (bool)__c11_atomic_compare_exchange_strong((_Atomic(u32)*)&atomic->atomicValue, &ov, newValue, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
#elif defined(__GNUC__)
   LT_INLINE  u32 LTAtomic_Load(LTAtomic *atomic)                                                     { return  (u32)__atomic_load_n(&atomic->atomicValue,                 __ATOMIC_SEQ_CST); }
   LT_INLINE void LTAtomic_Store(LTAtomic *atomic, u32 value)                                         {              __atomic_store_n(&atomic->atomicValue,       value,   __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_FetchAdd(LTAtomic *atomic, u32 operand)                                    { return  (u32)__atomic_fetch_add(&atomic->atomicValue,     operand, __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_FetchSubtract(LTAtomic *atomic, u32 operand)                               { return  (u32)__atomic_fetch_sub(&atomic->atomicValue,     operand, __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_FetchAnd(LTAtomic *atomic, u32 operand)                                    { return  (u32)__atomic_fetch_and(&atomic->atomicValue,     operand, __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_FetchOr(LTAtomic *atomic, u32 operand)                                     { return  (u32)__atomic_fetch_or(&atomic->atomicValue,      operand, __ATOMIC_SEQ_CST); }
   LT_INLINE  u32 LTAtomic_Exchange(LTAtomic *atomic, u32 value)                                      { return  (u32)__atomic_exchange_n(&atomic->atomicValue,    value,   __ATOMIC_SEQ_CST); }
   LT_NOINLINE_WEAK bool LTAtomic_CompareAndExchange(LTAtomic *atomic, u32 oldValue, u32 newValue)    { u32 ov = oldValue; return (bool)__atomic_compare_exchange_n(&atomic->atomicValue, &ov, newValue, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
#elif defined (_MSC_VER) /* Microsoft C/C++ Compiler */
    /* ____________________
       MS atomic operations - Do not use directly!
          These are re-declared MS intrinsics and forced inline functions, done without
          the MS #defined types and concurrency maarkers.   Why? Because LTTypes.h can *never*
          include *any* header file; it must always remain standalone. */
        /* _____________________________________________
           win32 - WriteBarrier - Do not use directly */
           extern  void      _WriteBarrier(void);
           #pragma intrinsic(_WriteBarrier)
        /* __________________________________________________
           win32 - 32 bit atomic ops - Do not use directly */
           extern       long __cdecl _InterlockedCompareExchange(long volatile * Destination, long ExChange, long Comperand);
           extern       long __cdecl _InterlockedExchangeAdd(long volatile * _Addend, long _Value);
           #pragma         intrinsic(_InterlockedCompareExchange)
           #pragma         intrinsic(_InterlockedExchangeAdd)

    LT_INLINE u32 LTAtomic_Load(LTAtomic *atomic) { _WriteBarrier(); return atomic->atomicValue; }
    LT_INLINE u32 LTAtomic_Store(LTAtomic *atomic, u32 value) { atomic->atomicValue = value; _WriteBarrier(); }
    LT_INLINE u32 LTAtomic_FetchAdd(LTAtomic *atomic, u32 operand) { u32 retVal = (u32)_InterlockedExchangeAdd(((volatile long *)(&atomic->atomicValue)), (long)(nValue32)); _WriteBarrier(); return retVal; }
    LT_INLINE u32 LTAtomic_FetchSubtract(LTAtomic *atomic, u32 operand) { u32 retVal = (u32)_InterlockedExchangeAdd(((volatile long *)(&atomic->atomicValue)), (long)((long)0 - (long)(nValue32))); _WriteBarrier(); return retVal; }
    LT_INLINE bool LTAtomic_CompareAndExchange(LTAtomic32 * atomic, u32 oldValue, u32 newValue) {
        return ((long)nOldValue == _InterlockedCompareExchange(((volatile long *)(&atomic->atomicValue)), (long)newValue, (long)oldValue)) ? true : false;
    }
    LT_INLINE u32 LTAtomic_FetchAnd(LTAtomic *atomic, u32 operand) {
        u32 retVal;
        do { retVal = LTAtomic_Load(atomic); } while (! LTAtomic_CompareAndExchange(atomic, retVal, retVal & operand));
        return retVal;
    }
    LT_INLINE u32 LTAtomic_FetchOr(LTAtomic *atomic, u32 operand) {
        u32 retVal;
        do { retVal = LTAtomic_Load(atomic); } while (! LTAtomic_CompareAndExchange(atomic, retVal, retVal | operand));
        return retVal;
    }
   LT_INLINE  u32 LTAtomic_Exchange(LTAtomic *atomic, u32 value) {
        u32 retVal;
        do { retVal = LTAtomic_Load(atomic); } while (! LTAtomic_CompareAndExchange(atomic, retVal, value));
        return retVal;
   }

#else
    #error "Unsupported Compiler"
#endif

/********************************
 * 5. CONSTANTS AND SIZE RANGES */
#define LT_MAKECONST(numeric, suffix) numeric##suffix

#if defined (_MSC_VER)          /* Microsoft C/C++ Compiler */
    #define LT_CONSTU64(x)      ((u64)(LT_MAKECONST(x, ui64)))
    #define LT_CONSTS64(x)      ((s64)(LT_MAKECONST(x, i64)))

    #define LT_INT_MAX          2147483647
#elif defined (__GNUC__)        /* gcc compiler */
    #define LT_CONSTU64(x)      ((u64)(LT_MAKECONST(x, ULL)))
    #define LT_CONSTS64(x)      ((s64)(LT_MAKECONST(x, LL)))

    #define LT_INT_MAX          __INT_MAX__
#else
    #error "Unsupported Compiler"
#endif

#define LT_SIZE_MIN             ((LT_SIZE)(0))
#define LT_U8_MIN                   ((u8 )(0))
#define LT_U16_MIN                  ((u16)(0))
#define LT_U32_MIN                  ((u32)(0))
#define LT_U64_MIN                  ((u64)(0))
#define LT_U8_MAX                   ((u8 )(0xFF))
#define LT_U16_MAX                  ((u16)(0xFFFF))
#define LT_U32_MAX                  ((u32)(0xFFFFFFFF))
#define LT_U64_MAX      ((u64)(LT_CONSTU64(0xFFFFFFFFFFFFFFFF)))
#define LT_S8_MAX                   ((s8 )(0x7F))
#define LT_S16_MAX                  ((s16)(0x7FFF))
#define LT_S32_MAX                  ((s32)(0x7FFFFFFF))
#define LT_S64_MAX      ((s64)(LT_CONSTS64(0x7FFFFFFFFFFFFFFF)))
#define LT_S8_MIN                   ((s8 )((-LT_S8_MAX ) - ((s8 )1)))
#define LT_S16_MIN                  ((s16)((-LT_S16_MAX) - ((s16)1)))
#define LT_S32_MIN                  ((s32)((-LT_S32_MAX) - ((s32)1)))
#define LT_S64_MIN                  ((s64)((-LT_S64_MAX) - ((s64)LT_CONSTS64(1))))
#define LT_INT_MIN                  ((-(LT_INT_MAX)) - 1)
#define LT_UINT_MAX                 (((LT_INT_MAX)*2U) + 1U)
#define LT_DBL_MAX                  (0x1.FFFFFFFFFFFFFp1023)

#define LT_NAN                      (0.0/0.0)
#define LT_INFINITY                 (1.0/0.0)
#define LT_HUGE_VAL                 LT_INFINITY

#if (LT_ARCHITECTURE_BITS == 64)
    #define LT_SIZE_MAX             ((LT_SIZE)(LT_CONSTU64(0xFFFFFFFFFFFFFFFF)))
    #define LT_SSIZE_MAX            ((LT_SSIZE)(LT_CONSTS64(0x7FFFFFFFFFFFFFFF)))
#elif (LT_ARCHITECTURE_BITS == 32)
    #define LT_SIZE_MAX             ((LT_SIZE)(0xFFFFFFFF))
    #define LT_SSIZE_MAX            ((LT_SSIZE)(0x7FFFFFFF))
#else
    #error "Couldn't determine LT_ARCHITECTURE_BITS"
#endif

/**************************************
 * 6. PRINTF STYLE FORMAT STRING DEFS */

/* LT supports platform independent format string specification for the LT core types.
   In order to print out the following types, use the indicated formatting strings with
   the indicated casting macro.  This will ensure that these types are specified properly
   across 32 bit and 64 bit targets.

              Type    Formatting strings      Casting macro
              ____    __________________      _____________
               s32      "%ld",  "%lx"         LT_Ps32(var);
               u32      "%lu",  "%lx"         LT_Pu32(var);
               s64      "%lld", "%llx"        LT_Ps64(var);
               u64      "%llu", "%llx"        LT_Pu64(var);
           LT_SIZE      "%lu",  "%lx"         LT_PLT_SIZE(var);
          LT_SSIZE      "%ld",  "%ld"         LT_PLT_SSIZE(var);
         LT_HANDLE      "%lu",  "%lx"         LT_PLT_HANDLE(var);

 Example:
    u32 nBinCount   = GetCount();
    u64 nGrainCount = GetNumGrains();
    LT_GetCore()->ConsolePrint("There are %llu grains of grain in %lu bins\n", LT_Pu64(nGrainCount), LT_Pu32(nBinCount));
*/
    #define LT_Pu32(x)         ((long unsigned) (x))  /*  "%lu", LT_Pu32(u32Var) */
    #define LT_Ps32(x)         ((long   signed) (x))  /*  "%ld", LT_Ps32(s32Var) */
    #define LT_Pu64(x)    ((long long unsigned) (x))  /* "%llu", LT_Pu64(u64Var) */
    #define LT_Ps64(x)    ((long long   signed) (x))  /* "%lld", LT_Ps64(s64Var) */
    #define LT_PLT_SIZE(x)     ((long unsigned) (x))  /*  "%lu", LT_PLT_SIZE(LT_SIZE_var); */
    #define LT_PLT_SSIZE(x)    ((long   signed) (x))  /*  "%ld", LT_PLT_SSIZE(LT_SSIZE_var); */
    #define LT_PLT_HANDLE(x)   ((long unsigned) (x))  /*  "%lx", LT_PLT_HANDLE(LT_HANDLE_var); */

    /* decorators for compiler printf format string error checking */
#if defined (_MSC_VER) /* Microsoft C/C++ Compiler */
    /* no-op on windows */
    #define LT_PRINTF_FORMAT_FUNCTION(nFormatStringArgPos)
    #define LT_PRINTF_FORMAT_INTERFACE_MEMBER_FUNCTION(nFormatStringArgPos)
#elif defined (__GNUC__)        /* gcc compiler */
    // decorators for compiler printf format string error checking
    // the second form for member functions adjusts for 'this' pointer
    #define LT_PRINTF_FORMAT_FUNCTION(nFormatStringArgPos) __attribute__ ((format (printf, (nFormatStringArgPos), (nFormatStringArgPos+1))))
    #define LT_PRINTF_FORMAT_INTERFACE_MEMBER_FUNCTION(nFormatStringArgPos) __attribute__ ((format (printf, (nFormatStringArgPos+1), (nFormatStringArgPos+2))))
#else
    #error "Unsupported Compiler"
#endif

/*********************************
 * 7. MARKERS AND UTILITY MACROS */
#undef  LT_ISR_SAFE
#define LT_ISR_SAFE
    /**< Indicates that a function may run in interrupt contexts.
     *
     * In addition, native LT functions with this marker may be invoked
     * in thread contexts when interrupts are disabled.
     *
     * NB: Functions WITHOUT this marker must NOT be invoked in interrupt
     * contexts or thread contexts when interrupts are disabled.
     */

#undef  LT_ASYNC
#define LT_ASYNC
    /**< Indicates asynchronous operation.
     *
     * Many functions in LT run asynchronously. This marker indicates that
     * the operation may not have completed when the function returns.
     * If the API has a callback registration mechanism, the caller may
     * have to wait for a notification on the callback before proceeding.
     *
     * Functions not labeled LT_ASYNC run synchronously.
     */

#define LT_STRINGIFY_HELPER(x)  #x
#define LT_STRINGIFY(x)         LT_STRINGIFY_HELPER(x)

#define LT_OFFSET_OF(type, member)               ((LT_SIZE)&((type *)0)->member)
    /**< Returns the byte offset of a structure member. */

#define LT_CONTAINER_OF(ptr, type, member)       ((type *)((u8 *)(ptr) - LT_OFFSET_OF(type, member)))
    /**< Returns a pointer to the structure containing the given structure member. */

#if defined (_MSC_VER)          /* Microsoft C/C++ Compiler */
    #define LT_UNUSED(x)        ((void)0)
    #define LT_USED_ARG(x)      ((void)0)
    #define LT_USED
    #define LT_VERBATIM
    #define LT_ALIGNED(x)       __declspec(align(x))
    #define LT_ALIGNED_MAX      __declspec(align(alignof(void*)))
    #define LT_SECTION(x)
    #define LT_WEAK
    #define LT_NORETURN
    #define LT_WARNUNUSEDRESULT
#elif defined (__GNUC__)        /* gcc compiler */
    #define LT_UNUSED(x)        ((void)x)
    #define LT_USED_ARG(x)      ((void)x)
    #define LT_USED             __attribute__((used))
    #define LT_USED_CONSTRUCTOR __attribute__((used, constructor))
    #define LT_VERBATIM         __attribute__((naked))
    #define LT_ALIGNED(x)       __attribute__((aligned(x)))
    #define LT_ALIGNED_MAX      __attribute__((aligned))
    #define LT_SECTION(x)       __attribute__((section(x)))
    #define LT_WEAK             __attribute__((weak))
    #define LT_NORETURN         __attribute__((noreturn))
    #define LT_WARNUNUSEDRESULT __attribute__((warn_unused_result))
#else
    #error "Unsupported Compiler"
#endif

/* preprocessor message output */
#if defined (_MSC_VER)          /* Microsoft C/C++ Compiler */
    #define LTTYPES_CPP_EXECUTE_PRAGMA(x) __pragma(x)
    #define LTTYPES_CPP_PRAGMA_MESSAGE(msg) LTTYPES_CPP_EXECUTE_PRAGMA(message (msg))
#elif defined (__GNUC__)        /* gcc compiler */
    #define LTTYPES_CPP_EXECUTE_PRAGMA(x) _Pragma(#x)
    #define LTTYPES_CPP_PRAGMA_MESSAGE(msg) LTTYPES_CPP_EXECUTE_PRAGMA(message (msg))
#else
    #error "Unsupported Compiler"
#endif

/* static assert helper macros */
#ifdef __cplusplus
    #define LT_STATIC_ASSERT(x, w) static_assert((x), w)
#else
    #define LT_STATIC_ASSERT(x, w) _Static_assert((x), w)
#endif
#if LT_ARCHITECTURE_BITS == 32
    #define LT_STATIC_ASSERT_SIZE_32_64(type, size32, size64) \
        LT_STATIC_ASSERT(sizeof(type) == size32, "Type " #type " size should be " #size32);
#else
    #define LT_STATIC_ASSERT_SIZE_32_64(type, size32, size64) \
        LT_STATIC_ASSERT(sizeof(type) == size64, "Type " #type " size should be " #size64);
#endif
/**<
 *  compile time assert for sanity checking structure sizes on both 32 and 64 bit systems
 *
 *  For any given structure we want the total size to be a multiple of 64 bits on both 32 bit and 64 bit systems.
 *  To do this we order our items from largest to smallest, putting pointers/LT_Size variables in pairs, 32 bit variables
 *  in pairs and leftover 16 and 8 bit variables organized so they add up to 64 bit multiples, making sure that 16 bit variables
 *  are 16 bit aligned and these leftover variables sum 64 bit multiples adding explicit padding as necessary so we can preserve
 *  the structure size (without any compiler padding) a multiple of 64 bits.  That's because malloc will pad anyway to 64 bit boundaries
 *  on both 32bit and 64bit systems and we want to own any padding explicitly.  Use LT_STATIC_ASSERT_SIZE_32_64 to
 *  sanity check your size calculations.
 *  <pre>Example:
 *  typedef struct GalacticDimensions {
 *      LT_SIZE width;
 *      LT_SIZE height;
 *  } GalacticDimensions;
 *  LT_STATIC_ASSERT_SIZE_32_64(GalacticDimensions, 8, 16);
 * </pre>
 *
 * @param type the type to sanity check sizeof
 * @param size32 the size in bytes you are asserting the structure is on 32 bit systems
 * @param size64 the size in bytes you are asserting the structure is on 64 bit systems
 */

/* check LTAtomic */
LT_STATIC_ASSERT_SIZE_32_64(LTAtomic, 4, 4);

/******************************
 * 8. MIN/MAX and ENDIANNESS */
#if defined (__cplusplus)
    #define LT_MINMAX(op, x, y, varX, varY)   \
        [&](){                                 \
            auto varX = (x);                    \
            auto varY = (y);                     \
            (void)(&varX == &varY);               \
            return ((varX op varY) ? varX : varY); \
        }()
    #define LT_CLIP_OXYCUTE(var, low, hi, varVar, varLow, varHi)                     \
        [&](){                                                                        \
            auto varVar = (var);                                                       \
            auto varLow = (low);                                                        \
            auto varHi  = (hi);                                                          \
            (void)(&varVar == &varLow);                                                   \
            (void)(&varLow == &varHi);                                                     \
            (void)(&varVar == &varHi);                                                      \
            return ((varLow <= varHi) ?                                                      \
                        ((varVar < varLow) ? varLow : ((varVar > varHi) ? varHi : varVar)) :  \
                        ((varVar < varHi)  ? varHi  : ((varVar > varLow) ? varLow : varVar))); \
        }()
#else
    #if defined (_MSC_VER)          /* Microsoft C Compiler */
        /* msvc doesn't support statements and declarations in expressions.  Do it the old fashioned way which
           evaluates the winner twice.  Expressions like LT_MIN(i++, j) can have unexpected consequences, beware!
         */
        #define LT_MINMAX(op, x, y, varX, varY) (((x) op (y)) ? (x) : (y))
        /* if msvc did support statements and declarations in expressions, we would use decltype(x) instead of __auto_type:
        #define LT_MINMAX(op,x,y,varX,varY) ({ \
            decltype(x) varX = (x);             \
            decltype(y) varY = (y);              \
            (void)(&varX == &varY);               \
            ((varX op varY) ? varX : varY); })
        */
        #define LT_CLIP_OXYCUTE(var, low, hi, varVar, varLow, varHi)                 \
            (((low) <= (hi)) ?                                                        \
                (((varVar) < (low)) ? (low) : (((varVar) > (hi)) ? (hi) : (varVar))) : \
                (((varVar) < (hi))  ? (hi)  : (((varVar) > (low)) ? (low) : (varVar))))
    #elif defined (__GNUC__)        /* gcc compiler */
        #define LT_MINMAX(op,x,y,varX,varY) ({ \
            __auto_type varX = (x);             \
            __auto_type varY = (y);              \
            (void)(&varX == &varY);               \
            ((varX op varY) ? varX : varY); })
        #define LT_CLIP_OXYCUTE(var, low, hi, varVar, varLow, varHi) ({        \
            __auto_type varVar = (var);                                         \
            __auto_type varLow = (low);                                          \
            __auto_type varHi  = (hi);                                            \
            (void)(&varVar == &varLow);                                            \
            (void)(&varLow == &varHi);                                              \
            (void)(&varVar == &varHi);                                               \
            ((varLow <= varHi) ?                                                      \
                ((varVar < varLow) ? varLow : ((varVar > varHi) ? varHi : varVar)) :   \
                ((varVar < varHi)  ? varHi  : ((varVar > varLow) ? varLow : varVar))); })
    #else
        #error "Unsupported Compiler"
    #endif
#endif

#define LT_MINMAX_INVOKE(op,  x, y,  counter) LT_MINMAX       (op, x, y, _lt_mm_X##counter, _lt_mm_Y##counter)
#define LT_MINMAX_UNIQUE(op,  x, y,  counter) LT_MINMAX_INVOKE(op, x, y, counter)

#define LT_CLIP_INVOKE(var, low, hi, counter) LT_CLIP_OXYCUTE(var, low, hi, _lt_rl_var##counter, _lt_rl_low##counter, _lt_rl_hi##counter)
#define LT_CLIP_UNIQUE(var, low, hi, counter) LT_CLIP_INVOKE(var, low, hi, counter)

/* LT_MIN and LT_MAX
     o evaluate each argument one time only so, for example, LT_MIN(++i, j) only increments i once because (++i) is only evaluated once
     o issue a compiler warning if the types being compared are incompatible (no silent automatic type coercion)
     o support  nested calls, e.g. LT_MIN(i, LT_MAX(j, LT_MIN(k, l)))
     o compile down to 2 instructions on most CPUs when using -Os
     o preprocessor expansion looks like this:
       LT_MIN(1, 2) ------------------------> ({ __auto_type _lt_mm_X0 = (1); __auto_type _lt_mm_Y0 = (2);   (void)(&_lt_mm_X0 == &_lt_mm_Y0); ((_lt_mm_X0 < _lt_mm_Y0) ? _lt_mm_X0 : _lt_mm_Y0); });
       LT_MIN(a, b++) ----------------------> ({ __auto_type _lt_mm_X1 = (a); __auto_type _lt_mm_Y1 = (b++); (void)(&_lt_mm_X1 == &_lt_mm_Y1); ((_lt_mm_X1 < _lt_mm_Y1) ? _lt_mm_X1 : _lt_mm_Y1); });
       LT_MIN(a, LT_MAX(b, LT_MIN(c, d))) --> ({ __auto_type _lt_mm_X4 = (a); __auto_type _lt_mm_Y4 = (({ __auto_type _lt_mm_X3 = (b); __auto_type _lt_mm_Y3 = (({ __auto_type _lt_mm_X2 = (c); __auto_type _lt_mm_Y2 = (d); (void)(&_lt_mm_X2 == &_lt_mm_Y2); ((_lt_mm_X2 < _lt_mm_Y2) ? _lt_mm_X2 : _lt_mm_Y2); })); (void)(&_lt_mm_X3 == &_lt_mm_Y3); ((_lt_mm_X3 > _lt_mm_Y3) ? _lt_mm_X3 : _lt_mm_Y3); })); (void)(&_lt_mm_X4 == &_lt_mm_Y4); ((_lt_mm_X4 < _lt_mm_Y4) ? _lt_mm_X4 : _lt_mm_Y4); })
 */
#define LT_MIN(x, y)            LT_MINMAX_UNIQUE( <, x, y, __COUNTER__)
#define LT_MAX(x, y)            LT_MINMAX_UNIQUE( >, x, y, __COUNTER__)

/* LT_CLIP
   o clips var such that var >= low && var <= hi
   o issue a compiler warning if the types being clipped to are incompatible (no silent automatic type coercion)
   o support  nested calls, e.g. LT_CLIP(i, LT_CLIP(j, k, l), LT_CLIP(m, n, o))
   o if (low > hi) treats hi as low and low as hi, so LT_CLIP(x, 1, 5) and LT_CLIP(x, 5, 1) are equivalent
*/
#define LT_CLIP(var, low, hi)   LT_CLIP_UNIQUE(var, low, hi, __COUNTER__)

/* ENDIANNESS */
#if defined (_MSC_VER)          /* Microsoft C/C++ Compiler */
     #if 1 /* treat windows as always little endian until a detection method is ascertained. */
        #define LT_LITTLEENDIAN
    #else
        #define LT_BIGENDIAN
    #endif
#elif defined (__GNUC__)        /* gcc compiler */
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define LT_LITTLEENDIAN
    #else
        #define LT_BIGENDIAN
    #endif
#else
    #error "Unsupported Compiler"
#endif

/* ISSUE: Make sure these don't go into every translation unit if they aren't used. */
LT_INLINE u16 LT_SWAP16(u16 x) { return (((x >>  8) &             0x00FF) | ((x <<  8) &             0xFF00)); }
LT_INLINE u32 LT_SWAP32(u32 x) { return (((x >> 24) &         0x000000FF) | ((x >>  8) &         0x0000FF00)   | ((x <<  8) &         0x00FF0000) | ((x << 24) &         0xFF000000)); }
LT_INLINE u64 LT_SWAP64(u64 x) { return (((x >> 56) & 0x00000000000000FF) | ((x >> 40) & 0x000000000000FF00)   | ((x >> 24) & 0x0000000000FF0000) | ((x >>  8) & 0x00000000FF000000)   | ((x << 8) & 0x000000FF00000000) | ((x << 24) & 0x0000FF0000000000) | ((x << 40) & 0x00FF000000000000) | ((x << 56) & 0xFF00000000000000)); }

#if defined(LT_LITTLEENDIAN)
    #define LT_LE16(x)          (x)
    #define LT_LE32(x)          (x)
    #define LT_LE64(x)          (x)
    #define LT_LE_LT_SIZE(x)    (x)
    #define LT_BE16             LT_SWAP16
    #define LT_BE32             LT_SWAP32
    #define LT_BE64             LT_SWAP64
#if LT_ARCHITECTURE_BITS == 32
    #define LT_BE_LT_SIZE       LT_SWAP32
#else
    #define LT_BE_LT_SIZE       LT_SWAP64
#endif
    #define LT_HTONS            LT_SWAP16
    #define LT_HTONL            LT_SWAP32
    #define LT_HTONLL           LT_SWAP64
    #define LT_NTOHS            LT_SWAP16
    #define LT_NTOHL            LT_SWAP32
    #define LT_NTOHLL           LT_SWAP64
#elif defined (LT_BIGENDIAN)
    #define LT_LE16             LT_SWAP16
    #define LT_LE32             LT_SWAP32
    #define LT_LE64             LT_SWAP64
#if LT_ARCHITECTURE_BITS == 32
    #define LT_LE_LT_SIZE       LT_SWAP32
#else
    #define LT_LE_LT_SIZE       LT_SWAP64
#endif
    #define LT_BE16(x)          (x)
    #define LT_BE32(x)          (x)
    #define LT_BE64(x)          (x)
    #define LT_BE_LT_SIZE(x)    (x)
    #define LT_HTONS(x)         (x)
    #define LT_HTONL(x)         (x)
    #define LT_HTONLL(x)        (x)
    #define LT_NTOHS(x)         (x)
    #define LT_NTOHL(x)         (x)
    #define LT_NTOHLL(x)        (x)
#else
    #error "LT ENDIANNESS UNDEFINED"
#endif

/**************************************************************************************
 * 9. Ram Critical section decorators.  USE WISELY AND SPARINGLY!                    *
 *     (always with empirical evidence collected to justify use)                    *
 *\________________________________________________________________________________*
 * USE THESE ONLY if you have empirical evidence to justify their use!            /
 *                                                                               /
 * The following two macros may be used in .c files to decorate functions       /
 * and const rodata to locate them in RAM instead of in Flash.  They should    /
 * only be used on identified problem hotspots that are continuously          /
 * thrashing in and out of the instruction and data caches from Flash.       /
 *                                                                          /
 * 1. LT_TEXT_RAM_CRITICAL(n)    where n == [1..3]                         /
 *      For decorating functions that continuously thrash in and out of   /
 *      the instruction cache from Flash.                                /
 * 2. LT_RODATA_RAM_CRITICAL(n)  where n == [1..3]                      /
 *      For decorating constant data that continuously thrashes in     /
 *      and out of the data cache from Flash.                         /
 *                                                                   /
 * Each value of n in [1..3] corresponds to a discrete section in   /
 * RAM for holding either text or rodata.  These discrete sections /
 * may or may not have different performance characteristics.     /
 * In all cases n=1 signifies the most critical.                 /
 *                                                              /
 * A platform's Makefile.config may selectively limit the      /
 * effective values of n by specifying values for:            /
 *   LT_TEXT_RAM_CRITICAL_THRESHOLD, and/or                  /
 *   LT_RODATA_RAM_CRITICAL_THRESHOLD                       /
 * These macros may be set to the following values:        /
 *   0: placement in RAM for text or rodata disabled      /
 *   1: placement in RAM occurs for n=1 only             /
 *   2: placement in RAM occurs for n=1,2 only          /
 *   3: placement in RAM occurs for n=1,2,3 (default)  /
 * _________________________________________________ */
/*_________________________________________________
  HELPER MACROS: DO NOT USE!  (SCROLL DOWN)        */
#if defined (__GNUC__)         /* gcc compiler */
    /* don't use these macros; scroll past this #if defined (__GNUC__) block */
    #ifndef     LT_TEXT_RAM_CRITICAL_SECTION_1
        #define LT_TEXT_RAM_CRITICAL_SECTION_1      .lt_text_ram_critical_1
    #endif
    #ifndef     LT_TEXT_RAM_CRITICAL_SECTION_2
        #define LT_TEXT_RAM_CRITICAL_SECTION_2      .lt_text_ram_critical_2
    #endif
    #ifndef     LT_TEXT_RAM_CRITICAL_SECTION_3
        #define LT_TEXT_RAM_CRITICAL_SECTION_3      .lt_text_ram_critical_3
    #endif
    #ifndef     LT_RODATA_RAM_CRITICAL_SECTION_1
        #define LT_RODATA_RAM_CRITICAL_SECTION_1    .lt_rodata_ram_critical_1
    #endif
    #ifndef     LT_RODATA_RAM_CRITICAL_SECTION_2
        #define LT_RODATA_RAM_CRITICAL_SECTION_2    .lt_rodata_ram_critical_2
    #endif
    #ifndef     LT_RODATA_RAM_CRITICAL_SECTION_3
        #define LT_RODATA_RAM_CRITICAL_SECTION_3    .lt_rodata_ram_critical_3
    #endif
    #ifndef     LT_TEXT_RAM_CRITICAL_THRESHOLD
        #define LT_TEXT_RAM_CRITICAL_THRESHOLD      3
    #endif
    #ifndef     LT_RODATA_RAM_CRITICAL_THRESHOLD
        #define LT_RODATA_RAM_CRITICAL_THRESHOLD    3
    #endif
    #if         LT_TEXT_RAM_CRITICAL_THRESHOLD    > 0
        #define LT_TEXT_RAM_CRITICAL_1              __attribute__((section(LT_STRINGIFY(LT_TEXT_RAM_CRITICAL_SECTION_1))))
    #else
        #define LT_TEXT_RAM_CRITICAL_1
    #endif
    #if         LT_TEXT_RAM_CRITICAL_THRESHOLD    > 1
        #define LT_TEXT_RAM_CRITICAL_2              __attribute__((section(LT_STRINGIFY(LT_TEXT_RAM_CRITICAL_SECTION_2))))
    #else
        #define LT_TEXT_RAM_CRITICAL_2
    #endif
    #if         LT_TEXT_RAM_CRITICAL_THRESHOLD    > 2
        #define LT_TEXT_RAM_CRITICAL_3              __attribute__((section(LT_STRINGIFY(LT_TEXT_RAM_CRITICAL_SECTION_3))))
    #else
        #define LT_TEXT_RAM_CRITICAL_3
    #endif
    #if         LT_RODATA_RAM_CRITICAL_THRESHOLD  > 0
        #define LT_RODATA_RAM_CRITICAL_1            __attribute__((section(LT_STRINGIFY(LT_RODATA_RAM_CRITICAL_SECTION_1))))
    #else
        #define LT_RODATA_RAM_CRITICAL_1
    #endif
    #if         LT_RODATA_RAM_CRITICAL_THRESHOLD  > 1
        #define LT_RODATA_RAM_CRITICAL_2            __attribute__((section(LT_STRINGIFY(LT_RODATA_RAM_CRITICAL_SECTION_2))))
    #else
        #define LT_RODATA_RAM_CRITICAL_2
    #endif
    #if         LT_RODATA_RAM_CRITICAL_THRESHOLD  > 2
        #define LT_RODATA_RAM_CRITICAL_3            __attribute__((section(LT_STRINGIFY(LT_RODATA_RAM_CRITICAL_SECTION_3))))
    #else
        #define LT_RODATA_RAM_CRITICAL_3
    #endif
#endif
/* _________________________________________________________________________________
 * USE THESE MACROS! But ONLY if you have empirical evidence to justify their use! /
 *                                     ___________________________________________/
 * 1. LT_TEXT_RAM_CRITICAL(n)       __/
 * 2. LT_RODATA_RAM_CRITICAL(n)  __/
 * _____________________________/                      */
#if defined (_MSC_VER) || defined (__clang__)
    #define   LT_TEXT_RAM_CRITICAL(n)
    #define LT_RODATA_RAM_CRITICAL(n)
#elif defined (__GNUC__)     /* gcc compiler */
    #define   LT_TEXT_RAM_CRITICAL(n)   LT_TEXT_RAM_CRITICAL_##n
    #define LT_RODATA_RAM_CRITICAL(n) LT_RODATA_RAM_CRITICAL_##n
#else
    #error "Unsupported Compiler"
#endif

/***************************************************************************************
 * 9.4. LTBitfield
 *
 * Macros for packing/unpacking bits out of 32-bit bitfields.
 ***************************************************************************************/

#define LTBitfieldGet(field, mask, shift) ((field >> shift) & mask)
#define LTBitfieldSet(field, value, mask, shift) (field) = ((field & ~(u32)(mask << shift)) | ((u32)(value & mask) << shift))

/***************************************************************************************
 * 9.5. LTTrace
 *
 * These types and macros implement low-overhead tracepoints that can be exposed throughout
 * the system for improved visibility.  By default, trace streams are disabled and attempting
 * to write to them amounts to no more than a load-compare-branch.
 ***************************************************************************************/

typedef enum {
    kLTTrace_Type_Descriptor,
    kLTTrace_Type_String,
    kLTTrace_Type_Numbers,
} LTTracePayloadType;

enum {
    kLTTrace_State_EnabledMask = 0x1,
    kLTTrace_State_EnabledShift = 0,
    kLTTrace_State_RecordedMask = 0x1,
    kLTTrace_State_RecordedShift = 1,
    kLTTrace_State_IdMask = 0xff,
    kLTTrace_State_IdShift = 24,
};

typedef struct LTTraceStream LTTraceStream;

struct LTTraceStream {
    u32 state;
    const char *name;
    LTTraceStream *next;
};

/* ************************************************************************** */
/*                           Public Trace Interface                           */
/* ************************************************************************** */
#if !defined(LTLIBRARY_INCLUDE_LTTRACE)
    #define LTLIBRARY_INCLUDE_LTTRACE 1
#endif

/* --------------------------- Stream Declaration --------------------------- */

#define declare_LTTRACE_STREAM_EXTERN(_LIB_, _NAME_) IMPL_declare_LTTRACE_STREAM(_LIB_, _NAME_)
#define declare_LTTRACE_STREAM(_NAME_) declare_LTTRACE_STREAM_EXTERN(CURRENTLY_BUILDING_LTLIBRARY, _NAME_)
#define define_LTTRACE_STREAMS(_NAME_)  IMPL_define_LTTRACE_STREAMS(CURRENTLY_BUILDING_LTLIBRARY, _NAME_)
#define define_OBJECT_LTTRACE_STREAMS(_NAME_)  IMPL_define_OBJECT_LTTRACE_STREAMS(CURRENTLY_BUILDING_LTLIBRARY, _NAME_)

/* --------------------------- Stream Writers ------------------------------- */

#define LTTRACE_STREAM_EXTERN(_LIB_, _NAME_) IMPL_LTTRACE_NAME_1(_LIB_, _NAME_)
#define LTTRACE_STREAM(_NAME_) LTTRACE_STREAM_EXTERN(CURRENTLY_BUILDING_LTLIBRARY, _NAME_)

#define LTTRACE_STRING_EXTERN(_LIB_, _STREAM_, _ID_, _NAME_) \
    IMPL_LTTRACE_SUBMIT(_LIB_, _STREAM_, kLTTrace_Type_String, _ID_, _NAME_)
#define LTTRACE_STRING(_STREAM_, _ID_, _NAME_) LTTRACE_STRING_EXTERN(CURRENTLY_BUILDING_LTLIBRARY, _STREAM_, _ID_, _NAME_)

#define LTTRACE_NUMERIC_EXTERN(_LIB_, _NAME_, ...) \
    IMPL_LTTRACE_NUMERIC(_LIB_, _NAME_, LTTYPES_COUNT_VARIADIC_ARGUMENTS(__VA_ARGS__), __VA_ARGS__)
#define LTTRACE_NUMERIC(_NAME_, ...) LTTRACE_NUMERIC_EXTERN(CURRENTLY_BUILDING_LTLIBRARY, _NAME_, __VA_ARGS__)

#define LTTRACE_U64(_VALUE_) (u32)((u64)_VALUE_ & 0xffffffff), (u32)((u64)_VALUE_ >> 32)
#if LT_ARCHITECTURE_BITS == 64
#define LTTRACE_PTR(_VALUE_) LTTRACE_U64(_VALUE_)
#else
#define LTTRACE_PTR(_VALUE_) _VALUE_
#endif

#define LTTRACE_PACK1111(_1_, _2_, _3_, _4_)                                                                       \
    ((_1_ & 0xff) | (((_2_) & 0xff) << 8) | (((_3_) & 0xff) << 16) | (((_4_) & 0xff) << 24))

#define LTTRACE_PACK112(_1_, _2_, _3_)                                                                       \
    ((_1_ & 0xff) | (((_2_) & 0xff) << 8) | (((_3_) & 0xffff) << 16))

/* ----------------------------- Debugging GPIO ----------------------------- */

#define LTTRACE_GPIO(name, pin, value) LTTRACE_NUMERIC(name, LTTRACE_PACK1111(0x0, 0x0, pin, value))
#define LTTRACE_GPIO_EXTERN(lib, name, pin, value) LTTRACE_NUMERIC_EXTERN(lib, name, LTTRACE_PACK1111(0x0, 0x0, pin, value))

/* ************************************************************************** */
/*                           Private Utility Macros                           */
/* ************************************************************************** */

#define IMPL_LTTRACE_NAME_2(_LIB_, _NAME_) s_##_LIB_##Trace_##_NAME_
#define IMPL_LTTRACE_NAME_1(_LIB_, _NAME_) IMPL_LTTRACE_NAME_2(_LIB_, _NAME_)
#define IMPL_LTTRACE_NAME(_NAME_)               IMPL_LTTRACE_NAME_1(CURRENTLY_BUILDING_LTLIBRARY, _NAME_)
#define IMPL_LTTRACE_STREAMS(_LIBRARY_)         s_p##_LIBRARY_##_ExportedStreams
#define IMPL_LTTRACE_STREAMS_PTR(_LIBRARY_)     s_pp##_LIBRARY_##_ExportedStreams

/* ************************************************************************** */
/*                        Private Implementation Macros                       */
/* ************************************************************************** */

/* ----------------------------- LTTRACE_NUMERIC ---------------------------- */

#define IMPL_LTTRACE_NUMERIC_ARGS_1(_ARG1_)                 (s32) _ARG1_
#define IMPL_LTTRACE_NUMERIC_ARGS_2(_ARG1_, _ARG2_)         (s32) _ARG1_, (s32)_ARG2_
#define IMPL_LTTRACE_NUMERIC_ARGS_3(_ARG1_, _ARG2_, _ARG3_) (s32) _ARG1_, (s32)_ARG2_, (s32)_ARG3_
#define IMPL_LTTRACE_NUMERIC_ARGS_4(_ARG1_, _ARG2_, _ARG3_, _ARG4_) \
    (s32) _ARG1_, (s32)_ARG2_, (s32)_ARG3_, (s32)_ARG4_
#define IMPL_LTTRACE_NUMERIC_N(_LIB_, _NAME_, _COUNT_, ...) \
    IMPL_LTTRACE_SUBMIT(_LIB_, _NAME_,                      \
                        kLTTrace_Type_Numbers,       \
                        _COUNT_,                     \
                        IMPL_LTTRACE_NUMERIC_ARGS_##_COUNT_(__VA_ARGS__))
#define IMPL_LTTRACE_NUMERIC(_LIB_, _NAME_, _COUNT_, ...) \
    IMPL_LTTRACE_NUMERIC_N(_LIB_, _NAME_, _COUNT_, __VA_ARGS__)
/* ----------------------------- Code Generation ---------------------------- */

#if LTLIBRARY_INCLUDE_LTTRACE
    #define IMPL_declare_LTTRACE_STREAM(_LIB_, _NAME_) extern LTTraceStream IMPL_LTTRACE_NAME_1(_LIB_, _NAME_)
    #define IMPL_define_LTTRACE_STREAMS(_LIBRARY_, _NAME_) \
        IMPL_define_LTTRACE_STREAMS_1(_LIBRARY_, _NAME_)
    #define IMPL_define_LTTRACE_STREAMS_1(_LIBRARY_, _NAME_) \
        IMPL_define_LTTRACE_STREAMS_2(_LIBRARY_, _NAME_)
    #define IMPL_define_OBJECT_LTTRACE_STREAMS(_LIBRARY_, _NAME_) \
        IMPL_define_OBJECT_LTTRACE_STREAMS_1(_LIBRARY_, _NAME_)
    #define IMPL_define_OBJECT_LTTRACE_STREAMS_1(_LIBRARY_, _NAME_) \
        IMPL_define_LTTRACE_STREAMS_2(_LIBRARY_##Lib, _NAME_)
    #define IMPL_define_LTTRACE_STREAMS_2(_LIBRARY_, _SEQ_)    \
        IMPL_define_LTTRACE_STREAM(_SEQ_) static LTTraceStream \
            *IMPL_LTTRACE_STREAMS(_LIBRARY_)[] = {             \
                IMPL_define_LTTRACE_STREAMREF(_SEQ_) NULL,     \
        };                                                     \
        static LTTraceStream **s_pp##_LIBRARY_##_ExportedStreams = IMPL_LTTRACE_STREAMS(_LIBRARY_);
    #define IMPL_LTTRACE_SUBMIT(_LIB_, _NAME_, ...)                \
        if (LTBitfieldGet(IMPL_LTTRACE_NAME_1(_LIB_, _NAME_).state, kLTTrace_State_EnabledMask, kLTTrace_State_EnabledShift) && LT_GetCore()) \
        LT_GetCore()->Trace(&IMPL_LTTRACE_NAME_1(_LIB_, _NAME_), __VA_ARGS__)

    #define IMPL_define_LTTRACE_STREAM(_SEQ_) \
        LTTYPES_JOIN_ARGUMENTS(IMPL_define_LTTRACE_STREAM_1 _SEQ_, _END)
    #define IMPL_define_LTTRACE_STREAM_1(_STREAM_, ...) \
        IMPL_define_LTTRACE_STREAM_CODE(_STREAM_) IMPL_define_LTTRACE_STREAM_2
    #define IMPL_define_LTTRACE_STREAM_1_END
    #define IMPL_define_LTTRACE_STREAM_2(_STREAM_, ...) \
        IMPL_define_LTTRACE_STREAM_CODE(_STREAM_) IMPL_define_LTTRACE_STREAM_1
    #define IMPL_define_LTTRACE_STREAM_2_END
    // clang-format off
    #define IMPL_define_LTTRACE_STREAM_CODE(_STREAM_)          \
        LTTraceStream IMPL_LTTRACE_NAME(_STREAM_) = {                    \
            .state = (u32)kLTTrace_State_IdMask << kLTTrace_State_IdShift LTTYPES_DEFERRED_COMMA_EXPANSION \
            .name  = #_STREAM_ LTTYPES_DEFERRED_COMMA_EXPANSION                               \
            .next  = NULL LTTYPES_DEFERRED_COMMA_EXPANSION                                    \
        };
    // clang-format on

    #define IMPL_define_LTTRACE_STREAMREF(_SEQ_) \
        LTTYPES_JOIN_ARGUMENTS(IMPL_define_LTTRACE_STREAMREF_1 _SEQ_, _END)
    #define IMPL_define_LTTRACE_STREAMREF_1(_STREAM_, ...) \
        IMPL_define_LTTRACE_STREAMREF_CODE(_STREAM_) IMPL_define_LTTRACE_STREAMREF_2
    #define IMPL_define_LTTRACE_STREAMREF_1_END
    #define IMPL_define_LTTRACE_STREAMREF_2(_STREAM_, ...) \
        IMPL_define_LTTRACE_STREAMREF_CODE(_STREAM_) IMPL_define_LTTRACE_STREAMREF_1
    #define IMPL_define_LTTRACE_STREAMREF_2_END
    #define IMPL_define_LTTRACE_STREAMREF_CODE(_STREAM_) \
        &IMPL_LTTRACE_NAME(_STREAM_) LTTYPES_DEFERRED_COMMA_EXPANSION
#else
    #define IMPL_declare_LTTRACE_STREAM(...)
    #define IMPL_define_LTTRACE_STREAMS(...)
    #define IMPL_LTTRACE_SUBMIT(...)
#endif

/***************************************************************************************
 * 10. LTCore, LTLibrary, LTInterface, LTHandle, LTBuffer, LTArgs, LTResource, and LTBootReason typedefs and enum constants */
/* ______________________________
 * Interface & Handle Typedefs */
typedef const struct LTCoreApi                    LTCore;
typedef const struct LTStdlibApi                  LTStdlib;
typedef const struct LTLibraryBase                LTLibrary;
typedef const struct LTInterfaceBase              LTInterface;
typedef const struct LTDeviceLibraryBase          LTDeviceLibrary;
typedef const struct LTDriverLibraryBase          LTDriverLibrary;

/* _____________________________________________
            LTHandle - the bane of my existence */
typedef u32 LTHandle;
typedef     LTHandle   LTDeviceUnit;
                    /* LTDeviceUnit - more bane */

#define LTHANDLE_INVALID            ((LTHandle)0)               /**< for testing handle validity                                    */
#define LTHANDLE_TO_VOIDPTR(handle) ((void *)(LT_SIZE)(handle)) /**< for passing handles as void *clientData, e.g. to QueueTaskProc */
#define VOIDPTR_TO_LTHANDLE(ptr)    ((LTHandle)(LT_SIZE)(ptr))  /**< for converting void *clientData back into handles              */

/* ___________________________
            LTMemoryRegion  */
typedef u32 LTMemoryRegion;

/* ______________________________________________
 * LTLibrary name string length/size constants */
enum {
    kLTLibrary_MaxNameLen           = 39,
    kLTLibrary_MaxNameBufferSize    = kLTLibrary_MaxNameLen + 1,

    kLTInterface_MaxNameLen         = kLTLibrary_MaxNameLen,
    kLTInterface_MaxNameBufferSize  = kLTInterface_MaxNameLen + 1
};

/* _________________________________________________
 * LTInterface Prototype Interface Type constants */
typedef
 u32 LTInterfaceType;
enum LTInterfaceType_ {
    kLTInterfaceType_Interface          = 0,
    kLTInterfaceType_LibraryRoot        = 1,
    kLTInterfaceType_DeviceLibraryRoot  = 2,
    kLTInterfaceType_DriverLibraryRoot  = 3,
    kLTInterfaceType_LTObjectApi        = 4,
    /* Never reorder, modify or add to these explicit enum values; they are exclusively reserved
       for the LT prototype interfaces declared here in LTTYpes.h only. NEVER ADD TO THIS LIST.
    \/ ABANDON HOPE ALL YE WHO ENTER HERE \/
*/       kLTInterfaceType_GatesOfHell,
         kLTInterfaceType_Last  = (kLTInterfaceType_GatesOfHell - 1)
};

/* ________________________________________
 * LTLibrary Init/Fini function typedefs */
typedef bool (LTLibrary_LibInit_Proc)(void);
typedef void (LTLibrary_LibFini_Proc)(void);

/* _______________________________________
 * LTLibrary optional function typedefs */
typedef int (LTLibrary_RunProc)(int argc, const char ** argv);

/* _________________________________________
 * LTInterface optional function typedefs */
typedef void (LTInterface_OnDestroyHandleProc)(LTHandle handle);

/** __________________________________________________________
 * LTArg typedefs - for encoding event callback arguments */
typedef union LTArg {
    void *   lta_pointer;   /**< DOCUMENTATION_NEEDED */
    char *   lta_charstar;  /**< charstar represents null-terminated c-string */
    LT_SIZE  lta_ltsize;    /**< DOCUMENTATION_NEEDED */
    LTHandle lta_lthandle; /**< represents an LT Handle */
    double   lta_double;    /**< DOCUMENTATION_NEEDED */
    u8       lta_u8;        /**< DOCUMENTATION_NEEDED */
    u16      lta_u16;       /**< DOCUMENTATION_NEEDED */
    u32      lta_u32;       /**< DOCUMENTATION_NEEDED */
    u64      lta_u64;       /**< DOCUMENTATION_NEEDED */
    s8       lta_s8;        /**< DOCUMENTATION_NEEDED */
    s16      lta_s16;       /**< DOCUMENTATION_NEEDED */
    s32      lta_s32;       /**< DOCUMENTATION_NEEDED */
    s64      lta_s64;       /**< DOCUMENTATION_NEEDED */
} LTArg; /**< DOCUMENTATION_NEEDED */

typedef enum LTArgType {
    kLTArgType_void = 0,
    kLTArgType_pointer,
    kLTArgType_ltsize,
    kLTArgType_lthandle,
    kLTArgType_charstar,
    kLTArgType_double,
    kLTArgType_u8,
    kLTArgType_u16,
    kLTArgType_u32,
    kLTArgType_u64,
    kLTArgType_s8,
    kLTArgType_s16,
    kLTArgType_s32,
    kLTArgType_s64
} LTArgType;

/** DOCUMENTATION_NEEDED */
typedef struct LTArgsDescriptor {
    LT_SIZE     nNumArgs;     /**< 4 or 8, 8 aligned */
    LTArgType   argTypes[];   /**< 4 or 8 aligned, 4 ok */
} LTArgsDescriptor;

typedef struct LTArgsDescriptor LTArgs;

LT_INLINE LT_SIZE      LTArgs_AllocSize (LT_SIZE nNumArgs)               { return ((((sizeof(LTArgsDescriptor) + (sizeof(LTArgType) * nNumArgs)) + 7) & ~7) + (sizeof(LTArg) * nNumArgs)); }
LT_INLINE LTArgType    LTArgs_ArgTypeAt (LT_SIZE nIndex, LTArgs * pArgs) { return (pArgs && nIndex < pArgs->nNumArgs) ? pArgs->argTypes[nIndex] : (LTArgType)kLTArgType_void; }
LT_INLINE LTArg *      LTArgs_ArgAt     (LT_SIZE nIndex, LTArgs * pArgs) { return (((LTArg *)((((LT_SIZE)(&pArgs->argTypes[pArgs->nNumArgs])) + 7) & ~7)) + nIndex); }
LT_INLINE void *       LTArgs_pointerAt (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_pointer  == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_pointer  : NULL; }
LT_INLINE LT_SIZE      LTArgs_ltsizeAt  (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_ltsize   == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_ltsize   : 0; }
LT_INLINE LTHandle     LTArgs_lthandleAt(LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_lthandle == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_lthandle : 0; }
LT_INLINE char *       LTArgs_charstarAt(LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_charstar == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_charstar : NULL; }
LT_INLINE double       LTArgs_doubleAt  (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_double   == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_double   : 0.0; }
LT_INLINE u8           LTArgs_u8At      (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_u8       == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_u8       : 0; }
LT_INLINE u16          LTArgs_u16At     (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_u16      == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_u16      : 0; }
LT_INLINE u32          LTArgs_u32At     (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_u32      == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_u32      : 0; }
LT_INLINE u64          LTArgs_u64At     (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_u64      == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_u64      : 0; }
LT_INLINE s8           LTArgs_s8At      (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_s8       == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_s8       : 0; }
LT_INLINE s16          LTArgs_s16At     (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_s16      == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_s16      : 0; }
LT_INLINE s32          LTArgs_s32At     (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_s32      == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_s32      : 0; }
LT_INLINE s64          LTArgs_s64At     (LT_SIZE nIndex, LTArgs * pArgs) { return (kLTArgType_s64      == LTArgs_ArgTypeAt(nIndex, pArgs)) ? LTArgs_ArgAt(nIndex, pArgs)->lta_s64      : 0; }

/* ________________
 * LTBufferChain */
typedef struct LTBufferChain {
    struct LTBufferChain *next;        /**<  next buffer in the chain or null for end   */
    u8                   *buffer;      /**<  storage allocated/deallocated elsewhere   */
    u32                   size;        /**<  size of above buffer in bytes            */
    u32                   bytesUsed;   /**<  bytes actually used in the buffer       */
} LTBufferChain;
    /**< represents a chain of buffers */


/* _____________
 * LTResource */
#define kLTResourceKey_FirstChild  ((const char *)(LT_SIZE)(1))
#define kLTResourceKey_NextSibling ((const char *)(LT_SIZE)(2))

typedef void LTResourceTree;          /**< opaque data type representing an LTResourceTree                        */
typedef_LTENUM_SIZED(LTResourceValueType, u32) {
    kLTResourceValueType_None    = 0, /**< none value, suitable for initialization                            */
    kLTResourceValueType_Integer = 1, /**< a 64 bit signed integer value                                     */
    kLTResourceValueType_String  = 2, /**< a null terminated string value                                   */
    kLTResourceValueType_Binary  = 3, /**< a binary value                                                  */
    kLTResourceValueType_Object  = 4, /**< tree object node containing 0 or more child values             */
    kLTResourceValueType_Array   = 5  /**< tree array node containing 0 or more child values             */
};

typedef struct LTResourceValue {      /**< represents a resource entry in a resource tree                           */
    union {        s64 integer;       /**< copy of integer data value stored in resource tree                      */
            const char *string;       /**< pointer to null terminated c string data value stored in resource tree */
            const   u8 *binary;       /**< pointer to binary data value stored inside resource tree              */
    };
    const char           *name;
    LTResourceValueType   type;       /**< data type of this resource entry: integer, string or binary         */
                    u32   size;       /**< size in bytes of binary data or c string including null terminator */
                    u32 offset;       /**< when type == Object, offset location of object within tree        */
} LTResourceValue;

LT_STATIC_ASSERT_SIZE_32_64(LTResourceValue, 24, 32);

/* ______________________________
 * LTBootReason - DO NOT CHANGE /
 *
 * LTBootReason is a set of generic boot reasons for shared use between LTBootloader and application software.
 * IF THIS IS CHANGED THERE IS RISK OF BREAKING COMPATIBILITY WITH BOOTLOADERS THAT ARE ALREADY IN THE FIELD.
 * Several SoCs support a large set of boot reasons, they should NOT be maintained here. Please use other methods to
 * define detailed boot reasons for the given platform. */

typedef_LTENUM_SIZED(LTBootReason, u32) {
    /* ________________________________________________________________________________________________________________
     * DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE */
    kLTBootReason_Undefined = 0,  /**< Unknown, invalid or undefined */
    kLTBootReason_PowerOn,        /**< A normal power on */
    kLTBootReason_WatchdogReset,  /**< Watchdog timer resets indicating boot issue or app hang that could result in OTA partition failover */
    kLTBootReason_Reset,          /**< Internal resets including watchdog timers solely used for reset (not for hang detection) and the SoC reset line */
    kLTBootReason_ResetExternal,  /**< Like Reset above but from a source external to the CPU or SoC (including GPIO inputs being used for reset) */
    kLTBootReason_DeepSleep,      /**< Coming out of deep sleep mode (no memory retention) */
    kLTBootReason_NumReasons      /**< Number of defined boot reasons (for sanity checking) */
    /* ________________________________________________________________________________________________________________
     * DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE - DO NOT CHANGE */
};

/**********************************************************
 *  10. LTINTERFACE AND LTLIBRARY PROTOTYPE HELPER MACROS *
 *      don't use directly; proceed to section 11         *
 **********************************************************
 *
 * _____________________________________________
 * Typedefs for Library Export Binding Macros */
typedef LTLibrary * (LTLibrary_OpenLibrary_Proc)(LTCore * pLTCore);
typedef void        (LTLibrary_CloseLibrary_Proc)(LTLibrary * pLibrary);
typedef void        (LTLibrary_InitializeExportedInterfacesFunction)(void);
typedef bool        (LTLibrary_OpenDependentLibrariesFunction)(void);
typedef void        (LTLibrary_CloseDependentLibrariesFunction)(void);

typedef struct LTLibrary_LTObjectMapEntry {
    const char * specializationName;
    const char * objectApiName;
    const char * libraryName;
    struct LTLibrary_LTObjectMapEntry * pNextEntry;
} LTLibrary_LTObjectMapEntry;

typedef struct LTObjectApi LTObjectApi;

typedef struct LTLibrary_LTObjectApiListEntry {
    const LTObjectApi * objectApi;
    const struct LTLibrary_LTObjectApiListEntry * pNextEntry;
} LTLibrary_LTObjectApiListEntry;

typedef struct LTLibrary_MacroCreateObjectParms {
    const char *                       creatingLibrary;
    LTLibrary_LTObjectApiListEntry **  libraryExportedObjectList;
    LTLibrary_LTObjectApiListEntry **  libraryPrivateObjectList;
} LTLibrary_MacroCreateObjectParms;

/* ________________________________
 * Library Export Binding Macros */
#if LTLIBRARY_INCLUDE_LTTRACE
    #define DEFINE_EXPORT_BINDING_LTTRACE_STATIC(libName)   static LTTraceStream** IMPL_LTTRACE_STREAMS_PTR(libName);
#else
    #define DEFINE_EXPORT_BINDING_LTTRACE_STATIC(libName)
    #define DEFINE_EXPORT_BINDING_LTTRACE_LIBOPEN(libName)
    #define DEFINE_EXPORT_BINDING_LTTRACE_LIBCLOSE(libName)
#endif

#define LTLIBRARY_DEFINE_EXPORT_BINDING_FOR(cblName, libName, version) LTLIBRARY_DEFINE_EXPORT_BINDING(cblName, libName, version)
#ifdef LT_NO_DYNAMIC_LOADER
    #define LT_LIBRARY_EXPORT_DECL LT_EXTERN_C
    #if LTLIBRARY_INCLUDE_LTTRACE
        #define DEFINE_EXPORT_BINDING_LTTRACE_LIBOPEN(libName)  pCore->TraceAddStreams(IMPL_LTTRACE_STREAMS_PTR(libName));
        #define DEFINE_EXPORT_BINDING_LTTRACE_LIBCLOSE(libName) LT_GetCore()->TraceRemoveStreams(IMPL_LTTRACE_STREAMS_PTR(libName));
    #endif

    #define LTLIBRARY_DEFINE_GETLIBRARYBUILDVERSION(name) static const char * name##Impl_GetLibraryBuildVersion(void) { return LT_GetCore()->GetLTCoreLibraryBuildVersion(); }

    typedef struct LTStaticallyBoundLibraryEntry {
            const char *                            pLibName;
            struct LTStaticallyBoundLibraryEntry *  pNextEntry;
            LTLibrary_OpenLibrary_Proc *            pOpenLibraryFunc;
            LTLibrary_CloseLibrary_Proc *           pCloseLibraryFunc;
    } LTStaticallyBoundLibraryEntry;

    #define LTLIBRARY_DEFINE_EXPORT_BINDING(cblName, libName, version)                                      \
        DEFINE_EXPORT_BINDING_LTTRACE_STATIC(libName);                                                      \
        LTLibrary *     cblName##Impl_GetLibrary(void)             { return (LTLibrary *)&s_##libName; }    \
        LTInterfaceType cblName##Impl_GetObjectInterfaceType(void) { return kLTInterfaceType_LTObjectApi; } \
        void cblName##Impl_AddRef(LTObject *object)    { LT_GetCore()->AddObjectRef(object); }              \
        void cblName##Impl_RemoveRef(LTObject *object) { LT_GetCore()->RemoveObjectRef(object); }           \
        /* struct LTInterfaceExt * cblName##Impl_GetLTInterfaceExt(void) { return NULL; } */                \
        static void libName##Impl_InitializeExportedInterfaces(void);                                       \
        static bool libName##Impl_OpenDependentLibraries(void);                                             \
        static void libName##Impl_CloseDependentLibraries(void);                                            \
        static bool libName##Impl_LibInit(void);                                                            \
        static LT_USED LTLibrary * LTLibrary_StaticOpenLibrary(LTCore * pCore) {                            \
            LT_UNUSED(pCore);                                                                               \
            libName##Impl_InitializeExportedInterfaces();                                                   \
            if (!libName##Impl_OpenDependentLibraries()) {                                                  \
                libName##Impl_CloseDependentLibraries();                                                    \
                return NULL;                                                                                \
            }                                                                                               \
            DEFINE_EXPORT_BINDING_LTTRACE_LIBOPEN(libName);                                                 \
            return libName##Impl_LibInit() ? (LTLibrary *)&s_##libName : NULL;                              \
        }                                                                                                   \
        static void libName##Impl_LibFini(void);                                                            \
        static LT_USED void LTLibrary_StaticCloseLibrary(LTLibrary * pLibrary) {                            \
            LT_UNUSED(pLibrary);                                                                            \
            DEFINE_EXPORT_BINDING_LTTRACE_LIBCLOSE(libName);                                                \
            libName##Impl_LibFini();                                                                        \
            libName##Impl_CloseDependentLibraries();                                                        \
        }                                                                                                   \
        static LT_USED LTStaticallyBoundLibraryEntry s_entry = {                                            \
            #cblName,                                                                                       \
            NULL,                                                                                           \
            &LTLibrary_StaticOpenLibrary,                                                                   \
            &LTLibrary_StaticCloseLibrary                                                                   \
        };                                                                                                  \
        static LT_USED_CONSTRUCTOR void LTLibrary_RegisterLibrary(void) {                                   \
            extern LTStaticallyBoundLibraryEntry * g_pStaticallyBoundLTLibraryEntries;                      \
            s_entry.pNextEntry = g_pStaticallyBoundLTLibraryEntries;                                        \
            g_pStaticallyBoundLTLibraryEntries = &s_entry;                                                  \
        }
#else
    #if defined(_WIN32)
        #define LT_LIBRARY_EXPORT_DECL LT_EXTERN_C __declspec(dllexport)
    #elif defined (__GNUC__)
        #define LT_LIBRARY_EXPORT_DECL LT_EXTERN_C __attribute__((visibility("default")))
    #else
        #error "Unsupported Compiler"
    #endif

    #if LTLIBRARY_INCLUDE_LTTRACE
        #define DEFINE_EXPORT_BINDING_LTTRACE_LIBOPEN(libName)  pLTCore->TraceAddStreams(IMPL_LTTRACE_STREAMS_PTR(libName));
        #define DEFINE_EXPORT_BINDING_LTTRACE_LIBCLOSE(libName) if(s_pCore__) s_pCore__->TraceRemoveStreams(IMPL_LTTRACE_STREAMS_PTR(libName));
    #endif

    #define LTLIBRARY_DEFINE_GETLIBRARYBUILDVERSION(name) static const char * name##Impl_GetLibraryBuildVersion(void) { return LTTYPES_LTLIBRARY_BUILD_VERSION; }

    #define LTLIBRARY_DEFINE_EXPORT_BINDING(cblName, libName, version)                                      \
        static  LTCore *    s_pCore__   = NULL;                                                             \
        static  LTStdlib * s_pstdlib__ = NULL;                                                              \
        LTStdlib * LT_GetStdlib(void)  { return s_pstdlib__; }                                              \
        LTCore *   LT_GetCore(void)    { return s_pCore__;   }                                              \
        static bool libName##Impl_LibInit(void);                                                            \
        LTLibrary * cblName##Impl_GetLibrary(void) { return (LTLibrary *)&s_##libName; }                    \
        LTInterfaceType cblName##Impl_GetObjectInterfaceType(void) { return kLTInterfaceType_LTObjectApi; } \
        void cblName##Impl_AddRef(LTObject *object)    { LT_GetCore()->AddObjectRef(object);  }             \
        void cblName##Impl_RemoveRef(LTObject *object) { LT_GetCore()->RemoveObjectRef(object); }           \
        /* struct LTInterfaceExt * cblName##Impl_GetLTInterfaceExt(void) { return NULL; } */                \
        DEFINE_EXPORT_BINDING_LTTRACE_STATIC(libName);                                                      \
        LT_LIBRARY_EXPORT_DECL LTLibrary * LTLibrary_##cblName##_OpenLibrary(LTCore * pLTCore) {            \
            s_pCore__ = pLTCore;                                                                            \
            s_pstdlib__ = pLTCore->GetLTStdlib();                                                           \
            libName##Impl_InitializeExportedInterfaces();                                                   \
            if (!libName##Impl_OpenDependentLibraries()) {                                                  \
                libName##Impl_CloseDependentLibraries();                                                    \
                return NULL;                                                                                \
            }                                                                                               \
            DEFINE_EXPORT_BINDING_LTTRACE_LIBOPEN(libName);                                                 \
            if (libName##Impl_LibInit()) return (LTLibrary *)&s_##libName;                                  \
            else {                                                                                          \
                s_pstdlib__ = NULL;                                                                         \
                s_pCore__ = NULL;                                                                           \
                return NULL;                                                                                \
            }                                                                                               \
        }                                                                                                   \
        static void libName##Impl_LibFini(void);                                                            \
        LT_LIBRARY_EXPORT_DECL void LTLibrary_##cblName##_CloseLibrary(LTLibrary * pLibrary) {              \
            LT_UNUSED(pLibrary);                                                                            \
            libName##Impl_LibFini();                                                                        \
            libName##Impl_CloseDependentLibraries();                                                        \
            DEFINE_EXPORT_BINDING_LTTRACE_LIBCLOSE(libName);                                                \
            s_pstdlib__ = NULL;                                                                             \
            s_pCore__ = NULL;                                                                               \
        }
#endif

#ifndef DOXY_SKIP // [
/* ___________________________________________________
 * Helper Macros for selection of VARIADIC defaults */
#define LTTYPES_IS_MACRO_DEFINED(x) LTTYPES_IS_MACRO_DEFINED_VALUE(x)
#define LTTYPES_IS_VARIADIC_LIST_EMPTY(...) LTTYPES_IS_VARIADIC_LIST_EMPTY_SHIFT_AND_TEST(dummy, __VA_ARGS__)
#define LTTYPES_COUNT_VARIADIC_ARGUMENTS(...) LTTYPES_ARGCOUNT(__VA_ARGS__, LTTYPES_COUNT_BACKWARD())
#define LTTYPES_EXTRACT_FIRST_ARG(a, ...) a
#define LTTYPES_EXTRACT_SECOND_ARG(a, b, ...) b
#define LTTYPES_DISCARD_FIRST_ARG(a, ...) __VA_ARGS__
#define LTTYPES_FORCE_VARIADIC_EXPANSION(x) x
#define LTTYPES_PUSH_ARG_IF_0 0
#define LTTYPES_PUSH_ARG_IF_1 ~,
#define LTTYPES_IS_MACRO_DEFINED_RESULT(...) LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_SECOND_ARG(__VA_ARGS__))
#define LTTYPES_IS_MACRO_DEFINED_VALUE(x) LTTYPES_IS_MACRO_DEFINED_RESULT(LTTYPES_PUSH_ARG_IF_##x 1, 0)
#define LTTYPES_ARGCOUNT(...) LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_COUNT_FORWARD(__VA_ARGS__))
#define LTTYPES_COUNT_FORWARD(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, count, ...) count
#define LTTYPES_COUNT_BACKWARD() 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
#define LTTYPES_VARIADIC_TEST_FOR_EMPTY 1
#define LTTYPES_IS_VARIADIC_LIST_EMPTY_SHIFT_AND_TEST(dummy, ...) LTTYPES_IS_VARIADIC_LIST_EMPTY_TEST(LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_FIRST_ARG(EMPTY##__VA_ARGS__)))
#define LTTYPES_IS_VARIADIC_LIST_EMPTY_TEST(x) LTTYPES_IS_VARIADIC_LIST_EMPTY_TEST_RESULT(x)
#define LTTYPES_IS_VARIADIC_LIST_EMPTY_TEST_RESULT(x) LTTYPES_IS_MACRO_DEFINED(LTTYPES_VARIADIC_TEST_FOR_##x)

#define LTTYPES_EVAL(...)       LTTYPES_EVAL1024(__VA_ARGS__)
#define LTTYPES_EVAL1024(...)   LTTYPES_EVAL512(LTTYPES_EVAL512(__VA_ARGS__))
#define LTTYPES_EVAL512(...)    LTTYPES_EVAL256(LTTYPES_EVAL256(__VA_ARGS__))
#define LTTYPES_EVAL256(...)    LTTYPES_EVAL128(LTTYPES_EVAL128(__VA_ARGS__))
#define LTTYPES_EVAL128(...)    LTTYPES_EVAL64(LTTYPES_EVAL64(__VA_ARGS__))
#define LTTYPES_EVAL64(...)     LTTYPES_EVAL32(LTTYPES_EVAL32(__VA_ARGS__))
#define LTTYPES_EVAL32(...)     LTTYPES_EVAL16(LTTYPES_EVAL16(__VA_ARGS__))
#define LTTYPES_EVAL16(...)     LTTYPES_EVAL8(LTTYPES_EVAL8(__VA_ARGS__))
#define LTTYPES_EVAL8(...)      LTTYPES_EVAL4(LTTYPES_EVAL4(__VA_ARGS__))
#define LTTYPES_EVAL4(...)      LTTYPES_EVAL2(LTTYPES_EVAL2(__VA_ARGS__))
#define LTTYPES_EVAL2(...)      LTTYPES_EVAL1(LTTYPES_EVAL1(__VA_ARGS__))
#define LTTYPES_EVAL1(...)      __VA_ARGS__


#define LTTYPES_EXPAND_RODATA_RAM_CRITICAL_NUMBER_0
#define LTTYPES_EXPAND_RODATA_RAM_CRITICAL_NUMBER_1 LT_RODATA_RAM_CRITICAL(1)
#define LTTYPES_EXPAND_RODATA_RAM_CRITICAL_NUMBER_2 LT_RODATA_RAM_CRITICAL(2)
#define LTTYPES_EXPAND_RODATA_RAM_CRITICAL_NUMBER_3 LT_RODATA_RAM_CRITICAL(3)
#endif // DOXY_SKIP  ]

/* __________________________________________________________________________
 * LTInterface Prototype Functions for macro typedef_LTLIBRARY_INTERFACE */
#define INHERIT_INTERFACE_BASE                                                                                           \
            const char *                    (* GetInterfaceName)(void);                                                     \
                /**< Returns the name of the interface. If GetInterfaceName() is called on an                               \
                   LTLibrary interface, then GetInterfaceName() returns the name of the library since the                   \
                   the name of a library and the name of its main interface are the same.                                   \
                 */                                                                                                         \
            u32                             (* GetInterfaceVersion)(void);                                                  \
                /**< Returns the version number of the interface. */                                                        \
            LTInterfaceType                 (* GetInterfaceType)(void);                                                     \
                /**< Returns the type of the interface. */                                                                  \
            LTLibrary *                     (* GetLibrary)(void);                                                           \
                /**< Returns the LTLibrary that the interface was exported from.  LTLibrary is                              \
                   a specialization of LTInterface that has additional prototype functions. A library can export            \
                   more than one LTInterface, but exports only one LTLibrary, the root interface of the library.            \
                 */                                                                                                         \
            void                            (* Destroy)(LTHandle handle);                                                   \
                /**< Destroys a handle.  This is on LTInterface because LTInterfaces can be                                 \
                   associated with handles so, as a point of convenience, one can do this:                                  \
                   LTFoo * iLTFoo = LT_GetCore()->GetHandleInterface(hFoo);                                                 \
                     iLTFoo->Bar(hFoo);                                                                                     \
                     iLTFoo->Baz(hFoo);                                                                                     \
                     iLTFoo->Destroy(hFoo);                                                                                 \
                   Note: iLTFoo->Destroy(hFoo) is equivalent to lt_destroyhandle(hFoo)                                      \
                         but more convenient.                                                                               \
                 */                                                                                                         \
            void                            (* OnDestroyHandle)(LTHandle handle);                                           \
                /**< Called by Destroy() to invoke the custom OnDestroyHandle procedure that                                \
                   was optionally specified when defining the interface with the define_LTLIBRARY_INTERFACE macro.          \
                   Never call OnDestroyHandle directly.                                                                     \
                 */                                                                                                         \
            struct LTInterfaceExt *         (* GetLTInterfaceExt)(void);                                                    \
                /**< For future expansion; currently there is no LTInterfaceExt. */                                         \

/* ___________________________________________________________________________
 * LTLibrary Prototype Functions for macro typedef_LTLIBRARY_ROOT_INTERFACE */
#define INHERIT_LIBRARY_BASE                                                                                            \
        INHERIT_INTERFACE_BASE                                                                                           \
            const char *                    (* GetLibraryExtrinsicName)(void);                                             \
                /**< Returns the extrinsic name of the library, which is the file name of the library.                      \
                 */                                                                                                         \
            const char *                    (* GetLibraryBuildVersion)(void);                                               \
                /**< Returns the build id version string of the library.                                                    \
                     The version string has the form:                                                                       \
                     "<product>-<major version>.<minor version>.<build number>.<build date>-<platform>-<build-mode>".       \
                     For example, "RokuLT-1.0.4095.20200503-elk-release".                                                   \
                 */                                                                                                         \
            LTInterface *                   (* GetInterface)(const char * pInterfaceName);                                  \
                /**< Returns the LTInterface named pInterfaceName or NULL if no such interface is exported                  \
                     from the library                                                                                       \
                 */                                                                                                         \
            const LTInterface **            (* GetInterfaces)(void);                                                        \
                /**< Returns an array of LTInterface pointers containing all of the library's                               \
                     exported interfaces, the last entry being NULL indicating the end of the list.                         \
                     Iterate like this:          LTInterface *  pInterface;                                                 \
                                                 LTInterface ** pInterfaces = pLibrary->GetInterfaces();                    \
                                                 while (NULL != (pInterface = *pInterfaces)) {                              \
                                                     DoSomething(pInterface);                                               \
                                                     pInterfaces++;                                                         \
                                                 }                                                                          \
                     If there are no exported interfaces, returns NULL.                                                     \
                 */                                                                                                         \
            const LTLibrary_LTObjectApiListEntry * (* GetExportedObjectList)(void);                                         \
            const LTResourceTree *          (* GetResourceTree)(void);                                                      \
                /**< Returns a pointer to the arbolated resource tree compiled into the library or null if the library      \
                     has no resource tree compiled in.  To create a resource tree for your library, place a json file       \
                     called ResourceTree.json in a directory called resources/ at your library source root, i.e. place      \
                     resources\ResourceTree.json inside your library source top level directory.                            \
                     @ return a pointer to the resource tree or NULL if no resource tree is in the library                  \
                 */                                                                                                         \
            u32                             (* GetRunFunctionStacksizeRequirement)(void);                                   \
                /**< Returns the size of the thread stack required to execute the                                           \
                     library's Run() function in its own thread or 0 if the default thread stack size should be used.       \
                 */                                                                                                         \
            int                             (* Run)(int argc, const char ** argv);                                          \
                /**< Invoked by LT to run a library as a program from a command shell,                                      \
                     from the console window, or as invoked in the genesis library specified to LT_Run() on boot.           \
                     This function pointer may be NULL, in which case this library cannot be executed as a program.         \
                 */                                                                                                         \
            struct LTLibraryExt *           (* GetLTLibraryExt)(void);                                                      \
                /**< For future expansion; currently there is no LTLibraryExt                                           */  \

#define INHERIT_DEVICE_LIBRARY_BASE                                                                                         \
        INHERIT_LIBRARY_BASE                                                                                                \
        struct  {                                                                                                           \
            u32                            (* GetNumDeviceUnits)(void);                                                     \
                /**< DOCUMENTATION_NEEDED */                                                                                \
            LTDeviceUnit                   (* CreateDeviceUnitHandle)(u32 nDeviceUnitNum);                                  \
                /**< DOCUMENTATION_NEEDED */                                                                                \
            struct LTDeviceLibraryExt *    (* GetLTDeviceLibraryExt)(void);                                                 \
                /* For future expansion; currently there is no LTDeviceLibraryExt                                      */   \
        };

#define INHERIT_DRIVER_LIBRARY_BASE                                                                                         \
        INHERIT_LIBRARY_BASE                                                                                                \
        struct  {                                                                                                           \
            u32                            (* GetNumDeviceUnits)(void);                                                     \
                /**< DOCUMENTATION_NEEDED */                                                                                \
            LTDeviceUnit                   (* CreateDeviceUnitHandle)(u32 nDeviceUnitNum);                                  \
                /**< DOCUMENTATION_NEEDED */                                                                                \
            struct LTDriverLibraryExt *    (* GetLTDriverLibraryExt)(void);                                                 \
                /* For future expansion; currently there is no LTDriverLibraryExt                                      */   \
        };

/**
 * @defgroup basicinterfaces Base LT Interfaces
 * @{
 *
 * @brief These are the base interfaces for all LT interfaces.
 *
 * And this needs a whole lot of work.
 */

/** Base interface methods for all LT interfaces */
            struct LTInterfaceBase              { INHERIT_INTERFACE_BASE                };
/** Base interface methods for all LT libraries */
            struct LTLibraryBase                { INHERIT_LIBRARY_BASE                  };
/** Base interface methods for all LTDevice libraries */
            struct LTDeviceLibraryBase          { INHERIT_DEVICE_LIBRARY_BASE           };
/** Base interface methods for all LTDriver libraries */
            struct LTDriverLibraryBase          { INHERIT_DRIVER_LIBRARY_BASE           };

/** @} */

/* _______________________________
 * LTTYPES_LTLIBRARY_BUILD_VERSION for LTLibrary prototype instance functions */
#if defined (LT_RELEASE_IDENTIFIER)
    /* Note we don't have to stringify LT_RELEASE_IDENTIFIER because the LT Master Makefile passes it
       in on the command line using quotes around escaped quotes because if we don't,
       the C preprocessor will separate components of our version string into tokens and expand them:
         1. If we use -D FOO="foo-linux-bar", we find it is equivalent to #define FOO foo-linux-bar
            (foo minus linux minus bar) and the preprocessor will use the predefined #define linux 1
            to turn FOO into foo-1-bar.
         2. Therefore we use -D FOO="\"foo-linux-bar\"" which we find is equivalent to #define FOO "foo-linux-bar"
    */
    #define LTTYPES_LTLIBRARY_BUILD_VERSION LT_RELEASE_IDENTIFIER
#else
    #ifdef LT_DEBUG
        #define LTTYPES_LTLIBRARY_BUILD_VERSION "RokuLT-0.0.0.19700101-unknown_product-unknown_platform-debug"
    #else
        #define LTTYPES_LTLIBRARY_BUILD_VERSION "RokuLT-0.0.0.19700101-unknown_product-unknown_platform-release"
    #endif
#endif

/* _________________________________
 * LTLIBRARY_ARBOLATED_RESOURCE_TREE for LTLibrary prototype instance function GetResourceTree() */
#if !defined(LTLIBRARY_ARBOLATED_RESOURCE_TREE)
    #define  LTLIBRARY_ARBOLATED_RESOURCE_TREE 0
#endif

/* ________________________________________________
 * LTLibrary export binding and Prototype Functions for macro define_LTLIBRARY_ROOT_INTERFACE(name, ...)             */
#define DEFINE_LTLIBRARY_PROTOTYPE_NAME_AND_RUNFUNCTION_AND_STACKSIZE_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, runFunction, stackSize, roDataRamCriticalSectionNumber) \
    LTLibrary_LTObjectApiListEntry *  LTLIBRARY_EXPORTED_OBJECT_LIST;                                                                                                 \
    LTLibrary_LTObjectApiListEntry *  LTLIBRARY_INTERNAL_OBJECT_LIST;                                                                                                 \
    struct LTLibrary_MacroCreateObjectParms LTLIBRARY_MACROCREATEOBJECT_PARMS = {                       \
        LT_STRINGIFY(CURRENTLY_BUILDING_LTLIBRARY), &LTLIBRARY_EXPORTED_OBJECT_LIST, &LTLIBRARY_INTERNAL_OBJECT_LIST };      \
    static const LTInterface ** s_pp##name##_ExportedInterfaces;                                                                                                          \
    static  LTLibrary_InitializeExportedInterfacesFunction *                                                                                                             \
        s_p##name##Impl_InitializeExportedInterfacesFunction;                                                                                                           \
    static void name##Impl_InitializeExportedInterfaces(void) {                                                                                                        \
        if (s_p##name##Impl_InitializeExportedInterfacesFunction)                                                                                                     \
            (*s_p##name##Impl_InitializeExportedInterfacesFunction)();                                                                                               \
    }                                                                                                                                                               \
    static  LTLibrary_OpenDependentLibrariesFunction *                                                                                                             \
        s_p##name##Impl_OpenDependentLibrariesFunction;                                                                                                           \
    static bool name##Impl_OpenDependentLibraries(void) {                                                                                                        \
        if (s_p##name##Impl_OpenDependentLibrariesFunction)                                                                                                     \
            return (*s_p##name##Impl_OpenDependentLibrariesFunction)();                                                                                        \
        return true;                                                                                                                                          \
    }                                                                                                                                                        \
    static  LTLibrary_CloseDependentLibrariesFunction *                                                                                                     \
        s_p##name##Impl_CloseDependentLibrariesFunction;                                                                                                   \
    static void name##Impl_CloseDependentLibraries(void) {                                                                                                \
        if (s_p##name##Impl_CloseDependentLibrariesFunction)                                                                                             \
            (*s_p##name##Impl_CloseDependentLibrariesFunction)();                                                                                       \
    }                                                                                                                                                  \
    DECLARE_LTLIBRARY_SHARED_PROTOTYPES(name)                                                                                                         \
    LTLIBRARY_DEFINE_EXPORT_BINDING_FOR(CURRENTLY_BUILDING_LTLIBRARY, name, k##name##_Version)                                                       \
    /* non-static */  name *                  LT_Get##name(void)                 { return (name *)&s_##name; }                                      \
    /* non-static */  /* LTLibrary *             name##Impl_GetLibrary(void)        { return (LTLibrary *)&s_##name; }    */                       \
    /* non-static */  struct LTInterfaceExt * name##Impl_GetLTInterfaceExt(void) { return NULL; }                                                 \
                                                                                                                                                 \
    static const char *     name##Impl_GetInterfaceName(void)                   { return #name ; }                                              \
    static u32              name##Impl_GetInterfaceVersion(void)                { return (u32)k##name##_Version; }                             \
    static LTInterfaceType  name##Impl_GetInterfaceType(void)                   { return (LTInterfaceType)k##name##_InterfaceType; }          \
    static void             name##Impl_Destroy(LTHandle handle)                 { LT_GetCore()->DestroyHandle(handle); }                     \
    static void             name##Impl_OnDestroyHandle(LTHandle handle)         { LT_UNUSED(handle); }                                      \
    static const char *     name##Impl_GetLibraryExtrinsicName(void)            { return LT_STRINGIFY(CURRENTLY_BUILDING_LTLIBRARY); }     \
    LTLIBRARY_DEFINE_GETLIBRARYBUILDVERSION(name)                                                                                           \
    static LTInterface *    name##Impl_GetInterface(const char * pInterfaceName)                                                             \
                                      { return LT_GetCore()->GetLibraryInterface((LTLibrary *)&s_##name, pInterfaceName); }                   \
    static const LTInterface **   name##Impl_GetInterfaces(void)                { return s_pp##name##_ExportedInterfaces; }                    \
    const LTLibrary_LTObjectApiListEntry * name##Impl_GetExportedObjectList(void) {  return LTLIBRARY_EXPORTED_OBJECT_LIST; }                   \
    static struct LTLibraryExt *  name##Impl_GetLTLibraryExt(void)              { return NULL; }                                                 \
    static const LTResourceTree * name##Impl_GetResourceTree(void)              { return (LTResourceTree *)LTLIBRARY_ARBOLATED_RESOURCE_TREE; }  \
    static u32              name##Impl_GetRunFunctionStacksizeRequirement(void) { return stackSize; }                                           \
    DEFINE_LTLIBRARY_STATIC_STRUCT_INSTANCE(name) LTTYPES_EXPAND_RODATA_RAM_CRITICAL_NUMBER_##roDataRamCriticalSectionNumber = {               \
        .GetInterfaceName                   = name##Impl_GetInterfaceName,                                                                    \
        .GetInterfaceVersion                = name##Impl_GetInterfaceVersion,                                                                \
        .GetInterfaceType                   = name##Impl_GetInterfaceType,                                                                  \
        .GetLibrary                         = LTLIBRARY_SHARED_PROTOTYPE(GetLibrary),                                                      \
        .Destroy                            = name##Impl_Destroy,                                                                         \
        .OnDestroyHandle                    = name##Impl_OnDestroyHandle,                                                                \
        .GetLTInterfaceExt                  = name##Impl_GetLTInterfaceExt,                                                             \
        .GetLibraryExtrinsicName            = name##Impl_GetLibraryExtrinsicName,                                                      \
        .GetLibraryBuildVersion             = name##Impl_GetLibraryBuildVersion,                                                      \
        .GetInterface                       = name##Impl_GetInterface,                                                               \
        .GetInterfaces                      = name##Impl_GetInterfaces,                                                             \
        .GetExportedObjectList               = name##Impl_GetExportedObjectList,                                                             \
        .GetResourceTree                    = name##Impl_GetResourceTree,                                                          \
        .GetRunFunctionStacksizeRequirement = name##Impl_GetRunFunctionStacksizeRequirement,                                      \
        .Run                                = runFunction,                                                                       \
        .GetLTLibraryExt                    = name##Impl_GetLTLibraryExt,
#define DEFINE_LTLIBRARY_PROTOTYPE_NAME(name) DEFINE_LTLIBRARY_PROTOTYPE_NAME_AND_RUNFUNCTION_AND_STACKSIZE_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, NULL, 0, 0)

/* ___________________________________
 * VARIADIC default argument selection for define_LTLIBRARY_ROOT_INTERFACE(name, ...)  */
#define DEFINE_LTLIBRARY_ARG_ERROR_MESSAGE "\n" __FILE__ "(" LT_STRINGIFY(__LINE__) "):\n\
    error: define_LTLIBRARY_ROOT_INTERFACE(): invalid macro args\n\
    usage:\n\
           define_LTLIBRARY_ROOT_INTERFACE(libraryName [, RunFunction [, StackSize]]) {\n\
               .MemberFunction1 = MyFunction1,\n\
               .MemberFunction2 = MyFunction2\n\
           } LTLIBRARY_DEFINITION;\n"
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER            LTTYPES_CPP_PRAGMA_MESSAGE(DEFINE_LTLIBRARY_ARG_ERROR_MESSAGE) typedef struct { struct {
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_9(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_8(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_7(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_6(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_5(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_4(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_3(name, ...)      LTTYPES_FORCE_VARIADIC_EXPANSION(DEFINE_LTLIBRARY_PROTOTYPE_NAME_AND_RUNFUNCTION_AND_STACKSIZE_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, __VA_ARGS__))
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_2(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_NAME_AND_RUNFUNCTION_AND_STACKSIZE_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_FIRST_ARG(__VA_ARGS__)), LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_SECOND_ARG(__VA_ARGS__)), 0)
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_1(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_NAME_AND_RUNFUNCTION_AND_STACKSIZE_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_FIRST_ARG(__VA_ARGS__)), 0, 0)
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_VALUE(name, count, ...)  LTTYPES_FORCE_VARIADIC_EXPANSION(DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_RESULT_##count(name, __VA_ARGS__))
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT(name, count, ...)        DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT_VALUE(name, count, __VA_ARGS__)
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_EMPTY_RESULT_1(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_NAME(name)
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_EMPTY_RESULT_0(name, ...)      DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_COUNT(name, LTTYPES_COUNT_VARIADIC_ARGUMENTS(__VA_ARGS__), __VA_ARGS__)
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_EMPTY_VALUE(name, empty, ...)  LTTYPES_FORCE_VARIADIC_EXPANSION(DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_EMPTY_RESULT_##empty(name, __VA_ARGS__))
#define DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_EMPTY(name, empty, ...)        DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_EMPTY_VALUE(name, empty, __VA_ARGS__)
#define DEFINE_LTLIBRARY_PROTOTYPE(name, ...)                               DEFINE_LTLIBRARY_PROTOTYPE_ARGS_TEST_EMPTY(name, LTTYPES_IS_VARIADIC_LIST_EMPTY(__VA_ARGS__), __VA_ARGS__)
#define CURRENTLY_BUILDING_LTLIBRARY_UNDEFINED_ERROR_MESSAGE __FILE__ "(" LT_STRINGIFY(__LINE__) "):\n" \
  "    * err: CURRENTLY_BUILDING_LTLIBRARY: macro undefined\n"                                           \
  "    * ---> specify -DCURRENTLY_BUILDING_LTLIBRARY=<libname> in CFLAGS/CXXFLAGS"
#define DECLARE_LTLIBRARY_SHARED_PROTOTYPES_USING(library, name)      \
    const name * library##Impl_Get##name(void) { return (const name *)&s_##name; } \
    LTInterfaceType library##Impl_GetObjectInterfaceType(void); \
    void library##Impl_AddRef(LTObject *object); \
    void library##Impl_RemoveRef(LTObject *object); \
    LTLibrary * library##Impl_GetLibrary(void);                         \
    struct LTInterfaceExt * library##Impl_GetLTInterfaceExt(void);
#define DECLARE_LTLIBRARY_SHARED_PROTOTYPES_FOR(library, name) DECLARE_LTLIBRARY_SHARED_PROTOTYPES_USING(library, name)
#define DECLARE_LTLIBRARY_SHARED_PROTOTYPES(name) DECLARE_LTLIBRARY_SHARED_PROTOTYPES_FOR(CURRENTLY_BUILDING_LTLIBRARY, name)
#define LTLIBRARY_SHARED_PROTOTYPE_USING(library, function) library##Impl_##function
#define LTLIBRARY_SHARED_PROTOTYPE_FOR(library, function) LTLIBRARY_SHARED_PROTOTYPE_USING(library, function)
#define LTLIBRARY_SHARED_PROTOTYPE(function) LTLIBRARY_SHARED_PROTOTYPE_FOR(CURRENTLY_BUILDING_LTLIBRARY, function)
#define LTLIBRARY_EXPORT_INTERFACE_WITH(library, name) library##Impl_Get##name
#define LTLIBRARY_EXPORT_INTERFACE_FOR(library, name) LTLIBRARY_EXPORT_INTERFACE_WITH(library, name)
#define LTLIBRARY_EXPORT_INTERFACE(name) LTLIBRARY_EXPORT_INTERFACE_FOR(CURRENTLY_BUILDING_LTLIBRARY, name)
#define DECLARE_LTLIBRARY_EXPORT_INTERFACE(name) const struct name##Api * LTLIBRARY_EXPORT_INTERFACE(name)(void);
#define LTLIBRARY_INIT_EXPORT_INTERFACE_WITH(library, name) s_p##library##_ExportedInterfaces[i++] = (LTInterface *)library##Impl_Get##name();
#define LTLIBRARY_INIT_EXPORT_INTERFACE_FOR(library, name) LTLIBRARY_INIT_EXPORT_INTERFACE_WITH(library, name)
#define LTLIBRARY_INIT_EXPORT_INTERFACE(name) LTLIBRARY_INIT_EXPORT_INTERFACE_FOR(CURRENTLY_BUILDING_LTLIBRARY, name)
#define INITIALIZE_LTLIBRARY_EXPORT_INTERFACE(name) LTLIBRARY_INIT_EXPORT_INTERFACE(name)

/* _______________________________
 * LTInterface Prototype Functions for macro define_LTLIBRARY_INTERFACE(name, ...) */
#define DEFINE_LTINTERFACE_PROTOTYPE_NAME_AND_ONDESTROYHANDLEPROC_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, onDestroyHandleProc, roDataRamCriticalSectionNumber) \
    DECLARE_LTLIBRARY_SHARED_PROTOTYPES(name)                                                                     \
    static struct LTInterfaceExt * name##Impl_GetLTInterfaceExt(void) { return NULL; }                              \
    static const char *    name##Impl_GetInterfaceName(void)    { return #name ; }                                   \
    static u32             name##Impl_GetInterfaceVersion(void) { return (u32)k##name##_Version; }                   \
    static LTInterfaceType name##Impl_GetInterfaceType(void)    { return (LTInterfaceType)k##name##_InterfaceType; } \
    static void name##Impl_Destroy(LTHandle handle) { LT_GetCore()->DestroyHandle(handle);   }                       \
    static void name##Impl_CallOnDestroyHandle(LTHandle handle) {                                                   \
        if (LT_GetCore()->HandleInDestroy(handle)) {                                                               \
            LTInterface_OnDestroyHandleProc * pOnDestroyHandleProc = onDestroyHandleProc;                         \
            if (pOnDestroyHandleProc) (*pOnDestroyHandleProc)(handle);                                             \
        }                                                                                                           \
    }                                                                                                                \
    static name s_##name LTTYPES_EXPAND_RODATA_RAM_CRITICAL_NUMBER_##roDataRamCriticalSectionNumber = {  \
        .GetInterfaceName                   = name##Impl_GetInterfaceName,                                            \
        .GetInterfaceVersion                = name##Impl_GetInterfaceVersion,                                         \
        .GetInterfaceType                   = name##Impl_GetInterfaceType,                                            \
        .GetLibrary                         = LTLIBRARY_SHARED_PROTOTYPE(GetLibrary),                                \
        .Destroy                            = name##Impl_Destroy,                                                   \
        .OnDestroyHandle                    = name##Impl_CallOnDestroyHandle,                                      \
        .GetLTInterfaceExt                  = name##Impl_GetLTInterfaceExt,
#define DEFINE_LTINTERFACE_PROTOTYPE_NAME(name) DEFINE_LTINTERFACE_PROTOTYPE_NAME_AND_ONDESTROYHANDLEPROC_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, NULL, 0)

#ifndef DOXY_SKIP // [
/* define_LTLIBRARY_INTERFACE(name, ...) VARIADIC default argument selection */
#define DEFINE_LTINTERFACE_ARG_ERROR_MESSAGE "\n" __FILE__ "(" LT_STRINGIFY(__LINE__) "):\n\
    error: define_LTLIBRARY_INTERFACE(): invalid macro args\n\
    usage:\n\
           define_LTLIBRARY_INTERFACE(interfaceName [, onDestroyHandleProc]) {\n\
               .MemberFunction1 = MyFunction1,\n\
               .MemberFunction2 = MyFunction2\n\
           } LTLIBRARY_DEFINITION;\n"
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER            LTTYPES_CPP_PRAGMA_MESSAGE(DEFINE_LTINTERFACE_ARG_ERROR_MESSAGE) typedef struct { struct {
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_9(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_8(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_7(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_6(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_5(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_4(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_3(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_ERROR_HALT_COMPILER
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_2(name, ...)      LTTYPES_FORCE_VARIADIC_EXPANSION(DEFINE_LTINTERFACE_PROTOTYPE_NAME_AND_ONDESTROYHANDLEPROC_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, __VA_ARGS__))
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_1(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_NAME_AND_ONDESTROYHANDLEPROC_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(name, LTTYPES_FORCE_VARIADIC_EXPANSION(LTTYPES_EXTRACT_FIRST_ARG(__VA_ARGS__)), 0)
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_VALUE(name, count, ...)  LTTYPES_FORCE_VARIADIC_EXPANSION(DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_RESULT_##count(name, __VA_ARGS__))
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT(name, count, ...)        DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT_VALUE(name, count, __VA_ARGS__)
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_EMPTY_RESULT_1(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_NAME(name)
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_EMPTY_RESULT_0(name, ...)      DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_COUNT(name, LTTYPES_COUNT_VARIADIC_ARGUMENTS(__VA_ARGS__), __VA_ARGS__)
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_EMPTY_VALUE(name, empty, ...)  LTTYPES_FORCE_VARIADIC_EXPANSION(DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_EMPTY_RESULT_##empty(name, __VA_ARGS__))
#define DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_EMPTY(name, empty, ...)        DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_EMPTY_VALUE(name, empty, __VA_ARGS__)
#define DEFINE_LTINTERFACE_PROTOTYPE(name, ...)                               DEFINE_LTINTERFACE_PROTOTYPE_ARGS_TEST_EMPTY(name, LTTYPES_IS_VARIADIC_LIST_EMPTY(__VA_ARGS__), __VA_ARGS__)

/* __________________________________________________
 * Helper Macros for sequence expansion of interfaces */
#define LTTYPES_JOIN_ARGUMENTS(arg1, ...) LTTYPES_JOIN_ARGUMENTS_RESULT(arg1, __VA_ARGS__)
#define LTTYPES_JOIN_ARGUMENTS_RESULT(arg1, ...) arg1## __VA_ARGS__
#define LTTYPES_EXPAND_EMPTY()
#define LTTYPES_DEFER_EXPANSION(x) x LTTYPES_EXPAND_EMPTY()
#define LTTYPES_DEFER_EXPANSION_TWICE(x) x LTTYPES_EXPAND_EMPTY LTTYPES_EXPAND_EMPTY()()
#define LTTYPES_EXPAND_COMMA() ,
#define LTTYPES_DEFERRED_COMMA_EXPANSION LTTYPES_DEFER_EXPANSION(LTTYPES_EXPAND_COMMA)()
//#define LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) (LTInterface *)LTLIBRARY_EXPORT_INTERFACE(interfaceName)
#define LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) (LTInterface *)NULL
#define LTLIBRARY_EXPORTED_INTERFACE_LIST(interfaceSequence) LTTYPES_JOIN_ARGUMENTS(LTLIBRARY_EXPORTED_INTERFACE_LIST_1 interfaceSequence, _END)
#define LTLIBRARY_EXPORTED_INTERFACE_LIST_1(interfaceName) LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) LTTYPES_DEFERRED_COMMA_EXPANSION LTLIBRARY_EXPORTED_INTERFACE_LIST_2
#define LTLIBRARY_EXPORTED_INTERFACE_LIST_2(interfaceName) LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) LTTYPES_DEFERRED_COMMA_EXPANSION LTLIBRARY_EXPORTED_INTERFACE_LIST_1
#define LTLIBRARY_EXPORTED_INTERFACE_LIST_1_END
#define LTLIBRARY_EXPORTED_INTERFACE_LIST_2_END
#define DECLARE_LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) DECLARE_LTLIBRARY_EXPORT_INTERFACE(interfaceName)
#define DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST(interfaceSequence) LTTYPES_JOIN_ARGUMENTS(DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST_1 interfaceSequence, _END)
#define DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST_1(interfaceName) DECLARE_LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST_2
#define DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST_2(interfaceName) DECLARE_LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST_1
#define DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST_1_END
#define DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST_2_END
#define INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) INITIALIZE_LTLIBRARY_EXPORT_INTERFACE(interfaceName)
#define INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST(interfaceSequence) LTTYPES_JOIN_ARGUMENTS(INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST_1 interfaceSequence, _END)
#define INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST_1(interfaceName) INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST_2
#define INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST_2(interfaceName) INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_INSTANCE(interfaceName) INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST_1
#define INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST_1_END
#define INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST_2_END
#define DECLARE_LTLIBRARY_IMPORT_LIBRARY(libName, interfaceName) \
    static interfaceName * _s_p##libName;           \
    static interfaceName * LT_Get##libName(void) {  \
        return _s_p##libName;                       \
    }
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_RESULT_1(libName, ...)      DECLARE_LTLIBRARY_IMPORT_LIBRARY(libName, libName)
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_RESULT_0(libName, ...)      DECLARE_LTLIBRARY_IMPORT_LIBRARY(libName, __VA_ARGS__)
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_VALUE(libName, empty, ...)  LTTYPES_FORCE_VARIADIC_EXPANSION(DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_RESULT_##empty(libName, __VA_ARGS__))
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY(libName, empty, ...)        DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_VALUE(libName, empty, __VA_ARGS__)
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName, ...)                      DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY(libName, LTTYPES_IS_VARIADIC_LIST_EMPTY(__VA_ARGS__), __VA_ARGS__)
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST(libSequence) LTTYPES_JOIN_ARGUMENTS(DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1 libSequence, _END)
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1(libName, ...) DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName, __VA_ARGS__) DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2(libName, ...) DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName, __VA_ARGS__) DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1_END
#define DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2_END

#define LTLIBRARY_OPEN_DEPENDENT_LIBRARY_WITH(libName, interfaceName) if (!(_s_p##libName = (interfaceName *)LT_GetCore()->OpenLibrary(#libName))) return false;
#define LTLIBRARY_OPEN_DEPENDENT_LIBRARY_FOR(libName, interfaceName) LTLIBRARY_OPEN_DEPENDENT_LIBRARY_WITH(libName, interfaceName)
#define LTLIBRARY_OPEN_DEPENDENT_LIBRARY(libName, interfaceName) LTLIBRARY_OPEN_DEPENDENT_LIBRARY_FOR(libName, interfaceName)
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY(libName, interfaceName) LTLIBRARY_OPEN_DEPENDENT_LIBRARY(libName, interfaceName)
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_RESULT_1(libName, ...)      OPEN_LTLIBRARY_DEPENDENT_LIBRARY(libName, libName)
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_RESULT_0(libName, ...)      OPEN_LTLIBRARY_DEPENDENT_LIBRARY(libName, __VA_ARGS__)
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_VALUE(libName, empty, ...)  LTTYPES_FORCE_VARIADIC_EXPANSION(OPEN_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_RESULT_##empty(libName, __VA_ARGS__))
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY(libName, empty, ...)        OPEN_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY_VALUE(libName, empty, __VA_ARGS__)
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName, ...)                      OPEN_LTLIBRARY_DEPENDENT_LIBRARY_ARGS_TEST_EMPTY(libName, LTTYPES_IS_VARIADIC_LIST_EMPTY(__VA_ARGS__), __VA_ARGS__)
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST(libSequence) LTTYPES_JOIN_ARGUMENTS(OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1 libSequence, _END)
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1(libName, ...) OPEN_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName, __VA_ARGS__) OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2(libName, ...) OPEN_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName, __VA_ARGS__) OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1_END
#define OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2_END

#define LTLIBRARY_CLOSE_DEPENDENT_LIBRARY_WITH(library, name)    \
    if (_s_p##name) {                                          \
        LT_GetCore()->CloseLibrary((LTLibrary *)(_s_p##name)); \
        _s_p##name = NULL;                                     \
    }
#define LTLIBRARY_CLOSE_DEPENDENT_LIBRARY_FOR(library, name) LTLIBRARY_CLOSE_DEPENDENT_LIBRARY_WITH(library, name)
#define LTLIBRARY_CLOSE_DEPENDENT_LIBRARY(name) LTLIBRARY_CLOSE_DEPENDENT_LIBRARY_FOR(CURRENTLY_BUILDING_LTLIBRARY, name)
#define CLOSE_LTLIBRARY_DEPENDENT_LIBRARY(name) LTLIBRARY_CLOSE_DEPENDENT_LIBRARY(name)
#define CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName) CLOSE_LTLIBRARY_DEPENDENT_LIBRARY(libName)
#define CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST(libSequence) LTTYPES_JOIN_ARGUMENTS(CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1 libSequence, _END)
#define CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1(libName, ...) CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName) CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2
#define CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2(libName, ...) CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_INSTANCE(libName) CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1
#define CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_1_END
#define CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST_2_END

/* ______________________________________________________________________________________________________________
 * Helper Macros for allowing forward declaration of static const library interface instances inside C++ files */
#if defined (__cplusplus)
    #define FORWARD_DECLARE_LTLIBRARY_STATIC_STRUCT(libraryName) namespace { extern const struct libraryName##Api s_##libraryName; }
    #define DEFINE_LTLIBRARY_STATIC_STRUCT_INSTANCE(libraryName) namespace { extern const struct libraryName##Api s_##libraryName
    #define END_LTLIBRARY_STATIC_STRUCT_DEFINITION                         { } }; };
#else
    #define FORWARD_DECLARE_LTLIBRARY_STATIC_STRUCT(libraryName) static libraryName s_##libraryName;
    #define DEFINE_LTLIBRARY_STATIC_STRUCT_INSTANCE(libraryName) static libraryName s_##libraryName
    #define END_LTLIBRARY_STATIC_STRUCT_DEFINITION               };
#endif

#endif // DOXY_SKIP  ]

/****************************************
 *             LTLIBRARY_IMPORT_REQUIRED_LIBRARIES
 *       TYPE: standard LTLibrary dependent library specification macro
 *  PLACEMENT: Placed in Library PRIVATE IMPLEMENTATION FILES ONLY.
 *   QUANTITY: Once per library
 *      USAGE: LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(libName, sequenceOfRequiredLibraries)
 *             where sequenceOfRequiredLibraries is a sequence tuples containing the required
 *             library name and optionally the interface type for that library.
 *
 * EXAMPLE:
 *  LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(libName, (LTDeviceSPI) (LTLibFoo, LTLibFooInterface))
 *
 * VARIANTS:
 *   LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(libName, (requiredLibName1) (requiredLibName2) ...)
 *   LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(libName, (requiredLibName1 requiredLibInterfaceName1) (requiredLibName2 requiredLibInterfaceName2) ...)
 */
#define LTLIBRARY_IMPORT_REQUIRED_LIBRARIES(libName, libSequence) LTLIBRARY_IMPORT_REQUIRED_LIBRARIES_2(libName, libSequence)
#define LTLIBRARY_IMPORT_REQUIRED_LIBRARIES_2(libName, libSequence)               \
   DECLARE_LTLIBRARY_DEPENDENT_LIBRARY_LIST(libSequence)                          \
   static bool libName##Impl_OpenDependentLibrariesFunction(void) {               \
       OPEN_LTLIBRARY_DEPENDENT_LIBRARY_LIST(libSequence)                         \
       return true;                                                               \
   }                                                                              \
   static LTLibrary_OpenDependentLibrariesFunction *                              \
       s_p##libName##Impl_OpenDependentLibrariesFunction =                        \
           & libName##Impl_OpenDependentLibrariesFunction;                        \
   static void libName##Impl_CloseDependentLibrariesFunction(void) {              \
       CLOSE_LTLIBRARY_DEPENDENT_LIBRARY_LIST(libSequence)                        \
   }                                                                              \
   static LTLibrary_CloseDependentLibrariesFunction *                             \
       s_p##libName##Impl_CloseDependentLibrariesFunction =                       \
           & libName##Impl_CloseDependentLibrariesFunction;

/****************************************************************
 ****************************************************************
 * ______________________________________________________________
 * 11. LTLIBRARY INTERFACE definition and declaration USER MACROS
 *                                                    USER MACROS
 *     USE DIRECTLY   USE DIRECTLY   USE DIRECTLY
 * ______________________________________________
 *      MACRO 1: typedef_LTLIBRARY_ROOT_INTERFACE  <-- THESE
 *      MACRO 2:  define_LTLIBRARY_ROOT_INTERFACE  <-- ARE THE
 *      MACRO 3: typedef_LTLIBRARY_INTERFACE       <-- IMPORTANT
 *      MACRO 4:  define_LTLIBRARY_INTERFACE       <-- ONES FOR GENERAL AND COMMON USE!
 *      _______
 *      MACRO 5: typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE  <-- FOR USE BY LTDEVICE Library Writers (usually only LT Team)
 *      MACRO 6:  define_LTDEVICE_LIBRARY_ROOT_INTERFACE  <-- FOR USE BY LTDEVICE Library Writers (usually only LT Team)
  *     MACRO 7:  define_LTDEVICE_DRIVER_IMPLEMENTATION   <-- FOR USE BY LTDRIVER Library Writers (any platform bringup team)
    *           ___________________________________________________________________________________
      * ******* USE THE FOLLOWING BLOCK TERMINATION MACROS FOR MACROS 1,3,5 and 2,4,6            */
        #define LTLIBRARY_INTERFACE ; }     /* TERMINATION FOR MACROS 1,3,5                     */
        #define LTLIBRARY_DEFINITION        /* TERMINATION FOR MACROS 2,4,6                    */\
                END_LTLIBRARY_STATIC_STRUCT_DEFINITION  /* YOU HAVE NO CHANCE TO SURVIVE, MYT */
        /* __________________________________________________________________________________*/
        /* BONUS MACRO!                                                                      /
         * LT_LIBRARY_EMPTY_INTERFACE or "how to make a rare but sometimes useful empty     |
         * interface." Use LT_LIBRARY_EMPTY_INTERFACE in place of LTLIBRARY_INTERFACE       |
         * without any curly braces:                                                        |
         * typedef_LTLIBRARY_INTERFACE(MyEmptyInterface, 1) LTLIBRARY_EMPTY_INTERFACE;     */
        #define LTLIBRARY_EMPTY_INTERFACE  {u8:0;};}; /* ALT TERMINATION FOR MACROS 1,3,5  /
      * ***********************************************************************************
    *
  *
 *
 ***********************************************
 **********************************************
 *    MACRO 1: typedef_LTLIBRARY_ROOT_INTERFACE
 * MACRO TYPE: LTLibrary ROOT Interface DECLARATION macro
 *  PLACEMENT: Placed in Library PUBLIC INTERFACE HEADER FILES ONLY.
 *   QUANTITY: Exactly ONE per library.
 *      USAGE: typedef_LTLIBRARY_ROOT_INTERFACE(libraryName, interfaceVersion)
 *
 * EXAMPLE:
 *   typedef_LTLIBRARY_ROOT_INTERFACE(MyLibrary, 1) {
 *       void (* MyLibraryFunction1)(void);
 *       void (* MyLibraryFunction2)(void);
 *   } LTLIBRARY_INTERFACE;
 */
#define typedef_LTLIBRARY_ROOT_INTERFACE(libraryName, interfaceVersion)   \
    enum { k##libraryName##_Version = interfaceVersion };                  \
    enum { k##libraryName##_InterfaceType = kLTInterfaceType_LibraryRoot }; \
    typedef const struct libraryName##Api libraryName;                       \
    struct libraryName##Api {                                                 \
        INHERIT_LIBRARY_BASE                                                   \
        struct

#define TYPEDEF_LTLIBRARY_ROOT_INTERFACE(libraryName, interfaceVersion)   \
    enum { k##libraryName##_Version = interfaceVersion };                  \
    enum { k##libraryName##_InterfaceType = kLTInterfaceType_LibraryRoot }; \
    typedef const struct libraryName##Api libraryName;

/*********************************************
 *    MACRO 2: define_LTLIBRARY_ROOT_INTERFACE
 * MACRO TYPE: LTLibrary ROOT Interface DEFINITION macro
 *  PLACEMENT: Placed in Library PRIVATE IMPLEMENTATION FILES ONLY.
 *   QUANTITY: Exactly ONE per library.
 *      USAGE: define_LTLIBRARY_ROOT_INTERFACE(libraryName [, RunFunction [, StackSize]])
 *
 * EXAMPLE:
 *  static void MyLibraryImpl_MyLibraryFunction1(void);
 *  static void MyLibraryImpl_MyLibraryFunction2(void);
 *  static int MyLibraryImpl_RunFunction(int argc, const char ** argv) { LT_GetCore()->ConsolePrint("Hello World!\n"); return 0; }
 *  \_______________________________
 *   define_LTLIBRARY_ROOT_INTERFACE(MyLibrary, MyLibraryImpl_RunFunction) {
 *       .MyLibraryFunction1 = MyLibraryImpl_MyLibraryFunction1,
 *       .MyLibraryFunction2 = MyLibraryImpl_MyLibraryFunction2
 *   } LTLIBRARY_DEFINITION;
 *
 * VARIANTS:
 *   define_LTLIBRARY_ROOT_INTERFACE(libraryName)
 *   define_LTLIBRARY_ROOT_INTERFACE(libraryName, runFunction)
 *   define_LTLIBRARY_ROOT_INTERFACE(libraryName, runFunction, stackSizeForRunFunction)
 */
#define define_LTLIBRARY_ROOT_INTERFACE(libraryName, ...) \
    FORWARD_DECLARE_LTLIBRARY_STATIC_STRUCT(libraryName)   \
    DEFINE_LTLIBRARY_PROTOTYPE(libraryName, __VA_ARGS__)

/*****************************************
 *    MACRO 3: typedef_LTLIBRARY_INTERFACE
 * MACRO TYPE: standard LTLibrary Interface (LTInterface) DECLARATION macro
 *  PLACEMENT: Placed in Library PUBLIC INTERFACE HEADER FILES.
 *   QUANTITY: Not limited.
 *      USAGE: typedef_LTLIBRARY_INTERFACE(interfaceName, interfaceVersion)
 *
 * EXAMPLE:
 *   typedef_LTLIBRARY_INTERFACE(MyInterface, 1) {
 *       void (* MyInterfaceFunction1)(void);
 *       void (* MyInterfaceFunction2)(void);
 *   } LTLIBRARY_INTERFACE;
 */
#define typedef_LTLIBRARY_INTERFACE(interfaceName, interfaceVersion)      \
    enum { k##interfaceName##_Version = interfaceVersion };                \
    enum { k##interfaceName##_InterfaceType = kLTInterfaceType_Interface }; \
    typedef const struct interfaceName##Api interfaceName;                        \
     struct interfaceName##Api {                                                   \
        INHERIT_INTERFACE_BASE                                                 \
        struct

#define TYPEDEF_LTLIBRARY_INTERFACE(interfaceName, interfaceVersion)      \
    enum { k##interfaceName##_Version = interfaceVersion };                \
    enum { k##interfaceName##_InterfaceType = kLTInterfaceType_Interface }; \
    typedef const struct interfaceName##Api interfaceName;

/****************************************
 *    MACRO 4: define_LTLIBRARY_INTERFACE
 *       TYPE: standard LTLibrary Interface (LTInterface) DEFINITION macro
 *  PLACEMENT: Placed in Library PRIVATE IMPLEMENTATION FILES ONLY.
 *   QUANTITY: Not limited.
 *      USAGE: define_LTLIBRARY_INTERFACE(interfaceName [, onDestroyHandleProc])
 *
 * EXAMPLE:
 *  static void MyInterfaceImpl_MyInterfaceFunction1(void);
 *  static void MyInterfaceImpl_MyInterfaceFunction2(void);
 *  \__________________________
 *   define_LTLIBRARY_INTERFACE(MyInterface) {
 *       .MyInterfaceFunction1 = MyInterfaceImpl_MyInterfaceFunction1,
 *       .MyInterfaceFunction2 = MyInterfaceImpl_MyInterfaceFunction2
 *   } LTLIBRARY_DEFINITION;
 *
 * VARIANTS:
 *   define_LTLIBRARY_INTERFACE(interfaceName)
 *   define_LTLIBRARY_INTERFACE(interfaceName, onDestroyHandleProc)
 */
#define define_LTLIBRARY_INTERFACE(name, ...)                                              \
    static const struct name##Api s_##name;                                                \
    DEFINE_LTINTERFACE_PROTOTYPE(name, __VA_ARGS__)

/************************************************
 **********************************************
 *    MACRO 5: typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE
 * MACRO TYPE: include/lt/device/[*]/LTDevice[*].h ONLY Library ROOT Interface DECLARATION macro
 *  PLACEMENT: Placed in <lt/device/[*]/LTDevice[*].h> PUBLIC INTERFACE HEADER FILES ONLY.
 *   QUANTITY: Exactly ONE per LT device library. Typically only used by LT Team
 *      USAGE: typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(deviceLibraryName, interfaceVersion)
 *
 * EXAMPLE:
 *   typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceFooBarSensor, 1) {
 *       void (* MyLibraryFunction1)(void);
 *       void (* MyLibraryFunction2)(void);
 *   } LTLIBRARY_INTERFACE;
 */
#define typedef_LTDEVICE_LIBRARY_ROOT_INTERFACE(libraryName, interfaceVersion)                 \
    enum { k##libraryName##_Version = interfaceVersion };                                       \
    enum { k##libraryName##_InterfaceType = kLTInterfaceType_DeviceLibraryRoot };                \
    typedef struct libraryName libraryName;                                                       \
    struct libraryName {                                                                           \
        INHERIT_DEVICE_LIBRARY_BASE                                                                 \
        struct

#define TYPEDEF_LTDEVICE_LIBRARY_ROOT_INTERFACE(libraryName, interfaceVersion)                 \
    enum { k##libraryName##_Version = interfaceVersion };                                       \
    enum { k##libraryName##_InterfaceType = kLTInterfaceType_DeviceLibraryRoot };                \
    typedef struct libraryName libraryName;

/*********************************************
 *    MACRO 6: define_LTDEVICE_LIBRARY_ROOT_INTERFACE
 * MACRO TYPE: LTDevice[*] Library ROOT Interface DEFINITION macro for LTDevice libraries only
 *  PLACEMENT: Placed in LTDevice library PRIVATE IMPLEMENTATION FILES ONLY.
 *   QUANTITY: Exactly ONE per LTDevice library.  Only for LTDevice libraries.
 *      USAGE: define_LTDEVICE_LIBRARY_ROOT_INTERFACE(libraryName [, RunFunction [, StackSize]])
 *
 * EXAMPLE:
 *  static void LTDeviceFooBarSensorImpl_FooBarSensorFunction1(void);
 *  static void LTDeviceFooBarSensorImpl_FooBarSensorFunction2(void);
 *  static int LTDeviceFooBarSensorImpl_RunFunction(int argc, const char ** argv) { LT_GetCore()->ConsolePrint("Hello World!\n"); return 0; }
 *  \_______________________________
 *   define_LTDEVICE_LIBRARY_ROOT_INTERFACE(LTDeviceFooBarSensor, LTDeviceFooBarSensorImpl_RunFunction) {
 *       .FooBarSensorFunction1 = LTDeviceFooBarSensorImpl_FooBarSensorFunction1,
 *       .FooBarSensorFunction2 = LTDeviceFooBarSensorImpl_FooBarSensorFunction2
 *   } LTLIBRARY_DEFINITION;
 *
 * VARIANTS:
 *   define_LTDEVICE_LIBRARY_ROOT_INTERFACE(deviceLibraryName)
 *   define_LTDEVICE_LIBRARY_ROOT_INTERFACE(deviceLibraryName, runFunction)
 *   define_LTDEVICE_LIBRARY_ROOT_INTERFACE(deviceLibraryName, runFunction, stackSizeForRunFunction)
 */
#define define_LTDEVICE_LIBRARY_ROOT_INTERFACE(deviceLibraryName, ...)                                  \
    static deviceLibraryName s_##deviceLibraryName;                                                \
    static u32 deviceLibraryName##Impl_GetNumDeviceUnits(void);                                           \
    static LTDeviceUnit deviceLibraryName##Impl_CreateDeviceUnitHandle(u32 nDeviceUnitNum);                \
    static struct LTDeviceLibraryExt * deviceLibraryName##Impl_GetLTDeviceLibraryExt(void) { return NULL; } \
    DEFINE_LTLIBRARY_PROTOTYPE(deviceLibraryName, __VA_ARGS__)                                              \
        .GetNumDeviceUnits      = deviceLibraryName##Impl_GetNumDeviceUnits,                               \
        .CreateDeviceUnitHandle = deviceLibraryName##Impl_CreateDeviceUnitHandle,                         \
        .GetLTDeviceLibraryExt  = deviceLibraryName##Impl_GetLTDeviceLibraryExt,

/****************************************
 *    MACRO 7: define_LTDEVICE_DRIVER_IMPLEMENTATION
 *       TYPE: Specialized MACRO for LT Device Driver Implementation Libraries ONLY
 *             Declares and defines LTDriver library root interface.
 *  PLACEMENT: Placed in LTDriver Library private implementation file
 *   QUANTITY: 1 per LTDriver library.
 *      USAGE: define_LTDEVICE_DRIVER_IMPLEMENTATION(deviceLibraryName_aka_device_class, driverLibraryName)
 *    EXAMPLE: define_LTDEVICE_DRIVER_IMPLEMENTATION(LTDeviceFooBarSensor, HalfordDriverFooBarSensor)
 */
#define define_LTDEVICE_DRIVER_IMPLEMENTATION(deviceLibraryName, driverLibraryName)              \
    enum { k##driverLibraryName##_Version = k##deviceLibraryName##_Version };                     \
    enum { k##driverLibraryName##_InterfaceType = kLTInterfaceType_DriverLibraryRoot };            \
    typedef struct driverLibraryName driverLibraryName;                                             \
    struct driverLibraryName {                                                                       \
        INHERIT_DRIVER_LIBRARY_BASE                                                                   \
    };                                                                                                 \
    static driverLibraryName s_##driverLibraryName;                                               \
    static u32 driverLibraryName##Impl_GetNumDeviceUnits(void);                                          \
    static LTDeviceUnit driverLibraryName##Impl_CreateDeviceUnitHandle(u32 nDeviceUnitNum);               \
    static struct LTDriverLibraryExt * driverLibraryName##Impl_GetLTDriverLibraryExt(void) { return NULL; } \
    DEFINE_LTLIBRARY_PROTOTYPE_NAME_AND_RUNFUNCTION_AND_STACKSIZE_AND_RODATA_RAM_CRITICAL_SECTION_NUMBER(driverLibraryName, NULL, 0, 0) \
        .GetNumDeviceUnits      = driverLibraryName##Impl_GetNumDeviceUnits,                              \
        .CreateDeviceUnitHandle = driverLibraryName##Impl_CreateDeviceUnitHandle,                        \
        .GetLTDriverLibraryExt  = driverLibraryName##Impl_GetLTDriverLibraryExt                        \
    };


/****************************************
 *    MACRO 8: define_LTLIBRARY_APPLICATION
 *       TYPE: Specialized MACRO for APPLICATION LTLIBRARIES
 *             Declares and defines empty library root interface,
 *             implements LibInit() and LibFini(), and specifies
 *             and declares library Run() function as AppName##_Main().
 *              .
 *  PLACEMENT: Placed in Application private implementation file
 *   QUANTITY: 1 per Application.
 *      USAGE: define_LTLIBRARY_APPLICATION(appName, appVersion, stackSize)
 *    EXAMPLE: HelloWorld.c : a complete example
 *  _______________________
 *  #include <lt/core/LTCore.h>
 *
 *  define_LTLIBRARY_APPLICATION(HelloWorld, 1, 2048); / * (app name, version, stack size) * /
 *
 *  static int HelloWorld_Main(int argc, const char ** argv) {
 *      LT_UNUSED(argc); LT_UNUSED(argv);
 *      LT_GetCore()->ConsolePrint("Hello World.\n");
 *      return 0;
 *  }
 *
 */
#define define_LTLIBRARY_APPLICATION(appName, appVersion, stackSize)                                      \
    static int appName##_Main(int argc, const char ** argv);                                                 \
    LT_EXTERN_C_BEGIN                                                                                      \
        typedef_LTLIBRARY_ROOT_INTERFACE(appName, appVersion)                  LTLIBRARY_EMPTY_INTERFACE;  \
        define_LTLIBRARY_ROOT_INTERFACE(appName, appName##_Main, stackSize)    LTLIBRARY_DEFINITION;         \
        static bool appName##Impl_LibInit(void) { return true; }                                                \
        static void appName##Impl_LibFini(void) { }                                                              \
    LT_EXTERN_C_END

/****************************************************************
 ****************************************************************
 * ______________________________________________________________
 * 12. LTLIBRARY_EXPORT_INTERFACES()                 USE DIRECTLY
 *                     USE DIRECTLY   USE DIRECTLY   USE DIRECTLY
 *
 * After declaring and defining your LTLIBRARY_ROOT_INTERFACE
 * and any LTLIBRARY_INTERFACEs, if you have LTLIBRARY_INTERFACEs,
 * in order to be visible**, they must be exported using the
 * LTLIBRARY_EXPORT_INTERFACES(libraryName, sequenceOfInterfaces)
 * macro.
 *
 *  **visible in this context means that the interfaces will be
 *    returned from both pLibrary->GetInterface(interfaceName),
 *    and LT_GetCore()->GetLibraryInterface(pLibary, interfaceName);
 *    Unless this macro is employed, those functions won't return
 *    any interfaces.
 *
 * _____________
 * Example Usage:
 * The library, MyLibrary, has declared and defined four
 * library interfaces: ISuppose, IDecline, IForget, and IAgree.
 *
 * The following macro invocation should be placed after all of
 * MyLibrary's interface definitions, in the same source file
 * that contains them:
 *
   LTLIBRARY_EXPORT_INTERFACES(
     MyLibrary, (ISuppose) (IDecline) (IForget) (IAgree)
   )
 *
 * The first argument is the library name, and the second argument is
 * a sequence of the interfaces to export.
 *   (A)(list)(of)(tokens)(like)(this)(is)(called)(a)(sequence)
 * and is actually one parameter to the preprocessor, not several.
 * Weird but true.
 *       ___________________________                            */
 #define LTLIBRARY_EXPORT_INTERFACES(libName, interfaces) LTLIBRARY_EXPORT_INTERFACE_LIST(CURRENTLY_BUILDING_LTLIBRARY, libName, interfaces)

 #define LTLIBRARY_EXPORT_INTERFACE_LIST(cblName, libName, interfaces) LTLIBRARY_EXPORT_LIBRARY_INTERFACES(cblName, libName, interfaces)
 #define LTLIBRARY_EXPORT_LIBRARY_INTERFACES(cblName, libName, interfaces)          \
    DECLARE_LTLIBRARY_EXPORTED_INTERFACE_LIST(interfaces)                           \
    static const LTInterface * s_p##cblName##_ExportedInterfaces[] = {              \
        LTLIBRARY_EXPORTED_INTERFACE_LIST(interfaces)                               \
        NULL                                                                        \
    };                                                                              \
    static void libName##Impl_InitializeExportedInterfacesFunction(void) {          \
        u32 i = 0;                                                                  \
        INITIALIZE_LTLIBRARY_EXPORTED_INTERFACE_LIST(interfaces)                    \
    }                                                                               \
    static LTLibrary_InitializeExportedInterfacesFunction *                         \
        s_p##libName##Impl_InitializeExportedInterfacesFunction =                   \
            & libName##Impl_InitializeExportedInterfacesFunction;                   \
    static const LTInterface ** s_pp##libName##_ExportedInterfaces =                \
                                 s_p##cblName##_ExportedInterfaces;

/*******************************************
 * ________________________________________*
 * 13. Ensure CURRENTLY_BUILDING_LT_LIBRARY
 */
#if ! defined(CURRENTLY_BUILDING_LTLIBRARY)
    #undef  define_LTLIBRARY_ROOT_INTERFACE
    #undef  define_LTLIBRARY_INTERFACE
    #undef  LTLIBRARY_EXPORT_INTERFACES
    #define define_LTLIBRARY_ROOT_INTERFACE(name, ...)                                          \
        LTTYPES_CPP_PRAGMA_MESSAGE(CURRENTLY_BUILDING_LTLIBRARY_UNDEFINED_ERROR_MESSAGE)       \
        union strike_local_299_stop_compiler __LINE__  {
    #define define_LTLIBRARY_INTERFACE(name, ...)                                            \
        LTTYPES_CPP_PRAGMA_MESSAGE(CURRENTLY_BUILDING_LTLIBRARY_UNDEFINED_ERROR_MESSAGE)    \
        union strike_local_299_stop_compiler __LINE__  {
    #define LTLIBRARY_EXPORT_INTERFACES(library, interfaces)                              \
        LTTYPES_CPP_PRAGMA_MESSAGE(CURRENTLY_BUILDING_LTLIBRARY_UNDEFINED_ERROR_MESSAGE) \
        union strike_local_299_stop_compiler __LINE__  {};
#endif

 /**************
/ 14. CLOSURE */
#if defined(__cplusplus)
 }
#endif
/*************************************************************/

#ifdef DOXY_SKIP // [
/* Redefine standard method struct macros to things that look better in doxygen */

#undef INHERIT_INTERFACE_BASE
#define INHERIT_INTERFACE_BASE \
  /** Methods inherited from the LTInterfaceBase interface (not really a named member). */ \
  LTInterfaceBase _base_interface_methods;

#undef INHERIT_LIBRARY_BASE
#define INHERIT_LIBRARY_BASE \
  /** Methods inherited from the LTLibraryBase interface (not really a named member). */ \
  LTLibraryBase _base_interface_methods;

#undef INHERIT_DRIVER_LIBRARY_BASE
#define INHERIT_DRIVER_LIBRARY_BASE \
  /** Methods inherited from the LTDriverLibraryBase interface (not really a named member). */ \
  LTDriverLibraryBase _base_interface_methods;

#undef INHERIT_DEVICE_LIBRARY_BASE
#define INHERIT_DEVICE_LIBRARY_BASE \
  /** Methods inherited from the LTDeviceLibraryBase interface (not really a named member). */ \
  LTDeviceLibraryBase _base_interface_methods;
#endif // DOXY_SKIP ]

/*  __________________________________________________________
 * /_________________________________________________________/ *
 * \_________________________________________________________\*
#include <lt/LTObject.h> is here for the time being.        */
#include <lt/LTObject.h>

/* ___________________________________________________________
 * ===========================================================
 *
 *
 * ___________________________________________________________
 * ***********************************************************
 * ***********************************************************
 *                        FIN
 * __________________________________________________________
 * _________________________________________________________/
 * ________________________________________________________/
 * _______________________________________________________/
 * From here below, the rest of this file is documentation.
 *
 * ________________________________________
 * ****************************************
 * I.  OVERVIEW OF LTTypes.h
 * ========================================
 * ___________
 * A. ABSTRACT
 * LTTypes.h defines the core types used by LT for:
 *   a. the core LT operating system
 *   b. the method of organizing and packaging code into LT Libraries
 *   c. the Public API and Private Implementations for all LT Libraries
 *
 * LTTypes.h is standalone - it does not and must never #include any other
 * header file.  It defines a small set of types, structures, macros, and
 * short functions, all for the singular purpose of enabling LT Libraries
 * to be "Platform Independent", allowing them to target arbitrary platforms
 * without ever requiring "porting" and consequently without ever accruing the
 * mounting technical debt that comes with "porting".
 * ______________________________________________
 * B. PORTABLE CODE VS. PLATFORM INDEPENDENT CODE
 * Unlike 'portable code', which relies on C preprocessor conditional
 * compilation via #if and #ifdef directives to compile different code for
 * different combinations of hardware platform, os, toolchain, product
 * configuration, etc., LT libraries are said to be 'platform independent',
 * a term used to describe their property of being able to be simply recompiled
 * for a target platform without the use of conditional compilation
 * (or any other mechanism of code substitution, such as conditional makefiles).
 *
 * Platform Independence for LT Libraries is enabled by:
 *
 * -> 1. limiting the set of allowed types to those defined in this file
 *       (and aggregate types thereof).
 *
 * -> 2. requiring lexically identical code across all crossed variants of
 *       h/w platform, processor architecture, underlying os, product model,
 *       product model configuration, feature enablement/entitlement, etc.
 *       This is accomplished by restricting all use of the preprocessor
 *       directives #if, #ifdef, and #ifndef to:
 *         a. the inclusion or exclusion of debug mode code using
 *            #ifdef LT_DEBUG and #ifndef LT_DEBUG
 *         b. #ifndef include guards at the top of header files, like the one
 *            found at the top of this file
 *         c. the inclusion and exclusion of experimental code blocks
 *            in library private implementation files either by using
 *            #if 0 and #if 1, or by using preprocessor symbols locally
 *            defined in library private implementation files that are not
 *            influenced by build-time definitions (compiler -D directives).
 *            _________
 *            IMPORTANT: LTTypes.h is the only source file where conditional
 *                       compilation is permitted.  Period.  Re-read the above
 *                       list if clarification is needed.
 *            CONDITIONAL COMPILATION IS NOT PERMITTED IN YOUR SOURCE FILES.
 *
 * -> 3. restricting header file inclusion to eliminate the possibility of
 *       external dependency inclusion and spread, a practice referred to as
 *       Self-Containment.  Header file inclusion is restricted in both LT
 *       Library Public Interface and Private Implementation files as follows:
 *         a. LT Public Interface Header Files may only include:
 *              i. other header files from the same LT Library public interface
 *             ii. header files from other LT Library public interfaces
 *         b. LT Private Implementation Source and Header files may only include:
 *              i. header files from its LT Library Public Interface
 *             ii. header files from other LT Library Public Interfaces
 *            iii. other header files from the same library private implementation
 * _______
 * SUMMARY
 *  LT Library Source and Header files:
 *    a. only use the types defined herein and aggregates thereof.
 *    b. limit #includes to public interface header files from their own or
 *       other LT Libraries. Private implementation files may also include
 *       header files existent within the same private implementation.
 *    c. never use conditional compilation (#if, #ifdef or #ifndef) except for
 *       (1) header file #include guards, (2) conditionalizing debug code using
 *       LT_DEBUG, and (3) for conditionalizing experimental code in private
 *       implementation files only using #if 0, #if 1, or with locally
 *       defined preprocessor symbols that aren't defined or influenced by compiler
 *       cmdline -D definitions.
 *
 * ______________
 * IMPORTANT NOTE
 *   LTTypes.h is designed to concentrate and localize all uses of conditional compilation
 *             into itself in order to eliminate one of the largest sources of technical
 *             debt in C programming.  Your cooperation with the following is required:
 *             _____________________________________________________
 *   --------> ALL CONDITIONAL COMPILATION IS STRICTLY PROHIBITED <-|  except in narrow cases
 *   --------> FROM ALL SOURCE FILES, INCLUDING YOUR SOURCE FILES <-|  of List Item 2 above
 * ******************************************************************************************
 * _______________________________________
 * ***************************************
 * II. LT FUNDAMENTAL TYPES AND TYPE USAGE
 * =======================================
 *
 * _______
 * Table 1: LT Fundamental types
 *
 *   Type                         | Use
 * -------------------------------|--------------------------------
 *   u8                           | an unsigned 8  bit quantity
 *   u16                          | an unsigned 16 bit quantity
 *   u16                          | an unsigned 32 bit quantity
 *   u64                          | an unsigned 64 bit quantity
 *   s8                           | a  signed   8  bit quantity
 *   s16                          | a  signed   16 bit quantity
 *   s32                          | a  signed   32 bit quantity
 *   s64                          | a  signed   64 bit quantity
 *   char                         | a  character
 *   const char                   | a  constant character
 *   char *                       | a  modifiable c string
 *   const char *                 | a  read-only c string
 *   void                         | when function returns no value
 *   void *                       | an opaque pointer or handle
 *   bool                         | a boolean value of true or false
 *   LT_SIZE                      | used in lieu of size_t
 *
 * _______
 * Table 2: LT Usage - pointer declarations in function argument lists
 *
 *   Pointers in function argument lists       | Semantic Use
 * --------------------------------------------|-------------------------------
 *   void func(const u32 * pNum, u32 nCount);  | When pNum supplies a vector of
 *                                             | nCount elements that won't be
 *                                             | modified inside func()
 *                                             |
 *   void func(u32 * pNumsToSet, u32 nCount);  | When pNumsToSet supplies the
 *                                             | start address of a vector of at
 *                                             | least nCount elements, where
 *                                             | func() may write up to nCount
 *                                             | elements into *pNumsToSet
 *
 * ________
 * Table 3: LT Usage - the 4 elements of callback procedure semantics
 *
 * 1. _________________________
 *    Callback Function Typedef
 *
 *      typedef bool (AcmeTable_ValueEnumCBProc)(AcmeTable_Table * pTable, u32 nValue, void * pClientData);
 *
 *      -> Declares AcmeTable_ValueEnumCBProc as a callback procedure for
 *         enumerating u32 values.
 *      -> By convention, enumeration callbacks return
 *         true to continue enumerating, false to abort enumeration.
 *      -> by convention the client data passed thru to the callback proc is
 *         of type void * and is referred to as pClientData in code and
 *         "client data" in conversation.
 *
 * 2. ____________________________
 *    Callback Function Definition
 *
 *      static bool MyTableValueEnumCB(AcmeTable_Table * pTable, u32 nValue, void * pClientData) {
 *          struct MyData * pData = (struct MyData *)pClientData;
 *          pData->nNumValuesSearched++;
 *          / * abort enumeration when we find the value we're looking for * /
 *          return (nValue == pData->nValueLookingFor) ? false : true;
 *       }
 *
 * 3. _______________________________________________
 *    Declaration of Function with Callback Parameter
 *
 *      bool AcmeTable_EnumTableValues(AcmeTable_Table * pTable,
 *                                     AcmeTable_ValueEnumCBProc * pCBProc,
 *                                     void * pClientData);
 *
 *      -> Callback argument type is pointer to callback function type
 *      -> By convention enumeration functions return true if the enumeration continued until
 *         the end of enumeration, false if enumeration was aborted by the callback.
 *
 * 4. _______________________________________________
 *    Invocation of Function with Callback Parameter
 *
 *      static bool CheckTableForValue(AcmeTable_Table * pTable, u32 nValue) {
 *          struct MyData data;
 *          data.nNumValuesSearched = 0;
 *          data.nValueLookingFor = nValue;
 *          / * enumerate the values in the table.  If we aborted enumeration
 *              in the callback then we found the item and can return true   * /
 *          return
 *              AcmeTable_EnumTableValues(pTable, &MyTableValueEnumCB, &data)
 *                  ? false : true);
 *      }
 *
 * ________________________________________________
 * ************************************************
 * III. DECLARING AND DEFINING LTLibrary Interfaces
 * ================================================
 * There are two types of interfaces:
 *  (1) the singleton LTLibrary root interface (conforms to type LTLibrary *)
 *  (2) regular LTLibrary interfaces (conforming to type LTInterface *)
 *
 * There are two macros for each:
 *  (a) the declaration (typedef) macro that goes in library public interface headers
 *      (a.1) typedef_LTLIBRARY_ROOT_INTERFACE { ... } LTLIBRARY_INTERFACE;
 *      (a.2) typedef_LTLIBRARY_INTERFACE { ... } LTLIBRARY_INTERFACE;
 *  (b) the definition (define) macro that goes in a library private implementation (.c) file.
 *      (b.1) define_LTLIBRARY_ROOT_INTERFACE { ... } LTLIBRARY_DEFINITION;
 *      (b.2) define_LTLIBRARY_INTERFACE { ... } LTLIBRARY_DEFINITION;
 *
 *********************************************************************
 * USAGE EXAMPLES of typedef_LTLIBRARY_INTERFACE and define_LTLIBRARY_INTERFACE
 * _________________________________________________________________
 * A. typedef_LTLIBRARY_ROOT_INTERFACE, typedef_LTLIBRARY_INTERFACE \
 * ___________
 * DESCRIPTION
 *   These macros are used in library public header files to declare LTLibrary public interfaces.  A library must
 *   export 1 interface with the same name as the library.  This is known as the library root interface,
 *   and always exists.  A library may export additional interfaces as well.  The library root interface is
 *   returned from the LTCore OpenLibrary() function and contains both the LTInterface and LTLibrary prototype
 *   functions.  Regular LT Library (non-root) interfaces contain only the LTInterface prototype functions and are obtained
 *   with the LTLibrary prototype's GetInterface() and GetInterfaces() functions.
 * _____
 * USAGE (example library named "MyLibrary")
 *
 *  typedef_LTLIBRARY_ROOT_INTERFACE(MyLibrary, 1) {
 *      void (* Foo)(void);
 *      void (* Bar)(Foo * pFoo);
 *      void (* Baz)(Foo * pFoo, Bar * pBar);
 *  } LTLIBRARY_INTERFACE;
 *
 *  typedef_LTLIBRARY_INTERFACE(IClaudius, 3) {
 *      void (* ProclaimLiviaTheQueenOfHeaven)(bool bBeforeSheDies);
 *      void (* MarryAgrippinaTheYounger)(void);
 *      void (* PrepareForExile)(void);
 * } LTLIBRARY_INTERFACE;
 * ______
 * DETAIL
 *  The first interface, MyLibrary version 1, is the library root interface for the library MyLibrary,
 *  made so by declaring it with typedef_LTLIBRARY_ROOT_INTERFACE.  The second interface, IClaudius version 3,
 *  is a regular library interface exported by the library MyLibrary.
 *
 *  The typedef_LTLIBRARY macros create one typedef for each interface:
 *    MyLibrary
 *    IClaudius
 *  These interfaces are obtained as follows:
 *    MyLibrary * pMyLibrary = (MyLibrary *)LTCore_GetCore()->OpenLibrary("MyLibrary"); / * returns NULL if MyLibrary not present * /
 *    IClaudius * pIClaudius = pMyLibrary ? (IClaudius *)pMyLibrary->GetInterface("IClaudius") : NULL;
 *
 *  There are also convenience macros that make the job a little easier:
 *    MyLibrary * pMyLibrary = lt_openlibrary(MyLibrary);                        / * returns NULL if MyLibary not present * /
 *    IClaudius * pIClaudius = lt_getlibraryinterface(IClaudius, pMyLibrary);    / * passing in NULL for pMyLibrary is ok (will return NULL), no need to guard * /
 *
 *  The typedef_LTLIBRARY macros also create one enum constant for each interface:
 *    kMyLibrary_Version = 1
 *    kIClaudius_Version = 3
 *
 * _______
 * CASTING
 *    ___________________________________
 *    MyLibrary -> LTLibrary, LTInterface
 *    MyLibrary declares its own interface functions: Foo, Bar, and Baz.  It also contains both the LTInterface and LTLibrary prototype
 *    functions.  It can be used as type MyLibrary, or cast to (LTLibrary *)pMyLibrary or cast to (LTInterface *)pMyLibrary
 *    with the following function access:
 *           ____   ___________________   ___________________   _____________________
 *           Type   MyLibrary Functions   LTLibrary Functions   LTInterface Functions
 *      MyLibrary                   Yes                   Yes                     Yes
 *      LTLibrary                    No                   Yes                     Yes
 *    LTInterface                    No                    No                     Yes
 *    ___________________________________
 *    LTLibrary, LTInterface -> MyLibrary
 *    It is safe to upcast a generic LTLibrary * or LTInterface * to a MyLibrary * when GetInterfaceName() returns "MyLibrary", e.g.:
 *      LTLibrary   * pLTLibrary = pSomeGenericLTLibraryPointerPreviouslySet;
 *      LTInterface * pLTInterface = pSomeGenericLTInterfacePointerPreviouslySet;
 *      MyLibrary   * pMyLibrary1 = (0 == lt_strcmp(pLTLibrary->GetInterfaceName(),   "MyLibrary")) ? (MyLibrary *)pLTLibrary : NULL;
 *      MyLibrary   * pMyLibrary2 = (0 == lt_strcmp(pLTInterface->GetInterfaceName(), "MyLibrary")) ? (MyLibrary *)pLTLibrary : NULL;
 *    ________________________
 *    IClaudius -> LTInterface
 *    IClaudius declares its own interface functions: ProclaimLiviaTheQueenOfHeaven(), MarryAgrippinaTheYounger(), and
 *    PrepareForExile().  It also contains the LTInterface prototype functions.  It can be used as type IClaudius, or cast to
 *    (LTInterface *)pIClaudius with the following function access:
 *           ____   ___________________   _____________________
 *           Type   IClaudius Functions   LTInterface Functions
 *      IClaudius                   Yes                     Yes
 *    LTInterface                    No                     Yes
 *    ________________________
 *    LTInterface -> IClaudius
 *    It is safe to upcast a generic LTInterface * to an IClaudius * when GetInterfaceName() returns "IClaudius", e.g.:
 *      LTInterface * pLTInterface = pSomeGenericLTInterfacePointerPreviouslySet;
 *      IClaudius   * pIClaudius = (0 == lt_strcmp(pLTInterface->GetInterfaceName(), "IClaudius")) ? (IClaudius *)pLTInterface : NULL;
 *   ________________________
 *   LTInterface -> LTLibrary
 *   Given any generic LTInterface *, it is safe to cast it to a generic LTLibrary * when the interface's GetLibrary() function
 *   returns the same pointer, e.g.
 *      LTInterface * pLTInterface = pSomeLTLibraryPointerPreviouslySet;
 *      LTLibrary   * pLTLibrary   = (pLTInterface->GetLibrary() == pLTInterface) ? (LTLibrary *)pLTInterface : NULL;
 *********************************
 *  _______________________________________________________________
 *  B. define_LTLIBRARY_ROOT_INTERFACE, define_LTLIBRARY_INTERFACE \
 * ___________
 * DESCRIPTION
 *   These macros are used in library private implementation files to define the LTLibrary interface implementation
 *   structs that bind interface function pointers to implementation functions.
 * _____
 * USAGE (example library named "MyLibrary")
 *
 *  static void MyLibraryImpl_Foo(void);
 *  static void MyLibraryImpl_Bar(Foo * pFoo);
 *  static void MyLibraryImpl_Baz(Foo * pFoo, Bar * pBar);
 *
 *  define_LTLIBRARY_ROOT_INTERFACE(MyLibrary) {
 *      .Foo = MyLibraryImpl_Foo,
 *      .Bar = MyLibraryImpl_Bar,
 *      .Baz = MyLibraryImpl_Baz
 *  } LTLIBRARY_DEFINITION;
 *
 * static void MyLibraryImpl_ProclaimLiviaTheQueenOfHeaven(bool bBeforeSheDies);
 * static void MyLibraryImpl_MarryAgrippinaTheYounger(void);
 * static void MyLibraryImpl_PrepareForExile(void);
 *
 *  define_LTLIBRARY_INTERFACE(IClaudius) {
 *      .ProclaimLiviaTheQueenOfHeaven = MyLibraryImpl_ProclaimLiviaTheQueenOfHeaven,
 *      .MarryAgrippinaTheYounger      = MyLibraryImpl_MarryAgrippinaTheYounger,
 *      .PrepareForExile               = MyLibraryImpl_PrepareForExile
 * } LTLIBRARY_DEFINITION;
 *********************************
 *  _______________________________
 *  C. LTLIBRARY_EXPORT_INTERFACES \
 * ___________
 * DESCRIPTION
 *   If a library defines regular interfaces they must be exported with the
 *   LTLIBRARY_EXPORT_INTERFACES(libraryName, interfaceSequence).
 *   This enables the library's GetInterface() and GetInterfaces() function
 *   to report the interfaces listed in the interfaceSequence passed into the macro.
 *   structs that bind interface function pointers to implementation functions.
 * _____
 * USAGE (example library named "MyLibrary", with additional interface "IClaudius")
 *
 * LTLIBRARY_EXPORT_INTERFACES(MyLibrary, (IClaudius) )
 *
 * Notice the interface IClaudius is in parentheses.  To the C preprocessor, this
 * is a token sequence with one token.  Multiple exported interfaces are each
 * individually placed in parentheses without comma separation, like this:
 *
 * LTLIBRARY_EXPORT_INTERFACES(MyLibrary, (IAugustus) (ITiberius) (ICaligula) (IClaudius) (INero) )
 *
 ******************************************************************************
 ******************************************************************************/
/*
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||                                 |||||||||||||||||||||||
 * |||||||||||||||||||| You have no chance to survive.  |||||||||||||||||||||||
 * ||||||||||||||||||||        Make your time.          |||||||||||||||||||||||
 * ||||||||||||||||||||_________________________________|||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 * ||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||||
 */

#endif // #ifndef ROKU_LT_INCLUDE_LT_LTTYPES_H

/******************************************************************************
 *  LOG
 ******************************************************************************
 *  01-Jan-19   augustus    created
 *  22-Jul-19   augustus    added Run() to LTLibrary
 *  25-Aug-19   constantine Move doxymentation into .dox file
 *  26-Aug-19   augustus    Intro comments without doxy markup for developer reference
 *  05-Nov-19   augustus    added class LTLibrary
 *  17-Dec-19   augustus    added varags support and got rid of windows intrin.h include
 *                          (this file must not include any other)
 *  17-Feb-20   augustus    got rid of DEBUG and RELEASE macro normalization - LT_DEBUG is
 *                          used exclusively (defined or not) to differentiate debug vs. release mode
 *  24-Feb-20   augustus    added GetRunFunctionStacksizeRequirement(); wrote comment guidelines about
 *                          use of third party libraries
 *  03-Mar-20   augustus    on __GNUC__, use __attribute__((used)) in #define of LT_LIBRARY_EXPORT_INTERFACE
 *  20-Mar-20   augustus    added LT_U8_MIN, LT_U8_MAX, etc.
 *  30-Apr-20   augustus    re-created in C
 *  22-Jul-20   augustus    renamed and optimized library root interface declaration and definition
 *                          macros; added ADJUNCT interface declaration and definition, LTHandle part 1
 *  01-Aug-20   augustus    re-optimizated library interface declaration and definition macros with auto detection
 *                          of LTLibrary root interface; finished LTHandle
 *  19-Aug-20   augustus    added LTLIBRARY_EXPORT_INTERFACES
 *  26-Aug-20   augustus    added CURRENTLY_BUILDING_LTLIBRARY to liberate all define_LTLIBRARY_INTERFACE
 *                          blocks from having to all be in the same .c file as the define_LTLIBRARY_ROOT_INTERFACE block
 *  06-Sep-20   augustus    changed the way Destroy() calls OnDestroyHandle() - always only once through LTCore->DestroyHandle
 *  11-Oct-20   augustus    added LTAtomic_PostDecrement and LTAtomic_TestAndSet
 *  11-Nov-20   augustus    added define_LTDRIVER_LIBRARY_DEVICE_CLASS_IMPLEMENTATION
 *  20-Nov-20   augustus    made prototype interfaces LTDeviceLibrary and LTDriverLibrary with attendant magic macros
 *  28-Nov-20   augustus    added LTAtomic32, LTAtomic64 and LTAtomic_LT_SIZE to complement LTAtomic
 *  05-Dec-20   augustus    fixed LTAtomic64 on windows; proper LT_SSIZE type and LT_Pu32, etc. macros for Dan
 *  24-Nov-20   augustus    reworked printf format normalization
 *  12-Dec-20   augustus    added support for lt_va_list on win64; prevent inadvertent use of va_list
 *  16-Aug-21   augustus    added GetDriverLibraryExtrinsicName() to distinguish driver libname from (common) root interface name
 *  15-Dec-21   augustus    added LTHandle type to args
 *  01-Feb-22   augustus    added LT_TEXT_RAM_CRITICAL(n) and LT_RODATA_RAM_CRITICAL(n)
 *  01-Feb-22   augustus    added lt_returnaddress()
 *  08-Mar-22   augustus    added define_LTLIBRARY_APPLICATION
 *  21-Jun-22   augustus    added LT_MIN and LT_MAX
 *  06-Sep-22   aurelian    added LTDeviceDriverConfigEntry
 *  21-Jan-23   augustus    added LTBufferChain and LT_PTRDIFF
 *  18-Mar-23   augustus    made GetLibraryExtrinsicName only be a member of LTLIBRARY_ROOT_INTERFACEs; added GetResourceTree() impl
 *  05-Apr-26   augustus    use LT_GetCore()->GetLTCoreLibraryBuildVersion() for LTLibrary's GetLibraryBuildVersion on systems with LT_NO_DYNAMIC_LOADER to save space
 *  27-Apr-26   augustus    added LTMemoryRegion
 */
