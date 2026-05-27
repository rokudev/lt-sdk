/******************************************************************************
 * ParseSPS.h
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#ifndef ROKU_LT_SOURCE_LT_MEDIA_MP4_PARSESPS_H
#define ROKU_LT_SOURCE_LT_MEDIA_MP4_PARSESPS_H

#include "LTMediaMP4Impl.h"

// NOTE: field names correspond to names in MPEG standard
typedef struct {
    u8 profile_idc;
    u8 level_idc;

    u32 seq_parameter_set_id;

    u32 pic_width_in_mbs_minus1;
    u32 pic_height_in_map_units_minus1;

    u32 frame_crop_left_offset;
    u32 frame_crop_right_offset;
    u32 frame_crop_top_offset;
    u32 frame_crop_bottom_offset;

    struct {
        u32 num_units_in_tick;
        u32 time_scale;
        bool fixed_frame_rate_flag;
    } timing_info;
} ParsedSPS;

ParsedSPS LTMediaMP4_ParseSPS(u8 * bytes, u32 length);

#endif // ROKU_LT_SOURCE_LT_MEDIA_MP4_PARSESPS_H
