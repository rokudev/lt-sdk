/******************************************************************************
 * ParseSPS.c
 *
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, you can obtain one at
 * https://mozilla.org/MPL/2.0/.
 *
 * Copyright 2026, Roku, Inc.  All rights reserved.
 *****************************************************************************/

#include "ParseSPS.h"

static bool GetBit(u8 * buf, u32 bitIndex) {
    u8 byte = buf[bitIndex/8];
    bool res = (byte >> (7 - bitIndex%8)) & 1;
    return res;
}

static u32 ParseExpGolomb(u8 * bytes, u32 * bitIndex) {
    int nZeroes = 0;
    while (GetBit(bytes, (*bitIndex)++) == 0) {
        nZeroes++;
    }
    u32 res = (1 << nZeroes) - 1;
    for (int i = 0; i < nZeroes; i++) {
        res += GetBit(bytes, (*bitIndex)++) << (nZeroes - (i+1));
    }
    return res;
}

// get non-byte aligned unsigned integer (big-endian)
static u32 GetUnaligned(u8 * bytes, u32 * bitIndex, u32 nBits) {
    u32 result = 0;
    for (u32 i = 0; i < nBits; i++) {
        result += GetBit(bytes, (*bitIndex)++) << (nBits-i-1);
    }
    return result;
};

// Remove emulation-prevention three-byte: 0x00 0x00 0x03 0xYY -> 0x00 0x00 0xYY
static void StripEmulationPreventionBytes(u8 * output, const u8 * bytes, u32 length) {
    u8 * cursor = output;
    for (u32 i = 0; i < length; i++) {
        if (i >= 2 && bytes[i-2] == 0x00 && bytes[i-1] == 0x00 && bytes[i] == 0x03) {
            continue;
        }
        *cursor++ = bytes[i];
    }
};

ParsedSPS LTMediaMP4_ParseSPS(u8 * bytes, u32 length) {
    ParsedSPS res = {};

    u8 * sps = lt_malloc(length);
    StripEmulationPreventionBytes(sps, bytes, length);

    res.profile_idc = sps[1];
    res.level_idc = sps[3];
    
    // The rest of the SPS is bit-encoded and not byte-aligned
    // We don't care about most of it, but we need to parse everything due to variable-width fields
    u32 bit_index = 4*8;

    ParseExpGolomb(sps, &bit_index); // seq_par_set_id
    ParseExpGolomb(sps, &bit_index); // log2_max_frame_num_minus4
    u32 pic_order_cnt_type = ParseExpGolomb(sps, &bit_index);
    if (pic_order_cnt_type == 0) {
        ParseExpGolomb(sps, &bit_index); // log2_max_pic_order_cnt_lsb_minus4 
    } else if (pic_order_cnt_type == 1) {
        bit_index++; // delta_pic_order_always_zero
        ParseExpGolomb(sps, &bit_index); // offset_for_non_ref_pic 
        ParseExpGolomb(sps, &bit_index); // offset_for_top_to_bottom_field
        u32 num_ref_frames_in_pic_order_cnt_cycle = ParseExpGolomb(sps, &bit_index); 
        for (u32 i=0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
            ParseExpGolomb(sps, &bit_index); // offset_for_ref_frame[i]
        }
    }
    ParseExpGolomb(sps, &bit_index); // num_ref_frames
    bit_index++; // gaps_in_frame_num_value_allowed_flag

    res.pic_width_in_mbs_minus1 = ParseExpGolomb(sps, &bit_index);
    res.pic_height_in_map_units_minus1 = ParseExpGolomb(sps, &bit_index);
    bool frame_mbs_only_flag = GetBit(sps, bit_index++);
    if (!frame_mbs_only_flag) {
        bit_index++; // mb_adaptive_frame_field_flag
    }
    bit_index++; // direct_8x8_inference_flag
    bool frame_cropping_flag = GetBit(sps, bit_index++);
    if (frame_cropping_flag) {
        res.frame_crop_left_offset = ParseExpGolomb(sps, &bit_index);
        res.frame_crop_right_offset = ParseExpGolomb(sps, &bit_index);
        res.frame_crop_top_offset = ParseExpGolomb(sps, &bit_index);
        res.frame_crop_bottom_offset = ParseExpGolomb(sps, &bit_index);
    }

    bool vui_parameters_present_flag = GetBit(sps, bit_index++);
    if (vui_parameters_present_flag) {
        bool aspect_ratio_info_present_flag = GetBit(sps, bit_index++);
        if (aspect_ratio_info_present_flag) {
            u8 aspect_ratio_idc = GetUnaligned(sps, &bit_index, 8);
            if (aspect_ratio_idc == 255) { // Exnteded_SAR
                GetUnaligned(sps, &bit_index, 16); // sar_width
                GetUnaligned(sps, &bit_index, 16); // sar_height
            }
        }

        bool overscan_info_present_flag = GetBit(sps, bit_index++);
        if (overscan_info_present_flag) {
            bit_index++; // overscan_appropriate_flag
        }

        bool video_signal_type_prsent_flag = GetBit(sps, bit_index++);
        if (video_signal_type_prsent_flag) {
            GetUnaligned(sps, &bit_index, 3); // video_format
            bit_index++; // video_full_range_flag
            bool color_description_present_flag = GetBit(sps, bit_index++);
            if (color_description_present_flag) {
                GetUnaligned(sps, &bit_index, 8); // colour_primaries
                GetUnaligned(sps, &bit_index, 8); // transfer_characteristics
                GetUnaligned(sps, &bit_index, 8); // matrix_coefficients
            }
        }

        bool chroma_loc_info_present_flag = GetBit(sps, bit_index++);
        if (chroma_loc_info_present_flag) {
            ParseExpGolomb(sps, &bit_index); // chroma_sample_loc_type_top_field
            ParseExpGolomb(sps, &bit_index); // chroma_sample_loc_type_bottom_field
        }

        bool timing_info_present = GetBit(sps, bit_index++);
        if (timing_info_present) {
            res.timing_info.num_units_in_tick = GetUnaligned(sps, &bit_index, 32);
            res.timing_info.time_scale = GetUnaligned(sps, &bit_index, 32);
            res.timing_info.fixed_frame_rate_flag = GetBit(sps, bit_index++);
        }

        // ignore the rest...
    }

    lt_free(sps);
    return res;
};
