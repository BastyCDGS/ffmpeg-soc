/*
 * AVSequencer playback engine
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

#ifndef AVSEQUENCER_PLAYER_H
#define AVSEQUENCER_PLAYER_H

#include "libavsequencer/order.h"
#include "libavsequencer/track.h"
#include "libavsequencer/instr.h"
#include "libavsequencer/sample.h"
#include "libavsequencer/synth.h"

typedef struct AVSequencerPlayerEffects {
    /** Function pointer to the actual effect to be executed for this
       effect. Can be NULL if this effect number is unused. This
       structure is actually for one effect and there actually
       pointed as an array with size of number of total effects.  */
    void (*effect_func)( AVSequencerContext *avctx,
        AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel,
        uint16_t channel, uint16_t fx_byte, uint16_t data_word );

    /** Function pointer for pre-pattern evaluation. Some effects
       require a pre-initialization stage. Can be NULL if the effect
       number either is not used or the effect does not require a
       pre-initialization stage.  */
    void (*pre_pattern_func)( AVSequencerContext *avctx,
        AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel,
        uint16_t channel, uint16_t data_word );

    /** Function pointer for parameter checking for an effect. Can
       be NULL if the effect number either is not used or the effect
       does not require pre-checking.  */
    void (*check_fx_func)( AVSequencerContext *avctx,
        AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel,
        uint16_t channel, uint16_t *fx_byte, uint16_t *data_word, uint16_t *flags );

    /** Special flags for this effect, this currently defines if the
       effect is executed during the whole row each tick or just only
       once per row.  */
    uint8_t flags;
#define AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW    0x80 // Effect will be executed during the whole row instead of only once

    /** Logical AND filter mask for the channel control command
       filtering the affected channel.  */
    uint8_t and_mask_ctrl;

    /** Standard execution tick when this effect starts to be executed
       and there is no execute effect command issued which is in most
       case tick 0 (immediately) or 1 (skip first tick at row).  */
    uint16_t std_exec_tick;
} AVSequencerPlayerEffects;

/**
 * Playback handler hook for allowing developers to execute customized
 * code in the playback handler under certain conditions. Currently
 * the hook can either be called once at song end found or each tick,
 * as well as before execution of the playback handler or after it.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerPlayerHook {
    /** Special flags for the hook which decide hook call time and
       purpose.  */
    int8_t flags;
#define AVSEQ_PLAYER_HOOK_FLAG_SONG_END    0x01 ///< Hook is only called when song end is being detected instead of each tick
#define AVSEQ_PLAYER_HOOK_FLAG_BEGINNING   0x02 ///< Hook is called before executing playback code instead of the end

    /** The actual hook function to be called which gets passed the
       associated AVSequencerContext and the module and sub-song
       currently processed (i.e. triggered the hook).  */
    void (*hook_func)( AVSequencerContext *avctx, AVSequencerModule *module,
                       AVSequencerSong *song, void *hook_data, uint64_t hook_len );

    /** The actual hook data to be passed to the hook function which
       also gets passed the associated AVSequencerContext and the
       module and sub-song currently processed (i.e. triggered the
       hook).  */
    void *hook_data;

    /** Size of the hook data passed to the hook function which gets
       passed the associated AVSequencerContext and the module and
       sub-song currently processed (i.e. triggered the hook).  */
    uint64_t hook_len;
} AVSequencerPlayerHook;

/**
 * Player envelope structure used by playback engine for processing
 * envelope playback in the module replay engine. This is initialized
 * when a new instrument is being played from the actual instrument
 * envelope data and then processed each tick.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerPlayerEnvelope {
    /** Pointer to associated instrument envelope this envelope
       belongs to.  */
    AVSequencerEnvelope *envelope;

    /** The current data value last processed by this envelope.
       For a volume envelope, we have a default scale range of -32767
       to +32767, for panning envelopes the scale range is between
       -8191 to +8191. For slide, vibrato, tremolo, pannolo (and their
       auto versions), the scale range is between -256 to +256.  */
    int16_t value;

    /** Current envelope position in ticks (0 is first tick).  */
    uint16_t pos;

    /** Current envelope normal loop restart point.  */
    uint16_t start;

    /** Current envelope normal loop end point.  */
    uint16_t end;

    /** Current sustain loop counted tick value, i.e. how often the
       sustain loop points already have been triggered.  */
    uint16_t sustain_counted;

    /** Current normal loop counted tick value, i.e. how often the
       normal loop points already have been triggered.  */
    uint16_t loop_counted;

    /** Current envelope tempo count in ticks.  */
    uint16_t tempo_count;

    /** Current envelope tempo in ticks.  */
    uint16_t tempo;

    /** Envelope sustain loop restart point.  */
    uint16_t sustain_start;

    /** Envelope sustain loop end point.  */
    uint16_t sustain_end;

    /** Envelope sustain loop tick counter in ticks.  */
    uint16_t sustain_count;

    /** Envelope normal loop restart point.  */
    uint16_t loop_start;

    /** Envelope normal loop end point.  */
    uint16_t loop_end;

    /** Envelope normal loop tick counter in ticks.  */
    uint16_t loop_count;

    /** Randomized lowest value allowed.  */
    int16_t value_min;

    /** Randomized highest value allowed.  */
    int16_t value_max;

    /** Player envelope flags. Some sequencers allow envelopes
       to operate in different modes, e.g. different loop types,
       randomization, processing modes which have to be taken
       care specially in the internal playback engine.  */
    int8_t flags;
#define AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD    0x01 ///< First process envelope position then get value
#define AVSEQ_PLAYER_ENVELOPE_FLAG_NO_RETRIG    0x02 ///< Do not retrigger envelope on new note playback
#define AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM       0x04 ///< Envelope returns randomized instead of waveform data
#define AVSEQ_PLAYER_ENVELOPE_FLAG_RND_DELAY    0x08 ///< If randomization is enabled speed is interpreted as delay
#define AVSEQ_PLAYER_ENVELOPE_FLAG_BACKWARDS    0x10 ///< Envelope is currently being played backwards
#define AVSEQ_PLAYER_ENVELOPE_FLAG_LOOPING      0x20 ///< Envelope is looping in either sustain or normal mode
#define AVSEQ_PLAYER_ENVELOPE_FLAG_PINGPONG     0x40 ///< Envelope is doing ping pong style loop

    /** Player envelope repeat flags. Some sequencers allow envelopes
       to operate in different repeat mode like sustain with or
       without ping pong mode loops, which have to be taken care
       specially in the internal playback engine.  */
    int8_t rep_flags;
#define AVSEQ_PLAYER_ENVELOPE_REP_FLAG_LOOP             0x01 ///< Envelope uses normal loop points
#define AVSEQ_PLAYER_ENVELOPE_REP_FLAG_SUSTAIN          0x02 ///< Envelope uses sustain loop points
#define AVSEQ_PLAYER_ENVELOPE_REP_FLAG_PINGPONG         0x04 ///< Envelope normal loop is in ping pong mode
#define AVSEQ_PLAYER_ENVELOPE_REP_FLAG_SUSTAIN_PINGPONG 0x08 ///< Envelope sustain loop is in ping pong mode

} AVSequencerPlayerEnvelope;

/**
 * Player global data structure used by playback engine for processing
 * parts of module and sub-song which have global meanings like speed,
 * timing mode, speed and pitch adjustments, global volume and panning
 * settings. This structure must be initialized before starting actual
 * playback.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerPlayerGlobals {
    /** Stack pointer for the GoSub command. This stores the
       return values of the order data and track row for
       recursive calls.  */
    uint16_t *gosub_stack;

    /** Stack pointer for the pattern loop command. This stores
        the loop start and loop count for recursive loops.  */
    uint16_t *loop_stack;

    /** Player envelope flags. Some sequencers allow envelopes
       to operate in different modes, e.g. different loop types,
       randomization, processing modes which have to be taken
       care specially in the internal playback engine.  */
    int8_t flags;
#define AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE         0x01 ///< Song is stopped at song end instead of continuous playback
#define AVSEQ_PLAYER_GLOBALS_FLAG_NO_PROC_PATTERN   0x02 ///< Do not process order list, pattern and track data
#define AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_PATTERN      0x04 ///< Play a single pattern only, i.e. do not process order list
#define AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND          0x08 ///< Initial global panning is surround panning
#define AVSEQ_PLAYER_GLOBALS_FLAG_SONG_END          0x10 ///< Song end found already once (marker for one-shoot playback)
#define AVSEQ_PLAYER_GLOBALS_FLAG_SPD_TIMING        0x20 ///< Use MED compatible SPD instead of the usual BpM timing
#define AVSEQ_PLAYER_GLOBALS_FLAG_TRACE_MODE        0x40 ///< Single step mode for synth sound instruction processing (debug mode)

    /** Speed slide to target value, i.e. BpM or SPD value
       where to stop the target slide at.  */
    uint8_t speed_slide_to;

    /** Speed multiplier (nominator), a value of zero means that the
       nominator is ignored.  */
    uint8_t speed_mul;

    /** Speed divider (denominator), a value of zero means that the
       denominator is ignored. The final result of speed will always
       be rounded down.  */
    uint8_t speed_div;

    /** Relative speed where a value of 65536 (=0x10000) indicates
       100%. This will accelate only the speed and not the pitch
       of the output data.  */
    uint32_t relative_speed;

    /** Relative pitch where a value of 65536 (=0x10000) indicates
       100%. This will accelate only the pitch and not the speed
       of the output data.  */
    uint32_t relative_pitch;

    /** Current playing time of the module, in AV_TIME_BASE
       fractional seconds scaled by relative speed.  */
    uint32_t play_time;

    /** Current playing time fraction of the module, in AV_TIME_BASE
       fractional seconds scaled by relative speed.  */
    uint32_t play_time_frac;

    /** Current playing ticks of the module, in AV_TIME_BASE
       fractional seconds not scaled by relative speed, i.e.
       you can always determine the exact module position by
       using playing ticks instead of playing time.  */
    uint32_t play_tics;

    /** Current playing ticks fraction of the module, in AV_TIME_BASE
       fractional seconds not scaled by relative speed, i.e.
       you can always determine the exact module position by
       using playing ticks instead of playing time.  */
    uint32_t play_tics_frac;

    /** Current final tempo (after done all BpM / SPD calculations)
       in AV_TIME_BASE fractional seconds.  */
    uint64_t tempo;

    /** Current MED style SPD speed.  */
    uint16_t spd_speed;

    /** Current number of rows per beat.  */
    uint16_t bpm_tempo;

    /** Current beats per minute speed.  */
    uint16_t bpm_speed;

    /** Global volume slide to target value, i.e. the volume level
       where to stop the target slide at.  */
    uint8_t global_volume_slide_to;

    /** Global panning slide to target value, i.e. the panning stereo
       position where to stop the target slide at.  */
    int8_t global_pan_slide_to;

    /** Current global volume of current sub-song being played.
       All other volume related commands are scaled by this.  */
    uint8_t global_volume;

    /** Current global sub-volume of current sub-song being played.
       This is basically volume divided by 256, but the sub-volume
       doesn't account into actual mixer output.  */
    uint8_t global_sub_volume;

    /** Current global panning of current sub-song being played. All
       other panning related commands are scaled by this stereo
       separation factor.  */
    int8_t global_panning;

    /** Current global sub-panning of current sub-song being played.
       This is basically panning divided by 256, but the sub-panning
       doesn't account into actual mixer output.  */
    uint8_t global_sub_panning;

    /** Current speed slide faster value or 0 if the speed slide
       faster effect was not used yet during playback.  */
    uint16_t speed_slide_faster;

    /** Current speed slide slower value or 0 if the speed slide
       slower effect was not used yet during playback.  */
    uint16_t speed_slide_slower;

    /** Current fine speed slide faster value or 0 if the fine speed
       slide faster effect was not used yet during playback.  */
    uint16_t fine_speed_slide_fast;

    /** Current fine speed slide slower value or 0 if the fine speed
       slide slower effect was not used yet during playback.  */
    uint16_t fine_speed_slide_slow;

    /** Current speed slide to target value, i.e. BpM or SPD value
       where to stop the target slide at or 0 if the speed slide
       to effect was not used yet during playback.  */
    uint16_t speed_slide_to_slide;

    /** Current speed slide to speed, i.e. how fast the BpM or
       SPD value are to be changed or 0 if the speed slide
       to effect was not used yet during playback.  */
    uint16_t speed_slide_to_speed;

    /** Current spenolo relative slide value or zero if the spenolo
       effect was not used yet during playback.  */
    int16_t spenolo_slide;

    /** Current spenolo depth as passed by the effect or zero if the
       spenolo effect was not used yet during playback.  */
    int8_t spenolo_depth;

    /** Current spenolo rate as passed by the effect or zero if the
       spenolo effect was not used yet during playback.  */
    uint8_t spenolo_rate;

    /** Current global volume slide up volume level or 0 if the global
       volume slide up effect was not used yet during playback.  */
    uint16_t global_vol_slide_up;

    /** Current global volume slide down volume level or 0 if the
       global volume slide down effect was not used yet during
       playback.  */
    uint16_t global_vol_slide_down;

    /** Current fine global volume slide up volume level or 0 if the
       fine global volume slide up effect was not used yet during
       playback.  */
    uint16_t fine_global_vol_sl_up;

    /** Current fine global volume slide down volume level or 0 if the
       fine global volume slide down effect was not used yet during
       playback.  */
    uint16_t fine_global_vol_sl_down;

    /** Current global volume slide to target volume and sub-volume
       level combined or 0 if the global volume slide to effect was
       not used yet during playback.  */
    uint16_t global_volume_slide_to_slide;

    /** Current global volume slide to target volume or 0 if the
       global volume slide to effect was not used yet during
       playback.  */
    uint8_t global_volume_sl_to_volume;

    /** Current global volume slide to target sub-volume or 0 if
       the global volume slide to effect was not used yet during
       playback. This is basically volume divided by 256, but
       the sub-volume doesn't account into actual mixer output.  */
    uint8_t global_volume_sl_to_sub_vol;

    /** Current global tremolo relative slide value or zero if the
       global tremolo effect was not used yet during playback.  */
    int16_t tremolo_slide;

    /** Current global tremolo depth as passed by the effect or zero
       if the global tremolo effect was not used yet during
       playback.  */
    int8_t tremolo_depth;

    /** Current global tremolo rate as passed by the effect or zero if
       the global tremolo effect was not used yet during playback.  */
    uint8_t tremolo_rate;

    /** Current global panning slide left panning stereo position or 0
       if the global panning slide left effect was not used yet during
       playback.  */
    uint16_t global_pan_slide_left;

    /** Current global panning slide right panning stereo position or
       0 if the global panning slide right effect was not used yet
       during playback.  */
    uint16_t global_pan_slide_right;

    /** Current fine global panning slide left panning stereo position
       or 0 if the fine global panning slide left effect was not used
       yet during playback.  */
    uint16_t fine_global_pan_sl_left;

    /** Current fine global panning slide right panning stereo
       position or 0 if the fine global panning slide right
       effect was not used yet during playback.  */
    uint16_t fine_global_pan_sl_right;

    /** Current global panning slide to target panning and sub-panning
       stereo position combined or 0 if the global panning slide to
       effect was not used yet during playback.  */
    uint16_t global_pan_slide_to_slide;

    /** Current global panning slide to target panning or 0 if the
       global panning slide to effect was not used yet during
       playback.  */
    uint8_t global_pan_slide_to_panning;

    /** Current global panning slide to target sub-panning or 0 if
       the global panning slide to effect was not used yet during
       playback. This is basically panning divided by 256, but the
       sub-panning doesn't account into actual mixer output.  */
    uint8_t global_pan_slide_to_sub_pan;

    /** Current global pannolo (panbrello) relative slide value or
       zero if the global pannolo effect was not used yet during
       playback.  */
    int16_t pannolo_slide;

    /** Current global pannolo (panbrello) depth as passed by the
       effect or zero if the global pannolo effect was not used yet
       during playback.  */
    int8_t pannolo_depth;

    /** Current global pannolo (panbrello) rate as passed by the
       effect or zero if the global pannolo effect was not used yet
       during playback.  */
    uint8_t pannolo_rate;

    /** Number of virtual channels which are actively being played at
       once in this moment. This also includes muted channels and
       channels currently played at volume level zero.  */
    uint16_t channels;

    /** Number of virtual channels which have been played at maximum
       at once since start of playback which also includes muted
       channels and channels currently played at volume level 0.  */
    uint16_t max_channels;

    /** AVSequencerPlayerEnvelope pointer to spenolo envelope.  */
    AVSequencerPlayerEnvelope spenolo_env;

    /** AVSequencerPlayerEnvelope pointer to global tremolo
       envelope.  */
    AVSequencerPlayerEnvelope tremolo_env;

    /** AVSequencerPlayerEnvelope pointer to global pannolo
       envelope.  */
    AVSequencerPlayerEnvelope pannolo_env;

    /** Speed timing mode as set by the track effect command set speed
       (0x60) or zero if the set speed effect was not used yet during
       playback.  */ 
    uint8_t speed_type;
#define AVSEQ_PLAYER_GLOBALS_SPEED_TYPE_BPM_SPEED           0x01 ///< Change BPM speed (beats per minute)
#define AVSEQ_PLAYER_GLOBALS_SPEED_TYPE_BPM_TEMPO           0x02 ///< Change BPM tempo (rows per beat)
#define AVSEQ_PLAYER_GLOBALS_SPEED_TYPE_SPD_SPEED           0x03 ///< Change SPD (MED-style timing)
#define AVSEQ_PLAYER_GLOBALS_SPEED_TYPE_BPM_SPEED           0x07 ///< Apply nominator (bits 4-7) and denominator (bits 0-3) to speed
#define AVSEQ_PLAYER_GLOBALS_SPEED_TYPE_BPM_SPEED_NO_USE    0x08 ///< Change BPM speed (beats per minute) but do not use it for playback
#define AVSEQ_PLAYER_GLOBALS_SPEED_TYPE_BPM_TEMPO_NO_USE    0x09 ///< Change BPM tempo (rows per beat) but do not use it for playback
#define AVSEQ_PLAYER_GLOBALS_SPEED_TYPE_SPD_SPEED_NO_USE    0x0A ///< Change SPD (MED-style timing) but do not use it for playback
#define AVSEQ_PLAYER_GLOBALS_SPEED_TYPE_BPM_SPEED_NO_USE    0x0F ///< Apply nominator (bits 4-7) and denominator (bits 0-3) to speed but do not use it for playback

    /** Play type, if the song bit is set, the sub-song is currently
       playing normally from the beginning to the end. Disk writers
       can use this flag to determine if there is the current mixing
       output should be really written.  */
    uint8_t play_type;
#define AVSEQ_PLAYER_GLOBALS_PLAY_TYPE_SONG 0x80

    /** Current trace counter for debugging synth sound instructions.
       Rest of playback data will not continue being processed if
       trace count does not equal to zero.  */
    uint16_t trace_count;
} AVSequencerPlayerGlobals;

/**
 * Player host channel data structure used by playback engine for
 * processing the host channels which are the channels associated
 * to tracks and the note data is encountered upon. This contains
 * effect memories and all data required for track playback. This
 * structure is actually for one host channel and therefore actually
 * pointed as an array with size of number of host channels.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerPlayerHostChannel {
    /** Pointer to sequencer order data entry currently being
       played by this host channel.  */
    AVSequencerOrderData *order;

    /** Pointer to sequencer track data currently being
       played by this host channel.  */
    AVSequencerTrack *track;

    /** Pointer to sequencer track effect currently being
       processed by this host channel.  */
    AVSequencerTrackEffect *effect;

    /** Pointer to sequencer instrument currently being
       played by this host channel.  */
    AVSequencerInstrument *instrument;

    /** Pointer to sequencer sample currently being played
       by this host channel.  */
    AVSequencerSample *sample;

    /** Current row of track being played by this host channel.  */
    uint16_t row;

    /** Current fine pattern delay value or 0 if the fine pattern
       delay effect was not used yet during playback.  */
    uint16_t fine_pattern_delay;

    /** Current tempo counter (tick of row). The next row will be
       played when the tempo counter reaches the tempo value.  */
    uint32_t tempo_counter;

    /** Current last row of track being played before breaking to
       next track by this host channel.  */
    uint32_t max_row;

    /** Player host channel flags. This stores certain information
       about the current track and track effects being processed and
       also about the current playback mode. Trackers, for example
       can play a simple instrument or sample only or even play a
       single note on a single row if both set instrument and set
       sample bits are set (0x00300000).  */
    uint32_t flags;
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ      0x00000001 ///< Use linear frequency table instead of Amiga
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS        0x00000002 ///< Playing back track in backwards direction
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_BREAK    0x00000004 ///< Pattern break encountered
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SMP_OFFSET_REL   0x00000008 ///< Sample offset is interpreted as relative to current position
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN    0x00000010 ///< Track panning is in surround mode
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN  0x00000020 ///< Channel panning is also affected
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHANNEL_SUR_PAN  0x00000040 ///< Channel panning uses surround mode
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX          0x00000080 ///< Execute command effect at tick invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TONE_PORTA       0x00000100 ///< Tone portamento effect invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_TRANSPOSE    0x00000200 ///< Set transpose effect invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SUB_SLIDE_RETRIG 0x00000400 ///< Allow sub-slides in multi retrigger note
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_EXEC      0x00000800 ///< Tremor effect in hold, i.e. invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_OFF       0x00001000 ///< Tremor effect is currently turning off volume
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_RETRIG_NOTE      0x00002000 ///< Note retrigger effect invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_VIBRATO          0x00004000 ///< Vibrato effect in hold, i.e. invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOLO          0x00008000 ///< Tremolo effect in hold, i.e. invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHG_PATTERN      0x00010000 ///< Change pattern effect invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP     0x00020000 ///< Performing pattern loop effect
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP_JMP 0x00040000 ///< Pattern loop effect has jumped back
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_RESET    0x00080000 ///< Pattern loop effect needs to be resetted
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT   0x00100000 ///< Only playing instrument without order list and pattern processing
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE       0x00200000 ///< Only playing sample without instrument, order list and pattern processing
#define AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END         0x80000000 ///< Song end triggered for this host channel / track

    /** Player host channel fine slide flags. This stores information
       about the slide commands, i.e. which direction and invoke state
       to be handled for the playback engine, i.e. execution of the
       actual slides while remaining expected behaviour.  */
    uint32_t fine_slide_flags;
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_DOWN           0x00000001 ///< Fine portamento is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE_DOWN           0x00000002 ///< Portamento once is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_ONCE_DOWN      0x00000004 ///< Fine portamento once is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA                0x00000008 ///< Fine portamento invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE                0x00000010 ///< Portamento once invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TONE_PORTA           0x00000020 ///< Fine tone portamento invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TONE_PORTA_ONCE           0x00000040 ///< Tone portamento once invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_VOL_SLIDE_DOWN            0x00000080 ///< Volume slide is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE_DOWN       0x00000100 ///< Fine volume slide is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE            0x00000200 ///< Fine volume slide invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_VOL_SLIDE_DOWN      0x00000400 ///< Track volume slide is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE_DOWN 0x00000800 ///< Fine track volume slide is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE      0x00001000 ///< Fine track volume slide invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PAN_SLIDE_RIGHT           0x00002000 ///< Panning slide is directed towards right
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE_RIGHT      0x00004000 ///< Fine panning slide is directed towards right
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE            0x00008000 ///< Fine panning slide invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_PAN_SLIDE_RIGHT     0x00010000 ///< Track panning slide is directed towards right
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRK_PAN_SLIDE_RIGHT  0x00020000 ///< Fine track panning slide is directed towards right
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_PAN_SLIDE      0x00040000 ///< Fine track panning slide invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_SPEED_SLIDE_SLOWER        0x00080000 ///< Speed slide is directed towards slowness
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE_SLOWER   0x00100000 ///< Fine speed slide is directed towards slowness
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE          0x00200000 ///< Fine speed slide invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_VOL_SLIDE_DOWN     0x00400000 ///< Global volume slide is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_VOL_SLIDE_DOWN  0x00800000 ///< Fine global volume slide is directed downwards
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_VOL_SLIDE     0x01000000 ///< Fine global volume slide invoked
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_PAN_SLIDE_RIGHT    0x02000000 ///< Global panning slide is directed towards right
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_PAN_SLIDE_RIGHT 0x04000000 ///< Fine global panning slide is directed towards right
#define AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_PAN_SLIDE     0x08000000 ///< Fine global panning slide invoked

    /** Current tempo (number of ticks of row). The next row will be
       played when the tempo counter reaches this value.  */
    uint16_t tempo;

    /** Current final note being played (after applying all transpose
       values, etc.) by the formula: current octave * 12 + current
       note where C-0 is represented with a value zero.  */
    int16_t final_note;

    /** Current instrument note being played (after applying
       current instrument transpose) by the formula: current
       octave * 12 + current note where C-0 equals to one.  */
    uint8_t instr_note;

    /** Current sample note being played (after applying
       current sample transpose) by the formula: current
       octave * 12 + current note where C-0 equals to one.  */
    uint8_t sample_note;

    /** Current track volume being played on this host channel.  */
    uint8_t track_volume;

    /** Current track sub-volume level for this host channel. This
       is basically track volume divided by 256, but the sub-volume
       doesn't account into actual mixer output.  */
    uint8_t track_sub_vol;

    /** Current track panning stereo position being played on this
       host channel.  */
    int8_t track_panning;

    /** Current track sub-panning stereo position for this host
       channel. This is basically track panning divided by 256,
       but the sub-panning doesn't account into actual mixer
       output.  */
    uint8_t track_sub_pan;

    /** Current track note panning which indicates a panning
       change relative to a base note and octave. This allows
       choosing the stereo position based on octave * 12 + note.  */
    int8_t track_note_pan;

    /** Current track note sub-panning stereo position for this host
       channel. This is basically track note panning divided by 256,
       but the sub-panning doesn't account into actual mixer
       output.  */
    uint8_t track_note_sub_pan;

    /** Current chhannel panning stereo position being played on this
       host channel.  */
    int8_t channel_panning;

    /** Current channel sub-panning stereo position for this host
       channel. This is basically channel panning divided by 256,
       but the sub-panning doesn't account into actual mixer
       output.  */
    uint8_t channel_sub_pan;

    /** Current finetune of the sample last played on this host
       channel.  */
    int8_t finetune;

    /** Current arpeggio tick count. If tick count modulo 3 is 0, then
       use arpeggio base note. If modulo value is 1 instead, use first
       arpeggio value and second arpeggio value for a modulo value of
       2. This value is 0 if the arpeggio effect was not used yet
       during playback.  */
    uint8_t arpeggio_tick;

    /** Current arpeggio frequency relative to played sample frequency
       to be able to undo the previous arpeggio frequency changes or 0
       if the arpeggio effect was not used yet during playback.  */
    int32_t arpeggio_freq;

    /** Current arpeggio first value which will be used if modulo of
       arpeggio tick count modulo 3 is 1. This value is 0 if the
       arpeggio effect was not used yet during playback.  */
    int8_t arpeggio_first;

    /** Current arpeggio second value which will be used if modulo of
       arpeggio tick count modulo 3 is 2. This value is 0 if the
       arpeggio effect was not used yet during playback.  */
    int8_t arpeggio_second;

    /** Current high 16-bits for the sample offset high word command
       or 0 if the arpeggio effect was not used yet during playback.
       The final sample position will be set to this value * 0x10000
       adding the data word of the sample offset low word command.  */
    uint16_t smp_offset_hi;

    /** Up to 4 channel numbers to be synchronized with. This is also
       used with the channel synchronization command. If multiple
       channels have identical values, they are synchronized only
       once. However, if all four channel numbers are 0, then the
       synchronization process is done with all channels. This is
       also the case if the channel synchronization effect was not
       used yet during playback.  */
    uint8_t channel_sync[4];

    /** Current portamento up slide value or 0 if the portamento up
       effect was not used yet during playback.  */
    uint16_t porta_up;

    /** Current portamento down slide value or 0 if the portamento
       down effect was not used yet during playback.  */
    uint16_t porta_down;

    /** Current fine portamento up slide value or 0 if the fine
       portamento up effect was not used yet during playback.  */
    uint16_t fine_porta_up;

    /** Current fine portamento down slide value or 0 if the fine
       portamento down effect was not used yet during playback.  */
    uint16_t fine_porta_down;

    /** Current portamento up once slide value or 0 if the portamento
       up once effect was not used yet during playback.  */
    uint16_t porta_up_once;

    /** Current portamento down once slide value or 0 if the
       portamento down once effect was not used yet during
       playback.  */
    uint16_t porta_down_once;

    /** Current fine portamento up once slide value or 0 if the
       fine portamento up once effect was not used yet during
       playback.  */
    uint16_t fine_porta_up_once;

    /** Current fine portamento down once slide value or 0 if the
       fine portamento down once effect was not used yet during
       playback.  */
    uint16_t fine_porta_down_once;

    /** Current tone portamento slide value or 0 if the tone
       portamento effect was not used yet during playback.  */
    uint16_t tone_porta;

    /** Current fine tone portamento slide value or 0 if the fine tone
       portamento effect was not used yet during playback.  */
    uint16_t fine_tone_porta;

    /** Current tone portamento once slide value or 0 if the tone
       portamento once effect was not used yet during playback.  */
    uint16_t tone_porta_once;

    /** Current fine tone portamento once slide value or 0 if the fine
       tone portamento once effect was not used yet during
       playback.  */
    uint16_t fine_tone_porta_once;

    /** Current tone portamento target pitch or 0 if none of the
       tone portamento effects were used yet during playback.  */
    uint32_t tone_porta_target_pitch;

    /** Current sub-slide value for for all portamento effects
       or 0 if neither one of the portamento effects nor the
       extended control effect were used yet during playback.  */
    uint8_t sub_slide;

    /** Current note slide type or 0 if the note slide effect was not
       used yet during playback..  */
    uint8_t note_slide_type;

    /** Current note slide value or 0 if the note slide effect was not
       used yet during playback..  */
    uint8_t note_slide;

    /** Current glissando value or 0 if the glissando control effect
       was not used yet during playback..  */
    uint8_t glissando;

    /** Current vibrato frequency relative to played sample frequency
       to be able to undo the previous vibrato frequency changes or 0
       if the vibrato effect was not used yet during playback.  */
    int32_t vibrato_slide;

    /** Current vibrato rate value or 0 if the vibrato effect was not
       used yet during playback.  */
    uint16_t vibrato_rate;

    /** Current vibrato depth value or 0 if the vibrato effect was not
       used yet during playback.  */
    int16_t vibrato_depth;

    /** Current tick number of note delay command or 0 if the note
       delay effect was not used yet during playback.  */
    uint16_t note_delay;

    /** Current number of on ticks for tremor command. During this
       number of ticks, the tremor command will not playback the
       note at a muted level. This can be 0 if the tremor effect
       was not used yet during playback.  */
    uint8_t tremor_on_ticks;

    /** Current number of off ticks for tremor command. During this
       number of ticks, the tremor command will playback the note
       at a muted level. This can be 0 if the tremor effect was not
       used yet during playback.  */
    uint8_t tremor_off_ticks;

    /** Current number of tick for tremor command. This will allow
       the player to determine if we are currently in a tremor on
       or tremor off phase and can also be 0 if the tremor effect
       was not used yet during playback.  */
    uint8_t tremor_count;

    /** Current mask of sub-slide bits or 0 if the set target
       sub-slide to effect was not used yet during playback.  */
    uint8_t sub_slide_bits;

    /** Current retrigger tick counter or 0 if the note retrigger
       effect was not used yet during playback.  */
    uint16_t retrig_tick_count;

    /** Current multi retrigger note tick counter or 0 if the multi
       retrigger note effect was not used yet during playback.  */
    uint8_t multi_retrig_tick;

    /** Current multi retrigger note volume change or 0 if the multi
       retrigger note effect was not used yet during playback.  */
    uint8_t multi_retrig_vol_chg;

    /** Current multi retrigger note scale ranging from 1 to 4 or 0
       if the multi retrigger note effect was not used yet during
       playback.  */
    uint8_t multi_retrig_scale;

    /** Current invert loop count or 0 if the invert loop effect was
       not used yet during playback.  */
    uint8_t invert_count;

    /** Current invert loop speed or 0 if the invert loop effect was
       not used yet during playback.  */
    uint16_t invert_speed;

    /** Current tick number where the next effect could be executed or
       0 if the execute command effect at tick effect was not used yet
       during playback.  */
    uint16_t exec_fx;

    /** Current volume slide to speed or 0 if the volume slide to
       effect was not used yet during playback.  */
    uint8_t volume_slide_to;

    /** Current track volume slide to speed or 0 if the track volume
       slide to effect was not used yet during playback.  */
    uint8_t track_vol_slide_to;

    /** Current panning slide to speed or 0 if the panning slide to
       effect was not used yet during playback.  */
    int8_t panning_slide_to;

    /** Current track panning slide to speed or 0 if the track panning
       slide to effect was not used yet during playback.  */
    uint8_t track_pan_slide_to;

    /** Current volume slide up value or 0 if the volume slide up
       effect was not used yet during playback.  */
    uint16_t vol_slide_up;

    /** Current volume slide down value or 0 if the volume slide down
       effect was not used yet during playback.  */
    uint16_t vol_slide_down;

    /** Current fine volume slide up value or 0 if the fine volume
       slide up effect was not used yet during playback.  */
    uint16_t fine_vol_slide_up;

    /** Current fine volume slide down value or 0 if the fine volume
       slide down effect was not used yet during playback.  */
    uint16_t fine_vol_slide_down;

    /** Current volume slide to slide or 0 if the volume slide to
       effect was not used yet during playback.  */
    uint16_t volume_slide_to_slide;

    /** Current volume slide to volume level or 0 if the volume slide
       to effect was not used yet during playback.  */
    uint8_t volume_slide_to_volume;

    /** Current sub-volume slide to volume level or 0 if the volume
       slide to effect was not used yet during playback. This is
       basically volume divided by 256, but the sub-volume does not
       take account into actual mixer output.  */
    uint8_t volume_slide_to_sub_vol;

    /** Current tremolo volume level relative to played sample volume
       to be able to undo the previous tremolo volume changes or 0
       if the tremolo effect was not used yet during playback.  */
    int16_t tremolo_slide;

    /** Current tremolo depth value or 0 if the tremolo effect was not
       used yet during playback.  */
    int8_t tremolo_depth;

    /** Current tremolo rate value or 0 if the tremolo effect was not
       used yet during playback.  */
    uint8_t tremolo_rate;

    /** Current track volume slide up value or 0 if the track volume
       slide up effect was not used yet during playback.  */
    uint16_t track_vol_slide_up;

    /** Current track volume slide down value or 0 if the track volume
       slide down effect was not used yet during playback.  */
    uint16_t track_vol_slide_down;

    /** Current fine track volume slide up value or 0 if the fine
       track volume slide up effect was not used yet during
       playback.  */
    uint16_t fine_trk_vol_slide_up;

    /** Current fine track volume slide down value or 0 if the fine
       track volume slide down effect was not used yet during
       playback.  */
    uint16_t fine_trk_vol_slide_dn;

    /** Current track volume slide to slide or 0 if the track volume
       slide to effect was not used yet during playback.  */
    uint16_t track_vol_slide_to_slide;

    /** Current track volume slide to volume level or 0 if the track
       volume slide to effect was not used yet during playback.  */
    uint8_t track_vol_slide_to_volume;

    /** Current track sub-volume slide to track volume level or 0 if
       the track volume slide to effect was not used yet during
       playback. This is basically track volume divided by 256, but
       the track sub-volume does not take account into actual mixer
       output.  */
    uint8_t track_vol_slide_to_sub_vol;

    /** Current track tremolo volume level relative to played sample
       volume to be able to undo the previous track tremolo volume
       changes or 0 if the track tremolo effect was not used yet
       during playback.  */
    int16_t track_trem_slide;

    /** Current track tremolo depth value or 0 if the track tremolo
       effect was not used yet during playback.  */
    int8_t track_trem_depth;

    /** Current track tremolo rate value or 0 if the track tremolo
       effect was not used yet during playback.  */
    uint8_t track_trem_rate;

    /** Current panning slide left value or 0 if the panning slide
       left effect was not used yet during playback.  */
    uint16_t pan_slide_left;

    /** Current panning slide right value or 0 if the panning slide
       right effect was not used yet during playback.  */
    uint16_t pan_slide_right;

    /** Current fine panning slide left value or 0 if the fine panning
       slide left effect was not used yet during playback.  */
    uint16_t fine_pan_slide_left;

    /** Current fine panning slide right value or 0 if the fine
       panning slide right effect was not used yet during
       playback.  */
    uint16_t fine_pan_slide_right;

    /** Current panning slide to slide or 0 if the panning slide to
       effect was not used yet during playback.  */
    int16_t panning_slide_to_slide;

    /** Current panning slide to panning position or 0 if the panning
       slide to effect was not used yet during playback.  */
    int8_t panning_slide_to_panning;

    /** Current sub-panning slide to panning position or 0 if the 
       panning slide to effect was not used yet during playback. This
       is basically panning divided by 256, but the sub-panning does
       not take account into actual mixer output.  */
    uint8_t panning_slide_to_sub_pan;

    /** Current pannolo (panbrello) panning position relative to
       played sample panning to be able to undo the previous pannolo
       panning changes or 0 if the pannolo effect was not used yet
       during playback.  */
    int16_t pannolo_slide;

    /** Current pannolo (panbrello) depth value or 0 if the pannolo
       effect was not used yet during playback.  */
    int8_t pannolo_depth;

    /** Current pannolo (panbrello) rate value or 0 if the pannolo
       effect was not used yet during playback.  */
    uint8_t pannolo_rate;

    /** Current track panning slide left value or 0 if the track
       panning slide left effect was not used yet during playback.  */
    uint16_t track_pan_slide_left;

    /** Current track panning slide right value or 0 if the track
       panning slide right effect was not used yet during
       playback.  */
    uint16_t track_pan_slide_right;

    /** Current fine track panning slide left value or 0 if the fine
       track panning slide left effect was not used yet during
       playback.  */
    uint16_t fine_trk_pan_sld_left;

    /** Current fine track panning slide right value or 0 if the fine
       track panning slide right effect was not used yet during
       playback.  */
    uint16_t fine_trk_pan_sld_right;

    /** Current track panning slide to slide or 0 if the track panning
       slide to effect was not used yet during playback.  */
    int16_t track_pan_slide_to_slide;

    /** Current track panning slide to panning position or 0 if the
       track panning slide to effect was not used yet during
       playback.  */
    int8_t track_pan_slide_to_panning;

    /** Current track sub-panning slide to track panning position or 0
       if the track panning slide to effect was not used yet during
       playback. This is basically track panning divided by 256, but
       the sub-panning does not take account into actual mixer
       output.  */
    uint8_t track_pan_slide_to_sub_pan;

    /** Current track pannolo (panbrello) panning position relative to
       played track panning to be able to undo the previous track
       pannolo panning changes or 0 if the track pannolo effect was
       not used yet during playback.  */
    int16_t track_pan_slide;

    /** Current track pannolo (panbrello) depth value or 0 if the
       track pannolo effect was not used yet during playback.  */
    int8_t track_pan_depth;

    /** Current track pannolo (panbrello) rate value or 0 if the
       track pannolo effect was not used yet during playback.  */
    uint8_t track_pan_rate;

    /** Current pattern break new row mumber or 0 if the pattern break
       effect was not used yet during playback.  */
    uint16_t break_row;

    /** Current position jump new order list entry mumber or 0 if the
       position jump effect was not used yet during playback.  */
    uint16_t pos_jump;

    /** Current change pattern target track mumber or 0 if the change
       pattern effect was not used yet during playback.  */
    uint16_t chg_pattern;

    /** Current pattern delay tick count or 0 if the pattern delay
       effect was not used yet during playback.  */
    uint16_t pattern_delay_count;

    /** Current pattern delay in number of ticks or 0 if the pattern
       delay effect was not used yet during playback.  */
    uint16_t pattern_delay;

    /** Current pattern loop used stack depth, i.e. number of nested
       loops or 0 if the pattern loop effect was not used yet during
       playback.  */
    uint16_t pattern_loop_depth;

    /** Current GoSub order list entry number or 0 if the GoSub effect
       was not used yet during playback.  */
    uint16_t gosub;

    /** Current GoSub used stack depth, i.e. number of nested order
       list entry calls or 0 if the GoSub effect was not used yet
       during playback.  */
    uint16_t gosub_depth;

    /** Current foreground virtual channel number, i.e. the virtual
       channel number which was allocated by the instrument currently
       playing and is still under direct control (can be manipulated
       using effect commands) or 0 if the virtual channel is moved
       to background by the NNA (new note action) mechanism.  */
    uint16_t virtual_channel;

    /** Current total amount of virtual channels allocated by this
       host channel including both the foreground channel and all
      the background channels.  */
    uint16_t virtual_channels;

    /** Current new transpose value in semitones or 0 if the set
       transpose effect was not used yet during playback.  */
    int8_t transpose;

    /** Current new finetune value in 1/128th of a semitone or 0 if
       the set transpose effect was not used yet during playback.  */
    int8_t trans_finetune;

    /** Current kind of envelope to be changed by the envelope control
       command or 0 if the envelope control effect was not used yet
       during playback.  */
    uint8_t env_ctrl_kind;
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_VOLUME_ENV      0x00 ///< Volume envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_PANNING_ENV     0x01 ///< Panning envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_SLIDE_ENV       0x02 ///< Slide envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_VIBRATO_ENV     0x03 ///< Vibrato envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_TREMOLO_ENV     0x04 ///< Tremolo envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_PANNOLO_ENV     0x05 ///< Pannolo (panbrello) envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_CHANNOLO_ENV    0x06 ///< Channolo envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_SPENOLO_ENV     0x07 ///< Spenolo envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_AUTO_VIB_ENV    0x08 ///< Auto vibrato envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_AUTO_TREM_ENV   0x09 ///< Auto tremolo envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_AUTO_PAN_ENV    0x0A ///< Auto pannolo (panbrello) envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_TRACK_TREMO_ENV 0x0B ///< Track tremolo envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_TRACK_PANNO_ENV 0x0C ///< Track pannolo (panbrello) envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_GLOBAL_TREM_ENV 0x0D ///< Global tremolo envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_GLOBAL_PAN_ENV  0x0E ///< Global pannolo (panbrello) envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_ARPEGGIO_ENV    0x0F ///< Arpeggio definition envelope selected
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_KIND_SEL_ARPEGGIO_ENV    0x10 ///< Resonance filter envelope selected

    /** Current type of envelope to be changed by the envelope control
       command or 0 if the envelope control effect was not used yet
       during playback.  */
    uint8_t env_ctrl_change;
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_SET_WAVEFORM         0x00 ///< Set the waveform number
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RESET_ENVELOPE       0x10 ///< Reset envelope
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RETRIGGER_OFF        0x01 ///< Turn off retrigger
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RETRIGGER_ON         0x11 ///< Turn on retrigger
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RANDOM_OFF           0x02 ///< Turn off randomization
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RANDOM_ON            0x12 ///< Turn on randomization
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RANDOM_DELAY_OFF     0x22 ///< Turn off randomization delay
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RANDOM_DELAY_ON      0x32 ///< Turn on randomization delay
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_COUNT_AND_SET_OFF    0x03 ///< Turn off count and set
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_COUNT_AND_SET_ON     0x13 ///< Turn on count and set
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_POSITION_BY_TICK     0x04 ///< Set envelope position by number of ticks
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_POSITION_BY_NODE     0x14 ///< Set envelope position by node number
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_TEMPO                0x05 ///< Set envelope tempo
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RELATIVE_TEMPO       0x15 ///< Set relative envelope tempo
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_FINE_TEMPO           0x25 ///< Set fine envelope tempo (count)
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_SUSTAIN_LOOP_START   0x06 ///< Set sustain loop start point
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_SUSTAIN_LOOP_END     0x07 ///< Set sustain loop end point
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_SUSTAIN_LOOP_COUNT   0x08 ///< Set sustain loop count value
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_SUSTAIN_LOOP_COUNTED 0x09 ///< Set sustain loop counted value
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_LOOP_START           0x0A ///< Set normal loop start point
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_LOOP_START_CURRENT   0x1A ///< Set normal current loop start value
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_LOOP_END             0x0B ///< Set normal loop end point
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_LOOP_END_CURRENT     0x1B ///< Set normal current loop end point
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_LOOP_COUNT           0x0C ///< Set normal loop count value
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_LOOP_COUNTED         0x0D ///< Set normal loop counted value
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RANDOM_MIN           0x0E ///< Set randomization minimum value
#define AVSEQ_PLAYER_HOST_CHANNEL_ENV_CTRL_RANDOM_MAX           0x0F ///< Set randomization maximum value

    /** Current envelope control value or 0 if the envelope control
       effect was not used yet during playback.  */
    uint16_t env_ctrl;

    /** Current synth control number of subsequent items to be changed
       or 0 if the synth control effect was not used yet during
       playback.  */
    uint8_t synth_ctrl_count;

    /** Current synth control first item to be changed or 0 if the
       synth control effect was not used yet during playback.  */
    uint8_t synth_ctrl_change;
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_VOL_CODE_LINE          0x00 ///< Set volume handling code position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_PAN_CODE_LINE          0x01 ///< Set panning handling code position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SLD_CODE_LINE          0x02 ///< Set slide handling code position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SPC_CODE_LINE          0x03 ///< Set special handling code position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_VOL_SUSTAIN_CODE_LINE  0x04 ///< Set volume sustain release position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_PAN_SUSTAIN_CODE_LINE  0x05 ///< Set panning sustain release position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SLD_SUSTAIN_CODE_LINE  0x06 ///< Set slide sustain release position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SPC_SUSTAIN_CODE_LINE  0x07 ///< Set special sustain release position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_VOL_NNA_CODE_LINE      0x08 ///< Set volume NNA trigger position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_PAN_NNA_CODE_LINE      0x09 ///< Set panning NNA trigger position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SLD_NNA_CODE_LINE      0x0A ///< Set slide NNA trigger position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SPC_NNA_CODE_LINE      0x0B ///< Set special NNA trigger position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_VOL_DNA_CODE_LINE      0x0C ///< Set volume DNA trigger position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_PAN_DNA_CODE_LINE      0x0D ///< Set panning DNA trigger position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SLD_DNA_CODE_LINE      0x0E ///< Set slide DNA trigger position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SPC_DNA_CODE_LINE      0x0F ///< Set special DNA trigger position
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_VARIABLE_MIN           0x10 ///< Set first variable specified by the lowest 4 bits
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_VARIABLE_MAX           0x1F ///< Set last variable specified by the lowest 4 bits
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_VOL_CONDITION_VARIABLE 0x20 ///< Set volume condition variable value
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_PAN_CONDITION_VARIABLE 0x21 ///< Set panning condition variable value
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SLD_CONDITION_VARIABLE 0x22 ///< Set slide condition variable value
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SPC_CONDITION_VARIABLE 0x23 ///< Set special condition variable value
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_SAMPLE_WAVEFORM        0x24 ///< Set sample waveform
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_VIBRATO_WAVEFORM       0x25 ///< Set vibrato waveform
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_TREMOLO_WAVEFORM       0x26 ///< Set tremolo waveform
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_PANNOLO_WAVEFORM       0x27 ///< Set pannolo (panbrello) waveform
#define AVSEQ_PLAYER_HOST_CHANNEL_SYNTH_CTRL_SET_ARPEGGIO_WAVEFORM      0x28 ///< Set arpeggio waveform

    /** Current synth control value or 0 if the synth control effect
       was not used yet during playback.  */
    uint16_t synth_ctrl;

    /** Current duplicate check type (DCT) value of the foreground
       instrument currently playing back or the instrument value
       if the NNA control effect was not used yet during playback.  */
    uint8_t dct;
#define AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_NOTE_OR     0x01   ///< Check for duplicate OR instrument notes
#define AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_NOTE_OR    0x02   ///< Check for duplicate OR sample notes
#define AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_OR          0x04   ///< Check for duplicate OR instruments
#define AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_OR         0x08   ///< Check for duplicate OR samples
#define AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_NOTE_AND    0x10   ///< Check for duplicate AND instrument notes
#define AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_NOTE_AND   0x20   ///< Check for duplicate AND sample notes
#define AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_AND         0x40   ///< Check for duplicate AND instruments
#define AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_AND        0x80   ///< Check for duplicate AND samples

    /** Current duplicate note action (DNA) value of the foreground
       instrument currently playing back or the instrument value
       if the NNA control effect was not used yet during playback.  */
    uint8_t dna;
#define AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_CUT      0x00   ///< Do note cut on duplicate note
#define AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_OFF      0x01   ///< Perform keyoff on duplicate note
#define AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_FADE     0x02   ///< Fade off notes on duplicate note
#define AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_CONTINUE 0x03   ///< Nothing (only useful for synth sound handling)

    /** Current new note action (NNA) value of the foreground
       instrument currently playing back or the instrument value
       if the NNA control effect was not used yet during playback.  */
    uint8_t nna;
#define AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_CUT      0x00   ///< Cut previous note
#define AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_CONTINUE 0x01   ///< Continue previous note
#define AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_OFF      0x02   ///< Perform key-off on previous note
#define AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_FADE     0x03   ///< Perform fadeout on previous note

    /** Current channel control flags which decide how note related
       effects affect volume and panning, etc. and how non-note
       related effects affect pattern loops and breaks, etc.  */
    uint8_t ch_control_flags;
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_FLAG_NOTES     0x01   ///< Affect note related effects (volume, panning, etc.)
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_FLAG_NON_NOTES 0x02   ///< Affect non-note related effects (pattern loops and breaks, etc.)

    /** Current channel control type which decides the channels
       affected by the channel control command or 0 if the channel
       control effect was not used yet during playback.  */
    uint8_t ch_control_type;
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_OFF       0x00   ///< Channel control is turned off
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_NORMAL    0x01   ///< Normal single channel control
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_MULTIPLE  0x02   ///< Multiple channels are controlled
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_TYPE_GLOBAL    0x03   ///< All channels are controlled

    /** Current channel control mode which decides the control scope
       by the channel control command or 0 if the channel control
       effect was not used yet during playback.  */
    uint8_t ch_control_mode;
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_MODE_NORMAL    0x00   ///< Channel control is for one effect
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_MODE_TICK      0x01   ///< Channel control is for one tick
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_MODE_ROW       0x02   ///< Channel control is for one row
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_MODE_TRACK     0x03   ///< Channel control is for one row
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_MODE_SONG      0x04   ///< Channel control is for the whole sub-song

    /** Current channel control affect which decide how note related
       effects affect volume and panning, etc. and how non-note
       related effects affect pattern loops and breaks, etc.  */
    uint8_t ch_control_affect;
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_AFFECT_NOTES       0x01   ///< Affect note related effects (volume, panning, etc.)
#define AVSEQ_PLAYER_HOST_CHANNEL_CH_CONTROL_AFFECT_NON_NOTES   0x02   ///< Affect non-note related effects (pattern loops and breaks, etc.)

    /** Current channel number to be controlled for normal single
       channel control mode or 0 if the channel control effect was
       not used yet during playback.  */
    uint8_t ch_control_channel;

    /** Current effect channel left value or 0 if the slide effect
       channel left effect was not used yet during playback.  */
    uint8_t slide_fx_ch_left;

    /** Current effect channel right value or 0 if the slide effect
       channel right effect was not used yet during playback.  */
    uint8_t slide_fx_ch_right;

    /** Current fine effect channel left value or 0 if the fine slide
       effect channel left effect was not used yet during
       playback.  */
    uint8_t fine_slide_fx_ch_left;

    /** Current fine effect channel right value or 0 if the fine slide
       effect channel right effect was not used yet during
       playback.  */
    uint8_t fine_slide_fx_ch_right;

    /** Current slide effect channel to value or 0 if the slide effect
       channel to effect was not used yet during playback.  */
    uint8_t slide_fx_channel_to;

    /** Current slide effect target channel or 0 if the slide effect
       channel to effect was not used yet during playback.  */
    uint8_t slide_fx_channel_to_channel;

    /** Current channolo channel number relative to played channel to
       be able to undo the previous channolo changes or 0 if the
       channolo effect was not used yet during playback.  */
    int16_t channolo_channel;

    /** Current channolo depth value or 0 if the channolo effect was
       not used yet during playback.  */
    int8_t channolo_depth;

    /** Current channolo rate value or 0 if the channolo effect was
       not used yet during playback.  */
    uint8_t channolo_rate;

    /** Current player vibrato envelope for the current host
       channel.  */
    AVSequencerPlayerEnvelope vibrato_env;

    /** Current player tremolo envelope for the current host
       channel.  */
    AVSequencerPlayerEnvelope tremolo_env;

    /** Current player pannolo / panbrello envelope for the
       current host channel.  */
    AVSequencerPlayerEnvelope pannolo_env;

    /** Current player channolo envelope for the current host
       channel.  */
    AVSequencerPlayerEnvelope channolo_env;

    /** Current player arpeggio definition envelope for the
       current host channel.  */
    AVSequencerPlayerEnvelope arpepggio_env;

    /** Current player track tremolo envelope for the current
       host channel.  */
    AVSequencerPlayerEnvelope track_trem_env;

    /** Current player track pannolo / panbrello envelope for
       the current host channel.  */
    AVSequencerPlayerEnvelope track_pan_env;

    /** Pointer to the previous volume envelope which was played by
       this host channel or NULL if there was no previous
       envelope.  */
    AVSequencerEnvelope *prev_volume_env;

    /** Pointer to the previous panning (panbrello) envelope which
       was played by this host channel or NULL if there was no
       previous envelope.  */
    AVSequencerEnvelope *prev_panning_env;

    /** Pointer to the previous slide envelope which was played by
       this host channel or NULL if there was no previous
       envelope.  */
    AVSequencerEnvelope *prev_slide_env;

    /** Pointer to the previous envelope data interpreted as resonance
       filter control or NULL if there was no previous envelope.  */
    AVSequencerEnvelope *prev_resonance_env;

    /** Pointer to the previous auto vibrato envelope which was
       played by this host channel or NULL if there was no previous
       envelope.  */
    AVSequencerEnvelope *prev_auto_vib_env;

    /** Pointer to the previous auto tremolo envelope which was
       played by this host channel or NULL if there was no previous
       envelope.  */
    AVSequencerEnvelope *prev_auto_trem_env;

    /** Pointer to the previous auto pannolo (panbrello) envelope
       which was played by this host channel or NULL if there was
       no previous envelope.  */
    AVSequencerEnvelope *prev_auto_pan_env;

    /** Array of pointers containing attached waveforms used by this
       host channel.  */
    AVSequencerSynthWave **waveform_list;

    /** Pointer to player synth sound definition for the
       current host channel for obtaining the synth
       sound code.  */
    AVSequencerSynth *synth;

    /** Number of attached waveforms used by this host channel.  */
    uint16_t waveforms;

    /** Current entry position (line number) of volume [0], panning
       [1], slide [2] and special [3] handling code or 0 if the
       current sample does not use synth sound.  */
    uint16_t entry_pos[4];

    /** Current sustain entry position (line number) of volume [0],
       panning [1], slide [2] and special [3] handling code. This will
       position jump the code to the target line number of a key off
       note is pressed or 0 if the current sample does not use synth
       sound.  */
    uint16_t sustain_pos[4];

    /** Current entry position (line number) of volume [0], panning
       [1], slide [2] and special [3] handling code when NNA has been
       triggered. This allows a complete custom new note action to be
       defined or 0 if the current sample does not use synth
       sound.  */
    uint16_t nna_pos[4];

    /** Current entry position (line number) of volume [0], panning
       [1], slide [2] and special [3] handling code when DNA has been
       triggered. This allows a complete custom duplicate note action
       to be defined or 0 if the current sample does not use synth
       sound.  */
    uint16_t dna_pos[4];

    /** Initial contents of the 16 variable registers (v0-v15) or 0
       if the current sample does not use synth sound.  */
    uint16_t variable[16];

    /** Current status of volume [0], panning [1], slide [2] and slide
       [3] variable condition status register or 0 if the current
       sample does not use synth sound.  */
    uint16_t cond_var[4];
#define AVSEQ_PLAYER_HOST_CHANNEL_COND_VAR_CARRY       0x01 ///< Carry (C) bit for condition variable
#define AVSEQ_PLAYER_HOST_CHANNEL_COND_VAR_OVERFLOW    0x02 ///< Overflow (V) bit for condition variable
#define AVSEQ_PLAYER_HOST_CHANNEL_COND_VAR_ZERO        0x04 ///< Zero (Z) bit for condition variable
#define AVSEQ_PLAYER_HOST_CHANNEL_COND_VAR_NEGATIVE    0x08 ///< Negative (N) bit for condition variable
#define AVSEQ_PLAYER_HOST_CHANNEL_COND_VAR_EXTEND      0x10 ///< Extend (X) bit for condition variable

    /** Bit numbers for the controlled channels from 0-255 where the
       first byte determines channel numbers 0-7, the second byte 8-15
       and so on. All values are zero if the channel control effect
       was not used yet during playback.  */
    uint8_t control_channels[256/8];

    /** Bit numbers for all used effects from the beginning of song
       playback ranging from 0-127 where the first byte determines
       channel numbers 0-7, the second byte 8-15 and so on.  */
    uint8_t effects_used[128/8];
} AVSequencerPlayerHostChannel;

/**
 * Player virtual channel data structure used by playback engine for
 * processing the virtual channels which are the true internal
 * channels associated by the tracks taking the new note actions
 * (NNAs) into account so one host channel can have none to multiple
 * virtual channels. This also contains the synth sound processing
 * stuff since these operate mostly on virtual channels. This
 * structure is actually for one virtual channel and therefore
 * actually pointed as an array with size of number of virtual
 * channels.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerPlayerChannel {
    /** Mixer channel data responsible for this virtual channel.
       This will be passed to the actual mixer which calculates
       the final audio data.  */
    AVSequencerMixerChannel mixer;

    /** Pointer to player instrument definition for the current
       virtual channel for obtaining instrument stuff.  */
    AVSequencerInstrument *instrument;

    /** Pointer to player sound sample definition for the
       current virtual channel for obtaining sample data.  */
    AVSequencerSample *sample;

    /** Current output frequency in Hz of currently playing sample
       or waveform. This will be forwarded after relative pitch
       scaling to the mixer channel data.  */
    uint32_t frequency;

    /** Current sample volume of currently playing sample or
       waveform for this virtual channel.  */
    uint8_t volume;

    /** Current sample sub-volume of currently playing sample or
       waveform. This is basically volume divided by 256, but the
       sub-volume doesn't account into actual mixer output.  */
    uint8_t sub_vol;

    /** Current instrument global volume of currently playing
       instrument being played by this virtual channel.  */
    uint16_t instr_volume;

    /** Current sample panning position of currently playing sample
       or waveform for this virtual channel.  */
    int8_t panning;

    /** Current sample sub-panning of currently playing sample or
       waveform. This is basically panning divided by 256, but
       the sub-panning doesn't account into actual mixer output.  */
    uint8_t sub_pan;

    /** Current final volume level of currently playing sample or
       waveform for this virtual channel as it will be forwarded
       to the mixer channel data.  */
    uint8_t final_volume;

    /** Current final panning of currently playing sample or waveform
       for this virtual channel as it will be forwarded to the mixer
       channel data.  */
    int8_t final_panning;

    /** Current sample global volume of currently playing sample or
       waveform for this virtual channel.  */
    uint8_t global_volume;

    /** Current sample global sub-volume of currently playing sample
       or waveform. This is basically global volume divided by 256,
       but the sub-volume doesn't account into actual mixer
       output.  */
    uint8_t global_sub_vol;

    /** Current sample global panning position of currently playing
       sample or waveform for this virtual channel.  */
    int8_t global_panning;

    /** Current sample global sub-panning of currently playing sample
       or waveform. This is basically global panning divided by 256,
       but sub-panning doesn't account into actual mixer output.  */
    uint8_t global_sub_pan;

    /** Current random volume swing in 1/256th steps (i.e. 256 means
       100%). The volume will vibrate randomnessly around that volume
       percentage and make the instrument sound more like a naturally
       played one.  */
    uint16_t volume_swing;

    /** Current random panning swing in 1/256th steps (i.e. 256 means
       100%). This will cause the stereo position to vary a bit each
       instrument play to make it sound more like a naturally played
       one.  */
    uint16_t panning_swing;

    /** Current random pitch swing in 1/65536th steps, i.e. 65536
       means 100%. This will cause the stereo position to vary a bit
       each instrument play to make it sound more like a naturally
       played one.  */
    uint32_t pitch_swing;

    /** Current host channel to which this virtual channel is mapped
       to, i.e. the creator of this virtual channel.  */
    uint16_t host_channel;

    /** Player virtual channel flags. This stores certain information
       about the current virtual channel based upon the host channel
       which allocated this virtual channel. The virtual channels are
       allocated according to the new note action (NNA) mechanism.  */
    uint16_t flags;
#define AVSEQ_PLAYER_CHANNEL_FLAG_SUSTAIN               0x0001 ///< Sustain triggered, i.e. release sustain loop points
#define AVSEQ_PLAYER_CHANNEL_FLAG_FADING                0x0002 ///< Current virtual channel is fading out
#define AVSEQ_PLAYER_CHANNEL_FLAG_DECAY                 0x0004 ///< Note decay action is running
#define AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN             0x0008 ///< Virtual channel uses track panning
#define AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN           0x0010 ///< Use surround mode for sample panning
#define AVSEQ_PLAYER_CHANNEL_FLAG_GLOBAL_SUR_PAN        0x0020 ///< Use surround mode for global panning
#define AVSEQ_PLAYER_CHANNEL_FLAG_SURROUND              0x0040 ///< Use surround sound output for this virtual channel
#define AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND            0x0080 ///< Virtual channel is put into background, i.e. no more direct control (NNA)
#define AVSEQ_PLAYER_CHANNEL_FLAG_PORTA_SLIDE_ENV       0x0100 ///< Values of slide envelope will be portamento slides instead of a transpose and finetune pair
#define AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV      0x0200 ///< Use linear frequency table instead of Amiga for slide envelope in portamento mode
#define AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_FREQ_AUTO_VIB  0x0400 ///< Use linear frequency table instead of Amiga for auto vibrato
#define AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED             0x8000 ///< Mark this virtual channel for allocation without playback

    /** Current player volume envelope for the current virtual
       channel.  */
    AVSequencerPlayerEnvelope vol_env;

    /** Current player panning envelope for the current virtual
       channel.  */
    AVSequencerPlayerEnvelope pan_env;

    /** Current player slide envelope for the current virtual
       channel.  */
    AVSequencerPlayerEnvelope slide_env;

    /** Pointer to player envelope data interpreted as resonance
       filter for the current virtual channel.  */
    AVSequencerPlayerEnvelope resonance_env;

    /** Current player auto vibrato envelope for the current
       virtual channel.  */
    AVSequencerPlayerEnvelope auto_vib_env;

    /** Current player auto tremolo envelope for the current
       virtual channel.  */
    AVSequencerPlayerEnvelope auto_trem_env;

    /** Current player auto pannolo / panbrello envelope for
       the current virtual channel.  */
    AVSequencerPlayerEnvelope auto_pan_env;

    /** Current slide envelope relative to played sample frequency
       to be able to undo the previous slide envelope frequency.  */
    int32_t slide_env_freq;

    /** Current auto vibrato frequency relative to played sample
       frequency to be able to undo the previous auto vibrato
       frequency changes.  */
    int32_t auto_vibrato_freq;

    /** Current auto tremolo volume level relative to played sample
       volume to be able to undo the previous auto tremolo volume
       changes.  */
    int16_t auto_tremolo_vol;

    /** Current auto pannolo (panbrello) panning position relative to
       played sample panning to be able to undo the previous auto
       pannolo panning changes.  */
    int16_t auto_pannolo_pan;

    /** Current number of tick for auto vibrato incremented by the
       auto vibrato sweep rate.  */
    uint16_t auto_vibrato_count;

    /** Current number of tick for auto tremolo incremented by the
       auto tremolo sweep rate.  */
    uint16_t auto_tremolo_count;

    /** Current number of tick for auto pannolo (panbrello)
       incremented by the auto pannolo sweep rate.  */
    uint16_t auto_pannolo_count;

    /** Current fade out value which is subtracted each tick with to
       fade out count value until zero is reached or 0 if fade out
       is disabled for this virtual channel.  */
    uint16_t fade_out;

    /** Current fade out count value where 65535 is the initial value
       (full volume level) which is subtracted each tick with the fade
       out value until zero is reached, when the note will be turned
       off.  */
    uint16_t fade_out_count;

    /** Current pitch panning separation.  */
    int16_t pitch_pan_separation;

    /** Current pitch panning center (0 is C-0, 1 is C#1, 12 is C-1,
       13 is C#1, 24 is C-2, 36 is C-3 and so on.  */
    uint8_t pitch_pan_center;

    /** Current decay action when decay is off.  */
    uint8_t dca;

    /** Hold value.  */
    uint16_t hold;

    /** Decay value.  */
    uint16_t decay;

    /** Current auto vibrato sweep.  */
    uint16_t auto_vibrato_sweep;

    /** Current auto tremolo sweep.  */
    uint16_t auto_tremolo_sweep;

    /** Current auto pannolo (panbrello) sweep.  */
    uint16_t auto_pan_sweep;

    /** Current auto vibrato depth.  */
    uint8_t auto_vibrato_depth;

    /** Current auto vibrato rate (speed).  */
    uint8_t auto_vibrato_rate;

    /** Current auto tremolo depth.  */
    uint8_t auto_tremolo_depth;

    /** Current auto tremolo rate (speed).  */
    uint8_t auto_tremolo_rate;

    /** Current auto pannolo (panbrello) depth.  */
    uint8_t auto_pan_depth;

    /** Current auto pannolo (panbrello) rate.  */
    uint8_t auto_pan_rate;

    /** Current instrument note being played (after applying
       current instrument transpose) by the formula: current
       octave * 12 + current note where C-0 equals to one.  */
    uint8_t instr_note;

    /** Current sample note being played (after applying
       current sample transpose) by the formula: current
       octave * 12 + current note where C-0 equals to one.  */
    uint8_t sample_note;

    /** Array of pointers containing attached waveforms used by this
       virtual channel.  */
    AVSequencerSynthWave **waveform_list;

    /** Pointer to sequencer sample synth sound currently being played
       by this virtual channel for obtaining the synth sound code.  */
    AVSequencerSynth *synth;

    /** Pointer to current sample data waveform used by the synth
       sound currently being played by this virtual channel.  */
    AVSequencerPlayerSynthWave *sample_waveform;

    /** Pointer to current vibrato waveform used by the synth
       sound currently being played by this virtual channel.  */
    AVSequencerPlayerSynthWave *vibrato_waveform;

    /** Pointer to current tremolo waveform used by the synth
       sound currently being played by this virtual channel.  */
    AVSequencerPlayerSynthWave *tremolo_waveform;

    /** Pointer to current pannolo (panbrello) waveform used by the
       synth sound currently being played by this virtual channel.  */
    AVSequencerPlayerSynthWave *pannolo_waveform;

    /** Pointer to current arpeggio data waveform used by the synth
       sound currently being played by this virtual channel.  */
    AVSequencerPlayerSynthWave *arpeggio_waveform;

    /** Number of attached waveforms used by this virtual channel.  */
    uint16_t waveforms;

    /** Current entry position (line number) of volume [0], panning
       [1], slide [2] and special [3] handling code or 0 if the
       current sample does not use synth sound.  */
    uint16_t entry_pos[4];

    /** Current sustain entry position (line number) of volume [0],
       panning [1], slide [2] and special [3] handling code. This will
       position jump the code to the target line number of a key off
       note is pressed or 0 if the current sample does not use synth
       sound.  */
    uint16_t sustain_pos[4];

    /** Current entry position (line number) of volume [0], panning
       [1], slide [2] and special [3] handling code when NNA has been
       triggered. This allows a complete custom new note action to be
       defined or 0 if the current sample does not use synth
       sound.  */
    uint16_t nna_pos[4];

    /** Current entry position (line number) of volume [0], panning
       [1], slide [2] and special [3] handling code when DNA has been
       triggered. This allows a complete custom duplicate note action
       to be defined or 0 if the current sample does not use synth
       sound.  */
    uint16_t dna_pos[4];

    /** Current contents of the 16 variable registers (v0-v15).  */
    uint16_t variable[16];

    /** Current status of volume [0], panning [1], slide [2] and
       special [3] variable condition status register or 0 if the
       current sample does not use synth sound.  */
    uint16_t cond_var[4];
#define AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY    0x01 ///< Carry (C) bit for volume condition variable
#define AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW 0x02 ///< Overflow (V) bit for volume condition variable
#define AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO     0x04 ///< Zero (Z) bit for volume condition variable
#define AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE 0x08 ///< Negative (N) bit for volume condition variable
#define AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND   0x10 ///< Extend (X) bit for volume condition variable

    /** Current usage of NNA trigger entry fields. This will run
       custom synth sound code execution on a NNA trigger.  */
    uint8_t use_nna_flags;
#define AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_VOLUME_NNA  0x01 ///< Use NNA trigger entry field for volume
#define AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_PANNING_NNA 0x02 ///< Use NNA trigger entry field for panning
#define AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_SLIDE_NNA   0x04 ///< Use NNA trigger entry field for slide
#define AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_SPECIAL_NNA 0x08 ///< Use NNA trigger entry field for special
#define AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_VOLUME_DNA  0x10 ///< Use NNA trigger entry field for volume
#define AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_PANNING_DNA 0x20 ///< Use NNA trigger entry field for panning
#define AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_SLIDE_DNA   0x40 ///< Use NNA trigger entry field for slide
#define AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_SPECIAL_DNA 0x80 ///< Use NNA trigger entry field for special

    /** Current usage of sustain entry position fields. This will run
       custom synth sound code execution on a note off trigger.  */
    uint8_t use_sustain_flags;
#define AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_VOLUME          0x01 ///< Use sustain entry position field for volume
#define AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_PANNING         0x02 ///< Use sustain entry position field for panning
#define AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_SLIDE           0x04 ///< Use sustain entry position field for slide
#define AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_SPECIAL         0x08 ///< Use sustain entry position field for special
#define AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_VOLUME_KEEP     0x10 ///< Keep sustain entry position for volume
#define AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_PANNING_KEEP    0x20 ///< Keep sustain entry position for panning
#define AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_SLIDE_KEEP      0x40 ///< Keep sustain entry position for slide
#define AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_SPECIAL_KEEP    0x80 ///< Keep sustain entry position for special

    /** Current final note being played (after applying all transpose
       values, etc.) by the formula: current octave * 12 + current
       note where C-0 is represented with a value zero.  */
    int16_t final_note;

    /** Current sample finetune value in 1/128th of a semitone.  */
    int8_t finetune;

    /** Current STOP synth sound instruction forbid / permit mask or 0
       if the current sample does not use synth sound.  */
    uint8_t stop_forbid_mask;

    /** Current waveform position in samples of the VIBRATO synth
       sound instruction or 0 if the current sample does not use synth
       sound.  */
    uint16_t vibrato_pos;

    /** Current waveform position in samples of the TREMOLO synth
       sound instruction or 0 if the current sample does not use synth
       sound.  */
    uint16_t tremolo_pos;

    /** Current waveform position in samples of the PANNOLO synth
       sound instruction or 0 if the current sample does not use synth
       sound.  */
    uint16_t pannolo_pos;

    /** Current waveform position in samples of the ARPEGIO synth
       sound instruction or 0 if the current sample does not use synth
       sound.  */
    uint16_t arpeggio_pos;

    /** Current player channel synth sound flags. The indicate certain
       status flags for some synth code instructions. Currently they
       are only defined for the KILL instruction.  */
    uint16_t synth_flags;
#define AVSEQ_PLAYER_CHANNEL_SYNTH_FLAG_KILL_VOLUME    0x01 ///< Volume handling code is running KILL
#define AVSEQ_PLAYER_CHANNEL_SYNTH_FLAG_KILL_PANNING   0x02 ///< Panning handling code is running KILL
#define AVSEQ_PLAYER_CHANNEL_SYNTH_FLAG_KILL_SLIDE     0x04 ///< Slide handling code is running KILL
#define AVSEQ_PLAYER_CHANNEL_SYNTH_FLAG_KILL_SPECIAL   0x08 ///< Special handling code is running KILL

    /** Current volume [0], panning [1], slide [2] and special [3]
       KILL count in number of ticks or 0 if the current sample does
       not use synth sound.  */
    uint16_t kill_count[4];

    /** Current volume [0], panning [1], slide [2] and special [3]
       WAIT count in number of ticks or 0 if the current sample does
       not use synth sound.  */
    uint16_t wait_count[4];

    /** Current volume [0], panning [1], slide [2] and special [3]
       WAIT line number to be reached to continue execution or 0 if
       the current sample does not use synth sound.  */
    uint16_t wait_line[4];

    /** Current volume [0], panning [1], slide [2] and special [3]
       WAIT type (0 is WAITVOL, 1 is WAITPAN, 2 is WAITSLD and 3 is
       WAITSPC) which has to reach the specified target line number
       before to continue execution or 0 if the current sample does
       not use synth sound.  */
    uint8_t wait_type[4];

    /** Current PORTAUP synth sound instruction memory or 0 if the
       current sample does not use synth sound.  */
    uint16_t porta_up;

    /** Current PORTADN synth sound instruction memory or 0 if the
       current sample does not use synth sound.  */
    uint16_t porta_dn;

    /** Current PORTAUP and PORTADN synth sound instruction total
       value, i.e. all PORTAUP and PORTADN instructions added together
       or 0 if the current sample does not use synth sound.  */
    int32_t portamento;

    /** Current VIBRATO synth sound instruction frequency relative to
       played sample frequency to be able to undo the previous vibrato
       frequency changes or 0  if the current sample does not use
       synth sound.  */
    int32_t vibrato_slide;

    /** Current VIBRATO synth sound instruction rate value or 0 if the
       current sample does not use synth sound.  */
    uint16_t vibrato_rate;

    /** Current VIBRATO synth sound instruction depth value or 0 if
       the current sample does not use synth sound.  */
    int16_t vibrato_depth;

    /** Current ARPEGIO synth sound instruction frequency relative to
       played sample frequency to be able to undo the previous
       arpeggio frequency changes or 0 if the current sample does not
       use synth sound.  */
    int32_t arpeggio_slide;

    /** Current ARPEGIO synth sound instruction speed value or 0 if
       the current sample does not use synth sound.  */
    uint16_t arpeggio_speed;

    /** Current ARPEGIO synth sound instruction transpose value or 0
       if the current sample does not use synth sound.  */
    int8_t arpeggio_transpose;

    /** Current ARPEGIO synth sound instruction finetuning value in
       1/128th of a semitone or 0 if the current sample does not use
       synth sound.  */
    int8_t arpeggio_finetune;

    /** Current VOLSLUP synth sound instruction memory or 0 if the
       current sample does not use synth sound.  */
    uint16_t vol_sl_up;

    /** Current VOLSLDN synth sound instruction memory or 0 if the
       current sample does not use synth sound.  */
    uint16_t vol_sl_dn;

    /** Current TREMOLO synth sound instruction volume level relative
       to played sample volume to be able to undo the previous tremolo
       volume changes or 0 if the current sample does not use synth
       sound.  */
    int16_t tremolo_slide;

    /** Current TREMOLO synth sound instruction depth value or 0 if
       the current sample does not use synth sound.  */
    int16_t tremolo_depth;

    /** Current TREMOLO synth sound instruction rate value or 0 if the
       current sample does not use synth sound.  */
    uint16_t tremolo_rate;

    /** Current PANLEFT synth sound instruction memory or 0 if the
       current sample does not use synth sound.  */
    uint16_t pan_sl_left;

    /** Current PANRIGHT synth sound instruction memory or 0 if the
       current sample does not use synth sound.  */
    uint16_t pan_sl_right;

    /** Current PANNOLO synth sound instruction relative slide value
       or 0 if the current sample does not use synth sound.  */
    int16_t pannolo_slide;

    /** Current PANNOLO synth sound instruction depth or 0 if the
       current sample does not use synth sound.  */
    int16_t pannolo_depth;

    /** Current PANNOLO synth sound instruction rate or 0 if the
       current sample does not use synth sound.  */
    uint16_t pannolo_rate;
} AVSequencerPlayerChannel;

#endif /* AVSEQUENCER_PLAYER_H */
