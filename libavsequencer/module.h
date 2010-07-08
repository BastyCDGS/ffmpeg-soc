/*
 * AVSequencer music module management
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

#ifndef AVSEQUENCER_MODULE_H
#define AVSEQUENCER_MODULE_H

#include "libavsequencer/avsequencer.h"
#include "libavsequencer/player.h"
#include "libavutil/tree.h"

/**
 * Sequencer module structure.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerModule {
    /** Metadata information: Original module file name, module name,
     *  module message, artist, genre, album, begin and finish date of
     * composition and comment.  */
    AVSequencerMetadata *metadata;

    /** AVSequencerPlayerChannel pointer to virtual channel data.  */
    AVSequencerPlayerChannel *channel_data;

    /** Integer indexed tree root of sub-songs available in
       this module with AVTreeNode->elem being a AVSequencerSong.  */
    AVTreeNode *song_list;

    /** Integer indexed tree root of instruments available for
       the whole module (shared between all sub-songs) with
       AVTreeNode->elem being a AVSequencerInstrument.  */
    AVTreeNode *instrument_list;

    /** Integer indexed tree root of envelopes used by module
       with AVTreeNode->elem being a AVSequencerEnvelope.
       There can be vibrato, tremolo, panbrello, spenolo,
       volume, panning, pitch, envelopes, a resonance filter and
       finally the auto vibrato, tremolo and panbrello envelopes.  */
    AVTreeNode *envelopes_list;

    /** Integer indexed tree root of keyboard definitions
       with AVTreeNode->elem being a AVSequencerKeyboard.
       A keyboard definition maps an instrument octave/note-pair
       to the sample number being played.  */
    AVTreeNode *keyboard_list;

    /** Integer indexed tree root of arpeggio envelopes
       with AVTreeNode->elem being a AVSequencerArpeggioEnvelope.
       Arpeggio envelopes allow to fully customize the arpeggio
       command by playing the envelope instead of only a 
       repetive set of 3 different notes.  */
    AVTreeNode *arp_env_list;

    /** Duration of the module, in AV_TIME_BASE fractional
       seconds. This is the total sum of all sub-song durations
       this module contains.  */
    uint64_t duration;

    /** Maximum number of virtual channels, including NNA (New Note
       Action) background channels to be allocated and processed by
       the mixing engine (defaults to 64).  */
    uint16_t channels;
#define AVSEQ_MODULE_CHANNELS   64

    /** Compatibility flags for playback. There are rare cases
       where effect handling can not be mapped into internal
       playback engine and have to be handled specially. For
       each module which needs this, this will define new
       flags which tag the player to handle it to that special
       way.  */
    int8_t compat_flags;

    /** Module playback flags.  */
    int8_t flags;

    /** 64-bit integer indexed unique key tree root of unknown data
       fields for input file reading with AVTreeNode->elem being
       unsigned 8-bit integer data. Some formats are chunk based
       and can store information, which can't be handled by some
       other, in case of a transition the unknown data is kept as
       is. Some programs write editor settings for module in those
       chunks, which won't get lost then. The first 8 bytes of this
       data is an unsigned 64-bit integer length in bytes of
       the unknown data.  */
    AVTreeNode *unknown_data;

    /** This is just a data field where the user solely
       decides, what the usage (if any) will be.  */
    uint8_t *user_data;

} AVSequencerModule;

/**
 * Register a module to the AVSequencer.
 *
 * @param module the AVSequencerModule to be registered
 * @return >= 0 on success, error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_module_register(AVSequencerModule *module);

#endif /* AVSEQUENCER_MODULE_H */
