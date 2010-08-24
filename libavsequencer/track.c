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
#include "libavformat/avformat.h"
#include "libavsequencer/avsequencer.h"

static const char *track_name(void *p)
{
    AVSequencerTrack *track = p;
    AVMetadataTag *tag      = av_metadata_get(track->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Track";
}

static const AVClass avseq_track_class = {
    "AVSequencer Track",
    track_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerTrack *avseq_track_create(void)
{
    return av_mallocz(sizeof(AVSequencerTrack) + FF_INPUT_BUFFER_PADDING_SIZE);
}

void avseq_track_destroy(AVSequencerTrack *track)
{
    if (track)
        av_metadata_free(&track->metadata);

    av_free(track);
}

int avseq_track_open(AVSequencerSong *song, AVSequencerTrack *track)
{
    AVSequencerTrack **track_list;
    uint16_t tracks;
    int res;

    if (!song)
        return AVERROR_INVALIDDATA;

    track_list = song->track_list;
    tracks     = song->tracks;

    if (!(track && ++tracks)) {
        return AVERROR_INVALIDDATA;
    } else if (!(track_list = av_realloc(track_list, (tracks * sizeof(AVSequencerTrack *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(song, AV_LOG_ERROR, "Cannot allocate track storage container.\n");
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

    if ((res = avseq_track_data_open(track, track->last_row + 1)) < 0) {
        av_free(track_list);
        return res;
    }

    track_list[tracks - 1] = track;
    song->track_list       = track_list;
    song->tracks           = tracks;

    return 0;
}

void avseq_track_close(AVSequencerSong *song, AVSequencerTrack *track)
{
    AVSequencerTrack **track_list;
    uint16_t tracks, i;

    if (!(song && track))
        return;

    track_list = song->track_list;
    tracks     = song->tracks;

    for (i = 0; i < tracks; ++i) {
        if (track_list[i] == track)
            break;
    }

    if (tracks && (i != tracks)) {
        AVSequencerOrderList *order_list;
        AVSequencerTrack *last_track = track_list[--tracks];

        if ((order_list = song->order_list)) {
            uint16_t channel;

            for (channel = 0; channel < song->channels; ++channel) {
                uint16_t order;

                for (order = 0; order < order_list->orders; ++order) {
                    AVSequencerOrderData *order_data = order_list->order_data[order];

                    if (order_data->track == track) {
                        if (last_track != track)
                            order_data->track = track_list[i + 1];
                        else if (i)
                            order_data->track = track_list[i - 1];
                        else
                            order_data->track = NULL;
                    }
                }

                order_list++;
            }
        }

        if (!tracks) {
            av_freep(&song->track_list);

            song->tracks = 0;
        } else if (!(track_list = av_realloc(track_list, (tracks * sizeof(AVSequencerTrack *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            const unsigned copy_tracks = i + 1;

            track_list = song->track_list;

            if (copy_tracks < tracks)
                memmove(track_list + i, track_list + copy_tracks, (tracks - copy_tracks) * sizeof(AVSequencerTrack *));

            track_list[tracks - 1] = NULL;
        } else {
            const unsigned copy_tracks = i + 1;

            if (copy_tracks < tracks) {
                memmove(track_list + i, track_list + copy_tracks, (tracks - copy_tracks) * sizeof(AVSequencerTrack *));

                track_list[tracks - 1] = last_track;
            }

            song->track_list = track_list;
            song->tracks     = tracks;
        }
    }

    avseq_track_data_close(track);
    av_freep(&track->data);

    track->last_row = 0;
}

int avseq_track_data_open(AVSequencerTrack *track, uint32_t rows)
{
    AVSequencerTrackRow *data;
    unsigned last_row;

    if (!track)
        return AVERROR_INVALIDDATA;

    data     = track->data;
    last_row = track->last_row + 1;

    if (!rows)
        rows = 64;

    if (!rows || rows >= 0x10000) {
        return AVERROR_INVALIDDATA;
    } else if (!(data = av_realloc(data, (rows * sizeof(AVSequencerTrackRow)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(track, AV_LOG_ERROR, "Cannot allocate storage container.\n");
        return AVERROR(ENOMEM);
    } else if (rows > last_row) {
        memset(data + last_row, 0, (rows - last_row) * sizeof(AVSequencerTrackRow));
    } else if (!track->data) {
        memset(data, 0, rows * sizeof(AVSequencerTrackRow));
    }

    track->data     = data;
    track->last_row = (uint16_t) (rows - 1);

    return 0;
}

void avseq_track_data_close(AVSequencerTrack *track)
{
    if (track) {
        AVSequencerTrackRow *track_data;

        if ((track_data = track->data)) {
            int i = track->last_row;

            do {
                int j = track_data->effects;

                track_data->instrument = 0;
                track_data->octave     = 0;
                track_data->note       = 0;

                while (j--) {
                    AVSequencerTrackEffect *effect = track_data->effects_data[j];

                    avseq_track_effect_close(track_data, effect);
                    avseq_track_effect_destroy(effect);
                }

                track_data++;
            } while (i--);
        }
    }
}

AVSequencerTrackEffect *avseq_track_effect_create(void)
{
    return av_mallocz(sizeof(AVSequencerTrackEffect));
}

void avseq_track_effect_destroy(AVSequencerTrackEffect *effect)
{
    av_free(effect);
}

int avseq_track_effect_open(AVSequencerTrack *track, AVSequencerTrackRow *data, AVSequencerTrackEffect *effect)
{
    AVSequencerTrackEffect **fx_list;
    uint16_t effects;

    if (!data)
        return AVERROR_INVALIDDATA;

    fx_list = data->effects_data;
    effects = data->effects;

    if (!(effect && ++effects)) {
        return AVERROR_INVALIDDATA;
    } else if (!(fx_list = av_realloc(fx_list, (effects * sizeof(AVSequencerTrackEffect *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(track, AV_LOG_ERROR, "Cannot allocate track data effect storage container.\n");
        return AVERROR(ENOMEM);
    }

    fx_list[effects - 1] = effect;
    data->effects_data   = fx_list;
    data->effects        = effects;

    return 0;
}

void avseq_track_effect_close(AVSequencerTrackRow *track_data, AVSequencerTrackEffect *effect)
{
    AVSequencerTrackEffect **fx_list;
    uint16_t effects, i;

    if (!(track_data && effect))
        return;

    fx_list = track_data->effects_data;
    effects = track_data->effects;

    for (i = 0; i < effects; ++i) {
        if (fx_list[i] == effect)
            break;
    }

    if (effects && (i != effects)) {
        AVSequencerTrackEffect *last_effect = fx_list[--effects];

        if (!effects) {
            av_freep(&track_data->effects_data);

            track_data->effects = 0;
        } else if (!(fx_list = av_realloc(fx_list, (effects * sizeof(AVSequencerTrackEffect *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            const unsigned copy_effects = i + 1;

            fx_list = track_data->effects_data;

            if (copy_effects < effects)
                memmove(fx_list + i, fx_list + copy_effects, (effects - copy_effects) * sizeof(AVSequencerTrackEffect *));

            fx_list[effects - 1] = NULL;
        } else {
            const unsigned copy_effects = i + 1;

            if (copy_effects < effects) {
                memmove(fx_list + i, fx_list + copy_effects, (effects - copy_effects) * sizeof(AVSequencerTrackEffect *));

                fx_list[effects - 1] = last_effect;
            }

            track_data->effects_data = fx_list;
            track_data->effects      = effects;
        }
    }
}

AVSequencerTrack *avseq_track_get_address(AVSequencerSong *song, uint32_t track)
{
    if (!(song && track))
        return NULL;

    if (track > song->tracks)
        return NULL;

    return song->track_list[--track];
}

int avseq_track_unpack(AVSequencerTrack *track, const uint8_t *buf, uint32_t len)
{
    AVSequencerTrackRow *data;
    uint16_t rows, last_pack_row = 0;
    uint8_t pack_type;

    if (!(track && buf && len))
        return AVERROR_INVALIDDATA;

    rows = track->last_row;
    data = track->data;

    while ((pack_type = *buf++)) {
        AVSequencerTrackEffect *fx;
        uint16_t tmp_pack_word = 0, tmp_pack_row;
        uint8_t tmp_pack_byte;

        if (last_pack_row > track->last_row) {
            av_log(track, AV_LOG_ERROR, "Cannot unpack track data, track has too few rows (expected: %d, got: %d).\n", track->last_row + 1, last_pack_row + 1);
            return AVERROR_INVALIDDATA;
        }

        if (pack_type & 1) { // row high byte follows
            if (!--len) {
                av_log(track, AV_LOG_ERROR, "Cannot unpack track data row high byte, unexpected end of stream.\n");
                return AVERROR_INVALIDDATA;
            }

            tmp_pack_word = (uint8_t) (*buf++ << 8);
        }

        if (pack_type & 2) { // row low byte follows
            if (!--len) {
                av_log(track, AV_LOG_ERROR, "Cannot unpack track data row low byte, unexpected end of stream.\n");
                return AVERROR_INVALIDDATA;
            }

            tmp_pack_word |= (uint8_t) *buf++;
        }

        if ((tmp_pack_row = tmp_pack_word)) {
            if (!(tmp_pack_row >>= 8)) {
                tmp_pack_row  = (uint8_t) tmp_pack_word;
                tmp_pack_word = (last_pack_row & 0xFF00) | tmp_pack_row;
            }

            tmp_pack_row  = last_pack_row;
            last_pack_row = tmp_pack_word;
            data         += tmp_pack_word - tmp_pack_row;
        }

        if (pack_type & 4) { // octave (high nibble) and note (low nibble) follows or 0xFx are special notes (keyoff, etc.)
            if (!--len) {
                av_log(track, AV_LOG_ERROR, "Cannot unpack track data octave and note byte, unexpected end of stream.\n");
                return AVERROR_INVALIDDATA;
            }

            if ( (tmp_pack_byte = *buf++) >= 0xF0) {
                data->note = tmp_pack_byte;
            } else {
                data->octave = tmp_pack_byte >> 4;
                data->note   = tmp_pack_byte & 0xF;
            }
        }

        tmp_pack_word = 0;

        if (pack_type & 8) { // instrument high byte follows
            if (!--len) {
                av_log(track, AV_LOG_ERROR, "Cannot unpack track data instrument high byte, unexpected end of stream.\n");
                return AVERROR_INVALIDDATA;
            }

            tmp_pack_word = *buf++ << 8;
        }

        if (pack_type & 16) { // instrument low byte follows
            if (!--len) {
                av_log(track, AV_LOG_ERROR, "Cannot unpack track data instrument low byte, unexpected end of stream.\n");
                return AVERROR_INVALIDDATA;
            }

            tmp_pack_word |= *buf++;
        }

        data->instrument = tmp_pack_word;

        if (pack_type & (32|64|128)) { // either track data effect byte, high or low byte of data word follow
            do {
                int res;

                tmp_pack_byte = 0;

                if (pack_type & 32) { // track data effect command follows
                    if (!--len) {
                        av_log(track, AV_LOG_ERROR, "Cannot unpack track data effect command, unexpected end of stream.\n");
                        return AVERROR_INVALIDDATA;
                    }

                    tmp_pack_byte = *buf++;
                }

                tmp_pack_word = 0;

                if (pack_type & 64) { // track data effect data word high byte follows
                    if (!--len) {
                        av_log(track, AV_LOG_ERROR, "Cannot unpack track data effect data word high byte, unexpected end of stream.\n");
                        return AVERROR_INVALIDDATA;
                    }

                    tmp_pack_word = *buf++ << 8;
                }

                if (pack_type & 128) { // track data effect data word low byte follows
                    if (!--len) {
                        av_log(track, AV_LOG_ERROR, "Cannot unpack track data effect data word low byte, unexpected end of stream.\n");
                        return AVERROR_INVALIDDATA;
                    }

                    tmp_pack_word |= *buf++;
                }

                if (!(fx = avseq_track_effect_create()))
                    return AVERROR(ENOMEM);

                fx->command = tmp_pack_byte;
                fx->data    = tmp_pack_word;

                if ((res = avseq_track_effect_open(track, data, fx)) < 0) {
                    avseq_track_effect_destroy(fx);

                    return res;
                }

                pack_type = 0xFF;
            } while ((int8_t) tmp_pack_byte < 0);
        }

        if (!len--) {
            av_log(track, AV_LOG_ERROR, "Cannot unpack track, unexpected end of stream.\n");
            return AVERROR_INVALIDDATA;
        }

        last_pack_row++;
        data++;
    }

    if (len || pack_type) {
        av_log(track, AV_LOG_ERROR, "Cannot unpack track, unexpected end of stream.\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}
