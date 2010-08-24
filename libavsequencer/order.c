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
#include "libavformat/avformat.h"
#include "libavsequencer/avsequencer.h"

static const char *order_list_name(void *p)
{
    AVSequencerOrderList *order_list = p;
    AVMetadataTag *tag               = av_metadata_get(order_list->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Order List";
}

static const AVClass avseq_order_list_class = {
    "AVSequencer Order List",
    order_list_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

int avseq_order_open(AVSequencerSong *song)
{
    AVSequencerOrderList *order_list;
    uint16_t channels;

    if (!song)
        return AVERROR_INVALIDDATA;

    order_list = song->order_list;
    channels   = song->channels;

    if ((channels == 0) || (channels > 256)) {
        return AVERROR_INVALIDDATA;
    } else if (!(order_list = av_realloc(order_list, (channels * sizeof(AVSequencerOrderList)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(song, AV_LOG_ERROR, "Cannot allocate order list storage container.\n");
        return AVERROR(ENOMEM);
    }

    memset(order_list, 0, channels * sizeof(AVSequencerOrderList));

    while (channels--) {
        order_list[channels].av_class        = &avseq_order_list_class;
        order_list[channels].volume          = 255;
        order_list[channels].track_panning   = -128;
        order_list[channels].channel_panning = -128;
    }

    song->order_list = order_list;

    return 0;
}

void avseq_order_close(AVSequencerSong *song)
{
    if (song) {
        AVSequencerOrderList *order_list;

        if ((order_list = song->order_list)) {
            int i = song->channels;

            while (i--) {
                int j = order_list->orders;

                while (j--) {
                    AVSequencerOrderData *order_data = order_list->order_data[j];

                    avseq_order_data_close(song, order_list, order_data);
                    avseq_order_data_destroy(order_data);
                }

                av_metadata_free(&order_list->metadata);

                order_list++;
            }
        }
    }
}

static const char *order_data_name(void *p)
{
    AVSequencerOrderData *order_data = p;
    AVMetadataTag *tag               = av_metadata_get(order_data->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Order Data";
}

static const AVClass avseq_order_data_class = {
    "AVSequencer Order Data",
    order_data_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerOrderData *avseq_order_data_create(void) {
    return av_mallocz(sizeof(AVSequencerOrderData) + FF_INPUT_BUFFER_PADDING_SIZE);
}

void avseq_order_data_destroy(AVSequencerOrderData *order_data)
{
    if (order_data)
        av_metadata_free(&order_data->metadata);

    av_free(order_data);
}

int avseq_order_data_open(AVSequencerOrderList *order_list, AVSequencerOrderData *order_data)
{
    AVSequencerOrderData **order_data_list;
    uint16_t orders;

    if (!order_list)
        return AVERROR_INVALIDDATA;

    order_data_list = order_list->order_data;
    orders          = order_list->orders;

    if (!(order_data && ++orders)) {
        return AVERROR_INVALIDDATA;
    } else if (!(order_data_list = av_realloc(order_data_list, (orders * sizeof(AVSequencerOrderData *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(order_list, AV_LOG_ERROR, "Cannot allocate order list data entry storage container.\n");
        return AVERROR(ENOMEM);
    }

    order_data->av_class   = &avseq_order_data_class;
    order_data->volume     = 255;
    order_data->last_row   = 65535;

    order_data_list[orders - 1] = order_data;
    order_list->order_data      = order_data_list;
    order_list->orders          = orders;

    return 0;
}

void avseq_order_data_close(AVSequencerSong *song, AVSequencerOrderList *order_list,
                            AVSequencerOrderData *order_data)
{
    AVSequencerOrderData **order_data_list;
    uint16_t orders, i;

    if (!(order_list && order_data))
        return;

    order_data_list = order_list->order_data;
    orders          = order_list->orders;

    for (i = 0; i < orders; ++i) {
        if (order_data_list[i] == order_data)
            break;
    }

    if (orders && (i != orders)) {
        AVSequencerOrderList *song_order_list;
        AVSequencerOrderData *last_order_data = order_data_list[--orders];

        if ((song_order_list = song->order_list)) {
            uint16_t channel;

            for (channel = 0; channel < song->channels; ++channel) {
                uint16_t order;

                for (order = 0; order < song_order_list->orders; ++order) {
                    AVSequencerOrderData *song_order_data = song_order_list->order_data[order];

                    if (song_order_data->next_pos == order_data) {
                        if (last_order_data != order_data)
                            song_order_data->next_pos = order_data_list[i + 1];
                        else if (i)
                            song_order_data->next_pos = order_data_list[i - 1];
                        else
                            song_order_data->next_pos = NULL;
                    }

                    if (song_order_data->prev_pos == order_data) {
                        if (last_order_data != order_data)
                            song_order_data->prev_pos = order_data_list[i + 1];
                        else if (i)
                            song_order_data->prev_pos = order_data_list[i - 1];
                        else
                            song_order_data->prev_pos = NULL;
                    }
                }

                song_order_list++;
            }
        }

        if (!orders) {
            av_freep(&order_list->order_data);

            order_list->orders = 0;
        } else if (!(order_data_list = av_realloc(order_data_list, (orders * sizeof(AVSequencerOrderData *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            const unsigned copy_orders = i + 1;

            order_data_list = order_list->order_data;

            if (copy_orders < orders)
                memmove(order_data_list + i, order_data_list + copy_orders, (orders - copy_orders) * sizeof(AVSequencerOrderData *));

            order_data_list[orders - 1] = NULL;
        } else {
            const unsigned copy_orders = i + 1;

            if (copy_orders < orders) {
                memmove(order_data_list + i, order_data_list + copy_orders, (orders - copy_orders) * sizeof(AVSequencerOrderData *));

                order_data_list[orders - 1] = last_order_data;
            }

            order_list->order_data = order_data_list;
            order_list->orders     = orders;
        }
    }
}

AVSequencerOrderData *avseq_order_get_address(AVSequencerSong *song, uint32_t channel, uint32_t order)
{
    AVSequencerOrderList *order_list;
    if (!(song && order))
        return NULL;

    order_list = song->order_list;

    if (!order_list || (channel >= song->channels))
        return NULL;

    order_list += channel;

    if (order > order_list->orders)
        return NULL;

    return order_list->order_data[--order];
}
