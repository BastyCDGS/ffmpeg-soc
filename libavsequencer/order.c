/*
 * Implement AVSequencer order list and data stuff
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
 * Implement AVSequencer order list and data stuff.
 */

#include "libavutil/log.h"
#include "libavsequencer/avsequencer.h"

static const char *order_list_name(void *p)
{
    AVSequencerOrderList *order_list = p;
    AVMetadataTag *tag               = av_metadata_get(order_list->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    return tag->value;
}

static const AVClass avseq_order_list_class = {
    "AVSequencer Order List",
    order_list_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

int avseq_order_open(AVSequencerSong *song) {
    AVSequencerOrderList *order_list;
    uint16_t channels;

    if (!song)
        return AVERROR_INVALIDDATA;

    channels = song->channels;

    if ((channels == 0) || (channels > 256)) {
        return AVERROR_INVALIDDATA;
    } else if (!(order_list = av_mallocz(channels * sizeof(AVSequencerOrderData)))) {
        av_log(song, AV_LOG_ERROR, "cannot allocate order list storage container.\n");
        return AVERROR(ENOMEM);
    }

    song->order_list = order_list;

    do {
        order_list->av_class        = &avseq_order_list_class;
        order_list->volume          = 255;
        order_list->track_panning   = -128;
        order_list->channel_panning = -128;
        order_list++;
    } while (--channels);

    return 0;
}
