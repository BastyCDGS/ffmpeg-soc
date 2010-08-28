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
        int16_t *sample_start_ptr;
        uint32_t sample_len;
        uint32_t offset;
        uint32_t fraction;
        uint32_t advance;
        uint32_t advance_frac;
        void (*mix_func)(AV_LQMixerData *mixer_data, struct ChannelBlock *channel_block, int32_t **buf, uint32_t *offset, uint32_t *fraction, uint32_t advance, uint32_t adv_frac, uint16_t len);
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
        void (*mix_backwards_func)(AV_LQMixerData *mixer_data, struct ChannelBlock *channel_block, int32_t **buf, uint32_t *offset, uint32_t *fraction, uint32_t advance, uint32_t adv_frac, uint16_t len);
        uint8_t bits_per_sample;
        uint8_t flags;
        uint8_t volume;
        uint8_t panning;
    } current;
    struct ChannelBlock next;
} AV_LQMixerChannelInfo;

#if CONFIG_LOW_QUALITY_MIXER
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

static void mix_sample(AV_LQMixerData *mixer_data, int32_t *buf, uint32_t len);
static void set_sample_mix_rate(AV_LQMixerData *mixer_data, struct ChannelBlock *channel_block, uint32_t rate);
static void set_mix_functions(AV_LQMixerData *mixer_data, struct ChannelBlock *channel_block);

#define CHANNEL_PREPARE(type)                                               \
    static void channel_prepare_##type(AV_LQMixerData *mixer_data,          \
                                       struct ChannelBlock *channel_block,  \
                                       uint32_t volume,                     \
                                       uint32_t panning)

CHANNEL_PREPARE(skip);
CHANNEL_PREPARE(stereo_8);
CHANNEL_PREPARE(stereo_8_left);
CHANNEL_PREPARE(stereo_8_right);
CHANNEL_PREPARE(stereo_8_center);
CHANNEL_PREPARE(stereo_16);
CHANNEL_PREPARE(stereo_16_left);
CHANNEL_PREPARE(stereo_16_right);
CHANNEL_PREPARE(stereo_16_center);
CHANNEL_PREPARE(stereo_32);
CHANNEL_PREPARE(stereo_32_left);
CHANNEL_PREPARE(stereo_32_right);
CHANNEL_PREPARE(stereo_32_center);

#define MIX(type)                                                               \
    static void mix_##type(AV_LQMixerData *mixer_data,                          \
                           struct ChannelBlock *channel_block,                  \
                           int32_t **buf, uint32_t *offset, uint32_t *fraction, \
                           uint32_t advance, uint32_t adv_frac, uint32_t len)

MIX(skip);
MIX(skip_backwards);

MIX(mono_8);
MIX(mono_backwards_8);
MIX(mono_16);
MIX(mono_backwards_16);
MIX(mono_32);
MIX(mono_backwards_32);
MIX(mono_x);
MIX(mono_backwards_x);
MIX(mono_16_to_8);
MIX(mono_backwards_16_to_8);
MIX(mono_32_to_8);
MIX(mono_backwards_32_to_8);
MIX(mono_x_to_8);
MIX(mono_backwards_x_to_8);

MIX(stereo_8);
MIX(stereo_backwards_8);
MIX(stereo_16);
MIX(stereo_backwards_16);
MIX(stereo_32);
MIX(stereo_backwards_32);
MIX(stereo_x);
MIX(stereo_backwards_x);
MIX(stereo_16_to_8);
MIX(stereo_backwards_16_to_8);
MIX(stereo_32_to_8);
MIX(stereo_backwards_32_to_8);
MIX(stereo_x_to_8);
MIX(stereo_backwards_x_to_8);

MIX(stereo_8_left);
MIX(stereo_backwards_8_left);
MIX(stereo_16_left);
MIX(stereo_backwards_16_left);
MIX(stereo_32_left);
MIX(stereo_backwards_32_left);
MIX(stereo_x_left);
MIX(stereo_backwards_x_left);
MIX(stereo_16_to_8_left);
MIX(stereo_backwards_16_to_8_left);
MIX(stereo_32_to_8_left);
MIX(stereo_backwards_32_to_8_left);
MIX(stereo_x_to_8_left);
MIX(stereo_backwards_x_to_8_left);

MIX(stereo_8_right);
MIX(stereo_backwards_8_right);
MIX(stereo_16_right);
MIX(stereo_backwards_16_right);
MIX(stereo_32_right);
MIX(stereo_backwards_32_right);
MIX(stereo_x_right);
MIX(stereo_backwards_x_right);
MIX(stereo_16_to_8_right);
MIX(stereo_backwards_16_to_8_right);
MIX(stereo_32_to_8_right);
MIX(stereo_backwards_32_to_8_right);
MIX(stereo_x_to_8_right);
MIX(stereo_backwards_x_to_8_right);

MIX(stereo_8_center);
MIX(stereo_backwards_8_center);
MIX(stereo_16_center);
MIX(stereo_backwards_16_center);
MIX(stereo_32_center);
MIX(stereo_backwards_32_center);
MIX(stereo_x_center);
MIX(stereo_backwards_x_center);
MIX(stereo_16_to_8_center);
MIX(stereo_backwards_16_to_8_center);
MIX(stereo_32_to_8_center);
MIX(stereo_backwards_32_to_8_center);
MIX(stereo_x_to_8_center);
MIX(stereo_backwards_x_to_8_center);

MIX(stereo_8_surround);
MIX(stereo_backwards_8_surround);
MIX(stereo_16_surround);
MIX(stereo_backwards_16_surround);
MIX(stereo_32_surround);
MIX(stereo_backwards_32_surround);
MIX(stereo_x_surround);
MIX(stereo_backwards_x_surround);
MIX(stereo_16_to_8_surround);
MIX(stereo_backwards_16_to_8_surround);
MIX(stereo_32_to_8_surround);
MIX(stereo_backwards_32_to_8_surround);
MIX(stereo_x_to_8_surround);
MIX(stereo_backwards_x_to_8_surround);

// TODO: Implement low quality mixer identification and configuration.

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

static av_cold AVMixerData *init(AVMixerContext *mixctx, const char *args, void *opaque)
{
    AV_LQMixerData *lq_mixer_data;
    AV_LQMixerChannelInfo *channel_info;
    const char *cfg_buf;
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

    return (AVMixerData *) lq_mixer_data;
}

static av_cold int uninit(AVMixerData *mixer_data)
{
    AV_LQMixerData *lq_mixer_data = (AV_LQMixerData *) mixer_data;

    if (!lq_mixer_data)
        return AVERROR_INVALIDDATA;

    av_freep(&lq_mixer_data->channel_info);
    av_freep(&lq_mixer_data->buf);
    av_freep(&lq_mixer_data->volume_lut);
    av_free(lq_mixer_data);

    return 0;
}

static av_cold uint32_t set_rate(AVMixerData *mixer_data, uint32_t new_mix_rate, uint32_t new_channels)
{
    AV_LQMixerData *lq_mixer_data = (AV_LQMixerData *) mixer_data;
    uint32_t buf_size, mix_rate, mix_rate_frac;

    lq_mixer_data->mixer_data.rate         = new_mix_rate;
    buf_size                               = lq_mixer_data->mixer_data.mix_buf_size;
    lq_mixer_data->mixer_data.channels_out = new_channels;

    if ((lq_mixer_data->buf_size * lq_mixer_data->channels_out) != (buf_size * new_channels)) {
        int32_t *buf = mixer_data->mix_buf;
        uint32_t mix_buf_mem_size = (buf_size * new_channels) << 2;

        if (!(buf = av_realloc(buf, mix_buf_mem_size + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(lq_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer output channel data.\n");

            return lq_mixer_data->mixer_data.rate;
        }

        memset(buf, 0, mix_buf_mem_size);

        lq_mixer_data->mixer_data.mix_buf      = buf;
        lq_mixer_data->mixer_data.mix_buf_size = buf_size;
    }

    lq_mixer_data->channels_out = new_channels;
    lq_mixer_data->buf          = lq_mixer_data->mixer_data.mix_buf;
    lq_mixer_data->buf_size     = lq_mixer_data->mixer_data.mix_buf_size;

    if (lq_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_MIXING) {
        mix_rate      = new_mix_rate; // TODO: Add check here if this mix rate is supported by target device
        mix_rate_frac = 0;

        if (lq_mixer_data->mix_rate != mix_rate) {
            AV_LQMixerChannelInfo *channel_info = lq_mixer_data->channel_info;
            uint16_t i;

            lq_mixer_data->mix_rate      = mix_rate;
            lq_mixer_data->mix_rate_frac = mix_rate_frac;

            if (lq_mixer_data->mixer_data.tempo)
                set_tempo((AVMixerData *) mixer_data, lq_mixer_data->mixer_data.tempo);

            for (i = lq_mixer_data->channels_in; i > 0; i--) {
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
    AV_LQMixerData *lq_mixer_data = (AV_LQMixerData *) mixer_data;
    uint32_t channel_rate;
    uint64_t pass_value;

    lq_mixer_data->mixer_data.tempo = new_tempo;
    channel_rate                    = lq_mixer_data->mix_rate * 10;
    pass_value                      = ((uint64_t) channel_rate << 16) + ((uint64_t) lq_mixer_data->mix_rate_frac >> 16);
    lq_mixer_data->pass_len         = (uint64_t) pass_value / lq_mixer_data->mixer_data.tempo;
    lq_mixer_data->pass_len_frac    = (((uint64_t) pass_value % lq_mixer_data->mixer_data.tempo) << 32) / lq_mixer_data->mixer_data.tempo;

    return new_tempo;
}

static av_cold uint32_t set_volume(AVMixerData *mixer_data, uint32_t amplify, uint32_t left_volume, uint32_t right_volume, uint32_t channels)
{
    AV_LQMixerData *lq_mixer_data           = (AV_LQMixerData *) mixer_data;
    AV_LQMixerChannelInfo *channel_info     = NULL;
    AV_LQMixerChannelInfo *old_channel_info = lq_mixer_data->channel_info;
    uint32_t old_channels, i;

    if (((old_channels = lq_mixer_data->channels_in) != channels) && (!(channel_info = av_mallocz((channels * sizeof(AV_LQMixerChannelInfo)) + FF_INPUT_BUFFER_PADDING_SIZE)))) {
        av_log(lq_mixer_data->mixer_data.mixctx, AV_LOG_ERROR, "Cannot allocate mixer channel data.\n");

        return old_channels;
    }

    lq_mixer_data->mixer_data.volume_boost = amplify;
    lq_mixer_data->mixer_data.volume_left  = left_volume;
    lq_mixer_data->mixer_data.volume_right = right_volume;
    lq_mixer_data->mixer_data.channels_in  = channels;

    if ((old_channels != channels) || (lq_mixer_data->amplify != amplify)) {
        int32_t *volume_lut  = lq_mixer_data->volume_lut;
        uint32_t volume_mult = 0, volume_div = channels << 8;
        uint8_t i            = 0, j = 0;

        lq_mixer_data->amplify = amplify;

        do {
            do {
                int32_t volume = (int8_t) j << 8;

                *volume_lut++ = (int64_t) volume * volume_mult / volume_div;
            } while (++j);

            volume_mult += amplify;
        } while (++i);
    }

    if (channel_info && (old_channels != channels)) {
        uint32_t copy_channels = old_channels;

        if (copy_channels > channels)
            copy_channels = channels;

        memcpy(channel_info, old_channel_info, copy_channels * sizeof(AV_LQMixerChannelInfo));
 
        lq_mixer_data->channel_info = channel_info;
        lq_mixer_data->channels_in  = channels;
 
        av_free(old_channel_info);
    }

    channel_info = lq_mixer_data->channel_info;

    for (i = channels; i > 0; i--) {
        set_sample_mix_rate(lq_mixer_data, &channel_info->current, channel_info->current.rate);

        channel_info++;
    }

    return channels;
}

static av_cold void get_channel(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    AV_LQMixerData *lq_mixer_data = (AV_LQMixerData *) mixer_data;
    AV_LQMixerChannelInfo *channel_info;

    channel_info                   = lq_mixer_data->channel_info + channel;
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
    AV_LQMixerData *lq_mixer_data = (AV_LQMixerData *) mixer_data;
    AV_LQMixerChannelInfo *channel_info;
    uint32_t repeat, repeat_len;

    channel_info                        = lq_mixer_data->channel_info + channel;
    channel_info->next.sample_start_ptr = NULL;

    if (mixer_channel->flags & AVSEQ_MIXER_CHANNEL_FLAG_SYNTH) {
        channel_info->next.flags           |= AVSEQ_MIXER_CHANNEL_FLAG_SYNTH;
        channel_info->next.offset           = mixer_channel->pos;
        channel_info->next.fraction         = 0;
        channel_info->next.bits_per_sample  = mixer_channel->bits_per_sample;
        channel_info->next.flags            = mixer_channel->flags;
        channel_info->next.volume           = mixer_channel->volume;
        channel_info->next.panning          = mixer_channel->panning;
        channel_info->next.sample_start_ptr = mixer_channel->data;
        channel_info->next.sample_len       = mixer_channel->len;
        repeat                              = mixer_channel->repeat_start;
        repeat_len                          = mixer_channel->repeat_length;
        channel_info->next.repeat           = repeat;
        channel_info->next.repeat_len       = repeat_len;

        if (!(channel_info->next.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP)) {
            repeat     = mixer_channel->len;
            repeat_len = 0;
        }

        repeat += repeat_len;

        if (channel_info->next.flags & AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS) {
            repeat -= repeat_len;

            if (!(channel_info->next.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP))
                repeat = -1;
        }

        channel_info->next.end_offset     = repeat;
        channel_info->next.restart_offset = repeat_len;
        channel_info->next.count_restart  = mixer_channel->repeat_count;
        channel_info->next.counted        = mixer_channel->repeat_counted;

        set_sample_mix_rate(lq_mixer_data, &channel_info->next, mixer_channel->rate);
    } else {
        channel_info->current.offset           = mixer_channel->pos;
        channel_info->current.fraction         = 0;
        channel_info->current.bits_per_sample  = mixer_channel->bits_per_sample;
        channel_info->current.flags            = mixer_channel->flags;
        channel_info->current.volume           = mixer_channel->volume;
        channel_info->current.panning          = mixer_channel->panning;
        channel_info->current.sample_start_ptr = mixer_channel->data;
        channel_info->current.sample_len       = mixer_channel->len;
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

        set_sample_mix_rate(lq_mixer_data, &channel_info->current, mixer_channel->rate);
    }
}

static av_cold void set_channel_volume_panning_pitch(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    AV_LQMixerData *lq_mixer_data       = (AV_LQMixerData *) mixer_data;
    AV_LQMixerChannelInfo *channel_info = lq_mixer_data->channel_info + channel;

    if ((channel_info->current.volume == mixer_channel->volume) && (channel_info->current.panning == mixer_channel->panning)) {
        uint32_t rate = mixer_channel->rate, mix_rate = lq_mixer_data->mix_rate, rate_frac;

        channel_info->current.rate         = rate;
        channel_info->next.rate            = rate;
        rate_frac                          = rate / mix_rate;
        channel_info->current.advance      = rate_frac;
        channel_info->next.advance         = rate_frac;
        rate_frac                          = (((uint64_t) rate % mix_rate) << 32) / mix_rate;
        channel_info->current.advance_frac = rate_frac;
        channel_info->next.advance_frac    = rate_frac;
    } else {
        uint32_t rate  = mixer_channel->rate, mix_rate = lq_mixer_data->mix_rate, rate_frac;
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

        set_mix_functions(lq_mixer_data, &channel_info->current);
        set_mix_functions(lq_mixer_data, &channel_info->next);
    }
}

static av_cold void set_channel_position_repeat_flags(AVMixerData *mixer_data, AVMixerChannel *mixer_channel, uint32_t channel)
{
    AV_LQMixerData *lq_mixer_data       = (AV_LQMixerData *) mixer_data;
    AV_LQMixerChannelInfo *channel_info = lq_mixer_data->channel_info + channel;

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

        set_mix_functions(lq_mixer_data, &channel_info->current);
    }
}

static av_cold void mix(AVMixerData *mixer_data, int32_t *buf) {
    AV_LQMixerData *lq_mixer_data = (AV_LQMixerData *) mixer_data;
    uint32_t mix_rate, current_left, current_left_frac, buf_size;

    if (!(lq_mixer_data->mixer_data.flags & AVSEQ_MIXER_DATA_FLAG_FROZEN)) {
        mix_rate          = lq_mixer_data->mix_rate;
        current_left      = lq_mixer_data->current_left;
        current_left_frac = lq_mixer_data->current_left_frac;
        buf_size          = lq_mixer_data->buf_size;

        memset(buf, 0, buf_size << ((lq_mixer_data->channels_out >= 2) ? 3 : 2));

        while (buf_size) {
            if (current_left) {
                uint32_t mix_len = buf_size;

                if (buf_size > current_left)
                    mix_len = current_left;

                current_left -= mix_len;
                buf_size     -= mix_len;

                mix_sample(lq_mixer_data, buf, mix_len);

                buf += (lq_mixer_data->channels_out >= 2) ? mix_len << 1 : mix_len;
            }

            if (current_left)
                continue;

            if (mixer_data->handler)
                mixer_data->handler(mixer_data);

            current_left       = lq_mixer_data->pass_len;
            current_left_frac += lq_mixer_data->pass_len_frac;

            if (current_left_frac < lq_mixer_data->pass_len_frac)
                current_left++;
        }

        lq_mixer_data->current_left      = current_left;
        lq_mixer_data->current_left_frac = current_left_frac;
    }

    // TODO: Execute post-processing step in libavfilter and pass the PCM data.
}

static void mix_sample(AV_LQMixerData *mixer_data, int32_t *buf, uint32_t len)
{
    AV_LQMixerChannelInfo *channel_info = mixer_data->channel_info;
    uint16_t i;

    for (i = mixer_data->channels_in; i > 0; i--) {
        if (channel_info->current.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            void (*mix_func)(AV_LQMixerData *mixer_data, struct ChannelBlock *channel_block, int32_t **buf, uint32_t *offset, uint32_t *fraction, uint32_t advance, uint32_t adv_frac, uint32_t len);
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
                            mix_func(mixer_data, &channel_info->current, (int32_t **) &mix_buf, (uint32_t *) &offset, (uint32_t *) &fraction, advance, adv_frac, remain_len);

                            if ((int32_t) offset <= (int32_t) channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            mix_func(mixer_data, &channel_info->current, (int32_t **) &mix_buf, (uint32_t *) &offset, (uint32_t *) &fraction, advance, adv_frac, (uint32_t) calc_mix);

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
                            mix_func(mixer_data, &channel_info->current, (int32_t **) &mix_buf, (uint32_t *) &offset, (uint32_t *) &fraction, advance, adv_frac, remain_len);

                            if (offset >= channel_info->current.end_offset)
                                remain_len = 0;
                            else
                                break;
                        } else {
                            calc_mix    = (((((uint64_t) remain_mix << 32) - fraction) - 1) / (((uint64_t) advance << 32) + adv_frac) + 1);
                            remain_len -= (uint32_t) calc_mix;

                            mix_func(mixer_data, &channel_info->current, (int32_t **) &mix_buf, (uint32_t *) &offset, (uint32_t *) &fraction, advance, adv_frac, (uint32_t) calc_mix);

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

static void set_sample_mix_rate(AV_LQMixerData *mixer_data, struct ChannelBlock *channel_block, uint32_t rate)
{
    uint32_t mix_rate;

    channel_block->rate         = rate;
    mix_rate                    = mixer_data->mix_rate;
    channel_block->advance      = rate / mix_rate;
    channel_block->advance_frac = (((uint64_t) rate % mix_rate) << 32) / mix_rate;

    set_mix_functions(mixer_data, channel_block);
}

static void set_mix_functions(AV_LQMixerData *mixer_data, struct ChannelBlock *channel_block)
{
    void **mix_func;
    void (*init_mixer_func)(AV_LQMixerData *mixer_data, struct ChannelBlock *channel_block, uint32_t volume, uint32_t panning);
    uint32_t panning = 0x80;

    if ((channel_block->bits_per_sample <= 8) || !mixer_data->real_16_bit_mode) {
        if ((channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_MUTED) || !channel_block->volume || !mixer_data->amplify || !channel_block->sample_start_ptr) {
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
    } else if ((channel_block->flags & AVSEQ_MIXER_CHANNEL_FLAG_MUTED) || !channel_block->volume || !mixer_data->amplify || !channel_block->sample_start_ptr) {
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

MIX(mono_8)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if ((curr_frac + adv_frac) < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += volume_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf++  += volume_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;

            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint8_t) *sample++];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_backwards_8)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset + 1;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += volume_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf++  += volume_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset + 1;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset + 1;
            smp     = volume_lut[(uint8_t) *--sample];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];

            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_16)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;

get_second_advance_sample:
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;

            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_backwards_16)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_32)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_backwards_32)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_x)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += ((int64_t) smp_data * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += ((int64_t) smp_data * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
            i    = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_backwards_x)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += ((int64_t) smp_data * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += ((int64_t) smp_data * mult_left_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data  = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }

            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_16_to_8)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf++  += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *sample++ >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_backwards_16_to_8)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf++  += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *--sample >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_32_to_8)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];

get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf++  += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *sample++ >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_backwards_32_to_8)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf++   += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *--sample >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_x_to_8)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32)
                    {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;

            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += volume_lut[(uint32_t) smp_data >> 24];
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += volume_lut[(uint32_t) smp_data >> 24];
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp  = volume_lut[(uint32_t) smp_data >> 24];
            i    = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(mono_backwards_x_to_8)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += volume_lut[(uint32_t) smp_data >> 24];
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += volume_lut[(uint32_t) smp_data >> 24];
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = ((int64_t) smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = volume_lut[(uint32_t) smp_data >> 24];
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_8)
{
    int8_t *sample            = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_left_lut  = channel_block->volume_left_lut;
    int32_t *volume_right_lut = channel_block->volume_right_lut;
    int32_t *mix_buf          = *buf;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += volume_left_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += volume_right_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf++  += volume_left_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += volume_right_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int8_t smp_in;
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = *sample++;
            smp       = volume_left_lut[(uint8_t) smp_in];
            smp_right = volume_right_lut[(uint8_t) smp_in];
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *sample++;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *sample++;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_8)
{
    int8_t *sample            = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_left_lut  = channel_block->volume_left_lut;
    int32_t *volume_right_lut = channel_block->volume_right_lut;
    int32_t *mix_buf          = *buf;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset + 1;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += volume_left_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += volume_right_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf++  += volume_left_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += volume_right_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset + 1;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int8_t smp_in;
            int32_t smp_right;

            sample   += curr_offset + 1;
            smp_in    = *--sample;
            smp       = volume_left_lut[(uint8_t) smp_in];
            smp_right = volume_right_lut[(uint8_t) smp_in];

            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *--sample;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *--sample;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int16_t smp_in;
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = *sample++;
            smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
            smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *sample++;
                    smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *sample++;
                    smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int16_t smp_in;
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = *--sample;
            smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
            smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *--sample;
                    smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *--sample;
                    smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else
        {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int32_t smp_in;
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = *sample++;
            smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
            smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *sample++;
                    smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *sample++;
                    smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int32_t smp_in;
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = *--sample;
            smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
            smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *--sample;
                    smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = *--sample;
                    smp       = ((int64_t) smp_in * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_in * mult_right_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);

                        bit &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) smp_data * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) smp_data * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);

                bit &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            int32_t smp_right;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = ((int64_t) smp_data * mult_left_volume) / div_volume;
            smp_right = ((int64_t) smp_data * mult_right_volume) / div_volume;
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_data * mult_right_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_data * mult_right_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) smp_data * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf++  += ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += ((int64_t) smp_data * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf++ += ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            int32_t smp_right;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = ((int64_t) smp_data * mult_left_volume) / div_volume;
            smp_right = ((int64_t) smp_data * mult_right_volume) / div_volume;
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_data * mult_right_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                    smp_right = ((int64_t) smp_data * mult_right_volume) / div_volume;
                }

            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_to_8)
{
    int16_t *sample           = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_left_lut  = channel_block->volume_left_lut;
    int32_t *volume_right_lut = channel_block->volume_right_lut;
    int32_t *mix_buf          = *buf;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp       >>= 8;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp       >>= 8;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            uint16_t smp;

            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp          = (uint16_t) sample[curr_offset] >> 8;
                *mix_buf++  += volume_left_lut[(uint8_t) smp];
                *mix_buf++  += volume_right_lut[(uint8_t) smp];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp          = (uint16_t) sample[curr_offset] >> 8;
                *mix_buf++  += volume_left_lut[(uint8_t) smp];
                *mix_buf++  += volume_right_lut[(uint8_t) smp];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        uint16_t smp_in;
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((uint16_t) smp >> 8);
                *mix_buf++ += volume_left_lut[(uint16_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint16_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((uint16_t) smp >> 8);
                *mix_buf++ += volume_left_lut[(uint16_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint16_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = (uint16_t) *sample++ >> 8;
            smp       = volume_left_lut[(uint8_t) smp_in];
            smp_right = volume_right_lut[(uint8_t) smp_in];
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = (uint16_t) *sample++ >> 8;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = (uint16_t) *sample++ >> 8;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_to_8)
{
    int16_t *sample           = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_left_lut  = channel_block->volume_left_lut;
    int32_t *volume_right_lut = channel_block->volume_right_lut;
    int32_t *mix_buf          = *buf;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp       >>= 8;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp       >>= 8;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            uint16_t smp;

            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp          = (uint16_t) sample[curr_offset] >> 8;
                *mix_buf++  += volume_left_lut[(uint8_t) smp];
                *mix_buf++  += volume_right_lut[(uint8_t) smp];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp          = (uint16_t) sample[curr_offset] >> 8;
                *mix_buf++  += volume_left_lut[(uint8_t) smp];
                *mix_buf++  += volume_right_lut[(uint8_t) smp];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        uint16_t smp_in;
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = (uint16_t) smp >> 8;
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = (uint16_t) smp >> 8;
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = (uint16_t) *--sample >> 8;
            smp       = volume_left_lut[(uint8_t) smp_in];
            smp_right = volume_right_lut[(uint8_t) smp_in];
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = (uint16_t) *--sample >> 8;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = (uint16_t) *--sample >> 8;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_to_8)
{
    int32_t *sample           = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_left_lut  = channel_block->volume_left_lut;
    int32_t *volume_right_lut = channel_block->volume_right_lut;
    int32_t *mix_buf          = *buf;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp       >>= 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp       >>= 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            uint16_t smp;

            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp          = (uint32_t) sample[curr_offset] >> 24;
                *mix_buf++  += volume_left_lut[(uint8_t) smp];
                *mix_buf++  += volume_right_lut[(uint8_t) smp];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp          = (uint32_t) sample[curr_offset] >> 24;
                *mix_buf++  += volume_left_lut[(uint8_t) smp];
                *mix_buf++  += volume_right_lut[(uint8_t) smp];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        uint32_t smp_in;
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = (uint32_t) smp >> 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = (uint32_t) smp >> 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = (uint32_t) *sample++ >> 24;
            smp       = volume_left_lut[(uint8_t) smp_in];
            smp_right = volume_right_lut[(uint8_t) smp_in];
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = (uint32_t) *sample++ >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = (uint32_t) *sample++ >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_to_8)
{
    int32_t *sample           = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_left_lut  = channel_block->volume_left_lut;
    int32_t *volume_right_lut = channel_block->volume_right_lut;
    int32_t *mix_buf          = *buf;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp       >>= 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp       >>= 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            uint16_t smp;

            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp          = (uint32_t) sample[curr_offset] >> 24;
                *mix_buf++  += volume_left_lut[(uint8_t) smp];
                *mix_buf++  += volume_right_lut[(uint8_t) smp];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp          = (uint32_t) sample[curr_offset] >> 24;
                *mix_buf++  += volume_left_lut[(uint8_t) smp];
                *mix_buf++  += volume_right_lut[(uint8_t) smp];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        uint32_t smp_in;
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = (uint32_t) smp >> 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];

                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = (uint32_t) smp >> 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            int32_t smp_right;

            sample   += curr_offset;
            smp_in    = (uint32_t) *--sample >> 24;
            smp       = volume_left_lut[(uint8_t) smp_in];
            smp_right = volume_right_lut[(uint8_t) smp_in];
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = (uint32_t) *--sample >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp_in    = (uint32_t) *--sample >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_to_8)
{
    int32_t *sample           = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_left_lut  = channel_block->volume_left_lut;
    int32_t *volume_right_lut = channel_block->volume_right_lut;
    int32_t *mix_buf          = *buf;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp       >>= 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp       >>= 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_data   >>= 24;
                *mix_buf++  += volume_left_lut[(uint8_t) smp_data];
                *mix_buf++  += volume_right_lut[(uint8_t) smp_data];
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_data   >>= 24;
                *mix_buf++  += volume_left_lut[(uint8_t) smp_data];
                *mix_buf++  += volume_right_lut[(uint8_t) smp_data];
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        uint32_t smp_in;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = (uint32_t) smp >> 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((uint32_t) smp >> 24);
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            int32_t smp_right;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp_in    = (uint32_t) smp_data >> 24;
            smp       = volume_left_lut[(uint8_t) smp_in];
            smp_right = volume_right_lut[(uint8_t) smp_in];
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp_in    = (uint32_t) smp_data >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp_in    = (uint32_t) smp_data >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_to_8)
{
    int32_t *sample           = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_left_lut  = channel_block->volume_left_lut;
    int32_t *volume_right_lut = channel_block->volume_right_lut;
    int32_t *mix_buf          = *buf;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp       >>= 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp       >>= 24;
                *mix_buf++ += volume_left_lut[(uint8_t) smp];
                *mix_buf++ += volume_right_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_data   >>= 24;
                *mix_buf++  += volume_left_lut[(uint8_t) smp_data];
                *mix_buf++  += volume_right_lut[(uint8_t) smp_data];
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_data   >>= 24;
                *mix_buf++  += volume_left_lut[(uint8_t) smp_data];
                *mix_buf++  += volume_right_lut[(uint8_t) smp_data];
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        uint32_t smp_in;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((uint32_t) smp >> 24);
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((uint32_t) smp >> 24);
                *mix_buf++ += volume_left_lut[(uint8_t) smp_in];
                *mix_buf++ += volume_right_lut[(uint8_t) smp_in];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            int32_t smp_right;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp_in    = (uint32_t) smp_data >> 24;
            smp       = volume_left_lut[(uint8_t) smp_in];
            smp_right = volume_right_lut[(uint8_t) smp_in];
            i         = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in    = (uint32_t) smp_data >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp_right;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp_in    = (uint32_t) smp_data >> 24;
                    smp       = volume_left_lut[(uint8_t) smp_in];
                    smp_right = volume_right_lut[(uint8_t) smp_in];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_8_left)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint8_t) smp];
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint8_t) smp];
                mix_buf    += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += volume_lut[(uint8_t) sample[curr_offset]];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf    += volume_lut[(uint8_t) sample[curr_offset]];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += volume_lut[(uint8_t) smp];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += volume_lut[(uint8_t) smp];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint8_t) *sample++];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_8_left)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset + 1;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint8_t) smp];
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint8_t) smp];
                mix_buf    += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += volume_lut[(uint8_t) sample[curr_offset]];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf    += volume_lut[(uint8_t) sample[curr_offset]];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset + 1;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += volume_lut[(uint8_t) smp];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += volume_lut[(uint8_t) smp];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset + 1;
            smp     = volume_lut[(uint8_t) *--sample];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_left)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp      /= interpolate_div;
                *mix_buf += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf    += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_left)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp      /= interpolate_div;
                *mix_buf += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf    += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_left)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf    += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_left)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp      /= interpolate_div;
                *mix_buf += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf    += ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_left)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp       /= interpolate_div;
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp      /= interpolate_div;
                *mix_buf += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf    += ((int64_t) smp_data * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf    += ((int64_t) smp_data * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);

                        bit &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
            i    = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_left)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp       /= interpolate_div;
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp      /= interpolate_div;
                *mix_buf += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf    += ((int64_t) smp_data * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf    += ((int64_t) smp_data * mult_left_volume) / div_volume;
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += ((int64_t) smp * mult_left_volume) / div_volume;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_to_8_left)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint16_t) smp >> 8];
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp      /= interpolate_div;
                *mix_buf += volume_lut[(uint16_t) smp >> 8];
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf    += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += volume_lut[(uint16_t) smp >> 8];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += volume_lut[(uint16_t) smp >> 8];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *sample++ >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_to_8_left)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint16_t) smp >> 8];
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp      /= interpolate_div;
                *mix_buf += volume_lut[(uint16_t) smp >> 8];
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf    += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += volume_lut[(uint16_t) smp >> 8];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += volume_lut[(uint16_t) smp >> 8];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *--sample >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_to_8_left)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint32_t) smp >> 24];
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp      /= interpolate_div;
                *mix_buf += volume_lut[(uint32_t) smp >> 24];
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                *mix_buf    += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *sample++ >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_to_8_left)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint32_t) smp >> 24];
                mix_buf    += 2;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp      /= interpolate_div;
                *mix_buf += volume_lut[(uint32_t) smp >> 24];
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                *mix_buf    += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                *mix_buf    += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *--sample >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_to_8_left)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp       /= interpolate_div;
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                *mix_buf   += volume_lut[(uint32_t) smp >> 24];
                mix_buf    += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf    += volume_lut[(uint32_t) smp_data >> 24];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf    += volume_lut[(uint32_t) smp_data >> 24];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);

                        bit &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp = volume_lut[(uint32_t) smp_data >> 24];
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32)
                    {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_to_8_left)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp      /= interpolate_div;
                *mix_buf += volume_lut[(uint32_t) smp >> 24];
                mix_buf  += 2;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp      /= interpolate_div;
                *mix_buf += volume_lut[(uint32_t) smp >> 24];
                mix_buf  += 2;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf    += volume_lut[(uint32_t) smp_data >> 24];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                *mix_buf    += volume_lut[(uint32_t) smp_data >> 24];
                mix_buf     += 2;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                *mix_buf  += volume_lut[(uint32_t) smp >> 24];
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = volume_lut[(uint32_t) smp_data >> 24];
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                *mix_buf  += smp;
                mix_buf   += 2;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_8_right)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_right_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += volume_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += volume_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += volume_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += volume_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint8_t) *sample++];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_8_right)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_right_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset + 1;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint8_t) smp];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint8_t) smp];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += volume_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += volume_lut[(uint8_t) sample[curr_offset]];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += (curr_offset + 1);
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += volume_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += volume_lut[(uint8_t) smp];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset + 1;
            smp     = volume_lut[(uint8_t) *--sample];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_right)
{
    int16_t *sample           = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf          = *buf;
    int32_t mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_right_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_right_volume) / div_volume;
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_right)
{
    int16_t *sample           = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf          = *buf;
    int32_t mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_right_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_right_volume) / div_volume;
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_right)
{
    int32_t *sample           = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf          = *buf;
    int32_t mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_right_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_right_volume) / div_volume;
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_right)
{
    int32_t *sample           = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf          = *buf;
    int32_t mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += ((int64_t) sample[curr_offset] * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_right_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_right_volume) / div_volume;
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_right)
{
    int32_t *sample           = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf          = *buf;
    int32_t mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                mix_buf++;
                *mix_buf++  += ((int64_t) smp_data * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                mix_buf++;
                *mix_buf++  += ((int64_t) smp_data * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp = ((int64_t) smp_data * mult_right_volume) / div_volume;
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_right_volume) / div_volume;
                }
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_right_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_right)
{
    int32_t *sample           = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf          = *buf;
    int32_t mult_right_volume = channel_block->mult_right_volume, div_volume = channel_block->div_volume;
    uint32_t curr_offset      = *offset;
    uint32_t curr_frac        = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                mix_buf++;
                *mix_buf++  += ((int64_t) smp_data * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                mix_buf++;
                *mix_buf++  += ((int64_t) smp_data * mult_right_volume) / div_volume;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += ((int64_t) smp * mult_right_volume) / div_volume;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = ((int64_t) smp_data * mult_right_volume) / div_volume;
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_right_volume) / div_volume;
                }
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_right_volume) / div_volume;
                }

            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_to_8_right)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_right_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *sample++ >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_to_8_right)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_right_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += volume_lut[(uint16_t) sample[curr_offset] >> 8];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += volume_lut[(uint16_t) smp >> 8];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *--sample >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];

            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_to_8_right)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_right_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *sample++ >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_to_8_right)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_right_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++  += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                mix_buf++;
                *mix_buf++  += volume_lut[(uint32_t) sample[curr_offset] >> 24];
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *--sample >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_to_8_right)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_right_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32)
                    {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                mix_buf++;
                *mix_buf++  += volume_lut[(uint32_t) smp_data >> 24];
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                mix_buf++;
                *mix_buf++  += volume_lut[(uint32_t) smp_data >> 24];
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp  = volume_lut[(uint32_t) smp_data >> 24];
            i    = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_to_8_right)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_right_lut;
    int32_t *mix_buf     = *buf;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                mix_buf++;
                *mix_buf++  += volume_lut[(uint32_t) smp_data >> 24];
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                mix_buf++;
                *mix_buf++  += volume_lut[(uint32_t) smp_data >> 24];
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                mix_buf++;
                *mix_buf++ += volume_lut[(uint32_t) smp >> 24];
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = volume_lut[(uint32_t) smp_data >> 24];
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                mix_buf++;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_8_center)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = volume_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint8_t) *sample++];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_8_center)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset + 1;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = volume_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset + 1;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset + 1;
            smp     = volume_lut[(uint8_t) *--sample];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_center)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_center)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_center)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;

                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_center)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_center)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len)
                    {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
            i    = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_center)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

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
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32){
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_to_8_center)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint16_t) sample[curr_offset] >> 8];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = volume_lut[(uint16_t) sample[curr_offset] >> 8];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *sample++ >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_to_8_center)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint16_t) sample[curr_offset] >> 8];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = volume_lut[(uint16_t) sample[curr_offset] >> 8];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *--sample >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_to_8_center)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint32_t) sample[curr_offset] >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = volume_lut[(uint32_t) sample[curr_offset] >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *sample++ >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];

            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_to_8_center)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint32_t) sample[curr_offset] >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = volume_lut[(uint32_t) sample[curr_offset] >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *--sample >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_to_8_center)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = volume_lut[(uint32_t) smp_data >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = volume_lut[(uint32_t) smp_data >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp  = volume_lut[(uint32_t) smp_data >> 24];
            i    = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_to_8_center)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = volume_lut[(uint32_t) smp_data >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = volume_lut[(uint32_t) smp_data >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = volume_lut[(uint32_t) smp_data >> 24];
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_8_surround)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = volume_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint8_t) *sample++];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *sample++];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_8_surround)
{
    int8_t *sample       = (int8_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int8_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset + 1;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = volume_lut[(uint8_t) sample[curr_offset]];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int8_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset + 1;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint8_t) smp];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset + 1;
            smp     = volume_lut[(uint8_t) *--sample];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint8_t) *--sample];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_surround)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else
    {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_surround)
{
    int16_t *sample          = (int16_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_surround)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *sample++ * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_surround)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = ((int64_t) sample[curr_offset] * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = ((int64_t) *--sample * mult_left_volume) / div_volume;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = ((int64_t) *--sample * mult_left_volume) / div_volume;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_surround)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit       += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
            i    = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_surround)
{
    int32_t *sample          = (int32_t *) channel_block->sample_start_ptr;
    int32_t *mix_buf         = *buf;
    int32_t mult_left_volume = channel_block->mult_left_volume, div_volume = channel_block->div_volume;
    int32_t smp_in;
    uint32_t curr_offset     = *offset;
    uint32_t curr_frac       = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = ((int64_t) smp_data * mult_left_volume) / div_volume;
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = ((int64_t) smp * mult_left_volume) / div_volume;
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = ((int64_t) smp_data * mult_left_volume) / div_volume;
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_16_to_8_surround)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint16_t) sample[curr_offset] >> 8];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = volume_lut[(uint16_t) sample[curr_offset] >> 8];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *sample++ >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *sample++ >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_16_to_8_surround)
{
    int16_t *sample      = (int16_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int16_t *pos = sample;
            int32_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint16_t) sample[curr_offset] >> 8];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = volume_lut[(uint16_t) sample[curr_offset] >> 8];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int16_t *pos = sample;
        int32_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint16_t) smp >> 8];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint16_t) *--sample >> 8];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint16_t) *--sample >> 8];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_32_to_8_surround)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *sample++;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint32_t) sample[curr_offset] >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
get_second_advance_sample:
                smp_in       = volume_lut[(uint32_t) sample[curr_offset] >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;

                if (curr_frac < adv_frac)
                    curr_offset++;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *sample++;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *sample++;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *sample++ >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *sample++ >> 24];

            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_32_to_8_surround)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i;

    if (advance) {
        if (mixer_data->interpolation) {
            int32_t *pos = sample;
            int64_t smp;
            int32_t interpolate_div;

            sample += curr_offset;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                curr_offset = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    curr_offset++;

                smp             = 0;
                interpolate_div = 0;

                do
                {
                    smp += *--sample;
                    interpolate_div++;
                } while (--curr_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            i = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                smp_in       = volume_lut[(uint32_t) sample[curr_offset] >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
get_second_advance_sample:
                smp_in       = volume_lut[(uint32_t) sample[curr_offset] >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;

                if (curr_frac < adv_frac)
                    curr_offset--;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int32_t *pos = sample;
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            sample   += curr_offset;
            smp       = *--sample;
            smp_value = 0;

            if (len)
                smp_value = (*sample - smp) * (int64_t) adv_frac;

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    smp       = *--sample;
                    smp_value = 0;

                    if (len)
                        smp_value = (*sample - smp) * (int64_t) adv_frac;

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        } else {
            sample += curr_offset;
            smp     = volume_lut[(uint32_t) *--sample >> 24];
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp = volume_lut[(uint32_t) *--sample >> 24];
            } while (--i);

            *buf      = mix_buf;
            *offset   = (sample - pos) - 1;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_x_to_8_surround)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    += smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = volume_lut[(uint32_t) smp_data >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = volume_lut[(uint32_t) smp_data >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset += advance;
                bit         += real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset++;
                    bit += bits_per_sample;
                }

                smp_offset += bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit      += bits_per_sample;
            curr_offset++;
            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit      += bits_per_sample;
                    curr_offset++;
                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample++ << bit;
                smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                bit      &= 31;
            }

            bit += bits_per_sample;
            curr_offset++;
            smp  = volume_lut[(uint32_t) smp_data >> 24];
            i    = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample++ << bit;
                        smp_data |= ((uint32_t) *sample & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        bit      &= 31;
                    }

                    bit += bits_per_sample;
                    curr_offset++;
                    smp  = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
}

MIX(stereo_backwards_x_to_8_surround)
{
    int32_t *sample      = (int32_t *) channel_block->sample_start_ptr;
    int32_t *volume_lut  = channel_block->volume_left_lut;
    int32_t *mix_buf     = *buf;
    int32_t smp_in;
    uint32_t curr_offset = *offset;
    uint32_t curr_frac   = *fraction;
    uint32_t i, bit, smp_data, bits_per_sample = channel_block->bits_per_sample;

    if (advance) {
        if (mixer_data->interpolation) {
            int64_t smp;
            int32_t interpolate_div, smp_offset;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            i       = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_interpolated_sample;

            i--;

            do {
                smp_offset = advance;
                curr_frac += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
get_second_advance_interpolated_sample:
                smp_offset  = advance;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac)
                    smp_offset++;

                curr_offset    -= smp_offset;
                smp             = 0;
                interpolate_div = 0;

                do {
                    bit -= bits_per_sample;

                    if ((int32_t) bit < 0) {
                        sample--;
                        bit &= 31;
                    }

                    if ((bit + bits_per_sample) < 32) {
                        smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                    } else {
                        smp_data  = (uint32_t) *sample << bit;
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp += smp_data;
                    interpolate_div++;
                } while (--smp_offset);

                smp        /= interpolate_div;
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            uint32_t real_advance = advance * bits_per_sample, smp_offset;

            bit        = curr_offset * bits_per_sample;
            smp_offset = bit >> 5;
            bit       &= 31;
            i          = (len >> 1) + 1;

            if (len & 1)
                goto get_second_advance_sample;

            i--;

            do {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = volume_lut[(uint32_t) smp_data >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
get_second_advance_sample:
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) sample[smp_offset] << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) sample[smp_offset] << bit;
                    smp_data |= ((uint32_t) sample[smp_offset+1] & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_in       = volume_lut[(uint32_t) smp_data >> 24];
                *mix_buf++  += smp_in;
                *mix_buf++  += ~smp_in;
                curr_frac   += adv_frac;
                curr_offset -= advance;
                bit         -= real_advance;

                if (curr_frac < adv_frac) {
                    curr_offset--;
                    bit -= bits_per_sample;
                }

                smp_offset += (int32_t) bit >> 5;
                bit        &= 31;
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    } else {
        int64_t smp;

        if (mixer_data->interpolation > 1) {
            uint32_t interpolate_frac, interpolate_count;
            int32_t interpolate_div;
            int64_t smp_value;

            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp       = (int32_t) smp_data;
            smp_value = 0;

            if (len) {
                if ((bit + bits_per_sample) < 32) {
                    smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                } else {
                    smp_data  = (uint32_t) *sample << bit;
                    smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                }

                smp_value = (smp_data - smp) * (int64_t) adv_frac;
            }

            interpolate_div   = smp_value >> 32;
            interpolate_frac  = smp_value;
            interpolate_count = 0;
            i                 = (len >> 1) + 1;

            if (len & 1)
                goto get_second_interpolated_sample;

            i--;

            do {
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
get_second_interpolated_sample:
                smp_in      = volume_lut[(uint32_t) smp >> 24];
                *mix_buf++ += smp_in;
                *mix_buf++ += ~smp_in;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp       = (int32_t) smp_data;
                    smp_value = 0;

                    if (len) {
                        if ((bit + bits_per_sample) < 32) {
                            smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
                        } else {
                            smp_data  = (uint32_t) *sample << bit;
                            smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                        }

                        smp_value = (smp_data - smp) * (int64_t) adv_frac;
                    }

                    interpolate_div   = smp_value >> 32;
                    interpolate_frac  = smp_value;
                    interpolate_count = 0;
                } else {
                    smp               += interpolate_div;
                    interpolate_count += interpolate_frac;

                    if (interpolate_count < interpolate_frac) {
                        smp++;

                        if (interpolate_div < 0)
                            smp -= 2;
                    }
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        } else {
            bit     = curr_offset * bits_per_sample;
            sample += bit >> 5;
            bit    &= 31;
            curr_offset--;
            bit    -= bits_per_sample;

            if ((int32_t) bit < 0) {
                sample--;
                bit &= 31;
            }

            if ((bit + bits_per_sample) < 32) {
                smp_data = ((uint32_t) *sample << bit) & ~((1 << (32 - bits_per_sample)) - 1);
            } else {
                smp_data  = (uint32_t) *sample << bit;
                smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
            }

            smp = volume_lut[(uint32_t) smp_data >> 24];
            i   = (len >> 1) + 1;

            if (len & 1)
                goto get_second_sample;

            i--;

            do {
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
get_second_sample:
                *mix_buf++ += smp;
                *mix_buf++ += ~(int32_t) smp;
                curr_frac  += adv_frac;

                if (curr_frac < adv_frac) {
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
                        smp_data |= ((uint32_t) *(sample + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
                    }

                    smp = volume_lut[(uint32_t) smp_data >> 24];
                }
            } while (--i);

            *buf      = mix_buf;
            *offset   = curr_offset;
            *fraction = curr_frac;
        }
    }
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
    .set_channel_volume_panning_pitch  = set_channel_volume_panning_pitch,
    .set_channel_position_repeat_flags = set_channel_position_repeat_flags,
    .mix                               = mix,
};

#endif /* CONFIG_LOW_QUALITY_MIXER */
