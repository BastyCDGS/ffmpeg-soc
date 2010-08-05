/*
 * AVSequencer instrument management
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

#ifndef AVSEQUENCER_INSTR_H
#define AVSEQUENCER_INSTR_H

#include "libavformat/avformat.h"
#include "libavsequencer/sample.h"

/**
 * Envelope structure used by instruments to apply volume / panning
 * or pitch manipulation according to an user defined waveform.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerEnvelope {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** The actual node data of this envelope as signed 16-bit integer.
       For a volume envelope, we have a default scale range of -32767
       to +32767, for panning envelopes the scale range is between -8191
       to +8191. For slide, vibrato, tremolo, pannolo (and their auto
       versions), the scale range is between -256 to +256.  */
    int16_t *data;

    /** The node points values or 0 if the envelope is empty.  */
    uint16_t *node_points;

    /** Number of dragable nodes of this envelope (defaults to 12).  */
    uint16_t nodes;

    /** Number of envelope points, i.e. node data values which
       defaults to 64.  */
    uint16_t points;

    /** Instrument envelope flags. Some sequencers feature
       loop points of various kinds, which have to be taken
       care specially in the internal playback engine.  */
    uint16_t flags;
    enum AVSequencerEnvelopeFlags {
    AVSEQ_ENVELOPE_LOOP             = 0x0001, ///< Envelope uses loop nodes
    AVSEQ_ENVELOPE_SUSTAIN          = 0x0002, ///< Envelope uses sustain nodes
    AVSEQ_ENVELOPE_PINGPONG         = 0x0004, ///< Envelope loop is in ping pong mode
    AVSEQ_ENVELOPE_SUSTAIN_PINGPONG = 0x0008, ///< Envelope sustain loop is in ping pong mode
    };

    /** Envelope tempo in ticks (defaults to 1, i.e. change envelope
       at every frame / tick).  */
    uint16_t tempo;

    /** Envelope sustain loop start point.  */
    uint16_t sustain_start;

    /** Envelope sustain loop end point.  */
    uint16_t sustain_end;

    /** Envelope sustain loop repeat counter for loop range.  */
    uint16_t sustain_count;

    /** Envelope loop repeat start point.  */
    uint16_t loop_start;

    /** Envelope loop repeat end point.  */
    uint16_t loop_end;

    /** Envelope loop repeat counter for loop range.  */
    uint16_t loop_count;

    /** Randomized lowest value allowed.  */
    int16_t value_min;

    /** Randomized highest value allowed.  */
    int16_t value_max;

    /** Array of pointers containing every unknown data field where
       the last element is indicated by a NULL pointer reference. The
       first 64-bit of the unknown data contains an unique identifier
       for this chunk and the second 64-bit data is actual unsigned
       length of the following raw data. Some formats are chunk based
       and can store information, which can't be handled by some
       other, in case of a transition the unknown data is kept as is.
       Some programs write editor settings for envelopes in those
       chunks, which then won't get lost in that case.  */
    uint8_t **unknown_data;
} AVSequencerEnvelope;

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
 * Keyboard definitions structure used by instruments to map
 * note to samples. C-0 is first key. B-9 is 120th key.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerKeyboard {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    struct AVSequencerKeyboardEntry {
        /** Sample number for this keyboard note.  */
        uint16_t sample;

        /** Octave value for this keyboard note.  */
        uint8_t octave;

        /** Note value for this keyboard note.  */
        uint8_t note;
    } key[120];
} AVSequencerKeyboard;

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
 * Arpeggio data structure, This structure is actually for one tick
 * and therefore actually pointed as an array with the amount of
 * different ticks handled by the arpeggio control.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerArpeggioData {
    /** Packed note or 0 if this is an arpeggio note.  */
    uint8_t tone;

    /** Transpose for this arpeggio tick.  */
    int8_t transpose;

    /** Instrument number to switch to or 0 for original instrument.  */
    uint16_t instrument;

    /** The four effect command bytes which are executed.  */
    uint8_t command[4];

    /** The four data word values of the four effect command bytes.  */
    uint16_t data[4];
} AVSequencerArpeggioData;

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
int avseq_arpeggio_data_open(AVSequencerApreggio *arpeggio, uint32_t entries);

/**
 * Arpeggio control envelope used by all instrumental stuff.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerArpeggio {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** AVSequencerArpeggioData pointer to arpeggio data structure.  */
    AVSequencerArpeggioData *data;

    /** Instrument arpeggio control flags. Some sequencers feature
       customized arpeggio command control.which have to be taken
       care specially in the internal playback engine.  */
    uint16_t flags;
    enum AVSequencerArpeggioFlags {
    AVSEQ_ARPEGGIO_FLAG_LOOP                = 0x0001, ///< Arpeggio control is looped
    AVSEQ_ARPEGGIO_FLAG_SUSTAIN             = 0x0002, ///< Arpeggio control has a sustain loop
    AVSEQ_ARPEGGIO_FLAG_PINGPONG            = 0x0004, ///< Arpeggio control will be looped in ping pong mpde
    AVSEQ_ARPEGGIO_FLAG_SUSTAIN_PINGPONG    = 0x0008, ///< Arpeggio control will have sustain loop ping pong mode enabled
    };

    /** Number of arpeggio ticks handled by this arpeggio control
       (defaults to 3 points as in normal arpeggio command).  */
    uint16_t entries;

    /** Sustain loop start tick of arpeggio control.  */
    uint16_t sustain_start;

    /** Sustain loop end tick of arpeggio control.  */
    uint16_t sustain_end;

    /** Sustain loop count number of how often to repeat loop
       of arpeggio control.  */
    uint16_t sustain_count;

    /** Loop start tick of arpeggio control.  */
    uint16_t loop_start;

    /** Loop end tick of arpeggio control.  */
    uint16_t loop_end;

    /** Loop count number of how often to repeat loop of arpeggio
       control.  */
    uint16_t loop_count;
} AVSequencerArpeggio;

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
int avseq_arpeggio_open(AVSequencerModule *module, AVSequencerApreggio *arpeggio,
                        uint32_t entries);

/**
 * Instrument structure used by all instrumental stuff.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerInstrument {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Metadata information: Original instrument file name, instrument
     *  name, artist and comment.  */
    AVMetadata *metadata;

    /** Array (of size samples) of pointers containing every sample
       used by this instrument.  */
    AVSequencerSample **sample_list;

    /** Number of samples associated with this instrument
       (a maximum number of 255 attached samples is allowed).
       The default is one attached sample.  */
    uint8_t samples;

    /** Pointer to envelope data interpreted as volume control
       or NULL if volume envelope control is not used.  */
    AVSequencerEnvelope *volume_env;

    /** Pointer to envelope data interpreted as panning control
       or NULL if panning envelope control is not used.  */
    AVSequencerEnvelope *panning_env;

    /** Pointer to envelope data interpreted as pitch and slide
       control or NULL if slide envelope control is not used.  */
    AVSequencerEnvelope *slide_env;

    /** Pointer to envelope data interpreted as vibrato waveform
       control or NULL if vibrato envelope is not used.  */
    AVSequencerEnvelope *vibrato_env;

    /** Pointer to envelope data interpreted as tremolo waveform
       control or NULL if tremolo envelope is not used.  */
    AVSequencerEnvelope *tremolo_env;

    /** Pointer to envelope data interpreted as pannolo / panbrello
       waveform control or NULL if pannolo envelope is not used.  */
    AVSequencerEnvelope *pannolo_env;

    /** Pointer to envelope data interpreted as channolo waveform
       control or NULL if channolo envelope is not used.  */
    AVSequencerEnvelope *channolo_env;

    /** Pointer to envelope data interpreted as spenolo waveform
       control or NULL if spenolo envelope is not used.  */
    AVSequencerEnvelope *spenolo_env;

    /** Pointer to envelope data interpreted as resonance filter
       control or NULL if resonance filter is unused.  */
    AVSequencerEnvelope *resonance_env;

    /** Pointer to arpeggio control structure to be used for custom
        apreggios or NULL if this instrument uses standard
        arpeggio behaviour.  */
    AVSequencerArpeggio *arpeggio_ctrl;

    /** Pointer to instrument keyboard definitions which maps
       the octave/instrument-pair to an associated sample.  */
    AVSequencerKeyboard *keyboard_defs;

    /** Global volume scaling instrument samples.  */
    uint8_t global_volume;

    /** NNA (New Note Action) mode.  */
    uint8_t nna;
    enum AVSequencerInstrumentNNA {
    AVSEQ_INSTRUMENT_NNA_NOTE_CUT       = 0x00, ///< Cut previous note
    AVSEQ_INSTRUMENT_NNA_NOTE_CONTINUE  = 0x01, ///< Continue previous note
    AVSEQ_INSTRUMENT_NNA_NOTE_OFF       = 0x02, ///< Perform key-off on previous note
    AVSEQ_INSTRUMENT_NNA_NOTE_FADE      = 0x03, ///< Perform fadeout on previous note
    };

    /** Random note swing in semi-tones. This value will cause
       a flip between each play of this instrument making it
       sounding more natural.  */
    uint8_t note_swing;

    /** Random volume swing in 1/256th steps (i.e. 256 means 100%).
       The volume will vibrate randomnessly around that volume
       percentage and make the instrument sound more like a
       natural playing.  */
    uint16_t volume_swing;

    /** Random panning swing, will cause the stereo position to
       vary a bit each instrument play to make it sound more naturally
       played.  */
    uint16_t panning_swing;

    /** Random pitch swing in 1/65536th steps, i.e. 65536 means 100%.  */
    uint32_t pitch_swing;

    /** Pitch panning separation.  */
    int16_t pitch_pan_separation;

    /** Default panning for all samples.  */
    uint8_t default_panning;

    /** Default sub-panning for all samples.  */
    uint8_t default_sub_pan;

    /** Duplicate note check type.  */
    uint8_t dct;
    enum AVSequencerInstrumentDCT {
    AVSEQ_INSTRUMENT_DCT_INSTR_NOTE_OR      = 0x01, ///< Check for duplicate OR instrument notes
    AVSEQ_INSTRUMENT_DCT_SAMPLE_NOTE_OR     = 0x02, ///< Check for duplicate OR sample notes
    AVSEQ_INSTRUMENT_DCT_INSTR_OR           = 0x04, ///< Check for duplicate OR instruments
    AVSEQ_INSTRUMENT_DCT_SAMPLE_OR          = 0x08, ///< Check for duplicate OR samples
    AVSEQ_INSTRUMENT_DCT_INSTR_NOTE_AND     = 0x10, ///< Check for duplicate AND instrument notes
    AVSEQ_INSTRUMENT_DCT_SAMPLE_NOTE_AND    = 0x20, ///< Check for duplicate AND sample notes
    AVSEQ_INSTRUMENT_DCT_INSTR_AND          = 0x40, ///< Check for duplicate AND instruments
    AVSEQ_INSTRUMENT_DCT_SAMPLE_AND         = 0x80, ///< Check for duplicate AND samples
    };

    /** Duplicate note check action.  */
    uint8_t dna;
    enum AVSequencerInstrumentDNA {
    AVSEQ_INSTRUMENT_DNA_NOTE_CUT       = 0x00. ///< Do note cut on duplicate note
    AVSEQ_INSTRUMENT_DNA_NOTE_OFF       = 0x01. ///< Perform keyoff on duplicate note
    AVSEQ_INSTRUMENT_DNA_NOTE_FADE      = 0x02. ///< Fade off notes on duplicate note
    AVSEQ_INSTRUMENT_DNA_NOTE_CONTINUE  = 0x03. ///< Nothing (only useful for synth sound handling)
    };

    /** Compatibility flags for playback. There are rare cases
       where instrument to sample mapping has to be handled
       a different way, or a different policy for no sample
       specified cases.  */
    uint8_t compat_flags;
    enum AVSequencerInstrumentCompatFlags {
    AVSEQ_INSTRUMENT_COMPAT_FLAG_LOCK_INSTR_WAVE    = 0x01, ///< Instrument wave is locked as in MOD, but volume / panning / etc. is taken, if both bits are clear it will handle like S3M/IT, i.e. instrument is changed
    AVSEQ_INSTRUMENT_COMPAT_FLAG_AFFECT_CHANNEL_PAN = 0x02, ///< Instrument panning affects channel panning (IT compatibility)
    AVSEQ_INSTRUMENT_COMPAT_FLAG_PREV_SAMPLE        = 0x04, ///< If no sample in keyboard definitions, use previous one
    AVSEQ_INSTRUMENT_COMPAT_FLAG_SEPARATE_SAMPLES   = 0x08, ///< Use absolute instead of relative sample values (IT compatibility)
    };

    /** Instrument playback flags. Some sequencers feature
       surround panning or allow different types of envelope
       interpretations, differend types of slides which have to
       be taken care specially in the internal playback engine.  */
    uint8_t flags;
    enum AVSequencerInstrumentFlags {
    AVSEQ_INSTRUMENT_FLAG_NO_TRANSPOSE          = 0x01, ///< Instrument can't be transpoed by the order list
    AVSEQ_INSTRUMENT_FLAG_PORTA_SLIDE_ENV       = 0x02, ///< Slide envelopes will be portamento values, otherwise transpose + finetune
    AVSEQ_INSTRUMENT_FLAG_LINEAR_SLIDE_ENV      = 0x04, ///< Use linear freqency table for slide envelope for portamento mode
    AVSEQ_INSTRUMENT_FLAG_DEFAULT_PANNING       = 0x10, ///< Use instrument panning and override sample default panning
    AVSEQ_INSTRUMENT_FLAG_SURROUND_PANNING      = 0x20, ///< Use surround sound as default instrument panning
    AVSEQ_INSTRUMENT_FLAG_NO_INSTR_TRANSPOSE    = 0x40, ///< Order instrument transpose doesn't apply to this instrument
    };

    /** Envelope usage flags. Some sequencers feature
       reloading of envelope data when a new note is played.  */
    uint16_t env_usage_flags;
    enum AVSequencerInstrumentEnvUsageFlags {
    AVSEQ_INSTRUMENT_FLAG_USE_VOLUME_ENV            = 0x0001, ///< Use (reload) volume envelope
    AVSEQ_INSTRUMENT_FLAG_USE_PANNING_ENV           = 0x0002, ///< Use (reload) panning envelope
    AVSEQ_INSTRUMENT_FLAG_USE_SLIDE_ENV             = 0x0004, ///< Use (reload) slide envelope
    AVSEQ_INSTRUMENT_FLAG_USE_VIBRATO_ENV           = 0x0008, ///< Use (reload) vibrato envelope
    AVSEQ_INSTRUMENT_FLAG_USE_TREMOLO_ENV           = 0x0010, ///< Use (reload) tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_USE_PANNOLO_ENV           = 0x0020, ///< Use (reload) pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_USE_CHANNOLO_ENV          = 0x0040, ///< Use (reload) channolo envelope
    AVSEQ_INSTRUMENT_FLAG_USE_SPENOLO_ENV           = 0x0080, ///< Use (reload) spenolo envelope
    AVSEQ_INSTRUMENT_FLAG_USE_TRACK_TREMOLO_ENV     = 0x0100, ///< Use (reload) track tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_USE_TRACK_PANNOLO_ENV     = 0x0200, ///< Use (reload) track pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_USE_GLOBAL_TREMOLO_ENV    = 0x0400, ///< Use (reload) global tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_USE_GLOBAL_PANNOLO_ENV    = 0x0800, ///< Use (reload) global pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_USE_RESONANCE_ENV         = 0x1000, ///< Use (reload) resonance filter
    };

    /** Envelope processing flags. Some sequencers differ in the
       way how they handle envelopes. Some first increment
       envelope node and then get the data and some do first
       get the data and then increment the envelope data.  */
    uint16_t env_proc_flags;
    enum AVSequencerInstrumentEnvProcFlags {
    AVSEQ_INSTRUMENT_FLAG_PROC_VOLUME_ENV           = 0x0001, ///< Add first, then get volume envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_PANNING_ENV          = 0x0002, ///< Add first, then get panning envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_SLIDE_ENV            = 0x0004, ///< Add first, then get slide envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_VIBRATO_ENV          = 0x0008, ///< Add first, then get vibrato envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_TREMOLO_ENV          = 0x0010, ///< Add first, then get tremolo envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_PANNOLO_ENV          = 0x0020, ///< Add first, then get pannolo envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_CHANNOLO_ENV         = 0x0040, ///< Add first, then get channolo envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_SPENOLO_ENV          = 0x0080, ///< Add first, then get spenolo envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_TRACK_TREMOLO_ENV    = 0x0100, ///< Add first, then get track tremolo envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_TRACK_PANNOLO_ENV    = 0x0200, ///< Add first, then get track pannolo envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_GLOBAL_TREMOLO_ENV   = 0x0400, ///< Add first, then get global tremolo envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_GLOBAL_PANNOLO_ENV   = 0x0800, ///< Add first, then get global pannolo envelope value
    AVSEQ_INSTRUMENT_FLAG_PROC_RESONANCE_ENV        = 0x1000, ///< Add first, then get resonance filter value
    };

    /** Envelope retrigger flags. Some sequencers differ in the
       way how they handle envelopes restart. Some continue
       the previous instrument envelope when an new instrument
       does not define an envelope, others disable this
       envelope instead.  */
    uint16_t env_retrig_flags;
    enum AVSequencerInstrumentEnvRetrigFlags {
    AVSEQ_INSTRUMENT_FLAG_RETRIG_VOLUME_ENV         = 0x0001, ///< Not retrigger volume envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_PANNING_ENV        = 0x0002, ///< Not retrigger panning envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_SLIDE_ENV          = 0x0004, ///< Not retrigger slide envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_VIBRATO_ENV        = 0x0008, ///< Not retrigger vibrato envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_TREMOLO_ENV        = 0x0010, ///< Not retrigger tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_PANNOLO_ENV        = 0x0020, ///< Not retrigger pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_CHANNOLO_ENV       = 0x0040, ///< Not retrigger channolo envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_SPENOLO_ENV        = 0x0080, ///< Not retrigger spenolo envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_TRACK_TREMOLO_ENV  = 0x0100, ///< Not retrigger track tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_TRACK_PANNOLO_ENV  = 0x0200, ///< Not retrigger track pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_GLOBAL_TREMOLO_ENV = 0x0400, ///< Not retrigger global tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_GLOBAL_PANNOLO_ENV = 0x0800, ///< Not retrigger global pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RETRIG_RESONANCE_ENV      = 0x1000, ///< Not retrigger resonance filter
    };

    /** Envelope randomize flags. Some sequencers allow to use
       data from a pseudo random number generator. If the
       approciate bit is set, the envelope data will be
       randomized each access.  */
    uint16_t env_random_flags;
    enum AVSequencerInstrumentEnvRandomFlags {
    AVSEQ_INSTRUMENT_FLAG_RANDOM_VOLUME_ENV         = 0x0001, ///< Randomize volume envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_PANNING_ENV        = 0x0002, ///< Randomize panning envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_SLIDE_ENV          = 0x0004, ///< Randomize slide envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_VIBRATO_ENV        = 0x0008, ///< Randomize vibrato envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_TREMOLO_ENV        = 0x0010, ///< Randomize tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_PANNOLO_ENV        = 0x0020, ///< Randomize pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_CHANNOLO_ENV       = 0x0040, ///< Randomize channolo envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_SPENOLO_ENV        = 0x0080, ///< Randomize spenolo envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_TRACK_TREMOLO_ENV  = 0x0100, ///< Randomize track tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_TRACK_PANNOLO_ENV  = 0x0200, ///< Randomize track pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_GLOBAL_TREMOLO_ENV = 0x0400, ///< Randomize global tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_GLOBAL_PANNOLO_ENV = 0x0800, ///< Randomize global pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RANDOM_RESONANCE_ENV      = 0x1000, ///< Randomize resonance filter
    );

    /** Envelope randomize delay flags. Some sequencers allow
       to specify a time interval when a new random value
       can be read.  */
    uint16_t env_rnd_delay_flags;
    enum AVSequencerInstrumentEnvRndDelayFlags {
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_VOLUME_ENV          = 0x0001, ///< Speed is randomized delay for volume envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_PANNING_ENV         = 0x0002, ///< Speed is randomized delay for panning envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_SLIDE_ENV           = 0x0004, ///< Speed is randomized delay for slide envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_VIBRATO_ENV         = 0x0008, ///< Speed is randomized delay for vibrato envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_TREMOLO_ENV         = 0x0010, ///< Speed is randomized delay for tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_PANNOLO_ENV         = 0x0020, ///< Speed is randomized delay for pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_CHANNOLO_ENV        = 0x0040, ///< Speed is randomized delay for channolo envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_SPENOLO_ENV         = 0x0080, ///< Speed is randomized delay for spenolo envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_TRACK_TREMOLO_ENV   = 0x0100, ///< Speed is randomized delay for track tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_TRACK_PANNOLO_ENV   = 0x0200, ///< Speed is randomized delay for track pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_GLOBAL_TREMOLO_ENV  = 0x0400, ///< Speed is randomized delay for global tremolo envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_GLOBAL_PANNOLO_ENV  = 0x0800, ///< Speed is randomized delay for global pannolo envelope
    AVSEQ_INSTRUMENT_FLAG_RND_DELAY_RESONANCE_ENV       = 0x1000, ///< Speed is randomized delay for resonance filter
    };

    /** Fade out value which defaults to 65535 (full volume level as in XM).  */
    uint16_t fade_out;

    /** Hold value.  */
    uint16_t hold;

    /** Decay value.  */
    uint16_t decay;

    /** Decay action when decay is off.  */
    uint8_t dca;

    /** Pitch panning center (0 is C-0, 1 is C#1, 12 is C-1, 13 is C#1,
       24 is C-2, 36 is C-3 and so on. Defaults to 48 = C-4).  */
    uint8_t pitch_pan_center;

    /** MIDI channel this instrument is associated with.  */
    uint8_t midi_channel;

    /** MIDI program (instrument) this instrument maps to.  */
    uint8_t midi_program;

    /** MIDI flags. Some sequencers allow general MIDI support
       and can play certain instruments directly through a MIDI
       channel.  */
    uint8_t midi_flags;
    enum AVSequencerInstrumentMIDIFlags {
    AVSEQ_INSTRUMENT_FLAG_MIDI_TICK_QUANTIZE    = 0x01, ///< Tick quantize (insert note delays)
    AVSEQ_INSTRUMENT_FLAG_MIDI_NOTE_OFF         = 0x02, ///< Record note off (keyoff note)
    AVSEQ_INSTRUMENT_FLAG_MIDI_VELOCITY         = 0x04, ///< Record velocity
    AVSEQ_INSTRUMENT_FLAG_MIDI_AFTER_TOUCH      = 0x08, ///< Record after touch
    AVSEQ_INSTRUMENT_FLAG_MIDI_EXTERNAL_SYNC    = 0x10, ///< External synchronization when recording
    AVSEQ_INSTRUMENT_FLAG_MIDI_ENABLE           = 0x80, ///< MIDI enabled
    };

    /** MIDI transpose (in half-tones).  */
    int8_t midi_transpose;

    /** MIDI after touch percentage.  */
    uint8_t midi_after_touch;

    /** MIDI pitch bender (in half-tones).  */
    uint8_t midi_pitch_bender;

    /** Array of pointers containing every unknown data field where
       the last element is indicated by a NULL pointer reference. The
       first 64-bit of the unknown data contains an unique identifier
       for this chunk and the second 64-bit data is actual unsigned
       length of the following raw data. Some formats are chunk based
       and can store information, which can't be handled by some
       other, in case of a transition the unknown data is kept as is.
       Some programs write editor settings for instruments in those
       chunks, which then won't get lost in that case.  */
    uint8_t **unknown_data;
} AVSequencerInstrument;

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

#endif /* AVSEQUENCER_INSTR_H */
