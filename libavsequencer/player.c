/*
 * Sequencer main playback handler
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
 * Sequencer main playback handler.
 */

#include <stddef.h>
#include <string.h>
#include "libavutil/intreadwrite.h"
#include "libavsequencer/avsequencer.h"
#include "libavsequencer/player.h"

#define AVSEQ_RANDOM_CONST  -1153374675
#define AVSEQ_SLIDE_CONST   (UINT64_C(0x100000000)*UINT64_C(8363*1712*4))

#define ASSIGN_INSTRUMENT_ENVELOPE(env_type)                                                                                \
    static const AVSequencerEnvelope *assign_##env_type##_envelope(const AVSequencerContext *const avctx,                   \
                                                                   const AVSequencerInstrument *const instrument,           \
                                                                   AVSequencerPlayerHostChannel *const player_host_channel, \
                                                                   AVSequencerPlayerChannel *const player_channel,          \
                                                                   const AVSequencerEnvelope **envelope,                    \
                                                                   AVSequencerPlayerEnvelope **player_envelope)

ASSIGN_INSTRUMENT_ENVELOPE(volume)
{
    if (instrument)
        *envelope = instrument->volume_env;

    *player_envelope = &player_channel->vol_env;

    return player_host_channel->prev_volume_env;
}

ASSIGN_INSTRUMENT_ENVELOPE(panning)
{
    if (instrument)
        *envelope = instrument->panning_env;

    *player_envelope = &player_channel->pan_env;

    return player_host_channel->prev_panning_env;
}

ASSIGN_INSTRUMENT_ENVELOPE(slide)
{
    if (instrument)
        *envelope = instrument->slide_env;

    *player_envelope = &player_channel->slide_env;

    return player_host_channel->prev_slide_env;
}

ASSIGN_INSTRUMENT_ENVELOPE(vibrato)
{
    if (instrument)
        *envelope = instrument->vibrato_env;

    *player_envelope = &player_host_channel->vibrato_env;

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(tremolo)
{
    if (instrument)
        *envelope = instrument->tremolo_env;

    *player_envelope = &player_host_channel->tremolo_env;

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(pannolo)
{
    if (instrument)
        *envelope = instrument->pannolo_env;

    *player_envelope = &player_host_channel->pannolo_env;

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(channolo)
{
    if (instrument)
        *envelope = instrument->channolo_env;

    *player_envelope = &player_host_channel->channolo_env;

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(spenolo)
{
    if (instrument)
        *envelope = instrument->spenolo_env;

    *player_envelope = &avctx->player_globals->spenolo_env;

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(track_tremolo)
{
    if (instrument)
        *envelope = instrument->tremolo_env;

    *player_envelope = &player_host_channel->track_trem_env;

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(track_pannolo)
{
    if (instrument)
        *envelope = instrument->pannolo_env;

    *player_envelope = &player_host_channel->track_pan_env;

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(global_tremolo)
{
    if (instrument)
        *envelope = instrument->tremolo_env;

    *player_envelope = &avctx->player_globals->tremolo_env;

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(global_pannolo)
{
    if (instrument)
        *envelope = instrument->pannolo_env;

    *player_envelope = &avctx->player_globals->pannolo_env;

    return (*player_envelope)->envelope;
}


#define ASSIGN_SAMPLE_ENVELOPE(env_type)                                                                            \
    static const AVSequencerEnvelope *assign_##env_type##_envelope(const AVSequencerSample *const sample,           \
                                                                   AVSequencerPlayerChannel *const player_channel,  \
                                                                   AVSequencerPlayerEnvelope **player_envelope)     \

ASSIGN_SAMPLE_ENVELOPE(auto_vibrato)
{
    *player_envelope = &player_channel->auto_vib_env;

    return sample->auto_vibrato_env;
}

ASSIGN_SAMPLE_ENVELOPE(auto_tremolo)
{
    *player_envelope = &player_channel->auto_trem_env;

    return sample->auto_tremolo_env;
}

ASSIGN_SAMPLE_ENVELOPE(auto_pannolo)
{
    *player_envelope = &player_channel->auto_pan_env;

    return sample->auto_pannolo_env;
}

ASSIGN_INSTRUMENT_ENVELOPE(resonance)
{
    if (instrument)
        *envelope = instrument->resonance_env;

    *player_envelope = &player_channel->resonance_env;

    return player_host_channel->prev_resonance_env;
}

#define USE_ENVELOPE(env_type)                                                                                              \
    static AVSequencerPlayerEnvelope *use_##env_type##_envelope(const AVSequencerContext *const avctx,                      \
                                                                AVSequencerPlayerHostChannel *const player_host_channel,    \
                                                                AVSequencerPlayerChannel *const player_channel)

USE_ENVELOPE(volume)
{
    return &player_channel->vol_env;
}

USE_ENVELOPE(panning)
{
    return &player_channel->pan_env;
}

USE_ENVELOPE(slide)
{
    return &player_channel->slide_env;
}

USE_ENVELOPE(vibrato)
{
    return &player_host_channel->vibrato_env;
}

USE_ENVELOPE(tremolo)
{
    return &player_host_channel->tremolo_env;
}

USE_ENVELOPE(pannolo)
{
    return &player_host_channel->pannolo_env;
}

USE_ENVELOPE(channolo)
{
    return &player_host_channel->channolo_env;
}

USE_ENVELOPE(spenolo)
{
    return &avctx->player_globals->spenolo_env;
}

USE_ENVELOPE(auto_vibrato)
{
    return &player_channel->auto_vib_env;
}

USE_ENVELOPE(auto_tremolo)
{
    return &player_channel->auto_trem_env;
}

USE_ENVELOPE(auto_pannolo)
{
    return &player_channel->auto_pan_env;
}

USE_ENVELOPE(track_tremolo)
{
    return &player_host_channel->track_trem_env;
}

USE_ENVELOPE(track_pannolo)
{
    return &player_host_channel->track_pan_env;
}

USE_ENVELOPE(global_tremolo)
{
    return &avctx->player_globals->tremolo_env;
}

USE_ENVELOPE(global_pannolo)
{
    return &avctx->player_globals->pannolo_env;
}

USE_ENVELOPE(arpeggio)
{
    return &player_host_channel->arpepggio_env;
}

USE_ENVELOPE(resonance)
{
    return &player_channel->resonance_env;
}

#define PRESET_EFFECT(fx_type)                                                            \
    static void preset_##fx_type(const AVSequencerContext *const avctx,                   \
                                 AVSequencerPlayerHostChannel *const player_host_channel, \
                                 AVSequencerPlayerChannel *const player_channel,          \
                                 const uint16_t channel,                                  \
                                 uint16_t data_word)

PRESET_EFFECT(tone_portamento)
{
    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TONE_PORTA;
}

PRESET_EFFECT(vibrato)
{
    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_VIBRATO;
}

PRESET_EFFECT(note_delay)
{
    if ((player_host_channel->note_delay = data_word)) {
        if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX)) {
            player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX;

            player_host_channel->exec_fx = player_host_channel->note_delay;
        }
    }
}

PRESET_EFFECT(tremolo)
{
    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOLO;
}

PRESET_EFFECT(set_transpose)
{
    player_host_channel->flags         |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_TRANSPOSE;
    player_host_channel->transpose      = data_word >> 8;
    player_host_channel->trans_finetune = data_word;
}

static const int32_t portamento_mask[8] = {
    0,
    0,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_ONCE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_ONCE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE
};

static const int32_t portamento_trigger_mask[6] = {
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_ONCE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_ONCE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE_DOWN
};

#define CHECK_EFFECT(fx_type)                                                               \
    static void check_##fx_type(const AVSequencerContext *const avctx,                      \
                                AVSequencerPlayerHostChannel *const player_host_channel,    \
                                AVSequencerPlayerChannel *const player_channel,             \
                                const uint16_t channel,                                     \
                                uint16_t *const fx_byte,                                    \
                                uint16_t *const data_word,                                  \
                                uint16_t *const flags)

CHECK_EFFECT(portamento)
{
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_ONCE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE);

        player_host_channel->fine_slide_flags |= portamento_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_PORTA_UP)];
    } else {
        const AVSequencerTrack *const track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            if ((*fx_byte <= AVSEQ_TRACK_EFFECT_CMD_PORTA_DOWN) && !(player_host_channel->fine_slide_flags & (AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE)))
                goto trigger_portamento_done;

            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_PORTA_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_PORTA_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE)
                *fx_byte += AVSEQ_TRACK_EFFECT_CMD_O_PORTA_UP - AVSEQ_TRACK_EFFECT_CMD_PORTA_UP;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES) && (*fx_byte > AVSEQ_TRACK_EFFECT_CMD_PORTA_DOWN)) {
            const uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= AVSEQ_TRACK_EFFECT_CMD_PORTA_UP - AVSEQ_TRACK_EFFECT_CMD_ARPEGGIO;
            *fx_byte &= -2;

            if (player_host_channel->fine_slide_flags & portamento_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_PORTA_DOWN - 1)])
                *fx_byte |= 1;

            *fx_byte += AVSEQ_TRACK_EFFECT_CMD_PORTA_UP - AVSEQ_TRACK_EFFECT_CMD_ARPEGGIO;
        }
trigger_portamento_done:
        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_O_PORTA_UP)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

CHECK_EFFECT(tone_portamento)
{
}

CHECK_EFFECT(note_slide)
{
    uint8_t note_slide_type;

    if (!(note_slide_type = (*data_word >> 8)))
        note_slide_type = player_host_channel->note_slide_type;

    if (note_slide_type & 0xF)
        *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
}

static const int32_t volume_slide_mask[4] = {
    0,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE
};

static const int32_t volume_slide_trigger_mask[4] = {
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE_DOWN
};

CHECK_EFFECT(volume_slide)
{
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE);

        player_host_channel->fine_slide_flags |= volume_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP)];
    } else {
        const AVSequencerTrack *const track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_VOLSL_UP;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            const uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP - AVSEQ_TRACK_EFFECT_CMD_SET_VOLUME;
            *fx_byte &= -2;

            if (volume_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;

            *fx_byte += AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP - AVSEQ_TRACK_EFFECT_CMD_SET_VOLUME;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_F_VOLSL_UP)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

CHECK_EFFECT(volume_slide_to)
{
    if ((*data_word >> 8) == 0xFF)
        *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
}

static const int32_t track_volume_slide_mask[4] = {
    0,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE
};

static const int32_t track_volume_slide_trigger_mask[4] = {
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE_DOWN
};

CHECK_EFFECT(track_volume_slide)
{
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE);

        player_host_channel->fine_slide_flags |= track_volume_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP)];
    } else {
        const AVSequencerTrack *const track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_TVOL_SL_UP;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            const uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP - AVSEQ_TRACK_EFFECT_CMD_SET_TRK_VOL;
            *fx_byte &= -2;

            if (track_volume_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;

            *fx_byte += AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP - AVSEQ_TRACK_EFFECT_CMD_SET_TRK_VOL;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_F_TVOL_SL_UP)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

static const int32_t panning_slide_mask[4] = {
    0,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE
};

static const int32_t panning_slide_trigger_mask[4] = {
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE_RIGHT
};

CHECK_EFFECT(panning_slide)
{
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE);

        player_host_channel->fine_slide_flags |= panning_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT)];
    } else {
        const AVSequencerTrack *const track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_P_SL_LEFT;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            const uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT - AVSEQ_TRACK_EFFECT_CMD_SET_PANNING;
            *fx_byte &= -2;

            if (panning_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;

            *fx_byte += AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT - AVSEQ_TRACK_EFFECT_CMD_SET_PANNING;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_F_P_SL_LEFT)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

static const int32_t track_panning_slide_mask[4] =
{
    0,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_PAN_SLIDE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRK_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_PAN_SLIDE
};

static const int32_t track_panning_slide_trigger_mask[4] =
{
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRK_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRK_PAN_SLIDE_RIGHT
};

CHECK_EFFECT(track_panning_slide)
{
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRK_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_PAN_SLIDE);

        player_host_channel->fine_slide_flags |= track_panning_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT)];
    } else {
        const AVSequencerTrack *const track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_PAN_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_TP_SL_LEFT;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            const uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT - AVSEQ_TRACK_EFFECT_CMD_SET_TRK_PAN;
            *fx_byte &= -2;

            if (track_panning_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;

            *fx_byte += AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT - AVSEQ_TRACK_EFFECT_CMD_SET_TRK_PAN;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_F_TP_SL_LEFT)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

static const int32_t speed_slide_mask[4] = {
    0,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_SPEED_SLIDE_SLOWER,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE_SLOWER|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE
};

static const int32_t speed_slide_trigger_mask[4] = {
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_SPEED_SLIDE_SLOWER,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_SPEED_SLIDE_SLOWER,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE_SLOWER,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE_SLOWER
};

CHECK_EFFECT(speed_slide)
{
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_SPEED_SLIDE_SLOWER|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE_SLOWER|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE);

        player_host_channel->fine_slide_flags |= speed_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST)];
    } else {
        const AVSequencerTrack *const track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_S_SLD_FAST;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            const uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST - AVSEQ_TRACK_EFFECT_CMD_SET_SPEED;
            *fx_byte &= -2;

            if (speed_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;

            *fx_byte += AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST - AVSEQ_TRACK_EFFECT_CMD_SET_SPEED;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_F_S_SLD_FAST)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

CHECK_EFFECT(channel_control)
{
}

static const int32_t global_volume_slide_mask[4] = {
    0,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_VOL_SLIDE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_VOL_SLIDE
};

static const int32_t global_volume_slide_trigger_mask[4] = {
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_VOL_SLIDE_DOWN,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_VOL_SLIDE_DOWN
};

CHECK_EFFECT(global_volume_slide)
{
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_VOL_SLIDE);

        player_host_channel->fine_slide_flags |= global_volume_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_UP)];
    } else {
        const AVSequencerTrack *const track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_VOL_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_G_VOL_UP;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            const uint16_t mask_volume_fx = *fx_byte;

            *fx_byte &= -2;

            if (global_volume_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_UP)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_F_G_VOL_UP)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

static const int32_t global_panning_slide_mask[4] = {
    0,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_PAN_SLIDE,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_PAN_SLIDE
};

static const int32_t global_panning_slide_trigger_mask[4] = {
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_PAN_SLIDE_RIGHT,
    AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_PAN_SLIDE_RIGHT
};

CHECK_EFFECT(global_panning_slide)
{
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_PAN_SLIDE);

        player_host_channel->fine_slide_flags |= global_panning_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_GPANSL_LEFT)];
    } else {
        const AVSequencerTrack *const track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_GPANSL_LEFT;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_PAN_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_FGP_SL_LEFT;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            const uint16_t mask_volume_fx = *fx_byte;

            *fx_byte &= -2;

            if (global_panning_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_GPANSL_LEFT)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_FGP_SL_LEFT)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

static int16_t step_envelope(AVSequencerContext *const avctx, AVSequencerPlayerEnvelope *const player_envelope, const int16_t *const envelope_data, uint16_t envelope_pos, const uint16_t tempo_multiplier, const int16_t value_adjustment)
{
    uint32_t seed, randomize_value;
    const uint16_t envelope_restart = player_envelope->start;
    int16_t value;

    value = envelope_data[envelope_pos];

    if (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM) {
        avctx->seed     = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
        randomize_value = ((int32_t) player_envelope->value_max - (int32_t) player_envelope->value_min) + 1;
        value           = ((uint64_t) seed * randomize_value) >> 32;
        value          += player_envelope->value_min;
    }

    value += value_adjustment;

    if (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_BACKWARDS) {
        if (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_LOOPING) {
            envelope_pos += tempo_multiplier;

            if (envelope_pos < tempo_multiplier)
                goto loop_envelope_over_back;

            for (;;) {
check_back_envelope_loop:
                if (envelope_pos <= envelope_restart)
                    break;
loop_envelope_over_back:
                if (envelope_restart == player_envelope->end)
                    goto run_envelope_check_pingpong_wait;

                if (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_PINGPONG) {
                    envelope_pos -= player_envelope->pos;
                    envelope_pos += -envelope_pos + envelope_restart;

                    if (envelope_pos < envelope_restart)
                        goto check_envelope_loop;

                    goto loop_envelope_over;
                } else {
                    envelope_pos += player_envelope->end - envelope_restart;
                }
            }
        } else {
            if (envelope_pos < tempo_multiplier)
                player_envelope->tempo = 0;

            envelope_pos -= tempo_multiplier;
        }
    } else if (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_LOOPING) {
        envelope_pos += tempo_multiplier;

        if (envelope_pos < tempo_multiplier)
            goto loop_envelope_over;

        for (;;) {
check_envelope_loop:
            if (envelope_pos <= player_envelope->end)
                break;
loop_envelope_over:
            if (envelope_restart == player_envelope->end) {
run_envelope_check_pingpong_wait:
                envelope_pos = envelope_restart;

                if (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_PINGPONG)
                    player_envelope->flags ^= AVSEQ_PLAYER_ENVELOPE_FLAG_BACKWARDS;

                break;
            }

            if (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_PINGPONG) {
                player_envelope->flags ^= AVSEQ_PLAYER_ENVELOPE_FLAG_BACKWARDS;

                envelope_pos -= player_envelope->pos;
                envelope_pos += -envelope_pos + player_envelope->end;

                if (envelope_pos < player_envelope->end)
                    goto check_back_envelope_loop;

                goto loop_envelope_over_back;
            } else {
                envelope_pos += envelope_restart - player_envelope->end;
            }
        }
    } else {
        envelope_pos += tempo_multiplier;

        if ((envelope_pos < tempo_multiplier) || (envelope_pos > player_envelope->end))
            player_envelope->tempo = 0;
    }

    player_envelope->pos = envelope_pos;

    if ((player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD) && !(player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM))
        value = envelope_data[envelope_pos] + value_adjustment;

    return value;
}

static void set_envelope(AVSequencerPlayerChannel *const player_channel, AVSequencerPlayerEnvelope *const envelope, uint16_t envelope_pos)
{
    const AVSequencerEnvelope *instrument_envelope;
    uint8_t envelope_flags;
    uint16_t envelope_loop_start, envelope_loop_end;

    if (!(instrument_envelope = envelope->envelope))
        return;

    envelope_flags      = AVSEQ_PLAYER_ENVELOPE_FLAG_LOOPING;
    envelope_loop_start = envelope->loop_start;
    envelope_loop_end   = envelope->loop_end;

    if ((envelope->rep_flags & AVSEQ_PLAYER_ENVELOPE_REP_FLAG_SUSTAIN) && !(player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SUSTAIN)) {
        envelope_loop_start = envelope->sustain_start;
        envelope_loop_end   = envelope->sustain_end;
    } else if (!(envelope->rep_flags & AVSEQ_PLAYER_ENVELOPE_REP_FLAG_LOOP)) {
        envelope_flags    = 0;
        envelope_loop_end = instrument_envelope->points - 1;
    }

    if (envelope_loop_start > envelope_loop_end)
        envelope_loop_start = envelope_loop_end;

    if (envelope_pos > envelope_loop_end)
        envelope_pos = envelope_loop_end;

    envelope->pos   = envelope_pos;
    envelope->start = envelope_loop_start;
    envelope->end   = envelope_loop_end;
    envelope->flags = envelope_flags;
}

static int16_t run_envelope(AVSequencerContext *const avctx, AVSequencerPlayerEnvelope *const player_envelope, uint16_t tempo_multiplier, int16_t value_adjustment)
{
    const AVSequencerEnvelope *envelope;
    int16_t value = player_envelope->value;

    if ((envelope = player_envelope->envelope)) {
        const int16_t *envelope_data = envelope->data;
        const uint16_t envelope_pos  = player_envelope->pos;

        if (player_envelope->tempo) {
            uint16_t envelope_count;

            if (!(envelope_count = player_envelope->tempo_count))
                player_envelope->value = value = step_envelope(avctx, player_envelope, envelope_data, envelope_pos, tempo_multiplier, value_adjustment);

            player_envelope->tempo_count = ++envelope_count;

            if ((player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM) && (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RND_DELAY))
                tempo_multiplier *= player_envelope->tempo;
            else
                tempo_multiplier  = player_envelope->tempo;

            if (envelope_count >= tempo_multiplier)
                player_envelope->tempo_count = 0;
            else if ((player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD) && !(player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM))
                value = envelope_data[envelope_pos] + value_adjustment;
        }
    }

    return value;
}

static void play_key_off(AVSequencerPlayerChannel *const player_channel)
{
    const AVSequencerSample *sample;
    const AVSequencerSynthWave *waveform;
    uint32_t repeat, repeat_length, repeat_count;
    uint8_t flags;

    if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SUSTAIN)
        return;

    player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SUSTAIN;

    set_envelope(player_channel, &player_channel->vol_env, player_channel->vol_env.pos);
    set_envelope(player_channel, &player_channel->pan_env, player_channel->pan_env.pos);
    set_envelope(player_channel, &player_channel->slide_env, player_channel->slide_env.pos);
    set_envelope(player_channel, &player_channel->auto_vib_env, player_channel->auto_vib_env.pos);
    set_envelope(player_channel, &player_channel->auto_trem_env, player_channel->auto_trem_env.pos);
    set_envelope(player_channel, &player_channel->auto_pan_env, player_channel->auto_pan_env.pos);
    set_envelope(player_channel, &player_channel->resonance_env, player_channel->resonance_env.pos);

    if ((!player_channel->vol_env.envelope) || (!player_channel->vol_env.tempo) || (player_channel->vol_env.flags & AVSEQ_PLAYER_ENVELOPE_FLAG_LOOPING))
        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;

    if ((sample = player_channel->sample) && (sample->flags & AVSEQ_SAMPLE_FLAG_SUSTAIN_LOOP)) {
        repeat                              = sample->repeat;
        repeat_length                       = sample->rep_len;
        repeat_count                        = sample->rep_count;
        player_channel->mixer.repeat_start  = repeat;
        player_channel->mixer.repeat_length = repeat_length;
        player_channel->mixer.repeat_count  = repeat_count;
        flags                               = player_channel->mixer.flags & ~(AVSEQ_MIXER_CHANNEL_FLAG_LOOP|AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG|AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS);

        if ((sample->flags & AVSEQ_SAMPLE_FLAG_LOOP) && repeat_length) {
            flags |= AVSEQ_MIXER_CHANNEL_FLAG_LOOP;

            if (sample->repeat_mode & AVSEQ_SAMPLE_REP_MODE_PINGPONG)
                flags |= AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG;

            if (sample->repeat_mode & AVSEQ_SAMPLE_REP_MODE_BACKWARDS)
                flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
        }

        player_channel->mixer.flags = flags;
    }

    if ((waveform = player_channel->sample_waveform) && (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_SUSTAIN_LOOP)) {
        repeat                              = waveform->repeat;
        repeat_length                       = waveform->rep_len;
        repeat_count                        = waveform->rep_count;
        player_channel->mixer.repeat_start  = repeat;
        player_channel->mixer.repeat_length = repeat_length;
        player_channel->mixer.repeat_count  = repeat_count;
        flags                               = player_channel->mixer.flags & ~(AVSEQ_MIXER_CHANNEL_FLAG_LOOP|AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG|AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS);

        if (!(waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_NOLOOP) && repeat_length) {
            flags |= AVSEQ_MIXER_CHANNEL_FLAG_LOOP;

            if (waveform->repeat_mode & AVSEQ_SYNTH_WAVE_REP_MODE_PINGPONG)
                flags |= AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG;

            if (waveform->repeat_mode & AVSEQ_SYNTH_WAVE_REP_MODE_BACKWARDS)
                flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
        }

        player_channel->mixer.flags = flags;
    }

    if (player_channel->use_sustain_flags & AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAG_VOLUME)
        player_channel->entry_pos[0] = player_channel->sustain_pos[0];

    if (player_channel->use_sustain_flags & AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAG_PANNING)
        player_channel->entry_pos[1] = player_channel->sustain_pos[1];

    if (player_channel->use_sustain_flags & AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAG_SLIDE)
        player_channel->entry_pos[2] = player_channel->sustain_pos[2];

    if (player_channel->use_sustain_flags & AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAG_SPECIAL)
        player_channel->entry_pos[3] = player_channel->sustain_pos[3];
}

/** Linear frequency table. Value is 16777216*2^(x/3072).  */
static const uint32_t linear_frequency_lut[] = {
    16777216, 16781002, 16784789, 16788576, 16792365, 16796154, 16799944, 16803735,
    16807527, 16811320, 16815114, 16818908, 16822704, 16826500, 16830297, 16834095,
    16837894, 16841693, 16845494, 16849295, 16853097, 16856900, 16860704, 16864509,
    16868315, 16872121, 16875928, 16879737, 16883546, 16887356, 16891166, 16894978,
    16898791, 16902604, 16906418, 16910233, 16914049, 16917866, 16921684, 16925502,
    16929322, 16933142, 16936963, 16940785, 16944608, 16948432, 16952256, 16956082,
    16959908, 16963735, 16967563, 16971392, 16975222, 16979052, 16982884, 16986716,
    16990549, 16994383, 16998218, 17002054, 17005891, 17009728, 17013567, 17017406,
    17021246, 17025087, 17028929, 17032772, 17036615, 17040460, 17044305, 17048151,
    17051999, 17055846, 17059695, 17063545, 17067396, 17071247, 17075099, 17078952,
    17082806, 17086661, 17090517, 17094374, 17098231, 17102090, 17105949, 17109809,
    17113670, 17117532, 17121394, 17125258, 17129123, 17132988, 17136854, 17140721,
    17144589, 17148458, 17152328, 17156198, 17160070, 17163942, 17167815, 17171689,
    17175564, 17179440, 17183317, 17187194, 17191073, 17194952, 17198832, 17202713,
    17206595, 17210478, 17214362, 17218247, 17222132, 17226018, 17229906, 17233794,
    17237683, 17241572, 17245463, 17249355, 17253247, 17257141, 17261035, 17264930,
    17268826, 17272723, 17276621, 17280519, 17284419, 17288319, 17292220, 17296123,
    17300026, 17303929, 17307834, 17311740, 17315646, 17319554, 17323462, 17327371,
    17331282, 17335192, 17339104, 17343017, 17346931, 17350845, 17354761, 17358677,
    17362594, 17366512, 17370431, 17374351, 17378271, 17382193, 17386115, 17390039,
    17393963, 17397888, 17401814, 17405741, 17409669, 17413597, 17417527, 17421457,
    17425389, 17429321, 17433254, 17437188, 17441123, 17445059, 17448995, 17452933,
    17456871, 17460810, 17464751, 17468692, 17472634, 17476577, 17480520, 17484465,
    17488410, 17492357, 17496304, 17500252, 17504202, 17508152, 17512102, 17516054,
    17520007, 17523960, 17527915, 17531870, 17535826, 17539783, 17543742, 17547700,
    17551660, 17555621, 17559583, 17563545, 17567508, 17571473, 17575438, 17579404,
    17583371, 17587339, 17591307, 17595277, 17599248, 17603219, 17607191, 17611165,
    17615139, 17619114, 17623090, 17627066, 17631044, 17635023, 17639002, 17642983,
    17646964, 17650946, 17654929, 17658913, 17662898, 17666884, 17670871, 17674858,
    17678847, 17682836, 17686826, 17690818, 17694810, 17698803, 17702797, 17706791,
    17710787, 17714784, 17718781, 17722780, 17726779, 17730779, 17734780, 17738782,
    17742785, 17746789, 17750794, 17754799, 17758806, 17762813, 17766822, 17770831,
    17774841, 17778852, 17782864, 17786877, 17790891, 17794906, 17798921, 17802938,
    17806955, 17810973, 17814993, 17819013, 17823034, 17827056, 17831078, 17835102,
    17839127, 17843152, 17847179, 17851206, 17855235, 17859264, 17863294, 17867325,
    17871357, 17875390, 17879423, 17883458, 17887494, 17891530, 17895567, 17899606,
    17903645, 17907685, 17911726, 17915768, 17919811, 17923855, 17927899, 17931945,
    17935992, 17940039, 17944087, 17948137, 17952187, 17956238, 17960290, 17964343,
    17968397, 17972451, 17976507, 17980563, 17984621, 17988679, 17992739, 17996799,
    18000860, 18004922, 18008985, 18013049, 18017114, 18021180, 18025246, 18029314,
    18033382, 18037452, 18041522, 18045593, 18049665, 18053738, 18057812, 18061887,
    18065963, 18070040, 18074118, 18078196, 18082276, 18086356, 18090437, 18094520,
    18098603, 18102687, 18106772, 18110858, 18114945, 18119033, 18123121, 18127211,
    18131302, 18135393, 18139486, 18143579, 18147673, 18151768, 18155865, 18159962,
    18164060, 18168158, 18172258, 18176359, 18180461, 18184563, 18188667, 18192771,
    18196877, 18200983, 18205090, 18209198, 18213307, 18217417, 18221528, 18225640,
    18229753, 18233867, 18237981, 18242097, 18246213, 18250331, 18254449, 18258568,
    18262689, 18266810, 18270932, 18275055, 18279179, 18283304, 18287429, 18291556,
    18295684, 18299812, 18303942, 18308072, 18312204, 18316336, 18320469, 18324603,
    18328739, 18332875, 18337012, 18341150, 18345288, 18349428, 18353569, 18357711,
    18361853, 18365997, 18370141, 18374287, 18378433, 18382580, 18386728, 18390877,
    18395028, 18399179, 18403330, 18407483, 18411637, 18415792, 18419948, 18424104,
    18428262, 18432420, 18436580, 18440740, 18444902, 18449064, 18453227, 18457391,
    18461556, 18465722, 18469889, 18474057, 18478226, 18482396, 18486566, 18490738,
    18494911, 18499084, 18503259, 18507434, 18511611, 18515788, 18519966, 18524145,
    18528325, 18532507, 18536689, 18540872, 18545056, 18549240, 18553426, 18557613,
    18561801, 18565989, 18570179, 18574369, 18578561, 18582753, 18586947, 18591141,
    18595336, 18599532, 18603730, 18607928, 18612127, 18616327, 18620528, 18624730,
    18628932, 18633136, 18637341, 18641547, 18645753, 18649961, 18654169, 18658379,
    18662589, 18666801, 18671013, 18675226, 18679441, 18683656, 18687872, 18692089,
    18696307, 18700526, 18704746, 18708967, 18713189, 18717412, 18721635, 18725860,
    18730086, 18734312, 18738540, 18742768, 18746998, 18751228, 18755460, 18759692,
    18763925, 18768160, 18772395, 18776631, 18780868, 18785106, 18789345, 18793585,
    18797826, 18802068, 18806311, 18810555, 18814800, 18819045, 18823292, 18827540,
    18831788, 18836038, 18840288, 18844540, 18848792, 18853046, 18857300, 18861555,
    18865812, 18870069, 18874327, 18878586, 18882846, 18887107, 18891370, 18895633,
    18899897, 18904161, 18908427, 18912694, 18916962, 18921231, 18925501, 18929771,
    18934043, 18938316, 18942589, 18946864, 18951139, 18955416, 18959693, 18963972,
    18968251, 18972531, 18976813, 18981095, 18985378, 18989663, 18993948, 18998234,
    19002521, 19006809, 19011098, 19015388, 19019679, 19023971, 19028264, 19032558,
    19036853, 19041149, 19045446, 19049743, 19054042, 19058342, 19062643, 19066944,
    19071247, 19075550, 19079855, 19084161, 19088467, 19092775, 19097083, 19101392,
    19105703, 19110014, 19114327, 19118640, 19122954, 19127270, 19131586, 19135903,
    19140221, 19144540, 19148861, 19153182, 19157504, 19161827, 19166151, 19170476,
    19174802, 19179129, 19183457, 19187786, 19192116, 19196446, 19200778, 19205111,
    19209445, 19213780, 19218116, 19222452, 19226790, 19231129, 19235468, 19239809,
    19244151, 19248493, 19252837, 19257182, 19261527, 19265874, 19270221, 19274570,
    19278919, 19283270, 19287621, 19291973, 19296327, 19300681, 19305037, 19309393,
    19313750, 19318109, 19322468, 19326828, 19331190, 19335552, 19339915, 19344279,
    19348645, 19353011, 19357378, 19361746, 19366115, 19370485, 19374857, 19379229,
    19383602, 19387976, 19392351, 19396727, 19401104, 19405482, 19409861, 19414241,
    19418622, 19423004, 19427387, 19431771, 19436156, 19440542, 19444929, 19449317,
    19453706, 19458096, 19462487, 19466878, 19471271, 19475665, 19480060, 19484456,
    19488853, 19493251, 19497649, 19502049, 19506450, 19510852, 19515255, 19519659,
    19524063, 19528469, 19532876, 19537284, 19541692, 19546102, 19550513, 19554925,
    19559337, 19563751, 19568166, 19572582, 19576998, 19581416, 19585835, 19590255,
    19594675, 19599097, 19603520, 19607943, 19612368, 19616794, 19621221, 19625648,
    19630077, 19634507, 19638937, 19643369, 19647802, 19652236, 19656670, 19661106,
    19665543, 19669980, 19674419, 19678859, 19683300, 19687741, 19692184, 19696628,
    19701072, 19705518, 19709965, 19714413, 19718861, 19723311, 19727762, 19732214,
    19736666, 19741120, 19745575, 19750031, 19754488, 19758945, 19763404, 19767864,
    19772325, 19776786, 19781249, 19785713, 19790178, 19794644, 19799111, 19803578,
    19808047, 19812517, 19816988, 19821460, 19825933, 19830407, 19834882, 19839358,
    19843835, 19848313, 19852791, 19857271, 19861752, 19866234, 19870717, 19875201,
    19879686, 19884172, 19888660, 19893148, 19897637, 19902127, 19906618, 19911110,
    19915603, 19920097, 19924592, 19929089, 19933586, 19938084, 19942583, 19947083,
    19951585, 19956087, 19960590, 19965094, 19969600, 19974106, 19978613, 19983122,
    19987631, 19992142, 19996653, 20001165, 20005679, 20010193, 20014709, 20019225,
    20023743, 20028261, 20032781, 20037302, 20041823, 20046346, 20050869, 20055394,
    20059920, 20064446, 20068974, 20073503, 20078033, 20082564, 20087095, 20091628,
    20096162, 20100697, 20105233, 20109770, 20114308, 20118847, 20123387, 20127928,
    20132470, 20137013, 20141557, 20146102, 20150648, 20155195, 20159744, 20164293,
    20168843, 20173394, 20177947, 20182500, 20187054, 20191610, 20196166, 20200724,
    20205282, 20209842, 20214402, 20218964, 20223526, 20228090, 20232655, 20237220,
    20241787, 20246355, 20250924, 20255493, 20260064, 20264636, 20269209, 20273783,
    20278358, 20282934, 20287511, 20292089, 20296668, 20301248, 20305829, 20310412,
    20314995, 20319579, 20324164, 20328751, 20333338, 20337927, 20342516, 20347107,
    20351698, 20356291, 20360884, 20365479, 20370074, 20374671, 20379269, 20383868,
    20388467, 20393068, 20397670, 20402273, 20406877, 20411482, 20416088, 20420695,
    20425303, 20429912, 20434523, 20439134, 20443746, 20448360, 20452974, 20457589,
    20462206, 20466823, 20471442, 20476061, 20480682, 20485304, 20489926, 20494550,
    20499175, 20503801, 20508428, 20513055, 20517684, 20522314, 20526945, 20531578,
    20536211, 20540845, 20545480, 20550116, 20554754, 20559392, 20564032, 20568672,
    20573313, 20577956, 20582600, 20587244, 20591890, 20596537, 20601185, 20605833,
    20610483, 20615134, 20619786, 20624439, 20629093, 20633749, 20638405, 20643062,
    20647720, 20652380, 20657040, 20661701, 20666364, 20671028, 20675692, 20680358,
    20685025, 20689692, 20694361, 20699031, 20703702, 20708374, 20713047, 20717721,
    20722396, 20727072, 20731750, 20736428, 20741107, 20745788, 20750469, 20755152,
    20759835, 20764520, 20769206, 20773892, 20778580, 20783269, 20787959, 20792650,
    20797342, 20802035, 20806729, 20811425, 20816121, 20820818, 20825517, 20830216,
    20834917, 20839618, 20844321, 20849025, 20853729, 20858435, 20863142, 20867850,
    20872559, 20877269, 20881980, 20886693, 20891406, 20896120, 20900836, 20905552,
    20910270, 20914988, 20919708, 20924429, 20929150, 20933873, 20938597, 20943322,
    20948048, 20952775, 20957504, 20962233, 20966963, 20971695, 20976427, 20981161,
    20985895, 20990631, 20995368, 21000105, 21004844, 21009584, 21014325, 21019067,
    21023810, 21028555, 21033300, 21038046, 21042794, 21047542, 21052292, 21057042,
    21061794, 21066547, 21071301, 21076056, 21080812, 21085569, 21090327, 21095086,
    21099846, 21104608, 21109370, 21114134, 21118898, 21123664, 21128431, 21133199,
    21137968, 21142738, 21147509, 21152281, 21157054, 21161828, 21166604, 21171380,
    21176158, 21180936, 21185716, 21190497, 21195278, 21200061, 21204845, 21209630,
    21214417, 21219204, 21223992, 21228781, 21233572, 21238364, 21243156, 21247950,
    21252745, 21257541, 21262338, 21267136, 21271935, 21276735, 21281536, 21286339,
    21291142, 21295947, 21300752, 21305559, 21310367, 21315176, 21319986, 21324797,
    21329609, 21334422, 21339236, 21344052, 21348868, 21353686, 21358504, 21363324,
    21368145, 21372967, 21377790, 21382614, 21387439, 21392265, 21397093, 21401921,
    21406751, 21411581, 21416413, 21421246, 21426080, 21430915, 21435751, 21440588,
    21445426, 21450266, 21455106, 21459948, 21464790, 21469634, 21474479, 21479325,
    21484172, 21489020, 21493869, 21498719, 21503571, 21508423, 21513277, 21518132,
    21522987, 21527844, 21532702, 21537561, 21542421, 21547283, 21552145, 21557008,
    21561873, 21566739, 21571605, 21576473, 21581342, 21586212, 21591083, 21595955,
    21600829, 21605703, 21610579, 21615455, 21620333, 21625212, 21630092, 21634973,
    21639855, 21644738, 21649623, 21654508, 21659395, 21664282, 21669171, 21674061,
    21678952, 21683844, 21688737, 21693631, 21698527, 21703423, 21708321, 21713219,
    21718119, 21723020, 21727922, 21732825, 21737729, 21742635, 21747541, 21752449,
    21757357, 21762267, 21767178, 21772090, 21777003, 21781917, 21786832, 21791749,
    21796666, 21801585, 21806505, 21811426, 21816348, 21821271, 21826195, 21831120,
    21836046, 21840974, 21845903, 21850832, 21855763, 21860695, 21865628, 21870562,
    21875498, 21880434, 21885372, 21890310, 21895250, 21900191, 21905133, 21910076,
    21915020, 21919965, 21924912, 21929859, 21934808, 21939758, 21944709, 21949661,
    21954614, 21959568, 21964524, 21969480, 21974438, 21979396, 21984356, 21989317,
    21994279, 21999243, 22004207, 22009172, 22014139, 22019107, 22024076, 22029045,
    22034016, 22038989, 22043962, 22048936, 22053912, 22058889, 22063866, 22068845,
    22073825, 22078807, 22083789, 22088772, 22093757, 22098742, 22103729, 22108717,
    22113706, 22118696, 22123688, 22128680, 22133674, 22138668, 22143664, 22148661,
    22153659, 22158658, 22163659, 22168660, 22173663, 22178666, 22183671, 22188677,
    22193684, 22198692, 22203702, 22208712, 22213724, 22218736, 22223750, 22228765,
    22233781, 22238799, 22243817, 22248837, 22253857, 22258879, 22263902, 22268926,
    22273951, 22278978, 22284005, 22289034, 22294063, 22299094, 22304126, 22309159,
    22314194, 22319229, 22324266, 22329303, 22334342, 22339382, 22344423, 22349465,
    22354509, 22359553, 22364599, 22369646, 22374693, 22379743, 22384793, 22389844,
    22394897, 22399950, 22405005, 22410061, 22415118, 22420176, 22425235, 22430296,
    22435357, 22440420, 22445484, 22450549, 22455615, 22460683, 22465751, 22470821,
    22475891, 22480963, 22486036, 22491111, 22496186, 22501262, 22506340, 22511419,
    22516499, 22521580, 22526662, 22531745, 22536830, 22541915, 22547002, 22552090,
    22557179, 22562269, 22567361, 22572453, 22577547, 22582642, 22587738, 22592835,
    22597933, 22603033, 22608133, 22613235, 22618338, 22623442, 22628547, 22633653,
    22638761, 22643870, 22648979, 22654090, 22659202, 22664316, 22669430, 22674546,
    22679662, 22684780, 22689899, 22695020, 22700141, 22705263, 22710387, 22715512,
    22720638, 22725765, 22730893, 22736023, 22741153, 22746285, 22751418, 22756552,
    22761687, 22766824, 22771961, 22777100, 22782240, 22787381, 22792523, 22797666,
    22802811, 22807956, 22813103, 22818251, 22823400, 22828551, 22833702, 22838855,
    22844009, 22849164, 22854320, 22859477, 22864635, 22869795, 22874956, 22880118,
    22885281, 22890445, 22895611, 22900777, 22905945, 22911114, 22916284, 22921455,
    22926628, 22931801, 22936976, 22942152, 22947329, 22952508, 22957687, 22962868,
    22968049, 22973232, 22978416, 22983602, 22988788, 22993976, 22999165, 23004355,
    23009546, 23014738, 23019932, 23025126, 23030322, 23035519, 23040717, 23045917,
    23051117, 23056319, 23061522, 23066726, 23071931, 23077137, 23082345, 23087554,
    23092764, 23097975, 23103187, 23108400, 23113615, 23118831, 23124048, 23129266,
    23134485, 23139706, 23144928, 23150150, 23155374, 23160600, 23165826, 23171054,
    23176282, 23181512, 23186743, 23191976, 23197209, 23202444, 23207680, 23212917,
    23218155, 23223394, 23228635, 23233877, 23239120, 23244364, 23249609, 23254856,
    23260103, 23265352, 23270602, 23275853, 23281106, 23286359, 23291614, 23296870,
    23302127, 23307386, 23312645, 23317906, 23323168, 23328431, 23333695, 23338961,
    23344227, 23349495, 23354764, 23360034, 23365306, 23370578, 23375852, 23381127,
    23386403, 23391681, 23396959, 23402239, 23407520, 23412802, 23418085, 23423370,
    23428656, 23433942, 23439231, 23444520, 23449810, 23455102, 23460395, 23465689,
    23470984, 23476281, 23481578, 23486877, 23492177, 23497478, 23502781, 23508084,
    23513389, 23518695, 23524002, 23529311, 23534620, 23539931, 23545243, 23550556,
    23555871, 23561186, 23566503, 23571821, 23577140, 23582461, 23587782, 23593105,
    23598429, 23603754, 23609081, 23614408, 23619737, 23625067, 23630399, 23635731,
    23641065, 23646399, 23651735, 23657073, 23662411, 23667751, 23673092, 23678434,
    23683777, 23689121, 23694467, 23699814, 23705162, 23710511, 23715862, 23721213,
    23726566, 23731921, 23737276, 23742632, 23747990, 23753349, 23758709, 23764071,
    23769433, 23774797, 23780162, 23785528, 23790896, 23796264, 23801634, 23807005,
    23812377, 23817751, 23823126, 23828502, 23833879, 23839257, 23844637, 23850017,
    23855399, 23860783, 23866167, 23871553, 23876939, 23882327, 23887717, 23893107,
    23898499, 23903892, 23909286, 23914681, 23920078, 23925476, 23930875, 23936275,
    23941676, 23947079, 23952483, 23957888, 23963294, 23968702, 23974111, 23979521,
    23984932, 23990344, 23995758, 24001173, 24006589, 24012006, 24017425, 24022844,
    24028265, 24033688, 24039111, 24044536, 24049962, 24055389, 24060817, 24066246,
    24071677, 24077109, 24082542, 24087977, 24093413, 24098850, 24104288, 24109727,
    24115168, 24120609, 24126052, 24131497, 24136942, 24142389, 24147837, 24153286,
    24158736, 24164188, 24169641, 24175095, 24180550, 24186007, 24191465, 24196924,
    24202384, 24207846, 24213308, 24218772, 24224237, 24229704, 24235172, 24240640,
    24246111, 24251582, 24257055, 24262528, 24268003, 24273480, 24278957, 24284436,
    24289916, 24295397, 24300880, 24306363, 24311848, 24317335, 24322822, 24328311,
    24333801, 24339292, 24344784, 24350278, 24355773, 24361269, 24366766, 24372265,
    24377765, 24383266, 24388768, 24394271, 24399776, 24405282, 24410790, 24416298,
    24421808, 24427319, 24432831, 24438345, 24443859, 24449375, 24454893, 24460411,
    24465931, 24471452, 24476974, 24482497, 24488022, 24493548, 24499075, 24504604,
    24510133, 24515664, 24521197, 24526730, 24532265, 24537801, 24543338, 24548876,
    24554416, 24559957, 24565499, 24571042, 24576587, 24582133, 24587680, 24593229,
    24598778, 24604329, 24609881, 24615435, 24620990, 24626546, 24632103, 24637661,
    24643221, 24648782, 24654344, 24659908, 24665472, 24671038, 24676606, 24682174,
    24687744, 24693315, 24698887, 24704461, 24710036, 24715612, 24721189, 24726767,
    24732347, 24737928, 24743511, 24749094, 24754679, 24760265, 24765853, 24771441,
    24777031, 24782622, 24788215, 24793809, 24799403, 24805000, 24810597, 24816196,
    24821796, 24827397, 24833000, 24838604, 24844209, 24849815, 24855423, 24861031,
    24866641, 24872253, 24877866, 24883479, 24889095, 24894711, 24900329, 24905948,
    24911568, 24917190, 24922812, 24928436, 24934062, 24939688, 24945316, 24950945,
    24956576, 24962208, 24967840, 24973475, 24979110, 24984747, 24990385, 24996024,
    25001665, 25007307, 25012950, 25018594, 25024240, 25029887, 25035535, 25041185,
    25046835, 25052487, 25058141, 25063795, 25069451, 25075108, 25080767, 25086427,
    25092088, 25097750, 25103413, 25109078, 25114744, 25120412, 25126080, 25131750,
    25137421, 25143094, 25148768, 25154443, 25160119, 25165797, 25171476, 25177156,
    25182837, 25188520, 25194204, 25199889, 25205576, 25211264, 25216953, 25222643,
    25228335, 25234028, 25239722, 25245418, 25251115, 25256813, 25262512, 25268213,
    25273915, 25279618, 25285323, 25291029, 25296736, 25302445, 25308154, 25313865,
    25319578, 25325291, 25331006, 25336722, 25342440, 25348158, 25353879, 25359600,
    25365322, 25371046, 25376772, 25382498, 25388226, 25393955, 25399685, 25405417,
    25411150, 25416884, 25422620, 25428357, 25434095, 25439834, 25445575, 25451317,
    25457060, 25462805, 25468551, 25474298, 25480047, 25485796, 25491548, 25497300,
    25503054, 25508809, 25514565, 25520323, 25526081, 25531842, 25537603, 25543366,
    25549130, 25554895, 25560662, 25566430, 25572199, 25577970, 25583742, 25589515,
    25595290, 25601066, 25606843, 25612621, 25618401, 25624182, 25629964, 25635748,
    25641533, 25647319, 25653107, 25658895, 25664686, 25670477, 25676270, 25682064,
    25687859, 25693656, 25699454, 25705253, 25711054, 25716856, 25722659, 25728464,
    25734270, 25740077, 25745885, 25751695, 25757506, 25763319, 25769132, 25774947,
    25780764, 25786581, 25792400, 25798221, 25804042, 25809865, 25815689, 25821515,
    25827342, 25833170, 25839000, 25844830, 25850662, 25856496, 25862331, 25868167,
    25874004, 25879843, 25885683, 25891524, 25897367, 25903211, 25909056, 25914903,
    25920751, 25926600, 25932451, 25938302, 25944156, 25950010, 25955866, 25961723,
    25967582, 25973442, 25979303, 25985165, 25991029, 25996894, 26002761, 26008628,
    26014497, 26020368, 26026240, 26032113, 26037987, 26043863, 26049740, 26055618,
    26061498, 26067379, 26073261, 26079145, 26085030, 26090916, 26096804, 26102693,
    26108583, 26114475, 26120368, 26126262, 26132158, 26138055, 26143953, 26149853,
    26155754, 26161656, 26167559, 26173464, 26179371, 26185278, 26191187, 26197098,
    26203009, 26208922, 26214836, 26220752, 26226669, 26232587, 26238507, 26244428,
    26250350, 26256274, 26262199, 26268125, 26274053, 26279982, 26285912, 26291844,
    26297777, 26303711, 26309647, 26315584, 26321522, 26327462, 26333403, 26339345,
    26345289, 26351234, 26357180, 26363128, 26369077, 26375028, 26380979, 26386933,
    26392887, 26398843, 26404800, 26410758, 26416718, 26422679, 26428642, 26434606,
    26440571, 26446538, 26452506, 26458475, 26464445, 26470417, 26476391, 26482365,
    26488341, 26494319, 26500297, 26506277, 26512259, 26518241, 26524226, 26530211,
    26536198, 26542186, 26548175, 26554166, 26560158, 26566152, 26572147, 26578143,
    26584141, 26590140, 26596140, 26602142, 26608145, 26614149, 26620155, 26626162,
    26632170, 26638180, 26644191, 26650204, 26656218, 26662233, 26668249, 26674267,
    26680287, 26686307, 26692329, 26698353, 26704377, 26710404, 26716431, 26722460,
    26728490, 26734522, 26740554, 26746589, 26752624, 26758661, 26764700, 26770739,
    26776780, 26782823, 26788867, 26794912, 26800958, 26807006, 26813055, 26819106,
    26825158, 26831211, 26837266, 26843322, 26849380, 26855438, 26861499, 26867560,
    26873623, 26879687, 26885753, 26891820, 26897888, 26903958, 26910029, 26916102,
    26922176, 26928251, 26934327, 26940405, 26946485, 26952566, 26958648, 26964731,
    26970816, 26976902, 26982990, 26989079, 26995169, 27001261, 27007354, 27013448,
    27019544, 27025641, 27031740, 27037840, 27043941, 27050044, 27056148, 27062254,
    27068360, 27074469, 27080578, 27086689, 27092802, 27098915, 27105030, 27111147,
    27117265, 27123384, 27129505, 27135627, 27141750, 27147875, 27154001, 27160129,
    27166258, 27172388, 27178520, 27184653, 27190787, 27196923, 27203060, 27209199,
    27215339, 27221480, 27227623, 27233767, 27239913, 27246060, 27252208, 27258358,
    27264509, 27270661, 27276815, 27282971, 27289127, 27295285, 27301445, 27307605,
    27313768, 27319931, 27326096, 27332263, 27338430, 27344600, 27350770, 27356942,
    27363116, 27369290, 27375466, 27381644, 27387823, 27394003, 27400185, 27406368,
    27412552, 27418738, 27424926, 27431114, 27437304, 27443496, 27449689, 27455883,
    27462079, 27468276, 27474474, 27480674, 27486875, 27493078, 27499282, 27505488,
    27511695, 27517903, 27524112, 27530324, 27536536, 27542750, 27548965, 27555182,
    27561400, 27567619, 27573840, 27580063, 27586286, 27592511, 27598738, 27604966,
    27611195, 27617426, 27623658, 27629892, 27636126, 27642363, 27648601, 27654840,
    27661080, 27667322, 27673566, 27679810, 27686057, 27692304, 27698553, 27704804,
    27711056, 27717309, 27723564, 27729820, 27736077, 27742336, 27748596, 27754858,
    27761121, 27767386, 27773652, 27779919, 27786188, 27792458, 27798730, 27805003,
    27811277, 27817553, 27823830, 27830109, 27836389, 27842671, 27848954, 27855238,
    27861524, 27867811, 27874100, 27880390, 27886681, 27892974, 27899268, 27905564,
    27911861, 27918160, 27924460, 27930761, 27937064, 27943368, 27949674, 27955981,
    27962290, 27968600, 27974911, 27981224, 27987538, 27993854, 28000171, 28006489,
    28012809, 28019131, 28025453, 28031778, 28038103, 28044430, 28050759, 28057089,
    28063420, 28069753, 28076087, 28082423, 28088760, 28095098, 28101438, 28107779,
    28114122, 28120466, 28126812, 28133159, 28139508, 28145858, 28152209, 28158562,
    28164916, 28171272, 28177629, 28183987, 28190347, 28196709, 28203072, 28209436,
    28215802, 28222169, 28228537, 28234907, 28241279, 28247652, 28254026, 28260402,
    28266779, 28273158, 28279538, 28285919, 28292302, 28298687, 28305073, 28311460,
    28317849, 28324239, 28330631, 28337024, 28343418, 28349814, 28356211, 28362610,
    28369011, 28375412, 28381816, 28388220, 28394626, 28401034, 28407443, 28413853,
    28420265, 28426678, 28433093, 28439509, 28445927, 28452346, 28458766, 28465188,
    28471612, 28478037, 28484463, 28490891, 28497320, 28503751, 28510183, 28516616,
    28523052, 28529488, 28535926, 28542365, 28548806, 28555249, 28561692, 28568137,
    28574584, 28581032, 28587482, 28593933, 28600385, 28606839, 28613295, 28619752,
    28626210, 28632670, 28639131, 28645594, 28652058, 28658523, 28664990, 28671459,
    28677929, 28684400, 28690873, 28697348, 28703823, 28710301, 28716779, 28723260,
    28729741, 28736224, 28742709, 28749195, 28755683, 28762172, 28768662, 28775154,
    28781647, 28788142, 28794639, 28801136, 28807636, 28814136, 28820638, 28827142,
    28833647, 28840154, 28846662, 28853171, 28859682, 28866195, 28872709, 28879224,
    28885741, 28892259, 28898779, 28905300, 28911823, 28918347, 28924873, 28931400,
    28937929, 28944459, 28950991, 28957524, 28964058, 28970594, 28977132, 28983671,
    28990211, 28996753, 29003296, 29009841, 29016388, 29022935, 29029485, 29036035,
    29042588, 29049141, 29055697, 29062253, 29068811, 29075371, 29081932, 29088495,
    29095059, 29101625, 29108192, 29114760, 29121330, 29127902, 29134475, 29141049,
    29147625, 29154202, 29160781, 29167362, 29173944, 29180527, 29187112, 29193698,
    29200286, 29206875, 29213466, 29220058, 29226652, 29233248, 29239844, 29246443,
    29253042, 29259643, 29266246, 29272850, 29279456, 29286063, 29292672, 29299282,
    29305894, 29312507, 29319122, 29325738, 29332355, 29338974, 29345595, 29352217,
    29358841, 29365466, 29372092, 29378720, 29385350, 29391981, 29398614, 29405248,
    29411883, 29418520, 29425159, 29431799, 29438441, 29445084, 29451728, 29458374,
    29465022, 29471671, 29478321, 29484974, 29491627, 29498282, 29504939, 29511597,
    29518256, 29524917, 29531580, 29538244, 29544910, 29551577, 29558245, 29564915,
    29571587, 29578260, 29584935, 29591611, 29598288, 29604968, 29611648, 29618330,
    29625014, 29631699, 29638386, 29645074, 29651764, 29658455, 29665148, 29671842,
    29678538, 29685235, 29691934, 29698634, 29705336, 29712039, 29718744, 29725450,
    29732158, 29738867, 29745578, 29752290, 29759004, 29765720, 29772437, 29779155,
    29785875, 29792596, 29799319, 29806044, 29812770, 29819497, 29826226, 29832957,
    29839689, 29846423, 29853158, 29859894, 29866633, 29873372, 29880113, 29886856,
    29893600, 29900346, 29907094, 29913842, 29920593, 29927345, 29934098, 29940853,
    29947609, 29954367, 29961127, 29967888, 29974650, 29981414, 29988180, 29994947,
    30001716, 30008486, 30015257, 30022031, 30028805, 30035582, 30042360, 30049139,
    30055920, 30062702, 30069486, 30076272, 30083059, 30089847, 30096637, 30103429,
    30110222, 30117016, 30123813, 30130610, 30137410, 30144210, 30151013, 30157817,
    30164622, 30171429, 30178237, 30185047, 30191859, 30198672, 30205487, 30212303,
    30219120, 30225940, 30232760, 30239583, 30246407, 30253232, 30260059, 30266887,
    30273717, 30280549, 30287382, 30294217, 30301053, 30307890, 30314730, 30321571,
    30328413, 30335257, 30342102, 30348949, 30355798, 30362648, 30369499, 30376353,
    30383207, 30390064, 30396921, 30403781, 30410642, 30417504, 30424368, 30431234,
    30438101, 30444969, 30451839, 30458711, 30465584, 30472459, 30479336, 30486214,
    30493093, 30499974, 30506857, 30513741, 30520627, 30527514, 30534403, 30541293,
    30548185, 30555079, 30561974, 30568870, 30575768, 30582668, 30589569, 30596472,
    30603377, 30610282, 30617190, 30624099, 30631010, 30637922, 30644836, 30651751,
    30658668, 30665586, 30672506, 30679428, 30686351, 30693275, 30700202, 30707129,
    30714059, 30720990, 30727922, 30734856, 30741792, 30748729, 30755668, 30762608,
    30769550, 30776493, 30783438, 30790385, 30797333, 30804283, 30811234, 30818187,
    30825141, 30832097, 30839055, 30846014, 30852975, 30859937, 30866901, 30873866,
    30880833, 30887802, 30894772, 30901743, 30908717, 30915691, 30922668, 30929646,
    30936625, 30943607, 30950589, 30957574, 30964559, 30971547, 30978536, 30985526,
    30992519, 30999512, 31006508, 31013505, 31020503, 31027503, 31034505, 31041508,
    31048513, 31055519, 31062527, 31069537, 31076548, 31083561, 31090575, 31097591,
    31104608, 31111627, 31118648, 31125670, 31132694, 31139719, 31146746, 31153775,
    31160805, 31167837, 31174870, 31181905, 31188941, 31195979, 31203019, 31210060,
    31217103, 31224148, 31231194, 31238241, 31245290, 31252341, 31259394, 31266448,
    31273503, 31280560, 31287619, 31294679, 31301741, 31308805, 31315870, 31322937,
    31330005, 31337075, 31344146, 31351220, 31358294, 31365371, 31372448, 31379528,
    31386609, 31393692, 31400776, 31407862, 31414949, 31422038, 31429129, 31436221,
    31443315, 31450411, 31457508, 31464606, 31471707, 31478809, 31485912, 31493017,
    31500124, 31507232, 31514342, 31521454, 31528567, 31535681, 31542798, 31549916,
    31557035, 31564156, 31571279, 31578403, 31585529, 31592657, 31599786, 31606917,
    31614049, 31621183, 31628319, 31635456, 31642595, 31649735, 31656877, 31664021,
    31671166, 31678313, 31685462, 31692612, 31699764, 31706917, 31714072, 31721229,
    31728387, 31735547, 31742708, 31749871, 31757036, 31764202, 31771370, 31778539,
    31785710, 31792883, 31800058, 31807234, 31814411, 31821590, 31828771, 31835954,
    31843138, 31850323, 31857511, 31864700, 31871890, 31879082, 31886276, 31893472,
    31900669, 31907867, 31915068, 31922270, 31929473, 31936678, 31943885, 31951094,
    31958304, 31965515, 31972729, 31979944, 31987160, 31994378, 32001598, 32008820,
    32016043, 32023268, 32030494, 32037722, 32044951, 32052183, 32059416, 32066650,
    32073886, 32081124, 32088363, 32095604, 32102847, 32110091, 32117337, 32124585,
    32131834, 32139085, 32146337, 32153592, 32160847, 32168105, 32175364, 32182624,
    32189887, 32197151, 32204416, 32211684, 32218952, 32226223, 32233495, 32240769,
    32248044, 32255321, 32262600, 32269880, 32277162, 32284446, 32291731, 32299018,
    32306307, 32313597, 32320889, 32328182, 32335478, 32342774, 32350073, 32357373,
    32364675, 32371978, 32379283, 32386590, 32393898, 32401208, 32408520, 32415833,
    32423148, 32430465, 32437783, 32445103, 32452424, 32459747, 32467072, 32474399,
    32481727, 32489057, 32496388, 32503721, 32511056, 32518392, 32525731, 32533070,
    32540412, 32547755, 32555099, 32562446, 32569794, 32577143, 32584495, 32591848,
    32599202, 32606559, 32613917, 32621276, 32628638, 32636001, 32643365, 32650732,
    32658099, 32665469, 32672840, 32680213, 32687588, 32694964, 32702342, 32709722,
    32717103, 32724486, 32731870, 32739257, 32746645, 32754034, 32761425, 32768818,
    32776213, 32783609, 32791007, 32798407, 32805808, 32813211, 32820615, 32828022,
    32835430, 32842839, 32850251, 32857664, 32865078, 32872495, 32879913, 32887332,
    32894754, 32902177, 32909601, 32917028, 32924456, 32931885, 32939317, 32946750,
    32954184, 32961621, 32969059, 32976499, 32983940, 32991383, 32998828, 33006275,
    33013723, 33021173, 33028624, 33036077, 33043532, 33050989, 33058447, 33065907,
    33073369, 33080832, 33088297, 33095764, 33103232, 33110702, 33118174, 33125647,
    33133122, 33140599, 33148078, 33155558, 33163040, 33170523, 33178009, 33185495,
    33192984, 33200474, 33207966, 33215460, 33222955, 33230453, 33237951, 33245452,
    33252954, 33260458, 33267963, 33275470, 33282979, 33290490, 33298002, 33305516,
    33313032, 33320549, 33328069, 33335589, 33343112, 33350636, 33358162, 33365689,
    33373219, 33380750, 33388282, 33395817, 33403353, 33410891, 33418430, 33425971,
    33433514, 33441059, 33448605, 33456153, 33463703, 33471254, 33478807, 33486362,
    33493919, 33501477, 33509037, 33516598, 33524162, 33531727, 33539293, 33546862,
    33554432
};

static uint32_t linear_slide_up(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, const uint32_t frequency, const uint32_t slide_value)
{
    const uint32_t linear_slide_div  = slide_value / 3072;
    const uint32_t linear_slide_mod  = slide_value % 3072;
    const uint32_t linear_multiplier = avctx->linear_frequency_lut ? avctx->linear_frequency_lut[linear_slide_mod] : linear_frequency_lut[linear_slide_mod];
    uint32_t new_frequency           = ((uint64_t) linear_multiplier * frequency) >> (24 - linear_slide_div);

    if (new_frequency == frequency)
        new_frequency++;

    if (new_frequency < frequency)
        new_frequency = 0xFFFFFFFF;

    return (player_channel->frequency = new_frequency);
}

static uint32_t amiga_slide_up(AVSequencerPlayerChannel *const player_channel, const uint32_t frequency, const uint32_t slide_value)
{
    uint32_t new_frequency;
    uint64_t period      = AVSEQ_SLIDE_CONST / frequency;
    const uint64_t slide = (uint64_t) slide_value << 32;

    if (period <= slide)
        period = slide + UINT64_C(0x100000000);

    period       -= slide;
    new_frequency = AVSEQ_SLIDE_CONST / period;

    if (new_frequency == frequency)
        new_frequency++;

    if (new_frequency < frequency)
        new_frequency = 0xFFFFFFFF;

    return (player_channel->frequency = new_frequency);
}

static uint32_t linear_slide_down(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, const uint32_t frequency, const uint32_t slide_value)
{
    const uint32_t linear_slide_div = slide_value / 3072;
    const uint32_t linear_slide_mod = slide_value % 3072;
    uint32_t linear_multiplier      = avctx->linear_frequency_lut ? avctx->linear_frequency_lut[-linear_slide_mod + 3072] : linear_frequency_lut[-linear_slide_mod + 3072];
    uint32_t new_frequency          = ((uint64_t) linear_multiplier * frequency) >> (25 + linear_slide_div);

    if (new_frequency == frequency)
        new_frequency--;

    if (new_frequency > frequency)
        new_frequency = 1;

    return (player_channel->frequency = new_frequency);
}

static uint32_t amiga_slide_down(AVSequencerPlayerChannel *const player_channel, const uint32_t frequency, const uint32_t slide_value)
{
    uint32_t new_frequency;
    uint64_t period      = AVSEQ_SLIDE_CONST / frequency;
    const uint64_t slide = (uint64_t) slide_value << 32;

    period += slide;

    if (period < slide)
        period = UINT64_C(0xFFFFFFFF00000000);

    new_frequency = AVSEQ_SLIDE_CONST / period;

    if (new_frequency == frequency)
        new_frequency--;

    if (new_frequency > frequency)
        new_frequency = 1;

    return (player_channel->frequency = new_frequency);
}

/** Note frequency lookup table. Value is 16777216*2^(x/12), where x=0
   equals note C-4.  */
static const uint32_t pitch_lut[] = {
    0x00F1A1BF, // B-3
    0x01000000, // C-4
    0x010F38F9, // C#4
    0x011F59AC, // D-4
    0x01306FE1, // D#4
    0x01428A30, // E-4
    0x0155B811, // F-4
    0x016A09E6, // F#4
    0x017F910D, // G-4
    0x01965FEA, // G#4
    0x01AE89FA, // A-4
    0x01C823E0, // A#4
    0x01E3437E, // B-4
    0x02000000  // C-5
};

static uint32_t get_tone_pitch(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, int16_t note)
{
    const AVSequencerSample *const sample = player_host_channel->sample;
    const uint32_t *frequency_lut;
    uint32_t frequency, next_frequency;
    uint16_t octave = note / 12;
    int8_t finetune;

    note %= 12;

    if (note < 0) {
        octave--;
        note += 12;
    }

    if ((finetune = player_host_channel->finetune) < 0) {
        note--;
        finetune += -0x80;
    }

    frequency_lut  = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
    frequency      = *frequency_lut++;
    next_frequency = *frequency_lut - frequency;
    frequency     += ((int32_t) finetune * (int32_t) next_frequency) >> 7;
    return ((uint64_t) frequency * sample->rate) >> ((24+4) - octave);
}

static void portamento_slide_up(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint32_t data_word, const uint32_t carry_add, const uint32_t portamento_shift, const uint16_t channel)
{
    if (player_channel->host_channel == channel) {
        uint32_t portamento_slide_value;
        const uint8_t portamento_sub_slide_value = data_word;

        if ((portamento_slide_value = ((data_word & 0xFFFFFF00) >> portamento_shift))) {
            player_host_channel->sub_slide += portamento_sub_slide_value;

            if (player_host_channel->sub_slide < portamento_sub_slide_value)
                portamento_slide_value += carry_add;

            if (player_channel->frequency) {
                if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
                    linear_slide_up(avctx, player_channel, player_channel->frequency, portamento_slide_value);
                else
                    amiga_slide_up(player_channel, player_channel->frequency, portamento_slide_value);
            }
        }
    }
}

static void portamento_slide_down(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *player_channel, const uint32_t data_word, const uint32_t carry_add, const uint32_t portamento_shift, const uint16_t channel)
{
    if (player_channel->host_channel == channel) {
        uint32_t portamento_slide_value;
        const uint8_t portamento_sub_slide_value = data_word;

        if ((int32_t) data_word < 0) {
            portamento_slide_up(avctx, player_host_channel, player_channel, -data_word, carry_add, portamento_shift, channel);

            return;
        }

        if ((portamento_slide_value = ((data_word & 0xFFFFFF00) >> portamento_shift))) {
            if (player_host_channel->sub_slide < portamento_sub_slide_value) {
                portamento_slide_value += carry_add;

                if (portamento_slide_value < carry_add)
                    portamento_slide_value = -1;
            }

            player_host_channel->sub_slide -= portamento_sub_slide_value;

            if (player_channel->frequency) {
                if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
                    linear_slide_down(avctx, player_channel, player_channel->frequency, portamento_slide_value);
                else
                    amiga_slide_down(player_channel, player_channel->frequency, portamento_slide_value);
            }
        }
    }
}

static void portamento_up_ok(AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    const AVSequencerTrack *const track = player_host_channel->track;
    uint16_t v0, v1, v3, v4, v5, v8;

    v0 = player_host_channel->fine_porta_up;
    v1 = player_host_channel->fine_porta_down;
    v3 = player_host_channel->porta_up_once;
    v4 = player_host_channel->porta_down_once;
    v5 = player_host_channel->fine_porta_up_once;
    v8 = player_host_channel->fine_porta_down_once;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
        v0 = data_word;
        v3 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) {
        player_host_channel->porta_down = data_word;
        v1 = v0;
        v4 = v3;
        v8 = v5;
    }

    player_host_channel->porta_up             = data_word;
    player_host_channel->fine_porta_up        = v0;
    player_host_channel->fine_porta_down      = v1;
    player_host_channel->porta_up_once        = v3;
    player_host_channel->porta_down_once      = v4;
    player_host_channel->fine_porta_up_once   = v5;
    player_host_channel->fine_porta_down_once = v8;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
        v1 = player_host_channel->fine_tone_porta;
        v4 = player_host_channel->tone_porta_once;
        v8 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v1 = v0;
            v4 = v3;
            v8 = v5;
        }

        player_host_channel->tone_porta           = data_word;
        player_host_channel->fine_tone_porta      = v1;
        player_host_channel->tone_porta_once      = v4;
        player_host_channel->fine_tone_porta_once = v8;
    }
}

static void portamento_down_ok(AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    const AVSequencerTrack *const track = player_host_channel->track;
    uint16_t v0, v1, v3, v4, v5, v8;

    v0 = player_host_channel->fine_porta_up;
    v1 = player_host_channel->fine_porta_down;
    v3 = player_host_channel->porta_up_once;
    v4 = player_host_channel->porta_down_once;
    v5 = player_host_channel->fine_porta_up_once;
    v8 = player_host_channel->fine_porta_down_once;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
        v1 = data_word;
        v4 = data_word;
        v8 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) {
        player_host_channel->porta_up = data_word;
        v0 = v1;
        v3 = v4;
        v5 = v8;
    }

    player_host_channel->porta_down           = data_word;
    player_host_channel->fine_porta_up        = v0;
    player_host_channel->fine_porta_down      = v1;
    player_host_channel->porta_up_once        = v3;
    player_host_channel->porta_down_once      = v4;
    player_host_channel->fine_porta_up_once   = v5;
    player_host_channel->fine_porta_down_once = v8;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
        v0 = player_host_channel->fine_tone_porta;
        v3 = player_host_channel->tone_porta_once;
        v5 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v0 = v1;
            v3 = v4;
            v5 = v8;
        }

        player_host_channel->tone_porta           = data_word;
        player_host_channel->fine_tone_porta      = v0;
        player_host_channel->tone_porta_once      = v3;
        player_host_channel->fine_tone_porta_once = v5;
    }
}

static void portamento_up_once_ok(AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    const AVSequencerTrack *const track = player_host_channel->track;
    uint16_t v0, v1, v3, v4, v5, v8;

    v0 = player_host_channel->porta_up;
    v1 = player_host_channel->porta_down;
    v3 = player_host_channel->fine_porta_up;
    v4 = player_host_channel->fine_porta_down;
    v5 = player_host_channel->fine_porta_up_once;
    v8 = player_host_channel->fine_porta_down_once;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
        v0 = data_word;
        v3 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) {
        player_host_channel->porta_down_once = data_word;
        v1 = v0;
        v4 = v3;
        v8 = v5;
    }

    player_host_channel->porta_up_once        = data_word;
    player_host_channel->porta_up             = v0;
    player_host_channel->porta_down           = v1;
    player_host_channel->fine_porta_up        = v3;
    player_host_channel->fine_porta_down      = v4;
    player_host_channel->fine_porta_up_once   = v5;
    player_host_channel->fine_porta_down_once = v8;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
        v1 = player_host_channel->tone_porta;
        v4 = player_host_channel->fine_tone_porta;
        v8 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v1 = v0;
            v4 = v3;
            v8 = v5;
        }

        player_host_channel->tone_porta           = v1;
        player_host_channel->fine_tone_porta      = v4;
        player_host_channel->tone_porta_once      = data_word;
        player_host_channel->fine_tone_porta_once = v8;
    }
}

static void portamento_down_once_ok(AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    const AVSequencerTrack *const track = player_host_channel->track;
    uint16_t v0, v1, v3, v4, v5, v8;

    v0 = player_host_channel->porta_up;
    v1 = player_host_channel->porta_down;
    v3 = player_host_channel->fine_porta_up;
    v4 = player_host_channel->fine_porta_down;
    v5 = player_host_channel->fine_porta_up_once;
    v8 = player_host_channel->fine_porta_down_once;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
        v1 = data_word;
        v4 = data_word;
        v8 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) {
        player_host_channel->porta_up_once = data_word;
        v0 = v1;
        v3 = v4;
        v5 = v8;
    }

    player_host_channel->porta_down_once      = data_word;
    player_host_channel->porta_up             = v0;
    player_host_channel->porta_down           = v1;
    player_host_channel->fine_porta_up        = v3;
    player_host_channel->fine_porta_down      = v4;
    player_host_channel->fine_porta_up_once   = v5;
    player_host_channel->fine_porta_down_once = v8;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
        v0 = player_host_channel->tone_porta;
        v3 = player_host_channel->fine_tone_porta;
        v5 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v0 = v1;
            v3 = v4;
            v5 = v8;
        }

        player_host_channel->tone_porta           = v0;
        player_host_channel->fine_tone_porta      = v3;
        player_host_channel->tone_porta_once      = data_word;
        player_host_channel->fine_tone_porta_once = v5;
    }
}

static void do_vibrato(AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel, const uint16_t vibrato_rate, int16_t vibrato_depth)
{
    int32_t vibrato_slide_value;

    if (!vibrato_depth)
        vibrato_depth = player_host_channel->vibrato_depth;

    player_host_channel->vibrato_depth = vibrato_depth;

    vibrato_slide_value = ((-(int32_t) vibrato_depth * run_envelope(avctx, &player_host_channel->vibrato_env, vibrato_rate, 0)) >> (7 - 2)) << 8;

    if (player_channel->host_channel == channel) {
        const uint32_t old_frequency = player_channel->frequency;

        player_channel->frequency -= player_host_channel->vibrato_slide;

        portamento_slide_down(avctx, player_host_channel, player_channel, vibrato_slide_value, 1, 8, channel);

        player_host_channel->vibrato_slide -= old_frequency - player_channel->frequency;
    }
}

static uint32_t check_old_volume(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, uint16_t *const data_word, const uint16_t channel)
{
    const AVSequencerSong *song;

    if (channel != player_channel->host_channel)
        return 0;

    song = avctx->player_song;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (*data_word < 0x4000)
            *data_word = ((*data_word & 0xFF00) << 2) | (*data_word & 0xFF);
        else
            *data_word = 0xFFFF;
    }

    return 1;
}

static void do_volume_slide(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, uint16_t data_word, const uint16_t channel)
{
    if (check_old_volume(avctx, player_channel, &data_word, channel)) {
        uint16_t slide_volume = (player_channel->volume << 8U) + player_channel->sub_volume;

        if ((slide_volume += data_word) < data_word)
            slide_volume = 0xFFFF;

        player_channel->volume     = slide_volume >> 8;
        player_channel->sub_volume = slide_volume;
    }
}

static void do_volume_slide_down(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, uint16_t data_word, const uint16_t channel)
{
    if (check_old_volume(avctx, player_channel, &data_word, channel)) {
        uint16_t slide_volume = (player_channel->volume << 8) + player_channel->sub_volume;

        if (slide_volume < data_word)
            data_word = slide_volume;

        slide_volume -= data_word;

        player_channel->volume     = slide_volume >> 8;
        player_channel->sub_volume = slide_volume;
    }
}

static void volume_slide_up_ok(AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    const AVSequencerTrack *const track = player_host_channel->track;
    uint16_t v3, v4, v5;

    v3 = player_host_channel->vol_slide_down;
    v4 = player_host_channel->fine_vol_slide_up;
    v5 = player_host_channel->fine_vol_slide_down;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v4 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v3 = data_word;
        v5 = v4;
    }

    player_host_channel->vol_slide_up        = data_word;
    player_host_channel->vol_slide_down      = v3;
    player_host_channel->fine_vol_slide_up   = v4;
    player_host_channel->fine_vol_slide_down = v5;
}

static void volume_slide_down_ok(AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    const AVSequencerTrack *const track = player_host_channel->track;
    uint16_t v0, v3, v4;

    v0 = player_host_channel->vol_slide_up;
    v3 = player_host_channel->fine_vol_slide_up;
    v4 = player_host_channel->fine_vol_slide_down;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v4 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = data_word;
        v3 = v4;
    }

    player_host_channel->vol_slide_up        = v0;
    player_host_channel->vol_slide_down      = data_word;
    player_host_channel->fine_vol_slide_up   = v3;
    player_host_channel->fine_vol_slide_down = v4;
}

static void fine_volume_slide_up_ok(AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    const AVSequencerTrack *const track = player_host_channel->track;
    uint16_t v0, v1, v4;

    v0 = player_host_channel->vol_slide_up;
    v1 = player_host_channel->vol_slide_down;
    v4 = player_host_channel->fine_vol_slide_down;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v0 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v1 = v0;
        v4 = data_word;
    }

    player_host_channel->vol_slide_up        = v0;
    player_host_channel->vol_slide_down      = v1;
    player_host_channel->fine_vol_slide_up   = data_word;
    player_host_channel->fine_vol_slide_down = v4;
}

static void fine_volume_slide_down_ok(AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    const AVSequencerTrack *const track = player_host_channel->track;
    uint16_t v0, v1, v3;

    v0 = player_host_channel->vol_slide_up;
    v1 = player_host_channel->vol_slide_down;
    v3 = player_host_channel->fine_vol_slide_up;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v1 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = v1;
        v3 = data_word;
    }

    player_host_channel->vol_slide_up        = v0;
    player_host_channel->vol_slide_down      = v1;
    player_host_channel->fine_vol_slide_up   = data_word;
    player_host_channel->fine_vol_slide_down = v3;
}

static uint32_t check_old_track_volume(const AVSequencerContext *const avctx, uint16_t *data_word)
{
    const AVSequencerSong *const song = avctx->player_song;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (*data_word < 0x4000)
            *data_word = ((*data_word & 0xFF00) << 2) | (*data_word & 0xFF);
        else
            *data_word = 0xFFFF;
    }

    return 1;
}

static void do_track_volume_slide(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    if (check_old_track_volume(avctx, &data_word)) {
        uint16_t track_volume = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_volume;

        if ((track_volume += data_word) < data_word)
            track_volume = 0xFFFF;

        player_host_channel->track_volume     = track_volume >> 8;
        player_host_channel->track_sub_volume = track_volume;
    }
}

static void do_track_volume_slide_down(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    if (check_old_track_volume(avctx, &data_word)) {
        uint16_t track_volume = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_volume;

        if (track_volume < data_word)
            data_word = track_volume;

        track_volume                         -= data_word;
        player_host_channel->track_volume     = track_volume >> 8;
        player_host_channel->track_sub_volume = track_volume;
    }
}

static void do_panning_slide(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, uint16_t data_word, const uint16_t channel)
{
    if (player_channel->host_channel == channel) {
        uint16_t panning = ((uint8_t) player_channel->panning << 8) + player_channel->sub_panning;

        if (panning < data_word)
            data_word = panning;

        panning -= data_word;

        player_host_channel->track_panning     = player_channel->panning     = panning >> 8;
        player_host_channel->track_sub_panning = player_channel->sub_panning = panning;
    } else {
        uint16_t track_panning = ((uint8_t) player_host_channel->track_panning << 8) + player_host_channel->track_sub_panning;

        if (track_panning < data_word)
            data_word = track_panning;

        track_panning                         -= data_word;
        player_host_channel->track_panning     = track_panning >> 8;
        player_host_channel->track_sub_panning = track_panning;
    }

    player_host_channel->track_note_panning     = player_host_channel->track_panning;
    player_host_channel->track_note_sub_panning = player_host_channel->track_sub_panning;
}

static void do_panning_slide_right(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t data_word, const uint16_t channel)
{
    if (player_channel->host_channel == channel) {
        uint16_t panning = ((uint8_t) player_channel->panning << 8) + player_channel->sub_panning;

        if ((panning += data_word) < data_word)
            panning = 0xFFFF;

        player_host_channel->track_panning     = player_channel->panning     = panning >> 8;
        player_host_channel->track_sub_panning = player_channel->sub_panning = panning;
    } else {
        uint16_t track_panning = ((uint8_t) player_host_channel->track_panning << 8) + player_host_channel->track_sub_panning;

        if ((track_panning += data_word) < data_word)
            track_panning = 0xFFFF;

        player_host_channel->track_panning     = track_panning >> 8;
        player_host_channel->track_sub_panning = track_panning;
    }

    player_host_channel->track_note_panning     = player_host_channel->track_panning;
    player_host_channel->track_note_sub_panning = player_host_channel->track_sub_panning;
}

static void do_track_panning_slide(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    uint16_t channel_panning = ((uint8_t) player_host_channel->channel_panning << 8) + player_host_channel->channel_sub_panning;

    if (channel_panning < data_word)
        data_word = channel_panning;

    channel_panning -= data_word;

    player_host_channel->channel_panning     = channel_panning >> 8;
    player_host_channel->channel_sub_panning = channel_panning;
}

static void do_track_panning_slide_right(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, uint16_t data_word)
{
    uint16_t channel_panning = ((uint8_t) player_host_channel->channel_panning << 8) + player_host_channel->channel_sub_panning;

    if ((channel_panning += data_word) < data_word)
        channel_panning = 0xFFFF;

    player_host_channel->channel_panning     = channel_panning >> 8;
    player_host_channel->channel_sub_panning = channel_panning;
}

static uint32_t check_surround_track_panning(AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel, const uint8_t channel_ctrl_byte)
{
    if (player_channel->host_channel == channel) {
        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN)
            return 1;

        if (channel_ctrl_byte)
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
        else
            player_channel->flags &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
    } else if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN) {
        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;

        if (channel_ctrl_byte)
            player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
    }

    return 0;
}

static uint16_t *get_speed_address(const AVSequencerContext *const avctx, const uint16_t speed_type, uint16_t *const speed_min_value, uint16_t *const speed_max_value)
{
    const AVSequencerSong *const song        = avctx->player_song;
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    uint16_t *speed_adr;

    switch (speed_type & 0x07) {
    case 0x00 :
        *speed_min_value = song->bpm_speed_min;
        *speed_max_value = song->bpm_speed_max;
        speed_adr        = (uint16_t *) &player_globals->bpm_speed;

        break;
    case 0x01 :
        *speed_min_value = song->bpm_tempo_min;
        *speed_max_value = song->bpm_tempo_max;
        speed_adr        = (uint16_t *) &player_globals->bpm_tempo;

        break;
    case 0x02 :
        *speed_min_value = song->spd_min;
        *speed_max_value = song->spd_max;
        speed_adr        = (uint16_t *) &player_globals->spd_speed;

        break;
    case 0x07 :
        *speed_min_value = 1;
        *speed_max_value = 0xFFFF;
        speed_adr        = (uint16_t *) &player_globals->speed_mul;

        break;
    default :
        *speed_min_value = 0;
        *speed_max_value = 0;
        speed_adr        = NULL;

        break;
    }

    return speed_adr;
}

/** Old SoundTracker tempo definition table.  */
static const uint32_t old_st_lut[] = {
    192345259,  96192529,  64123930,  48096264,  38475419,
     32061964,  27482767,  24048132,  21687744,  19240098
};

static void speed_val_ok(const AVSequencerContext *const avctx, uint16_t *const speed_adr, uint16_t speed_value, const uint8_t speed_type, const uint16_t speed_min_value, const uint16_t speed_max_value)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;

    if (speed_value < speed_min_value)
        speed_value = speed_min_value;

    if (speed_value > speed_max_value)
        speed_value = speed_max_value;

    if ((speed_type & 0x07) == 0x07) {
        player_globals->speed_mul = speed_value >> 8;
        player_globals->speed_div = speed_value;
    } else {
        *speed_adr = speed_value;
    }

    if (!((player_globals->speed_type = speed_type) & 0x08)) {
        AVMixerData *mixer = avctx->player_mixer_data;
        uint64_t tempo = 0;
        uint8_t speed_multiplier;

        switch (speed_type & 0x07) {
        case 0x00 :
            player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SPD_TIMING;

            break;
        case 0x02 :
            player_globals->flags |= AVSEQ_PLAYER_GLOBALS_FLAG_SPD_TIMING;

            break;
        }

        if (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_SPD_TIMING) {
            if (player_globals->spd_speed > 10) {
                tempo = (uint32_t) 989156 * player_globals->spd_speed;

                if ((speed_multiplier = player_globals->speed_mul))
                    tempo *= speed_multiplier;

                if ((speed_multiplier = player_globals->speed_div))
                    tempo /= speed_multiplier;
            } else {
                tempo = (avctx->old_st_lut ? avctx->old_st_lut[player_globals->spd_speed] : old_st_lut[player_globals->spd_speed]);
            }
        } else {
            tempo = (player_globals->bpm_speed) * (player_globals->bpm_tempo) << 16;

            if ((speed_multiplier = player_globals->speed_mul))
                tempo *= speed_multiplier;

            if ((speed_multiplier = player_globals->speed_div))
                tempo /= speed_multiplier;
        }

        player_globals->tempo = tempo;
        tempo                *= player_globals->relative_speed;
        tempo               >>= 16;

        if (mixer->mixctx->set_tempo)
            mixer->mixctx->set_tempo(mixer, tempo);
    }
}

static void do_speed_slide(const AVSequencerContext *const avctx, uint16_t data_word)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    uint16_t *speed_ptr;
    uint16_t speed_min_value, speed_max_value;

    if ((speed_ptr = get_speed_address(avctx, player_globals->speed_type, &speed_min_value, &speed_max_value))) {
        uint16_t speed_value;

        if ((player_globals->speed_type & 0x07) == 0x07)
            speed_value = (player_globals->speed_mul << 8) + player_globals->speed_div;
        else
            speed_value = *speed_ptr;

        if ((speed_value += data_word) < data_word)
            speed_value = 0xFFFF;

        speed_val_ok(avctx, speed_ptr, speed_value, player_globals->speed_type, speed_min_value, speed_max_value);
    }
}

static void do_speed_slide_slower(const AVSequencerContext *const avctx, uint16_t data_word)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    uint16_t *speed_ptr;
    uint16_t speed_min_value, speed_max_value;

    if ((speed_ptr = get_speed_address(avctx, player_globals->speed_type, &speed_min_value, &speed_max_value))) {
        uint16_t speed_value;

        if ((player_globals->speed_type & 0x07) == 0x07)
            speed_value = (player_globals->speed_mul << 8) + player_globals->speed_div;
        else
            speed_value = *speed_ptr;

        if (speed_value < data_word)
            data_word = speed_value;

        speed_value -= data_word;

        speed_val_ok(avctx, speed_ptr, speed_value, player_globals->speed_type, speed_min_value, speed_max_value);
    }
}

static void do_global_volume_slide(const AVSequencerContext *const avctx, AVSequencerPlayerGlobals *player_globals, uint16_t data_word)
{
    if (check_old_track_volume(avctx, &data_word)) {
        uint16_t global_volume = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

        if ((global_volume += data_word) < data_word)
            global_volume = 0xFFFF;

        player_globals->global_volume     = global_volume >> 8;
        player_globals->global_sub_volume = global_volume;
    }
}

static void do_global_volume_slide_down(const AVSequencerContext *const avctx, AVSequencerPlayerGlobals *player_globals, uint16_t data_word)
{
    if (check_old_track_volume(avctx, &data_word)) {
        uint16_t global_volume = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

        if (global_volume < data_word)
            data_word = global_volume;

        global_volume -= data_word;

        player_globals->global_volume     = global_volume >> 8;
        player_globals->global_sub_volume = global_volume;
    }
}

static void do_global_panning_slide(AVSequencerPlayerGlobals *player_globals, uint16_t data_word)
{
    uint16_t global_panning = ((uint8_t) player_globals->global_panning << 8) + player_globals->global_sub_panning;

    player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;

    if ((global_panning += data_word) < data_word)
        global_panning = 0xFFFF;

    player_globals->global_panning     = global_panning >> 8;
    player_globals->global_sub_panning = global_panning;
}

static void do_global_panning_slide_right(AVSequencerPlayerGlobals *player_globals, uint16_t data_word)
{
    uint16_t global_panning = ((uint8_t) player_globals->global_panning << 8) + player_globals->global_sub_panning;

    player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;

    if (global_panning < data_word)
        data_word = global_panning;

    global_panning                    -= data_word;
    player_globals->global_panning     = global_panning >> 8;
    player_globals->global_sub_panning = global_panning;
}

#define EXECUTE_EFFECT(fx_type)                                                     \
    static void fx_type(AVSequencerContext *const avctx,                            \
                        AVSequencerPlayerHostChannel *const player_host_channel,    \
                        AVSequencerPlayerChannel *const player_channel,             \
                        const uint16_t channel,                                     \
                        const unsigned fx_byte,                                     \
                        uint16_t data_word)

EXECUTE_EFFECT(arpeggio)
{
    int8_t first_arpeggio, second_arpeggio;
    int16_t arpeggio_value;

    if (!data_word)
        data_word = (player_host_channel->arpeggio_first << 8) + player_host_channel->arpeggio_second;

    player_host_channel->arpeggio_first  = first_arpeggio  = data_word >> 8;
    player_host_channel->arpeggio_second = second_arpeggio = data_word;

    switch (player_host_channel->arpeggio_tick) {
    case 0 :
        arpeggio_value = 0;

        break;
    case 1 :
        arpeggio_value = first_arpeggio;

        break;
    default :
        arpeggio_value = second_arpeggio;

        player_host_channel->arpeggio_tick -= 3;

        break;
    }

    if (player_channel->host_channel == channel) {
        uint32_t frequency, arpeggio_freq, old_frequency;
        uint16_t octave;
        int16_t note;

        octave = arpeggio_value / 12;
        note   = arpeggio_value % 12;

        if (note < 0) {
            octave--;
            note += 12;
        }

        old_frequency                       = player_channel->frequency;
        frequency                           = old_frequency + player_host_channel->arpeggio_freq;
        arpeggio_freq                       = (avctx->frequency_lut ? avctx->frequency_lut[note + 1] : pitch_lut[note + 1]);
        arpeggio_freq                       = ((uint64_t) frequency * arpeggio_freq) >> (24 - octave);
        player_host_channel->arpeggio_freq += old_frequency - arpeggio_freq;
        player_channel->frequency           = arpeggio_freq;
    }

    player_host_channel->arpeggio_tick++;
}

EXECUTE_EFFECT(portamento_up)
{
    const AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->porta_up;

    portamento_slide_up(avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        volume_slide_up_ok(player_host_channel, data_word);

    portamento_up_ok(player_host_channel, data_word);
}

EXECUTE_EFFECT(portamento_down)
{
    const AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->porta_down;

    portamento_slide_down(avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        volume_slide_down_ok(player_host_channel, data_word);

    portamento_down_ok(player_host_channel, data_word);
}

EXECUTE_EFFECT(fine_portamento_up)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->fine_porta_up;

    portamento_slide_up(avctx, player_host_channel, player_channel, data_word, 1, 8, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        volume_slide_up_ok(player_host_channel, data_word);

    v0 = player_host_channel->porta_up;
    v1 = player_host_channel->porta_down;
    v3 = player_host_channel->porta_up_once;
    v4 = player_host_channel->porta_down_once;
    v5 = player_host_channel->fine_porta_up_once;
    v8 = player_host_channel->fine_porta_down_once;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
        v0 = data_word;
        v3 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) {
        player_host_channel->fine_porta_down = data_word;
        v1 = v0;
        v4 = v3;
        v8 = v5;
    }

    player_host_channel->porta_up             = v0;
    player_host_channel->porta_down           = v1;
    player_host_channel->fine_porta_up        = data_word;
    player_host_channel->porta_up_once        = v3;
    player_host_channel->porta_down_once      = v4;
    player_host_channel->fine_porta_up_once   = v5;
    player_host_channel->fine_porta_down_once = v8;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
        v1 = player_host_channel->tone_porta;
        v4 = player_host_channel->tone_porta_once;
        v8 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v1 = v0;
            v4 = v3;
            v8 = v5;
        }

        player_host_channel->tone_porta           = v1;
        player_host_channel->fine_tone_porta      = data_word;
        player_host_channel->tone_porta_once      = v4;
        player_host_channel->fine_tone_porta_once = v8;
    }
}

EXECUTE_EFFECT(fine_portamento_down)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->fine_porta_down;

    portamento_slide_down(avctx, player_host_channel, player_channel, data_word, 1, 8, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        volume_slide_down_ok(player_host_channel, data_word);

    v0 = player_host_channel->porta_up;
    v1 = player_host_channel->porta_down;
    v3 = player_host_channel->porta_up_once;
    v4 = player_host_channel->porta_down_once;
    v5 = player_host_channel->fine_porta_up_once;
    v8 = player_host_channel->fine_porta_down_once;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
        v1 = data_word;
        v4 = data_word;
        v8 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) {
        player_host_channel->fine_porta_up = data_word;
        v0 = v1;
        v3 = v4;
        v5 = v8;
    }

    player_host_channel->porta_up             = v0;
    player_host_channel->porta_down           = v1;
    player_host_channel->fine_porta_down      = data_word;
    player_host_channel->porta_up_once        = v3;
    player_host_channel->porta_down_once      = v4;
    player_host_channel->fine_porta_up_once   = v5;
    player_host_channel->fine_porta_down_once = v8;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
        v0 = player_host_channel->tone_porta;
        v3 = player_host_channel->tone_porta_once;
        v5 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v0 = v1;
            v3 = v4;
            v5 = v8;
        }

        player_host_channel->tone_porta           = v0;
        player_host_channel->fine_tone_porta      = data_word;
        player_host_channel->tone_porta_once      = v3;
        player_host_channel->fine_tone_porta_once = v5;
    }
}

EXECUTE_EFFECT(portamento_up_once)
{
    const AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->porta_up_once;

    portamento_slide_up(avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        fine_volume_slide_up_ok(player_host_channel, data_word);

    portamento_up_once_ok(player_host_channel, data_word);
}

EXECUTE_EFFECT(portamento_down_once)
{
    const AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->porta_down_once;

    portamento_slide_down(avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        fine_volume_slide_down_ok(player_host_channel, data_word);

    portamento_down_once_ok(player_host_channel, data_word);
}

EXECUTE_EFFECT(fine_portamento_up_once)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->fine_porta_up_once;

    portamento_slide_up(avctx, player_host_channel, player_channel, data_word, 1, 8, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        fine_volume_slide_up_ok(player_host_channel, data_word);

    v0 = player_host_channel->porta_up;
    v1 = player_host_channel->porta_down;
    v3 = player_host_channel->fine_porta_up;
    v4 = player_host_channel->fine_porta_down;
    v5 = player_host_channel->porta_up_once;
    v8 = player_host_channel->porta_down_once;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
        v0 = data_word;
        v3 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) {
        player_host_channel->fine_porta_down_once = data_word;
        v1 = v0;
        v4 = v3;
        v8 = v5;
    }

    player_host_channel->fine_porta_up_once = data_word;
    player_host_channel->porta_up           = v0;
    player_host_channel->porta_down         = v1;
    player_host_channel->fine_porta_up      = v3;
    player_host_channel->fine_porta_down    = v4;
    player_host_channel->porta_up_once      = v5;
    player_host_channel->porta_down_once    = v8;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
        v1 = player_host_channel->tone_porta;
        v4 = player_host_channel->fine_tone_porta;
        v8 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v1 = v0;
            v4 = v3;
            v8 = v5;
        }

        player_host_channel->tone_porta           = v1;
        player_host_channel->fine_tone_porta      = v4;
        player_host_channel->tone_porta_once      = v8;
        player_host_channel->fine_tone_porta_once = data_word;
    }
}

EXECUTE_EFFECT(fine_portamento_down_once)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->fine_porta_down_once;

    portamento_slide_down(avctx, player_host_channel, player_channel, data_word, 1, 8, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        fine_volume_slide_down_ok(player_host_channel, data_word);

    v0 = player_host_channel->porta_up;
    v1 = player_host_channel->porta_down;
    v3 = player_host_channel->fine_porta_up;
    v4 = player_host_channel->fine_porta_down;
    v5 = player_host_channel->porta_up_once;
    v8 = player_host_channel->porta_down_once;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
        v0 = data_word;
        v3 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) {
        player_host_channel->fine_porta_up_once = data_word;
        v1 = v0;
        v4 = v3;
        v8 = v5;
    }

    player_host_channel->fine_porta_down_once = data_word;
    player_host_channel->porta_up             = v0;
    player_host_channel->porta_down           = v1;
    player_host_channel->fine_porta_up        = v3;
    player_host_channel->fine_porta_down      = v4;
    player_host_channel->porta_up_once        = v5;
    player_host_channel->porta_down_once      = v8;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
        v0 = player_host_channel->tone_porta;
        v3 = player_host_channel->fine_tone_porta;
        v5 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v0 = v1;
            v3 = v4;
            v5 = v8;
        }

        player_host_channel->tone_porta           = v0;
        player_host_channel->fine_tone_porta      = v3;
        player_host_channel->tone_porta_once      = v5;
        player_host_channel->fine_tone_porta_once = data_word;
    }
}

EXECUTE_EFFECT(tone_portamento)
{
    uint32_t tone_portamento_target_pitch;

    if (!data_word)
        data_word = player_host_channel->tone_porta;

    if ((tone_portamento_target_pitch = player_host_channel->tone_porta_target_pitch)) {
        const AVSequencerTrack *track = player_host_channel->track;
        uint16_t v0, v1, v3;

        if (player_channel->host_channel == channel) {
            if (tone_portamento_target_pitch <= player_channel->frequency) {
                portamento_slide_down(avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel);

                if (tone_portamento_target_pitch >= player_channel->frequency) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            } else {
                portamento_slide_up(avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel);

                if (!player_channel->frequency || (tone_portamento_target_pitch <= player_channel->frequency)) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            }
        }

        v0 = player_host_channel->fine_tone_porta;
        v1 = player_host_channel->tone_porta_once;
        v3 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v0 = data_word;
            v1 = data_word;
            v3 = data_word;
        }

        player_host_channel->tone_porta           = data_word;
        player_host_channel->fine_tone_porta      = v0;
        player_host_channel->tone_porta_once      = v1;
        player_host_channel->fine_tone_porta_once = v3;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
            player_host_channel->porta_up             = data_word;
            player_host_channel->porta_down           = data_word;
            player_host_channel->fine_porta_up        = data_word;
            player_host_channel->fine_porta_down      = data_word;
            player_host_channel->porta_up_once        = data_word;
            player_host_channel->porta_down_once      = data_word;
            player_host_channel->fine_porta_up_once   = data_word;
            player_host_channel->fine_porta_down_once = data_word;
        }
    }
}

EXECUTE_EFFECT(fine_tone_portamento)
{
    uint32_t tone_portamento_target_pitch;

    if (!data_word)
        data_word = player_host_channel->fine_tone_porta;

    if ((tone_portamento_target_pitch = player_host_channel->tone_porta_target_pitch)) {
        const AVSequencerTrack *track = player_host_channel->track;
        uint16_t v0, v1, v3;

        if (player_channel->host_channel == channel) {
            if (tone_portamento_target_pitch <= player_channel->frequency) {
                portamento_slide_down(avctx, player_host_channel, player_channel, data_word, 1, 8, channel);

                if (tone_portamento_target_pitch >= player_channel->frequency) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            } else {
                portamento_slide_up(avctx, player_host_channel, player_channel, data_word, 1, 8, channel);

                if (!player_channel->frequency || (tone_portamento_target_pitch <= player_channel->frequency)) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            }
        }

        v0 = player_host_channel->tone_porta;
        v1 = player_host_channel->tone_porta_once;
        v3 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v0 = data_word;
            v1 = data_word;
            v3 = data_word;
        }

        player_host_channel->tone_porta           = v0;
        player_host_channel->fine_tone_porta      = v1;
        player_host_channel->tone_porta_once      = data_word;
        player_host_channel->fine_tone_porta_once = v3;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
            player_host_channel->porta_up             = data_word;
            player_host_channel->porta_down           = data_word;
            player_host_channel->fine_porta_up        = data_word;
            player_host_channel->fine_porta_down      = data_word;
            player_host_channel->porta_up_once        = data_word;
            player_host_channel->porta_down_once      = data_word;
            player_host_channel->fine_porta_up_once   = data_word;
            player_host_channel->fine_porta_down_once = data_word;
        }
    }
}

EXECUTE_EFFECT(tone_portamento_once)
{
    uint32_t tone_portamento_target_pitch;

    if (!data_word)
        data_word = player_host_channel->tone_porta_once;

    if ((tone_portamento_target_pitch = player_host_channel->tone_porta_target_pitch)) {
        const AVSequencerTrack *track = player_host_channel->track;
        uint16_t v0, v1, v3;

        if (player_channel->host_channel == channel) {
            if (tone_portamento_target_pitch <= player_channel->frequency) {
                portamento_slide_down(avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel);

                if (tone_portamento_target_pitch >= player_channel->frequency) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            } else {
                portamento_slide_up(avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel);

                if (!player_channel->frequency || (tone_portamento_target_pitch <= player_channel->frequency)) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            }
        }

        v0 = player_host_channel->tone_porta;
        v1 = player_host_channel->fine_tone_porta;
        v3 = player_host_channel->fine_tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v0 = data_word;
            v1 = data_word;
            v3 = data_word;
        }

        player_host_channel->tone_porta           = v0;
        player_host_channel->fine_tone_porta      = v1;
        player_host_channel->tone_porta_once      = data_word;
        player_host_channel->fine_tone_porta_once = v3;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
            player_host_channel->porta_up             = data_word;
            player_host_channel->porta_down           = data_word;
            player_host_channel->fine_porta_up        = data_word;
            player_host_channel->fine_porta_down      = data_word;
            player_host_channel->porta_up_once        = data_word;
            player_host_channel->porta_down_once      = data_word;
            player_host_channel->fine_porta_up_once   = data_word;
            player_host_channel->fine_porta_down_once = data_word;
        }
    }
}

EXECUTE_EFFECT(fine_tone_portamento_once)
{
    uint32_t tone_portamento_target_pitch;

    if (!data_word)
        data_word = player_host_channel->fine_tone_porta_once;

    if ((tone_portamento_target_pitch = player_host_channel->tone_porta_target_pitch)) {
        const AVSequencerTrack *track = player_host_channel->track;
        uint16_t v0, v1, v3;

        if (player_channel->host_channel == channel) {
            if (tone_portamento_target_pitch <= player_channel->frequency) {
                portamento_slide_down(avctx, player_host_channel, player_channel, data_word, 1, 8, channel);

                if (tone_portamento_target_pitch >= player_channel->frequency) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            } else {
                portamento_slide_up(avctx, player_host_channel, player_channel, data_word, 1, 8, channel);

                if (!player_channel->frequency || (tone_portamento_target_pitch <= player_channel->frequency)) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            }
        }

        v0 = player_host_channel->tone_porta;
        v1 = player_host_channel->fine_tone_porta;
        v3 = player_host_channel->tone_porta_once;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            v0 = data_word;
            v1 = data_word;
            v3 = data_word;
        }

        player_host_channel->tone_porta           = v0;
        player_host_channel->fine_tone_porta      = v1;
        player_host_channel->tone_porta_once      = v3;
        player_host_channel->fine_tone_porta_once = data_word;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA) {
            player_host_channel->porta_up             = data_word;
            player_host_channel->porta_down           = data_word;
            player_host_channel->fine_porta_up        = data_word;
            player_host_channel->fine_porta_down      = data_word;
            player_host_channel->porta_up_once        = data_word;
            player_host_channel->porta_down_once      = data_word;
            player_host_channel->fine_porta_up_once   = data_word;
            player_host_channel->fine_porta_down_once = data_word;
        }
    }
}

EXECUTE_EFFECT(note_slide)
{
    uint16_t note_slide_value, note_slide_type;

    if (!(note_slide_value = (uint8_t) data_word))
        note_slide_value = player_host_channel->note_slide;

    player_host_channel->note_slide = note_slide_value;

    if (!(note_slide_type = (data_word >> 8)))
        note_slide_type = player_host_channel->note_slide_type;

    player_host_channel->note_slide_type = note_slide_type;

    if (!(note_slide_type & 0x10))
        note_slide_value = -note_slide_value;

    note_slide_value               += player_host_channel->final_note;
    player_host_channel->final_note = note_slide_value;

    if (player_channel->host_channel == channel)
        player_channel->frequency = get_tone_pitch(avctx, player_host_channel, player_channel, note_slide_value);
}

EXECUTE_EFFECT(vibrato)
{
    uint16_t vibrato_rate;
    int16_t vibrato_depth;

    if (!(vibrato_rate = (data_word >> 8)))
        vibrato_rate = player_host_channel->vibrato_rate;

    player_host_channel->vibrato_rate = vibrato_rate;
    vibrato_depth                     = (int8_t) data_word;

    do_vibrato(avctx, player_host_channel, player_channel, channel, vibrato_rate, vibrato_depth << 2);
}

EXECUTE_EFFECT(fine_vibrato) {
    uint16_t vibrato_rate;

    if (!(vibrato_rate = (data_word >> 8)))
        vibrato_rate = player_host_channel->vibrato_rate;

    player_host_channel->vibrato_rate = vibrato_rate;

    do_vibrato(avctx, player_host_channel, player_channel, channel, vibrato_rate, (int8_t) data_word);
}

EXECUTE_EFFECT(do_key_off)
{
    if (data_word <= player_host_channel->tempo_counter)
        play_key_off(player_channel);
}

EXECUTE_EFFECT(hold_delay)
{
    // TODO: Implement hold delay effect
}

EXECUTE_EFFECT(note_fade)
{
    if (data_word <= player_host_channel->tempo_counter) {
        player_host_channel->effects_used[fx_byte >> 3] |= 1 << (7 - (fx_byte & 7));

        if (player_channel->host_channel == channel)
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;
    }
}

EXECUTE_EFFECT(note_cut)
{
    if ((data_word & 0xFFF) <= player_host_channel->tempo_counter) {
        player_host_channel->effects_used[fx_byte >> 3] |= 1 << (7 - (fx_byte & 7));

        if (player_channel->host_channel == channel) {
            player_channel->volume     = 0;
            player_channel->sub_volume = 0;

            if (data_word & 0xF000) {
                player_host_channel->instrument = NULL;
                player_host_channel->sample     = NULL;
                player_channel->mixer.flags     = 0;
            }
        }
    }
}

EXECUTE_EFFECT(note_delay)
{
}

EXECUTE_EFFECT(tremor) {
    uint8_t tremor_off, tremor_on;

    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_EXEC;

    if (!(tremor_off = (uint8_t) data_word))
        tremor_off = player_host_channel->tremor_off_ticks;

    player_host_channel->tremor_off_ticks = tremor_off;

    if (!(tremor_on = (data_word >> 8)))
        tremor_on = player_host_channel->tremor_on_ticks;

    player_host_channel->tremor_on_ticks = tremor_on;

    if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_OFF))
        tremor_off = tremor_on;

    if (tremor_off <= player_host_channel->tremor_count) {
        player_host_channel->flags       ^= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_OFF;
        player_host_channel->tremor_count = 0;
    }

    player_host_channel->tremor_count++;
}

EXECUTE_EFFECT(note_retrigger)
{
    uint16_t retrigger_tick = data_word & 0x7FFF, retrigger_tick_count;

    if ((data_word & 0x8000) && data_word)
        retrigger_tick = player_host_channel->tempo / retrigger_tick;

    if ((retrigger_tick_count = player_host_channel->retrig_tick_count) && --retrigger_tick) {
        if (retrigger_tick <= retrigger_tick_count)
            retrigger_tick_count = -1;
    } else if (player_channel->host_channel == channel) {
        player_channel->mixer.pos = 0;
    }

    player_host_channel->retrig_tick_count = ++retrigger_tick_count;
}

EXECUTE_EFFECT(multi_retrigger_note)
{
    uint8_t multi_retrigger_tick, multi_retrigger_volume_change;
    uint16_t retrigger_tick_count;
    uint32_t volume;

    if (!(multi_retrigger_tick = (data_word >> 8)))
        multi_retrigger_tick = player_host_channel->multi_retrig_tick;

    player_host_channel->multi_retrig_tick = multi_retrigger_tick;

    if (!(multi_retrigger_volume_change = (uint8_t) data_word))
        multi_retrigger_volume_change = player_host_channel->multi_retrig_vol_chg;

    player_host_channel->multi_retrig_vol_chg = multi_retrigger_volume_change;

    if ((retrigger_tick_count = player_host_channel->retrig_tick_count) || (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_RETRIG_NOTE) || (player_channel->host_channel != channel)) {
        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_RETRIG_NOTE;
    } else if ((int8_t) multi_retrigger_volume_change < 0) {
        uint8_t multi_retrigger_scale = 4;

        if (!(avctx->player_song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES))
            multi_retrigger_scale = player_host_channel->multi_retrig_scale;

        if ((int8_t) (multi_retrigger_volume_change -= 0xBF) >= 0) {
            volume = multi_retrigger_volume_change * multi_retrigger_scale;

            if (player_channel->volume >= volume) {
                player_channel->volume -= volume;
            } else {
                player_channel->volume     = 0;
                player_channel->sub_volume = 0;
            }
        } else {
            volume = ((multi_retrigger_volume_change + 0x40) * multi_retrigger_scale) + player_channel->volume;

            if (volume < 0x100) {
                player_channel->volume  = volume;
            } else {
                player_channel->volume     = 0xFF;
                player_channel->sub_volume = 0xFF;
            }
        }
    } else {
        uint8_t volume_multiplier, volume_divider;

        volume = (player_channel->volume << 8);

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SUB_SLIDE_RETRIG)
            volume += player_channel->sub_volume;

        if ((volume_multiplier = (multi_retrigger_volume_change >> 4)))
            volume *= volume_multiplier;

        if ((volume_divider = (multi_retrigger_volume_change & 0xF)))
            volume /= volume_divider;

        if (volume > 0xFFFF)
            volume = 0xFFFF;

        player_channel->volume = volume >> 8;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SUB_SLIDE_RETRIG)
            player_channel->sub_volume = volume;
    }

    if ((retrigger_tick_count = player_host_channel->retrig_tick_count) && --multi_retrigger_tick) {
        if (multi_retrigger_tick <= retrigger_tick_count)
            retrigger_tick_count = -1;
    } else if (player_channel->host_channel == channel) {
        player_channel->mixer.pos = 0;
    }

    player_host_channel->retrig_tick_count = ++retrigger_tick_count;
}

EXECUTE_EFFECT(extended_ctrl)
{
    const AVSequencerModule *module;
    AVSequencerPlayerChannel *scan_player_channel;
    uint8_t extended_control_byte;
    uint16_t virtual_channel;
    const uint16_t extended_control_word = data_word & 0x0FFF;

    switch (data_word >> 12) {
    case 0 :
        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ;

        if (!extended_control_word)
            player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ;

        break;
    case 1 :
        player_host_channel->glissando = extended_control_word;

        break;
    case 2 :
        extended_control_byte = extended_control_word;

        switch (extended_control_word >> 8) {
        case 0 :
            if (!extended_control_byte)
                extended_control_byte = 1;

            if (extended_control_byte > 4)
                extended_control_byte = 4;

            player_host_channel->multi_retrig_scale = extended_control_byte;

            break;
        case 1 :
            player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SUB_SLIDE_RETRIG;

            if (extended_control_byte)
                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SUB_SLIDE_RETRIG;

            break;
        case 2 :
            if (extended_control_byte)
                player_host_channel->multi_retrig_tick = player_host_channel->tempo / extended_control_byte;

            break;
        }

        break;
    case 3 :
        module              = avctx->player_module;
        scan_player_channel = avctx->player_channel;
        virtual_channel     = 0;

        do {
            if ((scan_player_channel->host_channel == channel) && (scan_player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND))
                scan_player_channel->mixer.flags = 0;

            scan_player_channel++;
        } while (++virtual_channel < module->channels);

        break;
    case 4 :
        module              = avctx->player_module;
        scan_player_channel = avctx->player_channel;
        virtual_channel     = 0;

        do {
            if ((scan_player_channel->host_channel == channel) && (scan_player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND))
                scan_player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;

            scan_player_channel++;
        } while (++virtual_channel < module->channels);

        break;
    case 5 :
        module              = avctx->player_module;
        scan_player_channel = avctx->player_channel;
        virtual_channel     = 0;

        do {
            if ((scan_player_channel->host_channel == channel) && (scan_player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND))
                play_key_off(scan_player_channel);

            scan_player_channel++;
        } while (++virtual_channel < module->channels);

        break;
    case 6 :
        player_host_channel->sub_slide = extended_control_word;

        break;
    }
}

EXECUTE_EFFECT(invert_loop)
{
    // TODO: Implement invert loop
}

EXECUTE_EFFECT(exec_fx)
{
}

EXECUTE_EFFECT(stop_fx)
{
    uint8_t stop_fx = data_word;

    if ((int8_t) stop_fx < 0)
        stop_fx = 127;

    if (!(data_word >>= 8))
        data_word = player_host_channel->exec_fx;

    if (data_word >= player_host_channel->tempo_counter)
        player_host_channel->effects_used[(stop_fx >> 3)] |= (1 << (7 - (stop_fx & 7)));
}

EXECUTE_EFFECT(set_volume)
{
    player_host_channel->tremolo_slide = 0;

    if (check_old_volume(avctx, player_channel, &data_word, channel)) {
        player_channel->volume     = data_word >> 8;
        player_channel->sub_volume = data_word;
    }
}

EXECUTE_EFFECT(volume_slide_up)
{
    const AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->vol_slide_up;

    do_volume_slide(avctx, player_channel, data_word, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        portamento_up_ok(player_host_channel, data_word);

    volume_slide_up_ok(player_host_channel, data_word);
}

EXECUTE_EFFECT(volume_slide_down)
{
    const AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->vol_slide_down;

    do_volume_slide_down(avctx, player_channel, data_word, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        portamento_down_ok(player_host_channel, data_word);

    volume_slide_down_ok(player_host_channel, data_word);
}

EXECUTE_EFFECT(fine_volume_slide_up)
{
    const AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->fine_vol_slide_up;

    do_volume_slide(avctx, player_channel, data_word, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        portamento_up_once_ok(player_host_channel, data_word);

    fine_volume_slide_up_ok(player_host_channel, data_word);
}

EXECUTE_EFFECT(fine_volume_slide_down)
{
    const AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->vol_slide_down;

    do_volume_slide_down(avctx, player_channel, data_word, channel);

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        portamento_down_once_ok(player_host_channel, data_word);

    fine_volume_slide_down_ok(player_host_channel, data_word);
}

EXECUTE_EFFECT(volume_slide_to)
{
    uint8_t volume_slide_to_value, volume_slide_to_volume;

    if (!(volume_slide_to_value = (uint8_t) data_word))
        volume_slide_to_value = player_host_channel->volume_slide_to;

    player_host_channel->volume_slide_to        = volume_slide_to_value;
    player_host_channel->volume_slide_to_slide &= 0x00FF;
    player_host_channel->volume_slide_to_slide += volume_slide_to_value << 8;
    volume_slide_to_volume                      = data_word >> 8;

    if (volume_slide_to_volume && (volume_slide_to_volume < 0xFF)) {
        player_host_channel->volume_slide_to_volume = volume_slide_to_volume;
    } else if (volume_slide_to_volume && (player_channel->host_channel == channel)) {
        const uint16_t volume_slide_target = (volume_slide_to_volume << 8) + player_host_channel->volume_slide_to_volume;
        uint16_t volume                    = (player_channel->volume << 8) + player_channel->sub_volume;

        if (volume < volume_slide_target) {
            do_volume_slide(avctx, player_channel, player_host_channel->volume_slide_to_slide, channel);

            volume = (player_channel->volume << 8) + player_channel->sub_volume;

            if (volume_slide_target <= volume) {
                player_channel->volume     = volume_slide_target >> 8;
                player_channel->sub_volume = volume_slide_target;
            }
        } else {
            do_volume_slide_down(avctx, player_channel, player_host_channel->volume_slide_to_slide, channel);

            volume = (player_channel->volume << 8) + player_channel->sub_volume;

            if (volume_slide_target >= volume) {
                player_channel->volume     = volume_slide_target >> 8;
                player_channel->sub_volume = volume_slide_target;
            }
        }
    }
}

EXECUTE_EFFECT(tremolo)
{
    const AVSequencerSong *const song = avctx->player_song;
    int16_t tremolo_slide_value;
    uint8_t tremolo_rate;
    int16_t tremolo_depth;

    if (!(tremolo_rate = (data_word >> 8)))
        tremolo_rate = player_host_channel->tremolo_rate;

    player_host_channel->tremolo_rate = tremolo_rate;

    if (!(tremolo_depth = (int8_t) data_word))
        tremolo_depth = player_host_channel->tremolo_depth;

    player_host_channel->tremolo_depth = tremolo_depth;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (tremolo_depth > 63)
            tremolo_depth = 63;

        if (tremolo_depth < -63)
            tremolo_depth = -63;
    }

    tremolo_slide_value = (-(int32_t) tremolo_depth * run_envelope(avctx, &player_host_channel->tremolo_env, tremolo_rate, 0)) >> 7;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES)
        tremolo_slide_value <<= 2;

    if (player_channel->host_channel == channel) {
        const uint16_t volume = player_channel->volume;

        tremolo_slide_value -= player_host_channel->tremolo_slide;

        if ((int16_t) (tremolo_slide_value += volume) < 0)
            tremolo_slide_value = 0;

        if (tremolo_slide_value > 255)
            tremolo_slide_value = 255;

        player_channel->volume              = tremolo_slide_value;
        player_host_channel->tremolo_slide -= volume - tremolo_slide_value;
    }
}

EXECUTE_EFFECT(set_track_volume)
{
    if (check_old_track_volume(avctx, &data_word)) {
        player_host_channel->track_volume     = data_word >> 8;
        player_host_channel->track_sub_volume = data_word;
    }
}

EXECUTE_EFFECT(track_volume_slide_up)
{
    const AVSequencerTrack *track;
    uint16_t v3, v4, v5;

    if (!data_word)
        data_word = player_host_channel->track_vol_slide_up;

    do_track_volume_slide(avctx, player_host_channel, data_word);

    track = player_host_channel->track;

    v3 = player_host_channel->track_vol_slide_down;
    v4 = player_host_channel->fine_trk_vol_slide_up;
    v5 = player_host_channel->fine_trk_vol_slide_dn;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v4 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v3 = data_word;
        v5 = v4;
    }

    player_host_channel->track_vol_slide_up    = data_word;
    player_host_channel->track_vol_slide_down  = v3;
    player_host_channel->fine_trk_vol_slide_up = v4;
    player_host_channel->fine_trk_vol_slide_dn = v5;
}

EXECUTE_EFFECT(track_volume_slide_down)
{
    const AVSequencerTrack *track;
    uint16_t v0, v3, v4;

    if (!data_word)
        data_word = player_host_channel->track_vol_slide_down;

    do_track_volume_slide_down(avctx, player_host_channel, data_word);

    track = player_host_channel->track;

    v0 = player_host_channel->track_vol_slide_up;
    v3 = player_host_channel->fine_trk_vol_slide_up;
    v4 = player_host_channel->fine_trk_vol_slide_dn;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v4 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = data_word;
        v3 = v4;
    }

    player_host_channel->track_vol_slide_up    = v0;
    player_host_channel->track_vol_slide_down  = data_word;
    player_host_channel->fine_trk_vol_slide_up = v3;
    player_host_channel->fine_trk_vol_slide_dn = v4;
}

EXECUTE_EFFECT(fine_track_volume_slide_up)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v4;

    if (!data_word)
        data_word = player_host_channel->fine_trk_vol_slide_up;

    do_track_volume_slide(avctx, player_host_channel, data_word);

    track = player_host_channel->track;

    v0 = player_host_channel->track_vol_slide_up;
    v1 = player_host_channel->track_vol_slide_down;
    v4 = player_host_channel->fine_trk_vol_slide_dn;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v0 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v1 = v0;
        v4 = data_word;
    }

    player_host_channel->track_vol_slide_up    = v0;
    player_host_channel->track_vol_slide_down  = v1;
    player_host_channel->fine_trk_vol_slide_up = data_word;
    player_host_channel->fine_trk_vol_slide_dn = v4;
}

EXECUTE_EFFECT(fine_track_volume_slide_down)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3;

    if (!data_word)
        data_word = player_host_channel->fine_trk_vol_slide_dn;

    do_track_volume_slide_down(avctx, player_host_channel, data_word);

    track = player_host_channel->track;

    v0 = player_host_channel->track_vol_slide_up;
    v1 = player_host_channel->track_vol_slide_down;
    v3 = player_host_channel->fine_trk_vol_slide_up;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v1 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = v1;
        v3 = data_word;
    }

    player_host_channel->track_vol_slide_up    = v0;
    player_host_channel->track_vol_slide_down  = v1;
    player_host_channel->fine_trk_vol_slide_up = v3;
    player_host_channel->fine_trk_vol_slide_dn = data_word;
}

EXECUTE_EFFECT(track_volume_slide_to)
{
    uint8_t track_vol_slide_to, track_volume_slide_to_volume;

    if (!(track_vol_slide_to = (uint8_t) data_word))
        track_vol_slide_to = player_host_channel->track_vol_slide_to;

    player_host_channel->track_vol_slide_to        = track_vol_slide_to;
    player_host_channel->track_vol_slide_to_slide &= 0x00FF;
    player_host_channel->track_vol_slide_to_slide += track_vol_slide_to << 8;
    track_volume_slide_to_volume                   = data_word >> 8;

    if (track_volume_slide_to_volume && (track_volume_slide_to_volume < 0xFF)) {
        player_host_channel->track_vol_slide_to = track_volume_slide_to_volume;
    } else if (track_volume_slide_to_volume) {
        const uint16_t track_volume_slide_target = (track_volume_slide_to_volume << 8) + player_host_channel->track_vol_slide_to_sub_volume;
        uint16_t track_volume                    = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_volume;

        if (track_volume < track_volume_slide_target) {
            do_track_volume_slide(avctx, player_host_channel, player_host_channel->track_vol_slide_to_slide);

            track_volume = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_volume;

            if (track_volume_slide_target <= track_volume) {
                player_host_channel->track_volume     = track_volume_slide_target >> 8;
                player_host_channel->track_sub_volume = track_volume_slide_target;
            }
        } else {
            do_track_volume_slide_down(avctx, player_host_channel, player_host_channel->track_vol_slide_to_slide);

            track_volume = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_volume;

            if (track_volume_slide_target >= track_volume) {
                player_host_channel->track_volume     = track_volume_slide_target >> 8;
                player_host_channel->track_sub_volume = track_volume_slide_target;
            }
        }
    }
}

EXECUTE_EFFECT(track_tremolo)
{
    const AVSequencerSong *const song = avctx->player_song;
    int32_t track_tremolo_slide_value;
    uint8_t track_tremolo_rate;
    int16_t track_tremolo_depth;
    uint16_t track_volume;

    if (!(track_tremolo_rate = (data_word >> 8)))
        track_tremolo_rate = player_host_channel->track_trem_rate;

    player_host_channel->track_trem_rate = track_tremolo_rate;

    if (!(track_tremolo_depth = (int8_t) data_word))
        track_tremolo_depth = player_host_channel->track_trem_depth;

    player_host_channel->track_trem_depth = track_tremolo_depth;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (track_tremolo_depth > 63)
            track_tremolo_depth = 63;

        if (track_tremolo_depth < -63)
            track_tremolo_depth = -63;
    }

    track_tremolo_slide_value = (-(int32_t) track_tremolo_depth * run_envelope(avctx, &player_host_channel->track_trem_env, track_tremolo_rate, 0)) >> 7;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES)
        track_tremolo_slide_value <<= 2;

    track_volume               = player_host_channel->track_volume;
    track_tremolo_slide_value -= player_host_channel->track_trem_slide;

    if ((int16_t) (track_tremolo_slide_value += track_volume) < 0)
        track_tremolo_slide_value = 0;

    if (track_tremolo_slide_value > 255)
        track_tremolo_slide_value = 255;

    player_host_channel->track_volume      = track_tremolo_slide_value;
    player_host_channel->track_trem_slide -= track_volume - track_tremolo_slide_value;
}

EXECUTE_EFFECT(set_panning)
{
    const uint8_t panning = data_word >> 8;

    if (player_channel->host_channel == channel) {
        player_channel->panning     = panning;
        player_channel->sub_panning = data_word;
    }

    player_host_channel->flags                 &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
    player_host_channel->track_panning          = panning;
    player_host_channel->track_sub_panning      = data_word;
    player_host_channel->track_note_panning     = panning;
    player_host_channel->track_note_sub_panning = data_word;
}

EXECUTE_EFFECT(panning_slide_left)
{
    const AVSequencerTrack *track;
    uint16_t v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->pan_slide_left;

    do_panning_slide(avctx, player_host_channel, player_channel, data_word, channel);

    track = player_host_channel->track;

    v3 = player_host_channel->pan_slide_right;
    v4 = player_host_channel->fine_pan_slide_left;
    v5 = player_host_channel->fine_pan_slide_right;
    v8 = player_host_channel->panning_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v4 = data_word;
        v8 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v3 = data_word;
        v5 = v4;
    }

    player_host_channel->pan_slide_left         = data_word;
    player_host_channel->pan_slide_right        = v3;
    player_host_channel->fine_pan_slide_left    = v4;
    player_host_channel->fine_pan_slide_right   = v5;
    player_host_channel->panning_slide_to_slide = v8;
}

EXECUTE_EFFECT(panning_slide_right)
{
    const AVSequencerTrack *track;
    uint16_t v0, v3, v4, v5;

    if (!data_word)
        data_word = player_host_channel->pan_slide_right;

    do_panning_slide_right(avctx, player_host_channel, player_channel, data_word, channel);

    track = player_host_channel->track;

    v0 = player_host_channel->pan_slide_left;
    v3 = player_host_channel->fine_pan_slide_left;
    v4 = player_host_channel->fine_pan_slide_right;
    v5 = player_host_channel->panning_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v4 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = data_word;
        v3 = v4;
    }

    player_host_channel->pan_slide_left         = v0;
    player_host_channel->pan_slide_right        = data_word;
    player_host_channel->fine_pan_slide_left    = v3;
    player_host_channel->fine_pan_slide_right   = v4;
    player_host_channel->panning_slide_to_slide = v5;
}

EXECUTE_EFFECT(fine_panning_slide_left)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v4, v5;

    if (!data_word)
        data_word = player_host_channel->fine_pan_slide_left;

    do_panning_slide(avctx, player_host_channel, player_channel, data_word, channel);

    track = player_host_channel->track;

    v0 = player_host_channel->pan_slide_left;
    v1 = player_host_channel->pan_slide_right;
    v4 = player_host_channel->fine_pan_slide_right;
    v5 = player_host_channel->panning_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v0 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v1 = v0;
        v4 = data_word;
    }

    player_host_channel->pan_slide_left         = v0;
    player_host_channel->pan_slide_right        = v1;
    player_host_channel->fine_pan_slide_left    = data_word;
    player_host_channel->fine_pan_slide_right   = v4;
    player_host_channel->panning_slide_to_slide = v5;
}

EXECUTE_EFFECT(fine_panning_slide_right)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3, v5;

    if (!data_word)
        data_word = player_host_channel->fine_pan_slide_right;

    do_panning_slide_right(avctx, player_host_channel, player_channel, data_word, channel);

    track = player_host_channel->track;

    v0 = player_host_channel->pan_slide_left;
    v1 = player_host_channel->pan_slide_right;
    v3 = player_host_channel->fine_pan_slide_left;
    v5 = player_host_channel->panning_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v1 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = v1;
        v3 = data_word;
    }

    player_host_channel->pan_slide_left         = v0;
    player_host_channel->pan_slide_right        = v1;
    player_host_channel->fine_pan_slide_left    = v3;
    player_host_channel->fine_pan_slide_right   = data_word;
    player_host_channel->panning_slide_to_slide = v5;
}

EXECUTE_EFFECT(panning_slide_to)
{
    uint8_t panning_slide_to_value, panning_slide_to_panning;

    if (!(panning_slide_to_value = (uint8_t) data_word))
        panning_slide_to_value   = player_host_channel->panning_slide_to;

    player_host_channel->panning_slide_to        = panning_slide_to_value;
    player_host_channel->panning_slide_to_slide &= 0x00FF;
    player_host_channel->panning_slide_to_slide += panning_slide_to_value << 8;
    panning_slide_to_panning                     = data_word >> 8;

    if (panning_slide_to_panning && (panning_slide_to_panning < 0xFF)) {
        player_host_channel->panning_slide_to_panning = panning_slide_to_panning;
    } else if (panning_slide_to_panning && (player_channel->host_channel == channel)) {
        const uint16_t panning_slide_target = ((uint8_t) panning_slide_to_panning << 8) + player_host_channel->panning_slide_to_sub_panning;
        uint16_t panning                    = ((uint8_t) player_channel->panning << 8) + player_channel->sub_panning;

        if (panning < panning_slide_target) {
            do_panning_slide_right(avctx, player_host_channel, player_channel, player_host_channel->panning_slide_to_slide, channel);

            panning = ((uint8_t) player_channel->panning << 8) + player_channel->sub_panning;

            if (panning_slide_target <= panning) {
                player_channel->panning                       = panning_slide_target >> 8;
                player_channel->sub_panning                   = panning_slide_target;
                player_host_channel->panning_slide_to_panning = 0;
                player_host_channel->track_note_panning       = player_host_channel->track_panning = player_host_channel->panning_slide_to_slide >> 8;
                player_host_channel->track_note_sub_panning   = player_host_channel->track_sub_panning = player_host_channel->panning_slide_to_slide;
            }
        } else {
            do_panning_slide(avctx, player_host_channel, player_channel, player_host_channel->panning_slide_to_slide, channel);

            panning = ((uint8_t) player_channel->panning << 8) + player_channel->sub_panning;

            if (panning_slide_target >= panning) {
                player_channel->panning                       = panning_slide_target >> 8;
                player_channel->sub_panning                   = panning_slide_target;
                player_host_channel->panning_slide_to_panning = 0;
                player_host_channel->track_note_panning       = player_host_channel->track_panning = player_host_channel->panning_slide_to_slide >> 8;
                player_host_channel->track_note_sub_panning   = player_host_channel->track_sub_panning = player_host_channel->panning_slide_to_slide;
            }
        }
    }
}

EXECUTE_EFFECT(pannolo)
{
    int16_t pannolo_slide_value;
    uint8_t pannolo_rate;
    int16_t pannolo_depth;

    if (!(pannolo_rate = (data_word >> 8)))
        pannolo_rate = player_host_channel->pannolo_rate;

    player_host_channel->pannolo_rate = pannolo_rate;

    if (!(pannolo_depth = (int8_t) data_word))
        pannolo_depth = player_host_channel->pannolo_depth;

    player_host_channel->pannolo_depth = pannolo_depth;

    pannolo_slide_value = (-(int32_t) pannolo_depth * run_envelope(avctx, &player_host_channel->pannolo_env, pannolo_rate, 0)) >> 7;

    if (player_channel->host_channel == channel) {
        const int16_t panning = (uint8_t) player_channel->panning;

        pannolo_slide_value -= player_host_channel->pannolo_slide;

        if ((int16_t) (pannolo_slide_value += panning) < 0)
            pannolo_slide_value = 0;

        if (pannolo_slide_value > 255)
            pannolo_slide_value = 255;

        player_channel->panning                     = pannolo_slide_value;
        player_host_channel->pannolo_slide         -= panning - pannolo_slide_value;
        player_host_channel->track_note_panning     = player_host_channel->track_panning = panning;
        player_host_channel->track_note_sub_panning = player_host_channel->track_sub_panning = player_channel->sub_panning;
    }
}

EXECUTE_EFFECT(set_track_panning)
{
    player_host_channel->channel_panning     = data_word >> 8;
    player_host_channel->channel_sub_panning = data_word;

    player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHANNEL_SUR_PAN;
}

EXECUTE_EFFECT(track_panning_slide_left)
{
    const AVSequencerTrack *track;
    uint16_t v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->track_pan_slide_left;

    do_track_panning_slide(avctx, player_host_channel, data_word);

    track = player_host_channel->track;

    v3 = player_host_channel->track_pan_slide_right;
    v4 = player_host_channel->fine_trk_pan_sld_left;
    v5 = player_host_channel->fine_trk_pan_sld_right;
    v8 = player_host_channel->track_pan_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v4 = data_word;
        v8 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v3 = data_word;
        v5 = v4;
    }

    player_host_channel->track_pan_slide_left   = data_word;
    player_host_channel->track_pan_slide_right  = v3;
    player_host_channel->fine_trk_pan_sld_left  = v4;
    player_host_channel->fine_trk_pan_sld_right = v5;
    player_host_channel->panning_slide_to_slide = v8;
}

EXECUTE_EFFECT(track_panning_slide_right)
{
    const AVSequencerTrack *track;
    uint16_t v0, v3, v4, v5;

    if (!data_word)
        data_word = player_host_channel->track_pan_slide_right;

    do_track_panning_slide_right(avctx, player_host_channel, data_word);

    track = player_host_channel->track;

    v0 = player_host_channel->track_pan_slide_left;
    v3 = player_host_channel->fine_trk_pan_sld_left;
    v4 = player_host_channel->fine_trk_pan_sld_right;
    v5 = player_host_channel->track_pan_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v4 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = data_word;
        v3 = v4;
    }

    player_host_channel->track_pan_slide_left   = v0;
    player_host_channel->track_pan_slide_right  = data_word;
    player_host_channel->fine_trk_pan_sld_left  = v3;
    player_host_channel->fine_trk_pan_sld_right = v4;
    player_host_channel->panning_slide_to_slide = v5;
}

EXECUTE_EFFECT(fine_track_panning_slide_left)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v4, v5;

    if (!data_word)
        data_word = player_host_channel->fine_trk_pan_sld_left;

    do_track_panning_slide(avctx, player_host_channel, data_word);

    track = player_host_channel->track;

    v0 = player_host_channel->track_pan_slide_left;
    v1 = player_host_channel->track_pan_slide_right;
    v4 = player_host_channel->fine_trk_pan_sld_right;
    v5 = player_host_channel->track_pan_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v0 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v1 = v0;
        v4 = data_word;
    }

    player_host_channel->track_pan_slide_left   = v0;
    player_host_channel->track_pan_slide_right  = v1;
    player_host_channel->fine_trk_pan_sld_left  = data_word;
    player_host_channel->fine_trk_pan_sld_right = v4;
    player_host_channel->panning_slide_to_slide = v5;
}

EXECUTE_EFFECT(fine_track_panning_slide_right)
{
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3, v5;

    if (!data_word)
        data_word = player_host_channel->fine_trk_pan_sld_right;

    do_track_panning_slide_right(avctx, player_host_channel, data_word);

    track = player_host_channel->track;

    v0 = player_host_channel->track_pan_slide_left;
    v1 = player_host_channel->track_pan_slide_right;
    v3 = player_host_channel->fine_trk_pan_sld_left;
    v5 = player_host_channel->track_pan_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v1 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = v1;
        v3 = data_word;
    }

    player_host_channel->track_pan_slide_left   = v0;
    player_host_channel->track_pan_slide_right  = v1;
    player_host_channel->fine_trk_pan_sld_left  = v3;
    player_host_channel->fine_trk_pan_sld_right = data_word;
    player_host_channel->panning_slide_to_slide = v5;
}

EXECUTE_EFFECT(track_panning_slide_to)
{
    uint8_t track_pan_slide_to, track_panning_slide_to_panning;

    if (!(track_pan_slide_to = (uint8_t) data_word))
        track_pan_slide_to = player_host_channel->track_pan_slide_to;

    player_host_channel->track_pan_slide_to        = track_pan_slide_to;
    player_host_channel->track_pan_slide_to_slide &= 0x00FF;
    player_host_channel->track_pan_slide_to_slide += track_pan_slide_to << 8;

    track_panning_slide_to_panning = data_word >> 8;

    if (track_panning_slide_to_panning && (track_panning_slide_to_panning < 0xFF)) {
        player_host_channel->track_pan_slide_to_panning = track_panning_slide_to_panning;
    } else if (track_panning_slide_to_panning) {
        const uint16_t track_panning_slide_to_target = ((uint8_t) track_panning_slide_to_panning << 8) + player_host_channel->track_pan_slide_to_sub_panning;
        uint16_t track_panning                       = ((uint8_t) player_host_channel->track_panning << 8) + player_host_channel->track_sub_panning;

        if (track_panning < track_panning_slide_to_target) {
            do_track_panning_slide_right(avctx, player_host_channel, player_host_channel->track_pan_slide_to_slide);

            track_panning = ((uint8_t) player_host_channel->track_panning << 8) + player_host_channel->track_sub_panning;

            if (track_panning_slide_to_target <= track_panning) {
                player_host_channel->track_panning     = track_panning_slide_to_target >> 8;
                player_host_channel->track_sub_panning = track_panning_slide_to_target;
            }
        } else {
            do_track_panning_slide(avctx, player_host_channel, player_host_channel->track_pan_slide_to_slide);

            track_panning = ((uint8_t) player_host_channel->track_panning << 8) + player_host_channel->track_sub_panning;

            if (track_panning_slide_to_target >= track_panning) {
                player_host_channel->track_panning     = track_panning_slide_to_target >> 8;
                player_host_channel->track_sub_panning = track_panning_slide_to_target;
            }
        }
    }
}

EXECUTE_EFFECT(track_pannolo)
{
    int16_t track_pannolo_slide_value;
    uint8_t track_pannolo_rate;
    int16_t track_pannolo_depth;
    uint16_t track_panning;

    if (!(track_pannolo_rate = (data_word >> 8)))
        track_pannolo_rate = player_host_channel->track_pan_rate;

    player_host_channel->track_pan_rate = track_pannolo_rate;

    if (!(track_pannolo_depth = (int8_t) data_word))
        track_pannolo_depth = player_host_channel->track_pan_depth;

    player_host_channel->track_pan_depth = track_pannolo_depth;
    track_pannolo_slide_value            = (-(int32_t) track_pannolo_depth * run_envelope(avctx, &player_host_channel->track_pan_env, track_pannolo_rate, 0)) >> 7;

    track_panning              = (uint8_t) player_host_channel->track_panning;
    track_pannolo_slide_value -= player_host_channel->track_pan_slide;

    if ((int16_t) (track_pannolo_slide_value += track_panning) < 0)
        track_pannolo_slide_value = 0;

    if (track_pannolo_slide_value > 255)
        track_pannolo_slide_value = 255;

    player_host_channel->track_panning    = track_pannolo_slide_value;
    player_host_channel->track_pan_slide -= track_panning - track_pannolo_slide_value;
}

EXECUTE_EFFECT(set_tempo)
{
    if (!(player_host_channel->tempo = data_word))
        player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;
}

EXECUTE_EFFECT(set_relative_tempo)
{
    if (!(player_host_channel->tempo += data_word))
        player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;
}

EXECUTE_EFFECT(pattern_break)
{
    if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP)) {
        player_host_channel->flags    |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_BREAK;
        player_host_channel->break_row = data_word;
    }
}

EXECUTE_EFFECT(position_jump)
{
    if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP)) {
        AVSequencerOrderData *order_data = NULL;

        if (data_word--) {
            const AVSequencerOrderList *order_list = avctx->player_song->order_list + channel;

            if ((data_word < order_list->orders) && order_list->order_data[data_word])
                order_data = order_list->order_data[data_word];
        }

        player_host_channel->order = order_data;

        pattern_break(avctx, player_host_channel, player_channel, channel, AVSEQ_TRACK_EFFECT_CMD_PATT_BREAK, 0);
    }
}

EXECUTE_EFFECT(relative_position_jump)
{
    if (!data_word)
        data_word = player_host_channel->pos_jump;

    if ((player_host_channel->pos_jump = data_word)) {
        const AVSequencerOrderList *order_list = avctx->player_song->order_list + channel;
        AVSequencerOrderData *order_data = player_host_channel->order;
        uint32_t ord                     = -1;

        while (++ord < order_list->orders) {
            if (order_data == order_list->order_data[ord])
                break;

            ord++;
        }

        ord += (int32_t) data_word;

        if (ord > 0xFFFF)
            ord = 0;

        position_jump(avctx, player_host_channel, player_channel, channel, AVSEQ_TRACK_EFFECT_CMD_POS_JUMP, (uint16_t) ord);
    }
}

EXECUTE_EFFECT(change_pattern)
{
    player_host_channel->chg_pattern = data_word;
    player_host_channel->flags      |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHG_PATTERN;
}

EXECUTE_EFFECT(reverse_pattern_play)
{
    if (!data_word)
        player_host_channel->flags ^= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS;
    else if (data_word & 0x8000)
        player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS;
    else
        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS;
}

EXECUTE_EFFECT(pattern_delay)
{
    player_host_channel->pattern_delay = data_word;
}

EXECUTE_EFFECT(fine_pattern_delay)
{
    player_host_channel->fine_pattern_delay = data_word;
}

EXECUTE_EFFECT(pattern_loop)
{
    const AVSequencerSong *const song = avctx->player_song;
    uint16_t *loop_stack_ptr;
    uint16_t loop_length = player_host_channel->pattern_loop_depth;

    loop_stack_ptr = (uint16_t *) avctx->player_globals->loop_stack + (((song->loop_stack_size * channel) * sizeof (uint16_t[2])) + (loop_length * sizeof(uint16_t[2])));

    if (data_word) {
        if (data_word == *loop_stack_ptr) {
            *loop_stack_ptr = 0;

            if (loop_length--)
                player_host_channel->pattern_loop_depth = loop_length;
            else
                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_RESET;
        } else {
            (*loop_stack_ptr++)++;

            player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_BREAK|AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP|AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP_JMP;

            player_host_channel->break_row = *loop_stack_ptr;

            if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_RESET)
                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;
        }
    } else if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP_JMP)) {
        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_RESET;
        *loop_stack_ptr++           = 0;
        *loop_stack_ptr             = player_host_channel->row;

        if (++loop_length != song->loop_stack_size)
            player_host_channel->pattern_loop_depth = loop_length;
    }
}

EXECUTE_EFFECT(gosub)
{
    // TODO: Implement GoSub effect
}

EXECUTE_EFFECT(gosub_return)
{
    // TODO: Implement return effect
}

EXECUTE_EFFECT(channel_sync)
{
    // TODO: Implement channel synchronizattion effect
}

EXECUTE_EFFECT(set_sub_slides)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    uint8_t sub_slide_flags;

    if (!(sub_slide_flags = (data_word >> 8)))
        sub_slide_flags = player_host_channel->sub_slide_bits;

    if (sub_slide_flags & 0x01)
        player_host_channel->volume_slide_to_volume = data_word;

    if (sub_slide_flags & 0x02)
        player_host_channel->track_vol_slide_to_sub_volume = data_word;

    if (sub_slide_flags & 0x04)
        player_globals->global_volume_sl_to_sub_volume = data_word;

    if (sub_slide_flags & 0x08)
        player_host_channel->panning_slide_to_sub_panning = data_word;

    if (sub_slide_flags & 0x10)
        player_host_channel->track_pan_slide_to_sub_panning = data_word;

    if (sub_slide_flags & 0x20)
        player_globals->global_pan_slide_to_sub_panning = data_word;
}

EXECUTE_EFFECT(sample_offset_high)
{
    player_host_channel->smp_offset_hi = data_word;
}

EXECUTE_EFFECT(sample_offset_low)
{
    if (player_channel->host_channel == channel) {
        uint32_t sample_offset = (player_host_channel->smp_offset_hi << 16) + data_word;

        if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SMP_OFFSET_REL)) {
            const AVSequencerTrack *const track = player_host_channel->track;

            if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SAMPLE_OFFSET) {
                const AVSequencerSample *const sample = player_channel->sample;

                if (sample_offset >= sample->samples)
                    return;
            }

            player_channel->mixer.pos = 0;

            if (player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                const uint32_t repeat_end = player_channel->mixer.repeat_start + player_channel->mixer.repeat_length;

                if (repeat_end < sample_offset)
                    sample_offset = repeat_end;
            }
        }

        player_channel->mixer.pos += sample_offset;
    }
}

EXECUTE_EFFECT(set_hold)
{
    // TODO: Implement set hold effect
}

EXECUTE_EFFECT(set_decay)
{
    // TODO: Implement set decay effect
}

EXECUTE_EFFECT(set_transpose)
{
    // TODO: Implement set transpose effect
}

EXECUTE_EFFECT(instrument_ctrl)
{
    // TODO: Implement instrument control effect
}

EXECUTE_EFFECT(instrument_change)
{
    const AVSequencerInstrument *instrument;
    const AVSequencerSample *sample;
    AVMixerData *mixer;
    uint32_t volume, volume_swing, panning, abs_volume_swing, seed;

    switch (data_word >> 12) {
        case 0x0 :
            sample                              = player_host_channel->sample;
            volume                              = player_channel->instr_volume;
            player_channel->global_instr_volume = data_word;

            if (sample && (instrument = player_host_channel->instrument)) {
                volume            = sample->global_volume * player_channel->global_instr_volume;
                volume_swing      = (volume * player_channel->volume_swing) >> 8;
                abs_volume_swing  = (volume_swing << 1) + 1;
                avctx->seed       = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
                abs_volume_swing  = ((uint64_t) seed * abs_volume_swing) >> 32;
                abs_volume_swing -= volume_swing;

                if ((int32_t) (volume += abs_volume_swing) < 0)
                    volume = 0;

                if (volume > (255*255))
                    volume = 255*255;
            } else if (sample) {
                volume = player_channel->global_instr_volume * 255;
            }

            player_channel->instr_volume = volume;

            break;
        case 0x1 :
            sample       = player_host_channel->sample;
            volume       = player_channel->instr_volume;
            volume_swing = data_word & 0xFFF;

            if (sample && (instrument = player_host_channel->instrument)) {
                volume            = sample->global_volume * player_channel->global_instr_volume;
                volume_swing      = (volume * volume_swing) >> 8;
                abs_volume_swing  = (volume_swing << 1) + 1;
                avctx->seed       = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
                abs_volume_swing  = ((uint64_t) seed * abs_volume_swing) >> 32;
                abs_volume_swing -= volume_swing;

                if ((int32_t) (volume += abs_volume_swing) < 0)
                    volume = 0;

                if (volume > (255*255))
                    volume = 255*255;
            } else if (sample) {
                volume            = sample->global_volume * 255;
                volume_swing      = (volume * instrument->volume_swing) >> 8;
                abs_volume_swing  = (volume_swing << 1) + 1;
                avctx->seed       = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
                abs_volume_swing  = ((uint64_t) seed * abs_volume_swing) >> 32;
                abs_volume_swing -= volume_swing;

                if ((int32_t) (volume += abs_volume_swing) < 0)
                    volume = 0;

                if (volume > (255*255))
                    volume = 255*255;
            }

            player_channel->instr_volume = volume;
            player_channel->volume_swing = volume_swing;

            break;
        case 0x2 :
            if ((sample = player_host_channel->sample)) {
                panning                = (uint8_t) player_host_channel->track_note_panning;
                player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN;

                if (sample->flags & AVSEQ_SAMPLE_FLAG_SAMPLE_PANNING) {
                    player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

                    if (sample->flags & AVSEQ_SAMPLE_FLAG_SURROUND_PANNING)
                        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

                    player_channel->panning            = sample->panning;
                    player_channel->sub_panning        = sample->sub_panning;
                    player_host_channel->pannolo_slide = 0;
                    panning                            = (uint8_t) player_channel->panning;

                    if (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
                        player_host_channel->track_panning          = panning;
                        player_host_channel->track_sub_panning      = player_channel->sub_panning;
                        player_host_channel->track_note_panning     = panning;
                        player_host_channel->track_note_sub_panning = player_channel->sub_panning;
                        player_host_channel->flags                 &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
    
                        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                            player_host_channel->flags         |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                    }
                } else {
                    player_channel->panning     = player_host_channel->track_panning;
                    player_channel->sub_panning = player_host_channel->track_sub_panning;
                    player_channel->flags      &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
    
                    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN)
                        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
                }

                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

                if ((instrument = player_channel->instrument)) {
                    uint32_t panning_swing;
                    int32_t panning_separation;

                    player_channel->panning_swing = panning_swing = data_word & 0xFFF;

                    if ((instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) && (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN))
                        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

                    if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_DEFAULT_PANNING) {
                        player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

                        if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_SURROUND_PANNING)
                            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

                        player_channel->panning            = instrument->default_panning;
                        player_channel->sub_panning        = instrument->default_sub_pan;
                        player_host_channel->pannolo_slide = 0;
                        panning                            = (uint8_t) player_channel->panning;

                        if (instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
                            player_host_channel->track_panning     = player_channel->panning;
                            player_host_channel->track_sub_panning = player_channel->sub_panning;
                            player_host_channel->flags            &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN);
    
                            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                        }
                    }

                    panning_separation = ((int32_t) player_channel->pitch_pan_separation * (int32_t) (player_host_channel->instr_note - (player_channel->pitch_pan_center + 1))) >> 8;
                    panning_swing      = (panning_swing << 1) + 1;
                    avctx->seed        = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
                    panning_swing      = ((uint64_t) seed * panning_swing) >> 32;
                    panning_swing     -= instrument->panning_swing;
                    panning           += panning_swing;

                    if ((int32_t) (panning += panning_separation) < 0)
                        panning = 0;

                    if (panning > 255)
                        panning = 255;

                    if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN)
                        player_host_channel->track_panning = panning;
                    else
                        player_channel->panning            = panning;

                    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN) {
                        player_host_channel->track_panning = panning;
                        player_channel->panning            = panning;
                    }
                }
            }

            break;
        case 0x3 :
            player_channel->pitch_swing = (((uint32_t) data_word & 0xFFF) << 16) / 100;

            break;
        case 0x4 :
            player_channel->fade_out = data_word << 4;

            break;
        case 0x5 :
            if (data_word & 0xFFF)
                player_channel->fade_out_count = data_word << 4;
            else
                player_channel->flags &= ~AVSEQ_PLAYER_CHANNEL_FLAG_FADING;

            break;
        case 0x6 :
            switch ((data_word >> 8) & 0xF) {
                case 0x0 :
                    player_channel->auto_vibrato_sweep = data_word & 0xFF;

                    break;
                case 0x1 :
                    player_channel->auto_vibrato_depth = data_word;

                    break;
                case 0x2 :
                    player_channel->auto_vibrato_rate = data_word;

                    break;
                case 0x4 :
                    player_channel->auto_tremolo_sweep = data_word & 0xFF;

                    break;
                case 0x5 :
                    player_channel->auto_tremolo_depth = data_word;

                    break;
                case 0x6 :
                    player_channel->auto_tremolo_rate = data_word;

                    break;
                case 0x8 :
                    player_channel->auto_pannolo_sweep = data_word & 0xFF;

                    break;
                case 0x9 :
                    player_channel->auto_pannolo_sweep = data_word;

                    break;
                case 0xA :
                    player_channel->auto_pannolo_sweep = data_word;

                    break;
            }

            break;
        case 0x7 :
            if ((sample = player_host_channel->sample)) {
                panning                = (uint8_t) player_host_channel->track_note_panning;
                player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN;

                if (sample->flags & AVSEQ_SAMPLE_FLAG_SAMPLE_PANNING) {
                    player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

                    if (sample->flags & AVSEQ_SAMPLE_FLAG_SURROUND_PANNING)
                        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

                    player_channel->panning            = sample->panning;
                    player_channel->sub_panning        = sample->sub_panning;
                    player_host_channel->pannolo_slide = 0;
                    panning                            = (uint8_t) player_channel->panning;

                    if (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
                        player_host_channel->track_panning          = panning;
                        player_host_channel->track_sub_panning      = player_channel->sub_panning;
                        player_host_channel->track_note_panning     = panning;
                        player_host_channel->track_note_sub_panning = player_channel->sub_panning;
                        player_host_channel->flags                 &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;

                        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                            player_host_channel->flags         |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                    }
                } else {
                    player_channel->panning     = player_host_channel->track_panning;
                    player_channel->sub_panning = player_host_channel->track_sub_panning;
                    player_channel->flags      &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

                    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN)
                        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
                }

                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

                if ((instrument = player_channel->instrument)) {
                    uint32_t panning_swing;
                    int32_t panning_separation;

                    player_channel->pitch_pan_separation = data_word & 0xFFF;

                    if ((instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) && (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN))
                        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

                    if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_DEFAULT_PANNING) {
                        player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

                        if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_SURROUND_PANNING)
                            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

                        player_channel->panning            = instrument->default_panning;
                        player_channel->sub_panning        = instrument->default_sub_pan;
                        player_host_channel->pannolo_slide = 0;
                        panning                            = (uint8_t) player_channel->panning;

                        if (instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
                            player_host_channel->track_panning     = player_channel->panning;
                            player_host_channel->track_sub_panning = player_channel->sub_panning;
                            player_host_channel->flags            &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN);

                            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                        }
                    }

                    panning_separation = ((int32_t) player_channel->pitch_pan_separation * (int32_t) (player_host_channel->instr_note - (player_channel->pitch_pan_center + 1))) >> 8;
                    panning_swing      = (player_channel->panning_swing << 1) + 1;
                    avctx->seed        = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
                    panning_swing      = ((uint64_t) seed * panning_swing) >> 32;
                    panning_swing     -= instrument->panning_swing;
                    panning           += panning_swing;

                    if ((int32_t) (panning += panning_separation) < 0)
                        panning = 0;

                    if (panning > 255)
                        panning = 255;

                    if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN)
                        player_host_channel->track_panning = panning;
                    else
                        player_channel->panning            = panning;

                    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN) {
                        player_host_channel->track_panning = panning;
                        player_channel->panning            = panning;
                    }
                }
            }

            break;
        case 0x8 :
            if ((sample = player_host_channel->sample)) {
                panning                = (uint8_t) player_host_channel->track_note_panning;
                player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN;

                if (sample->flags & AVSEQ_SAMPLE_FLAG_SAMPLE_PANNING) {
                    player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

                    if (sample->flags & AVSEQ_SAMPLE_FLAG_SURROUND_PANNING)
                        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

                    player_channel->panning            = sample->panning;
                    player_channel->sub_panning        = sample->sub_panning;
                    player_host_channel->pannolo_slide = 0;
                    panning                            = (uint8_t) player_channel->panning;

                    if (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
                        player_host_channel->track_panning          = panning;
                        player_host_channel->track_sub_panning      = player_channel->sub_panning;
                        player_host_channel->track_note_panning     = panning;
                        player_host_channel->track_note_sub_panning = player_channel->sub_panning;
                        player_host_channel->flags                 &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;

                        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                            player_host_channel->flags         |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                    }
                } else {
                    player_channel->panning     = player_host_channel->track_panning;
                    player_channel->sub_panning = player_host_channel->track_sub_panning;
                    player_channel->flags      &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

                    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN)
                        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
                }

                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

                if ((instrument = player_channel->instrument)) {
                    uint32_t panning_swing;
                    int32_t panning_separation;

                    player_channel->pitch_pan_center = data_word;

                    if ((instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) && (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN))
                        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

                    if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_DEFAULT_PANNING) {
                        player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

                        if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_SURROUND_PANNING)
                            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

                        player_channel->panning            = instrument->default_panning;
                        player_channel->sub_panning        = instrument->default_sub_pan;
                        player_host_channel->pannolo_slide = 0;
                        panning                            = (uint8_t) player_channel->panning;

                        if (instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
                            player_host_channel->track_panning     = player_channel->panning;
                            player_host_channel->track_sub_panning = player_channel->sub_panning;
                            player_host_channel->flags            &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN);

                            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                        }
                    }

                    panning_separation = ((int32_t) player_channel->pitch_pan_separation * (int32_t) (player_host_channel->instr_note - (player_channel->pitch_pan_center + 1))) >> 8;
                    panning_swing      = (player_channel->panning_swing << 1) + 1;
                    avctx->seed        = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
                    panning_swing      = ((uint64_t) seed * panning_swing) >> 32;
                    panning_swing     -= instrument->panning_swing;
                    panning           += panning_swing;

                    if ((int32_t) (panning += panning_separation) < 0)
                        panning = 0;

                    if (panning > 255)
                        panning = 255;

                    if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN)
                        player_host_channel->track_panning = panning;
                    else
                        player_channel->panning            = panning;

                    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN) {
                        player_host_channel->track_panning = panning;
                        player_channel->panning            = panning;
                    }
                }
            }

            break;
        case 0x9 :
            if ((data_word & 0xFFF) <= 2)
                player_channel->dca = data_word;

            break;
        case 0xA :
            mixer                               = avctx->player_mixer_data;
            player_channel->mixer.filter_cutoff = data_word & 0xFFF;

            if (mixer->mixctx->set_channel_filter)
                mixer->mixctx->set_channel_filter(mixer, &player_channel->mixer, player_host_channel->virtual_channel);

            break;
        case 0xB :
            mixer                                = avctx->player_mixer_data;
            player_channel->mixer.filter_damping = data_word & 0xFFF;

            if (mixer->mixctx->set_channel_filter)
                mixer->mixctx->set_channel_filter(mixer, &player_channel->mixer, player_host_channel->virtual_channel);

            break;
        case 0xC :
            player_channel->note_swing = data_word;

            break;
    }
}

EXECUTE_EFFECT(set_synth_value)
{
    uint8_t synth_ctrl_count         = player_host_channel->synth_ctrl_count;
    const uint16_t synth_ctrl_change = player_host_channel->synth_ctrl_change & 0x7F;

    player_host_channel->synth_ctrl = data_word;

    do {
        switch (synth_ctrl_change) {
            case 0x00 :
            case 0x01 :
            case 0x02 :
            case 0x03 :
                player_channel->entry_pos[synth_ctrl_change & 3] = data_word;

                break;
            case 0x04 :
            case 0x05 :
            case 0x06 :
            case 0x07 :
                player_channel->sustain_pos[synth_ctrl_change & 3] = data_word;

                break;
            case 0x08 :
            case 0x09 :
            case 0x0A :
            case 0x0B :
                player_channel->nna_pos[synth_ctrl_change & 3] = data_word;

                break;
            case 0x0C :
            case 0x0D :
            case 0x0E :
            case 0x0F :
                player_channel->dna_pos[synth_ctrl_change & 3] = data_word;

                break;
            case 0x10 :
            case 0x11 :
            case 0x12 :
            case 0x13 :
            case 0x14 :
            case 0x15 :
            case 0x16 :
            case 0x17 :
            case 0x18 :
            case 0x19 :
            case 0x1A :
            case 0x1B :
            case 0x1C :
            case 0x1D :
            case 0x1E :
            case 0x1F :
                player_channel->variable[synth_ctrl_change & 0xF] = data_word;

                break;
            case 0x20 :
            case 0x21 :
            case 0x22 :
            case 0x23 :
                player_channel->cond_var[synth_ctrl_change & 3] = data_word;

                break;
            case 0x24 :
                if (data_word < player_channel->synth->waveforms)
                    player_channel->sample_waveform = player_channel->waveform_list[data_word];

                break;
            case 0x25 :
                if (data_word < player_channel->synth->waveforms)
                    player_channel->vibrato_waveform = player_channel->waveform_list[data_word];

                break;
            case 0x26 :
                if (data_word < player_channel->synth->waveforms)
                    player_channel->tremolo_waveform = player_channel->waveform_list[data_word];

                break;
            case 0x27 :
                if (data_word < player_channel->synth->waveforms)
                    player_channel->pannolo_waveform = player_channel->waveform_list[data_word];

                break;
            case 0x28 :
                if (data_word < player_channel->synth->waveforms)
                    player_channel->arpeggio_waveform = player_channel->waveform_list[data_word];

                break;
        }
    } while (synth_ctrl_count--);
}

EXECUTE_EFFECT(synth_ctrl)
{
    player_host_channel->synth_ctrl_count  = data_word >> 8;
    player_host_channel->synth_ctrl_change = data_word;

    if (data_word & 0x80)
        set_synth_value(avctx, player_host_channel, player_channel, channel, AVSEQ_TRACK_EFFECT_CMD_SET_SYN_VAL, player_host_channel->synth_ctrl);
}

static const void *envelope_ctrl_type_lut[] = {
    use_volume_envelope,
    use_panning_envelope,
    use_slide_envelope,
    use_vibrato_envelope,
    use_tremolo_envelope,
    use_pannolo_envelope,
    use_channolo_envelope,
    use_spenolo_envelope,
    use_auto_vibrato_envelope,
    use_auto_tremolo_envelope,
    use_auto_pannolo_envelope,
    use_track_tremolo_envelope,
    use_track_pannolo_envelope,
    use_global_tremolo_envelope,
    use_global_pannolo_envelope,
    use_arpeggio_envelope,
    use_resonance_envelope
};

EXECUTE_EFFECT(set_envelope_value)
{
    const AVSequencerModule *const module = avctx->player_module;
    const AVSequencerEnvelope *instrument_envelope;
    AVSequencerPlayerEnvelope *envelope;
    AVSequencerPlayerEnvelope *(*envelope_get_kind)(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel);

    player_host_channel->env_ctrl = data_word;
    envelope_get_kind             = envelope_ctrl_type_lut[player_host_channel->env_ctrl_kind];
    envelope                      = envelope_get_kind(avctx, player_host_channel, player_channel);

    switch (player_host_channel->env_ctrl_change) {
    case 0x00 :
        if ((data_word < module->envelopes) && ((instrument_envelope = module->envelope_list[data_word])))
            envelope->envelope = instrument_envelope;
        else
            envelope->envelope = NULL;

        break;
    case 0x04 :
        envelope->pos = data_word;

        break;
    case 0x14 :
        if ((instrument_envelope = envelope->envelope)) {
            if (++data_word > instrument_envelope->nodes)
                data_word = instrument_envelope->nodes;

            envelope->pos = instrument_envelope->node_points[data_word - 1];
        }

        break;
    case 0x05 :
        envelope->tempo           = data_word;

        break;
    case 0x15 :
        envelope->tempo          += data_word;

        break;
    case 0x25 :
        envelope->tempo_count     = data_word;

        break;
    case 0x06 :
        envelope->sustain_start   = data_word;

        break;
    case 0x07 :
        envelope->sustain_end     = data_word;

        break;
    case 0x08 :
        envelope->sustain_count   = data_word;

        break;
    case 0x09 :
        envelope->sustain_counted = data_word;

        break;
    case 0x0A :
        envelope->loop_start      = data_word;

        break;
    case 0x1A :
        envelope->start           = data_word;

        break;
    case 0x0B :
        envelope->loop_end        = data_word;

        break;
    case 0x1B :
        envelope->end             = data_word;

        break;
    case 0x0C :
        envelope->loop_count      = data_word;

        break;
    case 0x0D :
        envelope->loop_counted    = data_word;

        break;
    case 0x0E :
        envelope->value_min       = data_word;

        break;
    case 0x0F :
        envelope->value_max       = data_word;

        break;
    }
}

EXECUTE_EFFECT(envelope_ctrl)
{
    const uint8_t envelope_ctrl_kind = (data_word >> 8) & 0x7F;

    if (envelope_ctrl_kind <= 0x10) {
        const uint8_t envelope_ctrl_type = data_word;

        player_host_channel->env_ctrl_kind = envelope_ctrl_kind;

        if (envelope_ctrl_type <= 0x32) {
            const AVSequencerEnvelope *instrument_envelope;
            AVSequencerPlayerEnvelope *envelope;
            AVSequencerPlayerEnvelope *(*envelope_get_kind)(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel);

            envelope_get_kind = envelope_ctrl_type_lut[envelope_ctrl_kind];
            envelope          = envelope_get_kind(avctx, player_host_channel, player_channel);

            switch (envelope_ctrl_type) {
            case 0x10 :
                if ((instrument_envelope = envelope->envelope)) {
                    envelope->tempo           = instrument_envelope->tempo;
                    envelope->sustain_counted = 0;
                    envelope->loop_counted    = 0;
                    envelope->tempo_count     = 0;
                    envelope->sustain_start   = instrument_envelope->sustain_start;
                    envelope->sustain_end     = instrument_envelope->sustain_end;
                    envelope->sustain_count   = instrument_envelope->sustain_count;
                    envelope->loop_start      = instrument_envelope->loop_start;
                    envelope->loop_end        = instrument_envelope->loop_end;
                    envelope->loop_count      = instrument_envelope->loop_count;
                    envelope->value_min       = instrument_envelope->value_min;
                    envelope->value_max       = instrument_envelope->value_max;
                    envelope->rep_flags       = instrument_envelope->flags;

                    set_envelope(player_channel, envelope, envelope->pos);
                }

                break;
            case 0x01 :
                envelope->flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_NO_RETRIG;

                break;
            case 0x11 :
                envelope->flags &= ~AVSEQ_PLAYER_ENVELOPE_FLAG_NO_RETRIG;

                break;
            case 0x02 :
                envelope->flags &= ~AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM;

                break;
            case 0x12 :
                envelope->flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM;

                break;
            case 0x22 :
                envelope->flags &= ~AVSEQ_PLAYER_ENVELOPE_FLAG_RND_DELAY;

                break;
            case 0x32 :
                envelope->flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_RND_DELAY;

                break;
            case 0x03 :
                envelope->flags &= ~AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD;

                break;
            case 0x13 :
                envelope->flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD;

                break;
            default :
                player_host_channel->env_ctrl_change = envelope_ctrl_type;

                if (data_word & 0x8000)
                    set_envelope_value(avctx, player_host_channel, player_channel, channel, AVSEQ_TRACK_EFFECT_CMD_SET_ENV_VAL, player_host_channel->env_ctrl);

                break;
            }
        }
    }
}

EXECUTE_EFFECT(nna_ctrl)
{
    const uint8_t nna_ctrl_type = data_word >> 8;
    uint8_t nna_ctrl_action     = data_word;

    switch (nna_ctrl_type) {
    case 0x00 :
        switch (nna_ctrl_action) {
        case 0x00 :
            player_host_channel->nna = AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_CUT;

            break;
        case 0x01 :
            player_host_channel->nna = AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_OFF;

            break;
        case 0x02 :
            player_host_channel->nna = AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_CONTINUE;

            break;
        case 0x03 :
            player_host_channel->nna = AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_FADE;

            break;
        }

        break;
    case 0x11 :
        if (!nna_ctrl_action)
            nna_ctrl_action = 0xFF;

        player_host_channel->dct |= nna_ctrl_action;

        break;
    case 0x01 :
        if (!nna_ctrl_action)
            nna_ctrl_action = 0xFF;

        player_host_channel->dct &= ~nna_ctrl_action;

        break;
    case 0x02 :
        player_host_channel->dna  = nna_ctrl_action;

        break;
    }
}

EXECUTE_EFFECT(loop_ctrl)
{
    // TODO: Implement loop control effect
}

EXECUTE_EFFECT(set_speed)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    uint16_t *speed_ptr;
    uint16_t speed_value, speed_min_value, speed_max_value;
    uint8_t speed_type                             = data_word >> 12;

    if ((speed_ptr = get_speed_address(avctx, speed_type, &speed_min_value, &speed_max_value))) {
        if (!(speed_value = (data_word & 0xFFF))) {
            if ((data_word & 0x7000) == 0x7000)
                speed_value = (player_globals->speed_mul << 8U) + player_globals->speed_div;
            else
                speed_value = *speed_ptr;
        }

        speed_val_ok(avctx, speed_ptr, speed_value, speed_type, speed_min_value, speed_max_value);
    }
}

EXECUTE_EFFECT(speed_slide_faster)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v3, v4, v5;

    if (!data_word)
        data_word = player_globals->speed_slide_faster;

    do_speed_slide(avctx, data_word);

    track = player_host_channel->track;

    v3 = player_globals->speed_slide_slower;
    v4 = player_globals->fine_speed_slide_fast;
    v5 = player_globals->fine_speed_slide_slow;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v4 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v3 = data_word;
        v5 = v4;
    }

    player_globals->speed_slide_faster    = data_word;
    player_globals->speed_slide_slower    = v3;
    player_globals->fine_speed_slide_fast = v4;
    player_globals->fine_speed_slide_slow = v5;
}

EXECUTE_EFFECT(speed_slide_slower)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v3, v4;

    if (!data_word)
        data_word = player_globals->speed_slide_slower;

    do_speed_slide_slower(avctx, data_word);

    track = player_host_channel->track;

    v0 = player_globals->speed_slide_faster;
    v3 = player_globals->fine_speed_slide_fast;
    v4 = player_globals->fine_speed_slide_slow;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v4 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = data_word;
        v3 = v4;
    }

    player_globals->speed_slide_faster    = v0;
    player_globals->speed_slide_slower    = data_word;
    player_globals->fine_speed_slide_fast = v3;
    player_globals->fine_speed_slide_slow = v4;
}

EXECUTE_EFFECT(fine_speed_slide_faster)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v1, v4;

    if (!data_word)
        data_word = player_globals->fine_speed_slide_fast;

    do_speed_slide(avctx, data_word);

    track = player_host_channel->track;

    v0 = player_globals->speed_slide_faster;
    v1 = player_globals->speed_slide_slower;
    v4 = player_globals->fine_speed_slide_slow;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v0 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v1 = v0;
        v4 = data_word;
    }

    player_globals->speed_slide_faster    = v0;
    player_globals->speed_slide_slower    = v1;
    player_globals->fine_speed_slide_fast = data_word;
    player_globals->fine_speed_slide_slow = v4;
}

EXECUTE_EFFECT(fine_speed_slide_slower)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3;

    if (!data_word)
        data_word = player_globals->fine_speed_slide_slow;

    do_speed_slide_slower(avctx, data_word);

    track = player_host_channel->track;

    v0 = player_globals->speed_slide_faster;
    v1 = player_globals->speed_slide_slower;
    v3 = player_globals->fine_speed_slide_fast;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v1 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = v1;
        v3 = data_word;
    }

    player_globals->speed_slide_faster    = v0;
    player_globals->speed_slide_slower    = v1;
    player_globals->fine_speed_slide_fast = v3;
    player_globals->fine_speed_slide_slow = data_word;
}

EXECUTE_EFFECT(speed_slide_to)
{
    // TODO: Implement speed slide to effect
}

EXECUTE_EFFECT(spenolo) {
    // TODO: Implement spenolo effect
}

EXECUTE_EFFECT(channel_ctrl)
{
    const uint8_t channel_ctrl_byte = data_word;

    switch (data_word >> 8) {
    case 0x00 :


        break;
    case 0x01 :


        break;
    case 0x02 :


        break;
    case 0x03 :


        break;
    case 0x04 :


        break;
    case 0x05 :


        break;
    case 0x06 :


        break;
    case 0x07 :


        break;
    case 0x08 :


        break;
    case 0x09 :


        break;
    case 0x0A :


        break;
    case 0x10 :
        switch (channel_ctrl_byte) {
        case 0x00 :
            if (check_surround_track_panning(player_host_channel, player_channel, channel, 0)) {
                player_host_channel->flags          &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                player_channel->flags               &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
            }

            break;
        case 0x01 :
            if (check_surround_track_panning(player_host_channel, player_channel, channel, 1)) {
                player_host_channel->flags          |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                player_channel->flags               |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
            }

            break;
        case 0x10 :
            player_host_channel->flags              &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHANNEL_SUR_PAN;

            break;
        case 0x11 :
            player_host_channel->flags              |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHANNEL_SUR_PAN;

            break;
        case 0x20 :
            avctx->player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;

            break;
        case 0x21 :
            avctx->player_globals->flags |= AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;

            break;
        }

        break;
    case 0x11 :


        break;
    }
}

EXECUTE_EFFECT(set_global_volume)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;

    if (check_old_track_volume(avctx, &data_word)) {
        player_globals->global_volume     = data_word >> 8;
        player_globals->global_sub_volume = data_word;
    }
}

EXECUTE_EFFECT(global_volume_slide_up)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v3, v4, v5;

    if (!data_word)
        data_word = player_globals->global_vol_slide_up;

    do_global_volume_slide(avctx, player_globals, data_word);

    track = player_host_channel->track;

    v3 = player_globals->global_vol_slide_down;
    v4 = player_globals->fine_global_vol_sl_up;
    v5 = player_globals->fine_global_vol_sl_down;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v4 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v3 = data_word;
        v5 = v4;
    }

    player_globals->global_vol_slide_up     = data_word;
    player_globals->global_vol_slide_down   = v3;
    player_globals->fine_global_vol_sl_up   = v4;
    player_globals->fine_global_vol_sl_down = v5;
}

EXECUTE_EFFECT(global_volume_slide_down)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v3, v4;

    if (!data_word)
        data_word = player_globals->global_vol_slide_down;

    do_global_volume_slide_down(avctx, player_globals, data_word);

    track = player_host_channel->track;

    v0 = player_globals->global_vol_slide_up;
    v3 = player_globals->fine_global_vol_sl_up;
    v4 = player_globals->fine_global_vol_sl_down;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v4 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = data_word;
        v3 = v4;
    }

    player_globals->global_vol_slide_up     = v0;
    player_globals->global_vol_slide_down   = data_word;
    player_globals->fine_global_vol_sl_up   = v3;
    player_globals->fine_global_vol_sl_down = v4;
}

EXECUTE_EFFECT(fine_global_volume_slide_up)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v1, v4;

    if (!data_word)
        data_word = player_globals->fine_global_vol_sl_up;

    do_global_volume_slide(avctx, player_globals, data_word);

    track = player_host_channel->track;

    v0 = player_globals->global_vol_slide_up;
    v1 = player_globals->global_vol_slide_down;
    v4 = player_globals->fine_global_vol_sl_down;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        data_word = v0;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v1 = v0;
        v4 = data_word;
    }

    player_globals->global_vol_slide_up     = v0;
    player_globals->global_vol_slide_down   = v1;
    player_globals->fine_global_vol_sl_up   = data_word;
    player_globals->fine_global_vol_sl_down = v4;
}

EXECUTE_EFFECT(fine_global_volume_slide_down)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3;

    if (!data_word)
        data_word = player_globals->global_vol_slide_down;

    do_global_volume_slide_down(avctx, player_globals, data_word);

    track = player_host_channel->track;

    v0 = player_globals->global_vol_slide_up;
    v1 = player_globals->global_vol_slide_down;
    v3 = player_globals->fine_global_vol_sl_up;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES)
        v1 = data_word;

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = v1;
        v3 = data_word;
    }

    player_globals->global_vol_slide_up     = v0;
    player_globals->global_vol_slide_down   = v1;
    player_globals->fine_global_vol_sl_up   = v3;
    player_globals->fine_global_vol_sl_down = data_word;
}

EXECUTE_EFFECT(global_volume_slide_to)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    uint8_t global_volume_slide_to_value, global_volume_slide_to_volume;

    if (!(global_volume_slide_to_value = (uint8_t) data_word))
        global_volume_slide_to_value = player_globals->global_volume_slide_to;

    player_globals->global_volume_slide_to     = global_volume_slide_to_value;
    player_globals->global_volume_slide_to_slide &= 0x00FF;
    player_globals->global_volume_slide_to_slide += global_volume_slide_to_value << 8;

    global_volume_slide_to_volume = data_word >> 8;

    if (global_volume_slide_to_volume && (global_volume_slide_to_volume < 0xFF)) {
        player_globals->global_volume_sl_to_volume = global_volume_slide_to_volume;
    } else if (global_volume_slide_to_volume) {
        const uint16_t global_volume_slide_target = (global_volume_slide_to_volume << 8) + player_globals->global_volume_sl_to_sub_volume;
        uint16_t global_volume                    = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

        if (global_volume < global_volume_slide_target) {
            do_global_volume_slide(avctx, player_globals, player_globals->global_volume_slide_to_slide);

            global_volume = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

            if (global_volume_slide_target <= global_volume) {
                player_globals->global_volume     = global_volume_slide_target >> 8;
                player_globals->global_sub_volume = global_volume_slide_target;
            }
        } else {
            do_global_volume_slide_down(avctx, player_globals, player_globals->global_volume_slide_to_slide);

            global_volume = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

            if (global_volume_slide_target >= global_volume) {
                player_globals->global_volume     = global_volume_slide_target >> 8;
                player_globals->global_sub_volume = global_volume_slide_target;
            }
        }
    }
}

EXECUTE_EFFECT(global_tremolo)
{
    const AVSequencerSong *const song              = avctx->player_song;
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    int16_t global_tremolo_slide_value;
    uint8_t global_tremolo_rate;
    int16_t global_tremolo_depth;
    uint16_t global_volume;

    if (!(global_tremolo_rate = (data_word >> 8)))
        global_tremolo_rate = player_globals->tremolo_rate;

    player_globals->tremolo_rate = global_tremolo_rate;

    if (!(global_tremolo_depth = (int8_t) data_word))
        global_tremolo_depth = (int8_t) player_globals->tremolo_depth;

    player_globals->tremolo_depth = global_tremolo_depth;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (global_tremolo_depth > 63)
            global_tremolo_depth = 63;

        if (global_tremolo_depth < -63)
            global_tremolo_depth = -63;
    }

    global_tremolo_slide_value = (-(int32_t) global_tremolo_depth * run_envelope(avctx, &player_globals->tremolo_env, global_tremolo_rate, 0)) >> 7;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES)
        global_tremolo_slide_value <<= 2;

    global_volume               = player_globals->global_volume;
    global_tremolo_slide_value -= player_globals->tremolo_slide;

    if ((int16_t) (global_tremolo_slide_value += global_volume) < 0)
        global_tremolo_slide_value = 0;

    if (global_tremolo_slide_value > 255)
        global_tremolo_slide_value = 255;

    player_globals->global_volume  = global_tremolo_slide_value;
    player_globals->tremolo_slide -= global_volume - global_tremolo_slide_value;
}

EXECUTE_EFFECT(set_global_panning)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;

    player_globals->global_panning     = data_word >> 8;
    player_globals->global_sub_panning = data_word;
    player_globals->flags             &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;
}

EXECUTE_EFFECT(global_panning_slide_left)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v3, v4, v5, v8;

    if (!data_word)
        data_word = player_globals->global_pan_slide_left;

    do_global_panning_slide(player_globals, data_word);

    track = player_host_channel->track;

    v3 = player_globals->global_pan_slide_right;
    v4 = player_globals->fine_global_pan_sl_left;
    v5 = player_globals->fine_global_pan_sl_right;
    v8 = player_globals->global_pan_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v4 = data_word;
        v8 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v3 = data_word;
        v5 = v4;
    }

    player_globals->global_pan_slide_left     = data_word;
    player_globals->global_pan_slide_right    = v3;
    player_globals->fine_global_pan_sl_left   = v4;
    player_globals->fine_global_pan_sl_right  = v5;
    player_globals->global_pan_slide_to_slide = v8;
}

EXECUTE_EFFECT(global_panning_slide_right)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v3, v4, v5;

    if (!data_word)
        data_word = player_globals->global_pan_slide_right;

    do_global_panning_slide_right(player_globals, data_word);

    track = player_host_channel->track;

    v0 = player_globals->global_pan_slide_left;
    v3 = player_globals->fine_global_pan_sl_left;
    v4 = player_globals->fine_global_pan_sl_right;
    v5 = player_globals->global_pan_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v4 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = data_word;
        v3 = v4;
    }

    player_globals->global_pan_slide_left     = v0;
    player_globals->global_pan_slide_right    = data_word;
    player_globals->fine_global_pan_sl_left   = v3;
    player_globals->fine_global_pan_sl_right  = v4;
    player_globals->global_pan_slide_to_slide = v5;
}

EXECUTE_EFFECT(fine_global_panning_slide_left)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v1, v4, v5;

    if (!data_word)
        data_word = player_globals->fine_global_pan_sl_left;

    do_global_panning_slide(player_globals, data_word);

    track = player_host_channel->track;

    v0 = player_globals->global_pan_slide_left;
    v1 = player_globals->global_pan_slide_right;
    v4 = player_globals->fine_global_pan_sl_right;
    v5 = player_globals->global_pan_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v0 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v1 = v0;
        v4 = data_word;
    }

    player_globals->global_pan_slide_left     = v0;
    player_globals->global_pan_slide_right    = v1;
    player_globals->fine_global_pan_sl_left   = data_word;
    player_globals->fine_global_pan_sl_right  = v4;
    player_globals->global_pan_slide_to_slide = v5;
}

EXECUTE_EFFECT(fine_global_panning_slide_right)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    const AVSequencerTrack *track;
    uint16_t v0, v1, v3, v5;

    if (!data_word)
        data_word = player_globals->fine_global_pan_sl_right;

    do_global_panning_slide_right(player_globals, data_word);

    track = player_host_channel->track;

    v0 = player_globals->global_pan_slide_left;
    v1 = player_globals->global_pan_slide_right;
    v3 = player_globals->fine_global_pan_sl_left;
    v5 = player_globals->global_pan_slide_to_slide;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
        v1 = data_word;
        v5 = data_word;
    }

    if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
        v0 = v1;
        v3 = data_word;
    }

    player_globals->global_pan_slide_left     = v0;
    player_globals->global_pan_slide_right    = v1;
    player_globals->fine_global_pan_sl_left   = v3;
    player_globals->fine_global_pan_sl_right  = data_word;
    player_globals->global_pan_slide_to_slide = v5;
}

EXECUTE_EFFECT(global_panning_slide_to)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    uint8_t global_pan_slide_to, global_pan_slide_to_panning;

    if (!(global_pan_slide_to = (uint8_t) data_word))
        global_pan_slide_to = player_globals->global_pan_slide_to;

    player_globals->global_pan_slide_to        = global_pan_slide_to;
    player_globals->global_pan_slide_to_slide &= 0x00FF;
    player_globals->global_pan_slide_to_slide += global_pan_slide_to << 8;
    global_pan_slide_to_panning                = data_word >> 8;

    if (global_pan_slide_to_panning && (global_pan_slide_to_panning < 0xFF)) {
        player_globals->global_pan_slide_to_panning = global_pan_slide_to_panning;
    } else if (global_pan_slide_to_panning) {
        const uint16_t global_panning_slide_target = ((uint8_t) global_pan_slide_to_panning << 8) + player_globals->global_pan_slide_to_sub_panning;
        uint16_t global_panning                    = ((uint8_t) player_globals->global_panning << 8) + player_globals->global_sub_panning;

        if (global_panning < global_panning_slide_target) {
            do_global_panning_slide_right(player_globals, player_globals->global_pan_slide_to_slide);

            global_panning = ((uint8_t) player_globals->global_panning << 8) + player_globals->global_sub_panning;

            if (global_panning_slide_target <= global_panning) {
                player_globals->global_panning     = global_panning_slide_target >> 8;
                player_globals->global_sub_panning = global_panning_slide_target;
            }
        } else {
            do_global_panning_slide(player_globals, player_globals->global_pan_slide_to_slide);

            global_panning = ((uint8_t) player_globals->global_panning << 8) + player_globals->global_sub_panning;

            if (global_panning_slide_target >= global_panning) {
                player_globals->global_panning     = global_panning_slide_target >> 8;
                player_globals->global_sub_panning = global_panning_slide_target;
            }
        }
    }
}

EXECUTE_EFFECT(global_pannolo)
{
    AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
    int16_t global_pannolo_slide_value;
    uint8_t global_pannolo_rate;
    int16_t global_pannolo_depth;
    uint16_t global_panning;

    if (!(global_pannolo_rate = (data_word >> 8)))
        global_pannolo_rate = player_globals->pannolo_rate;

    player_globals->pannolo_rate = global_pannolo_rate;

    if (!(global_pannolo_depth = (int8_t) data_word))
        global_pannolo_depth = player_globals->pannolo_depth;

    player_globals->pannolo_depth = global_pannolo_depth;
    global_pannolo_slide_value    = (-(int32_t) global_pannolo_depth * run_envelope(avctx, &player_globals->pannolo_env, global_pannolo_rate, 0)) >> 7;
    global_panning                = (uint8_t) player_globals->global_panning;
    global_pannolo_slide_value   -= player_globals->pannolo_slide;

    if ((int16_t) (global_pannolo_slide_value += global_panning) < 0)
        global_pannolo_slide_value = 0;

    if (global_pannolo_slide_value > 255)
        global_pannolo_slide_value = 255;

    player_globals->global_panning = global_pannolo_slide_value;
    player_globals->pannolo_slide -= global_panning - global_pannolo_slide_value;
}

EXECUTE_EFFECT(user_sync)
{
}

static void se_vibrato_do(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, int32_t vibrato_slide_value)
{
    AVSequencerPlayerHostChannel *const player_host_channel = avctx->player_host_channel + player_channel->host_channel;
    uint32_t old_frequency                                  = player_channel->frequency;

    player_channel->frequency -= player_channel->vibrato_slide;

    if (vibrato_slide_value < 0) {
        vibrato_slide_value = -vibrato_slide_value;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
            linear_slide_up(avctx, player_channel, player_channel->frequency, vibrato_slide_value);
        else
            amiga_slide_up(player_channel, player_channel->frequency, vibrato_slide_value);
    } else if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ) {
        linear_slide_down(avctx, player_channel, player_channel->frequency, vibrato_slide_value);
    } else {
        amiga_slide_down(player_channel, player_channel->frequency, vibrato_slide_value);
    }

    player_channel->vibrato_slide -= old_frequency - player_channel->frequency;
}

static void se_arpegio_do(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, const int16_t arpeggio_transpose, uint8_t arpeggio_finetune)
{
    const uint32_t *frequency_lut;
    uint32_t frequency, next_frequency, slide_frequency, old_frequency;
    uint16_t octave;
    int16_t note;
    int32_t finetune = arpeggio_finetune;

    octave = arpeggio_transpose / 12;
    note   = arpeggio_transpose % 12;

    if (note < 0) {
        octave--;
        note    += 12;
        finetune = -finetune;
    }

    frequency_lut                   = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
    frequency                       = *frequency_lut++;
    next_frequency                  = *frequency_lut - frequency;
    frequency                      += ((int32_t) finetune * (int32_t) next_frequency) >> 8;
    old_frequency                   = player_channel->frequency;
    slide_frequency                 = player_channel->arpeggio_slide + old_frequency;
    player_channel->frequency       = frequency = ((uint64_t) frequency * slide_frequency) >> (24 - octave);
    player_channel->arpeggio_slide += old_frequency - frequency;
}

static void se_tremolo_do(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, int32_t tremolo_slide_value)
{
    uint16_t volume = player_channel->volume;

    tremolo_slide_value -= player_channel->tremolo_slide;

    if ((tremolo_slide_value += volume) < 0)
        tremolo_slide_value = 0;

    if (tremolo_slide_value > 255)
        tremolo_slide_value = 255;

    player_channel->volume                  = tremolo_slide_value;
    player_channel->tremolo_slide          -= volume - tremolo_slide_value;
}

static void se_pannolo_do(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, int32_t pannolo_slide_value)
{
    uint16_t panning = (uint8_t) player_channel->panning;

    pannolo_slide_value -= player_channel->pannolo_slide;

    if ((pannolo_slide_value += panning) < 0)
        pannolo_slide_value = 0;

    if (pannolo_slide_value > 255)
        pannolo_slide_value = 255;

    player_channel->panning                 = pannolo_slide_value;
    player_channel->pannolo_slide          -= panning - pannolo_slide_value;
}

#define EXECUTE_SYNTH_CODE_INSTRUCTION(fx_type)                                     \
    static uint16_t se_##fx_type(AVSequencerContext *const avctx,                   \
                                 AVSequencerPlayerChannel *const player_channel,    \
                                 const uint16_t virtual_channel,                    \
                                 uint16_t synth_code_line,                          \
                                 const int src_var,                                 \
                                 int dst_var,                                       \
                                 uint16_t instruction_data,                         \
                                 const int synth_type)

EXECUTE_SYNTH_CODE_INSTRUCTION(stop)
{
    instruction_data += player_channel->variable[src_var];

    if (instruction_data & 0x8000)
        player_channel->stop_forbid_mask &= ~instruction_data;
    else
        player_channel->stop_forbid_mask |= instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(kill)
{
    instruction_data                              += player_channel->variable[src_var];
    player_channel->kill_count[synth_type]         = instruction_data;
    player_channel->synth_flags                   |= 1 << synth_type;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(wait)
{
    instruction_data                      += player_channel->variable[src_var];
    player_channel->wait_count[synth_type] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(waitvol)
{
    instruction_data                     += player_channel->variable[src_var];
    player_channel->wait_line[synth_type] = instruction_data;
    player_channel->wait_type[synth_type] = ~0;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(waitpan)
{
    instruction_data                     += player_channel->variable[src_var];
    player_channel->wait_line[synth_type] = instruction_data;
    player_channel->wait_type[synth_type] = ~1;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(waitsld)
{
    instruction_data                     += player_channel->variable[src_var];
    player_channel->wait_line[synth_type] = instruction_data;
    player_channel->wait_type[synth_type] = ~2;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(waitspc)
{
    instruction_data                     += player_channel->variable[src_var];
    player_channel->wait_line[synth_type] = instruction_data;
    player_channel->wait_type[synth_type] = ~3;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jump)
{
    instruction_data += player_channel->variable[src_var];

    return instruction_data;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpeq)
{
    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpne)
{
    if (!(player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumppl)
{
    if (!(player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpmi)
{
    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumplt)
{
    uint16_t synth_cond_var = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE);

    if ((synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW) || (synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumple)
{
    uint16_t synth_cond_var = player_channel->cond_var[synth_type];

    if (synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO) {
        synth_cond_var = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE);

        if ((synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW) || (synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE)) {
            instruction_data += player_channel->variable[src_var];

            return instruction_data;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpgt)
{
    uint16_t synth_cond_var = player_channel->cond_var[synth_type];

    if (!(synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO)) {
        synth_cond_var = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE);

        if (!((synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW) || (synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE))) {
            instruction_data += player_channel->variable[src_var];

            return instruction_data;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpge)
{
    uint16_t synth_cond_var = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE);

    if (!((synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW) || (synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE))) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvs)
{
    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvc)
{
    if (!(player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpcs)
{
    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpcc)
{
    if (!(player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpls)
{
    uint16_t synth_cond_var = player_channel->cond_var[synth_type];

    if ((synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO) && (synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumphi)
{
    uint16_t synth_cond_var = player_channel->cond_var[synth_type];

    if (!((synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO) && (synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY))) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvol)
{
    if (!(player_channel->stop_forbid_mask & 1)) {
        instruction_data            += player_channel->variable[src_var];
        player_channel->entry_pos[0] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumppan)
{
    if (!(player_channel->stop_forbid_mask & 2)) {
        instruction_data            += player_channel->variable[src_var];
        player_channel->entry_pos[1] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpsld)
{
    if (!(player_channel->stop_forbid_mask & 4)) {
        instruction_data            += player_channel->variable[src_var];
        player_channel->entry_pos[2] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpspc)
{
    if (!(player_channel->stop_forbid_mask & 8)) {
        instruction_data            += player_channel->variable[src_var];
        player_channel->entry_pos[3] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(call)
{
    player_channel->variable[dst_var] = synth_code_line;
    instruction_data                 += player_channel->variable[src_var];

    return instruction_data;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ret)
{
    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = --synth_code_line;

    return instruction_data;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(posvar)
{
    player_channel->variable[src_var] += synth_code_line + --instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(load)
{
    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(add)
{
    uint16_t flags = 0;
    int32_t add_data;

    instruction_data                 += player_channel->variable[src_var];
    add_data                          = (int16_t) player_channel->variable[dst_var] + (int16_t) instruction_data;

    if ((((int16_t) player_channel->variable[dst_var] ^ add_data) & ((int16_t) instruction_data ^ add_data)) < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    player_channel->variable[dst_var] = add_data;

    if (player_channel->variable[dst_var] < instruction_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if (!add_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (add_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(addx)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;
    uint32_t add_unsigned_data;
    int32_t add_data;

    instruction_data += player_channel->variable[src_var];
    add_data          = (int16_t) player_channel->variable[dst_var] + (int16_t) instruction_data;
    add_unsigned_data = instruction_data;

    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND) {
        add_data++;
        add_unsigned_data++;

        if ((((int16_t) player_channel->variable[dst_var] ^ add_data) & ((int16_t) ++instruction_data ^ add_data)) < 0)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
    } else if ((((int16_t) player_channel->variable[dst_var] ^ add_data) & ((int16_t) instruction_data ^ add_data)) < 0) {
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
    }

    player_channel->variable[dst_var] = add_data;

    if ((player_channel->variable[dst_var] < add_unsigned_data))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if (add_data)
        flags &= ~AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (add_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(sub)
{
    uint16_t flags = 0;
    int32_t sub_data;

    instruction_data += player_channel->variable[src_var];
    sub_data          = (int16_t) player_channel->variable[dst_var] - (int16_t) instruction_data;

    if (player_channel->variable[dst_var] < instruction_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((((int16_t) player_channel->variable[dst_var] ^ sub_data) & (((int16_t) -instruction_data) ^ sub_data)) < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    player_channel->variable[dst_var] = sub_data;

    if (!sub_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (sub_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(subx)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;
    uint32_t sub_unsigned_data;
    int32_t sub_data;

    instruction_data += player_channel->variable[src_var];
    sub_data          = (int16_t) player_channel->variable[dst_var] - (int16_t) instruction_data;
    sub_unsigned_data = instruction_data;

    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND) {
        sub_data--;
        sub_unsigned_data++;

        if ((((int16_t) player_channel->variable[dst_var] ^ sub_data) & ((int16_t) -(++instruction_data) ^ sub_data)) < 0)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
    } else if ((((int16_t) player_channel->variable[dst_var] ^ sub_data) & (((int16_t) -instruction_data) ^ sub_data)) < 0) {
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
    }

    if (player_channel->variable[dst_var] < sub_unsigned_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    player_channel->variable[dst_var] = sub_data;

    if (sub_data)
        flags &= ~AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (sub_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(cmp)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int32_t sub_data;

    instruction_data += player_channel->variable[src_var];
    sub_data          = (int16_t) player_channel->variable[dst_var] - (int16_t) instruction_data;

    if ((((int16_t) player_channel->variable[dst_var] ^ sub_data) & (((int16_t) -instruction_data) ^ sub_data)) < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if (player_channel->variable[dst_var] < instruction_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY;

    if (!sub_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (sub_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(mulu)
{
    uint32_t umult_res, flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = umult_res = (uint32_t) player_channel->variable[dst_var] * (uint16_t) instruction_data;

    if (!umult_res)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (umult_res >= 0x10000)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if ((int16_t) umult_res < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(muls)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int32_t smult_res;

    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = smult_res = (int32_t) player_channel->variable[dst_var] * (int16_t) instruction_data;

    if (!smult_res)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if ((smult_res < -0x8000) || (smult_res > 0x7FFF))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if ((int16_t) smult_res < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(dmulu)
{
    uint32_t umult_res;
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    instruction_data |= player_channel->variable[src_var];
    umult_res         = player_channel->variable[dst_var] * instruction_data;

    if (dst_var == 15) {
        player_channel->variable[dst_var] = umult_res;

        if (umult_res >= 0x10000)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

        umult_res <<= 16;
    } else {
        player_channel->variable[dst_var++] = umult_res >> 16;
        player_channel->variable[dst_var]   = umult_res;
    }

    if (!umult_res)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if ((int32_t) umult_res < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(dmuls)
{
    int32_t smult_res;
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    instruction_data += player_channel->variable[src_var];
    smult_res         = (int32_t) player_channel->variable[dst_var] * (int16_t) instruction_data;

    if (dst_var == 15) {
        player_channel->variable[dst_var] = smult_res;

        if ((smult_res < -0x8000) || (smult_res > 0x7FFF))
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

        smult_res <<= 16;
    } else {
        player_channel->variable[dst_var++] = smult_res >> 16;
        player_channel->variable[dst_var]   = smult_res;
    }

    if (!smult_res)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (smult_res < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(divu)
{
    uint16_t udiv_res, flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((instruction_data += player_channel->variable[src_var])) {
        player_channel->variable[dst_var] = udiv_res = player_channel->variable[dst_var] / instruction_data;

        if (!udiv_res)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

        if ((int16_t) udiv_res < 0)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    } else {
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    }

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(divs)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int16_t sdiv_res;

    if ((instruction_data += player_channel->variable[src_var])) {
        player_channel->variable[dst_var] = sdiv_res = (int16_t) player_channel->variable[dst_var] / (int16_t) instruction_data;

        if (!sdiv_res)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

        if (sdiv_res < 0)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    } else {
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    }

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(modu)
{
    uint16_t umod_res, flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((instruction_data += player_channel->variable[src_var])) {
        player_channel->variable[dst_var] = umod_res = player_channel->variable[dst_var] % instruction_data;

        if (!umod_res)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

        if ((int16_t) umod_res < 0)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    } else {
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    }

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(mods)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int16_t smod_res;

    if ((instruction_data += player_channel->variable[src_var])) {
        player_channel->variable[dst_var] = smod_res = (int16_t) player_channel->variable[dst_var] % (int16_t) instruction_data;

        if (!smod_res)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

        if (smod_res < 0)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    } else
    {
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    }

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ddivu)
{
    uint32_t udiv_res;
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((instruction_data += player_channel->variable[src_var])) {
        if (dst_var == 15) {
            player_channel->variable[dst_var] = udiv_res = ((uint32_t) player_channel->variable[dst_var] << 16) / instruction_data;

            if (udiv_res < 0x10000) {
                if (!udiv_res)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

                if ((int32_t) udiv_res < 0)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
            } else {
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
            }
        } else {
            uint32_t dividend = ((uint32_t) player_channel->variable[dst_var + 1] << 16) + player_channel->variable[dst_var];

            if ((udiv_res = (dividend / instruction_data)) < 0x10000) {
                player_channel->variable[dst_var--] = udiv_res;
                player_channel->variable[dst_var]   = dividend % instruction_data;

                if (!udiv_res)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

                if ((int32_t) udiv_res < 0)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
            } else
            {
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
            }
        }
    } else {
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    }

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ddivs)
{
    int32_t sdiv_res;
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((instruction_data += player_channel->variable[src_var])) {
        if (dst_var == 15) {
            player_channel->variable[dst_var] = sdiv_res = ((int32_t) player_channel->variable[dst_var] << 16) / (int16_t) instruction_data;

            if ((sdiv_res >= -0x8000) && (sdiv_res <= 0x7FFF)) {
                if (!sdiv_res)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

                if ((int32_t) sdiv_res < 0)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
            } else {
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
            }
        } else {
            int32_t dividend = ((int32_t) player_channel->variable[dst_var + 1] << 16) + (int16_t) player_channel->variable[dst_var];
            sdiv_res         = dividend / instruction_data;

            if ((sdiv_res >= -0x8000) && (sdiv_res <= 0x7FFF)) {
                player_channel->variable[dst_var--] = sdiv_res;
                player_channel->variable[dst_var]   = dividend % instruction_data;

                if (!sdiv_res)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

                if ((int32_t) sdiv_res < 0)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
            } else {
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
            }
        }
    } else {
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
    }

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ashl)
{
    uint16_t flags      = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND, high_bit;
    int16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    high_bit = shift_value & 0x8000;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);

        if (shift_value & 0x8000)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

        shift_value <<= 1;

        if (high_bit != (shift_value & 0x8000))
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
    }

    if (shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]    = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ashr)
{
    uint16_t flags      = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);

        if (shift_value & 1)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

        shift_value >>= 1;
    }

    if (shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]    = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(lshl)
{
    uint16_t flags       = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);

        if (shift_value & 0x8000)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

        shift_value <<= 1;
    }

    if ((int16_t) shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]    = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(lshr)
{
    uint16_t flags       = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);

        if (shift_value & 1)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

        shift_value >>= 1;
    }

    if ((int16_t) shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]    = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(rol)
{
    uint16_t flags       = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY);

        if (shift_value & 0x8000)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY;

        shift_value <<= 1;

        if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY)
            shift_value++;
    }

    if ((int16_t) shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]    = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ror)
{
    uint16_t flags       = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY);

        if (shift_value & 1)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY;

        shift_value >>= 1;

        if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY)
            shift_value += 0x8000;
    }

    if ((int16_t) shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]    = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(rolx)
{
    uint16_t flags       = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);
    uint16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY);

        if (shift_value & 0x8000)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY;

        shift_value <<= 1;

        if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND) {
            if (!(flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY))
                flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);

            shift_value++;
        } else if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY) {
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
        }
    }

    if ((int16_t) shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]    = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(rorx)
{
    uint16_t flags       = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);
    uint16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY);

        if (shift_value & 1)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY;

        shift_value >>= 1;

        if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND) {
            if (!(flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY))
                flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);

            shift_value += 0x8000;
        } else if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY) {
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
        }
    }

    if ((int16_t) shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]    = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(or)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t logic_value;

    instruction_data += player_channel->variable[src_var];
    logic_value       = (player_channel->variable[dst_var] |= instruction_data);

    if ((int16_t) logic_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!logic_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(and)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t logic_value;

    instruction_data += player_channel->variable[src_var];
    logic_value       = (player_channel->variable[dst_var] &= instruction_data);

    if ((int16_t) logic_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!logic_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(xor)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t logic_value;

    instruction_data += player_channel->variable[src_var];
    logic_value       = (player_channel->variable[dst_var] ^= instruction_data);

    if ((int16_t) logic_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!logic_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(not)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t logic_value;

    instruction_data                 += player_channel->variable[src_var];
    logic_value                       = ~player_channel->variable[dst_var];
    player_channel->variable[dst_var] = logic_value + instruction_data;

    if (!logic_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if ((int16_t) logic_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(neg)
{
    uint16_t flags = 0;
    int16_t sub_data;

    instruction_data += player_channel->variable[src_var];

    sub_data                          = -player_channel->variable[dst_var];
    player_channel->variable[dst_var] = sub_data + instruction_data;

    if (sub_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if (sub_data == -0x8000)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if (!sub_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (sub_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(negx)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;
    int16_t sub_data;

    instruction_data += player_channel->variable[src_var];
    sub_data          = -player_channel->variable[dst_var];

    if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND)
        sub_data--;

    player_channel->variable[dst_var] = sub_data + instruction_data;

    if ((sub_data == -0x8000) && (!(flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND)))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY);

    if (sub_data || (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if (sub_data)
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO);

    if (sub_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(extb)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int16_t extend_value;

    instruction_data += player_channel->variable[src_var];

    extend_value                      = (int8_t) player_channel->variable[dst_var];
    player_channel->variable[dst_var] = extend_value + instruction_data;

    if (!extend_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (extend_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ext)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int32_t extend_value = 0;

    instruction_data += player_channel->variable[src_var];

    if (dst_var != 15)
        extend_value = (int16_t) player_channel->variable[dst_var + 1];

    player_channel->variable[dst_var] = (extend_value >> 16) + instruction_data;

    if (!extend_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (extend_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(xchg)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint32_t exchange_value;

    exchange_value                    = (player_channel->variable[dst_var] << 16) + player_channel->variable[src_var];
    player_channel->variable[dst_var] = exchange_value + instruction_data;
    player_channel->variable[src_var] = exchange_value >> 16;

    if (!exchange_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if ((int32_t) exchange_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(swap)
{
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t swap_value;

    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = swap_value = (player_channel->variable[dst_var] << 8) + (player_channel->variable[dst_var] >> 8);

    if (!swap_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if ((int16_t) swap_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getwave)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    const AVSequencerSynthWave *waveform       = player_channel->sample_waveform;
    uint32_t waveform_num                      = -1;

    instruction_data += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getwlen)
{
    uint16_t sample_length = -1;

    if (player_channel->mixer.len < 0x10000)
        sample_length = player_channel->mixer.len;

    instruction_data                 += player_channel->variable[src_var] + sample_length;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getwpos)
{
    uint16_t sample_pos = -1;

    if (player_channel->mixer.pos < 0x10000)
        sample_pos = player_channel->mixer.pos;

    instruction_data                 += player_channel->variable[src_var] + sample_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getchan)
{
    instruction_data                 += player_channel->variable[src_var] + player_channel->host_channel;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getnote)
{
    uint16_t note = player_channel->sample_note;

    instruction_data                 += player_channel->variable[src_var] + --note;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getrans)
{
    uint16_t note = player_channel->sample_note;

    instruction_data                 += player_channel->variable[src_var] + player_channel->final_note - --note;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getptch)
{
    uint32_t frequency = player_channel->frequency;

    instruction_data += player_channel->variable[src_var];
    frequency        += instruction_data;

    if (dst_var != 15)
        player_channel->variable[dst_var++] = frequency >> 16;

    player_channel->variable[dst_var] = frequency;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getper)
{
    uint32_t frequency = player_channel->frequency, period = 0;

    if (frequency)
        period = AVSEQ_SLIDE_CONST / frequency;

    instruction_data += player_channel->variable[src_var];
    period           += instruction_data;

    if (dst_var != 15)
        player_channel->variable[dst_var++] = period >> 16;

    player_channel->variable[dst_var] = period;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getfx)
{


    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getarpw)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    const AVSequencerSynthWave *waveform       = player_channel->arpeggio_waveform;
    uint32_t waveform_num                      = -1;

    instruction_data += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getarpv)
{
    const AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->arpeggio_waveform)) {
        uint32_t waveform_pos = instruction_data % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            player_channel->variable[dst_var] = ((uint8_t *) waveform->data)[waveform_pos] << 8;
        else
            player_channel->variable[dst_var] = waveform->data[waveform_pos];
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getarpl)
{
    const AVSequencerSynthWave *waveform;

    if ((waveform = player_channel->arpeggio_waveform)) {
        uint16_t waveform_length = -1;

        if (waveform->samples < 0x10000)
            waveform_length = waveform->samples;

        instruction_data                 += player_channel->variable[src_var] + waveform_length;
        player_channel->variable[dst_var] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getarpp)
{
    instruction_data                 += player_channel->variable[src_var] + player_channel->arpeggio_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getvibw)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    const AVSequencerSynthWave *waveform       = player_channel->vibrato_waveform;
    uint32_t waveform_num                      = -1;

    instruction_data += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    instruction_data += player_channel->variable[src_var];

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getvibv)
{
    const AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->vibrato_waveform)) {
        const uint32_t waveform_pos = instruction_data % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            player_channel->variable[dst_var] = ((uint8_t *) waveform->data)[waveform_pos] << 8;
        else
            player_channel->variable[dst_var] = waveform->data[waveform_pos];
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getvibl)
{
    const AVSequencerSynthWave *waveform;

    if ((waveform = player_channel->vibrato_waveform)) {
        uint16_t waveform_length = -1;

        if (waveform->samples < 0x10000)
            waveform_length = waveform->samples;

        instruction_data                 += player_channel->variable[src_var] + waveform_length;
        player_channel->variable[dst_var] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getvibp)
{
    instruction_data                 += player_channel->variable[src_var] + player_channel->vibrato_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmw)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    const AVSequencerSynthWave *waveform       = player_channel->tremolo_waveform;
    uint32_t waveform_num                      = -1;

    instruction_data += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmv)
{
    const AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->tremolo_waveform)) {
        const uint32_t waveform_pos = instruction_data % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            player_channel->variable[dst_var] = ((uint8_t *) waveform->data)[waveform_pos] << 8;
        else
            player_channel->variable[dst_var] = waveform->data[waveform_pos];
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(gettrml)
{
    const AVSequencerSynthWave *waveform;

    if ((waveform = player_channel->tremolo_waveform)) {
        uint16_t waveform_length = -1;

        if (waveform->samples < 0x10000)
            waveform_length = waveform->samples;

        instruction_data                 += player_channel->variable[src_var] + waveform_length;
        player_channel->variable[dst_var] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmp)
{
    instruction_data                 += player_channel->variable[src_var] + player_channel->tremolo_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getpanw)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    const AVSequencerSynthWave *waveform       = player_channel->pannolo_waveform;
    uint32_t waveform_num                      = -1;

    instruction_data += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getpanv)
{
    const AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->pannolo_waveform)) {
        uint32_t waveform_pos = instruction_data % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            player_channel->variable[dst_var] = ((uint8_t *) waveform->data)[waveform_pos] << 8;
        else
            player_channel->variable[dst_var] = waveform->data[waveform_pos];
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getpanl)
{
    const AVSequencerSynthWave *waveform;

    if ((waveform = player_channel->pannolo_waveform)) {
        uint16_t waveform_length = -1;

        if (waveform->samples < 0x10000)
            waveform_length = waveform->samples;

        instruction_data                 += player_channel->variable[src_var] + waveform_length;
        player_channel->variable[dst_var] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getpanp)
{
    instruction_data                 += player_channel->variable[src_var] + player_channel->pannolo_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getrnd)
{
    uint32_t seed;

    instruction_data                 += player_channel->variable[src_var];
    avctx->seed                       = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
    player_channel->variable[dst_var] = ((uint64_t) seed * instruction_data) >> 32;

    return synth_code_line;
}

/** Sine table for very fast sine calculation. Value is
   sin(x)*32767 with one element being one degree.  */
static const int16_t sine_lut[] = {
         0,    571,   1143,   1714,   2285,   2855,   3425,   3993,   4560,   5125,   5689,   6252,   6812,   7370,   7927,  8480,
      9031,   9580,  10125,  10667,  11206,  11742,  12274,  12803,  13327,  13847,  14364,  14875,  15383,  15885,  16383,  16876,
     17363,  17846,  18323,  18794,  19259,  19719,  20173,  20620,  21062,  21497,  21925,  22347,  22761,  23169,  23570,  23964,
     24350,  24729,  25100,  25464,  25820,  26168,  26509,  26841,  27165,  27480,  27787,  28086,  28377,  28658,  28931,  29195,
     29450,  29696,  29934,  30162,  30381,  30590,  30790,  30981,  31163,  31335,  31497,  31650,  31793,  31927,  32050,  32164,
     32269,  32363,  32448,  32522,  32587,  32642,  32687,  32722,  32747,  32762,  32767,  32762,  32747,  32722,  32687,  32642,
     32587,  32522,  32448,  32363,  32269,  32164,  32050,  31927,  31793,  31650,  31497,  31335,  31163,  30981,  30790,  30590,
     30381,  30162,  29934,  29696,  29450,  29195,  28931,  28658,  28377,  28086,  27787,  27480,  27165,  26841,  26509,  26168,
     25820,  25464,  25100,  24729,  24350,  23964,  23570,  23169,  22761,  22347,  21925,  21497,  21062,  20620,  20173,  19719,
     19259,  18794,  18323,  17846,  17363,  16876,  16383,  15885,  15383,  14875,  14364,  13847,  13327,  12803,  12274,  11742,
     11206,  10667,  10125,   9580,   9031,   8480,   7927,   7370,   6812,   6252,   5689,   5125,   4560,   3993,   3425,   2855,
      2285,   1714,   1143,    571,      0,   -571,  -1143,  -1714,  -2285,  -2855,  -3425,  -3993,  -4560,  -5125,  -5689,  -6252,
     -6812,  -7370,  -7927,  -8480,  -9031,  -9580, -10125, -10667, -11206, -11742, -12274, -12803, -13327, -13847, -14364, -14875,
    -15383, -15885, -16383, -16876, -17363, -17846, -18323, -18794, -19259, -19719, -20173, -20620, -21062, -21497, -21925, -22347,
    -22761, -23169, -23570, -23964, -24350, -24729, -25100, -25464, -25820, -26168, -26509, -26841, -27165, -27480, -27787, -28086,
    -28377, -28658, -28931, -29195, -29450, -29696, -29934, -30162, -30381, -30590, -30790, -30981, -31163, -31335, -31497, -31650,
    -31793, -31927, -32050, -32164, -32269, -32363, -32448, -32522, -32587, -32642, -32687, -32722, -32747, -32762, -32767, -32762,
    -32747, -32722, -32687, -32642, -32587, -32522, -32448, -32363, -32269, -32164, -32050, -31927, -31793, -31650, -31497, -31335,
    -31163, -30981, -30790, -30590, -30381, -30162, -29934, -29696, -29450, -29195, -28931, -28658, -28377, -28086, -27787, -27480,
    -27165, -26841, -26509, -26168, -25820, -25464, -25100, -24729, -24350, -23964, -23570, -23169, -22761, -22347, -21925, -21497,
    -21062, -20620, -20173, -19719, -19259, -18794, -18323, -17846, -17363, -16876, -16383, -15885, -15383, -14875, -14364, -13847,
    -13327, -12803, -12274, -11742, -11206, -10667, -10125,  -9580,  -9031,  -8480,  -7927,  -7370,  -6812,  -6252,  -5689,  -5125,
     -4560,  -3993,  -3425,  -2855,  -2285,  -1714,  -1143,   -571
};

EXECUTE_SYNTH_CODE_INSTRUCTION(getsine)
{
    int16_t degrees;

    instruction_data += player_channel->variable[src_var];
    degrees           = (int16_t) instruction_data % 360;

    if (degrees < 0)
        degrees += 360;

    player_channel->variable[dst_var] = (avctx->sine_lut ? avctx->sine_lut[degrees] : sine_lut[degrees]);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(portaup)
{
    AVSequencerPlayerHostChannel *const player_host_channel = avctx->player_host_channel + player_channel->host_channel;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->porta_up;

    player_channel->porta_up    = instruction_data;
    player_channel->portamento += instruction_data;

    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
        linear_slide_up(avctx, player_channel, player_channel->frequency, instruction_data);
    else
        amiga_slide_up(player_channel, player_channel->frequency, instruction_data);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(portadn)
{
    AVSequencerPlayerHostChannel *const player_host_channel = avctx->player_host_channel + player_channel->host_channel;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->porta_dn;

    player_channel->porta_dn    = instruction_data;
    player_channel->portamento -= instruction_data;

    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
        linear_slide_down(avctx, player_channel, player_channel->frequency, instruction_data);
    else
        amiga_slide_down(player_channel, player_channel->frequency, instruction_data);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibspd)
{
    instruction_data            += player_channel->variable[src_var];
    player_channel->vibrato_rate = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibdpth)
{
    instruction_data             += player_channel->variable[src_var];
    player_channel->vibrato_depth = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibwave)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    uint32_t waveform_num                      = -1;

    instruction_data                += player_channel->variable[src_var];
    player_channel->vibrato_waveform = NULL;

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->vibrato_waveform = waveform_list[waveform_num];

            break;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibwavp)
{
    const AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->vibrato_waveform))
        player_channel->vibrato_pos = instruction_data % waveform->samples;
    else
        player_channel->vibrato_pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibrato)
{
    const AVSequencerSynthWave *waveform;
    uint16_t vibrato_rate;
    int16_t vibrato_depth;

    instruction_data += player_channel->variable[src_var];

    if (!(vibrato_rate = (instruction_data >> 8)))
        vibrato_rate = player_channel->vibrato_rate;

    player_channel->vibrato_rate = vibrato_rate;
    vibrato_depth                = (instruction_data & 0xFF) << 2;

    if (!vibrato_depth)
        vibrato_depth = player_channel->vibrato_depth;

    player_channel->vibrato_depth = vibrato_depth;

    if ((waveform = player_channel->vibrato_waveform)) {
        uint32_t waveform_pos;
        int32_t vibrato_slide_value;

        waveform_pos = player_channel->vibrato_pos % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            vibrato_slide_value = ((int8_t *) waveform->data)[waveform_pos] << 8;
        else
            vibrato_slide_value = waveform->data[waveform_pos];

        vibrato_slide_value        *= -vibrato_depth;
        vibrato_slide_value       >>= 7 - 2;
        player_channel->vibrato_pos = (waveform_pos + vibrato_rate) % waveform->samples;

        se_vibrato_do(avctx, player_channel, vibrato_slide_value);
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibval)
{
    int32_t vibrato_slide_value;

    instruction_data   += player_channel->variable[src_var];
    vibrato_slide_value = (int16_t) instruction_data;

    se_vibrato_do(avctx, player_channel, vibrato_slide_value);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpspd)
{
    instruction_data              += player_channel->variable[src_var];
    player_channel->arpeggio_speed = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpwave)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    uint32_t waveform_num                      = -1;

    instruction_data                 += player_channel->variable[src_var];
    player_channel->arpeggio_waveform = NULL;

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->arpeggio_waveform = waveform_list[waveform_num];

            break;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpwavp)
{
    const AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->arpeggio_waveform))
        player_channel->arpeggio_pos = instruction_data % waveform->samples;
    else
        player_channel->arpeggio_pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpegio)
{
    const AVSequencerSynthWave *const waveform = player_channel->arpeggio_waveform;

    if (waveform) {
        uint16_t arpeggio_speed;
        int16_t arpeggio_transpose;
        uint8_t arpeggio_finetune;
        const uint32_t waveform_pos = player_channel->arpeggio_pos % waveform->samples;

        instruction_data += player_channel->variable[src_var];

        if (!(arpeggio_speed = (instruction_data >> 8)))
            arpeggio_speed = player_channel->arpeggio_speed;

        player_channel->arpeggio_speed = arpeggio_speed;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT) {
            arpeggio_finetune  = 0;
            arpeggio_transpose = ((int8_t *) waveform->data)[waveform_pos];
        } else {
            arpeggio_speed     = waveform->data[waveform_pos];
            arpeggio_finetune  = arpeggio_speed;
            arpeggio_transpose = (int16_t) arpeggio_speed >> 8;
        }

        player_channel->arpeggio_finetune  = arpeggio_finetune;
        player_channel->arpeggio_transpose = arpeggio_transpose;
        player_channel->arpeggio_pos       = (waveform_pos + arpeggio_speed) % waveform->samples;

        se_arpegio_do(avctx, player_channel, arpeggio_transpose, arpeggio_finetune + instruction_data);
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpval)
{
    int16_t arpeggio_transpose;
    uint8_t arpeggio_finetune;

    instruction_data                  += player_channel->variable[src_var];
    player_channel->arpeggio_finetune  = arpeggio_finetune  = instruction_data;
    player_channel->arpeggio_transpose = arpeggio_transpose = (int16_t) instruction_data >> 8;

    se_arpegio_do(avctx, player_channel, arpeggio_transpose, arpeggio_finetune);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setwave)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    const AVSequencerSynthWave *waveform       = NULL;
    uint32_t waveform_num                      = -1;

    instruction_data += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->sample_waveform = waveform = waveform_list[waveform_num];

            break;
        }
    }

    if (waveform) {
        AVMixerData *mixer;
        uint16_t flags, repeat_mode, playback_flags;

        player_channel->sample_waveform = waveform;
        player_channel->mixer.pos       = 0;
        player_channel->mixer.len       = waveform->samples;
        player_channel->mixer.data      = waveform->data;
        flags                           = waveform->flags;

        if (flags & AVSEQ_SYNTH_WAVE_FLAG_SUSTAIN_LOOP) {
            player_channel->mixer.repeat_start  = waveform->sustain_repeat;
            player_channel->mixer.repeat_length = waveform->sustain_rep_len;
            player_channel->mixer.repeat_count  = waveform->sustain_rep_count;
            repeat_mode                         = waveform->sustain_repeat_mode;
            flags                               = (~flags >> 1);
        } else {
            player_channel->mixer.repeat_start  = waveform->repeat;
            player_channel->mixer.repeat_length = waveform->rep_len;
            player_channel->mixer.repeat_count  = waveform->rep_count;
            repeat_mode                         = waveform->repeat_mode;
        }

        player_channel->mixer.repeat_counted  = 0;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            player_channel->mixer.bits_per_sample = 8;
        else
            player_channel->mixer.bits_per_sample = 16;

        playback_flags = player_channel->mixer.flags & (AVSEQ_MIXER_CHANNEL_FLAG_SURROUND|AVSEQ_MIXER_CHANNEL_FLAG_PLAY);

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_REVERSE)
            playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;

        if (!(flags & AVSEQ_SYNTH_WAVE_FLAG_NOLOOP) && player_channel->mixer.repeat_length) {
            playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_LOOP;

            if (repeat_mode & AVSEQ_SYNTH_WAVE_REP_MODE_PINGPONG)
                playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG;

            if (repeat_mode & AVSEQ_SYNTH_WAVE_REP_MODE_BACKWARDS)
                playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
        }

        playback_flags             |= AVSEQ_MIXER_CHANNEL_FLAG_SYNTH;
        player_channel->mixer.flags = playback_flags;
        mixer                       = avctx->player_mixer_data;

        if (mixer->mixctx->set_channel)
            mixer->mixctx->set_channel(mixer, &player_channel->mixer, virtual_channel);

        if (mixer->mixctx->get_channel)
            mixer->mixctx->get_channel(mixer, &player_channel->mixer, virtual_channel);
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(isetwav)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    const AVSequencerSynthWave *waveform       = NULL;
    uint32_t waveform_num                      = -1;

    instruction_data += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->sample_waveform = waveform = waveform_list[waveform_num];

            break;
        }
    }

    if (waveform) {
        AVMixerData *mixer;
        uint16_t flags, repeat_mode, playback_flags;

        player_channel->sample_waveform = waveform;
        player_channel->mixer.pos       = 0;
        player_channel->mixer.len       = waveform->samples;
        player_channel->mixer.data      = waveform->data;
        flags                           = waveform->flags;

        if (flags & AVSEQ_SYNTH_WAVE_FLAG_SUSTAIN_LOOP) {
            player_channel->mixer.repeat_start  = waveform->sustain_repeat;
            player_channel->mixer.repeat_length = waveform->sustain_rep_len;
            player_channel->mixer.repeat_count  = waveform->sustain_rep_count;
            repeat_mode                         = waveform->sustain_repeat_mode;
            flags                               = (~flags >> 1);
        } else {
            player_channel->mixer.repeat_start  = waveform->repeat;
            player_channel->mixer.repeat_length = waveform->rep_len;
            player_channel->mixer.repeat_count  = waveform->rep_count;
            repeat_mode                         = waveform->repeat_mode;
        }

        player_channel->mixer.repeat_counted  = 0;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            player_channel->mixer.bits_per_sample = 8;
        else
            player_channel->mixer.bits_per_sample = 16;

        playback_flags = player_channel->mixer.flags & (AVSEQ_MIXER_CHANNEL_FLAG_SURROUND|AVSEQ_MIXER_CHANNEL_FLAG_PLAY);

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_REVERSE)
            playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;

        if (!(flags & AVSEQ_SYNTH_WAVE_FLAG_NOLOOP) && player_channel->mixer.repeat_length) {
            playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_LOOP;

            if (repeat_mode & AVSEQ_SYNTH_WAVE_REP_MODE_PINGPONG)
                playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG;

            if (repeat_mode & AVSEQ_SYNTH_WAVE_REP_MODE_BACKWARDS)
                playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
        }

        player_channel->mixer.flags = playback_flags;
        mixer                       = avctx->player_mixer_data;

        if (mixer->mixctx->set_channel)
            mixer->mixctx->set_channel(mixer, &player_channel->mixer, virtual_channel);
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setwavp)
{
    instruction_data += player_channel->variable[src_var];

    player_channel->mixer.pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setrans)
{
    const uint32_t *frequency_lut;
    uint32_t frequency, next_frequency;
    uint16_t octave;
    int16_t note;
    int8_t finetune;

    player_channel->final_note = (instruction_data += player_channel->variable[src_var] + player_channel->sample_note);
    note                       = (int16_t) instruction_data % 12;
    octave                     = (int16_t) instruction_data / 12;

    if (note < 0) {
        octave--;
        note += 12;
    }

    if ((finetune = player_channel->finetune) < 0) {
        note--;
        finetune += -0x80;
    }

    frequency_lut             = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
    frequency                 = *frequency_lut++;
    next_frequency            = *frequency_lut - frequency;
    frequency                += ((int32_t) finetune * (int32_t) next_frequency) >> 7;
    player_channel->frequency = ((uint64_t) frequency * player_channel->sample->rate) >> ((24+4) - octave);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setnote)
{
    const uint32_t *frequency_lut;
    uint32_t frequency, next_frequency;
    uint16_t octave;
    int16_t note;
    int8_t finetune;

    instruction_data += player_channel->variable[src_var];
    note              = (int16_t) instruction_data % 12;
    octave            = (int16_t) instruction_data / 12;

    if (note < 0) {
        octave--;
        note += 12;
    }

    if ((finetune = player_channel->finetune) < 0) {
        note--;
        finetune += -0x80;
    }

    frequency_lut             = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
    frequency                 = *frequency_lut++;
    next_frequency            = *frequency_lut - frequency;
    frequency                += ((int32_t) finetune * (int32_t) next_frequency) >> 7;
    player_channel->frequency = ((uint64_t) frequency * player_channel->sample->rate) >> ((24+4) - octave);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setptch)
{
    uint32_t frequency = instruction_data + player_channel->variable[src_var];

    if (dst_var == 15)
        frequency += player_channel->variable[dst_var];
    else
        frequency += (player_channel->variable[dst_var + 1] << 16) + player_channel->variable[dst_var];

    player_channel->frequency = frequency;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setper)
{
    uint32_t period = instruction_data + player_channel->variable[src_var];

    if (dst_var == 15)
        period += player_channel->variable[dst_var];
    else
        period += (player_channel->variable[dst_var + 1] << 16) + player_channel->variable[dst_var];

    if (period)
        player_channel->frequency = AVSEQ_SLIDE_CONST / period;
    else
        player_channel->frequency = 0;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(reset)
{
    instruction_data += player_channel->variable[src_var];

    if (!(instruction_data & 0x01))
        player_channel->arpeggio_slide = 0;

    if (!(instruction_data & 0x02))
        player_channel->vibrato_slide  = 0;

    if (!(instruction_data & 0x04))
        player_channel->tremolo_slide  = 0;

    if (!(instruction_data & 0x08))
        player_channel->pannolo_slide  = 0;

    if (!(instruction_data & 0x10)) {
        AVSequencerPlayerHostChannel *const player_host_channel = avctx->player_host_channel + player_channel->host_channel;
        int32_t portamento_value                                = player_channel->portamento;

        if (portamento_value < 0) {
            portamento_value = -portamento_value;

            if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
                linear_slide_down(avctx, player_channel, player_channel->frequency, portamento_value);
            else
                amiga_slide_down(player_channel, player_channel->frequency, portamento_value);
        } else if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ) {
            linear_slide_up(avctx, player_channel, player_channel->frequency, portamento_value);
        } else {
            amiga_slide_up(player_channel, player_channel->frequency, portamento_value);
        }
    }

    if (!(instruction_data & 0x20))
        player_channel->portamento = 0;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(volslup)
{
    uint16_t slide_volume = (player_channel->volume << 8) + player_channel->sub_volume;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->vol_sl_up;

    player_channel->vol_sl_up = instruction_data;

    if ((slide_volume += instruction_data) < instruction_data)
        slide_volume = 0xFFFF;

    player_channel->volume     = slide_volume >> 8;
    player_channel->sub_volume = slide_volume;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(volsldn)
{
    uint16_t slide_volume = (player_channel->volume << 8) + player_channel->sub_volume;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->vol_sl_dn;

    player_channel->vol_sl_dn = instruction_data;

    if (slide_volume < instruction_data)
        instruction_data = slide_volume;

    slide_volume              -= instruction_data;
    player_channel->volume     = slide_volume >> 8;
    player_channel->sub_volume = slide_volume;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmspd)
{
    instruction_data            += player_channel->variable[src_var];
    player_channel->tremolo_rate = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmdpth)
{
    instruction_data            += player_channel->variable[src_var];
    player_channel->tremolo_rate = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmwave)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    uint32_t waveform_num                      = -1;

    instruction_data                += player_channel->variable[src_var];
    player_channel->tremolo_waveform = NULL;

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->tremolo_waveform = waveform_list[waveform_num];

            break;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmwavp)
{
    const AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->tremolo_waveform))
        player_channel->tremolo_pos = instruction_data % waveform->samples;
    else
        player_channel->tremolo_pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(tremolo)
{
    const AVSequencerSynthWave *waveform;
    uint16_t tremolo_rate;
    int16_t tremolo_depth;

    instruction_data += player_channel->variable[src_var];

    if (!(tremolo_rate = (instruction_data >> 8)))
        tremolo_rate = player_channel->tremolo_rate;

    player_channel->tremolo_rate = tremolo_rate;

    tremolo_depth = (instruction_data & 0xFF) << 2;

    if (!tremolo_depth)
        tremolo_depth = player_channel->tremolo_depth;

    player_channel->tremolo_depth = tremolo_depth;

    if ((waveform = player_channel->vibrato_waveform)) {
        uint32_t waveform_pos;
        int32_t tremolo_slide_value;

        waveform_pos = player_channel->tremolo_pos % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            tremolo_slide_value = ((int8_t *) waveform->data)[waveform_pos] << 8;
        else
            tremolo_slide_value = waveform->data[waveform_pos];

        tremolo_slide_value        *= -tremolo_depth;
        tremolo_slide_value       >>= 7 - 2;
        player_channel->tremolo_pos = (waveform_pos + tremolo_rate) % waveform->samples;

        se_tremolo_do(avctx, player_channel, tremolo_slide_value);
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmval)
{
    int32_t tremolo_slide_value;

    instruction_data   += player_channel->variable[src_var];
    tremolo_slide_value = (int16_t) instruction_data;

    se_tremolo_do(avctx, player_channel, tremolo_slide_value);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panleft)
{
    uint16_t panning = ((uint8_t) player_channel->panning << 8) + player_channel->sub_panning;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->pan_sl_left;

    player_channel->pan_sl_left = instruction_data;

    if (panning < instruction_data)
        instruction_data = panning;

    panning -= instruction_data;

    player_channel->panning     = panning >> 8;
    player_channel->sub_panning = panning;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panrght)
{
    uint16_t panning = ((uint8_t) player_channel->panning << 8) + player_channel->sub_panning;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->pan_sl_right;

    player_channel->pan_sl_right = instruction_data;

    if ((panning += instruction_data) < instruction_data)
        panning = 0xFFFF;

    player_channel->panning     = panning >> 8;
    player_channel->sub_panning = panning;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panspd)
{
    instruction_data            += player_channel->variable[src_var];
    player_channel->pannolo_rate = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(pandpth)
{
    instruction_data             += player_channel->variable[src_var];
    player_channel->pannolo_depth = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panwave)
{
    AVSequencerSynthWave *const *waveform_list = player_channel->waveform_list;
    uint32_t waveform_num                      = -1;

    instruction_data                += player_channel->variable[src_var];
    player_channel->pannolo_waveform = NULL;

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->pannolo_waveform = waveform_list[waveform_num];

            break;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panwavp)
{
    const AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->pannolo_waveform))
        player_channel->pannolo_pos = instruction_data % waveform->samples;
    else
        player_channel->pannolo_pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(pannolo)
{
    const AVSequencerSynthWave *waveform;
    uint16_t pannolo_rate;
    int16_t pannolo_depth;

    instruction_data += player_channel->variable[src_var];

    if (!(pannolo_rate = (instruction_data >> 8)))
        pannolo_rate = player_channel->pannolo_rate;

    player_channel->pannolo_rate = pannolo_rate;
    pannolo_depth                = (instruction_data & 0xFF) << 2;

    if (!pannolo_depth)
        pannolo_depth = player_channel->pannolo_depth;

    player_channel->pannolo_depth = pannolo_depth;

    if ((waveform = player_channel->vibrato_waveform)) {
        uint32_t waveform_pos;
        int32_t pannolo_slide_value;

        waveform_pos = player_channel->pannolo_pos % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAG_8BIT)
            pannolo_slide_value = ((int8_t *) waveform->data)[waveform_pos] << 8;
        else
            pannolo_slide_value = waveform->data[waveform_pos];

        pannolo_slide_value        *= -pannolo_depth;
        pannolo_slide_value       >>= 7 - 2;
        player_channel->pannolo_pos = (waveform_pos + pannolo_rate) % waveform->samples;

        se_pannolo_do(avctx, player_channel, pannolo_slide_value);
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panval)
{
    int32_t pannolo_slide_value;

    instruction_data   += player_channel->variable[src_var];
    pannolo_slide_value = (int16_t) instruction_data;

    se_pannolo_do(avctx, player_channel, pannolo_slide_value);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(nop)
{
    return synth_code_line;
}

static void process_row(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel)
{
    uint32_t current_tick;
    AVSequencerSong *song = avctx->player_song;
    uint16_t counted      = 0;

    player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_EXEC;
    current_tick                = player_host_channel->tempo_counter;
    current_tick++;

    if (current_tick >= ((uint32_t) player_host_channel->fine_pattern_delay + player_host_channel->tempo))
        current_tick  = 0;

    if (!(player_host_channel->tempo_counter = current_tick)) {
        const AVSequencerTrack *track;
        const AVSequencerOrderList *const order_list = song->order_list + channel;
        AVSequencerOrderData *order_data;
        const AVSequencerPlayerGlobals *const player_globals = avctx->player_globals;
        uint16_t pattern_delay, row, last_row, track_length;
        uint32_t ord = -1;

        if (player_channel->host_channel == channel) {
            const uint32_t slide_value = player_host_channel->arpeggio_freq;

            player_host_channel->arpeggio_freq = 0;
            player_channel->frequency         += slide_value;
        }

        player_host_channel->flags            &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX|AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TONE_PORTA|AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_TRANSPOSE|AVSEQ_PLAYER_HOST_CHANNEL_FLAG_VIBRATO|AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOLO);

        AV_WN64A(player_host_channel->effects_used, 0);
        AV_WN64A(player_host_channel->effects_used + 8, 0);

        player_host_channel->effect            = NULL;
        player_host_channel->arpeggio_tick     = 0;
        player_host_channel->note_delay        = 0;
        player_host_channel->retrig_tick_count = 0;

        if ((pattern_delay = player_host_channel->pattern_delay) && (pattern_delay > player_host_channel->pattern_delay_count++))
            return;

        player_host_channel->pattern_delay_count = 0;
        player_host_channel->pattern_delay       = 0;
        row                                      = player_host_channel->row;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP) {
            player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP;
            order_data                  = player_host_channel->order;
            track                       = player_host_channel->track;

            goto loop_to_row;
        }

        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP_JMP;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHG_PATTERN) {
            player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHG_PATTERN;
            order_data                  = player_host_channel->order;

            if ((player_host_channel->chg_pattern < song->tracks) && ((track = song->track_list[player_host_channel->chg_pattern]))) {
                if (!(avctx->player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_PATTERN))
                    player_host_channel->track = track;

                goto loop_to_row;
            }
        }

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_BREAK)
            goto get_new_pattern;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS) {
            if (!row--)
                goto get_new_pattern;
        } else if (++row >= player_host_channel->max_row) {
get_new_pattern:
            order_data = player_host_channel->order;
 
            if (avctx->player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_PATTERN) {
                track = player_host_channel->track;

                goto loop_to_row;
            }

            ord = -1;

            if (order_data) {
                while (++ord < order_list->orders) {
                    if (order_data == order_list->order_data[ord])
                        break;
                }
            }

check_next_empty_order:
            do {
                ord++;

                if ((ord >= order_list->orders) || !(order_data = order_list->order_data[ord])) {
song_end_found:
                    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;

                    if ((order_list->rep_start >= order_list->orders) || !(order_data = order_list->order_data[order_list->rep_start])) {
disable_channel:
                        player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;
                        player_host_channel->tempo  = 0;

                        return;
                    }

                    if (order_data->flags & (AVSEQ_ORDER_DATA_FLAG_END_ORDER|AVSEQ_ORDER_DATA_FLAG_END_SONG))
                        goto disable_channel;

                    row = 0;

                    if (((player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE) && (order_data->flags & AVSEQ_ORDER_DATA_FLAG_NOT_IN_ONCE)) || !(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE) && (order_data->flags & AVSEQ_ORDER_DATA_FLAG_NOT_IN_REPEAT))
                        goto disable_channel;

                    if ((track = order_data->track))
                        break;
                }

                if (order_data->flags & AVSEQ_ORDER_DATA_FLAG_END_ORDER)
                    goto song_end_found;

                if (order_data->flags & AVSEQ_ORDER_DATA_FLAG_END_SONG) {
                    if (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE)
                        goto disable_channel;

                    goto song_end_found;
                }
            } while (((player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE) && (order_data->flags & AVSEQ_ORDER_DATA_FLAG_NOT_IN_ONCE)) || !(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE) && (order_data->flags & AVSEQ_ORDER_DATA_FLAG_NOT_IN_REPEAT) || !(track = order_data->track));

            player_host_channel->order = order_data;
            player_host_channel->track = track;

            if (player_host_channel->gosub_depth < order_data->played) {
                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;

                if (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE)
                    player_host_channel->tempo = 0;
            }

            order_data->played++;

            player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_RESET;
loop_to_row:
            track_length = track->last_row;
            row          = order_data->first_row;
            last_row     = order_data->last_row;

            if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_BREAK) {
                player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_BREAK;
                row                         = player_host_channel->break_row;

                if (track_length < row)
                    row = order_data->first_row;
            }

            if (track_length < row)
                goto check_next_empty_order;

            if (track_length < last_row)
                last_row = track_length;

            player_host_channel->max_row = last_row + 1;

            if ((pattern_delay = order_data->tempo))
                player_host_channel->tempo = pattern_delay;

            if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS)
                row = last_row - row;
        }

        player_host_channel->row = row;

        if ((int) ((player_host_channel->track->data) + row)->note == AVSEQ_TRACK_DATA_NOTE_END) {
            if (++counted)
                goto get_new_pattern;

            goto disable_channel;
        }
    }
}

static const AVSequencerPlayerEffects fx_lut[128] = {
    {arpeggio,                          NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {portamento_up,                     NULL,                   check_portamento,           AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {portamento_down,                   NULL,                   check_portamento,           AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {fine_portamento_up,                NULL,                   check_portamento,           AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {fine_portamento_down,              NULL,                   check_portamento,           AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {portamento_up_once,                NULL,                   check_portamento,           0x00,                                       0x01, 0x0000},
    {portamento_down_once,              NULL,                   check_portamento,           0x00,                                       0x01, 0x0000},
    {fine_portamento_up_once,           NULL,                   check_portamento,           0x00,                                       0x01, 0x0000},
    {fine_portamento_down_once,         NULL,                   check_portamento,           0x00,                                       0x01, 0x0000},
    {tone_portamento,                   preset_tone_portamento, check_tone_portamento,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {fine_tone_portamento,              preset_tone_portamento, check_tone_portamento,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {tone_portamento_once,              preset_tone_portamento, check_tone_portamento,      0x00, 0x00, 0x0000},
    {fine_tone_portamento_once,         preset_tone_portamento, check_tone_portamento,      0x00, 0x00, 0x0000},
    {note_slide,                        NULL,                   check_note_slide,           AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {vibrato,                           preset_vibrato,         NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {fine_vibrato,                      preset_vibrato,         NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {vibrato,                           preset_vibrato,         NULL,                       0x00,                                       0x01, 0x0000},
    {fine_vibrato,                      preset_vibrato,         NULL,                       0x00,                                       0x01, 0x0000},
    {do_key_off,                        NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {hold_delay,                        NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {note_fade,                         NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {note_cut,                          NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {note_delay,                        preset_note_delay,      NULL,                       0x00,                                       0x00, 0x0000},
    {tremor,                            NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {note_retrigger,                    NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {multi_retrigger_note,              NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {extended_ctrl,                     NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {invert_loop,                       NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {exec_fx,                           NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {stop_fx,                           NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},

    {set_volume,                        NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {volume_slide_up,                   NULL,                   check_volume_slide,         AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {volume_slide_down,                 NULL,                   check_volume_slide,         AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {fine_volume_slide_up,              NULL,                   check_volume_slide,         0x00,                                       0x01, 0x0000},
    {fine_volume_slide_down,            NULL,                   check_volume_slide,         0x00,                                       0x01, 0x0000},
    {volume_slide_to,                   NULL,                   check_volume_slide_to,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {tremolo,                           preset_tremolo,         NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {tremolo,                           preset_tremolo,         NULL,                       0x00,                                       0x01, 0x0000},
    {set_track_volume,                  NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {track_volume_slide_up,             NULL,                   check_track_volume_slide,   AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {track_volume_slide_down,           NULL,                   check_track_volume_slide,   AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {fine_track_volume_slide_up,        NULL,                   check_track_volume_slide,   0x00,                                       0x01, 0x0000},
    {fine_track_volume_slide_down,      NULL,                   check_track_volume_slide,   0x00,                                       0x01, 0x0000},
    {track_volume_slide_to,             NULL,                   check_volume_slide_to,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {track_tremolo,                     NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {track_tremolo,                     NULL,                   NULL,                       0x00,                                       0x01, 0x0000},

    {set_panning,                       NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {panning_slide_left,                NULL,                   check_panning_slide,        AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {panning_slide_right,               NULL,                   check_panning_slide,        AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {fine_panning_slide_left,           NULL,                   check_panning_slide,        0x00,                                       0x01, 0x0000},
    {fine_panning_slide_right,          NULL,                   check_panning_slide,        0x00,                                       0x01, 0x0000},
    {panning_slide_to,                  NULL,                   check_volume_slide_to,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {pannolo,                           NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {pannolo,                           NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {set_track_panning,                 NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {track_panning_slide_left,          NULL,                   check_track_panning_slide,  AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {track_panning_slide_right,         NULL,                   check_track_panning_slide,  AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {fine_track_panning_slide_left,     NULL,                   check_track_panning_slide,  0x00,                                       0x01, 0x0000},
    {fine_track_panning_slide_right,    NULL,                   check_track_panning_slide,  0x00,                                       0x01, 0x0000},
    {track_panning_slide_to,            NULL,                   check_volume_slide_to,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {track_pannolo,                     NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {track_pannolo,                     NULL,                   NULL,                       0x00,                                       0x01, 0x0000},

    {set_tempo,                         NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {set_relative_tempo,                NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {pattern_break,                     NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {position_jump,                     NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {relative_position_jump,            NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {change_pattern,                    NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {reverse_pattern_play,              NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {pattern_delay,                     NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {fine_pattern_delay,                NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {pattern_loop,                      NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {gosub,                             NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {gosub_return,                      NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {channel_sync,                      NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {set_sub_slides,                    NULL,                   NULL,                       0x00,                                       0x02, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},

    {sample_offset_high,                NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {sample_offset_low,                 NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {set_hold,                          NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {set_decay,                         NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {set_transpose,                     preset_set_transpose,   NULL,                       0x00,                                       0x01, 0x0000},
    {instrument_ctrl,                   NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {instrument_change,                 NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {synth_ctrl,                        NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {set_synth_value,                   NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {envelope_ctrl,                     NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {set_envelope_value,                NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {nna_ctrl,                          NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {loop_ctrl,                         NULL,                   NULL,                       0x00,                                       0x01, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},

    {set_speed,                         NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {speed_slide_faster,                NULL,                   check_speed_slide,          AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {speed_slide_slower,                NULL,                   check_speed_slide,          AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {fine_speed_slide_faster,           NULL,                   check_speed_slide,          0x00,                                       0x00, 0x0000},
    {fine_speed_slide_slower,           NULL,                   check_speed_slide,          0x00,                                       0x00, 0x0000},
    {speed_slide_to,                    NULL,                   check_volume_slide_to,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0001},
    {spenolo,                           NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {spenolo,                           NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {channel_ctrl,                      NULL,                   check_channel_control,      0x00,                                       0x00, 0x0000},
    {set_global_volume,                 NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {global_volume_slide_up,            NULL,                   check_global_volume_slide,  AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {global_volume_slide_down,          NULL,                   check_global_volume_slide,  AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {fine_global_volume_slide_up,       NULL,                   check_global_volume_slide,  0x00,                                       0x00, 0x0000},
    {fine_global_volume_slide_down,     NULL,                   check_global_volume_slide,  0x00,                                       0x00, 0x0000},
    {global_volume_slide_to,            NULL,                   check_volume_slide_to,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {global_tremolo,                    NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {global_tremolo,                    NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {set_global_panning,                NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {global_panning_slide_left,         NULL,                   check_global_panning_slide, AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {global_panning_slide_right,        NULL,                   check_global_panning_slide, AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {fine_global_panning_slide_left,    NULL,                   check_global_panning_slide, 0x00,                                       0x00, 0x0000},
    {fine_global_panning_slide_right,   NULL,                   check_global_panning_slide, 0x00,                                       0x00, 0x0000},
    {global_panning_slide_to,           NULL,                   check_volume_slide_to,      AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x01, 0x0000},
    {global_pannolo,                    NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0001},
    {global_pannolo,                    NULL,                   NULL,                       0x00,                                       0x00, 0x0000},

    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},
    {NULL,                              NULL,                   NULL,                       0x00,                                       0x00, 0x0000},

    {user_sync,                         NULL,                   NULL,                       AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW,   0x00, 0x0000}
};

static void get_effects(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel)
{
    const AVSequencerTrack *track;
    const AVSequencerTrackEffect *track_fx;
    const AVSequencerTrackRow *track_data;
    uint32_t fx = -1;

    if (!(track = player_host_channel->track))
        return;

    track_data = track->data + player_host_channel->row;

    if ((track_fx = player_host_channel->effect)) {
        while (++fx < track_data->effects) {
            if (track_fx == track_data->effects_data[fx])
                break;
        }
    } else if (track_data->effects) {
        fx       = 0;
        track_fx = track_data->effects_data[0];
    } else {
        track_fx = NULL;
    }

    player_host_channel->effect = track_fx;

    if ((fx < track_data->effects) && track_data->effects_data[fx]) {
        do {
            const int fx_byte = track_fx->command & 0x7F;

            if (fx_byte == AVSEQ_TRACK_EFFECT_CMD_EXECUTE_FX) {
                player_host_channel->flags  |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX;
                player_host_channel->exec_fx = track_fx->data;

                if (player_host_channel->tempo_counter < player_host_channel->exec_fx)
                    break;
            }
        } while ((++fx < track_data->effects) && ((track_fx = track_data->effects_data[fx])));


        if (player_host_channel->effect != track_fx) {
            player_host_channel->effect = track_fx;

            AV_WN64A(player_host_channel->effects_used, 0);
            AV_WN64A(player_host_channel->effects_used + 8, 0);
        }

        fx         = -1;

        while ((++fx < track_data->effects) && ((track_fx = track_data->effects_data[fx]))) {
            const AVSequencerPlayerEffects *effects_lut;
            void (*pre_fx_func)(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel, uint16_t data_word);
            const int fx_byte = track_fx->command & 0x7F;

            effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;

            if ((pre_fx_func = effects_lut->pre_pattern_func))
                pre_fx_func(avctx, player_host_channel, player_channel, channel, track_fx->data);
        }
    }
}

static void run_effects(AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel)
{
    const AVSequencerSong *const song = avctx->player_song;
    const AVSequencerTrack *track;

    if ((track = player_host_channel->track) && player_host_channel->effect) {
        const AVSequencerTrackEffect *track_fx;
        const AVSequencerTrackRow *const track_data = track->data + player_host_channel->row;
        uint32_t fx                                 = -1;

        while ((++fx < track_data->effects) && ((track_fx = track_data->effects_data[fx]))) {
            const AVSequencerPlayerEffects *effects_lut;
            void (*check_func)(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel, uint16_t *const fx_byte, uint16_t *const data_word, uint16_t *const flags);
            uint16_t fx_byte, data_word, flags;
            uint8_t channel_ctrl_type;

            fx_byte     = track_fx->command & 0x7F;
            effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            data_word   = track_fx->data;
            flags       = effects_lut->flags;

            if ((check_func = effects_lut->check_fx_func)) {
                check_func(avctx, player_host_channel, player_channel, channel, &fx_byte, &data_word, &flags);

                effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            }

            if (flags & AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW)
                continue;

            flags = player_host_channel->exec_fx;

            if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX))
                flags = effects_lut->std_exec_tick;

            if (flags != player_host_channel->tempo_counter)
                continue;

            if (player_host_channel->effects_used[(fx_byte >> 3)] & (1 << (7 - (fx_byte & 7))))
                continue;

            effects_lut->effect_func(avctx, player_host_channel, player_channel, channel, fx_byte, data_word);

            if ((channel_ctrl_type = player_host_channel->ch_control_type)) {
                if (player_host_channel->ch_control_affect & effects_lut->and_mask_ctrl) {
                    AVSequencerPlayerHostChannel *new_player_host_channel;
                    AVSequencerPlayerChannel *new_player_channel;
                    uint16_t ctrl_fx_byte, ctrl_data_word, ctrl_flags, ctrl_channel;

                    switch (channel_ctrl_type) {
                    case AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_NORMAL :
                        if ((ctrl_channel = player_host_channel->ch_control_channel) != channel) {
                            new_player_host_channel = avctx->player_host_channel + ctrl_channel;
                            new_player_channel      = avctx->player_channel + new_player_host_channel->virtual_channel;
                            ctrl_fx_byte            = fx_byte;
                            ctrl_data_word          = data_word;
                            ctrl_flags              = flags;

                            if ((check_func = effects_lut->check_fx_func)) {
                                check_func(avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags);

                                effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                            }

                            effects_lut->effect_func(avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word);
                        }

                        break;
                    case AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_MULTIPLE :
                        ctrl_channel = 0;

                        do {
                            if ((ctrl_channel != channel) && (player_host_channel->control_channels[(ctrl_channel >> 3)] & (1 << (7 - (ctrl_channel & 7))))) {
                                new_player_host_channel = avctx->player_host_channel + ctrl_channel;
                                new_player_channel      = avctx->player_channel + new_player_host_channel->virtual_channel;

                                ctrl_fx_byte   = fx_byte;
                                ctrl_data_word = data_word;
                                ctrl_flags     = flags;

                                if ((check_func = effects_lut->check_fx_func)) {
                                    check_func(avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags);

                                    effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                                }

                                effects_lut->effect_func(avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word);
                            }
                        } while (++ctrl_channel < song->channels);

                        break;
                    default :
                        ctrl_channel = 0;

                        do {
                            if (ctrl_channel != channel) {
                                new_player_host_channel = avctx->player_host_channel + ctrl_channel;
                                new_player_channel      = avctx->player_channel + new_player_host_channel->virtual_channel;
                                ctrl_fx_byte            = fx_byte;
                                ctrl_data_word          = data_word;
                                ctrl_flags              = flags;

                                if ((check_func = effects_lut->check_fx_func)) {
                                    check_func(avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags);

                                    effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                                }

                                effects_lut->effect_func(avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word);
                            }
                        } while (++ctrl_channel < song->channels);

                        break;
                    }
                }
            }

            if (player_host_channel->effect == track_fx)
                break;
        }

        fx = -1;

        while ((++fx < track_data->effects) && ((track_fx = track_data->effects_data[fx]))) {
            const AVSequencerPlayerEffects *effects_lut;
            void (*check_func)(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel, uint16_t *const fx_byte, uint16_t *const data_word, uint16_t *const flags);
            uint16_t fx_byte, data_word, flags;
            uint8_t channel_ctrl_type;

            fx_byte     = track_fx->command & 0x7F;
            effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            data_word   = track_fx->data;
            flags       = effects_lut->flags;

            if ((check_func = effects_lut->check_fx_func)) {
                check_func(avctx, player_host_channel, player_channel, channel, &fx_byte, &data_word, &flags);

                effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            }

            if (!(flags & AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW))
                continue;

            flags = player_host_channel->exec_fx;

            if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX))
                flags = effects_lut->std_exec_tick;

            if (player_host_channel->tempo_counter < flags)
                continue;

            if (player_host_channel->effects_used[(fx_byte >> 3)] & (1 << (7 - (fx_byte & 7))))
                continue;

            effects_lut->effect_func(avctx, player_host_channel, player_channel, channel, fx_byte, data_word);

            if ((channel_ctrl_type = player_host_channel->ch_control_type)) {
                if (player_host_channel->ch_control_affect & effects_lut->and_mask_ctrl) {
                    AVSequencerPlayerHostChannel *new_player_host_channel;
                    AVSequencerPlayerChannel *new_player_channel;
                    uint16_t ctrl_fx_byte, ctrl_data_word, ctrl_flags, ctrl_channel;

                    switch (channel_ctrl_type) {
                    case AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_NORMAL :
                        if ((ctrl_channel = player_host_channel->ch_control_channel) != channel) {
                            new_player_host_channel = avctx->player_host_channel + ctrl_channel;
                            new_player_channel      = avctx->player_channel + new_player_host_channel->virtual_channel;
                            ctrl_fx_byte            = fx_byte;
                            ctrl_data_word          = data_word;
                            ctrl_flags              = flags;

                            if ((check_func = effects_lut->check_fx_func)) {
                                check_func(avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags);

                                effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                            }

                            if (new_player_host_channel->effects_used[(ctrl_fx_byte >> 3)] & (1 << (7 - (ctrl_fx_byte & 7))))
                                continue;

                            effects_lut->effect_func(avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word);
                        }

                        break;
                    case AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_MULTIPLE :
                        ctrl_channel = 0;

                        do {
                            if ((ctrl_channel != channel) && (player_host_channel->control_channels[(ctrl_channel >> 3)] & (1 << (7 - (ctrl_channel & 7))))) {
                                new_player_host_channel = avctx->player_host_channel + ctrl_channel;
                                new_player_channel      = avctx->player_channel + new_player_host_channel->virtual_channel;
                                ctrl_fx_byte            = fx_byte;
                                ctrl_data_word          = data_word;
                                ctrl_flags              = flags;

                                if ((check_func = effects_lut->check_fx_func)) {
                                    check_func(avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags);

                                    effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                                }

                                if (new_player_host_channel->effects_used[(ctrl_fx_byte >> 3)] & (1 << (7 - (ctrl_fx_byte & 7))))
                                    continue;

                                effects_lut->effect_func(avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word);
                            }
                        } while (++ctrl_channel < song->channels);

                        break;
                    default :
                        ctrl_channel = 0;

                        do {
                            if (ctrl_channel != channel) {
                                new_player_host_channel = avctx->player_host_channel + ctrl_channel;
                                new_player_channel      = avctx->player_channel + new_player_host_channel->virtual_channel;
                                ctrl_fx_byte            = fx_byte;
                                ctrl_data_word          = data_word;
                                ctrl_flags              = flags;

                                if ((check_func = effects_lut->check_fx_func)) {
                                    check_func(avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags);

                                    effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                                }

                                if (new_player_host_channel->effects_used[(ctrl_fx_byte >> 3)] & (1 << (7 - (ctrl_fx_byte & 7))))
                                    continue;

                                effects_lut->effect_func(avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word);
                            }
                        } while (++ctrl_channel < song->channels);

                        break;
                    }
                }
            }

            if (player_host_channel->effect == track_fx)
                break;
        }
    }
}

static int16_t get_key_table(const AVSequencerContext *const avctx, const AVSequencerInstrument *const instrument, AVSequencerPlayerHostChannel *const player_host_channel, uint16_t note)
{
    const AVSequencerModule *const module = avctx->player_module;
    const AVSequencerKeyboard *keyboard;
    const AVSequencerSample *sample;
    uint16_t smp = 1, i;
    int8_t transpose = 0;

    if (!player_host_channel->instrument)
        player_host_channel->nna = instrument->nna;

    player_host_channel->instr_note  = note;
    player_host_channel->sample_note = note;
    player_host_channel->instrument  = instrument;

    if (!(keyboard = instrument->keyboard_defs))
        goto do_not_play_keyboard;

    i                                = --note;
    note                             = ((uint16_t) (keyboard->key[i].octave & 0x7F) * 12) + keyboard->key[i].note;
    player_host_channel->sample_note = note;

    if ((smp = keyboard->key[i].sample)) {
do_not_play_keyboard:
        smp--;

        if (!(instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_SEPARATE_SAMPLES)) {
            if ((smp >= instrument->samples) || !(sample = instrument->sample_list[smp]))
                return 0x8000;
        } else {
            AVSequencerInstrument *scan_instrument;

            if ((smp >= module->instruments) || !(scan_instrument = module->instrument_list[smp]))
                return 0x8000;

            if (!scan_instrument->samples || !(sample = scan_instrument->sample_list[0]))
                return 0x8000;
        }
    } else {
        sample = player_host_channel->sample;

        if (!((instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_PREV_SAMPLE) && sample))
            return 0x8000;
    }

    player_host_channel->sample = sample;
    transpose                   = sample->transpose;

    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_TRANSPOSE)
        transpose = player_host_channel->transpose;

    note += transpose;

    if (!(instrument->flags & AVSEQ_INSTRUMENT_FLAG_NO_TRANSPOSE))
        note += player_host_channel->order->transpose;

    note += player_host_channel->track->transpose;

    return note - 1;
}

static int16_t get_key_table_note(const AVSequencerContext *const avctx, const AVSequencerInstrument *const instrument, AVSequencerPlayerHostChannel *const player_host_channel, const uint16_t octave, const uint16_t note)
{
    return get_key_table(avctx, instrument, player_host_channel, (octave * 12) + note);
}

static int trigger_dct(const AVSequencerPlayerHostChannel *const player_host_channel, const AVSequencerPlayerChannel *const player_channel, const unsigned dct)
{
    int trigger = 0;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_NOTE_OR)
        trigger |= player_host_channel->instr_note == player_channel->instr_note;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_NOTE_OR)
        trigger |= player_host_channel->sample_note == player_channel->sample_note;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_OR)
        trigger |= player_host_channel->instrument == player_channel->instrument;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_OR)
        trigger |= player_host_channel->sample == player_channel->sample;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_NOTE_AND)
        trigger &= player_host_channel->instr_note == player_channel->instr_note;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_NOTE_AND)
        trigger &= player_host_channel->sample_note == player_channel->sample_note;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_AND)
        trigger &= player_host_channel->instrument == player_channel->instrument;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_AND)
        trigger &= player_host_channel->sample == player_channel->sample;

    return trigger;
}

static AVSequencerPlayerChannel *trigger_nna(const AVSequencerContext *const avctx, const AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel, uint16_t *const virtual_channel)
{
    const AVSequencerModule *const module        = avctx->player_module;
    AVSequencerPlayerChannel *new_player_channel = player_channel;
    AVSequencerPlayerChannel *scan_player_channel;
    uint16_t nna_channel, nna_max_volume, nna_volume;
    uint8_t nna;

    *virtual_channel = player_host_channel->virtual_channel;

    if (player_channel->host_channel != channel) {
        new_player_channel = avctx->player_channel;
        nna_channel        = 0;

        do {
            if (new_player_channel->host_channel == channel)
                goto previous_nna_found;

            new_player_channel++;
        } while (++nna_channel < module->channels);

        goto find_nna;
previous_nna_found:
        *virtual_channel = nna_channel;
    }

    nna_volume                 = new_player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED;
    new_player_channel->flags &= ~AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED;

    if (nna_volume || !(nna = player_host_channel->nna))
        goto nna_found;

    if (new_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAG_VOLUME_NNA)
        new_player_channel->entry_pos[0] = new_player_channel->nna_pos[0];

    if (new_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAG_PANNING_NNA)
        new_player_channel->entry_pos[1] = new_player_channel->nna_pos[1];

    if (new_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAG_SLIDE_NNA)
        new_player_channel->entry_pos[2] = new_player_channel->nna_pos[2];

    if (new_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAG_SPECIAL_NNA)
        new_player_channel->entry_pos[3] = new_player_channel->nna_pos[3];

    new_player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND;

    switch (nna) {
    case AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_OFF :
        play_key_off(new_player_channel);

        break;
    case AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_FADE :
        new_player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;

        break;
    }

    if (!player_host_channel->dct || player_host_channel->dna)
        goto find_nna;

    scan_player_channel = avctx->player_channel;
    nna_channel         = 0;

    do {
        if (scan_player_channel->host_channel == channel) {
            if (trigger_dct(player_host_channel, scan_player_channel, player_host_channel->dct)) {
                *virtual_channel   = nna_channel;
                new_player_channel = scan_player_channel;

                goto nna_found;
            }
        }

        scan_player_channel++;
    } while (++nna_channel < module->channels);
find_nna:
    scan_player_channel = avctx->player_channel;
    new_player_channel  = NULL;
    nna_channel         = 0;

    do {
        if (!((scan_player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED) || (scan_player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY))) {
            *virtual_channel   = nna_channel;
            new_player_channel = scan_player_channel;

            goto nna_found;
        }

        scan_player_channel++;
    } while (++nna_channel < module->channels);

    nna_max_volume      = 256;
    scan_player_channel = avctx->player_channel;
    nna_channel         = 0;

    do {
        if (scan_player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND) {
            nna_volume = player_channel->final_volume;

            if (nna_max_volume > nna_volume) {
                nna_max_volume = nna_volume;

                *virtual_channel   = nna_channel;
                new_player_channel = scan_player_channel;

                break;
            }
        }

        scan_player_channel++;
    } while (++nna_channel < module->channels);

    if (!new_player_channel)
        new_player_channel = player_channel;
nna_found:
    if (player_host_channel->dct && (new_player_channel != player_channel)) {
        scan_player_channel = avctx->player_channel;
        nna_channel         = 0;

        do {
            if (scan_player_channel->host_channel == channel) {
                if (trigger_dct(player_host_channel, scan_player_channel, player_host_channel->dct)) {
                    if (scan_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAG_VOLUME_DNA)
                        scan_player_channel->entry_pos[0] = scan_player_channel->dna_pos[0];

                    if (scan_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAG_PANNING_DNA)
                        scan_player_channel->entry_pos[1] = scan_player_channel->dna_pos[1];

                    if (scan_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAG_SLIDE_DNA)
                        scan_player_channel->entry_pos[2] = scan_player_channel->dna_pos[2];

                    if (scan_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAG_SPECIAL_DNA)
                        scan_player_channel->entry_pos[3] = scan_player_channel->dna_pos[3];

                    switch (player_host_channel->dna) {
                    case AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_CUT :
                        player_channel->mixer.flags = 0;

                        break;
                    case AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_OFF :
                        play_key_off(scan_player_channel);

                        break;
                    case AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_FADE :
                        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;
                    }
                }
            }

            scan_player_channel++;
        } while (++nna_channel < module->channels);
    }

    player_channel->flags &= ~AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED;

    return new_player_channel;
}

static AVSequencerPlayerChannel *play_note_got(AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t note, const uint16_t channel)
{
    const AVSequencerInstrument *const instrument = player_host_channel->instrument;
    const AVSequencerSample *const sample         = player_host_channel->sample;
    uint32_t note_swing, pitch_swing, frequency   = 0;
    uint32_t seed;
    uint16_t virtual_channel;

    player_host_channel->dct        = instrument->dct;
    player_host_channel->dna        = instrument->dna;
    note_swing                      = (player_channel->note_swing << 1) + 1;
    avctx->seed                     = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
    note_swing                      = ((uint64_t) seed * note_swing) >> 32;
    note_swing                     -= player_channel->note_swing;
    note                           += note_swing;
    player_host_channel->final_note = note;
    player_host_channel->finetune   = sample->finetune;

    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_TRANSPOSE)
        player_host_channel->finetune = player_host_channel->trans_finetune;

    player_host_channel->prev_volume_env    = player_channel->vol_env.envelope;
    player_host_channel->prev_panning_env   = player_channel->pan_env.envelope;
    player_host_channel->prev_slide_env     = player_channel->slide_env.envelope;
    player_host_channel->prev_auto_vib_env  = player_channel->auto_vib_env.envelope;
    player_host_channel->prev_auto_trem_env = player_channel->auto_trem_env.envelope;
    player_host_channel->prev_auto_pan_env  = player_channel->auto_pan_env.envelope;
    player_host_channel->prev_resonance_env = player_channel->resonance_env.envelope;

    player_channel = trigger_nna(avctx, player_host_channel, player_channel, channel, (uint16_t *const) &virtual_channel);

    player_channel->mixer.pos            = sample->start_offset;
    player_host_channel->virtual_channel = virtual_channel;
    player_channel->host_channel         = channel;
    player_channel->instrument           = player_host_channel->instrument;
    player_channel->sample               = player_host_channel->sample;
    player_channel->instr_note           = player_host_channel->instr_note;
    player_channel->sample_note          = player_host_channel->sample_note;

    if (player_channel->instr_note || player_channel->sample_note) {
        const int16_t final_note = player_host_channel->final_note;

        player_channel->final_note = final_note;
        frequency                  = get_tone_pitch(avctx, player_host_channel, player_channel, final_note);
    }

    note_swing    = pitch_swing = ((uint64_t) frequency * player_channel->pitch_swing) >> 16;
    pitch_swing <<= 1;

    if (pitch_swing < note_swing)
        pitch_swing = 0xFFFFFFFE;

    note_swing = pitch_swing++ >> 1;

    avctx->seed  = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
    pitch_swing  = ((uint64_t) seed * pitch_swing) >> 32;
    pitch_swing -= note_swing;

    if ((int32_t) (frequency += pitch_swing) < 0)
        frequency = 0;

    player_channel->frequency = frequency;

    return player_channel;
}

static AVSequencerPlayerChannel *play_note(AVSequencerContext *const avctx, const AVSequencerInstrument *const instrument, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t octave, uint16_t note, const uint16_t channel)
{
    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_RETRIG_NOTE;

    if ((note = get_key_table_note(avctx, instrument, player_host_channel, octave, note)) == 0x8000)
        return NULL;

    return play_note_got(avctx, player_host_channel, player_channel, note, channel);
}

static const void *assign_envelope_lut[] = {
    assign_volume_envelope,
    assign_panning_envelope,
    assign_slide_envelope,
    assign_vibrato_envelope,
    assign_tremolo_envelope,
    assign_pannolo_envelope,
    assign_channolo_envelope,
    assign_spenolo_envelope,
    assign_track_tremolo_envelope,
    assign_track_pannolo_envelope,
    assign_global_tremolo_envelope,
    assign_global_pannolo_envelope,
    assign_resonance_envelope
};

static const void *assign_auto_envelope_lut[] = {
    assign_auto_vibrato_envelope,
    assign_auto_tremolo_envelope,
    assign_auto_pannolo_envelope
};

static void init_new_instrument(AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel)
{
    const AVSequencerInstrument *const instrument = player_host_channel->instrument;
    const AVSequencerSample *const sample         = player_host_channel->sample;
    AVSequencerPlayerGlobals *player_globals;
    const AVSequencerEnvelope * (**assign_envelope)(const AVSequencerContext *const avctx, const AVSequencerInstrument *instrument, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const AVSequencerEnvelope **envelope, AVSequencerPlayerEnvelope **player_envelope);
    const AVSequencerEnvelope * (**assign_auto_envelope)(const AVSequencerSample *sample, AVSequencerPlayerChannel *const player_channel, AVSequencerPlayerEnvelope **player_envelope);
    uint32_t volume = 0, panning, i;

    if (instrument) {
        uint32_t volume_swing, abs_volume_swing, seed;

        player_channel->global_instr_volume = instrument->global_volume;
        player_channel->volume_swing        = instrument->volume_swing;
        volume                              = sample->global_volume * player_channel->global_volume;
        volume_swing                        = (volume * player_channel->volume_swing) >> 8;
        abs_volume_swing                    = (volume_swing << 1) + 1;
        avctx->seed                         = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
        abs_volume_swing                    = ((uint64_t) seed * abs_volume_swing) >> 32;
        abs_volume_swing                   -= volume_swing;

        if ((int32_t) (volume += abs_volume_swing) < 0)
            volume = 0;

        if (volume > (255*255))
            volume = 255*255;
    } else {
        volume = sample->global_volume * 255;
    }

    player_channel->instr_volume       = volume;
    player_globals                     = avctx->player_globals;
    player_channel->global_volume      = player_globals->global_volume;
    player_channel->global_sub_volume  = player_globals->global_sub_volume;
    player_channel->global_panning     = player_globals->global_panning;
    player_channel->global_sub_panning = player_globals->global_sub_panning;

    if (instrument) {
        player_channel->fade_out       = instrument->fade_out;
        player_channel->fade_out_count = 65535;
        player_host_channel->nna       = instrument->nna;
    }

    player_channel->auto_vibrato_sweep = sample->vibrato_sweep;
    player_channel->auto_tremolo_sweep = sample->tremolo_sweep;
    player_channel->auto_pannolo_sweep = sample->pannolo_sweep;
    player_channel->auto_vibrato_depth = sample->vibrato_depth;
    player_channel->auto_vibrato_rate  = sample->vibrato_rate;
    player_channel->auto_tremolo_depth = sample->tremolo_depth;
    player_channel->auto_tremolo_rate  = sample->tremolo_rate;
    player_channel->auto_pannolo_depth = sample->pannolo_depth;
    player_channel->auto_pannolo_rate  = sample->pannolo_rate;
    player_channel->auto_vibrato_count = 0;
    player_channel->auto_tremolo_count = 0;
    player_channel->auto_pannolo_count = 0;
    player_channel->auto_vibrato_freq  = 0;
    player_channel->auto_tremolo_vol   = 0;
    player_channel->auto_pannolo_pan   = 0;
    player_channel->slide_env_freq     = 0;
    player_channel->flags             &= AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED;
    player_host_channel->arpeggio_freq = 0;
    player_host_channel->vibrato_slide = 0;
    player_host_channel->tremolo_slide = 0;

    if (sample->env_proc_flags & AVSEQ_SAMPLE_FLAG_PROC_LINEAR_AUTO_VIB)
        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_FREQ_AUTO_VIB;

    if (instrument) {
        if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_PORTA_SLIDE_ENV)
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_PORTA_SLIDE_ENV;

        if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_LINEAR_SLIDE_ENV)
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV;
    }

    assign_envelope = (void *) &assign_envelope_lut;
    i               = 0;

    do {
        const AVSequencerEnvelope *envelope;
        AVSequencerPlayerEnvelope *player_envelope;
        const uint16_t mask = 1 << i;

        if (instrument) {
            if (assign_envelope[i](avctx, instrument, player_host_channel, player_channel, &envelope, &player_envelope) && (instrument->env_usage_flags & mask))
                continue;

            if ((player_envelope->envelope = envelope)) {
                uint8_t flags         = 0;
                uint16_t envelope_pos = 0, envelope_value = 0;

                if (instrument->env_proc_flags & mask)
                    flags  = AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD;

                if (instrument->env_retrig_flags & mask) {
                    flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_NO_RETRIG;

                    envelope_pos   = player_envelope->pos;
                    envelope_value = player_envelope->value;
                }

                if (instrument->env_random_flags & mask)
                    flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM;

                if (instrument->env_rnd_delay_flags & mask)
                    flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_RND_DELAY;

                player_envelope->value           = envelope_value;
                player_envelope->tempo           = envelope->tempo;
                player_envelope->tempo_count     = 0;
                player_envelope->sustain_counted = 0;
                player_envelope->loop_counted    = 0;
                player_envelope->sustain_start   = envelope->sustain_start;
                player_envelope->sustain_end     = envelope->sustain_end;
                player_envelope->sustain_count   = envelope->sustain_count;
                player_envelope->loop_start      = envelope->loop_start;
                player_envelope->loop_end        = envelope->loop_end;
                player_envelope->loop_count      = envelope->loop_count;
                player_envelope->value_min       = envelope->value_min;
                player_envelope->value_max       = envelope->value_max;
                player_envelope->rep_flags       = envelope->flags;

                set_envelope(player_channel, player_envelope, envelope_pos);

                player_envelope->flags |= flags;
            }
        } else {
            assign_envelope[i](avctx, instrument, player_host_channel, player_channel, &envelope, &player_envelope);

            player_envelope->envelope     = NULL;
            player_channel->vol_env.value = 0;
        }
    } while (++i < (sizeof (assign_envelope_lut) / sizeof (void *)));

    player_channel->vol_env.value = -1;
    assign_auto_envelope          = (void *) &assign_auto_envelope_lut;
    i                             = 0;

    do {
        const AVSequencerEnvelope *envelope;
        AVSequencerPlayerEnvelope *player_envelope;
        const uint16_t mask = 1 << i;

        envelope = assign_auto_envelope[i](sample, player_channel, &player_envelope);

        if (player_envelope->envelope && (sample->env_usage_flags & mask))
            continue;

        if ((player_envelope->envelope = envelope)) {
            uint8_t flags         = 0;
            uint16_t envelope_pos = 0, envelope_value = 0;

            if (sample->env_proc_flags & mask)
                flags  = AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD;

            if (sample->env_retrig_flags & mask) {
                flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_NO_RETRIG;

                envelope_pos   = player_envelope->pos;
                envelope_value = player_envelope->value;
            }

            if (sample->env_random_flags & mask)
                flags |= AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM;

            player_envelope->value           = envelope_value;
            player_envelope->tempo           = envelope->tempo;
            player_envelope->tempo_count     = 0;
            player_envelope->sustain_counted = 0;
            player_envelope->loop_counted    = 0;
            player_envelope->sustain_start   = envelope->sustain_start;
            player_envelope->sustain_end     = envelope->sustain_end;
            player_envelope->sustain_count   = envelope->sustain_count;
            player_envelope->loop_start      = envelope->loop_start;
            player_envelope->loop_end        = envelope->loop_end;
            player_envelope->loop_count      = envelope->loop_count;
            player_envelope->value_min       = envelope->value_min;
            player_envelope->value_max       = envelope->value_max;
            player_envelope->rep_flags       = envelope->flags;

            set_envelope(player_channel, player_envelope, envelope_pos);

            player_envelope->flags |= flags;
        }
    } while (++i < (sizeof (assign_auto_envelope_lut) / sizeof (void *)));

    panning                = (uint8_t) player_host_channel->track_note_panning;
    player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN;

    if (sample->flags & AVSEQ_SAMPLE_FLAG_SAMPLE_PANNING) {
        player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

        if (sample->flags & AVSEQ_SAMPLE_FLAG_SURROUND_PANNING)
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

        player_channel->panning            = sample->panning;
        player_channel->sub_panning        = sample->sub_panning;
        player_host_channel->pannolo_slide = 0;
        panning                            = (uint8_t) player_channel->panning;

        if (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
            player_host_channel->track_panning          = panning;
            player_host_channel->track_sub_panning      = player_channel->sub_panning;
            player_host_channel->track_note_panning     = panning;
            player_host_channel->track_note_sub_panning = player_channel->sub_panning;
            player_host_channel->flags                 &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                player_host_channel->flags         |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
        }
    } else {
        player_channel->panning     = player_host_channel->track_panning;
        player_channel->sub_panning = player_host_channel->track_sub_panning;
        player_channel->flags      &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN)
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
    }

    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

    if (instrument) {
        uint32_t panning_swing, seed;
        int32_t panning_separation;

        if ((instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) && (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN))
            player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

        if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_DEFAULT_PANNING) {
            player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

            if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_SURROUND_PANNING)
                player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

            player_channel->panning            = instrument->default_panning;
            player_channel->sub_panning        = instrument->default_sub_pan;
            player_host_channel->pannolo_slide = 0;
            panning                            = (uint8_t) player_channel->panning;

            if (instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
                player_host_channel->track_panning     = player_channel->panning;
                player_host_channel->track_sub_panning = player_channel->sub_panning;
                player_host_channel->flags            &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN);

                if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
            }
        }

        player_channel->pitch_pan_separation = instrument->pitch_pan_separation;
        player_channel->pitch_pan_center     = instrument->pitch_pan_center;
        player_channel->panning_swing        = instrument->panning_swing;
        panning_separation                   = ((int32_t) player_channel->pitch_pan_separation * (int32_t) (player_host_channel->instr_note - (player_channel->pitch_pan_center + 1))) >> 8;
        panning_swing                        = (player_channel->panning_swing << 1) + 1;
        avctx->seed                          = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
        panning_swing                        = ((uint64_t) seed * panning_swing) >> 32;
        panning_swing                       -= instrument->panning_swing;
        panning                             += panning_swing;

        if ((int32_t) (panning += panning_separation) < 0)
            panning = 0;

        if (panning > 255)
            panning = 255;

        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN)
            player_host_channel->track_panning = panning;
        else
            player_channel->panning            = panning;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN) {
            player_host_channel->track_panning = panning;
            player_channel->panning            = panning;
        }

        player_channel->note_swing  = instrument->note_swing;
        player_channel->pitch_swing = instrument->pitch_swing;
    }
}

static void init_new_sample(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel)
{
    const AVSequencerSample *const sample = player_host_channel->sample;
    const AVSequencerSynth *synth;
    AVMixerData *mixer;
    uint32_t samples;

    if ((samples = sample->samples)) {
        uint8_t flags, repeat_mode, playback_flags;

        player_channel->mixer.len  = samples;
        player_channel->mixer.data = sample->data;
        player_channel->mixer.rate = player_channel->frequency;
        flags                      = sample->flags;

        if (flags & AVSEQ_SAMPLE_FLAG_SUSTAIN_LOOP) {
            player_channel->mixer.repeat_start  = sample->sustain_repeat;
            player_channel->mixer.repeat_length = sample->sustain_rep_len;
            player_channel->mixer.repeat_count  = sample->sustain_rep_count;
            repeat_mode                         = sample->sustain_repeat_mode;
            flags                             >>= 1;
        } else {
            player_channel->mixer.repeat_start  = sample->repeat;
            player_channel->mixer.repeat_length = sample->rep_len;
            player_channel->mixer.repeat_count  = sample->rep_count;
            repeat_mode                         = sample->repeat_mode;
        }

        player_channel->mixer.repeat_counted  = 0;
        player_channel->mixer.bits_per_sample = sample->bits_per_sample;
        playback_flags                        = AVSEQ_MIXER_CHANNEL_FLAG_PLAY;

        if (sample->flags & AVSEQ_SAMPLE_FLAG_REVERSE)
            playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;

        if ((flags & AVSEQ_SAMPLE_FLAG_LOOP) && player_channel->mixer.repeat_length) {
            playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_LOOP;

            if (repeat_mode & AVSEQ_SAMPLE_REP_MODE_PINGPONG)
                playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG;

            if (repeat_mode & AVSEQ_SAMPLE_REP_MODE_BACKWARDS)
                playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
        }

        player_channel->mixer.flags = playback_flags;
    }

    if (!(synth = sample->synth) || !player_host_channel->synth || !(synth->pos_keep_mask & AVSEQ_SYNTH_POS_KEEP_MASK_CODE))
        player_host_channel->synth = synth;

    if ((player_channel->synth = player_host_channel->synth)) {
        const uint16_t *src_var;
        uint16_t *dst_var;
        uint16_t keep_flags, i;

        player_channel->mixer.flags |= AVSEQ_MIXER_CHANNEL_FLAG_PLAY;

        if (!player_host_channel->waveform_list || !(synth->pos_keep_mask & AVSEQ_SYNTH_POS_KEEP_MASK_WAVEFORMS)) {
            AVSequencerSynthWave *const *waveform_list = synth->waveform_list;
            const AVSequencerSynthWave *waveform       = NULL;

            player_host_channel->waveform_list = waveform_list;
            player_host_channel->waveforms     = synth->waveforms;

            if (synth->waveforms)
                waveform = waveform_list[0];

            player_channel->vibrato_waveform  = waveform;
            player_channel->tremolo_waveform  = waveform;
            player_channel->pannolo_waveform  = waveform;
            player_channel->arpeggio_waveform = waveform;
        }

        player_channel->waveform_list = player_host_channel->waveform_list;
        player_channel->waveforms     = player_host_channel->waveforms;

        keep_flags = synth->pos_keep_mask;

        if (!(synth->pos_keep_mask & AVSEQ_SYNTH_POS_KEEP_MASK_VOLUME))
            player_host_channel->entry_pos[0] = synth->entry_pos[0];

        if (!(synth->pos_keep_mask & AVSEQ_SYNTH_POS_KEEP_MASK_PANNING))
            player_host_channel->entry_pos[1] = synth->entry_pos[1];

        if (!(synth->pos_keep_mask & AVSEQ_SYNTH_POS_KEEP_MASK_SLIDE))
            player_host_channel->entry_pos[2] = synth->entry_pos[2];

        if (!(synth->pos_keep_mask & AVSEQ_SYNTH_POS_KEEP_MASK_SPECIAL))
            player_host_channel->entry_pos[3] = synth->entry_pos[3];

        player_channel->use_sustain_flags = synth->use_sustain_flags;

        if (!(player_channel->use_sustain_flags & AVSEQ_SYNTH_USE_SUSTAIN_FLAG_VOLUME_KEEP))
            player_host_channel->sustain_pos[0] = synth->sustain_pos[0];

        if (!(player_channel->use_sustain_flags & AVSEQ_SYNTH_USE_SUSTAIN_FLAG_PANNING_KEEP))
            player_host_channel->sustain_pos[1] = synth->sustain_pos[1];

        if (!(player_channel->use_sustain_flags & AVSEQ_SYNTH_USE_SUSTAIN_FLAG_SLIDE_KEEP))
            player_host_channel->sustain_pos[2] = synth->sustain_pos[2];

        if (!(player_channel->use_sustain_flags & AVSEQ_SYNTH_USE_SUSTAIN_FLAG_SPECIAL_KEEP))
            player_host_channel->sustain_pos[3] = synth->sustain_pos[3];

        player_channel->use_nna_flags = synth->use_nna_flags;

        if (!(synth->nna_pos_keep_mask & AVSEQ_SYNTH_NNA_POS_KEEP_MASK_VOLUME_NNA))
            player_host_channel->nna_pos[0] = synth->nna_pos[0];

        if (!(synth->nna_pos_keep_mask & AVSEQ_SYNTH_NNA_POS_KEEP_MASK_PANNING_NNA))
            player_host_channel->nna_pos[1] = synth->nna_pos[1];

        if (!(synth->nna_pos_keep_mask & AVSEQ_SYNTH_NNA_POS_KEEP_MASK_SLIDE_NNA))
            player_host_channel->nna_pos[2] = synth->nna_pos[2];

        if (!(synth->nna_pos_keep_mask & AVSEQ_SYNTH_NNA_POS_KEEP_MASK_SPECIAL_NNA))
            player_host_channel->nna_pos[3] = synth->nna_pos[3];

        if (!(synth->nna_pos_keep_mask & AVSEQ_SYNTH_NNA_POS_KEEP_MASK_VOLUME_DNA))
            player_host_channel->dna_pos[0] = synth->dna_pos[0];

        if (!(synth->nna_pos_keep_mask & AVSEQ_SYNTH_NNA_POS_KEEP_MASK_PANNING_DNA))
            player_host_channel->dna_pos[1] = synth->dna_pos[1];

        if (!(synth->nna_pos_keep_mask & AVSEQ_SYNTH_NNA_POS_KEEP_MASK_SLIDE_DNA))
            player_host_channel->dna_pos[2] = synth->dna_pos[2];

        if (!(synth->nna_pos_keep_mask & AVSEQ_SYNTH_NNA_POS_KEEP_MASK_SPECIAL_DNA))
            player_host_channel->dna_pos[3] = synth->dna_pos[3];

        keep_flags = 1;
        src_var    = (const uint16_t *) &(synth->variable[0]);
        dst_var    = (uint16_t *) &(player_host_channel->variable[0]);
        i          = 16;

        do {
            if (!(synth->var_keep_mask & keep_flags))
                *dst_var = *src_var;

            keep_flags <<= 1;
            src_var++;
            dst_var++;
        } while (--i);

        player_channel->entry_pos[0]       = player_host_channel->entry_pos[0];
        player_channel->entry_pos[1]       = player_host_channel->entry_pos[1];
        player_channel->entry_pos[2]       = player_host_channel->entry_pos[2];
        player_channel->entry_pos[3]       = player_host_channel->entry_pos[3];
        player_channel->sustain_pos[0]     = player_host_channel->sustain_pos[0];
        player_channel->sustain_pos[1]     = player_host_channel->sustain_pos[1];
        player_channel->sustain_pos[2]     = player_host_channel->sustain_pos[2];
        player_channel->sustain_pos[3]     = player_host_channel->sustain_pos[3];
        player_channel->nna_pos[0]         = player_host_channel->nna_pos[0];
        player_channel->nna_pos[1]         = player_host_channel->nna_pos[1];
        player_channel->nna_pos[2]         = player_host_channel->nna_pos[2];
        player_channel->nna_pos[3]         = player_host_channel->nna_pos[3];
        player_channel->dna_pos[0]         = player_host_channel->dna_pos[0];
        player_channel->dna_pos[1]         = player_host_channel->dna_pos[1];
        player_channel->dna_pos[2]         = player_host_channel->dna_pos[2];
        player_channel->dna_pos[3]         = player_host_channel->dna_pos[3];
        player_channel->variable[0]        = player_host_channel->variable[0];
        player_channel->variable[1]        = player_host_channel->variable[1];
        player_channel->variable[2]        = player_host_channel->variable[2];
        player_channel->variable[3]        = player_host_channel->variable[3];
        player_channel->variable[4]        = player_host_channel->variable[4];
        player_channel->variable[5]        = player_host_channel->variable[5];
        player_channel->variable[6]        = player_host_channel->variable[6];
        player_channel->variable[7]        = player_host_channel->variable[7];
        player_channel->variable[8]        = player_host_channel->variable[8];
        player_channel->variable[9]        = player_host_channel->variable[9];
        player_channel->variable[10]       = player_host_channel->variable[10];
        player_channel->variable[11]       = player_host_channel->variable[11];
        player_channel->variable[12]       = player_host_channel->variable[12];
        player_channel->variable[13]       = player_host_channel->variable[13];
        player_channel->variable[14]       = player_host_channel->variable[14];
        player_channel->variable[15]       = player_host_channel->variable[15];
        player_channel->cond_var[0]        = player_host_channel->cond_var[0] = synth->cond_var[0];
        player_channel->cond_var[1]        = player_host_channel->cond_var[1] = synth->cond_var[1];
        player_channel->cond_var[2]        = player_host_channel->cond_var[2] = synth->cond_var[2];
        player_channel->cond_var[3]        = player_host_channel->cond_var[3] = synth->cond_var[3];
        player_channel->finetune           = 0;
        player_channel->stop_forbid_mask   = 0;
        player_channel->vibrato_pos        = 0;
        player_channel->tremolo_pos        = 0;
        player_channel->pannolo_pos        = 0;
        player_channel->arpeggio_pos       = 0;
        player_channel->synth_flags        = 0;
        player_channel->kill_count[0]      = 0;
        player_channel->kill_count[1]      = 0;
        player_channel->kill_count[2]      = 0;
        player_channel->kill_count[3]      = 0;
        player_channel->wait_count[0]      = 0;
        player_channel->wait_count[1]      = 0;
        player_channel->wait_count[2]      = 0;
        player_channel->wait_count[3]      = 0;
        player_channel->wait_line[0]       = 0;
        player_channel->wait_line[1]       = 0;
        player_channel->wait_line[2]       = 0;
        player_channel->wait_line[3]       = 0;
        player_channel->wait_type[0]       = 0;
        player_channel->wait_type[1]       = 0;
        player_channel->wait_type[2]       = 0;
        player_channel->wait_type[3]       = 0;
        player_channel->porta_up           = 0;
        player_channel->porta_dn           = 0;
        player_channel->portamento         = 0;
        player_channel->vibrato_slide      = 0;
        player_channel->vibrato_rate       = 0;
        player_channel->vibrato_depth      = 0;
        player_channel->arpeggio_slide     = 0;
        player_channel->arpeggio_speed     = 0;
        player_channel->arpeggio_transpose = 0;
        player_channel->arpeggio_finetune  = 0;
        player_channel->vol_sl_up          = 0;
        player_channel->vol_sl_dn          = 0;
        player_channel->tremolo_slide      = 0;
        player_channel->tremolo_depth      = 0;
        player_channel->tremolo_rate       = 0;
        player_channel->pan_sl_left        = 0;
        player_channel->pan_sl_right       = 0;
        player_channel->pannolo_slide      = 0;
        player_channel->pannolo_depth      = 0;
        player_channel->pannolo_rate       = 0;
    }

    player_channel->finetune = player_host_channel->finetune;
    mixer                    = avctx->player_mixer_data;

    if (mixer->mixctx->set_channel)
        mixer->mixctx->set_channel(mixer, &player_channel->mixer, player_host_channel->virtual_channel);
}

static uint32_t get_note(AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *player_channel, const uint16_t channel)
{
    const AVSequencerModule *const module = avctx->player_module;
    const AVSequencerTrack *track;
    const AVSequencerTrackRow *track_data;
    const AVSequencerInstrument *instrument;
    AVSequencerPlayerChannel *new_player_channel;
    uint32_t instr;
    uint16_t octave_note;
    uint8_t octave;
    int8_t note;

    if (player_host_channel->pattern_delay_count || (player_host_channel->tempo_counter != player_host_channel->note_delay) || !(track = player_host_channel->track))
        return 0;

    track_data = track->data + player_host_channel->row;

    if (!(track_data->octave || track_data->note || track_data->instrument))
        return 0;

    octave_note = (track_data->octave << 8) | track_data->note;
    octave      = track_data->octave;

    if ((note = track_data->note) < 0) {
        switch ((int) note) {
        case AVSEQ_TRACK_DATA_NOTE_END :
            if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP)) {
                player_host_channel->flags    |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_BREAK;
                player_host_channel->break_row = 0;
            }

            return 1;
        case AVSEQ_TRACK_DATA_NOTE_FADE :
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;

            break;
        case AVSEQ_TRACK_DATA_NOTE_HOLD_DELAY :
            break;
        case AVSEQ_TRACK_DATA_NOTE_KEYOFF :
            play_key_off(player_channel);

            break;
        case AVSEQ_TRACK_DATA_NOTE_OFF :
            player_channel->volume = 0;

            break;
        case AVSEQ_TRACK_DATA_NOTE_KILL :
            player_host_channel->instrument  = NULL;
            player_host_channel->sample      = NULL;
            player_host_channel->instr_note  = 0;
            player_host_channel->sample_note = 0;

            if (player_channel->host_channel == channel)
                player_channel->mixer.flags = 0;

            break;
        }

        return 0;
    } else if ((instr = track_data->instrument)) {
        instr--;

        if ((instr >= module->instruments) || !(instrument = module->instrument_list[instr]))
            return 0;

        if (!(instrument->flags & AVSEQ_INSTRUMENT_FLAG_NO_INSTR_TRANSPOSE)) {
            AVSequencerOrderData *order_data = player_host_channel->order;

            if (order_data->instr_transpose) {
                AVSequencerInstrument *instrument_scan;

                instr += order_data->instr_transpose;

                if ((instr < module->instruments) && ((instrument_scan = module->instrument_list[instr])))
                    instrument = instrument_scan;
            }
        }

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TONE_PORTA) {
            player_host_channel->tone_porta_target_pitch = get_tone_pitch(avctx, player_host_channel, player_channel, get_key_table_note(avctx, instrument, player_host_channel, octave, note));

            return 0;
        }

        if (octave_note) {
            const AVSequencerSample *sample;

            if ((new_player_channel = play_note(avctx, instrument, player_host_channel, player_channel, octave, note, channel)))
                player_channel = new_player_channel;

            sample                     = player_host_channel->sample;
            player_channel->volume     = sample->volume;
            player_channel->sub_volume = sample->sub_volume;

            init_new_instrument(avctx, player_host_channel, player_channel);
            init_new_sample(avctx, player_host_channel, player_channel);
        } else {
            const AVSequencerSample *sample;
            uint16_t note;

            if (!instrument)
                return 0;

            if ((note = player_host_channel->instr_note)) {
                if ((note = get_key_table(avctx, instrument, player_host_channel, note)) == 0x8000)
                    return 0;

                if ((player_channel->host_channel != channel) || (player_host_channel->instrument != instrument)) {
                    if ((new_player_channel = play_note_got(avctx, player_host_channel, player_channel, note, channel)))
                        player_channel = new_player_channel;
                }
            } else {
                note                             = get_key_table(avctx, instrument, player_host_channel, 1);
                player_host_channel->instr_note  = 0;
                player_host_channel->sample_note = 0;

                if ((new_player_channel = play_note_got(avctx, player_host_channel, player_channel, note, channel)))
                    player_channel = new_player_channel;

                player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED;
            }

            sample                     = player_host_channel->sample;
            player_channel->volume     = sample->volume;
            player_channel->sub_volume = sample->sub_volume;

            init_new_instrument(avctx, player_host_channel, player_channel);

            if (!(instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_LOCK_INSTR_WAVE))
                init_new_sample(avctx, player_host_channel, player_channel);
        }
    } else if ((instrument = player_host_channel->instrument) && module->instruments) {
        if (!(instrument->flags & AVSEQ_INSTRUMENT_FLAG_NO_INSTR_TRANSPOSE)) {
            AVSequencerOrderData *order_data = player_host_channel->order;

            if (order_data->instr_transpose) {
                const AVSequencerInstrument *instrument_scan;

                do {
                    if (module->instrument_list[instr] == instrument)
                        break;
                } while (++instr < module->instruments);

                instr += order_data->instr_transpose;

                if ((instr < module->instruments) && ((instrument_scan = module->instrument_list[instr])))
                    instrument = instrument_scan;
            }
        }

        if ((new_player_channel = play_note(avctx, instrument, player_host_channel, player_channel, octave, note, channel))) {
            const AVSequencerSample *const sample = player_host_channel->sample;

            new_player_channel->mixer.pos = sample->start_offset;

            if (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_VOLUME_ONLY) {
                new_player_channel->volume     = player_channel->volume;
                new_player_channel->sub_volume = player_channel->sub_volume;
            } else if (player_channel != new_player_channel) {
                new_player_channel->volume               = player_channel->volume;
                new_player_channel->sub_volume           = player_channel->sub_volume;
                new_player_channel->instr_volume         = player_channel->instr_volume;
                new_player_channel->panning              = player_channel->panning;
                new_player_channel->sub_panning          = player_channel->sub_panning;
                new_player_channel->final_volume         = player_channel->final_volume;
                new_player_channel->final_panning        = player_channel->final_panning;
                new_player_channel->global_volume        = player_channel->global_volume;
                new_player_channel->global_sub_volume    = player_channel->global_sub_volume;
                new_player_channel->global_panning       = player_channel->global_panning;
                new_player_channel->global_sub_panning   = player_channel->global_sub_panning;
                new_player_channel->volume_swing         = player_channel->volume_swing;
                new_player_channel->panning_swing        = player_channel->panning_swing;
                new_player_channel->pitch_swing          = player_channel->pitch_swing;
                new_player_channel->host_channel         = player_channel->host_channel;
                new_player_channel->flags                = player_channel->flags;
                new_player_channel->vol_env              = player_channel->vol_env;
                new_player_channel->pan_env              = player_channel->pan_env;
                new_player_channel->slide_env            = player_channel->slide_env;
                new_player_channel->resonance_env        = player_channel->resonance_env;
                new_player_channel->auto_vib_env         = player_channel->auto_vib_env;
                new_player_channel->auto_trem_env        = player_channel->auto_trem_env;
                new_player_channel->auto_pan_env         = player_channel->auto_pan_env;
                new_player_channel->slide_env_freq       = player_channel->slide_env_freq;
                new_player_channel->auto_vibrato_freq    = player_channel->auto_vibrato_freq;
                new_player_channel->auto_tremolo_vol     = player_channel->auto_tremolo_vol;
                new_player_channel->auto_pannolo_pan     = player_channel->auto_pannolo_pan;
                new_player_channel->auto_vibrato_count   = player_channel->auto_vibrato_count;
                new_player_channel->auto_tremolo_count   = player_channel->auto_tremolo_count;
                new_player_channel->auto_pannolo_count   = player_channel->auto_pannolo_count;
                new_player_channel->fade_out             = player_channel->fade_out;
                new_player_channel->fade_out_count       = player_channel->fade_out_count;
                new_player_channel->pitch_pan_separation = player_channel->pitch_pan_separation;
                new_player_channel->pitch_pan_center     = player_channel->pitch_pan_center;
                new_player_channel->dca                  = player_channel->dca;
                new_player_channel->hold                 = player_channel->hold;
                new_player_channel->decay                = player_channel->decay;
                new_player_channel->auto_vibrato_sweep   = player_channel->auto_vibrato_sweep;
                new_player_channel->auto_tremolo_sweep   = player_channel->auto_tremolo_sweep;
                new_player_channel->auto_pannolo_sweep   = player_channel->auto_pannolo_sweep;
                new_player_channel->auto_vibrato_depth   = player_channel->auto_vibrato_depth;
                new_player_channel->auto_vibrato_rate    = player_channel->auto_vibrato_rate;
                new_player_channel->auto_tremolo_depth   = player_channel->auto_tremolo_depth;
                new_player_channel->auto_tremolo_rate    = player_channel->auto_tremolo_rate;
                new_player_channel->auto_pannolo_depth   = player_channel->auto_pannolo_depth;
                new_player_channel->auto_pannolo_rate    = player_channel->auto_pannolo_rate;
            }

            init_new_instrument(avctx, player_host_channel, new_player_channel);
            init_new_sample(avctx, player_host_channel, new_player_channel);
        }
    }

    return 0;
}

static const void *se_lut[128] = {
    se_stop,    se_kill,    se_wait,    se_waitvol, se_waitpan, se_waitsld, se_waitspc, se_jump,
    se_jumpeq,  se_jumpne,  se_jumppl,  se_jumpmi,  se_jumplt,  se_jumple,  se_jumpgt,  se_jumpge,
    se_jumpvs,  se_jumpvc,  se_jumpcs,  se_jumpcc,  se_jumpls,  se_jumphi,  se_jumpvol, se_jumppan,
    se_jumpsld, se_jumpspc, se_call,    se_ret,     se_posvar,  se_load,    se_add,     se_addx,
    se_sub,     se_subx,    se_cmp,     se_mulu,    se_muls,    se_dmulu,   se_dmuls,   se_divu,
    se_divs,    se_modu,    se_mods,    se_ddivu,   se_ddivs,   se_ashl,    se_ashr,    se_lshl,
    se_lshr,    se_rol,     se_ror,     se_rolx,    se_rorx,    se_or,      se_and,     se_xor,
    se_not,     se_neg,     se_negx,    se_extb,    se_ext,     se_xchg,    se_swap,    se_getwave,
    se_getwlen, se_getwpos, se_getchan, se_getnote, se_getrans, se_getptch, se_getper,  se_getfx,
    se_getarpw, se_getarpv, se_getarpl, se_getarpp, se_getvibw, se_getvibv, se_getvibl, se_getvibp,
    se_gettrmw, se_gettrmv, se_gettrml, se_gettrmp, se_getpanw, se_getpanv, se_getpanl, se_getpanp,
    se_getrnd,  se_getsine, se_portaup, se_portadn, se_vibspd,  se_vibdpth, se_vibwave, se_vibwavp,
    se_vibrato, se_vibval,  se_arpspd,  se_arpwave, se_arpwavp, se_arpegio, se_arpval,  se_setwave,
    se_isetwav, se_setwavp, se_setrans, se_setnote, se_setptch, se_setper,  se_reset,   se_volslup,
    se_volsldn, se_trmspd,  se_trmdpth, se_trmwave, se_trmwavp, se_tremolo, se_trmval,  se_panleft,
    se_panrght, se_panspd,  se_pandpth, se_panwave, se_panwavp, se_pannolo, se_panval,  se_nop
};

static int execute_synth(AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel, const int synth_type)
{
    uint16_t synth_count = 0, bit_mask = 1 << synth_type;

    do {
        const AVSequencerSynth *synth          = player_channel->synth;
        const AVSequencerSynthCode *synth_code = synth->code;
        uint16_t synth_code_line               = player_channel->entry_pos[synth_type], instruction_data, i;
        int8_t instruction;
        int src_var, dst_var;

        synth_code += synth_code_line;

        if (player_channel->wait_count[synth_type]--) {
exec_synth_done:

            if ((player_channel->synth_flags & bit_mask) && !(player_channel->kill_count[synth_type]--))
                return 0;

            return 1;
        }

        player_channel->wait_count[synth_type] = 0;

        if ((synth_code_line >= synth->size) || ((int8_t) player_channel->wait_type[synth_type] < 0))
            goto exec_synth_done;

        i = 4 - 1;

        do {
            int8_t wait_volume_type;

            if (((wait_volume_type = ~player_channel->wait_type[synth_type]) >= 0) && (wait_volume_type == i) && (player_channel->wait_line[synth_type] == synth_code_line))
                player_channel->wait_type[synth_type] = 0;

        } while (i--);

        instruction      = synth_code->instruction;
        dst_var          = synth_code->src_dst_var;
        instruction_data = synth_code->data;

        if (!instruction && !dst_var && !instruction_data)
            goto exec_synth_done;

        src_var  = dst_var >> 4;
        dst_var &= 0x0F;

        synth_code_line++;

        if (instruction < 0) {
            const AVSequencerPlayerEffects *effects_lut;
            void (*check_func)(const AVSequencerContext *const avctx, AVSequencerPlayerHostChannel *const player_host_channel, AVSequencerPlayerChannel *const player_channel, const uint16_t channel, uint16_t *const fx_byte, uint16_t *const data_word, uint16_t *const flags);
            uint16_t fx_byte, data_word, flags;

            fx_byte     = ~instruction;
            effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            data_word   = instruction_data + player_channel->variable[src_var];
            flags       = effects_lut->flags;

            if ((check_func = effects_lut->check_fx_func)) {
                check_func(avctx, player_host_channel, player_channel, player_channel->host_channel, &fx_byte, &data_word, &flags);

                effects_lut = (const AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            }

            if (!effects_lut->pre_pattern_func) {
                instruction_data                     = player_host_channel->virtual_channel;
                player_host_channel->virtual_channel = channel;

                effects_lut->effect_func(avctx, player_host_channel, player_channel, player_channel->host_channel, fx_byte, data_word);

                player_host_channel->virtual_channel = instruction_data;
            }

            player_channel->entry_pos[synth_type] = synth_code_line;
        } else {
            uint16_t (**fx_exec_func)(const AVSequencerContext *const avctx, AVSequencerPlayerChannel *const player_channel, const uint16_t virtual_channel, uint16_t synth_code_line, const int src_var, int dst_var, uint16_t instruction_data, const int synth_type);

            fx_exec_func = (avctx->synth_code_exec_lut ? avctx->synth_code_exec_lut : se_lut);

            player_channel->entry_pos[synth_type] = fx_exec_func[(uint8_t) instruction](avctx, player_channel, channel, synth_code_line, src_var, dst_var, instruction_data, synth_type);
        }
    } while (++synth_count);

    return 0;
}

static const int8_t empty_waveform[256];

int avseq_playback_handler(AVMixerData *mixer_data)
{
    AVSequencerContext *const avctx                   = (AVSequencerContext *) mixer_data->opaque;
    const AVSequencerModule *const module             = avctx->player_module;
    const AVSequencerSong *const song                 = avctx->player_song;
    AVSequencerPlayerGlobals *const player_globals    = avctx->player_globals;
    AVSequencerPlayerHostChannel *player_host_channel = avctx->player_host_channel;
    AVSequencerPlayerChannel *player_channel          = avctx->player_channel;
    const AVSequencerPlayerHook *player_hook;
    uint16_t channel, virtual_channel;

    if (!(module && song && player_globals && player_host_channel && player_channel))
        return 0;

    channel = 0;

    do {
        if (mixer_data->mixctx->get_channel)
            mixer_data->mixctx->get_channel(mixer_data, &player_channel->mixer, channel);

        player_channel++;
    } while (++channel < module->channels);

    if (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_TRACE_MODE) {
        if (!player_globals->trace_count--)
            player_globals->trace_count = 0;

        return 0;
    }

    player_hook = avctx->player_hook;

    if (player_hook && (player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_BEGINNING) &&
                       (((player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_SONG_END) &&
                       (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_SONG_END)) ||
                       !(player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_SONG_END)))
        player_hook->hook_func(avctx, player_hook->hook_data, player_hook->hook_len);

    if (player_globals->play_type & AVSEQ_PLAYER_GLOBALS_PLAY_TYPE_SONG) {
        uint32_t play_time_calc, play_time_advance, play_time_fraction;

        play_time_calc                  = ((uint64_t) player_globals->tempo * player_globals->relative_speed) >> 16;
        play_time_advance               = UINT64_C(AV_TIME_BASE * 655360) / play_time_calc;
        play_time_fraction              = ((UINT64_C(AV_TIME_BASE * 655360) % play_time_calc) << 32) / play_time_calc;
        player_globals->play_time_frac += play_time_fraction;

        if (player_globals->play_time_frac < play_time_fraction)
            play_time_advance++;

        player_globals->play_time      += play_time_advance;
        play_time_calc                  = player_globals->tempo;
        play_time_advance               = UINT64_C(AV_TIME_BASE * 655360) / play_time_calc;
        play_time_fraction              = ((UINT64_C(AV_TIME_BASE * 655360) % play_time_calc) << 32) / play_time_calc;
        player_globals->play_tics_frac += play_time_fraction;

        if (player_globals->play_tics_frac < play_time_fraction)
            play_time_advance++;

        player_globals->play_tics += play_time_advance;
    }

    channel = 0;

    do {
        player_channel = avctx->player_channel + player_host_channel->virtual_channel;

        if ((player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT) &&
            (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE)) {
            const AVSequencerTrack *const old_track        = player_host_channel->track;
            const AVSequencerTrackEffect const *old_effect = player_host_channel->effect;
            const uint32_t old_tempo_counter               = player_host_channel->tempo_counter;
            const uint16_t old_row                         = player_host_channel->row;

            player_host_channel->flags        &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT|AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE);
            player_host_channel->track         = (const AVSequencerTrack *) player_host_channel->instrument;
            player_host_channel->effect        = NULL;
            player_host_channel->row           = (uint32_t) player_host_channel->sample;
            player_host_channel->instrument    = NULL;
            player_host_channel->sample        = NULL;

            get_effects(avctx, player_host_channel, player_channel, channel);

            player_host_channel->tempo_counter = player_host_channel->note_delay;

            get_note(avctx, player_host_channel, player_channel, channel);
            run_effects(avctx, player_host_channel, player_channel, channel);

            player_host_channel->track         = old_track;
            player_host_channel->effect        = old_effect;
            player_host_channel->tempo_counter = old_tempo_counter;
            player_host_channel->row           = old_row;
        }

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT) {
            const uint16_t note = player_host_channel->instr_note;

            player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT;

            if ((int8_t) note < 0) {
                switch ((int) note) {
                case AVSEQ_TRACK_DATA_NOTE_FADE :
                    player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;

                    break;
                case AVSEQ_TRACK_DATA_NOTE_HOLD_DELAY :
                    break;
                case AVSEQ_TRACK_DATA_NOTE_KEYOFF :
                    play_key_off(player_channel);

                    break;
                case AVSEQ_TRACK_DATA_NOTE_OFF :
                    player_channel->volume = 0;

                    break;
                case AVSEQ_TRACK_DATA_NOTE_KILL :
                    player_host_channel->instrument  = NULL;
                    player_host_channel->sample      = NULL;
                    player_host_channel->instr_note  = 0;
                    player_host_channel->sample_note = 0;

                    if (player_channel->host_channel == channel)
                        player_channel->mixer.flags = 0;

                    break;
                }
            } else {
                const AVSequencerInstrument *const instrument = player_host_channel->instrument;
                AVSequencerPlayerChannel *new_player_channel;

                if ((new_player_channel = play_note(avctx, instrument,
                                                    player_host_channel, player_channel,
                                                    note / 12,
                                                    note % 12, channel)))
                    player_channel = new_player_channel;

                player_channel->volume     = player_host_channel->sample_note;
                player_channel->sub_volume = 0;

                init_new_instrument(avctx, player_host_channel, player_channel);
                init_new_sample(avctx, player_host_channel, player_channel);
            }
        }

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE) {
            const AVSequencerInstrument *instrument;
            const AVSequencerSample *sample = player_host_channel->sample;
            const uint32_t frequency        = (uint32_t) player_host_channel->instrument;
            uint32_t i;
            uint16_t virtual_channel;

            player_host_channel->flags             &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE;
            player_host_channel->dct                = 0;
            player_host_channel->nna                = AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_CUT;
            player_host_channel->finetune           = sample->finetune;
            player_host_channel->prev_auto_vib_env  = player_channel->auto_vib_env.envelope;
            player_host_channel->prev_auto_trem_env = player_channel->auto_trem_env.envelope;
            player_host_channel->prev_auto_pan_env  = player_channel->auto_pan_env.envelope;

            player_channel = trigger_nna(avctx, player_host_channel, player_channel, channel, (uint16_t *) &virtual_channel);

            sample                               = player_host_channel->sample;
            player_channel->mixer.pos            = sample->start_offset;
            player_host_channel->virtual_channel = virtual_channel;
            player_channel->host_channel         = channel;
            player_host_channel->instrument      = NULL;
            player_channel->sample               = sample;
            player_channel->frequency            = frequency;
            player_channel->volume               = player_host_channel->instr_note;
            player_channel->sub_volume           = 0;
            player_host_channel->instr_note      = 0;

            init_new_instrument(avctx, player_host_channel, player_channel);

            i = -1;

            while (++i < module->instruments) {
                uint16_t smp = -1;

                if (!(instrument = module->instrument_list[i]))
                    continue;

                while (++smp < instrument->samples) {
                    if (!(sample = instrument->sample_list[smp]))
                        continue;

                    if (sample == player_channel->sample) {
                        player_host_channel->instrument = instrument;

                        goto instrument_found;
                    }
                }
            }
instrument_found:
            player_channel->instrument = player_host_channel->instrument;

            init_new_sample(avctx, player_host_channel, player_channel);
        }

        if (!(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_NO_PROC_PATTERN) && player_host_channel->tempo) {
            do {
                process_row(avctx, player_host_channel, player_channel, channel);
                get_effects(avctx, player_host_channel, player_channel, channel);

                if (player_channel->host_channel == channel) {
                    if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_VIBRATO)) {
                        const int32_t slide_value = player_host_channel->vibrato_slide;

                        player_host_channel->vibrato_slide = 0;
                        player_channel->frequency         -= slide_value;
                    }

                    if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOLO)) {
                        int16_t slide_value = player_host_channel->tremolo_slide;

                        player_host_channel->tremolo_slide = 0;

                        if ((int16_t) (slide_value = (player_channel->volume - slide_value)) < 0)
                            slide_value = 0;

                        if (slide_value > 255)
                            slide_value = 255;

                        player_channel->volume = slide_value;
                    }
                }
            } while (get_note(avctx, player_host_channel, player_channel, channel));
        }

        player_host_channel->virtual_channels = 0;
        player_host_channel++;
    } while (++channel < song->channels);

    channel             = 0;
    player_host_channel = avctx->player_host_channel;

    do {
        if (!(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_NO_PROC_PATTERN) && player_host_channel->tempo) {
            player_channel = avctx->player_channel + player_host_channel->virtual_channel;

            run_effects(avctx, player_host_channel, player_channel, channel);
        }

        player_host_channel->virtual_channels = 0;
        player_host_channel++;
    } while (++channel < song->channels);

    virtual_channel = 0;
    channel         = 0;
    player_channel  = avctx->player_channel;

    do {
        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED)
            player_channel->mixer.flags &= ~AVSEQ_MIXER_CHANNEL_FLAG_PLAY;

        if (player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            const AVSequencerSample *sample;
            AVSequencerPlayerEnvelope *player_envelope;
            uint32_t frequency, host_volume, virtual_volume;
            uint32_t auto_vibrato_depth, auto_vibrato_count;
            int32_t auto_vibrato_value;
            uint16_t flags, slide_envelope_value;
            int16_t panning, abs_panning, panning_envelope_value;

            player_host_channel = avctx->player_host_channel + player_channel->host_channel;
            player_envelope     = &player_channel->vol_env;

            if (player_envelope->tempo) {
                const uint16_t volume = run_envelope(avctx, player_envelope, 1, -0x8000);

                if (!player_envelope->tempo) {
                    if (!(volume >> 8))
                        goto turn_note_off;

                    player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;
                }
            }

            run_envelope(avctx, &player_channel->pan_env, 1, 0);
            slide_envelope_value = run_envelope(avctx, &player_channel->slide_env, 1, 0);

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_PORTA_SLIDE_ENV) {
                const uint32_t old_frequency = player_channel->frequency;

                player_channel->frequency += player_channel->slide_env_freq;

                if ((frequency = player_channel->frequency)) {
                    if ((int16_t) slide_envelope_value < 0) {
                        slide_envelope_value = -slide_envelope_value;

                        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV)
                            frequency = linear_slide_down(avctx, player_channel, frequency, slide_envelope_value);
                        else
                            frequency = amiga_slide_down(player_channel, frequency, slide_envelope_value);
                    } else if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV) {
                        frequency = linear_slide_up(avctx, player_channel, frequency, slide_envelope_value);
                    } else {
                        frequency = amiga_slide_up(player_channel, frequency, slide_envelope_value);
                    }

                    player_channel->slide_env_freq += old_frequency - frequency;
                }
            } else {
                const uint32_t *frequency_lut;
                uint32_t frequency, next_frequency, slide_envelope_frequency, old_frequency;
                int16_t octave, note;
                const int16_t slide_note = (int16_t) slide_envelope_value >> 8;
                int32_t finetune         = slide_envelope_value & 0xFF;

                octave = slide_note / 12;
                note   = slide_note % 12;

                if (note < 0) {
                    octave--;
                    note    += 12;
                    finetune = -finetune;
                }

                frequency_lut                   = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
                frequency                       = *frequency_lut++;
                next_frequency                  = *frequency_lut - frequency;
                frequency                      += (finetune * (int32_t) next_frequency) >> 8;
                slide_envelope_frequency        = player_channel->slide_env_freq;
                old_frequency                   = player_channel->frequency;
                slide_envelope_frequency       += old_frequency;
                player_channel->frequency       = frequency = ((uint64_t) frequency * slide_envelope_frequency) >> (24 - octave);
                player_channel->slide_env_freq += old_frequency - frequency;
            }

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_FADING) {
                int32_t fade_out = (uint32_t) player_channel->fade_out_count;

                if ((fade_out -= (int32_t) player_channel->fade_out) <= 0)
                    goto turn_note_off;

                player_channel->fade_out_count = fade_out;
            }

            auto_vibrato_value = run_envelope(avctx, &player_channel->auto_vib_env, player_channel->auto_vibrato_rate, 0);
            auto_vibrato_depth = player_channel->auto_vibrato_depth << 8;
            auto_vibrato_count = (uint32_t) player_channel->auto_vibrato_count + player_channel->auto_vibrato_sweep;

            if (auto_vibrato_count > auto_vibrato_depth)
                auto_vibrato_count = auto_vibrato_depth;

            player_channel->auto_vibrato_count = auto_vibrato_count;
            auto_vibrato_count               >>= 8;

            if ((auto_vibrato_value *= (int32_t) -auto_vibrato_count)) {
                uint32_t old_frequency = player_channel->frequency;

                auto_vibrato_value      >>= 7 - 2;
                player_channel->frequency -= player_channel->auto_vibrato_freq;

                if ((frequency = player_channel->frequency)) {
                    if (auto_vibrato_value < 0) {
                        auto_vibrato_value = -auto_vibrato_value;

                        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_FREQ_AUTO_VIB)
                            frequency = linear_slide_up(avctx, player_channel, frequency, auto_vibrato_value);
                        else
                            frequency = amiga_slide_up(player_channel, frequency, auto_vibrato_value);
                    } else if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_FREQ_AUTO_VIB) {
                        frequency = linear_slide_down(avctx, player_channel, frequency, auto_vibrato_value);
                    } else {
                        frequency = amiga_slide_down(player_channel, frequency, auto_vibrato_value);
                    }

                    player_channel->auto_vibrato_freq -= old_frequency - frequency;
                }
            }

            if ((sample = player_channel->sample) && sample->synth) {
                if (!execute_synth(avctx, player_host_channel, player_channel, channel, 0))
                    goto turn_note_off;

                if (!execute_synth(avctx, player_host_channel, player_channel, channel, 1))
                    goto turn_note_off;

                if (!execute_synth(avctx, player_host_channel, player_channel, channel, 2))
                    goto turn_note_off;

                if (!execute_synth(avctx, player_host_channel, player_channel, channel, 3))
                    goto turn_note_off;
            }

            if ((!player_channel->mixer.data || !player_channel->mixer.bits_per_sample) && (player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY)) {
                player_channel->mixer.pos             = 0;
                player_channel->mixer.len             = (sizeof (empty_waveform) / sizeof (empty_waveform[0]));
                player_channel->mixer.data            = (int16_t *) &empty_waveform;
                player_channel->mixer.repeat_start    = 0;
                player_channel->mixer.repeat_length   = (sizeof (empty_waveform) / sizeof (empty_waveform[0]));
                player_channel->mixer.repeat_count    = 0;
                player_channel->mixer.repeat_counted  = 0;
                player_channel->mixer.bits_per_sample = (sizeof (empty_waveform[0]) * 8);
                player_channel->mixer.flags           = AVSEQ_MIXER_CHANNEL_FLAG_LOOP|AVSEQ_MIXER_CHANNEL_FLAG_PLAY;
            }

            frequency = player_channel->frequency;

            if (sample) {
                if (frequency < sample->rate_min)
                    frequency = sample->rate_min;

                if (frequency > sample->rate_max)
                    frequency = sample->rate_max;
            }

            if (!(player_channel->frequency = frequency)) {
turn_note_off:
                player_channel->mixer.flags = 0;

                goto not_calculate_no_playing;
            }

            if (!(player_channel->mixer.rate = ((uint64_t) frequency * player_globals->relative_pitch) >> 16))
                goto turn_note_off;

            if (!(song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_GLOBAL_NEW_ONLY)) {
                player_channel->global_volume      = player_globals->global_volume;
                player_channel->global_sub_volume  = player_globals->global_sub_volume;
                player_channel->global_panning     = player_globals->global_panning;
                player_channel->global_sub_panning = player_globals->global_sub_panning;
            }

            host_volume = player_channel->volume;

            player_host_channel->virtual_channels++;
            virtual_channel++;

            if (!(player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND) && (player_host_channel->virtual_channel == channel) && (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_EXEC) && (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_OFF))
                host_volume = 0;

            host_volume                        *= (uint16_t) player_host_channel->track_volume * (uint16_t) player_channel->instr_volume;
            virtual_volume                      = (((uint16_t) player_channel->vol_env.value >> 8) * (uint16_t) player_channel->global_volume) * (uint16_t) player_channel->fade_out_count;
            player_channel->mixer.volume        = player_channel->final_volume = ((uint64_t) host_volume * virtual_volume) / UINT64_C(70660093200890625); /* / (255ULL*255ULL*255ULL*255ULL*65535ULL*255ULL) */
            flags                               = 0;
            player_channel->flags              &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SURROUND;
            player_channel->mixer.flags        &= ~AVSEQ_MIXER_CHANNEL_FLAG_SURROUND;

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                flags = AVSEQ_MIXER_CHANNEL_FLAG_SURROUND;

            panning = (uint8_t) player_channel->panning;

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN) {
                panning = (uint8_t) player_host_channel->track_panning;
                flags   = 0;

                if ((player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN) || (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHANNEL_SUR_PAN))
                    flags = AVSEQ_MIXER_CHANNEL_FLAG_SURROUND;
            }

            player_channel->flags |= flags;

            if (!(song->flags & AVSEQ_SONG_FLAG_MONO))
                player_channel->mixer.flags |= flags;

            if (panning == 255)
                panning++;

            panning_envelope_value = panning;

            if ((int16_t) (panning = (128 - panning)) < 0)
                panning = -panning;

            abs_panning = 128 - panning;
            panning     = player_channel->pan_env.value >> 8;

            if (panning == 127)
                panning++;

            panning     = 128 - (((panning * abs_panning) >> 7) + panning_envelope_value);
            abs_panning = (uint8_t) player_host_channel->channel_panning;

            if (abs_panning == 255)
                abs_panning++;

            abs_panning           -= 128;
            panning_envelope_value = abs_panning = ((panning * abs_panning) >> 7) + 128;

            if (panning_envelope_value > 255)
                panning_envelope_value = 255;

            player_channel->final_panning = panning_envelope_value;
            panning                       = 128;

            if (!(song->flags & AVSEQ_SONG_FLAG_MONO)) {
                if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_GLOBAL_SUR_PAN)
                    player_channel->mixer.flags |= AVSEQ_MIXER_CHANNEL_FLAG_SURROUND;

                panning    -= abs_panning;
                abs_panning = (uint8_t) player_channel->global_panning;

                if (abs_panning == 255)
                    abs_panning++;

                abs_panning -= 128;
                panning      = ((panning * abs_panning) >> 7) + 128;

                if (panning == 256)
                    panning--;
            }

            player_channel->mixer.panning = panning;

            if (mixer_data->mixctx->set_channel_volume_panning_pitch)
                mixer_data->mixctx->set_channel_volume_panning_pitch(mixer_data, &player_channel->mixer, channel);
        }
not_calculate_no_playing:
        if (mixer_data->mixctx->set_channel_position_repeat_flags)
            mixer_data->mixctx->set_channel_position_repeat_flags(mixer_data, &player_channel->mixer, channel);

        player_channel++;
    } while (++channel < module->channels);

    player_globals->channels = virtual_channel;

    if (virtual_channel > player_globals->max_channels)
        player_globals->max_channels = virtual_channel;

    channel             = 0;
    player_host_channel = avctx->player_host_channel;

    do {
        if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END))
            goto check_song_end_done;

        player_host_channel++;
    } while (++channel < song->channels);

    player_globals->flags |= AVSEQ_PLAYER_GLOBALS_FLAG_SONG_END;
check_song_end_done:
    if (player_hook && !(player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_BEGINNING) &&
                       (((player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_SONG_END) &&
                       !(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_SONG_END)) ||
                       !(player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_SONG_END)))
        player_hook->hook_func(avctx, player_hook->hook_data, player_hook->hook_len);

    if (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_SONG_END) {
        player_host_channel = avctx->player_host_channel;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END) {
            AVSequencerOrderList *order_list = song->order_list;
            channel                          = song->channels;

            do {
                AVSequencerOrderData *order_data;
                uint32_t i = -1;

                if (player_host_channel->tempo)
                    player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;

                while (++i < order_list->orders) {
                    if ((order_data = order_list->order_data[i]) && (order_data != player_host_channel->order))
                        order_data->played = 0;
                }

                order_list++;
                player_host_channel++;
            } while (--channel);
        }
    }

    return 0;
}
