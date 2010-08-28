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
 * Return LIBAVSEQUENCER_VERSION_INT constant.
 */
unsigned avsequencer_version(void);

/**
 * Return the libavsequencer build-time configuration.
 */
const char *avsequencer_configuration(void);

/**
 * Return the libavsequencer license.
 */
const char *avsequencer_license(void);

#include "libavutil/log.h"
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavsequencer/mixer.h"
#include "libavsequencer/module.h"
#include "libavsequencer/song.h"

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
    struct AVSequencerPlayerGlobals *player_globals;

    /** AVSequencerPlayerHostChannel pointer to host channel data.  */
    struct AVSequencerPlayerHostChannel *player_host_channel;

    /** AVSequencerPlayerChannel pointer to virtual channel data.  */
    struct AVSequencerPlayerChannel *player_channel;

    /** AVSequencerPlayerHook pointer to callback hook.  */
    struct AVSequencerPlayerHook *player_hook;

    /** Current module used by current playback handler or NULL if
       no module is currently being processed.  */
    AVSequencerModule *player_module;

    /** Current sub-song used by current playback handler or NULL
       if no sub-song is currently being processed.  */
    AVSequencerSong *player_song;

    /** Current mixing engine used by current playback handler
       or NULL if there is no module and sub-song being processed.  */
    AVMixerData *player_mixer_data;

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
       internal one.  */
    void *synth_code_exec_lut;

    /** Array of pointers containing every module which is registered
       and ready for access to the sequencer.  */
    AVSequencerModule **module_list;

    /** Total amount of modules registered to the sequencer.  */
    uint16_t modules;

    /** Array of pointers containing every mixing engine instance
       which is registered and ready for access to the sequencer.  */
    AVMixerData **mixer_data_list;

    /** Total amount of mixer instances registered to the
       sequencer.  */
    uint16_t mixers;

    /** Current randomization seed value for a very fast randomize
       function used by volume, panning and pitch swing or envelopes
       featuring randomized data instead of waveforms.  */
    uint32_t seed;

    /** Executes one tick of the playback handler.  */
    int (*playback_handler)(AVMixerData *mixer_data);
} AVSequencerContext;

/** Registers all mixers to the AVSequencer.
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avsequencer_register_all(void);

/** Registers a mixer to the AVSequencer.
 *
 * @param mixctx the AVMixerContext to register
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_mixer_register(AVMixerContext *mixctx);

/** Gets a mixer by its name.
 *
 * @param name the name of the mixer to get
 * @return pointer to mixer context on success, NULL otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVMixerContext *avseq_mixer_get_by_name(const char *name);

/** Gets the pointer to the next mixer context array.
 *
 * @param mixctx the AVMixerContext array of the next mixer to get
 * @return pointer to next mixer context array on success, NULL otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVMixerContext **avseq_mixer_next(AVMixerContext **mixctx);

/** Uninitializes all the mixers registered to the AVSequencer.
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avsequencer_uninit(void);

/**
 * Opens and registers a new AVSequencer context.
 *
 * @param mixctx the AVMixerContext to use as an initial mixer or NULL for none
 * @param args The string of parameters to use when initializing the mixer.
 *             The format and meaning of this string varies by mixer.
 * @param opaque The xtra non-string data needed by the mixer. The meaning
 *               of this parameter varies by mixer.
 * @return pointer to registered AVSequencerContext, NULL otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerContext *avsequencer_open(AVMixerContext *mixctx,
                                     const char *args, void *opaque);

/** Recursively destroys the AVSequencerContext and frees all memory.
 *
 * @param avctx the AVSequencerContext to be destroyed recursively
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avsequencer_destroy(AVSequencerContext *avctx);

/**
 * Opens and initializes a new AVSequencer mixer context.
 *
 * @param mixctx the AVMixerContext to initialize
 * @param args   The string of parameters to use when initializing the mixer.
 *               The format and meaning of this string varies by mixer.
 * @param opaque The xtra non-string data needed by the mixer. The meaning
 *               of this parameter varies by mixer.
 * @return pointer to the new mixer data used for mixing, NULL otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVMixerData *avseq_mixer_init(AVSequencerContext *avctx, AVMixerContext *mixctx,
                                       const char *args, void *opaque);

/**
 * Closes and uninitializes an AVSequencer mixer data container.
 *
 * @param avctx the AVSequencerContext to uninitialize the mixer from
 * @param mixer_data the AVMixerData to uninitialize
 * @return >= 0 on success, a negative error code otherwise
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_mixer_uninit(AVSequencerContext *avctx, AVMixerData *mixer_data);

/**
 * Sets and transfers a new mixing rate and number of output
 * channels to the mixing engine.
 *
 * @param mixer_data the AVMixerData to set the new mixing rate and
 *                   number of output channels of
 * @param new_mix_rate the new mixing rate in Hz to use for mixing
 * @param new_channels the new number of output channels to use for mixing
 * @return the mixing rate in Hz which actually has been set
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
uint32_t avseq_mixer_set_rate(AVMixerData *mixer_data, uint32_t new_mix_rate,
                              uint32_t new_channels);

/**
 * Sets and transfers a new tempo for the playback handler to the
 * mixing engine.
 *
 * @param mixer_data the AVMixerData to set the new tempo
 * @param new_tempo the new tempo in AV_TIME_BASE fractional seconds
 *                  to use for mixing
 * @return the tempo in AV_TIME_BASE fractional seconds which actually has been set
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
uint32_t avseq_mixer_set_tempo(AVMixerData *mixer_data, uint32_t new_tempo);

/**
 * Sets and transfers a new volume boost, left and right volume and
 * number of channels to the mixing engine.
 *
 * @param mixer_data the AVMixerData to set the volume and channels
 * @param amplify the new volume boost where a value of 65536 (=0x10000)
 *                indicates 100%
 * @param left_volume the new left volume where a value of 65536 (=0x10000)
 *                    indicates 100%
 * @param right_volume the new volume boost where a value of 65536 (=0x10000)
 *                     indicates 100%
 * @param channels the new number of channels to be used for mixing
 * @return the number of channels which actually have been set
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
uint32_t avseq_mixer_set_volume(AVMixerData *mixer_data, uint32_t amplify,
                                uint32_t left_volume, uint32_t right_volume,
                                uint32_t channels);

/**
 * Gets and transfers a channel data block from the internal mixing
 * engine to the AVSequencer.
 *
 * @param mixer_data the AVMixerData to get the channel data block from
 * @param mixer_channel the AVMixerChannel to be received from the
 *                      internal mixing engine
 * @param channel the number of channel to be received
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_mixer_get_channel(AVMixerData *mixer_data,
                             AVMixerChannel *mixer_channel, uint32_t channel);

/**
 * Sets and transfers a channel data block from the AVSequencer to the
 * internal mixing engine.
 *
 * @param mixer_data the AVMixerData to set the channel data block of
 * @param mixer_channel the AVMixerChannel to be stored into the internal
 *                      mixing engine
 * @param channel the number of channel to be stored
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_mixer_set_channel(AVMixerData *mixer_data,
                             AVMixerChannel *mixer_channel, uint32_t channel);

/**
 * Fills the output mixing buffer by calculating all the input channel samples.
 *
 * @param mixer_data the AVMixerData to do the actual mixing step
 * @param buf the target buffer to mix the output data to
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_mixer_do_mix(AVMixerData *mixer_data, int32_t *buf);

/**
 * Creates a new uninitialized empty module.
 *
 * @return pointer to freshly allocated AVSequencerModule, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerModule *avseq_module_create(void);

/**
 * Destroys a module by freeing its occupied memory.
 *
 * @param module the AVSequencerModule to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_module_destroy(AVSequencerModule *module);

/**
 * Opens and registers a module to the AVSequencer.
 *
 * @param avctx the AVSequencerContext to store the opened module into
 * @param module the AVSequencerModule which has been opened to be registered
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_module_open(AVSequencerContext *avctx, AVSequencerModule *module);

/**
 * Closes and unregisters module from the AVSequencer.
 *
 * @param avctx the AVSequencerContext to remove the module reference
 * @param module the AVSequencerModule which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_module_close(AVSequencerContext *avctx, AVSequencerModule *module);

/**
 * Changes module virtual channels to new number of channels specified.
 *
 * @param avctx the AVSequencerContext to update the number of virtual
 *              channels for playback
 * @param module the AVSequencerModule to set the new number of virtual channels to
 * @param channels the new amount of virtual channels to use
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_module_set_channels(AVSequencerContext *avctx, AVSequencerModule *module,
                              uint32_t channels);

/**
 * Initializes the playback structures for playing back a sub-song in a module.
 *
 * @param avctx the AVSequencerContext to be initialized for playback
 * @param mixctx the AVMixerContext to be initialized for playback
 * @param module the AVSequencerModule to be initialized for playback
 * @param song the AVSequencerSong to be initialized for playback
 * @param args The string of parameters to use when initializing the mixer.
 *             The format and meaning of this string varies by mixer.
 * @param opaque The xtra non-string data needed by the mixer. The meaning
 *               of this parameter varies by mixer.
 * @param mode the playback mode to use, 0 is one-shoot playback and 1 is looping
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_module_play(AVSequencerContext *avctx, AVMixerContext *mixctx,
                      AVSequencerModule *module, AVSequencerSong *song,
                      const char *args, void *opaque, uint32_t mode);

/**
 * Uninitializes the playback structures of sub-song of a module playback.
 *
 * @param avctx the AVSequencerContext to be uninitialized for playback
 * @param mode the uninitialize mode to use, 0 is mixer only (pause) and 1 is all (stop)
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_module_stop(AVSequencerContext *avctx, uint32_t mode);

/**
 * Creates a new uninitialized empty sub-song.
 *
 * @return pointer to freshly allocated AVSequencerSong, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerSong *avseq_song_create(void);

/**
 * Destroys a sub-song by freeing its occupied memory.
 *
 * @param song the AVSequencerSong to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_song_destroy(AVSequencerSong *song);

/**
 * Opens and registers a new sub-song to a module.
 *
 * @param module the AVSequencerModule to register the new sub-song to
 * @param song the AVSequencerSong structure to initialize
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_song_open(AVSequencerModule *module, AVSequencerSong *song);

/**
 * Closes and unregisters a sub-song from a module.
 *
 * @param module the AVSequencerModule to remove the sub-song reference
 * @param song the AVSequencerSong which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_song_close(AVSequencerModule *module, AVSequencerSong *song);

/**
 * Calculates the speed of a sub-song and fills the player globals structure.
 *
 * @param avctx the AVSequencerContext to fill the player globals structure of
 * @param song the AVSequencerSong structure to calculate the speed of
 * @return relative tempo value in AV_TIME_BASE fractional seconds, 0 on error
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
uint32_t avseq_song_calc_speed(AVSequencerContext *avctx, AVSequencerSong *song);

/**
 * Resets a sub-song to start playback from very beginning and fills the player
 * globals and host channel structures.
 *
 * @param avctx the AVSequencerContext to fill the player globals and host channel
 *              structure of
 * @param song the AVSequencerSong structure to reset to playback from beginning
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_song_reset(AVSequencerContext *avctx, AVSequencerSong *song);

/**
 * Changes sub-song channels to new number of channels specified.
 *
 * @param avctx the AVSequencerContext to update the number of
 *              channels for playback
 * @param song the AVSequencerSong to set the new number of channels to
 * @param channels the new amount of channels to use
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_song_set_channels(AVSequencerContext *avctx, AVSequencerSong *song,
                            uint32_t channels);

/**
 * Sets the sub-song GoSub and pattern loop command stack size to a
 * new stack size.
 *
 * @param avctx the AVSequencerContext to update the stack size
 *              pointers for playback
 * @param song the AVSequencerSong to set the new GoSub and pattern
 *             loop command stack size to
 * @param gosub_stack the new size of the GoSub command stack
 * @param loop_stack the new size of the pattern loop command stack
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_song_set_stack(AVSequencerContext *avctx, AVSequencerSong *song,
                         uint32_t gosub_stack, uint32_t loop_stack);

/**
 * Opens and registers a new order list to a sub-song.
 *
 * @param song the AVSequencerSong structure to store the initialized order list
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_order_open(AVSequencerSong *song);

/**
 * Closes and unregisters an array of order data from the order list
 * of a sub-song.
 *
 * @param song the AVSequencerSong of which the order list's data has
 *             been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_order_close(AVSequencerSong *song);

/**
 * Creates a new uninitialized empty order list data entry.
 *
 * @return pointer to freshly allocated AVSequencerOrderData, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerOrderData *avseq_order_data_create(void);

/**
 * Destroys an order data entry by freeing its occupied memory.
 *
 * @param track the AVSequencerOrderData to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_order_data_destroy(AVSequencerOrderData *order_data);

/**
 * Opens and registers a new order list data entry to an order list.
 *
 * @param order_list the AVSequencerOrderList to register the order list data entry to
 * @param order_data the AVSequencerOrderData structure to initialize
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_order_data_open(AVSequencerOrderList *order_list, AVSequencerOrderData *order_data);

/**
 * Closes and unregisters an order list data entry from from a order list channel.
 *
 * @param song the AVSequencerSong to remove the order list data entry references
 * @param order_list the AVSequencerOrderList to remove the order list data entry reference
 * @param order_data the AVSequencerOrderData which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_order_data_close(AVSequencerSong *song, AVSequencerOrderList *order_list,
                            AVSequencerOrderData *order_data);

/**
 * Gets the address of the order data entry represented by an integer value.
 *
 * @param song the AVSequencerSong to get the order data entry address from
 * @param channel the order list channel number of which to get the order data entry
 * @param order the order data entry number to get the AVSequencerOrderData structure of
 * @return pointer to order data entry address, NULL if order data entry number not found
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerOrderData *avseq_order_get_address(AVSequencerSong *song, uint32_t channel, uint32_t order);

/**
 * Creates a new uninitialized empty track.
 *
 * @return pointer to freshly allocated AVSequencerTrack, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerTrack *avseq_track_create(void);

/**
 * Destroys a track by freeing its occupied memory.
 *
 * @param track the AVSequencerTrack to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_track_destroy(AVSequencerTrack *track);

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
 * Closes and unregisters a track from a sub-song.
 *
 * @param song the AVSequencerSong to remove the track reference
 * @param track the AVSequencerTrack which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_track_close(AVSequencerSong *song, AVSequencerTrack *track);

/**
 * Opens and registers a new array of track data to a track.
 *
 * @param track the AVSequencerTrack structure to store the initialized track data
 * @param rows the number of rows to use for the initialized track data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_track_data_open(AVSequencerTrack *track, uint32_t rows);

/**
 * Closes and unregisters an array of track data from a track.
 *
 * @param track the AVSequencerTrack of which the track data has been
 *              opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_track_data_close(AVSequencerTrack *track);

/**
 * Creates a new uninitialized empty track effect.
 *
 * @return pointer to freshly allocated AVSequencerTrackEffect, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerTrackEffect *avseq_track_effect_create(void);

/**
 * Destroys a track effect by freeing its occupied memory.
 *
 * @param track the AVSequencerTrackEffect to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_track_effect_destroy(AVSequencerTrackEffect *effect);

/**
 * Opens and registers a new track data effect to track data.
 *
 * @param track the AVSequencerTrack structure to add the new track data effect to
 * @param data the AVSequencerTrackRow structure to add the new track data effect to
 * @param effect the AVSequencerTrackEffect to be added to the track data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_track_effect_open(AVSequencerTrack *track, AVSequencerTrackRow *data, AVSequencerTrackEffect *effect);

/**
 * Closes and unregisters a track effect from a row of track data.
 *
 * @param track_data the AVSequencerTrackRow to remove the track effect reference
 * @param effect the AVSequencerTrackEffect which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_track_effect_close(AVSequencerTrackRow *track_data, AVSequencerTrackEffect *effect);

/**
 * Gets the address of the track represented by an integer value.
 *
 * @param song the AVSequencerSong structure to get the track address from
 * @param track the track number to get the AVSequencerTrack structure of
 * @return pointer to track address, NULL if track number not found
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerTrack *avseq_track_get_address(AVSequencerSong *song, uint32_t track);

/**
 * Unpacks a compressed AVSequencerTrack by using a track contents based algorithm.
 *
 * @param track the AVSequencerTrack where the track to be unpacked belongs to
 * @param buf the byte buffer containing the packed track stream
 * @param len the size of the byte buffer containing the packed track stream
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_track_unpack(AVSequencerTrack *track, const uint8_t *buf, uint32_t len);

/**
 * Creates a new uninitialized empty instrument.
 *
 * @return pointer to freshly allocated AVSequencerInstrument, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerInstrument *avseq_instrument_create(void);

/**
 * Destroys an instrument by freeing its occupied memory.
 *
 * @param instrument the AVSequencerInstrument to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_instrument_destroy(AVSequencerInstrument *instrument);

/**
 * Opens and registers a new instrument to a module.
 *
 * @param module the AVSequencerModule structure to add the new instrument to
 * @param instrument the AVSequencerInstrument to be added to the module
 * @param samples the number of empty samples to be added to the instrument
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_instrument_open(AVSequencerModule *module, AVSequencerInstrument *instrument,
                          uint32_t samples);

/**
 * Closes and unregisters an instrument from a module.
 *
 * @param module the AVSequencerModule to remove the instrument reference
 * @param instrument the AVSequencerInstrument which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_instrument_close(AVSequencerModule *module, AVSequencerInstrument *instrument);

/**
 * Creates a new uninitialized empty envelope.
 *
 * @return pointer to freshly allocated AVSequencerEnvelope, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerEnvelope *avseq_envelope_create(void);

/**
 * Destroys an envelope by freeing its occupied memory.
 *
 * @param envelope the AVSequencerEnvelope to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_envelope_destroy(AVSequencerEnvelope *envelope);

/**
 * Opens and registers a new envelope to a module.
 *
 * @param avctx the AVSequencerContext to add the new envelope to
 * @param module the AVSequencerModule structure to add the new envelope to
 * @param envelope the AVSequencerEnvelope to be added to the module
 * @param points the number of data points to be used in the envelope data
 * @param type the type of envelope data to initialize: 0 = create uninitialized envelope,
 * @param type                                          1 = create empty envelope,
 *                                                      2 = create sine envelope,
 *                                                      3 = create cosine envelope,
 *                                                      4 = create ramp envelope,
 *                                                      5 = create triangle envelope,
 *                                                      6 = create square envelope,
 *                                                      7 = create sawtooth envelope
 * @param scale the scale factor for the envelope data
 * @param y_offset the y offset value to add as absolute value to the envelope data
 * @param nodes the number of dragable nodes with linear connection between data points
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_envelope_open(AVSequencerContext *avctx, AVSequencerModule *module,
                        AVSequencerEnvelope *envelope, uint32_t points,
                        uint32_t type, uint32_t scale,
                        uint32_t y_offset, uint32_t nodes);

/**
 * Closes and unregisters an envelope from a module.
 *
 * @param module the AVSequencerModule to remove the envelope reference
 * @param envelope the AVSequencerEnvelope which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_envelope_close(AVSequencerModule *module, AVSequencerEnvelope *envelope);

/**
 * Opens and registers a new envelope data and node set to an envelope.
 *
 * @param avctx the AVSequencerContext to add the new envelope data and node set to
 * @param envelope the AVSequencerEnvelope to add the new envelope data and node set to
 * @param points the number of data points to be used in the envelope data
 * @param type the type of envelope data to initialize: 0 = just resize envelope
                                                        1 = create empty envelope,
 *                                                      2 = create sine envelope,
 *                                                      3 = create cosine envelope,
 *                                                      4 = create ramp envelope,
 *                                                      5 = create triangle envelope,
 *                                                      6 = create square envelope,
 *                                                      7 = create sawtooth envelope
 * @param scale the scale factor for the envelope data
 * @param y_offset the y offset value to add as absolute value to the envelope data
 * @param nodes the number of dragable nodes with linear connection between data points
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_envelope_data_open(AVSequencerContext *avctx, AVSequencerEnvelope *envelope,
                             uint32_t points, uint32_t type, uint32_t scale,
                             uint32_t y_offset, uint32_t nodes);

/**
 * Closes and unregisters an array of envelope data from an envelope.
 *
 * @param envelope the AVSequencerEnvelope of which the envelope data
 *                 has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_envelope_data_close(AVSequencerEnvelope *envelope);

/**
 * Gets the address of the envelope represented by an integer value.
 *
 * @param module the AVSequencerModule structure to get the envelope address from
 * @param envelope the envelope number to get the AVSequencerEnvelope structure of
 * @return pointer to envelope address, NULL if envelope number not found
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerEnvelope *avseq_envelope_get_address(AVSequencerModule *module, uint32_t envelope);

/**
 * Creates a new uninitialized empty keyboard definition.
 *
 * @return pointer to freshly allocated AVSequencerKeyboard, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerKeyboard *avseq_keyboard_create(void);

/**
 * Destroys a keyboard definition by freeing its occupied memory.
 *
 * @param keyboard the AVSequencerKeyboard to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_keyboard_destroy(AVSequencerKeyboard *keyboard);

/**
 * Opens and registers a new keyboard definition to a module.
 *
 * @param module the AVSequencerModule structure to add the new keyboard definition to
 * @param keyboard the AVSequencerKeyboard to be added to the module
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_keyboard_open(AVSequencerModule *module, AVSequencerKeyboard *keyboard);

/**
 * Closes and unregisters a keyboard definition from a module.
 *
 * @param module the AVSequencerModule to remove the keyboard definition reference
 * @param keyboard the AVSequencerKeyboard which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_keyboard_close(AVSequencerModule *module, AVSequencerKeyboard *keyboard);

/**
 * Gets the address of the keyboard definition represented by an integer value.
 *
 * @param module the AVSequencerModule structure to get the keyboard definition address from
 * @param keyboard the keyboard definition number to get the AVSequencerKeyboard structure of
 * @return pointer to keyboard definition address, NULL if keyboard definition number not found
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerKeyboard *avseq_keyboard_get_address(AVSequencerModule *module, uint32_t keyboard);

/**
 * Creates a new uninitialized empty arpeggio structure.
 *
 * @return pointer to freshly allocated AVSequencerArpeggio, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerArpeggio *avseq_arpeggio_create(void);

/**
 * Destroys an arpeggio structure by freeing its occupied memory.
 *
 * @param arpeggio the AVSequencerArpeggio to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_arpeggio_destroy(AVSequencerArpeggio *arpeggio);

/**
 * Opens and registers a new arpeggio structure to a module.
 *
 * @param module the AVSequencerModule structure to add the new arpeggio to
 * @param arpeggio the AVSequencerArpeggio to be added to the module
 * @param entries the number of arpeggio trigger entries to be used in the arpeggio data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_arpeggio_open(AVSequencerModule *module, AVSequencerArpeggio *arpeggio,
                        uint32_t entries);

/**
 * Closes and unregisters an arpeggio structure from a module.
 *
 * @param module the AVSequencerModule to remove the arpeggio structure reference
 * @param arpeggio the AVSequencerArpeggio which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_arpeggio_close(AVSequencerModule *module, AVSequencerArpeggio *arpeggio);

/**
 * Opens and registers a new arpeggio data set to an arpeggio structure.
 *
 * @param arpeggio the AVSequencerArpeggio to add the new arpeggio data set to
 * @param entries the number of arpeggio trigger entries to be used in the arpeggio data
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_arpeggio_data_open(AVSequencerArpeggio *arpeggio, uint32_t entries);

/**
 * Closes and unregisters an array of arpeggio data from an arpeggio structure.
 *
 * @param arpeggio the AVSequencerArpeggio of which the arpeggio structure data
 *                 has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_arpeggio_data_close(AVSequencerArpeggio *arpeggio);

/**
 * Gets the address of the arpeggio structure represented by an integer value.
 *
 * @param module the AVSequencerModule structure to get the arpeggio structure address from
 * @param arpeggio the arpeggio structure number to get the AVSequencerArpeggio structure of
 * @return pointer to arpeggio structure address, NULL if arpeggio structure number not found
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerArpeggio *avseq_arpeggio_get_address(AVSequencerModule *module, uint32_t arpeggio);

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
 * Destroys a sample by freeing its occupied memory.
 *
 * @param sample the AVSequencerSample to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_sample_destroy(AVSequencerSample *sample);

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
 * Closes and unregisters an audio sample from an instrument.
 *
 * @param instrument the AVSequencerInstrument to remove the audio sample reference
 * @param sample the AVSequencerSample which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_sample_close(AVSequencerInstrument *instrument, AVSequencerSample *sample);

/**
 * Opens and registers audio sample PCM data stream to a sample.
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

/**
 * Closes and unregisters an audio sample PCM data stream from a sample.
 *
 * @param sample the AVSequencerSample of which the sample PCM data
 *               stream has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_sample_data_close(AVSequencerSample *sample);

/**
 * Decrunches a AVSequencerSample by using delta compression algorithm.
 *
 * @param module the AVSequencerModule where the sample to be decrunched belongs to
 * @param sample the AVSequencerSample containing the crunched sample data to decrunch
 * @param delta_bits_per_sample the number of bits to be used in the delta compression
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_sample_decrunch(AVSequencerModule *module, AVSequencerSample *sample,
                           uint8_t delta_bits_per_sample);

/**
 * Finds the origin AVSequencerSample by searching through module instrument and sample list.
 *
 * @param module the AVSequencerModule where the origin sample to be searched belongs to
 * @param sample the redirected AVSequencerSample of which to find the origin
 * @return pointer to origin AVSequencerSample, NULL if search failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerSample *avseq_sample_find_origin(AVSequencerModule *module, AVSequencerSample *sample);

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
 * Destroys a synth sound by freeing its occupied memory.
 *
 * @param synth the AVSequencerSynth to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_synth_destroy(AVSequencerSynth *synth);

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
 * Closes and unregisters a synth sound from a sample.
 *
 * @param sample the AVSequencerSample of which the synth sound has
 *               been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_synth_close(AVSequencerSample *sample);

/**
 * Creates a new uninitialized empty synth sound symbol.
 *
 * @return pointer to freshly allocated AVSequencerSynthSymbolTable, NULL if allocation failed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
AVSequencerSynthSymbolTable *avseq_synth_symbol_create(void);

/**
 * Destroys a synth sound symbol by freeing its occupied memory.
 *
 * @param symbol the AVSequencerSynthSymbol to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_synth_symbol_destroy(AVSequencerSynthSymbolTable *symbol);

/**
 * Opens and registers a synth sound symbol to a synth sound.
 *
 * @param synth the AVSequencerSynth structure to add the new synth sound symbol to
 * @param symbol the AVSequencerSynthSymbolTable structure to add the new synth sound symbol to
 * @param name the name of the symbol to assign to the new synth sound symbol
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_symbol_open(AVSequencerSynth *synth, AVSequencerSynthSymbolTable *symbol,
                            const uint8_t *name);

/**
 * Closes and unregisters a synth sound symbol from a synth sound.
 *
 * @param synth the AVSequencerSynth to remove the synth sound symbol reference
 * @param symbol the AVSequencerSynthSymbolTable which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_synth_symbol_close(AVSequencerSynth *synth, AVSequencerSynthSymbolTable *symbol);

/**
 * Assigns a new symbol name to a synth sound symbol.
 *
 * @param synth the AVSequencerSynth structure to assign the new synth sound symbol name to
 * @param symbol the AVSequencerSynthSymbolTable structure of which to change the name
 * @param name the new name to assign to the new synth sound symbol
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_synth_symbol_assign(AVSequencerSynth *synth, AVSequencerSynthSymbolTable *symbol,
                              const uint8_t *name);

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
 * Destroys a synth sound waveform by freeing its occupied memory.
 *
 * @param waveform the AVSequencerSynthWave to be destroyed
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_synth_waveform_destroy(AVSequencerSynthWave *waveform);

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
 * Closes and unregisters a synth sound waveform from a synth sound.
 *
 * @param synth the AVSequencerSynth to remove the synth sound waveform reference
 * @param waveform the AVSequencerSynthWave which has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_synth_waveform_close(AVSequencerSynth *synth, AVSequencerSynthWave *waveform);

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
 * Closes and unregisters synth sound waveform data from a synth sound waveform.
 *
 * @param waveform the AVSequencerSynthWave of which the synth sound
 *                 waveform data has been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_synth_waveform_data_close(AVSequencerSynthWave *waveform);

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

/**
 * Closes and unregisters a synth sound code from a synth sound.
 *
 * @param synth the AVSequencerSynth of which the synth sound code has
 *              been opened to be unregistered
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
void avseq_synth_code_close(AVSequencerSynth *synth);

/**
 * Executes one tick of the playback handler, calculating everything
 * needed for the next step of the mixer. This function usually is
 * called from the mixing engines when they processed all channel data
 * and need to run the next tick of playback to further full their
 * output buffers. This function might also be called from a hardware
 * and/or software interrupts on some platforms.
 *
 * @param mixer_data the AVMixerData of which to process the next tick
 * @return >= 0 on success, a negative error code otherwise
 *
 * @note This is part of the new sequencer API which is still under construction.
 *       Thus do not use this yet. It may change at any time, do not expect
 *       ABI compatibility yet!
 */
int avseq_playback_handler(AVMixerData *mixer_data);

#endif /* AVSEQUENCER_AVSEQUENCER_H */
