/*
 * AVSequencer sub-song management
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

#ifndef AVSEQUENCER_SONG_H
#define AVSEQUENCER_SONG_H

#include "libavutil/log.h"
#include "libavformat/avformat.h"
#include "libavsequencer/order.h"
#include "libavsequencer/track.h"

/** AVSequencerSong->compat_flags bitfield.  */
enum AVSequencerSongCompatFlags {
    AVSEQ_SONG_COMPAT_FLAG_SYNC             = 0x01, ///< Tracks are synchronous (linked together, pattern based)
    AVSEQ_SONG_COMPAT_FLAG_GLOBAL_LOOP      = 0x02, ///< Global pattern loop memory
    AVSEQ_SONG_COMPAT_FLAG_AMIGA_LIMITS     = 0x04, ///< Enforce AMIGA sound hardware limits (portamento)
    AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES      = 0x08, ///< All volume related commands range from 0-64 instead of 0-255
    AVSEQ_SONG_COMPAT_FLAG_GLOBAL_NEW_ONLY  = 0x10, ///< Global volume/panning changes affect new notes only (S3M)
};

/** AVSequencerSong->compat_flags bitfield.  */
enum AVSequencerSongFlags {
    AVSEQ_SONG_FLAG_LINEAR_FREQ_TABLE   = 0x01, ///< Use linear instead of Amiga frequency table
    AVSEQ_SONG_FLAG_SPD                 = 0x02, ///< Use SPD (OctaMED style) timing instead of BpM
    AVSEQ_SONG_FLAG_MONO                = 0x04, ///< Use mono instead of stereo output
    AVSEQ_SONG_FLAG_SURROUND            = 0x08, ///< Initial global surround instead of stereo panning
};

/**
 * Sequencer song structure.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerSong {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Metadata information: Original sub-song file name, sub-song
     * title, song message, artist, genre, album, begin and finish
     * date of composition and comment.  */
    AVMetadata *metadata;

    /** AVSequencerOrderList pointer to list of order data.  */
    AVSequencerOrderList *order_list;

    /** Array (of size tracks) of pointers containing all tracks for
       this sub-song.  */
    AVSequencerTrack **track_list;

    /** Number of tracks attached to this sub-song.  */
    uint16_t tracks;

    /** Duration of this sub-song, in AV_TIME_BASE fractional
       seconds.  */
    uint64_t duration;

    /** Stack size, i.e. maximum recursion depth of GoSub command which
       defaults to 4.  */
    uint16_t gosub_stack_size;

    /** Stack size, i.e. maximum recursion depth of the pattern loop
       command, which defaults to 1 to imitate most trackers (most
       trackers do not even support any other value than one, i.e.
       the pattern loop command is not nestable).  */
    uint16_t loop_stack_size;

    /** Compatibility flags for playback. There are rare cases
       where effect handling can not be mapped into internal
       playback engine and have to be handled specially. For
       each sub-song which needs this, this will define new
       flags which tag the player to handle it to that special
       way.  */
    uint8_t compat_flags;

    /** Song playback flags. Some sequencers use a totally
       different timing scheme which has to be taken care
       specially in the internal playback engine. Also
       sequencers differ in how they handle slides.  */
    uint8_t flags;

    /** Maximum number of host channels, as edited in the track view.
       to be allocated and usable for order list (defaults to 16).  */
    uint16_t channels;

    /** Initial number of frames per row, i.e. sequencer tempo
       (defaults to 6 as in most tracker formats), a value of
       zero is pointless, since that would mean to play unlimited
       rows and tracks in just one tick.  */
    uint16_t frames;

    /** Initial speed multiplier, i.e. nominator which defaults
       to disabled = 0.  */
    uint8_t speed_mul;

    /** Initial speed divider, i.e. denominator which defaults
       to disabled = 0.  */
    uint8_t speed_div;

    /** Initial MED style SPD speed (defaults to 33 as in
       OctaMED Soundstudio).  */
    uint16_t spd_speed;

    /** Initial number of rows per beat (defaults to 4 rows are a beat).  */
    uint16_t bpm_tempo;

    /** Initial beats per minute speed (defaults to 50 Hz => 125 BpM).  */
    uint16_t bpm_speed;

    /** Minimum and lower limit of number of frames per row
       (defaults to 1), a value of zero is pointless, since
       that would mean to play unlimited rows and tracks in
       just one tick.  */
    uint16_t frames_min;

    /** Maximum and upper limit of number of frames per row
       (defaults to 255) since a larger value would not make
       sense (see track effects, they all set 8-bit values only),
       if we would allow a higher speed here, we could never
       change the speed values which are larger than 255.  */
    uint16_t frames_max;

    /** Minimum and lower limit of MED style SPD timing values
       (defaults to 1).  */
    uint16_t spd_min;

    /** Maximum and upper limit of MED style SPD timing values
       (defaults to 255) since a larger value would not make
       sense (see track effects, they all set 8-bit values only),
       if we would allow a higher speed here, we could never
       change the speed values which are larger than 255.  */
    uint16_t spd_max;

    /** Minimum and lower limit of rows per beat timing values
       (defaults to 1).  */
    uint16_t bpm_tempo_min;

    /** Maximum and upper limit of rows per beat timing values
       (defaults to 255) since a larger value would not make
       sense (see track effects, they all set 8-bit values only),
       if we would allow a higher speed here, we could never
       change the speed values which are larger than 255.  */
    uint16_t bpm_tempo_max;

    /** Minimum and lower limit of beats per minute timing values
       (defaults to 1).  */
    uint16_t bpm_speed_min;

    /** Maximum and upper limit of beats per minute timing values
       (defaults to 255) since a larger value would not make
       sense (see track effects, they all set 8-bit values only),
       if we would allow a higher speed here, we could never
       change the speed values which are larger than 255.  */
    uint16_t bpm_speed_max;

    /** Global volume of this sub-song. All other volume related
       commands are scaled by this (defaults to 255 = no scaling).  */
    uint8_t global_volume;

    /** Global sub-volume of this sub-song. This is basically
       volume divided by 256, but the sub-volume doesn't account
       into actual mixer output (defaults to 0).  */
    uint8_t global_sub_volume;

    /** Global panning of this sub-song. All other panning related
       commands are scaled by this stereo separation factor
       (defaults to 0 which means full stereo separation).  */
    uint8_t global_panning;

    /** Global sub-panning of this sub-song. This is basically
       panning divided by 256, but the sub-panning doesn't account
       into actual mixer output (defaults to 0).  */
    uint8_t global_sub_panning;

    /** Array of pointers containing every unknown data field where
       the last element is indicated by a NULL pointer reference. The
       first 64-bit of the unknown data contains an unique identifier
       for this chunk and the second 64-bit data is actual unsigned
       length of the following raw data. Some formats are chunk based
       and can store information, which can't be handled by some
       other, in case of a transition the unknown data is kept as is.
       Some programs write editor settings for sub-songs in those
       chunks, which then won't get lost in that case.  */
    uint8_t **unknown_data;
} AVSequencerSong;

#endif /* AVSEQUENCER_SONG_H */
