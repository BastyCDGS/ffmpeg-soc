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

static AVSequencerMixerContext *registered_mixers[AVSEQUENCER_MAX_REGISTERED_MIXERS_NB + 1];
static int next_registered_mixer_idx = 0;

AVSequencerMixerContext *avseq_mixer_get_by_name(const char *name)
{
    int i;

    for (i = 0; i < next_registered_mixer_idx; i++) {
        if (!strcmp(registered_mixers[i]->name, name))
            return registered_mixers[i];
    }

    return NULL;
}

int avseq_mixer_register(AVSequencerMixerContext *mixctx)
{
    if (next_registered_mixer_idx == AVSEQUENCER_MAX_REGISTERED_MIXERS_NB)
        return -1;

    registered_mixers[next_registered_mixer_idx++] = mixctx;
    return 0;
}

AVSequencerMixerContext **avseq_mixer_next(AVSequencerMixerContext **mixctx)
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

AVSequencerContext *avsequencer_open(AVSequencerMixerContext *mixctx,
                                     const char *inst_name)
{
    AVSequencerContext *avctx;
    int i;

    avctx                   = av_mallocz(sizeof(AVSequencerContext));
    avctx->av_class         = &avsequencer_class;
    avctx->playback_handler = avseq_playback_handler;

    if (!(avctx->mixer_list = av_malloc(next_registered_mixer_idx * sizeof(AVSequencerMixerContext *))))
        return NULL;

    for (i = 0; i < next_registered_mixer_idx; ++i) {
        avctx->mixer_list[i] = registered_mixers[i];
    }

    avsequencer_register_all ();

    avctx->mixers            = next_registered_mixer_idx;
    avctx->seed              = av_get_random_seed ();

    if (mixctx)
        avctx->player_mixer_data = avseq_mixer_init(avctx, mixctx, NULL, NULL);

    return avctx;
}

void avsequencer_destroy(AVSequencerContext *avctx)
{
    int i;

    for (i = 0; i < avctx->modules; ++i) {
    // TODO: actual module list destroy
//        avseq_module_destroy(avctx->module_list[i]);
    }

    av_freep(&avctx->module_list);

    for (i = 0; i < avctx->mixers; ++i) {
        avseq_mixer_uninit(avctx, avctx->mixer_list[i]);
    }

    av_freep(&avctx->mixer_list);
    av_free(avctx);
}

AVSequencerMixerData *avseq_mixer_init(AVSequencerContext *avctx, AVSequencerMixerContext *mixctx,
                                       const char *args, void *opaque)
{
    AVSequencerMixerData *mixer_data = NULL;

    if (avctx && mixctx && mixctx->init) {
        mixer_data = mixctx->init(mixctx, args, opaque);

        if (mixer_data) {
            mixer_data->opaque       = (void *) avctx;
            mixer_data->handler      = avctx->playback_handler;
        }
    }

    return mixer_data;
}

int avseq_mixer_uninit(AVSequencerContext *avctx, AVSequencerMixerData *mixer_data)
{
    AVSequencerMixerContext *mixctx;

    if (!(avctx && mixer_data))
        return AVERROR_INVALIDDATA;

    mixctx = mixer_data->mixctx;

    if (mixctx && mixctx->uninit) {
        if (avctx->player_mixer_data == mixer_data)
            avctx->player_mixer_data = NULL;

        return mixctx->uninit(mixer_data);
    }

    return 0;
}

uint32_t avseq_mixer_set_rate(AVSequencerMixerData *mixer_data, uint32_t new_mix_rate)
{
    AVSequencerMixerContext *mixctx;

    if (!mixer_data)
        return 0;

    mixctx = mixer_data->mixctx;

    if (new_mix_rate && mixctx && mixctx->set_rate)
        return mixctx->set_rate(mixer_data, new_mix_rate);

    return mixer_data->rate;
}

uint32_t avseq_mixer_set_tempo(AVSequencerMixerData *mixer_data, uint32_t new_tempo
{
    AVSequencerMixerContext *mixctx;

    if (!mixer_data)
        return 0;

    mixctx = mixer_data->mixctx;

    if (new_tempo && mixctx && mixctx->set_tempo)
        return mixctx->set_tempo(mixer_data, new_tempo);

    return mixer_data->tempo;
}

uint32_t avseq_mixer_set_volume(AVSequencerMixerData *mixer_data, uint32_t amplify,
                                uint32_t left_volume, uint32_t right_volume,
                                uint32_t channels)
{
    AVSequencerMixerContext *mixctx;

    if (!mixer_data)
        return 0;

    mixctx = mixer_data->mixctx;

    if (channels && mixctx && mixctx->set_volume)
        return mixctx->set_volume(mixer_data, amplify, left_volume, right_volume, channels);

    return mixer_data->tempo;
}

void avseq_mixer_do_mix(AVSequencerMixerData *mixer_data, int32_t *buf)
{
    AVSequencerMixerContext *mixctx;

    if (!mixer_data)
        return;

    mixctx = mixer_data->mixctx;

    if (mixctx) {
        if (buf)
            mixctx->mix(mixer_data, buf);
        else if (mixer_data->mix_buf)
            mixctx->mix(mixer_data, mixer_data->mix_buf);
    }
}
