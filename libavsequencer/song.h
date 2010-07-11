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

#include "libavformat/avformat.h"
#include "libavsequencer/avsequencer.h"
#include "libavsequencer/order.h"
#include "libavsequencer/track.h"
#include "libavsequencer/player.h"

/**
 * Sequencer song structure.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerSong {
    /** Metadata information: Original sub-song file name, sub-song
     * title, song message, artist, genre, album, begin and finish
     * date of composition and comment.  */
    AVMetadata *metadata;

    /** AVSequencerPlayerGlobals pointer to global channel data.  */
    AVSequencerPlayerGlobals *global_data;

    /** AVSequencerPlayerHostChannel pointer to host channel data.  */
    AVSequencerPlayerHostChannel *channel_data;

    /** AVSequencerOrderList pointer to list of order data.  */
    AVSequencerOrderList *order_list;

    /** Array of pointers containing all tracks for this sub-song.  */
    AVSequencerTrack **track_list;

    /** Duration of this sub-song, in AV_TIME_BASE fractional
       seconds.  */
    uint64_t duration;

    /** Number of tracks attached to this sub-song.  */
    uint16_t tracks;

    /** Stack size, i.e. maximum recursion depth of GoSub command which
       defaults to 4.  */
    uint16_t gosub_stack_size;
#define AVSEQ_SONG_GOSUB_STACK  4

    /** Stack size, i.e. maximum recursion depth of the pattern loop
       command, which defaults to 1 to imitate most trackers.  */
    uint16_t loop_stack_size;
#define AVSEQ_SONG_PATTERN_LOOP_STACK   1

    /** Compatibility flags for playback. There are rare cases
       where effect handling can not be mapped into internal
       playback engine and have to be handled specially. For
       each sub-song which needs this, this will define new
       flags which tag the player to handle it to that special
       way.  */
    uint8_t compat_flags;
#define AVSEQ_SONG_COMPAT_FLAG_SYNC             0x01 ///< Tracks are synchronous (linked together, pattern based)
#define AVSEQ_SONG_COMPAT_FLAG_GLOBAL_LOOP      0x02 ///< Global pattern loop memory
#define AVSEQ_SONG_COMPAT_FLAG_AMIGA_LIMITS     0x04 ///< Enforce AMIGA sound hardware limits (portamento)
#define AVSEQ_SONG_COMPAT_FLAG_OLD_VOLUMES      0x08 ///< All volume related commands range from 0-64 instead of 0-255
#define AVSEQ_SONG_COMPAT_FLAG_GLOBAL_NEW_ONLY  0x10 ///< Global volume/panning changes affect new notes only (S3M)

    /** Song playback flags. Some sequencers use a totally
       different timing scheme which has to be taken care
       specially in the internal playback engine. Also
       sequencers differ in how they handle slides.  */
    uint8_t flags;
#define AVSEQ_SONG_FLAG_LINEAR_FREQ_TABLE   0x01 ///< Use linear instead of Amiga frequency table
#define AVSEQ_SONG_FLAG_SPD                 0x02 ///< Use SPD (OctaMED style) timing instead of BpM
#define AVSEQ_SONG_FLAG_MONO                0x04 ///< Use mono instead of stereo output
#define AVSEQ_SONG_FLAG_SURROUND            0x08 ///< Initial global surround instead of stereo panning

    /** Maximum number of host channels, as edited in the track view.
       to be allocated and usable for order list (defaults to 16).  */
    uint16_t channels;
#define AVSEQ_SONG_CHANNELS     16
#define AVSEQ_SONG_CHANNELS_MIN 1
#define AVSEQ_SONG_CHANNELS_MAX 256

    /** Initial number of frames per row, i.e. sequencer tempo
       (defaults to 6 as in most tracker formats).  */
    uint16_t frames;
#define AVSEQ_SONG_FRAMES   6

    /** Initial speed multiplier, i.e. nominator which defaults
       to disabled = 0.  */
    uint8_t speed_mul;
#define AVSEQ_SONG_SPEED_MUL    0

    /** Initial speed divider, i.e. denominator which defaults
       to disabled = 0.  */
    uint8_t speed_div;
#define AVSEQ_SONG_SPEED_DIV    0

    /** Initial MED style SPD speed (defaults to 33 as in
       OctaMED Soundstudio).  */
    uint16_t spd_speed;
#define AVSEQ_SONG_SPD_SPEED    33

    /** Initial number of rows per beat (defaults to 4 rows are a beat).  */
    uint16_t bpm_tempo;
#define AVSEQ_SONG_BPM_TEMPO    4

    /** Initial beats per minute speed (defaults to 50 Hz => 125 BpM).  */
    uint16_t bpm_speed;
#define AVSEQ_SONG_BPM_SPEED    125

    /** Minimum and lower limit of number of frames per row
       (defaults to 1).  */
    uint16_t frames_min;
#define AVSEQ_SONG_FRAMES_MIN   1

    /** Maximum and upper limit of number of frames per row
       (defaults to 255).  */
    uint16_t frames_max;
#define AVSEQ_SONG_FRAMES_MAX   255

    /** Minimum and lower limit of MED style SPD timing values
       (defaults to 1).  */
    uint16_t spd_min;
#define AVSEQ_SONG_SPD_MIN  1

    /** Maximum and upper limit of MED style SPD timing values
       (defaults to 255).  */
    uint16_t spd_max;
#define AVSEQ_SONG_SPD_MAX  255

    /** Minimum and lower limit of rows per beat timing values
       (defaults to 1).  */
    uint16_t bpm_tempo_min;
#define AVSEQ_SONG_BPM_TEMPO_MIN    1

    /** Maximum and upper limit of rows per beat timing values
       (defaults to 255).  */
    uint16_t bpm_tempo_max;
#define AVSEQ_SONG_BPM_TEMPO_MAX    255

    /** Minimum and lower limit of beats per minute timing values
       (defaults to 1).  */
    uint16_t bpm_speed_min;
#define AVSEQ_SONG_BPM_SPEED_MIN    1

    /** Maximum and upper limit of beats per minute timing values
       (defaults to 255).  */
    uint16_t bpm_speed_max;
#define AVSEQ_SONG_BPM_SPEED_MAX    255

    /** Global volume of this sub-song. All other volume related
       commands are scaled by this (defaults to 255 = no scaling).  */
    uint8_t global_volume;
#define AVSEQ_SONG_VOLUME   255

    /** Global sub-volume of this sub-song. This is basically
       volume divided by 256, but the sub-volume doesn't account
       into actual mixer output (defaults to 0).  */
    uint8_t global_sub_volume;
#define AVSEQ_SONG_SUB_VOLUME   0

    /** Global panning of this sub-song. All other panning related
       commands are scaled by this stereo separation factor
       (defaults to 0 which means full stereo separation).  */
    uint8_t global_panning;
#define AVSEQ_SONG_PANNING  0

    /** Global sub-panning of this sub-song. This is basically
       panning divided by 256, but the sub-panning doesn't account
       into actual mixer output (defaults to 0).  */
    uint8_t global_sub_panning;
#define AVSEQ_SONG_SUB_PANNING  0

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
