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

static const char *song_name(void *p)
{
    AVSequencerSong *song = p;
    AVMetadataTag *tag    = av_metadata_get(song->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    return tag->value;
}

static const AVClass avseq_song_class = {
    "AVSequencer Song",
    song_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

int avseq_song_open(AVSequencerModule *module, AVSequencerSong *song) {
    AVSequencerSong **song_list = module->song_list;
    uint16_t songs              = module->songs;
    int res;

    if (!song || !++songs) {
        return AVERROR_INVALIDDATA;
    } else if (!(song_list = av_realloc(song_list, songs * sizeof(AVSequencerSong *)))) {
        av_log(module, AV_LOG_ERROR, "cannot allocate sub-song storage container.\n");
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

    song_list[songs]       = song;
    module->song_list      = song_list;
    module->songs          = songs;

    return 0;
}
