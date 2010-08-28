/*
 * Implement AVSequencer functions
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

#include "config.h"
#include "avsequencer.h"
#include "libavutil/random_seed.h"

/**
 * @file
 * Implement AVSequencer functions.
 */

unsigned avsequencer_version(void)
{
    return LIBAVSEQUENCER_VERSION_INT;
}

const char *avsequencer_configuration(void)
{
    return FFMPEG_CONFIGURATION;
}

const char *avsequencer_license(void)
{
#define LICENSE_PREFIX "libavsequencer license: "
    return LICENSE_PREFIX FFMPEG_LICENSE + sizeof(LICENSE_PREFIX) - 1;
}

#define AVSEQUENCER_MAX_REGISTERED_MIXERS_NB 64

static AVMixerContext *registered_mixers[AVSEQUENCER_MAX_REGISTERED_MIXERS_NB + 1];
static int next_registered_mixer_idx = 0;

AVMixerContext *avseq_mixer_get_by_name(const char *name)
{
    int i;

    for (i = 0; i < next_registered_mixer_idx; i++) {
        if (!strcmp(registered_mixers[i]->name, name))
            return registered_mixers[i];
    }

    return NULL;
}

int avseq_mixer_register(AVMixerContext *mixctx)
{
    if (next_registered_mixer_idx == AVSEQUENCER_MAX_REGISTERED_MIXERS_NB)
        return -1;

    registered_mixers[next_registered_mixer_idx++] = mixctx;
    return 0;
}

AVMixerContext **avseq_mixer_next(AVMixerContext **mixctx)
{
    return mixctx ? ++mixctx : &registered_mixers[0];
}

void avsequencer_uninit(void)
{
    memset(registered_mixers, 0, sizeof(registered_mixers));
    next_registered_mixer_idx = 0;
}

static const char *mixer_name(void *p)
{
    AVSequencerContext *avctx = p;

    if (avctx->player_mixer_data && avctx->player_mixer_data->mixctx)
        return avctx->player_mixer_data->mixctx->name;
    else
        return "AVSequencer";
}

static const AVClass avsequencer_class = {
    "AVSequencer",
    mixer_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerContext *avsequencer_open(AVMixerContext *mixctx,
                                     const char *args, void *opaque)
{
    AVSequencerContext *avctx;

    if (!(avctx = av_mallocz(sizeof(AVSequencerContext) + FF_INPUT_BUFFER_PADDING_SIZE)))
        return NULL;

    avctx->av_class         = &avsequencer_class;
    avctx->playback_handler = avseq_playback_handler;
    avctx->seed             = av_get_random_seed();

    avsequencer_register_all();

    if (mixctx)
        avctx->player_mixer_data = avseq_mixer_init(avctx, mixctx, args, opaque);

    return avctx;
}

void avsequencer_destroy(AVSequencerContext *avctx)
{
    int i;

    if (avctx) {
        avseq_module_stop(avctx, 1);

        i = avctx->mixers;

        while (i--) {
            AVMixerData *mixer_data = avctx->mixer_data_list[i];

            avseq_mixer_uninit(avctx, mixer_data);
        }

        i = avctx->modules;

        while (i--) {
            AVSequencerModule *module = avctx->module_list[i];

            avseq_module_close(avctx, module);
            avseq_module_destroy(module);
        }

        av_free(avctx);
    }
}

AVMixerData *avseq_mixer_init(AVSequencerContext *avctx, AVMixerContext *mixctx,
                              const char *args, void *opaque)
{
    AVMixerData *mixer_data = NULL;

    if (avctx && mixctx && mixctx->init) {
        mixer_data = mixctx->init(mixctx, args, opaque);

        if (mixer_data) {
            AVMixerData **mixer_data_list = avctx->mixer_data_list;
            uint16_t mixers               = avctx->mixers;

            mixer_data->opaque       = (void *) avctx;
            mixer_data->handler      = avctx->playback_handler;

            if (!++mixers) {
                avseq_mixer_uninit(avctx, mixer_data);
                av_log(avctx, AV_LOG_ERROR, "Too many mixer data instances.\n");
                return NULL;
            } else if (!(mixer_data_list = av_realloc(mixer_data_list, (mixers * sizeof(AVMixerData *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
                avseq_mixer_uninit(avctx, mixer_data);
                av_log(avctx, AV_LOG_ERROR, "Cannot allocate mixer data storage container.\n");
                return NULL;
            }

            mixer_data_list[mixers - 1] = mixer_data;
            avctx->mixer_data_list      = mixer_data_list;
            avctx->mixers               = mixers;
        }
    }

    return mixer_data;
}

int avseq_mixer_uninit(AVSequencerContext *avctx, AVMixerData *mixer_data)
{
    AVMixerContext *mixctx;

    if (!(avctx && mixer_data))
        return AVERROR_INVALIDDATA;

    mixctx = mixer_data->mixctx;

    if (mixctx && mixctx->uninit) {
        AVMixerData **mixer_data_list = avctx->mixer_data_list;
        uint16_t mixers               = avctx->mixers, i;

        if (avctx->player_mixer_data == mixer_data) {
            avctx->player_mixer_data = NULL;

            avseq_module_stop(avctx, 0);
        }

        for (i = 0; i < mixers; ++i) {
            if (mixer_data_list[i] == mixer_data)
                break;
        }

        if (mixers && (i != mixers)) {
            AVMixerData *last_mixer_data = mixer_data_list[--mixers];

            if (!mixers) {
                av_freep(&avctx->mixer_data_list);
                avctx->mixers = 0;
            } else if (!(mixer_data_list = av_realloc(mixer_data_list, (mixers * sizeof(AVMixerData *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
                const unsigned copy_mixers = i + 1;

                mixer_data_list = avctx->mixer_data_list;

                if (copy_mixers < mixers)
                    memmove(mixer_data_list + i, mixer_data_list + copy_mixers, (mixers - copy_mixers) * sizeof(AVMixerData *));

                mixer_data_list[mixers - 1] = NULL;
            } else {
                const unsigned copy_mixers = i + 1;

                if (copy_mixers < mixers) {
                    memmove(mixer_data_list + i, mixer_data_list + copy_mixers, (mixers - copy_mixers) * sizeof(AVMixerData *));

                    mixer_data_list[mixers - 1] = last_mixer_data;
                }

                avctx->mixer_data_list = mixer_data_list;
                avctx->mixers          = mixers;
            }
        }

        return mixctx->uninit(mixer_data);
    }

    return 0;
}

uint32_t avseq_mixer_set_rate(AVMixerData *mixer_data, uint32_t new_mix_rate)
{
    AVMixerContext *mixctx;

    if (!mixer_data)
        return 0;

    mixctx = mixer_data->mixctx;

    if (new_mix_rate && mixctx && mixctx->set_rate)
        return mixctx->set_rate(mixer_data, new_mix_rate);

    return mixer_data->rate;
}

uint32_t avseq_mixer_set_tempo(AVMixerData *mixer_data, uint32_t new_tempo)
{
    AVMixerContext *mixctx;

    if (!mixer_data)
        return 0;

    mixctx = mixer_data->mixctx;

    if (new_tempo && mixctx && mixctx->set_tempo)
        return mixctx->set_tempo(mixer_data, new_tempo);

    return mixer_data->tempo;
}

uint32_t avseq_mixer_set_volume(AVMixerData *mixer_data, uint32_t amplify,
                                uint32_t left_volume, uint32_t right_volume,
                                uint32_t channels)
{
    AVMixerContext *mixctx;

    if (!mixer_data)
        return 0;

    mixctx = mixer_data->mixctx;

    if (channels && mixctx && mixctx->set_volume)
        return mixctx->set_volume(mixer_data, amplify, left_volume, right_volume, channels);

    return mixer_data->tempo;
}

void avseq_mixer_get_channel(AVMixerData *mixer_data,
                             AVMixerChannel *mixer_channel, uint32_t channel)
{
    AVMixerContext *mixctx;

    if (!(mixer_data && mixer_channel))
        return;

    mixctx = mixer_data->mixctx;

    if (mixctx && mixctx->get_channel && (channel < mixer_data->channels_max))
        mixctx->get_channel(mixer_data, mixer_channel, channel);
}

void avseq_mixer_set_channel(AVMixerData *mixer_data,
                             AVMixerChannel *mixer_channel, uint32_t channel)
{
    AVMixerContext *mixctx;

    if (!(mixer_data && mixer_channel))
        return;

    mixctx = mixer_data->mixctx;

    if (mixctx && mixctx->set_channel && (channel < mixer_data->channels_max))
        mixctx->set_channel(mixer_data, mixer_channel, channel);
}

void avseq_mixer_do_mix(AVMixerData *mixer_data, int32_t *buf)
{
    AVMixerContext *mixctx;

    if (!mixer_data)
        return;

    mixctx = mixer_data->mixctx;

    if (mixctx && mixctx->mix) {
        if (buf)
            mixctx->mix(mixer_data, buf);
        else if (mixer_data->mix_buf)
            mixctx->mix(mixer_data, mixer_data->mix_buf);
    }
}
