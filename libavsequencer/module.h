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

#include "libavutil/log.h"
#include "libavformat/avformat.h"
#include "libavsequencer/song.h"
#include "libavsequencer/instr.h"

/**
 * Sequencer module structure.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerModule {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Metadata information: Original module file name, module name,
     *  module message, artist, genre, album, begin and finish date of
     * composition and comment.  */
    AVMetadata *metadata;

    /** Array (of size songs) of pointers containing every sub-song
       for this module.  */
    AVSequencerSong **song_list;

    /** Number of sub-songs attached to this module.  */
    uint16_t songs;

    /** Array (of size instruments) of pointers containing every
       instrument for this module.  */
    AVSequencerInstrument **instrument_list;

    /** Number of instruments attached to this module.  */
    uint16_t instruments;

    /** Array (of size envelopes) of pointers containing every
       evelope for this module.  */
    AVSequencerEnvelope **envelope_list;

    /** Number of envelopes attached to this module.  */
    uint16_t envelopes;

    /** Array (of size keyboards) of pointers containing every
       keyboard definition list for this module.  */
    AVSequencerKeyboard **keyboard_list;

    /** Number of keyboard definitions attached to this module.  */
    uint16_t keyboards;

    /** Array (of size arpeggios) of pointers containing every
       arpeggio envelope definition list for this module.  */
    AVSequencerArpeggio **arpeggio_list;

    /** Number of arpeggio definitions attached to this module.  */
    uint16_t arpeggios;

    /** Forced duration of the module, in AV_TIME_BASE fractional
       seconds. If non-zero, the song should forcibly end after
       this duration, even if there are still notes to
       be played.  */
    uint64_t forced_duration;

    /** Maximum number of virtual channels, including NNA (New Note
       Action) background channels to be allocated and processed by
       the mixing engine (defaults to 64).  */
    uint16_t channels;

    /** Array of pointers containing every unknown data field where
       the last element is indicated by a NULL pointer reference. The
       first 64-bit of the unknown data contains an unique identifier
       for this chunk and the second 64-bit data is actual unsigned
       length of the following raw data. Some formats are chunk based
       and can store information, which can't be handled by some
       other, in case of a transition the unknown data is kept as is.
       Some programs write editor settings for module in those chunks,
       which then won't get lost in that case.  */
    uint8_t **unknown_data;
} AVSequencerModule;

#endif /* AVSEQUENCER_MODULE_H */
