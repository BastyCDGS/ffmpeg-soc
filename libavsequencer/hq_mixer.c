/*
 * Sequencer high quality integer mixer
 * Copyright (c) 2011 Sebastian Vater <cdgs.basty@googlemail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Sequencer high quality integer mixer.
 */

#include "libavcodec/avcodec.h"
#include "libavutil/avstring.h"
#include "libavsequencer/mixer.h"

typedef struct AV_HQMixerData {
    AVMixerData mixer_data;
    int32_t *buf;
    int32_t *filter_buf;
    uint32_t buf_size;
    uint32_t mix_buf_size;
    int32_t *volume_lut;
    struct AV_HQMixerChannelInfo *channel_info;
    uint32_t amplify;
    uint32_t mix_rate;
    uint32_t mix_rate_frac;
    uint32_t current_left;
    uint32_t current_left_frac;
    uint32_t pass_len;
    uint32_t pass_len_frac;
    uint16_t channels_in;
    uint16_t channels_out;
    uint8_t interpolation;
    uint8_t real_16_bit_mode;
} AV_HQMixerData;

typedef struct AV_HQMixerChannelInfo {
    struct ChannelBlock {
        const int16_t *data;
        uint32_t len;
        uint32_t offset;
        uint32_t fraction;
        uint32_t advance;
        uint32_t advance_frac;
        void (*mix_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len);
        uint32_t end_offset;
        uint32_t restart_offset;
        uint32_t repeat;
        uint32_t repeat_len;
        uint32_t count_restart;
        uint32_t counted;
        uint32_t rate;
        int32_t *volume_left_lut;
        int32_t *volume_right_lut;
        uint32_t mult_left_volume;
        uint32_t div_volume;
        uint32_t mult_right_volume;
        int32_t filter_c1;
        int32_t filter_c2;
        int32_t filter_c3;
        void (*mix_backwards_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len);
        uint8_t bits_per_sample;
        uint8_t flags;
        uint8_t volume;
        uint8_t panning;
        uint8_t filter_cutoff;
        uint8_t filter_damping;
    } current;
    struct ChannelBlock next;
    int32_t filter_tmp1;
    int32_t filter_tmp2;
    int32_t prev_sample;
    int32_t curr_sample;
    int32_t next_sample;
    int32_t prev_sample_r;
    int32_t curr_sample_r;
    int32_t next_sample_r;
    int mix_right;
} AV_HQMixerChannelInfo;

#if CONFIG_HIGH_QUALITY_MIXER
static const char *high_quality_mixer_name(void *p)
{
    AVMixerContext *mixctx = p;

    return mixctx->name;
}

static const AVClass avseq_high_quality_mixer_class = {
    "AVSequencer High Quality Mixer",
    high_quality_mixer_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

static void apply_filter(AV_HQMixerChannelInfo *channel_info, struct ChannelBlock *const channel_block, int32_t **const dest_buf, const int32_t *src_buf, const uint32_t len)
{
    int32_t *mix_buf = *dest_buf;
    uint32_t i = len >> 2;
    int32_t c1 = channel_block->filter_c1;
    int32_t c2 = channel_block->filter_c2;
    int32_t c3 = channel_block->filter_c3;
    int32_t o1 = channel_info->filter_tmp2;
    int32_t o2 = channel_info->filter_tmp1;
    int32_t o3, o4;

    while (i--) {
        mix_buf[0] += o3 = (((int64_t) c1 * src_buf[0]) + ((int64_t) c2 * o2) + ((int64_t) c3 * o1)) >> 24;
        mix_buf[1] += o4 = (((int64_t) c1 * src_buf[1]) + ((int64_t) c2 * o3) + ((int64_t) c3 * o2)) >> 24;
        mix_buf[2] += o1 = (((int64_t) c1 * src_buf[2]) + ((int64_t) c2 * o4) + ((int64_t) c3 * o3)) >> 24;
        mix_buf[3] += o2 = (((int64_t) c1 * src_buf[3]) + ((int64_t) c2 * o1) + ((int64_t) c3 * o4)) >> 24;

        src_buf += 4;
        mix_buf += 4;
    }

    i = len & 3;

    while (i--) {
        *mix_buf++ += o3 = (((int64_t) c1 * *src_buf++) + ((int64_t) c2 * o2) + ((int64_t) c3 * o1)) >> 24;

        o1 = o2;
        o2 = o3;
    }

    *dest_buf                 = mix_buf;
    channel_info->filter_tmp1 = o2;
    channel_info->filter_tmp2 = o1;
}

static void mix_sample(AV_HQMixerData *const mixer_data, int32_t *const buf, const uint32_t len)
{
    AV_HQMixerChannelInfo *channel_info = mixer_data->channel_info;
    uint16_t i                          = mixer_data->channels_in;

    do {
        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            void (*mix_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len);
            int32_t *mix_buf        = buf;
            uint32_t offset         = channel_info->current.offset;
            uint32_t fraction       = channel_info->current.fraction;
            const uint32_t advance  = channel_info->current.advance;
            const uint32_t adv_frac = channel_info->current.advance_frac;
            uint32_t remain_len     = len, remain_mix;
            uint64_t calc_mix;

            mix_func = channel_info->current.mix_func;

            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
mix_sample_backwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = offset - channel_info->current.end_offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            if ((channel_info->current.filter_cutoff == 127) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, channel_info, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, remain_len);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = remain_len;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, channel_info, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, remain_len);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if ((int32_t) offset <= (int32_t) channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            if ((channel_info->current.filter_cutoff == 127) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, channel_info, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = (uint32_t) calc_mix;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, channel_info, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if (((int32_t) offset > (int32_t) channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        const uint32_t count_restart = channel_info->current.count_restart;
                        const uint32_t counted       = channel_info->current.counted++;

                        if (count_restart && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = -1;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                void (*mixer_change_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len);

                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

                                mixer_change_func                        = channel_info->current.mix_backwards_func;
                                channel_info->current.mix_backwards_func = mix_func;
                                mix_func                                 = mixer_change_func;
                                channel_info->current.mix_func           = mix_func;
                                channel_info->current.flags             ^= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
                                remain_mix                               = channel_info->current.end_offset;
                                offset                                  -= remain_mix;
                                offset                                   = -offset + remain_mix;
                                remain_mix                              += channel_info->current.restart_offset;
                                channel_info->current.end_offset         = remain_mix;

                                if ((int32_t) remain_len > 0)
                                    goto mix_sample_forwards;

                                break;
                            } else {
                                offset += channel_info->current.restart_offset;

                                if (channel_info->next.data)
                                    goto mix_sample_synth;

                                if ((int32_t) remain_len > 0)
                                    continue;

                                break;
                            }
                        }
                    } else {
                        if (channel_info->next.data)
                            goto mix_sample_synth;
                        else
                            channel_info->current.flags &= ~AVSEQ_MIXER_CHANNEL_FLAG_PLAY;

                        break;
                    }
                }
            } else {
mix_sample_forwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = channel_info->current.end_offset - offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            if ((channel_info->current.filter_cutoff == 127) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, channel_info, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, remain_len);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = remain_len;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, channel_info, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, remain_len);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if (offset >= channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            if ((channel_info->current.filter_cutoff == 127) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, channel_info, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = (uint32_t) calc_mix;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, channel_info, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if ((offset < channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        const uint32_t count_restart = channel_info->current.count_restart;
                        const uint32_t counted       = channel_info->current.counted++;

                        if (count_restart && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = channel_info->current.len;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                void (*mixer_change_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len);

                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

                                mixer_change_func                        = channel_info->current.mix_backwards_func;
                                channel_info->current.mix_backwards_func = mix_func;
                                mix_func                                 = mixer_change_func;
                                channel_info->current.mix_func           = mix_func;
                                channel_info->current.flags             ^= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
                                remain_mix                               = channel_info->current.end_offset;
                                offset                                  -= remain_mix;
                                offset                                   = -offset + remain_mix;
                                remain_mix                              -= channel_info->current.restart_offset;
                                channel_info->current.end_offset         = remain_mix;

                                if (remain_len)
                                    goto mix_sample_backwards;

                                break;
                            } else {
                                offset -= channel_info->current.restart_offset;

                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

                                if ((int32_t) remain_len > 0)
                                    continue;

                                break;
                            }
                        }
                    } else {
                        if (channel_info->next.data) {
mix_sample_synth:
                            memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                            channel_info->next.data = NULL;

                            if ((int32_t) remain_len > 0)
                                continue;
                        } else {
                            channel_info->current.flags &= ~AVSEQ_MIXER_CHANNEL_FLAG_PLAY;
                        }

                        break;
                    }
                }
            }

            channel_info->current.offset   = offset;
            channel_info->current.fraction = fraction;
        }

        channel_info++;
    } while (--i);
}

static void mix_sample_parallel(AV_HQMixerData *const mixer_data, int32_t *const buf, const uint32_t len, const uint32_t first_channel, const uint32_t last_channel)
{
    AV_HQMixerChannelInfo *channel_info = mixer_data->channel_info + first_channel;
    uint16_t i                          = (last_channel - first_channel) + 1;

    do {
        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            void (*mix_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len);
            int32_t *mix_buf        = buf;
            uint32_t offset         = channel_info->current.offset;
            uint32_t fraction       = channel_info->current.fraction;
            const uint32_t advance  = channel_info->current.advance;
            const uint32_t adv_frac = channel_info->current.advance_frac;
            uint32_t remain_len     = len, remain_mix;
            uint64_t calc_mix;

            mix_func = channel_info->current.mix_func;

            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
mix_sample_backwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = offset - channel_info->current.end_offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            if ((channel_info->current.filter_cutoff == 127) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, channel_info, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, remain_len);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = remain_len;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, channel_info, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, remain_len);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if ((int32_t) offset <= (int32_t) channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            if ((channel_info->current.filter_cutoff == 127) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, channel_info, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = (uint32_t) calc_mix;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, channel_info, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if (((int32_t) offset > (int32_t) channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        const uint32_t count_restart = channel_info->current.count_restart;
                        const uint32_t counted       = channel_info->current.counted++;

                        if (count_restart && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = -1;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                void (*mixer_change_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len);

                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

                                mixer_change_func                        = channel_info->current.mix_backwards_func;
                                channel_info->current.mix_backwards_func = mix_func;
                                mix_func                                 = mixer_change_func;
                                channel_info->current.mix_func           = mix_func;
                                channel_info->current.flags             ^= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
                                remain_mix                               = channel_info->current.end_offset;
                                offset                                  -= remain_mix;
                                offset                                   = -offset + remain_mix;
                                remain_mix                              += channel_info->current.restart_offset;
                                channel_info->current.end_offset         = remain_mix;

                                if ((int32_t) remain_len > 0)
                                    goto mix_sample_forwards;

                                break;
                            } else {
                                offset += channel_info->current.restart_offset;

                                if (channel_info->next.data)
                                    goto mix_sample_synth;

                                if ((int32_t) remain_len > 0)
                                    continue;

                                break;
                            }
                        }
                    } else {
                        if (channel_info->next.data)
                            goto mix_sample_synth;
                        else
                            channel_info->current.flags &= ~AVSEQ_MIXER_CHANNEL_FLAG_PLAY;

                        break;
                    }
                }
            } else {
mix_sample_forwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = channel_info->current.end_offset - offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            if ((channel_info->current.filter_cutoff == 127) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, channel_info, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, remain_len);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = remain_len;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, channel_info, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, remain_len);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if (offset >= channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            if ((channel_info->current.filter_cutoff == 127) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, channel_info, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = (uint32_t) calc_mix;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, channel_info, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if ((offset < channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        const uint32_t count_restart = channel_info->current.count_restart;
                        const uint32_t counted       = channel_info->current.counted++;

                        if (count_restart && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = channel_info->current.len;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                void (*mixer_change_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len);

                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

                                mixer_change_func                        = channel_info->current.mix_backwards_func;
                                channel_info->current.mix_backwards_func = mix_func;
                                mix_func                                 = mixer_change_func;
                                channel_info->current.mix_func           = mix_func;
                                channel_info->current.flags             ^= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
                                remain_mix                               = channel_info->current.end_offset;
                                offset                                  -= remain_mix;
                                offset                                   = -offset + remain_mix;
                                remain_mix                              -= channel_info->current.restart_offset;
                                channel_info->current.end_offset         = remain_mix;

                                if (remain_len)
                                    goto mix_sample_backwards;

                                break;
                            } else {
                                offset -= channel_info->current.restart_offset;

                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

                                if ((int32_t) remain_len > 0)
                                    continue;

                                break;
                            }
                        }
                    } else {
                        if (channel_info->next.data) {
mix_sample_synth:
                            memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                            channel_info->next.data = NULL;

                            if ((int32_t) remain_len > 0)
                                continue;
                        } else {
                            channel_info->current.flags &= ~AVSEQ_MIXER_CHANNEL_FLAG_PLAY;
                        }

                        break;
                    }
                }
            }

            channel_info->current.offset   = offset;
            channel_info->current.fraction = fraction;
        }

        channel_info++;
    } while (--i);
}

#define MIX(type)                                                                                   \
    static void mix_##type(const AV_HQMixerData *const mixer_data,                                  \
                           struct AV_HQMixerChannelInfo *const channel_info,                        \
                           struct ChannelBlock *const channel_block,                                \
                           int32_t **const buf, uint32_t *const offset, uint32_t *const fraction,   \
                           const uint32_t advance, const uint32_t adv_frac, const uint32_t len)

MIX(skip)
{
    uint32_t curr_offset    = *offset, curr_frac = *fraction, skip_div;
    const uint64_t skip_len = (((uint64_t) advance << 32) + adv_frac) * len;

    skip_div     = skip_len >> 32;
    curr_offset += skip_div;
    skip_div     = skip_len;
    curr_frac   += skip_div;

    if (curr_frac < skip_div)
        curr_offset++;

    *offset   = curr_offset;
    *fraction = curr_frac;
}

MIX(skip_backwards)
{
    uint32_t curr_offset    = *offset, curr_frac = *fraction, skip_div;
    const uint64_t skip_len = (((uint64_t) advance << 32) + adv_frac) * len;

    skip_div     = skip_len >> 32;
    curr_offset -= skip_div;
    skip_div     = skip_len;
    curr_frac   += skip_div;

    if (curr_frac < skip_div)
        curr_offset--;

    *offset   = curr_offset;
    *fraction = curr_frac;
}

static void get_next_sample_16_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_next_sample_32_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_next_sample_x_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_next_sample_16(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_next_sample_32(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_next_sample_x(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);

static void get_next_sample_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int8_t *sample      = (const int8_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    int32_t smp;

    if (offset >= (channel_block->end_offset - 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                            if (channel_next_block->bits_per_sample == 16)
                                get_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else if (channel_next_block->bits_per_sample == 32)
                                get_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else
                                get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 16) {
                            get_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 32) {
                            get_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else {
                            get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        }

                        return;
                    }

                    sample     = (const int8_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset -= channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                            if (channel_next_block->bits_per_sample == 16)
                                get_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else if (channel_next_block->bits_per_sample == 32)
                                get_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else
                                get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 16) {
                            get_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 32) {
                            get_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else {
                            get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        }

                        return;
                    }

                    sample     = (const int8_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                        if (channel_next_block->bits_per_sample == 16)
                            get_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else if (channel_next_block->bits_per_sample == 16) {
                        get_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else if (channel_next_block->bits_per_sample == 32) {
                        get_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else {
                        get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    return;
                }

                sample     = (const int8_t *) channel_next_block->data;
                volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset     = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = volume_lut[(uint8_t) sample[offset + 1]];

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_backwards_next_sample_16_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_backwards_next_sample_32_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_backwards_next_sample_x_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_backwards_next_sample_16(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_backwards_next_sample_32(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);
static void get_backwards_next_sample_x(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset);

static void get_backwards_next_sample_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int8_t *sample      = (const int8_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    int32_t smp;

    if ((int32_t) offset <= ((int32_t) channel_block->end_offset + 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                            if (channel_next_block->bits_per_sample == 16)
                                get_backwards_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else if (channel_next_block->bits_per_sample == 32)
                                get_backwards_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else
                                get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 16) {
                            get_backwards_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 32) {
                            get_backwards_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else {
                            get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        }

                        return;
                    }

                    sample     = (const int8_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset += channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                            if (channel_next_block->bits_per_sample == 16)
                                get_backwards_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else if (channel_next_block->bits_per_sample == 32)
                                get_backwards_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else
                                get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 16) {
                            get_backwards_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 32) {
                            get_backwards_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else {
                            get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        }

                        return;
                    }

                    sample     = (const int8_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                        if (channel_next_block->bits_per_sample == 16)
                            get_backwards_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_backwards_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else if (channel_next_block->bits_per_sample == 16) {
                        get_backwards_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else if (channel_next_block->bits_per_sample == 32) {
                        get_backwards_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else {
                        get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    return;
                }

                sample     = (const int8_t *) channel_next_block->data;
                volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset     = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }

            return;
        }
    }

    smp = volume_lut[(uint8_t) sample[offset - 1]];

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_next_sample_16_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int16_t *sample     = (const int16_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    int32_t smp;

    if (offset >= (channel_block->end_offset - 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample     = (const int16_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset -= channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample     = (const int16_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 32)
                        get_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                    return;
                }

                sample     = (const int16_t *) channel_next_block->data;
                volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset     = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = volume_lut[(uint16_t) sample[offset + 1] >> 8];

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_backwards_next_sample_16_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int16_t *sample     = (const int16_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    int32_t smp;

    if ((int32_t) offset <= ((int32_t) channel_block->end_offset + 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_backwards_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample     = (const int16_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset += channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_backwards_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample     = (const int16_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 32)
                        get_backwards_next_sample_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                    return;
                }

                sample     = (const int16_t *) channel_next_block->data;
                volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset     = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = volume_lut[(uint16_t) sample[offset - 1] >> 8];

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_next_sample_32_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int32_t *sample     = (const int32_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    int32_t smp;

    if (offset >= (channel_block->end_offset - 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            get_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample     = (const int32_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset -= channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            get_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample     = (const int32_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 16)
                        get_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        get_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                    return;
                }

                sample     = (const int32_t *) channel_next_block->data;
                volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset     = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = volume_lut[(uint32_t) sample[offset + 1] >> 24];

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_backwards_next_sample_32_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int32_t *sample     = (const int32_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    int32_t smp;

    if ((int32_t) offset <= ((int32_t) channel_block->end_offset + 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            get_backwards_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample     = (const int32_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset += channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            get_backwards_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample     = (const int32_t *) channel_next_block->data;
                    volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset     = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 16)
                        get_backwards_next_sample_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        get_backwards_next_sample_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                    return;
                }

                sample     = (const int32_t *) channel_next_block->data;
                volume_lut = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset     = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = volume_lut[(uint32_t) sample[offset - 1] >> 24];

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_next_sample_x_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int32_t *sample     = (const int32_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t bits_per_sample  = channel_block->bits_per_sample;
    int32_t smp;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    if (offset >= (channel_block->end_offset - 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset -= channel_block->restart_offset;

                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                sample          = (const int32_t *) channel_next_block->data;
                volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                bits_per_sample = channel_next_block->bits_per_sample;
                offset          = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    bit        = ++offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[offset] << bit;
        smp_data |= ((uint32_t) sample[offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    smp = volume_lut[(uint32_t) smp_data >> 24];

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_backwards_next_sample_x_to_8(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int32_t *sample     = (const int32_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t bits_per_sample  = channel_block->bits_per_sample;
    int32_t smp;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    if ((int32_t) offset <= ((int32_t) channel_block->end_offset + 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset += channel_block->restart_offset;

                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                sample          = (const int32_t *) channel_next_block->data;
                volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                bits_per_sample = channel_next_block->bits_per_sample;
                offset          = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    bit        = --offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[smp_offset] << bit;
        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    smp = volume_lut[(uint32_t) smp_data >> 24];

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_next_sample_16(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int16_t *sample = (const int16_t *) channel_block->data;
    int32_t mult_volume   = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume    = channel_block->div_volume;
    int32_t smp;

    if (offset >= (channel_block->end_offset - 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample      = (const int16_t *) channel_next_block->data;
                    mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume  = channel_next_block->div_volume;
                    offset      = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset -= channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample      = (const int16_t *) channel_next_block->data;
                    mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume  = channel_next_block->div_volume;
                    offset      = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 32)
                        get_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                    return;
                }

                sample      = (const int16_t *) channel_next_block->data;
                mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                div_volume  = channel_next_block->div_volume;
                offset      = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = ((int64_t) sample[offset + 1] * mult_volume) / div_volume;

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_backwards_next_sample_16(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int16_t *sample = (const int16_t *) channel_block->data;
    int32_t mult_volume   = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume    = channel_block->div_volume;
    int32_t smp;

    if ((int32_t) offset <= ((int32_t) channel_block->end_offset + 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_backwards_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample      = (const int16_t *) channel_next_block->data;
                    mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume  = channel_next_block->div_volume;
                    offset      = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset += channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            get_backwards_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample      = (const int16_t *) channel_next_block->data;
                    mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume  = channel_next_block->div_volume;
                    offset      = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 32)
                        get_backwards_next_sample_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                    return;
                }

                sample      = (const int16_t *) channel_next_block->data;
                mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                div_volume  = channel_next_block->div_volume;
                offset      = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = ((int64_t) sample[offset - 1] * mult_volume) / div_volume;

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_next_sample_32(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int32_t *sample = (const int32_t *) channel_block->data;
    int32_t mult_volume   = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume    = channel_block->div_volume;
    int32_t smp;

    if (offset >= (channel_block->end_offset - 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            get_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample      = (const int32_t *) channel_next_block->data;
                    mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume  = channel_next_block->div_volume;
                    offset      = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset -= channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            get_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample      = (const int32_t *) channel_next_block->data;
                    mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume  = channel_next_block->div_volume;
                    offset      = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        get_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 16)
                        get_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        get_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                    return;
                }

                sample      = (const int32_t *) channel_next_block->data;
                mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                div_volume  = channel_next_block->div_volume;
                offset      = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = ((int64_t) sample[offset + 1] * mult_volume) / div_volume;

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_backwards_next_sample_32(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int32_t *sample = (const int32_t *) channel_block->data;
    int32_t mult_volume   = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume    = channel_block->div_volume;
    int32_t smp;

    if ((int32_t) offset <= ((int32_t) channel_block->end_offset + 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            get_backwards_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample      = (const int32_t *) channel_next_block->data;
                    mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume  = channel_next_block->div_volume;
                    offset      = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset += channel_block->restart_offset;

                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            get_backwards_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                        return;
                    }

                    sample      = (const int32_t *) channel_next_block->data;
                    mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume  = channel_next_block->div_volume;
                    offset      = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        get_backwards_next_sample_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 16)
                        get_backwards_next_sample_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        get_backwards_next_sample_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);

                    return;
                }

                sample      = (const int32_t *) channel_next_block->data;
                mult_volume = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                div_volume  = channel_next_block->div_volume;
                offset      = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    smp = ((int64_t) sample[offset - 1] * mult_volume) / div_volume;

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_next_sample_x(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int32_t *sample    = (const int32_t *) channel_block->data;
    int32_t mult_volume      = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume       = channel_block->div_volume;
    uint32_t bits_per_sample = channel_block->bits_per_sample;
    int32_t smp;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    if (offset >= (channel_block->end_offset - 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    mult_volume     = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume      = channel_next_block->div_volume;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset -= channel_block->restart_offset;

                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    mult_volume     = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume      = channel_next_block->div_volume;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                sample          = (const int32_t *) channel_next_block->data;
                mult_volume     = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                div_volume      = channel_next_block->div_volume;
                bits_per_sample = channel_next_block->bits_per_sample;
                offset          = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    bit        = ++offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[offset] << bit;
        smp_data |= ((uint32_t) sample[offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    smp = ((int64_t) smp_data * mult_volume) / div_volume;

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static void get_backwards_next_sample_x(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset)
{
    const int32_t *sample    = (const int32_t *) channel_block->data;
    int32_t mult_volume      = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume       = channel_block->div_volume;
    uint32_t bits_per_sample = channel_block->bits_per_sample;
    int32_t smp;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    if ((int32_t) offset <= ((int32_t) channel_block->end_offset + 1)) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            struct ChannelBlock *const channel_next_block = &channel_info->next;
            const uint32_t count_restart                  = channel_info->current.count_restart;
            const uint32_t counted                        = channel_info->current.counted + 1;

            if (count_restart && (count_restart == counted)) {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    mult_volume     = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume      = channel_next_block->div_volume;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset;
                } else {
                    if (channel_info->mix_right)
                        channel_info->next_sample_r = 0;
                    else
                        channel_info->next_sample = 0;

                    return;
                }
            } else {
                offset += channel_block->restart_offset;

                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    mult_volume     = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                    div_volume      = channel_next_block->div_volume;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset;
                }
            }
        } else {
            struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                sample          = (const int32_t *) channel_next_block->data;
                mult_volume     = channel_info->mix_right ? channel_next_block->mult_right_volume : channel_next_block->mult_left_volume;
                div_volume      = channel_next_block->div_volume;
                bits_per_sample = channel_next_block->bits_per_sample;
                offset          = channel_next_block->offset;
            } else {
                if (channel_info->mix_right)
                    channel_info->next_sample_r = 0;
                else
                    channel_info->next_sample = 0;

                return;
            }
        }
    }

    bit        = --offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[smp_offset] << bit;
        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    smp = ((int64_t) smp_data * mult_volume) / div_volume;

    if (channel_info->mix_right)
        channel_info->next_sample_r = smp;
    else
        channel_info->next_sample = smp;
}

static int32_t get_curr_sample_8(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset)
{
    const int8_t *const sample      = (const int8_t *const) channel_block->data;
    const int32_t *const volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;

    return volume_lut[(uint8_t) sample[offset]];
}

static int32_t get_curr_sample_16_to_8(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset)
{
    const int16_t *const sample     = (const int16_t *const) channel_block->data;
    const int32_t *const volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;

    return volume_lut[(uint16_t) sample[offset] >> 8];
}

static int32_t get_curr_sample_32_to_8(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset)
{
    const int32_t *const sample     = (const int32_t *const) channel_block->data;
    const int32_t *const volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;

    return volume_lut[(uint32_t) sample[offset] >> 24];
}

static int32_t get_curr_sample_x_to_8(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset)
{
    const int32_t *const sample     = (const int32_t *const) channel_block->data;
    const int32_t *const volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    const uint32_t bits_per_sample  = channel_block->bits_per_sample;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    bit        = offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[offset] << bit;
        smp_data |= ((uint32_t) sample[offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    return volume_lut[(uint32_t) smp_data >> 24];
}

static int32_t get_curr_sample_16(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset)
{
    const int16_t *const sample = (const int16_t *const) channel_block->data;
    const int32_t mult_volume   = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    const int32_t div_volume    = channel_block->div_volume;

    return ((int64_t) sample[offset] * mult_volume) / div_volume;
}

static int32_t get_curr_sample_32(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset)
{
    const int32_t *const sample = (const int32_t *const) channel_block->data;
    const int32_t mult_volume   = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    const int32_t div_volume    = channel_block->div_volume;

    return ((int64_t) sample[offset] * mult_volume) / div_volume;
}

static int32_t get_curr_sample_x(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset)
{
    const int32_t *const sample    = (const int32_t *const) channel_block->data;
    const int32_t mult_volume      = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    const int32_t div_volume       = channel_block->div_volume;
    const uint32_t bits_per_sample = channel_block->bits_per_sample;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    bit        = offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[offset] << bit;
        smp_data |= ((uint32_t) sample[offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    return ((int64_t) smp_data * mult_volume) / div_volume;
}

static int32_t get_sample_1_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_sample_1_16_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_sample_1_32_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_sample_1_x_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_sample_1_16(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_sample_1_32(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_sample_1_x(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);

static int32_t get_sample_1_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int8_t *sample      = (const int8_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t end_offset       = channel_block->end_offset;
    uint32_t restart_offset   = channel_block->restart_offset;
    uint32_t count_restart    = channel_block->count_restart;
    uint32_t counted          = channel_block->counted;

    while (offset >= end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                            if (channel_next_block->bits_per_sample == 16)
                                return get_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else if (channel_next_block->bits_per_sample == 32)
                                return get_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else
                                return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 16) {
                            return get_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 32) {
                            return get_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else {
                            return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        }
                    }

                    sample         = (const int8_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                            if (channel_next_block->bits_per_sample == 16)
                                return get_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else if (channel_next_block->bits_per_sample == 32)
                                return get_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else
                                return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 16) {
                            return get_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 32) {
                            return get_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else {
                            return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        }
                    }

                    sample         = (const int8_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset -= restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                        if (channel_next_block->bits_per_sample == 16)
                            return get_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else if (channel_next_block->bits_per_sample == 16) {
                        return get_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else if (channel_next_block->bits_per_sample == 32) {
                        return get_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else {
                        return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }
                }

                sample         = (const int8_t *) channel_next_block->data;
                volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return volume_lut[(uint8_t) sample[offset]];
}

static int32_t get_backwards_sample_1_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_backwards_sample_1_16_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_backwards_sample_1_32_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_backwards_sample_1_x_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_backwards_sample_1_16(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_backwards_sample_1_32(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);
static int32_t get_backwards_sample_1_x(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset);

static int32_t get_backwards_sample_1_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int8_t *sample      = (const int8_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t end_offset       = channel_block->end_offset;
    uint32_t restart_offset   = channel_block->restart_offset;
    uint32_t count_restart    = channel_block->count_restart;
    uint32_t counted          = channel_block->counted;

    while (offset < end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                            if (channel_next_block->bits_per_sample == 16)
                                return get_backwards_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else if (channel_next_block->bits_per_sample == 32)
                                return get_backwards_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else
                                return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 16) {
                            return get_backwards_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 32) {
                            return get_backwards_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else {
                            return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        }
                    }

                    sample         = (const int8_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                            if (channel_next_block->bits_per_sample == 16)
                                return get_backwards_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else if (channel_next_block->bits_per_sample == 32)
                                return get_backwards_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                            else
                                return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 16) {
                            return get_backwards_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else if (channel_next_block->bits_per_sample == 32) {
                            return get_backwards_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        } else {
                            return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        }
                    }

                    sample         = (const int8_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset += restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if ((channel_next_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
                        if (channel_next_block->bits_per_sample == 16)
                            return get_backwards_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_backwards_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else if (channel_next_block->bits_per_sample == 16) {
                        return get_backwards_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else if (channel_next_block->bits_per_sample == 32) {
                        return get_backwards_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    } else {
                        return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }
                }

                sample         = (const int8_t *) channel_next_block->data;
                volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return volume_lut[(uint8_t) sample[offset]];
}

static int32_t get_sample_1_16_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int16_t *sample     = (const int16_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t end_offset       = channel_block->end_offset;
    uint32_t restart_offset   = channel_block->restart_offset;
    uint32_t count_restart    = channel_block->count_restart;
    uint32_t counted          = channel_block->counted;

    while (offset >= end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int16_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int16_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset -= restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 32)
                        return get_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                }

                sample         = (const int16_t *) channel_next_block->data;
                volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return volume_lut[(uint16_t) sample[offset] >> 8];
}

static int32_t get_backwards_sample_1_16_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int16_t *sample     = (const int16_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t end_offset       = channel_block->end_offset;
    uint32_t restart_offset   = channel_block->restart_offset;
    uint32_t count_restart    = channel_block->count_restart;
    uint32_t counted          = channel_block->counted;

    while (offset < end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_backwards_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int16_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_backwards_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int16_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset += restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 32)
                        return get_backwards_sample_1_32_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                }

                sample         = (const int16_t *) channel_next_block->data;
                volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return volume_lut[(uint16_t) sample[offset] >> 8];
}

static int32_t get_sample_1_32_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int32_t *sample     = (const int32_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t end_offset       = channel_block->end_offset;
    uint32_t restart_offset   = channel_block->restart_offset;
    uint32_t count_restart    = channel_block->count_restart;
    uint32_t counted          = channel_block->counted;

    while (offset >= end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            return get_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int32_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            return get_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int32_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset -= restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 16)
                        return get_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        return get_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                }

                sample         = (const int32_t *) channel_next_block->data;
                volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return volume_lut[(uint32_t) sample[offset] >> 24];
}

static int32_t get_backwards_sample_1_32_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int32_t *sample     = (const int32_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t end_offset       = channel_block->end_offset;
    uint32_t restart_offset   = channel_block->restart_offset;
    uint32_t count_restart    = channel_block->count_restart;
    uint32_t counted          = channel_block->counted;

    while (offset < end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            return get_backwards_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int32_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            return get_backwards_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int32_t *) channel_next_block->data;
                    volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset += restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 16)
                        return get_backwards_sample_1_16_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        return get_backwards_sample_1_x_to_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                }

                sample         = (const int32_t *) channel_next_block->data;
                volume_lut     = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return volume_lut[(uint32_t) sample[offset] >> 24];
}

static int32_t get_sample_1_x_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int32_t *sample     = (const int32_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t bits_per_sample  = channel_block->bits_per_sample;
    uint32_t end_offset       = channel_block->end_offset;
    uint32_t restart_offset   = channel_block->restart_offset;
    uint32_t count_restart    = channel_block->count_restart;
    uint32_t counted          = channel_block->counted;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    while (offset >= end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset + (offset - end_offset);
                    end_offset      = channel_next_block->end_offset;
                    restart_offset  = channel_next_block->restart_offset;
                    count_restart   = channel_next_block->count_restart;
                    counted         = channel_next_block->counted;
                    channel_block   = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset + (offset - end_offset);
                    end_offset      = channel_next_block->end_offset;
                    restart_offset  = channel_next_block->restart_offset;
                    count_restart   = channel_next_block->count_restart;
                    counted         = channel_next_block->counted;
                    channel_block   = channel_next_block;
                } else {
                    offset -= restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                sample          = (const int32_t *) channel_next_block->data;
                volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                bits_per_sample = channel_next_block->bits_per_sample;
                offset          = channel_next_block->offset + (offset - end_offset);
                end_offset      = channel_next_block->end_offset;
                restart_offset  = channel_next_block->restart_offset;
                count_restart   = channel_next_block->count_restart;
                counted         = channel_next_block->counted;
                channel_block   = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    bit        = offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[offset] << bit;
        smp_data |= ((uint32_t) sample[offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    return volume_lut[(uint32_t) smp_data >> 24];
}

static int32_t get_backwards_sample_1_x_to_8(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int32_t *sample     = (const int32_t *) channel_block->data;
    const int32_t *volume_lut = channel_info->mix_right ? channel_block->volume_right_lut : channel_block->volume_left_lut;
    uint32_t bits_per_sample  = channel_block->bits_per_sample;
    uint32_t end_offset       = channel_block->end_offset;
    uint32_t restart_offset   = channel_block->restart_offset;
    uint32_t count_restart    = channel_block->count_restart;
    uint32_t counted          = channel_block->counted;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    while (offset < end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset + (offset - end_offset);
                    end_offset      = channel_next_block->end_offset;
                    restart_offset  = channel_next_block->restart_offset;
                    count_restart   = channel_next_block->count_restart;
                    counted         = channel_next_block->counted;
                    channel_block   = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset + (offset - end_offset);
                    end_offset      = channel_next_block->end_offset;
                    restart_offset  = channel_next_block->restart_offset;
                    count_restart   = channel_next_block->count_restart;
                    counted         = channel_next_block->counted;
                    channel_block   = channel_next_block;
                } else {
                    offset += restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                sample          = (const int32_t *) channel_next_block->data;
                volume_lut      = channel_info->mix_right ? channel_next_block->volume_right_lut : channel_next_block->volume_left_lut;
                bits_per_sample = channel_next_block->bits_per_sample;
                offset          = channel_next_block->offset + (offset - end_offset);
                end_offset      = channel_next_block->end_offset;
                restart_offset  = channel_next_block->restart_offset;
                count_restart   = channel_next_block->count_restart;
                counted         = channel_next_block->counted;
                channel_block   = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    bit        = offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[smp_offset] << bit;
        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    return volume_lut[(uint32_t) smp_data >> 24];
}

static int32_t get_sample_1_16(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int16_t *sample   = (const int16_t *) channel_block->data;
    int32_t mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume      = channel_block->div_volume;
    uint32_t end_offset     = channel_block->end_offset;
    uint32_t restart_offset = channel_block->restart_offset;
    uint32_t count_restart  = channel_block->count_restart;
    uint32_t counted        = channel_block->counted;

    while (offset >= end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int16_t *) channel_next_block->data;
                    mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume     = channel_block->div_volume;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int16_t *) channel_next_block->data;
                    mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume     = channel_block->div_volume;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset -= restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 32)
                        return get_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                }

                sample         = (const int16_t *) channel_next_block->data;
                mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                div_volume     = channel_block->div_volume;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return ((int64_t) sample[offset] * mult_volume) / div_volume;
}

static int32_t get_backwards_sample_1_16(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int16_t *sample   = (const int16_t *) channel_block->data;
    int32_t mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume      = channel_block->div_volume;
    uint32_t end_offset     = channel_block->end_offset;
    uint32_t restart_offset = channel_block->restart_offset;
    uint32_t count_restart  = channel_block->count_restart;
    uint32_t counted        = channel_block->counted;

    while (offset < end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_backwards_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int16_t *) channel_next_block->data;
                    mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume     = channel_block->div_volume;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 32)
                            return get_backwards_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int16_t *) channel_next_block->data;
                    mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume     = channel_block->div_volume;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset += restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 32)
                        return get_backwards_sample_1_32(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                }

                sample         = (const int16_t *) channel_next_block->data;
                mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                div_volume     = channel_block->div_volume;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return ((int64_t) sample[offset] * mult_volume) / div_volume;
}

static int32_t get_sample_1_32(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int32_t *sample   = (const int32_t *) channel_block->data;
    int32_t mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume      = channel_block->div_volume;
    uint32_t end_offset     = channel_block->end_offset;
    uint32_t restart_offset = channel_block->restart_offset;
    uint32_t count_restart  = channel_block->count_restart;
    uint32_t counted        = channel_block->counted;

    while (offset >= end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            return get_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int32_t *) channel_next_block->data;
                    mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume     = channel_block->div_volume;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            return get_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int32_t *) channel_next_block->data;
                    mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume     = channel_block->div_volume;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset -= restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        return get_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 16)
                        return get_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        return get_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                }

                sample         = (const int32_t *) channel_next_block->data;
                mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                div_volume     = channel_block->div_volume;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return ((int64_t) sample[offset] * mult_volume) / div_volume;
}

static int32_t get_backwards_sample_1_32(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int32_t *sample   = (const int32_t *) channel_block->data;
    int32_t mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume      = channel_block->div_volume;
    uint32_t end_offset     = channel_block->end_offset;
    uint32_t restart_offset = channel_block->restart_offset;
    uint32_t count_restart  = channel_block->count_restart;
    uint32_t counted        = channel_block->counted;

    while (offset < end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            return get_backwards_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int32_t *) channel_next_block->data;
                    mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume     = channel_block->div_volume;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                        if (channel_next_block->bits_per_sample == 8)
                            return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else if (channel_next_block->bits_per_sample == 16)
                            return get_backwards_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                        else
                            return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    }

                    sample         = (const int32_t *) channel_next_block->data;
                    mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume     = channel_block->div_volume;
                    offset         = channel_next_block->offset + (offset - end_offset);
                    end_offset     = channel_next_block->end_offset;
                    restart_offset = channel_next_block->restart_offset;
                    count_restart  = channel_next_block->count_restart;
                    counted        = channel_next_block->counted;
                    channel_block  = channel_next_block;
                } else {
                    offset += restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                if (channel_block->bits_per_sample != channel_next_block->bits_per_sample) {
                    if (channel_next_block->bits_per_sample == 8)
                        return get_backwards_sample_1_8(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else if (channel_next_block->bits_per_sample == 16)
                        return get_backwards_sample_1_16(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                    else
                        return get_backwards_sample_1_x(mixer_data, channel_info, channel_next_block, channel_next_block->offset);
                }

                sample         = (const int32_t *) channel_next_block->data;
                mult_volume    = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                div_volume     = channel_block->div_volume;
                offset         = channel_next_block->offset + (offset - end_offset);
                end_offset     = channel_next_block->end_offset;
                restart_offset = channel_next_block->restart_offset;
                count_restart  = channel_next_block->count_restart;
                counted        = channel_next_block->counted;
                channel_block  = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    return ((int64_t) sample[offset] * mult_volume) / div_volume;
}

static int32_t get_sample_1_x(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int32_t *sample    = (const int32_t *) channel_block->data;
    int32_t mult_volume      = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume       = channel_block->div_volume;
    uint32_t bits_per_sample = channel_block->bits_per_sample;
    uint32_t end_offset      = channel_block->end_offset;
    uint32_t restart_offset  = channel_block->restart_offset;
    uint32_t count_restart   = channel_block->count_restart;
    uint32_t counted         = channel_block->counted;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    while (offset >= end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume      = channel_block->div_volume;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset + (offset - end_offset);
                    end_offset      = channel_next_block->end_offset;
                    restart_offset  = channel_next_block->restart_offset;
                    count_restart   = channel_next_block->count_restart;
                    counted         = channel_next_block->counted;
                    channel_block   = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume      = channel_block->div_volume;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset + (offset - end_offset);
                    end_offset      = channel_next_block->end_offset;
                    restart_offset  = channel_next_block->restart_offset;
                    count_restart   = channel_next_block->count_restart;
                    counted         = channel_next_block->counted;
                    channel_block   = channel_next_block;
                } else {
                    offset -= restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                sample          = (const int32_t *) channel_next_block->data;
                mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                div_volume      = channel_block->div_volume;
                bits_per_sample = channel_next_block->bits_per_sample;
                offset          = channel_next_block->offset + (offset - end_offset);
                end_offset      = channel_next_block->end_offset;
                restart_offset  = channel_next_block->restart_offset;
                count_restart   = channel_next_block->count_restart;
                counted         = channel_next_block->counted;
                channel_block   = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    bit        = offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[offset] << bit;
        smp_data |= ((uint32_t) sample[offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    return ((int64_t) smp_data * mult_volume) / div_volume;
}

static int32_t get_backwards_sample_1_x(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset)
{
    const int32_t *sample    = (const int32_t *) channel_block->data;
    int32_t mult_volume      = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
    int32_t div_volume       = channel_block->div_volume;
    uint32_t bits_per_sample = channel_block->bits_per_sample;
    uint32_t end_offset      = channel_block->end_offset;
    uint32_t restart_offset  = channel_block->restart_offset;
    uint32_t count_restart   = channel_block->count_restart;
    uint32_t counted         = channel_block->counted;
    uint32_t bit;
    uint32_t smp_offset;
    uint32_t smp_data;

    while (offset < end_offset) {
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
            const struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (count_restart && (count_restart == ++counted)) {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume      = channel_block->div_volume;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset + (offset - end_offset);
                    end_offset      = channel_next_block->end_offset;
                    restart_offset  = channel_next_block->restart_offset;
                    count_restart   = channel_next_block->count_restart;
                    counted         = channel_next_block->counted;
                    channel_block   = channel_next_block;
                } else {
                    return 0;
                }
            } else {
                if (channel_next_block->data) {
                    sample          = (const int32_t *) channel_next_block->data;
                    mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                    div_volume      = channel_block->div_volume;
                    bits_per_sample = channel_next_block->bits_per_sample;
                    offset          = channel_next_block->offset + (offset - end_offset);
                    end_offset      = channel_next_block->end_offset;
                    restart_offset  = channel_next_block->restart_offset;
                    count_restart   = channel_next_block->count_restart;
                    counted         = channel_next_block->counted;
                    channel_block   = channel_next_block;
                } else {
                    offset += restart_offset;
                }
            }
        } else {
            const struct ChannelBlock *const channel_next_block = &channel_info->next;

            if (channel_next_block->data) {
                sample          = (const int32_t *) channel_next_block->data;
                mult_volume     = channel_info->mix_right ? channel_block->mult_right_volume : channel_block->mult_left_volume;
                div_volume      = channel_block->div_volume;
                bits_per_sample = channel_next_block->bits_per_sample;
                offset          = channel_next_block->offset + (offset - end_offset);
                end_offset      = channel_next_block->end_offset;
                restart_offset  = channel_next_block->restart_offset;
                count_restart   = channel_next_block->count_restart;
                counted         = channel_next_block->counted;
                channel_block   = channel_next_block;
            } else {
                return 0;
            }
        }
    }

    bit        = offset * bits_per_sample;
    smp_offset = bit >> 5;

    if (((bit &= 31) + bits_per_sample) < 32) {
        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
    } else {
        smp_data  = (uint32_t) sample[smp_offset] << bit;
        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
    }

    return ((int64_t) smp_data * mult_volume) / div_volume;
}

static void mix_mono_loop(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;
    int32_t smp;

    get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);

    do {
        int64_t interpolate_frac = -((int64_t) channel_info->prev_sample - (int64_t) channel_info->curr_sample);
        int64_t interpolate_div  = ((int64_t) channel_info->next_sample - ((int64_t) channel_info->curr_sample + interpolate_frac)) >> 2;
        int32_t smp_value;

        smp               = (uint32_t) curr_frac >> 1;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div   = ((interpolate_div << 2) + interpolate_frac) >> 2;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div <<= 3;
        smp_value         = ((int64_t) channel_info->prev_sample + (int64_t) channel_info->curr_sample) >> 1;
        smp               = (uint32_t) smp_value + (uint32_t) interpolate_div;

        if (((smp_value ^ smp) & (interpolate_div ^ smp)) < 0)
            smp = channel_info->curr_sample;

        *mix_buf++ += smp;
        curr_frac  += adv_frac;

        if (curr_frac < adv_frac) {
            curr_offset              += offset_inc;
            channel_info->prev_sample = channel_info->curr_sample;
            channel_info->curr_sample = channel_info->next_sample;

            get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        }
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

static void mix_average_mono(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, int32_t **const buf, int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset), int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;

    do {
        int32_t smp                = get_curr_sample_func(channel_info, channel_block, curr_offset);
        int64_t interpolate_div    = ((int64_t) ((uint32_t) ~curr_frac >> 1) * smp) >> 31;
        int64_t interpolate_frac   = (uint32_t) ~curr_frac;
        uint32_t interpolate_count = advance - 1;

        curr_offset += offset_inc;

        while (interpolate_count--) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        curr_frac += adv_frac;

        if (curr_frac < adv_frac) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        smp               = get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        interpolate_frac += curr_frac;
        interpolate_div  += (((int64_t) ((uint32_t) curr_frac >> 1) * smp) >> 31);
        *mix_buf++       += (interpolate_div << 24) / (interpolate_frac >> 8);
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

MIX(mono_8)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_8;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_backwards_8)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_8;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_16_to_8)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16_to_8;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_backwards_16_to_8)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16_to_8;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_32_to_8)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32_to_8;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_backwards_32_to_8)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32_to_8;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_x_to_8)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x_to_8;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_backwards_x_to_8)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x_to_8;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_16)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_backwards_16)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_32)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_backwards_32)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_x)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(mono_backwards_x)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x;

        mix_average_mono(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_mono_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

static void mix_left_loop(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;
    int32_t smp;

    get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);

    do {
        int64_t interpolate_frac = -((int64_t) channel_info->prev_sample - (int64_t) channel_info->curr_sample);
        int64_t interpolate_div  = ((int64_t) channel_info->next_sample - ((int64_t) channel_info->curr_sample + interpolate_frac)) >> 2;
        int32_t smp_value;

        smp               = (uint32_t) curr_frac >> 1;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div   = ((interpolate_div << 2) + interpolate_frac) >> 2;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div <<= 3;
        smp_value         = ((int64_t) channel_info->prev_sample + (int64_t) channel_info->curr_sample) >> 1;
        smp               = (uint32_t) smp_value + (uint32_t) interpolate_div;

        if (((smp_value ^ smp) & (interpolate_div ^ smp)) < 0)
            smp = channel_info->curr_sample;

        *mix_buf  += smp;
        mix_buf   += 2;
        curr_frac += adv_frac;

        if (curr_frac < adv_frac) {
            curr_offset              += offset_inc;
            channel_info->prev_sample = channel_info->curr_sample;
            channel_info->curr_sample = channel_info->next_sample;

            get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        }
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

static void mix_average_left(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, int32_t **const buf, int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset), int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;

    do {
        int32_t smp                = get_curr_sample_func(channel_info, channel_block, curr_offset);
        int64_t interpolate_div    = ((int64_t) ((uint32_t) ~curr_frac >> 1) * smp) >> 31;
        int64_t interpolate_frac   = (uint32_t) ~curr_frac;
        uint32_t interpolate_count = advance - 1;

        curr_offset += offset_inc;

        while (interpolate_count--) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        curr_frac += adv_frac;

        if (curr_frac < adv_frac) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        smp               = get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        interpolate_frac += curr_frac;
        interpolate_div  += (((int64_t) ((uint32_t) curr_frac >> 1) * smp) >> 31);
        *mix_buf         += (interpolate_div << 24) / (interpolate_frac >> 8);
        mix_buf          += 2;
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

MIX(stereo_8_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_8;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_8_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_8;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_16_to_8_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_16_to_8_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_32_to_8_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_32_to_8_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_x_to_8_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_x_to_8_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_16_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_16_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_32_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_32_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_x_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_x_left)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x;

        mix_average_left(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

static void mix_right_loop(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;
    int32_t smp;

    get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);

    do {
        int64_t interpolate_frac = -((int64_t) channel_info->prev_sample - (int64_t) channel_info->curr_sample);
        int64_t interpolate_div  = ((int64_t) channel_info->next_sample - ((int64_t) channel_info->curr_sample + interpolate_frac)) >> 2;
        int32_t smp_value;

        smp               = (uint32_t) curr_frac >> 1;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div   = ((interpolate_div << 2) + interpolate_frac) >> 2;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div <<= 3;
        smp_value         = ((int64_t) channel_info->prev_sample_r + (int64_t) channel_info->curr_sample_r) >> 1;
        smp               = (uint32_t) smp_value + (uint32_t) interpolate_div;

        if (((smp_value ^ smp) & (interpolate_div ^ smp)) < 0)
            smp = channel_info->curr_sample_r;

        mix_buf++;
        *mix_buf++ += smp;
        curr_frac  += adv_frac;

        if (curr_frac < adv_frac) {
            curr_offset                += offset_inc;
            channel_info->prev_sample_r = channel_info->curr_sample_r;
            channel_info->curr_sample_r = channel_info->next_sample_r;

            get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        }
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

static void mix_average_right(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, int32_t **const buf, int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset), int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;

    do {
        int32_t smp                = get_curr_sample_func(channel_info, channel_block, curr_offset);
        int64_t interpolate_div    = ((int64_t) ((uint32_t) ~curr_frac >> 1) * smp) >> 31;
        int64_t interpolate_frac   = (uint32_t) ~curr_frac;
        uint32_t interpolate_count = advance - 1;

        curr_offset += offset_inc;

        while (interpolate_count--) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        curr_frac += adv_frac;

        if (curr_frac < adv_frac) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        smp               = get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        interpolate_frac += curr_frac;
        interpolate_div  += (((int64_t) ((uint32_t) curr_frac >> 1) * smp) >> 31);
        mix_buf++;
        *mix_buf++       += (interpolate_div << 24) / (interpolate_frac >> 8);
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

MIX(stereo_8_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_8;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_8_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_8;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_16_to_8_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16_to_8;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_16_to_8_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16_to_8;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_32_to_8_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32_to_8;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_32_to_8_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32_to_8;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_x_to_8_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x_to_8;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_x_to_8_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x_to_8;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_16_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_16_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_32_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_32_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_x_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_x_right)
{
    channel_info->mix_right = 1;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x;

        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_8)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_8;
        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_8)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_8;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_16_to_8)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_16_to_8)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_32_to_8)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);
 
        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
   }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_32_to_8)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_x_to_8)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_x_to_8)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x_to_8;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_16)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_16)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_32)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_32)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_x)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, 1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

MIX(stereo_backwards_x)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;

    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x;

        mix_average_left(mixer_data, channel_info, channel_block, &mix_buf, get_curr_sample_func, get_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right = 1;
        mix_average_right(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_left_loop(mixer_data, channel_info, channel_block, &mix_buf, get_next_sample_func, -1, &curr_offset, &curr_frac, advance, adv_frac, len);

        channel_info->mix_right     = 1;
        channel_info->curr_sample_r = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_right_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }

    channel_info->mix_right = 0;
}

static void mix_center_loop(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;
    int32_t smp;

    get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);

    do {
        int64_t interpolate_frac = -((int64_t) channel_info->prev_sample - (int64_t) channel_info->curr_sample);
        int64_t interpolate_div  = ((int64_t) channel_info->next_sample - ((int64_t) channel_info->curr_sample + interpolate_frac)) >> 2;
        int32_t smp_value;

        smp               = (uint32_t) curr_frac >> 1;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div   = ((interpolate_div << 2) + interpolate_frac) >> 2;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div <<= 3;
        smp_value         = ((int64_t) channel_info->prev_sample + (int64_t) channel_info->curr_sample) >> 1;
        smp               = (uint32_t) smp_value + (uint32_t) interpolate_div;

        if (((smp_value ^ smp) & (interpolate_div ^ smp)) < 0)
            smp = channel_info->curr_sample;

        *mix_buf++ += smp;
        *mix_buf++ += smp;
        curr_frac  += adv_frac;

        if (curr_frac < adv_frac) {
            curr_offset              += offset_inc;
            channel_info->prev_sample = channel_info->curr_sample;
            channel_info->curr_sample = channel_info->next_sample;

            get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        }
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

static void mix_average_center(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, int32_t **const buf, int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset), int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;

    do {
        int32_t smp                = get_curr_sample_func(channel_info, channel_block, curr_offset);
        int64_t interpolate_div    = ((int64_t) ((uint32_t) ~curr_frac >> 1) * smp) >> 31;
        int64_t interpolate_frac   = (uint32_t) ~curr_frac;
        uint32_t interpolate_count = advance - 1;

        curr_offset += offset_inc;

        while (interpolate_count--) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        curr_frac += adv_frac;

        if (curr_frac < adv_frac) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        smp               = get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        interpolate_frac += curr_frac;
        interpolate_div  += (((int64_t) ((uint32_t) curr_frac >> 1) * smp) >> 31);
        smp               = (interpolate_div << 24) / (interpolate_frac >> 8);
        *mix_buf++       += smp;
        *mix_buf++       += smp;
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

MIX(stereo_8_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_8;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_8_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_8;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_16_to_8_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16_to_8;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_16_to_8_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16_to_8;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_32_to_8_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32_to_8;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_32_to_8_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32_to_8;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_x_to_8_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x_to_8;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_x_to_8_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x_to_8;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_16_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_16_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_32_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_32_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_x_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_x_center)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x;

        mix_average_center(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_center_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

static void mix_surround_loop(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, int32_t **const buf, void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;
    int32_t smp;

    get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);

    do {
        int64_t interpolate_frac = -((int64_t) channel_info->prev_sample - (int64_t) channel_info->curr_sample);
        int64_t interpolate_div  = ((int64_t) channel_info->next_sample - ((int64_t) channel_info->curr_sample + interpolate_frac)) >> 2;
        int32_t smp_value;

        smp               = (uint32_t) curr_frac >> 1;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div   = ((interpolate_div << 2) + interpolate_frac) >> 2;
        interpolate_div   = ((int64_t) smp * interpolate_div) >> 32;
        interpolate_div <<= 3;
        smp_value         = ((int64_t) channel_info->prev_sample + (int64_t) channel_info->curr_sample) >> 1;
        smp               = (uint32_t) smp_value + (uint32_t) interpolate_div;

        if (((smp_value ^ smp) & (interpolate_div ^ smp)) < 0)
            smp = channel_info->curr_sample;

        *mix_buf++ += smp;
        *mix_buf++ += ~smp;
        curr_frac  += adv_frac;

        if (curr_frac < adv_frac) {
            curr_offset              += offset_inc;
            channel_info->prev_sample = channel_info->curr_sample;
            channel_info->curr_sample = channel_info->next_sample;

            get_next_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        }
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

static void mix_average_surround(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, int32_t **const buf, int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset), int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset), const uint32_t offset_inc, uint32_t *const offset, uint32_t *const fraction, const uint32_t advance, const uint32_t adv_frac, const uint32_t len)
{
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i           = len;

    do {
        int32_t smp                = get_curr_sample_func(channel_info, channel_block, curr_offset);
        int64_t interpolate_div    = ((int64_t) ((uint32_t) ~curr_frac >> 1) * smp) >> 31;
        int64_t interpolate_frac   = (uint32_t) ~curr_frac;
        uint32_t interpolate_count = advance - 1;

        curr_offset += offset_inc;

        while (interpolate_count--) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        curr_frac += adv_frac;

        if (curr_frac < adv_frac) {
            interpolate_frac += INT64_C(0x100000000);
            interpolate_div  += get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
            curr_offset      += offset_inc;
        }

        smp               = get_sample_func(mixer_data, channel_info, channel_block, curr_offset);
        interpolate_frac += curr_frac;
        interpolate_div  += (((int64_t) ((uint32_t) curr_frac >> 1) * smp) >> 31);
        smp               = (interpolate_div << 24) / (interpolate_frac >> 8);
        *mix_buf++       += smp;
        *mix_buf++       += ~smp;
    } while (--i);

    *buf      = mix_buf;
    *offset   = curr_offset;
    *fraction = curr_frac;
}

MIX(stereo_8_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_8;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_8_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_8;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_8;

        channel_info->curr_sample = get_curr_sample_8(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_16_to_8_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16_to_8;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_16_to_8_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16_to_8;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16_to_8;

        channel_info->curr_sample = get_curr_sample_16_to_8(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_32_to_8_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32_to_8;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_32_to_8_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32_to_8;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32_to_8;

        channel_info->curr_sample = get_curr_sample_32_to_8(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_x_to_8_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x_to_8;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_x_to_8_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x_to_8;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x_to_8;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x_to_8;

        channel_info->curr_sample = get_curr_sample_x_to_8(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_16_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_16;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_16_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_16;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_16;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_16;

        channel_info->curr_sample = get_curr_sample_16(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_32_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_32;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_32_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_32;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_32;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_32;

        channel_info->curr_sample = get_curr_sample_32(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_x_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_sample_1_x;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, 1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, 1, offset, fraction, advance, adv_frac, len);
    }
}

MIX(stereo_backwards_x_surround)
{
    if (advance) {
        int32_t (*get_curr_sample_func)(const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *const channel_block, const uint32_t offset) = get_curr_sample_x;
        int32_t (*get_sample_func)(const AV_HQMixerData *const mixer_data, const struct AV_HQMixerChannelInfo *const channel_info, const struct ChannelBlock *channel_block, uint32_t offset) = get_backwards_sample_1_x;

        mix_average_surround(mixer_data, channel_info, channel_block, buf, get_curr_sample_func, get_sample_func, -1, offset, fraction, advance, adv_frac, len);
   } else {
        void (*get_next_sample_func)(const AV_HQMixerData *const mixer_data, struct AV_HQMixerChannelInfo *const channel_info, struct ChannelBlock *const channel_block, uint32_t offset) = get_backwards_next_sample_x;

        channel_info->curr_sample = get_curr_sample_x(channel_info, channel_block, *offset);
        mix_surround_loop(mixer_data, channel_info, channel_block, buf, get_next_sample_func, -1, offset, fraction, advance, adv_frac, len);
    }
}

#define CHANNEL_PREPARE(type)                                                       \
    static void channel_prepare_##type(const AV_HQMixerData *const mixer_data,      \
                                       struct ChannelBlock *const channel_block,    \
                                       uint32_t volume,                             \
                                       uint32_t panning)

CHANNEL_PREPARE(skip)
{
}

CHANNEL_PREPARE(stereo_8)
{
    uint32_t left_volume = 255 - panning;

    left_volume                    *= mixer_data->mixer_data.volume_left * volume;
    left_volume                   >>= 16;
    left_volume                    &= 0xFF00;
    channel_block->volume_left_lut  = mixer_data->volume_lut + left_volume;
    volume                          = ((panning * mixer_data->mixer_data.volume_right * volume) >> 16) & 0xFF00;
    channel_block->volume_right_lut = mixer_data->volume_lut + volume;
}

CHANNEL_PREPARE(stereo_8_left)
{
    volume                        *= mixer_data->mixer_data.volume_left;
    volume                       >>= 8;
    volume                        &= 0xFF00;
    channel_block->volume_left_lut = mixer_data->volume_lut + volume;
}

CHANNEL_PREPARE(stereo_8_right)
{
    volume                         *= mixer_data->mixer_data.volume_right;
    volume                        >>= 8;
    volume                         &= 0xFF00;
    channel_block->volume_right_lut = mixer_data->volume_lut + volume;
}

CHANNEL_PREPARE(stereo_8_center)
{
    volume                        *= mixer_data->mixer_data.volume_left;
    volume                       >>= 9;
    volume                        &= 0xFF00;
    channel_block->volume_left_lut = mixer_data->volume_lut + volume;
}

CHANNEL_PREPARE(stereo_16)
{
    uint32_t left_volume = 255 - panning;

    left_volume                     *= mixer_data->mixer_data.volume_left * volume;
    left_volume                    >>= 24;
    channel_block->mult_left_volume  = left_volume * mixer_data->amplify;
    volume                           = (panning * mixer_data->mixer_data.volume_right * volume) >> 24;
    channel_block->mult_right_volume = volume * mixer_data->amplify;
    channel_block->div_volume        = (uint32_t) mixer_data->channels_in << 8;
}

CHANNEL_PREPARE(stereo_16_left)
{
    volume                         *= mixer_data->mixer_data.volume_left;
    volume                        >>= 16;
    channel_block->mult_left_volume = volume * mixer_data->amplify;
    channel_block->div_volume       = (uint32_t) mixer_data->channels_in << 8;
}

CHANNEL_PREPARE(stereo_16_right)
{
    volume                          *= mixer_data->mixer_data.volume_right;
    volume                         >>= 16;
    channel_block->mult_right_volume = volume * mixer_data->amplify;
    channel_block->div_volume        = (uint32_t) mixer_data->channels_in << 8;
}

CHANNEL_PREPARE(stereo_16_center)
{
    volume                         *= mixer_data->mixer_data.volume_left;
    volume                        >>= 17;
    channel_block->mult_left_volume = volume * mixer_data->amplify;
    channel_block->div_volume       = (uint32_t) mixer_data->channels_in << 8;
}

CHANNEL_PREPARE(stereo_32)
{
    uint32_t left_volume = 255 - panning;

    left_volume                     *= mixer_data->mixer_data.volume_left * volume;
    left_volume                    >>= 24;
    channel_block->mult_left_volume  = (left_volume * mixer_data->amplify) >> 8;
    volume                           = (panning * mixer_data->mixer_data.volume_right * volume) >> 24;
    channel_block->mult_right_volume = (volume * mixer_data->amplify) >> 8;
    channel_block->div_volume        = (uint32_t) mixer_data->channels_in << 16;
}

CHANNEL_PREPARE(stereo_32_left)
{
    volume                         *= mixer_data->mixer_data.volume_left;
    volume                        >>= 16;
    channel_block->mult_left_volume = (volume * mixer_data->amplify) >> 8;
    channel_block->div_volume       = (uint32_t) mixer_data->channels_in << 16;
}

CHANNEL_PREPARE(stereo_32_right)
{
    volume                          *= mixer_data->mixer_data.volume_right;
    volume                         >>= 16;
    channel_block->mult_right_volume = (volume * mixer_data->amplify) >> 8;
    channel_block->div_volume        = (uint32_t) mixer_data->channels_in << 16;
}

CHANNEL_PREPARE(stereo_32_center)
{
    volume                         *= mixer_data->mixer_data.volume_left;
    volume                        >>= 17;
    channel_block->mult_left_volume = (volume * mixer_data->amplify) >> 8;
    channel_block->div_volume       = (uint32_t) mixer_data->channels_in << 16;
}

static const void *mixer_skip[] = {
    channel_prepare_skip,
    channel_prepare_skip,
    channel_prepare_skip,
    mix_skip,
    mix_skip,
    mix_skip,
    mix_skip,
    mix_skip_backwards,
    mix_skip_backwards,
    mix_skip_backwards,
    mix_skip_backwards
};

static const void *mixer_mono[] = {
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_16_center,
    channel_prepare_stereo_32_center,
    mix_mono_8,
    mix_mono_16,
    mix_mono_32,
    mix_mono_x,
    mix_mono_backwards_8,
    mix_mono_backwards_16,
    mix_mono_backwards_32,
    mix_mono_backwards_x
};

static const void *mixer_stereo[] = {
    channel_prepare_stereo_8,
    channel_prepare_stereo_16,
    channel_prepare_stereo_32,
    mix_stereo_8,
    mix_stereo_16,
    mix_stereo_32,
    mix_stereo_x,
    mix_stereo_backwards_8,
    mix_stereo_backwards_16,
    mix_stereo_backwards_32,
    mix_stereo_backwards_x
};

static const void *mixer_stereo_left[] = {
    channel_prepare_stereo_8_left,
    channel_prepare_stereo_16_left,
    channel_prepare_stereo_32_left,
    mix_stereo_8_left,
    mix_stereo_16_left,
    mix_stereo_32_left,
    mix_stereo_x_left,
    mix_stereo_backwards_8_left,
    mix_stereo_backwards_16_left,
    mix_stereo_backwards_32_left,
    mix_stereo_backwards_x_left
};

static const void *mixer_stereo_right[] = {
    channel_prepare_stereo_8_right,
    channel_prepare_stereo_16_right,
    channel_prepare_stereo_32_right,
    mix_stereo_8_right,
    mix_stereo_16_right,
    mix_stereo_32_right,
    mix_stereo_x_right,
    mix_stereo_backwards_8_right,
    mix_stereo_backwards_16_right,
    mix_stereo_backwards_32_right,
    mix_stereo_backwards_x_right
};

static const void *mixer_stereo_center[] = {
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_16_center,
    channel_prepare_stereo_32_center,
    mix_stereo_8_center,
    mix_stereo_16_center,
    mix_stereo_32_center,
    mix_stereo_x_center,
    mix_stereo_backwards_8_center,
    mix_stereo_backwards_16_center,
    mix_stereo_backwards_32_center,
    mix_stereo_backwards_x_center
};

static const void *mixer_stereo_surround[] = {
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_16_center,
    channel_prepare_stereo_32_center,
    mix_stereo_8_surround,
    mix_stereo_16_surround,
    mix_stereo_32_surround,
    mix_stereo_x_surround,
    mix_stereo_backwards_8_surround,
    mix_stereo_backwards_16_surround,
    mix_stereo_backwards_32_surround,
    mix_stereo_backwards_x_surround
};

static const void *mixer_skip_16_to_8[] = {
    channel_prepare_skip,
    channel_prepare_skip,
    channel_prepare_skip,
    mix_skip,
    mix_skip,
    mix_skip,
    mix_skip,
    mix_skip_backwards,
    mix_skip_backwards,
    mix_skip_backwards,
    mix_skip_backwards
};

static const void *mixer_mono_16_to_8[] = {
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_8_center,
    mix_mono_8,
    mix_mono_16_to_8,
    mix_mono_32_to_8,
    mix_mono_x_to_8,
    mix_mono_backwards_8,
    mix_mono_backwards_16_to_8,
    mix_mono_backwards_32_to_8,
    mix_mono_backwards_x_to_8
};

static const void *mixer_stereo_16_to_8[] = {
    channel_prepare_stereo_8,
    channel_prepare_stereo_8,
    channel_prepare_stereo_8,
    mix_stereo_8,
    mix_stereo_16_to_8,
    mix_stereo_32_to_8,
    mix_stereo_x_to_8,
    mix_stereo_backwards_8,
    mix_stereo_backwards_16_to_8,
    mix_stereo_backwards_32_to_8,
    mix_stereo_backwards_x_to_8
};

static const void *mixer_stereo_left_16_to_8[] = {
    channel_prepare_stereo_8_left,
    channel_prepare_stereo_8_left,
    channel_prepare_stereo_8_left,
    mix_stereo_8_left,
    mix_stereo_16_to_8_left,
    mix_stereo_32_to_8_left,
    mix_stereo_x_to_8_left,
    mix_stereo_backwards_8_left,
    mix_stereo_backwards_16_to_8_left,
    mix_stereo_backwards_32_to_8_left,
    mix_stereo_backwards_x_to_8_left
};

static const void *mixer_stereo_right_16_to_8[] = {
    channel_prepare_stereo_8_right,
    channel_prepare_stereo_8_right,
    channel_prepare_stereo_8_right,
    mix_stereo_8_right,
    mix_stereo_16_to_8_right,
    mix_stereo_32_to_8_right,
    mix_stereo_x_to_8_right,
    mix_stereo_backwards_8_right,
    mix_stereo_backwards_16_to_8_right,
    mix_stereo_backwards_32_to_8_right,
    mix_stereo_backwards_x_to_8_right
};

static const void *mixer_stereo_center_16_to_8[] = {
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_8_center,
    mix_stereo_8_center,
    mix_stereo_16_to_8_center,
    mix_stereo_32_to_8_center,
    mix_stereo_x_to_8_center,
    mix_stereo_backwards_8_center,
    mix_stereo_backwards_16_to_8_center,
    mix_stereo_backwards_32_to_8_center,
    mix_stereo_backwards_x_to_8_center
};

static const void *mixer_stereo_surround_16_to_8[] = {
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_8_center,
    channel_prepare_stereo_8_center,
    mix_stereo_8_surround,
    mix_stereo_16_to_8_surround,
    mix_stereo_32_to_8_surround,
    mix_stereo_x_to_8_surround,
    mix_stereo_backwards_8_surround,
    mix_stereo_backwards_16_to_8_surround,
    mix_stereo_backwards_32_to_8_surround,
    mix_stereo_backwards_x_to_8_surround
};

static void set_mix_functions(const AV_HQMixerData *const mixer_data, struct ChannelBlock *const channel_block)
{
    void **mix_func;
    void (*init_mixer_func)(const AV_HQMixerData *const mixer_data, struct ChannelBlock *channel_block, uint32_t volume, uint32_t panning);
    uint32_t panning = 0x80;

    if ((channel_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
        if ((channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_MUTED) || !channel_block->volume || !mixer_data->amplify || !channel_block->data) {
            mix_func = (void *) &mixer_skip_16_to_8;
        } else if (mixer_data->channels_out <= 1) {
            mix_func = (void *) &mixer_mono_16_to_8;
        } else if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_SURROUND) {
            mix_func = (void *) &mixer_stereo_16_to_8;

            if (mixer_data->mixer_data.volume_left == mixer_data->mixer_data.volume_right)
                mix_func = (void *) &mixer_stereo_surround_16_to_8;
        } else {
            switch ((panning = channel_block->panning)) {
            case 0 :
                mix_func = (void *) &mixer_skip_16_to_8;

                if (mixer_data->mixer_data.volume_left)
                    mix_func = (void *) &mixer_stereo_left_16_to_8;

                break;
            case 0xFF :
                mix_func = (void *) &mixer_skip_16_to_8;

                if (mixer_data->mixer_data.volume_right)
                    mix_func = (void *) &mixer_stereo_right_16_to_8;

                break;
            case 0x80 :
                mix_func = (void *) &mixer_stereo_16_to_8;

                if (mixer_data->mixer_data.volume_left == mixer_data->mixer_data.volume_right)
                    mix_func = (void *) &mixer_stereo_center_16_to_8;

                break;
            default :
                mix_func = (void *) &mixer_stereo_16_to_8;

                break;
            }
        }
    } else if ((channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_MUTED) || !channel_block->volume || !mixer_data->amplify || !channel_block->data) {
        mix_func = (void *) &mixer_skip;
    } else if (mixer_data->channels_out <= 1) {
        mix_func = (void *) &mixer_mono;
    } else if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_SURROUND) {
        mix_func = (void *) &mixer_stereo;

        if (mixer_data->mixer_data.volume_left == mixer_data->mixer_data.volume_right)
            mix_func = (void *) &mixer_stereo_surround;
    } else {
        switch ((panning = channel_block->panning)) {
        case 0 :
            mix_func = (void *) &mixer_skip;

            if (mixer_data->mixer_data.volume_left)
                mix_func = (void *) &mixer_stereo_left;

            break;
        case 0xFF :
            mix_func = (void *) &mixer_skip;

            if (mixer_data->mixer_data.volume_right)
                mix_func = (void *) &mixer_stereo_right;

            break;
        case 0x80 :
            mix_func = (void *) &mixer_stereo;

            if (mixer_data->mixer_data.volume_left == mixer_data->mixer_data.volume_right)
                mix_func = (void *) &mixer_stereo_center;

            break;
        default :
            mix_func = (void *) &mixer_stereo;

            break;
        }
    }

    switch (channel_block->bits_per_sample) {
    case 8 :
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
            channel_block->mix_func           = (void *) mix_func[7];
            channel_block->mix_backwards_func = (void *) mix_func[3];
        } else {
            channel_block->mix_func           = (void *) mix_func[3];
            channel_block->mix_backwards_func = (void *) mix_func[7];
        }

        init_mixer_func = (void *) mix_func[0];

        break;
    case 16 :
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
            channel_block->mix_func           = (void *) mix_func[8];
            channel_block->mix_backwards_func = (void *) mix_func[4];
        } else {
            channel_block->mix_func           = (void *) mix_func[4];
            channel_block->mix_backwards_func = (void *) mix_func[8];
        }

        init_mixer_func = (void *) mix_func[1];

        break;
    case 32 :
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
            channel_block->mix_func           = (void *) mix_func[9];
            channel_block->mix_backwards_func = (void *) mix_func[5];
        } else {
            channel_block->mix_func           = (void *) mix_func[5];
            channel_block->mix_backwards_func = (void *) mix_func[9];
        }

        init_mixer_func = (void *) mix_func[2];

        break;
    default :
        if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
            channel_block->mix_func           = (void *) mix_func[10];
            channel_block->mix_backwards_func = (void *) mix_func[6];
        } else {
            channel_block->mix_func           = (void *) mix_func[6];
            channel_block->mix_backwards_func = (void *) mix_func[10];
        }

        init_mixer_func = (void *) mix_func[2];

        break;
    }

    init_mixer_func(mixer_data, channel_block, channel_block->volume, panning);
}

static void set_sample_mix_rate(const AV_HQMixerData *const mixer_data, struct ChannelBlock *const channel_block, const uint32_t rate)
{
    const uint32_t mix_rate = mixer_data->mix_rate;

    channel_block->rate         = rate;
    channel_block->advance      = rate / mix_rate;
    channel_block->advance_frac = (((uint64_t) rate % mix_rate) << 32) / mix_rate;

    set_mix_functions(mixer_data, channel_block);
}

// TODO: Implement low quality mixer identification and configuration.

/** Filter natural frequency table. Value is (2*PI*110*(2^0.25)*2^(x/24))*(2^24).  */
static const int64_t nat_freq_lut[] = {
    INT64_C( 13789545379), INT64_C( 14193609901), INT64_C( 14609514417), INT64_C( 15037605866),
    INT64_C( 15478241352), INT64_C( 15931788442), INT64_C( 16398625478), INT64_C( 16879141882),
    INT64_C( 17373738492), INT64_C( 17882827888), INT64_C( 18406834743), INT64_C( 18946196171),
    INT64_C( 19501362094), INT64_C( 20072795621), INT64_C( 20660973429), INT64_C( 21266386161),
    INT64_C( 21889538841), INT64_C( 22530951288), INT64_C( 23191158555), INT64_C( 23870711371),
    INT64_C( 24570176604), INT64_C( 25290137733), INT64_C( 26031195334), INT64_C( 26793967580),
    INT64_C( 27579090758), INT64_C( 28387219802), INT64_C( 29219028834), INT64_C( 30075211732),
    INT64_C( 30956482703), INT64_C( 31863576885), INT64_C( 32797250955), INT64_C( 33758283764),
    INT64_C( 34747476983), INT64_C( 35765655777), INT64_C( 36813669486), INT64_C( 37892392341),
    INT64_C( 39002724188), INT64_C( 40145591242), INT64_C( 41321946857), INT64_C( 42532772322),
    INT64_C( 43779077682), INT64_C( 45061902576), INT64_C( 46382317109), INT64_C( 47741422741),
    INT64_C( 49140353208), INT64_C( 50580275467), INT64_C( 52062390668), INT64_C( 53587935159),
    INT64_C( 55158181517), INT64_C( 56774439604), INT64_C( 58438057669), INT64_C( 60150423464),
    INT64_C( 61912965406), INT64_C( 63727153770), INT64_C( 65594501910), INT64_C( 67516567528),
    INT64_C( 69494953967), INT64_C( 71531311553), INT64_C( 73627338972), INT64_C( 75784784682),
    INT64_C( 78005448377), INT64_C( 80291182485), INT64_C( 82643893714), INT64_C( 85065544645),
    INT64_C( 87558155364), INT64_C( 90123805153), INT64_C( 92764634219), INT64_C( 95482845483),
    INT64_C( 98280706416), INT64_C(101160550933), INT64_C(104124781336), INT64_C(107175870319),
    INT64_C(110316363033), INT64_C(113548879209), INT64_C(116876115338), INT64_C(120300846927),
    INT64_C(123825930812), INT64_C(127454307540), INT64_C(131189003821), INT64_C(135033135055),
    INT64_C(138989907934), INT64_C(143062623107), INT64_C(147254677944), INT64_C(151569569364),
    INT64_C(156010896753), INT64_C(160582364969), INT64_C(165287787428), INT64_C(170131089290),
    INT64_C(175116310728), INT64_C(180247610306), INT64_C(185529268437), INT64_C(190965690965),
    INT64_C(196561412833), INT64_C(202321101866), INT64_C(208249562671), INT64_C(214351740638),
    INT64_C(220632726067), INT64_C(227097758417), INT64_C(233752230676), INT64_C(240601693855),
    INT64_C(247651861625), INT64_C(254908615079), INT64_C(262378007641), INT64_C(270066270111),
    INT64_C(277979815867), INT64_C(286125246214), INT64_C(294509355888), INT64_C(303139138728),
    INT64_C(312021793507), INT64_C(321164729938), INT64_C(330575574856), INT64_C(340262178579),
    INT64_C(350232621457), INT64_C(360495220611), INT64_C(371058536874), INT64_C(381931381930),
    INT64_C(393122825665), INT64_C(404642203733), INT64_C(416499125343), INT64_C(428703481275),
    INT64_C(441265452133), INT64_C(454195516834), INT64_C(467504461351), INT64_C(481203387710),
    INT64_C(495303723250), INT64_C(509817230159), INT64_C(524756015282), INT64_C(540132540222)
};

/** Filter damping factor table. Value is 2*10^(-((24/128)*x)/20)*(2^24).  */
static const int32_t damp_factor_lut[] = {
    33554432, 32837863, 32136597, 31450307, 30778673, 30121382, 29478127, 28848610,
    28232536, 27629619, 27039577, 26462136, 25897026, 25343984, 24802753, 24273080,
    23754719, 23247427, 22750969, 22265112, 21789632, 21324305, 20868916, 20423252,
    19987105, 19560272, 19142554, 18733757, 18333690, 17942167, 17559005, 17184025,
    16817053, 16457918, 16106452, 15762492, 15425878, 15096452, 14774061, 14458555,
    14149787, 13847612, 13551891, 13262485, 12979259, 12702081, 12430823, 12165358,
    11905562, 11651314, 11402495, 11158990, 10920685, 10687470, 10459234, 10235873,
    10017282,  9803359,  9594004,  9389120,  9188612,  8992385,  8800349,  8612414,
     8428492,  8248498,  8072348,  7899960,  7731253,  7566149,  7404571,  7246443,
     7091692,  6940246,  6792035,  6646988,  6505039,  6366121,  6230170,  6097122,
     5966916,  5839490,  5714785,  5592743,  5473308,  5356423,  5242035,  5130089,
     5020534,  4913318,  4808392,  4705707,  4605215,  4506869,  4410623,  4316432,
     4224253,  4134042,  4045758,  3959359,  3874805,  3792057,  3711076,  3631825,
     3554266,  3478363,  3404081,  3331386,  3260242,  3190619,  3122482,  3055800,
     2990542,  2926678,  2864177,  2803012,  2743152,  2684571,  2627241,  2571135,
     2516227,  2462492,  2409905,  2358440,  2308075,  2258785,  2210548,  2163341
};

static inline void mulu_128(uint64_t *result, const uint64_t a, const uint64_t b)
{
    const uint32_t a_hi = a >> 32;
    const uint32_t a_lo = (uint32_t) a;
    const uint32_t b_hi = b >> 32;
    const uint32_t b_lo = (uint32_t) b;

    uint64_t x0 = (uint64_t) a_hi * b_hi;
    uint64_t x1 = (uint64_t) a_lo * b_hi;
    uint64_t x2 = (uint64_t) a_hi * b_lo;
    uint64_t x3 = (uint64_t) a_lo * b_lo;

    x2 += x3 >> 32;
    x2 += x1;

    if (x2 < x1)
        x0 += UINT64_C(0x100000000);

    *result++ = x0 + (x2 >> 32);
    *result = ((x2 & UINT64_C(0xFFFFFFFF)) << 32) + (x3 & UINT64_C(0xFFFFFFFF));
}

static inline void muls_128(int64_t *result, const int64_t a, const int64_t b)
{
    int sign = (a ^ b) < 0;

    mulu_128(result, a < 0 ? -a : a, b < 0 ? -b : b );

    if (sign)
        *result = -(*result);
}

static inline uint64_t divu_128(uint64_t a_hi, uint64_t a_lo, const uint64_t b)
{
    uint64_t result = 0, result_r = 0;
    uint16_t i = 128;

    while (i--) {
        const uint64_t carry  = a_lo >> 63;
        const uint64_t carry2 = a_hi >> 63;

        result <<= 1;
        a_lo   <<= 1;
        a_hi     = (((a_hi << 1) | (a_hi >> 63)) & ~UINT64_C(1)) | carry; // simulate bitwise rotate with extend (carry)
        result_r = (((result_r << 1) | (result_r >> 63)) & ~UINT64_C(1)) | carry2; // simulate bitwise rotate with extend (carry)

        if (result_r >= b) {
            result_r -= b;
            result++;
        }
    }

    return result;
}

static inline int64_t divs_128(int64_t a_hi, uint64_t a_lo, const int64_t b)
{
    int sign = (a_hi ^ b) < 0;
    int64_t result = divu_128(a_hi < 0 ? -a_hi : a_hi, a_lo, b < 0 ? -b : b );

    if (sign)
        result = -result;

    return result;
}

static void update_sample_filter(const AV_HQMixerData *const mixer_data, struct ChannelBlock *const channel_block)
{
    const uint32_t mix_rate = mixer_data->mix_rate;
    uint64_t tmp_128[2];
    int64_t nat_freq, damp_factor, d, e, tmp;

    if ((channel_block->filter_cutoff == 127) && (channel_block->filter_damping == 0)) {
        channel_block->filter_c1 = 16777216;
        channel_block->filter_c2 = 0;
        channel_block->filter_c3 = 0;

        return;
    }

    nat_freq    = nat_freq_lut[channel_block->filter_cutoff];
    damp_factor = damp_factor_lut[channel_block->filter_damping];

    d = (nat_freq * (INT64_C(16777216) - damp_factor)) / ((int64_t) mix_rate << 24);

    if (d > INT64_C(33554432))
        d = INT64_C(33554432);

    muls_128(tmp_128, damp_factor - d, (int64_t) mix_rate << 24);
    d = divs_128(tmp_128[0], tmp_128[1], nat_freq);

    mulu_128(tmp_128, (uint64_t) mix_rate << 29, (uint64_t) mix_rate << 29); // Using more than 58 (2*29) bits in total will result in 128-bit integer multiply overflow for maximum allowed mixing rate of 768kHz
    e = (divu_128(tmp_128[0], tmp_128[1], nat_freq) / nat_freq) << 14;

    tmp = INT64_C(16777216) + d + e;

    channel_block->filter_c1 = (int32_t) (INT64_C(281474976710656) / tmp);
    channel_block->filter_c2 = (int32_t) (((d + e + e) << 24) / tmp);
    channel_block->filter_c3 = (int32_t) (((-e) << 24) / tmp);
}

static void set_sample_filter(const AV_HQMixerData *const mixer_data, struct ChannelBlock *const channel_block, uint8_t cutoff, uint8_t damping)
{
    if ((int8_t) cutoff < 0)
        cutoff = 127;

    if ((int8_t) damping < 0)
        damping = 127;

    if ((channel_block->filter_cutoff == cutoff) && (channel_block->filter_damping == damping))
        return;

    channel_block->filter_cutoff  = cutoff;
    channel_block->filter_damping = damping;

    update_sample_filter(mixer_data, channel_block);
}

static av_cold AVMixerData *init(AVMixerContext *mixctx, const char *args, void *opaque)
{
    AV_HQMixerData *hq_mixer_data;
    AV_HQMixerChannelInfo *channel_info;
    const char *cfg_buf;
    uint16_t i;
    int32_t *buf;
    unsigned real16bit = 1, buf_size;
    uint32_t mix_buf_mem_size, channel_rate;
    uint16_t channels_in = 1, channels_out = 1;

    if (!(hq_mixer_data = av_mallocz(sizeof(AV_HQMixerData) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer data factory.\n");

        return NULL;
    }

    if (!(hq_mixer_data->volume_lut = av_malloc((256 * 256 * sizeof(int32_t)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer volume lookup table.\n");
        av_free(hq_mixer_data);

        return NULL;
    }

    hq_mixer_data->mixer_data.mixctx = mixctx;
    buf_size                         = hq_mixer_data->mixer_data.mixctx->buf_size;

    if ((cfg_buf = av_stristr(args, "buffer=")))
        sscanf(cfg_buf, "buffer=%d;", &buf_size);

    if (av_stristr(args, "real16bit=false;") || av_stristr(args, "real16bit=disabled;"))
        real16bit = 0;
    else if ((cfg_buf = av_stristr(args, "real16bit=;")))
        sscanf(cfg_buf, "real16bit=%d;", &real16bit);

    if (!(channel_info = av_mallocz((channels_in * sizeof(AV_HQMixerChannelInfo)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer channel data.\n");
        av_freep(&hq_mixer_data->volume_lut);
        av_free(hq_mixer_data);

        return NULL;
    }

    hq_mixer_data->channel_info            = channel_info;
    hq_mixer_data->mixer_data.channels_in  = channels_in;
    hq_mixer_data->channels_in             = channels_in;
    hq_mixer_data->channels_out            = channels_out;
    mix_buf_mem_size                       = (buf_size << 2) * channels_out;

    if (!(buf = av_mallocz(mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer output buffer.\n");
        av_freep(&hq_mixer_data->channel_info);
        av_freep(&hq_mixer_data->volume_lut);
        av_free(hq_mixer_data);

        return NULL;
    }

    hq_mixer_data->mix_buf_size            = mix_buf_mem_size;
    hq_mixer_data->buf                     = buf;
    hq_mixer_data->buf_size                = buf_size;
    hq_mixer_data->mixer_data.mix_buf_size = hq_mixer_data->buf_size;
    hq_mixer_data->mixer_data.mix_buf      = hq_mixer_data->buf;
    channel_rate                           = hq_mixer_data->mixer_data.mixctx->frequency;
    hq_mixer_data->mixer_data.rate         = channel_rate;
    hq_mixer_data->mix_rate                = channel_rate;
    hq_mixer_data->real_16_bit_mode        = real16bit ? 1 : 0;

    if (!(buf = av_mallocz(mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer (resonance) filter output buffer.\n");
        av_freep(&hq_mixer_data->buf);
        av_freep(&hq_mixer_data->channel_info);
        av_freep(&hq_mixer_data->volume_lut);
        av_free(hq_mixer_data);

        return NULL;
    }

    hq_mixer_data->filter_buf = buf;

    for (i = hq_mixer_data->channels_in; i > 0; i--) {
        set_sample_filter(hq_mixer_data, &channel_info->current, 127, 0);
        set_sample_filter(hq_mixer_data, &channel_info->next, 127, 0);

        channel_info++;
    }

    return (AVMixerData *) hq_mixer_data;
}

static av_cold int uninit(AVMixerData *mixer_data)
{
    AV_HQMixerData *hq_mixer_data = (AV_HQMixerData *) mixer_data;

    if (!hq_mixer_data)
        return AVERROR_INVALIDDATA;

    av_freep(&hq_mixer_data->channel_info);
    av_freep(&hq_mixer_data->volume_lut);
    av_freep(&hq_mixer_data->buf);
    av_freep(&hq_mixer_data->filter_buf);
    av_free(hq_mixer_data);

    return 0;
}

static av_cold uint32_t set_tempo(AVMixerData *mixer_data, uint32_t new_tempo)
{
    AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    uint32_t channel_rate = hq_mixer_data->mix_rate * 10;
    uint64_t pass_value;

    hq_mixer_data->mixer_data.tempo = new_tempo;
    pass_value                      = ((uint64_t) channel_rate << 16) + ((uint64_t) hq_mixer_data->mix_rate_frac >> 16);
    hq_mixer_data->pass_len         = (uint64_t) pass_value / hq_mixer_data->mixer_data.tempo;
    hq_mixer_data->pass_len_frac    = (((uint64_t) pass_value % hq_mixer_data->mixer_data.tempo) << 32) / hq_mixer_data->mixer_data.tempo;

    return new_tempo;
}

static av_cold uint32_t set_rate(AVMixerData *mixer_data, uint32_t new_mix_rate, uint32_t new_channels)
{
    AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    uint32_t buf_size, mix_rate, mix_rate_frac;

    hq_mixer_data->mixer_data.rate         = new_mix_rate;
    buf_size                               = hq_mixer_data->mixer_data.mix_buf_size;
    hq_mixer_data->mixer_data.channels_out = new_channels;

    if ((hq_mixer_data->buf_size * hq_mixer_data->channels_out) != (buf_size * new_channels)) {
        int32_t *buf                    = hq_mixer_data->mixer_data.mix_buf;
        int32_t *filter_buf             = hq_mixer_data->filter_buf;
        const uint32_t mix_buf_mem_size = (buf_size * new_channels) << 2;

        if (!(buf = av_realloc(buf, mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(hq_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer output buffer.\n");

            return hq_mixer_data->mixer_data.rate;
        } else if (!(filter_buf = av_realloc(filter_buf, mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(hq_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer (resonance) filter output buffer.\n");

            return hq_mixer_data->mixer_data.rate;
        }

        memset(buf, 0, mix_buf_mem_size);

        hq_mixer_data->mixer_data.mix_buf      = buf;
        hq_mixer_data->mixer_data.mix_buf_size = buf_size;
        hq_mixer_data->filter_buf              = filter_buf;
    }

    hq_mixer_data->channels_out = new_channels;
    hq_mixer_data->buf          = hq_mixer_data->mixer_data.mix_buf;
    hq_mixer_data->buf_size     = hq_mixer_data->mixer_data.mix_buf_size;

    if (hq_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_MIXING) {
        mix_rate      = new_mix_rate; // TODO: Add check here if this mix rate is supported by target device
        mix_rate_frac = 0;

        if (hq_mixer_data->mix_rate != mix_rate) {
            AV_HQMixerChannelInfo *channel_info = hq_mixer_data->channel_info;
            uint16_t i;

            hq_mixer_data->mix_rate      = mix_rate;
            hq_mixer_data->mix_rate_frac = mix_rate_frac;

            if (hq_mixer_data->mixer_data.tempo)
                set_tempo((AVMixerData *) mixer_data, hq_mixer_data->mixer_data.tempo);

            for (i = hq_mixer_data->channels_in; i > 0; i--) {
                channel_info->current.advance      = channel_info->current.rate / mix_rate;
                channel_info->current.advance_frac = (((uint64_t) channel_info->current.rate % mix_rate) << 32) / mix_rate;
                channel_info->next.advance         = channel_info->next.rate / mix_rate;
                channel_info->next.advance_frac    = (((uint64_t) channel_info->next.rate % mix_rate) << 32) / mix_rate;

                update_sample_filter(hq_mixer_data, &channel_info->current);
                update_sample_filter(hq_mixer_data, &channel_info->next);

                channel_info++;
            }
        }
    }

    // TODO: Inform libavfilter that the target mixing rate has been changed.

    return new_mix_rate;
}

static av_cold uint32_t set_volume(AVMixerData *mixer_data, uint32_t amplify, uint32_t left_volume, uint32_t right_volume, uint32_t channels)
{
    AV_HQMixerData *const hq_mixer_data           = (AV_HQMixerData *) mixer_data;
    AV_HQMixerChannelInfo *channel_info           = NULL;
    AV_HQMixerChannelInfo *const old_channel_info = hq_mixer_data->channel_info;
    uint32_t old_channels, i;

    if (((old_channels = hq_mixer_data->channels_in) != channels) && !(channel_info = av_mallocz((channels * sizeof(AV_HQMixerChannelInfo)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(hq_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer channel data.\n");

        return old_channels;
    }

    hq_mixer_data->mixer_data.volume_boost = amplify;
    hq_mixer_data->mixer_data.volume_left  = left_volume;
    hq_mixer_data->mixer_data.volume_right = right_volume;
    hq_mixer_data->mixer_data.channels_in  = channels;

    if ((old_channels != channels) || (hq_mixer_data->amplify != amplify)) {
        int32_t *volume_lut = hq_mixer_data->volume_lut;
        int32_t volume_mult = 0, volume_div = channels << 8;
        uint8_t i           = 0, j = 0;

        hq_mixer_data->amplify = amplify;

        do {
            do {
                const int32_t volume = (int8_t) j << 8;

                *volume_lut++ = ((int64_t) volume * volume_mult) / volume_div;
            } while (++j);

            volume_mult += amplify;
        } while (++i);
    }

    if (channel_info && (old_channels != channels)) {
        uint32_t copy_channels = old_channels;
        uint16_t i;

        if (copy_channels > channels)
            copy_channels = channels;

        memcpy(channel_info, old_channel_info, copy_channels * sizeof(AV_HQMixerChannelInfo));

        hq_mixer_data->channel_info = channel_info;
        hq_mixer_data->channels_in  = channels;

        channel_info += copy_channels;

        for (i = copy_channels; i < channels; ++i) {
            set_sample_filter(hq_mixer_data, &channel_info->current, 127, 0);
            set_sample_filter(hq_mixer_data, &channel_info->next, 127, 0);

            channel_info++;
        }

        av_free(old_channel_info);
    }

    channel_info = hq_mixer_data->channel_info;

    for (i = channels; i > 0; i--) {
        set_sample_mix_rate(hq_mixer_data, &channel_info->current, channel_info->current.rate);

        channel_info++;
    }

    return channels;
}

static av_cold void get_channel(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    const AV_HQMixerData *const hq_mixer_data       = (AV_HQMixerData *) mixer_data;
    const AV_HQMixerChannelInfo *const channel_info = hq_mixer_data->channel_info + channel;

    mixer_channel->pos             = channel_info->current.offset;
    mixer_channel->bits_per_sample = channel_info->current.bits_per_sample;
    mixer_channel->flags           = channel_info->current.flags;
    mixer_channel->volume          = channel_info->current.volume;
    mixer_channel->panning         = channel_info->current.panning;
    mixer_channel->data            = channel_info->current.data;
    mixer_channel->len             = channel_info->current.len;
    mixer_channel->repeat_start    = channel_info->current.repeat;
    mixer_channel->repeat_length   = channel_info->current.repeat_len;
    mixer_channel->repeat_count    = channel_info->current.count_restart;
    mixer_channel->repeat_counted  = channel_info->current.counted;
    mixer_channel->rate            = channel_info->current.rate;
    mixer_channel->filter_cutoff   = channel_info->current.filter_cutoff;
    mixer_channel->filter_damping  = channel_info->current.filter_damping;
}

static av_cold void set_channel(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    const AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    AV_HQMixerChannelInfo *const channel_info = hq_mixer_data->channel_info + channel;
    struct ChannelBlock *channel_block;
    uint32_t repeat, repeat_len;

    channel_info->next.data = NULL;

    if (mixer_channel->flags & AVSEQ_MIXER_CHANNEL_FLAG_SYNTH)
        channel_block = &channel_info->next;
    else
        channel_block = &channel_info->current;

    channel_block->offset           = mixer_channel->pos;
    channel_block->fraction         = 0;
    channel_block->bits_per_sample  = mixer_channel->bits_per_sample;
    channel_block->flags            = mixer_channel->flags;
    channel_block->volume           = mixer_channel->volume;
    channel_block->panning          = mixer_channel->panning;
    channel_block->data             = mixer_channel->data;
    channel_block->len              = mixer_channel->len;
    repeat                          = mixer_channel->repeat_start;
    repeat_len                      = mixer_channel->repeat_length;
    channel_block->repeat           = repeat;
    channel_block->repeat_len       = repeat_len;

    if (!(channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP)) {
        repeat     = mixer_channel->len;
        repeat_len = 0;
    }

    repeat += repeat_len;

    if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
        repeat -= repeat_len;

        if (!(channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP))
            repeat = -1;
    }

    channel_block->end_offset     = repeat;
    channel_block->restart_offset = repeat_len;
    channel_block->count_restart  = mixer_channel->repeat_count;
    channel_block->counted        = mixer_channel->repeat_counted;

    set_sample_mix_rate(hq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(hq_mixer_data, channel_block, mixer_channel->filter_cutoff, mixer_channel->filter_damping);
}

static av_cold void reset_channel(AVMixerData *mixer_data, uint32_t channel)
{
    const AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    AV_HQMixerChannelInfo *const channel_info = hq_mixer_data->channel_info + channel;
    struct ChannelBlock *channel_block        = &channel_info->current;
    uint32_t repeat, repeat_len;

    channel_block->offset           = 0;
    channel_block->fraction         = 0;
    channel_block->bits_per_sample  = 0;
    channel_block->flags            = 0;
    channel_block->volume           = 0;
    channel_block->panning          = 0;
    channel_block->data             = NULL;
    channel_block->len              = 0;
    repeat                          = 0;
    repeat_len                      = 0;
    channel_block->repeat           = 0;
    channel_block->repeat_len       = 0;
    channel_block->end_offset       = 0;
    channel_block->restart_offset   = 0;
    channel_block->count_restart    = 0;
    channel_block->counted          = 0;

    set_sample_mix_rate(hq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(hq_mixer_data, channel_block, 127, 0);

    channel_block                   = &channel_info->next;
    channel_block->offset           = 0;
    channel_block->fraction         = 0;
    channel_block->bits_per_sample  = 0;
    channel_block->flags            = 0;
    channel_block->volume           = 0;
    channel_block->panning          = 0;
    channel_block->data             = NULL;
    channel_block->len              = 0;
    repeat                          = 0;
    repeat_len                      = 0;
    channel_block->repeat           = 0;
    channel_block->repeat_len       = 0;
    channel_block->end_offset       = 0;
    channel_block->restart_offset   = 0;
    channel_block->count_restart    = 0;
    channel_block->counted          = 0;
    channel_info->filter_tmp1       = 0;
    channel_info->filter_tmp2       = 0;
    channel_info->prev_sample       = 0;
    channel_info->curr_sample       = 0;
    channel_info->next_sample       = 0;
    channel_info->prev_sample_r     = 0;
    channel_info->curr_sample_r     = 0;
    channel_info->next_sample_r     = 0;

    set_sample_mix_rate(hq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(hq_mixer_data, channel_block, 127, 0);
}

static av_cold void get_both_channels(AVMixerData *mixer_data, AVMixerChannel *mixer_channel_current, AVMixerChannel *mixer_channel_next, uint32_t channel)
{
    const AV_HQMixerData *const hq_mixer_data       = (AV_HQMixerData *) mixer_data;
    const AV_HQMixerChannelInfo *const channel_info = hq_mixer_data->channel_info + channel;

    mixer_channel_current->pos             = channel_info->current.offset;
    mixer_channel_current->bits_per_sample = channel_info->current.bits_per_sample;
    mixer_channel_current->flags           = channel_info->current.flags;
    mixer_channel_current->volume          = channel_info->current.volume;
    mixer_channel_current->panning         = channel_info->current.panning;
    mixer_channel_current->data            = channel_info->current.data;
    mixer_channel_current->len             = channel_info->current.len;
    mixer_channel_current->repeat_start    = channel_info->current.repeat;
    mixer_channel_current->repeat_length   = channel_info->current.repeat_len;
    mixer_channel_current->repeat_count    = channel_info->current.count_restart;
    mixer_channel_current->repeat_counted  = channel_info->current.counted;
    mixer_channel_current->rate            = channel_info->current.rate;
    mixer_channel_current->filter_cutoff   = channel_info->current.filter_cutoff;
    mixer_channel_current->filter_damping  = channel_info->current.filter_damping;

    mixer_channel_next->pos             = channel_info->next.offset;
    mixer_channel_next->bits_per_sample = channel_info->next.bits_per_sample;
    mixer_channel_next->flags           = channel_info->next.flags;
    mixer_channel_next->volume          = channel_info->next.volume;
    mixer_channel_next->panning         = channel_info->next.panning;
    mixer_channel_next->data            = channel_info->next.data;
    mixer_channel_next->len             = channel_info->next.len;
    mixer_channel_next->repeat_start    = channel_info->next.repeat;
    mixer_channel_next->repeat_length   = channel_info->next.repeat_len;
    mixer_channel_next->repeat_count    = channel_info->next.count_restart;
    mixer_channel_next->repeat_counted  = channel_info->next.counted;
    mixer_channel_next->rate            = channel_info->next.rate;
    mixer_channel_next->filter_cutoff   = channel_info->next.filter_cutoff;
    mixer_channel_next->filter_damping  = channel_info->next.filter_damping;
}

static av_cold void set_both_channels(AVMixerData *mixer_data, AVMixerChannel *mixer_channel_current, AVMixerChannel *mixer_channel_next, uint32_t channel)
{
    const AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    AV_HQMixerChannelInfo *const channel_info = hq_mixer_data->channel_info + channel;
    struct ChannelBlock *channel_block        = &channel_info->current;
    uint32_t repeat, repeat_len;

    channel_block->offset           = mixer_channel_current->pos;
    channel_block->fraction         = 0;
    channel_block->bits_per_sample  = mixer_channel_current->bits_per_sample;
    channel_block->flags            = mixer_channel_current->flags;
    channel_block->volume           = mixer_channel_current->volume;
    channel_block->panning          = mixer_channel_current->panning;
    channel_block->data             = mixer_channel_current->data;
    channel_block->len              = mixer_channel_current->len;
    repeat                          = mixer_channel_current->repeat_start;
    repeat_len                      = mixer_channel_current->repeat_length;
    channel_block->repeat           = repeat;
    channel_block->repeat_len       = repeat_len;

    if (!(channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP)) {
        repeat     = mixer_channel_current->len;
        repeat_len = 0;
    }

    repeat += repeat_len;

    if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
        repeat -= repeat_len;

        if (!(channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP))
            repeat = -1;
    }

    channel_block->end_offset     = repeat;
    channel_block->restart_offset = repeat_len;
    channel_block->count_restart  = mixer_channel_current->repeat_count;
    channel_block->counted        = mixer_channel_current->repeat_counted;

    set_sample_mix_rate(hq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(hq_mixer_data, channel_block, mixer_channel_current->filter_cutoff, mixer_channel_current->filter_damping);

    channel_block                   = &channel_info->next;
    channel_block->offset           = mixer_channel_next->pos;
    channel_block->fraction         = 0;
    channel_block->bits_per_sample  = mixer_channel_next->bits_per_sample;
    channel_block->flags            = mixer_channel_next->flags;
    channel_block->volume           = mixer_channel_next->volume;
    channel_block->panning          = mixer_channel_next->panning;
    channel_block->data             = mixer_channel_next->data;
    channel_block->len              = mixer_channel_next->len;
    repeat                          = mixer_channel_next->repeat_start;
    repeat_len                      = mixer_channel_next->repeat_length;
    channel_block->repeat           = repeat;
    channel_block->repeat_len       = repeat_len;

    if (!(channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP)) {
        repeat     = mixer_channel_next->len;
        repeat_len = 0;
    }

    repeat += repeat_len;

    if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
        repeat -= repeat_len;

        if (!(channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP))
            repeat = -1;
    }

    channel_block->end_offset     = repeat;
    channel_block->restart_offset = repeat_len;
    channel_block->count_restart  = mixer_channel_next->repeat_count;
    channel_block->counted        = mixer_channel_next->repeat_counted;
    channel_info->filter_tmp1     = 0;
    channel_info->filter_tmp2     = 0;
    channel_info->prev_sample     = 0;
    channel_info->curr_sample     = 0;
    channel_info->next_sample     = 0;
    channel_info->prev_sample_r   = 0;
    channel_info->curr_sample_r   = 0;
    channel_info->next_sample_r   = 0;

    set_sample_mix_rate(hq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(hq_mixer_data, channel_block, mixer_channel_next->filter_cutoff, mixer_channel_next->filter_damping);
}

static av_cold void set_channel_volume_panning_pitch(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    const AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    AV_HQMixerChannelInfo *const channel_info = hq_mixer_data->channel_info + channel;

    if ((channel_info->current.volume == mixer_channel->volume) && (channel_info->current.panning == mixer_channel->panning)) {
        const uint32_t rate = mixer_channel->rate, mix_rate = hq_mixer_data->mix_rate;
        uint32_t rate_frac;

        channel_info->current.rate         = rate;
        channel_info->next.rate            = rate;
        rate_frac                          = rate / mix_rate;
        channel_info->current.advance      = rate_frac;
        channel_info->next.advance         = rate_frac;
        rate_frac                          = (((uint64_t) rate % mix_rate) << 32) / mix_rate;
        channel_info->current.advance_frac = rate_frac;
        channel_info->next.advance_frac    = rate_frac;
    } else {
        const uint32_t rate  = mixer_channel->rate, mix_rate = hq_mixer_data->mix_rate;
        const uint8_t volume = mixer_channel->volume;
        const int8_t panning = mixer_channel->panning;
        uint32_t rate_frac;

        channel_info->current.volume       = volume;
        channel_info->next.volume          = volume;
        channel_info->current.panning      = panning;
        channel_info->next.panning         = panning;
        channel_info->current.rate         = rate;
        channel_info->next.rate            = rate;
        rate_frac                          = rate / mix_rate;
        channel_info->current.advance      = rate_frac;
        channel_info->next.advance         = rate_frac;
        rate_frac                          = (((uint64_t) rate % mix_rate) << 32) / mix_rate;
        channel_info->current.advance_frac = rate_frac;
        channel_info->next.advance_frac    = rate_frac;

        set_mix_functions(hq_mixer_data, &channel_info->current);
        set_mix_functions(hq_mixer_data, &channel_info->next);
    }
}

static av_cold void set_channel_position_repeat_flags(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    const AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    AV_HQMixerChannelInfo *const channel_info = hq_mixer_data->channel_info + channel;

    if (channel_info->current.flags == mixer_channel->flags) {
        uint32_t repeat = mixer_channel->pos, repeat_len;

        if (repeat != channel_info->current.offset) {
            channel_info->current.offset   = repeat;
            channel_info->current.fraction = 0;
        }

        repeat                           = mixer_channel->repeat_start;
        repeat_len                       = mixer_channel->repeat_length;
        channel_info->current.repeat     = repeat;
        channel_info->current.repeat_len = repeat_len;

        if (!(channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP)) {
            repeat     = mixer_channel->len;
            repeat_len = 0;
        }

        repeat += repeat_len;

        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
            repeat -= repeat_len;

            if (!(channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP))
                repeat = -1;
        }

        channel_info->current.end_offset     = repeat;
        channel_info->current.restart_offset = repeat_len;
        channel_info->current.count_restart  = mixer_channel->repeat_count;
        channel_info->current.counted        = mixer_channel->repeat_counted;
    } else {
        uint32_t repeat, repeat_len;

        channel_info->current.flags = mixer_channel->flags;
        repeat                      = mixer_channel->pos;

        if (repeat != channel_info->current.offset) {
            channel_info->current.offset   = repeat;
            channel_info->current.fraction = 0;
        }

        repeat                           = mixer_channel->repeat_start;
        repeat_len                       = mixer_channel->repeat_length;
        channel_info->current.repeat     = repeat;
        channel_info->current.repeat_len = repeat_len;

        if (!(channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP)) {
            repeat     = mixer_channel->len;
            repeat_len = 0;
        }

        repeat += repeat_len;

        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
            repeat -= repeat_len;

            if (!(channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP))
                repeat = -1;
        }

        channel_info->current.end_offset     = repeat;
        channel_info->current.restart_offset = repeat_len;
        channel_info->current.count_restart  = mixer_channel->repeat_count;
        channel_info->current.counted        = mixer_channel->repeat_counted;

        set_mix_functions(hq_mixer_data, &channel_info->current);
    }
}

static av_cold void set_channel_filter(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    const AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    AV_HQMixerChannelInfo *const channel_info = hq_mixer_data->channel_info + channel;

    set_sample_filter(hq_mixer_data, &channel_info->current, mixer_channel->filter_cutoff, mixer_channel->filter_damping);
}

static av_cold void mix(AVMixerData *mixer_data, int32_t *buf) {
    AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    uint32_t mix_rate, current_left, current_left_frac, buf_size;

    if (!(hq_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_FROZEN)) {
        mix_rate          = hq_mixer_data->mix_rate;
        current_left      = hq_mixer_data->current_left;
        current_left_frac = hq_mixer_data->current_left_frac;
        buf_size          = hq_mixer_data->buf_size;

        memset(buf, 0, buf_size << ((hq_mixer_data->channels_out >= 2) ? 3 : 2));

        while (buf_size) {
            if (current_left) {
                uint32_t mix_len = buf_size;

                if (buf_size > current_left)
                    mix_len = current_left;

                current_left -= mix_len;
                buf_size     -= mix_len;

                mix_sample(hq_mixer_data, buf, mix_len);

                buf += (hq_mixer_data->channels_out >= 2) ? mix_len << 1 : mix_len;
            }

            if (current_left)
                continue;

            if (mixer_data->handler)
                mixer_data->handler(mixer_data);

            current_left       = hq_mixer_data->pass_len;
            current_left_frac += hq_mixer_data->pass_len_frac;

            if (current_left_frac < hq_mixer_data->pass_len_frac)
                current_left++;
        }

        hq_mixer_data->current_left      = current_left;
        hq_mixer_data->current_left_frac = current_left_frac;
    }

    // TODO: Execute post-processing step in libavfilter and pass the PCM data.
}

static av_cold void mix_parallel(AVMixerData *mixer_data, int32_t *buf, const uint32_t first_channel, const uint32_t last_channel) {
    AV_HQMixerData *const hq_mixer_data = (AV_HQMixerData *) mixer_data;
    uint32_t mix_rate, current_left, current_left_frac, buf_size;

    if (!(hq_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_FROZEN)) {
        mix_rate          = hq_mixer_data->mix_rate;
        current_left      = hq_mixer_data->current_left;
        current_left_frac = hq_mixer_data->current_left_frac;
        buf_size          = hq_mixer_data->buf_size;

        memset(buf, 0, buf_size << ((hq_mixer_data->channels_out >= 2) ? 3 : 2));

        while (buf_size) {
            if (current_left) {
                uint32_t mix_len = buf_size;

                if (buf_size > current_left)
                    mix_len = current_left;

                current_left -= mix_len;
                buf_size     -= mix_len;

                mix_sample_parallel(hq_mixer_data, buf, mix_len, first_channel, last_channel);

                buf += (hq_mixer_data->channels_out >= 2) ? mix_len << 1 : mix_len;
            }

            if (current_left)
                continue;

            if (mixer_data->handler)
                mixer_data->handler(mixer_data);

            current_left       = hq_mixer_data->pass_len;
            current_left_frac += hq_mixer_data->pass_len_frac;

            if (current_left_frac < hq_mixer_data->pass_len_frac)
                current_left++;
        }

        hq_mixer_data->current_left      = current_left;
        hq_mixer_data->current_left_frac = current_left_frac;
    }

    // TODO: Execute post-processing step in libavfilter and pass the PCM data.
}

AVMixerContext high_quality_mixer = {
    .av_class                          = &avseq_high_quality_mixer_class,
    .name                              = "High quality mixer",
    .description                       = NULL_IF_CONFIG_SMALL("Optimized for quality and supports advanced interpolation."),

    .flags                             = AVSEQ_MIXER_CONTEXT_FLAG_SURROUND|AVSEQ_MIXER_CONTEXT_FLAG_AVFILTER,
    .frequency                         = 44100,
    .frequency_min                     = 1000,
    .frequency_max                     = 768000,
    .buf_size                          = 512,
    .buf_size_min                      = 64,
    .buf_size_max                      = 32768,
    .volume_boost                      = 0x10000,
    .channels_in                       = 65535,
    .channels_out                      = 2,

    .init                              = init,
    .uninit                            = uninit,
    .set_rate                          = set_rate,
    .set_tempo                         = set_tempo,
    .set_volume                        = set_volume,
    .get_channel                       = get_channel,
    .set_channel                       = set_channel,
    .reset_channel                     = reset_channel,
    .get_both_channels                 = get_both_channels,
    .set_both_channels                 = set_both_channels,
    .set_channel_volume_panning_pitch  = set_channel_volume_panning_pitch,
    .set_channel_position_repeat_flags = set_channel_position_repeat_flags,
    .set_channel_filter                = set_channel_filter,
    .mix                               = mix,
    .mix_parallel                      = mix_parallel,
};

#endif /* CONFIG_HIGH_QUALITY_MIXER */
