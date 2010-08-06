/*
 * AVSequencer synth sound, code, symbol and waveform management
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

#ifndef AVSEQUENCER_SYNTH_H
#define AVSEQUENCER_SYNTH_H

#include "libavutil/log.h"
#include "libavformat/avformat.h"

/**
 * Synth table. Used for both assembling and disassembling.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerSynthTable {
    /** Instruction code name as you have to type in the synth
       sound assembler. This is zero terminated.  */
    uint8_t name[8];

    /** Instruction code in memory. This will allow fast lookup and
       therefore execution of the synth sound code.  */
    uint8_t code;

    /** Input and output flags for this synth code instruction.  */
    uint8_t flags;
    enum AVSequencerSynthTableFlags {
    AVSEQ_SYNTH_TABLE_SRC           = 0x01, ///< Source parameter required
    AVSEQ_SYNTH_TABLE_SRC_LINE      = 0x02, ///< Source parameter is a line number
    AVSEQ_SYNTH_TABLE_SRC_NO_V0     = 0x04, ///< Don't display source variable if v0
    AVSEQ_SYNTH_TABLE_SRC_NO_DATA   = 0x08, ///< Don't display source increment data if 0x0000
    AVSEQ_SYNTH_TABLE_DEST          = 0x10, ///< Destination parameter required
    AVSEQ_SYNTH_TABLE_DEST_DOUBLE   = 0x20, ///< Destination double register required (= vH:vL)
    AVSEQ_SYNTH_TABLE_VAR_PRIORITY  = 0x40, ///< Variables are prioritized against immediate data
    AVSEQ_SYNTH_TABLE_SRC_NOT_REQ   = 0x80, ///< Source parameter is optional
    };
} AVSequencerSynthTable;

/**
 * Synth waveform structure. This structure contains the waveforms
 * for the synth sound code data.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerSynthWave {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Metadata information: Original waveform file name, waveform
     *  name, artist and comment.
     */
    AVMetadata *metadata;

    /** Pointer to raw waveform data, must be padded for
       perfect perfomance gain when accessing sample data.
       Depending on bit depth, the data is either arranged
       in signed 8-bit or 16-bit values.  */
    int16_t *data;

    /** Length of synth waveform data in bytes (default is
       64 bytes).  */
    uint32_t size;

    /** Number of samples for this synth waveform.  */
    uint32_t samples;

    /** Repeat start count in samples for this waveform.  */
    uint32_t repeat;

    /** Repeat length in samples of this waveform.  */
    uint32_t repeat_len;

    /** Synth waveform playback flags. Some sequencers feature
       non-looping waveforms or allow switching between 8-bit
       and 16-bit waveforms which have to be taken care specially
       in the internal playback engine.  */
    uint16_t flags;
    enum AVSequencerSynthWaveFlags {
    AVSEQ_SYNTH_WAVE_FLAGS_NOLOOP   = 0x0080, ///< Don't loop the waveform
    AVSEQ_SYNTH_WAVE_FLAGS_8BIT     = 0x8000, ///< 8-bit waveform instead of a 16-bit one, the GETxxxW instructions return 8-bit values in the upper 8-bits of the 16-bit result
    };
} AVSequencerSynthWave;

/**
 * Synth programming code structure. This contains the byte-layout
 * for executables. THis means that this is the compile target of
 * synth sound instruction set.
 * The programming language is splitted into lines. Each line contains
 * one instruction which does some action (like pitch sliding,
 * vibratos, arpeggios, panning slides, etc.). Each synth has 16
 * 16-bit variables which can be accessed / changed freely by the
 * synth code (they are like assembly language registers). You can do
 * calculations with them. The synth code has 4 entry points:
 * The volume, panning, slide and special entry points. That means
 * that you can treat a synth like a 4-processor system. Although they
 * all share the 16 variables, each has it's own condition variable,
 * where certain instructions (most arithmetic) store several states
 * such as carry, overflow, negative, zero. You can't access the
 * condition variables directly, because it's not required. The
 * condition variables are compatible with the MC680x0 CCR register
 * (same bits with same meanings). That means that you must convert
 * the initial values according to the processor you're coding your
 * player for.
 * All processors should support these flags. If not, you must write
 * small emulation code, but there shouldn't be any speed decreases to
 * be noticed, because that's so simple. The synth programming
 * language supports up to 128 instructions (the negative instruction
 * bytes are the normal track effect commands which corresponds to
 * the logical NOT value of the negative instruction byte).
 * The instruction format is as follows: INST vX+YYYY,vZ where X is
 * the higher 4-bit nibble, Z the lower 4-bit nibble and YYYY the
 * instruction data. If YYYY is zero, it's omitted if Source
 * parameter allowed. If X is zero, the vX+ will not be displayed.
 * The instructions are listed and described below in detail convering
 * parameters description, i.e. which parameter bits the instruction
 * takes, either none, source only, destination only or both. Please
 * note that source always means vX+YYYY, not vX only, except for the
 * instructions from NOT to SWAP. The negative instruction byte is
 * SETFXxx where xx is the inverted instruction byte, i.e. SETFX0E
 * is 0xF1. It will execute the normal effects, in this case it would
 * be vibrato. Please note that the vibrato and tremolo commands
 * can't be invoked with this because they need early preparations and
 * only work on host channels. Use the synth VIBRATO & TREMOLO
 * instructions instead. This doesn't apply to track or global
 * tremolo.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerSynthCode {
    /** Instruction code to execute on this line.  */
    int8_t instruction;
    enum AVSequencerSynthCodeInstruction {
    /** Synth sound instruction codes.  */

    /** Instruction list (only positive instruction byte ones):  */
    /** Flow control / variable accessing instructions:  */
    /** 0x00: STOP    vX+YYYY
       Also named END. Stops the synth sound instruction execution
       here if vX+YYYY is non-zero, otherwise this will set an
       external influence forbid mask in where a set of the most
       upper bit indicates a permit instead of a forbid.
       The mask is defined as in the following table:
         Mask | Meanings
       0x0001 | Forbid external JUMPVOL command for this synth.
       0x0002 | Forbid external JUMPPAN command for this synth.
       0x0004 | Forbid external JUMPSLD command for this synth.
       0x0008 | Forbid external JUMPSPC command for this synth.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_STOP       = 0x00,

    /** 0x01: KILL    vX+YYYY
       Stops and frees current channel, most likely to be used in NNA
       handling code. vX+YYYY is the number of ticks to wait before
       the channel actually will be killed. Synth code instruction
       processing continues as normally until the wait counter has
       been reached. Please note that even with YYYY set to zero, all
       instructions executing in the same tick as the KILL instruction
       will continue do so. If you don't want this, just place a STOP
       instruction straight afterwards.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_KILL       = 0x01,

    /** 0x02: WAIT    vX+YYYY
       Waits the given amount in ticks specified by vX+YYYY before
       continue processing of synth code instructions.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_WAIT       = 0x02,

    /** 0x03: WAITVOL vX+YYYY
       Waits until the volume handling code has reached the line
       specified by vX+YYYY. The delay can be until song end if
       the volume code never reaches the specified line.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_WAITVOL    = 0x03,

    /** 0x04: WAITPAN vX+YYYY
       Waits until the panning handling code has reached the line
       specified by vX+YYYY. The delay can be until song end if
       the panning code never reaches the specified line.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_WAITPAN    = 0x04,

    /** 0x05: WAITSLD vX+YYYY
       Waits until the slide handling code has reached the line
       specified by vX+YYYY. The delay can be until song end if
       the slide code never reaches the specified line.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_WAITSLD    = 0x05,

    /** 0x06: WAITSPC vX+YYYY
       Waits until the special handling code has reached the line
       specified by vX+YYYY. The delay can be until song end if
       the special code never reaches the specified line.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_WAITSPC    = 0x06,

    /** 0x07: JUMP    vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMP       = 0x07,

    /** 0x08: JUMPEQ  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if the zero flag of the
       condition variable is set otherwise do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPEQ     = 0x08,

    /** 0x09: JUMPNE  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if the zero flag of the
       condition variable is cleared otherwise do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPNE     = 0x09,

    /** 0x0A: JUMPPL  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if the negative flag of the
       condition variable is cleared otherwise do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPPL     = 0x0A,

    /** 0x0B: JUMPMI  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if the negative flag of the
       condition variable is set otherwise do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPMI     = 0x0B,

    /** 0x0C: JUMPLT  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if either the negative or
       the overflow flag of the condition variable are set,
       like a signed less than comparision, otherwise will
       do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPLT     = 0x0C,

    /** 0x0D: JUMPLE  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if either the negative or
       the overflow flag and in addition the zero flag of
       the condition variable are set, like a signed less
       or equal than comparision, otherwise will do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPLE     = 0x0D,

    /** 0x0E: JUMPGT  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if neither the negative nor
       the overflow flag of the condition variable are set,
       like a signed greater than comparision, otherwise will
       do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPGT     = 0x0E,

    /** 0x0F: JUMPGE  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if neither the negative nor
       the overflow flag and in addition the zero flag of
       the condition variable are set, like a signed greater
       or equal than comparision, otherwise will do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPGE     = 0x0F,

    /** 0x10: JUMPVS  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if the overflow flag of the
       condition variable is set otherwise do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPVS     = 0x10,

    /** 0x11: JUMPVC  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if the overflow flag of the
       condition variable is cleared otherwise do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPVC     = 0x11,

    /** 0x12: JUMPCS  vX+YYYY
       Also named JUMPLO, jumps to the target line number within the
       same synth code specified by vX+YYYY if the carry flag of the
       condition variable is set otherwise do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPCS     = 0x12,

    /** 0x13: JUMPCC  vX+YYYY
       Also named JUMPHS, jumps to the target line number within the
       same synth code specified by vX+YYYY if the carry flag of the
       condition variable is cleared otherwise do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPCC     = 0x13,

    /** 0x14: JUMPLS  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if both the carry and negative
       flag are set, like an unsigned less or equal than
       comparision, otherwise this one will do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPLS     = 0x14,

    /** 0x15: JUMPHI  vX+YYYY
       Jumps to the target line number within the same synth
       code specified by vX+YYYY if both the carry and negative
       flag are cleared, like an unsigned greater than
       comparision, otherwise this one will do nothing.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPHI     = 0x15,

    /** 0x16: JUMPVOL vX+YYYY
       Jumps the synth sound volume handling code to the target line
       number within the same synth code specified by vX+YYYY.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPVOL    = 0x16,

    /** 0x17: JUMPPAN vX+YYYY
       Jumps the synth sound panning handling code to the target line
       number within the same synth code specified by vX+YYYY.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPPAN    = 0x17,

    /** 0x18: JUMPSLD vX+YYYY
       Jumps the synth sound slide handling code to the target line
       number within the same synth code specified by vX+YYYY.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPSLD    = 0x18,

    /** 0x19: JUMPSPC vX+YYYY
       Jumps the synth sound special handling code to the target line
       number within the same synth code specified by vX+YYYY.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_JUMPSPC    = 0x19,

    /** 0x1A: CALL    vX+YYYY,vZ
       Pushes the next line number being executed to the destination
       variable specified by vZ, then continues execution at the line
       specified by vX+YYYY.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_CALL       = 0x1A,

    /** 0x1B: RETURN  vX+YYYY,vZ
       Pops the next line number being executed from vX+YYYY and
       continues execution there. The old line number will be
       stored in the destination variable specified by vZ.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_RETURN     = 0x1B,

    /** 0x1C: POSVAR  vX+YYYY
       Pushes the next line number being executed to the source
       variable specified by vX and adds YYYY to it afterwards,
       then continues execution normally at the next line.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_POSVAR     = 0x1C,

    /** 0x1D: LOAD    vX+YYYY,vZ
       Loads, i.e. moves the contents from the source variable
       vX, adds YYYY afterward to it and stores the final result
       into vZ and sets the condition variable accordingly.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_LOAD       = 0x1D,

    /** Arithmetic instructions:  */
    /** 0x1E: ADD     vX+YYYY,vZ
       Adds the contents from vX+YYYY to vZ and stores the final
       result into vZ and sets the condition variable accordingly.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ADD        = 0x1E,

    /** 0x1F: ADDX    vX+YYYY,vZ
       Adds the contents from vX+YYYY (if extend flag is cleared)
       or vX+YYYY+1 (if extend flag is set) to vZ and stores the final
       result into vZ and sets the condition variable accordingly.
       Please note that the zero flag is only cleared if the result
       is non-zero, it is not touched otherwise.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ADDX       = 0x1F,

    /** 0x20: SUB     vX+YYYY,vZ
       Subtracts the contents from vX+YYYY from vZ and stores the
       final result into vZ and sets the condition variable
       accordingly.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SUB        = 0x20,

    /** 0x21: SUBX    vX+YYYY,vZ
       Subtracts the contents from vX+YYYY (if extend flag is cleared)
       or vX+YYYY+1 (if extend flag is set) from vZ and stores the
       final result into vZ and sets the condition variable
       accordingly. Please note that the zero flag is only cleared if
       the result is non-zero, it is not touched otherwise.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SUBX       = 0x21,

    /** 0x22: CMP     vX+YYYY,vZ
       Subtracts the contents from vX+YYYY from vZ and sets the
       condition variable accordingly. This does effectively a
       comparision of two values.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_CMP        = 0x22,

    /** 0x23: MULU    vX+YYYY,vZ
       Multiplies the contents from vX+YYYY with vZ by threating
       both values as unsigned integers and discards the upper 16 bits
       of the result. The lower 16 bits are stored into vZ and finally
       sets the condition variable accordingly. Please note that the
       extend flag is never affected and the carry flag will always be
       cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_MULU       = 0x23,

    /** 0x24: MULS    vX+YYYY,vZ
       Multiplies the contents from vX+YYYY with vZ by threating
       both values as signed integers and discards the upper 16 bits
       of the result. The lower 16 bits are stored into vZ and finally
       sets the condition variable accordingly. Please note that the
       extend flag is never affected and the carry flag will always be
       cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_MULS       = 0x24,

    /** 0x25: DMULU   vX+YYYY,[vH:]vL
       Multiplies the contents from vX+YYYY with vL by threating both
       values as unsigned integers and stores the upper 16 bits of the
       result into vH and the lower 16 bits into vL. vH is always vL
       decremented by one which also means that if vL is 15, then vH
       will be ignored and only sets the lower 16 bits into vL and then
       sets the condition variable accordingly. Please note that the
       extend flag is never affected and the carry flag will always be
       cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_DMULU      = 0x25,

    /** 0x26: DMULS   vX+YYYY,[vH:]vL
       Multiplies the contents from vX+YYYY with vL by threating both
       values as signed integers and stores the upper 16 bits of the
       result into vH and the lower 16 bits into vL. vH is always vL
       decremented by one which also means that if vL is 15, then vH
       will be ignored and only sets the lower 16 bits into vL and then
       sets the condition variable accordingly. Please note that the
       extend flag is never affected and the carry flag will always be
       cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_DMULS      = 0x26,

    /** 0x27: DIVU    vX+YYYY,vZ
       Divides the contents from vX+YYYY by vZ by threating both
       values as unsigned integers and stores the quotient into
       vZ and then sets the condition variable accordingly. If a
       division by zero occurs, the instruction is ignored and all
       flags are set except the extend flag otherwise note that 
       the extend flag is never affected and the carry flag will
       always be cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_DIVU       = 0x27,

    /** 0x28: DIVS    vX+YYYY,vZ
       Divides the contents from vX+YYYY by vZ by threating both
       values as signed integers and stores the quotient into
       vZ and then sets the condition variable accordingly. If a
       division by zero occurs, the instruction is ignored and all
       flags are set except the extend flag otherwise note that 
       the extend flag is never affected and the carry flag will
       always be cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_DIVS       = 0x28,

    /** 0x29: MODU    vX+YYYY,vZ
       Divides the contents from vX+YYYY by vZ by threating both
       values as unsigned integers and stores the remainder into
       vZ and then sets the condition variable accordingly. If a
       division by zero occurs, the instruction is ignored and all
       flags are set except the extend flag otherwise note that 
       the extend flag is never affected and the carry flag will
       always be cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_MODU       = 0x29,

    /** 0x2A: MODS    vX+YYYY,vZ
       Divides the contents from vX+YYYY by vZ by threating both
       values as signed integers and stores the remainder into
       vZ and then sets the condition variable accordingly. If a
       division by zero occurs, the instruction is ignored and all
       flags are set except the extend flag otherwise note that 
       the extend flag is never affected and the carry flag will
       always be cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_MODS       = 0x2A,

    /** 0x2B: DDIVU   vX+YYYY,[vH:]vL
       Divides the contents from vX+YYYY by the 32-bit integer value
       represented by vH * 0x10000 + vL by threating both values as
       unsigned integers and stores the quotient in vL and the
       remainder in vH. Since vH is always vL decremented by one which
       also means that if vL is 15, then vL will be threated as the
       upper 16 bits of the dividend and vH is completely ignored
       and only stores the quotient of the result into vL and then
       sets the condition variable accordingly. If a division by zero
       occurs, the instruction is ignored and all flags are set except
       the extend flag otherwise note that the extend flag is never
       affected and the carry flag will always be cleared. If the
       quotient does not fit into unsigned 16-bit range, vH and vL are
       unchanged and only the overflow flag is set.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_DDIVU      = 0x2B,

    /** 0x2C: DDIVS   vX+YYYY,[vH:]vL
       Divides the contents from vX+YYYY by the 32-bit integer value
       represented by vH * 0x10000 + vL by threating both values as
       signed integers and stores the quotient in vL and the remainder
       in vH. Since vH is always vL decremented by one which also
       means that if vL is 15, then vL will be threated as the
       upper 16 bits of the dividend and vH is completely ignored
       and only stores the quotient of the result into vL and then
       sets the condition variable accordingly. If a division by zero
       occurs, the instruction is ignored and all flags are set except
       the extend flag otherwise note that the extend flag is never
       affected and the carry flag will always be cleared. If the
       quotient does not fit into signed 16-bit range, vH and vL are
       unchanged and only the overflow flag is set.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_DDIVS      = 0x2C,

    /** 0x2D: ASHL    vX+YYYY,vZ
       Arithmetically left shifts the destination variable specified
       by vZ by a number of bits specified with (vX + YYYY) & 0x003F
       by threatening both values as signed integers and finally sets
       the condition variable according to the following table:
       X | Set according to the last bit shifted out of the operand.
         | Not affected when the shift count is zero.
       Z | Set if the result is zero.
       N | Set if the result is negative.
       V | Set if the sign bit changes at any time during operation.
       C | Like X, but always cleared when the shift count is 0.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ASHL       = 0x2D,

    /** 0x2E: ASHR    vX+YYYY,vZ
       Arithmetically right shifts the destination variable specified
       by vZ by a number of bits specified with (vX + YYYY) & 0x003F
       by threatening both values as signed integers and finally sets
       the condition variable according to the following table:
       X | Set according to the last bit shifted out of the operand.
         | Not affected when the shift count is zero.
       Z | Set if the result is zero.
       N | Set if the result is negative.
       V | Set if the sign bit changes at any time during operation.
       C | Like X, but always cleared when the shift count is 0.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ASHR       = 0x2E,

    /** 0x2F: LSHL    vX+YYYY,vZ
       Logically left shifts the destination variable specified
       by vZ by a number of bits specified with (vX + YYYY) & 0x003F
       by threatening both values as unsigned integers and finally
       sets the condition variable according to the following table:
       X | Set according to the last bit shifted out.
       Z | Set if the result is zero.
       N | Set if the result is negative.
       V | Always cleared.
       C | Like X, but always cleared when the shift count is 0.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_LSHL       = 0x2F,

    /** 0x30: LSHR    vX+YYYY,vZ
       Logically right shifts the destination variable specified
       by vZ by a number of bits specified with (vX + YYYY) & 0x003F
       by threatening both values as unsigned integers and finally
       sets the condition variable according to the following table:
       X | Set according to the last bit shifted out.
       Z | Set if the result is zero.
       N | Set if the result is negative.
       V | Always cleared.
       C | Like X, but always cleared when the shift count is 0.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_LSHR       = 0x30,

    /** 0x31: ROL     vX+YYYY,vZ
       Left rotates the destination variable specified by vZ by a
       number of bits specified with (vX + YYYY) & 0x003F and finally
       sets the condition variable according to the following table:
       X | Unaffected.
       Z | Set if the result is zero.
       N | Set if the result is negative.
       V | Always cleared.
       C | Set according to the last bit shifted out, always cleared
           for a shift count of zero.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ROL        = 0x31,

    /** 0x32: ROR     vX+YYYY,vZ
       Right rotates the destination variable specified by vZ by a
       number of bits specified with (vX + YYYY) & 0x003F and finally
       sets the condition variable according to the following table:
       X | Unaffected.
       Z | Set if the result is zero.
       N | Set if the result is negative.
       V | Always cleared.
       C | Set according to the last bit shifted out, always cleared
           for a shift count of zero.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ROR        = 0x32,

    /** 0x33: ROLX    vX+YYYY,vZ
       Left rotates the destination variable specified by vZ by a
       number of bits specified with (vX + YYYY) & 0x003F by using the
       extend flag to determine what bit to rotate in from the right
       side and finally sets the condition variable according to the
       following table:
       X | Set according to the last bit shifted out of the operand.
       Z | Set if the result is zero.
       N | Set if the result is negative.
       V | Always cleared.
       C | Set according to the last bit shifted out, always cleared
           for a shift count of zero.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ROLX       = 0x33,

    /** 0x34: RORX    vX+YYYY,vZ
       Right rotates the destination variable specified by vZ by a
       number of bits specified with (vX + YYYY) & 0x003F by using the
       extend flag to determine what bit to rotate in from the left
       side and finally sets the condition variable according to the
       following table:
       X | Set according to the last bit shifted out of the operand.
       Z | Set if the result is zero.
       N | Set if the result is negative.
       V | Always cleared.
       C | Set according to the last bit shifted out, always cleared
           for a shift count of zero.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_RORX       = 0x34,

    /** 0x35: OR      vX+YYYY,vZ
       Combines the contents from vX+YYYY to vZ by applying a logical
       OR operator and stores the final result into vZ and sets the
       condition variable accordingly. Please note that the extend
       flag is never affected and that the overflow and carry flags
       are always cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_OR         = 0x35,

    /** 0x36: AND     vX+YYYY,vZ
       Combines the contents from vX+YYYY to vZ by applying a logical
       AND operator and stores the final result into vZ and sets the
       condition variable accordingly. Please note that the extend
       flag is never affected and that the overflow and carry flags
       are always cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_AND        = 0x36,

    /** 0x37: XOR     vX+YYYY,vZ
       Combines the contents from vX+YYYY to vZ by applying a logical
       exclusive OR operator and stores the final result into vZ and
       sets the condition variable accordingly. Please note that the
       extend flag is never affected and that the overflow and carry
       flags are always cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_XOR        = 0x37,

    /** 0x38: NOT     vX+YYYY,vZ
       Inverts the destination variable specified by vZ then adds
       vX+YYYY to the final result which is stored in vZ and sets the
       condition variable accordingly. Please note that the extend
       flag is never affected and that the overflow and carry flags
       are always cleared.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_NOT        = 0x38,

    /** 0x39: NEG     vX+YYYY,vZ
       Negates the destination variable specified by vZ by subtracting
       it from zero, then adds the contents from vX+YYYY to the final
       result which is then stored into vZ and sets the condition
       variable accordingly.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_NEG        = 0x39,

    /** 0x3A: NEGX    vX+YYYY,vZ
       Negates the destination variable specified by vZ by subtracting
       it from either one (if the extend flag is set) or from zero (if
       the extend flag is cleared), then adds the contents from
       vX+YYYY to the final result which is then stored into vZ and
       sets the condition variable accordingly.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_NEGX       = 0x3A,

    /** 0x3B: EXTB    vX+YYYY,vZ
       Copies bit 7 of the value referenced by vZ to bits 8-15, then
       continues adding the contents from vX+YYYY to the final result
       which is finally stored in vZ and sets the condition variable
       accordingly.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_EXTB       = 0x3B,

    /** 0x3C: EXT     vX+YYYY,[vH:]vL
       Copies bit 15 of vL to bits 0-15 of vH, i.e. vH * 0x10000 + vL
       is threatened as a signed 32-bit value which is sign extended
       from a signed 16-bit value. Since vH is always vL decremented
       by one which also means that if vL is 15, then vL will simply
       filled out with zeroes, then adds vX+YYYY to the final result
       which is finally stored in vZ and sets the condition variables
       according to if the final result would be tested against zero
       as a 32-bit variable.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_EXT        = 0x3C,

    /** 0x3D: XCHG    vX+YYYY,vZ
       Exchanges the contents of the two variables vX and vZ, then
       adds YYYY to the final results which is finally stored in vZ
       and sets the condition variables according to if the final
       result would be tested against zero as a 32-bit variable
       calculated by the formula: vZ * 0x10000 + vX+YYYY.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_XCHG       = 0x3D,

    /** 0x3E: SWAP    vX+YYYY,vZ
       Swaps the upper byte (bits 8-15) of the value represented by
       vZ with the lower byte (bits 0-7) of vZ. The condition
       variable is set as if vZ would be compared against zero.
       After setting the flags, vX+YYYY is added to the final result
       of vZ.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SWAP       = 0x3E,

    /** Sound instructions:  */
    /** 0x3F: GETWAVE vX+YYYY,vZ
       Gets the current sample waveform number and adds vX+YYYY to the
       obtained sample waveform number, stores the final result in vZ
       and the condition variable remains completely unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETWAVE    = 0x3F,

    /** 0x40: GETWLEN vX+YYYY,vZ
       Gets the current sample waveform length in samples and adds
       vX+YYYY to the obtained sample waveform length, stores the
       final result in vZ and the condition variable remains
       completely unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETWLEN    = 0x40,

    /** 0x41: GETWPOS vX+YYYY,vZ
       Gets the current sample waveform position in samples and adds
       vX+YYYY to the obtained sample waveform position, stores the
       final result in vZ and the condition variable remains
       completely unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETWPOS    = 0x41,

    /** 0x42: GETCHAN vX+YYYY,vZ
       Gets the current host channel number and adds vX+YYYY to the
       obtained host channel number, stores the final result in vZ
       and the condition variable remains completely unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETCHAN    = 0x42,

    /** 0x43: GETNOTE vX+YYYY,vZ
       Gets the current note playing and adds vX+YYYY to the obtained
       current octave playing * 12 + current note playing where C- is
       considered as zero, C# as one, D- as two, stores the final
       result in vZ and the condition variable remains completely
       unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETNOTE    = 0x43,

    /** 0x44: GETRANS vX+YYYY,vZ
       Gets the current note playing and adds vX+YYYY to the obtained
       current octave playing * 12 + current note playing + current
       transpose value, i.e. the final note being played where C- is
       considered as zero, C# as one, D- as two, stores the final
       result in vZ and the condition variable remains completely
       unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETRANS    = 0x44,

    /** 0x45: GETPTCH vX+YYYY,[vH:]vL
       Gets the current sample frequency in Hz then adds vX+YYYY to
       the final result which contains,the upper 16 bits of frequency
       in vH and the lower 16 bits of frequency in vL. Since vH is
       always vL decremented by one which also means that if vL is 15,
       then vL will simply filled with the lower 16 bits of the sample
       frequency and the condition variable remains completely
       unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETPTCH    = 0x45,

    /** 0x46: GETPER  vX+YYYY,[vH:]vL
       Gets the current sample frequency in Hz and converts it Amiga
       Paula sound chip period value then adds vX+YYYY to the final
       result which contains,the upper 16 bits of the Amiga period
       value in vH and the lower 16 bits in vL. Since vH is always
       vL decremented by one which also means that if vL is 15,
       then vL will simply filled with the lower 16 bits of the period
       and the condition variable remains completely unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETPER     = 0x46,

    /** 0x47: GETFX   vX+YYYY,vZ
       Stores the value of effect number specified by the upper 8 bits
       of vX+YYYY into the destination variable referenced by vZ which
       usually is the last command data word passed to it, e.g. if
       vX+YYYY is within 0x2000 and 0x20FF the current volume set with
       the command byte 0x20 would be returned. The result is always
       directly usable with the instructions having a negative value,
       i.e. the SETFXxx instruction series while the value of the
       condition variable is completely untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETFX      = 0x47,

    /** 0x48: GETARPW vX+YYYY,vZ
       Gets the current arpeggio waveform number and adds vX+YYYY to
       the obtained arpeggio waveform number, stores the final result
       in vZ and the condition variable is completely untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETARPW    = 0x48,

    /** 0x49: GETARPV vX+YYYY,vZ
       Gets the current arpeggio waveform data value and adds vX+YYYY
       to the obtained arpeggio waveform data value, stores the final
       result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETARPV    = 0x49,

    /** 0x4A: GETARPL vX+YYYY,vZ
       Gets the current arpeggio waveform length in ticks and adds
       vX+YYYY to the obtained arpeggio waveform length, stores the
       final result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETARPL    = 0x4A,

    /** 0x4B: GETARPP vX+YYYY,vZ
       Gets the current arpeggio waveform position and adds vX+YYYY
       to the obtained arpeggio waveform position, stores the final
       result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETARPP    = 0x4B,

    /** 0x4C: GETVIBW vX+YYYY,vZ
       Gets the current vibrato waveform number and adds vX+YYYY to
       the obtained vibrato waveform number, stores the final result
       in vZ and the condition variable is completely untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETVIBW    = 0x4C,

    /** 0x4D: GETVIBV vX+YYYY,vZ
       Gets the current vibrato waveform data value and adds vX+YYYY
       to the obtained vibrato waveform data value, stores the final
       result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETVIBV    = 0x4D,

    /** 0x4E: GETVIBL vX+YYYY,vZ
       Gets the current vibrato waveform length in ticks and adds
       vX+YYYY to the vibrato waveform length, stores the
       final result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETVIBL    = 0x4E,

    /** 0x4F: GETVIBP vX+YYYY,vZ
       Gets the current vibrato waveform position and adds vX+YYYY
       to the obtained vibrato waveform position, stores the final
       result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETVIBP    = 0x4F,

    /** 0x50: GETTRMW vX+YYYY,vZ
       Gets the current tremolo waveform number and adds vX+YYYY to
       the obtained tremolo waveform number, stores the final result
       in vZ and the condition variable is completely untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETTRMW    = 0x50,

    /** 0x51: GETTRMV vX+YYYY,vZ
       Gets the current tremolo waveform data value and adds vX+YYYY
       to the obtained tremolo waveform data value, stores the final
       result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETTRMV    = 0x51,

    /** 0x52: GETTRML vX+YYYY,vZ
       Gets the current tremolo waveform length in ticks and adds
       vX+YYYY to the tremolo waveform length, stores the
       final result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETTRML    = 0x52,

    /** 0x53: GETTRMP vX+YYYY,vZ
       Gets the current tremolo waveform position and adds vX+YYYY
       to the obtained tremolo waveform position, stores the final
       result in vZ and the condition variable is completely
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETTRMP    = 0x53,

    /** 0x54: GETPANW vX+YYYY,vZ
       Gets the current pannolo / panbrello waveform number and adds
       vX+YYYY to the obtained pannolo / panbrello waveform number,
       stores the final result in vZ and the condition variable is
       completely untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETPANW    = 0x54,

    /** 0x55: GETPANV vX+YYYY,vZ
       Gets the current pannolo / panbrello waveform data value and
       adds vX+YYYY to the obtained pannolo / panbrello waveform data
       value, stores the final result in vZ and the condition variable
       is completely untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETPANV    = 0x55,

    /** 0x56: GETPANL vX+YYYY,vZ
       Gets the current pannolo / panbrello waveform length in ticks
       and adds vX+YYYY to the pannolo / panbrello waveform length,
       stores the final result in vZ and the condition variable is
       completely untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETPANL    = 0x56,

    /** 0x57: GETPANP vX+YYYY,vZ
       Gets the current pannolo / panbrello waveform position and
       adds vX+YYYY to the obtained pannolo / panbrello waveform
       position, stores the final result in vZ and the condition
       variable is completely untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETPANP    = 0x57,

    /** 0x58: GETRND  vX+YYYY,vZ
       Gets a random value in the closed interval of zero and
       vX+YYYY and stores the final result in vZ while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETRND     = 0x58,

    /** 0x59: GETSINE vX+YYYY,vZ
       Gets the sine value by considering vX+YYYY as a 16-bit
       signed value which represents the degrees to calculate
       the sine value from and the final result which ranges always
       between -32767 and +32767 (use DMULS and DDIVS to scale to
       required value) is stored in vZ while the condition variable
       will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_GETSINE    = 0x59,

    /** 0x5A: PORTAUP vX+YYYY
       Slides the pitch up with the portamento value specified by
       vX+YYYY (the difference between this and the generic command
       is, that this instruction has its own memory and so does not
       interfere with the pattern data while the condition variable
       will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PORTAUP    = 0x5A,

    /** 0x5B: PORTADN vX+YYYY
       Slides the pitch down with the portamento value specified by
       vX+YYYY (the difference between this and the generic command
       is, that this instruction has its own memory and so does not
       interfere with the pattern data while the condition variable
       will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PORTADN    = 0x5B,

    /** 0x5C: VIBSPD  vX+YYYY
       Sets the vibrato speed specified by vX+YYYY while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_VIBSPD     = 0x5C,

    /** 0x5D: VIBDPTH vX+YYYY
       Sets the vibrato depth specified by vX+YYYY while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_VIBDPTH    = 0x5D,

    /** 0x5E: VIBWAVE vX+YYYY
       Sets the vibrato waveform specified by vX+YYYY while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_VIBWAVE    = 0x5E,

    /** 0x5F: VIBWAVP vX+YYYY
       Sets the vibrato waveform position specified by vX+YYYY while
       the condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_VIBWAVP    = 0x5F,

    /** 0x60: VIBRATO vX+YYYY
       Executes the vibrato. The upper 8 bits of vX+YYYY contains the
       vibrato speed or 0 to use the previous value and the lower 8
       bits represents the vibrato depth or 0 to use the previous one.
       Please note that vibrato depth is considered as a signed value
       while vibrato speed is unsigned and that the condition variable
       remains completely unchanged.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_VIBRATO    = 0x60,

    /** 0x61: VIBVAL  vX+YYYY
       Executes the vibrato with the Amiga period specified by vX+YYYY
       without changing the the condition variable.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_VIBVAL     = 0x61,

    /** 0x62: ARPSPD  vX+YYYY
       Sets the arpeggio speed specified by vX+YYYY while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ARPSPD     = 0x62,

    /** 0x63: ARPWAVE vX+YYYY
       Sets the arpeggio waveform specified by vX+YYYY while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ARPWAVE    = 0x63,

    /** 0x64: ARPWAVP vX+YYYY
       Sets the arpeggio waveform position specified by vX+YYYY while
       the condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ARPWAVP    = 0x64,

    /** 0x65: ARPEGIO vX+YYYY
       Executes the arpeggio. The upper 8 bits of vX+YYYY contains the
       unsigned arpeggio speed or 0 to use the previous value and the
       lower 8 bits represents the signed finetuning value. or 0 to
       use the previous one. The condition variable will remain
       unchanged.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ARPEGIO    = 0x65,

    /** 0x66: ARPVAL  vX+YYYY
       Executes the arpeggio. The upper 8 bits of vX+YYYY contains the
       signed transpose value to be used as arpeggio or 0 to use the
       previous one and the lower 8 bits represents the signed
       finetuning value. or 0 to use the previous one. The condition
       variable will remain unchanged.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ARPVAL     = 0x66,

    /** 0x67: SETWAVE vX+YYYY
       Sets the sample waveform number specified by vX+YYYY when
       the current sample playing either arrives at end of sample or
       reaches a loop end marker or if no sample is being played it
       will be started immediately. The condition variable will be
       completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SETWAVE    = 0x67,

    /** 0x68: ISETWAV vX+YYYY
       Sets the sample waveform number specified by vX+YYYY either by
       immediately breaking the current sample playing or simply
       start the new one if no sample being played The condition
       variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_ISETWAV    = 0x68,

    /** 0x69: SETWAVP vX+YYYY
       Sets the sample waveform position in samples specified by
       vX+YYYY while the condition variable remains unchanged.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SETWAVP    = 0x69,

    /** 0x6A: SETRANS vX+YYYY
       Replaces the sample transpose value specified by vX+YYYY and
       interpreted as a signed 16-bit value with the transpose value
       of current sample while preserving all flags of the condition
       variable.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SETRANS    = 0x6A,

    /** 0x6B: SETNOTE vX+YYYY
       Sets a new sample frequency by using the transpose value
       specified by vX+YYYY without replacing the old transpose value
       interpreted as a signed 16-bit value while preserving all flags
       of the condition variable.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SETNOTE    = 0x6B,

    /** 0x6C: SETPTCH vX+YYYY,[vH:]vL
       Sets the current sample frequency in Hz by adding vX+YYYY to
       vH * 0x10000 + vL which contains,the upper 16 bits of frequency
       in vH and the lower 16 bits of frequency in vL and take that
       final result as the new sample frequency rate. Since vH is
       always vL decremented by one which also means that if vL is 15,
       then just vL will be considered as the lower 16 bits of the
       new sampling rate to be set. The condition variable remains
       completely unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SETPTCH    = 0x6C,

    /** 0x6D: SETPER  vX+YYYY,[vH:]vL
       Sets the current sample frequency in Hz by converting first
       the Amiga Paula sound chip period value gathered by adding
       first vX+YYYY to vH * 0x10000 + vL which contains,the upper 16
       bits of period in vH and the lower 16 bits of Amiga period in
       vL to hertzian and sets the new sample frequency rate to this
       converted value. Since vH is always vL decremented by one which
       also means that if vL is 15, then just vL will be considered as
       the lower 16 bits of the new period to be set. The condition
       variable remains completely unaffected.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_SETPER     = 0x6D,

    /** 0x6E: RESET   vX+YYYY
       Resets the vibrato / tremolo / pannolo (panbrello) counters
       depending on the mask obtained by vX+YYYY which is defined
       by the following table (all condition variable flags are
       preserved):
           Mask | Meanings
         0x0001 | Disables arpeggio envelope reset.
         0x0002 | Disables vibrato envelope reset.
         0x0004 | Disables tremolo envelope reset.
         0x0008 | Disables pannolo envelope reset.
         0x0010 | Disables redo of portamento.
         0x0020 | Disables portamento reset.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_RESET      = 0x6E,

    /** 0x6F: VOLSLUP vX+YYYY
       Slides the volume up with the volume level specified by
       vX+YYYY (the difference between this and the generic command
       is, that this instruction has its own memory and so does not
       interfere with the pattern data while the condition variable
       will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_VOLSLUP    = 0x6F,

    /** 0x70: VOLSLDN vX+YYYY
       Slides the volume down with the volume level specified by
       vX+YYYY (the difference between this and the generic command
       is, that this instruction has its own memory and so does not
       interfere with the pattern data while the condition variable
       will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_VOLSLDN    = 0x70,

    /** 0x71: TRMSPD  vX+YYYY
       Sets the tremolo speed specified by vX+YYYY while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_TRMSPD     = 0x71,

    /** 0x72: TRMDPTH vX+YYYY
       Sets the tremolo depth specified by vX+YYYY while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_TRMDPTH    = 0x72,

    /** 0x73: TRMWAVE vX+YYYY
       Sets the tremolo waveform specified by vX+YYYY while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_TRMWAVE    = 0x73,

    /** 0x74: TRMWAVP vX+YYYY
       Sets the tremolo waveform position specified by vX+YYYY while
       the condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_TRMWAVP    = 0x74,

    /** 0x75: TREMOLO vX+YYYY
       Executes the tremolo. The upper 8 bits of vX+YYYY contains the
       tremolo speed or 0 to use the previous value and the lower 8
       bits represents the tremolo depth or 0 to use the previous one.
       Please note that tremolo depth is considered as a signed value
       while tremolo speed is unsigned and that the condition variable
       remains completely unchanged.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_TREMOLO    = 0x75,

    /** 0x76: TRMVAL  vX+YYYY
       Executes the tremolo with a absolute volume obtained from
       vX+YYYY or 0 to use the previous one. The condition variable
       will remain unchanged.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_TRMVAL     = 0x76,

    /** 0x77: PANLEFT vX+YYYY
       Slides the panning position to left stereo with the panning
       level specified by vX+YYYY (the difference between this and the
       generic command is, that this instruction has its own memory
       and so does not interfere with the pattern data while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PANLEFT    = 0x77,

    /** 0x78: PANRIGHT vX+YYYY
       Slides the panning position to right stereo with the panning
       level specified by vX+YYYY (the difference between this and the
       generic command is, that this instruction has its own memory
       and so does not interfere with the pattern data while the
       condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PANRGHT    = 0x78,

    /** 0x79: PANSPD  vX+YYYY
       Sets the pannolo (panbrello) speed specified by vX+YYYY while
       the condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PANSPD     = 0x79,

    /** 0x7A: PANDPTH vX+YYYY
       Sets the pannolo (panbrello) depth specified by vX+YYYY while
       the condition variable will be completely kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PANDPTH    = 0x7A,

    /** 0x7B: PANWAVE vX+YYYY
       Sets the pannolo (panbrello) waveform specified by vX+YYYY
       while the condition variable will be completely kept
       untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PANWAVE    = 0x7B,

    /** 0x7C: PANWAVP vX+YYYY
       Sets the pannolo (panbrello) waveform position specified by
       vX+YYYY while the condition variable will be completely
       kept untouched.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PANWAVP    = 0x7C,

    /** 0x7D: PANNOLO vX+YYYY
       Executes the pannolo (panbrello). The upper 8 bits of vX+YYYY
       contains the pannolo speed or 0 to use the previous value and
       the lower 8 bits represents the pannolo depth or 0 to use the
       previous one. Please note that pannolo depth is considered as
       a signed value while pannolo speed is unsigned and that the
       condition variable remains completely unchanged.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PANNOLO    = 0x7D,

    /** 0x7E: PANVAL  vX+YYYY
       Executes the pannolo (panbrello) with a absolute panning
       obtained from vX+YYYY or 0 to use the previous one. The
       condition variable will remain unchanged.  */
    AVSEQ_SYNTH_CODE_INSTRUCTION_PANVAL     = 0x7E,
    };

    /** Source and destinaton variable. These are actually 2 nibbles.
       The upper nibble (bits 4-7) is the source variable where the
       instruction reads the data from. Lower nibble (bits 0-3) is
       the destination variable where the instruction stores the
       result.  */
    uint8_t src_dst_var;

    /** Instruction data. Depending on instruction, this value will
       be added, subtracted, moved, multiplied, shifted, used as
       volume envelope, etc. It's a 16-bit data value to be used
       as an immediate increment value.
       Please note that the effects receive this value and the
       variable value of the source (for all except NOT and SWAP
       synth instructions).  */
    uint16_t data;
} AVSequencerSynthCode;

/**
 * Synth sound code symbol table. It has the same purpose as a
 * linker symbol table: replacing values by symbols.
 * This enhances the readability of complex synth sound code.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerSynthSymbolTable {
    /** Name of symbol (this is the string which will be displayed
       as integer replacement and can be used for declaring either
       labels or symbolic integer value references.  */
    uint8_t *symbol_name;

    /** Symbol value. This refers to the 16-bit integer value
       this symbol replaces.  */
    uint16_t symbol_value;

    /** First line number (instruction number) for which this
       symbol has validity.  */
    uint16_t line_min;

    /** Last line number (instruction number) for which this
       symbol has validity.  */
    uint16_t line_max;

    /** Type of symbol. This declares if this symbol applies to
       immediate values, source or destination variable or is
       just referencing a label.  */
    int8_t type;
    enum AVSequencerSynthSymbolTableType {
    AVSEQ_SYNTH_SYMBOL_TABLE_TYPE_PARAM     = 0x00, ///< Symbol is an ordinary instruction parameter constant value. symbol_value points to the constant to be referenced
    AVSEQ_SYNTH_SYMBOL_TABLE_TYPE_VAR_SRC   = 0x01, ///< Symbol is a source variable reference. symbol_value points to variable number to be referenced (0 - 15)
    AVSEQ_SYNTH_SYMBOL_TABLE_TYPE_VAR_DEST  = 0x02, ///< Symbol is a destination variable reference. symbol_value points to variable number to be referenced (0 - 15)
    AVSEQ_SYNTH_SYMBOL_TABLE_TYPE_VAR_BOTH  = 0x03, ///< Symbol is a source and destination variable reference. symbol_value points to single value variable number to be referenced (0 - 15)
    AVSEQ_SYNTH_SYMBOL_TABEL_TYPE_LABEL     = 0x04, ///< Symbol is a label reference pointing to a target line. symbol_value points to the line number (instruction number) to be referenced.
    };

    /** Special symbol flags for this symbol. These flags contains
       stuff like if the symbol is currently disabled or enabled so
       it can be turned off without deleting it.  */
    uint8_t flags;
    enum AVSequencerSynthSymbolTableFlags {
    AVSEQ_SYNTH_SYMBOL_TABLE_FLAGS_UNUSED   = 0x80, ///< Symbol is currently disabled, i.e. not evaluated
    };
} AVSequencerSynthSymbolTable;

/**
 * Synth sound structure used by all samples which are
 * either are declared as synths or hybrids.
 * New fields can be added to the end with minor version bumps.
 * Removal, reordering and changes to existing fields require a major
 * version bump.
 */
typedef struct AVSequencerSynth {
    /**
     * information on struct for av_log
     * - set by avseq_alloc_context
     */
    const AVClass *av_class;

    /** Metadata information: Original synth file name, synth name,
     *  artist and comment.  */
    AVMetadata *metadata;

    /** Array (of size waveforms) of pointers containing attached
       waveform used by this synth sound.  */
    AVSequencerSynthWave **waveform_list;

    /** Number of waveforms. Can be 0 if this is a hybrid, the normal
       sample data is used in that case. Default is one waveform.  */
    uint16_t waveforms;

    /** Array (of size symbols) of pointers containing named symbols
       used by this synth sound code.  */
    AVSequencerSynthSymbolTable **symbol_list;

    /** Number of named symbols used by this synth sound code.  */
    uint16_t symbols;

    /** AVSequencerSynthCode pointer to synth sound code structure.  */
    AVSequencerSynthCode *code;

    /** Number of instructions (lines) in the synth sound execution
       code (defaults to one line).  */
    uint16_t size;

    /** Entry position (line number) of volume [0], panning [1], slide
       [2] and special [3] handling code.  */
    uint16_t entry_pos[4];

    /** Sustain entry position (line number) of volume [0], panning
       [1], slide [2] and special [3] handling code. This will
       position jump the code to the target line number of a key off
       note is pressed.  */
    uint16_t sustain_pos[4];

    /** Entry position (line number) of volume [0], panning [1], slide
       [2] and special [3] handling code when NNA has been triggered.
       This allows a complete custom new note action to be
       defined.  */
    uint16_t nna_pos[4];

    /** Entry position (line number) of volume [0], panning [1], slide
       [2] and special [3] handling code when DNA has been triggered.
       This allows a complete custom duplicate note action to be
       defined.  */
    uint16_t dna_pos[4];

    /** Contents of the 16 variable registers (v0-v15).  */
    uint16_t variable[16];

    /** Initial status of volume [0], panning [1], slide [2] and
       special [3] variable condition status register.  */
    uint16_t cond_var[4];
    enum AVSequencerSynthCondVar {
    AVSEQ_SYNTH_COND_VAR_CARRY      = 0x01, ///< Carry (C) bit for condition variable
    AVSEQ_SYNTH_COND_VAR_OVERFLOW   = 0x02, ///< Overflow (V) bit for condition variable
    AVSEQ_SYNTH_COND_VAR_ZERO       = 0x04, ///< Zero (Z) bit for condition variable
    AVSEQ_SYNTH_COND_VAR_NEGATIVE   = 0x08, ///< Negative (N) bit for condition variable
    AVSEQ_SYNTH_COND_VAR_EXTEND     = 0x10, ///< Extend (X) bit for condition variable
    };

    /** Use NNA trigger entry fields. This will run custom synth sound
       code execution on a new note action trigger.  */
    uint8_t use_nna_flags;
    enum AVSequencerSynthUseNNAFlags {
    AVSEQ_SYNTH_USE_NNA_FLAGS_VOLUME_NNA    = 0x01, ///< Use NNA trigger entry field for volume
    AVSEQ_SYNTH_USE_NNA_FLAGS_PANNING_NNA   = 0x02, ///< Use NNA trigger entry field for panning
    AVSEQ_SYNTH_USE_NNA_FLAGS_SLIDE_NNA     = 0x04, ///< Use NNA trigger entry field for slide
    AVSEQ_SYNTH_USE_NNA_FLAGS_SPECIAL_NNA   = 0x08, ///< Use NNA trigger entry field for special
    AVSEQ_SYNTH_USE_NNA_FLAGS_VOLUME_DNA    = 0x10, ///< Use NNA trigger entry field for volume
    AVSEQ_SYNTH_USE_NNA_FLAGS_PANNING_DNA   = 0x20, ///< Use NNA trigger entry field for panning
    AVSEQ_SYNTH_USE_NNA_FLAGS_SLIDE_DNA     = 0x40, ///< Use NNA trigger entry field for slide
    AVSEQ_SYNTH_USE_NNA_FLAGS_SPECIAL_DNA   = 0x80, ///< Use NNA trigger entry field for special
    };

    /** Use sustain entry position fields. This will run custom synth
       sound code execution on a note off trigger.  */
    uint8_t use_sustain_flags;
    enum AVSequencerSynthUseSustainFlags {
    AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_VOLUME        = 0x01, ///< Use sustain entry position field for volume
    AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_PANNING       = 0x02, ///< Use sustain entry position field for panning
    AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_SLIDE         = 0x04, ///< Use sustain entry position field for slide
    AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_SPECIAL       = 0x08, ///< Use sustain entry position field for special
    AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_VOLUME_KEEP   = 0x10, ///< Keep sustain entry position for volume
    AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_PANNING_KEEP  = 0x20, ///< Keep sustain entry position for panning
    AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_SLIDE_KEEP    = 0x40, ///< Keep sustain entry position for slide
    AVSEQ_SYNTH_USE_SUSTAIN_FLAGS_SPECIAL_KEEP  = 0x80, ///< Keep sustain entry position for special
    };

    /** Position keep mask. All initial entry positions will be taken
       from the previous instrument if the approciate bit is set.  */
    int8_t pos_keep_mask;
    enum AVSequencerSynthPosKeepMask {
    AVSEQ_SYNTH_POS_KEEP_MASK_VOLUME    = 0x01, ///< Keep entry position for volume
    AVSEQ_SYNTH_POS_KEEP_MASK_PANNING   = 0x02, ///< Keep entry position for panning
    AVSEQ_SYNTH_POS_KEEP_MASK_SLIDE     = 0x04, ///< Keep entry position for slide
    AVSEQ_SYNTH_POS_KEEP_MASK_SPECIAL   = 0x08, ///< Keep entry position for special
    AVSEQ_SYNTH_POS_KEEP_MASK_WAVEFORMS = 0x40, ///< Keep the synthetic waveforms
    AVSEQ_SYNTH_POS_KEEP_MASK_CODE      = 0x80, ///< Keep the synthetic programming code
    };

    /** NNA position trigger keep mask. All initial entry positions
       will be taken from the previous instrument if bit is set.  */
    int8_t nna_pos_keep_mask;
    enum AVSequencerSynthNNAPosKeepMask {
    AVSEQ_SYNTH_NNA_POS_KEEP_MASK_VOLUME_NNA    = 0x01, ///< Keep NNA trigger position for volume
    AVSEQ_SYNTH_NNA_POS_KEEP_MASK_PANNING_NNA   = 0x02, ///< Keep NNA trigger position for panning
    AVSEQ_SYNTH_NNA_POS_KEEP_MASK_SLIDE_NNA     = 0x04, ///< Keep NNA trigger position for slide
    AVSEQ_SYNTH_NNA_POS_KEEP_MASK_SPECIAL_NNA   = 0x08, ///< Keep NNA trigger position for special
    AVSEQ_SYNTH_NNA_POS_KEEP_MASK_VOLUME_DNA    = 0x10, ///< Keep DNA trigger position for volume
    AVSEQ_SYNTH_NNA_POS_KEEP_MASK_PANNING_DNA   = 0x20, ///< Keep DNA trigger position for panning
    AVSEQ_SYNTH_NNA_POS_KEEP_MASK_SLIDE_DNA     = 0x40, ///< Keep DNA trigger position for slide
    AVSEQ_SYNTH_NNA_POS_KEEP_MASK_SPECIAL_DNA   = 0x80, ///< Keep DNA trigger position for special
    };

    /** Variable keep mask. All variables where bit is set will be
       keeped (normally they will be overwritten with the initial
       values), e.g. bit 5 set will keep variable 5 (v5).  */
    int16_t var_keep_mask;

    /** Array of pointers containing every unknown data field where
       the last element is indicated by a NULL pointer reference. The
       first 64-bit of the unknown data contains an unique identifier
       for this chunk and the second 64-bit data is actual unsigned
       length of the following raw data. Some formats are chunk based
       and can store information, which can't be handled by some
       other, in case of a transition the unknown data is kept as is.
       Some programs write editor settings for synth sounds in those
       chunks, which then won't get lost in that case.  */
    uint8_t **unknown_data;
} AVSequencerSynth;

#include "libavsequencer/sample.h"

/**
 * Creates a new uninitialized empty synth sound.
 *
 * @return pointer to freshly allocated AVSequencerSynth, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerSynth *avseq_synth_create(void);

/**
 * Opens and registers a synth sound to a sample.
 *
 * @param sample the AVSequencerSample structure to attach the new synth sound to
 * @param lines the number of synth code lines to be used for the new synth sound
 * @param waveforms the number of waveforms to allocate at once for the new synth sound
 * @param samples the number of samples to allocate for each waveform in the new synth sound
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_open(AVSequencerSample *sample, uint32_t lines,
                     uint32_t waveforms, uint32_t samples);

/**
 * Creates a new uninitialized empty synth sound waveform.
 *
 * @return pointer to freshly allocated AVSequencerSynthWave, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerSynthWave *avseq_synth_waveform_create(void);

/**
 * Opens and registers a synth sound waveform to a synth sound.
 *
 * @param synth the AVSequencerSynth structure to add the new synth sound waveform to
 * @param samples the number of samples to allocate to the new synth sound waveform
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_waveform_open(AVSequencerSynth *synth, uint32_t samples);

/**
 * Opens and registers synth sound waveform data to a synth sound waveform.
 *
 * @param waveform the AVSequencerSynthWave structure to attach the synth sound waveform data to
 * @param samples the number of samples to allocate for the synth sound waveform data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_waveform_data_open(AVSequencerSynthWave *waveform, uint32_t samples);

/**
 * Opens and registers a synth sound code to a synth sound.
 *
 * @param synth the AVSequencerSynth structure to attach the new synth sound code to
 * @param lines the number of synth code lines to be used for the new synth sound
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_code_open(AVSequencerSynth *synth, uint32_t lines);

#endif /* AVSEQUENCER_SYNTH_H */
