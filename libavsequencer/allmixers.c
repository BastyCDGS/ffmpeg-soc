/*
 * Provide registration of all mixers for the sequencer
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

/**
 * @file
 * Provide registration of all mixers for the sequencer.
 */

#include "libavsequencer/avsequencer.h"

#define REGISTER_MIXER(X,x) { \
          extern AVSequencerMixerContext x##_mixer; \
          if(CONFIG_##X##_MIXER)  avseq_mixer_register(&x##_mixer); }

void avsequencer_register_all(void)
{
    static int initialized;

    if (initialized)
        return;
    initialized = 1;

    /* Mixers */
    REGISTER_MIXER (NULL, null);
    REGISTER_MIXER (LOW_QUALITY, low_quality);
//    rEGISTER_mIXER (HIGH_QUALITY, high_quality);
}
