/*
 * Implement AVSequencer pattern and track stuff
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
 * Implement AVSequencer pattern and track stuff.
 */

#include "libavutil/log.h"
#include "libavsequencer/avsequencer.h"

static const char *track_name(void *p)
{
    AVSequencerTrack *track = p;
    AVMetadataTag *tag      = av_metadata_get(track->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    return tag->value;
}

static const AVClass avseq_track_class = {
    "AVSequencer Track",
    track_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

int avseq_track_open(AVSequencerSong *song, AVSequencerTrack *track) {
    AVSequencerTrack **track_list;
    uint16_t tracks;
    int res;

    if (!song)
        return AVERROR_INVALIDDATA;

    track_list = song->track_list;
    tracks     = song->tracks;

    if (!(track && ++tracks)) {
        return AVERROR_INVALIDDATA;
    } else if (!(track_list = av_realloc(track_list, tracks * sizeof(AVSequencerTrack *)))) {
        av_log(song, AV_LOG_ERROR, "cannot allocate track storage container.\n");
        return AVERROR(ENOMEM);
    }

    track->av_class  = &avseq_track_class;
    track->last_row  = 63;
    track->volume    = 255;
    track->panning   = -128;
    track->frames    = 6;
    track->spd_speed = 33;
    track->bpm_tempo = 4;
    track->bpm_speed = 125;

    if ((res = avseq_track_data_open(track)) < 0) {
        av_free(track_list);
        return res;
    }

    track_list[tracks] = track;
    song->track_list   = track_list;
    song->tracks       = tracks;

    return 0;
}

int avseq_track_data_open(AVSequencerTrack *track) {
    AVSequencerTrackData *data;
    unsigned last_row;

    if (!track)
        return AVERROR_INVALIDDATA;

    data     = track->data;
    last_row = track->last_row + 1;

    if (!(data = av_realloc(data, last_row * sizeof(AVSequencerTrackData *)))) {
        av_log(track, AV_LOG_ERROR, "cannot allocate storage container.\n");
        return AVERROR(ENOMEM);
    }

    track->data = data;

    return 0;
}
