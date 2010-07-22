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

#include "libavsequencer/song.h"
#include "libavsequencer/order.h"

int avseq_order_open(AVSequencerSong *song) {
    AVSequencerOrderList *order_list;
    uint16_t channels = song->channels;

    if (!song || (channels < 1) || (channels > 256))
        return AVERROR_INVALIDDATA;
    } else if (!(order_list = av_mallocz(channels * sizeof(AVSequencerOrderData *)))) {
        av_log(song, AV_LOG_ERROR, "avseq: cannot allocate storage container.\n");
        return AVERROR(ENOMEM);
    }

    song->order_list = order_list;

    do {
        order_list->volume          = 255;
        order_list->track_panning   = -128;
        order_list->channel_panning = -128;
        order_list++;
    } while (--channels);

    return 0;
}
