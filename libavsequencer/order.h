/*
 * AVSequencer order list and data management
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

#ifndef AVSEQUENCER_ORDER_H
#define AVSEQUENCER_ORDER_H

#include "libavsequencer/track.h"

/**
 * Song order list structure, This structure is actually for one channel
 * and therefore actually pointed as an array with size of number of
 * host channels.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerOrderList {
    /** Array of pointers containing all order list data used by this
      channel.  */
    AVSequencerOrderData **order_data;

    /** Number of order list data used for this channel.  */
    uint16_t orders;

    /** Number of order list data entries to use for this channel.  */
    uint16_t length;

    /** Repeat start order list data number for this channel.  */
    uint16_t rep_start;

    /** Volume level for this channel (defaults to 255).  */
    uint8_t volume;
#define AVSEQ_ORDER_LIST_VOLUME 255

    /** Sub-volume level for this channel. This is basically channel
       volume divided by 256, but the sub-volume doesn't account
       into actual mixer output (defaults 0).  */
    uint8_t sub_volume;
#define AVSEQ_ORDER_LIST_SUB_VOLUME 0

    /** Stereo track panning level for this channel (defaults to
       -128 = central stereo track panning).  */
    int8_t track_panning;
#define AVSEQ_ORDER_LIST_TRACK_PAN  -128

    /** Stereo track sub-panning level for this channel. This is
       basically track panning divided by 256, but the sub-panning
       doesn't account into actual mixer output (defaults 0).  */
    uint8_t track_sub_panning;
#define AVSEQ_ORDER_LIST_TRACK_SUB_PAN  -128

    /** Stereo panning level for this channel (defaults to
       -128 = central stereo panning).  */
    int8_t channel_panning;
#define AVSEQ_ORDER_LIST_PANNING    -128

    /** Stereo sub-panning level for this channel. This is
       basically channel panning divided by 256, but the sub-panning
       doesn't account into actual mixer output (defaults 0).  */
    uint8_t channel_sub_panning;
#define AVSEQ_ORDER_LIST_SUB_PANNING    0

    /** Compatibility flags for playback. There are rare cases
       where order handling can not be mapped into internal
       playback engine and have to be handled specially. For
       each order list which needs this, this will define new
       flags which tag the player to handle it to that special
       way.  */
    uint8_t compat_flags;

    /** Order list playback flags. Some sequencers feature
       surround panning or allow initial muting. which has to
       be taken care specially in the internal playback engine.
       Also sequencers differ in how they handle slides.  */
    uint8_t flags;
#define AVSEQ_ORDER_LIST_FLAG_CHANNEL_SURROUND  0x01 ///< Initial channel surround instead of stereo panning
#define AVSEQ_ORDER_LIST_FLAG_TRACK_SURROUND    0x02 ///< Initial track surround instead of stereo panning
#define AVSEQ_ORDER_LIST_FLAG_MUTED             0x04 ///< Initial muted channel

} AVSequencerOrderList;

/**
 * Song order list data structure, this contains actual order list data.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerOrderData {
    /** AVSequencerTrack pointer to track which should be played.  */
    AVSequencerTrack *track;

    /** Next order list data pointer if seeking forward one frame.  */
    AVSequencerOrderData *next_pos;

    /** Previous order list data pointer if seeking backward one
       frame.  */
    AVSequencerOrderData *prev_pos;

    /** Number of row to jump to if forward seeking one frame.  */
    uint16_t next_row;

    /** Number of row to jump to if backward seeking one frame.  */
    uint16_t prev_row;

    /** Beginning row for this track. If this is a track synchronization
       point, the high byte is interpreted as the first track number
       to be synchronized with and the low byte as the second track
       number or for all channels when all 4 tracks are 0.  */
    uint16_t first_row;

    /** Last row for this track. If this is a track synchronization
       point, the high byte is interpreted as the third track number
       to be synchronized with and the low byte as the fourth track
       number or for all channels when all 4 tracks are 0.
       If last row is set to 65535 in non synchronization mode,
       the last row is always taken from AVSequencerTrack.  */
    uint16_t last_row;

    /** Order list data playback flags. Some sequencers feature
       special end markers or even different playback routes for
       different playback modules (one-shot and repeat mode
       playback), mark synchronization points or temporary
       change volume), which has to be taken care specially
       in the internal playback engine.  */
    uint8_t flags;
#define AVSEQ_ORDER_DATA_FLAG_END_ORDER     0x01 ///< Order data indicates end of order
#define AVSEQ_ORDER_DATA_FLAG_END_SONG      0x02 ///< Order data indicates end of whole song
#define AVSEQ_ORDER_DATA_FLAG_NOT_IN_ONCE   0x04 ///< Order data will be skipped if you're playing in one-time mode
#define AVSEQ_ORDER_DATA_FLAG_NOT_IN_REPEAT 0x08 ///< Order data will be skipped if you're playing in repeat mode
#define AVSEQ_ORDER_DATA_FLAG_TRACK_SYNC    0x10 ///< Order data is a track synchronization point.
#define AVSEQ_ORDER_DATA_FLAG_SET_VOLUME    0x20 ///< Order data takes advantage of the order list volume set

    /** Relative note transpose for full track. Allows playing several
       tracks some half-tones up/down.  */
    int8_t transpose;

    /** Instrument transpose. All instruments will be relatively
       mapped to this if this is non-zero.  */
    int16_t instr_transpose;

    /** Tempo change or zero to skip tempo change.  */
    uint16_t tempo;

    /** Played nesting level (GoSub command maximum nesting depth).  */
    uint16_t played;

    /** Track volume (this overrides settings in AVSequencerTrack).
       To enable this, the flag AVSEQ_ORDER_DATA_FLAG_SET_VOLUME
       must be set in flags.  */
    uint8_t volume;

    /** Track sub-volume. This is basically track volume
       divided by 256, but the sub-volume doesn't account
       into actual mixer output (this overrides AVSequencerTrack).  */
    uint8_t sub_volume;
} AVSequencerOrderData;

#endif /* AVSEQUENCER_ORDER_H */
