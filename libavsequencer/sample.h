/*
 * AVSequencer samples management
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

#ifndef AVSEQUENCER_SAMPLE_H
#define AVSEQUENCER_SAMPLE_H

#include "libavformat/avformat.h"
#include "libavsequencer/avsequencer.h"
#include "libavsequencer/instr.h"
#include "libavsequencer/synth.h"

/**
 * Sample structure used by all instruments which are either
 * have samples attached or are hybrids.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerSample {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Metadata information: Original sample file name, sample name,
     *  artist and comment.  */
    AVMetadata *metadata;

    /** AVSequencerSynth pointer to synth sound structure or NULL
       if this is neither a synth nor a hybrid.  */
    AVSequencerSynth *synth;

    /** Pointer to raw sample data, must be padded for
       perfect perfomance gain when accessing sample data.
       This can be NULL in case if this is a MIDI instrument
       or a synth sound instead.  */
    int16_t *data;

    /** Number of bytes the raw sample data occupies. 0 for
       MIDI instruments and synth sounds.  */
    uint32_t size;

    /** Number of samples of the raw sample data or 0 for
       MIDI instruments and synth sounds.  */
    uint32_t samples;

    /** Sample repeat starting loop point. If looping is enabled, this
       will be used as restart point.  */
    uint32_t repeat;

    /** Sample repeat length. End loop point is repeat + rep_len.  */
    uint32_t rep_len;

    /** Sample repeat count. Some sequencers allow to not only define
       an infinite repeat count but specify that more precisely.
       In that case, set this to a non zero value indicating the
       number of loop counts.  */
    uint32_t rep_count;

    /** Sample sustain repeat starting loop point. If sustain looping is
       enabled, this will be used as sustain restart point.
       Sustain loop is triggered by a note keyoff event.  */
    uint32_t sustain_repeat;

    /** Sample sustain repeat length. End sustain loop point is
       sustain_repeat + sustain_rep_len.  */
    uint32_t sustain_rep_len;

    /** Sample sustain repeat count. Some sequencers allow to not only
       define an infinite sustain repeat count but specify that more
       precisely. In that case, this has to be set to a non-zero value
       indicating the number of sustain loop counts.  */
    uint32_t sustain_rep_count;

    /** Sampling rate (frequency) in Hz to play C-4 at which defaults
       to 8363 (NTSC base frequency used by 60Hz sequencers).  */
    uint32_t rate;

    /** Lower sample rate limit (the sample can never exceed this
       minimum allowed frequency rate during playback).  */
    uint32_t rate_min;

    /** Upper sample rate limit (the sample can never exceed this
       maximum allowed frequency rate during playback).  */
    uint32_t rate_max;

    /** Initial sample offset to start playback at (usually 0).  */
    uint32_t start_offset;

    /** Sample bit depth (currently samples having bit depths from
       1 to 32 are supported, default is 8-bit sample).  */
    uint8_t bits_per_sample;

    /** Sample transpose. This is a relative number of half-tones to
       be added to the note calculation (defaults to 0).  */
    int8_t transpose;

    /** Sample fine-tuning control. This is a relative number in
       one of 128th a half-tone for fine sampling rate adjustments
       (default is 0 = no fine-tuning).  */
    int8_t finetune;

    /** Compatibility flags for playback. There are rare cases
       where sample loop control has to be handled a different
       way, or a different policy for no sample specified cases.  */
    uint8_t compat_flags;
    enum AVSequencerSampleCompatFlags {
    AVSEQ_SAMPLE_COMPAT_FLAG_AFFECT_CHANNEL_PAN     = 0x01, ///< Sample panning affects channel panning (IT compatibility)
    AVSEQ_SAMPLE_COMPAT_FLAG_VOLUME_ONLY            = 0x02, ///< If a note without a sample is played, only the sample volume will be left unchanged
    AVSEQ_SAMPLE_COMPAT_FLAG_START_TONE_PORTAMENTO  = 0x04, ///< If a tone portamento with a note is executed but no note is currently played, the tone portamento will be ignored and start playing the note normally
    AVSEQ_SAMPLE_COMPAT_FLAG_PLAY_BEGIN_TONE_PORTA  = 0x08, ///< If you change a sample within a tone portamento the sample will be played from beginning
    };

    /** Sample playback flags. Some sequencers feature
       surround panning or allow different types of loop control
       differend types of frequency tables which have to be taken
       care specially in the internal playback engine.  */
    uint8_t flags;
    enum AVSequencerSampleFlags {
    AVSEQ_SAMPLE_FLAG_REDIRECT          = 0x01, ///< Sample is a redirection (symbolic link)
    AVSEQ_SAMPLE_FLAG_LOOP              = 0x02, ///< Use normal loop points
    AVSEQ_SAMPLE_FLAG_SUSTAIN_LOOP      = 0x04, ///< Use sustain loop points
    AVSEQ_SAMPLE_FLAG_SAMPLE_PANNING    = 0x08, ///< Use sample panning
    AVSEQ_SAMPLE_FLAG_SURROUND_PANNING  = 0x10, ///< Sample panning is surround panning
    AVSEQ_SAMPLE_FLAG_REVERSE           = 0x40, ///< Sample will be initially played backwards
    };

    /** Sample repeat mode. Some sequencers allow to define
       different loop modes. There is a normal forward loop mode,
       a normal backward loop and a ping-pong loop mode (switch
       between forward and backward looping each touch of loop
       points).  */
    uint8_t rep_mode;
    enum AVSequencerSampleRepMode {
    AVSEQ_SAMPLE_REP_MODE_BACKWARDS = 0x01, ///< Use always backward instead of always forward loop
    AVSEQ_SAMPLE_REP_MODE_PINGPONG  = 0x02, ///< Use ping-pong loop mode, i.e. forward <-> backward
    };

    /** Sample sustain loop mode. Some sequencers allow to define
       different loop types. There is a normal forward sustain loop
       mode, a normal backward sustain loop and a ping-pong sustain
       loop mode (switch between forward and backward looping each
       touch of sustain loop points).  */
    uint8_t sustain_rep_mode;
    enum AVSequencerSampleSustainRepMode {
    AVSEQ_SAMPLE_SUSTAIN_REP_MODE_BACKWARDS = 0x01, ///< Use always backward instead of always forward loop
    AVSEQ_SAMPLE_SUSTAIN_REP_MODE_PINGPONG  = 0x02, ///< Use ping-pong loop mode, i.e. forward <-> backward
    };

    /** Sample global volume. This will scale all volume operations
       of this sample (default is 255 = no scaling).  */
    uint8_t global_volume;

    /** Sample initial volume (defaults to 255 = maximum).  */
    uint8_t volume;

    /** Sub-volume level for this sample. This is basically sample
       volume divided by 256, but the sub-volume doesn't
       account into actual mixer output (defaults to 0).  */
    uint8_t sub_volume;

    /** Stereo panning level for this sample (defaults to
       -128 = central stereo panning) if instrument panning
        is not used.  */
    int8_t panning;

    /** Stereo sub-panning level for this sample. This is
       basically sample panning divided by 256, but the sub-panning
       doesn't account into actual mixer output (defaults 0).  */
    uint8_t sub_panning;

    /** Pointer to envelope data interpreted as auto vibrato
       waveform control or NULL for turn off auto vibrato.  */
    AVSequencerEnvelope *auto_vibrato_env;

    /** Pointer to envelope data interpreted as auto tremolo
       waveform control or NULL for turn off auto tremolo.  */
    AVSequencerEnvelope *auto_tremolo_env;

    /** Pointer to envelope data interpreted as auto pannolo
       waveform control or NULL for turn off auto pannolo.  */
    AVSequencerEnvelope *auto_pannolo_env;

    /** Auto vibrato / tremolo / pannolo envelope usage flags.
       Some sequencers feature reloading of envelope data when
       a new note is played.  */
    uint8_t env_usage_flags;
    enum AVSequencerSampleEnvUsageFlags {
    AVSEQ_SAMPLE_FLAG_USE_AUTO_VIBRATO_ENV  = 0x01, ///< Use (reload) auto vibrato envelope
    AVSEQ_SAMPLE_FLAG_USE_AUTO_TREMOLO_ENV  = 0x02, ///< Use (reload) auto tremolo envelope
    AVSEQ_SAMPLE_FLAG_USE_AUTO_PANNOLO_ENV  = 0x04, ///< Use (reload) auto pannolo envelope
    };

    /** Auto vibrato / tremolo / pannolo envelope processing flags.
       Sequencers differ in the way how they handle envelopes.
       Some first increment envelope node and then get the data and
       others first get the data and then increment the envelope data.  */
    uint8_t env_proc_flags;
    enum AVSequencerSampleEnvProcFlags {
    AVSEQ_SAMPLE_FLAG_PROC_AUTO_VIBRATO_ENV = 0x01, ///< Add first, then get auto vibrato envelope value
    AVSEQ_SAMPLE_FLAG_PROC_AUTO_TREMOLO_ENV = 0x02, ///< Add first, then get auto tremolo envelope value
    AVSEQ_SAMPLE_FLAG_PROC_AUTO_PANNOLO_ENV = 0x04, ///< Add first, then get auto pannolo envelope value
    AVSEQ_SAMPLE_FLAG_PROC_LINEAR_AUTO_VIB  = 0x80, ///< Use linear frequency table for auto vibrato
    };

    /** Auto vibrato / tremolo / pannolo envelope retrigger flags.
       Sequencers differ in the way how they handle envelopes restart.
       Some continue the previous instrument envelope when a new
       instrument does not define an envelope, others disable this
       envelope instead.  */
    uint8_t env_retrig_flags;
    enum AVSequencerSampleEnvRetrigFlags {
    AVSEQ_SAMPLE_FLAG_RETRIG_AUTO_VIBRATO_ENV   = 0x01, ///< Not retrigger auto vibrato envelope
    AVSEQ_SAMPLE_FLAG_RETRIG_AUTO_TREMOLO_ENV   = 0x02, ///< Not retrigger auto tremolo envelope
    AVSEQ_SAMPLE_FLAG_RETRIG_AUTO_PANNOLO_ENV   = 0x04, ///< Not retrigger auto pannolo envelope
    };

    /** Auto vibrato / tremolo / pannolo envelope randomize flags.
       Sequencers allow to use data from a pseudo random number generator.
       If the approciate bit is set, the envelope data will be
       randomized each access.  */
    uint8_t env_random_flags;
    enum AVSequencerSampleEnvRandomFlags {
    AVSEQ_SAMPLE_FLAG_RANDOM_AUTO_VIBRATO_ENV   = 0x01, ///< Randomize auto vibrato envelope
    AVSEQ_SAMPLE_FLAG_RANDOM_AUTO_TREMOLO_ENV   = 0x02, ///< Randomize auto tremolo envelope
    AVSEQ_SAMPLE_FLAG_RANDOM_AUTO_PANNOLO_ENV   = 0x04, ///< Randomize auto pannolo envelope
    };

    /** Auto vibrato sweep.  */
    uint16_t vibrato_sweep;

    /** Auto vibrato depth.  */
    uint8_t vibrato_depth;

    /** Auto vibrato rate (speed).  */
    uint8_t vibrato_rate;

    /** Auto tremolo sweep.  */
    uint16_t tremolo_sweep;

    /** Auto tremolo depth.  */
    uint8_t tremolo_depth;

    /** Auto tremolo rate (speed).  */
    uint8_t tremolo_rate;

    /** Auto pannolo sweep.  */
    uint16_t pannolo_sweep;

    /** Auto panoolo depth.  */
    uint8_t pannolo_depth;

    /** Auto pannolo rate (speed).  */
    uint8_t pannolo_rate;

    /** Array of pointers containing every unknown data field where
       the last element is indicated by a NULL pointer reference. The
       first 64-bit of the unknown data contains an unique identifier
       for this chunk and the second 64-bit data is actual unsigned
       length of the following raw data. Some formats are chunk based
       and can store information, which can't be handled by some
       other, in case of a transition the unknown data is kept as is.
       Some programs write editor settings for samples in those
       chunks, which then won't get lost in that case.  */
    uint8_t **unknown_data;
} AVSequencerSample;

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

#endif /* AVSEQUENCER_SAMPLE_H */
