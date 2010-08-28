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
#include "libavformat/avformat.h"
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

AVSequencerModule *avseq_module_create(void)
{
    return av_mallocz(sizeof(AVSequencerModule) + FF_INPUT_BUFFER_PADDING_SIZE);
}

void avseq_module_destroy(AVSequencerModule *module)
{
    if (module)
        av_metadata_free(&module->metadata);

    av_free(module);
}

int avseq_module_open(AVSequencerContext *avctx, AVSequencerModule *module)
{
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

void avseq_module_close(AVSequencerContext *avctx, AVSequencerModule *module)
{
    AVSequencerModule **module_list;
    uint16_t modules, i;

    if (!(avctx && module))
        return;

    module_list = avctx->module_list;
    modules     = avctx->modules;

    for (i = 0; i < modules; ++i) {
        if (module_list[i] == module)
            break;
    }

    if (modules && (i != modules)) {
        AVSequencerModule *last_module = module_list[--modules];

        if (!modules) {
            av_freep(&avctx->module_list);

            avctx->modules = 0;
        } else if (!(module_list = av_realloc(module_list, (modules * sizeof(AVSequencerModule *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            const unsigned copy_modules = i + 1;

            module_list = avctx->module_list;

            if (copy_modules < modules)
                memmove(module_list + i, module_list + copy_modules, (modules - copy_modules) * sizeof(AVSequencerModule *));

            module_list[modules - 1] = NULL;
        } else {
            const unsigned copy_modules = i + 1;

            if (copy_modules < modules) {
                memmove(module_list + i, module_list + copy_modules, (modules - copy_modules) * sizeof(AVSequencerModule *));

                module_list[modules - 1] = last_module;
            }

            avctx->module_list = module_list;
            avctx->modules     = modules;
        }
    }

    i = module->songs;

    while (i--) {
        AVSequencerSong *song = module->song_list[i];

        avseq_song_close(module, song);
        avseq_song_destroy(song);
    }

    i = module->instruments;

    while (i--) {
        AVSequencerInstrument *instrument = module->instrument_list[i];

        avseq_instrument_close(module, instrument);
        avseq_instrument_destroy(instrument);
    }

    i = module->envelopes;

    while (i--) {
        AVSequencerEnvelope *envelope = module->envelope_list[i];

        avseq_envelope_close(module, envelope);
        avseq_envelope_destroy(envelope);
    }

    i = module->keyboards;

    while (i--) {
        AVSequencerKeyboard *keyboard = module->keyboard_list[i];

        avseq_keyboard_close(module, keyboard);
        avseq_keyboard_destroy(keyboard);
    }

    i = module->arpeggios;

    while (i--) {
        AVSequencerArpeggio *arpeggio = module->arpeggio_list[i];

        avseq_arpeggio_close(module, arpeggio);
        avseq_arpeggio_destroy(arpeggio);
    }
}

int avseq_module_play(AVSequencerContext *avctx, AVMixerContext *mixctx,
                      AVSequencerModule *module, AVSequencerSong *song,
                      const char *args, void *opaque, uint32_t mode)
{
    AVSequencerPlayerGlobals *player_globals;
    AVSequencerPlayerHostChannel *player_host_channel;
    AVSequencerPlayerChannel *player_channel;
    AVMixerData *mixer_data;
    uint16_t *gosub_stack     = NULL;
    uint16_t *loop_stack      = NULL;
    uint16_t *new_gosub_stack = NULL;
    uint16_t *new_loop_stack  = NULL;
    uint64_t volume_boost;
    uint32_t tempo;

    if (!(avctx && module && song))
        return AVERROR_INVALIDDATA;

    player_globals      = avctx->player_globals;
    player_host_channel = avctx->player_host_channel;
    player_channel      = avctx->player_channel;
    mixer_data          = avctx->player_mixer_data;

    if (!(player_globals = av_realloc(player_globals, sizeof(AVSequencerPlayerGlobals) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate player globals storage container.\n");
        return AVERROR(ENOMEM);
    } else if (!(player_host_channel = av_realloc(player_host_channel, (song->channels * sizeof(AVSequencerPlayerHostChannel)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_free(player_globals);
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate player host channel data.\n");
        return AVERROR(ENOMEM);
    } else if (!(player_channel = av_realloc(player_channel, (module->channels * sizeof(AVSequencerPlayerChannel)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_free(player_host_channel);
        av_free(player_globals);
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate player host channel data.\n");
        return AVERROR(ENOMEM);
    } else if (mixctx && !(mixer_data = avseq_mixer_init(avctx, mixctx, args, opaque))) {
        av_free(player_channel);
        av_free(player_host_channel);
        av_free(player_globals);
        av_log(avctx, AV_LOG_ERROR, "Cannot allocate mixer data.\n");
    }

    if (avctx->player_globals) {
        gosub_stack = player_globals->gosub_stack;
        loop_stack  = player_globals->loop_stack;
    } else {
        memset(player_globals, 0, sizeof(AVSequencerPlayerGlobals));
    }

    if (!avctx->player_host_channel) {
        if (avctx->player_globals) {
            if (song->channels > player_globals->stack_channels)
                memset(player_host_channel + player_globals->stack_channels, 0, (song->channels - player_globals->stack_channels) * sizeof(AVSequencerPlayerHostChannel));
        } else {
            memset(player_host_channel, 0, song->channels * sizeof(AVSequencerPlayerHostChannel));
        }
    }

    if (!avctx->player_channel) {
        if (avctx->player_globals) {
            if (module->channels > player_globals->virtual_channels)
                memset(player_channel + player_globals->virtual_channels, 0, (module->channels - player_globals->virtual_channels) * sizeof(AVSequencerPlayerChannel));
        } else {
            memset(player_channel, 0, module->channels * sizeof(AVSequencerPlayerChannel));
        }
    }

    if (!gosub_stack || (player_globals->stack_channels != song->channels) || (player_globals->gosub_stack_size != song->gosub_stack_size)) {
        if (!(new_gosub_stack = av_mallocz((song->channels * song->gosub_stack_size << 2) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            avseq_mixer_uninit(avctx, mixer_data);
            av_free(player_channel);
            av_free(player_host_channel);
            av_free(player_globals);
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate GoSub command stack storage container.\n");
            return AVERROR(ENOMEM);
        }
    } else if (!loop_stack || (player_globals->stack_channels != song->channels) || (player_globals->loop_stack_size != song->loop_stack_size)) {
        if (!(new_loop_stack = av_mallocz((song->channels * song->loop_stack_size << 2) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            av_free(new_gosub_stack);
            avseq_mixer_uninit(avctx, mixer_data);
            av_free(player_channel);
            av_free(player_host_channel);
            av_free(player_globals);
            av_log(avctx, AV_LOG_ERROR, "Cannot allocate pattern loop command stack storage container.\n");
            return AVERROR(ENOMEM);
        }
    }

    avctx->player_globals       = player_globals;
    avctx->player_host_channel  = player_host_channel;
    avctx->player_channel       = player_channel;
    avctx->player_module        = module;
    avctx->player_song          = song;
    avctx->player_mixer_data    = mixer_data;

    if (new_gosub_stack && gosub_stack) {
        uint32_t *process_stack     = (uint32_t *) player_globals->gosub_stack;
        uint32_t *process_new_stack = (uint32_t *) new_gosub_stack;
        uint16_t skip_over_channel  = song->channels, skip_over_stack, skip_new_stack, skip_old_stack, i;

        if (player_globals->stack_channels < skip_over_channel)
            skip_over_channel = player_globals->stack_channels;

        skip_over_stack = song->gosub_stack_size;

        if (player_globals->gosub_stack_size < skip_over_stack)
            skip_over_stack = player_globals->gosub_stack_size;

        for (i = skip_over_channel; i > 0; i--) {
            uint16_t j;

            skip_new_stack = song->gosub_stack_size;
            skip_old_stack = player_globals->gosub_stack_size;

            for (j = skip_over_stack; j > 0; j--) {
                *process_new_stack++ = *process_stack++;

                skip_new_stack--;
                skip_old_stack--;
            }

            process_stack     += skip_old_stack;
            process_new_stack += skip_new_stack;
        }

        av_free(gosub_stack);
    }

    if (new_loop_stack && loop_stack) {
        uint32_t *process_stack     = (uint32_t *) player_globals->loop_stack;
        uint32_t *process_new_stack = (uint32_t *) new_loop_stack;
        uint16_t skip_over_channel  = song->channels, skip_over_stack, skip_new_stack, skip_old_stack, i;

        if (player_globals->stack_channels < skip_over_channel)
            skip_over_channel = player_globals->stack_channels;

        skip_over_stack = song->loop_stack_size;

        if (player_globals->loop_stack_size < skip_over_stack)
            skip_over_stack = player_globals->loop_stack_size;

        for (i = skip_over_channel; i > 0; i--) {
            uint16_t j;

            skip_new_stack = song->loop_stack_size;
            skip_old_stack = player_globals->loop_stack_size;

            for (j = skip_over_stack; j > 0; j--) {
                *process_new_stack++ = *process_stack++;

                skip_new_stack--;
                skip_old_stack--;
            }

            process_stack     += skip_old_stack;
            process_new_stack += skip_new_stack;
        }

        av_free(loop_stack);
    }

    player_globals->gosub_stack      = new_gosub_stack;
    player_globals->gosub_stack_size = song->gosub_stack_size;
    player_globals->loop_stack       = new_loop_stack;
    player_globals->loop_stack_size  = song->loop_stack_size;
    player_globals->stack_channels   = song->channels;
    player_globals->virtual_channels = module->channels;
    player_globals->flags           &= ~(AVSEQ_PLAYER_GLOBALS_FLAG_NO_PROC_PATTERN|AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_PATTERN);

    if (mode)
        player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE;
    else
        player_globals->flags |= AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE;

    player_globals->play_type = AVSEQ_PLAYER_GLOBALS_PLAY_TYPE_SONG;

    if (!player_globals->relative_speed)
        player_globals->relative_speed = 0x10000;

    if (!player_globals->relative_pitch)
        player_globals->relative_pitch = player_globals->relative_speed;

    tempo                          = avseq_song_calc_speed(avctx, song);
    volume_boost                   = ((uint64_t) module->channels * 65536*125/1000) + (65536*75/100);
    mixer_data->flags             |= AVSEQ_MIXER_DATA_FLAG_MIXING;

    if (mixctx)
        avseq_mixer_set_rate(mixer_data, mixctx->frequency);

    avseq_mixer_set_tempo(mixer_data, tempo);
    avseq_mixer_set_volume(mixer_data, volume_boost, 65536, 65536, module->channels);

    return 0;
}

void avseq_module_stop(AVSequencerContext *avctx, uint32_t mode)
{
    if (avctx) {
        AVMixerData *mixer_data;

        if ((mixer_data = avctx->player_mixer_data)) {
            avctx->player_mixer_data = NULL;

            avseq_mixer_uninit(avctx, mixer_data);
        }

        if (mode & 1) {
            AVSequencerPlayerGlobals *player_globals;

            av_freep(&avctx->player_hook);
            av_freep(&avctx->player_channel);
            av_freep(&avctx->player_host_channel);

            if ((player_globals = avctx->player_globals)) {
                av_free(player_globals->gosub_stack);
                av_free(player_globals->loop_stack);
            }

            av_freep(&avctx->player_globals);
        }
    }
}

int avseq_module_set_channels(AVSequencerContext *avctx, AVSequencerModule *module,
                              uint32_t channels)
{
    if (!(avctx && module))
        return AVERROR_INVALIDDATA;

    if (!channels)
        channels = 64;

    if (channels > 65535)
        channels = 65535;

    if ((module == avctx->player_module) && (channels != module->channels)) {
        AVSequencerPlayerGlobals *player_globals;
        AVSequencerPlayerChannel *player_channel;

        if ((player_channel = avctx->player_channel)) {
            AVSequencerSong *song;

            if (!(player_channel = av_realloc(player_channel, (module->channels * sizeof(AVSequencerPlayerChannel)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
                av_log(avctx, AV_LOG_ERROR, "Cannot allocate player host channel data.\n");
                return AVERROR(ENOMEM);
            }

            if (channels > module->channels)
                memset ( player_channel + module->channels, 0, (channels - module->channels) * sizeof(AVSequencerPlayerChannel) );

            if ((song = avctx->player_song)) {
                AVSequencerPlayerHostChannel *player_host_channel;

                if ((song == avctx->player_song) && (player_host_channel = avctx->player_host_channel)) {
                    uint16_t i, host_channel = 0;

                    for (i = song->channels; i > 0; i--) {
                        if (player_host_channel->virtual_channel >= channels) {
                            AVSequencerPlayerChannel *updated_player_channel = player_channel;
                            uint16_t j;

                            player_host_channel->virtual_channel  = 0;
                            player_host_channel->virtual_channels = 0;

                            for (j = channels; j > 0; j--) {
                                if (player_channel->host_channel == host_channel)
                                    updated_player_channel->mixer.flags = 0;

                                updated_player_channel++;
                            }
                        }

                        host_channel++;
                        player_host_channel++;
                    }
                }
            }

            avctx->player_channel = player_channel;

            if ((player_globals = avctx->player_globals))
                player_globals->virtual_channels = module->channels;
        }
    }

    module->channels = channels;

    return 0;
}
