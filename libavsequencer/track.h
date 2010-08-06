r/*
 * AVSequencer track and pattern management
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

#ifndef AVSEQUENCER_TRACK_H
#define AVSEQUENCER_TRACK_H

#include "libavutil/log.h"
#include "libavformat/avformat.h"

/** AVSequencerTrackEffect->command values.  */
enum AVSequencerTrackEffectCommand {
    /** Note effect commands.
       0x00 - Arpeggio:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the first halftone and yy the second halftone.
       Both xx and yy are signed values which means that a
       negative value is a backward arpeggio.  */
    AVSEQ_TRACK_EFFECT_CMD_ARPEGGIO      = 0x00,

    /** 0x01 - Portamento up:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the portamento up slide value and yy is
       a super finetuning with a 256x accuracy.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_PORTA_UP      = 0x01,

    /** 0x02 - Portamento down:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the portamento down slide value and yy is
       a super finetuning with a 256x accuracy.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_PORTA_DOWN    = 0x02,

    /** 0x03 - Fine portamento up:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the fine portamento up slide value which is
       16 times more accurate than command byte 01 and yy is
       a super finetuning with a 256x accuracy.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_PORTA_UP    = 0x03,

    /** 0x04 - Fine portamento down:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the fine portamento down slide value which is
       16 times more accurate than command byte 01 and yy is
       a super finetuning with a 256x accuracy.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_PORTA_DOWN  = 0x04,

    /** 0x05 - Portamento up once:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the portamento up slide value and yy is
       a super finetuning with a 256x accuracy. The portamento
       up is done only once per row (at first tick).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_O_PORTA_UP    = 0x05,

    /** 0x06 - Portamento down once:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the portamento down slide value and yy is
       a super finetuning with a 256x accuracy. The portamento
       down is done only once per row (at first tick).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_O_PORTA_DOWN  = 0x06,

    /** 0x07 - Fine portamento up once:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the fine portamento up slide value which is
       16 times more accurate than command byte 01 and yy is
       a super finetuning with a 256x accuracy. The fine portamento
       up is done only once per row (at first tick).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_OFPORTA_UP    = 0x07,

    /** 0x08 - Fine portamento down once:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the fine portamento down slide value which is
       16 times more accurate than command byte 01 and yy is
       a super finetuning with a 256x accuracy. The fine portamento
       down is done only once per row (at first tick).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_OFPORTA_DOWN  = 0x08,

    /** 0x09 - Tone portamento:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the target tone slide value encoded as;
       octave * 12 + note and yy is a super finetuning with
       a 256x accuracy. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_TONE_PORTA    = 0x09,

    /** 0x0A - Fine tone portamento:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the target tone slide value encoded as;
       octave * 12 + note which is 16 times more accurate
       than command byte 09 and yy is a super finetuning with
       a 256x accuracy. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_TONE_PORTA  = 0x0A,

    /** 0x0B - Tone portamento once:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the target tone slide value encoded as;
       octave * 12 + note and yy is a super finetuning with
       a 256x accuracy. The tone portamento is done only
       once per row (at first tick).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_O_TONE_PORTA  = 0x0B,

    /** 0x0C - Fine tone portamento once:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the target tone slide value encoded as;
       octave * 12 + note which is 16 times more accurate
       than command byte 09 and yy is a super finetuning with
       a 256x accuracy. The fine tone portamento is done only
       once per row (at first tick).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_OFTONE_PORTA  = 0x0C,

    /** 0x0D - Note slide:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is a declared set of values which interpret
       the actual action according to the following table:
         xx | Meanings
       0x10 | Note slide up by yy notes each row
       0x1F | Fine note slide up yy notes on 1st row
       0x20 | Note slide down by yy notes each row
       0x2F | Fine note slide down yy notes on 1st row

       If xx or yy are zero, the previous value will be used.
       Pitch panning separation is not affected by this one,
       you have to do a panning slide instead if you want this.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_NOTE_SLIDE    = 0x0D,

    /** 0x0E - Vibrato:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the vibrato speed and yy is the vibrato
       depth. The vibrato values are compatible with the
       MOD, S3M, XM and IT vibrato if the vibrato envelope
       with a length of 64 ticks and the amplitude of 256.
       xx is unsigned and yy is signed. A negative vibrato
       depth means that the vibrato envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_VIBRATO       = 0x0E,

    /** 0x0F - Fine vibrato:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the vibrato speed and yy is the fine vibrato
       depth. The vibrato values are compatible with the MOD
       S3M, XM and IT fine vibrato if the inverted vibrato
       envelope has a length of 64 ticks and an amplitude of 256.
       xx is unsigned and yy is signed. A negative fine vibrato
       depth means that the vibrato envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_FINE_VIBRATO  = 0x0F,

    /** 0x10 - Vibrato once:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the vibrato speed and yy is the vibrato
       depth. The vibrato values are compatible with the MOD,
       S3M, XM and IT vibrato if the inverted vibrato envelope
       with a length of 64 ticks and the amplitude of 256.
       The vibrato is done only once per row (at first tick).
       xx is unsigned and yy is signed. A negative vibrato
       depth means that the vibrato envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_VIBRATO_ONCE  = 0x10,

    /** 0x11 - Fine vibrato once:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the vibrato speed and yy is the fine vibrato
       depth. The vibrato values are compatible with the MOD,
       S3M, XM and IT fine vibrato if the inverted vibrato
       envelope has a length of 64 ticks and an amplitude of 256.
       The vibrato is done only once per row (at first tick).
       xx is unsigned and yy is signed. A negative fine vibrato
       depth means that the vibrato envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_F_VIB_ONCE    = 0x11,

    /** 0x12 - Do a keyoff:
       Data word consists of a 16-bit unsigned value
       which represents the tick number at which the
       keyoff is executed.  */
    AVSEQ_TRACK_EFFECT_CMD_DO_KEYOFF     = 0x12,

    /** 0x13 - Do a hold delay:
       Data word consists of a 16-bit unsigned value
       which represents the tick number at which the
       hold delay is executed.  */
    AVSEQ_TRACK_EFFECT_CMD_DO_HOLD       = 0x13,

    /** 0x14 - Do a fadeout:
       Data word consists of a 16-bit unsigned value
       which represents the tick number at which the
       fadeout is executed.  */
    AVSEQ_TRACK_EFFECT_CMD_NOTE_FADE     = 0x14,

    /** 0x15 - Note cut:
       Data word consists of 4-bit upper nibble named x
       and 12-bit unsigned value named yyy. If x is
       zero then just the volume is set to zero,
       otherwise the note will be killed, i.e. turned off
       completely.  */
    AVSEQ_TRACK_EFFECT_CMD_NOTE_CUT      = 0x15,

    /** 0x16 - Note delay:
       Data word consists of a 16-bit unsigned value
       which represents the tick number at which the note
       delay is executed.  */
    AVSEQ_TRACK_EFFECT_CMD_NOTE_DELAY    = 0x16,

    /** 0x17 - Tremor:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the number of ticks the note is turned on
       and yy is the number of ticks the note is turned off
       afterwards.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_TREMOR        = 0x17,

    /** 0x18 - Note retrigger:
       Data word consists of a 16-bit unsigned value which represents
       the retrigger repeat tick count. This command is mostly the
       same as the MOD note retrigger (E9x) command. It differs in
       that if the first x is greater than 8, the others are a
       divider, e.g. 18-8002 will be a divider of 2, which means
       that the current tempo will be divided by 2 and be used as a
       retrigger value. That means if the tempo is 6, for example,
       the retrigger will be executed 2 times, i.e. at tick 0 and 3.
       But, if the tempo is 12, for example, it will be retriggered at
       ticks 0 and 6, instead. 18-8003 with a tempo of 6 will do 3
       retriggers  at ticks 0, 2 and 4. The same with tempo 12
       will do 3 retriggers at ticks 0, 4 and 8. It will always be
       rounded down, i.e. tempo 8 with 18-8003 will retrigger each
       2 ticks, because 8 / 3 is 2,7 which gets 2 by down rounding
       (fraction is is just dropped away).  */
    AVSEQ_TRACK_EFFECT_CMD_RETRIG_NOTE   = 0x18,

    /** 0x19 - Multi retrigger note:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the retrigger repeat tick count and yy
       is the mode how to affect the volume levels, 0x80-0xBF
       will cause a volume add of (yy-0x7F)*zz where zz is the
       value get with extended control (see next command).
       0xC0-0xFF will do the same thing but do a subtraction
       instead. If you use tracker like volumes (values ranging
       from 0x00-0x40), the formula will be (yy-0x7F) or (yy-0xBF).
       00-7F will cause to multiply with the upper nibble of yy
       and divided by the lower nibble of yy, e.g. 0x25 will cause
       the original volume multiplied and then divided by 2/5,
       rounding down. If one of the values is zero, the previous
       value will be reused.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_MULTI_RETRIG  = 0x19,

    /** 0x1A - Extended control:
       Data word consists of one upper 4-bit nibble named x and of
       a 12-bit value named yyy. All values are considered unsigned
       and the meaning of x is declared by the following table:
       x | Meanings
       0 | Set frequency table: If yyy = 000 then use linear
           frequency table else use Amiga frequency table).
       1 | Glissando control: If the lower 8-bits are zero, the
           glissando is tunred off, otherwise on, causing tone
           portamentoes rounded to the nearest yyyth half-tone).
       2 | Multi retrigger control. In that case the upper 8-bits of
           yyy determine which part of the multi retrigger will be
           controlled. The upper 8 bits are named xx while the lower
           8-bits are named yy here:
             xx | Meanings
           0x20 | Set volume amplifier to yy for 8-bit volumes, 00
                  will be set to 01 which is the default and values
                  greater than 04 are clipped to 04. This effect
                  won't do anything in tracker compatible volume mode,
                  i.e. volumes ranging from 0x00-0x40.
           0x21 | If yy is zero, sub-slides will be disabled, else they
                  will be enabled
           0x22 | Set multi retrigger divider to yy (see note retrigger
                  (0x18) for details). The divider will be used until
                  it will be resetted with multi retrigger note (0x19)
                  i.e. initializing a new xx value. Changing the divider
                  must be done after Multi retrigger note (0x19).
       3 | Cut all NNA notes (yyy must always be set to 0 now, because
           other values could do something in newer versions of FFmpeg.
       4 | Fadeout all NNA notes (yyy must always be set to 0 now,
           since other values are reserved for newer versions of FFmpeg.
       5 | Do keyoff on all NNA notes (yyy must always be set to 0 now,
           since other values are reserved for newer versions of FFmpeg.
       6 | Set pitch sub-slide value to lower 8-bits of yyy.  */

    /** 0x1B - Invert loop:
       Data word consists of a 16-bit unsigned value
       which represents the tick number at which the sample
       speed being inverted. Some trackers this effect Funk It!
       This effect allows you to play several positions of a sample
       to be inverted. Remember that this effect changes the actual
       sample data.  */
    AVSEQ_TRACK_EFFECT_CMD_INVERT_LOOP   = 0x1B,

    /** 0x1C - Execute command effect at tick:
       Data word consists of a 16-bit unsigned value which
       represents the tick number at where the next effect
       will be executed. This also can be used to simulate
       fast slides or vibratos by putting a zero as value.  */
    AVSEQ_TRACK_EFFECT_CMD_EXECUTE_FX    = 0x1C,

    /** 0x1D - Stop command effect at tick:
       Data word consists of two 8-bit pairs named xx and yy
       where xx represents the tick number at where to stop
       command effect execution and yy will be the command
       effect number which should be stopped.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_STOP_FX       = 0x1D,

    /* Volume effect commands.  */

    /** 0x20 - Set volume:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the volume level (ranging either from
       0x00 - 0xFF or in tracker volume mode from 0x00 - 0x40)
       and yy is the sub-volume level which allows to set 1/256th
       of a volume level. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_VOLUME    = 0x20,

    /** 0x21 - Volume slide up:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the volume level (ranging either from
       0x00 - 0xFF or in tracker volume mode from 0x00 - 0x40)
       to slide up and yy is the sub-volume slide up level
       which allows to set 1/256th of a volume slide up level.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_UP    = 0x21,

    /** 0x22 - Volume slide down:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the volume level (ranging either from
       0x00 - 0xFF or in tracker volume mode from 0x00 - 0x40)
       to slide down and yy is the sub-volume slide down level
       which allows to set 1/256th of a volume slide down level.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_DOWN  = 0x22,

    /** 0x23 - Fine volume slide up:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the volume level (ranging either from
       0x00 - 0xFF or in tracker volume mode from 0x00 - 0x40)
       to slide up and yy is the sub-volume slide up level
       which allows to set 1/256th of a volume slide up level.
       The fine volume slide up is done only once per row
       (at first tick). Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_VOLSL_UP    = 0x23,

    /** 0x24 - Fine volume slide down:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the volume level (ranging either from
       0x00 - 0xFF or in tracker volume mode from 0x00 - 0x40)
       to slide down and yy is the sub-volume slide down level
       which allows to set 1/256th of a volume slide down level.
       The fine volume slide down is done only once per row
       (at first tick). Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_VOLSL_DOWN  = 0x24,

    /** 0x25 - Volume slide to:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is either the volume level (ranging either from
       0x01 - 0xFE or in tracker volume mode from 0x01 - 0x3F)
       to slide to (the target volume desired) and yy is the
       sub-volume slide to level which allows to set 1/256th of
       a volume slide to level, 0x00 to execute the slide
       instead of setting it or 0xFF to execute a fine volume
       slide instead (only once per row).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_VOL_SLD_TO    = 0x25,

    /** 0x26 - Tremolo:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the tremolo speed and yy is the tremolo
       depth. The tremolo values are compatible with the MOD,
       S3M, XM and IT tremolo if an inverted tremolo envelope
       with a length of 64 ticks and the amplitude of 256 is used.
       xx is unsigned and yy is signed. A negative tremolo
       depth means that the tremolo envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_TREMOLO       = 0x26,

    /** 0x27 - Tremolo once:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the tremolo speed and yy is the tremolo depth. The
       tremolo values are compatible with the MOD, S3M, XM and IT
       tremolo if an inverted tremolo envelope with a length of 64
       ticks and the amplitude of 256 is used. The tremolo is
       executed only once per row, i.e. at first tick.
       xx is unsigned and yy is signed. A negative tremolo depth
       means that the tremolo envelope values will be inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_TREMOLO_ONCE  = 0x27,

    /** 0x28 - Set track volume:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track volume level (ranging either from 0x00 - 0xFF
       or in tracker volume mode from 0x00 - 0x40) and yy is the
       sub-volume track level which allows to set 1/256th of a track
       volume level. Other volume levels are scaled with track volume.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_TRK_VOL   = 0x28,

    /** 0x29 - Track volume slide up:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track volume level (ranging either from 0x00 - 0xFF
       or in tracker volume mode from 0x00 - 0x40) to slide up and
       yy is the sub-volume track slide up level which allows to set
       1/256th of a volume slide up level. Other volume levels are
       scaled with track volume. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_UP    = 0x29,

    /** 0x2A - Track volume slide down:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track volume level (ranging either from 0x00 - 0xFF
       or in tracker volume mode from 0x00 - 0x40) to slide down and
       yy is the sub-volume track slide down level which allows to set
       1/256th of a volume slide down level. Other volume levels are
       scaled with track volume. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_TVOL_SL_DOWN  = 0x2A,

    /** 0x2B - Fine track volume slide up:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track volume level (ranging either from 0x00 - 0xFF
       or in tracker volume mode from 0x00 - 0x40) to slide up and
       yy is the sub-volume track slide up level which allows to set
       1/256th of a volume slide up level. Other volume levels are
       scaled with track volume. The fine track volume slide up is
       done only once per row, i.e. at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_TVOL_SL_UP  = 0x2B,

    /** 0x2C - Fine track volume slide down:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track volume level (ranging either from 0x00 - 0xFF
       or in tracker volume mode from 0x00 - 0x40) to slide down and
       yy is the sub-volume track slide down level which allows to set
       1/256th of a volume slide down level. Other volume levels are
       scaled with track volume. The fine track volume slide down is
       done only once per row, i.e. at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_TVOL_SL_DN  = 0x2C,

    /** 0x2D - Track volume slide to:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is either the track volume level (ranging either from
       0x01 - 0xFE or in tracker volume mode from 0x01 - 0x3F) to
       slide to (the target track volume desired) and yy is the
       sub-volume track slide to level which allows to set 1/256th
       of a track volume slide to level, 0x00 to execute the slide
       instead of setting it or 0xFF to execute a fine track volume
       slide instead (only once per row).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_TVOL_SLD_TO   = 0x2D,

    /** 0x2E - Track tremolo:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track tremolo speed and yy is the track tremolo
       depth. The track tremolo values are compatible with the MOD,
       S3M, XM and IT tremolo if an inverted tremolo envelope with
       a length of 64 ticks and the amplitude of 256 is used.
       xx is unsigned and yy is signed. A negative track tremolo
       depth means that the tremolo envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_TRK_TREMOLO   = 0x2E,

    /** 0x2F - Track tremolo once:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track tremolo speed and yy is the track tremolo
       depth. The track tremolo values are compatible with the MOD,
       S3M, XM and IT tremolo if an inverted tremolo envelope with a
       length of 64 ticks and the amplitude of 256 is used. The
       tremolo is executed only once per row, i.e. at first tick.
       xx is unsigned and yy is signed. A negative tremolo depth
       means that the tremolo envelope values will be inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_T_TREMO_ONCE  = 0x2F,

    /* Panning effect commands.  */

    /** 0x30 - Set panning position:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the panning position ranging from 0x00 (full stereo left
       left panning) to 0xFF (full stereo right panning) while 0x80
       is exact centered panning (i.e. the note will be played with
       half of volume at left and right speakers) and yy is the
       sub-panning position which allows to set 1/256th of a panning
       position. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_PANNING   = 0x30,

    /** 0x31 - Panning slide left:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the panning left position level ranging from 0x00 - 0xFF
       and yy is the sub-panning slide left position which allows to
       set 1/256th of a panning slide left position.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_PAN_SL_LEFT   = 0x31,

    /** 0x32 - Panning slide right:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the panning right slide position ranging from 0x00 - 0xFF
       and yy is the sub-panning slide right position which allows to
       set 1/256th of a panning slide right position.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_PAN_SL_RIGHT  = 0x32,

    /** 0x33 - Fine panning slide left:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the panning left slide position ranging from 0x00 - 0xFF
       and yy is the sub-panning slide left position which allows to
       set 1/256th of a panning slide left position. The fine panning
       slide left is done only once per row, i.e. at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_P_SL_LEFT   = 0x33,

    /** 0x34 - Fine panning slide right:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the panning right slide position ranging from 0x00 - 0xFF
       and yy is the sub-panning slide right position which allows to
       set 1/256th of a panning slide right position. The fine panning
       slide right is done only once per row, i.e. at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_P_SL_RIGHT  = 0x34,

    /** 0x35 - Panning slide to:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the panning position ranging from 0x01 (almost full stereo
       left panning) to 0xFE (almost full stereo right panning) while
       0x80 is exact centered panning (i.e. the note will be played
       with half of volume at left and right speakers) to slide to
       (the target stereo position desired) and yy is the sub-panning
       slide to position which allows to set 1/256th of a panning slide
       to position, 0x00 to execute the slide instead of setting it or
       0xFF to do a fine panning slide instead (only once per row).
       Other stereo positions are scaled to track panning position.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_PAN_SLD_TO    = 0x35,

    /** 0x36 - Pannolo / Panbrello:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the pannolo speed and yy is the pannolo
       depth. The pannolo values are compatible with the MOD,
       S3M, XM and IT pannolo if an inverted pannolo envelope
       with a length of 64 ticks and the amplitude of 256 is used.
       xx is unsigned and yy is signed. A negative pannolo
       depth means that the pannolo envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_PANNOLO       = 0x36,

    /** 0x37 - Pannolo / Panbrello once:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the pannolo speed and yy is the pannolo depth. The
       pannolo values are compatible with the MOD, S3M, XM and IT
       pannolo if an inverted pannolo envelope with a length of 64
       ticks and the amplitude of 256 is used. The pannolo is
       executed only once per row, i.e. at first tick.
       xx is unsigned and yy is signed. A negative pannolo depth
       means that the pannolo envelope values will be inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_PANNOLO_ONCE  = 0x37,

    /** 0x38 - Set track panning:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track panning position ranging from 0x00 (full stereo
       left panning) to 0xFF (full stereo right panning) while 0x80
       is exact centered panning (i.e. the note will be played with
       half of volume at left and right speakers) and yy is the
       sub-panning position which allows to set 1/256th of a track
       panning position. Other stereo positions are scaled to track
       panning position. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_TRK_PAN   = 0x38,

    /** 0x39 - Track panning slide left:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track panning left slide position ranging from
       0x00 - 0xFF and yy is the sub-panning track slide left
       position which allows to set 1/256th of a panning slide
       left position. Other stereo positions are scaled to track
       panning position. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_LEFT  = 0x39,

    /** 0x3A - Track panning slide right:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track panning right slide position ranging from
       0x00 - 0xFF and yy is the sub-panning track slide right
       position which allows to set 1/256th of a panning slide
       right position. Other stereo positions are scaled to track
       panning position. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_TPAN_SL_RGHT  = 0x3A,

    /** 0x3B - Fine track panning slide left:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track panning left slide position ranging from
       0x00 - 0xFF and yy is the sub-panning track slide left
       position which allows to set 1/256th of a panning slide
       left position. Other stereo positions are scaled to track
       panning position. The fine track panning slide left is done
       only once per row, i.e. only at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_TP_SL_LEFT  = 0x3B,

    /** 0x3C - Fine track panning slide right:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track panning right slide position ranging from
       0x00 - 0xFF and yy is the sub-panning track slide right
       position which allows to set 1/256th of a panning slide
       right position. Other stereo positions are scaled to track
       panning position. The fine track panning slide right is done
       only once per row, i.e. only at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_TP_SL_RGHT  = 0x3C,

    /** 0x3D - Track panning slide to:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track panning position ranging from 0x01 (almost full
       stereo left panning) to 0xFE (almost full stereo right panning)
       while 0x80 is exact centered panning (i.e. the note will be
       played with half of volume at left and right speakers) to slide
       to (the target track stereo position desired) and yy is the
       sub-panning track slide to position which allows to set 1/256th
       of a track panning slide to position, 0x00 to execute the slide
       instead of setting it or 0xFF to do a fine track panning slide
       instead, i.e. execute it only once per row (first tick). Other
       stereo positions are scaled to track panning position.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_TPAN_SLD_TO   = 0x3D,

    /** 0x3E - Track pannolo / panbrello:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track pannolo speed and yy is the track pannolo
       depth. The track pannolo values are compatible with the MOD,
       S3M, XM and IT pannolo if an inverted pannolo envelope with
       a length of 64 ticks and the amplitude of 256 is used.
       xx is unsigned and yy is signed. A negative track pannolo
       depth means that the pannolo envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_TRK_PANNOLO   = 0x3E,

    /** 0x3F - Track pannolo / panbrello once:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the track pannolo speed and yy is the track pannolo depth.
       The track pannolo values are compatible with the MOD, S3M, XM
       and IT pannolo if an inverted pannolo envelope with a length
       of 64 ticks and the amplitude of 256 is used. The track pannolo
       is executed only once per row, i.e. at first tick.
       xx is unsigned and yy is signed. A negative pannolo depth
       means that the pannolo envelope values will be inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_TPANOLO_ONCE  = 0x3F,

    /* Track effect commands.  */

    /** 0x40 - Set channel tempo:
       Data word consists of a 16-bit unsigned value which
       represents the number of ticks per row to change to
       for this track or zero for marking the song end.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_TEMPO     = 0x40,

    /** 0x41 - Set channel tempo:
       Data word consists of a 16-bit signed value which
       represents the relative number of ticks per row to
       add for this track. A positive value will make the
       song slower (adds more ticks per row) and a negative
       value will make it faster instead.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_REL_TMPO  = 0x41,

    /** 0x42 - Pattern break:
       Data word consists of a 16-bit unsigned value which
       represents the new row where to start next track after
       breaking current track. Zero is the first track.  */
    AVSEQ_TRACK_EFFECT_CMD_PATT_BREAK    = 0x42,

    /** 0x43 - Position jump:
       Data word consists of a 16-bit unsigned value which
       represents the new position / order list number to
       jump to (zero being the first order list entry).
       The current track is cancelled immediately.  */
    AVSEQ_TRACK_EFFECT_CMD_POS_JUMP      = 0x43,

    /** 0x44 - Relative position jump:
       Data word consists of a 16-bit signed value which
       represents the new relative position / order list
       number to jump to. A positive value means a forward
       jump and negative values represent backward jumping.  */
    AVSEQ_TRACK_EFFECT_CMD_REL_POS_JUMP  = 0x44,

    /** 0x45 - Change pattern:
       Data word consists of a 16-bit unsigned value which
       represents the new track number where to switch
       to. Unlike pattern break this effect does not change
       the track immediately but finishes the currently
       track being played.  */
    AVSEQ_TRACK_EFFECT_CMD_CHG_PATTERN   = 0x45,

    /** 0x46 - Reverse play control:
       Data word consists of a 16-bit unsigned value which
       either has the value 0xFF00 for changing playback
       direction to always backwards, 0x0001 for changing
       to always forwards and zero for inverting current
       playback direction.  */
    AVSEQ_TRACK_EFFECT_CMD_REVERSE_PLAY  = 0x46,

    /** 0x47 - Pattern delay:
       Data word consists of a 16-bit unsigned value which
       determines the value of number of rows to compact
       to one, i.e. the row will be delayed for this track
       while the effects continue normal executing.  */
    AVSEQ_TRACK_EFFECT_CMD_PATT_DELAY    = 0x47,

    /** 0x48 - Fine pattern delay:
       Data word consists of a 16-bit unsigned value which
       determines the value of number of ticks to compact delay
       this track, the effects continue normal executing.  */
    AVSEQ_TRACK_EFFECT_CMD_F_PATT_DELAY  = 0x48,

    /** 0x49 - Pattern loop:
       Data word consists of a 16-bit unsigned value which determines
       determines either setting the loop mark when the value is zero
       which pushes the current order list / position number on the
       loop stack) or when using a non-zero value, it will cause to
       jump back to this order list entry number. If the loop stack
       is full, the newest loop mark will be overwritten.  */
    AVSEQ_TRACK_EFFECT_CMD_PATT_LOOP     = 0x49,

    /** 0x4A - GoSub:
       Data word consists of a 16-bit unsigned value which represents
       the new order list entry number where to jump to. The current
       order list entry and track row number is pushed on the GoSub
       stack, if the stack is not full and continue playback at the
       new order list entry otherwise simply nothing happens.  */
    AVSEQ_TRACK_EFFECT_CMD_GOSUB         = 0x4A,

    /** 0x4B - Return from GoSub:
       Data word consists of a 16-bit unsigned value which determines,
       if set to non-zero, the row number of the track number stored
       with the GoSub command otherwise it will continue playback at
       specified row number. If the GoSub stack is empty, this track
       will be disabled for the whole song.  */
    AVSEQ_TRACK_EFFECT_CMD_RETURN        = 0x4B,

    /** 0x4C - Channel synchronization:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the first channel to synchronize with and yy the second
       channel to synchronize with. If both xx and yy are zero, it
       will synchronize with all channels instead, if both xx and
       yy are equal then only synchronize with one channel. The
       current channel will be halted until all synchronization
       is finished. Both xx and yy represent unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_CHANNEL_SYNC  = 0x4C,

    /** 0x4D - Set target sub-slide to:
       Data word consists of two unsigned 8-bit pairs named xx and yy
       where xx defines the the target sub-slide types to set the
       sub-slide values to for having 256x more finer control which
       is represented by yy to as declared by the following table:
         xx | Meanings
       0x01 | Apply sub-slide to volume
       0x02 | Apply sub-slide to track volume
       0x04 | Apply sub-slide to global volume
       0x08 | Apply sub-slide to panning
       0x10 | Apply sub-slide to track panning
       0x20 | Apply sub-slide to global panning

       You can add these values together to set multiple target values
       at once, e.g. 0x11 will apply the sub-slide value to volume and
       track panning.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_SUBSLIDE  = 0x4D,

    /* Instrument, sample and synth effect commands.  */

    /** 0x50 - Sample offset high word:
       Data word consists of a 16-bit unsigned value which
       is the high word (upper 16 bits) of the target
       sample position to start the sample playback at.
       This effect does not change actually the sample
       position (see below).  */
    AVSEQ_TRACK_EFFECT_CMD_SMP_OFF_HIGH  = 0x50,

    /** 0x51 - Sample offset low word:
       Data word consists of a 16-bit unsigned value which is the low
       word (lower 16 bits) of the target sample position to start
       the sample playback at. This effect executes the actual change
       and sets the sample position according to the high word value
       of effect 0x50 with the formula high word * 0x10000 + low word.
       This allows setting the sample playback start position with an
       accuracy of exactly one sample.  */
    AVSEQ_TRACK_EFFECT_CMD_SMP_OFF_LOW   = 0x51,

    /** 0x52 - Set hold:
       Data word consists of a 16-bit unsigned value which
       represents the new hold count of MED-style trackers
       to be used. This allows resetting the hold instrument.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_HOLD      = 0x52,

    /** 0x53 - Set decay:
       Data word consists of a 16-bit unsigned value which
       contains either the decay to be set (if decay is not
       alread in action or the decay counter otherwise, i.e.
       new hold count of MED-style trackers.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_DECAY     = 0x53,

    /** 0x54 - Set transpose:
       Data word consists of two signed 8-bit pairs named xx and yy
       where xx is the new transpose and yy the new finetune value
       which should be applied to the new note. This command has
       no effect if not used in conjunction with a new note.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_TRANSP    = 0x54,

    /** Instrument control. Data word = xxyy.  */
    /** 0x55 - Instrument control:
       Data word consists of two 8-bit pairs named xx and yy
       where xx are the turn off bits and yy the turn off bits.
       Bits not touched by either will be prevented from the
       special instrument control changes. The table is declared as:
       xx/yy | Meanings
        0x01 | Apply to use sample panning bit. If set, the sample
               panning with be used instead of the instrument panning.
        0x02 | Apply to order list is transposable bit. If this is
               set, the order list entry's transpose values are
               applied to the final note.
        0x08 | Apply to use slide envelopes portamento bit. If set,
               slide envelope values are interpreted as portamento
               values instead of transpose and finetune.
        0x10 | Apply to use slide envelope linear frequency bit. If
               this one is set, the slide envelopes will use
               linear frequency scaling instead of Amiga frequency
               scaling.
        0x20 | Apply to set sample offset relative bit. If this is
               set, the commands 0x50 and 0x51 are applied relative
               to the current sample playback position instead of
               being absolute.
        0x80 | Apply to use auto vibrato linear frequency bit. If set,
               the auto vibratos will use linear frequency scaling
               instead of the Amiga one.  */
    AVSEQ_TRACK_EFFECT_CMD_INS_CONTROL   = 0x55,

    /** 0x56 - Instrument change:
       Data word consists of one upper 4-bit nibble named x and of a
       12-bit value named yyy. All values are considered unsigned and
       the meanings of x change by declarition of the following table:
       x | Meanings
       0 | Change instrument global volume to the lower 8 bits of yyy.
       1 | Change instrument volume swing to yyy.
       2 | Change instrument panning swing to yyy.
       3 | Change instrument pitch swing to to yyy in percentage.
       4 | Change instrument fadeout to yyy * 0x10.
       5 | Change fadeout count to yyy * 0x10 if yyy is non-zero,
           otherwise immediately stop the fadeout.
       6 | Auto vibrato / tremolo / pannolo change. The upper 4 bits
           of yyy are interpreted as the exact change type which is
           declared by the following table:
             y | Meanings
           0x0 | Set auto vibrato sweep to the lower 8 bits of yyy.
           0x1 | Set auto vibrato depth to the lower 8 bits of yyy.
           0x2 | Set auto vibrato rate to the lower 8 bits of yyy.
           0x4 | Set auto tremolo sweep to the lower 8 bits of yyy.
           0x5 | Set auto tremolo depth to the lower 8 bits of yyy.
           0x6 | Set auto tremolo rate to the lower 8 bits of yyy.
           0x8 | Set auto pannolo sweep to the lower 8 bits of yyy.
           0x9 | Set auto pannolo depth to the lower 8 bits of yyy.
           0xA | Set auto pannolo rate to the lower 8 bits of yyy.
       7 | Set pitch panning separation to yyy.
       8 | Set pitch panning center to the lower 8 bits of yyy.
       9 | Set DCA (Decay Action) to either cut when yyy equals zero,
           keyoff if yyy equals 0x001 and fadeout for yyy
           being 0x002.  */
    AVSEQ_TRACK_EFFECT_CMD_INS_CHANGE    = 0x56,

    /** 0x57 - Synth control:
       Data word consists of two 8-bit pairs named xx and yy where
       xx the amount of following entries (see table below) are
       to be included into changing values, e.g. 0x0F10 would change
       all variable numbers since first variable starts at 0x10 and
       the following 15 variables are affected also, i.e. a total of
       16 value changes, or zero for only changing the value(s)
       declared by yy with the following table which also can have the
       most upper bit set to indicate that the value should be set
       immediately by using the previous synth value (0x58) effect:
         yy | Meanings
       0x00 | Set volume handling code position.
       0x01 | Set panning handling code position.
       0x02 | Set slide handling code position.
       0x03 | Set special handling code position.
       0x04 | Set volume sustain release position.
       0x05 | Set panning sustain release position.
       0x06 | Set slide sustain release position.
       0x07 | Set special sustain release position.
       0x08 | Set volume NNA trigger position.
       0x09 | Set panning NNA trigger position.
       0x0A | Set slide NNA trigger position.
       0x0B | Set special NNA trigger position.
       0x0C | Set volume DNA trigger position.
       0x0D | Set panning DNA trigger position.
       0x0E | Set slide DNA trigger position.
       0x0F | Set special DNA trigger position.
       0x1y | Set value of the variable number specified by the
              lower 4 bits of yy.
       0x20 | Set volume condition variable value.
       0x21 | Set panning condition variable value.
       0x22 | Set slide condition variable value.
       0x23 | Set special condition variable value.
       0x24 | Set sample waveform.
       0x25 | Set vibrato waveform.
       0x26 | Set tremolo waveform.
       0x27 | Set pannolo waveform.
       0x28 | Set arpeggio waveform.  */
    AVSEQ_TRACK_EFFECT_CMD_SYNTH_CTRL    = 0x57,

    /** 0x58 - Set synth value:
       Data word consists of a 16-bit unsigned value which
       contains the actual value to be set, since command
       0x57 (synth control) does only set the type not the
       actual value (unless most upper bit is set).  */
    AVSEQ_TRACK_EFFECT_CMD_SET_SYN_VAL   = 0x58,

    /** 0x59 - Envelope control:
       Data word consists of two unsigned 8-bit pairs named xx and yy
       where xx is the kind of envelope to be selected as declared by
       the following table which can have the most upper bit set to
       indicate that the value should be set immedately by using the
       previous envelope value (0x5A) effect:
         xx | Meanings
       0x00 | Select volume envelope.
       0x01 | Select panning envelope.
       0x02 | Select slide envelope.
       0x03 | Select vibrato envelope.
       0x04 | Select tremolo envelope.
       0x05 | Select pannolo envelope.
       0x06 | Select channolo envelope.
       0x07 | Select spenolo envelope.
       0x08 | Select auto vibrato envelope.
       0x09 | Select auto tremolo envelope.
       0x0A | Select auto pannolo envelope.
       0x0B | Select track tremolo envelope.
       0x0C | Select track pannolo envelope.
       0x0D | Select global tremolo envelope.
       0x0E | Select global pannolo envelope.
       0x0F | Select arpeggio envelope, an yy of 0x01, 0x11, 0x02 or
              0x12 will not have an effect for this kind of envelope.
       0x10 | Select resonance filter envelope.

       yy selects the envelope type of which to change the value as
       declared by the following table:
         yy | Meanings
       0x00 | Set the waveform number.
       0x10 | Reset envelope, no command 0x5A required.
       0x01 | Set retrigger flag off, no command 0x5A required.
       0x11 | Set retrigger flag on, no command 0x5A required.
       0x02 | Set random flag off, no command 0x5A required.
       0x12 | Set random flag on, no command 0x5A required.
       0x22 | Set random delay flag off, no command 0x5A required.
       0x32 | Set random delay flag on, no command 0x5A required.
       0x03 | Set count and set off, no command 0x5A required.
       0x13 | Set count and set on, no command 0x5A required.
       0x04 | Set envelope position by tick.
       0x14 | Set envelope position by node number.
       0x05 | Set envelope tempo.
       0x15 | Set envelope tempo relatively.
       0x25 | Set fine envelope tempo (count).
       0x06 | Set sustain start point.
       0x07 | Set sustain end point.
       0x08 | Set sustain loop count value.
       0x09 | Set sustain loop counted value.
       0x0A | Set loop start point.
       0x1A | Set current loop start point.
       0x0B | Set loop end point.
       0x1B | Set current loop end point.
       0x0C | Set loop count value.
       0x0D | Set loop counted value.
       0x0E | Set random lowest value.
       0x0F | Set random highest value.  */
    AVSEQ_TRACK_EFFECT_CMD_ENV_CONTROL   = 0x59,

    /** 0x5A - Set envelope value:
       Data word consists of a 16-bit unsigned value which
       contains the actual value to be set, since command
       0x59 (envelope control) does only set the type not
       the actual value (unless most upper bit is set).  */
    AVSEQ_TRACK_EFFECT_CMD_SET_ENV_VAL   = 0x5A,

    /** 0x5B - NNA (New Note Action) control:
       Data word consists of two unsigned 8-bit pairs named xx and yy
       where xx is the kind of note action to be changed and yy
       the type of note action to be controlled. xx and yy are declared
       as by the following table:
         xxyy | Meanings
       0x0000 | Note cut (as known to all trackers).
       0x0001 | Note off (do a keyoff note on previous note).
       0x0002 | Note continue (until sample end).
       0x0003 | Note fade (fade out previous note).
       0x1100 | Set all DCT (Duplicate Note Check) bits (this is
                also the same as a xxyy value of 0x11FF).
       0x1101 | Set DCT (Duplicate Note Check) Type to instrument note
                (trigger logical OR combined).
       0x1102 | Set DCT to sample note (trigger logical OR combined).
       0x1104 | Set DCT to instrument (trigger logical OR combined).
       0x1108 | Set DCT to sample (trigger logical OR combined).
       0x1110 | Set DCT to instrument note (trigger logical AND
                combined).
       0x1120 | Set DCT to sample note (trigger logical AND combined).
       0x1140 | Set DCT to instrument (trigger logical AND combined).
       0x1180 | Set DCT to sample (trigger logical AND combined).
       0x0100 | Clear all DCT (Duplicate Note Check) bits (this is
                also the same as a xxyy value of 0x01FF).
       0x0101 | Clear DCT to instrument note (logical OR combined).
       0x0102 | Clear DCT to sample note (logical OR combined).
       0x0104 | Clear DCT to instrument (logical OR combined).
       0x0108 | Clear DCT to sample (logical OR combined).
       0x0110 | Clear DCT to instrument note (logical AND combined).
       0x0120 | Clear DCT to sample note (logical AND combined).
       0x0140 | Clear DCT to instrument (logical AND combined).
       0x0180 | Clear DCT to sample (logical AND combined).
       0x0200 | Set DNA (Duplicate Note Action) to cut duplicate.
       0x0201 | Set DNA (Duplicate Note Action) to do a keyoff note.
       0x0202 | Set DNA (Duplicate Note Action) to fadeout note.

       Remember, DCT bits can be combined, e.g. a yy value of 0x06
       will affect both instrument and sample note to be logically
       compared with OR when an xx of 0x11 would be used.  */
    AVSEQ_TRACK_EFFECT_CMD_NNA_CONTROL   = 0x5B,

    /** 0x5C - Loop control:
       Data word consists of one upper 4-bit nibble named x and of a
       12-bit value named yyy. All values are considered unsigned and
       the meanings of x change by declarition of the following table
       which incides the kind of loop control to be applied:
         x | Meanings
       0x0 | Change loop mode where a yyy value of zero means turning
             off the loop, 0x001 will declare a normal forward loop,
             0x002 will declare an always backward loop and 0x003
             declares a ping-pong loop (forward <=> backward).
       0x1 | Set repeat start value to yyy * 0x100
       0x2 | Set repeat length value to yyy * 0x100
       0x3 | Set loop counting value or 0x000 for
             unlimited loop count).
       0x4 | Set loop counted value.
       0x5 | Change sustain loop mode where a yyy value of zero means
             turning off the sustain loop, 0x001 will declare a normal
             forward sustain loop, 0x002 will declare an always
             backward sustain loop and 0x003 declares a ping pong
             sustain loop.
       0x6 | Set sustain repeat start to yyy * 0x100.
       0x7 | Set sustain repeat length to yyy * 0x100.
       0x8 | Set sustain loop count value.
       0x9 | Set sustain loop counted value.
       0xA | Change playback direction where 0xFFF represents backward
             direction, 0x001 is forward direction and a value of zero
             indicates a simple inversion of playback direction,  */
    AVSEQ_TRACK_EFFECT_CMD_SET_LOOP      = 0x5C,

    /* Global effect commands.  */

    /** 0x60 - Set speed:
       Data word consists of one upper 4-bit nibble named x and of a
       12-bit value named yyy. All values are considered unsigned and
       the meanings of x indicate what kind of tempo or speed should
       be changed as declared by the following table:
       x | Meanings
       0 | Change BPM speed (beats per minute)
       1 | Change BPM tempo (rows per beat)
       2 | Change SPD (MED-style timing)
       7 | Apply nominator represented by bits 4-7 of yyy divided by
           denominator represented in the lower 4 bits of yyy, e.g.
           a value of 0x025 will scale speed down-rounded by 2/5.

       If 8 is added is added to x it will only set the value of the
       but not immediately use it. This can be useful if you want to
       want to prepare a pattern that uses MED-style SPD timing when
       the current one uses BPM. If x is 0 or 1, BPM timing will be
       used. BPM tempo tells how many rows are considered as a beat.
       The default value for BPM speed is 125, for BPM tempo, 4
       and for SPD timing it is 33. To use a pre-defined speed value,
       just set yyy to zero. x = 7 is a special case, if it's set
       (default is 1/1), the speed values will be multiplied by the
       value of the higher nibble of the low word and then divided by
       the lower nibble of the low word, e.g. a xyyy of 0x7023 will
       cause SPD timing to be multiplied by 2 and then divided by 3,
       causing a 2/3 down-rounded of SPD to be used. SPD values
       smaller or equal than 10 be recognized as old SoundTracker
       tempos which is required for MED compatibility.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_SPEED     = 0x60,

    /** 0x61 - Speed slide faster:
       Data word consists of a 16-bit unsigned value which contains
       the actual speed faster slide value. It will always slide the
       currently selected timing mode with set tempo (0x60) command.
       Note that the new slide faster value will only be applied to
       the playback engine if the selected timing mode is smaller
       than 8 (see there). Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_FAST  = 0x61,

    /** 0x62 - Speed slide slower:
       Data word consists of a 16-bit unsigned value which contains
       the actual speed slower slide value. It will always slide the
       currently selected timing mode with set tempo (0x60) command.
       Note that the new slide slower value will only be applied to
       the playback engine if the selected timing mode is smaller
       than 8 (see there). Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_SLOW  = 0x62,

    /** 0x63 - Fine speed slide faster:
       Data word consists of a 16-bit unsigned value which contains
       the actual speed faster slide value. It will always slide the
       currently selected timing mode with set tempo (0x60) command.
       Note that the new slide faster value will only be applied to
       the playback engine if the selected timing mode is smaller
       than 8 (see there). The fine speed slide faster is done only
       once per row i.e. just executed at the first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_S_SLD_FAST  = 0x63,

    /** 0x64 - Fine speed slide slower:
       Data word consists of a 16-bit unsigned value which contains
       the actual speed slower slide value. It will always slide the
       currently selected timing mode with set tempo (0x60) command.
       Note that the new slide slower value will only be applied to
       the playback engine if the selected timing mode is smaller
       than 8 (see there). The fine speed slide slower is done only
       once per row i.e. just executed at the first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_S_SLD_SLOW  = 0x64,

    /** 0x65 - Speed slide to:
       Data word consists of two 8-bit pairs named xx and yy where
       xx represents the unsigned speed ranging from 0x01 - 0xFE
       to slide to (the target speed desired), 0x00 to execute the
       slide instead of setting it or 0xFF to execute a fine speed
       slide instead, i.e. only once per row and yy is the signed
       slide count number, i.e. the value to be added to current
       speed. It will always slide the currently selected timing
       mode with set tempo (0x60) command. Note that the new slide
       to value will only be applied to the playback engine if the
       selected timing mode is smaller than 8 (see there).  */
    AVSEQ_TRACK_EFFECT_CMD_SPD_SLD_TO    = 0x65,

    /** 0x66 - Spenolo (vibrato for speed):
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the spenolo speed and yy is the spenolo depth. The
       spenolo values are compatible with the MOD, S3M, XM and IT
       spenolo if the spenolo envelope with a length of 64 ticks
       and the amplitude of 256. xx is unsigned and yy is signed.
       A negative spenolo depth means that the spenolo envelope values
       will be inverted. It will always apply to the currently
       selected timing mode with set tempo (0x60) command. Note that
       the actual tempo will only be applied to the playback engine
       if the selected timing mode is smaller than 8 (see there).  */
    AVSEQ_TRACK_EFFECT_CMD_SPENOLO       = 0x66,

    /** 0x67 - Spenolo once (vibrato once for speed):
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the spenolo speed and yy is the spenolo depth. The
       spenolo values are compatible with the MOD, S3M, XM and IT
       spenolo if the spenolo envelope with a length of 64 ticks
       and the amplitude of 256. The spenolo is done only once per
       row, i.e. only at first tick. xx is unsigned and yy is signed.
       A negative spenolo depth means that the spenolo envelope values
       will be inverted. It will always apply to the currently
       selected timing mode with set tempo (0x60) command. Note that
       the actual tempo will only be applied to the playback engine
       if the selected timing mode is smaller than 8 (see there).  */
    AVSEQ_TRACK_EFFECT_CMD_SPENOLO_ONCE  = 0x67,

    /** 0x68 - Channel control:
       Data word consists of one upper 4-bit pair named x, followed
       by one 4-bit pair named y (bits 8-11) and an 8-bit pair named
       zz where x is the kind of the channel action and y chooses the
       channel aspect to be controlled. This effect allows to redirect
       effects to another channel. The difference between once effects
       and normal effects are, that once returns after one effect
       automatically while the other needs a xyzz of 0x0000. This does
       not affect notes, only effects. The main idea behind this is
       that channels can control other channels by performing, for
       example a pattern break. This allows some synchronization
       within your channels. A MOD, S3M, XM and IT compatible mode can
       be obtained by using global channel control and a control mode
       of whole song and affecting non-note effects only, The xy
       values are declared according to the following table:
         xy | Meanings
       0x00 | Set channel control mode. Exact action is defined by
              zz as follows:
                zz | Meanings
              0x00 | Disable channel control.
              0x01 | Use normal channel control.
              0x02 | Use multiple channel control.
              0x03 | Use global channel control.
              0x04 | Use multiple select channel control.
              0x05 | Use multiple deselect channel control.
              0x06 | Use multiple invert channel control.
              0x10 | Use normal control mode (once control mode).
              0x11 | Use one tick control mode.
              0x12 | Use one row control mode.
              0x13 | Use one track control mode.
              0x14 | Use whole song control mode.
              0x20 | Affect note effects.
              0x21 | Do not affect note effects.
              0x30 | Affect non-note effects.
              0x31 | Do not affect non-note effects.
       0x01 | Set effect channel to zz.
       0x02 | Slide effect channel left by zz.
       0x03 | Slide effect channel right by zz.
       0x04 | Fine slide effect channel left by zz.
       0x05 | Fine slide effect channel right by zz.
       0x06 | Set target slide effect channel to zz.
       0x07 | Do actual slide effect channel to zz.
       0x08 | Do actual fine slide effect channel to zz.
       0x09 | Channolo (Vibrato for channels). The upper 4 bits of zz
              are unsigned channolo speed and the lower 4 bits are the
              the signed channolo depth. A negative channolo depth
              will cause the channolo envelope values to be inverted.
       0x0A | Fine channolo (fine vibrato for channels). The upper 4
              bits of zz are unsigned channolo speed and the lower 4
              bits are the the signed channolo depth. A negative
              channolo depth will cause the channolo envelope values
              to be inverted. The channolo will be executed only once.
       0x10 | Channel surround mode. Finer surround control is
              declared as in the following table:
                zz | Meanings
              0x00 | Turn channel surround panning off.
              0x01 | Turn channel surround panning on.
              0x10 | Turn track surround panning off.
              0x11 | Turn track surround panning on.
              0x20 | Turn global surround panning off.
              0x21 | Turn global surround panning on.
       0x11 | Mutes the channel is zz is zero, un-mutes otherwise.  */
    AVSEQ_TRACK_EFFECT_CMD_CHANNEL_CTRL  = 0x68,

    /** 0x69 - Set global volume:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global volume level (ranging either from 0x00 - 0xFF
       or in tracker volume mode from 0x00 - 0x40) and yy is the
       sub-volume global level which allows to set 1/256th of a global
       volume level. Other volume levels are scaled with global volume.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_G_VOLUME  = 0x69,

    /** 0x6A - Global volume slide up:
       Data word consists of two 8-bit pairs named xx and yy
       where xx is the global volume level (ranging either from
       0x00 - 0xFF or in tracker volume mode from 0x00 - 0x40)
       to slide up and yy is the sub-volume global slide up level
       which allows to set 1/256th of a global volume slide up level.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_UP    = 0x6A,

    /** 0x6B - Global volume slide down:
       Data word consists of two 8-bit pairs named xx and yy where xx
       is the global volume level (ranging either from 0x00 - 0xFF or
       in tracker volume mode from 0x00 - 0x40) to slide down and yy
       is the sub-volume global slide down level which allows to set
       1/256th of a global volume slide down level.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_DOWN  = 0x6B,

    /** 0x6C - Fine global volume slide up:
       Data word consists of two 8-bit pairs named xx and yy where xx
       xx is the global volume level (ranging either from 0x00 - 0xFF
       or in tracker volume mode from 0x00 - 0x40) to slide up and yy
       is the sub-volume global slide up level which allows to set
       1/256th of a global volume slide up level. The fine global
       volume slide up is done only once per row, i.e. at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_G_VOL_UP    = 0x6C,

    /** 0x6D - Fine global volume slide down:
       Data word consists of two 8-bit pairs named xx and yy where xx
       is the global volume level (ranging either from 0x00 - 0xFF or
       in tracker volume mode from 0x00 - 0x40) to slide down and yy
       is the sub-volume global slide down level which allows to set
       1/256th of a global volume slide down level. The fine global
       volume slide down is done only once per row (at first tick).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_F_G_VOL_DOWN  = 0x6D,

    /** 0x6E - Global volume slide to:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is either the global volume level (ranging either from
       0x01 - 0xFE or in tracker volume mode from 0x01 - 0x3F) to
       slide to (the target global volume desired) and yy is the
       sub-volume global slide to level which allows to set 1/256th
       of a global volume slide to level, 0x00 to execute the slide
       instead of setting it or 0xFF to execute a fine global volume
       slide instead (only once per row).
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_G_VOLSL_TO    = 0x6E,

    /** 0x6F - Global tremolo:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global tremolo speed and yy is the global tremolo
       depth. The global tremolo values are compatible with the MOD,
       S3M, XM and IT tremolo if an inverted tremolo envelope with
       a length of 64 ticks and the amplitude of 256 is used.
       xx is unsigned and yy is signed. A negative global tremolo
       depth means that the tremolo envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_G_TREMOLO     = 0x6F,

    /** 0x70 - Global tremolo once:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global tremolo speed and yy is the global tremolo
       depth. The global tremolo values are compatible with the MOD,
       S3M, XM and IT tremolo if an inverted tremolo envelope with a
       length of 64 ticks and the amplitude of 256 is used. The
       tremolo is executed only once per row, i.e. at first tick.
       xx is unsigned and yy is signed. A negative tremolo depth
       means that the tremolo envelope values will be inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_G_TREMO_ONCE  = 0x70,

    /** 0x71 - Set global panning:
       Data word consists of two 8-bit pairs named xx and yy where xx
       is the global panning position ranging from 0x00 (full stereo
       left panning) to 0xFF (full stereo right panning) while 0x80
       is exact centered panning (i.e. the note will be played with
       half of volume at left and right speakers) and yy is the
       sub-panning position which allows to set 1/256th of a global
       panning position. Other stereo positions are scaled to global
       panning position. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_SET_G_PAN     = 0x71,

    /** 0x72 - Global panning slide left:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global panning left slide position ranging from
       0x00 - 0xFF and yy is the sub-panning global slide left
       position which allows to set 1/256th of a panning slide
       left position. Other stereo positions are scaled to global
       panning position. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_GPANSL_LEFT   = 0x72,

    /** 0x73 - Global panning slide right:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global panning right slide position ranging from
       0x00 - 0xFF and yy is the sub-panning global slide right
       position which allows to set 1/256th of a panning slide
       right position. Other stereo positions are scaled to global
       panning position. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_GPANSL_RIGHT  = 0x73,

    /** 0x74 - Fine global panning slide left:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global panning left slide position ranging from
       0x00 - 0xFF and yy is the sub-panning global slide left
       position which allows to set 1/256th of a panning slide
       left position. Other stereo positions are scaled to global
       panning position. The fine global panning slide left is done
       only once per row, i.e. only at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_FGP_SL_LEFT   = 0x74,

    /** 0x75 - Fine global panning slide right:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global panning right slide position ranging from
       0x00 - 0xFF and yy is the sub-panning global slide right
       position which allows to set 1/256th of a panning slide
       right position. Other stereo positions are scaled to global
       panning position. The fine global panning slide right is done
       only once per row, i.e. only at first tick.
       Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_FGP_SL_RIGHT  = 0x75,

    /** 0x76 - Global panning slide to:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global panning position ranging from 0x01 (almost
       full stereo left panning) to 0xFE (almost full stereo right
       panning) while 0x80 is exact centered panning (i.e. the note
       will be played with half of volume at left and right speakers)
       to slide to (the target global stereo position desired) and yy
       is the sub-panning global slide to position which allows to set
       1/256th of a global panning slide to position, 0x00 to execute
       the slide instead of setting it or 0xFF to do a fine global
       panning slide instead, i.e. execute it only once per row (at
       first tick). Other stereo positions are scaled to global panning
       position. Both xx and yy are unsigned values.  */
    AVSEQ_TRACK_EFFECT_CMD_GPANSL_TO     = 0x76,

    /** 0x77 - Global pannolo / panbrello:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global pannolo speed and yy is the global pannolo
       depth. The global pannolo values are compatible with the MOD,
       S3M, XM and IT pannolo if an inverted pannolo envelope with
       a length of 64 ticks and the amplitude of 256 is used.
       xx is unsigned and yy is signed. A negative global pannolo
       depth means that the pannolo envelope values will be
       inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_G_PANNOLO    = 0x77,

    /** 0x78 - Global pannolo / panbrello once:
       Data word consists of two 8-bit pairs named xx and yy where
       xx is the global pannolo speed and yy is the global pannolo
       depth. The global pannolo values are compatible with the MOD,
       S3M, XM and IT pannolo if an inverted pannolo envelope with a
       length of 64 ticks and the amplitude of 256 is used. The global
       pannolo is executed only once per row, i.e. only at first tick.
       xx is unsigned and yy is signed. A negative pannolo depth
       means that the pannolo envelope values will be inverted.  */
    AVSEQ_TRACK_EFFECT_CMD_G_PANNOLO_O  = 0x78,

    /** User customized effect for trigger in demos, etc.  */
    AVSEQ_TRACK_EFFECT_CMD_USER         = 0x7F,
};

/**
 * Song track effect structure, This structure is actually for one row
 * and therefore actually pointed as an array with the amount of
 * rows of the whole track.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerTrackEffect {
    /** Effect command byte.  */
    uint8_t command;

    /** Effect command data word.  */
    uint16_t data;
} AVSequencerTrackEffect;

/** AVSequencerTrackEffect->note values.  */
enum AVSequencerTrackDataNote {
    /** ---  */
    AVSEQ_TRACK_DATA_NOTE_NONE          = 0,

    /** C-n  */
    AVSEQ_TRACK_DATA_NOTE_C             = 1,

    /** C#n = Dbn  */
    AVSEQ_TRACK_DATA_NOTE_C_SHARP       = 2,

    /** D-n  */
    AVSEQ_TRACK_DATA_NOTE_D             = 3,

    /** D#n = Ebn  */
    AVSEQ_TRACK_DATA_NOTE_D_SHARP       = 4,

    /** E-n  */
    AVSEQ_TRACK_DATA_NOTE_E             = 5,

    /** F-n  */
    AVSEQ_TRACK_DATA_NOTE_F             = 6,

    /** F#n = Gbn  */
    AVSEQ_TRACK_DATA_NOTE_F_SHARP       = 7,

    /** G-n  */
    AVSEQ_TRACK_DATA_NOTE_G             = 8,

    /** G#n = Abn  */
    AVSEQ_TRACK_DATA_NOTE_G_SHARP       = 9,

    /** A-n  */
    AVSEQ_TRACK_DATA_NOTE_A             = 10,

    /** A#n = Bbn  */
    AVSEQ_TRACK_DATA_NOTE_A_SHARP       = 11,

    /** B-n = H-n  */
    AVSEQ_TRACK_DATA_NOTE_B             = 12,

    /** ^^^ = note kill  */
    AVSEQ_TRACK_DATA_NOTE_KILL          = -1,

    /** ^^- = note off  */
    AVSEQ_TRACK_DATA_NOTE_OFF           = -2,

    /** === = keyoff note  */
    AVSEQ_TRACK_DATA_NOTE_KEYOFF        = -3,

    /** -|- = hold delay  */
    AVSEQ_TRACK_DATA_NOTE_HOLD_DELAY    = -4,

    /** -\- = note fade  */
    AVSEQ_TRACK_DATA_NOTE_FADE          = -5,

    /** END = pattern end marker  */
    AVSEQ_TRACK_DATA_NOTE_END           = -16,
};

/**
 * Song track data structure, This structure is actually for one row
 * and therefore actually pointed as an array with the amount of
 * rows of the whole track.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerTrackData {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Array (of size effects) of pointers containing all effects
       used by this track.  */
    AVSequencerTrackEffect **effects_data;

    /** Number of effects used by this track.  */
    uint16_t effects;

    /** Which octave the note is played upon, if note is a positive
       value (defaults to 4). Allowed values are in the [0:9] range,
       since the keyboard definition table has 120 entries (10 octave
       range * 12 notes per octave), also expect trouble with most
       trackers if values outside this range are used.  */
    uint8_t octave;

    /** Note to be played (see defines below, n is octave number).  */
    int8_t note;

    /** Number of instrument to be played or 0 to take previous one.  */
    uint16_t instrument;
} AVSequencerTrackData;

/** AVSequencerTrack->compat_flags bitfield.  */
enum AVSequencerTrackCompatFlags {
    AVSEQ_TRACK_COMPAT_FLAG_SAMPLE_OFFSET       = 0x01, ///< Sample offset beyond end of sample will be ignored (IT compatibility)
    AVSEQ_TRACK_COMPAT_FLAG_TONE_PORTA          = 0x02, ///< Share tone portamento memory with portamentoes and unlock tone portamento samples and adjusts frequency to: new_freq = freq * new_rate / old_rate. If an instrument number is given the envelope will be retriggered (IT compatibility).
    AVSEQ_TRACK_COMPAT_FLAG_SLIDES              = 0x04, ///< Portamentos of same type share the same memory (e.g. porta up/fine porta up)
    AVSEQ_TRACK_COMPAT_FLAG_VOLUME_SLIDES       = 0x08, ///< All except portamento slides share the same memory (e.g. volume/panning slides)
    AVSEQ_TRACK_COMPAT_FLAG_OP_SLIDES           = 0x10, ///< Oppositional portamento directions don't share the same memory (e.g. porta up and porta down)
    AVSEQ_TRACK_COMPAT_FLAG_OP_VOLUME_SLIDES    = 0x20, ///< Oppositional non-portamento slide directions don't share the same memory
    AVSEQ_TRACK_COMPAT_FLAG_VOLUME_PITCH        = 0x40, ///< Volume & pitch slides share same memory (S3M compatibility)
};

/** AVSequencerTrack->flags bitfield.  */
enum AVSequencerTrackFlags {
    AVSEQ_TRACK_FLAG_USE_TIMING             = 0x01, ///< Use track timing fields
    AVSEQ_TRACK_FLAG_SPD_TIMING             = 0x02, ///< SPD speed timing instead of BpM
    AVSEQ_TRACK_FLAG_PANNING                = 0x04, ///< Use track panning and sub-panning fields
    AVSEQ_TRACK_FLAG_SURROUND               = 0x08, ///< Use track surround panning
    AVSEQ_TRACK_FLAG_REVERSE                = 0x10, ///< Playback of track in backward direction
};

/**
 * Song track structure.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerTrack {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Metadata information: Original track file name, track
       title, track message, track artist, track album,
       track begin and finish date of composition and comment.  */
    AVMetadata *metadata;

    /** AVSequencerTrackData pointer to array of track data.  */
    AVSequencerTrackData *data;

    /** Last valid row of track (defaults to 63 = 0x3F) which
       is the default for most trackers (64 rows per pattern).  */
    uint16_t last_row;

    /** Track global volume (defaults to 255 = no volume scaling).  */
    uint8_t volume;

    /** Sub-volume level for this track. This is basically track
       global volume divided by 256, but the sub-volume doesn't
       account into actual mixer output (defaults 0).  */
    uint8_t sub_volume;

    /** Stereo panning level for this track (defaults to
       -128 = central stereo panning).  */
    int8_t panning;

    /** Stereo track sub-panning level for this channel. This is
       basically track panning divided by 256, but the sub-panning
       doesn't account into actual mixer output (defaults 0).  */
    uint8_t sub_panning;

    /** Track transpose. Add this value to octave * 12 + note to
       get the final octave / note played (defaults to 0).  */
    int8_t transpose;

    /** Compatibility flags for playback. There are rare cases
       where track handling can not be mapped into internal
       playback engine and have to be handled specially. For
       each sub-song which needs this, this will define new
       flags which tag the player to handle it to that special
       way.  */
    uint8_t compat_flags;

    /** Track playback flags. Some sequencers feature
       surround panning or allow initial reverse playback,
       different timing methods which have all to be taken
       care specially in the internal playback engine.  */
    uint8_t flags;

    /** Initial number of frames per row, i.e. sequencer tempo
       (defaults to 6 as in most tracker formats).  */
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

    /** Array of pointers containing every unknown data field where
       the last element is indicated by a NULL pointer reference. The
       first 64-bit of the unknown data contains an unique identifier
       for this chunk and the second 64-bit data is actual unsigned
       length of the following raw data. Some formats are chunk based
       and can store information, which can't be handled by some
       other, in case of a transition the unknown data is kept as is.
       Some programs write editor settings for tracks in those chunks,
       which then won't get lost in that case.  */
    uint8_t **unknown_data;
} AVSequencerTrack;

#include "libavsequencer/song.h"

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

#endif /* AVSEQUENCER_TRACK_H */
