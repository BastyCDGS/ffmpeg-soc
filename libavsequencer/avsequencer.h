/*
 * AVSequencer main header file which connects to AVFormat and AVCodec
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

#ifndef AVSEQUENCER_AVSEQUENCER_H
#define AVSEQUENCER_AVSEQUENCER_H

#define LIBAVSEQUENCER_VERSION_MAJOR 0
#define LIBAVSEQUENCER_VERSION_MINOR 0
#define LIBAVSEQUENCER_VERSION_MICRO 0

#define LIBAVSEQUENCER_VERSION_INT AV_VERSION_INT(LIBAVSEQUENCER_VERSION_MAJOR, \
                                                  LIBAVSEQUENCER_VERSION_MINOR, \
                                                  LIBAVSEQUENCER_VERSION_MICRO)
#define LIBAVSEQUENCER_VERSION     AV_VERSION(LIBAVSEQUENCER_VERSION_MAJOR,   \
                                              LIBAVSEQUENCER_VERSION_MINOR,   \
                                              LIBAVSEQUENCER_VERSION_MICRO)
#define LIBAVSEQUENCER_BUILD       LIBAVSEQUENCER_VERSION_INT

#define LIBAVSEQUENCER_IDENT       "Lavsequencer" AV_STRINGIFY(LIBAVSEQUENCER_VERSION)

/**
 * Returns LIBAVSEQUENCER_VERSION_INT constant.
 */
unsigned avsequencer_version(void);

/**
 * Returns the libavsequencer build-time configuration.
 */
const char *avsequencer_configuration(void);

/**
 * Returns the libavsequencer license.
 */
const char *avsequencer_license(void);

#include "libavutil/log.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"

/**
 * Identify the mixing engines.
 * The principle is roughly:
 *
 * If you add a mixer ID to this list, add it so that
 * 1. no value of a existing mixer ID changes (that would break ABI),
 * 2. it is as close as possible to similar mixers.
 */
enum AVSequencerMixerID {
    MIXER_ID_NULL,

    /* Integer based mixers */
    MIXER_ID_LQ, ///< Low quality mixer optimized for fastest playback
    MIXER_ID_HQ, ///< High quality mixer optimized for quality playback and disk writers
};

/** AVSequencerMixerContext->flags bitfield.  */
enum AVSequencerMixerContextFlags {
    AVSEQ_MIXER_CONTEXT_FLAG_STEREO     = 0x08, ///< This mixer supports stereo mixing in addition to mono
    AVSEQ_MIXER_CONTEXT_FLAG_SURROUND   = 0x10, ///< This mixer supports surround panning in addition to stereo panning
    AVSEQ_MIXER_CONTEXT_FLAG_AVFILTER   = 0x20, ///< This mixer supports additional audio filters if FFmpeg is compiled with AVFilter enabled
};

/**
 * Mixer context structure which is used to describe certain features
 * of registered mixers to the sequencer context.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerMixerContext {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Certain metadata describing this mixer, i.e. who developed
       it (artist) and a brief description of the features
       (comment).  */
    AVMetadata *metadata;

    /** Default mixing rate in Hz used by this mixer. This will
       usually set to the value which this mixer can handle the best
       way.  */
    uint32_t frequency;

    /** Minimum mixing rate in Hz supported by this mixer.  */
    uint32_t frequency_min;

    /** Maximum mixing rate in Hz supported by this mixer.  */
    uint32_t frequency_max;

    /** Default mixing buffer size preferred. This will usually set
       to the value which this mixer can handle the at best without
       causing jittering and too much lag.  */
    uint32_t buf_size;

    /** Minimum mixing buffer size supported by this mixer.  */
    uint32_t buf_size_min;

    /** Maximum mixing buffer size supported by this mixer.  */
    uint32_t buf_size_max;

    /** Default volume boost level. 65536 equals to 100% which
       means no boost.  */
    uint32_t volume_boost;

    /** Maximum number of channels supported by this mixer, some
       engines might support less channels than maximum allowed by
       the sequencer.  */
    uint16_t channels_max;

    /** Special flags indicating supported features by this mixer.  */
    uint8_t flags;
} AVSequencerMixerContext;

/** AVSequencerMixerData->flags bitfield.  */
enum AVSequencerMixerDataFlags {
    AVSEQ_MIXER_DATA_FLAG_ALLOCATED = 0x01, ///< The mixer is currently allocated and ready to use
    AVSEQ_MIXER_DATA_FLAG_MIXING    = 0x02, ///< The mixer is currently in actual mixing to output
    AVSEQ_MIXER_DATA_FLAG_STEREO    = 0x04, ///< The mixer is currently mixing in stereo mode instead of mono
    AVSEQ_MIXER_DATA_FLAG_FROZEN    = 0x08, ///< The mixer has been delayed by some external process like disk I/O writing
};

/**
 * Mixer data allocation structure which is used to allocate a mixer
 * for be used by the playback handler. This structure is also used
 * for setting global parameters like the output mixing rate, the
 * size of the mixing buffer, volume boost and the tempo which
 * decides when to call the actual playback handler.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerMixerData {
    /** Pointer to basic mixer context structure which describes
       the mixer features.  */
    AVSequencerMixerContext *mixctx;

    /** Current mixing rate in Hz which is used to output the
       calculated sample data from the channels.  */
    uint32_t rate;

    /** Pointer to the mixing output buffer for the calculated sample
       data from the channels. This is always SAMPLE_FMT_S32 in native
       endianess.  */
    int32_t *mix_buf;

    /** The current actual size of the output buffer for the
       calculated sample data from the channels.  */
    uint32_t mix_buf_size;

    /** The current volume boost level. 65536 equals to 100% which
       means no boost.  */
    uint32_t volume_boost;

    /** Left channel volume level. 65536 is full volume.  */
    uint32_t volume_left;

    /** Right channel volume level. 65536 is full volume.  */
    uint32_t volume_right;

    /** Speed of playback handler in AV_TIME_BASE fractional
       seconds.  */
    uint32_t tempo;

    /** Current maximum number of allocated channels. The more
       channels are used the more CPU power is required to
       calculate the output audio buffer.  */
    uint16_t channels_max;

    /** Current status flags for this mixer which contain information
       like if the mixer has been allocated, is currently mixing,
       output mode (stereo or mono) or if it frozen because of some
       delaying (like caused by disk I/O when using disk writers.  */
    uint8_t flags;
} AVSequencerMixerData;

/** AVSequencerMixerChannel->flags bitfield.  */
enum AVSequencerMixerChannelFlags {
    AVSEQ_MIXER_CHANNEL_FLAG_MUTED      = 0x01, ///< Channel is muted (i.e. processed but not outputted)
    AVSEQ_MIXER_CHANNEL_FLAG_SYNTH      = 0x02, ///< Start new synthetic waveform when old has finished playback
    AVSEQ_MIXER_CHANNEL_FLAG_LOOP       = 0x04, ///< Loop points are being used
    AVSEQ_MIXER_CHANNEL_FLAG_PINGPONG   = 0x08, ///< Channel loop is in ping-pong style
    AVSEQ_MIXER_CHANNEL_FLAG_BACKWARDS  = 0x10, ///< Channel is currently playing in backward direction
    AVSEQ_MIXER_CHANNEL_FLAG_BACK_LOOP  = 0x20, ///< Backward loop instead of forward
    AVSEQ_MIXER_CHANNEL_FLAG_SURROUND   = 0x40, ///< Use surround sound output for this channel
    AVSEQ_MIXER_CHANNEL_FLAG_PLAY       = 0x80, ///< This channel is currently playing a sample (i.e. enabled)
};

/**
 * Mixer channel structure which is used by the mixer to determine how
 * to mix the samples of each channel into the target output buffer.
 * This structure is actually for one mixing channel and therefore
 * actually pointed as an array with size of number of total amount of
 * allocated channels.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerMixerChannel {
    /** Current position in samples of this channel to be mixed to
       output data.  */
    uint32_t pos;

    /** Current length in samples for this channel.  */
    uint32_t len;

    /** Current sample data for this channel. Actual layout depends
       on number of bits per sample.  */
    int16_t *data;

    /** Current sample rate in Hz for this channel.  */
    uint32_t rate;

    /** Current repeat start in samples for this channel. If the loop
       flag is also set, the sample will be looped between
       repeat_start and repeat_start + repeat_length.  */
    uint32_t repeat_start;

    /** Current repeat length in samples for this channel. If the loop
       flag is also set, the sample will be looped between repeat_start
       and repeat_start + repeat_length.  */
    uint32_t repeat_length;

    /** Current repeat count in loop end point touches for this channel.
       If the loop flag is also set, the sample will be looped exactly
       the number of times this value between repeat_start and
       repeat_start + repeat_length, unless the repeat count is zero
       which means an unlimited repeat count.  */
    uint32_t repeat_count;

    /** Current number of loop end point touches for this channel.
       If the loop flag is also set, the sample will stop looping when
       this number reaches repeat_count between repeat_start and
       repeat_start + repeat_length, unless the repeat count is zero
       which means an unlimited repeat count.  */
    uint32_t repeat_counted;

    /** Number of bits per sample between 1 and 32. Mixers usually use
       special accelated code for 8, 16, 24 or 32 bits per sample.  */
    uint8_t bits_per_sample;

    /** Special flags which indicate things like a muted channel,
       start new synthetic waveform, if to use loop points, type of
       loop (normal forward, ping-pong or normal backward), if normal
       stereo or surround panning and if this channel is active.  */
    uint8_t flags;

    /** Current volume for this channel which ranges from 0 (muted)
       to 255 (full volume).  */
    uint8_t volume;

    /** Current stereo panning level for this channel (where 0-127
       indicate left stereo channel panning, -128 is central stereo
       panning and -127 to -1 indicate right stereo panning).  */
    int8_t panning;
} AVSequencerMixerChannel;

#include "libavsequencer/module.h"
#include "libavsequencer/song.h"
#include "libavsequencer/player.h"

/**
 * Sequencer context structure which is the very root of the
 * sequencer. It manages all modules currently in memory, controls
 * the playback stuff and declares some customizable lookup tables
 * for very strange sound formats. Also all registered mixing engines
 * are stored in this structure.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerContext {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Associated decoder packet for this sequencer context.  */
    AVPacket *pkt;

    /** AVSequencerPlayerGlobals pointer to global channel data.  */
    AVSequencerPlayerGlobals *player_globals;

    /** AVSequencerPlayerHostChannel pointer to host channel data.  */
    AVSequencerPlayerHostChannel *player_host_channel;

    /** AVSequencerPlayerChannel pointer to virtual channel data.  */
    AVSequencerPlayerChannel *player_channel;

    /** Current module used by current playback handler or NULL if
       no module is currently being processed.  */
    AVSequencerModule *player_module;

    /** Current sub-song used by current playback handler or NULL
       if no sub-song is currently being processed.  */
    AVSequencerSong *player_song;

    /** Current mixing engine used by current playback handler
       or NULL if there is no module and sub-song being processed.  */
    AVSequencerMixerData *player_mixer_data;

    /** Pointer to sine table for very fast sine calculation. Value
       is sin(x)*32767 with one element being one degree or NULL to
       use the internal one.  */
    int16_t *sine_lut;

    /** Pointer to linear frequency table for non-Amiga slide modes.
       Value is 65536*2^(x/3072) or NULL to use the internal one.  */
    uint16_t *linear_frequency_lut;

    /** Pointer to note calculation frequency table. Value is
       65536*2^(x/12) or NULL to use the internal one.  */
    uint32_t *frequency_lut;

    /** Pointer to old SoundTracker tempo definition table or NULL to
       use the internal one.  */
    uint32_t *old_st_lut;

    /** Pointer to playback handler effects table which contains all the
       definition for effect commands like arpeggio, vibrato, slides or
       NULL to use the internal one.  */
    void *effects_lut;

    /** Pointer to synth code instruction table which contains all the
       names and specific flags for each instruction or NULL to use the
       internal one.  */
    AVSequencerSynthTable *synth_code_lut;

    /** Pointer to synth sound code execution table or NULL to use the
       interal one.  */
    void *synth_code_exec_lut;

    /** Array of pointers containing every module which is registered
       and ready for access to the sequencer.  */
    AVSequencerModule **module_list;

    /** Total amount of modules registered to the sequencer.  */
    uint16_t modules;

    /** Array of pointers containing every mixing engine which is
       registered and ready for access to the sequencer.  */
    AVSequencerMixerContext **mixer_list;

    /** Total amount of mixers registered to the sequencer.  */
    uint16_t mixers;

    /** Current randomization seed value for a very fast randomize
       function used by volume, panning and pitch swing or envelopes
       featuring randomized data instead of waveforms.  */
    uint32_t seed;
} AVSequencerContext;

#endif /* AVSEQUENCER_AVSEQUENCER_H */
