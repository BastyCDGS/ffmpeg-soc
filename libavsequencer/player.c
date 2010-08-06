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
#define AVSEQ_SLIDE_CONST   (8363*1712*4)

static void process_row ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel );
static void get_effects ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel );
static uint32_t get_note ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel );
static void run_effects ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel );

static int16_t get_key_table_note ( AVSequencerContext *avctx, AVSequencerInstrument *instrument, AVSequencerPlayerHostChannel *player_host_channel, uint16_t octave, uint16_t note );
static int16_t get_key_table ( AVSequencerContext *avctx, AVSequencerInstrument *instrument, AVSequencerPlayerHostChannel *player_host_channel, uint16_t note );
static uint32_t get_tone_pitch ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, int16_t note );

static AVSequencerPlayerChannel *play_note ( AVSequencerContext *avctx, AVSequencerInstrument *instrument, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t octave, uint16_t note, uint32_t channel );
static AVSequencerPlayerChannel *play_note_got ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t note, uint32_t channel );

static void init_new_instrument ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel );
static void init_new_sample ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel );

static AVSequencerPlayerChannel *trigger_nna ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t channel, uint16_t *virtual_channel );
static uint32_t trigger_dct ( AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t dct );

static void play_key_off ( AVSequencerPlayerChannel *player_channel );

static void set_envelope ( AVSequencerPlayerChannel *player_channel, AVSequencerPlayerEnvelope *envelope, uint16_t envelope_pos );
static int16_t run_envelope ( AVSequencerContext *avctx, AVSequencerPlayerEnvelope *player_envelope, uint16_t tempo_multiplier, uint16_t value_adjustment );
static int16_t step_envelope ( AVSequencerContext *avctx, AVSequencerPlayerEnvelope *player_envelope, int16_t *envelope_data, uint16_t envelope_pos, uint16_t tempo_multiplier, uint16_t value_adjustment );

#define ASSIGN_INSTRUMENT_ENVELOPE(env_type) \
    static AVSequencerEnvelope *assign_##env_type##_envelope ( AVSequencerContext *avctx, \
                                                               AVSequencerInstrument *instrument, \
                                                               AVSequencerPlayerHostChannel *player_host_channel, \
                                                               AVSequencerPlayerChannel *player_channel, \
                                                               AVSequencerEnvelope **envelope, \
                                                               AVSequencerPlayerEnvelope **player_envelope )

ASSIGN_INSTRUMENT_ENVELOPE(volume);
ASSIGN_INSTRUMENT_ENVELOPE(panning);
ASSIGN_INSTRUMENT_ENVELOPE(slide);
ASSIGN_INSTRUMENT_ENVELOPE(vibrato);
ASSIGN_INSTRUMENT_ENVELOPE(tremolo);
ASSIGN_INSTRUMENT_ENVELOPE(pannolo);
ASSIGN_INSTRUMENT_ENVELOPE(channolo);
ASSIGN_INSTRUMENT_ENVELOPE(spenolo);
ASSIGN_INSTRUMENT_ENVELOPE(track_tremolo);
ASSIGN_INSTRUMENT_ENVELOPE(track_pannolo);
ASSIGN_INSTRUMENT_ENVELOPE(global_tremolo);
ASSIGN_INSTRUMENT_ENVELOPE(global_pannolo);
ASSIGN_INSTRUMENT_ENVELOPE(resonance);

#define ASSIGN_SAMPLE_ENVELOPE(env_type) \
    static AVSequencerEnvelope *assign_##env_type##_envelope ( AVSequencerSample *sample, \
                                                               AVSequencerPlayerChannel *player_channel, \
                                                               AVSequencerPlayerEnvelope **player_envelope )

ASSIGN_SAMPLE_ENVELOPE(auto_vibrato);
ASSIGN_SAMPLE_ENVELOPE(auto_tremolo);
ASSIGN_SAMPLE_ENVELOPE(auto_pannolo);

#define USE_ENVELOPE(env_type) \
    static AVSequencerPlayerEnvelope *use_##env_type##_envelope ( AVSequencerContext *avctx, \
                                                                  AVSequencerPlayerHostChannel *player_host_channel, \
                                                                  AVSequencerPlayerChannel *player_channel )

USE_ENVELOPE(volume);
USE_ENVELOPE(panning);
USE_ENVELOPE(slide);
USE_ENVELOPE(vibrato);
USE_ENVELOPE(tremolo);
USE_ENVELOPE(pannolo);
USE_ENVELOPE(channolo);
USE_ENVELOPE(spenolo);
USE_ENVELOPE(auto_vibrato);
USE_ENVELOPE(auto_tremolo);
USE_ENVELOPE(auto_pannolo);
USE_ENVELOPE(track_tremolo);
USE_ENVELOPE(track_pannolo);
USE_ENVELOPE(global_tremolo);
USE_ENVELOPE(global_pannolo);
USE_ENVELOPE(arpeggio);
USE_ENVELOPE(resonance);

static uint32_t linear_slide_up ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint32_t frequency, uint32_t slide_value );
static uint32_t amiga_slide_up ( AVSequencerPlayerChannel *player_channel, uint32_t frequency, uint32_t slide_value );

static uint32_t linear_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint32_t frequency, uint32_t slide_value );
static uint32_t amiga_slide_down ( AVSequencerPlayerChannel *player_channel, uint32_t frequency, uint32_t slide_value );

static void portamento_slide_up ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t data_word, uint32_t carry_add, uint32_t portamento_shift, uint16_t channel );
static void portamento_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t data_word, uint32_t carry_add, uint32_t portamento_shift, uint16_t channel );
static void portamento_up_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );
static void portamento_down_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );
static void portamento_up_once_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );
static void portamento_down_once_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );

static void do_vibrato ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel, uint16_t vibrato_rate, int16_t vibrato_depth );

static uint32_t check_old_volume ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint16_t *data_word, uint16_t channel );
static void do_volume_slide ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint16_t data_word, uint16_t channel );
static void do_volume_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint16_t data_word, uint16_t channel );
static void volume_slide_up_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );
static void volume_slide_down_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );
static void fine_volume_slide_up_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );
static void fine_volume_slide_down_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );

static uint32_t check_old_track_volume ( AVSequencerContext *avctx, uint16_t *data_word );
static void do_track_volume_slide ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );
static void do_track_volume_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );

static void do_panning_slide ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t data_word, uint16_t channel );
static void do_panning_slide_right ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t data_word, uint16_t channel );

static void do_track_panning_slide ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );
static void do_track_panning_slide_right ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word );

static uint32_t check_surround_track_panning ( AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel, uint8_t channel_ctrl_byte );

static uint16_t *get_speed_address ( AVSequencerContext *avctx, uint16_t speed_type, uint16_t *speed_min_value, uint16_t *speed_max_value );
static void speed_val_ok ( AVSequencerContext *avctx, uint16_t *speed_adr, uint16_t speed_value, uint8_t speed_type, uint16_t speed_min_value, uint16_t speed_max_value );
static void do_speed_slide ( AVSequencerContext *avctx, uint16_t data_word );
static void do_speed_slide_slower ( AVSequencerContext *avctx, uint16_t data_word );

static void do_global_volume_slide ( AVSequencerContext *avctx, AVSequencerPlayerGlobals *player_globals, uint16_t data_word );
static void do_global_volume_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerGlobals *player_globals, uint16_t data_word );

static void do_global_panning_slide ( AVSequencerPlayerGlobals *player_globals, uint16_t data_word );
static void do_global_panning_slide_right ( AVSequencerPlayerGlobals *player_globals, uint16_t data_word );

#define PRESET_EFFECT(fx_type) \
    static void preset_##fx_type ( AVSequencerContext *avctx, \
                                   AVSequencerPlayerHostChannel *player_host_channel, \
                                   AVSequencerPlayerChannel *player_channel, \
                                   uint16_t channel, \
                                   uint16_t data_word )

PRESET_EFFECT(tone_portamento);
PRESET_EFFECT(vibrato);
PRESET_EFFECT(note_delay);
PRESET_EFFECT(tremolo);
PRESET_EFFECT(set_transpose);

#define CHECK_EFFECT(fx_type) \
    static void check_##fx_type ( AVSequencerContext *avctx, \
                                  AVSequencerPlayerHostChannel *player_host_channel, \
                                  AVSequencerPlayerChannel *player_channel, \
                                  uint16_t channel, \
                                  uint16_t *fx_byte, \
                                  uint16_t *data_word, \
                                  uint16_t *flags )

CHECK_EFFECT(portamento);
CHECK_EFFECT(tone_portamento);
CHECK_EFFECT(note_slide);
CHECK_EFFECT(volume_slide);
CHECK_EFFECT(volume_slide_to);
CHECK_EFFECT(track_volume_slide);
CHECK_EFFECT(panning_slide);
CHECK_EFFECT(track_panning_slide);
CHECK_EFFECT(speed_slide);
CHECK_EFFECT(channel_control);
CHECK_EFFECT(global_volume_slide);
CHECK_EFFECT(global_panning_slide);

#define EXECUTE_EFFECT(fx_type) \
    static void fx_type ( AVSequencerContext *avctx, \
                          AVSequencerPlayerHostChannel *player_host_channel, \
                          AVSequencerPlayerChannel *player_channel, \
                          uint16_t channel, \
                          uint16_t fx_byte, \
                          uint16_t data_word )

EXECUTE_EFFECT(arpeggio);
EXECUTE_EFFECT(portamento_up);
EXECUTE_EFFECT(portamento_down);
EXECUTE_EFFECT(fine_portamento_up);
EXECUTE_EFFECT(fine_portamento_down);
EXECUTE_EFFECT(portamento_up_once);
EXECUTE_EFFECT(portamento_down_once);
EXECUTE_EFFECT(fine_portamento_up_once);
EXECUTE_EFFECT(fine_portamento_down_once);
EXECUTE_EFFECT(tone_portamento);
EXECUTE_EFFECT(fine_tone_portamento);
EXECUTE_EFFECT(tone_portamento_once);
EXECUTE_EFFECT(fine_tone_portamento_once);
EXECUTE_EFFECT(note_slide);
EXECUTE_EFFECT(vibrato);
EXECUTE_EFFECT(fine_vibrato);
EXECUTE_EFFECT(do_key_off);
EXECUTE_EFFECT(hold_delay);
EXECUTE_EFFECT(note_fade);
EXECUTE_EFFECT(note_cut);
EXECUTE_EFFECT(note_delay);
EXECUTE_EFFECT(tremor);
EXECUTE_EFFECT(note_retrigger);
EXECUTE_EFFECT(multi_retrigger_note);
EXECUTE_EFFECT(extended_ctrl);
EXECUTE_EFFECT(invert_loop);
EXECUTE_EFFECT(exec_fx);
EXECUTE_EFFECT(stop_fx);

EXECUTE_EFFECT(set_volume);
EXECUTE_EFFECT(volume_slide_up);
EXECUTE_EFFECT(volume_slide_down);
EXECUTE_EFFECT(fine_volume_slide_up);
EXECUTE_EFFECT(fine_volume_slide_down);
EXECUTE_EFFECT(volume_slide_to);
EXECUTE_EFFECT(tremolo);
EXECUTE_EFFECT(set_track_volume);
EXECUTE_EFFECT(track_volume_slide_up);
EXECUTE_EFFECT(track_volume_slide_down);
EXECUTE_EFFECT(fine_track_volume_slide_up);
EXECUTE_EFFECT(fine_track_volume_slide_down);
EXECUTE_EFFECT(track_volume_slide_to);
EXECUTE_EFFECT(track_tremolo);

EXECUTE_EFFECT(set_panning);
EXECUTE_EFFECT(panning_slide_left);
EXECUTE_EFFECT(panning_slide_right);
EXECUTE_EFFECT(fine_panning_slide_left);
EXECUTE_EFFECT(fine_panning_slide_right);
EXECUTE_EFFECT(panning_slide_to);
EXECUTE_EFFECT(pannolo);
EXECUTE_EFFECT(set_track_panning);
EXECUTE_EFFECT(track_panning_slide_left);
EXECUTE_EFFECT(track_panning_slide_right);
EXECUTE_EFFECT(fine_track_panning_slide_left);
EXECUTE_EFFECT(fine_track_panning_slide_right);
EXECUTE_EFFECT(track_panning_slide_to);
EXECUTE_EFFECT(track_pannolo);

EXECUTE_EFFECT(set_tempo);
EXECUTE_EFFECT(set_relative_tempo);
EXECUTE_EFFECT(pattern_break);
EXECUTE_EFFECT(position_jump);
EXECUTE_EFFECT(relative_position_jump);
EXECUTE_EFFECT(change_pattern);
EXECUTE_EFFECT(reverse_pattern_play);
EXECUTE_EFFECT(pattern_delay);
EXECUTE_EFFECT(fine_pattern_delay);
EXECUTE_EFFECT(pattern_loop);
EXECUTE_EFFECT(gosub);
EXECUTE_EFFECT(gosub_return);
EXECUTE_EFFECT(channel_sync);
EXECUTE_EFFECT(set_sub_slides);

EXECUTE_EFFECT(sample_offset_high);
EXECUTE_EFFECT(sample_offset_low);
EXECUTE_EFFECT(set_hold);
EXECUTE_EFFECT(set_decay);
EXECUTE_EFFECT(set_transpose);
EXECUTE_EFFECT(instrument_ctrl);
EXECUTE_EFFECT(instrument_change);
EXECUTE_EFFECT(synth_ctrl);
EXECUTE_EFFECT(set_synth_value);
EXECUTE_EFFECT(envelope_ctrl);
EXECUTE_EFFECT(set_envelope_value);
EXECUTE_EFFECT(nna_ctrl);
EXECUTE_EFFECT(loop_ctrl);

EXECUTE_EFFECT(set_speed);
EXECUTE_EFFECT(speed_slide_faster);
EXECUTE_EFFECT(speed_slide_slower);
EXECUTE_EFFECT(fine_speed_slide_faster);
EXECUTE_EFFECT(fine_speed_slide_slower);
EXECUTE_EFFECT(speed_slide_to);
EXECUTE_EFFECT(spenolo);
EXECUTE_EFFECT(channel_ctrl);
EXECUTE_EFFECT(set_global_volume);
EXECUTE_EFFECT(global_volume_slide_up);
EXECUTE_EFFECT(global_volume_slide_down);
EXECUTE_EFFECT(fine_global_volume_slide_up);
EXECUTE_EFFECT(fine_global_volume_slide_down);
EXECUTE_EFFECT(global_volume_slide_to);
EXECUTE_EFFECT(global_tremolo);
EXECUTE_EFFECT(set_global_panning);
EXECUTE_EFFECT(global_panning_slide_left);
EXECUTE_EFFECT(global_panning_slide_right);
EXECUTE_EFFECT(fine_global_panning_slide_left);
EXECUTE_EFFECT(fine_global_panning_slide_right);
EXECUTE_EFFECT(global_panning_slide_to);
EXECUTE_EFFECT(global_pannolo);

EXECUTE_EFFECT(user_sync);

static uint32_t execute_synth ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t channel, uint32_t synth_type );

static void se_vibrato_do ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, int32_t vibrato_slide_value );
static void se_arpegio_do ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, int16_t arpeggio_transpose, uint16_t arpeggio_finetune );
static void se_tremolo_do ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, int32_t tremolo_slide_value );
static void se_pannolo_do ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, int32_t pannolo_slide_value );

#define EXECUTE_SYNTH_CODE_INSTRUCTION(fx_type) \
    static uint16_t se_##fx_type ( AVSequencerContext *avctx, \
                                   AVSequencerPlayerChannel *player_channel, \
                                   uint16_t virtual_channel, \
                                   uint16_t synth_code_line, \
                                   uint32_t src_var, \
                                   uint32_t dst_var, \
                                   uint16_t instruction_data, \
                                   uint32_t synth_type )

EXECUTE_SYNTH_CODE_INSTRUCTION(stop);
EXECUTE_SYNTH_CODE_INSTRUCTION(kill);
EXECUTE_SYNTH_CODE_INSTRUCTION(wait);
EXECUTE_SYNTH_CODE_INSTRUCTION(waitvol);
EXECUTE_SYNTH_CODE_INSTRUCTION(waitpan);
EXECUTE_SYNTH_CODE_INSTRUCTION(waitsld);
EXECUTE_SYNTH_CODE_INSTRUCTION(waitspc);
EXECUTE_SYNTH_CODE_INSTRUCTION(jump);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpeq);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpne);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumppl);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpmi);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumplt);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumple);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpgt);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpge);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvs);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvc);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpcs);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpcc);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpls);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumphi);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvol);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumppan);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpsld);
EXECUTE_SYNTH_CODE_INSTRUCTION(jumpspc);
EXECUTE_SYNTH_CODE_INSTRUCTION(call);
EXECUTE_SYNTH_CODE_INSTRUCTION(ret);
EXECUTE_SYNTH_CODE_INSTRUCTION(posvar);
EXECUTE_SYNTH_CODE_INSTRUCTION(load);
EXECUTE_SYNTH_CODE_INSTRUCTION(add);
EXECUTE_SYNTH_CODE_INSTRUCTION(addx);
EXECUTE_SYNTH_CODE_INSTRUCTION(sub);
EXECUTE_SYNTH_CODE_INSTRUCTION(subx);
EXECUTE_SYNTH_CODE_INSTRUCTION(cmp);
EXECUTE_SYNTH_CODE_INSTRUCTION(mulu);
EXECUTE_SYNTH_CODE_INSTRUCTION(muls);
EXECUTE_SYNTH_CODE_INSTRUCTION(dmulu);
EXECUTE_SYNTH_CODE_INSTRUCTION(dmuls);
EXECUTE_SYNTH_CODE_INSTRUCTION(divu);
EXECUTE_SYNTH_CODE_INSTRUCTION(divs);
EXECUTE_SYNTH_CODE_INSTRUCTION(modu);
EXECUTE_SYNTH_CODE_INSTRUCTION(mods);
EXECUTE_SYNTH_CODE_INSTRUCTION(ddivu);
EXECUTE_SYNTH_CODE_INSTRUCTION(ddivs);
EXECUTE_SYNTH_CODE_INSTRUCTION(ashl);
EXECUTE_SYNTH_CODE_INSTRUCTION(ashr);
EXECUTE_SYNTH_CODE_INSTRUCTION(lshl);
EXECUTE_SYNTH_CODE_INSTRUCTION(lshr);
EXECUTE_SYNTH_CODE_INSTRUCTION(rol);
EXECUTE_SYNTH_CODE_INSTRUCTION(ror);
EXECUTE_SYNTH_CODE_INSTRUCTION(rolx);
EXECUTE_SYNTH_CODE_INSTRUCTION(rorx);
EXECUTE_SYNTH_CODE_INSTRUCTION(or);
EXECUTE_SYNTH_CODE_INSTRUCTION(and);
EXECUTE_SYNTH_CODE_INSTRUCTION(xor);
EXECUTE_SYNTH_CODE_INSTRUCTION(not);
EXECUTE_SYNTH_CODE_INSTRUCTION(neg);
EXECUTE_SYNTH_CODE_INSTRUCTION(negx);
EXECUTE_SYNTH_CODE_INSTRUCTION(extb);
EXECUTE_SYNTH_CODE_INSTRUCTION(ext);
EXECUTE_SYNTH_CODE_INSTRUCTION(xchg);
EXECUTE_SYNTH_CODE_INSTRUCTION(swap);
EXECUTE_SYNTH_CODE_INSTRUCTION(getwave);
EXECUTE_SYNTH_CODE_INSTRUCTION(getwlen);
EXECUTE_SYNTH_CODE_INSTRUCTION(getwpos);
EXECUTE_SYNTH_CODE_INSTRUCTION(getchan);
EXECUTE_SYNTH_CODE_INSTRUCTION(getnote);
EXECUTE_SYNTH_CODE_INSTRUCTION(getrans);
EXECUTE_SYNTH_CODE_INSTRUCTION(getptch);
EXECUTE_SYNTH_CODE_INSTRUCTION(getper);
EXECUTE_SYNTH_CODE_INSTRUCTION(getfx);
EXECUTE_SYNTH_CODE_INSTRUCTION(getarpw);
EXECUTE_SYNTH_CODE_INSTRUCTION(getarpv);
EXECUTE_SYNTH_CODE_INSTRUCTION(getarpl);
EXECUTE_SYNTH_CODE_INSTRUCTION(getarpp);
EXECUTE_SYNTH_CODE_INSTRUCTION(getvibw);
EXECUTE_SYNTH_CODE_INSTRUCTION(getvibv);
EXECUTE_SYNTH_CODE_INSTRUCTION(getvibl);
EXECUTE_SYNTH_CODE_INSTRUCTION(getvibp);
EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmw);
EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmv);
EXECUTE_SYNTH_CODE_INSTRUCTION(gettrml);
EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmp);
EXECUTE_SYNTH_CODE_INSTRUCTION(getpanw);
EXECUTE_SYNTH_CODE_INSTRUCTION(getpanv);
EXECUTE_SYNTH_CODE_INSTRUCTION(getpanl);
EXECUTE_SYNTH_CODE_INSTRUCTION(getpanp);
EXECUTE_SYNTH_CODE_INSTRUCTION(getrnd);
EXECUTE_SYNTH_CODE_INSTRUCTION(getsine);
EXECUTE_SYNTH_CODE_INSTRUCTION(portaup);
EXECUTE_SYNTH_CODE_INSTRUCTION(portadn);
EXECUTE_SYNTH_CODE_INSTRUCTION(vibspd);
EXECUTE_SYNTH_CODE_INSTRUCTION(vibdpth);
EXECUTE_SYNTH_CODE_INSTRUCTION(vibwave);
EXECUTE_SYNTH_CODE_INSTRUCTION(vibwavp);
EXECUTE_SYNTH_CODE_INSTRUCTION(vibrato);
EXECUTE_SYNTH_CODE_INSTRUCTION(vibval);
EXECUTE_SYNTH_CODE_INSTRUCTION(arpspd);
EXECUTE_SYNTH_CODE_INSTRUCTION(arpwave);
EXECUTE_SYNTH_CODE_INSTRUCTION(arpwavp);
EXECUTE_SYNTH_CODE_INSTRUCTION(arpegio);
EXECUTE_SYNTH_CODE_INSTRUCTION(arpval);
EXECUTE_SYNTH_CODE_INSTRUCTION(setwave);
EXECUTE_SYNTH_CODE_INSTRUCTION(isetwav);
EXECUTE_SYNTH_CODE_INSTRUCTION(setwavp);
EXECUTE_SYNTH_CODE_INSTRUCTION(setrans);
EXECUTE_SYNTH_CODE_INSTRUCTION(setnote);
EXECUTE_SYNTH_CODE_INSTRUCTION(setptch);
EXECUTE_SYNTH_CODE_INSTRUCTION(setper);
EXECUTE_SYNTH_CODE_INSTRUCTION(reset);
EXECUTE_SYNTH_CODE_INSTRUCTION(volslup);
EXECUTE_SYNTH_CODE_INSTRUCTION(volsldn);
EXECUTE_SYNTH_CODE_INSTRUCTION(trmspd);
EXECUTE_SYNTH_CODE_INSTRUCTION(trmdpth);
EXECUTE_SYNTH_CODE_INSTRUCTION(trmwave);
EXECUTE_SYNTH_CODE_INSTRUCTION(trmwavp);
EXECUTE_SYNTH_CODE_INSTRUCTION(tremolo);
EXECUTE_SYNTH_CODE_INSTRUCTION(trmval);
EXECUTE_SYNTH_CODE_INSTRUCTION(panleft);
EXECUTE_SYNTH_CODE_INSTRUCTION(panrght);
EXECUTE_SYNTH_CODE_INSTRUCTION(panspd);
EXECUTE_SYNTH_CODE_INSTRUCTION(pandpth);
EXECUTE_SYNTH_CODE_INSTRUCTION(panwave);
EXECUTE_SYNTH_CODE_INSTRUCTION(panwavp);
EXECUTE_SYNTH_CODE_INSTRUCTION(pannolo);
EXECUTE_SYNTH_CODE_INSTRUCTION(panval);
EXECUTE_SYNTH_CODE_INSTRUCTION(nop);

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

/** Note frequency lookup table. Value is 65536*2^(x/12).  */
static const uint32_t pitch_lut[] = {
    0x0000F1A2, // B-3
    0x00010000, // C-4
    0x00010F39, // C#4
    0x00011F5A, // D-4
    0x00013070, // D#4
    0x0001428A, // E-4
    0x000155B8, // F-4
    0x00016A0A, // F#4
    0x00017F91, // G-4
    0x00019660, // G#4
    0x0001AE8A, // A-4
    0x0001C824, // A#4
    0x0001E343, // B-4
    0x00020000  // C-5
};

/** Old SoundTracker tempo definition table.  */
static const uint32_t old_st_lut[] = {
    192345259,  96192529,  64123930,  48096264,  38475419,
     32061964,  27482767,  24048132,  21687744,  19240098
};

/** Sine table for very fast sine calculation. Value is
   sin(x)*32767 with one element being one degree.  */
static const int16_t sine_lut[] = {
         0,    571,   1143,   1714,   2285,   2855,   3425,   3993,   4560,   5125,   5689,   6252,  6812,    7370,   7927,  8480,
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

/** Linear frequency table. Value is 65536*2^(x/3072).  */
static const uint16_t linear_frequency_lut[] = {
        0,    15,    30,    44,    59,    74,    89,   104,   118,   133,   148,   163,   178,   193,   207,   222,
      237,   252,   267,   282,   296,   311,   326,   341,   356,   371,   386,   400,   415,   430,   445,   460,
      475,   490,   505,   520,   535,   549,   564,   579,   594,   609,   624,   639,   654,   669,   684,   699,
      714,   729,   744,   758,   773,   788,   803,   818,   833,   848,   863,   878,   893,   908,   923,   938,
      953,   968,   983,   998,  1013,  1028,  1043,  1058,  1073,  1088,  1103,  1118,  1134,  1149,  1164,  1179,
     1194,  1209,  1224,  1239,  1254,  1269,  1284,  1299,  1314,  1329,  1344,  1360,  1375,  1390,  1405,  1420,
     1435,  1450,  1465,  1480,  1496,  1511,  1526,  1541,  1556,  1571,  1586,  1601,  1617,  1632,  1647,  1662,
     1677,  1692,  1708,  1723,  1738,  1753,  1768,  1784,  1799,  1814,  1829,  1844,  1859,  1875,  1890,  1905,
     1920,  1936,  1951,  1966,  1981,  1996,  2012,  2027,  2042,  2057,  2073,  2088,  2103,  2119,  2134,  2149,
     2164,  2180,  2195,  2210,  2225,  2241,  2256,  2271,  2287,  2302,  2317,  2333,  2348,  2363,  2379,  2394,
     2409,  2425,  2440,  2455,  2471,  2486,  2501,  2517,  2532,  2547,  2563,  2578,  2593,  2609,  2624,  2640,
     2655,  2670,  2686,  2701,  2716,  2732,  2747,  2763,  2778,  2794,  2809,  2824,  2840,  2855,  2871,  2886,
     2902,  2917,  2932,  2948,  2963,  2979,  2994,  3010,  3025,  3041,  3056,  3072,  3087,  3103,  3118,  3134,
     3149,  3165,  3180,  3196,  3211,  3227,  3242,  3258,  3273,  3289,  3304,  3320,  3335,  3351,  3366,  3382,
     3397,  3413,  3429,  3444,  3460,  3475,  3491,  3506,  3522,  3538,  3553,  3569,  3584,  3600,  3616,  3631,
     3647,  3662,  3678,  3694,  3709,  3725,  3740,  3756,  3772,  3787,  3803,  3819,  3834,  3850,  3866,  3881,
     3897,  3913,  3928,  3944,  3960,  3975,  3991,  4007,  4022,  4038,  4054,  4070,  4085,  4101,  4117,  4132,
     4148,  4164,  4180,  4195,  4211,  4227,  4242,  4258,  4274,  4290,  4305,  4321,  4337,  4353,  4369,  4384,
     4400,  4416,  4432,  4447,  4463,  4479,  4495,  4511,  4526,  4542,  4558,  4574,  4590,  4606,  4621,  4637,
     4653,  4669,  4685,  4701,  4716,  4732,  4748,  4764,  4780,  4796,  4812,  4827,  4843,  4859,  4875,  4891,
     4907,  4923,  4939,  4955,  4971,  4986,  5002,  5018,  5034,  5050,  5066,  5082,  5098,  5114,  5130,  5146,
     5162,  5178,  5194,  5210,  5226,  5241,  5257,  5273,  5289,  5305,  5321,  5337,  5353,  5369,  5385,  5401,
     5417,  5433,  5449,  5465,  5481,  5497,  5513,  5530,  5546,  5562,  5578,  5594,  5610,  5626,  5642,  5658,
     5674,  5690,  5706,  5722,  5738,  5754,  5770,  5787,  5803,  5819,  5835,  5851,  5867,  5883,  5899,  5915,
     5932,  5948,  5964,  5980,  5996,  6012,  6028,  6044,  6061,  6077,  6093,  6109,  6125,  6141,  6158,  6174,
     6190,  6206,  6222,  6239,  6255,  6271,  6287,  6303,  6320,  6336,  6352,  6368,  6384,  6401,  6417,  6433,
     6449,  6466,  6482,  6498,  6514,  6531,  6547,  6563,  6579,  6596,  6612,  6628,  6645,  6661,  6677,  6693,
     6710,  6726,  6742,  6759,  6775,  6791,  6808,  6824,  6840,  6857,  6873,  6889,  6906,  6922,  6938,  6955,
     6971,  6987,  7004,  7020,  7037,  7053,  7069,  7086,  7102,  7118,  7135,  7151,  7168,  7184,  7200,  7217,
     7233,  7250,  7266,  7283,  7299,  7315,  7332,  7348,  7365,  7381,  7398,  7414,  7431,  7447,  7463,  7480,
     7496,  7513,  7529,  7546,  7562,  7579,  7595,  7612,  7628,  7645,  7661,  7678,  7694,  7711,  7728,  7744,
     7761,  7777,  7794,  7810,  7827,  7843,  7860,  7876,  7893,  7910,  7926,  7943,  7959,  7976,  7992,  8009,
     8026,  8042,  8059,  8075,  8092,  8109,  8125,  8142,  8159,  8175,  8192,  8208,  8225,  8242,  8258,  8275,
     8292,  8308,  8325,  8342,  8358,  8375,  8392,  8408,  8425,  8442,  8458,  8475,  8492,  8509,  8525,  8542,
     8559,  8575,  8592,  8609,  8626,  8642,  8659,  8676,  8693,  8709,  8726,  8743,  8760,  8776,  8793,  8810,
     8827,  8843,  8860,  8877,  8894,  8911,  8927,  8944,  8961,  8978,  8995,  9012,  9028,  9045,  9062,  9079,
     9096,  9112,  9129,  9146,  9163,  9180,  9197,  9214,  9230,  9247,  9264,  9281,  9298,  9315,  9332,  9349,
     9366,  9382,  9399,  9416,  9433,  9450,  9467,  9484,  9501,  9518,  9535,  9552,  9569,  9586,  9603,  9620,
     9636,  9653,  9670,  9687,  9704,  9721,  9738,  9755,  9772,  9789,  9806,  9823,  9840,  9857,  9874,  9891,
     9908,  9925,  9942,  9959,  9976,  9993, 10011, 10028, 10045, 10062, 10079, 10096, 10113, 10130, 10147, 10164,
    10181, 10198, 10215, 10232, 10250, 10267, 10284, 10301, 10318, 10335, 10352, 10369, 10386, 10404, 10421, 10438,
    10455, 10472, 10489, 10506, 10524, 10541, 10558, 10575, 10592, 10610, 10627, 10644, 10661, 10678, 10695, 10713,
    10730, 10747, 10764, 10782, 10799, 10816, 10833, 10850, 10868, 10885, 10902, 10919, 10937, 10954, 10971, 10988,
    11006, 11023, 11040, 11058, 11075, 11092, 11109, 11127, 11144, 11161, 11179, 11196, 11213, 11231, 11248, 11265,
    11283, 11300, 11317, 11335, 11352, 11369, 11387, 11404, 11421, 11439, 11456, 11473, 11491, 11508, 11526, 11543,
    11560, 11578, 11595, 11613, 11630, 11647, 11665, 11682, 11700, 11717, 11735, 11752, 11769, 11787, 11804, 11822,
    11839, 11857, 11874, 11892, 11909, 11927, 11944, 11961, 11979, 11996, 12014, 12031, 12049, 12066, 12084, 12102,
    12119, 12137, 12154, 12172, 12189, 12207, 12224, 12242, 12259, 12277, 12294, 12312, 12330, 12347, 12365, 12382,
    12400, 12417, 12435, 12453, 12470, 12488, 12505, 12523, 12541, 12558, 12576, 12594, 12611, 12629, 12646, 12664,
    12682, 12699, 12717, 12735, 12752, 12770, 12788, 12805, 12823, 12841, 12858, 12876, 12894, 12912, 12929, 12947,
    12965, 12982, 13000, 13018, 13036, 13053, 13071, 13089, 13106, 13124, 13142, 13160, 13177, 13195, 13213, 13231,
    13249, 13266, 13284, 13302, 13320, 13337, 13355, 13373, 13391, 13409, 13427, 13444, 13462, 13480, 13498, 13516,
    13533, 13551, 13569, 13587, 13605, 13623, 13641, 13658, 13676, 13694, 13712, 13730, 13748, 13766, 13784, 13802,
    13819, 13837, 13855, 13873, 13891, 13909, 13927, 13945, 13963, 13981, 13999, 14017, 14035, 14053, 14071, 14088,
    14106, 14124, 14142, 14160, 14178, 14196, 14214, 14232, 14250, 14268, 14286, 14304, 14322, 14340, 14358, 14376,
    14394, 14413, 14431, 14449, 14467, 14485, 14503, 14521, 14539, 14557, 14575, 14593, 14611, 14629, 14647, 14665,
    14684, 14702, 14720, 14738, 14756, 14774, 14792, 14810, 14829, 14847, 14865, 14883, 14901, 14919, 14937, 14956,
    14974, 14992, 15010, 15028, 15046, 15065, 15083, 15101, 15119, 15137, 15156, 15174, 15192, 15210, 15228, 15247,
    15265, 15283, 15301, 15320, 15338, 15356, 15374, 15393, 15411, 15429, 15447, 15466, 15484, 15502, 15521, 15539,
    15557, 15575, 15594, 15612, 15630, 15649, 15667, 15685, 15704, 15722, 15740, 15759, 15777, 15795, 15814, 15832,
    15850, 15869, 15887, 15906, 15924, 15942, 15961, 15979, 15997, 16016, 16034, 16053, 16071, 16089, 16108, 16126,
    16145, 16163, 16182, 16200, 16218, 16237, 16255, 16274, 16292, 16311, 16329, 16348, 16366, 16385, 16403, 16422,
    16440, 16459, 16477, 16496, 16514, 16533, 16551, 16570, 16588, 16607, 16625, 16644, 16662, 16681, 16700, 16718,
    16737, 16755, 16774, 16792, 16811, 16830, 16848, 16867, 16885, 16904, 16922, 16941, 16960, 16978, 16997, 17016,
    17034, 17053, 17071, 17090, 17109, 17127, 17146, 17165, 17183, 17202, 17221, 17239, 17258, 17277, 17295, 17314,
    17333, 17352, 17370, 17389, 17408, 17426, 17445, 17464, 17483, 17501, 17520, 17539, 17557, 17576, 17595, 17614,
    17633, 17651, 17670, 17689, 17708, 17726, 17745, 17764, 17783, 17802, 17820, 17839, 17858, 17877, 17896, 17914,
    17933, 17952, 17971, 17990, 18009, 18028, 18046, 18065, 18084, 18103, 18122, 18141, 18160, 18179, 18197, 18216,
    18235, 18254, 18273, 18292, 18311, 18330, 18349, 18368, 18387, 18405, 18424, 18443, 18462, 18481, 18500, 18519,
    18538, 18557, 18576, 18595, 18614, 18633, 18652, 18671, 18690, 18709, 18728, 18747, 18766, 18785, 18804, 18823,
    18842, 18861, 18880, 18899, 18918, 18937, 18957, 18976, 18995, 19014, 19033, 19052, 19071, 19090, 19109, 19128,
    19147, 19167, 19186, 19205, 19224, 19243, 19262, 19281, 19300, 19320, 19339, 19358, 19377, 19396, 19415, 19435,
    19454, 19473, 19492, 19511, 19530, 19550, 19569, 19588, 19607, 19626, 19646, 19665, 19684, 19703, 19723, 19742,
    19761, 19780, 19800, 19819, 19838, 19857, 19877, 19896, 19915, 19934, 19954, 19973, 19992, 20012, 20031, 20050,
    20070, 20089, 20108, 20128, 20147, 20166, 20186, 20205, 20224, 20244, 20263, 20282, 20302, 20321, 20340, 20360,
    20379, 20399, 20418, 20437, 20457, 20476, 20496, 20515, 20534, 20554, 20573, 20593, 20612, 20632, 20651, 20670,
    20690, 20709, 20729, 20748, 20768, 20787, 20807, 20826, 20846, 20865, 20885, 20904, 20924, 20943, 20963, 20982,
    21002, 21021, 21041, 21060, 21080, 21099, 21119, 21139, 21158, 21178, 21197, 21217, 21236, 21256, 21276, 21295,
    21315, 21334, 21354, 21374, 21393, 21413, 21432, 21452, 21472, 21491, 21511, 21531, 21550, 21570, 21589, 21609,
    21629, 21648, 21668, 21688, 21708, 21727, 21747, 21767, 21786, 21806, 21826, 21845, 21865, 21885, 21905, 21924,
    21944, 21964, 21984, 22003, 22023, 22043, 22063, 22082, 22102, 22122, 22142, 22161, 22181, 22201, 22221, 22241,
    22260, 22280, 22300, 22320, 22340, 22360, 22379, 22399, 22419, 22439, 22459, 22479, 22498, 22518, 22538, 22558,
    22578, 22598, 22618, 22638, 22658, 22677, 22697, 22717, 22737, 22757, 22777, 22797, 22817, 22837, 22857, 22877,
    22897, 22917, 22937, 22957, 22977, 22996, 23016, 23036, 23056, 23076, 23096, 23116, 23136, 23156, 23176, 23196,
    23216, 23237, 23257, 23277, 23297, 23317, 23337, 23357, 23377, 23397, 23417, 23437, 23457, 23477, 23497, 23517,
    23537, 23558, 23578, 23598, 23618, 23638, 23658, 23678, 23698, 23719, 23739, 23759, 23779, 23799, 23819, 23839,
    23860, 23880, 23900, 23920, 23940, 23961, 23981, 24001, 24021, 24041, 24062, 24082, 24102, 24122, 24142, 24163,
    24183, 24203, 24223, 24244, 24264, 24284, 24304, 24325, 24345, 24365, 24386, 24406, 24426, 24446, 24467, 24487,
    24507, 24528, 24548, 24568, 24589, 24609, 24629, 24650, 24670, 24690, 24711, 24731, 24752, 24772, 24792, 24813,
    24833, 24853, 24874, 24894, 24915, 24935, 24956, 24976, 24996, 25017, 25037, 25058, 25078, 25099, 25119, 25139,
    25160, 25180, 25201, 25221, 25242, 25262, 25283, 25303, 25324, 25344, 25365, 25385, 25406, 25426, 25447, 25467,
    25488, 25508, 25529, 25550, 25570, 25591, 25611, 25632, 25652, 25673, 25694, 25714, 25735, 25755, 25776, 25797,
    25817, 25838, 25858, 25879, 25900, 25920, 25941, 25962, 25982, 26003, 26023, 26044, 26065, 26085, 26106, 26127,
    26148, 26168, 26189, 26210, 26230, 26251, 26272, 26292, 26313, 26334, 26355, 26375, 26396, 26417, 26438, 26458,
    26479, 26500, 26521, 26541, 26562, 26583, 26604, 26625, 26645, 26666, 26687, 26708, 26729, 26749, 26770, 26791,
    26812, 26833, 26854, 26874, 26895, 26916, 26937, 26958, 26979, 27000, 27021, 27041, 27062, 27083, 27104, 27125,
    27146, 27167, 27188, 27209, 27230, 27251, 27271, 27292, 27313, 27334, 27355, 27376, 27397, 27418, 27439, 27460,
    27481, 27502, 27523, 27544, 27565, 27586, 27607, 27628, 27649, 27670, 27691, 27712, 27733, 27754, 27775, 27796,
    27818, 27839, 27860, 27881, 27902, 27923, 27944, 27965, 27986, 28007, 28028, 28049, 28071, 28092, 28113, 28134,
    28155, 28176, 28197, 28219, 28240, 28261, 28282, 28303, 28324, 28346, 28367, 28388, 28409, 28430, 28452, 28473,
    28494, 28515, 28536, 28558, 28579, 28600, 28621, 28643, 28664, 28685, 28706, 28728, 28749, 28770, 28791, 28813,
    28834, 28855, 28877, 28898, 28919, 28941, 28962, 28983, 29005, 29026, 29047, 29069, 29090, 29111, 29133, 29154,
    29175, 29197, 29218, 29240, 29261, 29282, 29304, 29325, 29346, 29368, 29389, 29411, 29432, 29454, 29475, 29496,
    29518, 29539, 29561, 29582, 29604, 29625, 29647, 29668, 29690, 29711, 29733, 29754, 29776, 29797, 29819, 29840,
    29862, 29883, 29905, 29926, 29948, 29969, 29991, 30012, 30034, 30056, 30077, 30099, 30120, 30142, 30164, 30185,
    30207, 30228, 30250, 30272, 30293, 30315, 30336, 30358, 30380, 30401, 30423, 30445, 30466, 30488, 30510, 30531,
    30553, 30575, 30596, 30618, 30640, 30661, 30683, 30705, 30727, 30748, 30770, 30792, 30814, 30835, 30857, 30879,
    30900, 30922, 30944, 30966, 30988, 31009, 31031, 31053, 31075, 31097, 31118, 31140, 31162, 31184, 31206, 31227,
    31249, 31271, 31293, 31315, 31337, 31359, 31380, 31402, 31424, 31446, 31468, 31490, 31512, 31534, 31555, 31577,
    31599, 31621, 31643, 31665, 31687, 31709, 31731, 31753, 31775, 31797, 31819, 31841, 31863, 31885, 31907, 31929,
    31951, 31973, 31995, 32017, 32039, 32061, 32083, 32105, 32127, 32149, 32171, 32193, 32215, 32237, 32259, 32281,
    32303, 32325, 32347, 32369, 32392, 32414, 32436, 32458, 32480, 32502, 32524, 32546, 32568, 32591, 32613, 32635,
    32657, 32679, 32701, 32724, 32746, 32768, 32790, 32812, 32834, 32857, 32879, 32901, 32923, 32945, 32968, 32990,
    33012, 33034, 33057, 33079, 33101, 33123, 33146, 33168, 33190, 33213, 33235, 33257, 33279, 33302, 33324, 33346,
    33369, 33391, 33413, 33436, 33458, 33480, 33503, 33525, 33547, 33570, 33592, 33614, 33637, 33659, 33682, 33704,
    33726, 33749, 33771, 33794, 33816, 33838, 33861, 33883, 33906, 33928, 33951, 33973, 33995, 34018, 34040, 34063,
    34085, 34108, 34130, 34153, 34175, 34198, 34220, 34243, 34265, 34288, 34310, 34333, 34355, 34378, 34400, 34423,
    34446, 34468, 34491, 34513, 34536, 34558, 34581, 34604, 34626, 34649, 34671, 34694, 34717, 34739, 34762, 34785,
    34807, 34830, 34852, 34875, 34898, 34920, 34943, 34966, 34988, 35011, 35034, 35057, 35079, 35102, 35125, 35147,
    35170, 35193, 35216, 35238, 35261, 35284, 35307, 35329, 35352, 35375, 35398, 35420, 35443, 35466, 35489, 35512,
    35534, 35557, 35580, 35603, 35626, 35648, 35671, 35694, 35717, 35740, 35763, 35785, 35808, 35831, 35854, 35877,
    35900, 35923, 35946, 35969, 35991, 36014, 36037, 36060, 36083, 36106, 36129, 36152, 36175, 36198, 36221, 36244,
    36267, 36290, 36313, 36336, 36359, 36382, 36405, 36428, 36451, 36474, 36497, 36520, 36543, 36566, 36589, 36612,
    36635, 36658, 36681, 36704, 36727, 36750, 36773, 36796, 36820, 36843, 36866, 36889, 36912, 36935, 36958, 36981,
    37004, 37028, 37051, 37074, 37097, 37120, 37143, 37167, 37190, 37213, 37236, 37259, 37282, 37306, 37329, 37352,
    37375, 37399, 37422, 37445, 37468, 37491, 37515, 37538, 37561, 37584, 37608, 37631, 37654, 37678, 37701, 37724,
    37747, 37771, 37794, 37817, 37841, 37864, 37887, 37911, 37934, 37957, 37981, 38004, 38028, 38051, 38074, 38098,
    38121, 38144, 38168, 38191, 38215, 38238, 38261, 38285, 38308, 38332, 38355, 38379, 38402, 38426, 38449, 38472,
    38496, 38519, 38543, 38566, 38590, 38613, 38637, 38660, 38684, 38707, 38731, 38754, 38778, 38802, 38825, 38849,
    38872, 38896, 38919, 38943, 38966, 38990, 39014, 39037, 39061, 39084, 39108, 39132, 39155, 39179, 39202, 39226,
    39250, 39273, 39297, 39321, 39344, 39368, 39392, 39415, 39439, 39463, 39486, 39510, 39534, 39558, 39581, 39605,
    39629, 39652, 39676, 39700, 39724, 39747, 39771, 39795, 39819, 39843, 39866, 39890, 39914, 39938, 39961, 39985,
    40009, 40033, 40057, 40081, 40104, 40128, 40152, 40176, 40200, 40224, 40248, 40271, 40295, 40319, 40343, 40367,
    40391, 40415, 40439, 40463, 40486, 40510, 40534, 40558, 40582, 40606, 40630, 40654, 40678, 40702, 40726, 40750,
    40774, 40798, 40822, 40846, 40870, 40894, 40918, 40942, 40966, 40990, 41014, 41038, 41062, 41086, 41110, 41134,
    41158, 41182, 41207, 41231, 41255, 41279, 41303, 41327, 41351, 41375, 41399, 41424, 41448, 41472, 41496, 41520,
    41544, 41568, 41593, 41617, 41641, 41665, 41689, 41714, 41738, 41762, 41786, 41810, 41835, 41859, 41883, 41907,
    41932, 41956, 41980, 42004, 42029, 42053, 42077, 42101, 42126, 42150, 42174, 42199, 42223, 42247, 42272, 42296,
    42320, 42345, 42369, 42393, 42418, 42442, 42466, 42491, 42515, 42539, 42564, 42588, 42613, 42637, 42661, 42686,
    42710, 42735, 42759, 42784, 42808, 42833, 42857, 42881, 42906, 42930, 42955, 42979, 43004, 43028, 43053, 43077,
    43102, 43126, 43151, 43175, 43200, 43224, 43249, 43274, 43298, 43323, 43347, 43372, 43396, 43421, 43446, 43470,
    43495, 43519, 43544, 43569, 43593, 43618, 43642, 43667, 43692, 43716, 43741, 43766, 43790, 43815, 43840, 43864,
    43889, 43914, 43938, 43963, 43988, 44013, 44037, 44062, 44087, 44111, 44136, 44161, 44186, 44210, 44235, 44260,
    44285, 44310, 44334, 44359, 44384, 44409, 44434, 44458, 44483, 44508, 44533, 44558, 44583, 44607, 44632, 44657,
    44682, 44707, 44732, 44757, 44781, 44806, 44831, 44856, 44881, 44906, 44931, 44956, 44981, 45006, 45031, 45056,
    45081, 45106, 45131, 45155, 45180, 45205, 45230, 45255, 45280, 45305, 45330, 45355, 45381, 45406, 45431, 45456,
    45481, 45506, 45531, 45556, 45581, 45606, 45631, 45656, 45681, 45706, 45731, 45757, 45782, 45807, 45832, 45857,
    45882, 45907, 45932, 45958, 45983, 46008, 46033, 46058, 46083, 46109, 46134, 46159, 46184, 46209, 46235, 46260,
    46285, 46310, 46336, 46361, 46386, 46411, 46437, 46462, 46487, 46512, 46538, 46563, 46588, 46614, 46639, 46664,
    46690, 46715, 46740, 46766, 46791, 46816, 46842, 46867, 46892, 46918, 46943, 46968, 46994, 47019, 47045, 47070,
    47095, 47121, 47146, 47172, 47197, 47223, 47248, 47273, 47299, 47324, 47350, 47375, 47401, 47426, 47452, 47477,
    47503, 47528, 47554, 47579, 47605, 47630, 47656, 47681, 47707, 47733, 47758, 47784, 47809, 47835, 47860, 47886,
    47912, 47937, 47963, 47988, 48014, 48040, 48065, 48091, 48117, 48142, 48168, 48194, 48219, 48245, 48271, 48296,
    48322, 48348, 48373, 48399, 48425, 48450, 48476, 48502, 48528, 48553, 48579, 48605, 48631, 48656, 48682, 48708,
    48734, 48759, 48785, 48811, 48837, 48863, 48888, 48914, 48940, 48966, 48992, 49018, 49044, 49069, 49095, 49121,
    49147, 49173, 49199, 49225, 49251, 49276, 49302, 49328, 49354, 49380, 49406, 49432, 49458, 49484, 49510, 49536,
    49562, 49588, 49614, 49640, 49666, 49692, 49718, 49744, 49770, 49796, 49822, 49848, 49874, 49900, 49926, 49952,
    49978, 50004, 50030, 50056, 50082, 50108, 50135, 50161, 50187, 50213, 50239, 50265, 50291, 50317, 50343, 50370,
    50396, 50422, 50448, 50474, 50500, 50527, 50553, 50579, 50605, 50631, 50658, 50684, 50710, 50736, 50763, 50789,
    50815, 50841, 50868, 50894, 50920, 50946, 50973, 50999, 51025, 51052, 51078, 51104, 51131, 51157, 51183, 51210,
    51236, 51262, 51289, 51315, 51341, 51368, 51394, 51420, 51447, 51473, 51500, 51526, 51552, 51579, 51605, 51632,
    51658, 51685, 51711, 51738, 51764, 51790, 51817, 51843, 51870, 51896, 51923, 51949, 51976, 52002, 52029, 52056,
    52082, 52109, 52135, 52162, 52188, 52215, 52241, 52268, 52295, 52321, 52348, 52374, 52401, 52428, 52454, 52481,
    52507, 52534, 52561, 52587, 52614, 52641, 52667, 52694, 52721, 52747, 52774, 52801, 52827, 52854, 52881, 52908,
    52934, 52961, 52988, 53015, 53041, 53068, 53095, 53122, 53148, 53175, 53202, 53229, 53256, 53282, 53309, 53336,
    53363, 53390, 53416, 53443, 53470, 53497, 53524, 53551, 53578, 53605, 53631, 53658, 53685, 53712, 53739, 53766,
    53793, 53820, 53847, 53874, 53901, 53928, 53955, 53981, 54008, 54035, 54062, 54089, 54116, 54143, 54170, 54197,
    54224, 54251, 54278, 54306, 54333, 54360, 54387, 54414, 54441, 54468, 54495, 54522, 54549, 54576, 54603, 54630,
    54658, 54685, 54712, 54739, 54766, 54793, 54820, 54848, 54875, 54902, 54929, 54956, 54983, 55011, 55038, 55065,
    55092, 55119, 55147, 55174, 55201, 55228, 55256, 55283, 55310, 55337, 55365, 55392, 55419, 55447, 55474, 55501,
    55529, 55556, 55583, 55611, 55638, 55665, 55693, 55720, 55747, 55775, 55802, 55829, 55857, 55884, 55912, 55939,
    55966, 55994, 56021, 56049, 56076, 56104, 56131, 56158, 56186, 56213, 56241, 56268, 56296, 56323, 56351, 56378,
    56406, 56433, 56461, 56488, 56516, 56543, 56571, 56599, 56626, 56654, 56681, 56709, 56736, 56764, 56792, 56819,
    56847, 56874, 56902, 56930, 56957, 56985, 57013, 57040, 57068, 57096, 57123, 57151, 57179, 57206, 57234, 57262,
    57289, 57317, 57345, 57373, 57400, 57428, 57456, 57484, 57511, 57539, 57567, 57595, 57622, 57650, 57678, 57706,
    57734, 57761, 57789, 57817, 57845, 57873, 57901, 57929, 57956, 57984, 58012, 58040, 58068, 58096, 58124, 58152,
    58179, 58207, 58235, 58263, 58291, 58319, 58347, 58375, 58403, 58431, 58459, 58487, 58515, 58543, 58571, 58599,
    58627, 58655, 58683, 58711, 58739, 58767, 58795, 58823, 58851, 58879, 58907, 58935, 58964, 58992, 59020, 59048,
    59076, 59104, 59132, 59160, 59189, 59217, 59245, 59273, 59301, 59329, 59357, 59386, 59414, 59442, 59470, 59498,
    59527, 59555, 59583, 59611, 59640, 59668, 59696, 59724, 59753, 59781, 59809, 59837, 59866, 59894, 59922, 59951,
    59979, 60007, 60036, 60064, 60092, 60121, 60149, 60177, 60206, 60234, 60263, 60291, 60319, 60348, 60376, 60405,
    60433, 60461, 60490, 60518, 60547, 60575, 60604, 60632, 60661, 60689, 60717, 60746, 60774, 60803, 60831, 60860,
    60889, 60917, 60946, 60974, 61003, 61031, 61060, 61088, 61117, 61146, 61174, 61203, 61231, 61260, 61289, 61317,
    61346, 61374, 61403, 61432, 61460, 61489, 61518, 61546, 61575, 61604, 61632, 61661, 61690, 61718, 61747, 61776,
    61805, 61833, 61862, 61891, 61920, 61948, 61977, 62006, 62035, 62063, 62092, 62121, 62150, 62179, 62208, 62236,
    62265, 62294, 62323, 62352, 62381, 62409, 62438, 62467, 62496, 62525, 62554, 62583, 62612, 62641, 62670, 62698,
    62727, 62756, 62785, 62814, 62843, 62872, 62901, 62930, 62959, 62988, 63017, 63046, 63075, 63104, 63133, 63162,
    63191, 63220, 63249, 63278, 63308, 63337, 63366, 63395, 63424, 63453, 63482, 63511, 63540, 63569, 63599, 63628,
    63657, 63686, 63715, 63744, 63774, 63803, 63832, 63861, 63890, 63919, 63949, 63978, 64007, 64036, 64066, 64095,
    64124, 64153, 64183, 64212, 64241, 64270, 64300, 64329, 64358, 64388, 64417, 64446, 64476, 64505, 64534, 64564,
    64593, 64622, 64652, 64681, 64711, 64740, 64769, 64799, 64828, 64858, 64887, 64916, 64946, 64975, 65005, 65034,
    65064, 65093, 65123, 65152, 65182, 65211, 65241, 65270, 65300, 65329, 65359, 65388, 65418, 65447, 65477, 65506,
        0
};

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

static const int8_t empty_waveform[256];

void avseq_playback_handler ( AVSequencerContext *avctx ) {
    AVSequencerModule *module                         = avctx->player_module;
    AVSequencerSong *song                             = avctx->player_song;
    AVSequencerPlayerGlobals *player_globals          = avctx->player_globals;
    AVSequencerPlayerHostChannel *player_host_channel = avctx->player_host_channel;
    AVSequencerPlayerChannel *player_channel          = avctx->player_channel;
    AVSequencerMixerData *mixer                       = avctx->player_mixer_data;
    AVSequencerPlayerHook *player_hook;
    uint16_t channel, virtual_channel;

    if (!(module && song && player_globals && player_host_channel && player_channel && mixer))
        return;

    channel = 0;

    do {
        mixer_get_channel ( mixer, (AVSequencerMixerChannel *) &(player_channel->mixer), channel, mixer->mixctx );

        player_channel++;
    } while (++channel < module->channels);

    if (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_TRACE_MODE) {
        if (!player_globals->trace_count--)
            player_globals->trace_count = 0;

        return;
    }

    player_hook = avctx->player_hook;

    if (player_hook && (player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_BEGINNING) &&
                       (((player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_SONG_END) &&
                       (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_SONG_END)) ||
                       (!(player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_SONG_END))))
        player_hook->hook_func ( avctx, player_hook->hook_data, player_hook->hook_len );

    if (player_globals->play_type & AVSEQ_PLAYER_GLOBALS_PLAY_TYPE_SONG) {
        uint32_t play_time_calc, play_time_advance, play_time_fraction;

        play_time_calc                  = ((uint64_t) player_globals->tempo * player_globals->relative_speed) >> 16;
        play_time_advance               = 65536000 / play_time_calc;
        play_time_fraction              = ((uint64_t) (65536000 % play_time_calc) << 32) / play_time_calc;
        player_globals->play_time_frac += play_time_fraction;

        if (player_globals->play_time_frac < play_time_fraction)
            play_time_advance++;

        player_globals->play_time      += play_time_advance;
        play_time_calc                  = player_globals->tempo;
        play_time_advance               = 65536000 / play_time_calc;
        play_time_fraction              = ((uint64_t) (65536000 % play_time_calc) << 32) / play_time_calc;
        player_globals->play_tics_frac += play_time_fraction;

        if (player_globals->play_tics_frac < play_time_fraction)
            play_time_advance++;

        player_globals->play_tics      += play_time_advance;
    }

    channel = 0;

    do {
        player_channel = avctx->player_channel + player_host_channel->virtual_channel;

        if ((player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT) &&
            (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE)) {
            AVSequencerTrack *old_track        = player_host_channel->track;
            AVSequencerTrackEffect *old_effect = player_host_channel->effect;
            uint32_t old_tempo_counter         = player_host_channel->tempo_counter;
            uint16_t old_row                   = player_host_channel->row;

            player_host_channel->flags        &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT|AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE);
            player_host_channel->track         = (AVSequencerTrack *) player_host_channel->instrument;
            player_host_channel->effect        = NULL;
            player_host_channel->row           = *(uint32_t *) &(player_host_channel->sample);
            player_host_channel->instrument    = NULL;
            player_host_channel->sample        = NULL;

            get_effects ( avctx, player_host_channel, player_channel, channel );

            player_host_channel->tempo_counter = player_host_channel->note_delay;

            get_note ( avctx, player_host_channel, player_channel, channel );
            run_effects ( avctx, player_host_channel, player_channel, channel );

            player_host_channel->track         = old_track;
            player_host_channel->effect        = old_effect;
            player_host_channel->tempo_counter = old_tempo_counter;
            player_host_channel->row           = old_row;
        }

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT) {
            uint16_t note = (uint8_t) player_host_channel->instr_note;

            player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_INSTRUMENT;

            if ((int16_t) note < 0) {
                switch ((int) note) {
                case AVSEQ_TRACK_DATA_NOTE_FADE :
                    player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;

                    break;
                case AVSEQ_TRACK_DATA_NOTE_HOLD_DELAY :
                    break;
                case AVSEQ_TRACK_DATA_NOTE_KEYOFF :
                    play_key_off ( player_channel );

                    break;
                case AVSEQ_TRACK_DATA_NOTE_OFF :
                    player_channel->volume = 0;

                    break;
                case AVSEQ_TRACK_DATA_NOTE_KILL :
                    player_host_channel->instrument = NULL;
                    player_host_channel->sample     = NULL;
                    player_host_channel->instr_note = 0;

                    if (player_channel->host_channel == channel)
                        player_channel->mixer.flags = 0;

                    break;
                }
            } else {
                AVSequencerInstrument *instrument = player_host_channel->instrument;
                AVSequencerSample *sample;
                AVSequencerPlayerChannel *new_player_channel;

                if ((new_player_channel = play_note ( avctx, instrument,
                                                      player_host_channel, player_channel,
                                                      note / 12,
                                                      note % 12, channel )))
                    player_channel = new_player_channel;

                sample                  = player_host_channel->sample;
                player_channel->volume  = player_host_channel->sample_note;
                player_channel->sub_vol = 0;

                init_new_instrument ( avctx, player_host_channel, player_channel );
                init_new_sample ( avctx, player_host_channel, player_channel );
            }
        }

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE) {
            AVSequencerInstrument *instrument;
            AVSequencerSample *sample = player_host_channel->sample;
            uint32_t frequency        = *(uint32_t *) &(player_host_channel->instrument), i;
            uint16_t virtual_channel;

            player_host_channel->flags             &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_SAMPLE;
            player_host_channel->dct                = 0;
            player_host_channel->nna                = AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_CUT;
            player_host_channel->finetune           = sample->finetune;
            player_host_channel->prev_auto_vib_env  = player_channel->auto_vib_env.envelope;
            player_host_channel->prev_auto_trem_env = player_channel->auto_trem_env.envelope;
            player_host_channel->prev_auto_pan_env  = player_channel->auto_pan_env.envelope;

            player_channel = trigger_nna ( avctx, player_host_channel, player_channel, channel, (uint16_t *) &virtual_channel );

            player_channel->mixer.pos            = sample->start_offset;
            player_host_channel->virtual_channel = virtual_channel;
            player_channel->host_channel         = channel;
            player_host_channel->instrument      = NULL;
            player_channel->sample               = sample;
            player_channel->frequency            = frequency;
            player_channel->volume               = player_host_channel->instr_note;
            player_channel->sub_vol              = 0;
            player_host_channel->instr_note      = 0;

            init_new_instrument ( avctx, player_host_channel, player_channel );

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

            init_new_sample ( avctx, player_host_channel, player_channel );
        }

        if ((!(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_NO_PROC_PATTERN)) && player_host_channel->tempo) {
rescan_row:
            process_row ( avctx, player_host_channel, player_channel, channel );
            get_effects ( avctx, player_host_channel, player_channel, channel );

            if (player_channel->host_channel == channel) {
                if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_VIBRATO)) {
                    int32_t slide_value                = player_host_channel->vibrato_slide;

                    player_host_channel->vibrato_slide = 0;
                    player_channel->frequency         -= slide_value;
                }

                if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOLO)) {
                    int16_t slide_value                = player_host_channel->tremolo_slide;

                    player_host_channel->tremolo_slide = 0;

                    if ((slide_value = ( player_channel->volume - slide_value)) < 0)
                        slide_value = 0;

                    if ((uint16_t) slide_value >= 255)
                        slide_value = -1;

                    player_channel->volume = slide_value;
                }
            }

            if (get_note ( avctx, player_host_channel, player_channel, channel))
                goto rescan_row;
        }

        player_host_channel->virtual_channels = 0;
        player_host_channel++;
    } while (++channel < song->channels);

    channel             = 0;
    player_host_channel = avctx->player_host_channel;

    do {
        if ((!(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_NO_PROC_PATTERN)) && player_host_channel->tempo) {
            player_channel = avctx->player_channel + player_host_channel->virtual_channel;

            run_effects ( avctx, player_host_channel, player_channel, channel );
        }

        player_host_channel->virtual_channels = 0;
        player_host_channel++;
    } while (++channel < song->channels);

    virtual_channel = 0;
    channel         = 0;
    player_channel  = avctx->player_channel;

    do {
        AVSequencerPlayerEnvelope *player_envelope;

        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED)
            player_channel->mixer.flags &= ~AVSEQ_MIXER_CHANNEL_FLAG_PLAY;

        if (player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
            AVSequencerSample *sample;
            uint32_t frequency, host_volume, virtual_volume;
            uint32_t auto_vibrato_depth, auto_vibrato_count;
            uint16_t flags, slide_envelope_value;
            int16_t auto_vibrato_value, panning, abs_panning, panning_envelope_value;

            player_host_channel = avctx->player_host_channel + player_channel->host_channel;
            player_envelope     = (AVSequencerPlayerEnvelope *) &(player_channel->vol_env);

            if (player_envelope->tempo) {
                uint16_t volume = run_envelope ( avctx, player_envelope, 1, 0x8000 );

                if (!player_envelope->tempo) {
                    if (!(volume >> 8))
                        goto turn_note_off;

                    player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;
                }
            }

            run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_channel->pan_env), 1, 0 );
            slide_envelope_value = run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_channel->slide_env), 1, 0 );

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_PORTA_SLIDE_ENV) {
                uint32_t old_frequency = player_channel->frequency;

                player_channel->frequency += player_channel->slide_env_freq;

                if ((frequency = player_channel->frequency)) {
                    if ((int16_t) slide_envelope_value < 0) {
                        slide_envelope_value = -slide_envelope_value;

                        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV)
                            frequency = linear_slide_down ( avctx, player_channel, frequency, slide_envelope_value );
                        else
                            frequency = amiga_slide_down ( player_channel, frequency, slide_envelope_value );
                    } else if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV) {
                        frequency = linear_slide_up ( avctx, player_channel, frequency, slide_envelope_value );
                    } else {
                        frequency = amiga_slide_up ( player_channel, frequency, slide_envelope_value );
                    }

                    old_frequency -= frequency;

                    player_channel->slide_env_freq += old_frequency;
                }
            } else {
                uint32_t *frequency_lut;
                uint32_t frequency, next_frequency, slide_envelope_frequency, old_frequency;
                int16_t octave, note;
                int16_t slide_note = (int16_t) slide_envelope_value >> 8;
                int16_t finetune   = slide_envelope_value & 0xFF;

                octave             = slide_note / 12;
                note               = slide_note % 12;

                if (note < 0) {
                    octave--;
                    note += 12;

                    finetune = -finetune;
                }

                frequency_lut  = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
                frequency      = *frequency_lut++;
                next_frequency = *frequency_lut - frequency;
                frequency     += (int32_t) (finetune * (int16_t) next_frequency) >> 8;

                if ((int16_t) octave < 0) {
                    octave = -octave;
                    frequency >>= octave;
                } else {
                    frequency <<= octave;
                }

                slide_envelope_frequency        = player_channel->slide_env_freq;
                old_frequency                   = player_channel->frequency;
                slide_envelope_frequency       += old_frequency;
                player_channel->frequency       = frequency = ((uint64_t) frequency * slide_envelope_frequency) >> 16;
                player_channel->slide_env_freq += old_frequency - frequency;
            }

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_FADING) {
                int32_t fade_out = (uint32_t) player_channel->fade_out_count;

                if ((fade_out -= player_channel->fade_out) <= 0)
                    goto turn_note_off;

                player_channel->fade_out_count = fade_out;
            }

            auto_vibrato_value = run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_channel->auto_vib_env), player_channel->auto_vibrato_rate, 0 );
            auto_vibrato_depth = player_channel->auto_vibrato_depth << 8;
            auto_vibrato_count = player_channel->auto_vibrato_count + player_channel->auto_vibrato_sweep;

            if (auto_vibrato_count > auto_vibrato_depth)
                auto_vibrato_count = auto_vibrato_depth;

            player_channel->auto_vibrato_count = auto_vibrato_count;

            auto_vibrato_count >>= 8;

            if ((auto_vibrato_value *= (int16_t) -auto_vibrato_count)) {
                uint32_t old_frequency = player_channel->frequency;

                auto_vibrato_value >>= 7 - 2;

                player_channel->frequency -= player_channel->auto_vibrato_freq;

                if ((frequency = player_channel->frequency)) {
                    if ((int16_t) auto_vibrato_value < 0) {
                        auto_vibrato_value = -auto_vibrato_value;

                        if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV)
                            frequency = linear_slide_up ( avctx, player_channel, frequency, auto_vibrato_value );
                        else
                            frequency = amiga_slide_up ( player_channel, frequency, auto_vibrato_value );
                    } else if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV) {
                        frequency = linear_slide_down ( avctx, player_channel, frequency, auto_vibrato_value );
                    } else {
                        frequency = amiga_slide_down ( player_channel, frequency, auto_vibrato_value );
                    }

                    old_frequency -= frequency;

                    player_channel->auto_vibrato_freq -= old_frequency;
                }
            }

            sample = player_channel->sample;

            if (sample->synth) {
                if (!(execute_synth ( avctx, player_host_channel, player_channel, channel, 0)))
                    goto turn_note_off;

                if (!(execute_synth ( avctx, player_host_channel, player_channel, channel, 1)))
                    goto turn_note_off;

                if (!(execute_synth ( avctx, player_host_channel, player_channel, channel, 2)))
                    goto turn_note_off;

                if (!(execute_synth ( avctx, player_host_channel, player_channel, channel, 3)))
                    goto turn_note_off;
            }

            if (((!(player_channel->mixer.data)) || (!(player_channel->mixer.bits_per_sample))) && (player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY)) {
                player_channel->mixer.pos             = 0;
                player_channel->mixer.len             = (sizeof (empty_waveform) / sizeof (empty_waveform[0]));
                player_channel->mixer.data            = (int16_t *) &(empty_waveform);
                player_channel->mixer.repeat_start    = 0;
                player_channel->mixer.repeat_length   = (sizeof (empty_waveform) / sizeof (empty_waveform[0]));
                player_channel->mixer.repeat_count    = 0;
                player_channel->mixer.repeat_counted  = 0;
                player_channel->mixer.bits_per_sample = (sizeof (empty_waveform[0]) * 8);
                player_channel->mixer.flags           = AVSEQ_MIXER_CHANNEL_FLAG_LOOP|AVSEQ_MIXER_CHANNEL_FLAG_PLAY;
            }

            frequency = player_channel->frequency;

            if (frequency < sample->rate_min)
                frequency = sample->rate_min;

            if (frequency > sample->rate_max)
                frequency = sample->rate_max;

            if (!(player_channel->frequency = frequency)) {
turn_note_off:
                player_channel->mixer.flags = 0;

                goto not_calculate_no_playing;
            }

            if (!(player_channel->mixer.rate = ((uint64_t) frequency * player_globals->relative_pitch) >> 16))
                goto turn_note_off;

            if (!(song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_GLOBAL_NEW_ONLY)) {
                player_channel->global_volume  = player_globals->global_volume;
                player_channel->global_sub_vol = player_globals->global_sub_volume;
                player_channel->global_panning = player_globals->global_panning;
                player_channel->global_sub_pan = player_globals->global_sub_panning;
            }

            host_volume = player_channel->volume;

            player_host_channel->virtual_channels++;
            virtual_channel++;

            if ((!(player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND)) && (player_host_channel->virtual_channel == channel) && (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_EXEC) && (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_OFF))
                host_volume = 0;

            host_volume                        *= (uint16_t) player_host_channel->track_volume * (uint16_t) player_channel->instr_volume;
            virtual_volume                      = (((uint16_t) player_channel->vol_env.value >> 8) * (uint16_t) player_channel->global_volume) * (uint16_t) player_channel->fade_out_count;
            player_channel->mixer.volume        = player_channel->final_volume = ((uint64_t) host_volume * virtual_volume) / (255ULL*255ULL*255ULL*255ULL*65535ULL*255ULL);
            flags                               = 0;
            player_channel->flags              &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SURROUND;
            player_channel->mixer.flags        &= ~AVSEQ_MIXER_CHANNEL_FLAG_SURROUND;

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                flags = AVSEQ_MIXER_CHANNEL_FLAG_SURROUND;

            panning = player_channel->panning;

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN) {
                panning = player_host_channel->track_panning;
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
            abs_panning = (uint16_t) player_host_channel->channel_panning;

            if (abs_panning == 255)
                abs_panning++;

            abs_panning           -= 128;
            panning_envelope_value = abs_panning = ((panning * abs_panning) >> 7) + 128;

            if (panning_envelope_value > 255)
                panning_envelope_value = 255;

            player_channel->final_panning = panning_envelope_value;

            panning = 128;

            if (!(song->flags & AVSEQ_SONG_FLAG_MONO)) {
                if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_GLOBAL_SUR_PAN)
                    player_channel->mixer.flags |= AVSEQ_MIXER_CHANNEL_FLAG_SURROUND;

                panning -= abs_panning;

                abs_panning = (uint16_t) player_channel->global_panning;

                if (abs_panning == 255)
                    abs_panning++;

                abs_panning -= 128;
                panning      = ((panning * abs_panning) >> 7) + 128;

                if (panning == 256)
                    panning--;
            }

            player_channel->mixer.panning = panning;

            mixer_set_channel_volume_panning_pitch ( mixer, (AVSequencerMixerChannel *) &(player_channel->mixer), channel, mixer->mixctx );
        }
not_calculate_no_playing:
        mixer_set_channel_position_repeat_flags ( mixer, (AVSequencerMixerChannel *) &(player_channel->mixer), channel, mixer->mixctx );

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
    if (player_hook && (!(player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_BEGINNING)) &&
                       (((player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_SONG_END) &&
                       (!(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_SONG_END))) ||
                       (!(player_hook->flags & AVSEQ_PLAYER_HOOK_FLAG_SONG_END))))
        player_hook->hook_func ( avctx, player_hook->hook_data, player_hook->hook_len );

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

                while (i++ < order_list->orders) {
                    if ((order_data = order_list->order_data[i]) && (order_data != player_host_channel->order))
                        order_data->played = 0;
                }

                order_list++;
                player_host_channel++;
            } while (--channel);
        }
    }
}

static void process_row ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel ) {
    AVSequencerSong *song = avctx->player_song;
    uint32_t current_tick;
    uint16_t counted = 0;

    player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_EXEC;
    current_tick                = player_host_channel->tempo_counter;
    current_tick++;

    if (current_tick >= (player_host_channel->fine_pattern_delay + player_host_channel->tempo))
        current_tick  = 0;

    if (!(player_host_channel->tempo_counter = current_tick)) {
        AVSequencerTrack *track;
        AVSequencerOrderList *order_list;
        AVSequencerOrderData *order_data;
        AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
        uint16_t pattern_delay, row, last_row, track_length;
        uint32_t ord;

        if (player_channel->host_channel == channel) {
            uint32_t slide_value               = player_host_channel->arpeggio_freq;

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
            if (!(row--))
                goto get_new_pattern;
        } else if (++row >= player_host_channel->max_row) {
get_new_pattern:
            order_list = (AVSequencerOrderList *) song->order_list + channel;
            order_data = player_host_channel->order;
 
            if (avctx->player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_PATTERN) {
                track = player_host_channel->track;

                goto loop_to_row;
            }

            ord        = -1;

            while (++ord < order_list->orders) {
                if (order_data == order_list->order_data[ord])
                    break;
            }

check_next_empty_order:
            do {
                ord++;

                if ((ord >= order_list->orders) || (!(order_data = order_list->order_data[ord]))) {
song_end_found:
                    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;
                    order_list                  = (AVSequencerOrderList *) song->order_list + channel;

                    if ((order_list->rep_start >= order_list->orders) || (!(order_data = order_list->order_data[order_list->rep_start]))) {
disable_channel:
                        player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;
                        player_host_channel->tempo  = 0;

                        return;
                    }

                    if (order_data->flags & (AVSEQ_ORDER_DATA_FLAG_END_ORDER|AVSEQ_ORDER_DATA_FLAG_END_SONG))
                        goto disable_channel;

                    row = 0;

                    if (((player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE) && (order_data->flags & AVSEQ_ORDER_DATA_FLAG_NOT_IN_ONCE)) || ((!(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE)) && (order_data->flags & AVSEQ_ORDER_DATA_FLAG_NOT_IN_REPEAT)))
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
            } while (((player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE) && (order_data->flags & AVSEQ_ORDER_DATA_FLAG_NOT_IN_ONCE)) || ((!(player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_PLAY_ONCE)) && (order_data->flags & AVSEQ_ORDER_DATA_FLAG_NOT_IN_REPEAT)) || (!(track = order_data->track)));

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

static void get_effects ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel ) {
    AVSequencerTrack *track;

    if ((track = player_host_channel->track)) {
        AVSequencerTrackData *track_data = track->data;
        AVSequencerTrackEffect *track_fx;
        uint32_t fx;

        if ((track_fx = player_host_channel->effect)) {
            fx         = -1;

            while (++fx < track_data->effects) {
                if (track_fx == track_data->effects_data[fx])
                    break;
            }
        } else {
            fx          = 0;
            track_data += player_host_channel->row;
        }

        player_host_channel->effect = track_fx;

        if ((fx < track_data->effects) && track_data->effects_data[fx]) {
            for (;;) {
                uint8_t fx_byte = track_fx->command;

                if (fx_byte == AVSEQ_TRACK_EFFECT_CMD_EXECUTE_FX) {
                    player_host_channel->flags  |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX;
                    player_host_channel->exec_fx = track_fx->data;

                    if (player_host_channel->tempo_counter < player_host_channel->exec_fx)
                        break;
                }

                fx++;

                if ((fx >= track_data->effects) || (!(track_fx = track_data->effects_data[fx])))
                    break;
            }

            if (player_host_channel->effect != track_fx) {
                player_host_channel->effect = track_fx;

                AV_WN64A(player_host_channel->effects_used, 0);
                AV_WN64A(player_host_channel->effects_used + 8, 0);
            }

            track_data = track->data + player_host_channel->row;
            fx         = -1;

            while ((++fx < track_data->effects) && ((track_fx = track_data->effects_data[fx]))) {
                AVSequencerPlayerEffects *fx_lut;
                void (*pre_fx_func)( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel, uint16_t data_word );
                uint16_t fx_byte;

                fx_byte = track_fx->command;
                fx_lut  = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;

                if ((pre_fx_func = fx_lut->pre_pattern_func))
                    pre_fx_func ( avctx, player_host_channel, player_channel, channel, track_fx->data );
            }
        }
    }
}

static uint32_t get_note ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel ) {
    AVSequencerModule *module = avctx->player_module;
    AVSequencerTrack *track;
    AVSequencerTrackData *track_data;
    AVSequencerInstrument *instrument;
    AVSequencerPlayerChannel *new_player_channel;
    uint32_t instr;
    uint16_t octave_note;
    uint8_t octave;
    int8_t note;

    if (player_host_channel->pattern_delay_count || (player_host_channel->tempo_counter != player_host_channel->note_delay) || (!(track = player_host_channel->track)))
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
            play_key_off ( player_channel );

            break;
        case AVSEQ_TRACK_DATA_NOTE_OFF :
            player_channel->volume = 0;

            break;
        case AVSEQ_TRACK_DATA_NOTE_KILL :
            player_host_channel->instrument = NULL;
            player_host_channel->sample     = NULL;
            player_host_channel->instr_note = 0;

            if (player_channel->host_channel == channel)
                player_channel->mixer.flags = 0;

            break;
        }

        return 0;
    } else if ((instr = track_data->instrument)) {
        AVSequencerSample *sample;

        instr--;

        if ((instr >= module->instruments) || (!(instrument = module->instrument_list[instr])))
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
            player_host_channel->tone_porta_target_pitch = get_tone_pitch ( avctx, player_host_channel, player_channel, get_key_table_note ( avctx, instrument, player_host_channel, octave, note ));

            return 0;
        }

        if (octave_note) {
            if ((new_player_channel = play_note ( avctx, instrument, player_host_channel, player_channel, octave, note, channel )))
                player_channel = new_player_channel;

            sample                  = player_host_channel->sample;
            player_channel->volume  = sample->volume;
            player_channel->sub_vol = sample->sub_volume;

            init_new_instrument ( avctx, player_host_channel, player_channel );
            init_new_sample ( avctx, player_host_channel, player_channel );
        } else {
            uint16_t note;

            if (!instrument)
                return 0;

            if ((note = player_host_channel->instr_note)) {
                if ((note = get_key_table ( avctx, instrument, player_host_channel, note )) == 0x8000)
                    return 0;

                if ((player_channel->host_channel != channel) || (player_host_channel->instrument != instrument)) {
                    if ((new_player_channel = play_note_got ( avctx, player_host_channel, player_channel, note, channel )))
                        player_channel = new_player_channel;
                }
            } else {
                note                             = get_key_table ( avctx, instrument, player_host_channel, 1 );
                player_host_channel->instr_note  = 0;
                player_host_channel->sample_note = 0;

                if ((new_player_channel = play_note_got ( avctx, player_host_channel, player_channel, note, channel )))
                    player_channel = new_player_channel;

                player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED;
            }

            sample                  = player_host_channel->sample;
            player_channel->volume  = sample->volume;
            player_channel->sub_vol = sample->sub_volume;

            init_new_instrument ( avctx, player_host_channel, player_channel );

            if (!(instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_LOCK_INSTR_WAVE))
                init_new_sample ( avctx, player_host_channel, player_channel );
        }
    } else if ((instrument = player_host_channel->instrument) && module->instruments) {
        if (!(instrument->flags & AVSEQ_INSTRUMENT_FLAG_NO_INSTR_TRANSPOSE)) {
            AVSequencerOrderData *order_data = player_host_channel->order;

            if (order_data->instr_transpose) {
                AVSequencerInstrument *instrument_scan;

                do {
                    if (module->instrument_list[instr] == instrument)
                        break;
                } while (++instr < module->instruments);

                instr += order_data->instr_transpose;

                if ((instr < module->instruments) && ((instrument_scan = module->instrument_list[instr])))
                    instrument = instrument_scan;
            }
        }

        if ((new_player_channel = play_note ( avctx, instrument, player_host_channel, player_channel, octave, note, channel ))) {
            AVSequencerSample *sample = player_host_channel->sample;

            new_player_channel->mixer.pos = sample->start_offset;

            if (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_VOLUME_ONLY) {
                new_player_channel->volume  = player_channel->volume;
                new_player_channel->sub_vol = player_channel->sub_vol;

                init_new_instrument ( avctx, player_host_channel, new_player_channel );
                init_new_sample ( avctx, player_host_channel, new_player_channel );
            } else {
                if (player_channel != new_player_channel) {
                    memcpy ( &(new_player_channel->volume), &(player_channel->volume), (offsetof (AVSequencerPlayerChannel, instr_note) - offsetof (AVSequencerPlayerChannel, volume)));

                    new_player_channel->host_channel = channel;
                }

                init_new_instrument ( avctx, player_host_channel, new_player_channel );
                init_new_sample ( avctx, player_host_channel, new_player_channel );
            }
        }
    }

    return 0;
}

static int16_t get_key_table_note ( AVSequencerContext *avctx, AVSequencerInstrument *instrument, AVSequencerPlayerHostChannel *player_host_channel, uint16_t octave, uint16_t note ) {
    return get_key_table ( avctx, instrument, player_host_channel, (( octave * 12 ) + note ));
}

static int16_t get_key_table ( AVSequencerContext *avctx, AVSequencerInstrument *instrument, AVSequencerPlayerHostChannel *player_host_channel, uint16_t note ) {
    AVSequencerModule *module = avctx->player_module;
    AVSequencerKeyboard *keyboard;
    AVSequencerSample *sample;
    uint16_t smp = 1, i;
    int8_t transpose;

    if (!player_host_channel->instrument)
        player_host_channel->nna = instrument->nna;

    player_host_channel->instr_note  = note;
    player_host_channel->sample_note = note;
    player_host_channel->instrument  = instrument;

    if (!(keyboard = (AVSequencerKeyboard *) instrument->keyboard_defs))
        goto do_not_play_keyboard;

    i                                = ++note;
    note                             = ((uint16_t) (keyboard->key[i].octave & 0x7F) * 12) + keyboard->key[i].note;
    player_host_channel->sample_note = note;

    if ((smp = keyboard->key[i].sample)) {
do_not_play_keyboard:
        smp--;

        if (!(instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_SEPARATE_SAMPLES)) {
            if ((smp >= instrument->samples) || (!(sample = instrument->sample_list[smp])))
                return 0x8000;
        } else {
            if ((smp >= module->instruments) || (!(instrument = module->instrument_list[smp])))
                return 0x8000;

            if (!instrument->samples || !(sample = instrument->sample_list[0]))
                return 0x8000;
        }
    } else {
        sample = player_host_channel->sample;

        if ((instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_PREV_SAMPLE) || !sample)
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

static uint32_t get_tone_pitch ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, int16_t note ) {
    AVSequencerSample *sample = player_host_channel->sample;
    uint32_t *frequency_lut;
    uint32_t frequency, next_frequency;
    uint16_t octave;
    int8_t finetune;

    octave = note / 12;
    note   = note % 12;

    if (note < 0) {
        octave--;

        note += 12;
    }

    if ((finetune = player_host_channel->finetune) < 0) {
        finetune += -0x80;

        note--;
    }

    frequency_lut  = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
    frequency      = *frequency_lut++;
    next_frequency = *frequency_lut - frequency;
    frequency     += (int32_t) (finetune * (int16_t) next_frequency) >> 7;

    if ((int16_t) (octave -= 4) < 0) {
        octave = -octave;

        return ((uint64_t) frequency * sample->rate) >> (16 + octave);
    } else {
        frequency <<= octave;

        return ((uint64_t) frequency * sample->rate) >> 16;
    }
}

static AVSequencerPlayerChannel *play_note ( AVSequencerContext *avctx, AVSequencerInstrument *instrument, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t octave, uint16_t note, uint32_t channel ) {
    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_RETRIG_NOTE;

    if ((note = get_key_table_note ( avctx, instrument, player_host_channel, octave, note )) == 0x8000)
        return NULL;

    return play_note_got ( avctx, player_host_channel, player_channel, note, channel );
}

static AVSequencerPlayerChannel *play_note_got ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t note, uint32_t channel ) {
    AVSequencerInstrument *instrument = player_host_channel->instrument;
    AVSequencerSample *sample = player_host_channel->sample;
    uint32_t note_swing, pitch_swing, frequency = 0;
    uint32_t seed;
    uint16_t virtual_channel;

    player_host_channel->dct        = instrument->dct;
    player_host_channel->dna        = instrument->dna;
    note_swing                      = (instrument->note_swing << 1) + 1;
    avctx->seed                     = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
    note_swing                      = ((uint64_t) seed * note_swing) >> 32;
    note_swing                     -= instrument->note_swing;
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

    player_channel = trigger_nna ( avctx, player_host_channel, player_channel, channel, (uint16_t *) &virtual_channel );

    player_channel->mixer.pos            = sample->start_offset;
    player_host_channel->virtual_channel = virtual_channel;
    player_channel->host_channel         = channel;
    player_channel->instrument           = instrument;
    player_channel->sample               = sample;

    if ((player_channel->instr_note = player_host_channel->instr_note)) {
        int16_t final_note;

        player_channel->sample_note = player_host_channel->sample_note;
        player_channel->final_note  = final_note = player_host_channel->final_note;

        frequency = get_tone_pitch ( avctx, player_host_channel, player_channel, final_note );
    }

    note_swing    = pitch_swing = ((uint64_t) frequency * instrument->pitch_swing) >> 16;
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

static void init_new_instrument ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel ) {
    AVSequencerInstrument *instrument = player_host_channel->instrument;
    AVSequencerSample *sample = player_host_channel->sample;
    AVSequencerPlayerGlobals *player_globals;
    AVSequencerEnvelope * (**assign_envelope)( AVSequencerContext *avctx, AVSequencerInstrument *instrument, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, AVSequencerEnvelope **envelope, AVSequencerPlayerEnvelope **player_envelope );
    AVSequencerEnvelope * (**assign_auto_envelope)( AVSequencerSample *sample, AVSequencerPlayerChannel *player_channel, AVSequencerPlayerEnvelope **player_envelope );
    uint32_t volume, panning, panning_separation, i;

    if (instrument) {
        uint32_t volume_swing, abs_volume_swing, seed;

        volume            = sample->global_volume * instrument->global_volume;
        volume_swing      = (volume * instrument->volume_swing) >> 8;
        abs_volume_swing  = (volume_swing << 1) + 1;
        avctx->seed       = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
        abs_volume_swing  = ((uint64_t) seed * abs_volume_swing) >> 32;
        abs_volume_swing -= volume_swing;

        if ((int32_t) (volume += abs_volume_swing) < 0)
            volume = 0;

        if (volume > (255*255))
            volume = 255*255;
    } else {
        volume = sample->global_volume * 255;
    }

    player_channel->instr_volume   = volume;
    player_globals                 = avctx->player_globals;
    player_channel->global_volume  = player_globals->global_volume;
    player_channel->global_sub_vol = player_globals->global_sub_volume;
    player_channel->global_panning = player_globals->global_panning;
    player_channel->global_sub_pan = player_globals->global_sub_panning;

    if (instrument) {
        player_channel->fade_out       = instrument->fade_out;
        player_channel->fade_out_count = 65535;

        player_host_channel->nna = instrument->nna;
    }

    player_channel->auto_vibrato_sweep = sample->vibrato_sweep;
    player_channel->auto_tremolo_sweep = sample->tremolo_sweep;
    player_channel->auto_pan_sweep     = sample->pannolo_sweep;
    player_channel->auto_vibrato_depth = sample->vibrato_depth;
    player_channel->auto_vibrato_rate  = sample->vibrato_rate;
    player_channel->auto_tremolo_depth = sample->tremolo_depth;
    player_channel->auto_tremolo_rate  = sample->tremolo_rate;
    player_channel->auto_pan_depth     = sample->pannolo_depth;
    player_channel->auto_pan_rate      = sample->pannolo_rate;
    player_channel->slide_env_freq     = 0;
    player_channel->auto_vibrato_count = 0;
    player_channel->auto_tremolo_count = 0;
    player_channel->auto_pannolo_count = 0;

    player_channel->flags &= AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED;

    player_host_channel->vibrato_slide = 0;
    player_host_channel->tremolo_slide = 0;
    player_channel->auto_vibrato_freq  = 0;
    player_channel->auto_tremolo_vol   = 0;
    player_channel->auto_pannolo_pan   = 0;
    player_channel->flags             &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_FREQ_AUTO_VIB|AVSEQ_PLAYER_CHANNEL_FLAG_PORTA_SLIDE_ENV|AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV);

    if (sample->env_proc_flags & AVSEQ_SAMPLE_FLAG_PROC_LINEAR_AUTO_VIB)
        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_FREQ_AUTO_VIB;

    if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_PORTA_SLIDE_ENV)
        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_PORTA_SLIDE_ENV;

    if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_LINEAR_SLIDE_ENV)
        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_LINEAR_SLIDE_ENV;

    assign_envelope = (void *) &(assign_envelope_lut);
    i               = 0;

    do {
        AVSequencerEnvelope *envelope;
        AVSequencerPlayerEnvelope *player_envelope;
        uint16_t mask = 1 << i;

        if (instrument) {
            if (assign_envelope[i] ( avctx, instrument, player_host_channel, player_channel, (AVSequencerEnvelope **) &envelope, (AVSequencerPlayerEnvelope **) &player_envelope )) {
                if (instrument->env_usage_flags & mask)
                    continue;
            }

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

                set_envelope ( player_channel, player_envelope, envelope_pos );

                player_envelope->flags |= flags;
            }
        } else {
            assign_envelope[i] ( avctx, instrument, player_host_channel, player_channel, (AVSequencerEnvelope **) &envelope, (AVSequencerPlayerEnvelope **) &player_envelope );

            player_envelope->envelope     = NULL;
            player_channel->vol_env.value = 0;
        }
    } while (++i < (sizeof (assign_envelope_lut) / sizeof (void *)));

    player_channel->vol_env.value = -1;
             assign_auto_envelope = (void *) &(assign_auto_envelope_lut);
    i                             = 0;

    do {
        AVSequencerEnvelope *envelope;
        AVSequencerPlayerEnvelope *player_envelope;
        uint16_t mask = 1 << i;

        envelope = assign_auto_envelope[i] ( sample, player_channel, (AVSequencerPlayerEnvelope **) &player_envelope );

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

            set_envelope ( player_channel, player_envelope, envelope_pos );

            player_envelope->flags |= flags;
        }
    } while (++i < (sizeof (assign_auto_envelope_lut) / sizeof (void *)));

    panning                = player_host_channel->track_note_pan;
    player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN;

    if (sample->flags & AVSEQ_SAMPLE_FLAG_SAMPLE_PANNING) {
        player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

        if (sample->flags & AVSEQ_SAMPLE_FLAG_SURROUND_PANNING)
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

        player_channel->panning            = sample->panning;
        player_channel->sub_pan            = sample->sub_panning;
        player_host_channel->pannolo_slide = 0;

        panning = player_channel->panning;

        if (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
            player_host_channel->track_panning      = player_channel->panning;
            player_host_channel->track_sub_pan      = player_channel->sub_pan;
            player_host_channel->track_note_pan     = player_channel->panning;
            player_host_channel->track_note_sub_pan = player_channel->sub_pan;
            player_host_channel->flags             &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                player_host_channel->flags         |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
        }
    } else {
        player_channel->panning = player_host_channel->track_panning;
        player_channel->sub_pan = player_host_channel->track_sub_pan;
        player_channel->flags  &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN);
        player_channel->flags  |= (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN);
    }

    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

    if (instrument) {
        uint32_t volume_swing, seed;

        if ((instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) && (sample->compat_flags & AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN))
            player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN;

        if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_DEFAULT_PANNING) {
            player_channel->flags &= ~(AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN|AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN);

            if (instrument->flags & AVSEQ_INSTRUMENT_FLAG_SURROUND_PANNING)
                player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;

            player_channel->panning = instrument->default_panning;
            player_channel->sub_pan = instrument->default_sub_pan;
        }

        player_host_channel->pannolo_slide = 0;
        panning                            = player_channel->panning;

        if (instrument->compat_flags & AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN) {
            player_host_channel->track_panning = player_channel->panning;
            player_host_channel->track_sub_pan = player_channel->sub_pan;

            player_host_channel->flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN);

            if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN)
                player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
        }

        panning_separation = ((int16_t) (player_host_channel->instr_note - (1 + instrument->pitch_pan_center)) * (int16_t) instrument->pitch_pan_separation) >> 8;

        volume_swing           = (instrument->panning_swing << 1) + 1;
        avctx->seed            = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
        volume_swing           = ((uint64_t) seed * volume_swing) >> 32;
        volume_swing          -= instrument->volume_swing;
        panning               += volume_swing;

        if ((int32_t) (panning += panning_separation) < 0)
            panning = 0;

        if (panning > 255)
            panning = 255;
    }

    if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_TRACK_PAN)
        player_host_channel->track_panning = panning;
    else
        player_channel->panning            = panning;

    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_AFFECT_CHAN_PAN) {
        player_host_channel->track_panning = panning;
        player_channel->panning            = panning;
    }
}

static void init_new_sample ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel ) {
    AVSequencerSample *sample = player_host_channel->sample;
    AVSequencerSynth *synth;
    AVSequencerMixerData *mixer;
    uint32_t samples;

    if ((samples = sample->samples)) {
        uint8_t flags, repeat_mode, playback_flags;

        player_channel->mixer.len  = samples;
        player_channel->mixer.data = sample->data;
        player_channel->mixer.rate = player_channel->frequency;

        flags = sample->flags;

        if (flags & AVSEQ_SAMPLE_FLAG_SUSTAIN_LOOP) {
            player_channel->mixer.repeat_start  = sample->sustain_repeat;
            player_channel->mixer.repeat_length = sample->sustain_rep_len;
            player_channel->mixer.repeat_count  = sample->sustain_rep_count;
            repeat_mode                         = sample->sustain_repeat_mode;

            flags >>= 1;
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

        if ((flags & AVSEQ_SAMPLE_FLAG_LOOP) && samples) {
            playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_LOOP;

            if (repeat_mode & AVSEQ_SAMPLE_REP_MODE_PINGPONG)
                playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG;

            if (repeat_mode & AVSEQ_SAMPLE_REP_MODE_BACKWARDS)
                playback_flags |= AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS;
        }

        player_channel->mixer.flags = playback_flags;
    }

    if ((!(synth = sample->synth)) || (!player_host_channel->synth) || (!(synth->pos_keep_mask & AVSEQ_SYNTH_POS_KEEP_MASK_CODE)))
        player_host_channel->synth = synth;

    if ((player_channel->synth = player_host_channel->synth)) {
        uint16_t *src_var;
        uint16_t *dst_var;
        uint16_t keep_flags, i;

        player_channel->mixer.flags |= AVSEQ_MIXER_CHANNEL_FLAG_PLAY;

        if ((!(player_host_channel->waveform_list)) || (!(synth->pos_keep_mask & AVSEQ_SYNTH_POS_KEEP_MASK_WAVEFORMS))) {
            AVSequencerSynthWave **waveform_list = synth->waveform_list;
            AVSequencerSynthWave *waveform       = NULL;

            player_host_channel->waveform_list   = waveform_list;
            player_host_channel->waveforms       = synth->waveforms;

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

        if (!(player_channel->use_sustain_flags & AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_VOLUME_KEEP))
            player_host_channel->sustain_pos[0] = synth->sustain_pos[0];

        if (!(player_channel->use_sustain_flags & AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_PANNING_KEEP))
            player_host_channel->sustain_pos[1] = synth->sustain_pos[1];

        if (!(player_channel->use_sustain_flags & AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_SLIDE_KEEP))
            player_host_channel->sustain_pos[2] = synth->sustain_pos[2];

        if (!(player_channel->use_sustain_flags & AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_SPECIAL_KEEP))
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
        src_var    = (uint16_t *) &(synth->variable[0]);
        dst_var    = (uint16_t *) &(player_host_channel->variable[0]);
        i          = 16;

        do {
            if (!(synth->var_keep_mask & keep_flags))
                *dst_var = *src_var;

            keep_flags <<= 1;

            src_var++;
            dst_var++;
        } while (--i);

        player_host_channel->cond_var[0] = synth->cond_var[0];
        player_host_channel->cond_var[1] = synth->cond_var[1];
        player_host_channel->cond_var[2] = synth->cond_var[2];
        player_host_channel->cond_var[3] = synth->cond_var[3];

        memcpy ( &(player_channel->entry_pos[0]), &(player_host_channel->entry_pos[0]), (offsetof (AVSequencerPlayerChannel, use_nna_flags) - offsetof (AVSequencerPlayerChannel, entry_pos[0])));
        memset ( &(player_channel->finetune), 0, (sizeof (AVSequencerPlayerChannel) - offsetof (AVSequencerPlayerChannel, finetune)));
    }

    player_channel->finetune = player_host_channel->finetune;
    mixer                    = avctx->player_mixer_data;
    mixer_set_channel ( mixer, (AVSequencerMixerChannel *) &(player_channel->mixer), player_host_channel->virtual_channel, mixer->mixctx );
}

static AVSequencerPlayerChannel *trigger_nna ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t channel, uint16_t *virtual_channel ) {
    AVSequencerModule *module                    = avctx->player_module;
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

    if (nna_volume || (!(nna = player_host_channel->nna)))
        goto nna_found;

    if (new_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_VOLUME_NNA)
        new_player_channel->entry_pos[0] = new_player_channel->nna_pos[0];

    if (new_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_PANNING_NNA)
        new_player_channel->entry_pos[1] = new_player_channel->nna_pos[1];

    if (new_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_SLIDE_NNA)
        new_player_channel->entry_pos[2] = new_player_channel->nna_pos[2];

    if (new_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_SPECIAL_NNA)
        new_player_channel->entry_pos[3] = new_player_channel->nna_pos[3];

    new_player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_BACKGROUND;

    switch (nna) {
    case AVSEQ_PLAYER_HOST_CHANNEL_NNA_NOTE_OFF :
        play_key_off ( new_player_channel );

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
            if (trigger_dct ( player_host_channel, scan_player_channel, player_host_channel->dct )) {
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
        if (!((scan_player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED) || (scan_player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY)))
        {
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
                if (trigger_dct ( player_host_channel, scan_player_channel, player_host_channel->dct )) {
                    if (scan_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_VOLUME_DNA)
                        scan_player_channel->entry_pos[0] = scan_player_channel->dna_pos[0];

                    if (scan_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_PANNING_DNA)
                        scan_player_channel->entry_pos[1] = scan_player_channel->dna_pos[1];

                    if (scan_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_SLIDE_DNA)
                        scan_player_channel->entry_pos[2] = scan_player_channel->dna_pos[2];

                    if (scan_player_channel->use_nna_flags & AVSEQ_PLAYER_CHANNEL_USE_NNA_FLAGS_SPECIAL_DNA)
                        scan_player_channel->entry_pos[3] = scan_player_channel->dna_pos[3];

                    switch (player_host_channel->dna) {
                    case AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_CUT :
                        player_channel->mixer.flags = 0;

                        break;
                    case AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_OFF :
                        play_key_off ( scan_player_channel );

                        break;
                    case AVSEQ_PLAYER_HOST_CHANNEL_DNA_NOTE_FADE :
                        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;
                    }
                }
            }
        } while (++nna_channel < module->channels);
    }

    player_channel->flags &= ~AVSEQ_PLAYER_CHANNEL_FLAG_ALLOCATED;

    return new_player_channel;
}

static uint32_t trigger_dct ( AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t dct ) {
    uint32_t trigger = 0;

    if ((dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_NOTE_OR) && (player_host_channel->instr_note == player_channel->instr_note))
        trigger++;

    if ((dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_NOTE_OR) && (player_host_channel->sample_note == player_channel->sample_note))
        trigger++;

    if ((dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_OR) && (player_host_channel->instrument == player_channel->instrument))
        trigger++;

    if ((dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_OR) && (player_host_channel->sample == player_channel->sample))
        trigger++;

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_NOTE_AND) {
        if (player_host_channel->instr_note != player_channel->instr_note)
            return 0;

        trigger++;
    }

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_NOTE_AND) {
        if (player_host_channel->sample_note != player_channel->sample_note)
            return 0;

        trigger++;
    }

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_INSTR_AND) {
        if (player_host_channel->instrument != player_channel->instrument)
            return 0;

        trigger++;
    }

    if (dct & AVSEQ_PLAYER_HOST_CHANNEL_DCT_SAMPLE_AND) {
        if (player_host_channel->sample != player_channel->sample)
            return 0;

        trigger++;
    }

    return trigger;
}

static void play_key_off ( AVSequencerPlayerChannel *player_channel ) {
    AVSequencerSample *sample;
    uint32_t repeat, repeat_length, repeat_count;
    uint8_t flags;

    if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SUSTAIN)
        return;

    player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_SUSTAIN;

    set_envelope ( player_channel, (AVSequencerPlayerEnvelope *) &(player_channel->vol_env), player_channel->vol_env.pos );
    set_envelope ( player_channel, (AVSequencerPlayerEnvelope *) &(player_channel->pan_env), player_channel->pan_env.pos );
    set_envelope ( player_channel, (AVSequencerPlayerEnvelope *) &(player_channel->slide_env), player_channel->slide_env.pos );
    set_envelope ( player_channel, (AVSequencerPlayerEnvelope *) &(player_channel->auto_vib_env), player_channel->auto_vib_env.pos );
    set_envelope ( player_channel, (AVSequencerPlayerEnvelope *) &(player_channel->auto_trem_env), player_channel->auto_trem_env.pos );
    set_envelope ( player_channel, (AVSequencerPlayerEnvelope *) &(player_channel->auto_pan_env), player_channel->auto_pan_env.pos );
    set_envelope ( player_channel, (AVSequencerPlayerEnvelope *) &(player_channel->resonance_env), player_channel->resonance_env.pos );

    if ((!player_channel->vol_env.envelope) || (!player_channel->vol_env.tempo) || (player_channel->vol_env.flags & AVSEQ_PLAYER_ENVELOPE_FLAG_LOOPING))
        player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;

    sample = player_channel->sample;

    if (!sample->flags & AVSEQ_SAMPLE_FLAG_SUSTAIN_LOOP)
        return;

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

    if (player_channel->use_sustain_flags & AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_VOLUME)
        player_channel->entry_pos[0] = player_channel->sustain_pos[0];

    if (player_channel->use_sustain_flags & AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_PANNING)
        player_channel->entry_pos[1] = player_channel->sustain_pos[1];

    if (player_channel->use_sustain_flags & AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_SLIDE)
        player_channel->entry_pos[2] = player_channel->sustain_pos[2];

    if (player_channel->use_sustain_flags & AVSEQ_PLAYER_CHANNEL_USE_SUSTAIN_FLAGS_SPECIAL)
        player_channel->entry_pos[3] = player_channel->sustain_pos[3];
}

static void set_envelope ( AVSequencerPlayerChannel *player_channel, AVSequencerPlayerEnvelope *envelope, uint16_t envelope_pos ) {
    AVSequencerEnvelope *instrument_envelope;
    uint8_t envelope_flags;
    uint16_t envelope_loop_start, envelope_loop_end;

    if (!(instrument_envelope = envelope->envelope))
        return;

    envelope_flags      = AVSEQ_PLAYER_ENVELOPE_FLAG_LOOPING;
    envelope_loop_start = envelope->loop_start;
    envelope_loop_end   = envelope->loop_end;

    if ((envelope->rep_flags & AVSEQ_PLAYER_ENVELOPE_REP_FLAG_SUSTAIN) && (!(player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SUSTAIN))) {
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

static int16_t run_envelope ( AVSequencerContext *avctx, AVSequencerPlayerEnvelope *player_envelope, uint16_t tempo_multiplier, uint16_t value_adjustment ) {
    AVSequencerEnvelope *envelope;
    int16_t value = player_envelope->value;

    if ((envelope = player_envelope->envelope)) {
        int16_t *envelope_data = envelope->data;
        uint16_t envelope_pos  = player_envelope->pos;

        if (player_envelope->tempo) {
            uint16_t envelope_count;

            if (!(envelope_count = player_envelope->tempo_count))
                player_envelope->value = value = step_envelope ( avctx, player_envelope, envelope_data, envelope_pos, tempo_multiplier, value_adjustment );

            player_envelope->tempo_count = ++envelope_count;

            if ((player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM) && (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RND_DELAY))
                tempo_multiplier *= player_envelope->tempo;
            else
                tempo_multiplier  = player_envelope->tempo;

            if (envelope_count >= tempo_multiplier)
                player_envelope->tempo_count = 0;
            else if ((player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD) && (!(player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM)))
                value = envelope_data[envelope_pos] + value_adjustment;
        }
    }

    return value;
}

static int16_t step_envelope ( AVSequencerContext *avctx, AVSequencerPlayerEnvelope *player_envelope, int16_t *envelope_data, uint16_t envelope_pos, uint16_t tempo_multiplier, uint16_t value_adjustment ) {
    uint32_t seed, randomize_value;
    uint16_t envelope_restart = player_envelope->start;
    int16_t value;

    value = envelope_data[envelope_pos];

    if (player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM) {
        avctx->seed     = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
        randomize_value = ((uint16_t) player_envelope->value_max - (uint16_t) player_envelope->value_min) + 1;
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

    if ((player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_FIRST_ADD) && (!(player_envelope->flags & AVSEQ_PLAYER_ENVELOPE_FLAG_RANDOM)))
        value = envelope_data[envelope_pos] + value_adjustment;

    return value;
}

ASSIGN_INSTRUMENT_ENVELOPE(volume) {
    if (instrument)
        *envelope = instrument->volume_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_channel->vol_env);

    return player_host_channel->prev_volume_env;
}

ASSIGN_INSTRUMENT_ENVELOPE(panning) {
    if (instrument)
        *envelope = instrument->panning_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_channel->pan_env);

    return player_host_channel->prev_panning_env;
}

ASSIGN_INSTRUMENT_ENVELOPE(slide) {
    if (instrument)
        *envelope = instrument->slide_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_channel->slide_env);

    return player_host_channel->prev_slide_env;
}

ASSIGN_INSTRUMENT_ENVELOPE(vibrato) {
    if (instrument)
        *envelope = instrument->vibrato_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_host_channel->vibrato_env);

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(tremolo) {
    if (instrument)
        *envelope = instrument->tremolo_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_host_channel->tremolo_env);

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(pannolo) {
    if (instrument)
        *envelope = instrument->pannolo_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_host_channel->pannolo_env);

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(channolo) {
    if (instrument)
        *envelope = instrument->channolo_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_host_channel->channolo_env);

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(spenolo) {
    if (instrument)
        *envelope = instrument->spenolo_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(avctx->player_globals->spenolo_env);

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(track_tremolo) {
    if (instrument)
        *envelope = instrument->tremolo_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_host_channel->track_trem_env);

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(track_pannolo) {
    if (instrument)
        *envelope = instrument->pannolo_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_host_channel->track_pan_env);

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(global_tremolo) {
    if (instrument)
        *envelope = instrument->tremolo_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(avctx->player_globals->tremolo_env);

    return (*player_envelope)->envelope;
}

ASSIGN_INSTRUMENT_ENVELOPE(global_pannolo) {
    if (instrument)
        *envelope = instrument->pannolo_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(avctx->player_globals->pannolo_env);

    return (*player_envelope)->envelope;
}

ASSIGN_SAMPLE_ENVELOPE(auto_vibrato) {
    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_channel->auto_vib_env);

    return sample->auto_vibrato_env;
}

ASSIGN_SAMPLE_ENVELOPE(auto_tremolo) {
    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_channel->auto_trem_env);

    return sample->auto_tremolo_env;
}

ASSIGN_SAMPLE_ENVELOPE(auto_pannolo) {
    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_channel->auto_pan_env);

    return sample->auto_pannolo_env;
}

ASSIGN_INSTRUMENT_ENVELOPE(resonance) {
    if (instrument)
        *envelope = instrument->resonance_env;

    *player_envelope = (AVSequencerPlayerEnvelope *) &(player_channel->resonance_env);

    return player_host_channel->prev_resonance_env;
}

USE_ENVELOPE(volume) {
    return (AVSequencerPlayerEnvelope *) &(player_channel->vol_env);
}

USE_ENVELOPE(panning) {
    return (AVSequencerPlayerEnvelope *) &(player_channel->pan_env);
}

USE_ENVELOPE(slide) {
    return (AVSequencerPlayerEnvelope *) &(player_channel->slide_env);
}

USE_ENVELOPE(vibrato) {
    return (AVSequencerPlayerEnvelope *) &(player_host_channel->vibrato_env);
}

USE_ENVELOPE(tremolo) {
    return (AVSequencerPlayerEnvelope *) &(player_host_channel->tremolo_env);
}

USE_ENVELOPE(pannolo) {
    return (AVSequencerPlayerEnvelope *) &(player_host_channel->pannolo_env);
}

USE_ENVELOPE(channolo) {
    return (AVSequencerPlayerEnvelope *) &(player_host_channel->channolo_env);
}

USE_ENVELOPE(spenolo) {
    return (AVSequencerPlayerEnvelope *) &(avctx->player_globals->spenolo_env);
}

USE_ENVELOPE(auto_vibrato) {
    return (AVSequencerPlayerEnvelope *) &(player_channel->auto_vib_env);
}

USE_ENVELOPE(auto_tremolo) {
    return (AVSequencerPlayerEnvelope *) &(player_channel->auto_trem_env);
}

USE_ENVELOPE(auto_pannolo) {
    return (AVSequencerPlayerEnvelope *) &(player_channel->auto_pan_env);
}

USE_ENVELOPE(track_tremolo) {
    return (AVSequencerPlayerEnvelope *) &(player_host_channel->track_trem_env);
}

USE_ENVELOPE(track_pannolo) {
    return (AVSequencerPlayerEnvelope *) &(player_host_channel->track_pan_env);
}

USE_ENVELOPE(global_tremolo) {
    return (AVSequencerPlayerEnvelope *) &(avctx->player_globals->tremolo_env);
}

USE_ENVELOPE(global_pannolo) {
    return (AVSequencerPlayerEnvelope *) &(avctx->player_globals->pannolo_env);
}

USE_ENVELOPE(arpeggio) {
    return (AVSequencerPlayerEnvelope *) &(player_host_channel->arpepggio_env);
}

USE_ENVELOPE(resonance) {
    return (AVSequencerPlayerEnvelope *) &(player_channel->resonance_env);
}

static void run_effects ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel ) {
    AVSequencerSong *song     = avctx->player_song;
    AVSequencerTrack *track;

    if ((track = player_host_channel->track) && player_host_channel->effect) {
        AVSequencerTrackEffect *track_fx;
        AVSequencerTrackData *track_data = track->data + player_host_channel->row;
        uint32_t fx                      = -1;

        while ((++fx < track_data->effects) && ((track_fx = track_data->effects_data[fx]))) {
            AVSequencerPlayerEffects *fx_lut;
            void (*check_func)( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel, uint16_t *fx_byte, uint16_t *data_word, uint16_t *flags );
            uint16_t fx_byte, data_word, flags;
            uint8_t channel_ctrl_type;

            if (player_host_channel->effect == track_fx)
                break;

            fx_byte   = track_fx->command;
            fx_lut    = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            data_word = track_fx->data;
            flags     = fx_lut->flags;

            if ((check_func = fx_lut->check_fx_func)) {
                check_func ( avctx, player_host_channel, player_channel, channel, &fx_byte, &data_word, &flags );

                fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            }

            if (flags & AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW)
                continue;

            flags = player_host_channel->exec_fx;

            if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX))
                flags = fx_lut->std_exec_tick;

            if (flags != player_host_channel->tempo_counter)
                continue;

            if (player_host_channel->effects_used[(fx_byte >> 3)] & (1 << (7 - (fx_byte & 7))))
                continue;

            fx_lut->effect_func ( avctx, player_host_channel, player_channel, channel, fx_byte, data_word );

            if ((channel_ctrl_type = player_host_channel->ch_control_type)) {
                if (player_host_channel->ch_control_affect & fx_lut->and_mask_ctrl) {
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

                            if ((check_func = fx_lut->check_fx_func)) {
                                check_func ( avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags );

                                fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                            }

                            fx_lut->effect_func ( avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word );
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

                                if ((check_func = fx_lut->check_fx_func)) {
                                    check_func ( avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags );

                                    fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                                }

                                fx_lut->effect_func ( avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word );
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

                                if ((check_func = fx_lut->check_fx_func)) {
                                    check_func ( avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags );

                                    fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                                }

                                fx_lut->effect_func ( avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word );
                            }
                        } while (++ctrl_channel < song->channels);

                        break;
                    }
                }
            }
        }

        fx = -1;

        while ((++fx < track_data->effects) && ((track_fx = track_data->effects_data[fx]))) {
            AVSequencerPlayerEffects *fx_lut;
            void (*check_func)( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel, uint16_t *fx_byte, uint16_t *data_word, uint16_t *flags );
            uint16_t fx_byte, data_word, flags;
            uint8_t channel_ctrl_type;

            if (player_host_channel->effect == track_fx)
                break;

            fx_byte   = track_fx->command;
            fx_lut    = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            data_word = track_fx->data;
            flags     = fx_lut->flags;

            if ((check_func = fx_lut->check_fx_func)) {
                check_func ( avctx, player_host_channel, player_channel, channel, &fx_byte, &data_word, &flags );

                fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            }

            if (!(flags & AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW))
                continue;

            flags = player_host_channel->exec_fx;

            if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX))
                flags = fx_lut->std_exec_tick;

            if (player_host_channel->tempo_counter < flags)
                continue;

            if (player_host_channel->effects_used[(fx_byte >> 3)] & (1 << (7 - (fx_byte & 7))))
                continue;

            fx_lut->effect_func ( avctx, player_host_channel, player_channel, channel, fx_byte, data_word );

            if ((channel_ctrl_type = player_host_channel->ch_control_type)) {
                if (player_host_channel->ch_control_affect & fx_lut->and_mask_ctrl) {
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

                            if ((check_func = fx_lut->check_fx_func)) {
                                check_func ( avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags );

                                fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                            }

                            if (new_player_host_channel->effects_used[(ctrl_fx_byte >> 3)] & (1 << (7 - (ctrl_fx_byte & 7))))
                                continue;

                            fx_lut->effect_func ( avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word );
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

                                if ((check_func = fx_lut->check_fx_func)) {
                                    check_func ( avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags );

                                    fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                                }

                                if (new_player_host_channel->effects_used[(ctrl_fx_byte >> 3)] & (1 << (7 - (ctrl_fx_byte & 7))))
                                    continue;

                                fx_lut->effect_func ( avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word );
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

                                if ((check_func = fx_lut->check_fx_func)) {
                                    check_func ( avctx, player_host_channel, player_channel, channel, &ctrl_fx_byte, &ctrl_data_word, &ctrl_flags );

                                    fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + ctrl_fx_byte;
                                }

                                if (new_player_host_channel->effects_used[(ctrl_fx_byte >> 3)] & (1 << (7 - (ctrl_fx_byte & 7))))
                                    continue;

                                fx_lut->effect_func ( avctx, new_player_host_channel, new_player_channel, ctrl_channel, ctrl_fx_byte, ctrl_data_word );
                            }
                        } while (++ctrl_channel < song->channels);

                        break;
                    }
                }
            }
        }
    }
}

static uint32_t linear_slide_up ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint32_t frequency, uint32_t slide_value ) {
    uint32_t linear_slide_div  = slide_value / 3072;
    uint32_t linear_slide_mod  = slide_value % 3072;
    uint32_t linear_multiplier = (0x10000 + (avctx->linear_frequency_lut ? avctx->linear_frequency_lut[linear_slide_mod] : linear_frequency_lut[linear_slide_mod])) << linear_slide_div;
    uint64_t slide_frequency   = ((uint64_t) linear_multiplier * frequency) >> 16;

    if (slide_frequency > 0xFFFFFFFF)
        slide_frequency = -1;

    return (player_channel->frequency = slide_frequency);
}

static uint32_t amiga_slide_up ( AVSequencerPlayerChannel *player_channel, uint32_t frequency, uint32_t slide_value ) {
    uint32_t period = AVSEQ_SLIDE_CONST / frequency;

    if (period <= slide_value)
        return 1;

    period -= slide_value;

    return (player_channel->frequency = (AVSEQ_SLIDE_CONST / period));
}

static uint32_t linear_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint32_t frequency, uint32_t slide_value ) {
    uint32_t linear_slide_div  = slide_value / 3072;
    uint32_t linear_slide_mod  = slide_value % 3072;
    uint32_t linear_multiplier = 0x10000;

    if (!(linear_slide_mod))
        linear_multiplier <<= 1;

    linear_multiplier += (avctx->linear_frequency_lut ? avctx->linear_frequency_lut[-linear_slide_mod + 3072] : linear_frequency_lut[-linear_slide_mod + 3072]);

    return (player_channel->frequency = ((uint64_t) linear_multiplier * frequency) >> (17 + linear_slide_div));
}

static uint32_t amiga_slide_down ( AVSequencerPlayerChannel *player_channel, uint32_t frequency, uint32_t slide_value ) {
    uint32_t period = AVSEQ_SLIDE_CONST / frequency;
    uint32_t new_frequency;

    period += slide_value;

    if (period < slide_value)
        return 0;

    new_frequency = AVSEQ_SLIDE_CONST / period;

    if (frequency == new_frequency) {
        if (!(new_frequency--))
            new_frequency = 0;
    }

    return (player_channel->frequency = new_frequency);
}

static void portamento_slide_up ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t data_word, uint32_t carry_add, uint32_t portamento_shift, uint16_t channel ) {
    if (player_channel->host_channel == channel) {
        uint32_t portamento_slide_value;
        uint8_t portamento_sub_slide_value = data_word;

        if ((portamento_slide_value = (data_word & 0xFFFFFF00) >> portamento_shift)) {
            player_host_channel->sub_slide += portamento_sub_slide_value;

            if (player_host_channel->sub_slide < portamento_sub_slide_value)
                portamento_slide_value += carry_add;

            if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
                linear_slide_up ( avctx, player_channel, player_channel->frequency, portamento_slide_value );
            else
                amiga_slide_up ( player_channel, player_channel->frequency, portamento_slide_value );
        }
    }
}

static void portamento_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t data_word, uint32_t carry_add, uint32_t portamento_shift, uint16_t channel ) {
    if (player_channel->host_channel == channel) {
        uint32_t portamento_slide_value;
        uint8_t portamento_sub_slide_value = data_word;

        if ((int32_t) data_word < 0) {
            portamento_slide_up ( avctx, player_host_channel, player_channel, -data_word, carry_add, portamento_shift, channel );

            return;
        }

        if ((portamento_slide_value = ( data_word & 0xFFFFFF00 ) >> portamento_shift)) {
            if (player_host_channel->sub_slide < portamento_sub_slide_value) {
                portamento_slide_value += carry_add;

                if (portamento_slide_value < carry_add)
                    portamento_slide_value = -1;
            }

            player_host_channel->sub_slide -= portamento_sub_slide_value;

            if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
                linear_slide_down ( avctx, player_channel, player_channel->frequency, portamento_slide_value );
            else
                amiga_slide_down ( player_channel, player_channel->frequency, portamento_slide_value );
        }
    }
}

static void portamento_up_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    AVSequencerTrack *track = player_host_channel->track;
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

static void portamento_down_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word )
{
    AVSequencerTrack *track = player_host_channel->track;
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

static void portamento_up_once_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    AVSequencerTrack *track = player_host_channel->track;
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

static void portamento_down_once_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    AVSequencerTrack *track = player_host_channel->track;
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

static void do_vibrato ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel, uint16_t vibrato_rate, int16_t vibrato_depth ) {
    int32_t vibrato_slide_value;

    if (!vibrato_depth)
        vibrato_depth = player_host_channel->vibrato_depth;

    player_host_channel->vibrato_depth = vibrato_depth;

    vibrato_slide_value = ((-vibrato_depth * run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_host_channel->vibrato_env), vibrato_rate, 0 )) >> (7 - 2)) << 8;

    if (player_channel->host_channel == channel) {
        uint32_t old_frequency     = player_channel->frequency;
        player_channel->frequency -= player_host_channel->vibrato_slide;

        portamento_slide_down ( avctx, player_host_channel, player_channel, vibrato_slide_value, 1, 8, channel );

        player_host_channel->vibrato_slide -= old_frequency - player_channel->frequency;
    }
}

static uint32_t check_old_volume ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint16_t *data_word, uint16_t channel ) {
    AVSequencerSong *song;

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

static void do_volume_slide ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint16_t data_word, uint16_t channel ) {
    if (check_old_volume ( avctx, player_channel, &data_word, channel)) {
        uint16_t slide_volume = (player_channel->volume << 8U) + player_channel->sub_vol;

        if ((slide_volume += data_word) < data_word)
            slide_volume = 0xFFFF;

        player_channel->volume  = slide_volume >> 8;
        player_channel->sub_vol = slide_volume;
    }
}

static void do_volume_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint16_t data_word, uint16_t channel ) {
    if (check_old_volume ( avctx, player_channel, &data_word, channel )) {
        uint16_t slide_volume = (player_channel->volume << 8) + player_channel->sub_vol;

        if (slide_volume < data_word)
            data_word = slide_volume;

        slide_volume -= data_word;

        player_channel->volume  = slide_volume >> 8;
        player_channel->sub_vol = slide_volume;
    }
}

static void volume_slide_up_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    AVSequencerTrack *track = player_host_channel->track;
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

static void volume_slide_down_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    AVSequencerTrack *track = player_host_channel->track;
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

static void fine_volume_slide_up_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    AVSequencerTrack *track = player_host_channel->track;
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

static void fine_volume_slide_down_ok ( AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    AVSequencerTrack *track = player_host_channel->track;
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

static uint32_t check_old_track_volume ( AVSequencerContext *avctx, uint16_t *data_word ) {
    AVSequencerSong *song = avctx->player_song;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (*data_word < 0x4000)
            *data_word = ((*data_word & 0xFF00) << 2) | (*data_word & 0xFF);
        else
            *data_word = 0xFFFF;
    }

    return 1;
}

static void do_track_volume_slide ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    if (check_old_track_volume ( avctx, &data_word )) {
        uint16_t track_volume = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_vol;

        if ((track_volume += data_word) < data_word)
            track_volume = 0xFFFF;

        player_host_channel->track_volume  = track_volume >> 8;
        player_host_channel->track_sub_vol = track_volume;
    }
}

static void do_track_volume_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    if (check_old_track_volume ( avctx, &data_word )) {
        uint16_t track_volume = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_vol;

        if (track_volume < data_word)
            data_word = track_volume;

        track_volume                      -= data_word;
        player_host_channel->track_volume  = track_volume >> 8;
        player_host_channel->track_sub_vol = track_volume;
    }
}

static void do_panning_slide ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t data_word, uint16_t channel ) {
    if (player_channel->host_channel == channel) {
        uint16_t panning = (player_channel->panning << 8) + player_channel->sub_pan;

        if (panning < data_word)
            data_word = panning;

        panning -= data_word;

        player_host_channel->track_panning = player_channel->panning = panning >> 8;
        player_host_channel->track_sub_pan = player_channel->sub_pan = panning;
    } else {
        uint16_t track_panning = (player_host_channel->track_panning << 8) + player_host_channel->track_sub_pan;

        if (track_panning < data_word)
            data_word = track_panning;

        track_panning                     -= data_word;
        player_host_channel->track_panning = track_panning >> 8;
        player_host_channel->track_sub_pan = track_panning;
    }

    player_host_channel->track_note_pan     = player_host_channel->track_panning;
    player_host_channel->track_note_sub_pan = player_host_channel->track_sub_pan;
}

static void do_panning_slide_right ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t data_word, uint16_t channel ) {
    if (player_channel->host_channel == channel) {
        uint16_t panning = (player_channel->panning << 8) + player_channel->sub_pan;

        if ((panning += data_word) < data_word)
            panning = 0xFFFF;

        player_host_channel->track_panning = player_channel->panning = panning >> 8;
        player_host_channel->track_sub_pan = player_channel->sub_pan = panning;
    } else {
        uint16_t track_panning = (player_host_channel->track_panning << 8) + player_host_channel->track_sub_pan;

        if ((track_panning += data_word) < data_word)
            track_panning = 0xFFFF;

        player_host_channel->track_panning = track_panning >> 8U;
        player_host_channel->track_sub_pan = track_panning;
    }

    player_host_channel->track_note_pan     = player_host_channel->track_panning;
    player_host_channel->track_note_sub_pan = player_host_channel->track_sub_pan;
}

static void do_track_panning_slide ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    uint16_t channel_panning = (player_host_channel->channel_panning << 8) + player_host_channel->channel_sub_pan;

    if (channel_panning < data_word)
        data_word = channel_panning;

    channel_panning -= data_word;

    player_host_channel->channel_panning = channel_panning >> 8;
    player_host_channel->channel_sub_pan = channel_panning;
}

static void do_track_panning_slide_right ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, uint16_t data_word ) {
    uint16_t channel_panning = (player_host_channel->channel_panning << 8) + player_host_channel->channel_sub_pan;

    if ((channel_panning += data_word) < data_word)
        channel_panning = 0xFFFF;

    player_host_channel->channel_panning = channel_panning >> 8;
    player_host_channel->channel_sub_pan = channel_panning;
}

static uint32_t check_surround_track_panning ( AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel, uint8_t channel_ctrl_byte ) {
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

static uint16_t *get_speed_address ( AVSequencerContext *avctx, uint16_t speed_type, uint16_t *speed_min_value, uint16_t *speed_max_value ) {
    AVSequencerSong *song                    = avctx->player_song;
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    uint16_t *speed_adr = NULL;

    switch (speed_type & 0x07) {
    case 0x00 :
        *speed_min_value = song->bpm_speed_min;
        *speed_max_value = song->bpm_speed_max;

        speed_adr = (uint16_t *) &(player_globals->bpm_speed);

        break;
    case 0x01 :
        *speed_min_value = song->bpm_tempo_min;
        *speed_max_value = song->bpm_tempo_max;

        speed_adr = (uint16_t *) &(player_globals->bpm_tempo);

        break;
    case 0x02 :
        *speed_min_value = song->spd_min;
        *speed_max_value = song->spd_max;

        speed_adr = (uint16_t *) &(player_globals->spd_speed);

        break;
    case 0x07 :
        *speed_min_value = 1;
        *speed_max_value = 0xFFFF;

        speed_adr = (uint16_t *) &(player_globals->speed_mul);

        break;
    }

    return speed_adr;
}

static void speed_val_ok ( AVSequencerContext *avctx, uint16_t *speed_adr, uint16_t speed_value, uint8_t speed_type, uint16_t speed_min_value, uint16_t speed_max_value ) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;

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
        AVSequencerMixerData *mixer = avctx->player_mixer_data;
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

        mixer_set_tempo ( mixer, tempo, mixer->mixctx );
    }
}

static void do_speed_slide ( AVSequencerContext *avctx, uint16_t data_word ) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    uint16_t *speed_ptr;
    uint16_t speed_min_value, speed_max_value;

    if ((speed_ptr = get_speed_address ( avctx, player_globals->speed_type, &speed_min_value, &speed_max_value ))) {
        uint16_t speed_value;

        if ((player_globals->speed_type & 0x07) == 0x07)
            speed_value = (player_globals->speed_mul << 8) + player_globals->speed_div;
        else
            speed_value = *speed_ptr;

        if ((speed_value += data_word) < data_word)
            speed_value = 0xFFFF;

        speed_val_ok ( avctx, speed_ptr, speed_value, player_globals->speed_type, speed_min_value, speed_max_value );
    }
}

static void do_speed_slide_slower ( AVSequencerContext *avctx, uint16_t data_word )
{
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    uint16_t *speed_ptr;
    uint16_t speed_min_value, speed_max_value;

    if ((speed_ptr = get_speed_address ( avctx, player_globals->speed_type, &speed_min_value, &speed_max_value ))) {
        uint16_t speed_value;

        if ((player_globals->speed_type & 0x07) == 0x07)
            speed_value = (player_globals->speed_mul << 8) + player_globals->speed_div;
        else
            speed_value = *speed_ptr;

        if (speed_value < data_word)
            data_word = speed_value;

        speed_value -= data_word;

        speed_val_ok ( avctx, speed_ptr, speed_value, player_globals->speed_type, speed_min_value, speed_max_value );
    }
}

static void do_global_volume_slide ( AVSequencerContext *avctx, AVSequencerPlayerGlobals *player_globals, uint16_t data_word ) {
    if (check_old_track_volume ( avctx, &data_word )) {
        uint16_t global_volume = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

        if ((global_volume += data_word) < data_word)
            global_volume = 0xFFFF;

        player_globals->global_volume     = global_volume >> 8;
        player_globals->global_sub_volume = global_volume;
    }
}

static void do_global_volume_slide_down ( AVSequencerContext *avctx, AVSequencerPlayerGlobals *player_globals, uint16_t data_word ) {
    if (check_old_track_volume ( avctx, &data_word )) {
        uint16_t global_volume = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

        if (global_volume < data_word)
            data_word = global_volume;

        global_volume -= data_word;

        player_globals->global_volume     = global_volume >> 8;
        player_globals->global_sub_volume = global_volume;
    }
}

static void do_global_panning_slide ( AVSequencerPlayerGlobals *player_globals, uint16_t data_word ) {
    uint16_t global_panning = (player_globals->global_panning << 8) + player_globals->global_sub_panning;

    player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;

    if ((global_panning += data_word) < data_word)
        global_panning = 0xFFFF;

    player_globals->global_panning     = global_panning >> 8;
    player_globals->global_sub_panning = global_panning;
}

static void do_global_panning_slide_right ( AVSequencerPlayerGlobals *player_globals, uint16_t data_word ) {
    uint16_t global_panning = (player_globals->global_panning << 8) + player_globals->global_sub_panning;

    player_globals->flags &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;

    if (global_panning < data_word)
        data_word = global_panning;

    global_panning                    -= data_word;
    player_globals->global_panning     = global_panning >> 8;
    player_globals->global_sub_panning = global_panning;
}

PRESET_EFFECT(tone_portamento) {
    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TONE_PORTA;
}

PRESET_EFFECT(vibrato) {
    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_VIBRATO;
}

PRESET_EFFECT(note_delay) {
    if ((player_host_channel->note_delay = data_word)) {
        if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX)) {
            player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_EXEC_FX;

            player_host_channel->exec_fx = player_host_channel->note_delay;
        }
    }
}

PRESET_EFFECT(tremolo) {
    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOLO;
}

PRESET_EFFECT(set_transpose) {
    player_host_channel->flags         |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SET_TRANSPOSE;
    player_host_channel->transpose      = data_word >> 8;
    player_host_channel->trans_finetune = data_word;
}

CHECK_EFFECT(portamento) {
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA_ONCE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE);

        player_host_channel->fine_slide_flags |= portamento_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_PORTA_UP)];
    } else {
        AVSequencerTrack *track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SLIDES) {
            if ((*fx_byte <= AVSEQ_TRACK_EFFECT_CMD_PORTA_DOWN) && (!(player_host_channel->fine_slide_flags & (AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE))))
                goto trigger_portamento_done;

            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_PORTA_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PORTA)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_PORTA_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PORTA_ONCE)
                *fx_byte += AVSEQ_TRACK_EFFECT_CMD_O_PORTA_UP - AVSEQ_TRACK_EFFECT_CMD_PORTA_UP;
        }

        if ((!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES)) && (*fx_byte > AVSEQ_TRACK_EFFECT_CMD_PORTA_DOWN)) {
            uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= (AVSEQ_TRACK_EFFECT_CMD_PORTA_UP - AVSEQ_TRACK_EFFECT_CMD_ARPEGGIO);
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

CHECK_EFFECT(tone_portamento) {
}

CHECK_EFFECT(note_slide) {
    uint8_t note_slide_type;

    if (!(note_slide_type = *data_word >> 8))
        note_slide_type = player_host_channel->note_slide_type;

    if (note_slide_type & 0xF)
        *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
}

CHECK_EFFECT(volume_slide) {
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE);

        player_host_channel->fine_slide_flags |= volume_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP)];
    } else {
        AVSequencerTrack *track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_VOL_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_VOLSL_UP;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= (AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP - AVSEQ_TRACK_EFFECT_CMD_SET_VOLUME);
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

CHECK_EFFECT(volume_slide_to) {
    if ((*data_word >> 8U) == 0xFF)
        *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
}

CHECK_EFFECT(track_volume_slide) {
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE);

        player_host_channel->fine_slide_flags |= track_volume_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP)];
    } else {
        AVSequencerTrack *track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_VOL_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_TVOL_SL_UP;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= (AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP - AVSEQ_TRACK_EFFECT_CMD_SET_TRK_VOL);
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

CHECK_EFFECT(panning_slide) {
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE);

        player_host_channel->fine_slide_flags |= panning_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT)];
    } else {
        AVSequencerTrack *track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_PAN_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_P_SL_LEFT;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= (AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT - AVSEQ_TRACK_EFFECT_CMD_SET_PANNING);
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

CHECK_EFFECT(track_panning_slide) {
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_TRACK_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRK_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_PAN_SLIDE);

        player_host_channel->fine_slide_flags |= track_panning_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT)];
    } else {
        AVSequencerTrack *track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_TRACK_PAN_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_TP_SL_LEFT;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= (AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT - AVSEQ_TRACK_EFFECT_CMD_SET_TRK_PAN);
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

CHECK_EFFECT(speed_slide) {
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_SPEED_SLIDE_SLOWER|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE_SLOWER|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE);

        player_host_channel->fine_slide_flags |= speed_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST)];
    } else {
        AVSequencerTrack *track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_SPEED_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_S_SLD_FAST;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            uint16_t mask_volume_fx = *fx_byte;

            *fx_byte -= (AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST - AVSEQ_TRACK_EFFECT_CMD_SET_SPEED);
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

CHECK_EFFECT(channel_control) {
}

CHECK_EFFECT(global_volume_slide) {
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_VOL_SLIDE_DOWN|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_VOL_SLIDE);

        player_host_channel->fine_slide_flags |= global_volume_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_UP)];
    } else {
        AVSequencerTrack *track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_UP;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_VOL_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_F_G_VOL_UP;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            uint16_t mask_volume_fx = *fx_byte;

            *fx_byte &= -2;

            if (global_volume_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_UP)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_F_G_VOL_UP)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

CHECK_EFFECT(global_panning_slide) {
    if (*data_word) {
        player_host_channel->fine_slide_flags &= ~(AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_GLOBAL_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOB_PAN_SLIDE_RIGHT|AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_PAN_SLIDE);

        player_host_channel->fine_slide_flags |= global_panning_slide_mask[(*fx_byte - AVSEQ_TRACK_EFFECT_CMD_GPANSL_LEFT)];
    } else {
        AVSequencerTrack *track = player_host_channel->track;

        if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES) {
            *fx_byte = AVSEQ_TRACK_EFFECT_CMD_GPANSL_LEFT;

            if (player_host_channel->fine_slide_flags & AVSEQ_PLAYER_HOST_CHANNEL_FINE_SLIDE_FLAG_FINE_GLOBAL_PAN_SLIDE)
                *fx_byte = AVSEQ_TRACK_EFFECT_CMD_FGP_SL_LEFT;
        }

        if (!(track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES)) {
            uint16_t mask_volume_fx = *fx_byte;

            *fx_byte &= -2;

            if (global_panning_slide_trigger_mask[(mask_volume_fx - AVSEQ_TRACK_EFFECT_CMD_GPANSL_LEFT)] & player_host_channel->fine_slide_flags)
                *fx_byte |= 1;
        }

        *flags |= AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;

        if (*fx_byte >= AVSEQ_TRACK_EFFECT_CMD_FGP_SL_LEFT)
            *flags &= ~AVSEQ_PLAYER_EFFECTS_FLAG_EXEC_WHOLE_ROW;
    }
}

EXECUTE_EFFECT(arpeggio) {
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

        old_frequency = player_channel->frequency;
        frequency     = old_frequency + player_host_channel->arpeggio_freq;
        arpeggio_freq = (avctx->frequency_lut ? avctx->frequency_lut[note + 1] : pitch_lut[note + 1]);

        if ((int16_t) octave < 0) {
            octave          = -octave;
            arpeggio_freq   = ((uint64_t) frequency * arpeggio_freq) >> (16 + octave);
        } else {
            arpeggio_freq <<= octave;
            arpeggio_freq   = ((uint64_t) frequency * arpeggio_freq) >> 16;
        }

        player_host_channel->arpeggio_freq += old_frequency - arpeggio_freq;
        player_channel->frequency           = arpeggio_freq;
    }

    player_host_channel->arpeggio_tick++;
}

EXECUTE_EFFECT(portamento_up) {
    AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->porta_up;

    portamento_slide_up ( avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        volume_slide_up_ok ( player_host_channel, data_word );

    portamento_up_ok ( player_host_channel, data_word );
}

EXECUTE_EFFECT(portamento_down) {
    AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->porta_down;

    portamento_slide_down ( avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        volume_slide_down_ok ( player_host_channel, data_word );

    portamento_down_ok ( player_host_channel, data_word );
}

EXECUTE_EFFECT(fine_portamento_up) {
    AVSequencerTrack *track;
    uint16_t v0, v1, v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->fine_porta_up;

    portamento_slide_up ( avctx, player_host_channel, player_channel, data_word, 1, 8, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        volume_slide_up_ok ( player_host_channel, data_word );

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

EXECUTE_EFFECT(fine_portamento_down) {
    AVSequencerTrack *track;
    uint16_t v0, v1, v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->fine_porta_down;

    portamento_slide_down ( avctx, player_host_channel, player_channel, data_word, 1, 8, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        volume_slide_down_ok ( player_host_channel, data_word );

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

EXECUTE_EFFECT(portamento_up_once) {
    AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->porta_up_once;

    portamento_slide_up ( avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        fine_volume_slide_up_ok ( player_host_channel, data_word );

    portamento_up_once_ok ( player_host_channel, data_word );
}

EXECUTE_EFFECT(portamento_down_once) {
    AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->porta_down_once;

    portamento_slide_down ( avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        fine_volume_slide_down_ok ( player_host_channel, data_word );

    portamento_down_once_ok ( player_host_channel, data_word );
}

EXECUTE_EFFECT(fine_portamento_up_once) {
    AVSequencerTrack *track;
    uint16_t v0, v1, v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->fine_porta_up_once;

    portamento_slide_up ( avctx, player_host_channel, player_channel, data_word, 1, 8, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        fine_volume_slide_up_ok ( player_host_channel, data_word );

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

EXECUTE_EFFECT(fine_portamento_down_once) {
    AVSequencerTrack *track;
    uint16_t v0, v1, v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->fine_porta_down_once;

    portamento_slide_down ( avctx, player_host_channel, player_channel, data_word, 1, 8, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        fine_volume_slide_down_ok ( player_host_channel, data_word );

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

EXECUTE_EFFECT(tone_portamento) {
    uint32_t tone_portamento_target_pitch;

    if (!data_word)
        data_word = player_host_channel->tone_porta;

    if ((tone_portamento_target_pitch = player_host_channel->tone_porta_target_pitch)) {
        AVSequencerTrack *track = player_host_channel->track;
        uint16_t v0, v1, v3;

        if (player_channel->host_channel == channel) {
            if (tone_portamento_target_pitch <= player_channel->frequency) {
                portamento_slide_down ( avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel );

                if (tone_portamento_target_pitch >= player_channel->frequency) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            } else {
                portamento_slide_up ( avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel );

                if ((!(player_channel->frequency)) || (tone_portamento_target_pitch <= player_channel->frequency)) {
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

EXECUTE_EFFECT(fine_tone_portamento) {
    uint32_t tone_portamento_target_pitch;

    if (!data_word)
        data_word = player_host_channel->fine_tone_porta;

    if ((tone_portamento_target_pitch = player_host_channel->tone_porta_target_pitch)) {
        AVSequencerTrack *track = player_host_channel->track;
        uint16_t v0, v1, v3;

        if (player_channel->host_channel == channel) {
            if (tone_portamento_target_pitch <= player_channel->frequency) {
                portamento_slide_down ( avctx, player_host_channel, player_channel, data_word, 1, 8, channel );

                if (tone_portamento_target_pitch >= player_channel->frequency) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            } else {
                portamento_slide_up ( avctx, player_host_channel, player_channel, data_word, 1, 8, channel );

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

EXECUTE_EFFECT(tone_portamento_once) {
    uint32_t tone_portamento_target_pitch;

    if (!data_word)
        data_word = player_host_channel->tone_porta_once;

    if ((tone_portamento_target_pitch = player_host_channel->tone_porta_target_pitch)) {
        AVSequencerTrack *track = player_host_channel->track;
        uint16_t v0, v1, v3;

        if (player_channel->host_channel == channel) {
            if (tone_portamento_target_pitch <= player_channel->frequency) {
                portamento_slide_down ( avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel );

                if (tone_portamento_target_pitch >= player_channel->frequency) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            } else {
                portamento_slide_up ( avctx, player_host_channel, player_channel, data_word, 16, 8-4, channel );

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

EXECUTE_EFFECT(fine_tone_portamento_once) {
    uint32_t tone_portamento_target_pitch;

    if (!data_word)
        data_word = player_host_channel->fine_tone_porta_once;

    if ((tone_portamento_target_pitch = player_host_channel->tone_porta_target_pitch)) {
        AVSequencerTrack *track = player_host_channel->track;
        uint16_t v0, v1, v3;

        if (player_channel->host_channel == channel) {
            if (tone_portamento_target_pitch <= player_channel->frequency) {
                portamento_slide_down ( avctx, player_host_channel, player_channel, data_word, 1, 8, channel );

                if (tone_portamento_target_pitch >= player_channel->frequency) {
                    player_channel->frequency                    = tone_portamento_target_pitch;
                    player_host_channel->tone_porta_target_pitch = 0;
                }
            } else {
                portamento_slide_up ( avctx, player_host_channel, player_channel, data_word, 1, 8, channel );

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

EXECUTE_EFFECT(note_slide) {
    uint16_t note_slide_value, note_slide_type;

    if (!(note_slide_value = data_word & 0xFF))
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
        player_channel->frequency = get_tone_pitch ( avctx, player_host_channel, player_channel, note_slide_value );
}

EXECUTE_EFFECT(vibrato) {
    uint16_t vibrato_rate;
    int8_t vibrato_depth;

    if (!(vibrato_rate = ( data_word >> 8)))
        vibrato_rate = player_host_channel->vibrato_rate;

    player_host_channel->vibrato_rate = vibrato_rate;
    vibrato_depth                     = data_word;

    do_vibrato ( avctx, player_host_channel, player_channel, channel, vibrato_rate, ((int16_t) vibrato_depth << 2) );
}

EXECUTE_EFFECT(fine_vibrato) {
    uint16_t vibrato_rate;

    if (!(vibrato_rate = (data_word >> 8)))
        vibrato_rate = player_host_channel->vibrato_rate;

    player_host_channel->vibrato_rate = vibrato_rate;

    do_vibrato ( avctx, player_host_channel, player_channel, channel, vibrato_rate, (int8_t) data_word );
}

EXECUTE_EFFECT(do_key_off) {
    if (data_word >= player_host_channel->tempo_counter)
        play_key_off ( player_channel );
}

EXECUTE_EFFECT(hold_delay) {

}

EXECUTE_EFFECT(note_fade) {
    if (data_word >= player_host_channel->tempo_counter) {
        player_host_channel->effects_used[(fx_byte >> 3)] |= (1 << (7 - (fx_byte & 7)));

        if (player_channel->host_channel == channel)
            player_channel->flags |= AVSEQ_PLAYER_CHANNEL_FLAG_FADING;
    }
}

EXECUTE_EFFECT(note_cut) {
    if ((data_word & 0xFFF) >= player_host_channel->tempo_counter) {
        player_host_channel->effects_used[(fx_byte >> 3)] |= (1 << (7 - (fx_byte & 7)));

        if (player_channel->host_channel == channel) {
            player_channel->volume  = 0;
            player_channel->sub_vol = 0;

            if (data_word & 0xF000) {
                player_host_channel->instrument = NULL;
                player_host_channel->sample     = NULL;
                player_channel->flags           = 0;
            }
        }
    }
}

EXECUTE_EFFECT(note_delay) {
}

EXECUTE_EFFECT(tremor) {
    uint8_t tremor_off, tremor_on;

    player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TREMOR_EXEC;

    if (!(tremor_off = data_word))
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

EXECUTE_EFFECT(note_retrigger) {
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

EXECUTE_EFFECT(multi_retrigger_note) {
    uint8_t multi_retrigger_tick, multi_retrigger_volume_change;
    uint16_t retrigger_tick_count;
    uint32_t volume;

    if (!(multi_retrigger_tick = ( data_word >> 8)))
        multi_retrigger_tick = player_host_channel->multi_retrig_tick;

    player_host_channel->multi_retrig_tick = multi_retrigger_tick;

    if (!(multi_retrigger_volume_change = data_word))
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
                player_channel->volume  = 0;
                player_channel->sub_vol = 0;
            }
        } else {
            volume = ((multi_retrigger_volume_change + 0x40) * multi_retrigger_scale) + player_channel->volume;

            if (volume < 0x100) {
                player_channel->volume  = volume;
            } else {
                player_channel->volume  = 0xFF;
                player_channel->sub_vol = 0xFF;
            }
        }
    } else {
        uint8_t volume_multiplier, volume_divider;

        volume = (player_channel->volume << 8);

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SUB_SLIDE_RETRIG)
            volume += player_channel->sub_vol;

        if ((volume_multiplier = (multi_retrigger_volume_change >> 4)))
            volume *= volume_multiplier;

        if ((volume_divider = (multi_retrigger_volume_change & 0xF)))
            volume /= volume_divider;

        if (volume > 0xFFFF)
            volume = 0xFFFF;

        player_channel->volume = volume >> 8;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SUB_SLIDE_RETRIG)
            player_channel->sub_vol = volume;
    }

    if ((retrigger_tick_count = player_host_channel->retrig_tick_count) && --multi_retrigger_tick) {
        if (multi_retrigger_tick <= retrigger_tick_count)
            retrigger_tick_count = -1;
    } else if (player_channel->host_channel == channel) {
        player_channel->mixer.pos = 0;
    }

    player_host_channel->retrig_tick_count = ++retrigger_tick_count;
}

EXECUTE_EFFECT(extended_ctrl) {
    AVSequencerModule *module;
    AVSequencerPlayerChannel *scan_player_channel;
    uint8_t extended_control_byte;
    uint16_t virtual_channel, extended_control_word = data_word & 0x0FFF;

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
                scan_player_channel->flags = 0;

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
                play_key_off ( scan_player_channel );

            scan_player_channel++;
        } while (++virtual_channel < module->channels);

        break;
    case 6 :
        player_host_channel->sub_slide = extended_control_word;

        break;
    }
}

EXECUTE_EFFECT(invert_loop) {
}

EXECUTE_EFFECT(exec_fx) {
}

EXECUTE_EFFECT(stop_fx) {
    uint8_t stop_fx = data_word;

    if ((int8_t) stop_fx < 0)
        stop_fx = 127;

    if (!(data_word >>= 8))
        data_word = player_host_channel->exec_fx;

    if (data_word >= player_host_channel->tempo_counter)
        player_host_channel->effects_used[(stop_fx >> 3)] |= (1 << (7 - (stop_fx & 7)));
}

EXECUTE_EFFECT(set_volume) {
    player_host_channel->tremolo_slide = 0;

    if (check_old_volume ( avctx, player_channel, &data_word, channel )) {
        player_channel->volume  = data_word >> 8;
        player_channel->sub_vol = data_word;
    }
}

EXECUTE_EFFECT(volume_slide_up) {
    AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->vol_slide_up;

    do_volume_slide ( avctx, player_channel, data_word, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        portamento_up_ok ( player_host_channel, data_word );

    volume_slide_up_ok ( player_host_channel, data_word );
}

EXECUTE_EFFECT(volume_slide_down) {
    AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->vol_slide_down;

    do_volume_slide_down ( avctx, player_channel, data_word, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        portamento_down_ok ( player_host_channel, data_word );

    volume_slide_down_ok ( player_host_channel, data_word );
}

EXECUTE_EFFECT(fine_volume_slide_up) {
    AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->fine_vol_slide_up;

    do_volume_slide ( avctx, player_channel, data_word, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        portamento_up_once_ok ( player_host_channel, data_word );

    fine_volume_slide_up_ok ( player_host_channel, data_word );
}

EXECUTE_EFFECT(fine_volume_slide_down) {
    AVSequencerTrack *track;

    if (!data_word)
        data_word = player_host_channel->vol_slide_down;

    do_volume_slide_down ( avctx, player_channel, data_word, channel );

    track = player_host_channel->track;

    if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH)
        portamento_down_once_ok ( player_host_channel, data_word );

    fine_volume_slide_down_ok ( player_host_channel, data_word );
}

EXECUTE_EFFECT(volume_slide_to) {
    uint8_t volume_slide_to_value, volume_slide_to_volume;

    if (!(volume_slide_to_value = data_word))
        volume_slide_to_value = player_host_channel->volume_slide_to;

    player_host_channel->volume_slide_to        = volume_slide_to_value;
    player_host_channel->volume_slide_to_slide &= 0x00FF;
    player_host_channel->volume_slide_to_slide += volume_slide_to_value << 8;
    volume_slide_to_volume                      = data_word >> 8;

    if (volume_slide_to_volume && (volume_slide_to_volume < 0xFF)) {
        player_host_channel->volume_slide_to_volume = volume_slide_to_volume;
    } else if (volume_slide_to_volume && (player_channel->host_channel == channel)) {
        uint16_t volume_slide_target = (volume_slide_to_volume << 8) + player_host_channel->volume_slide_to_volume;
        uint16_t volume              = (player_channel->volume << 8) + player_channel->sub_vol;

        if (volume < volume_slide_target) {
            do_volume_slide ( avctx, player_channel, player_host_channel->volume_slide_to_slide, channel );

            volume = (player_channel->volume << 8) + player_channel->sub_vol;

            if (volume_slide_target <= volume) {
                player_channel->volume  = volume_slide_target >> 8;
                player_channel->sub_vol = volume_slide_target;
            }
        } else {
            do_volume_slide_down ( avctx, player_channel, player_host_channel->volume_slide_to_slide, channel );

            volume = (player_channel->volume << 8) + player_channel->sub_vol;

            if (volume_slide_target >= volume) {
                player_channel->volume  = volume_slide_target >> 8;
                player_channel->sub_vol = volume_slide_target;
            }
        }
    }
}

EXECUTE_EFFECT(tremolo) {
    AVSequencerSong *song = avctx->player_song;
    int32_t tremolo_slide_value;
    uint8_t tremolo_rate;
    int16_t tremolo_depth;

    if (!(tremolo_rate = (data_word >> 8)))
        tremolo_rate = player_host_channel->tremolo_rate;

    player_host_channel->tremolo_rate = tremolo_rate;

    if (!(tremolo_depth = data_word))
        tremolo_depth = player_host_channel->tremolo_depth;

    player_host_channel->tremolo_depth = tremolo_depth;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (tremolo_depth > 63)
            tremolo_depth = 63;

        if (tremolo_depth < -63)
            tremolo_depth = -63;
    }

    tremolo_slide_value = (-tremolo_depth * run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_host_channel->tremolo_env), tremolo_rate, 0 )) >> 7;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES)
        tremolo_slide_value <<= 2;

    if (player_channel->host_channel == channel) {
        uint16_t volume = player_channel->volume;

        tremolo_slide_value -= player_host_channel->tremolo_slide;

        if ((tremolo_slide_value += volume) < 0)
            tremolo_slide_value = 0;

        if (tremolo_slide_value > 255)
            tremolo_slide_value = 255;

        player_channel->volume              = tremolo_slide_value;
        player_host_channel->tremolo_slide -= tremolo_slide_value - volume;
    }
}

EXECUTE_EFFECT(set_track_volume) {
    if (check_old_track_volume ( avctx, &data_word )) {
        player_host_channel->track_volume  = data_word >> 8;
        player_host_channel->track_sub_vol = data_word;
    }
}

EXECUTE_EFFECT(track_volume_slide_up) {
    AVSequencerTrack *track;
    uint16_t v3, v4, v5;

    if (!data_word)
        data_word = player_host_channel->track_vol_slide_up;

    do_track_volume_slide ( avctx, player_host_channel, data_word );

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

EXECUTE_EFFECT(track_volume_slide_down) {
    AVSequencerTrack *track;
    uint16_t v0, v3, v4;

    if (!data_word)
        data_word = player_host_channel->track_vol_slide_down;

    do_track_volume_slide_down ( avctx, player_host_channel, data_word );

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

EXECUTE_EFFECT(fine_track_volume_slide_up) {
    AVSequencerTrack *track;
    uint16_t v0, v1, v4;

    if (!data_word)
        data_word = player_host_channel->fine_trk_vol_slide_up;

    do_track_volume_slide ( avctx, player_host_channel, data_word );

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

EXECUTE_EFFECT(fine_track_volume_slide_down) {
    AVSequencerTrack *track;
    uint16_t v0, v1, v3;

    if (!data_word)
        data_word = player_host_channel->fine_trk_vol_slide_dn;

    do_track_volume_slide_down ( avctx, player_host_channel, data_word );

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

EXECUTE_EFFECT(track_volume_slide_to) {
    uint8_t track_vol_slide_to, track_volume_slide_to_volume;

    if (!(track_vol_slide_to = data_word))
        track_vol_slide_to = player_host_channel->track_vol_slide_to;

    player_host_channel->track_vol_slide_to        = track_vol_slide_to;
    player_host_channel->track_vol_slide_to_slide &= 0x00FF;
    player_host_channel->track_vol_slide_to_slide += track_vol_slide_to << 8;
    track_volume_slide_to_volume                   = data_word >> 8;

    if (track_volume_slide_to_volume && (track_volume_slide_to_volume < 0xFF)) {
        player_host_channel->track_vol_slide_to = track_volume_slide_to_volume;
    } else if (track_volume_slide_to_volume) {
        uint16_t track_volume_slide_target = (track_volume_slide_to_volume << 8) + player_host_channel->track_vol_slide_to_sub_vol;
        uint16_t track_volume              = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_vol;

        if (track_volume < track_volume_slide_target) {
            do_track_volume_slide ( avctx, player_host_channel, player_host_channel->track_vol_slide_to_slide );

            track_volume = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_vol;

            if (track_volume_slide_target <= track_volume) {
                player_host_channel->track_volume  = track_volume_slide_target >> 8;
                player_host_channel->track_sub_vol = track_volume_slide_target;
            }
        } else {
            do_track_volume_slide_down ( avctx, player_host_channel, player_host_channel->track_vol_slide_to_slide );

            track_volume = (player_host_channel->track_volume << 8) + player_host_channel->track_sub_vol;

            if (track_volume_slide_target >= track_volume) {
                player_host_channel->track_volume  = track_volume_slide_target >> 8;
                player_host_channel->track_sub_vol = track_volume_slide_target;
            }
        }
    }
}

EXECUTE_EFFECT(track_tremolo) {
    AVSequencerSong *song = avctx->player_song;
    int32_t track_tremolo_slide_value;
    uint8_t track_tremolo_rate;
    int16_t track_tremolo_depth;
    uint16_t track_volume;

    if (!(track_tremolo_rate = (data_word >> 8)))
        track_tremolo_rate = player_host_channel->track_trem_rate;

    player_host_channel->track_trem_rate = track_tremolo_rate;

    if (!(track_tremolo_depth = data_word))
        track_tremolo_depth = player_host_channel->track_trem_depth;

    player_host_channel->track_trem_depth = track_tremolo_depth;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (track_tremolo_depth > 63)
            track_tremolo_depth = 63;

        if (track_tremolo_depth < -63)
            track_tremolo_depth = -63;
    }

    track_tremolo_slide_value = (-track_tremolo_depth * run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_host_channel->track_trem_env), track_tremolo_rate, 0 )) >> 7;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES)
        track_tremolo_slide_value <<= 2;

    track_volume               = player_host_channel->track_volume;
    track_tremolo_slide_value -= player_host_channel->track_trem_slide;

    if ((track_tremolo_slide_value += track_volume) < 0)
        track_tremolo_slide_value = 0;

    if (track_tremolo_slide_value > 255)
        track_tremolo_slide_value = 255;

    player_host_channel->track_volume      = track_tremolo_slide_value;
    player_host_channel->track_trem_slide -= track_tremolo_slide_value - track_volume;
}

EXECUTE_EFFECT(set_panning) {
    uint8_t panning = data_word >> 8U;

    if (player_channel->host_channel == channel) {
        player_channel->panning = panning;
        player_channel->sub_pan = data_word;
    }

    player_host_channel->flags             &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
    player_host_channel->track_panning      = panning;
    player_host_channel->track_sub_pan      = data_word;
    player_host_channel->track_note_pan     = panning;
    player_host_channel->track_note_sub_pan = data_word;
}

EXECUTE_EFFECT(panning_slide_left) {
    AVSequencerTrack *track;
    uint16_t v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->pan_slide_left;

    do_panning_slide ( avctx, player_host_channel, player_channel, data_word, channel );

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
    AVSequencerTrack *track;
    uint16_t v0, v3, v4, v5;

    if (!data_word)
        data_word = player_host_channel->pan_slide_right;

    do_panning_slide_right ( avctx, player_host_channel, player_channel, data_word, channel );

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

EXECUTE_EFFECT(fine_panning_slide_left) {
    AVSequencerTrack *track;
    uint16_t v0, v1, v4, v5;

    if (!data_word)
        data_word = player_host_channel->fine_pan_slide_left;

    do_panning_slide ( avctx, player_host_channel, player_channel, data_word, channel );

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

EXECUTE_EFFECT(fine_panning_slide_right) {
    AVSequencerTrack *track;
    uint16_t v0, v1, v3, v5;

    if (!data_word)
        data_word = player_host_channel->fine_pan_slide_right;

    do_panning_slide_right ( avctx, player_host_channel, player_channel, data_word, channel );

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

EXECUTE_EFFECT(panning_slide_to) {
    uint8_t panning_slide_to_value, panning_slide_to_panning;

    if (!(panning_slide_to_value = data_word))
        panning_slide_to_value   = player_host_channel->panning_slide_to;

    player_host_channel->panning_slide_to        = panning_slide_to_value;
    player_host_channel->panning_slide_to_slide &= 0x00FF;
    player_host_channel->panning_slide_to_slide += panning_slide_to_value << 8;
    panning_slide_to_panning                     = data_word >> 8;

    if (panning_slide_to_panning && (panning_slide_to_panning < 0xFF)) {
        player_host_channel->panning_slide_to_panning = panning_slide_to_panning;
    } else if (panning_slide_to_panning && (player_channel->host_channel == channel)) {
        uint16_t panning_slide_target = (panning_slide_to_panning << 8) + player_host_channel->panning_slide_to_sub_pan;
        uint16_t panning              = (player_channel->panning << 8) + player_channel->sub_pan;

        if (panning < panning_slide_target) {
            do_panning_slide_right ( avctx, player_host_channel, player_channel, player_host_channel->panning_slide_to_slide, channel );

            panning = (player_channel->panning << 8) + player_channel->sub_pan;

            if (panning_slide_target <= panning) {
                player_channel->panning = panning_slide_target >> 8;
                player_channel->sub_pan = panning_slide_target;

                player_host_channel->panning_slide_to_panning = 0;
                player_host_channel->track_note_pan           = player_host_channel->track_panning = player_host_channel->panning_slide_to_slide >> 8;
                player_host_channel->track_note_sub_pan       = player_host_channel->track_sub_pan = player_host_channel->panning_slide_to_slide;
            }
        } else {
            do_panning_slide ( avctx, player_host_channel, player_channel, player_host_channel->panning_slide_to_slide, channel );

            panning = (player_channel->panning << 8) + player_channel->sub_pan;

            if (panning_slide_target >= panning) {
                player_channel->panning = panning_slide_target >> 8;
                player_channel->sub_pan = panning_slide_target;

                player_host_channel->panning_slide_to_panning = 0;
                player_host_channel->track_note_pan           = player_host_channel->track_panning = player_host_channel->panning_slide_to_slide >> 8;
                player_host_channel->track_note_sub_pan       = player_host_channel->track_sub_pan = player_host_channel->panning_slide_to_slide;
            }
        }
    }
}

EXECUTE_EFFECT(pannolo) {
    int32_t pannolo_slide_value;
    uint8_t pannolo_rate;
    int16_t pannolo_depth;

    if (!(pannolo_rate = (data_word >> 8)))
        pannolo_rate = player_host_channel->pannolo_rate;

    player_host_channel->pannolo_rate = pannolo_rate;

    if (!(pannolo_depth = data_word))
        pannolo_depth = player_host_channel->pannolo_depth;

    player_host_channel->pannolo_depth = pannolo_depth;

    pannolo_slide_value = (-pannolo_depth * run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_host_channel->pannolo_env), pannolo_rate, 0 )) >> 7;

    if (player_channel->host_channel == channel) {
        uint16_t panning     = player_channel->panning;
        pannolo_slide_value -= player_host_channel->pannolo_slide;

        if ((pannolo_slide_value += panning) < 0)
            pannolo_slide_value = 0;

        if (pannolo_slide_value > 255)
            pannolo_slide_value = 255;

        player_channel->panning                 = pannolo_slide_value;
        player_host_channel->pannolo_slide     -= pannolo_slide_value - panning;
        player_host_channel->track_note_pan     = player_host_channel->track_panning = panning;
        player_host_channel->track_note_sub_pan = player_host_channel->track_sub_pan = player_channel->sub_pan;
    }
}

EXECUTE_EFFECT(set_track_panning) {
    player_host_channel->channel_panning = data_word >> 8;
    player_host_channel->channel_sub_pan = data_word;

    player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHANNEL_SUR_PAN;
}

EXECUTE_EFFECT(track_panning_slide_left) {
    AVSequencerTrack *track;
    uint16_t v3, v4, v5, v8;

    if (!data_word)
        data_word = player_host_channel->track_pan_slide_left;

    do_track_panning_slide ( avctx, player_host_channel, data_word );

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

EXECUTE_EFFECT(track_panning_slide_right) {
    AVSequencerTrack *track;
    uint16_t v0, v3, v4, v5;

    if (!data_word)
        data_word = player_host_channel->track_pan_slide_right;

    do_track_panning_slide_right ( avctx, player_host_channel, data_word );

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
    AVSequencerTrack *track;
    uint16_t v0, v1, v4, v5;

    if (!data_word)
        data_word = player_host_channel->fine_trk_pan_sld_left;

    do_track_panning_slide ( avctx, player_host_channel, data_word );

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
    AVSequencerTrack *track;
    uint16_t v0, v1, v3, v5;

    if (!data_word)
        data_word = player_host_channel->fine_trk_pan_sld_right;

    do_track_panning_slide_right ( avctx, player_host_channel, data_word );

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

EXECUTE_EFFECT(track_panning_slide_to) {
    uint8_t track_pan_slide_to, track_panning_slide_to_panning;

    if (!(track_pan_slide_to = data_word))
        track_pan_slide_to = player_host_channel->track_pan_slide_to;

    player_host_channel->track_pan_slide_to        = track_pan_slide_to;
    player_host_channel->track_pan_slide_to_slide &= 0x00FF;
    player_host_channel->track_pan_slide_to_slide += track_pan_slide_to << 8;

    track_panning_slide_to_panning = data_word >> 8;

    if (track_panning_slide_to_panning && (track_panning_slide_to_panning < 0xFF)) {
        player_host_channel->track_pan_slide_to_panning = track_panning_slide_to_panning;
    } else if (track_panning_slide_to_panning) {
        uint16_t track_panning_slide_to_target = (track_panning_slide_to_panning << 8) + player_host_channel->track_pan_slide_to_sub_pan;
        uint16_t track_panning = (player_host_channel->track_panning << 8) + player_host_channel->track_sub_pan;

        if (track_panning < track_panning_slide_to_target) {
            do_track_panning_slide_right ( avctx, player_host_channel, player_host_channel->track_pan_slide_to_slide );

            track_panning = (player_host_channel->track_panning << 8) + player_host_channel->track_sub_pan;

            if (track_panning_slide_to_target <= track_panning) {
                player_host_channel->track_panning = track_panning_slide_to_target >> 8;
                player_host_channel->track_sub_pan = track_panning_slide_to_target;
            }
        } else {
            do_track_panning_slide ( avctx, player_host_channel, player_host_channel->track_pan_slide_to_slide );

            track_panning = (player_host_channel->track_panning << 8) + player_host_channel->track_sub_pan;

            if (track_panning_slide_to_target >= track_panning) {
                player_host_channel->track_panning = track_panning_slide_to_target >> 8;
                player_host_channel->track_sub_pan = track_panning_slide_to_target;
            }
        }
    }
}

EXECUTE_EFFECT(track_pannolo) {
    int32_t track_pannolo_slide_value;
    uint8_t track_pannolo_rate;
    int16_t track_pannolo_depth;
    uint16_t track_panning;

    if (!(track_pannolo_rate = (data_word >> 8)))
        track_pannolo_rate = player_host_channel->track_pan_rate;

    player_host_channel->track_pan_rate = track_pannolo_rate;

    if (!(track_pannolo_depth = data_word))
        track_pannolo_depth = player_host_channel->track_pan_depth;

    player_host_channel->track_pan_depth = track_pannolo_depth;
    track_pannolo_slide_value            = (-track_pannolo_depth * run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_host_channel->track_pan_env), track_pannolo_rate, 0 )) >> 7;

    track_panning              = player_host_channel->track_panning;
    track_pannolo_slide_value -= player_host_channel->track_pan_slide;

    if ((track_pannolo_slide_value += track_panning) < 0)
        track_pannolo_slide_value = 0;

    if (track_pannolo_slide_value > 255)
        track_pannolo_slide_value = 255;

    player_host_channel->track_panning    = track_pannolo_slide_value;
    player_host_channel->track_pan_slide -= track_pannolo_slide_value - track_panning;
}

EXECUTE_EFFECT(set_tempo) {
    if (!(player_host_channel->tempo = data_word))
        player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;
}

EXECUTE_EFFECT(set_relative_tempo) {
    if (!(player_host_channel->tempo += data_word))
        player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SONG_END;
}

EXECUTE_EFFECT(pattern_break) {
    if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP)) {
        player_host_channel->flags    |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_BREAK;
        player_host_channel->break_row = data_word;
    }
}

EXECUTE_EFFECT(position_jump) {
    if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_PATTERN_LOOP)) {
        AVSequencerOrderData *order_data;

        if (data_word--) {
            AVSequencerOrderList *order_list = avctx->player_song->order_list + channel;

            if ((data_word < order_list->orders) && order_list->order_data[data_word])
                order_data = order_list->order_data[data_word];
        } else {
            order_data = NULL;
        }

        player_host_channel->order = order_data;

        pattern_break ( avctx, player_host_channel, player_channel, channel, AVSEQ_TRACK_EFFECT_CMD_PATT_BREAK, 0 );
    }
}

EXECUTE_EFFECT(relative_position_jump) {
    if (!data_word)
        data_word = player_host_channel->pos_jump;

    if ((player_host_channel->pos_jump = data_word)) {
        AVSequencerOrderList *order_list = avctx->player_song->order_list + channel;
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

        position_jump ( avctx, player_host_channel, player_channel, channel, AVSEQ_TRACK_EFFECT_CMD_POS_JUMP, (uint16_t) ord );
    }
}

EXECUTE_EFFECT(change_pattern) {
    player_host_channel->chg_pattern = data_word;
    player_host_channel->flags      |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_CHG_PATTERN;
}

EXECUTE_EFFECT(reverse_pattern_play) {
    if (!data_word)
        player_host_channel->flags ^= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS;
    else if (data_word & 0x8000)
        player_host_channel->flags |= AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS;
    else
        player_host_channel->flags &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_BACKWARDS;
}

EXECUTE_EFFECT(pattern_delay) {
    player_host_channel->pattern_delay = data_word;
}

EXECUTE_EFFECT(fine_pattern_delay) {
    player_host_channel->fine_pattern_delay = data_word;
}

EXECUTE_EFFECT(pattern_loop) {
    AVSequencerSong *song = avctx->player_song;

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

EXECUTE_EFFECT(gosub) {
}

EXECUTE_EFFECT(gosub_return) {
}

EXECUTE_EFFECT(channel_sync) {
}

EXECUTE_EFFECT(set_sub_slides) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    uint8_t sub_slide_flags;

    if (!(sub_slide_flags = ( data_word >> 8)))
        sub_slide_flags = player_host_channel->sub_slide_bits;

    if (sub_slide_flags & 0x01)
        player_host_channel->volume_slide_to_volume     = data_word;

    if (sub_slide_flags & 0x02)
        player_host_channel->track_vol_slide_to_sub_vol = data_word;

    if (sub_slide_flags & 0x04)
        player_globals->global_volume_sl_to_sub_vol     = data_word;

    if (sub_slide_flags & 0x08)
        player_host_channel->panning_slide_to_sub_pan   = data_word;

    if (sub_slide_flags & 0x10)
        player_host_channel->track_pan_slide_to_sub_pan = data_word;

    if (sub_slide_flags & 0x20)
        player_globals->global_pan_slide_to_sub_pan     = data_word;
}

EXECUTE_EFFECT(sample_offset_high) {
    player_host_channel->smp_offset_hi = data_word;
}

EXECUTE_EFFECT(sample_offset_low) {
    if (player_channel->host_channel == channel) {
        uint32_t sample_offset = (player_host_channel->smp_offset_hi << 16) + data_word;

        if (!(player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_SMP_OFFSET_REL)) {
            AVSequencerTrack *track = player_host_channel->track;

            if (track->compat_flags & AVSEQ_TRACK_COMPAT_FLAG_SAMPLE_OFFSET) {
                AVSequencerSample *sample = player_channel->sample;

                if (sample_offset >= sample->samples)
                    return;
            }

            player_channel->mixer.pos = 0;

            if (player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_LOOP) {
                uint32_t repeat_end = player_channel->mixer.repeat_start + player_channel->mixer.repeat_length;

                if (repeat_end < sample_offset)
                    sample_offset = repeat_end;
            }
        }

        player_channel->mixer.pos += sample_offset;
    }
}

EXECUTE_EFFECT(set_hold) {
}

EXECUTE_EFFECT(set_decay) {
}

EXECUTE_EFFECT(set_transpose) {
}

EXECUTE_EFFECT(instrument_ctrl) {
}

EXECUTE_EFFECT(instrument_change) {
}

EXECUTE_EFFECT(synth_ctrl) {
    player_host_channel->synth_ctrl_count  = data_word >> 8;
    player_host_channel->synth_ctrl_change = data_word;

    if (data_word & 0x80)
        set_synth_value ( avctx, player_host_channel, player_channel, channel, AVSEQ_TRACK_EFFECT_CMD_SET_SYN_VAL, player_host_channel->synth_ctrl );
}

EXECUTE_EFFECT(set_synth_value) {
    AVSequencerSynthWave **waveform;
    uint16_t *synth_change;
    uint8_t synth_ctrl_count   = player_host_channel->synth_ctrl_count;
    uint16_t synth_ctrl_change = player_host_channel->synth_ctrl_change & 0x7F;

    player_host_channel->synth_ctrl = data_word;
    waveform                        = (AVSequencerSynthWave **) &(player_channel->sample_waveform) + (synth_ctrl_change - 0x24);
    synth_change                    = (uint16_t *) &(player_channel->entry_pos[0]) + synth_ctrl_change;

    do {
        if (synth_ctrl_change < 0x24) {
            *synth_change++ = data_word;
        } else if (synth_ctrl_change <= 0x28) {
            if (data_word >= player_channel->synth->waveforms)
                break;

            *waveform++ = player_channel->waveform_list[data_word];
        }
    } while (synth_ctrl_count--);
}

EXECUTE_EFFECT(envelope_ctrl) {
    uint8_t envelope_ctrl_kind = (data_word >> 8) & 0x7F;

    if (envelope_ctrl_kind <= 0x10) {
        uint8_t envelope_ctrl_type = data_word;

        player_host_channel->env_ctrl_kind = envelope_ctrl_kind;

        if (envelope_ctrl_type <= 0x32) {
            AVSequencerEnvelope *instrument_envelope;
            AVSequencerPlayerEnvelope *envelope;
            AVSequencerPlayerEnvelope *(*envelope_get_kind)( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel );

            envelope_get_kind = (const void *const) envelope_ctrl_type_lut[envelope_ctrl_kind];
            envelope          = envelope_get_kind ( avctx, player_host_channel, player_channel );

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

                    set_envelope ( player_channel, envelope, envelope->pos );
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
                    set_envelope_value ( avctx, player_host_channel, player_channel, channel, AVSEQ_TRACK_EFFECT_CMD_SET_ENV_VAL, player_host_channel->env_ctrl );

                break;
            }
        }
    }
}

EXECUTE_EFFECT(set_envelope_value) {
    AVSequencerModule *module = avctx->player_module;
    AVSequencerEnvelope *instrument_envelope;
    AVSequencerPlayerEnvelope *envelope;
    AVSequencerPlayerEnvelope *(*envelope_get_kind)( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel );

    player_host_channel->env_ctrl = data_word;
    envelope_get_kind             = (const void *const) envelope_ctrl_type_lut[player_host_channel->env_ctrl_kind];
    envelope                      = envelope_get_kind ( avctx, player_host_channel, player_channel );

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

EXECUTE_EFFECT(nna_ctrl) {
    uint8_t nna_ctrl_type   = (data_word >> 8);
    uint8_t nna_ctrl_action = data_word;

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

EXECUTE_EFFECT(loop_ctrl) {
}

EXECUTE_EFFECT(set_speed) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    uint16_t *speed_ptr;
    uint16_t speed_value, speed_min_value, speed_max_value;
    uint8_t speed_type                       = (data_word >> 12);

    if ((speed_ptr = get_speed_address ( avctx, speed_type, &speed_min_value, &speed_max_value ))) {
        if (!(speed_value = (data_word & 0xFFF))) {
            if ((data_word & 0x7000) == 0x7000)
                speed_value = (player_globals->speed_mul << 8U) + player_globals->speed_div;
            else
                speed_value = *speed_ptr;
        }

        speed_val_ok ( avctx, speed_ptr, speed_value, speed_type, speed_min_value, speed_max_value );
    }
}

EXECUTE_EFFECT(speed_slide_faster) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v3, v4, v5;

    if (!data_word)
        data_word = player_globals->speed_slide_faster;

    do_speed_slide ( avctx, data_word );

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

EXECUTE_EFFECT(speed_slide_slower) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v3, v4;

    if (!data_word)
        data_word = player_globals->speed_slide_slower;

    do_speed_slide_slower ( avctx, data_word );

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
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v1, v4;

    if (!data_word)
        data_word = player_globals->fine_speed_slide_fast;

    do_speed_slide ( avctx, data_word );

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

EXECUTE_EFFECT(fine_speed_slide_slower) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v1, v3;

    if (!data_word)
        data_word = player_globals->fine_speed_slide_slow;

    do_speed_slide_slower ( avctx, data_word );

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

EXECUTE_EFFECT(speed_slide_to) {
}

EXECUTE_EFFECT(spenolo) {
}

EXECUTE_EFFECT(channel_ctrl) {
    uint8_t channel_ctrl_byte = data_word;

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
            if (check_surround_track_panning ( player_host_channel, player_channel, channel, 0 )) {
                player_host_channel->flags          &= ~AVSEQ_PLAYER_HOST_CHANNEL_FLAG_TRACK_SUR_PAN;
                player_channel->flags               &= ~AVSEQ_PLAYER_CHANNEL_FLAG_SMP_SUR_PAN;
            }

            break;
        case 0x01 :
            if (check_surround_track_panning ( player_host_channel, player_channel, channel, 1 )) {
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

EXECUTE_EFFECT(set_global_volume) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;

    if (check_old_track_volume ( avctx, &data_word )) {
        player_globals->global_volume     = data_word >> 8;
        player_globals->global_sub_volume = data_word;
    }
}

EXECUTE_EFFECT(global_volume_slide_up) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v3, v4, v5;

    if (!data_word)
        data_word = player_globals->global_vol_slide_up;

    do_global_volume_slide ( avctx, player_globals, data_word );

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

EXECUTE_EFFECT(global_volume_slide_down) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v3, v4;

    if (!data_word)
        data_word = player_globals->global_vol_slide_down;

    do_global_volume_slide_down ( avctx, player_globals, data_word );

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

EXECUTE_EFFECT(fine_global_volume_slide_up) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v1, v4;

    if (!data_word)
        data_word = player_globals->fine_global_vol_sl_up;

    do_global_volume_slide ( avctx, player_globals, data_word );

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

EXECUTE_EFFECT(fine_global_volume_slide_down) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v1, v3;

    if (!data_word)
        data_word = player_globals->global_vol_slide_down;

    do_global_volume_slide_down ( avctx, player_globals, data_word );

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

EXECUTE_EFFECT(global_volume_slide_to) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    uint8_t global_volume_slide_to_value, global_volume_slide_to_volume;

    if (!(global_volume_slide_to_value = data_word))
        global_volume_slide_to_value = player_globals->global_volume_slide_to;

    player_globals->global_volume_slide_to     = global_volume_slide_to_value;
    player_globals->global_volume_slide_to_slide &= 0x00FF;
    player_globals->global_volume_slide_to_slide += global_volume_slide_to_value << 8;

    global_volume_slide_to_volume = data_word >> 8;

    if (global_volume_slide_to_volume && (global_volume_slide_to_volume < 0xFF)) {
        player_globals->global_volume_sl_to_volume = global_volume_slide_to_volume;
    } else if (global_volume_slide_to_volume) {
        uint16_t global_volume_slide_target = (global_volume_slide_to_volume << 8) + player_globals->global_volume_sl_to_sub_vol;
        uint16_t global_volume              = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

        if (global_volume < global_volume_slide_target) {
            do_global_volume_slide ( avctx, player_globals, player_globals->global_volume_slide_to_slide );

            global_volume = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

            if (global_volume_slide_target <= global_volume) {
                player_globals->global_volume     = global_volume_slide_target >> 8;
                player_globals->global_sub_volume = global_volume_slide_target;
            }
        } else {
            do_global_volume_slide_down ( avctx, player_globals, player_globals->global_volume_slide_to_slide );

            global_volume = (player_globals->global_volume << 8) + player_globals->global_sub_volume;

            if (global_volume_slide_target >= global_volume) {
                player_globals->global_volume     = global_volume_slide_target >> 8;
                player_globals->global_sub_volume = global_volume_slide_target;
            }
        }
    }
}

EXECUTE_EFFECT(global_tremolo) {
    AVSequencerSong *song                    = avctx->player_song;
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    int32_t global_tremolo_slide_value;
    uint8_t global_tremolo_rate;
    int16_t global_tremolo_depth;
    uint16_t global_volume;

    if (!(global_tremolo_rate = (data_word >> 8)))
        global_tremolo_rate = player_globals->tremolo_rate;

    player_globals->tremolo_rate = global_tremolo_rate;

    if (!(global_tremolo_depth = data_word))
        global_tremolo_depth = player_globals->tremolo_depth;

    player_globals->tremolo_depth = global_tremolo_depth;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES) {
        if (global_tremolo_depth > 63)
            global_tremolo_depth = 63;

        if (global_tremolo_depth < -63)
            global_tremolo_depth = -63;
    }

    global_tremolo_slide_value = (-global_tremolo_depth * run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_globals->tremolo_env), global_tremolo_rate, 0 )) >> 7L;

    if (song->compat_flags & AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES)
        global_tremolo_slide_value <<= 2;

    global_volume               = player_globals->global_volume;
    global_tremolo_slide_value -= player_globals->tremolo_slide;

    if ((global_tremolo_slide_value += global_volume) < 0)
        global_tremolo_slide_value = 0;

    if (global_tremolo_slide_value > 255)
        global_tremolo_slide_value = 255;

    player_globals->global_volume  = global_tremolo_slide_value;
    player_globals->tremolo_slide -= global_tremolo_slide_value - global_volume;
}

EXECUTE_EFFECT(set_global_panning) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;

    player_globals->global_panning     = data_word >> 8;
    player_globals->global_sub_panning = data_word;
    player_globals->flags             &= ~AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND;
}

EXECUTE_EFFECT(global_panning_slide_left) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v3, v4, v5, v8;

    if (!data_word)
        data_word = player_globals->global_pan_slide_left;

    do_global_panning_slide ( player_globals, data_word );

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

EXECUTE_EFFECT(global_panning_slide_right) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v3, v4, v5;

    if (!data_word)
        data_word = player_globals->global_pan_slide_right;

    do_global_panning_slide_right ( player_globals, data_word );

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

EXECUTE_EFFECT(fine_global_panning_slide_left) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v1, v4, v5;

    if (!data_word)
        data_word = player_globals->fine_global_pan_sl_left;

    do_global_panning_slide ( player_globals, data_word );

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

EXECUTE_EFFECT(fine_global_panning_slide_right) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    AVSequencerTrack *track;
    uint16_t v0, v1, v3, v5;

    if (!data_word)
        data_word = player_globals->fine_global_pan_sl_right;

    do_global_panning_slide_right ( player_globals, data_word );

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

EXECUTE_EFFECT(global_panning_slide_to) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    uint8_t global_pan_slide_to, global_pan_slide_to_panning;

    if (!(global_pan_slide_to = data_word))
        global_pan_slide_to = player_globals->global_pan_slide_to;

    player_globals->global_pan_slide_to        = global_pan_slide_to;
    player_globals->global_pan_slide_to_slide &= 0x00FF;
    player_globals->global_pan_slide_to_slide += global_pan_slide_to << 8;

    global_pan_slide_to_panning = data_word >> 8;

    if (global_pan_slide_to_panning && (global_pan_slide_to_panning < 0xFF)) {
        player_globals->global_pan_slide_to_panning = global_pan_slide_to_panning;
    } else if (global_pan_slide_to_panning) {
        uint16_t global_panning_slide_target = (global_pan_slide_to_panning << 8) + player_globals->global_pan_slide_to_sub_pan;
        uint16_t global_panning              = (player_globals->global_panning << 8) + player_globals->global_sub_panning;

        if (global_panning < global_panning_slide_target) {
            do_global_panning_slide_right ( player_globals, player_globals->global_pan_slide_to_slide );

            global_panning = (player_globals->global_panning << 8) + player_globals->global_sub_panning;

            if (global_panning_slide_target <= global_panning) {
                player_globals->global_panning     = global_panning_slide_target >> 8;
                player_globals->global_sub_panning = global_panning_slide_target;
            }
        } else {
            do_global_panning_slide ( player_globals, player_globals->global_pan_slide_to_slide );

            global_panning = (player_globals->global_panning << 8) + player_globals->global_sub_panning;

            if (global_panning_slide_target >= global_panning) {
                player_globals->global_panning     = global_panning_slide_target >> 8;
                player_globals->global_sub_panning = global_panning_slide_target;
            }
        }
    }
}

EXECUTE_EFFECT(global_pannolo) {
    AVSequencerPlayerGlobals *player_globals = avctx->player_globals;
    int32_t global_pannolo_slide_value;
    uint8_t global_pannolo_rate;
    int16_t global_pannolo_depth;
    uint16_t global_panning;

    if (!(global_pannolo_rate = (data_word >> 8)))
        global_pannolo_rate = player_globals->pannolo_rate;

    player_globals->pannolo_rate = global_pannolo_rate;

    if (!(global_pannolo_depth = data_word))
        global_pannolo_depth = player_globals->pannolo_depth;

    player_globals->pannolo_depth = global_pannolo_depth;
    global_pannolo_slide_value    = (-global_pannolo_depth * run_envelope ( avctx, (AVSequencerPlayerEnvelope *) &(player_globals->pannolo_env), global_pannolo_rate, 0 )) >> 7;
    global_panning                = player_globals->global_panning;
    global_pannolo_slide_value   -= player_globals->pannolo_slide;

    if ((global_pannolo_slide_value += global_panning) < 0)
        global_pannolo_slide_value = 0;

    if (global_pannolo_slide_value > 255)
        global_pannolo_slide_value = 255;

    player_globals->global_panning = global_pannolo_slide_value;
    player_globals->pannolo_slide -= global_pannolo_slide_value - global_panning;
}

EXECUTE_EFFECT(user_sync) {
}

static uint32_t execute_synth ( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint32_t channel, uint32_t synth_type ) {
    uint16_t synth_count = 0, bit_mask = 1 << synth_type;

    do {
        AVSequencerSynth *synth          = player_channel->synth;
        AVSequencerSynthCode *synth_code = synth->code;
        uint16_t synth_code_line         = player_channel->entry_pos[synth_type], instruction_data, i;
        int8_t instruction;
        uint8_t src_var, dst_var;

        synth_code += synth_code_line;

        if (player_channel->wait_count[synth_type]--) {
exec_synth_done:

            if ((player_channel->synth_flags & bit_mask) && (!(player_channel->kill_count[synth_type]--)))
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
            AVSequencerPlayerEffects *fx_lut;
            void (*check_func)( AVSequencerContext *avctx, AVSequencerPlayerHostChannel *player_host_channel, AVSequencerPlayerChannel *player_channel, uint16_t channel, uint16_t *fx_byte, uint16_t *data_word, uint16_t *flags );
            uint16_t fx_byte, data_word, flags;

            fx_byte   = ~instruction;
            fx_lut    = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            data_word = instruction_data + player_channel->variable[src_var];
            flags     = fx_lut->flags;

            if ((check_func = fx_lut->check_fx_func)) {
                check_func ( avctx, player_host_channel, player_channel, player_channel->host_channel, &fx_byte, &data_word, &flags );

                fx_lut = (AVSequencerPlayerEffects *) (avctx->effects_lut ? avctx->effects_lut : fx_lut) + fx_byte;
            }

            if (!fx_lut->pre_pattern_func) {
                instruction_data                     = player_host_channel->virtual_channel;
                player_host_channel->virtual_channel = channel;

                fx_lut->effect_func ( avctx, player_host_channel, player_channel, player_channel->host_channel, fx_byte, data_word );

                player_host_channel->virtual_channel = instruction_data;
            }

            player_channel->entry_pos[synth_type] = synth_code_line;
        } else {
            uint16_t (**fx_exec_func)( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, uint16_t virtual_channel, uint16_t synth_code_line, uint32_t src_var, uint32_t dst_var, uint16_t instruction_data, uint32_t synth_type );

            fx_exec_func = (void *) (avctx->synth_code_exec_lut ? avctx->synth_code_exec_lut : se_lut);

            player_channel->entry_pos[synth_type] = fx_exec_func[(uint8_t) instruction] ( avctx, player_channel, channel, synth_code_line, src_var, dst_var, instruction_data, synth_type );
        }
    } while (++synth_count);

    return 0;
}

static void se_vibrato_do ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, int32_t vibrato_slide_value ) {
    AVSequencerPlayerHostChannel *player_host_channel = avctx->player_host_channel + player_channel->host_channel;
    uint32_t old_frequency = player_channel->frequency;

    player_channel->frequency -= player_channel->vibrato_slide;

    if (vibrato_slide_value < 0) {
        vibrato_slide_value = -vibrato_slide_value;

        if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
            linear_slide_up ( avctx, player_channel, player_channel->frequency, vibrato_slide_value );
        else
            amiga_slide_up ( player_channel, player_channel->frequency, vibrato_slide_value );
    } else if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ) {
        linear_slide_down ( avctx, player_channel, player_channel->frequency, vibrato_slide_value );
    } else {
        amiga_slide_down ( player_channel, player_channel->frequency, vibrato_slide_value );
    }

    player_channel->vibrato_slide -= old_frequency - player_channel->frequency;
}

static void se_arpegio_do ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, int16_t arpeggio_transpose, uint16_t arpeggio_finetune ) {
    uint32_t *frequency_lut;
    uint32_t frequency, next_frequency, slide_frequency, old_frequency;
    uint16_t octave;
    int16_t note;

    octave = arpeggio_transpose / 12;
    note   = arpeggio_transpose % 12;

    if (note < 0) {
        octave--;
        note             += 12;
        arpeggio_finetune = -arpeggio_finetune;
    }

    frequency_lut  = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
    frequency      = *frequency_lut++;
    next_frequency = *frequency_lut - frequency;
    frequency     += (int32_t) (arpeggio_finetune * (int16_t) next_frequency) >> 8;

    if ((int16_t) (octave) < 0) {
        octave      = -octave;
        frequency >>= octave;
    } else {
        frequency <<= octave;
    }

    old_frequency                           = player_channel->frequency;
    slide_frequency                         = player_channel->arpeggio_slide + old_frequency;
    player_channel->frequency               = frequency = ((uint64_t) frequency * slide_frequency) >> 16;
    player_channel->arpeggio_slide         += old_frequency - frequency;
}

static void se_tremolo_do ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, int32_t tremolo_slide_value ) {
    uint16_t volume      = player_channel->volume;

    tremolo_slide_value -= player_channel->tremolo_slide;

    if ((tremolo_slide_value += volume) < 0)
        tremolo_slide_value = 0;

    if (tremolo_slide_value > 255)
        tremolo_slide_value = 255;

    player_channel->volume                  = tremolo_slide_value;
    player_channel->tremolo_slide          -= tremolo_slide_value - volume;
}

static void se_pannolo_do ( AVSequencerContext *avctx, AVSequencerPlayerChannel *player_channel, int32_t pannolo_slide_value ) {
    uint16_t panning = player_channel->panning;

    pannolo_slide_value -= player_channel->pannolo_slide;

    if ((pannolo_slide_value += panning) < 0)
        pannolo_slide_value = 0;

    if (pannolo_slide_value > 255)
        pannolo_slide_value = 255;

    player_channel->panning                 = pannolo_slide_value;
    player_channel->pannolo_slide          -= pannolo_slide_value - panning;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(stop) {
    instruction_data += player_channel->variable[src_var];

    if (instruction_data & 0x8000)
        player_channel->stop_forbid_mask &= ~instruction_data;
    else
        player_channel->stop_forbid_mask |= instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(kill) {
    instruction_data                              += player_channel->variable[src_var];
    player_channel->kill_count[synth_type]         = instruction_data;
    player_channel->synth_flags                   |= 1 << synth_type;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(wait) {
    instruction_data                      += player_channel->variable[src_var];
    player_channel->wait_count[synth_type] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(waitvol) {
    instruction_data                     += player_channel->variable[src_var];
    player_channel->wait_line[synth_type] = instruction_data;
    player_channel->wait_type[synth_type] = ~0;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(waitpan) {
    instruction_data                     += player_channel->variable[src_var];
    player_channel->wait_line[synth_type] = instruction_data;
    player_channel->wait_type[synth_type] = ~1;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(waitsld) {
    instruction_data                     += player_channel->variable[src_var];
    player_channel->wait_line[synth_type] = instruction_data;
    player_channel->wait_type[synth_type] = ~2;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(waitspc) {
    instruction_data                     += player_channel->variable[src_var];
    player_channel->wait_line[synth_type] = instruction_data;
    player_channel->wait_type[synth_type] = ~3;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jump) {
    instruction_data += player_channel->variable[src_var];

    return instruction_data;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpeq) {
    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpne) {
    if (!(player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumppl) {
    if (!(player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpmi) {
    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumplt) {
    uint16_t synth_cond_var = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE);

    if ((synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW) || (synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumple) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpgt) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpge) {
    uint16_t synth_cond_var = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW|AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE);

    if (!((synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW) || (synth_cond_var == AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE))) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvs) {
    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvc) {
    if (!(player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpcs) {
    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpcc) {
    if (!(player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpls) {
    uint16_t synth_cond_var = player_channel->cond_var[synth_type];

    if ((synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO) && (synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY)) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumphi) {
    uint16_t synth_cond_var = player_channel->cond_var[synth_type];

    if (!((synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO) && (synth_cond_var & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY))) {
        instruction_data += player_channel->variable[src_var];

        return instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpvol) {
    if (!(player_channel->stop_forbid_mask & 1)) {
        instruction_data            += player_channel->variable[src_var];
        player_channel->entry_pos[0] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumppan) {
    if (!(player_channel->stop_forbid_mask & 2)) {
        instruction_data            += player_channel->variable[src_var];
        player_channel->entry_pos[1] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpsld) {
    if (!(player_channel->stop_forbid_mask & 4)) {
        instruction_data            += player_channel->variable[src_var];
        player_channel->entry_pos[2] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(jumpspc) {
    if (!(player_channel->stop_forbid_mask & 8)) {
        instruction_data            += player_channel->variable[src_var];
        player_channel->entry_pos[3] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(call) {
    player_channel->variable[dst_var] = synth_code_line;
    instruction_data                 += player_channel->variable[src_var];

    return instruction_data;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ret) {
    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = --synth_code_line;

    return instruction_data;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(posvar) {
    player_channel->variable[src_var] += synth_code_line + --instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(load) {
    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(add) {
    uint16_t flags = 0;
    int32_t add_data;

    instruction_data                 += player_channel->variable[src_var];
    add_data                          = (int16_t) player_channel->variable[dst_var] + (int16_t) instruction_data;
    player_channel->variable[dst_var] = add_data;

    if (player_channel->variable[dst_var] < instruction_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((add_data < -0x8000) || (add_data >= 0x8000))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if (!add_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (add_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(addx) {
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;
    uint32_t add_unsigned_data;
    int32_t add_data;

    instruction_data += player_channel->variable[src_var];
    add_data          = (int16_t) player_channel->variable[dst_var] + (int16_t) instruction_data;
    add_unsigned_data = instruction_data;

    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND) {
        add_data++;
        add_unsigned_data++;
    }

    player_channel->variable[dst_var] = add_data;

    if ((player_channel->variable[dst_var] < add_unsigned_data))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((add_data < -0x8000) || (add_data >= 0x8000))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if (add_data)
        flags &= ~AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (add_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(sub) {
    uint16_t flags = 0;
    int32_t sub_data;

    instruction_data += player_channel->variable[src_var];
    sub_data          = (int16_t) player_channel->variable[dst_var] - (int16_t) instruction_data;

    if (player_channel->variable[dst_var] < instruction_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    player_channel->variable[dst_var] = sub_data;

    if ((sub_data < -0x8000) || (sub_data >= 0x8000))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if (!sub_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (sub_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(subx) {
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;
    uint32_t sub_unsigned_data;
    int32_t sub_data;

    instruction_data += player_channel->variable[src_var];
    sub_data          = (int16_t) player_channel->variable[dst_var] - (int16_t) instruction_data;
    sub_unsigned_data = instruction_data;

    if (player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND) {
        sub_data--;
        sub_unsigned_data++;
    }

    if (player_channel->variable[dst_var] < sub_unsigned_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    player_channel->variable[dst_var] = sub_data;

    if ((sub_data < -0x8000) || (sub_data >= 0x8000))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if (sub_data)
        flags &= ~AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (sub_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(cmp) {
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int32_t sub_data;

    instruction_data += player_channel->variable[src_var];
    sub_data          = (int16_t) player_channel->variable[dst_var] - (int16_t) instruction_data;

    if (player_channel->variable[dst_var] < instruction_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY;

    if ((sub_data < -0x8000) || (sub_data >= 0x8000))
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;

    if (!sub_data)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (sub_data < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(mulu) {
    uint16_t umult_res, flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = umult_res = (uint8_t) player_channel->variable[dst_var] * (uint8_t) instruction_data;

    if (!umult_res)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if ((int16_t) umult_res < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(muls) {
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    int16_t smult_res;

    instruction_data                 += player_channel->variable[src_var];
    player_channel->variable[dst_var] = smult_res = (int8_t) player_channel->variable[dst_var] * (int8_t) instruction_data;

    if (!smult_res)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    if (smult_res < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(dmulu) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(dmuls) {
    int32_t smult_res;
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    instruction_data += player_channel->variable[src_var];
    smult_res         = (int16_t) player_channel->variable[dst_var] * (int16_t) instruction_data;

    if (dst_var == 15) {
        player_channel->variable[dst_var] = smult_res;

        if ((smult_res <= -0x10000) || (smult_res >= 0x10000))
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

EXECUTE_SYNTH_CODE_INSTRUCTION(divu) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(divs) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(modu) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(mods) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(ddivu) {
    uint32_t udiv_res;
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((instruction_data += player_channel->variable[src_var])) {
        if (dst_var == 15) {
            player_channel->variable[dst_var] = udiv_res = (player_channel->variable[dst_var] << 16) / instruction_data;

            if (udiv_res < 0x10000) {
                if (!udiv_res)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

                if ((int32_t) udiv_res < 0)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
            } else {
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
            }
        } else {
            uint32_t dividend = (player_channel->variable[dst_var + 1] << 16) + player_channel->variable[dst_var];

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

EXECUTE_SYNTH_CODE_INSTRUCTION(ddivs) {
    int32_t sdiv_res;
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;

    if ((instruction_data += player_channel->variable[src_var])) {
        if (dst_var == 15) {
            player_channel->variable[dst_var] = sdiv_res = ((int16_t) player_channel->variable[dst_var] << 16) / (int16_t) instruction_data;

            if ((sdiv_res < -0x10000) || (sdiv_res < 0x10000)) {
                if (!sdiv_res)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

                if ((int32_t) sdiv_res < 0)
                    flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;
            } else {
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_OVERFLOW;
            }
        } else {
            int32_t dividend = ((int16_t) player_channel->variable[dst_var + 1] << 16) + (int16_t) player_channel->variable[dst_var];
            sdiv_res         = dividend / instruction_data;

            if ((sdiv_res < -0x10000) || (sdiv_res < 0x10000)) {
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

    player_channel->variable[dst_var]             = shift_value;
    player_channel->cond_var[synth_type]          = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ashr) {
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

    player_channel->variable[dst_var]             = shift_value;
    player_channel->cond_var[synth_type]          = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(lshl) {
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

    player_channel->variable[dst_var]             = shift_value;
    player_channel->cond_var[synth_type]          = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(lshr) {
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

    player_channel->variable[dst_var]             = shift_value;
    player_channel->cond_var[synth_type]          = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(rol) {
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

    player_channel->variable[dst_var]             = shift_value;
    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(ror) {
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

    player_channel->variable[dst_var]             = shift_value;
    player_channel->cond_var[synth_type]          = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(rolx) {
    uint16_t flags       = player_channel->cond_var[synth_type] & (AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY|AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);
    uint16_t shift_value = player_channel->variable[dst_var];

    instruction_data += player_channel->variable[src_var];
    instruction_data &= 0x3F;

    while (instruction_data--) {
        flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY);

        if (shift_value & 0x8000)
            flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY;

        shift_value <<= 1;

        if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND)
        {
            if (!(flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY))
                flags &= ~(AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND);

            shift_value++;
        } else
        {
            if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY)
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
        }
    }

    if ((int16_t) shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]             = shift_value;
    player_channel->cond_var[synth_type]          = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(rorx) {
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
        } else {
            if (flags & AVSEQ_PLAYER_CHANNEL_COND_VAR_CARRY)
                flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
        }
    }

    if ((int16_t) shift_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!shift_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->variable[dst_var]             = shift_value;
    player_channel->cond_var[synth_type]          = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(or) {
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t logic_value;

    instruction_data += player_channel->variable[src_var];
    logic_value       = (player_channel->variable[dst_var] |= instruction_data );

    if ((int16_t) logic_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!logic_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(and) {
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t logic_value;

    instruction_data += player_channel->variable[src_var];
    logic_value       = (player_channel->variable[dst_var] &= instruction_data );

    if ((int16_t) logic_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!logic_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(xor) {
    uint16_t flags = player_channel->cond_var[synth_type] & AVSEQ_PLAYER_CHANNEL_COND_VAR_EXTEND;
    uint16_t logic_value;

    instruction_data += player_channel->variable[src_var];
    logic_value       = (player_channel->variable[dst_var] ^= instruction_data );

    if ((int16_t) logic_value < 0)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_NEGATIVE;

    if (!logic_value)
        flags |= AVSEQ_PLAYER_CHANNEL_COND_VAR_ZERO;

    player_channel->cond_var[synth_type] = flags;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(not) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(neg) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(negx) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(extb) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(ext) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(xchg) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(swap) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(getwave) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    AVSequencerSynthWave *waveform       = player_channel->sample_waveform;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getwlen) {
    uint16_t sample_length = -1;

    if (player_channel->mixer.len < 0x10000)
        sample_length = player_channel->mixer.len;

    instruction_data                 += player_channel->variable[src_var] + sample_length;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getwpos) {
    uint16_t sample_pos = -1;

    if (player_channel->mixer.pos < 0x10000)
        sample_pos = player_channel->mixer.pos;

    instruction_data                 += player_channel->variable[src_var] + sample_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getchan) {
    instruction_data                 += player_channel->variable[src_var] + player_channel->host_channel;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getnote) {
    uint16_t note = player_channel->sample_note;

    instruction_data                 += player_channel->variable[src_var] + --note;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getrans) {
    uint16_t note = player_channel->sample_note;

    instruction_data                 += player_channel->variable[src_var] + player_channel->final_note - --note;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getptch) {
    uint32_t frequency = player_channel->frequency;

    instruction_data += player_channel->variable[src_var];
    frequency        += instruction_data;

    if (dst_var != 15)
        player_channel->variable[dst_var++] = frequency >> 16;

    player_channel->variable[dst_var] = frequency;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getper) {
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

EXECUTE_SYNTH_CODE_INSTRUCTION(getfx) {


    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getarpw) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    AVSequencerSynthWave *waveform       = player_channel->arpeggio_waveform;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getarpv) {
    AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->arpeggio_waveform)) {
        uint32_t waveform_pos = instruction_data % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            player_channel->variable[dst_var] = ((uint8_t *) waveform->data)[waveform_pos] << 8;
        else
            player_channel->variable[dst_var] = waveform->data[waveform_pos];
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getarpl) {
    AVSequencerSynthWave *waveform;

    if ((waveform = player_channel->arpeggio_waveform)) {
        uint16_t waveform_length = -1;

        if (waveform->samples < 0x10000)
            waveform_length = waveform->samples;

        instruction_data                 += player_channel->variable[src_var] + waveform_length;
        player_channel->variable[dst_var] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getarpp) {
    instruction_data                 += player_channel->variable[src_var] + player_channel->arpeggio_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getvibw) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    AVSequencerSynthWave *waveform       = player_channel->vibrato_waveform;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    instruction_data += player_channel->variable[src_var];

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getvibv) {
    AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->vibrato_waveform)) {
        uint32_t waveform_pos = instruction_data % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            player_channel->variable[dst_var] = ((uint8_t *) waveform->data)[waveform_pos] << 8;
        else
            player_channel->variable[dst_var] = waveform->data[waveform_pos];
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getvibl) {
    AVSequencerSynthWave *waveform;

    if ((waveform = player_channel->vibrato_waveform)) {
        uint16_t waveform_length = -1;

        if (waveform->samples < 0x10000)
            waveform_length = waveform->samples;

        instruction_data                 += player_channel->variable[src_var] + waveform_length;
        player_channel->variable[dst_var] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getvibp) {
    instruction_data                 += player_channel->variable[src_var] + player_channel->vibrato_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmw) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    AVSequencerSynthWave *waveform       = player_channel->tremolo_waveform;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmv) {
    AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->tremolo_waveform)) {
        uint32_t waveform_pos = instruction_data % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            player_channel->variable[dst_var] = ((uint8_t *) waveform->data)[waveform_pos] << 8;
        else
            player_channel->variable[dst_var] = waveform->data[waveform_pos];
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(gettrml) {
    AVSequencerSynthWave *waveform;

    if ((waveform = player_channel->tremolo_waveform)) {
        uint16_t waveform_length = -1;

        if (waveform->samples < 0x10000)
            waveform_length = waveform->samples;

        instruction_data                 += player_channel->variable[src_var] + waveform_length;
        player_channel->variable[dst_var] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(gettrmp) {
    instruction_data                 += player_channel->variable[src_var] + player_channel->tremolo_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getpanw) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    AVSequencerSynthWave *waveform       = player_channel->pannolo_waveform;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform == waveform_list[waveform_num]) {
            instruction_data += waveform_num;

            break;
        }
    }

    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getpanv) {
    AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->pannolo_waveform)) {
        uint32_t waveform_pos = instruction_data % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            player_channel->variable[dst_var] = ((uint8_t *) waveform->data)[waveform_pos] << 8;
        else
            player_channel->variable[dst_var] = waveform->data[waveform_pos];
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getpanl) {
    AVSequencerSynthWave *waveform;

    if ((waveform = player_channel->pannolo_waveform)) {
        uint16_t waveform_length = -1;

        if (waveform->samples < 0x10000)
            waveform_length = waveform->samples;

        instruction_data                 += player_channel->variable[src_var] + waveform_length;
        player_channel->variable[dst_var] = instruction_data;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getpanp) {
    instruction_data                 += player_channel->variable[src_var] + player_channel->pannolo_pos;
    player_channel->variable[dst_var] = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getrnd) {
    uint32_t seed;

    instruction_data                 += player_channel->variable[src_var];
    avctx->seed                       = seed = ((int32_t) avctx->seed * AVSEQ_RANDOM_CONST) + 1;
    player_channel->variable[dst_var] = ((uint64_t) seed * instruction_data) >> 32;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(getsine) {
    int16_t degrees;

    instruction_data += player_channel->variable[src_var];
    degrees           = (int16_t) instruction_data % 360;

    if (degrees < 0)
        degrees += 360;

    player_channel->variable[dst_var] = (avctx->sine_lut ? avctx->sine_lut[degrees] : sine_lut[degrees]);

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(portaup) {
    AVSequencerPlayerHostChannel *player_host_channel = avctx->player_host_channel + player_channel->host_channel;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->porta_up;

    player_channel->porta_up    = instruction_data;
    player_channel->portamento += instruction_data;

    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
        linear_slide_up ( avctx, player_channel, player_channel->frequency, instruction_data );
    else
        amiga_slide_up ( player_channel, player_channel->frequency, instruction_data );

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(portadn) {
    AVSequencerPlayerHostChannel *player_host_channel = avctx->player_host_channel + player_channel->host_channel;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->porta_dn;

    player_channel->porta_dn    = instruction_data;
    player_channel->portamento -= instruction_data;

    if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
        linear_slide_down ( avctx, player_channel, player_channel->frequency, instruction_data );
    else
        amiga_slide_down ( player_channel, player_channel->frequency, instruction_data );

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibspd) {
    instruction_data               += player_channel->variable[src_var];
    player_channel->vibrato_rate = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibdpth) {
    instruction_data                += player_channel->variable[src_var];
    player_channel->vibrato_depth    = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibwave) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];
    player_channel->vibrato_waveform     = NULL;

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->vibrato_waveform = waveform_list[waveform_num];

            break;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibwavp) {
    AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->vibrato_waveform))
        player_channel->vibrato_pos = instruction_data % waveform->samples;
    else
        player_channel->vibrato_pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibrato) {
    AVSequencerSynthWave *waveform;
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

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            vibrato_slide_value = ((int8_t *) waveform->data)[waveform_pos] << 8;
        else
            vibrato_slide_value = waveform->data[waveform_pos];

        vibrato_slide_value        *= -vibrato_depth;
        vibrato_slide_value       >>= (7 - 2);
        player_channel->vibrato_pos = (waveform_pos + vibrato_rate) % waveform->samples;

        se_vibrato_do ( avctx, player_channel, vibrato_slide_value );
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(vibval) {
    int32_t vibrato_slide_value;

    instruction_data   += player_channel->variable[src_var];
    vibrato_slide_value = (int16_t) instruction_data;

    se_vibrato_do ( avctx, player_channel, vibrato_slide_value );

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpspd) {
    instruction_data                 += player_channel->variable[src_var];
    player_channel->arpeggio_speed    = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpwave) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];
    player_channel->arpeggio_waveform    = NULL;

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->arpeggio_waveform = waveform_list[waveform_num];

            break;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpwavp) {
    AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->arpeggio_waveform))
        player_channel->arpeggio_pos = instruction_data % waveform->samples;
    else
        player_channel->arpeggio_pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpegio) {
    AVSequencerSynthWave *waveform;
    uint16_t arpeggio_speed;

    instruction_data += player_channel->variable[src_var];

    if (!(arpeggio_speed = ( instruction_data >> 8)))
        arpeggio_speed = player_channel->arpeggio_speed;

    player_channel->arpeggio_speed = arpeggio_speed;

    if ((waveform = player_channel->arpeggio_waveform)) {
        uint32_t waveform_pos;
        int8_t arpeggio_transpose;
        uint8_t arpeggio_finetune;

        waveform_pos = player_channel->arpeggio_pos % waveform->samples;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            arpeggio_transpose = ((int8_t *) waveform->data)[waveform_pos] << 8;
        else
            arpeggio_transpose = waveform->data[waveform_pos];

        player_channel->arpeggio_finetune  = arpeggio_finetune = arpeggio_transpose;
        player_channel->arpeggio_transpose = (arpeggio_transpose >>= 8);
        player_channel->arpeggio_pos       = (waveform_pos + arpeggio_speed) % waveform->samples;

        se_arpegio_do ( avctx, player_channel, arpeggio_transpose, arpeggio_finetune );
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(arpval) {
    int8_t arpeggio_transpose;
    uint8_t arpeggio_finetune;

    instruction_data += player_channel->variable[src_var];

    player_channel->arpeggio_finetune  = arpeggio_finetune  = instruction_data;
    player_channel->arpeggio_transpose = arpeggio_transpose = (instruction_data >> 8);

    se_arpegio_do ( avctx, player_channel, arpeggio_transpose, arpeggio_finetune );

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setwave) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    AVSequencerSynthWave *waveform       = NULL;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->vibrato_waveform = waveform_list[waveform_num];

            break;
        }
    }

    if (waveform) {
        AVSequencerMixerData *mixer;
        uint8_t flags;

        player_channel->sample_waveform      = waveform;
        player_channel->mixer.pos            = 0;
        player_channel->mixer.len            = waveform->samples;
        player_channel->mixer.data           = waveform->data;
        player_channel->mixer.repeat_start   = waveform->repeat;
        player_channel->mixer.repeat_length  = waveform->repeat_len;
        player_channel->mixer.repeat_count   = 0;
        player_channel->mixer.repeat_counted = 0;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            player_channel->mixer.bits_per_sample = 8;
        else
            player_channel->mixer.bits_per_sample = 16;

        flags = player_channel->mixer.flags & (AVSEQ_MIXER_CHANNEL_FLAG_SURROUND|AVSEQ_MIXER_CHANNEL_FLAG_PLAY);

        if ((!(waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_NOLOOP)) && (waveform->repeat_len))
            flags |= AVSEQ_MIXER_CHANNEL_FLAG_LOOP;

        flags                      |= AVSEQ_MIXER_CHANNEL_FLAG_SYNTH;
        player_channel->mixer.flags = flags;
        mixer                       = avctx->player_mixer_data;

        mixer_set_channel ( mixer, (AVSequencerMixerChannel *) &(player_channel->mixer), virtual_channel, mixer->mixctx );
        mixer_get_channel ( mixer, (AVSequencerMixerChannel *) &(player_channel->mixer), virtual_channel, mixer->mixctx );
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(isetwav) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    AVSequencerSynthWave *waveform       = NULL;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->vibrato_waveform = waveform_list[waveform_num];

            break;
        }
    }

    if (waveform) {
        AVSequencerMixerData *mixer;
        uint8_t flags;

        player_channel->sample_waveform      = waveform;
        player_channel->mixer.pos            = 0;
        player_channel->mixer.len            = waveform->samples;
        player_channel->mixer.data           = waveform->data;
        player_channel->mixer.repeat_start   = waveform->repeat;
        player_channel->mixer.repeat_length  = waveform->repeat_len;
        player_channel->mixer.repeat_count   = 0;
        player_channel->mixer.repeat_counted = 0;

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            player_channel->mixer.bits_per_sample = 8;
        else
            player_channel->mixer.bits_per_sample = 16;

        flags = player_channel->mixer.flags & (AVSEQ_MIXER_CHANNEL_FLAG_SURROUND|AVSEQ_MIXER_CHANNEL_FLAG_PLAY);

        if ((!(waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_NOLOOP)) && waveform->repeat_len)
            flags |= AVSEQ_MIXER_CHANNEL_FLAG_LOOP;

        player_channel->mixer.flags = flags;
        mixer                       = avctx->player_mixer_data;

        mixer_set_channel ( mixer, (AVSequencerMixerChannel *) &(player_channel->mixer), virtual_channel, mixer->mixctx );
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setwavp) {
    instruction_data += player_channel->variable[src_var];

    player_channel->mixer.pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setrans) {
    uint32_t *frequency_lut;
    uint32_t frequency, next_frequency;
    uint16_t octave;
    int16_t note;
    int8_t finetune;

    player_channel->final_note = (instruction_data += player_channel->variable[src_var] + player_channel->sample_note);

    octave = (int16_t) instruction_data / 12;
    note   = (int16_t) instruction_data % 12;

    if (note < 0) {
        octave--;

        note += 12;
    }

    if ((finetune = player_channel->finetune) < 0) {
        finetune += -0x80;

        note--;
    }

    frequency_lut  = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
    frequency      = *frequency_lut++;
    next_frequency = *frequency_lut - frequency;
    frequency     += (int32_t) (finetune * (int16_t) next_frequency) >> 7;

    if ((int16_t) (octave -= 4) < 0) {
        octave = -octave;

        player_channel->frequency = ((uint64_t) frequency * player_channel->sample->rate) >> (16 + octave);
    } else {
        frequency <<= octave;

        player_channel->frequency = ((uint64_t) frequency * player_channel->sample->rate) >> 16;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setnote) {
    uint32_t *frequency_lut;
    uint32_t frequency, next_frequency;
    uint16_t octave;
    int16_t note;
    int8_t finetune;

    instruction_data += player_channel->variable[src_var];
    octave            = (int16_t) instruction_data / 12;
    note              = (int16_t) instruction_data % 12;

    if (note < 0) {
        octave--;

        note += 12;
    }

    if ((finetune = player_channel->finetune) < 0) {
        finetune += -0x80;

        note--;
    }

    frequency_lut  = (avctx->frequency_lut ? avctx->frequency_lut : pitch_lut) + note + 1;
    frequency      = *frequency_lut++;
    next_frequency = *frequency_lut - frequency;
    frequency     += (int32_t) (finetune * (int16_t) next_frequency) >> 7;

    if ((int16_t) (octave -= 4) < 0) {
        octave = -octave;

        player_channel->frequency = ((uint64_t) frequency * player_channel->sample->rate) >> (16 + octave);
    } else {
        frequency <<= octave;

        player_channel->frequency = ((uint64_t) frequency * player_channel->sample->rate) >> 16;
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setptch) {
    uint32_t frequency;

    frequency = instruction_data + player_channel->variable[src_var];

    if (dst_var == 15)
        frequency += player_channel->variable[dst_var];
    else
        frequency += (player_channel->variable[dst_var + 1] << 16) + player_channel->variable[dst_var];

    player_channel->frequency = frequency;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(setper) {
    uint32_t period;

    period = instruction_data + player_channel->variable[src_var];

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

EXECUTE_SYNTH_CODE_INSTRUCTION(reset) {
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
        AVSequencerPlayerHostChannel *player_host_channel = avctx->player_host_channel + player_channel->host_channel;
        int32_t portamento_value                          = player_channel->portamento;

        if (portamento_value < 0) {
            portamento_value = -portamento_value;

            if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ)
                linear_slide_down ( avctx, player_channel, player_channel->frequency, portamento_value );
            else
                amiga_slide_down ( player_channel, player_channel->frequency, portamento_value );
        } else if (player_host_channel->flags & AVSEQ_PLAYER_HOST_CHANNEL_FLAG_LINEAR_FREQ) {
            linear_slide_up ( avctx, player_channel, player_channel->frequency, portamento_value );
        } else {
            amiga_slide_up ( player_channel, player_channel->frequency, portamento_value );
        }
    }

    if (!(instruction_data & 0x20))
        player_channel->portamento = 0;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(volslup) {
    uint16_t slide_volume = (player_channel->volume << 8) + player_channel->sub_vol;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->vol_sl_up;

    player_channel->vol_sl_up = instruction_data;

    if ((slide_volume += instruction_data) < instruction_data)
        slide_volume = 0xFFFF;

    player_channel->volume  = slide_volume >> 8;
    player_channel->sub_vol = slide_volume;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(volsldn) {
    uint16_t slide_volume = (player_channel->volume << 8) + player_channel->sub_vol;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->vol_sl_dn;

    player_channel->vol_sl_dn = instruction_data;

    if (slide_volume < instruction_data)
        instruction_data = slide_volume;

    slide_volume           -= instruction_data;
    player_channel->volume  = slide_volume >> 8;
    player_channel->sub_vol = slide_volume;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmspd) {
    instruction_data            += player_channel->variable[src_var];
    player_channel->tremolo_rate = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmdpth) {
    instruction_data            += player_channel->variable[src_var];
    player_channel->tremolo_rate = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmwave) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];
    player_channel->tremolo_waveform     = NULL;

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->tremolo_waveform = waveform_list[waveform_num];

            break;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmwavp) {
    AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->tremolo_waveform))
        player_channel->tremolo_pos = instruction_data % waveform->samples;
    else
        player_channel->tremolo_pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(tremolo) {
    AVSequencerSynthWave *waveform;
    uint16_t tremolo_rate;
    int16_t tremolo_depth;

    instruction_data += player_channel->variable[src_var];

    if (!(tremolo_rate = ( instruction_data >> 8)))
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

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            tremolo_slide_value = ((int8_t *) waveform->data)[waveform_pos] << 8;
        else
            tremolo_slide_value = waveform->data[waveform_pos];

        tremolo_slide_value        *= -tremolo_depth;
        tremolo_slide_value       >>= 7 - 2;
        player_channel->tremolo_pos = (waveform_pos + tremolo_rate) % waveform->samples;

        se_tremolo_do ( avctx, player_channel, tremolo_slide_value );
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(trmval) {
    int32_t tremolo_slide_value;

    instruction_data   += player_channel->variable[src_var];
    tremolo_slide_value = (int16_t) instruction_data;

    se_tremolo_do ( avctx, player_channel, tremolo_slide_value );

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panleft) {
    uint16_t panning = (player_channel->panning << 8) + player_channel->sub_pan;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->pan_sl_left;

    player_channel->pan_sl_left = instruction_data;

    if (panning < instruction_data)
        instruction_data = panning;

    panning -= instruction_data;

    player_channel->panning = panning >> 8;
    player_channel->sub_pan = panning;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panrght) {
    uint16_t panning = (player_channel->panning << 8) + player_channel->sub_pan;

    if (!(instruction_data += player_channel->variable[src_var]))
        instruction_data = player_channel->pan_sl_right;

    player_channel->pan_sl_right = instruction_data;

    if ((panning += instruction_data) < instruction_data)
        panning = 0xFFFF;

    player_channel->panning = panning >> 8;
    player_channel->sub_pan = panning;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panspd) {
    instruction_data            += player_channel->variable[src_var];
    player_channel->pannolo_rate = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(pandpth) {
    instruction_data             += player_channel->variable[src_var];
    player_channel->pannolo_depth = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panwave) {
    AVSequencerSynthWave **waveform_list = player_channel->waveform_list;
    uint32_t waveform_num                = -1;

    instruction_data                    += player_channel->variable[src_var];
    player_channel->pannolo_waveform     = NULL;

    while ((++waveform_num < player_channel->synth->waveforms) && waveform_list[waveform_num]) {
        if (waveform_num == instruction_data) {
            player_channel->pannolo_waveform = waveform_list[waveform_num];

            break;
        }
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panwavp) {
    AVSequencerSynthWave *waveform;

    instruction_data += player_channel->variable[src_var];

    if ((waveform = player_channel->pannolo_waveform))
        player_channel->pannolo_pos = instruction_data % waveform->samples;
    else
        player_channel->pannolo_pos = instruction_data;

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(pannolo) {
    AVSequencerSynthWave *waveform;
    uint16_t pannolo_rate;
    int16_t pannolo_depth;

    instruction_data += player_channel->variable[src_var];

    if (!(pannolo_rate = ( instruction_data >> 8)))
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

        if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT)
            pannolo_slide_value = ((int8_t *) waveform->data)[waveform_pos] << 8;
        else
            pannolo_slide_value = waveform->data[waveform_pos];

        pannolo_slide_value        *= -pannolo_depth;
        pannolo_slide_value       >>= 7 - 2;
        player_channel->pannolo_pos = (waveform_pos + pannolo_rate) % waveform->samples;

        se_pannolo_do ( avctx, player_channel, pannolo_slide_value );
    }

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(panval) {
    int32_t pannolo_slide_value;

    instruction_data   += player_channel->variable[src_var];
    pannolo_slide_value = (int16_t) instruction_data;

    se_pannolo_do ( avctx, player_channel, pannolo_slide_value );

    return synth_code_line;
}

EXECUTE_SYNTH_CODE_INSTRUCTION(nop) {
    return synth_code_line;
}
