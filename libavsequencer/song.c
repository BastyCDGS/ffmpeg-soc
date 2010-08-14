/*
 * Implement AVSequencer sub-song stuff
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
 * Implement AVSequencer sub-song stuff.
 */

#include "libavutil/log.h"
#include "libavsequencer/avsequencer.h"
#include "libavsequencer/player.h"

static const char *song_name(void *p)
{
    AVSequencerSong *song = p;
    AVMetadataTag *tag    = av_metadata_get(song->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Song";
}

static const AVClass avseq_song_class = {
    "AVSequencer Song",
    song_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerSong *avseq_song_create(void) {
    return av_mallocz(sizeof(AVSequencerSong) + FF_INPUT_BUFFER_PADDING_SIZE);
}

int avseq_song_open(AVSequencerModule *module, AVSequencerSong *song) {
    AVSequencerSong **song_list;
    uint16_t songs;
    int res;

    if (!module)
        return AVERROR_INVALIDDATA;

    song_list = module->song_list;
    songs     = module->songs;

    if (!(song && ++songs)) {
        return AVERROR_INVALIDDATA;
    } else if (!(song_list = av_realloc(song_list, (songs * sizeof(AVSequencerSong *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(module, AV_LOG_ERROR, "Cannot allocate sub-song storage container.\n");
        return AVERROR(ENOMEM);
    }

    song->av_class         = &avseq_song_class;
    song->channels         = 16;
    song->gosub_stack_size = 4;
    song->loop_stack_size  = 1;
    song->frames           = 6;
    song->spd_speed        = 33;
    song->bpm_tempo        = 4;
    song->bpm_speed        = 125;
    song->frames_min       = 1;
    song->frames_max       = 65535;
    song->spd_min          = 1;
    song->spd_max          = 65535;
    song->bpm_tempo_min    = 1;
    song->bpm_tempo_max    = 65535;
    song->bpm_speed_min    = 1;
    song->bpm_speed_max    = 65535;
    song->global_volume    = 255;

    if ((res = avseq_order_open(song)) < 0) {
        av_free(song_list);
        return res;
    }

    song_list[songs - 1] = song;
    module->song_list    = song_list;
    module->songs        = songs;

    return 0;
}

int avseq_song_set_channels ( AVSequencerSong *song, uint32_t channels ) {
    if (!song)
        return AVERROR_INVALIDDATA;

    if (!channels)
        channels = 16;

    if (channels > 256)
        channels = 256;

    if (channels != song->channels) {
        AVSequencerOrderList *order_list = song->order_list;

        if ((channels == 0) || (channels > 256)) {
            return AVERROR_INVALIDDATA;
        } else if (!(order_list = av_realloc(order_list, (channels * sizeof(AVSequencerOrderList)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(song, AV_LOG_ERROR, "Cannot allocate order list storage container.\n");
            return AVERROR(ENOMEM);
        }

        if (channels > song->channels) {
            uint16_t channel = channels;

            memset ( order_list + song->channels, 0, (channels - song->channels) * sizeof(AVSequencerOrderList) );

            while (channel-- != song->channels) {
                order_list[channel].av_class        = order_list[0].av_class;
                order_list[channel].volume          = 255;
                order_list[channel].track_panning   = -128;
                order_list[channel].channel_panning = -128;
            }
        }

        song->order_list = order_list;
        song->channels   = channels;
    }

    return 0;
}

/** Old SoundTracker tempo definition table.  */
static const uint32_t old_st_lut[] = {
    192345259,  96192529,  64123930,  48096264,  38475419,
     32061964,  27482767,  24048132,  21687744,  19240098
};

uint32_t avseq_song_calc_speed ( AVSequencerContext *avctx, AVSequencerSong *song ) {
    AVSequencerPlayerGlobals *player_globals;
    uint64_t tempo = 0;
    uint16_t speed;
    uint8_t speed_mul;

    if (!(avctx && song))
        return 0;

    if (!(player_globals = avctx->player_globals))
        return 0;

    if (song->flags & AVSEQ_SONG_FLAG_SPD) {
        player_globals->flags |= AVSEQ_PLAYER_GLOBALS_FLAG_SPD_TIMING;

        if ((speed = song->spd_speed) > 10) {
            tempo = (uint32_t) 989156 * speed;

            if ((speed_mul = song->speed_mul))
                tempo *= speed_mul;

            if ((speed_mul = song->speed_div))
                tempo /= speed_mul;
        } else {
            tempo = avctx->old_st_lut ? avctx->old_st_lut[speed] : old_st_lut[speed];
        }
    } else {
        player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SPD_TIMING;
        tempo                  = (song->bpm_speed) * (song->bpm_tempo) << 16;

        if ((speed_mul = song->speed_mul))
            tempo *= speed_mul;

        if ((speed_mul = song->speed_div))
            tempo /= speed_mul;
    }

    player_globals->speed_mul = song->speed_mul;
    player_globals->speed_div = song->speed_div;
    player_globals->spd_speed = song->spd_speed;
    player_globals->bpm_tempo = song->bpm_tempo;
    player_globals->bpm_speed = song->bpm_speed;
    player_globals->tempo     = (tempo <= 0xFFFFFFFF) ? (uint32_t) tempo : 0;
    tempo                    *= player_globals->relative_speed;
    tempo                   >>= 16;

    return (tempo <= 0xFFFFFFFF) ? (uint32_t) tempo : 0;
}
