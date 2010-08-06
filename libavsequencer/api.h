/*
 * AVSequencer API functions
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

#ifndef AVSEQUENCER_API_H
#define AVSEQUENCER_API_H

#include "libavsequencer/avsequencer.h"
#include "libavsequencer/module.h"
#include "libavsequencer/song.h"
#include "libavsequencer/order.h"
#include "libavsequencer/track.h"
#include "libavsequencer/instr.h"
#include "libavsequencer/sample.h"
#include "libavsequencer/synth.h"
#include "libavsequencer/player.h"

/**
 * Opens and registers module to the AVSequencer.
 *
 * @param avctx the AVSequencerContext to store the opened module into
 * @param module the AVSequencerModule which has been opened to be registered
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_module_open(AVSequencerContext *avctx, AVSequencerModule *module);

/**
 * Opens and registers a new sub-song to a module.
 *
 * @param module the AVSequencerModule to register the new sub-song to
 * @param song the AVSequencerSong structure to initialize
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_song_open(AVSequencerModule *module, AVSequencerSong *song);

/**
 * Opens and registers a new order list to a sub-song.
 *
 * @param song the AVSequencerSong structure to store the initialized order list
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_order_open(AVSequencerSong *song);

/**
 * Opens and registers a new track to a sub-song.
 *
 * @param song the AVSequencerSong structure to add the new track to
 * @param track the AVSequencerTrack to be added to the sub-song
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_track_open(AVSequencerSong *song, AVSequencerTrack *track);

/**
 * Opens and registers a new array of track data to a track.
 *
 * @param track the AVSequencerTrack structure to store the initialized track data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_track_data_open(AVSequencerTrack *track);

/**
 * Opens and registers a new instrument to a module.
 *
 * @param module the AVSequencerModule structure to add the new instrument to
 * @param instrument the AVSequencerInstrument to be added to the module
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_instrument_open(AVSequencerModule *module, AVSequencerInstrument *instrument);

/**
 * Opens and registers a new envelope to a module.
 *
 * @param avctx the AVSequencerContext to add the new envelope to
 * @param module the AVSequencerModule structure to add the new envelope to
 * @param envelope the AVSequencerEnvelope to be added to the module
 * @param points the number of data points to be used in the envelope data
 * @param type the type of envelope data to initialize: 0 = create empty envelope,
 *                                                      1 = create sine envelope,
 *                                                      2 = create cosine envelope,
 *                                                      3 = create ramp envelope,
 *                                                      4 = create triangle envelope,
 *                                                      5 = create square envelope,
 *                                                      6 = create sawtooth envelope
 * @param scale the scale factor for the envelope data
 * @param y_offset the y offset value to add as absolute value to the envelope data
 * @param nodes the number of dragable nodes with linear connection between data points
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_envelope_open(AVSequencerContext *avctx, AVSequencerModule *module,
                        AVSequencerEnvelope *envelope, uint32_t points,
                        uint32_t type, uint32_t scale,
                        uint32_t y_offset, uint32_t nodes);

/**
 * Opens and registers a new envelope data and node set to an envelope.
 *
 * @param avctx the AVSequencerContext to add the new envelope data and node set to
 * @param envelope the AVSequencerEnvelope to add the new envelope data and node set to
 * @param points the number of data points to be used in the envelope data
 * @param type the type of envelope data to initialize: 0 = create empty envelope,
 *                                                      1 = create sine envelope,
 *                                                      2 = create cosine envelope,
 *                                                      3 = create ramp envelope,
 *                                                      4 = create triangle envelope,
 *                                                      5 = create square envelope,
 *                                                      6 = create sawtooth envelope
 * @param scale the scale factor for the envelope data
 * @param y_offset the y offset value to add as absolute value to the envelope data
 * @param nodes the number of dragable nodes with linear connection between data points
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_envelope_data_open(AVSequencerContext *avctx, AVSequencerEnvelope *envelope,
                             uint32_t points, uint32_t type, uint32_t scale,
                             uint32_t y_offset, uint32_t nodes);

/**
 * Opens and registers a new keyboard definition to a module.
 *
 * @param module the AVSequencerModule structure to add the new keyboard definition to
 * @param keyboard the AVSequencerKeyboard to be added to the module
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_keyboard_open(AVSequencerModule *module, AVSequencerKeyboard *keyboard);

/**
 * Opens and registers a new arpeggio structure to a module.
 *
 * @param module the AVSequencerModule structure to add the new arpeggio to
 * @param arpeggio the AVSequencerArpeggio to be added to the module
 * @param entries the number of arpeggio trigger entries to be used in the arpeggio data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_arpeggio_open(AVSequencerModule *module, AVSequencerArpeggio *arpeggio,
                        uint32_t entries);

/**
 * Opens and registers a new arpeggio data set to an arpeggio structure.
 *
 * @param arpeggio the AVSequencerArpeggio to add the new arpeggio data set to
 * @param entries the number of arpeggio trigger entries to be used in the arpeggio data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_arpeggio_data_open(AVSequencerArpeggio *arpeggio, uint32_t entries);

/**
 * Creates a new uninitialized empty audio sample.
 *
 * @return pointer to freshly allocated AVSequencerSample, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerSample *avseq_sample_create(void);

/**
 * Opens and registers a new audio sample to an instrument.
 *
 * @param instrument the AVSequencerInstrument structure to add the new sample to
 * @param sample the AVSequencerSample to be added to the instrument
 * @param data the original sample data to create a redirection sample or NULL for a new one
 * @param length the number of samples to allocate initially if not a redirection sample
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_sample_open(AVSequencerInstrument *instrument, AVSequencerSample *sample,
                      int16_t *data, uint32_t length);

/**
 * Opens and registers audio sample PCM data stream to an sample.
 *
 * @param sample the AVSequencerSample to add the sample PCM data stream to
 * @param data the original sample data to create a redirection sample or NULL for a new one
 * @param samples the number of samples to allocate initially if not a redirection sample
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_sample_data_open(AVSequencerSample *sample, int16_t *data, uint32_t samples);

/**
 * Creates a new uninitialized empty synth sound.
 *
 * @return pointer to freshly allocated AVSequencerSynth, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerSynth *avseq_synth_create(void);

/**
 * Opens and registers a synth sound to a sample.
 *
 * @param sample the AVSequencerSample structure to attach the new synth sound to
 * @param lines the number of synth code lines to be used for the new synth sound
 * @param waveforms the number of waveforms to allocate at once for the new synth sound
 * @param samples the number of samples to allocate for each waveform in the new synth sound
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_open(AVSequencerSample *sample, uint32_t lines,
                     uint32_t waveforms, uint32_t samples);

/**
 * Creates a new uninitialized empty synth sound waveform.
 *
 * @return pointer to freshly allocated AVSequencerSynthWave, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerSynthWave *avseq_synth_waveform_create(void);

/**
 * Opens and registers a synth sound waveform to a synth sound.
 *
 * @param synth the AVSequencerSynth structure to add the new synth sound waveform to
 * @param samples the number of samples to allocate to the new synth sound waveform
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_waveform_open(AVSequencerSynth *synth, uint32_t samples);

/**
 * Opens and registers synth sound waveform data to a synth sound waveform.
 *
 * @param waveform the AVSequencerSynthWave structure to attach the synth sound waveform data to
 * @param samples the number of samples to allocate for the synth sound waveform data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_waveform_data_open(AVSequencerSynthWave *waveform, uint32_t samples);

/**
 * Opens and registers a synth sound code to a synth sound.
 *
 * @param synth the AVSequencerSynth structure to attach the new synth sound code to
 * @param lines the number of synth code lines to be used for the new synth sound
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_code_open(AVSequencerSynth *synth, uint32_t lines);

/**
 * Executes one tick of the playback handler, calculating everything
 * needed for the next step of the mixer. This function usually is
 * called from the mixing engines when they processed all channel data
 * and need to run the next tick of playback to further full their
 * output buffers. This function might also be called from a hardware
 * and/or software interrupts on some platforms.
 *
 * @param avctx the AVSequencerContext of which to process the next tick
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_playback_handler ( AVSequencerContext *avctx );

#endif /* AVSEQUENCER_API_H */
