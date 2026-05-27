/*******************************************************************************
 * <lt/device/keypad/LTDeviceKeypad_KeyDefs.h>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *******************************************************************************
 * @file LTDeviceKeypad_KeyDefs.h header for logical keypad key values
 * @brief provide enum values for logical keypad keys
 *
 * @note This file will also generate a static constant array of keyname strings
 *       if doubly included as follows:
 *   #include <lt/device/keypad/LTDeviceKeypad_KeyDefs.h>
 *   #undef  LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_H
 *   #define LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_GENNAMETABLE
 *   #include <lt/device/keypad/LTDeviceKeypad_KeyDefs.h>
 */

#ifndef LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_H
#define LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_H

#include <lt/LTTypes.h>

LT_EXTERN_C_BEGIN;

                                    #if defined(LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_GENNAMETABLE)
                                        #undef  kLTKey_
                                        #define kLTKey_(id_, num_) #id_,
                                        static const char * s_LTKeyButtonNames[kLTKey_LastButtonMarker-kLTKey_FirstButton] = {
                                    #else
                                        #define kLTKey_(id_, num_) kLTKey_##id_ = kLTKey_FirstButton + num_,

typedef_LTENUM_SIZED(LTKey, u32) {
    kLTKey_FlagsKeyDown             =  ((u32)1) << 31,
    kLTKey_Invalid                  =  ~kLTKey_FlagsKeyDown,

    kLTKey_FirstAscii               =   0,
    kLTKey_LastAscii                =   255,
    kLTKey_FirstUnicode             =   256,
    kLTKey_LastUnicode              =   65535,
    kLTKey_FirstButton              =   65536,
    #endif

    /* Buttons - must be monotonically increasing starting at 0 with no gaps */
    kLTKey_(Digit0                  ,   0)
    kLTKey_(Digit1                  ,   1)
    kLTKey_(Digit2                  ,   2)
    kLTKey_(Digit3                  ,   3)
    kLTKey_(Digit4                  ,   4)
    kLTKey_(Digit5                  ,   5)
    kLTKey_(Digit6                  ,   6)
    kLTKey_(Digit7                  ,   7)
    kLTKey_(Digit8                  ,   8)
    kLTKey_(Digit9                  ,   9)
    kLTKey_(Enter                   ,  10)
    kLTKey_(OK                      ,  11)
    kLTKey_(Select                  ,  12)
    kLTKey_(Home                    ,  13)
    kLTKey_(Back                    ,  14)
    kLTKey_(Menu                    ,  15)
    kLTKey_(Info                    ,  16)
    kLTKey_(Exit                    ,  17)
    kLTKey_(Join                    ,  18)
    kLTKey_(Pairing                 ,  19)
    kLTKey_(Guide                   ,  20)
    kLTKey_(Dot                     ,  21)
    kLTKey_(Up                      ,  22)
    kLTKey_(Down                    ,  23)
    kLTKey_(Left                    ,  24)
    kLTKey_(Right                   ,  25)
    kLTKey_(UpLeft                  ,  26)
    kLTKey_(UpRight                 ,  27)
    kLTKey_(DownLeft                ,  28)
    kLTKey_(DownRight               ,  29)
    kLTKey_(Fastforward             ,  30)
    kLTKey_(Rewind                  ,  31)
    kLTKey_(Play                    ,  32)
    kLTKey_(Pause                   ,  33)
    kLTKey_(Playpause               ,  34)
    kLTKey_(Stop                    ,  35)
    kLTKey_(Replay                  ,  36)
    kLTKey_(Forward                 ,  37)
    kLTKey_(Reverse                 ,  38)
    kLTKey_(Eject                   ,  39)
    kLTKey_(Next                    ,  40)
    kLTKey_(Previous                ,  41)
    kLTKey_(ZoomIn                  ,  42)
    kLTKey_(ZoomOut                 ,  43)
    kLTKey_(Rotate                  ,  44)
    kLTKey_(Power                   ,  45)
    kLTKey_(IdleHeartbeat           ,  46)
    kLTKey_(PowerOn                 ,  47)
    kLTKey_(PowerOff                ,  48)
    kLTKey_(User1                   ,  49)
    kLTKey_(User2                   ,  50)
    kLTKey_(User3                   ,  51)
    kLTKey_(User4                   ,  52)
    kLTKey_(User5                   ,  53)
    kLTKey_(User6                   ,  54)
    kLTKey_(User7                   ,  55)
    kLTKey_(User8                   ,  56)
    kLTKey_(User9                   ,  57)
    kLTKey_(User10                  ,  58)
    kLTKey_(UserRpt1                ,  59)
    kLTKey_(UserRpt2                ,  60)
    kLTKey_(UserRpt3                ,  61)
    kLTKey_(UserRpt4                ,  62)
    kLTKey_(UserRpt5                ,  63)
    kLTKey_(UserRpt6                ,  64)
    kLTKey_(UserRpt7                ,  65)
    kLTKey_(UserRpt8                ,  66)
    kLTKey_(UserRpt9                ,  67)
    kLTKey_(UserRpt10               ,  68)
    kLTKey_(Search                  ,  69)
    kLTKey_(Add                     ,  70)
    kLTKey_(Shuffle                 ,  71)
    kLTKey_(Repeat                  ,  72)
    kLTKey_(VolumeUp                ,  73)
    kLTKey_(VolumeDown              ,  74)
    kLTKey_(Volume50                ,  75)
    kLTKey_(VolumeMute              ,  76)
    kLTKey_(Brightness              ,  77)
    kLTKey_(RotaryClockwise         ,  78)
    kLTKey_(RotaryCounterclockwise  ,  79)
    kLTKey_(RotarySwitch            ,  80)
    kLTKey_(Preset1                 ,  81)
    kLTKey_(Preset2                 ,  82)
    kLTKey_(Preset3                 ,  83)
    kLTKey_(Preset4                 ,  84)
    kLTKey_(Preset5                 ,  85)
    kLTKey_(Preset6                 ,  86)
    kLTKey_(Preset7                 ,  87)
    kLTKey_(Preset8                 ,  88)
    kLTKey_(Preset9                 ,  89)
    kLTKey_(Preset10                ,  90)
    kLTKey_(Snooze                  ,  91)
    kLTKey_(Source                  ,  92)
    kLTKey_(ScanUp                  ,  93)
    kLTKey_(ScanDown                ,  94)
    kLTKey_(ChannelUp               ,  95)
    kLTKey_(ChannelDown             ,  96)
    kLTKey_(Group                   ,  97)
    kLTKey_(Alarm                   ,  98)
    kLTKey_(Playlists               ,  99)
    kLTKey_(Browse                  , 100)
    kLTKey_(BrowseArtists           , 101)
    kLTKey_(BrowseAlbums            , 102)
    kLTKey_(BrowseSongs             , 103)
    kLTKey_(BrowseGenres            , 104)
    kLTKey_(BrowseComposers         , 105)
    kLTKey_(PartnerA                , 106)
    kLTKey_(PartnerB                , 107)
    kLTKey_(PartnerC                , 108)
    kLTKey_(PartnerD                , 109)
    kLTKey_(PartnerE                , 110)
    kLTKey_(PartnerF                , 111)
    kLTKey_(PartnerG                , 112)
    kLTKey_(PartnerH                , 113)
    kLTKey_(PartnerI                , 114)
    kLTKey_(PartnerJ                , 115)
    kLTKey_(A                       , 116)
    kLTKey_(B                       , 117)
    kLTKey_(Red                     , 118)
    kLTKey_(Blue                    , 119)
    kLTKey_(Green                   , 120)
    kLTKey_(Yellow                  , 121)
    kLTKey_(Purple                  , 122)
    kLTKey_(LiveTV                  , 123)

                                    #if defined(LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_GENNAMETABLE)
                                    #     undef LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_GENNAMETABLE
                                    };
                                    #else

    kLTKey_LastButtonMarker,
    kLTKey_LastButton               = kLTKey_LastButtonMarker - 1
};

LT_INLINE LTKey LTKey_KeyVal(u32 key)           { return (LTKey)(key & ~kLTKey_FlagsKeyDown); }
LT_INLINE u32   LTKey_MakeKeyDown(LTKey key)    { return key | kLTKey_FlagsKeyDown; }
LT_INLINE u32   LTKey_MakeKeyUp(LTKey key)      { return key & ~kLTKey_FlagsKeyDown; }
LT_INLINE void  LTKey_SetKeyDown(u32 *key)      { *key |= kLTKey_FlagsKeyDown; }
LT_INLINE void  LTKey_SetKeyUp(u32 *key)        { *key &= ~kLTKey_FlagsKeyDown; }
LT_INLINE bool  LTKey_Valid(u32 key)            { return LTKey_KeyVal(key) != kLTKey_Invalid;   }
LT_INLINE bool  LTKey_Invalid(u32 key)          { return LTKey_KeyVal(key) == kLTKey_Invalid;   }
LT_INLINE bool  LTKey_IsDown(u32 key)           { return key & kLTKey_FlagsKeyDown;          }
LT_INLINE bool  LTKey_IsUp(u32 key)             { return (0 == (key & kLTKey_FlagsKeyDown)); }
LT_INLINE bool  LTKey_IsButton(u32 key)         { return ((LTKey_KeyVal(key) >= kLTKey_FirstButton) && (LTKey_KeyVal(key) <= kLTKey_LastButton)); }

#endif

LT_EXTERN_C_END;

#endif /* #ifndef LT_INCLUDE_LT_DEVICE_KEYPAD_LTDEVICEKEYPAD__KEYDEFS_H */

/*******************************************************************************
 *  LOG
 *******************************************************************************
 *  24-Jan-25   augustus    created
 */
