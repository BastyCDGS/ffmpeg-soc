/*
 * Sequencer null mixer
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
 * Sequencer null mixer.
 */

#include "libavcodec/avcodec.h"
#include "libavutil/avstring.h"
#include "libavsequencer/mixer.h"

typedef struct AV_NULLMixerData {
    AVMixerData mixer_data;
    struct AV_NULLMixerChannelInfo *channel_info;
    uint32_t amplify;
    uint32_t mix_rate;
    uint32_t mix_rate_frac;
    uint32_t current_left;
    uint32_t current_left_frac;
    uint32_t pass_len;
    uint32_t pass_len_frac;
    uint16_t channels_in;
    uint16_t channels_out;
} AV_NULLMixerData;

typedef struct AV_NULLMixerChannelInfo {
    struct ChannelBlock {
        const int16_t *data;
        uint32_t len;
        uint32_t offset;
        uint32_t fraction;
        uint32_t offset_one_shoot;
        uint32_t advance;
        uint32_t advance_frac;
        uint32_t end_offset;
        uint32_t restart_offset;
        uint32_t repeat;
        uint32_t repeat_len;
        uint32_t count_restart;
        uint32_t counted;
        uint32_t rate;
        uint8_t bits_per_sample;
        uint8_t flags;
        uint8_t volume;
        uint8_t panning;
        uint16_t filter_cutoff;
        uint16_t filter_damping;
    } current;
    struct ChannelBlock next;
} AV_NULLMixerChannelInfo;

#if CONFIG_NULL_MIXER
static const char *null_mixer_name(void *p)
{
    AVMixerContext *mixctx = p;

    return mixctx->name;
}

static const AVClass avseq_null_mixer_class = {
    "AVSequencer Null Mixer",
    null_mixer_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

#define MIX(type)                                                                                      \
    static inline void mix_##type(const AV_NULLMixerData *const mixer_data,                            \
                                  const struct ChannelBlock *const channel_block,                      \
                                  uint32_t *const offset, uint32_t *const fraction,                    \
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

static void mix_sample(AV_NULLMixerData *const mixer_data,
                       const uint32_t len)
{
    AV_NULLMixerChannelInfo *channel_info = mixer_data->channel_info;
    uint16_t i                            = mixer_data->channels_in;

    do {
        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            uint32_t offset     = channel_info->current.offset;
            uint32_t fraction   = channel_info->current.fraction;
            uint32_t advance    = channel_info->current.advance;
            uint32_t adv_frac   = channel_info->current.advance_frac;
            uint32_t remain_len = len, remain_mix;
            uint32_t counted;
            uint32_t count_restart;
            uint64_t calc_mix;

            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
mix_sample_backwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = offset - channel_info->current.end_offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            mix_skip_backwards(mixer_data, &channel_info->current, &offset, &fraction, advance, adv_frac, remain_len);

                            if ((int32_t) offset <= (int32_t) channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            mix_skip_backwards(mixer_data, &channel_info->current, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);

                            if (((int32_t) offset > (int32_t) channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        counted = channel_info->current.counted++;

                        if ((count_restart = channel_info->current.count_restart) && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = -1;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

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
                            mix_skip(mixer_data, &channel_info->current, &offset, &fraction, advance, adv_frac, remain_len);

                            if (offset >= channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            mix_skip(mixer_data, &channel_info->current, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);

                            if ((offset < channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        counted = channel_info->current.counted++;

                        if ((count_restart = channel_info->current.count_restart) && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = channel_info->current.len;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

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

static void mix_sample_parallel(AV_NULLMixerData *const mixer_data,
                                const uint32_t len,
                                const uint32_t first_channel,
                                const uint32_t last_channel)
{
    AV_NULLMixerChannelInfo *channel_info = mixer_data->channel_info + first_channel;
    uint16_t i                            = (last_channel - first_channel) + 1;

    do {
        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            uint32_t offset     = channel_info->current.offset;
            uint32_t fraction   = channel_info->current.fraction;
            uint32_t advance    = channel_info->current.advance;
            uint32_t adv_frac   = channel_info->current.advance_frac;
            uint32_t remain_len = len, remain_mix;
            uint32_t counted;
            uint32_t count_restart;
            uint64_t calc_mix;

            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
mix_sample_backwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = offset - channel_info->current.end_offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            mix_skip_backwards(mixer_data, &channel_info->current, &offset, &fraction, advance, adv_frac, remain_len);

                            if ((int32_t) offset <= (int32_t) channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            mix_skip_backwards(mixer_data, &channel_info->current, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);

                            if (((int32_t) offset > (int32_t) channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        counted = channel_info->current.counted++;

                        if ((count_restart = channel_info->current.count_restart) && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = -1;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

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
                            mix_skip(mixer_data, &channel_info->current, &offset, &fraction, advance, adv_frac, remain_len);

                            if (offset >= channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            mix_skip(mixer_data, &channel_info->current, &offset, &fraction, advance, adv_frac, (uint32_t) calc_mix);

                            if ((offset < channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        counted = channel_info->current.counted++;

                        if ((count_restart = channel_info->current.count_restart) && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = channel_info->current.len;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                if (channel_info->next.data) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.data = NULL;
                                }

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

static void set_sample_mix_rate(const AV_NULLMixerData *const mixer_data,
                                struct ChannelBlock *const channel_block,
                                const uint32_t rate)
{
    const uint32_t mix_rate = mixer_data->mix_rate;

    channel_block->rate         = rate;
    channel_block->advance      = rate / mix_rate;
    channel_block->advance_frac = (((uint64_t) rate % mix_rate) << 32) / mix_rate;
}

static av_cold AVMixerData *init(AVMixerContext *const mixctx,
                                 const char *args, void *opaque)
{
    AV_NULLMixerData *null_mixer_data = NULL;
    AV_NULLMixerChannelInfo *channel_info;
    const char *cfg_buf;
    uint16_t i;
    int32_t *buf;
    unsigned buf_size;
    uint32_t mix_buf_mem_size, channel_rate;
    uint16_t channels_in = 1, channels_out = 1;

    if (!(null_mixer_data = av_mallocz(sizeof(AV_NULLMixerData) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer data factory.\n");

        return NULL;
    }

    null_mixer_data->mixer_data.mixctx = mixctx;
    buf_size                           = null_mixer_data->mixer_data.mixctx->buf_size;

    if ((cfg_buf = av_stristr(args, "buffer=")))
        sscanf(cfg_buf, "buffer=%d;", &buf_size);

    if (!(channel_info = av_mallocz((channels_in * sizeof(AV_NULLMixerChannelInfo)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer channel data.\n");
        av_free(null_mixer_data);

        return NULL;
    }

    null_mixer_data->channel_info           = channel_info;
    null_mixer_data->mixer_data.channels_in = channels_in;
    null_mixer_data->channels_in            = channels_in;
    null_mixer_data->channels_out           = channels_out;
    mix_buf_mem_size                       = (buf_size << 2) * channels_out;

    if (!(buf = av_mallocz(mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(mixctx, AV_LOG_ERROR, "Cannot allocate mixer output buffer.\n");
        av_freep(&null_mixer_data->channel_info);
        av_free(null_mixer_data);

        return NULL;
    }

    null_mixer_data->mixer_data.mix_buf_size = buf_size;
    null_mixer_data->mixer_data.mix_buf      = buf;
    channel_rate                             = null_mixer_data->mixer_data.mixctx->frequency;
    null_mixer_data->mixer_data.rate         = channel_rate;
    null_mixer_data->mix_rate                = channel_rate;
    null_mixer_data->channels_in             = channels_in;
    null_mixer_data->channels_out            = channels_out;

    for (i = null_mixer_data->channels_in; i > 0; i--) {
        channel_info->current.filter_cutoff = 4095;
        channel_info->next.filter_cutoff    = 4095;

        channel_info++;
    }

    return (AVMixerData *) null_mixer_data;
}

static av_cold int uninit(AVMixerData *const mixer_data)
{
    AV_NULLMixerData *null_mixer_data = (AV_NULLMixerData *) mixer_data;

    if (!null_mixer_data)
        return AVERROR_INVALIDDATA;

    av_freep(&null_mixer_data->channel_info);
    av_freep(&null_mixer_data->mixer_data.mix_buf);
    av_free(null_mixer_data);

    return 0;
}

static av_cold uint32_t set_tempo(AVMixerData *const mixer_data,
                                  const uint32_t tempo)
{
    AV_NULLMixerData *const null_mixer_data = (AV_NULLMixerData *const) mixer_data;
    const uint32_t channel_rate             = null_mixer_data->mix_rate * 10;
    uint64_t pass_value;

    null_mixer_data->mixer_data.tempo = tempo;
    pass_value                        = ((uint64_t) channel_rate << 16) + ((uint64_t) null_mixer_data->mix_rate_frac >> 16);
    null_mixer_data->pass_len         = (uint64_t) pass_value / null_mixer_data->mixer_data.tempo;
    null_mixer_data->pass_len_frac    = (((uint64_t) pass_value % null_mixer_data->mixer_data.tempo) << 32) / null_mixer_data->mixer_data.tempo;

    return tempo;
}

static av_cold uint32_t set_rate(AVMixerData *const mixer_data,
                                 const uint32_t mix_rate,
                                 const uint32_t channels)
{
    AV_NULLMixerData *const null_mixer_data = (AV_NULLMixerData *const) mixer_data;
    uint32_t buf_size, old_mix_rate, mix_rate_frac;

    buf_size                                 = null_mixer_data->mixer_data.mix_buf_size;
    null_mixer_data->mixer_data.rate         = mix_rate;
    null_mixer_data->mixer_data.channels_out = channels;

    if ((null_mixer_data->mixer_data.mix_buf_size * null_mixer_data->channels_out) != (buf_size * channels)) {
        int32_t *buf                    = null_mixer_data->mixer_data.mix_buf;
        const uint32_t mix_buf_mem_size = (buf_size * channels) << 2;

        if (!(buf = av_realloc(buf, mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(null_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer output channel data.\n");

            return null_mixer_data->mixer_data.rate;
        }

        memset(buf, 0, mix_buf_mem_size);

        null_mixer_data->mixer_data.mix_buf      = buf;
        null_mixer_data->mixer_data.mix_buf_size = buf_size;
    }

    null_mixer_data->channels_out = channels;

    if (null_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_MIXING) {
        old_mix_rate  = mix_rate; // TODO: Add check here if this mix rate is supported by target device
        mix_rate_frac = 0;

        if (null_mixer_data->mix_rate != old_mix_rate) {
            AV_NULLMixerChannelInfo *channel_info = null_mixer_data->channel_info;
            uint16_t i;

            null_mixer_data->mix_rate      = old_mix_rate;
            null_mixer_data->mix_rate_frac = mix_rate_frac;

            if (null_mixer_data->mixer_data.tempo)
                set_tempo((AVMixerData *) mixer_data, null_mixer_data->mixer_data.tempo);

            for (i = null_mixer_data->channels_in; i > 0; i--) {
                channel_info->current.advance      = channel_info->current.rate / old_mix_rate;
                channel_info->current.advance_frac = (((uint64_t) channel_info->current.rate % old_mix_rate) << 32) / old_mix_rate;
                channel_info->next.advance         = channel_info->next.rate / old_mix_rate;
                channel_info->next.advance_frac    = (((uint64_t) channel_info->next.rate % old_mix_rate) << 32) / old_mix_rate;

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
    AV_NULLMixerData *const null_mixer_data         = (AV_NULLMixerData *const) mixer_data;
    AV_NULLMixerChannelInfo *channel_info           = NULL;
    AV_NULLMixerChannelInfo *const old_channel_info = null_mixer_data->channel_info;
    uint32_t old_channels, i;

    if (((old_channels = null_mixer_data->channels_in) != channels) && !(channel_info = av_mallocz((channels * sizeof(AV_NULLMixerChannelInfo)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(null_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer channel data.\n");

        return old_channels;
    }

    null_mixer_data->mixer_data.volume_boost = amplify;
    null_mixer_data->mixer_data.volume_left  = left_volume;
    null_mixer_data->mixer_data.volume_right = right_volume;
    null_mixer_data->mixer_data.channels_in  = channels;

    if (old_channels && channel_info && (old_channels != channels)) {
        uint32_t copy_channels = old_channels;
        uint16_t i;

        if (copy_channels > channels)
            copy_channels = channels;

        memcpy(channel_info, old_channel_info, copy_channels * sizeof(AV_NULLMixerChannelInfo));

        null_mixer_data->channel_info = channel_info;
        null_mixer_data->channels_in  = channels;

        channel_info += copy_channels;

        for (i = copy_channels; i < channels; ++i) {
            channel_info->current.filter_cutoff = 4095;
            channel_info->next.filter_cutoff    = 4095;

            channel_info++;
        }

        av_free(old_channel_info);
    }

    channel_info = null_mixer_data->channel_info;

    for (i = channels; i > 0; i--) {
        set_sample_mix_rate(null_mixer_data, &channel_info->current, channel_info->current.rate);

        channel_info++;
    }

    return channels;
}

static av_cold void get_channel(const AVMixerData *const mixer_data,
                                AVMixerChannel *const mixer_channel,
                                const uint32_t channel)
{
    const AV_NULLMixerData *const null_mixer_data     = (const AV_NULLMixerData *const) mixer_data;
    const AV_NULLMixerChannelInfo *const channel_info = null_mixer_data->channel_info + channel;

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
    const AV_NULLMixerData *const null_mixer_data = (const AV_NULLMixerData *const) mixer_data;
    AV_NULLMixerChannelInfo *const channel_info   = null_mixer_data->channel_info + channel;
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

    if ((channel_block->filter_cutoff = mixer_channel->filter_cutoff) > 4095)
        channel_block->filter_cutoff = 4095;

    if ((channel_block->filter_damping = mixer_channel->filter_damping) > 4095)
        channel_block->filter_damping = 4095;

    set_sample_mix_rate(null_mixer_data, channel_block, mixer_channel->rate);
}

static av_cold void reset_channel(AVMixerData *const mixer_data,
                                  const uint32_t channel)
{
    const AV_NULLMixerData *const null_mixer_data = (const AV_NULLMixerData *const) mixer_data;
    AV_NULLMixerChannelInfo *const channel_info = null_mixer_data->channel_info + channel;
    struct ChannelBlock *channel_block        = &channel_info->current;

    channel_block->offset             = 0;
    channel_block->fraction           = 0;
    channel_block->offset_one_shoot   = 0;
    channel_block->bits_per_sample    = 0;
    channel_block->flags              = 0;
    channel_block->volume             = 0;
    channel_block->panning            = 0;
    channel_block->data               = NULL;
    channel_block->len                = 0;
    channel_block->repeat             = 0;
    channel_block->repeat_len         = 0;
    channel_block->end_offset         = 0;
    channel_block->restart_offset     = 0;
    channel_block->count_restart      = 0;
    channel_block->counted            = 0;
    channel_block->filter_cutoff      = 4095;
    channel_block->filter_damping     = 0;

    channel_block                     = &channel_info->next;
    channel_block->offset             = 0;
    channel_block->fraction           = 0;
    channel_block->offset_one_shoot   = 0;
    channel_block->bits_per_sample    = 0;
    channel_block->flags              = 0;
    channel_block->volume             = 0;
    channel_block->panning            = 0;
    channel_block->data               = NULL;
    channel_block->len                = 0;
    channel_block->repeat             = 0;
    channel_block->repeat_len         = 0;
    channel_block->end_offset         = 0;
    channel_block->restart_offset     = 0;
    channel_block->count_restart      = 0;
    channel_block->counted            = 0;
    channel_block->filter_cutoff      = 4095;
    channel_block->filter_damping     = 0;
}

static av_cold void get_both_channels(const AVMixerData *const mixer_data,
                                      AVMixerChannel *const mixer_channel_current,
                                      AVMixerChannel *const mixer_channel_next,
                                      const uint32_t channel)
{
    const AV_NULLMixerData *const null_mixer_data     = (const AV_NULLMixerData *const) mixer_data;
    const AV_NULLMixerChannelInfo *const channel_info = null_mixer_data->channel_info + channel;

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
    const AV_NULLMixerData *const null_mixer_data = (const AV_NULLMixerData *const) mixer_data;
    AV_NULLMixerChannelInfo *const channel_info   = null_mixer_data->channel_info + channel;
    struct ChannelBlock *channel_block            = &channel_info->current;
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

    if ((channel_block->filter_cutoff = mixer_channel_current->filter_cutoff) > 4095)
        channel_block->filter_cutoff = 4095;

    if ((channel_block->filter_damping = mixer_channel_current->filter_damping) > 4095)
        channel_block->filter_damping = 4095;

    set_sample_mix_rate(null_mixer_data, channel_block, mixer_channel_current->rate);

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

    if ((channel_block->filter_cutoff = mixer_channel_next->filter_cutoff) > 4095)
        channel_block->filter_cutoff = 4095;

    if ((channel_block->filter_damping = mixer_channel_next->filter_damping) > 4095)
        channel_block->filter_damping = 4095;

    set_sample_mix_rate(null_mixer_data, channel_block, mixer_channel_next->rate);
}

static av_cold void set_channel_volume_panning_pitch(AVMixerData *const mixer_data,
                                                     const AVMixerChannel *const mixer_channel,
                                                     const uint32_t channel)
{
    const AV_NULLMixerData *const null_mixer_data = (const AV_NULLMixerData *const) mixer_data;
    AV_NULLMixerChannelInfo *const channel_info   = null_mixer_data->channel_info + channel;

    if ((channel_info->current.volume == mixer_channel->volume) && (channel_info->current.panning == mixer_channel->panning)) {
        const uint32_t rate = mixer_channel->rate, mix_rate = null_mixer_data->mix_rate;
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
        const uint32_t rate  = mixer_channel->rate, mix_rate = null_mixer_data->mix_rate;
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
    }
}

static av_cold void set_channel_position_repeat_flags(AVMixerData *const mixer_data,
                                                      const AVMixerChannel *const mixer_channel,
                                                      const uint32_t channel)
{
    const AV_NULLMixerData *const null_mixer_data = (const AV_NULLMixerData *const) mixer_data;
    AV_NULLMixerChannelInfo *const channel_info   = null_mixer_data->channel_info + channel;

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
    }
}

static av_cold void set_channel_filter(AVMixerData *const mixer_data,
                                       const AVMixerChannel *const mixer_channel,
                                       const uint32_t channel)
{
    const AV_NULLMixerData *const null_mixer_data = (const AV_NULLMixerData *const) mixer_data;
    AV_NULLMixerChannelInfo *const channel_info   = null_mixer_data->channel_info + channel;

    if ((channel_info->current.filter_cutoff = mixer_channel->filter_cutoff) > 4095)
        channel_info->current.filter_cutoff = 4095;

    if ((channel_info->current.filter_damping = mixer_channel->filter_damping) > 4095)
        channel_info->current.filter_damping = 4095;
}

static av_cold void mix(AVMixerData *const mixer_data, int32_t *buf)
{
    AV_NULLMixerData *const null_mixer_data = (AV_NULLMixerData *const) mixer_data;

    if (!(null_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_FROZEN)) {
        uint32_t current_left      = null_mixer_data->current_left;
        uint32_t current_left_frac = null_mixer_data->current_left_frac;
        uint32_t buf_size          = null_mixer_data->mixer_data.mix_buf_size;

        while (buf_size) {
            if (current_left) {
                uint32_t mix_len = buf_size;

                if (buf_size > current_left)
                    mix_len = current_left;

                current_left -= mix_len;
                buf_size     -= mix_len;

                mix_sample(null_mixer_data, mix_len);
            }

            if (current_left)
                continue;

            if (mixer_data->handler)
                mixer_data->handler(mixer_data);

            current_left       = null_mixer_data->pass_len;
            current_left_frac += null_mixer_data->pass_len_frac;

            if (current_left_frac < null_mixer_data->pass_len_frac)
                current_left++;
        }

        null_mixer_data->current_left      = current_left;
        null_mixer_data->current_left_frac = current_left_frac;
    }
}

static av_cold void mix_parallel(AVMixerData *const mixer_data,
                                 int32_t *buf,
                                 const uint32_t first_channel,
                                 const uint32_t last_channel)
{
    AV_NULLMixerData *const null_mixer_data = (AV_NULLMixerData *const) mixer_data;

    if (!(null_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_FROZEN)) {
        uint32_t current_left      = null_mixer_data->current_left;
        uint32_t current_left_frac = null_mixer_data->current_left_frac;
        uint32_t buf_size          = null_mixer_data->mixer_data.mix_buf_size;

        while (buf_size) {
            if (current_left) {
                uint32_t mix_len = buf_size;

                if (buf_size > current_left)
                    mix_len = current_left;

                current_left -= mix_len;
                buf_size     -= mix_len;

                mix_sample_parallel(null_mixer_data, mix_len, first_channel, last_channel);
            }

            if (current_left)
                continue;

            if (mixer_data->handler)
                mixer_data->handler(mixer_data);

            current_left       = null_mixer_data->pass_len;
            current_left_frac += null_mixer_data->pass_len_frac;

            if (current_left_frac < null_mixer_data->pass_len_frac)
                current_left++;
        }

        null_mixer_data->current_left      = current_left;
        null_mixer_data->current_left_frac = current_left_frac;
    }
}

AVMixerContext null_mixer = {
    .av_class                          = &avseq_null_mixer_class,
    .name                              = "Null mixer",
    .description                       = NULL_IF_CONFIG_SMALL("Always outputs silence and simulates basic mixing"),

    .flags                             = AVSEQ_MIXER_CONTEXT_FLAG_SURROUND,
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

#endif /* CONFIG_NULL_MIXER */
