/*
 * Sequencer low quality integer mixer
 * Copyright (c) 2010 Sebastian Vater <cdgs.basty@googlemail.com>
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
 * Sequencer low quality integer mixer.
 */

#include "libavcodec/avcodec.h"
#include "libavutil/avstring.h"
#include "libavsequencer/mixer.h"

typedef struct AV_LQMixerData {
    AVMixerData mixer_data;
    int32_t *buf;
    int32_t *filter_buf;
    uint32_t buf_size;
    uint32_t mix_buf_size;
    int32_t *volume_lut;
    struct AV_LQMixerChannelInfo *channel_info;
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
} AV_LQMixerData;

typedef struct AV_LQMixerChannelInfo {
    struct ChannelBlock {
        const int16_t *data;
        uint32_t len;
        uint32_t offset;
        uint32_t fraction;
        uint32_t offset_one_shoot;
        uint32_t advance;
        uint32_t advance_frac;
        void (*mix_func)(const AV_LQMixerData *const mixer_data,
                         const struct ChannelBlock *const channel_block,
                         int32_t **const buf,
                         uint32_t *const offset,
                         uint32_t *const fraction,
                         const uint32_t advance,
                         const uint32_t adv_frac,
                         const uint32_t len);
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
        uint32_t mult_right_volume;
        int32_t filter_c1;
        int32_t filter_c2;
        int32_t filter_c3;
        void (*mix_backwards_func)(const AV_LQMixerData *const mixer_data,
                                   const struct ChannelBlock *const channel_block,
                                   int32_t **const buf,
                                   uint32_t *const offset,
                                   uint32_t *const fraction,
                                   const uint32_t advance,
                                   const uint32_t adv_frac,
                                   const uint32_t len);
        uint8_t bits_per_sample;
        uint8_t flags;
        uint8_t volume;
        uint8_t panning;
        uint16_t filter_cutoff;
        uint16_t filter_damping;
    } current;
    struct ChannelBlock next;
    int32_t filter_tmp1;
    int32_t filter_tmp2;
} AV_LQMixerChannelInfo;

#if CONFIG_LOW_QUALITY_MIXER
static const char *low_quality_mixer_name(void *p)
{
    AVMixerContext *mixctx = p;

    return mixctx->name;
}

static const AVClass avseq_low_quality_mixer_class = {
    "AVSequencer Low Quality Mixer",
    low_quality_mixer_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

static void apply_filter(AV_LQMixerChannelInfo *const channel_info,
                         struct ChannelBlock *const channel_block,
                         int32_t **const dest_buf,
                         const int32_t *src_buf,
                         const uint32_t len)
{
    int32_t *mix_buf = *dest_buf;
    uint32_t i       = len >> 2;
    const int64_t c1 = channel_block->filter_c1;
    const int64_t c2 = channel_block->filter_c2;
    const int64_t c3 = channel_block->filter_c3;
    int32_t o1       = channel_info->filter_tmp2;
    int32_t o2       = channel_info->filter_tmp1;
    int32_t o3, o4;

    while (i--) {
        mix_buf[0] += o3 = ((c1 * src_buf[0]) + (c2 * o2) + (c3 * o1)) >> 24;
        mix_buf[1] += o4 = ((c1 * src_buf[1]) + (c2 * o3) + (c3 * o2)) >> 24;
        mix_buf[2] += o1 = ((c1 * src_buf[2]) + (c2 * o4) + (c3 * o3)) >> 24;
        mix_buf[3] += o2 = ((c1 * src_buf[3]) + (c2 * o1) + (c3 * o4)) >> 24;
        src_buf    += 4;
        mix_buf    += 4;
    }

    i = len & 3;

    while (i--) {
        *mix_buf++ += o3 = ((c1 * *src_buf++) + (c2 * o2) + (c3 * o1)) >> 24;
        o1          = o2;
        o2          = o3;
    }

    *dest_buf                 = mix_buf;
    channel_info->filter_tmp1 = o2;
    channel_info->filter_tmp2 = o1;
}

static void mix_sample(AV_LQMixerData *const mixer_data,
                       int32_t *const buf, const uint32_t len)
{
    AV_LQMixerChannelInfo *channel_info = mixer_data->channel_info;
    uint16_t i                          = mixer_data->channels_in;

    do {
        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            void (*mix_func)(const AV_LQMixerData *const mixer_data,
                             const struct ChannelBlock *const channel_block,
                             int32_t **const buf,
                             uint32_t *const offset,
                             uint32_t *const fraction,
                             const uint32_t advance,
                             const uint32_t adv_frac,
                             const uint32_t len) = channel_info->current.mix_func;
            int32_t *mix_buf        = buf;
            uint32_t offset         = channel_info->current.offset;
            uint32_t fraction       = channel_info->current.fraction;
            const uint32_t advance  = channel_info->current.advance;
            const uint32_t adv_frac = channel_info->current.advance_frac;
            uint32_t remain_len     = len, remain_mix;
            uint64_t calc_mix;

            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
mix_sample_backwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = offset - channel_info->current.end_offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            if ((channel_info->current.filter_cutoff == 4095) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, remain_len);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = remain_len;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, remain_len);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if ((int32_t) offset <= (int32_t) channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            if ((channel_info->current.filter_cutoff == 4095) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = (uint32_t) calc_mix;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
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
                                void (*mixer_change_func)(const AV_LQMixerData *const mixer_data,
                                                          const struct ChannelBlock *const channel_block,
                                                          int32_t **const buf,
                                                          uint32_t *const offset,
                                                          uint32_t *const fraction,
                                                          const uint32_t advance,
                                                          const uint32_t adv_frac,
                                                          const uint32_t len);

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
                            if ((channel_info->current.filter_cutoff == 4095) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, remain_len);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = remain_len;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, remain_len);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if (offset >= channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            if ((channel_info->current.filter_cutoff == 4095) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = (uint32_t) calc_mix;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
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
                                void (*mixer_change_func)(const AV_LQMixerData *const mixer_data,
                                                          const struct ChannelBlock *const channel_block,
                                                          int32_t **const buf,
                                                          uint32_t *const offset,
                                                          uint32_t *const fraction,
                                                          const uint32_t advance,
                                                          const uint32_t adv_frac,
                                                          const uint32_t len);

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

            channel_info->current.offset_one_shoot += (offset > channel_info->current.offset) ? (offset - channel_info->current.offset) : (channel_info->current.offset - offset);

            if (channel_info->current.offset_one_shoot >= channel_info->current.len) {
                channel_info->current.offset_one_shoot = channel_info->current.len;

                if (!(channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP))
                    channel_info->current.flags &= ~AVSEQ_MIXER_CHANNEL_FLAG_PLAY;
            }

            channel_info->current.offset   = offset;
            channel_info->current.fraction = fraction;
        }

        channel_info++;
    } while (--i);
}

static void mix_sample_parallel(AV_LQMixerData *const mixer_data,
                                int32_t *const buf,
                                const uint32_t len,
                                const uint32_t first_channel,
                                const uint32_t last_channel)
{
    AV_LQMixerChannelInfo *channel_info = mixer_data->channel_info + first_channel;
    uint16_t i                          = (last_channel - first_channel) + 1;

    do {
        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            void (*mix_func)(const AV_LQMixerData *const mixer_data,
                             const struct ChannelBlock *const channel_block,
                             int32_t **const buf,
                             uint32_t *const offset,
                             uint32_t *const fraction,
                             const uint32_t advance,
                             const uint32_t adv_frac,
                             const uint32_t len) = channel_info->current.mix_func;
            int32_t *mix_buf        = buf;
            uint32_t offset         = channel_info->current.offset;
            uint32_t fraction       = channel_info->current.fraction;
            const uint32_t advance  = channel_info->current.advance;
            const uint32_t adv_frac = channel_info->current.advance_frac;
            uint32_t remain_len     = len, remain_mix;
            uint64_t calc_mix;

            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
mix_sample_backwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = offset - channel_info->current.end_offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            if ((channel_info->current.filter_cutoff == 4095) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, remain_len);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = remain_len;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, remain_len);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if ((int32_t) offset <= (int32_t) channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            if ((channel_info->current.filter_cutoff == 4095) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = (uint32_t) calc_mix;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
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
                                void (*mixer_change_func)(const AV_LQMixerData *const mixer_data,
                                                          const struct ChannelBlock *const channel_block,
                                                          int32_t **const buf,
                                                          uint32_t *const offset,
                                                          uint32_t *const fraction,
                                                          const uint32_t advance,
                                                          const uint32_t adv_frac,
                                                          const uint32_t len);

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
                            if ((channel_info->current.filter_cutoff == 4095) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, remain_len);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = remain_len;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, remain_len);
                                apply_filter(channel_info, &channel_info->current, &mix_buf, mixer_data->filter_buf, filter_len);
                            }

                            if (offset >= channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            if ((channel_info->current.filter_cutoff == 4095) && (channel_info->current.filter_damping == 0)) {
                                mix_func(mixer_data, &channel_info->current, &mix_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
                            } else {
                                int32_t *filter_buf = mixer_data->filter_buf;
                                uint32_t filter_len = (uint32_t) calc_mix;

                                if (mixer_data->channels_out >= 2)
                                    filter_len <<= 1;

                                memset(filter_buf, 0, filter_len * sizeof(int32_t));
                                mix_func(mixer_data, &channel_info->current, &filter_buf, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);
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
                                void (*mixer_change_func)(const AV_LQMixerData *const mixer_data,
                                                          const struct ChannelBlock *const channel_block,
                                                          int32_t **const buf,
                                                          uint32_t *const offset,
                                                          uint32_t *const fraction,
                                                          const uint32_t advance,
                                                          const uint32_t adv_frac,
                                                          const uint32_t len);

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

            channel_info->current.offset_one_shoot += (offset > channel_info->current.offset) ? (offset - channel_info->current.offset) : (channel_info->current.offset - offset);

            if (channel_info->current.offset_one_shoot >= channel_info->current.len) {
                channel_info->current.offset_one_shoot = channel_info->current.len;

                if (!(channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP))
                    channel_info->current.flags &= ~AVSEQ_MIXER_CHANNEL_FLAG_PLAY;
            }

            channel_info->current.offset   = offset;
            channel_info->current.fraction = fraction;
        }

        channel_info++;
    } while (--i);
}

#define MIX_FUNCTION(INIT, TYPE, OP, OFFSET_START, OFFSET_END, SKIP,      \
                     NEXTS, NEXTA, NEXTI, SHIFTS, SHIFTN, SHIFTB)         \
    const TYPE *sample              = (const TYPE *) channel_block->data; \
    int32_t *mix_buf                = *buf;                               \
    uint32_t curr_offset            = *offset;                            \
    uint32_t curr_frac              = *fraction;                          \
    uint32_t i;                                                           \
    INIT;                                                                 \
                                                                          \
    if (advance) {                                                        \
        if (mixer_data->interpolation) {                                  \
            int32_t smp;                                                  \
            int32_t interpolate_div;                                      \
                                                                          \
            OFFSET_START;                                                 \
            i       = (len >> 1) + 1;                                     \
                                                                          \
            if (len & 1)                                                  \
                goto get_second_advance_interpolated_sample;              \
                                                                          \
            i--;                                                          \
                                                                          \
            do {                                                          \
                uint32_t interpolate_offset;                              \
                                                                          \
                curr_frac          += adv_frac;                           \
                interpolate_offset  = advance + (curr_frac < adv_frac);   \
                smp                 = 0;                                  \
                interpolate_div     = 0;                                  \
                                                                          \
                do {                                                      \
                    NEXTA;                                                \
                    interpolate_div++;                                    \
                } while (--interpolate_offset);                           \
                                                                          \
                smp        /= interpolate_div;                            \
                SHIFTS;                                                   \
get_second_advance_interpolated_sample:                                   \
                curr_frac          += adv_frac;                           \
                interpolate_offset  = advance + (curr_frac < adv_frac);   \
                smp                 = 0;                                  \
                interpolate_div     = 0;                                  \
                                                                          \
                do {                                                      \
                    NEXTA;                                                \
                    interpolate_div++;                                    \
                } while (--interpolate_offset);                           \
                                                                          \
                smp        /= interpolate_div;                            \
                SHIFTS;                                                   \
            } while (--i);                                                \
                                                                          \
            *buf      = mix_buf;                                          \
            OFFSET_END;                                                   \
            *fraction = curr_frac;                                        \
        } else {                                                          \
            i = (len >> 1) + 1;                                           \
                                                                          \
            if (len & 1)                                                  \
                goto get_second_advance_sample;                           \
                                                                          \
            i--;                                                          \
                                                                          \
            do {                                                          \
                SKIP;                                                     \
                curr_frac   += adv_frac;                                  \
                curr_offset OP advance + (curr_frac < adv_frac);          \
get_second_advance_sample:                                                \
                SKIP;                                                     \
                curr_frac   += adv_frac;                                  \
                curr_offset OP advance + (curr_frac < adv_frac);          \
            } while (--i);                                                \
                                                                          \
            *buf      = mix_buf;                                          \
            *offset   = curr_offset;                                      \
            *fraction = curr_frac;                                        \
        }                                                                 \
    } else {                                                              \
        int32_t smp;                                                      \
                                                                          \
        if (mixer_data->interpolation > 1) {                              \
            uint32_t interpolate_frac, interpolate_count;                 \
            int32_t interpolate_div;                                      \
            int64_t smp_value;                                            \
                                                                          \
            OFFSET_START;                                                 \
            NEXTS;                                                        \
            smp_value = 0;                                                \
                                                                          \
            if (len > 1) {                                                \
                NEXTI;                                                    \
            }                                                             \
                                                                          \
            interpolate_div   = smp_value >> 32;                          \
            interpolate_frac  = smp_value;                                \
            interpolate_count = 0;                                        \
                                                                          \
            i = (len >> 1) + 1;                                           \
                                                                          \
            if (len & 1)                                                  \
                goto get_second_interpolated_sample;                      \
                                                                          \
            i--;                                                          \
                                                                          \
            do {                                                          \
                SHIFTS;                                                   \
                curr_frac  += adv_frac;                                   \
                                                                          \
                if (curr_frac < adv_frac) {                               \
                    NEXTS;                                                \
                    NEXTI;                                                \
                                                                          \
                    interpolate_div   = smp_value >> 32;                  \
                    interpolate_frac  = smp_value;                        \
                    interpolate_count = 0;                                \
                } else {                                                  \
                    smp               += interpolate_div;                 \
                    interpolate_count += interpolate_frac;                \
                                                                          \
                    if (interpolate_count < interpolate_frac) {           \
                        smp++;                                            \
                                                                          \
                        if (interpolate_div < 0)                          \
                            smp -= 2;                                     \
                    }                                                     \
                }                                                         \
get_second_interpolated_sample:                                           \
                SHIFTS;                                                   \
                curr_frac  += adv_frac;                                   \
                                                                          \
                if (curr_frac < adv_frac) {                               \
                    NEXTS;                                                \
                    smp_value = 0;                                        \
                                                                          \
                    if (i > 1) {                                          \
                        NEXTI;                                            \
                    }                                                     \
                                                                          \
                    interpolate_div   = smp_value >> 32;                  \
                    interpolate_frac  = smp_value;                        \
                    interpolate_count = 0;                                \
                } else {                                                  \
                    smp               += interpolate_div;                 \
                    interpolate_count += interpolate_frac;                \
                                                                          \
                    if (interpolate_count < interpolate_frac) {           \
                        smp++;                                            \
                                                                          \
                        if (interpolate_div < 0)                          \
                            smp -= 2;                                     \
                    }                                                     \
                }                                                         \
            } while (--i);                                                \
                                                                          \
            *buf      = mix_buf;                                          \
            OFFSET_END;                                                   \
            *fraction = curr_frac;                                        \
        } else {                                                          \
            OFFSET_START;                                                 \
            SHIFTN;                                                       \
            i       = (len >> 1) + 1;                                     \
                                                                          \
            if (len & 1)                                                  \
                goto get_second_sample;                                   \
                                                                          \
            i--;                                                          \
                                                                          \
            do {                                                          \
                SHIFTB;                                                   \
                curr_frac  += adv_frac;                                   \
                                                                          \
                if (curr_frac < adv_frac) {                               \
                    SHIFTN;                                               \
                }                                                         \
get_second_sample:                                                        \
                SHIFTB;                                                   \
                curr_frac  += adv_frac;                                   \
                                                                          \
                if (curr_frac < adv_frac) {                               \
                    SHIFTN;                                               \
                }                                                         \
            } while (--i);                                                \
                                                                          \
            *buf      = mix_buf;                                          \
            OFFSET_END;                                                   \
            *fraction = curr_frac;                                        \
        }                                                                 \
    }

#define MIX(type)                                                                                   \
    static void mix_##type(const AV_LQMixerData *const mixer_data,                                  \
                           const struct ChannelBlock *const channel_block,                          \
                           int32_t **const buf, uint32_t *const offset, uint32_t *const fraction,   \
                           const uint32_t advance, const uint32_t adv_frac, const uint32_t len)

MIX(skip)
{
    uint32_t curr_offset    = *offset, curr_frac = *fraction, skip_frac;
    const uint64_t skip_len = (((uint64_t) advance << 32) + adv_frac) * len;

    curr_offset += skip_len >> 32;
    skip_frac    = skip_len;
    curr_frac   += skip_frac;
    curr_offset += (curr_frac < skip_frac);
    *offset      = curr_offset;
    *fraction    = curr_frac;
}

MIX(skip_backwards)
{
    uint32_t curr_offset    = *offset, curr_frac = *fraction, skip_frac;
    const uint64_t skip_len = (((uint64_t) advance << 32) + adv_frac) * len;

    curr_offset -= skip_len >> 32;
    skip_frac    = skip_len;
    curr_frac   += skip_frac;
    curr_offset -= (curr_frac < skip_frac);
    *offset      = curr_offset;
    *fraction    = curr_frac;
}

MIX(mono_8)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int8_t *const pos = sample,
                 int8_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += volume_lut[(uint8_t) sample[curr_offset]],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_lut[(uint8_t) smp],
                 smp = volume_lut[(uint8_t) *sample++],
                 *mix_buf++ += smp)
}

MIX(mono_backwards_8)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int8_t *const pos = sample,
                 int8_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += volume_lut[(uint8_t) sample[curr_offset]],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_lut[(uint8_t) smp],
                 smp = volume_lut[(uint8_t) *--sample],
                 *mix_buf++ += smp)
}

MIX(mono_16_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += volume_lut[(uint16_t) sample[curr_offset] >> 8],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_lut[(uint16_t) smp >> 8],
                 smp = volume_lut[(uint16_t) *sample++ >> 8],
                 *mix_buf++ += smp)
}

MIX(mono_backwards_16_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += volume_lut[(uint16_t) sample[curr_offset] >> 8],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_lut[(uint16_t) smp >> 8],
                 smp = volume_lut[(uint16_t) *--sample >> 8],
                 *mix_buf++ += smp)
}

MIX(mono_32_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += volume_lut[(uint32_t) sample[curr_offset] >> 24],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_lut[(uint32_t) smp >> 24],
                 smp = volume_lut[(uint32_t) *sample++ >> 24],
                 *mix_buf++ += smp)
}

MIX(mono_backwards_32_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += volume_lut[(uint32_t) sample[curr_offset] >> 24],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_lut[(uint32_t) smp >> 24],
                 smp = volume_lut[(uint32_t) *--sample >> 24],
                 *mix_buf++ += smp)
}

MIX(mono_x_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    *mix_buf++ += volume_lut[(uint32_t) smp_data >> 24],
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_lut[(uint32_t) smp >> 24],
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24],
                 *mix_buf++ += smp)
}

MIX(mono_backwards_x_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    *mix_buf++ += volume_lut[(uint32_t) smp_data >> 24],
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_lut[(uint32_t) smp >> 24],
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24],
                 *mix_buf++ += smp)
}

MIX(mono_16)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += ((int64_t) sample[curr_offset] * mult_left_volume) >> 16,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 16,
                 smp = ((int64_t) *sample++ * mult_left_volume) >> 16,
                 *mix_buf++ += smp)
}

MIX(mono_backwards_16)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int16_t *const pos = sample, 
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += ((int64_t) sample[curr_offset] * mult_left_volume) >> 16,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 16,
                 smp = ((int64_t) *--sample * mult_left_volume) >> 16,
                 *mix_buf++ += smp)
}

MIX(mono_32)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += ((int64_t) sample[curr_offset] * mult_left_volume) >> 32,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 32,
                 smp = ((int64_t) *sample++ * mult_left_volume) >> 32,
                 *mix_buf++ += smp)
}

MIX(mono_backwards_32)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf++ += ((int64_t) sample[curr_offset] * mult_left_volume) >> 32,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 32,
                 smp = ((int64_t) *--sample * mult_left_volume) >> 32,
                 *mix_buf++ += smp)
}

MIX(mono_x)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    *mix_buf++ += ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 32,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                 *mix_buf++ += smp)
}

MIX(mono_backwards_x)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    *mix_buf++ += ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 32,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                 *mix_buf++ += smp)
}

MIX(stereo_8)
{
    MIX_FUNCTION(const int32_t *const volume_left_lut = channel_block->volume_left_lut; const int32_t *const volume_right_lut = channel_block->volume_right_lut; int8_t smp_in; int32_t smp_right; const int8_t *const pos = sample,
                 int8_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = sample[curr_offset]; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_left_lut[(uint8_t) smp]; *mix_buf++ += volume_right_lut[(uint8_t) smp],
                 smp_in = *sample++; smp = volume_left_lut[(uint8_t) smp_in]; smp_right = volume_right_lut[(uint8_t) smp_in],
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_backwards_8)
{
    MIX_FUNCTION(const int32_t *const volume_left_lut = channel_block->volume_left_lut; const int32_t *const volume_right_lut = channel_block->volume_right_lut; int8_t smp_in; int32_t smp_right; const int8_t *const pos = sample,
                 int8_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = sample[curr_offset]; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf++ += volume_left_lut[(uint8_t) smp]; *mix_buf++ += volume_right_lut[(uint8_t) smp],
                 smp_in = *--sample; smp = volume_left_lut[(uint8_t) smp_in]; smp_right = volume_right_lut[(uint8_t) smp_in],
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_16_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_left_lut = channel_block->volume_left_lut; const int32_t *const volume_right_lut = channel_block->volume_right_lut; int16_t smp_in; int32_t smp_right; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = (uint16_t) sample[curr_offset] >> 8; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = (uint16_t) smp >> 8; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp_in = (uint16_t) *sample++ >> 8; smp = volume_left_lut[(uint8_t) smp_in]; smp_right = volume_right_lut[(uint8_t) smp_in],
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_backwards_16_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_left_lut = channel_block->volume_left_lut; const int32_t *const volume_right_lut = channel_block->volume_right_lut; int16_t smp_in; int32_t smp_right; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = (uint16_t) sample[curr_offset] >> 8; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = (uint16_t) smp >> 8; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp_in = (uint16_t) *--sample >> 8; smp = volume_left_lut[(uint8_t) smp_in]; smp_right = volume_right_lut[(uint8_t) smp_in],
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_32_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_left_lut = channel_block->volume_left_lut; const int32_t *const volume_right_lut = channel_block->volume_right_lut; int32_t smp_in; int32_t smp_right; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = (uint32_t) sample[curr_offset] >> 24; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = (uint32_t) smp >> 24; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp_in = (uint32_t) *sample++ >> 24; smp = volume_left_lut[(uint8_t) smp_in]; smp_right = volume_right_lut[(uint8_t) smp_in],
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_backwards_32_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_left_lut = channel_block->volume_left_lut; const int32_t *const volume_right_lut = channel_block->volume_right_lut; int32_t smp_in; int32_t smp_right; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = (uint32_t) sample[curr_offset] >> 24; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = (uint32_t) smp >> 24; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                 smp_in = (uint32_t) *--sample >> 24; smp = volume_left_lut[(uint8_t) smp_in]; smp_right = volume_right_lut[(uint8_t) smp_in],
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_x_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_left_lut = channel_block->volume_left_lut; const int32_t *const volume_right_lut = channel_block->volume_right_lut; int32_t smp_in; int32_t smp_right; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = (uint32_t) smp_data >> 24;
                    *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                    *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = (uint32_t) smp >> 24; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp_in    = (uint32_t) smp_data >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in],
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_backwards_x_to_8)
{
    MIX_FUNCTION(const int32_t *const volume_left_lut = channel_block->volume_left_lut; const int32_t *const volume_right_lut = channel_block->volume_right_lut; int32_t smp_in; int32_t smp_right; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = (uint32_t) smp_data >> 24;
                    *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                    *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = (uint32_t) smp >> 24; *mix_buf++ += volume_left_lut[(uint8_t) smp_in]; *mix_buf++ += volume_right_lut[(uint8_t) smp_in],
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in    = (uint32_t) smp_data >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in],
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_16)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t mult_right_volume = channel_block->mult_right_volume; int16_t smp_in; int32_t smp_right; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = sample[curr_offset]; *mix_buf++ += ((int64_t) smp_in * mult_left_volume) >> 16; *mix_buf++ += ((int64_t) smp_in * mult_right_volume) >> 16,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 16; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 16,
                 smp_in = *sample++; smp = ((int64_t) smp_in * mult_left_volume) >> 16; smp_right = ((int64_t) smp_in * mult_right_volume) >> 16,
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_backwards_16)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t mult_right_volume = channel_block->mult_right_volume; int16_t smp_in; int32_t smp_right; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = sample[curr_offset]; *mix_buf++ += ((int64_t) smp_in * mult_left_volume) >> 16; *mix_buf++ += ((int64_t) smp_in * mult_right_volume) >> 16,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 16; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 16,
                 smp_in = *--sample; smp = ((int64_t) smp_in * mult_left_volume) >> 16; smp_right = ((int64_t) smp_in * mult_right_volume) >> 16,
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_32)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t mult_right_volume = channel_block->mult_right_volume; int32_t smp_in; int32_t smp_right; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = sample[curr_offset]; *mix_buf++ += ((int64_t) smp_in * mult_left_volume) >> 32; *mix_buf++ += ((int64_t) smp_in * mult_right_volume) >> 32,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 32,
                 smp_in = *sample++; smp = ((int64_t) smp_in * mult_left_volume) >> 32; smp_right = ((int64_t) smp_in * mult_right_volume) >> 32,
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_backwards_32)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t mult_right_volume = channel_block->mult_right_volume; int32_t smp_in; int32_t smp_right; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = sample[curr_offset]; *mix_buf++ += ((int64_t) smp_in * mult_left_volume) >> 32; *mix_buf++ += ((int64_t) smp_in * mult_right_volume) >> 32,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 32,
                 smp_in = *--sample; smp = ((int64_t) smp_in * mult_left_volume) >> 32; smp_right = ((int64_t) smp_in * mult_right_volume) >> 32,
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_x)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t mult_right_volume = channel_block->mult_right_volume; int32_t smp_in; int32_t smp_right; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = smp_data;
                    *mix_buf++ += ((int64_t) smp_in * mult_left_volume) >> 32;
                    *mix_buf++ += ((int64_t) smp_in * mult_right_volume) >> 32,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 32,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp_in    = smp_data;
                    smp       = ((int64_t) smp_in * mult_left_volume) >> 32;
                    smp_right = ((int64_t) smp_in * mult_right_volume) >> 32,
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_backwards_x)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t mult_right_volume = channel_block->mult_right_volume; int32_t smp_in; int32_t smp_right; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = smp_data;
                    *mix_buf++ += ((int64_t) smp_in * mult_left_volume) >> 32;
                    *mix_buf++ += ((int64_t) smp_in * mult_right_volume) >> 32,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf++ += ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 32,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in    = smp_data;
                    smp       = ((int64_t) smp_in * mult_left_volume) >> 32;
                    smp_right = ((int64_t) smp_in * mult_right_volume) >> 32,
                 *mix_buf++ += smp; *mix_buf++ += smp_right)
}

MIX(stereo_8_left)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int8_t *const pos = sample,
                 int8_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += volume_lut[(uint8_t) sample[curr_offset]]; mix_buf += 2,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf += volume_lut[(uint8_t) smp]; mix_buf += 2,
                 smp = volume_lut[(uint8_t) *sample++],
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_backwards_8_left)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int8_t *const pos = sample,
                 int8_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += volume_lut[(uint8_t) sample[curr_offset]]; mix_buf += 2,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf += volume_lut[(uint8_t) smp]; mix_buf += 2,
                 smp = volume_lut[(uint8_t) *--sample],
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_16_to_8_left)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += volume_lut[(uint16_t) sample[curr_offset] >> 8]; mix_buf += 2,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf += volume_lut[(uint16_t) smp >> 8]; mix_buf += 2,
                 smp = volume_lut[(uint16_t) *sample++ >> 8],
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_backwards_16_to_8_left)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += volume_lut[(uint16_t) sample[curr_offset] >> 8]; mix_buf += 2,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf += volume_lut[(uint16_t) smp >> 8]; mix_buf += 2,
                 smp = volume_lut[(uint16_t) *--sample >> 8],
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_32_to_8_left)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += volume_lut[(uint32_t) sample[curr_offset] >> 24]; mix_buf += 2,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf += volume_lut[(uint32_t) smp >> 24]; mix_buf += 2,
                 smp = volume_lut[(uint32_t) *sample++ >> 24],
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_backwards_32_to_8_left)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += volume_lut[(uint32_t) sample[curr_offset] >> 24]; mix_buf += 2,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf += volume_lut[(uint32_t) smp >> 24]; mix_buf += 2,
                 smp = volume_lut[(uint32_t) *--sample >> 24],
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_x_to_8_left)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    *mix_buf   += volume_lut[(uint32_t) smp_data >> 24];
                    mix_buf    += 2,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf += volume_lut[(uint32_t) smp >> 24]; mix_buf += 2,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24],
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_backwards_x_to_8_left)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    *mix_buf   += volume_lut[(uint32_t) smp_data >> 24];
                    mix_buf    += 2,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf += volume_lut[(uint32_t) smp >> 24]; mix_buf += 2,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24],
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_16_left)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += ((int64_t) sample[curr_offset] * mult_left_volume) >> 16; mix_buf += 2,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf += ((int64_t) smp * mult_left_volume) >> 16; mix_buf += 2,
                 smp = ((int64_t) *sample++ * mult_left_volume) >> 16,
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_backwards_16_left)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += ((int64_t) sample[curr_offset] * mult_left_volume) >> 16; mix_buf += 2,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf += ((int64_t) smp * mult_left_volume) >> 16; mix_buf += 2,
                 smp = ((int64_t) *--sample * mult_left_volume) >> 16,
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_32_left)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += ((int64_t) sample[curr_offset] * mult_left_volume) >> 32; mix_buf += 2,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 *mix_buf += ((int64_t) smp * mult_left_volume) >> 32; mix_buf += 2,
                 smp = ((int64_t) *sample++ * mult_left_volume) >> 32,
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_backwards_32_left)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 *mix_buf += ((int64_t) sample[curr_offset] * mult_left_volume) >> 32; mix_buf += 2,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 *mix_buf += ((int64_t) smp * mult_left_volume) >> 32; mix_buf += 2,
                 smp = ((int64_t) *--sample * mult_left_volume) >> 32,
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_x_left)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    *mix_buf   += ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32;
                    mix_buf    += 2,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf += ((int64_t) smp * mult_left_volume) >> 32; mix_buf += 2,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_backwards_x_left)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    *mix_buf   += ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32;
                    mix_buf    += 2,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 *mix_buf += ((int64_t) smp * mult_left_volume) >> 32; mix_buf += 2,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                 *mix_buf += smp; mix_buf += 2)
}

MIX(stereo_8_right)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_right_lut; const int8_t *const pos = sample,
                 int8_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += volume_lut[(uint8_t) sample[curr_offset]],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += volume_lut[(uint8_t) smp],
                 smp = volume_lut[(uint8_t) *sample++],
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_backwards_8_right)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_right_lut; const int8_t *const pos = sample,
                 int8_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += volume_lut[(uint8_t) sample[curr_offset]],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += volume_lut[(uint8_t) smp],
                 smp = volume_lut[(uint8_t) *--sample],
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_16_to_8_right)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_right_lut; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += volume_lut[(uint16_t) sample[curr_offset] >> 8],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += volume_lut[(uint16_t) smp >> 8],
                 smp = volume_lut[(uint16_t) *sample++ >> 8],
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_backwards_16_to_8_right)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_right_lut; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += volume_lut[(uint16_t) sample[curr_offset] >> 8],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += volume_lut[(uint16_t) smp >> 8],
                 smp = volume_lut[(uint16_t) *--sample >> 8],
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_32_to_8_right)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_right_lut; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += volume_lut[(uint32_t) sample[curr_offset] >> 24],
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += volume_lut[(uint32_t) smp >> 24],
                 smp = volume_lut[(uint32_t) *sample++ >> 24],
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_backwards_32_to_8_right)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_right_lut; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += volume_lut[(uint32_t) sample[curr_offset] >> 24],
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += volume_lut[(uint32_t) smp >> 24],
                 smp = volume_lut[(uint32_t) *--sample >> 24],
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_x_to_8_right)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_right_lut; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    mix_buf++;
                    *mix_buf++ += volume_lut[(uint32_t) smp_data >> 24],
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += volume_lut[(uint32_t) smp >> 24],
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24],
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_backwards_x_to_8_right)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_right_lut; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    mix_buf++;
                    *mix_buf++ += volume_lut[(uint32_t) smp_data >> 24],
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += volume_lut[(uint32_t) smp >> 24],
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24],
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_16_right)
{
    MIX_FUNCTION(const int32_t mult_right_volume = channel_block->mult_right_volume; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += ((int64_t) sample[curr_offset] * mult_right_volume) >> 16,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 16,
                 smp = ((int64_t) *sample++ * mult_right_volume) >> 16,
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_backwards_16_right)
{
    MIX_FUNCTION(const int32_t mult_right_volume = channel_block->mult_right_volume; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += ((int64_t) sample[curr_offset] * mult_right_volume) >> 16,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 16,
                 smp = ((int64_t) *--sample * mult_right_volume) >> 16,
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_32_right)
{
    MIX_FUNCTION(const int32_t mult_right_volume = channel_block->mult_right_volume; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += ((int64_t) sample[curr_offset] * mult_right_volume) >> 32,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 32,
                 smp = ((int64_t) *sample++ * mult_right_volume) >> 32,
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_backwards_32_right)
{
    MIX_FUNCTION(const int32_t mult_right_volume = channel_block->mult_right_volume; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 mix_buf++; *mix_buf++ += ((int64_t) sample[curr_offset] * mult_right_volume) >> 32,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 32,
                 smp = ((int64_t) *--sample * mult_right_volume) >> 32,
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_x_right)
{
    MIX_FUNCTION(const int32_t mult_right_volume = channel_block->mult_right_volume; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    mix_buf++;
                    *mix_buf++ += ((int64_t) ((int32_t) smp_data) * mult_right_volume) >> 32,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 32,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) ((int32_t) smp_data) * mult_right_volume) >> 32,
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_backwards_x_right)
{
    MIX_FUNCTION(const int32_t mult_right_volume = channel_block->mult_right_volume; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    mix_buf++;
                    *mix_buf++ += ((int64_t) ((int32_t) smp_data) * mult_right_volume) >> 32,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 mix_buf++; *mix_buf++ += ((int64_t) smp * mult_right_volume) >> 32,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) ((int32_t) smp_data) * mult_right_volume) >> 32,
                 mix_buf++; *mix_buf++ += smp)
}

MIX(stereo_8_center)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int8_t *const pos = sample,
                 int8_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint8_t) sample[curr_offset]]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint8_t) smp]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = volume_lut[(uint8_t) *sample++],
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_backwards_8_center)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int8_t *const pos = sample,
                 int8_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint8_t) sample[curr_offset]]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint8_t) smp]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = volume_lut[(uint8_t) *--sample],
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_16_to_8_center)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint16_t) sample[curr_offset] >> 8]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint16_t) smp >> 8]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = volume_lut[(uint16_t) *sample++ >> 8],
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_backwards_16_to_8_center)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint16_t) sample[curr_offset] >> 8]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint16_t) smp >> 8]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = volume_lut[(uint16_t) *--sample >> 8],
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_32_to_8_center)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint32_t) sample[curr_offset] >> 24]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint32_t) smp >> 24]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = volume_lut[(uint32_t) *sample++ >> 24],
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_backwards_32_to_8_center)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint32_t) sample[curr_offset] >> 24]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint32_t) smp >> 24]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = volume_lut[(uint32_t) *--sample >> 24],
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_x_to_8_center)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = volume_lut[(uint32_t) smp_data >> 24];
                    *mix_buf++ += smp_in;
                    *mix_buf++ += smp_in,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint32_t) smp >> 24]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit    += bits_per_sample;
                    curr_offset++;
                    smp_in  = volume_lut[(uint32_t) smp_data >> 24],
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_backwards_x_to_8_center)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = volume_lut[(uint32_t) smp_data >> 24];
                    *mix_buf++ += smp_in;
                    *mix_buf++ += smp_in,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint32_t) smp >> 24]; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in = volume_lut[(uint32_t) smp_data >> 24],
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_16_center)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = ((int64_t) sample[curr_offset] * mult_left_volume) >> 16; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 16; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = ((int64_t) *sample++ * mult_left_volume) >> 16,
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_backwards_16_center)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = ((int64_t) sample[curr_offset] * mult_left_volume) >> 16; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 16; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = ((int64_t) *--sample * mult_left_volume) >> 16,
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_32_center)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = ((int64_t) sample[curr_offset] * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = ((int64_t) *sample++ * mult_left_volume) >> 32,
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_backwards_32_center)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = ((int64_t) sample[curr_offset] * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                 smp_in = ((int64_t) *--sample * mult_left_volume) >> 32,
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_x_center)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32;
                    *mix_buf++ += smp_in;
                    *mix_buf++ += smp_in,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit   += bits_per_sample;
                    curr_offset++;
                    smp_in = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_backwards_x_center)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32;
                    *mix_buf++ += smp_in;
                    *mix_buf++ += smp_in,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += smp_in,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                 *mix_buf++ += smp_in; *mix_buf++ += smp_in)
}

MIX(stereo_8_surround)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int8_t *const pos = sample,
                 int8_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint8_t) sample[curr_offset]]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint8_t) smp]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = volume_lut[(uint8_t) *sample++],
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_backwards_8_surround)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int8_t *const pos = sample,
                 int8_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint8_t) sample[curr_offset]]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint8_t) smp]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = volume_lut[(uint8_t) *--sample],
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_16_to_8_surround)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint16_t) sample[curr_offset] >> 8]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint16_t) smp >> 8]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = volume_lut[(uint16_t) *sample++ >> 8],
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_backwards_16_to_8_surround)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint16_t) sample[curr_offset] >> 8]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint16_t) smp >> 8]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = volume_lut[(uint16_t) *--sample >> 8],
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_32_to_8_surround)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint32_t) sample[curr_offset] >> 24]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint32_t) smp >> 24]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = volume_lut[(uint32_t) *sample++ >> 24],
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_backwards_32_to_8_surround)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = volume_lut[(uint32_t) sample[curr_offset] >> 24]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint32_t) smp >> 24]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = volume_lut[(uint32_t) *--sample >> 24],
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_x_to_8_surround)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = volume_lut[(uint32_t) smp_data >> 24];
                    *mix_buf++ += smp_in;
                    *mix_buf++ += ~smp_in,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint32_t) smp >> 24]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit    += bits_per_sample;
                    curr_offset++;
                    smp_in  = volume_lut[(uint32_t) smp_data >> 24],
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_backwards_x_to_8_surround)
{
    MIX_FUNCTION(const int32_t *const volume_lut = channel_block->volume_left_lut; int32_t smp_in; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = volume_lut[(uint32_t) smp_data >> 24];
                    *mix_buf++ += smp_in;
                    *mix_buf++ += ~smp_in,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = volume_lut[(uint32_t) smp >> 24]; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in = volume_lut[(uint32_t) smp_data >> 24],
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_16_surround)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const int16_t *const pos = sample,
                 int16_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = ((int64_t) sample[curr_offset] * mult_left_volume) >> 16; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 16; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = ((int64_t) *sample++ * mult_left_volume) >> 16,
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_backwards_16_surround)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const int16_t *const pos = sample,
                 int16_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = ((int64_t) sample[curr_offset] * mult_left_volume) >> 16; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 16; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = ((int64_t) *--sample * mult_left_volume) >> 16,
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_32_surround)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const int32_t *const pos = sample,
                 int32_t, +=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = ((int64_t) sample[curr_offset] * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *sample++,
                 smp += *sample++,
                 smp_value = (*sample - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = ((int64_t) *sample++ * mult_left_volume) >> 32,
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_backwards_32_surround)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const int32_t *const pos = sample,
                 int32_t, -=,
                 sample += curr_offset,
                 *offset = (sample - pos) - 1,
                 smp_in = ((int64_t) sample[curr_offset] * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp = *--sample,
                 smp += *--sample,
                 smp_value = (*(sample-1) - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                 smp_in = ((int64_t) *--sample * mult_left_volume) >> 32,
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_x_surround)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, +=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32;
                    *mix_buf++ += smp_in;
                    *mix_buf++ += ~smp_in,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp += smp_data,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    bit   += bits_per_sample;
                    curr_offset++;
                    smp_in = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

MIX(stereo_backwards_x_surround)
{
    MIX_FUNCTION(const int32_t mult_left_volume = channel_block->mult_left_volume; int32_t smp_in; const uint32_t bits_per_sample = channel_block->bits_per_sample; uint32_t bit = curr_offset * bits_per_sample; uint32_t smp_offset = bit >> 5; uint32_t smp_data,
                 int32_t, -=,
                 sample += smp_offset,
                 *offset = curr_offset,
                    bit        = curr_offset * bits_per_sample;
                    smp_offset = bit >> 5;

                    if (((bit &= 31) + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) sample[smp_offset] << bit;
                        smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in      = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32;
                    *mix_buf++ += smp_in;
                    *mix_buf++ += ~smp_in,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = smp_data,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data,
                    smp_value = bits_per_sample;
                    bit      -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        bit &= 31;

                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *(sample-1) << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *(sample-1) << bit;
                            smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    } else {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }
                    }

                    bit       = smp_value;
                    smp_value = (smp_data - smp) * (int64_t) adv_frac,
                 smp_in = ((int64_t) smp * mult_left_volume) >> 32; *mix_buf++ += smp_in; *mix_buf++ += ~smp_in,
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample+1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in = ((int64_t) ((int32_t) smp_data) * mult_left_volume) >> 32,
                 *mix_buf++ += smp_in; *mix_buf++ += ~smp_in)
}

#define CHANNEL_PREPARE(type)                                                       \
    static void channel_prepare_##type(const AV_LQMixerData *const mixer_data,      \
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
    const uint64_t left_volume  = volume * (255 - panning);
    const uint64_t right_volume = volume * panning;
    const uint32_t channels     = mixer_data->channels_in << 16;

    channel_block->mult_left_volume  = (left_volume * (uint64_t) mixer_data->amplify * (uint64_t) mixer_data->mixer_data.volume_left) / channels;
    channel_block->mult_right_volume = (right_volume * (uint64_t) mixer_data->amplify * (uint64_t) mixer_data->mixer_data.volume_right) / channels;
}

CHANNEL_PREPARE(stereo_16_left)
{
    channel_block->mult_left_volume  = (volume * (uint64_t) mixer_data->amplify * (uint64_t) mixer_data->mixer_data.volume_left) / (mixer_data->channels_in << 8);
}

CHANNEL_PREPARE(stereo_16_right)
{
    channel_block->mult_right_volume  = (volume * (uint64_t) mixer_data->amplify * (uint64_t) mixer_data->mixer_data.volume_right) / (mixer_data->channels_in << 8);
}

CHANNEL_PREPARE(stereo_16_center)
{
    channel_block->mult_left_volume  = (volume * (uint64_t) mixer_data->amplify * (uint64_t) mixer_data->mixer_data.volume_left) / (mixer_data->channels_in << 9);
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
    channel_prepare_stereo_16_center,
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
    channel_prepare_stereo_16,
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
    channel_prepare_stereo_16_left,
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
    channel_prepare_stereo_16_right,
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
    channel_prepare_stereo_16_center,
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
    channel_prepare_stereo_16_center,
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

static void set_mix_functions(const AV_LQMixerData *const mixer_data,
                              struct ChannelBlock *const channel_block)
{
    void **mix_func;
    void (*init_mixer_func)(const AV_LQMixerData *const mixer_data,
                            struct ChannelBlock *channel_block,
                            uint32_t volume,
                            uint32_t panning);
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

static void set_sample_mix_rate(const AV_LQMixerData *const mixer_data,
                                struct ChannelBlock *const channel_block,
                                const uint32_t rate)
{
    const uint32_t mix_rate = mixer_data->mix_rate;

    channel_block->rate         = rate;
    channel_block->advance      = rate / mix_rate;
    channel_block->advance_frac = (((uint64_t) rate % mix_rate) << 32) / mix_rate;

    set_mix_functions(mixer_data, channel_block);
}

// TODO: Implement low quality mixer identification and configuration.

/** Filter natural frequency table. Value is
   16777216*(2*PI*110*(2^0.25)*2^(x/768)).  */
static const int64_t nat_freq_lut[] = {
    INT64_C( 13789545379), INT64_C( 13801996550), INT64_C( 13814458963), INT64_C( 13826932630), INT64_C( 13839417559), INT64_C( 13851913761), INT64_C( 13864421247), INT64_C( 13876940026),
    INT64_C( 13889470109), INT64_C( 13902011506), INT64_C( 13914564228), INT64_C( 13927128283), INT64_C( 13939703683), INT64_C( 13952290438), INT64_C( 13964888559), INT64_C( 13977498054),
    INT64_C( 13990118935), INT64_C( 14002751212), INT64_C( 14015394896), INT64_C( 14028049996), INT64_C( 14040716522), INT64_C( 14053394486), INT64_C( 14066083898), INT64_C( 14078784767),
    INT64_C( 14091497104), INT64_C( 14104220920), INT64_C( 14116956225), INT64_C( 14129703029), INT64_C( 14142461342), INT64_C( 14155231176), INT64_C( 14168012540), INT64_C( 14180805445),
    INT64_C( 14193609901), INT64_C( 14206425919), INT64_C( 14219253509), INT64_C( 14232092681), INT64_C( 14244943447), INT64_C( 14257805816), INT64_C( 14270679799), INT64_C( 14283565407),
    INT64_C( 14296462649), INT64_C( 14309371537), INT64_C( 14322292081), INT64_C( 14335224292), INT64_C( 14348168179), INT64_C( 14361123755), INT64_C( 14374091028), INT64_C( 14387070010),
    INT64_C( 14400060711), INT64_C( 14413063142), INT64_C( 14426077314), INT64_C( 14439103236), INT64_C( 14452140921), INT64_C( 14465190377), INT64_C( 14478251617), INT64_C( 14491324650),
    INT64_C( 14504409487), INT64_C( 14517506139), INT64_C( 14530614617), INT64_C( 14543734931), INT64_C( 14556867091), INT64_C( 14570011110), INT64_C( 14583166996), INT64_C( 14596334762),
    INT64_C( 14609514417), INT64_C( 14622705973), INT64_C( 14635909440), INT64_C( 14649124829), INT64_C( 14662352151), INT64_C( 14675591416), INT64_C( 14688842636), INT64_C( 14702105820),
    INT64_C( 14715380981), INT64_C( 14728668128), INT64_C( 14741967273), INT64_C( 14755278426), INT64_C( 14768601599), INT64_C( 14781936801), INT64_C( 14795284045), INT64_C( 14808643340),
    INT64_C( 14822014698), INT64_C( 14835398129), INT64_C( 14848793645), INT64_C( 14862201256), INT64_C( 14875620974), INT64_C( 14889052809), INT64_C( 14902496772), INT64_C( 14915952874),
    INT64_C( 14929421126), INT64_C( 14942901539), INT64_C( 14956394125), INT64_C( 14969898893), INT64_C( 14983415856), INT64_C( 14996945023), INT64_C( 15010486407), INT64_C( 15024040017),
    INT64_C( 15037605866), INT64_C( 15051183964), INT64_C( 15064774322), INT64_C( 15078376952), INT64_C( 15091991863), INT64_C( 15105619069), INT64_C( 15119258579), INT64_C( 15132910404),
    INT64_C( 15146574557), INT64_C( 15160251047), INT64_C( 15173939887), INT64_C( 15187641087), INT64_C( 15201354658), INT64_C( 15215080611), INT64_C( 15228818959), INT64_C( 15242569711),
    INT64_C( 15256332880), INT64_C( 15270108476), INT64_C( 15283896510), INT64_C( 15297696995), INT64_C( 15311509940), INT64_C( 15325335358), INT64_C( 15339173259), INT64_C( 15353023655),
    INT64_C( 15366886557), INT64_C( 15380761977), INT64_C( 15394649925), INT64_C( 15408550413), INT64_C( 15422463453), INT64_C( 15436389055), INT64_C( 15450327231), INT64_C( 15464277993),
    INT64_C( 15478241352), INT64_C( 15492217318), INT64_C( 15506205904), INT64_C( 15520207121), INT64_C( 15534220980), INT64_C( 15548247493), INT64_C( 15562286671), INT64_C( 15576338526),
    INT64_C( 15590403069), INT64_C( 15604480311), INT64_C( 15618570264), INT64_C( 15632672940), INT64_C( 15646788349), INT64_C( 15660916504), INT64_C( 15675057416), INT64_C( 15689211096),
    INT64_C( 15703377556), INT64_C( 15717556808), INT64_C( 15731748863), INT64_C( 15745953732), INT64_C( 15760171428), INT64_C( 15774401961), INT64_C( 15788645344), INT64_C( 15802901587),
    INT64_C( 15817170703), INT64_C( 15831452704), INT64_C( 15845747600), INT64_C( 15860055404), INT64_C( 15874376126), INT64_C( 15888709780), INT64_C( 15903056376), INT64_C( 15917415926),
    INT64_C( 15931788442), INT64_C( 15946173936), INT64_C( 15960572419), INT64_C( 15974983903), INT64_C( 15989408400), INT64_C( 16003845921), INT64_C( 16018296478), INT64_C( 16032760084),
    INT64_C( 16047236749), INT64_C( 16061726486), INT64_C( 16076229306), INT64_C( 16090745222), INT64_C( 16105274244), INT64_C( 16119816386), INT64_C( 16134371658), INT64_C( 16148940072),
    INT64_C( 16163521642), INT64_C( 16178116377), INT64_C( 16192724291), INT64_C( 16207345394), INT64_C( 16221979700), INT64_C( 16236627220), INT64_C( 16251287966), INT64_C( 16265961949),
    INT64_C( 16280649182), INT64_C( 16295349677), INT64_C( 16310063446), INT64_C( 16324790500), INT64_C( 16339530852), INT64_C( 16354284514), INT64_C( 16369051497), INT64_C( 16383831815),
    INT64_C( 16398625478), INT64_C( 16413432498), INT64_C( 16428252889), INT64_C( 16443086662), INT64_C( 16457933828), INT64_C( 16472794401), INT64_C( 16487668392), INT64_C( 16502555814),
    INT64_C( 16517456678), INT64_C( 16532370996), INT64_C( 16547298782), INT64_C( 16562240046), INT64_C( 16577194801), INT64_C( 16592163060), INT64_C( 16607144834), INT64_C( 16622140136),
    INT64_C( 16637148978), INT64_C( 16652171372), INT64_C( 16667207330), INT64_C( 16682256865), INT64_C( 16697319988), INT64_C( 16712396713), INT64_C( 16727487051), INT64_C( 16742591015),
    INT64_C( 16757708617), INT64_C( 16772839870), INT64_C( 16787984785), INT64_C( 16803143375), INT64_C( 16818315652), INT64_C( 16833501629), INT64_C( 16848701318), INT64_C( 16863914732),
    INT64_C( 16879141882), INT64_C( 16894382782), INT64_C( 16909637443), INT64_C( 16924905878), INT64_C( 16940188100), INT64_C( 16955484121), INT64_C( 16970793953), INT64_C( 16986117609),
    INT64_C( 17001455102), INT64_C( 17016806443), INT64_C( 17032171646), INT64_C( 17047550723), INT64_C( 17062943686), INT64_C( 17078350548), INT64_C( 17093771322), INT64_C( 17109206020),
    INT64_C( 17124654654), INT64_C( 17140117238), INT64_C( 17155593783), INT64_C( 17171084303), INT64_C( 17186588810), INT64_C( 17202107316), INT64_C( 17217639835), INT64_C( 17233186379),
    INT64_C( 17248746961), INT64_C( 17264321593), INT64_C( 17279910288), INT64_C( 17295513058), INT64_C( 17311129917), INT64_C( 17326760877), INT64_C( 17342405951), INT64_C( 17358065152),
    INT64_C( 17373738492), INT64_C( 17389425984), INT64_C( 17405127641), INT64_C( 17420843475), INT64_C( 17436573501), INT64_C( 17452317729), INT64_C( 17468076174), INT64_C( 17483848847),
    INT64_C( 17499635763), INT64_C( 17515436933), INT64_C( 17531252370), INT64_C( 17547082089), INT64_C( 17562926100), INT64_C( 17578784418), INT64_C( 17594657054), INT64_C( 17610544023),
    INT64_C( 17626445337), INT64_C( 17642361009), INT64_C( 17658291052), INT64_C( 17674235479), INT64_C( 17690194302), INT64_C( 17706167536), INT64_C( 17722155192), INT64_C( 17738157285),
    INT64_C( 17754173826), INT64_C( 17770204830), INT64_C( 17786250308), INT64_C( 17802310275), INT64_C( 17818384743), INT64_C( 17834473725), INT64_C( 17850577234), INT64_C( 17866695285),
    INT64_C( 17882827888), INT64_C( 17898975059), INT64_C( 17915136810), INT64_C( 17931313153), INT64_C( 17947504104), INT64_C( 17963709673), INT64_C( 17979929875), INT64_C( 17996164724),
    INT64_C( 18012414231), INT64_C( 18028678411), INT64_C( 18044957276), INT64_C( 18061250840), INT64_C( 18077559117), INT64_C( 18093882118), INT64_C( 18110219859), INT64_C( 18126572352),
    INT64_C( 18142939610), INT64_C( 18159321646), INT64_C( 18175718475), INT64_C( 18192130109), INT64_C( 18208556562), INT64_C( 18224997847), INT64_C( 18241453978), INT64_C( 18257924967),
    INT64_C( 18274410829), INT64_C( 18290911577), INT64_C( 18307427224), INT64_C( 18323957783), INT64_C( 18340503269), INT64_C( 18357063694), INT64_C( 18373639073), INT64_C( 18390229418),
    INT64_C( 18406834743), INT64_C( 18423455062), INT64_C( 18440090388), INT64_C( 18456740735), INT64_C( 18473406116), INT64_C( 18490086545), INT64_C( 18506782035), INT64_C( 18523492601),
    INT64_C( 18540218255), INT64_C( 18556959012), INT64_C( 18573714884), INT64_C( 18590485886), INT64_C( 18607272032), INT64_C( 18624073334), INT64_C( 18640889807), INT64_C( 18657721464),
    INT64_C( 18674568319), INT64_C( 18691430386), INT64_C( 18708307679), INT64_C( 18725200211), INT64_C( 18742107995), INT64_C( 18759031047), INT64_C( 18775969379), INT64_C( 18792923005),
    INT64_C( 18809891940), INT64_C( 18826876196), INT64_C( 18843875788), INT64_C( 18860890730), INT64_C( 18877921036), INT64_C( 18894966719), INT64_C( 18912027793), INT64_C( 18929104272),
    INT64_C( 18946196171), INT64_C( 18963303502), INT64_C( 18980426280), INT64_C( 18997564519), INT64_C( 19014718234), INT64_C( 19031887436), INT64_C( 19049072142), INT64_C( 19066272365),
    INT64_C( 19083488118), INT64_C( 19100719416), INT64_C( 19117966273), INT64_C( 19135228703), INT64_C( 19152506720), INT64_C( 19169800338), INT64_C( 19187109571), INT64_C( 19204434434),
    INT64_C( 19221774940), INT64_C( 19239131103), INT64_C( 19256502938), INT64_C( 19273890458), INT64_C( 19291293679), INT64_C( 19308712614), INT64_C( 19326147277), INT64_C( 19343597682),
    INT64_C( 19361063844), INT64_C( 19378545778), INT64_C( 19396043496), INT64_C( 19413557014), INT64_C( 19431086345), INT64_C( 19448631505), INT64_C( 19466192507), INT64_C( 19483769365),
    INT64_C( 19501362094), INT64_C( 19518970709), INT64_C( 19536595223), INT64_C( 19554235651), INT64_C( 19571892007), INT64_C( 19589564306), INT64_C( 19607252562), INT64_C( 19624956789),
    INT64_C( 19642677003), INT64_C( 19660413217), INT64_C( 19678165445), INT64_C( 19695933703), INT64_C( 19713718004), INT64_C( 19731518364), INT64_C( 19749334797), INT64_C( 19767167316),
    INT64_C( 19785015938), INT64_C( 19802880675), INT64_C( 19820761544), INT64_C( 19838658558), INT64_C( 19856571732), INT64_C( 19874501080), INT64_C( 19892446618), INT64_C( 19910408359),
    INT64_C( 19928386319), INT64_C( 19946380512), INT64_C( 19964390952), INT64_C( 19982417656), INT64_C( 20000460636), INT64_C( 20018519908), INT64_C( 20036595486), INT64_C( 20054687386),
    INT64_C( 20072795621), INT64_C( 20090920207), INT64_C( 20109061159), INT64_C( 20127218491), INT64_C( 20145392218), INT64_C( 20163582355), INT64_C( 20181788916), INT64_C( 20200011917),
    INT64_C( 20218251373), INT64_C( 20236507297), INT64_C( 20254779706), INT64_C( 20273068613), INT64_C( 20291374035), INT64_C( 20309695985), INT64_C( 20328034478), INT64_C( 20346389531),
    INT64_C( 20364761157), INT64_C( 20383149371), INT64_C( 20401554189), INT64_C( 20419975625), INT64_C( 20438413695), INT64_C( 20456868414), INT64_C( 20475339796), INT64_C( 20493827856),
    INT64_C( 20512332611), INT64_C( 20530854074), INT64_C( 20549392261), INT64_C( 20567947186), INT64_C( 20586518866), INT64_C( 20605107315), INT64_C( 20623712548), INT64_C( 20642334581),
    INT64_C( 20660973429), INT64_C( 20679629106), INT64_C( 20698301628), INT64_C( 20716991010), INT64_C( 20735697268), INT64_C( 20754420417), INT64_C( 20773160471), INT64_C( 20791917447),
    INT64_C( 20810691359), INT64_C( 20829482223), INT64_C( 20848290054), INT64_C( 20867114867), INT64_C( 20885956678), INT64_C( 20904815502), INT64_C( 20923691355), INT64_C( 20942584252),
    INT64_C( 20961494207), INT64_C( 20980421237), INT64_C( 20999365358), INT64_C( 21018326583), INT64_C( 21037304930), INT64_C( 21056300413), INT64_C( 21075313048), INT64_C( 21094342850),
    INT64_C( 21113389835), INT64_C( 21132454018), INT64_C( 21151535416), INT64_C( 21170634042), INT64_C( 21189749914), INT64_C( 21208883046), INT64_C( 21228033454), INT64_C( 21247201154),
    INT64_C( 21266386161), INT64_C( 21285588491), INT64_C( 21304808160), INT64_C( 21324045183), INT64_C( 21343299576), INT64_C( 21362571355), INT64_C( 21381860535), INT64_C( 21401167132),
    INT64_C( 21420491162), INT64_C( 21439832640), INT64_C( 21459191583), INT64_C( 21478568005), INT64_C( 21497961923), INT64_C( 21517373353), INT64_C( 21536802311), INT64_C( 21556248811),
    INT64_C( 21575712871), INT64_C( 21595194505), INT64_C( 21614693731), INT64_C( 21634210563), INT64_C( 21653745017), INT64_C( 21673297111), INT64_C( 21692866858), INT64_C( 21712454276),
    INT64_C( 21732059380), INT64_C( 21751682187), INT64_C( 21771322712), INT64_C( 21790980971), INT64_C( 21810656980), INT64_C( 21830350756), INT64_C( 21850062314), INT64_C( 21869791670),
    INT64_C( 21889538841), INT64_C( 21909303843), INT64_C( 21929086691), INT64_C( 21948887402), INT64_C( 21968705991), INT64_C( 21988542476), INT64_C( 22008396872), INT64_C( 22028269196),
    INT64_C( 22048159463), INT64_C( 22068067690), INT64_C( 22087993893), INT64_C( 22107938088), INT64_C( 22127900291), INT64_C( 22147880519), INT64_C( 22167878788), INT64_C( 22187895115),
    INT64_C( 22207929515), INT64_C( 22227982005), INT64_C( 22248052601), INT64_C( 22268141320), INT64_C( 22288248178), INT64_C( 22308373191), INT64_C( 22328516376), INT64_C( 22348677749),
    INT64_C( 22368857327), INT64_C( 22389055126), INT64_C( 22409271162), INT64_C( 22429505452), INT64_C( 22449758012), INT64_C( 22470028860), INT64_C( 22490318010), INT64_C( 22510625481),
    INT64_C( 22530951288), INT64_C( 22551295448), INT64_C( 22571657978), INT64_C( 22592038894), INT64_C( 22612438213), INT64_C( 22632855951), INT64_C( 22653292126), INT64_C( 22673746753),
    INT64_C( 22694219849), INT64_C( 22714711431), INT64_C( 22735221517), INT64_C( 22755750121), INT64_C( 22776297262), INT64_C( 22796862955), INT64_C( 22817447219), INT64_C( 22838050068),
    INT64_C( 22858671521), INT64_C( 22879311594), INT64_C( 22899970304), INT64_C( 22920647667), INT64_C( 22941343701), INT64_C( 22962058422), INT64_C( 22982791847), INT64_C( 23003543993),
    INT64_C( 23024314878), INT64_C( 23045104517), INT64_C( 23065912928), INT64_C( 23086740128), INT64_C( 23107586134), INT64_C( 23128450963), INT64_C( 23149334631), INT64_C( 23170237156),
    INT64_C( 23191158555), INT64_C( 23212098844), INT64_C( 23233058042), INT64_C( 23254036164), INT64_C( 23275033229), INT64_C( 23296049252), INT64_C( 23317084252), INT64_C( 23338138246),
    INT64_C( 23359211249), INT64_C( 23380303281), INT64_C( 23401414358), INT64_C( 23422544496), INT64_C( 23443693714), INT64_C( 23464862028), INT64_C( 23486049457), INT64_C( 23507256016),
    INT64_C( 23528481723), INT64_C( 23549726597), INT64_C( 23570990653), INT64_C( 23592273909), INT64_C( 23613576383), INT64_C( 23634898091), INT64_C( 23656239053), INT64_C( 23677599283),
    INT64_C( 23698978801), INT64_C( 23720377623), INT64_C( 23741795767), INT64_C( 23763233251), INT64_C( 23784690091), INT64_C( 23806166306), INT64_C( 23827661912), INT64_C( 23849176928),
    INT64_C( 23870711371), INT64_C( 23892265258), INT64_C( 23913838606), INT64_C( 23935431435), INT64_C( 23957043760), INT64_C( 23978675600), INT64_C( 24000326973), INT64_C( 24021997895),
    INT64_C( 24043688385), INT64_C( 24065398461), INT64_C( 24087128139), INT64_C( 24108877438), INT64_C( 24130646375), INT64_C( 24152434968), INT64_C( 24174243236), INT64_C( 24196071194),
    INT64_C( 24217918863), INT64_C( 24239786258), INT64_C( 24261673399), INT64_C( 24283580302), INT64_C( 24305506986), INT64_C( 24327453468), INT64_C( 24349419767), INT64_C( 24371405901),
    INT64_C( 24393411886), INT64_C( 24415437742), INT64_C( 24437483485), INT64_C( 24459549135), INT64_C( 24481634709), INT64_C( 24503740225), INT64_C( 24525865700), INT64_C( 24548011154),
    INT64_C( 24570176604), INT64_C( 24592362068), INT64_C( 24614567564), INT64_C( 24636793111), INT64_C( 24659038726), INT64_C( 24681304427), INT64_C( 24703590233), INT64_C( 24725896162),
    INT64_C( 24748222232), INT64_C( 24770568461), INT64_C( 24792934868), INT64_C( 24815321470), INT64_C( 24837728285), INT64_C( 24860155333), INT64_C( 24882602632), INT64_C( 24905070198),
    INT64_C( 24927558052), INT64_C( 24950066211), INT64_C( 24972594694), INT64_C( 24995143518), INT64_C( 25017712703), INT64_C( 25040302267), INT64_C( 25062912227), INT64_C( 25085542604),
    INT64_C( 25108193414), INT64_C( 25130864676), INT64_C( 25153556409), INT64_C( 25176268632), INT64_C( 25199001362), INT64_C( 25221754619), INT64_C( 25244528421), INT64_C( 25267322786),
    INT64_C( 25290137733), INT64_C( 25312973281), INT64_C( 25335829448), INT64_C( 25358706253), INT64_C( 25381603714), INT64_C( 25404521850), INT64_C( 25427460680), INT64_C( 25450420223),
    INT64_C( 25473400497), INT64_C( 25496401520), INT64_C( 25519423312), INT64_C( 25542465892), INT64_C( 25565529277), INT64_C( 25588613488), INT64_C( 25611718542), INT64_C( 25634844459),
    INT64_C( 25657991257), INT64_C( 25681158956), INT64_C( 25704347573), INT64_C( 25727557129), INT64_C( 25750787641), INT64_C( 25774039130), INT64_C( 25797311613), INT64_C( 25820605109),
    INT64_C( 25843919639), INT64_C( 25867255220), INT64_C( 25890611872), INT64_C( 25913989613), INT64_C( 25937388464), INT64_C( 25960808442), INT64_C( 25984249567), INT64_C( 26007711858),
    INT64_C( 26031195334), INT64_C( 26054700014), INT64_C( 26078225918), INT64_C( 26101773064), INT64_C( 26125341472), INT64_C( 26148931161), INT64_C( 26172542150), INT64_C( 26196174459),
    INT64_C( 26219828106), INT64_C( 26243503111), INT64_C( 26267199493), INT64_C( 26290917272), INT64_C( 26314656466), INT64_C( 26338417096), INT64_C( 26362199180), INT64_C( 26386002738),
    INT64_C( 26409827789), INT64_C( 26433674353), INT64_C( 26457542449), INT64_C( 26481432096), INT64_C( 26505343314), INT64_C( 26529276123), INT64_C( 26553230542), INT64_C( 26577206590),
    INT64_C( 26601204288), INT64_C( 26625223654), INT64_C( 26649264708), INT64_C( 26673327469), INT64_C( 26697411958), INT64_C( 26721518194), INT64_C( 26745646197), INT64_C( 26769795985),
    INT64_C( 26793967580), INT64_C( 26818161000), INT64_C( 26842376265), INT64_C( 26866613395), INT64_C( 26890872411), INT64_C( 26915153330), INT64_C( 26939456174), INT64_C( 26963780962),
    INT64_C( 26988127714), INT64_C( 27012496449), INT64_C( 27036887189), INT64_C( 27061299951), INT64_C( 27085734757), INT64_C( 27110191626), INT64_C( 27134670579), INT64_C( 27159171634),
    INT64_C( 27183694812), INT64_C( 27208240134), INT64_C( 27232807618), INT64_C( 27257397286), INT64_C( 27282009157), INT64_C( 27306643250), INT64_C( 27331299587), INT64_C( 27355978187),
    INT64_C( 27380679071), INT64_C( 27405402258), INT64_C( 27430147768), INT64_C( 27454915623), INT64_C( 27479705841), INT64_C( 27504518444), INT64_C( 27529353451), INT64_C( 27554210882),
    INT64_C( 27579090758), INT64_C( 27603993100), INT64_C( 27628917927), INT64_C( 27653865259), INT64_C( 27678835118), INT64_C( 27703827522), INT64_C( 27728842494), INT64_C( 27753880053),
    INT64_C( 27778940219), INT64_C( 27804023013), INT64_C( 27829128455), INT64_C( 27854256566), INT64_C( 27879407367), INT64_C( 27904580877), INT64_C( 27929777117), INT64_C( 27954996108),
    INT64_C( 27980237871), INT64_C( 28005502425), INT64_C( 28030789792), INT64_C( 28056099992), INT64_C( 28081433045), INT64_C( 28106788973), INT64_C( 28132167795), INT64_C( 28157569534),
    INT64_C( 28182994208), INT64_C( 28208441840), INT64_C( 28233912450), INT64_C( 28259406057), INT64_C( 28284922685), INT64_C( 28310462352), INT64_C( 28336025080), INT64_C( 28361610890),
    INT64_C( 28387219802), INT64_C( 28412851838), INT64_C( 28438507018), INT64_C( 28464185363), INT64_C( 28489886894), INT64_C( 28515611632), INT64_C( 28541359599), INT64_C( 28567130814),
    INT64_C( 28592925299), INT64_C( 28618743075), INT64_C( 28644584163), INT64_C( 28670448584), INT64_C( 28696336359), INT64_C( 28722247509), INT64_C( 28748182056), INT64_C( 28774140020),
    INT64_C( 28800121422), INT64_C( 28826126284), INT64_C( 28852154628), INT64_C( 28878206473), INT64_C( 28904281841), INT64_C( 28930380755), INT64_C( 28956503233), INT64_C( 28982649300),
    INT64_C( 29008818974), INT64_C( 29035012278), INT64_C( 29061229233), INT64_C( 29087469861), INT64_C( 29113734183), INT64_C( 29140022219), INT64_C( 29166333992), INT64_C( 29192669524),
    INT64_C( 29219028834), INT64_C( 29245411946), INT64_C( 29271818880), INT64_C( 29298249658), INT64_C( 29324704302), INT64_C( 29351182832), INT64_C( 29377685272), INT64_C( 29404211641),
    INT64_C( 29430761962), INT64_C( 29457336257), INT64_C( 29483934546), INT64_C( 29510556853), INT64_C( 29537203198), INT64_C( 29563873603), INT64_C( 29590568089), INT64_C( 29617286680),
    INT64_C( 29644029395), INT64_C( 29670796258), INT64_C( 29697587290), INT64_C( 29724402513), INT64_C( 29751241948), INT64_C( 29778105618), INT64_C( 29804993544), INT64_C( 29831905748),
    INT64_C( 29858842252), INT64_C( 29885803079), INT64_C( 29912788250), INT64_C( 29939797786), INT64_C( 29966831711), INT64_C( 29993890046), INT64_C( 30020972813), INT64_C( 30048080034),
    INT64_C( 30075211732), INT64_C( 30102367928), INT64_C( 30129548644), INT64_C( 30156753903), INT64_C( 30183983727), INT64_C( 30211238138), INT64_C( 30238517158), INT64_C( 30265820809),
    INT64_C( 30293149114), INT64_C( 30320502095), INT64_C( 30347879774), INT64_C( 30375282173), INT64_C( 30402709315), INT64_C( 30430161223), INT64_C( 30457637918), INT64_C( 30485139423),
    INT64_C( 30512665760), INT64_C( 30540216952), INT64_C( 30567793021), INT64_C( 30595393989), INT64_C( 30623019880), INT64_C( 30650670715), INT64_C( 30678346518), INT64_C( 30706047310),
    INT64_C( 30733773114), INT64_C( 30761523953), INT64_C( 30789299850), INT64_C( 30817100826), INT64_C( 30844926905), INT64_C( 30872778110), INT64_C( 30900654463), INT64_C( 30928555986),
    INT64_C( 30956482703), INT64_C( 30984434636), INT64_C( 31012411808), INT64_C( 31040414242), INT64_C( 31068441961), INT64_C( 31096494987), INT64_C( 31124573343), INT64_C( 31152677052),
    INT64_C( 31180806138), INT64_C( 31208960622), INT64_C( 31237140528), INT64_C( 31265345880), INT64_C( 31293576698), INT64_C( 31321833008), INT64_C( 31350114832), INT64_C( 31378422192),
    INT64_C( 31406755113), INT64_C( 31435113616), INT64_C( 31463497725), INT64_C( 31491907464), INT64_C( 31520342855), INT64_C( 31548803922), INT64_C( 31577290687), INT64_C( 31605803174),
    INT64_C( 31634341407), INT64_C( 31662905407), INT64_C( 31691495200), INT64_C( 31720110807), INT64_C( 31748752253), INT64_C( 31777419560), INT64_C( 31806112752), INT64_C( 31834831853),
    INT64_C( 31863576885), INT64_C( 31892347872), INT64_C( 31921144838), INT64_C( 31949967806), INT64_C( 31978816799), INT64_C( 32007691842), INT64_C( 32036592957), INT64_C( 32065520167),
    INT64_C( 32094473498), INT64_C( 32123452972), INT64_C( 32152458612), INT64_C( 32181490443), INT64_C( 32210548488), INT64_C( 32239632771), INT64_C( 32268743315), INT64_C( 32297880145),
    INT64_C( 32327043283), INT64_C( 32356232754), INT64_C( 32385448581), INT64_C( 32414690789), INT64_C( 32443959401), INT64_C( 32473254440), INT64_C( 32502575931), INT64_C( 32531923898),
    INT64_C( 32561298365), INT64_C( 32590699355), INT64_C( 32620126892), INT64_C( 32649581000), INT64_C( 32679061705), INT64_C( 32708569028), INT64_C( 32738102995), INT64_C( 32767663629),
    INT64_C( 32797250955), INT64_C( 32826864997), INT64_C( 32856505778), INT64_C( 32886173323), INT64_C( 32915867657), INT64_C( 32945588802), INT64_C( 32975336785), INT64_C( 33005111627),
    INT64_C( 33034913355), INT64_C( 33064741992), INT64_C( 33094597563), INT64_C( 33124480092), INT64_C( 33154389602), INT64_C( 33184326120), INT64_C( 33214289668), INT64_C( 33244280272),
    INT64_C( 33274297955), INT64_C( 33304342743), INT64_C( 33334414659), INT64_C( 33364513729), INT64_C( 33394639977), INT64_C( 33424793426), INT64_C( 33454974103), INT64_C( 33485182031),
    INT64_C( 33515417235), INT64_C( 33545679739), INT64_C( 33575969569), INT64_C( 33606286749), INT64_C( 33636631304), INT64_C( 33667003258), INT64_C( 33697402636), INT64_C( 33727829463),
    INT64_C( 33758283764), INT64_C( 33788765563), INT64_C( 33819274886), INT64_C( 33849811756), INT64_C( 33880376200), INT64_C( 33910968242), INT64_C( 33941587906), INT64_C( 33972235219),
    INT64_C( 34002910204), INT64_C( 34033612887), INT64_C( 34064343293), INT64_C( 34095101446), INT64_C( 34125887372), INT64_C( 34156701097), INT64_C( 34187542644), INT64_C( 34218412039),
    INT64_C( 34249309308), INT64_C( 34280234475), INT64_C( 34311187566), INT64_C( 34342168606), INT64_C( 34373177620), INT64_C( 34404214633), INT64_C( 34435279671), INT64_C( 34466372759),
    INT64_C( 34497493922), INT64_C( 34528643185), INT64_C( 34559820575), INT64_C( 34591026116), INT64_C( 34622259834), INT64_C( 34653521754), INT64_C( 34684811902), INT64_C( 34716130304),
    INT64_C( 34747476983), INT64_C( 34778851968), INT64_C( 34810255281), INT64_C( 34841686951), INT64_C( 34873147001), INT64_C( 34904635458), INT64_C( 34936152347), INT64_C( 34967697695),
    INT64_C( 34999271525), INT64_C( 35030873866), INT64_C( 35062504741), INT64_C( 35094164177), INT64_C( 35125852200), INT64_C( 35157568835), INT64_C( 35189314109), INT64_C( 35221088047),
    INT64_C( 35252890674), INT64_C( 35284722018), INT64_C( 35316582104), INT64_C( 35348470957), INT64_C( 35380388605), INT64_C( 35412335072), INT64_C( 35444310385), INT64_C( 35476314569),
    INT64_C( 35508347652), INT64_C( 35540409659), INT64_C( 35572500616), INT64_C( 35604620549), INT64_C( 35636769485), INT64_C( 35668947449), INT64_C( 35701154469), INT64_C( 35733390569),
    INT64_C( 35765655777), INT64_C( 35797950118), INT64_C( 35830273619), INT64_C( 35862626307), INT64_C( 35895008207), INT64_C( 35927419346), INT64_C( 35959859751), INT64_C( 35992329447),
    INT64_C( 36024828462), INT64_C( 36057356821), INT64_C( 36089914552), INT64_C( 36122501681), INT64_C( 36155118233), INT64_C( 36187764237), INT64_C( 36220439718), INT64_C( 36253144703),
    INT64_C( 36285879219), INT64_C( 36318643293), INT64_C( 36351436950), INT64_C( 36384260218), INT64_C( 36417113124), INT64_C( 36449995694), INT64_C( 36482907955), INT64_C( 36515849934),
    INT64_C( 36548821658), INT64_C( 36581823153), INT64_C( 36614854447), INT64_C( 36647915566), INT64_C( 36681006538), INT64_C( 36714127388), INT64_C( 36747278145), INT64_C( 36780458836),
    INT64_C( 36813669486), INT64_C( 36846910124), INT64_C( 36880180776), INT64_C( 36913481470), INT64_C( 36946812232), INT64_C( 36980173090), INT64_C( 37013564071), INT64_C( 37046985202),
    INT64_C( 37080436511), INT64_C( 37113918024), INT64_C( 37147429769), INT64_C( 37180971773), INT64_C( 37214544064), INT64_C( 37248146668), INT64_C( 37281779614), INT64_C( 37315442928),
    INT64_C( 37349136639), INT64_C( 37382860773), INT64_C( 37416615358), INT64_C( 37450400421), INT64_C( 37484215991), INT64_C( 37518062094), INT64_C( 37551938758), INT64_C( 37585846010),
    INT64_C( 37619783879), INT64_C( 37653752392), INT64_C( 37687751577), INT64_C( 37721781461), INT64_C( 37755842072), INT64_C( 37789933437), INT64_C( 37824055586), INT64_C( 37858208544),
    INT64_C( 37892392341), INT64_C( 37926607004), INT64_C( 37960852560), INT64_C( 37995129039), INT64_C( 38029436467), INT64_C( 38063774873), INT64_C( 38098144284), INT64_C( 38132544729),
    INT64_C( 38166976236), INT64_C( 38201438832), INT64_C( 38235932547), INT64_C( 38270457406), INT64_C( 38305013440), INT64_C( 38339600676), INT64_C( 38374219143), INT64_C( 38408868868),
    INT64_C( 38443549879), INT64_C( 38478262206), INT64_C( 38513005875), INT64_C( 38547780917), INT64_C( 38582587358), INT64_C( 38617425227), INT64_C( 38652294553), INT64_C( 38687195364),
    INT64_C( 38722127689), INT64_C( 38757091555), INT64_C( 38792086992), INT64_C( 38827114028), INT64_C( 38862172691), INT64_C( 38897263010), INT64_C( 38932385013), INT64_C( 38967538730),
    INT64_C( 39002724188), INT64_C( 39037941417), INT64_C( 39073190445), INT64_C( 39108471301), INT64_C( 39143784014), INT64_C( 39179128612), INT64_C( 39214505124), INT64_C( 39249913579),
    INT64_C( 39285354006), INT64_C( 39320826433), INT64_C( 39356330890), INT64_C( 39391867406), INT64_C( 39427436009), INT64_C( 39463036728), INT64_C( 39498669593), INT64_C( 39534334632),
    INT64_C( 39570031875), INT64_C( 39605761350), INT64_C( 39641523088), INT64_C( 39677317115), INT64_C( 39713143463), INT64_C( 39749002160), INT64_C( 39784893235), INT64_C( 39820816718),
    INT64_C( 39856772638), INT64_C( 39892761024), INT64_C( 39928781905), INT64_C( 39964835311), INT64_C( 40000921271), INT64_C( 40037039815), INT64_C( 40073190972), INT64_C( 40109374771),
    INT64_C( 40145591242), INT64_C( 40181840415), INT64_C( 40218122318), INT64_C( 40254436982), INT64_C( 40290784436), INT64_C( 40327164710), INT64_C( 40363577833), INT64_C( 40400023835),
    INT64_C( 40436502745), INT64_C( 40473014594), INT64_C( 40509559411), INT64_C( 40546137226), INT64_C( 40582748069), INT64_C( 40619391969), INT64_C( 40656068957), INT64_C( 40692779061),
    INT64_C( 40729522313), INT64_C( 40766298742), INT64_C( 40803108378), INT64_C( 40839951251), INT64_C( 40876827391), INT64_C( 40913736828), INT64_C( 40950679592), INT64_C( 40987655713),
    INT64_C( 41024665222), INT64_C( 41061708148), INT64_C( 41098784521), INT64_C( 41135894373), INT64_C( 41173037732), INT64_C( 41210214630), INT64_C( 41247425097), INT64_C( 41284669162),
    INT64_C( 41321946857), INT64_C( 41359258211), INT64_C( 41396603256), INT64_C( 41433982021), INT64_C( 41471394536), INT64_C( 41508840833), INT64_C( 41546320942), INT64_C( 41583834894),
    INT64_C( 41621382718), INT64_C( 41658964446), INT64_C( 41696580108), INT64_C( 41734229734), INT64_C( 41771913357), INT64_C( 41809631005), INT64_C( 41847382710), INT64_C( 41885168503),
    INT64_C( 41922988414), INT64_C( 41960842475), INT64_C( 41998730715), INT64_C( 42036653167), INT64_C( 42074609860), INT64_C( 42112600826), INT64_C( 42150626096), INT64_C( 42188685700),
    INT64_C( 42226779670), INT64_C( 42264908037), INT64_C( 42303070831), INT64_C( 42341268084), INT64_C( 42379499827), INT64_C( 42417766092), INT64_C( 42456066908), INT64_C( 42494402308),
    INT64_C( 42532772322), INT64_C( 42571176983), INT64_C( 42609616321), INT64_C( 42648090367), INT64_C( 42686599153), INT64_C( 42725142710), INT64_C( 42763721070), INT64_C( 42802334264),
    INT64_C( 42840982324), INT64_C( 42879665280), INT64_C( 42918383165), INT64_C( 42957136010), INT64_C( 42995923847), INT64_C( 43034746707), INT64_C( 43073604621), INT64_C( 43112497622),
    INT64_C( 43151425742), INT64_C( 43190389011), INT64_C( 43229387462), INT64_C( 43268421126), INT64_C( 43307490035), INT64_C( 43346594221), INT64_C( 43385733716), INT64_C( 43424908552),
    INT64_C( 43464118761), INT64_C( 43503364374), INT64_C( 43542645423), INT64_C( 43581961941), INT64_C( 43621313960), INT64_C( 43660701511), INT64_C( 43700124627), INT64_C( 43739583340),
    INT64_C( 43779077682), INT64_C( 43818607685), INT64_C( 43858173381), INT64_C( 43897774803), INT64_C( 43937411983), INT64_C( 43977084953), INT64_C( 44016793745), INT64_C( 44056538392),
    INT64_C( 44096318926), INT64_C( 44136135379), INT64_C( 44175987785), INT64_C( 44215876175), INT64_C( 44255800582), INT64_C( 44295761039), INT64_C( 44335757577), INT64_C( 44375790230),
    INT64_C( 44415859030), INT64_C( 44455964010), INT64_C( 44496105203), INT64_C( 44536282641), INT64_C( 44576496356), INT64_C( 44616746383), INT64_C( 44657032753), INT64_C( 44697355499),
    INT64_C( 44737714654), INT64_C( 44778110251), INT64_C( 44818542324), INT64_C( 44859010904), INT64_C( 44899516024), INT64_C( 44940057719), INT64_C( 44980636021), INT64_C( 45021250962),
    INT64_C( 45061902576), INT64_C( 45102590897), INT64_C( 45143315957), INT64_C( 45184077789), INT64_C( 45224876426), INT64_C( 45265711903), INT64_C( 45306584251), INT64_C( 45347493505),
    INT64_C( 45388439698), INT64_C( 45429422863), INT64_C( 45470443033), INT64_C( 45511500242), INT64_C( 45552594524), INT64_C( 45593725911), INT64_C( 45634894437), INT64_C( 45676100137),
    INT64_C( 45717343042), INT64_C( 45758623188), INT64_C( 45799940607), INT64_C( 45841295334), INT64_C( 45882687401), INT64_C( 45924116844), INT64_C( 45965583694), INT64_C( 46007087987),
    INT64_C( 46048629756), INT64_C( 46090209034), INT64_C( 46131825857), INT64_C( 46173480257), INT64_C( 46215172268), INT64_C( 46256901925), INT64_C( 46298669262), INT64_C( 46340474312),
    INT64_C( 46382317109), INT64_C( 46424197689), INT64_C( 46466116084), INT64_C( 46508072328), INT64_C( 46550066457), INT64_C( 46592098505), INT64_C( 46634168505), INT64_C( 46676276491),
    INT64_C( 46718422499), INT64_C( 46760606562), INT64_C( 46802828715), INT64_C( 46845088992), INT64_C( 46887387428), INT64_C( 46929724057), INT64_C( 46972098913), INT64_C( 47014512032),
    INT64_C( 47056963447), INT64_C( 47099453193), INT64_C( 47141981305), INT64_C( 47184547818), INT64_C( 47227152765), INT64_C( 47269796183), INT64_C( 47312478105), INT64_C( 47355198566),
    INT64_C( 47397957602), INT64_C( 47440755246), INT64_C( 47483591535), INT64_C( 47526466502), INT64_C( 47569380183), INT64_C( 47612332612), INT64_C( 47655323825), INT64_C( 47698353856),
    INT64_C( 47741422741), INT64_C( 47784530515), INT64_C( 47827677213), INT64_C( 47870862870), INT64_C( 47914087521), INT64_C( 47957351201), INT64_C( 48000653946), INT64_C( 48043995791),
    INT64_C( 48087376771), INT64_C( 48130796921), INT64_C( 48174256278), INT64_C( 48217754876), INT64_C( 48261292750), INT64_C( 48304869937), INT64_C( 48348486471), INT64_C( 48392142389),
    INT64_C( 48435837725), INT64_C( 48479572516), INT64_C( 48523346797), INT64_C( 48567160604), INT64_C( 48611013972), INT64_C( 48654906937), INT64_C( 48698839535), INT64_C( 48742811801),
    INT64_C( 48786823772), INT64_C( 48830875483), INT64_C( 48874966971), INT64_C( 48919098270), INT64_C( 48963269418), INT64_C( 49007480449), INT64_C( 49051731401), INT64_C( 49096022308),
    INT64_C( 49140353208), INT64_C( 49184724136), INT64_C( 49229135129), INT64_C( 49273586222), INT64_C( 49318077452), INT64_C( 49362608855), INT64_C( 49407180467), INT64_C( 49451792325),
    INT64_C( 49496444464), INT64_C( 49541136922), INT64_C( 49585869735), INT64_C( 49630642939), INT64_C( 49675456571), INT64_C( 49720310667), INT64_C( 49765205263), INT64_C( 49810140397),
    INT64_C( 49855116105), INT64_C( 49900132423), INT64_C( 49945189388), INT64_C( 49990287037), INT64_C( 50035425407), INT64_C( 50080604534), INT64_C( 50125824455), INT64_C( 50171085207),
    INT64_C( 50216386827), INT64_C( 50261729352), INT64_C( 50307112819), INT64_C( 50352537264), INT64_C( 50398002725), INT64_C( 50443509239), INT64_C( 50489056842), INT64_C( 50534645572),
    INT64_C( 50580275467), INT64_C( 50625946562), INT64_C( 50671658896), INT64_C( 50717412506), INT64_C( 50763207428), INT64_C( 50809043701), INT64_C( 50854921361), INT64_C( 50900840446),
    INT64_C( 50946800993), INT64_C( 50992803040), INT64_C( 51038846624), INT64_C( 51084931783), INT64_C( 51131058555), INT64_C( 51177226976), INT64_C( 51223437084), INT64_C( 51269688918),
    INT64_C( 51315982515), INT64_C( 51362317911), INT64_C( 51408695146), INT64_C( 51455114258), INT64_C( 51501575282), INT64_C( 51548078259), INT64_C( 51594623225), INT64_C( 51641210219),
    INT64_C( 51687839277), INT64_C( 51734510440), INT64_C( 51781223743), INT64_C( 51827979227), INT64_C( 51874776927), INT64_C( 51921616883), INT64_C( 51968499133), INT64_C( 52015423716),
    INT64_C( 52062390668), INT64_C( 52109400029), INT64_C( 52156451836), INT64_C( 52203546129), INT64_C( 52250682945), INT64_C( 52297862323), INT64_C( 52345084301), INT64_C( 52392348918),
    INT64_C( 52439656212), INT64_C( 52487006222), INT64_C( 52534398986), INT64_C( 52581834543), INT64_C( 52629312932), INT64_C( 52676834191), INT64_C( 52724398360), INT64_C( 52772005475),
    INT64_C( 52819655578), INT64_C( 52867348705), INT64_C( 52915084897), INT64_C( 52962864192), INT64_C( 53010686629), INT64_C( 53058552247), INT64_C( 53106461084), INT64_C( 53154413181),
    INT64_C( 53202408576), INT64_C( 53250447307), INT64_C( 53298529415), INT64_C( 53346654939), INT64_C( 53394823916), INT64_C( 53443036388), INT64_C( 53491292393), INT64_C( 53539591970),
    INT64_C( 53587935159), INT64_C( 53636322000), INT64_C( 53684752530), INT64_C( 53733226791), INT64_C( 53781744821), INT64_C( 53830306660), INT64_C( 53878912348), INT64_C( 53927561924),
    INT64_C( 53976255428), INT64_C( 54024992899), INT64_C( 54073774377), INT64_C( 54122599902), INT64_C( 54171469514), INT64_C( 54220383253), INT64_C( 54269341157), INT64_C( 54318343268),
    INT64_C( 54367389625), INT64_C( 54416480268), INT64_C( 54465615237), INT64_C( 54514794572), INT64_C( 54564018313), INT64_C( 54613286501), INT64_C( 54662599174), INT64_C( 54711956375),
    INT64_C( 54761358142), INT64_C( 54810804516), INT64_C( 54860295537), INT64_C( 54909831246), INT64_C( 54959411682), INT64_C( 55009036887), INT64_C( 55058706901), INT64_C( 55108421764),
    INT64_C( 55158181517), INT64_C( 55207986199), INT64_C( 55257835853), INT64_C( 55307730518), INT64_C( 55357670235), INT64_C( 55407655045), INT64_C( 55457684988), INT64_C( 55507760105),
    INT64_C( 55557880437), INT64_C( 55608046026), INT64_C( 55658256910), INT64_C( 55708513133), INT64_C( 55758814733), INT64_C( 55809161754), INT64_C( 55859554234), INT64_C( 55909992217),
    INT64_C( 55960475741), INT64_C( 56011004850), INT64_C( 56061579583), INT64_C( 56112199983), INT64_C( 56162866090), INT64_C( 56213577945), INT64_C( 56264335591), INT64_C( 56315139068),
    INT64_C( 56365988417), INT64_C( 56416883680), INT64_C( 56467824899), INT64_C( 56518812115), INT64_C( 56569845369), INT64_C( 56620924704), INT64_C( 56672050160), INT64_C( 56723221780),
    INT64_C( 56774439604), INT64_C( 56825703676), INT64_C( 56877014036), INT64_C( 56928370726), INT64_C( 56979773788), INT64_C( 57031223265), INT64_C( 57082719197), INT64_C( 57134261627),
    INT64_C( 57185850597), INT64_C( 57237486149), INT64_C( 57289168325), INT64_C( 57340897167), INT64_C( 57392672718), INT64_C( 57444495018), INT64_C( 57496364111), INT64_C( 57548280039),
    INT64_C( 57600242844), INT64_C( 57652252569), INT64_C( 57704309255), INT64_C( 57756412946), INT64_C( 57808563683), INT64_C( 57860761509), INT64_C( 57913006467), INT64_C( 57965298599),
    INT64_C( 58017637948), INT64_C( 58070024556), INT64_C( 58122458467), INT64_C( 58174939722), INT64_C( 58227468365), INT64_C( 58280044438), INT64_C( 58332667985), INT64_C( 58385339047),
    INT64_C( 58438057669), INT64_C( 58490823892), INT64_C( 58543637760), INT64_C( 58596499317), INT64_C( 58649408604), INT64_C( 58702365665), INT64_C( 58755370543), INT64_C( 58808423282),
    INT64_C( 58861523924), INT64_C( 58914672513), INT64_C( 58967869093), INT64_C( 59021113706), INT64_C( 59074406395), INT64_C( 59127747205), INT64_C( 59181136179), INT64_C( 59234573359),
    INT64_C( 59288058791), INT64_C( 59341592517), INT64_C( 59395174580), INT64_C( 59448805025), INT64_C( 59502483896), INT64_C( 59556211235), INT64_C( 59609987087), INT64_C( 59663811496),
    INT64_C( 59717684505), INT64_C( 59771606158), INT64_C( 59825576499), INT64_C( 59879595573), INT64_C( 59933663422), INT64_C( 59987780092), INT64_C( 60041945626), INT64_C( 60096160069),
    INT64_C( 60150423464), INT64_C( 60204735855), INT64_C( 60259097288), INT64_C( 60313507806), INT64_C( 60367967454), INT64_C( 60422476275), INT64_C( 60477034315), INT64_C( 60531641618),
    INT64_C( 60586298228), INT64_C( 60641004189), INT64_C( 60695759547), INT64_C( 60750564346), INT64_C( 60805418631), INT64_C( 60860322446), INT64_C( 60915275836), INT64_C( 60970278845),
    INT64_C( 61025331520), INT64_C( 61080433903), INT64_C( 61135586041), INT64_C( 61190787978), INT64_C( 61246039760), INT64_C( 61301341430), INT64_C( 61356693035), INT64_C( 61412094619),
    INT64_C( 61467546228), INT64_C( 61523047906), INT64_C( 61578599699), INT64_C( 61634201652), INT64_C( 61689853811), INT64_C( 61745556220), INT64_C( 61801308926), INT64_C( 61857111972),
    INT64_C( 61912965406), INT64_C( 61968869273), INT64_C( 62024823617), INT64_C( 62080828485), INT64_C( 62136883922), INT64_C( 62192989974), INT64_C( 62249146686), INT64_C( 62305354105),
    INT64_C( 62361612276), INT64_C( 62417921244), INT64_C( 62474281057), INT64_C( 62530691759), INT64_C( 62587153397), INT64_C( 62643666016), INT64_C( 62700229663), INT64_C( 62756844384),
    INT64_C( 62813510225), INT64_C( 62870227232), INT64_C( 62926995451), INT64_C( 62983814928), INT64_C( 63040685710), INT64_C( 63097607843), INT64_C( 63154581374), INT64_C( 63211606349),
    INT64_C( 63268682813), INT64_C( 63325810815), INT64_C( 63382990400), INT64_C( 63440221615), INT64_C( 63497504506), INT64_C( 63554839120), INT64_C( 63612225505), INT64_C( 63669663706),
    INT64_C( 63727153770), INT64_C( 63784695744), INT64_C( 63842289676), INT64_C( 63899935612), INT64_C( 63957633599), INT64_C( 64015383683), INT64_C( 64073185913), INT64_C( 64131040335),
    INT64_C( 64188946996), INT64_C( 64246905943), INT64_C( 64304917224), INT64_C( 64362980886), INT64_C( 64421096977), INT64_C( 64479265542), INT64_C( 64537486631), INT64_C( 64595760289),
    INT64_C( 64654086566), INT64_C( 64712465508), INT64_C( 64770897163), INT64_C( 64829381578), INT64_C( 64887918801), INT64_C( 64946508880), INT64_C( 65005151863), INT64_C( 65063847796),
    INT64_C( 65122596729), INT64_C( 65181398709), INT64_C( 65240253784), INT64_C( 65299162001), INT64_C( 65358123409), INT64_C( 65417138056), INT64_C( 65476205990), INT64_C( 65535327258),
    INT64_C( 65594501910), INT64_C( 65653729993), INT64_C( 65713011556), INT64_C( 65772346647), INT64_C( 65831735313), INT64_C( 65891177605), INT64_C( 65950673569), INT64_C( 66010223255),
    INT64_C( 66069826711), INT64_C( 66129483985), INT64_C( 66189195126), INT64_C( 66248960183), INT64_C( 66308779205), INT64_C( 66368652240), INT64_C( 66428579336), INT64_C( 66488560544),
    INT64_C( 66548595911), INT64_C( 66608685486), INT64_C( 66668829319), INT64_C( 66729027458), INT64_C( 66789279953), INT64_C( 66849586853), INT64_C( 66909948206), INT64_C( 66970364062),
    INT64_C( 67030834469), INT64_C( 67091359479), INT64_C( 67151939139), INT64_C( 67212573499), INT64_C( 67273262608), INT64_C( 67334006516), INT64_C( 67394805272), INT64_C( 67455658926),
    INT64_C( 67516567528), INT64_C( 67577531126), INT64_C( 67638549771), INT64_C( 67699623513), INT64_C( 67760752400), INT64_C( 67821936484), INT64_C( 67883175813), INT64_C( 67944470438),
    INT64_C( 68005820408), INT64_C( 68067225774), INT64_C( 68128686585), INT64_C( 68190202892), INT64_C( 68251774745), INT64_C( 68313402193), INT64_C( 68375085288), INT64_C( 68436824079),
    INT64_C( 68498618616), INT64_C( 68560468951), INT64_C( 68622375132), INT64_C( 68684337212), INT64_C( 68746355240), INT64_C( 68808429266), INT64_C( 68870559342), INT64_C( 68932745517),
    INT64_C( 68994987844), INT64_C( 69057286371), INT64_C( 69119641150), INT64_C( 69182052233), INT64_C( 69244519669), INT64_C( 69307043509), INT64_C( 69369623805), INT64_C( 69432260607),
    INT64_C( 69494953967), INT64_C( 69557703935), INT64_C( 69620510563), INT64_C( 69683373902), INT64_C( 69746294002), INT64_C( 69809270916), INT64_C( 69872304695), INT64_C( 69935395389),
    INT64_C( 69998543051), INT64_C( 70061747731), INT64_C( 70125009482), INT64_C( 70188328354), INT64_C( 70251704400), INT64_C( 70315137670), INT64_C( 70378628218), INT64_C( 70442176093),
    INT64_C( 70505781349), INT64_C( 70569444036), INT64_C( 70633164208), INT64_C( 70696941915), INT64_C( 70760777209), INT64_C( 70824670143), INT64_C( 70888620769), INT64_C( 70952629139),
    INT64_C( 71016695305), INT64_C( 71080819318), INT64_C( 71145001232), INT64_C( 71209241099), INT64_C( 71273538970), INT64_C( 71337894899), INT64_C( 71402308937), INT64_C( 71466781138),
    INT64_C( 71531311553), INT64_C( 71595900236), INT64_C( 71660547239), INT64_C( 71725252614), INT64_C( 71790016414), INT64_C( 71854838692), INT64_C( 71919719502), INT64_C( 71984658894),
    INT64_C( 72049656924), INT64_C( 72114713643), INT64_C( 72179829104), INT64_C( 72245003361), INT64_C( 72310236467), INT64_C( 72375528474), INT64_C( 72440879436), INT64_C( 72506289407),
    INT64_C( 72571758439), INT64_C( 72637286585), INT64_C( 72702873900), INT64_C( 72768520437), INT64_C( 72834226248), INT64_C( 72899991388), INT64_C( 72965815910), INT64_C( 73031699868),
    INT64_C( 73097643316), INT64_C( 73163646307), INT64_C( 73229708894), INT64_C( 73295831133), INT64_C( 73362013075), INT64_C( 73428254777), INT64_C( 73494556291), INT64_C( 73560917671),
    INT64_C( 73627338972), INT64_C( 73693820248), INT64_C( 73760361552), INT64_C( 73826962939), INT64_C( 73893624464), INT64_C( 73960346180), INT64_C( 74027128142), INT64_C( 74093970404),
    INT64_C( 74160873021), INT64_C( 74227836047), INT64_C( 74294859537), INT64_C( 74361943546), INT64_C( 74429088127), INT64_C( 74496293336), INT64_C( 74563559228), INT64_C( 74630885857),
    INT64_C( 74698273278), INT64_C( 74765721546), INT64_C( 74833230716), INT64_C( 74900800843), INT64_C( 74968431981), INT64_C( 75036124187), INT64_C( 75103877515), INT64_C( 75171692020),
    INT64_C( 75239567758), INT64_C( 75307504784), INT64_C( 75375503154), INT64_C( 75443562921), INT64_C( 75511684143), INT64_C( 75579866875), INT64_C( 75648111171), INT64_C( 75716417088),
    INT64_C( 75784784682), INT64_C( 75853214008), INT64_C( 75921705121), INT64_C( 75990258078), INT64_C( 76058872934), INT64_C( 76127549746), INT64_C( 76196288569), INT64_C( 76265089459),
    INT64_C( 76333952472), INT64_C( 76402877665), INT64_C( 76471865093), INT64_C( 76540914813), INT64_C( 76610026881), INT64_C( 76679201353), INT64_C( 76748438286), INT64_C( 76817737735),
    INT64_C( 76887099758), INT64_C( 76956524411), INT64_C( 77026011751), INT64_C( 77095561834), INT64_C( 77165174716), INT64_C( 77234850455), INT64_C( 77304589107), INT64_C( 77374390729),
    INT64_C( 77444255378), INT64_C( 77514183111), INT64_C( 77584173984), INT64_C( 77654228055), INT64_C( 77724345381), INT64_C( 77794526019), INT64_C( 77864770026), INT64_C( 77935077460),
    INT64_C( 78005448377), INT64_C( 78075882835), INT64_C( 78146380891), INT64_C( 78216942603), INT64_C( 78287568028), INT64_C( 78358257224), INT64_C( 78429010248), INT64_C( 78499827158),
    INT64_C( 78570708011), INT64_C( 78641652866), INT64_C( 78712661781), INT64_C( 78783734812), INT64_C( 78854872018), INT64_C( 78926073457), INT64_C( 78997339186), INT64_C( 79068669265),
    INT64_C( 79140063750), INT64_C( 79211522701), INT64_C( 79283046175), INT64_C( 79354634231), INT64_C( 79426286926), INT64_C( 79498004320), INT64_C( 79569786471), INT64_C( 79641633436),
    INT64_C( 79713545276), INT64_C( 79785522047), INT64_C( 79857563810), INT64_C( 79929670622), INT64_C( 80001842543), INT64_C( 80074079630), INT64_C( 80146381944), INT64_C( 80218749542),
    INT64_C( 80291182485), INT64_C( 80363680830), INT64_C( 80436244637), INT64_C( 80508873964), INT64_C( 80581568872), INT64_C( 80654329420), INT64_C( 80727155666), INT64_C( 80800047670),
    INT64_C( 80873005491), INT64_C( 80946029189), INT64_C( 81019118823), INT64_C( 81092274453), INT64_C( 81165496138), INT64_C( 81238783938), INT64_C( 81312137913), INT64_C( 81385558123),
    INT64_C( 81459044626), INT64_C( 81532597484), INT64_C( 81606216756), INT64_C( 81679902502), INT64_C( 81753654781), INT64_C( 81827473655), INT64_C( 81901359183), INT64_C( 81975311426),
    INT64_C( 82049330443), INT64_C( 82123416295), INT64_C( 82197569042), INT64_C( 82271788746), INT64_C( 82346075465), INT64_C( 82420429261), INT64_C( 82494850194), INT64_C( 82569338325),
    INT64_C( 82643893714), INT64_C( 82718516423), INT64_C( 82793206512), INT64_C( 82867964041), INT64_C( 82942789073), INT64_C( 83017681667), INT64_C( 83092641885), INT64_C( 83167669787),
    INT64_C( 83242765436), INT64_C( 83317928891), INT64_C( 83393160215), INT64_C( 83468459469), INT64_C( 83543826713), INT64_C( 83619262010), INT64_C( 83694765420), INT64_C( 83770337006),
    INT64_C( 83845976829), INT64_C( 83921684950), INT64_C( 83997461431), INT64_C( 84073306334), INT64_C( 84149219720), INT64_C( 84225201652), INT64_C( 84301252192), INT64_C( 84377371400),
    INT64_C( 84453559340), INT64_C( 84529816073), INT64_C( 84606141662), INT64_C( 84682536168), INT64_C( 84758999655), INT64_C( 84835532183), INT64_C( 84912133816), INT64_C( 84988804616),
    INT64_C( 85065544645), INT64_C( 85142353966), INT64_C( 85219232641), INT64_C( 85296180734), INT64_C( 85373198306), INT64_C( 85450285420), INT64_C( 85527442140), INT64_C( 85604668528),
    INT64_C( 85681964647), INT64_C( 85759330560), INT64_C( 85836766330), INT64_C( 85914272020), INT64_C( 85991847694), INT64_C( 86069493413), INT64_C( 86147209243), INT64_C( 86224995245),
    INT64_C( 86302851483), INT64_C( 86380778022), INT64_C( 86458774923), INT64_C( 86536842251), INT64_C( 86614980070), INT64_C( 86693188442), INT64_C( 86771467433), INT64_C( 86849817104),
    INT64_C( 86928237521), INT64_C( 87006728747), INT64_C( 87085290847), INT64_C( 87163923883), INT64_C( 87242627920), INT64_C( 87321403023), INT64_C( 87400249255), INT64_C( 87479166681),
    INT64_C( 87558155364), INT64_C( 87637215370), INT64_C( 87716346763), INT64_C( 87795549606), INT64_C( 87874823966), INT64_C( 87954169905), INT64_C( 88033587489), INT64_C( 88113076783),
    INT64_C( 88192637852), INT64_C( 88272270759), INT64_C( 88351975570), INT64_C( 88431752350), INT64_C( 88511601164), INT64_C( 88591522077), INT64_C( 88671515154), INT64_C( 88751580460),
    INT64_C( 88831718061), INT64_C( 88911928021), INT64_C( 88992210406), INT64_C( 89072565281), INT64_C( 89152992713), INT64_C( 89233492766), INT64_C( 89314065505), INT64_C( 89394710998),
    INT64_C( 89475429308), INT64_C( 89556220503), INT64_C( 89637084647), INT64_C( 89718021807), INT64_C( 89799032049), INT64_C( 89880115438), INT64_C( 89961272041), INT64_C( 90042501924),
    INT64_C( 90123805153), INT64_C( 90205181794), INT64_C( 90286631913), INT64_C( 90368155577), INT64_C( 90449752852), INT64_C( 90531423805), INT64_C( 90613168503), INT64_C( 90694987011),
    INT64_C( 90776879396), INT64_C( 90858845726), INT64_C( 90940886066), INT64_C( 91023000484), INT64_C( 91105189047), INT64_C( 91187451822), INT64_C( 91269788875), INT64_C( 91352200273),
    INT64_C( 91434686085), INT64_C( 91517246376), INT64_C( 91599881215), INT64_C( 91682590668), INT64_C( 91765374803), INT64_C( 91848233687), INT64_C( 91931167388), INT64_C( 92014175974),
    INT64_C( 92097259511), INT64_C( 92180418069), INT64_C( 92263651713), INT64_C( 92346960513), INT64_C( 92430344536), INT64_C( 92513803850), INT64_C( 92597338523), INT64_C( 92680948623),
    INT64_C( 92764634219), INT64_C( 92848395377), INT64_C( 92932232167), INT64_C( 93016144657), INT64_C( 93100132915), INT64_C( 93184197009), INT64_C( 93268337009), INT64_C( 93352552982),
    INT64_C( 93436844998), INT64_C( 93521213124), INT64_C( 93605657430), INT64_C( 93690177985), INT64_C( 93774774856), INT64_C( 93859448114), INT64_C( 93944197827), INT64_C( 94029024063),
    INT64_C( 94113926894), INT64_C( 94198906386), INT64_C( 94283962610), INT64_C( 94369095636), INT64_C( 94454305531), INT64_C( 94539592366), INT64_C( 94624956210), INT64_C( 94710397133),
    INT64_C( 94795915204), INT64_C( 94881510493), INT64_C( 94967183070), INT64_C( 95052933004), INT64_C( 95138760365), INT64_C( 95224665224), INT64_C( 95310647649), INT64_C( 95396707712),
    INT64_C( 95482845483), INT64_C( 95569061030), INT64_C( 95655354426), INT64_C( 95741725739), INT64_C( 95828175041), INT64_C( 95914702402), INT64_C( 96001307892), INT64_C( 96087991581),
    INT64_C( 96174753541), INT64_C( 96261593842), INT64_C( 96348512555), INT64_C( 96435509751), INT64_C( 96522585500), INT64_C( 96609739874), INT64_C( 96696972943), INT64_C( 96784284778),
    INT64_C( 96871675451), INT64_C( 96959145033), INT64_C( 97046693595), INT64_C( 97134321208), INT64_C( 97222027944), INT64_C( 97309813874), INT64_C( 97397679069), INT64_C( 97485623602),
    INT64_C( 97573647544), INT64_C( 97661750966), INT64_C( 97749933941), INT64_C( 97838196540), INT64_C( 97926538835), INT64_C( 98014960898), INT64_C( 98103462801), INT64_C( 98192044617),
    INT64_C( 98280706416), INT64_C( 98369448272), INT64_C( 98458270257), INT64_C( 98547172444), INT64_C( 98636154903), INT64_C( 98725217709), INT64_C( 98814360933), INT64_C( 98903584649),
    INT64_C( 98992888929), INT64_C( 99082273845), INT64_C( 99171739471), INT64_C( 99261285879), INT64_C( 99350913142), INT64_C( 99440621333), INT64_C( 99530410526), INT64_C( 99620280794),
    INT64_C( 99710232209), INT64_C( 99800264845), INT64_C( 99890378776), INT64_C( 99980574074), INT64_C(100070850813), INT64_C(100161209067), INT64_C(100251648910), INT64_C(100342170414),
    INT64_C(100432773655), INT64_C(100523458704), INT64_C(100614225637), INT64_C(100705074528), INT64_C(100796005450), INT64_C(100887018477), INT64_C(100978113684), INT64_C(101069291145),
    INT64_C(101160550933), INT64_C(101251893124), INT64_C(101343317792), INT64_C(101434825011), INT64_C(101526414856), INT64_C(101618087401), INT64_C(101709842721), INT64_C(101801680891),
    INT64_C(101893601986), INT64_C(101985606080), INT64_C(102077693249), INT64_C(102169863567), INT64_C(102262117109), INT64_C(102354453952), INT64_C(102446874169), INT64_C(102539377836),
    INT64_C(102631965029), INT64_C(102724635823), INT64_C(102817390293), INT64_C(102910228515), INT64_C(103003150565), INT64_C(103096156518), INT64_C(103189246450), INT64_C(103282420437),
    INT64_C(103375678555), INT64_C(103469020879), INT64_C(103562447487), INT64_C(103655958453), INT64_C(103749553854), INT64_C(103843233767), INT64_C(103936998267), INT64_C(104030847431),
    INT64_C(104124781336), INT64_C(104218800057), INT64_C(104312903672), INT64_C(104407092257), INT64_C(104501365890), INT64_C(104595724645), INT64_C(104690168602), INT64_C(104784697836),
    INT64_C(104879312424), INT64_C(104974012444), INT64_C(105068797972), INT64_C(105163669087), INT64_C(105258625865), INT64_C(105353668383), INT64_C(105448796719), INT64_C(105544010951),
    INT64_C(105639311155), INT64_C(105734697411), INT64_C(105830169794), INT64_C(105925728384), INT64_C(106021373258), INT64_C(106117104493), INT64_C(106212922169), INT64_C(106308826362),
    INT64_C(106404817151), INT64_C(106500894615), INT64_C(106597058830), INT64_C(106693309877), INT64_C(106789647833), INT64_C(106886072776), INT64_C(106982584786), INT64_C(107079183941),
    INT64_C(107175870319), INT64_C(107272643999), INT64_C(107369505061), INT64_C(107466453582), INT64_C(107563489642), INT64_C(107660613321), INT64_C(107757824696), INT64_C(107855123848),
    INT64_C(107952510856), INT64_C(108049985798), INT64_C(108147548754), INT64_C(108245199805), INT64_C(108342939029), INT64_C(108440766505), INT64_C(108538682314), INT64_C(108636686536),
    INT64_C(108734779250), INT64_C(108832960536), INT64_C(108931230474), INT64_C(109029589144), INT64_C(109128036626), INT64_C(109226573001), INT64_C(109325198349), INT64_C(109423912749),
    INT64_C(109522716283), INT64_C(109621609031), INT64_C(109720591074), INT64_C(109819662491), INT64_C(109918823365), INT64_C(110018073775), INT64_C(110117413802), INT64_C(110216843528),
    INT64_C(110316363033), INT64_C(110415972399), INT64_C(110515671706), INT64_C(110615461036), INT64_C(110715340470), INT64_C(110815310090), INT64_C(110915369976), INT64_C(111015520210),
    INT64_C(111115760875), INT64_C(111216092051), INT64_C(111316513821), INT64_C(111417026265), INT64_C(111517629467), INT64_C(111618323507), INT64_C(111719108469), INT64_C(111819984433),
    INT64_C(111920951483), INT64_C(112022009700), INT64_C(112123159167), INT64_C(112224399966), INT64_C(112325732180), INT64_C(112427155891), INT64_C(112528671182), INT64_C(112630278135),
    INT64_C(112731976834), INT64_C(112833767360), INT64_C(112935649798), INT64_C(113037624230), INT64_C(113139690739), INT64_C(113241849408), INT64_C(113344100320), INT64_C(113446443559),
    INT64_C(113548879209), INT64_C(113651407351), INT64_C(113754028071), INT64_C(113856741452), INT64_C(113959547577), INT64_C(114062446529), INT64_C(114165438394), INT64_C(114268523255),
    INT64_C(114371701195), INT64_C(114474972299), INT64_C(114578336651), INT64_C(114681794335), INT64_C(114785345435), INT64_C(114888990036), INT64_C(114992728223), INT64_C(115096560078),
    INT64_C(115200485688), INT64_C(115304505137), INT64_C(115408618510), INT64_C(115512825891), INT64_C(115617127365), INT64_C(115721523018), INT64_C(115826012934), INT64_C(115930597198),
    INT64_C(116035275896), INT64_C(116140049113), INT64_C(116244916934), INT64_C(116349879445), INT64_C(116454936730), INT64_C(116560088877), INT64_C(116665335970), INT64_C(116770678095),
    INT64_C(116876115338), INT64_C(116981647785), INT64_C(117087275521), INT64_C(117192998633), INT64_C(117298817207), INT64_C(117404731330), INT64_C(117510741087), INT64_C(117616846564),
    INT64_C(117723047849), INT64_C(117829345027), INT64_C(117935738186), INT64_C(118042227411), INT64_C(118148812790), INT64_C(118255494410), INT64_C(118362272357), INT64_C(118469146719),
    INT64_C(118576117582), INT64_C(118683185033), INT64_C(118790349160), INT64_C(118897610051), INT64_C(119004967792), INT64_C(119112422470), INT64_C(119219974174), INT64_C(119327622992),
    INT64_C(119435369009), INT64_C(119543212316), INT64_C(119651152999), INT64_C(119759191146), INT64_C(119867326845), INT64_C(119975560184), INT64_C(120083891253), INT64_C(120192320137),
    INT64_C(120300846927), INT64_C(120409471711), INT64_C(120518194576), INT64_C(120627015612), INT64_C(120735934907), INT64_C(120844952550), INT64_C(120954068630), INT64_C(121063283235),
    INT64_C(121172596455), INT64_C(121282008378), INT64_C(121391519095), INT64_C(121501128693), INT64_C(121610837262), INT64_C(121720644892), INT64_C(121830551671), INT64_C(121940557691),
    INT64_C(122050663039), INT64_C(122160867807), INT64_C(122271172082), INT64_C(122381575957), INT64_C(122492079520), INT64_C(122602682861), INT64_C(122713386070), INT64_C(122824189239),
    INT64_C(122935092456), INT64_C(123046095812), INT64_C(123157199398), INT64_C(123268403305), INT64_C(123379707622), INT64_C(123491112440), INT64_C(123602617851), INT64_C(123714223945),
    INT64_C(123825930812), INT64_C(123937738545), INT64_C(124049647234), INT64_C(124161656969), INT64_C(124273767843), INT64_C(124385979947), INT64_C(124498293372), INT64_C(124610708209),
    INT64_C(124723224551), INT64_C(124835842489), INT64_C(124948562114), INT64_C(125061383518), INT64_C(125174306794), INT64_C(125287332033), INT64_C(125400459327), INT64_C(125513688769),
    INT64_C(125627020450), INT64_C(125740454464), INT64_C(125853990901), INT64_C(125967629856), INT64_C(126081371421), INT64_C(126195215687), INT64_C(126309162748), INT64_C(126423212697),
    INT64_C(126537365627), INT64_C(126651621630), INT64_C(126765980800), INT64_C(126880443229), INT64_C(126995009012), INT64_C(127109678240), INT64_C(127224451009), INT64_C(127339327411),
    INT64_C(127454307540), INT64_C(127569391489), INT64_C(127684579352), INT64_C(127799871224), INT64_C(127915267197), INT64_C(128030767367), INT64_C(128146371826), INT64_C(128262080670),
    INT64_C(128377893992), INT64_C(128493811887), INT64_C(128609834449), INT64_C(128725961773), INT64_C(128842193953), INT64_C(128958531084), INT64_C(129074973261), INT64_C(129191520579),
    INT64_C(129308173132), INT64_C(129424931016), INT64_C(129541794325), INT64_C(129658763156), INT64_C(129775837602), INT64_C(129893017760), INT64_C(130010303725), INT64_C(130127695593),
    INT64_C(130245193459), INT64_C(130362797418), INT64_C(130480507567), INT64_C(130598324002), INT64_C(130716246818), INT64_C(130834276112), INT64_C(130952411979), INT64_C(131070654517),
    INT64_C(131189003821), INT64_C(131307459987), INT64_C(131426023112), INT64_C(131544693293), INT64_C(131663470627), INT64_C(131782355210), INT64_C(131901347138), INT64_C(132020446510),
    INT64_C(132139653421), INT64_C(132258967970), INT64_C(132378390253), INT64_C(132497920367), INT64_C(132617558410), INT64_C(132737304479), INT64_C(132857158673), INT64_C(132977121087),
    INT64_C(133097191821), INT64_C(133217370972), INT64_C(133337658638), INT64_C(133458054917), INT64_C(133578559906), INT64_C(133699173705), INT64_C(133819896411), INT64_C(133940728123),
    INT64_C(134061668939), INT64_C(134182718958), INT64_C(134303878277), INT64_C(134425146997), INT64_C(134546525216), INT64_C(134668013032), INT64_C(134789610544), INT64_C(134911317853),
    INT64_C(135033135055), INT64_C(135155062252), INT64_C(135277099543), INT64_C(135399247026), INT64_C(135521504801), INT64_C(135643872967), INT64_C(135766351626), INT64_C(135888940875),
    INT64_C(136011640816), INT64_C(136134451548), INT64_C(136257373170), INT64_C(136380405784), INT64_C(136503549490), INT64_C(136626804387), INT64_C(136750170576), INT64_C(136873648158),
    INT64_C(136997237233), INT64_C(137120937901), INT64_C(137244750265), INT64_C(137368674424), INT64_C(137492710479), INT64_C(137616858532), INT64_C(137741118683), INT64_C(137865491035),
    INT64_C(137989975687), INT64_C(138114572742), INT64_C(138239282301), INT64_C(138364104465), INT64_C(138489039337), INT64_C(138614087018), INT64_C(138739247610), INT64_C(138864521214),
    INT64_C(138989907934), INT64_C(139115407870), INT64_C(139241021126), INT64_C(139366747803), INT64_C(139492588005), INT64_C(139618541832), INT64_C(139744609389), INT64_C(139870790778),
    INT64_C(139997086101), INT64_C(140123495462), INT64_C(140250018963), INT64_C(140376656708), INT64_C(140503408799), INT64_C(140630275341), INT64_C(140757256435), INT64_C(140884352186),
    INT64_C(141011562697), INT64_C(141138888073), INT64_C(141266328415), INT64_C(141393883829), INT64_C(141521554418), INT64_C(141649340287), INT64_C(141777241539), INT64_C(141905258278),
    INT64_C(142033390609), INT64_C(142161638636), INT64_C(142290002464), INT64_C(142418482197), INT64_C(142547077940), INT64_C(142675789798), INT64_C(142804617875), INT64_C(142933562276),
    INT64_C(143062623107), INT64_C(143191800472), INT64_C(143321094477), INT64_C(143450505228), INT64_C(143580032828), INT64_C(143709677385), INT64_C(143839439003), INT64_C(143969317789),
    INT64_C(144099313848), INT64_C(144229427286), INT64_C(144359658208), INT64_C(144490006722), INT64_C(144620472933), INT64_C(144751056948), INT64_C(144881758872), INT64_C(145012578813),
    INT64_C(145143516877), INT64_C(145274573170), INT64_C(145405747800), INT64_C(145537040873), INT64_C(145668452496), INT64_C(145799982776), INT64_C(145931631821), INT64_C(146063399737),
    INT64_C(146195286632), INT64_C(146327292613), INT64_C(146459417788), INT64_C(146591662265), INT64_C(146724026151), INT64_C(146856509554), INT64_C(146989112582), INT64_C(147121835342),
    INT64_C(147254677944), INT64_C(147387640495), INT64_C(147520723104), INT64_C(147653925878), INT64_C(147787248927), INT64_C(147920692360), INT64_C(148054256284), INT64_C(148187940808),
    INT64_C(148321746042), INT64_C(148455672095), INT64_C(148589719075), INT64_C(148723887092), INT64_C(148858176255), INT64_C(148992586673), INT64_C(149127118456), INT64_C(149261771714),
    INT64_C(149396546556), INT64_C(149531443092), INT64_C(149666461432), INT64_C(149801601685), INT64_C(149936863963), INT64_C(150072248374), INT64_C(150207755030), INT64_C(150343384041),
    INT64_C(150479135517), INT64_C(150615009569), INT64_C(150751006307), INT64_C(150887125843), INT64_C(151023368286), INT64_C(151159733749), INT64_C(151296222342), INT64_C(151432834177),
    INT64_C(151569569364), INT64_C(151706428015), INT64_C(151843410242), INT64_C(151980516156), INT64_C(152117745868), INT64_C(152255099492), INT64_C(152392577137), INT64_C(152530178918),
    INT64_C(152667904944), INT64_C(152805755330), INT64_C(152943730186), INT64_C(153081829626), INT64_C(153220053762), INT64_C(153358402706), INT64_C(153496876571), INT64_C(153635475471),
    INT64_C(153774199517), INT64_C(153913048823), INT64_C(154052023502), INT64_C(154191123667), INT64_C(154330349432), INT64_C(154469700910), INT64_C(154609178214), INT64_C(154748781458),
    INT64_C(154888510756), INT64_C(155028366221), INT64_C(155168347968), INT64_C(155308456111), INT64_C(155448690763), INT64_C(155589052039), INT64_C(155729540053), INT64_C(155870154920),
    INT64_C(156010896753), INT64_C(156151765669), INT64_C(156292761782), INT64_C(156433885205), INT64_C(156575136056), INT64_C(156716514447), INT64_C(156858020495), INT64_C(156999654315),
    INT64_C(157141416023), INT64_C(157283305733), INT64_C(157425323561), INT64_C(157567469623), INT64_C(157709744035), INT64_C(157852146913), INT64_C(157994678372), INT64_C(158137338530),
    INT64_C(158280127501), INT64_C(158423045402), INT64_C(158566092350), INT64_C(158709268461), INT64_C(158852573852), INT64_C(158996008640), INT64_C(159139572941), INT64_C(159283266873),
    INT64_C(159427090551), INT64_C(159571044095), INT64_C(159715127620), INT64_C(159859341244), INT64_C(160003685085), INT64_C(160148159261), INT64_C(160292763888), INT64_C(160437499085),
    INT64_C(160582364969), INT64_C(160727361659), INT64_C(160872489273), INT64_C(161017747929), INT64_C(161163137745), INT64_C(161308658839), INT64_C(161454311331), INT64_C(161600095339),
    INT64_C(161746010982), INT64_C(161892058377), INT64_C(162038237646), INT64_C(162184548906), INT64_C(162330992276), INT64_C(162477567877), INT64_C(162624275827), INT64_C(162771116245),
    INT64_C(162918089253), INT64_C(163065194968), INT64_C(163212433512), INT64_C(163359805003), INT64_C(163507309563), INT64_C(163654947311), INT64_C(163802718367), INT64_C(163950622852),
    INT64_C(164098660886), INT64_C(164246832590), INT64_C(164395138085), INT64_C(164543577491), INT64_C(164692150930), INT64_C(164840858521), INT64_C(164989700387), INT64_C(165138676649),
    INT64_C(165287787428), INT64_C(165437032846), INT64_C(165586413023), INT64_C(165735928082), INT64_C(165885578145), INT64_C(166035363333), INT64_C(166185283769), INT64_C(166335339574),
    INT64_C(166485530871), INT64_C(166635857783), INT64_C(166786320431), INT64_C(166936918938), INT64_C(167087653426), INT64_C(167238524020), INT64_C(167389530841), INT64_C(167540674012),
    INT64_C(167691953657), INT64_C(167843369899), INT64_C(167994922862), INT64_C(168146612667), INT64_C(168298439441), INT64_C(168450403305), INT64_C(168602504383), INT64_C(168754742800),
    INT64_C(168907118680), INT64_C(169059632147), INT64_C(169212283324), INT64_C(169365072337), INT64_C(169517999309), INT64_C(169671064366), INT64_C(169824267632), INT64_C(169977609231),
    INT64_C(170131089290), INT64_C(170284707932), INT64_C(170438465282), INT64_C(170592361467), INT64_C(170746396612), INT64_C(170900570841), INT64_C(171054884281), INT64_C(171209337057),
    INT64_C(171363929295), INT64_C(171518661121), INT64_C(171673532661), INT64_C(171828544041), INT64_C(171983695387), INT64_C(172138986827), INT64_C(172294418485), INT64_C(172449990490),
    INT64_C(172605702967), INT64_C(172761556043), INT64_C(172917549846), INT64_C(173073684503), INT64_C(173229960140), INT64_C(173386376885), INT64_C(173542934865), INT64_C(173699634209),
    INT64_C(173856475043), INT64_C(174013457495), INT64_C(174170581693), INT64_C(174327847766), INT64_C(174485255840), INT64_C(174642806046), INT64_C(174800498510), INT64_C(174958333361),
    INT64_C(175116310728), INT64_C(175274430740), INT64_C(175432693525), INT64_C(175591099213), INT64_C(175749647931), INT64_C(175908339810), INT64_C(176067174979), INT64_C(176226153567),
    INT64_C(176385275703), INT64_C(176544541518), INT64_C(176703951140), INT64_C(176863504700), INT64_C(177023202328), INT64_C(177183044154), INT64_C(177343030308), INT64_C(177503160920),
    INT64_C(177663436121), INT64_C(177823856041), INT64_C(177984420812), INT64_C(178145130563), INT64_C(178305985426), INT64_C(178466985531), INT64_C(178628131011), INT64_C(178789421995),
    INT64_C(178950858616), INT64_C(179112441005), INT64_C(179274169294), INT64_C(179436043614), INT64_C(179598064098), INT64_C(179760230877), INT64_C(179922544083), INT64_C(180085003848),
    INT64_C(180247610306), INT64_C(180410363587), INT64_C(180573263826), INT64_C(180736311154), INT64_C(180899505705), INT64_C(181062847611), INT64_C(181226337005), INT64_C(181389974021),
    INT64_C(181553758792), INT64_C(181717691451), INT64_C(181881772132), INT64_C(182046000969), INT64_C(182210378094), INT64_C(182374903643), INT64_C(182539577749), INT64_C(182704400546),
    INT64_C(182869372169), INT64_C(183034492752), INT64_C(183199762429), INT64_C(183365181335), INT64_C(183530749605), INT64_C(183696467374), INT64_C(183862334777), INT64_C(184028351948),
    INT64_C(184194519023), INT64_C(184360836138), INT64_C(184527303427), INT64_C(184693921027), INT64_C(184860689073), INT64_C(185027607701), INT64_C(185194677047), INT64_C(185361897247),
    INT64_C(185529268437), INT64_C(185696790754), INT64_C(185864464334), INT64_C(186032289314), INT64_C(186200265830), INT64_C(186368394019), INT64_C(186536674018), INT64_C(186705105965),
    INT64_C(186873689996), INT64_C(187042426249), INT64_C(187211314860), INT64_C(187380355969), INT64_C(187549549712), INT64_C(187718896228), INT64_C(187888395653), INT64_C(188058048127),
    INT64_C(188227853787), INT64_C(188397812772), INT64_C(188567925221), INT64_C(188738191271), INT64_C(188908611062), INT64_C(189079184732), INT64_C(189249912420), INT64_C(189420794266),
    INT64_C(189591830408), INT64_C(189763020986), INT64_C(189934366139), INT64_C(190105866007), INT64_C(190277520730), INT64_C(190449330447), INT64_C(190621295299), INT64_C(190793415425),
    INT64_C(190965690965), INT64_C(191138122061), INT64_C(191310708852), INT64_C(191483451479), INT64_C(191656350082), INT64_C(191829404803), INT64_C(192002615783), INT64_C(192175983162),
    INT64_C(192349507083), INT64_C(192523187685), INT64_C(192697025111), INT64_C(192871019502), INT64_C(193045171000), INT64_C(193219479747), INT64_C(193393945885), INT64_C(193568569556),
    INT64_C(193743350902), INT64_C(193918290065), INT64_C(194093387189), INT64_C(194268642415), INT64_C(194444055887), INT64_C(194619627747), INT64_C(194795358139), INT64_C(194971247204),
    INT64_C(195147295088), INT64_C(195323501933), INT64_C(195499867883), INT64_C(195676393080), INT64_C(195853077670), INT64_C(196029921797), INT64_C(196206925603), INT64_C(196384089233),
    INT64_C(196561412833), INT64_C(196738896545), INT64_C(196916540515), INT64_C(197094344887), INT64_C(197272309807), INT64_C(197450435418), INT64_C(197628721867), INT64_C(197807169298),
    INT64_C(197985777857), INT64_C(198164547690), INT64_C(198343478941), INT64_C(198522571757), INT64_C(198701826284), INT64_C(198881242667), INT64_C(199060821053), INT64_C(199240561588),
    INT64_C(199420464418), INT64_C(199600529690), INT64_C(199780757551), INT64_C(199961148148), INT64_C(200141701627), INT64_C(200322418135), INT64_C(200503297820), INT64_C(200684340829),
    INT64_C(200865547309), INT64_C(201046917409), INT64_C(201228451275), INT64_C(201410149056), INT64_C(201592010900), INT64_C(201774036954), INT64_C(201956227368), INT64_C(202138582289),
    INT64_C(202321101866), INT64_C(202503786248), INT64_C(202686635584), INT64_C(202869650022), INT64_C(203052829712), INT64_C(203236174802), INT64_C(203419685443), INT64_C(203603361783),
    INT64_C(203787203972), INT64_C(203971212160), INT64_C(204155386498), INT64_C(204339727134), INT64_C(204524234219), INT64_C(204708907903), INT64_C(204893748338), INT64_C(205078755672),
    INT64_C(205263930058), INT64_C(205449271646), INT64_C(205634780586), INT64_C(205820457030), INT64_C(206006301130), INT64_C(206192313036), INT64_C(206378492900), INT64_C(206564840874),
    INT64_C(206751357110), INT64_C(206938041759), INT64_C(207124894974), INT64_C(207311916906), INT64_C(207499107709), INT64_C(207686467534), INT64_C(207873996534), INT64_C(208061694862),
    INT64_C(208249562671), INT64_C(208437600114), INT64_C(208625807344), INT64_C(208814184515), INT64_C(209002731779), INT64_C(209191449291), INT64_C(209380337203), INT64_C(209569395671),
    INT64_C(209758624848), INT64_C(209948024888), INT64_C(210137595945), INT64_C(210327338174), INT64_C(210517251729), INT64_C(210707336766), INT64_C(210897593438), INT64_C(211088021902),
    INT64_C(211278622311), INT64_C(211469394822), INT64_C(211660339589), INT64_C(211851456768), INT64_C(212042746516), INT64_C(212234208987), INT64_C(212425844337), INT64_C(212617652724),
    INT64_C(212809634302), INT64_C(213001789229), INT64_C(213194117661), INT64_C(213386619754), INT64_C(213579295666), INT64_C(213772145553), INT64_C(213965169572), INT64_C(214158367882),
    INT64_C(214351740638), INT64_C(214545287998), INT64_C(214739010121), INT64_C(214932907164), INT64_C(215126979285), INT64_C(215321226642), INT64_C(215515649393), INT64_C(215710247696),
    INT64_C(215905021711), INT64_C(216099971596), INT64_C(216295097509), INT64_C(216490399610), INT64_C(216685878057), INT64_C(216881533010), INT64_C(217077364629), INT64_C(217273373072),
    INT64_C(217469558500), INT64_C(217665921071), INT64_C(217862460948), INT64_C(218059178288), INT64_C(218256073253), INT64_C(218453146002), INT64_C(218650396697), INT64_C(218847825499),
    INT64_C(219045432567), INT64_C(219243218063), INT64_C(219441182148), INT64_C(219639324983), INT64_C(219837646730), INT64_C(220036147550), INT64_C(220234827605), INT64_C(220433687056),
    INT64_C(220632726067), INT64_C(220831944798), INT64_C(221031343412), INT64_C(221230922072), INT64_C(221430680940), INT64_C(221630620179), INT64_C(221830739952), INT64_C(222031040421),
    INT64_C(222231521750), INT64_C(222432184102), INT64_C(222633027641), INT64_C(222834052530), INT64_C(223035258933), INT64_C(223236647014), INT64_C(223438216937), INT64_C(223639968866),
    INT64_C(223841902966), INT64_C(224044019400), INT64_C(224246318334), INT64_C(224448799932), INT64_C(224651464360), INT64_C(224854311782), INT64_C(225057342364), INT64_C(225260556270),
    INT64_C(225463953667), INT64_C(225667534721), INT64_C(225871299596), INT64_C(226075248460), INT64_C(226279381477), INT64_C(226483698815), INT64_C(226688200640), INT64_C(226892887119),
    INT64_C(227097758417), INT64_C(227302814703), INT64_C(227508056143), INT64_C(227713482904), INT64_C(227919095153), INT64_C(228124893059), INT64_C(228330876788), INT64_C(228537046509),
    INT64_C(228743402390), INT64_C(228949944598), INT64_C(229156673302), INT64_C(229363588670), INT64_C(229570690870), INT64_C(229777980073), INT64_C(229985456445), INT64_C(230193120157),
    INT64_C(230400971377), INT64_C(230609010275), INT64_C(230817237020), INT64_C(231025651782), INT64_C(231234254731), INT64_C(231443046036), INT64_C(231652025868), INT64_C(231861194396),
    INT64_C(232070551792), INT64_C(232280098226), INT64_C(232489833868), INT64_C(232699758889), INT64_C(232909873461), INT64_C(233120177754), INT64_C(233330671940), INT64_C(233541356190),
    INT64_C(233752230676), INT64_C(233963295569), INT64_C(234174551042), INT64_C(234385997267), INT64_C(234597634415), INT64_C(234809462660), INT64_C(235021482173), INT64_C(235233693128),
    INT64_C(235446095697), INT64_C(235658690054), INT64_C(235871476371), INT64_C(236084454822), INT64_C(236297625581), INT64_C(236510988820), INT64_C(236724544714), INT64_C(236938293437),
    INT64_C(237152235163), INT64_C(237366370066), INT64_C(237580698321), INT64_C(237795220101), INT64_C(238009935583), INT64_C(238224844941), INT64_C(238439948349), INT64_C(238655245983),
    INT64_C(238870738019), INT64_C(239086424632), INT64_C(239302305997), INT64_C(239518382291), INT64_C(239734653690), INT64_C(239951120369), INT64_C(240167782505), INT64_C(240384640275),
    INT64_C(240601693855), INT64_C(240818943422), INT64_C(241036389153), INT64_C(241254031224), INT64_C(241471869815), INT64_C(241689905101), INT64_C(241908137260), INT64_C(242126566471),
    INT64_C(242345192910), INT64_C(242564016757), INT64_C(242783038189), INT64_C(243002257385), INT64_C(243221674524), INT64_C(243441289783), INT64_C(243661103343), INT64_C(243881115382),
    INT64_C(244101326078), INT64_C(244321735613), INT64_C(244542344165), INT64_C(244763151914), INT64_C(244984159039), INT64_C(245205365721), INT64_C(245426772141), INT64_C(245648378477),
    INT64_C(245870184912), INT64_C(246092191625), INT64_C(246314398797), INT64_C(246536806610), INT64_C(246759415244), INT64_C(246982224881), INT64_C(247205235702), INT64_C(247428447890),
    INT64_C(247651861625), INT64_C(247875477090), INT64_C(248099294467), INT64_C(248323313939), INT64_C(248547535687), INT64_C(248771959894), INT64_C(248996586744), INT64_C(249221416419),
    INT64_C(249446449102), INT64_C(249671684977), INT64_C(249897124227), INT64_C(250122767036), INT64_C(250348613587), INT64_C(250574664065), INT64_C(250800918654), INT64_C(251027377537),
    INT64_C(251254040900), INT64_C(251480908927), INT64_C(251707981803), INT64_C(251935259713), INT64_C(252162742841), INT64_C(252390431374), INT64_C(252618325496), INT64_C(252846425394),
    INT64_C(253074731253), INT64_C(253303243260), INT64_C(253531961599), INT64_C(253760886458), INT64_C(253990018023), INT64_C(254219356481), INT64_C(254448902018), INT64_C(254678654822),
    INT64_C(254908615079), INT64_C(255138782978), INT64_C(255369158705), INT64_C(255599742448), INT64_C(255830534395), INT64_C(256061534733), INT64_C(256292743652), INT64_C(256524161339),
    INT64_C(256755787984), INT64_C(256987623774), INT64_C(257219668898), INT64_C(257451923546), INT64_C(257684387906), INT64_C(257917062169), INT64_C(258149946523), INT64_C(258383041158),
    INT64_C(258616346264), INT64_C(258849862032), INT64_C(259083588651), INT64_C(259317526311), INT64_C(259551675205), INT64_C(259786035521), INT64_C(260020607451), INT64_C(260255391186),
    INT64_C(260490386917), INT64_C(260725594836), INT64_C(260961015135), INT64_C(261196648004), INT64_C(261432493636), INT64_C(261668552224), INT64_C(261904823959), INT64_C(262141309034),
    INT64_C(262378007641), INT64_C(262614919974), INT64_C(262852046225), INT64_C(263089386587), INT64_C(263326941254), INT64_C(263564710419), INT64_C(263802694277), INT64_C(264040893020),
    INT64_C(264279306843), INT64_C(264517935940), INT64_C(264756780505), INT64_C(264995840734), INT64_C(265235116820), INT64_C(265474608959), INT64_C(265714317345), INT64_C(265954242175),
    INT64_C(266194383642), INT64_C(266434741944), INT64_C(266675317276), INT64_C(266916109833), INT64_C(267157119813), INT64_C(267398347410), INT64_C(267639792822), INT64_C(267881456246),
    INT64_C(268123337878), INT64_C(268365437915), INT64_C(268607756555), INT64_C(268850293994), INT64_C(269093050431), INT64_C(269336026063), INT64_C(269579221089), INT64_C(269822635705),
    INT64_C(270066270111), INT64_C(270310124505), INT64_C(270554199085), INT64_C(270798494051), INT64_C(271043009601), INT64_C(271287745935), INT64_C(271532703252), INT64_C(271777881751),
    INT64_C(272023281632), INT64_C(272268903095), INT64_C(272514746341), INT64_C(272760811568), INT64_C(273007098979), INT64_C(273253608773), INT64_C(273500341152), INT64_C(273747296315),
    INT64_C(273994474465), INT64_C(274241875803), INT64_C(274489500530), INT64_C(274737348848), INT64_C(274985420958), INT64_C(275233717064), INT64_C(275482237367), INT64_C(275730982069),
    INT64_C(275979951374), INT64_C(276229145484), INT64_C(276478564602), INT64_C(276728208931), INT64_C(276978078674), INT64_C(277228174036), INT64_C(277478495219), INT64_C(277729042428),
    INT64_C(277979815867), INT64_C(278230815740), INT64_C(278482042252), INT64_C(278733495606), INT64_C(278985176009), INT64_C(279237083665), INT64_C(279489218779), INT64_C(279741581556),
    INT64_C(279994172203), INT64_C(280246990924), INT64_C(280500037927), INT64_C(280753313416), INT64_C(281006817599), INT64_C(281260550681), INT64_C(281514512870), INT64_C(281768704372),
    INT64_C(282023125395), INT64_C(282277776145), INT64_C(282532656830), INT64_C(282787767658), INT64_C(283043108837), INT64_C(283298680574), INT64_C(283554483077), INT64_C(283810516556),
    INT64_C(284066781218), INT64_C(284323277273), INT64_C(284580004928), INT64_C(284836964395), INT64_C(285094155880), INT64_C(285351579596), INT64_C(285609235750), INT64_C(285867124552),
    INT64_C(286125246214), INT64_C(286383600945), INT64_C(286642188955), INT64_C(286901010455), INT64_C(287160065657), INT64_C(287419354770), INT64_C(287678878007), INT64_C(287938635578),
    INT64_C(288198627696), INT64_C(288458854571), INT64_C(288719316417), INT64_C(288980013444), INT64_C(289240945866), INT64_C(289502113896), INT64_C(289763517745), INT64_C(290025157626),
    INT64_C(290287033754), INT64_C(290549146341), INT64_C(290811495600), INT64_C(291074081746), INT64_C(291336904992), INT64_C(291599965553), INT64_C(291863263642), INT64_C(292126799474),
    INT64_C(292390573264), INT64_C(292654585226), INT64_C(292918835577), INT64_C(293183324530), INT64_C(293448052302), INT64_C(293713019107), INT64_C(293978225163), INT64_C(294243670684),
    INT64_C(294509355888), INT64_C(294775280990), INT64_C(295041446207), INT64_C(295307851757), INT64_C(295574497855), INT64_C(295841384719), INT64_C(296108512567), INT64_C(296375881616),
    INT64_C(296643492085), INT64_C(296911344190), INT64_C(297179438150), INT64_C(297447774183), INT64_C(297716352509), INT64_C(297985173346), INT64_C(298254236912), INT64_C(298523543428),
    INT64_C(298793093112), INT64_C(299062886184), INT64_C(299332922863), INT64_C(299603203370), INT64_C(299873727925), INT64_C(300144496748), INT64_C(300415510060), INT64_C(300686768082),
    INT64_C(300958271034), INT64_C(301230019138), INT64_C(301502012614), INT64_C(301774251685), INT64_C(302046736573), INT64_C(302319467499), INT64_C(302592444685), INT64_C(302865668354),
    INT64_C(303139138728), INT64_C(303412856030), INT64_C(303686820484), INT64_C(303961032312), INT64_C(304235491737), INT64_C(304510198983), INT64_C(304785154275), INT64_C(305060357835),
    INT64_C(305335809888), INT64_C(305611510659), INT64_C(305887460372), INT64_C(306163659252), INT64_C(306440107523), INT64_C(306716805412), INT64_C(306993753142), INT64_C(307270950941),
    INT64_C(307548399033), INT64_C(307826097646), INT64_C(308104047004), INT64_C(308382247335), INT64_C(308660698864), INT64_C(308939401819), INT64_C(309218356428), INT64_C(309497562916),
    INT64_C(309777021512), INT64_C(310056732442), INT64_C(310336695936), INT64_C(310616912221), INT64_C(310897381525), INT64_C(311178104077), INT64_C(311459080105), INT64_C(311740309839),
    INT64_C(312021793507), INT64_C(312303531339), INT64_C(312585523563), INT64_C(312867770411), INT64_C(313150272111), INT64_C(313433028894), INT64_C(313716040991), INT64_C(313999308631),
    INT64_C(314282832045), INT64_C(314566611466), INT64_C(314850647122), INT64_C(315134939247), INT64_C(315419488071), INT64_C(315704293826), INT64_C(315989356745), INT64_C(316274677059),
    INT64_C(316560255001), INT64_C(316846090804), INT64_C(317132184700), INT64_C(317418536923), INT64_C(317705147705), INT64_C(317992017280), INT64_C(318279145882), INT64_C(318566533745),
    INT64_C(318854181103), INT64_C(319142088189), INT64_C(319430255240), INT64_C(319718682489), INT64_C(320007370171), INT64_C(320296318521), INT64_C(320585527775), INT64_C(320874998169),
    INT64_C(321164729938), INT64_C(321454723318), INT64_C(321744978546), INT64_C(322035495857), INT64_C(322326275489), INT64_C(322617317679), INT64_C(322908622663), INT64_C(323200190678),
    INT64_C(323492021963), INT64_C(323784116755), INT64_C(324076475291), INT64_C(324369097811), INT64_C(324661984552), INT64_C(324955135753), INT64_C(325248551653), INT64_C(325542232491),
    INT64_C(325836178505), INT64_C(326130389936), INT64_C(326424867023), INT64_C(326719610007), INT64_C(327014619126), INT64_C(327309894621), INT64_C(327605436734), INT64_C(327901245704),
    INT64_C(328197321772), INT64_C(328493665180), INT64_C(328790276170), INT64_C(329087154982), INT64_C(329384301859), INT64_C(329681717042), INT64_C(329979400775), INT64_C(330277353299),
    INT64_C(330575574856), INT64_C(330874065691), INT64_C(331172826046), INT64_C(331471856165), INT64_C(331771156290), INT64_C(332070726667), INT64_C(332370567538), INT64_C(332670679149),
    INT64_C(332971061743), INT64_C(333271715565), INT64_C(333572640861), INT64_C(333873837875), INT64_C(334175306853), INT64_C(334477048040), INT64_C(334779061682), INT64_C(335081348025),
    INT64_C(335383907315), INT64_C(335686739799), INT64_C(335989845723), INT64_C(336293225335), INT64_C(336596878881), INT64_C(336900806609), INT64_C(337205008766), INT64_C(337509485601),
    INT64_C(337814237360), INT64_C(338119264293), INT64_C(338424566648), INT64_C(338730144674), INT64_C(339035998619), INT64_C(339342128732), INT64_C(339648535264), INT64_C(339955218463),
    INT64_C(340262178579), INT64_C(340569415863), INT64_C(340876930565), INT64_C(341184722935), INT64_C(341492793223), INT64_C(341801141682), INT64_C(342109768562), INT64_C(342418674113),
    INT64_C(342727858590), INT64_C(343037322241), INT64_C(343347065321), INT64_C(343657088082), INT64_C(343967390775), INT64_C(344277973653), INT64_C(344588836970), INT64_C(344899980979),
    INT64_C(345211405933), INT64_C(345523112087), INT64_C(345835099692), INT64_C(346147369005), INT64_C(346459920280), INT64_C(346772753770), INT64_C(347085869731), INT64_C(347399268417),
    INT64_C(347712950085), INT64_C(348026914990), INT64_C(348341163386), INT64_C(348655695531), INT64_C(348970511681), INT64_C(349285612091), INT64_C(349600997020), INT64_C(349916666722),
    INT64_C(350232621457), INT64_C(350548861480), INT64_C(350865387051), INT64_C(351182198425), INT64_C(351499295862), INT64_C(351816679621), INT64_C(352134349958), INT64_C(352452307134),
    INT64_C(352770551406), INT64_C(353089083035), INT64_C(353407902280), INT64_C(353727009401), INT64_C(354046404657), INT64_C(354366088308), INT64_C(354686060616), INT64_C(355006321840),
    INT64_C(355326872242), INT64_C(355647712083), INT64_C(355968841623), INT64_C(356290261125), INT64_C(356611970851), INT64_C(356933971062), INT64_C(357256262021), INT64_C(357578843990),
    INT64_C(357901717233), INT64_C(358224882011), INT64_C(358548338588), INT64_C(358872087229), INT64_C(359196128196), INT64_C(359520461753), INT64_C(359845088165), INT64_C(360170007696),
    INT64_C(360495220611), INT64_C(360820727175), INT64_C(361146527652), INT64_C(361472622309), INT64_C(361799011410), INT64_C(362125695222), INT64_C(362452674011), INT64_C(362779948043),
    INT64_C(363107517584), INT64_C(363435382903), INT64_C(363763544265), INT64_C(364092001937), INT64_C(364420756189), INT64_C(364749807286), INT64_C(365079155498), INT64_C(365408801093),
    INT64_C(365738744338), INT64_C(366068985504), INT64_C(366399524858), INT64_C(366730362671), INT64_C(367061499211), INT64_C(367392934748), INT64_C(367724669553), INT64_C(368056703896),
    INT64_C(368389038046), INT64_C(368721672275), INT64_C(369054606854), INT64_C(369387842054), INT64_C(369721378146), INT64_C(370055215402), INT64_C(370389354094), INT64_C(370723794494),
    INT64_C(371058536874), INT64_C(371393581508), INT64_C(371728928668), INT64_C(372064578628), INT64_C(372400531660), INT64_C(372736788038), INT64_C(373073348037), INT64_C(373410211930),
    INT64_C(373747379992), INT64_C(374084852497), INT64_C(374422629721), INT64_C(374760711938), INT64_C(375099099424), INT64_C(375437792455), INT64_C(375776791306), INT64_C(376116096254),
    INT64_C(376455707575), INT64_C(376795625545), INT64_C(377135850442), INT64_C(377476382542), INT64_C(377817222123), INT64_C(378158369464), INT64_C(378499824840), INT64_C(378841588531),
    INT64_C(379183660816), INT64_C(379526041972), INT64_C(379868732278), INT64_C(380211732015), INT64_C(380555041460), INT64_C(380898660894), INT64_C(381242590597), INT64_C(381586830849),
    INT64_C(381931381930), INT64_C(382276244121), INT64_C(382621417703), INT64_C(382966902957), INT64_C(383312700164), INT64_C(383658809607), INT64_C(384005231566), INT64_C(384351966325),
    INT64_C(384699014165), INT64_C(385046375370), INT64_C(385394050222), INT64_C(385742039004), INT64_C(386090342000), INT64_C(386438959494), INT64_C(386787891770), INT64_C(387137139112),
    INT64_C(387486701804), INT64_C(387836580131), INT64_C(388186774378), INT64_C(388537284831), INT64_C(388888111774), INT64_C(389239255494), INT64_C(389590716277), INT64_C(389942494409),
    INT64_C(390294590176), INT64_C(390647003866), INT64_C(390999735765), INT64_C(391352786161), INT64_C(391706155341), INT64_C(392059843593), INT64_C(392413851206), INT64_C(392768178467),
    INT64_C(393122825665), INT64_C(393477793090), INT64_C(393833081030), INT64_C(394188689774), INT64_C(394544619613), INT64_C(394900870836), INT64_C(395257443734), INT64_C(395614338596),
    INT64_C(395971555715), INT64_C(396329095379), INT64_C(396686957882), INT64_C(397045143514), INT64_C(397403652567), INT64_C(397762485334), INT64_C(398121642106), INT64_C(398481123176),
    INT64_C(398840928836), INT64_C(399201059381), INT64_C(399561515103), INT64_C(399922296296), INT64_C(400283403253), INT64_C(400644836270), INT64_C(401006595640), INT64_C(401368681657),
    INT64_C(401731094618), INT64_C(402093834817), INT64_C(402456902550), INT64_C(402820298112), INT64_C(403184021799), INT64_C(403548073909), INT64_C(403912454736), INT64_C(404277164578),
    INT64_C(404642203733), INT64_C(405007572497), INT64_C(405373271168), INT64_C(405739300044), INT64_C(406105659423), INT64_C(406472349604), INT64_C(406839370885), INT64_C(407206723566),
    INT64_C(407574407944), INT64_C(407942424321), INT64_C(408310772995), INT64_C(408679454268), INT64_C(409048468438), INT64_C(409417815807), INT64_C(409787496676), INT64_C(410157511345),
    INT64_C(410527860116), INT64_C(410898543291), INT64_C(411269561172), INT64_C(411640914061), INT64_C(412012602260), INT64_C(412384626072), INT64_C(412756985801), INT64_C(413129681749),
    INT64_C(413502714220), INT64_C(413876083518), INT64_C(414249789947), INT64_C(414623833812), INT64_C(414998215417), INT64_C(415372935067), INT64_C(415747993068), INT64_C(416123389724),
    INT64_C(416499125343), INT64_C(416875200229), INT64_C(417251614689), INT64_C(417628369030), INT64_C(418005463558), INT64_C(418382898581), INT64_C(418760674407), INT64_C(419138791342),
    INT64_C(419517249696), INT64_C(419896049775), INT64_C(420275191890), INT64_C(420654676348), INT64_C(421034503459), INT64_C(421414673532), INT64_C(421795186877), INT64_C(422176043803),
    INT64_C(422557244622), INT64_C(422938789643), INT64_C(423320679178), INT64_C(423702913537), INT64_C(424085493031), INT64_C(424468417973), INT64_C(424851688675), INT64_C(425235305448),
    INT64_C(425619268605), INT64_C(426003578458), INT64_C(426388235322), INT64_C(426773239508), INT64_C(427158591332), INT64_C(427544291106), INT64_C(427930339145), INT64_C(428316735763),
    INT64_C(428703481275), INT64_C(429090575997), INT64_C(429478020242), INT64_C(429865814328), INT64_C(430253958570), INT64_C(430642453283), INT64_C(431031298785), INT64_C(431420495393),
    INT64_C(431810043422), INT64_C(432199943192), INT64_C(432590195018), INT64_C(432980799219), INT64_C(433371756114), INT64_C(433763066021), INT64_C(434154729258), INT64_C(434546746144),
    INT64_C(434939116999), INT64_C(435331842143), INT64_C(435724921895), INT64_C(436118356576), INT64_C(436512146505), INT64_C(436906292004), INT64_C(437300793395), INT64_C(437695650997),
    INT64_C(438090865133), INT64_C(438486436125), INT64_C(438882364295), INT64_C(439278649966), INT64_C(439675293459), INT64_C(440072295099), INT64_C(440469655209), INT64_C(440867374113),
    INT64_C(441265452133), INT64_C(441663889596), INT64_C(442062686825), INT64_C(442461844144), INT64_C(442861361880), INT64_C(443261240358), INT64_C(443661479903), INT64_C(444062080842),
    INT64_C(444463043500), INT64_C(444864368204), INT64_C(445266055282), INT64_C(445668105060), INT64_C(446070517866), INT64_C(446473294028), INT64_C(446876433874), INT64_C(447279937732),
    INT64_C(447683805931), INT64_C(448088038800), INT64_C(448492636668), INT64_C(448897599865), INT64_C(449302928720), INT64_C(449708623564), INT64_C(450114684727), INT64_C(450521112540),
    INT64_C(450927907335), INT64_C(451335069442), INT64_C(451742599192), INT64_C(452150496919), INT64_C(452558762955), INT64_C(452967397631), INT64_C(453376401281), INT64_C(453785774237),
    INT64_C(454195516834), INT64_C(454605629406), INT64_C(455016112285), INT64_C(455426965807), INT64_C(455838190306), INT64_C(456249786118), INT64_C(456661753577), INT64_C(457074093019),
    INT64_C(457486804780), INT64_C(457899889196), INT64_C(458313346603), INT64_C(458727177339), INT64_C(459141381741), INT64_C(459555960145), INT64_C(459970912890), INT64_C(460386240314),
    INT64_C(460801942754), INT64_C(461218020550), INT64_C(461634474040), INT64_C(462051303564), INT64_C(462468509462), INT64_C(462886092072), INT64_C(463304051736), INT64_C(463722388793),
    INT64_C(464141103585), INT64_C(464560196452), INT64_C(464979667736), INT64_C(465399517779), INT64_C(465819746922), INT64_C(466240355508), INT64_C(466661343880), INT64_C(467082712380),
    INT64_C(467504461351), INT64_C(467926591138), INT64_C(468349102084), INT64_C(468771994533), INT64_C(469195268830), INT64_C(469618925319), INT64_C(470042964346), INT64_C(470467386256),
    INT64_C(470892191395), INT64_C(471317380108), INT64_C(471742952742), INT64_C(472168909644), INT64_C(472595251161), INT64_C(473021977640), INT64_C(473449089429), INT64_C(473876586875),
    INT64_C(474304470326), INT64_C(474732740132), INT64_C(475161396641), INT64_C(475590440203), INT64_C(476019871166), INT64_C(476449689881), INT64_C(476879896698), INT64_C(477310491966),
    INT64_C(477741476038), INT64_C(478172849263), INT64_C(478604611994), INT64_C(479036764582), INT64_C(479469307379), INT64_C(479902240738), INT64_C(480335565010), INT64_C(480769280550),
    INT64_C(481203387710), INT64_C(481637886843), INT64_C(482072778305), INT64_C(482508062449), INT64_C(482943739629), INT64_C(483379810201), INT64_C(483816274520), INT64_C(484253132941),
    INT64_C(484690385820), INT64_C(485128033514), INT64_C(485566076378), INT64_C(486004514771), INT64_C(486443349047), INT64_C(486882579567), INT64_C(487322206686), INT64_C(487762230763),
    INT64_C(488202652157), INT64_C(488643471226), INT64_C(489084688330), INT64_C(489526303827), INT64_C(489968318078), INT64_C(490410731443), INT64_C(490853544281), INT64_C(491296756954),
    INT64_C(491740369823), INT64_C(492184383249), INT64_C(492628797594), INT64_C(493073613219), INT64_C(493518830487), INT64_C(493964449762), INT64_C(494410471404), INT64_C(494856895779),
    INT64_C(495303723250), INT64_C(495750954180), INT64_C(496198588934), INT64_C(496646627877), INT64_C(497095071373), INT64_C(497543919788), INT64_C(497993173488), INT64_C(498442832838),
    INT64_C(498892898205), INT64_C(499343369954), INT64_C(499794248455), INT64_C(500245534072), INT64_C(500697227175), INT64_C(501149328131), INT64_C(501601837308), INT64_C(502054755075),
    INT64_C(502508081800), INT64_C(502961817854), INT64_C(503415963606), INT64_C(503870519425), INT64_C(504325485682), INT64_C(504780862748), INT64_C(505236650993), INT64_C(505692850789),
    INT64_C(506149462507), INT64_C(506606486519), INT64_C(507063923198), INT64_C(507521772916), INT64_C(507980036046), INT64_C(508438712962), INT64_C(508897804037), INT64_C(509357309644),
    INT64_C(509817230159), INT64_C(510277565956), INT64_C(510738317409), INT64_C(511199484895), INT64_C(511661068789), INT64_C(512123069467), INT64_C(512585487304), INT64_C(513048322679),
    INT64_C(513511575967), INT64_C(513975247547), INT64_C(514439337796), INT64_C(514903847091), INT64_C(515368775812), INT64_C(515834124337), INT64_C(516299893045), INT64_C(516766082316),
    INT64_C(517232692529), INT64_C(517699724064), INT64_C(518167177302), INT64_C(518635052623), INT64_C(519103350409), INT64_C(519572071041), INT64_C(520041214901), INT64_C(520510782371),
    INT64_C(520980773834), INT64_C(521451189672), INT64_C(521922030269), INT64_C(522393296008), INT64_C(522864987273), INT64_C(523337104448), INT64_C(523809647918), INT64_C(524282618067),
    INT64_C(524756015282), INT64_C(525229839947), INT64_C(525704092449), INT64_C(526178773174), INT64_C(526653882508), INT64_C(527129420839), INT64_C(527605388553), INT64_C(528081786040),
    INT64_C(528558613686), INT64_C(529035871880), INT64_C(529513561011), INT64_C(529991681467), INT64_C(530470233640), INT64_C(530949217917), INT64_C(531428634690), INT64_C(531908484349),
    INT64_C(532388767285), INT64_C(532869483888), INT64_C(533350634552), INT64_C(533832219667), INT64_C(534314239625), INT64_C(534796694820), INT64_C(535279585645), INT64_C(535762912492),
    INT64_C(536246675756), INT64_C(536730875830), INT64_C(537215513109), INT64_C(537700587989), INT64_C(538186100862), INT64_C(538672052127), INT64_C(539158442177), INT64_C(539645271410),
    INT64_C(540132540222), INT64_C(540620249010), INT64_C(541108398170), INT64_C(541596988102), INT64_C(542086019202), INT64_C(542575491870), INT64_C(543065406503), INT64_C(543555763501),
    INT64_C(544046563264), INT64_C(544537806190), INT64_C(545029492681), INT64_C(545521623137), INT64_C(546014197958), INT64_C(546507217546), INT64_C(547000682303), INT64_C(547494592630),
    INT64_C(547988948930), INT64_C(548483751605), INT64_C(548979001059), INT64_C(549474697695), INT64_C(549970841917), INT64_C(550467434128), INT64_C(550964474734), INT64_C(551461964139),
    INT64_C(551959902748), INT64_C(552458290968), INT64_C(552957129203), INT64_C(553456417861), INT64_C(553956157348), INT64_C(554456348071), INT64_C(554956990438), INT64_C(555458084856)
};

/** Filter damping factor table. Value is
   16777216*2*10^(-((24/128)*x)/640).  */
static const int32_t damp_factor_lut[] = {
    33554432, 33531804, 33509192, 33486595, 33464013, 33441446, 33418894, 33396358, 33373837, 33351331, 33328840, 33306365, 33283904, 33261459, 33239029, 33216614,
    33194214, 33171829, 33149460, 33127105, 33104766, 33082441, 33060132, 33037837, 33015558, 32993294, 32971044, 32948810, 32926591, 32904387, 32882197, 32860023,
    32837863, 32815719, 32793589, 32771475, 32749375, 32727290, 32705220, 32683165, 32661125, 32639100, 32617089, 32595094, 32573113, 32551147, 32529196, 32507260,
    32485338, 32463431, 32441539, 32419662, 32397800, 32375952, 32354119, 32332301, 32310497, 32288708, 32266934, 32245175, 32223430, 32201700, 32179984, 32158284,
    32136597, 32114926, 32093269, 32071626, 32049999, 32028386, 32006787, 31985203, 31963633, 31942079, 31920538, 31899012, 31877501, 31856004, 31834522, 31813054,
    31791600, 31770162, 31748737, 31727327, 31705931, 31684550, 31663184, 31641831, 31620493, 31599170, 31577861, 31556566, 31535285, 31514019, 31492768, 31471530,
    31450307, 31429098, 31407904, 31386724, 31365558, 31344406, 31323269, 31302146, 31281037, 31259942, 31238862, 31217796, 31196744, 31175706, 31154682, 31133673,
    31112678, 31091697, 31070730, 31049777, 31028838, 31007914, 30987003, 30966107, 30945225, 30924357, 30903503, 30882663, 30861837, 30841025, 30820227, 30799443,
    30778673, 30757917, 30737175, 30716447, 30695734, 30675034, 30654348, 30633676, 30613018, 30592373, 30571743, 30551127, 30530525, 30509936, 30489361, 30468801,
    30448254, 30427721, 30407202, 30386696, 30366205, 30345727, 30325263, 30304813, 30284377, 30263954, 30243546, 30223151, 30202769, 30182402, 30162048, 30141708,
    30121382, 30101069, 30080770, 30060485, 30040214, 30019956, 29999712, 29979481, 29959264, 29939061, 29918871, 29898695, 29878533, 29858384, 29838249, 29818127,
    29798019, 29777924, 29757843, 29737776, 29717722, 29697682, 29677655, 29657641, 29637642, 29617655, 29597682, 29577723, 29557777, 29537844, 29517925, 29498020,
    29478127, 29458249, 29438383, 29418531, 29398693, 29378867, 29359055, 29339257, 29319472, 29299700, 29279941, 29260196, 29240464, 29220746, 29201041, 29181349,
    29161670, 29142005, 29122352, 29102714, 29083088, 29063476, 29043876, 29024290, 29004718, 28985158, 28965612, 28946078, 28926558, 28907052, 28887558, 28868077,
    28848610, 28829156, 28809714, 28790286, 28770871, 28751470, 28732081, 28712705, 28693342, 28673993, 28654656, 28635333, 28616022, 28596725, 28577440, 28558169,
    28538911, 28519665, 28500433, 28481213, 28462007, 28442813, 28423632, 28404465, 28385310, 28366168, 28347039, 28327923, 28308820, 28289730, 28270652, 28251588,
    28232536, 28213497, 28194471, 28175458, 28156458, 28137470, 28118495, 28099534, 28080584, 28061648, 28042724, 28023814, 28004916, 27986030, 27967158, 27948298,
    27929451, 27910616, 27891794, 27872985, 27854189, 27835405, 27816634, 27797876, 27779130, 27760397, 27741677, 27722969, 27704274, 27685591, 27666921, 27648264,
    27629619, 27610986, 27592367, 27573760, 27555165, 27536583, 27518013, 27499456, 27480912, 27462380, 27443861, 27425354, 27406859, 27388377, 27369907, 27351450,
    27333006, 27314573, 27296154, 27277746, 27259351, 27240969, 27222599, 27204241, 27185895, 27167562, 27149242, 27130933, 27112637, 27094354, 27076083, 27057824,
    27039577, 27021343, 27003120, 26984911, 26966713, 26948528, 26930355, 26912194, 26894046, 26875910, 26857786, 26839674, 26821574, 26803487, 26785412, 26767349,
    26749298, 26731260, 26713233, 26695219, 26677217, 26659227, 26641249, 26623283, 26605329, 26587388, 26569459, 26551541, 26533636, 26515743, 26497862, 26479993,
    26462136, 26444291, 26426458, 26408637, 26390828, 26373031, 26355246, 26337473, 26319713, 26301964, 26284227, 26266502, 26248789, 26231088, 26213398, 26195721,
    26178056, 26160403, 26142761, 26125132, 26107514, 26089908, 26072314, 26054732, 26037162, 26019603, 26002057, 25984522, 25966999, 25949488, 25931989, 25914502,
    25897026, 25879562, 25862110, 25844670, 25827241, 25809824, 25792419, 25775026, 25757644, 25740274, 25722916, 25705570, 25688235, 25670912, 25653601, 25636301,
    25619013, 25601736, 25584472, 25567219, 25549977, 25532747, 25515529, 25498323, 25481128, 25463944, 25446772, 25429612, 25412463, 25395326, 25378201, 25361087,
    25343984, 25326893, 25309814, 25292746, 25275690, 25258645, 25241612, 25224590, 25207579, 25190580, 25173593, 25156617, 25139652, 25122699, 25105757, 25088827,
    25071908, 25055001, 25038105, 25021220, 25004347, 24987485, 24970635, 24953795, 24936968, 24920151, 24903346, 24886552, 24869770, 24852999, 24836239, 24819490,
    24802753, 24786027, 24769313, 24752609, 24735917, 24719236, 24702567, 24685908, 24669261, 24652625, 24636000, 24619387, 24602785, 24586194, 24569614, 24553045,
    24536487, 24519941, 24503406, 24486882, 24470369, 24453867, 24437376, 24420897, 24404428, 24387971, 24371525, 24355090, 24338666, 24322253, 24305851, 24289460,
    24273080, 24256711, 24240354, 24224007, 24207671, 24191347, 24175033, 24158731, 24142439, 24126158, 24109889, 24093630, 24077382, 24061145, 24044920, 24028705,
    24012501, 23996308, 23980126, 23963954, 23947794, 23931645, 23915506, 23899379, 23883262, 23867156, 23851061, 23834977, 23818904, 23802841, 23786789, 23770749,
    23754719, 23738699, 23722691, 23706694, 23690707, 23674731, 23658765, 23642811, 23626867, 23610934, 23595012, 23579101, 23563200, 23547310, 23531430, 23515562,
    23499704, 23483857, 23468020, 23452194, 23436379, 23420575, 23404781, 23388998, 23373225, 23357463, 23341712, 23325971, 23310241, 23294522, 23278813, 23263115,
    23247427, 23231750, 23216083, 23200427, 23184782, 23169147, 23153523, 23137909, 23122306, 23106713, 23091131, 23075559, 23059998, 23044447, 23028907, 23013377,
    22997858, 22982349, 22966851, 22951363, 22935886, 22920419, 22904962, 22889516, 22874080, 22858655, 22843240, 22827836, 22812441, 22797058, 22781684, 22766321,
    22750969, 22735626, 22720294, 22704973, 22689661, 22674361, 22659070, 22643790, 22628520, 22613260, 22598010, 22582771, 22567542, 22552324, 22537115, 22521917,
    22506730, 22491552, 22476385, 22461227, 22446081, 22430944, 22415817, 22400701, 22385595, 22370499, 22355413, 22340338, 22325272, 22310217, 22295172, 22280137,
    22265112, 22250098, 22235093, 22220099, 22205114, 22190140, 22175176, 22160222, 22145278, 22130344, 22115421, 22100507, 22085603, 22070710, 22055826, 22040953,
    22026089, 22011236, 21996392, 21981559, 21966735, 21951922, 21937118, 21922325, 21907541, 21892768, 21878004, 21863251, 21848507, 21833773, 21819050, 21804336,
    21789632, 21774938, 21760254, 21745579, 21730915, 21716261, 21701616, 21686982, 21672357, 21657742, 21643137, 21628542, 21613956, 21599381, 21584815, 21570259,
    21555713, 21541177, 21526650, 21512134, 21497627, 21483130, 21468642, 21454165, 21439697, 21425239, 21410791, 21396352, 21381923, 21367504, 21353095, 21338695,
    21324305, 21309925, 21295555, 21281194, 21266843, 21252501, 21238169, 21223847, 21209535, 21195232, 21180939, 21166655, 21152381, 21138117, 21123862, 21109617,
    21095382, 21081156, 21066940, 21052733, 21038536, 21024349, 21010171, 20996002, 20981843, 20967694, 20953554, 20939424, 20925304, 20911192, 20897091, 20882999,
    20868916, 20854843, 20840779, 20826725, 20812680, 20798645, 20784620, 20770603, 20756596, 20742599, 20728611, 20714633, 20700664, 20686704, 20672754, 20658813,
    20644881, 20630959, 20617047, 20603143, 20589250, 20575365, 20561490, 20547624, 20533768, 20519920, 20506083, 20492254, 20478435, 20464625, 20450825, 20437034,
    20423252, 20409479, 20395716, 20381962, 20368217, 20354482, 20340755, 20327039, 20313331, 20299632, 20285943, 20272263, 20258592, 20244931, 20231279, 20217635,
    20204001, 20190377, 20176761, 20163155, 20149558, 20135970, 20122391, 20108821, 20095261, 20081709, 20068167, 20054634, 20041110, 20027595, 20014089, 20000592,
    19987105, 19973626, 19960157, 19946697, 19933246, 19919803, 19906370, 19892946, 19879531, 19866125, 19852729, 19839341, 19825962, 19812592, 19799231, 19785880,
    19772537, 19759203, 19745878, 19732562, 19719256, 19705958, 19692669, 19679389, 19666118, 19652856, 19639603, 19626359, 19613124, 19599897, 19586680, 19573472,
    19560272, 19547081, 19533900, 19520727, 19507563, 19494408, 19481262, 19468124, 19454996, 19441876, 19428765, 19415663, 19402570, 19389486, 19376411, 19363344,
    19350286, 19337237, 19324197, 19311165, 19298143, 19285129, 19272124, 19259128, 19246140, 19233161, 19220191, 19207230, 19194277, 19181334, 19168398, 19155472,
    19142554, 19129646, 19116745, 19103854, 19090971, 19078097, 19065231, 19052375, 19039526, 19026687, 19013856, 19001034, 18988221, 18975416, 18962619, 18949832,
    18937053, 18924283, 18911521, 18898768, 18886023, 18873287, 18860560, 18847841, 18835131, 18822429, 18809736, 18797052, 18784376, 18771708, 18759049, 18746399,
    18733757, 18721124, 18708499, 18695883, 18683275, 18670676, 18658086, 18645503, 18632930, 18620364, 18607807, 18595259, 18582719, 18570188, 18557665, 18545150,
    18532644, 18520147, 18507658, 18495177, 18482704, 18470240, 18457785, 18445338, 18432899, 18420469, 18408047, 18395633, 18383228, 18370831, 18358442, 18346062,
    18333690, 18321327, 18308972, 18296625, 18284286, 18271956, 18259634, 18247321, 18235016, 18222719, 18210430, 18198150, 18185878, 18173614, 18161358, 18149111,
    18136872, 18124641, 18112419, 18100205, 18087999, 18075801, 18063611, 18051430, 18039257, 18027092, 18014935, 18002787, 17990646, 17978514, 17966390, 17954274,
    17942167, 17930067, 17917976, 17905893, 17893818, 17881751, 17869692, 17857642, 17845599, 17833565, 17821539, 17809521, 17797511, 17785509, 17773515, 17761529,
    17749552, 17737582, 17725621, 17713667, 17701722, 17689785, 17677855, 17665934, 17654021, 17642116, 17630219, 17618330, 17606449, 17594576, 17582711, 17570854,
    17559005, 17547164, 17535330, 17523505, 17511688, 17499879, 17488078, 17476285, 17464499, 17452722, 17440953, 17429191, 17417438, 17405692, 17393954, 17382225,
    17370503, 17358789, 17347083, 17335385, 17323695, 17312012, 17300338, 17288671, 17277012, 17265361, 17253718, 17242083, 17230456, 17218836, 17207225, 17195621,
    17184025, 17172437, 17160856, 17149284, 17137719, 17126162, 17114613, 17103071, 17091538, 17080012, 17068494, 17056984, 17045481, 17033986, 17022499, 17011020,
    16999549, 16988085, 16976629, 16965181, 16953740, 16942307, 16930882, 16919464, 16908055, 16896653, 16885258, 16873871, 16862492, 16851121, 16839757, 16828401,
    16817053, 16805712, 16794379, 16783054, 16771736, 16760426, 16749123, 16737828, 16726541, 16715261, 16703989, 16692725, 16681468, 16670219, 16658977, 16647743,
    16636516, 16625297, 16614086, 16602882, 16591686, 16580497, 16569316, 16558142, 16546976, 16535818, 16524667, 16513523, 16502387, 16491258, 16480137, 16469024,
    16457918, 16446819, 16435728, 16424645, 16413569, 16402500, 16391439, 16380385, 16369339, 16358300, 16347269, 16336245, 16325228, 16314219, 16303218, 16292224,
    16281237, 16270257, 16259285, 16248321, 16237364, 16226414, 16215471, 16204536, 16193609, 16182688, 16171776, 16160870, 16149972, 16139081, 16128197, 16117321,
    16106452, 16095591, 16084737, 16073890, 16063050, 16052218, 16041393, 16030575, 16019765, 16008962, 15998166, 15987378, 15976596, 15965823, 15955056, 15944296,
    15933544, 15922799, 15912062, 15901331, 15890608, 15879892, 15869183, 15858482, 15847788, 15837101, 15826421, 15815748, 15805082, 15794424, 15783773, 15773129,
    15762492, 15751863, 15741240, 15730625, 15720017, 15709416, 15698822, 15688236, 15677656, 15667084, 15656519, 15645961, 15635410, 15624866, 15614329, 15603799,
    15593277, 15582761, 15572253, 15561752, 15551258, 15540771, 15530290, 15519818, 15509352, 15498893, 15488441, 15477996, 15467558, 15457128, 15446704, 15436288,
    15425878, 15415475, 15405080, 15394691, 15384310, 15373935, 15363568, 15353207, 15342854, 15332507, 15322167, 15311835, 15301509, 15291190, 15280879, 15270574,
    15260276, 15249985, 15239701, 15229424, 15219154, 15208891, 15198635, 15188385, 15178143, 15167908, 15157679, 15147457, 15137242, 15127035, 15116833, 15106639,
    15096452, 15086272, 15076098, 15065931, 15055772, 15045619, 15035472, 15025333, 15015201, 15005075, 14994956, 14984844, 14974739, 14964641, 14954549, 14944465,
    14934387, 14924316, 14914251, 14904194, 14894143, 14884099, 14874062, 14864031, 14854008, 14843991, 14833981, 14823977, 14813980, 14803991, 14794007, 14784031,
    14774061, 14764098, 14754142, 14744192, 14734249, 14724313, 14714384, 14704461, 14694545, 14684636, 14674733, 14664837, 14654947, 14645065, 14635189, 14625319,
    14615457, 14605601, 14595751, 14585909, 14576072, 14566243, 14556420, 14546604, 14536794, 14526991, 14517195, 14507405, 14497622, 14487845, 14478075, 14468312,
    14458555, 14448805, 14439061, 14429324, 14419593, 14409870, 14400152, 14390441, 14380737, 14371039, 14361348, 14351663, 14341985, 14332313, 14322648, 14312990,
    14303338, 14293692, 14284053, 14274420, 14264794, 14255175, 14245562, 14235955, 14226355, 14216761, 14207174, 14197593, 14188019, 14178451, 14168890, 14159335,
    14149787, 14140245, 14130709, 14121180, 14111657, 14102141, 14092631, 14083127, 14073630, 14064140, 14054655, 14045178, 14035706, 14026241, 14016782, 14007330,
    13997884, 13988444, 13979011, 13969584, 13960164, 13950750, 13941342, 13931940, 13922545, 13913157, 13903774, 13894398, 13885028, 13875665, 13866308, 13856957,
    13847612, 13838274, 13828942, 13819616, 13810297, 13800984, 13791677, 13782377, 13773082, 13763794, 13754513, 13745237, 13735968, 13726705, 13717448, 13708198,
    13698954, 13689716, 13680484, 13671258, 13662039, 13652826, 13643619, 13634418, 13625224, 13616035, 13606853, 13597677, 13588508, 13579344, 13570187, 13561036,
    13551891, 13542752, 13533619, 13524493, 13515372, 13506258, 13497150, 13488048, 13478952, 13469863, 13460779, 13451702, 13442631, 13433566, 13424506, 13415454,
    13406407, 13397366, 13388331, 13379303, 13370280, 13361264, 13352254, 13343250, 13334251, 13325259, 13316273, 13307293, 13298320, 13289352, 13280390, 13271434,
    13262485, 13253541, 13244603, 13235672, 13226746, 13217827, 13208913, 13200005, 13191104, 13182208, 13173319, 13164435, 13155558, 13146686, 13137821, 13128961,
    13120107, 13111260, 13102418, 13093582, 13084753, 13075929, 13067111, 13058299, 13049493, 13040693, 13031899, 13023111, 13014329, 13005552, 12996782, 12988017,
    12979259, 12970506, 12961759, 12953018, 12944284, 12935554, 12926831, 12918114, 12909402, 12900697, 12891997, 12883303, 12874615, 12865933, 12857257, 12848587,
    12839922, 12831263, 12822611, 12813964, 12805322, 12796687, 12788057, 12779434, 12770816, 12762204, 12753597, 12744997, 12736402, 12727813, 12719230, 12710653,
    12702081, 12693516, 12684956, 12676401, 12667853, 12659310, 12650773, 12642242, 12633717, 12625197, 12616683, 12608175, 12599673, 12591176, 12582685, 12574200,
    12565720, 12557247, 12548779, 12540316, 12531859, 12523409, 12514963, 12506524, 12498090, 12489662, 12481239, 12472822, 12464411, 12456006, 12447606, 12439212,
    12430823, 12422440, 12414063, 12405692, 12397326, 12388966, 12380611, 12372262, 12363919, 12355581, 12347249, 12338922, 12330602, 12322286, 12313977, 12305673,
    12297374, 12289081, 12280794, 12272513, 12264236, 12255966, 12247701, 12239442, 12231188, 12222940, 12214697, 12206460, 12198229, 12190003, 12181782, 12173567,
    12165358, 12157154, 12148956, 12140763, 12132576, 12124394, 12116218, 12108047, 12099882, 12091723, 12083568, 12075420, 12067277, 12059139, 12051007, 12042880,
    12034759, 12026643, 12018533, 12010428, 12002329, 11994235, 11986146, 11978063, 11969986, 11961914, 11953847, 11945786, 11937730, 11929680, 11921635, 11913596,
    11905562, 11897533, 11889510, 11881492, 11873480, 11865473, 11857471, 11849475, 11841484, 11833499, 11825519, 11817544, 11809575, 11801611, 11793653, 11785699,
    11777752, 11769809, 11761872, 11753940, 11746014, 11738093, 11730177, 11722267, 11714362, 11706462, 11698568, 11690679, 11682795, 11674917, 11667044, 11659176,
    11651314, 11643456, 11635605, 11627758, 11619917, 11612081, 11604250, 11596425, 11588605, 11580790, 11572980, 11565176, 11557377, 11549583, 11541794, 11534011,
    11526233, 11518460, 11510693, 11502930, 11495173, 11487421, 11479675, 11471933, 11464197, 11456466, 11448740, 11441020, 11433304, 11425594, 11417889, 11410190,
    11402495, 11394806, 11387121, 11379442, 11371769, 11364100, 11356437, 11348778, 11341125, 11333477, 11325834, 11318197, 11310564, 11302937, 11295315, 11287697,
    11280086, 11272479, 11264877, 11257280, 11249689, 11242103, 11234521, 11226945, 11219374, 11211809, 11204248, 11196692, 11189142, 11181596, 11174056, 11166520,
    11158990, 11151465, 11143945, 11136430, 11128920, 11121415, 11113915, 11106420, 11098931, 11091446, 11083966, 11076492, 11069022, 11061558, 11054098, 11046644,
    11039195, 11031750, 11024311, 11016877, 11009447, 11002023, 10994604, 10987189, 10979780, 10972376, 10964976, 10957582, 10950193, 10942808, 10935429, 10928055,
    10920685, 10913321, 10905961, 10898607, 10891257, 10883913, 10876573, 10869238, 10861909, 10854584, 10847264, 10839949, 10832639, 10825334, 10818034, 10810738,
    10803448, 10796163, 10788882, 10781607, 10774336, 10767070, 10759809, 10752553, 10745302, 10738056, 10730815, 10723579, 10716347, 10709120, 10701899, 10694682,
    10687470, 10680262, 10673060, 10665863, 10658670, 10651482, 10644299, 10637121, 10629948, 10622780, 10615616, 10608457, 10601303, 10594154, 10587010, 10579871,
    10572736, 10565606, 10558481, 10551361, 10544246, 10537135, 10530029, 10522928, 10515832, 10508741, 10501654, 10494572, 10487495, 10480423, 10473355, 10466292,
    10459234, 10452181, 10445133, 10438089, 10431050, 10424015, 10416986, 10409961, 10402941, 10395926, 10388915, 10381909, 10374908, 10367912, 10360920, 10353933,
    10346951, 10339973, 10333001, 10326032, 10319069, 10312110, 10305156, 10298207, 10291262, 10284322, 10277387, 10270456, 10263530, 10256609, 10249692, 10242780,
    10235873, 10228970, 10222072, 10215179, 10208290, 10201406, 10194527, 10187652, 10180782, 10173917, 10167056, 10160199, 10153348, 10146501, 10139659, 10132821,
    10125988, 10119159, 10112335, 10105516, 10098701, 10091891, 10085085, 10078284, 10071488, 10064696, 10057909, 10051126, 10044348, 10037575, 10030806, 10024042,
    10017282, 10010527, 10003776,  9997030,  9990288,  9983551,  9976819,  9970091,  9963367,  9956648,  9949934,  9943224,  9936519,  9929818,  9923122,  9916430,
     9909743,  9903060,  9896382,  9889708,  9883039,  9876374,  9869714,  9863059,  9856407,  9849761,  9843118,  9836481,  9829847,  9823218,  9816594,  9809974,
     9803359,  9796748,  9790141,  9783539,  9776941,  9770348,  9763760,  9757175,  9750596,  9744020,  9737449,  9730883,  9724321,  9717763,  9711210,  9704661,
     9698116,  9691576,  9685041,  9678510,  9671983,  9665460,  9658942,  9652429,  9645920,  9639415,  9632914,  9626418,  9619927,  9613440,  9606957,  9600478,
     9594004,  9587534,  9581069,  9574608,  9568151,  9561699,  9555251,  9548807,  9542368,  9535933,  9529502,  9523076,  9516654,  9510236,  9503823,  9497414,
     9491009,  9484609,  9478213,  9471821,  9465434,  9459051,  9452672,  9446297,  9439927,  9433561,  9427200,  9420842,  9414489,  9408141,  9401796,  9395456,
     9389120,  9382788,  9376461,  9370138,  9363819,  9357505,  9351194,  9344888,  9338586,  9332289,  9325996,  9319706,  9313422,  9307141,  9300865,  9294593,
     9288325,  9282061,  9275802,  9269546,  9263295,  9257049,  9250806,  9244568,  9238334,  9232104,  9225878,  9219656,  9213439,  9207226,  9201017,  9194812,
     9188612,  9182415,  9176223,  9170035,  9163851,  9157671,  9151496,  9145324,  9139157,  9132994,  9126835,  9120680,  9114530,  9108383,  9102241,  9096103,
     9089969,  9083839,  9077713,  9071591,  9065474,  9059361,  9053251,  9047146,  9041045,  9034948,  9028856,  9022767,  9016682,  9010602,  9004525,  8998453,
     8992385,  8986321,  8980261,  8974205,  8968153,  8962105,  8956062,  8950022,  8943987,  8937955,  8931928,  8925904,  8919885,  8913870,  8907859,  8901852,
     8895849,  8889850,  8883855,  8877864,  8871877,  8865894,  8859915,  8853941,  8847970,  8842003,  8836041,  8830082,  8824127,  8818177,  8812230,  8806288,
     8800349,  8794414,  8788484,  8782557,  8776635,  8770716,  8764801,  8758891,  8752984,  8747081,  8741183,  8735288,  8729397,  8723511,  8717628,  8711749,
     8705874,  8700003,  8694136,  8688274,  8682415,  8676559,  8670708,  8664861,  8659018,  8653179,  8647343,  8641512,  8635684,  8629861,  8624041,  8618226,
     8612414,  8606606,  8600802,  8595002,  8589206,  8583414,  8577625,  8571841,  8566061,  8560284,  8554511,  8548742,  8542978,  8537217,  8531459,  8525706,
     8519957,  8514211,  8508470,  8502732,  8496998,  8491268,  8485542,  8479820,  8474101,  8468387,  8462676,  8456969,  8451266,  8445567,  8439871,  8434180,
     8428492,  8422808,  8417128,  8411452,  8405780,  8400111,  8394447,  8388786,  8383129,  8377476,  8371826,  8366181,  8360539,  8354901,  8349267,  8343636,
     8338010,  8332387,  8326768,  8321153,  8315541,  8309933,  8304330,  8298730,  8293133,  8287541,  8281952,  8276367,  8270786,  8265208,  8259634,  8254065,
     8248498,  8242936,  8237377,  8231822,  8226271,  8220724,  8215180,  8209640,  8204104,  8198571,  8193042,  8187517,  8181996,  8176478,  8170965,  8165454,
     8159948,  8154445,  8148946,  8143451,  8137959,  8132471,  8126987,  8121507,  8116030,  8110557,  8105087,  8099622,  8094160,  8088701,  8083247,  8077796,
     8072348,  8066905,  8061465,  8056028,  8050596,  8045167,  8039741,  8034320,  8028902,  8023487,  8018077,  8012670,  8007266,  8001866,  7996470,  7991078,
     7985689,  7980304,  7974922,  7969544,  7964170,  7958799,  7953432,  7948069,  7942709,  7937353,  7932000,  7926651,  7921306,  7915964,  7910626,  7905291,
     7899960,  7894633,  7889309,  7883989,  7878672,  7873359,  7868049,  7862744,  7857441,  7852143,  7846847,  7841556,  7836268,  7830983,  7825702,  7820425,
     7815151,  7809881,  7804614,  7799351,  7794092,  7788836,  7783583,  7778334,  7773089,  7767847,  7762609,  7757374,  7752143,  7746915,  7741691,  7736470,
     7731253,  7726039,  7720829,  7715623,  7710420,  7705220,  7700024,  7694831,  7689642,  7684457,  7679275,  7674096,  7668921,  7663749,  7658581,  7653417,
     7648256,  7643098,  7637944,  7632793,  7627646,  7622502,  7617362,  7612225,  7607092,  7601962,  7596835,  7591712,  7586593,  7581477,  7576364,  7571255,
     7566149,  7561047,  7555948,  7550852,  7545760,  7540672,  7535587,  7530505,  7525427,  7520352,  7515281,  7510213,  7505148,  7500087,  7495029,  7489975,
     7484924,  7479876,  7474832,  7469792,  7464754,  7459720,  7454690,  7449663,  7444639,  7439619,  7434602,  7429588,  7424578,  7419571,  7414568,  7409568,
     7404571,  7399577,  7394588,  7389601,  7384618,  7379638,  7374661,  7369688,  7364718,  7359752,  7354789,  7349829,  7344873,  7339920,  7334970,  7330023,
     7325080,  7320141,  7315204,  7310271,  7305341,  7300415,  7295492,  7290572,  7285656,  7280743,  7275833,  7270926,  7266023,  7261123,  7256226,  7251333,
     7246443,  7241556,  7236673,  7231793,  7226916,  7222043,  7217172,  7212305,  7207442,  7202581,  7197724,  7192870,  7188020,  7183173,  7178329,  7173488,
     7168650,  7163816,  7158985,  7154157,  7149333,  7144512,  7139694,  7134879,  7130068,  7125259,  7120454,  7115653,  7110854,  7106059,  7101267,  7096478,
     7091692,  7086910,  7082131,  7077355,  7072582,  7067813,  7063047,  7058284,  7053524,  7048767,  7044014,  7039264,  7034517,  7029773,  7025032,  7020295,
     7015561,  7010830,  7006102,  7001377,  6996656,  6991938,  6987223,  6982511,  6977802,  6973096,  6968394,  6963695,  6958999,  6954306,  6949616,  6944930,
     6940246,  6935566,  6930889,  6926215,  6921545,  6916877,  6912212,  6907551,  6902893,  6898238,  6893586,  6888937,  6884292,  6879649,  6875010,  6870374,
     6865741,  6861111,  6856484,  6851860,  6847239,  6842622,  6838008,  6833396,  6828788,  6824183,  6819581,  6814982,  6810387,  6805794,  6801204,  6796618,
     6792035,  6787454,  6782877,  6778303,  6773732,  6769164,  6764599,  6760038,  6755479,  6750923,  6746371,  6741821,  6737275,  6732732,  6728191,  6723654,
     6719120,  6714589,  6710061,  6705536,  6701014,  6696495,  6691979,  6687466,  6682957,  6678450,  6673946,  6669446,  6664948,  6660453,  6655962,  6651473,
     6646988,  6642506,  6638026,  6633550,  6629076,  6624606,  6620139,  6615674,  6611213,  6606755,  6602299,  6597847,  6593398,  6588951,  6584508,  6580068,
     6575630,  6571196,  6566765,  6562336,  6557911,  6553489,  6549069,  6544653,  6540239,  6535829,  6531421,  6527017,  6522615,  6518217,  6513821,  6509428,
     6505039,  6500652,  6496268,  6491888,  6487510,  6483135,  6478763,  6474394,  6470028,  6465665,  6461304,  6456947,  6452593,  6448242,  6443893,  6439548,
     6435205,  6430865,  6426529,  6422195,  6417864,  6413536,  6409211,  6404889,  6400570,  6396254,  6391940,  6387630,  6383322,  6379018,  6374716,  6370417,
     6366121,  6361828,  6357538,  6353251,  6348966,  6344685,  6340406,  6336130,  6331858,  6327588,  6323321,  6319056,  6314795,  6310537,  6306281,  6302028,
     6297779,  6293532,  6289288,  6285046,  6280808,  6276572,  6272340,  6268110,  6263883,  6259659,  6255438,  6251219,  6247004,  6242791,  6238581,  6234374,
     6230170,  6225969,  6221770,  6217574,  6213381,  6209191,  6205004,  6200820,  6196638,  6192459,  6188284,  6184110,  6179940,  6175773,  6171608,  6167446,
     6163287,  6159131,  6154977,  6150827,  6146679,  6142534,  6138391,  6134252,  6130115,  6125981,  6121850,  6117722,  6113596,  6109474,  6105354,  6101237,
     6097122,  6093010,  6088902,  6084795,  6080692,  6076592,  6072494,  6068399,  6064306,  6060217,  6056130,  6052046,  6047965,  6043886,  6039811,  6035738,
     6031667,  6027600,  6023535,  6019473,  6015414,  6011357,  6007304,  6003252,  5999204,  5995159,  5991116,  5987076,  5983038,  5979003,  5974971,  5970942,
     5966916,  5962892,  5958871,  5954852,  5950836,  5946823,  5942813,  5938806,  5934801,  5930799,  5926799,  5922802,  5918808,  5914817,  5910828,  5906842,
     5902859,  5898878,  5894900,  5890925,  5886952,  5882982,  5879015,  5875051,  5871089,  5867129,  5863173,  5859219,  5855268,  5851319,  5847373,  5843430,
     5839490,  5835552,  5831616,  5827684,  5823754,  5819827,  5815902,  5811980,  5808061,  5804144,  5800230,  5796318,  5792410,  5788503,  5784600,  5780699,
     5776801,  5772905,  5769012,  5765122,  5761234,  5757349,  5753466,  5749586,  5745709,  5741835,  5737962,  5734093,  5730226,  5726362,  5722500,  5718641,
     5714785,  5710931,  5707080,  5703231,  5699385,  5695542,  5691701,  5687863,  5684027,  5680194,  5676364,  5672536,  5668710,  5664888,  5661067,  5657250,
     5653435,  5649622,  5645813,  5642005,  5638201,  5634398,  5630599,  5626802,  5623007,  5619215,  5615426,  5611639,  5607855,  5604073,  5600294,  5596517,
     5592743,  5588972,  5585203,  5581436,  5577673,  5573911,  5570152,  5566396,  5562642,  5558891,  5555143,  5551396,  5547653,  5543912,  5540173,  5536437,
     5532703,  5528972,  5525244,  5521518,  5517794,  5514073,  5510355,  5506639,  5502926,  5499215,  5495506,  5491800,  5488097,  5484396,  5480697,  5477002,
     5473308,  5469617,  5465929,  5462243,  5458559,  5454878,  5451200,  5447523,  5443850,  5440179,  5436510,  5432844,  5429180,  5425519,  5421860,  5418204,
     5414550,  5410899,  5407250,  5403604,  5399960,  5396318,  5392679,  5389043,  5385408,  5381777,  5378147,  5374521,  5370896,  5367274,  5363655,  5360038,
     5356423,  5352811,  5349201,  5345594,  5341989,  5338387,  5334787,  5331189,  5327594,  5324002,  5320411,  5316823,  5313238,  5309655,  5306074,  5302496,
     5298920,  5295347,  5291776,  5288207,  5284641,  5281078,  5277516,  5273957,  5270401,  5266847,  5263295,  5259746,  5256199,  5252654,  5249112,  5245572,
     5242035,  5238500,  5234967,  5231437,  5227909,  5224383,  5220860,  5217340,  5213821,  5210305,  5206792,  5203280,  5199772,  5196265,  5192761,  5189259,
     5185760,  5182263,  5178768,  5175276,  5171786,  5168298,  5164813,  5161330,  5157849,  5154371,  5150895,  5147422,  5143950,  5140481,  5137015,  5133551,
     5130089,  5126629,  5123172,  5119717,  5116265,  5112815,  5109367,  5105921,  5102478,  5099037,  5095599,  5092162,  5088728,  5085297,  5081867,  5078440,
     5075016,  5071593,  5068173,  5064756,  5061340,  5057927,  5054516,  5051107,  5047701,  5044297,  5040896,  5037496,  5034099,  5030704,  5027312,  5023922,
     5020534,  5017148,  5013765,  5010384,  5007005,  5003628,  5000254,  4996882,  4993513,  4990145,  4986780,  4983417,  4980056,  4976698,  4973342,  4969988,
     4966637,  4963287,  4959940,  4956596,  4953253,  4949913,  4946575,  4943239,  4939906,  4936574,  4933245,  4929918,  4926594,  4923272,  4919952,  4916634,
     4913318,  4910005,  4906694,  4903385,  4900078,  4896774,  4893472,  4890172,  4886874,  4883578,  4880285,  4876994,  4873705,  4870419,  4867134,  4863852,
     4860572,  4857294,  4854019,  4850745,  4847474,  4844205,  4840939,  4837674,  4834412,  4831152,  4827894,  4824638,  4821384,  4818133,  4814884,  4811637,
     4808392,  4805150,  4801909,  4798671,  4795435,  4792201,  4788970,  4785740,  4782513,  4779288,  4776065,  4772844,  4769625,  4766409,  4763195,  4759983,
     4756773,  4753565,  4750359,  4747156,  4743955,  4740755,  4737558,  4734364,  4731171,  4727980,  4724792,  4721606,  4718422,  4715240,  4712060,  4708883,
     4705707,  4702534,  4699363,  4696194,  4693027,  4689862,  4686699,  4683539,  4680380,  4677224,  4674070,  4670918,  4667768,  4664620,  4661475,  4658331,
     4655190,  4652051,  4648913,  4645778,  4642645,  4639515,  4636386,  4633259,  4630135,  4627013,  4623892,  4620774,  4617658,  4614544,  4611432,  4608322,
     4605215,  4602109,  4599006,  4595904,  4592805,  4589708,  4586613,  4583520,  4580429,  4577340,  4574253,  4571169,  4568086,  4565005,  4561927,  4558851,
     4555776,  4552704,  4549634,  4546566,  4543500,  4540436,  4537374,  4534314,  4531256,  4528201,  4525147,  4522096,  4519046,  4515999,  4512953,  4509910,
     4506869,  4503829,  4500792,  4497757,  4494724,  4491693,  4488664,  4485637,  4482612,  4479589,  4476568,  4473549,  4470533,  4467518,  4464505,  4461494,
     4458486,  4455479,  4452475,  4449472,  4446472,  4443473,  4440477,  4437482,  4434490,  4431499,  4428511,  4425524,  4422540,  4419558,  4416577,  4413599,
     4410623,  4407648,  4404676,  4401706,  4398737,  4395771,  4392807,  4389844,  4386884,  4383926,  4380969,  4378015,  4375063,  4372112,  4369164,  4366217,
     4363273,  4360331,  4357390,  4354452,  4351515,  4348581,  4345648,  4342718,  4339789,  4336863,  4333938,  4331015,  4328095,  4325176,  4322259,  4319345,
     4316432,  4313521,  4310612,  4307705,  4304800,  4301897,  4298996,  4296097,  4293200,  4290305,  4287412,  4284521,  4281631,  4278744,  4275859,  4272975,
     4270094,  4267214,  4264336,  4261461,  4258587,  4255715,  4252845,  4249977,  4247111,  4244247,  4241385,  4238525,  4235667,  4232810,  4229956,  4227103,
     4224253,  4221404,  4218557,  4215712,  4212870,  4210029,  4207190,  4204352,  4201517,  4198684,  4195852,  4193023,  4190195,  4187370,  4184546,  4181724,
     4178904,  4176086,  4173270,  4170455,  4167643,  4164833,  4162024,  4159217,  4156412,  4153610,  4150809,  4148009,  4145212,  4142417,  4139623,  4136832,
     4134042,  4131254,  4128468,  4125684,  4122902,  4120122,  4117343,  4114567,  4111792,  4109019,  4106248,  4103479,  4100712,  4097947,  4095183,  4092422,
     4089662,  4086904,  4084148,  4081394,  4078641,  4075891,  4073142,  4070396,  4067651,  4064908,  4062166,  4059427,  4056689,  4053954,  4051220,  4048488,
     4045758,  4043030,  4040303,  4037579,  4034856,  4032135,  4029416,  4026698,  4023983,  4021269,  4018558,  4015848,  4013140,  4010433,  4007729,  4005026,
     4002325,  3999626,  3996929,  3994234,  3991540,  3988849,  3986159,  3983471,  3980784,  3978100,  3975417,  3972736,  3970057,  3967380,  3964705,  3962031,
     3959359,  3956689,  3954021,  3951354,  3948690,  3946027,  3943366,  3940707,  3938049,  3935394,  3932740,  3930088,  3927437,  3924789,  3922142,  3919497,
     3916854,  3914213,  3911573,  3908935,  3906299,  3903665,  3901033,  3898402,  3895773,  3893146,  3890520,  3887897,  3885275,  3882655,  3880037,  3877420,
     3874805,  3872192,  3869581,  3866972,  3864364,  3861758,  3859154,  3856551,  3853951,  3851352,  3848754,  3846159,  3843565,  3840973,  3838383,  3835795,
     3833208,  3830623,  3828040,  3825458,  3822879,  3820301,  3817724,  3815150,  3812577,  3810006,  3807437,  3804869,  3802303,  3799739,  3797177,  3794616,
     3792057,  3789500,  3786945,  3784391,  3781839,  3779289,  3776740,  3774193,  3771648,  3769104,  3766563,  3764023,  3761484,  3758948,  3756413,  3753880,
     3751348,  3748819,  3746291,  3743764,  3741240,  3738717,  3736195,  3733676,  3731158,  3728642,  3726127,  3723615,  3721104,  3718594,  3716087,  3713581,
     3711076,  3708574,  3706073,  3703574,  3701076,  3698580,  3696086,  3693594,  3691103,  3688614,  3686126,  3683640,  3681156,  3678674,  3676193,  3673714,
     3671237,  3668761,  3666287,  3663815,  3661344,  3658875,  3656407,  3653942,  3651478,  3649015,  3646554,  3644095,  3641638,  3639182,  3636728,  3634276,
     3631825,  3629376,  3626928,  3624482,  3622038,  3619596,  3617155,  3614715,  3612278,  3609842,  3607408,  3604975,  3602544,  3600114,  3597687,  3595260,
     3592836,  3590413,  3587992,  3585572,  3583154,  3580738,  3578323,  3575910,  3573499,  3571089,  3568681,  3566274,  3563869,  3561466,  3559064,  3556664,
     3554266,  3551869,  3549474,  3547080,  3544688,  3542298,  3539909,  3537522,  3535136,  3532752,  3530370,  3527989,  3525610,  3523232,  3520857,  3518482,
     3516110,  3513738,  3511369,  3509001,  3506635,  3504270,  3501907,  3499545,  3497185,  3494827,  3492470,  3490115,  3487761,  3485409,  3483059,  3480710,
     3478363,  3476017,  3473673,  3471331,  3468990,  3466650,  3464313,  3461976,  3459642,  3457309,  3454977,  3452647,  3450319,  3447992,  3445667,  3443344,
     3441022,  3438701,  3436382,  3434065,  3431749,  3429435,  3427122,  3424811,  3422501,  3420193,  3417887,  3415582,  3413279,  3410977,  3408677,  3406378,
     3404081,  3401785,  3399491,  3397199,  3394908,  3392619,  3390331,  3388045,  3385760,  3383477,  3381195,  3378915,  3376636,  3374359,  3372084,  3369810,
     3367537,  3365266,  3362997,  3360729,  3358463,  3356198,  3353935,  3351673,  3349413,  3347154,  3344897,  3342641,  3340387,  3338134,  3335883,  3333634,
     3331386,  3329139,  3326894,  3324650,  3322408,  3320168,  3317929,  3315691,  3313455,  3311221,  3308988,  3306757,  3304527,  3302298,  3300071,  3297846,
     3295622,  3293400,  3291179,  3288959,  3286741,  3284525,  3282310,  3280096,  3277884,  3275674,  3273465,  3271258,  3269052,  3266847,  3264644,  3262442,
     3260242,  3258044,  3255847,  3253651,  3251457,  3249264,  3247073,  3244884,  3242695,  3240509,  3238323,  3236140,  3233957,  3231776,  3229597,  3227419,
     3225243,  3223068,  3220894,  3218722,  3216552,  3214382,  3212215,  3210049,  3207884,  3205721,  3203559,  3201398,  3199240,  3197082,  3194926,  3192772,
     3190619,  3188467,  3186317,  3184168,  3182021,  3179875,  3177731,  3175588,  3173446,  3171306,  3169168,  3167030,  3164895,  3162760,  3160628,  3158496,
     3156366,  3154238,  3152111,  3149985,  3147861,  3145738,  3143617,  3141497,  3139378,  3137261,  3135146,  3133031,  3130919,  3128807,  3126697,  3124589,
     3122482,  3120376,  3118272,  3116169,  3114067,  3111967,  3109869,  3107772,  3105676,  3103582,  3101489,  3099397,  3097307,  3095218,  3093131,  3091045,
     3088961,  3086878,  3084796,  3082716,  3080637,  3078559,  3076483,  3074409,  3072336,  3070264,  3068193,  3066124,  3064056,  3061990,  3059925,  3057862,
     3055800,  3053739,  3051680,  3049622,  3047565,  3045510,  3043456,  3041404,  3039353,  3037303,  3035255,  3033208,  3031163,  3029119,  3027076,  3025035,
     3022995,  3020956,  3018919,  3016883,  3014849,  3012816,  3010784,  3008754,  3006725,  3004697,  3002671,  3000646,  2998622,  2996600,  2994579,  2992560,
     2990542,  2988525,  2986510,  2984496,  2982483,  2980472,  2978462,  2976454,  2974446,  2972441,  2970436,  2968433,  2966431,  2964431,  2962432,  2960434,
     2958437,  2956442,  2954449,  2952456,  2950465,  2948476,  2946487,  2944500,  2942515,  2940530,  2938547,  2936566,  2934585,  2932607,  2930629,  2928653,
     2926678,  2924704,  2922732,  2920761,  2918791,  2916823,  2914856,  2912890,  2910926,  2908963,  2907001,  2905041,  2903082,  2901124,  2899168,  2897213,
     2895259,  2893306,  2891355,  2889405,  2887457,  2885510,  2883564,  2881619,  2879676,  2877734,  2875794,  2873854,  2871916,  2869980,  2868044,  2866110,
     2864177,  2862246,  2860316,  2858387,  2856459,  2854533,  2852608,  2850684,  2848762,  2846841,  2844921,  2843002,  2841085,  2839169,  2837255,  2835341,
     2833429,  2831519,  2829609,  2827701,  2825794,  2823889,  2821984,  2820081,  2818179,  2816279,  2814380,  2812482,  2810585,  2808690,  2806796,  2804903,
     2803012,  2801121,  2799232,  2797345,  2795458,  2793573,  2791689,  2789807,  2787925,  2786045,  2784167,  2782289,  2780413,  2778538,  2776664,  2774792,
     2772920,  2771050,  2769182,  2767314,  2765448,  2763583,  2761720,  2759857,  2757996,  2756136,  2754278,  2752420,  2750564,  2748709,  2746856,  2745003,
     2743152,  2741302,  2739454,  2737606,  2735760,  2733915,  2732072,  2730229,  2728388,  2726548,  2724709,  2722872,  2721036,  2719201,  2717367,  2715535,
     2713703,  2711873,  2710045,  2708217,  2706391,  2704566,  2702742,  2700919,  2699098,  2697278,  2695459,  2693641,  2691825,  2690009,  2688195,  2686383,
     2684571,  2682761,  2680951,  2679144,  2677337,  2675531,  2673727,  2671924,  2670122,  2668322,  2666522,  2664724,  2662927,  2661131,  2659337,  2657543,
     2655751,  2653960,  2652171,  2650382,  2648595,  2646809,  2645024,  2643240,  2641458,  2639676,  2637896,  2636117,  2634340,  2632563,  2630788,  2629014,
     2627241,  2625469,  2623699,  2621929,  2620161,  2618394,  2616629,  2614864,  2613101,  2611339,  2609578,  2607818,  2606059,  2604302,  2602545,  2600790,
     2599037,  2597284,  2595532,  2593782,  2592033,  2590285,  2588538,  2586793,  2585048,  2583305,  2581563,  2579822,  2578082,  2576344,  2574606,  2572870,
     2571135,  2569401,  2567669,  2565937,  2564207,  2562477,  2560749,  2559023,  2557297,  2555572,  2553849,  2552127,  2550406,  2548686,  2546967,  2545249,
     2543533,  2541818,  2540104,  2538391,  2536679,  2534968,  2533259,  2531551,  2529843,  2528137,  2526433,  2524729,  2523026,  2521325,  2519625,  2517925,
     2516227,  2514531,  2512835,  2511140,  2509447,  2507755,  2506064,  2504374,  2502685,  2500997,  2499310,  2497625,  2495941,  2494258,  2492576,  2490895,
     2489215,  2487536,  2485859,  2484182,  2482507,  2480833,  2479160,  2477488,  2475818,  2474148,  2472480,  2470812,  2469146,  2467481,  2465817,  2464154,
     2462492,  2460832,  2459172,  2457514,  2455857,  2454201,  2452546,  2450892,  2449239,  2447587,  2445937,  2444287,  2442639,  2440992,  2439346,  2437701,
     2436057,  2434414,  2432772,  2431132,  2429492,  2427854,  2426217,  2424581,  2422945,  2421312,  2419679,  2418047,  2416416,  2414787,  2413158,  2411531,
     2409905,  2408280,  2406656,  2405033,  2403411,  2401790,  2400170,  2398552,  2396934,  2395318,  2393703,  2392088,  2390475,  2388863,  2387252,  2385642,
     2384034,  2382426,  2380819,  2379214,  2377609,  2376006,  2374404,  2372803,  2371202,  2369603,  2368005,  2366409,  2364813,  2363218,  2361624,  2360032,
     2358440,  2356850,  2355261,  2353672,  2352085,  2350499,  2348914,  2347330,  2345747,  2344165,  2342584,  2341004,  2339426,  2337848,  2336272,  2334696,
     2333122,  2331548,  2329976,  2328405,  2326835,  2325265,  2323697,  2322130,  2320564,  2319000,  2317436,  2315873,  2314311,  2312751,  2311191,  2309632,
     2308075,  2306518,  2304963,  2303409,  2301855,  2300303,  2298752,  2297202,  2295652,  2294104,  2292557,  2291011,  2289466,  2287922,  2286380,  2284838,
     2283297,  2281757,  2280218,  2278681,  2277144,  2275609,  2274074,  2272540,  2271008,  2269476,  2267946,  2266417,  2264888,  2263361,  2261835,  2260309,
     2258785,  2257262,  2255740,  2254218,  2252698,  2251179,  2249661,  2248144,  2246628,  2245113,  2243599,  2242086,  2240574,  2239063,  2237553,  2236044,
     2234536,  2233029,  2231523,  2230019,  2228515,  2227012,  2225510,  2224009,  2222510,  2221011,  2219513,  2218016,  2216521,  2215026,  2213532,  2212039,
     2210548,  2209057,  2207567,  2206079,  2204591,  2203104,  2201619,  2200134,  2198650,  2197168,  2195686,  2194205,  2192725,  2191247,  2189769,  2188292,
     2186817,  2185342,  2183868,  2182396,  2180924,  2179453,  2177983,  2176515,  2175047,  2173580,  2172114,  2170650,  2169186,  2167723,  2166261,  2164800,
     2163341,  2161882,  2160424,  2158967,  2157511,  2156056,  2154602,  2153149,  2151697,  2150246,  2148796,  2147347,  2145899,  2144452,  2143006,  2141561,
     2140116,  2138673,  2137231,  2135790,  2134349,  2132910,  2131472,  2130034,  2128598,  2127163,  2125728,  2124295,  2122862,  2121430,  2120000,  2118570
};

static inline void mulu_128(uint64_t *const result, const uint64_t a,
                            const uint64_t b)
{
    const uint32_t a_hi = a >> 32;
    const uint32_t a_lo = (uint32_t) a;
    const uint32_t b_hi = b >> 32;
    const uint32_t b_lo = (uint32_t) b;
    const uint64_t x1 = (uint64_t) a_lo * b_hi;
    uint64_t x2 = (uint64_t) a_hi * b_lo;
    const uint64_t x3 = (uint64_t) a_lo * b_lo;
    uint64_t x0 = (uint64_t) a_hi * b_hi;

    x2       += x3 >> 32;
    x2       += x1;
    x0       += (x2 < x1) ? UINT64_C(0x100000000) : 0;
    result[0] = x0 + (x2 >> 32);
    result[1] = (x2 << 32) + (x3 & UINT64_C(0xFFFFFFFF));
}

static inline void neg_128(uint64_t *const result)
{
    result[1]  = ~result[1];
    result[0]  = ~result[0];
    result[0] += (++result[1] ? 0 : 1);
}

static inline void muls_128(int64_t *const result, const int64_t a,
                            const int64_t b)
{
    const int sign = (a ^ b) < 0;

    mulu_128(result, a < 0 ? -a : a, b < 0 ? -b : b);

    if (sign)
        neg_128(result);
}

static inline uint64_t divu_128(const uint64_t *const a,
                                const uint64_t b)
{
    uint64_t a_hi   = a[0];
    uint64_t a_lo   = a[1];
    uint64_t result = 0, result_r = 0;
    uint16_t i      = 128;

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

static inline int64_t divs_128(int64_t *const a, const int64_t b)
{
    const int sign = (a[0] ^ b) < 0;
    int64_t result;

    if (a[0] < 0)
        neg_128(a);

    result = divu_128(a, b < 0 ? -b : b);

    if (sign)
        result = -result;

    return result;
}

static inline void add_128(int64_t *const a, const int64_t b)
{
    a[1] += b;
    a[0] += ((uint64_t) a[1] < (uint64_t) b);
}

static void update_sample_filter(const AV_LQMixerData *const mixer_data,
                                 AV_LQMixerChannelInfo *const channel_info,
                                 struct ChannelBlock *const channel_block)
{
    const uint32_t mix_rate = mixer_data->mix_rate;
    uint64_t tmp_128[2];
    int64_t nat_freq, damp_factor, d, e, tmp, tmp_round;

    if ((channel_block->filter_cutoff == 4095) && (channel_block->filter_damping == 0)) {
        channel_block->filter_c1  = 16777216;
        channel_block->filter_c2  = 0;
        channel_block->filter_c3  = 0;
        channel_info->filter_tmp1 = 0;
        channel_info->filter_tmp2 = 0;

        return;
    }

    nat_freq    = nat_freq_lut[channel_block->filter_cutoff];
    damp_factor = damp_factor_lut[channel_block->filter_damping];
    d           = ((nat_freq * (INT64_C(16777216) - damp_factor)) + ((int64_t) mix_rate << 23)) / ((int64_t) mix_rate << 24);
    tmp_round   = nat_freq >> 1;

    if (d > INT64_C(33554432))
        d = INT64_C(33554432);

    muls_128(tmp_128, damp_factor - d, (int64_t) mix_rate << 24);
    add_128(tmp_128, tmp_round);
    d = divs_128(tmp_128, nat_freq);

    mulu_128(tmp_128, (uint64_t) mix_rate << 29, (uint64_t) mix_rate << 29); // Using more than 58 (2*29) bits in total will result in 128-bit integer multiply overflow for maximum allowed mixing rate of 768kHz
    add_128(tmp_128, tmp_round);
    e = ((divu_128(tmp_128, nat_freq) + tmp_round) / nat_freq) << 14;

    tmp                      = INT64_C(16777216) + d + e;
    tmp_round                = tmp >> 1;
    channel_block->filter_c1 = (INT64_C(281474976710656) + tmp_round) / tmp;
    channel_block->filter_c2 = (((d + e + e) << 24) + tmp_round) / tmp;
    channel_block->filter_c3 = (((-e) << 24) + tmp_round) / tmp;
}

static void set_sample_filter(const AV_LQMixerData *const mixer_data,
                              AV_LQMixerChannelInfo *const channel_info,
                              struct ChannelBlock *const channel_block,
                              uint16_t cutoff,
                              uint16_t damping)
{
    if (cutoff > 4095)
        cutoff = 4095;

    if (damping > 4095)
        damping = 4095;

    if ((channel_block->filter_cutoff == cutoff) && (channel_block->filter_damping == damping))
        return;

    channel_block->filter_cutoff  = cutoff;
    channel_block->filter_damping = damping;

    update_sample_filter(mixer_data, channel_info, channel_block);
}

static av_cold AVMixerData *init(AVMixerContext *const mixctx,
                                 const char *args, void *opaque)
{
    AV_LQMixerData *lq_mixer_data;
    AV_LQMixerChannelInfo *channel_info;
    const char *cfg_buf;
    uint16_t i;
    int32_t *buf;
    unsigned interpolation = 0, real16bit = 0, buf_size;
    uint32_t mix_buf_mem_size, channel_rate;
    uint16_t channels_in = 1, channels_out = 1;

    if (!(lq_mixer_data = av_mallocz(sizeof(AV_LQMixerData) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer data factory.\n");

        return NULL;
    }

    if (!(lq_mixer_data->volume_lut = av_malloc((256 * 256 * sizeof(int32_t)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer volume lookup table.\n");
        av_free(lq_mixer_data);

        return NULL;
    }

    lq_mixer_data->mixer_data.mixctx = mixctx;
    buf_size                         = lq_mixer_data->mixer_data.mixctx->buf_size;

    if ((cfg_buf = av_stristr(args, "buffer=")))
        sscanf(cfg_buf, "buffer=%d;", &buf_size);

    if (av_stristr(args, "real16bit=true;") || av_stristr(args, "real16bit=enabled;"))
        real16bit = 1;
    else if ((cfg_buf = av_stristr(args, "real16bit=;")))
        sscanf(cfg_buf, "real16bit=%d;", &real16bit);

    if (av_stristr(args, "interpolation=true;") || av_stristr(args, "interpolation=enabled;"))
        interpolation = 2;
    else if ((cfg_buf = av_stristr(args, "interpolation=")))
        sscanf(cfg_buf, "interpolation=%d;", &interpolation);

    if (!(channel_info = av_mallocz((channels_in * sizeof(AV_LQMixerChannelInfo)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer channel data.\n");
        av_freep(&lq_mixer_data->volume_lut);
        av_free(lq_mixer_data);

        return NULL;
    }

    lq_mixer_data->channel_info            = channel_info;
    lq_mixer_data->mixer_data.channels_in  = channels_in;
    lq_mixer_data->channels_in             = channels_in;
    lq_mixer_data->channels_out            = channels_out;
    mix_buf_mem_size                       = (buf_size << 2) * channels_out;

    if (!(buf = av_mallocz(mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer output buffer.\n");
        av_freep(&lq_mixer_data->channel_info);
        av_freep(&lq_mixer_data->volume_lut);
        av_free(lq_mixer_data);

        return NULL;
    }

    lq_mixer_data->mix_buf_size            = mix_buf_mem_size;
    lq_mixer_data->buf                     = buf;
    lq_mixer_data->buf_size                = buf_size;
    lq_mixer_data->mixer_data.mix_buf_size = lq_mixer_data->buf_size;
    lq_mixer_data->mixer_data.mix_buf      = lq_mixer_data->buf;
    channel_rate                           = lq_mixer_data->mixer_data.mixctx->frequency;
    lq_mixer_data->mixer_data.rate         = channel_rate;
    lq_mixer_data->mix_rate                = channel_rate;
    lq_mixer_data->real_16_bit_mode        = real16bit ? 1 : 0;
    lq_mixer_data->interpolation           = interpolation >= 2 ? 2 : interpolation;

    if (!(buf = av_mallocz(mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer (resonance) filter output buffer.\n");
        av_freep(&lq_mixer_data->buf);
        av_freep(&lq_mixer_data->channel_info);
        av_freep(&lq_mixer_data->volume_lut);
        av_free(lq_mixer_data);

        return NULL;
    }

    lq_mixer_data->filter_buf = buf;

    for (i = lq_mixer_data->channels_in; i > 0; i--) {
        set_sample_filter(lq_mixer_data, channel_info, &channel_info->current, 4095, 0);
        set_sample_filter(lq_mixer_data, channel_info, &channel_info->next, 4095, 0);

        channel_info++;
    }

    return (AVMixerData *) lq_mixer_data;
}

static av_cold int uninit(AVMixerData *const mixer_data)
{
    AV_LQMixerData *lq_mixer_data = (AV_LQMixerData *) mixer_data;

    if (!lq_mixer_data)
        return AVERROR_INVALIDDATA;

    av_freep(&lq_mixer_data->channel_info);
    av_freep(&lq_mixer_data->volume_lut);
    av_freep(&lq_mixer_data->buf);
    av_freep(&lq_mixer_data->filter_buf);
    av_free(lq_mixer_data);

    return 0;
}

static av_cold uint32_t set_tempo(AVMixerData *const mixer_data,
                                  const uint32_t tempo)
{
    AV_LQMixerData *const lq_mixer_data = (AV_LQMixerData *const) mixer_data;
    uint32_t channel_rate               = lq_mixer_data->mix_rate * 10;
    uint64_t pass_value;

    lq_mixer_data->mixer_data.tempo = tempo;
    pass_value                      = ((uint64_t) channel_rate << 16) + ((uint64_t) lq_mixer_data->mix_rate_frac >> 16);
    lq_mixer_data->pass_len         = (uint64_t) pass_value / lq_mixer_data->mixer_data.tempo;
    lq_mixer_data->pass_len_frac    = (((uint64_t) pass_value % lq_mixer_data->mixer_data.tempo) << 32) / lq_mixer_data->mixer_data.tempo;

    return tempo;
}

static av_cold uint32_t set_rate(AVMixerData *const mixer_data,
                                 const uint32_t mix_rate,
                                 const uint32_t channels)
{
    AV_LQMixerData *const lq_mixer_data = (AV_LQMixerData *const) mixer_data;
    uint32_t buf_size, old_mix_rate, mix_rate_frac;

    lq_mixer_data->mixer_data.rate         = mix_rate;
    buf_size                               = lq_mixer_data->mixer_data.mix_buf_size;
    lq_mixer_data->mixer_data.channels_out = channels;

    if ((lq_mixer_data->buf_size * lq_mixer_data->channels_out) != (buf_size * channels)) {
        int32_t *buf                    = lq_mixer_data->mixer_data.mix_buf;
        int32_t *filter_buf             = lq_mixer_data->filter_buf;
        const uint32_t mix_buf_mem_size = (buf_size * channels) << 2;

        if (!(buf = av_realloc(buf, mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(lq_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer output buffer.\n");

            return lq_mixer_data->mixer_data.rate;
        } else if (!(filter_buf = av_realloc(filter_buf, mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(lq_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer (resonance) filter output buffer.\n");

            return lq_mixer_data->mixer_data.rate;
        }

        memset(buf, 0, mix_buf_mem_size);

        lq_mixer_data->mixer_data.mix_buf      = buf;
        lq_mixer_data->mixer_data.mix_buf_size = buf_size;
        lq_mixer_data->filter_buf              = filter_buf;
    }

    lq_mixer_data->channels_out = channels;
    lq_mixer_data->buf          = lq_mixer_data->mixer_data.mix_buf;
    lq_mixer_data->buf_size     = lq_mixer_data->mixer_data.mix_buf_size;

    if (lq_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_MIXING) {
        old_mix_rate  = mix_rate; // TODO: Add check here if this mix rate is supported by target device
        mix_rate_frac = 0;

        if (lq_mixer_data->mix_rate != old_mix_rate) {
            AV_LQMixerChannelInfo *channel_info = lq_mixer_data->channel_info;
            uint16_t i;

            lq_mixer_data->mix_rate      = old_mix_rate;
            lq_mixer_data->mix_rate_frac = mix_rate_frac;

            if (lq_mixer_data->mixer_data.tempo)
                set_tempo((AVMixerData *) mixer_data, lq_mixer_data->mixer_data.tempo);

            for (i = lq_mixer_data->channels_in; i > 0; i--) {
                channel_info->current.advance      = channel_info->current.rate / old_mix_rate;
                channel_info->current.advance_frac = (((uint64_t) channel_info->current.rate % old_mix_rate) << 32) / old_mix_rate;
                channel_info->next.advance         = channel_info->next.rate / old_mix_rate;
                channel_info->next.advance_frac    = (((uint64_t) channel_info->next.rate % old_mix_rate) << 32) / old_mix_rate;

                update_sample_filter(lq_mixer_data, channel_info, &channel_info->current);
                update_sample_filter(lq_mixer_data, channel_info, &channel_info->next);

                channel_info++;
            }
        }
    }

    // TODO: Inform libavfilter that the target mixing rate has been changed.

    return mix_rate;
}

static av_cold uint32_t set_volume(AVMixerData *const mixer_data,
                                   const uint32_t amplify,
                                   const uint32_t left_volume,
                                   const uint32_t right_volume,
                                   const uint32_t channels)
{
    AV_LQMixerData *const lq_mixer_data           = (AV_LQMixerData *const) mixer_data;
    AV_LQMixerChannelInfo *channel_info           = NULL;
    AV_LQMixerChannelInfo *const old_channel_info = lq_mixer_data->channel_info;
    uint32_t old_channels, i;

    if (((old_channels = lq_mixer_data->channels_in) != channels) && !(channel_info = av_mallocz((channels * sizeof(AV_LQMixerChannelInfo)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(lq_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer channel data.\n");

        return old_channels;
    }

    lq_mixer_data->mixer_data.volume_boost = amplify;
    lq_mixer_data->mixer_data.volume_left  = left_volume;
    lq_mixer_data->mixer_data.volume_right = right_volume;
    lq_mixer_data->mixer_data.channels_in  = channels;

    if ((old_channels != channels) || (lq_mixer_data->amplify != amplify)) {
        int32_t *volume_lut = lq_mixer_data->volume_lut;
        int64_t volume_mult = 0;
        int32_t volume_div  = channels << 8;
        uint8_t i           = 0, j = 0;

        lq_mixer_data->amplify = amplify;

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

        memcpy(channel_info, old_channel_info, copy_channels * sizeof(AV_LQMixerChannelInfo));

        lq_mixer_data->channel_info = channel_info;
        lq_mixer_data->channels_in  = channels;

        channel_info += copy_channels;

        for (i = copy_channels; i < channels; ++i) {
            set_sample_filter(lq_mixer_data, channel_info, &channel_info->current, 4095, 0);
            set_sample_filter(lq_mixer_data, channel_info, &channel_info->next, 4095, 0);

            channel_info++;
        }

        av_free(old_channel_info);
    }

    channel_info = lq_mixer_data->channel_info;

    for (i = channels; i > 0; i--) {
        set_sample_mix_rate(lq_mixer_data, &channel_info->current, channel_info->current.rate);

        channel_info++;
    }

    return channels;
}

static av_cold void get_channel(const AVMixerData *const mixer_data,
                                AVMixerChannel *const mixer_channel,
                                const uint32_t channel)
{
    const AV_LQMixerData *const lq_mixer_data       = (const AV_LQMixerData *const) mixer_data;
    const AV_LQMixerChannelInfo *const channel_info = lq_mixer_data->channel_info + channel;

    mixer_channel->pos             = channel_info->current.offset;
    mixer_channel->pos_one_shoot   = channel_info->current.offset_one_shoot;
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

static av_cold void set_channel(AVMixerData *const mixer_data,
                                const AVMixerChannel *const mixer_channel,
                                const uint32_t channel)
{
    const AV_LQMixerData *const lq_mixer_data = (const AV_LQMixerData *const) mixer_data;
    AV_LQMixerChannelInfo *const channel_info = lq_mixer_data->channel_info + channel;
    struct ChannelBlock *channel_block;
    uint32_t repeat, repeat_len;

    channel_info->next.data = NULL;

    if (mixer_channel->flags & AVSEQ_MIXER_CHANNEL_FLAG_SYNTH)
        channel_block = &channel_info->next;
    else
        channel_block = &channel_info->current;

    channel_block->offset           = mixer_channel->pos;
    channel_block->fraction         = 0;
    channel_block->offset_one_shoot = mixer_channel->pos_one_shoot;
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

    set_sample_mix_rate(lq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(lq_mixer_data, channel_info, channel_block, mixer_channel->filter_cutoff, mixer_channel->filter_damping);
}

static av_cold void reset_channel(AVMixerData *const mixer_data,
                                  const uint32_t channel)
{
    const AV_LQMixerData *const lq_mixer_data = (const AV_LQMixerData *const) mixer_data;
    AV_LQMixerChannelInfo *const channel_info = lq_mixer_data->channel_info + channel;
    struct ChannelBlock *channel_block        = &channel_info->current;

    channel_block->offset           = 0;
    channel_block->fraction         = 0;
    channel_block->offset_one_shoot = 0;
    channel_block->bits_per_sample  = 0;
    channel_block->flags            = 0;
    channel_block->volume           = 0;
    channel_block->panning          = 0;
    channel_block->data             = NULL;
    channel_block->len              = 0;
    channel_block->repeat           = 0;
    channel_block->repeat_len       = 0;
    channel_block->end_offset       = 0;
    channel_block->restart_offset   = 0;
    channel_block->count_restart    = 0;
    channel_block->counted          = 0;

    set_sample_mix_rate(lq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(lq_mixer_data, channel_info, channel_block, 4095, 0);

    channel_block                   = &channel_info->next;
    channel_block->offset           = 0;
    channel_block->fraction         = 0;
    channel_block->offset_one_shoot = 0;
    channel_block->bits_per_sample  = 0;
    channel_block->flags            = 0;
    channel_block->volume           = 0;
    channel_block->panning          = 0;
    channel_block->data             = NULL;
    channel_block->len              = 0;
    channel_block->repeat           = 0;
    channel_block->repeat_len       = 0;
    channel_block->end_offset       = 0;
    channel_block->restart_offset   = 0;
    channel_block->count_restart    = 0;
    channel_block->counted          = 0;

    set_sample_mix_rate(lq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(lq_mixer_data, channel_info, channel_block, 4095, 0);
}

static av_cold void get_both_channels(const AVMixerData *const mixer_data,
                                      AVMixerChannel *const mixer_channel_current,
                                      AVMixerChannel *const mixer_channel_next,
                                      const uint32_t channel)
{
    const AV_LQMixerData *const lq_mixer_data       = (const AV_LQMixerData *const) mixer_data;
    const AV_LQMixerChannelInfo *const channel_info = lq_mixer_data->channel_info + channel;

    mixer_channel_current->pos             = channel_info->current.offset;
    mixer_channel_current->pos_one_shoot   = channel_info->current.offset_one_shoot;
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
    mixer_channel_next->pos_one_shoot   = channel_info->next.offset_one_shoot;
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

static av_cold void set_both_channels(AVMixerData *const mixer_data,
                                      const AVMixerChannel *const mixer_channel_current,
                                      const AVMixerChannel *const mixer_channel_next,
                                      const uint32_t channel)
{
    const AV_LQMixerData *const lq_mixer_data = (const AV_LQMixerData *const) mixer_data;
    AV_LQMixerChannelInfo *const channel_info = lq_mixer_data->channel_info + channel;
    struct ChannelBlock *channel_block        = &channel_info->current;
    uint32_t repeat, repeat_len;

    channel_block->offset           = mixer_channel_current->pos;
    channel_block->fraction         = 0;
    channel_block->offset_one_shoot = mixer_channel_current->pos_one_shoot;
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

    set_sample_mix_rate(lq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(lq_mixer_data, channel_info, channel_block, mixer_channel_current->filter_cutoff, mixer_channel_current->filter_damping);

    channel_block                   = &channel_info->next;
    channel_block->offset           = mixer_channel_next->pos;
    channel_block->fraction         = 0;
    channel_block->offset_one_shoot = mixer_channel_next->pos_one_shoot;
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

    set_sample_mix_rate(lq_mixer_data, channel_block, channel_block->rate);
    set_sample_filter(lq_mixer_data, channel_info, channel_block, mixer_channel_next->filter_cutoff, mixer_channel_next->filter_damping);
}

static av_cold void set_channel_volume_panning_pitch(AVMixerData *const mixer_data,
                                                     const AVMixerChannel *const mixer_channel,
                                                     const uint32_t channel)
{
    const AV_LQMixerData *const lq_mixer_data = (const AV_LQMixerData *const) mixer_data;
    AV_LQMixerChannelInfo *const channel_info = lq_mixer_data->channel_info + channel;

    if ((channel_info->current.volume == mixer_channel->volume) && (channel_info->current.panning == mixer_channel->panning)) {
        const uint32_t rate = mixer_channel->rate, mix_rate = lq_mixer_data->mix_rate;
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
        const uint32_t rate  = mixer_channel->rate, mix_rate = lq_mixer_data->mix_rate;
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

        set_mix_functions(lq_mixer_data, &channel_info->current);
        set_mix_functions(lq_mixer_data, &channel_info->next);
    }
}

static av_cold void set_channel_position_repeat_flags(AVMixerData *const mixer_data,
                                                      const AVMixerChannel *const mixer_channel,
                                                      const uint32_t channel)
{
    const AV_LQMixerData *const lq_mixer_data = (const AV_LQMixerData *const) mixer_data;
    AV_LQMixerChannelInfo *const channel_info = lq_mixer_data->channel_info + channel;

    if (channel_info->current.flags == mixer_channel->flags) {
        uint32_t repeat = mixer_channel->pos, repeat_len;

        if (repeat != channel_info->current.offset) {
            channel_info->current.offset   = repeat;
            channel_info->current.fraction = 0;
        }

        channel_info->current.offset_one_shoot = mixer_channel->pos_one_shoot;
        repeat                                 = mixer_channel->repeat_start;
        repeat_len                             = mixer_channel->repeat_length;
        channel_info->current.repeat           = repeat;
        channel_info->current.repeat_len       = repeat_len;

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

        channel_info->current.offset_one_shoot = mixer_channel->pos_one_shoot;
        repeat                                 = mixer_channel->repeat_start;
        repeat_len                             = mixer_channel->repeat_length;
        channel_info->current.repeat           = repeat;
        channel_info->current.repeat_len       = repeat_len;

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

        set_mix_functions(lq_mixer_data, &channel_info->current);
    }
}

static av_cold void set_channel_filter(AVMixerData *const mixer_data,
                                       const AVMixerChannel *const mixer_channel,
                                       const uint32_t channel)
{
    const AV_LQMixerData *const lq_mixer_data = (const AV_LQMixerData *const) mixer_data;
    AV_LQMixerChannelInfo *const channel_info = lq_mixer_data->channel_info + channel;

    set_sample_filter(lq_mixer_data, channel_info, &channel_info->current, mixer_channel->filter_cutoff, mixer_channel->filter_damping);
}

static av_cold void mix(AVMixerData *const mixer_data, int32_t *buf) {
    AV_LQMixerData *const lq_mixer_data = (AV_LQMixerData *const) mixer_data;

    if (!(lq_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_FROZEN)) {
        uint32_t current_left      = lq_mixer_data->current_left;
        uint32_t current_left_frac = lq_mixer_data->current_left_frac;
        uint32_t buf_size          = lq_mixer_data->buf_size;

        memset(buf, 0, buf_size << ((lq_mixer_data->channels_out >= 2) ? 3 : 2));

        while (buf_size) {
            if (current_left) {
                const uint32_t mix_len = (buf_size > current_left) ? current_left : buf_size;

                current_left -= mix_len;
                buf_size     -= mix_len;

                mix_sample(lq_mixer_data, buf, mix_len);

                buf += (lq_mixer_data->channels_out >= 2) ? mix_len << 1 : mix_len;
            }

            if (current_left)
                continue;

            if (mixer_data->handler)
                mixer_data->handler(mixer_data);

            current_left_frac += lq_mixer_data->pass_len_frac;
            current_left       = lq_mixer_data->pass_len + (current_left_frac < lq_mixer_data->pass_len_frac);
        }

        lq_mixer_data->current_left      = current_left;
        lq_mixer_data->current_left_frac = current_left_frac;
    }

    // TODO: Execute post-processing step in libavfilter and pass the PCM data.
}

static av_cold void mix_parallel(AVMixerData *const mixer_data,
                                 int32_t *buf,
                                 const uint32_t first_channel,
                                 const uint32_t last_channel) {
    AV_LQMixerData *const lq_mixer_data = (AV_LQMixerData *const) mixer_data;

    if (!(lq_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_FROZEN)) {
        uint32_t current_left      = lq_mixer_data->current_left;
        uint32_t current_left_frac = lq_mixer_data->current_left_frac;
        uint32_t buf_size          = lq_mixer_data->buf_size;

        memset(buf, 0, buf_size << ((lq_mixer_data->channels_out >= 2) ? 3 : 2));

        while (buf_size) {
            if (current_left) {
                const uint32_t mix_len = (buf_size > current_left) ? current_left : buf_size;

                current_left -= mix_len;
                buf_size     -= mix_len;

                mix_sample_parallel(lq_mixer_data, buf, mix_len, first_channel, last_channel);

                buf += (lq_mixer_data->channels_out >= 2) ? mix_len << 1 : mix_len;
            }

            if (current_left)
                continue;

            if (mixer_data->handler)
                mixer_data->handler(mixer_data);

            current_left_frac += lq_mixer_data->pass_len_frac;
            current_left       = lq_mixer_data->pass_len + (current_left_frac < lq_mixer_data->pass_len_frac);
        }

        lq_mixer_data->current_left      = current_left;
        lq_mixer_data->current_left_frac = current_left_frac;
    }

    // TODO: Execute post-processing step in libavfilter and pass the PCM data.
}

AVMixerContext low_quality_mixer = {
    .av_class                          = &avseq_low_quality_mixer_class,
    .name                              = "Low quality mixer",
    .description                       = NULL_IF_CONFIG_SMALL("Optimized for speed and supports linear interpolation."),

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

#endif /* CONFIG_LOW_QUALITY_MIXER */
