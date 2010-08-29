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
    int32_t *buf;
    uint32_t buf_size;
    uint32_t mix_buf_size;
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
        int16_t *sample_start_ptr;
        uint32_t sample_len;
        uint32_t offset;
        uint32_t fraction;
        uint32_t advance;
        uint32_t advance_frac;
        void (*mix_func)(AV_NULLMixerData *mixer_data, struct AV_NULLMixerChannelInfo *channel_info, int32_t **buf, uint32_t *offset, uint32_t *fraction, uint32_t advance, uint32_t adv_frac, uint16_t len);
        uint32_t end_offset;
        uint32_t restart_offset;
        uint32_t repeat;
        uint32_t repeat_len;
        uint32_t count_restart;
        uint32_t counted;
        uint32_t rate;
        void (*mix_backwards_func)(AV_NULLMixerData *mixer_data, struct AV_NULLMixerChannelInfo *channel_info, int32_t **buf, uint32_t *offset, uint32_t *fraction, uint32_t advance, uint32_t adv_frac, uint16_t len);
        uint8_t bits_per_sample;
        uint8_t flags;
        uint8_t volume;
        uint8_t panning;
    } current;
    struct ChannelBlock next;
} AV_NULLMixerChannelInfo;

#if CONFIG_NULL_MIXER
static av_cold AVMixerData *init(AVMixerContext *mixctx, const char *args, void *opaque);
static av_cold int uninit(AVMixerData *mixer_data);
static av_cold uint32_t set_rate(AVMixerData *mixer_data, uint32_t new_mix_rate, uint32_t new_channels);
static av_cold uint32_t set_tempo(AVMixerData *mixer_data, uint32_t new_tempo);
static av_cold uint32_t set_volume(AVMixerData *mixer_data, uint32_t amplify, uint32_t left_volume, uint32_t right_volume, uint32_t channels);
static av_cold void get_channel(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel);
static av_cold void set_channel(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel);
static av_cold void set_channel_volume_panning_pitch(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel);
static av_cold void set_channel_position_repeat_flags(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel);
static av_cold void mix(AVMixerData *mixer_data, int32_t *buf);

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

static void mix_sample(AV_NULLMixerData *mixer_data, int32_t *buf, uint32_t len);
static void set_sample_mix_rate(AV_NULLMixerData *mixer_data, struct ChannelBlock *channel_block, uint32_t rate);
static void set_mix_functions(AV_NULLMixerData *mixer_data, struct ChannelBlock *channel_block);

#define MIX(type)                                                               \
    static void mix_##type(AV_NULLMixerData *mixer_data,                        \
                           AV_NULLMixerChannelInfo *channel_info,               \
                           int32_t **buf, uint32_t *offset, uint32_t *fraction, \
                           uint32_t advance, uint32_t adv_frac, uint32_t len)

MIX(skip);
MIX(skip_backwards);

static av_cold AVMixerData *init(AVMixerContext *mixctx, const char *args, void *opaque)
{
    AV_NULLMixerData *null_mixer_data = NULL;
    AV_NULLMixerChannelInfo *channel_info;
    const char *cfg_buf;
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

    null_mixer_data->mix_buf_size            = mix_buf_mem_size;
    null_mixer_data->buf                     = buf;
    null_mixer_data->buf_size                = buf_size;
    null_mixer_data->mixer_data.mix_buf_size = null_mixer_data->buf_size;
    null_mixer_data->mixer_data.mix_buf      = null_mixer_data->buf;
    channel_rate                             = null_mixer_data->mixer_data.mixctx->frequency;
    null_mixer_data->mixer_data.rate         = channel_rate;
    null_mixer_data->mix_rate                = channel_rate;
    null_mixer_data->channels_in             = channels_in;
    null_mixer_data->channels_out            = channels_out;

    return (AVMixerData *) null_mixer_data;
}

static av_cold int uninit(AVMixerData *mixer_data)
{
    AV_NULLMixerData *null_mixer_data = (AV_NULLMixerData *) mixer_data;

    if (!null_mixer_data)
        return AVERROR_INVALIDDATA;

    av_freep(&null_mixer_data->channel_info);
    av_freep(&null_mixer_data->buf);
    av_free(null_mixer_data);

    return 0;
}

static av_cold uint32_t set_rate(AVMixerData *mixer_data, uint32_t new_mix_rate, uint32_t new_channels)
{
    AV_NULLMixerData *null_mixer_data = (AV_NULLMixerData *) mixer_data;
    uint32_t buf_size, mix_rate, mix_rate_frac;

    buf_size                                 = null_mixer_data->mixer_data.mix_buf_size;
    null_mixer_data->mixer_data.rate         = new_mix_rate;
    null_mixer_data->mixer_data.channels_out = new_channels;

    if ((null_mixer_data->buf_size * null_mixer_data->channels_out) != (buf_size * new_channels)) {
        int32_t *buf = mixer_data->mix_buf;
        uint32_t mix_buf_mem_size = (buf_size * new_channels) << 2;

        if (!(buf = av_realloc(buf, mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(null_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer output channel data.\n");

            return null_mixer_data->mixer_data.rate;
        }

        memset(buf, 0, mix_buf_mem_size);

        null_mixer_data->mixer_data.mix_buf      = buf;
        null_mixer_data->mixer_data.mix_buf_size = buf_size;
    }

    null_mixer_data->channels_out = new_channels;
    null_mixer_data->buf          = null_mixer_data->mixer_data.mix_buf;
    null_mixer_data->buf_size     = null_mixer_data->mixer_data.mix_buf_size;

    if (null_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_MIXING) {
        mix_rate      = new_mix_rate; // TODO: Add check here if this mix rate is supported by target device
        mix_rate_frac = 0;

        if (null_mixer_data->mix_rate != mix_rate) {
            AV_NULLMixerChannelInfo *channel_info = null_mixer_data->channel_info;
            uint16_t i;

            null_mixer_data->mix_rate      = mix_rate;
            null_mixer_data->mix_rate_frac = mix_rate_frac;

            if (null_mixer_data->mixer_data.tempo)
                set_tempo((AVMixerData *) mixer_data, null_mixer_data->mixer_data.tempo);

            for (i = null_mixer_data->channels_in; i > 0; i--) {
                channel_info->current.advance      = channel_info->current.rate / mix_rate;
                channel_info->current.advance_frac = (((uint64_t) channel_info->current.rate % mix_rate) << 32) / mix_rate;
                channel_info->next.advance         = channel_info->next.rate / mix_rate;
                channel_info->next.advance_frac    = (((uint64_t) channel_info->next.rate % mix_rate) << 32) / mix_rate;

                channel_info++;
            }
        }
    }

    // TODO: Inform libavfilter that the target mixing rate has been changed.

    return new_mix_rate;
}

static av_cold uint32_t set_tempo(AVMixerData *mixer_data, uint32_t new_tempo)
{
    AV_NULLMixerData *null_mixer_data = (AV_NULLMixerData *) mixer_data;
    uint32_t channel_rate;
    uint64_t pass_value;

    null_mixer_data->mixer_data.tempo = new_tempo;
    channel_rate                      = null_mixer_data->mix_rate * 10;
    pass_value                        = ((uint64_t) channel_rate << 16) + ((uint64_t) null_mixer_data->mix_rate_frac >> 16);
    null_mixer_data->pass_len         = (uint64_t) pass_value / null_mixer_data->mixer_data.tempo;
    null_mixer_data->pass_len_frac    = (((uint64_t) pass_value % null_mixer_data->mixer_data.tempo) << 32) / null_mixer_data->mixer_data.tempo;

    return new_tempo;
}

static av_cold uint32_t set_volume(AVMixerData *mixer_data, uint32_t amplify, uint32_t left_volume, uint32_t right_volume, uint32_t channels)
{
    AV_NULLMixerData *null_mixer_data         = (AV_NULLMixerData *) mixer_data;
    AV_NULLMixerChannelInfo *channel_info     = NULL;
    AV_NULLMixerChannelInfo *old_channel_info = null_mixer_data->channel_info;
    uint32_t old_channels, i;

    if (((old_channels = null_mixer_data->channels_in) != channels) && (!(channel_info = av_mallocz((channels * sizeof(AV_NULLMixerChannelInfo)) + FF_INPUT_BUFFER_PADDING_SIZE)))) {
        av_log(null_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer channel data.\n");

        return old_channels;
    }

    null_mixer_data->mixer_data.volume_boost = amplify;
    null_mixer_data->mixer_data.volume_left  = left_volume;
    null_mixer_data->mixer_data.volume_right = right_volume;
    null_mixer_data->mixer_data.channels_in  = channels;

    if (old_channels && channel_info && (old_channels != channels)) {
        uint32_t copy_channels = old_channels;

        if (copy_channels > channels)
            copy_channels = channels;

        memcpy(channel_info, old_channel_info, copy_channels * sizeof(AV_NULLMixerChannelInfo));

        null_mixer_data->channel_info = channel_info;
        null_mixer_data->channels_in  = channels;

        av_free(old_channel_info);
    }

    channel_info = null_mixer_data->channel_info;

    for (i = channels; i > 0; i--) {
        set_sample_mix_rate(null_mixer_data, &channel_info->current, channel_info->current.rate);

        channel_info++;
    }

    return channels;
}

static av_cold void get_channel(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    AV_NULLMixerData *null_mixer_data = (AV_NULLMixerData *) mixer_data;
    AV_NULLMixerChannelInfo *channel_info;

    channel_info                   = null_mixer_data->channel_info + channel;
    mixer_channel->pos             = channel_info->current.offset;
    mixer_channel->bits_per_sample = channel_info->current.bits_per_sample;
    mixer_channel->flags           = channel_info->current.flags;
    mixer_channel->volume          = channel_info->current.volume;
    mixer_channel->panning         = channel_info->current.panning;
    mixer_channel->data            = channel_info->current.sample_start_ptr;
    mixer_channel->len             = channel_info->current.sample_len;
    mixer_channel->repeat_start    = channel_info->current.repeat;
    mixer_channel->repeat_length   = channel_info->current.repeat_len;
    mixer_channel->repeat_count    = channel_info->current.count_restart;
    mixer_channel->repeat_counted  = channel_info->current.counted;
    mixer_channel->rate            = channel_info->current.rate;
}

static av_cold void set_channel(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    AV_NULLMixerData *null_mixer_data = (AV_NULLMixerData *) mixer_data;
    AV_NULLMixerChannelInfo *channel_info;
    struct ChannelBlock *channel_block;
    uint32_t repeat, repeat_len;

    channel_info                        = null_mixer_data->channel_info + channel;
    channel_block                       = &channel_info->current;
    channel_info->next.sample_start_ptr = NULL;

    if (mixer_channel->flags & AVSEQ_MIXER_CHANNEL_FLAG_SYNTH)
        channel_block = &channel_info->next;

    channel_block->offset           = mixer_channel->pos;
    channel_block->fraction         = 0;
    channel_block->bits_per_sample  = mixer_channel->bits_per_sample;
    channel_block->flags            = mixer_channel->flags;
    channel_block->volume           = mixer_channel->volume;
    channel_block->panning          = mixer_channel->panning;
    channel_block->sample_start_ptr = mixer_channel->data;
    channel_block->sample_len       = mixer_channel->len;
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

    set_sample_mix_rate(null_mixer_data, channel_block, mixer_channel->rate);
}

static av_cold void set_channel_volume_panning_pitch(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    AV_NULLMixerData *null_mixer_data     = (AV_NULLMixerData *) mixer_data;
    AV_NULLMixerChannelInfo *channel_info = null_mixer_data->channel_info + channel;

    if ((channel_info->current.volume == mixer_channel->volume) && (channel_info->current.panning == mixer_channel->panning)) {
        uint32_t rate = mixer_channel->rate, mix_rate = null_mixer_data->mix_rate, rate_frac;

        channel_info->current.rate         = rate;
        channel_info->next.rate            = rate;
        rate_frac                          = rate / mix_rate;
        channel_info->current.advance      = rate_frac;
        channel_info->next.advance         = rate_frac;
        rate_frac                          = (((uint64_t) rate % mix_rate) << 32) / mix_rate;
        channel_info->current.advance_frac = rate_frac;
        channel_info->next.advance_frac    = rate_frac;
    } else {
        uint32_t rate  = mixer_channel->rate, mix_rate = null_mixer_data->mix_rate, rate_frac;
        uint8_t volume = mixer_channel->volume;
        int8_t panning = mixer_channel->panning;

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

        set_mix_functions(null_mixer_data, &channel_info->current);
        set_mix_functions(null_mixer_data, &channel_info->next);
    }
}

static av_cold void set_channel_position_repeat_flags(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    AV_NULLMixerData *null_mixer_data     = (AV_NULLMixerData *) mixer_data;
    AV_NULLMixerChannelInfo *channel_info = null_mixer_data->channel_info + channel;

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

        set_mix_functions(null_mixer_data, &channel_info->current);
    }
}

static av_cold void mix(AVMixerData *mixer_data, int32_t *buf)
{
    AV_NULLMixerData *null_mixer_data = (AV_NULLMixerData *) mixer_data;
    uint32_t mix_rate, current_left, current_left_frac, buf_size;

    if (!(null_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_FROZEN)) {
        mix_rate          = null_mixer_data->mix_rate;
        current_left      = null_mixer_data->current_left;
        current_left_frac = null_mixer_data->current_left_frac;
        buf_size          = null_mixer_data->buf_size;

        while (buf_size) {
            if (current_left) {
                uint32_t mix_len = buf_size;

                if (buf_size > current_left)
                    mix_len = current_left;

                current_left -= mix_len;
                buf_size     -= mix_len;

                mix_sample(null_mixer_data, buf, mix_len);
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

static void mix_sample(AV_NULLMixerData *mixer_data, int32_t *buf, uint32_t len)
{
    AV_NULLMixerChannelInfo *channel_info = mixer_data->channel_info;
    uint16_t i;

    for (i = mixer_data->channels_in; i > 0; i--) {
        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            void (*mix_func)(AV_NULLMixerData *mixer_data, AV_NULLMixerChannelInfo *channel_info, int32_t **buf, uint32_t *offset, uint32_t *fraction, uint32_t advance, uint32_t adv_frac, uint32_t len);
            int32_t *mix_buf    = buf;
            uint32_t offset     = channel_info->current.offset;
            uint32_t fraction   = channel_info->current.fraction;
            uint32_t advance    = channel_info->current.advance;
            uint32_t adv_frac   = channel_info->current.advance_frac;
            uint32_t remain_len = len, remain_mix;
            uint32_t counted;
            uint32_t count_restart;
            uint64_t calc_mix;

            mix_func = (void *) channel_info->current.mix_func;

            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
mix_sample_backwards:
                for (;;) {
                    calc_mix = (((((uint64_t) advance << 32) + adv_frac) * remain_len) + fraction) >> 32;

                    if ((int32_t) (remain_mix = offset - channel_info->current.end_offset) > 0) {
                        if ((uint32_t) calc_mix < remain_mix) {
                            mix_func(mixer_data, channel_info, (int32_t **) &mix_buf, (uint32_t *) &offset, (uint32_t *) &fraction, advance, adv_frac, remain_len);

                            if ((int32_t) offset <= (int32_t) channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            mix_func(mixer_data, channel_info, (int32_t **) &mix_buf, (uint32_t *) &offset, (uint32_t *) &fraction, advance, adv_frac, (uint32_t) calc_mix);

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
                                void *mixer_change_func;

                                if (channel_info->next.sample_start_ptr) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.sample_start_ptr = NULL;
                                }

                                mixer_change_func                        = (void *) channel_info->current.mix_backwards_func;
                                channel_info->current.mix_backwards_func = (void *) mix_func;
                                mix_func                                 = (void *) mixer_change_func;
                                channel_info->current.mix_func           = (void *) mix_func;
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

                                if (channel_info->next.sample_start_ptr)
                                    goto mix_sample_synth;

                                if ((int32_t) remain_len > 0)
                                    continue;

                                break;
                            }
                        }
                    } else {
                        if (channel_info->next.sample_start_ptr)
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
                            mix_func(mixer_data, channel_info, (int32_t **) &mix_buf, (uint32_t *) &offset, (uint32_t *) &fraction, advance, adv_frac, remain_len);

                            if (offset >= channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            mix_func(mixer_data, channel_info, (int32_t **) &mix_buf, (uint32_t *) &offset, (uint32_t *) &fraction, advance, adv_frac, (uint32_t) calc_mix);

                            if ((offset < channel_info->current.end_offset) && !remain_len)
                                break;
                        }
                    }

                    if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                        counted = channel_info->current.counted++;

                        if ((count_restart = channel_info->current.count_restart) && (count_restart == counted)) {
                            channel_info->current.flags     &= ~AVSEQ_MIXER_CHANNEL_FLAG_LOOP;
                            channel_info->current.end_offset = channel_info->current.sample_len;

                            goto mix_sample_synth;
                        } else {
                            if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG) {
                                void *mixer_change_func;

                                if (channel_info->next.sample_start_ptr) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.sample_start_ptr = NULL;
                                }

                                mixer_change_func                        = (void *) channel_info->current.mix_backwards_func;
                                channel_info->current.mix_backwards_func = (void *) mix_func;
                                mix_func                                 = (void *) mixer_change_func;
                                channel_info->current.mix_func           = (void *) mix_func;
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

                                if (channel_info->next.sample_start_ptr) {
                                    memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                                    channel_info->next.sample_start_ptr = NULL;
                                }

                                if ((int32_t) remain_len > 0)
                                    continue;

                                break;
                            }
                        }
                    } else {
                        if (channel_info->next.sample_start_ptr) {
mix_sample_synth:
                            memcpy(&channel_info->current, &channel_info->next, sizeof(struct ChannelBlock));

                            channel_info->next.sample_start_ptr = NULL;

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
    }
}

static void set_sample_mix_rate(AV_NULLMixerData *mixer_data, struct ChannelBlock *channel_block, uint32_t rate)
{
    uint32_t mix_rate;

    channel_block->rate         = rate;
    mix_rate                    = mixer_data->mix_rate;
    channel_block->advance      = rate / mix_rate;
    channel_block->advance_frac = (((uint64_t) rate % mix_rate) << 32) / mix_rate;

    set_mix_functions(mixer_data, channel_block);
}

static void set_mix_functions(AV_NULLMixerData *mixer_data, struct ChannelBlock *channel_block)
{
    if (channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
        channel_block->mix_func           = (void *) mix_skip_backwards;
        channel_block->mix_backwards_func = (void *) mix_skip;
    } else {
        channel_block->mix_func           = (void *) mix_skip;
        channel_block->mix_backwards_func = (void *) mix_skip_backwards;
    }
}

MIX(skip)
{
    uint32_t curr_offset = *offset, curr_frac = *fraction, skip_div;
    uint64_t skip_len    = (((uint64_t) advance << 32) + adv_frac) * len;

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
    uint32_t curr_offset = *offset, curr_frac = *fraction, skip_div;
    uint64_t skip_len    = (((uint64_t) advance << 32) + adv_frac) * len;

    skip_div     = skip_len >> 32;
    curr_offset -= skip_div;
    skip_div     = skip_len;
    curr_frac   += skip_div;

    if (curr_frac < skip_div)
        curr_offset--;

    *offset   = curr_offset;
    *fraction = curr_frac;
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
    .set_channel_volume_panning_pitch  = set_channel_volume_panning_pitch,
    .set_channel_position_repeat_flags = set_channel_position_repeat_flags,
    .mix                               = mix,
};

#endif /* CONFIG_NULL_MIXER */
