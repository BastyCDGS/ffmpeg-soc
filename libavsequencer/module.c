/*
 * Implement AVSequencer module stuff
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
 * Implement AVSequencer module stuff.
 */

#include "libavsequencer/avseq.h"
#include "libavsequencer/module.h"

int avseq_module_open(AVSequencerContext *avctx, AVSequencerModule *module) {
    AVSequencerModule **module_list = avctx->module_list;
    uint16_t modules                = avctx->modules;

    if (!module || !++modules) {
        return AVERROR_INVALIDDATA;
    } else if (!(module_list = av_realloc(module_list, modules * sizeof(AVSequencerModule *)))) {
        av_log(module, AV_LOG_ERROR, "cannot allocate module storage container.\n");
        return AVERROR(ENOMEM);
    }

    if (!module->channels)
        module->channels = 64;

    module_list[modules]          = module;
    avctx->module_list            = module_list;
    avctx->modules                = modules;

    return 0;
}
