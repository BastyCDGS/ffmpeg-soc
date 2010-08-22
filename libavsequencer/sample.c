/*
 * Implement AVSequencer samples management
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
 * Implement AVSequencer samples management.
 */

#include "libavutil/log.h"
#include "libavsequencer/avsequencer.h"

static const char *sample_name(void *p)
{
    AVSequencerSample *sample = p;
    AVMetadataTag *tag        = av_metadata_get(sample->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Sample";
}

static const AVClass avseq_sample_class = {
    "AVSequencer Sample",
    sample_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerSample *avseq_sample_create(void)
{
    return av_mallocz(sizeof(AVSequencerSample) + FF_INPUT_BUFFER_PADDING_SIZE);
}

int avseq_sample_open(AVSequencerInstrument *instrument, AVSequencerSample *sample,
                      int16_t *data, uint32_t length)
{
    AVSequencerSample **sample_list;
    uint8_t samples;
    int res;

    if (!instrument)
        return AVERROR_INVALIDDATA;

    sample_list = instrument->sample_list;
    samples     = instrument->samples;

    if (!(sample && ++samples)) {
        return AVERROR_INVALIDDATA;
    } else if (!(sample_list = av_realloc(sample_list, (samples * sizeof(AVSequencerSample *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(instrument, AV_LOG_ERROR, "Cannot allocate sample storage container.\n");
        return AVERROR(ENOMEM);
    }

    sample->av_class        = &avseq_sample_class;
    sample->bits_per_sample = 16;
    sample->rate            = 8363; // NTSC frequency (60 Hz sequencers), for PAL use 8287
    sample->rate_max        = 0xFFFFFFFF;
    sample->global_volume   = 255;
    sample->volume          = 255;
    sample->panning         = -128;

    if (length && (res = avseq_sample_data_open(sample, data, length)) < 0) {
        av_free(sample_list);
        return res;
    }

    sample_list[samples - 1] = sample;
    instrument->sample_list  = sample_list;
    instrument->samples      = samples;

    return 0;
}

int avseq_sample_data_open(AVSequencerSample *sample, int16_t *data, uint32_t samples)
{
    uint64_t size;
    int8_t *smp_data;

    if (!sample)
        return AVERROR_INVALIDDATA;

    smp_data = (int8_t *) sample->data;
    size     = FFALIGN((uint64_t) samples * sample->bits_per_sample, 8) >> 3;

    if (size > (UINT64_C(0xFFFFFFFF) - FF_INPUT_BUFFER_PADDING_SIZE)) {
        av_log(sample, AV_LOG_ERROR, "Exceeded maximum number of samples.\n");
        return AVERROR_INVALIDDATA;
    } else if (data) {
        smp_data      = (int8_t *) data;
        sample->flags = AVSEQ_SAMPLE_FLAG_REDIRECT;
    } else if (!(smp_data = av_realloc(smp_data, size + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(sample, AV_LOG_ERROR, "Cannot allocate sample data.\n");
        return AVERROR(ENOMEM);
    } else if (samples > sample->samples) {
        memset(smp_data + sample->size, 0, (samples - sample->samples) * (FFALIGN((uint64_t) samples * sample->bits_per_sample, 8) >> 3));
    } else if (!sample->data) {
        memset(smp_data, 0, size);
    }

    sample->data    = (int16_t *) smp_data;
    sample->size    = (uint32_t) size;
    sample->samples = samples;

    return 0;
}

static void decrunch_sample_8 (int8_t *data, uint32_t length);
static void decrunch_sample_16(int16_t *data, uint32_t length);
static void decrunch_sample_32(int32_t *data, uint32_t length);
static void decrunch_sample_x (int32_t *data, uint32_t samples, const uint8_t bits_per_sample);

int avseq_sample_decrunch(AVSequencerModule *module, AVSequencerSample *sample,
                          uint8_t delta_bits_per_sample)
{
    int16_t *data;

    if (!(sample = avseq_sample_find_origin(module, sample)))
        return AVERROR_INVALIDDATA;

    if (!((data = sample->data) && sample->samples && sample->size)) {
        av_log(sample, AV_LOG_ERROR, "Empty sample data encountered.\n");
        return AVERROR_INVALIDDATA;
    }

    if (!delta_bits_per_sample)
        delta_bits_per_sample = sample->bits_per_sample;

    switch (delta_bits_per_sample) {
    case 8 :
        decrunch_sample_8((int8_t *) data, FFALIGN(sample->size, 8) >> 3);

        break;
    case 16 :
        decrunch_sample_16(data, FFALIGN(sample->size, 8) >> 3);

        break;
    case 32 :
        decrunch_sample_32((int32_t *) data, FFALIGN(sample->size, 8) >> 3);

        break;
    default :
        decrunch_sample_x((int32_t *) data, sample->samples, delta_bits_per_sample);

        break;
    }

    return 0;
}

static void decrunch_sample_8(int8_t *data, uint32_t length)
{
    int8_t sample = 0;

    do {
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
    } while (--length);
}

static void decrunch_sample_16(int16_t *data, uint32_t length)
{
    int16_t sample = 0;

    do {
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
    } while (--length);
}

static void decrunch_sample_32(int32_t *data, uint32_t length)
{
    int32_t sample = 0;

    do {
        sample += *data;
        *data++ = sample;
        sample += *data;
        *data++ = sample;
    } while (--length);
}

static void decrunch_sample_x(int32_t *data, uint32_t samples, const uint8_t bits_per_sample)
{
    uint32_t bit = 0;
    uint32_t sample = 0, tmp_sample;

    do {
        if ((bit + bits_per_sample) < 32) {
            tmp_sample = (*data << bit) & ~((1 << (32 - bits_per_sample)) - 1);
        } else {
            tmp_sample  = *data << bit;
            tmp_sample |= (*(data + 1) & ~((1 << (64 - (bit + bits_per_sample))) - 1)) >> (32 - bit);
        }

        sample     += tmp_sample;
        tmp_sample  = sample;
        tmp_sample &= ~((1 << (32 - bits_per_sample)) - 1);

        if ((bit + bits_per_sample) < 32) {
            *data &= ~((1 << (32 - bit)) - (1 << (32 - bits_per_sample - bit)));
            *data |= tmp_sample >> bit;
        } else {
            *data   &= ~((1 << (32 - bit)) - 1);
            *data++ |= tmp_sample >> bit;
            *data   &= (1 << (64 - (bit + bits_per_sample))) - 1;
            *data   |= tmp_sample << (32 - bit);

            bit -= 32;
        }

        bit += bits_per_sample;
    } while (--samples);
}

AVSequencerSample *avseq_sample_find_origin(AVSequencerModule *module, AVSequencerSample *sample)
{
    AVSequencerSample *origin_sample = sample;

    if (sample) {
        if (sample->flags & AVSEQ_SAMPLE_FLAG_REDIRECT) {
            uint16_t i;

            if (!module) {
                av_log(sample, AV_LOG_ERROR, "Origin sample cannot be found because no module was specified.\n");
                return NULL;
            }

            for (i = 0; i < module->instruments; ++i) {
                AVSequencerInstrument *instrument = module->instrument_list[i];
                uint8_t j;

                if (!instrument)
                    continue;

                for (j = 0; j < instrument->samples; ++j) {
                    if (!(sample = instrument->sample_list[j]))
                        continue;

                    if (sample->flags & AVSEQ_SAMPLE_FLAG_REDIRECT)
                        continue;

                    if (origin_sample->data == sample->data)
                        return sample;
                }
            }

            av_log(sample, AV_LOG_ERROR, "Origin sample cannot be found in module.\n");
            origin_sample = NULL;
        }
    }

    return origin_sample;
}
