/*
 * Implement AVSequencer synth sound, code, symbol and waveform management
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
 * Implement AVSequencer synth sound, code, symbol and waveform management.
 */

#include "libavutil/log.h"
#include "libavsequencer/avsequencer.h"

static const char *synth_name(void *p)
{
    AVSequencerSynth *synth = p;
    AVMetadataTag *tag      = av_metadata_get(synth->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    return tag->value;
}

static const AVClass avseq_synth_class = {
    "AVSequencer Synth",
    synth_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerSample *avseq_sample_create(void) {
    return av_mallocz(sizeof(AVSequencerSample));
}

AVSequencerSynth *avseq_synth_create(void) {
    return av_mallocz(sizeof(AVSequencerSynth));
}

int avseq_synth_open(AVSequencerSample *sample, uint32_t lines,
                     uint32_t waveforms, uint32_t samples) {
    AVSequencerSynth *synth;
    uint32_t i;
    int res;

    if (!lines)
        lines = 1;

    if (!waveforms)
        waveforms = 1;

    if (!samples)
        samples = 64;

    if (!sample || !lines >= 0x10000 || waveforms >= 0x10000) {
        return AVERROR_INVALIDDATA;
    } else if (!(synth = avseq_synth_create())) {
        av_log(sample, AV_LOG_ERROR, "cannot allocate synth sound container.\n");
        return AVERROR(ENOMEM);
    }

    synth->av_class = &avseq_synth_class;

    if ((res = avseq_synth_code_open(synth, lines)) < 0) {
        av_free(synth);
        return res;
    }

    for (i = 0; i < waveforms; ++i) {
        if ((res = avseq_synth_waveform_open(synth, samples)) < 0) {
            while (i--) {
                av_free(synth->waveform_list[i]);
            }

            av_free(synth->code);
            av_free(synth);
            return res;
        }
    }

    sample->synth = synth;

    return 0;
}

int avseq_synth_code_open(AVSequencerSynth *synth, uint32_t lines) {
    AVSequencerSynthCode *code = synth->code;

    if (!lines)
        lines = 1;

    if (!synth || lines >= 0x10000) {
        return AVERROR_INVALIDDATA;
    } else if (!(code = av_realloc(code, lines * sizeof(AVSequencerSynthCode)))) {
        av_log(synth, AV_LOG_ERROR, "cannot allocate synth sound code.\n");
        return AVERROR(ENOMEM);
    }

    synth->code = code;
    synth->size = (uint16_t) lines;

    return 0;
}

static const char *waveform_name(void *p)
{
    AVSequencerSynthWave *waveform = p;
    AVMetadataTag *tag             = av_metadata_get(waveform->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    return tag->value;
}

static const AVClass avseq_waveform_class = {
    "AVSequencer Synth Waveform",
    waveform_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerSynthWave *avseq_synth_waveform_create(void) {
    return av_mallocz(sizeof(AVSequencerSynthWave));
}

int avseq_synth_waveform_open(AVSequencerSynth *synth, uint32_t samples) {
    AVSequencerSynthWave *waveform;
    AVSequencerSynthWave **waveform_list = synth->waveform_list;
    uint16_t waveforms                   = synth->waveforms;
    int res;

    if (!synth || !++waveforms) {
        return AVERROR_INVALIDDATA;
    } else if (!(waveform_list = av_realloc(waveform_list, waveforms * sizeof(AVSequencerSynthWave *)))) {
        av_log(synth, AV_LOG_ERROR, "cannot allocate synth sound waveform storage container.\n");
        return AVERROR(ENOMEM);
    } else if (!(waveform = avseq_synth_waveform_create())) {
        av_free(waveform_list);
        av_log(synth, AV_LOG_ERROR, "cannot allocate synth sound waveform.\n");
        return AVERROR(ENOMEM);
    }

    waveform->av_class   = &avseq_waveform_class;
    waveform->repeat_len = samples;

    if ((res = avseq_synth_waveform_data_open(waveform, samples)) < 0) {
        av_free(waveform);
        av_free(waveform_list);
        return res;
    }

    waveform_list[waveforms] = waveform;
    synth->waveform_list     = waveform_list;
    synth->waveforms         = waveforms;

    return 0;
}

int avseq_synth_waveform_data_open(AVSequencerSynthWave *waveform, uint32_t samples) {
    uint32_t size;
    int16_t *data;

    if (!samples)
        samples = 64;

    size = waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT ? samples : samples << 1;

    if (!waveform) {
        return AVERROR_INVALIDDATA;
    } else if (!(data = av_mallocz(size))) {
        av_log(waveform, AV_LOG_ERROR, "cannot allocate synth sound waveform data.\n");
        return AVERROR(ENOMEM);
    }

    waveform->data       = data;
    waveform->size       = size;
    waveform->samples    = samples;

    return 0;
}
