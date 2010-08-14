/*
 * Implement AVSequencer module stuff
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
 * Implement AVSequencer module stuff.
 */

#include "libavutil/log.h"
#include "libavsequencer/avsequencer.h"
#include "libavsequencer/player.h"

static const char *module_name(void *p)
{
    AVSequencerModule *module = p;
    AVMetadataTag *tag        = av_metadata_get(module->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Module";
}

static const AVClass avseq_module_class = {
    "AVSequencer Module",
    module_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerModule *avseq_module_create(void) {
    return av_mallocz(sizeof(AVSequencerModule) + FF_INPUT_BUFFER_PADDING_SIZE);
}

int avseq_module_open(AVSequencerContext *avctx, AVSequencerModule *module) {
    AVSequencerModule **module_list;
    uint16_t modules;

    if (!avctx)
        return AVERROR_INVALIDDATA;

    module_list = avctx->module_list;
    modules     = avctx->modules;

    if (!(module && ++modules)) {
        return AVERROR_INVALIDDATA;
    } else if (!(module_list = av_realloc(module_list, (modules * sizeof(AVSequencerModule *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate module storage container.\n");
        return AVERROR(ENOMEM);
    }

    module->av_class = &avseq_module_class;

    if (!module->channels)
        module->channels = 64;

    module_list[modules - 1] = module;
    avctx->module_list       = module_list;
    avctx->modules           = modules;

    return 0;
}

int avseq_module_play(AVSequencerContext *avctx, AVSequencerMixerContext *mixctx,
                      AVSequencerModule *module, AVSequencerSong *song,
                      const char *args, void *opaque, uint32_t mode) {
    AVSequencerPlayerGlobals *player_globals;
    AVSequencerPlayerHostChannel *player_host_channel;
    AVSequencerPlayerChannel *player_channel;
    AVSequencerMixerData *mixer_data;
    uint64_t volume_boost;
    uint32_t tempo;

    if (!(avctx && module && song))
        return AVERROR_INVALIDDATA;

    player_globals      = avctx->player_globals;
    player_host_channel = avctx->player_host_channel;
    player_channel      = avctx->player_channel;
    mixer_data          = avctx->player_mixer_data;

    if (!(player_globals = av_realloc(player_globals, sizeof (AVSequencerPlayerGlobals) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate player globals storage container.\n");
        return AVERROR(ENOMEM);
    } else if (!(player_host_channel = av_realloc(player_host_channel, (song->channels * sizeof (AVSequencerPlayerHostChannel)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_free(player_globals);
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate player host channel data.\n");
        return AVERROR(ENOMEM);
    } else if (!(player_channel = av_realloc(player_channel, (module->channels * sizeof (AVSequencerPlayerChannel)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_free(player_host_channel);
        av_free(player_globals);
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate player host channel data.\n");
        return AVERROR(ENOMEM);
    } else if (!(mixer_data = avseq_mixer_init ( avctx, mixctx, args, opaque ))) {
        av_free(player_channel);
        av_free(player_host_channel);
        av_free(player_globals);
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate mixer data.\n");
    }

    memset ( player_globals, 0, sizeof(AVSequencerPlayerGlobals) );
    memset ( player_host_channel, 0, song->channels * sizeof(AVSequencerPlayerHostChannel) );
    memset ( player_channel, 0, module->channels * sizeof(AVSequencerPlayerChannel) );

    avctx->player_globals      = player_globals;
    avctx->player_host_channel = player_host_channel;
    avctx->player_channel      = player_channel;
    avctx->player_module       = module;
    avctx->player_song         = song;
    avctx->player_mixer_data   = mixer_data;

    player_globals->flags &= ~(AVSEQ_PLAYER_GLOBALS_FLAG_NO_PROC_PATTERN|AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_PATTERN);

    if (mode)
        player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE;
    else
        player_globals->flags |= AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE;

    player_globals->play_type      = AVSEQ_PLAYER_GLOBALS_PLAY_TYPE_SONG;
    player_globals->relative_speed = 0x10000;
    player_globals->relative_pitch = 0x10000;
    tempo                          = avseq_song_calc_speed ( avctx, song );
    volume_boost                   = ((module->channels * 65536*125/1000) + (65536*75/1000)) >> 16;
    mixer_data->flags             |= AVSEQ_MIXER_DATA_FLAG_MIXING;

    avseq_mixer_set_rate ( mixer_data, mixctx->frequency );
    avseq_mixer_set_tempo ( mixer_data, tempo );
    avseq_mixer_set_volume ( mixer_data, volume_boost, 256, 256, module->channels );

    return 0;
}
int avseq_module_set_channels ( AVSequencerModule *module, uint32_t channels ) {
    if (!module)
        return AVERROR_INVALIDDATA;

    if (!channels)
        channels = 64;

    if (channels > 65535)
        channels = 65535;

    module->channels = channels;

    return 0;
}
