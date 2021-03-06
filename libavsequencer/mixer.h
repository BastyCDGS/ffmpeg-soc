/*
 * AVSequencer mixer header file for various mixing engines
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

#ifndef AVSEQUENCER_MIXER_H
#define AVSEQUENCER_MIXER_H

#include "libavutil/avutil.h"

/**
 * Identify the mixing engines.
 * The principle is roughly:
 *
 * If you add a mixer ID to this list, add it so that
 * 1. no value of a existing mixer ID changes (that would break ABI),
 * 2. it is as close as possible to similar mixers.
 */
enum AVMixerID {
    MIXER_ID_NULL,

    /* Integer based mixers */
    MIXER_ID_LQ, ///< Low quality mixer optimized for fastest playback
    MIXER_ID_HQ, ///< High quality mixer optimized for quality playback and disk writers
};

/** AVMixerChannel->flags bitfield.  */
enum AVMixerChannelFlags {
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
typedef struct AVMixerChannel {
    /** Current position in samples of this channel to be mixed to
       output data.  */
    uint32_t pos;

    /** Current one shoot position in samples of this channel.
       This will increment until a new sample is played.  */
    uint32_t pos_one_shoot;

    /** Current length in samples for this channel.  */
    uint32_t len;

    /** Current sample data for this channel. Actual layout depends
       on number of bits per sample.  */
    const int16_t *data;

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

    /** Current (resonance) filter cutoff for this channel which
       ranges from 0 to 4095. Natural frequency is calculated by
       nat_freq = 2*PI*110*(2^0.25)*2^(filter_cutoff/768).  */
    uint16_t filter_cutoff;

    /** Current (resonance) filter damping for this channel which
       ranges from 0 to 4095. Damping factor is calculated by
       damp_factor = 10^(-((24/128)*filter_damping)/640).  */
    uint16_t filter_damping;
} AVMixerChannel;

/** AVMixerData->flags bitfield.  */
enum AVMixerDataFlags {
    AVSEQ_MIXER_DATA_FLAG_ALLOCATED = 0x01, ///< The mixer is currently allocated and ready to use
    AVSEQ_MIXER_DATA_FLAG_MIXING    = 0x02, ///< The mixer is currently in actual mixing to output
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
typedef struct AVMixerData {
    /** Pointer to basic mixer context structure which describes
       the mixer features.  */
    struct AVMixerContext *mixctx;

    /** Pointer to be used by the playback handler of the mixing
       engine which is called every tempo tick.  */
    void *opaque;

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

    /** Maximum number of allocated input channels. The more
       channels are used the more CPU power is required to
       calculate the output audio buffer.  */
    uint16_t channels_in;

    /** Maximum number of allocated output channels. The more
       channels are used the more CPU power is required to
       calculate the data from input audio buffer.  */
    uint16_t channels_out;

    /** Current status flags for this mixer which contain information
       like if the mixer has been allocated, is currently mixing,
       output mode (stereo or mono) or if it frozen because of some
       delaying (like caused by disk I/O when using disk writers.  */
    uint8_t flags;

    /** Executes one tick of the playback handler when enough mixing
       data has been processed.  */
    int (*handler)(struct AVMixerData *mixer_data);
} AVMixerData;

/** AVMixerContext->flags bitfield.  */
enum AVMixerContextFlags {
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
typedef struct AVMixerContext {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Mixer name.  */
    const char *name;

    /**
     * A description for the filter. You should use the
     * NULL_IF_CONFIG_SMALL() macro to define it.
     */
    const char *description;

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

    /** Maximum number of input channels supported by this mixer,
       some engines might support less channels than maximum allowed
       by the sequencer.  */
    uint16_t channels_in;

    /** Maximum number of output channels supported by this mixer,
       some engines might support less channels than maximum allowed
       by the sequencer.  */
    uint16_t channels_out;

    /** Special flags indicating supported features by this mixer.  */
    uint8_t flags;

    /** The initialization function to call for the mixer.  */
    AVMixerData * (*init)(struct AVMixerContext *const mixctx,
                          const char *args, void *opaque);

    /** The destruction function to call for the mixer.  */
    int (*uninit)(AVMixerData *const mixer_data);

    /** Transfers a new mixing rate in Hz and number of output
       channels from the AVSequencer to the internal mixer data.  */
    uint32_t (*set_rate)(AVMixerData *const mixer_data,
                         const uint32_t mix_rate,
                         const uint32_t channels);

    /** Transfers the new time interval for calling the playback
       handler to the interal mixer, in AV_TIME_BASE fractional
       seconds.  */
    uint32_t (*set_tempo)(AVMixerData *const mixer_data,
                          const uint32_t tempo);

    /** Transfers the new volume boost, the new left position volume,
       the new right position volume and new number of maximum
       channels from the AVSequencer to the internal mixer data.  */
    uint32_t (*set_volume)(AVMixerData *const mixer_data,
                           const uint32_t amplify,
                           const uint32_t left_volume,
                           const uint32_t right_volume,
                           const uint32_t channels);

    /** Transfers the current internal mixer channel data to the
       AVSequencer.  */
    void (*get_channel)(const AVMixerData *const mixer_data,
                        AVMixerChannel *const mixer_channel,
                        const uint32_t channel);

    /** Transfers the current AVSequencer channel data to the
       internal mixer channel data.  */
    void (*set_channel)(AVMixerData *const mixer_data,
                        const AVMixerChannel *const mixer_channel,
                        const uint32_t channel);

    /** Resets the AVSequencer channel data to the mixer internal
       default values.  */
    void (*reset_channel)(AVMixerData *const mixer_data,
                          const uint32_t channel);

    /** Transfers both (current and next) internal mixer channel data
       to the AVSequencer.  */
    void (*get_both_channels)(const AVMixerData *const mixer_data,
                              AVMixerChannel *const mixer_channel_current,
                              AVMixerChannel *const mixer_channel_next,
                              const uint32_t channel);

    /** Transfers both (current and next) AVSequencer channel data to
       the internal mixer channel data.  */
    void (*set_both_channels)(AVMixerData *const mixer_data,
                              const AVMixerChannel *const mixer_channel_current,
                              const AVMixerChannel *const mixer_channel_next,
                              const uint32_t channel);

    /** Signals a volume, panning or pitch change from AVSequencer to
       the internal mixer.  */
    void (*set_channel_volume_panning_pitch)(AVMixerData *const mixer_data,
                                             const AVMixerChannel *const mixer_channel,
                                             const uint32_t channel);

    /** Signals a set sample position, set repeat and flags change
       from AVSequencer to the internal mixer.  */
    void (*set_channel_position_repeat_flags)(AVMixerData *const mixer_data,
                                              const AVMixerChannel *const mixer_channel,
                                              const uint32_t channel);

    /** Signals a set (resonance) filter from AVSequencer to the
       internal mixer.  */
    void (*set_channel_filter)(AVMixerData *const mixer_data,
                               const AVMixerChannel *const mixer_channel,
                               const uint32_t channel);

    /** Run the actual mixing engine by filling the buffer, i.e. the
       player data is converted to SAMPLE_FMT_S32.  */
    void (*mix)(AVMixerData *const mixer_data, int32_t *buf);

    /** Run the actual mixing engine by filling the buffer by
       specifying a channel range suitable for parallelziation, i.e.
       the player data is converted to SAMPLE_FMT_S32.  */
    void (*mix_parallel)(AVMixerData *const mixer_data,
                         int32_t *buf,
                         const uint32_t first_channel,
                         const uint32_t last_channel);
} AVMixerContext;

#endif /* AVSEQUENCER_MIXER_H */
