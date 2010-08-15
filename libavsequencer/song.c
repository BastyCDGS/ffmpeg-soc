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

AVSequencerSong *avseq_song_create(void)
{
    return av_mallocz(sizeof(AVSequencerSong) + FF_INPUT_BUFFER_PADDING_SIZE);
}

int avseq_song_open(AVSequencerModule *module, AVSequencerSong *song)
{
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

/** Old SoundTracker tempo definition table.  */
static const uint32_t old_st_lut[] = {
    192345259,  96192529,  64123930,  48096264,  38475419,
     32061964,  27482767,  24048132,  21687744,  19240098
};

uint32_t avseq_song_calc_speed(AVSequencerContext *avctx, AVSequencerSong *song)
{
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
            tempo = (uint64_t) 989156 * speed;

            if ((speed_mul = song->speed_mul))
                tempo *= speed_mul;

            if ((speed_mul = song->speed_div))
                tempo /= speed_mul;
        } else {
            tempo = avctx->old_st_lut ? avctx->old_st_lut[speed] : old_st_lut[speed];
        }
    } else {
        player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SPD_TIMING;
        tempo                  = (song->bpm_speed * song->bpm_tempo) << 16;

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

int avseq_song_reset(AVSequencerContext *avctx, AVSequencerSong *song)
{
    AVSequencerModule *module;
    AVSequencerOrderList *order_list;
    AVSequencerPlayerGlobals *player_globals;
    AVSequencerPlayerHostChannel *player_host_channel;
    AVSequencerPlayerChannel *player_channel;
    uint16_t *gosub_stack;
    uint16_t *loop_stack;
    uint32_t i;
    uint16_t frames;

    if (!(avctx && song))
        return AVERROR_INVALIDDATA;

    module              = avctx->player_module;
    player_globals      = avctx->player_globals;
    player_host_channel = avctx->player_host_channel;
    player_channel      = avctx->player_channel;

    if (!(module && player_globals && player_host_channel && player_channel))
        return AVERROR_INVALIDDATA;

    if (!(gosub_stack = av_mallocz((song->channels * song->gosub_stack_size << 2) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate GoSub command stack storage container.\n");
        return AVERROR(ENOMEM);
    } else if (!(loop_stack = av_mallocz((song->channels * song->loop_stack_size << 2) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_free(gosub_stack);
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate pattern loop command stack storage container.\n");
        return AVERROR(ENOMEM);
    }

    av_free(player_globals->gosub_stack);
    av_free(player_globals->loop_stack);

    memset(player_globals, 0, sizeof(AVSequencerPlayerGlobals));
    memset(player_host_channel, 0, song->channels * sizeof(AVSequencerPlayerHostChannel));
    memset(player_channel, 0, module->channels * sizeof(AVSequencerPlayerChannel));

    if ((module == avctx->player_module) && (song == avctx->player_song)) {
        AVSequencerMixerData *mixer_data = avctx->player_mixer_data;

        if (mixer_data) {
            uint32_t channel = 0;

            for (i = module->channels; i > 0; i--) {
                if (mixer_data->mixctx->set_channel_position_repeat_flags)
                    mixer_data->mixctx->set_channel_position_repeat_flags(mixer_data, (AVSequencerMixerChannel *) &(player_channel[channel].mixer), channel);

                channel++;
            }
        }
    }

    player_globals->gosub_stack        = gosub_stack;
    player_globals->gosub_stack_size   = song->gosub_stack_size;
    player_globals->loop_stack         = loop_stack;
    player_globals->loop_stack_size    = song->loop_stack_size;
    player_globals->stack_channels     = song->channels;
    player_globals->virtual_channels   = module->channels;
    player_globals->global_volume      = song->global_volume;
    player_globals->global_sub_volume  = song->global_sub_volume;
    player_globals->global_panning     = song->global_panning;
    player_globals->global_sub_panning = song->global_sub_panning;

    if (song->flags & AVSEQ_SONG_FLAG_SURROUND)
        player_globals->flags |= AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;

    player_globals->relative_speed = 0x10000;
    player_globals->relative_pitch = 0x10000;
    frames                         = song->frames;
    order_list                     = song->order_list;

    for (i = song->channels; i > 0; i--) {
        uint16_t order;

        player_host_channel->tempo              = frames;
        player_host_channel->order              = (AVSequencerOrderData *) order_list;
        player_host_channel->multi_retrig_scale = 4;
        player_host_channel->tempo_counter      = 0xFFFFFFFE;
        player_host_channel->track_volume       = order_list->volume;
        player_host_channel->track_sub_vol      = order_list->sub_volume;
        player_host_channel->track_panning      = order_list->channel_panning;
        player_host_channel->track_sub_pan      = order_list->channel_sub_panning;
        player_host_channel->track_note_pan     = order_list->channel_panning;
        player_host_channel->track_note_sub_pan = order_list->channel_sub_panning;
        player_host_channel->flags              = AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

        if (order_list->flags & AVSEQ_ORDER_LIST_FLAG_CHANNEL_SURROUND)
            player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;

        if (song->flags & AVSEQ_SONG_FLAG_LINEAR_FREQ_TABLE)
            player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ;

        player_host_channel->ch_control_type   = AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_GLOBAL;
        player_host_channel->ch_control_affect = AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_AFFECT_NOTES;

        for (order = 0; order < order_list->orders; ++order) {
            AVSequencerOrderData *order_data = order_list->order_data[order];

            order_data->played = 0;
        }

        order_list++;
        player_host_channel++;
    }

    if (!(avseq_song_calc_speed ( avctx, song ))) {
        av_log(song, AV_LOG_ERROR, "Relative song speed is invalid!\n");
        return AVERROR_INVALIDDATA;
    } else if (!avctx->player_globals->tempo) {
        av_log(song, AV_LOG_ERROR, "Absolute song speed is invalid!\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int set_new_channel_stack(AVSequencerContext *avctx, AVSequencerSong *song, uint16_t gosub_stack, uint16_t loop_stack, uint16_t channels);

int avseq_song_set_channels(AVSequencerContext *avctx, AVSequencerSong *song,
                            uint32_t channels)
{
    if (!(avctx && song))
        return AVERROR_INVALIDDATA;

    if (!channels)
        channels = 16;

    if (channels > 256)
        channels = 256;

    if (channels != song->channels) {
        AVSequencerPlayerHostChannel *player_host_channel;
        AVSequencerOrderList *order_list = song->order_list;
        int res;

        if ((channels == 0) || (channels > 256)) {
            return AVERROR_INVALIDDATA;
        } else if (!(order_list = av_realloc(order_list, (channels * sizeof(AVSequencerOrderList)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(song, AV_LOG_ERROR, "Cannot allocate order list storage container.\n");
            return AVERROR(ENOMEM);
        }

        if ((song == avctx->player_song) && (player_host_channel = avctx->player_host_channel)) {
            AVSequencerModule *module;
            AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
            uint16_t i;

            if (!(player_host_channel = av_realloc(player_host_channel, (song->channels * sizeof (AVSequencerPlayerHostChannel)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
                av_free(order_list);
                av_log(song, AV_LOG_ERROR, "Cannot allocate player host channel data.\n");
                return AVERROR(ENOMEM);
            }

            if (channels > song->channels)
                memset(player_host_channel + song->channels, 0, (channels - song->channels) * sizeof(AVSequencerPlayerHostChannel));

            if ((module = avctx->player_module)) {
                AVSequencerPlayerChannel *player_channel;

                if ((player_channel = avctx->player_channel)) {
                    for (i = module->channels; i > 0; i--) {
                        if (player_channel->host_channel >= channels)
                            player_channel->mixer.flags = 0;

                        if (player_globals)
                            player_globals->channels--;
                    }
                }
            }

            if ((res = set_new_channel_stack(avctx, song, song->gosub_stack_size, song->loop_stack_size, channels)) < 0) {
                av_freep(&player_host_channel);
                av_free(order_list);
                return res;
            }

            if (player_globals)
                player_globals->stack_channels = channels;

            if (player_host_channel)
                avctx->player_host_channel = player_host_channel;
        }

        if (channels > song->channels) {
            uint16_t channel = channels;

            memset(order_list + song->channels, 0, (channels - song->channels) * sizeof(AVSequencerOrderList));

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

static int set_new_channel_stack(AVSequencerContext *avctx, AVSequencerSong *song,
                                 uint16_t gosub_stack, uint16_t loop_stack,
                                 uint16_t channels)
{
    AVSequencerPlayerGlobals *player_globals = NULL;
    uint16_t *new_gosub_stack                = NULL;
    uint16_t *new_loop_stack                 = NULL;

    if (!gosub_stack)
        gosub_stack = 4;

    if (!loop_stack)
        loop_stack = 1;

    if (song == avctx->player_song)
        player_globals = avctx->player_globals;

    if (player_globals && (!player_globals->gosub_stack || (channels != song->channels) || (gosub_stack != song->gosub_stack_size))) {
        if (!(new_gosub_stack = av_mallocz((channels * gosub_stack << 2) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate GoSub command stack storage container.\n");
            return AVERROR(ENOMEM);
        }
    }

    if (player_globals && (!player_globals->loop_stack || (channels != song->channels) || (loop_stack != song->loop_stack_size))) {
        if (!(new_loop_stack = av_mallocz((channels * loop_stack << 2) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_free(new_gosub_stack);
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate pattern loop command stack storage container.\n");
            return AVERROR(ENOMEM);
        }
    }

    if (new_gosub_stack && player_globals->gosub_stack) {
        uint32_t *process_stack     = (uint32_t *) player_globals->gosub_stack;
        uint32_t *process_new_stack = (uint32_t *) new_gosub_stack;
        uint16_t skip_over_channel  = channels, skip_over_stack, skip_new_stack, skip_old_stack, i;

        if (song->channels < skip_over_channel)
            skip_over_channel = song->channels;

        skip_over_stack = gosub_stack;

        if (song->gosub_stack_size < skip_over_stack)
            skip_over_stack = song->gosub_stack_size;

        for (i = skip_over_channel; i > 0; i--) {
            uint16_t j;

            skip_new_stack = gosub_stack;
            skip_old_stack = song->gosub_stack_size;

            for (j = skip_over_stack; j > 0; j--) {
                *process_new_stack++ = *process_stack++;

                skip_new_stack--;
                skip_old_stack--;
            }

            process_stack     += skip_old_stack;
            process_new_stack += skip_new_stack;
        }

        av_free(player_globals->gosub_stack);
    }

    if (new_loop_stack && player_globals->loop_stack) {
        uint32_t *process_stack     = (uint32_t *) player_globals->loop_stack;
        uint32_t *process_new_stack = (uint32_t *) new_loop_stack;
        uint16_t skip_over_channel  = channels, skip_over_stack, skip_new_stack, skip_old_stack, i;

        if (song->channels < skip_over_channel)
            skip_over_channel = song->channels;

        skip_over_stack = loop_stack;

        if (song->loop_stack_size < skip_over_stack)
            skip_over_stack = song->loop_stack_size;

        for (i = skip_over_channel; i > 0; i--) {
            uint16_t j;

            skip_new_stack = loop_stack;
            skip_old_stack = song->loop_stack_size;

            for (j = skip_over_stack; j > 0; j--) {
                *process_new_stack++ = *process_stack++;

                skip_new_stack--;
                skip_old_stack--;
            }

            process_stack     += skip_old_stack;
            process_new_stack += skip_new_stack;
        }

        av_free(player_globals->loop_stack);
    }

    if (player_globals) {
        player_globals->gosub_stack      = new_gosub_stack;
        player_globals->gosub_stack_size = gosub_stack;
        player_globals->loop_stack       = new_loop_stack;
        player_globals->loop_stack_size  = loop_stack;
        player_globals->stack_channels   = channels;
    }

    song->gosub_stack_size = gosub_stack;
    song->loop_stack_size  = loop_stack;

    return 0;
}

int avseq_song_set_stack(AVSequencerContext *avctx, AVSequencerSong *song,
                         uint32_t gosub_stack, uint32_t loop_stack)
{
    if (!(avctx && song))
        return AVERROR_INVALIDDATA;

    if (gosub_stack >= 0x10000)
        gosub_stack = 0xFFFF;

    if (loop_stack >= 0x10000)
        loop_stack = 0xFFFF;

    return set_new_channel_stack(avctx, song, gosub_stack, loop_stack, song->channels);
}
