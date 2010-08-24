/*
 * Implement AVSequencer instrument management
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
 * Implement AVSequencer instrument management.
 */

#include "libavutil/log.h"
#include "libavformat/avformat.h"
#include "libavsequencer/avsequencer.h"

static const char *instrument_name(void *p)
{
    AVSequencerInstrument *instrument = p;
    AVMetadataTag *tag                = av_metadata_get(instrument->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Instrument";
}

static const AVClass avseq_instrument_class = {
    "AVSequencer Instrument",
    instrument_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerInstrument *avseq_instrument_create(void)
{
    return av_mallocz(sizeof(AVSequencerInstrument) + FF_INPUT_BUFFER_PADDING_SIZE);
}

void avseq_instrument_destroy(AVSequencerInstrument *instrument)
{
    if (instrument)
        av_metadata_free(&instrument->metadata);

    av_free(instrument);
}

int avseq_instrument_open(AVSequencerModule *module, AVSequencerInstrument *instrument,
                          uint32_t samples)
{
    AVSequencerSample *sample;
    AVSequencerInstrument **instrument_list;
    uint32_t i;
    uint16_t instruments;
    int res;

    if (!module)
        return AVERROR_INVALIDDATA;

    instrument_list = module->instrument_list;
    instruments     = module->instruments;

    if (!(instrument && ++instruments)) {
        return AVERROR_INVALIDDATA;
    } else if (!(instrument_list = av_realloc(instrument_list, (instruments * sizeof(AVSequencerInstrument *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(module, AV_LOG_ERROR, "Cannot allocate instrument storage container.\n");
        return AVERROR(ENOMEM);
    }

    instrument->av_class = &avseq_instrument_class;

    for (i = 0; i < samples; ++i) {
        if (!(sample = avseq_sample_create())) {
            while (i--) {
                av_free(instrument->sample_list[i]);
            }

            av_log(instrument, AV_LOG_ERROR, "Cannot allocate sample number %d.\n", i + 1);
            return AVERROR(ENOMEM);
        }

        if ((res = avseq_sample_open(instrument, sample, NULL, 0)) < 0) {
            while (i--) {
                av_free(instrument->sample_list[i]);
            }

            return res;
        }
    }

    instrument->fade_out         = 65535;
    instrument->pitch_pan_center = 4*12; // C-4
    instrument->global_volume    = 255;
    instrument->default_panning  = -128;
    instrument->env_usage_flags  = ~(AVSEQ_INSTRUMENT_FLAG_USE_VOLUME_ENV|AVSEQ_INSTRUMENT_FLAG_USE_PANNING_ENV|AVSEQ_INSTRUMENT_FLAG_USE_SLIDE_ENV|-0x2000);

    instrument_list[instruments - 1] = instrument;
    module->instrument_list          = instrument_list;
    module->instruments              = instruments;

    return 0;
}

void avseq_instrument_close(AVSequencerModule *module, AVSequencerInstrument *instrument)
{
    AVSequencerInstrument **instrument_list;
    uint16_t instruments, i;

    if (!(module && instrument))
        return;

    instrument_list = module->instrument_list;
    instruments     = module->instruments;

    for (i = 0; i < instruments; ++i) {
        if (instrument_list[i] == instrument)
            break;
    }

    if (instruments && (i != instruments)) {
        AVSequencerInstrument *last_instrument = instrument_list[--instruments];

        if (!instruments) {
            av_freep(&module->instrument_list);

            module->instruments = 0;
        } else if (!(instrument_list = av_realloc(instrument_list, (instruments * sizeof(AVSequencerInstrument *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            const unsigned copy_instruments = i + 1;

            instrument_list = module->instrument_list;

            if (copy_instruments < instruments)
                memmove(instrument_list + i, instrument_list + copy_instruments, (instruments - copy_instruments) * sizeof(AVSequencerInstrument *));

            instrument_list[instruments - 1] = NULL;
        } else {
            const unsigned copy_instruments = i + 1;

            if (copy_instruments < instruments) {
                memmove(instrument_list + i, instrument_list + copy_instruments, (instruments - copy_instruments) * sizeof(AVSequencerInstrument *));

                instrument_list[instruments - 1] = last_instrument;
            }

            module->instrument_list = instrument_list;
            module->instruments     = instruments;
        }
    }

    i = instrument->samples;

    while (i--) {
        AVSequencerSample *sample = instrument->sample_list[i];

        avseq_sample_close(instrument, sample);
        avseq_sample_destroy(sample);
    }
}

static const char *envelope_name(void *p)
{
    AVSequencerEnvelope *envelope = p;
    AVMetadataTag *tag            = av_metadata_get(envelope->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Envelope";
}

static const AVClass avseq_envelope_class = {
    "AVSequencer Envelope",
    envelope_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

#define CREATE_ENVELOPE(env_type) \
    static void create_##env_type##_envelope (AVSequencerContext *avctx,  \
                                              int16_t *data,              \
                                              uint32_t points,            \
                                              uint32_t scale,             \
                                              uint32_t scale_type,        \
                                              uint32_t y_offset)

CREATE_ENVELOPE(empty);
CREATE_ENVELOPE(sine);
CREATE_ENVELOPE(cosine);
CREATE_ENVELOPE(ramp);
CREATE_ENVELOPE(triangle);
CREATE_ENVELOPE(square);
CREATE_ENVELOPE(sawtooth);

static const void *create_env_lut[] = {
    create_empty_envelope,
    create_sine_envelope,
    create_cosine_envelope,
    create_ramp_envelope,
    create_triangle_envelope,
    create_square_envelope,
    create_sawtooth_envelope
};

AVSequencerEnvelope *avseq_envelope_create(void)
{
    return av_mallocz(sizeof(AVSequencerEnvelope) + FF_INPUT_BUFFER_PADDING_SIZE);
}

void avseq_envelope_destroy(AVSequencerEnvelope *envelope)
{
    if (envelope)
        av_metadata_free(&envelope->metadata);

    av_free(envelope);
}

int avseq_envelope_open(AVSequencerContext *avctx, AVSequencerModule *module,
                        AVSequencerEnvelope *envelope, uint32_t points,
                        uint32_t type, uint32_t scale,
                        uint32_t y_offset, uint32_t nodes)
{
    AVSequencerEnvelope **envelope_list;
    uint16_t envelopes;
    int res;

    if (!module)
        return AVERROR_INVALIDDATA;

    envelope_list = module->envelope_list;
    envelopes     = module->envelopes;

    if (!(envelope && ++envelopes)) {
        return AVERROR_INVALIDDATA;
    } else if (!(envelope_list = av_realloc(envelope_list, (envelopes * sizeof(AVSequencerEnvelope *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(module, AV_LOG_ERROR, "Cannot allocate envelope storage container.\n");
        return AVERROR(ENOMEM);
    }

    envelope->av_class  = &avseq_envelope_class;
    envelope->value_min = -scale;
    envelope->value_max = scale;
    envelope->tempo     = 1;

    if ((res = avseq_envelope_data_open(avctx, envelope, points, type, scale, y_offset, nodes)) < 0) {
        av_free(envelope_list);
        return res;
    }

    envelope_list[envelopes - 1] = envelope;
    module->envelope_list        = envelope_list;
    module->envelopes            = envelopes;

    return 0;
}

void avseq_envelope_close(AVSequencerModule *module, AVSequencerEnvelope *envelope)
{
    AVSequencerEnvelope **envelope_list;
    uint16_t envelopes, i;

    if (!(module && envelope))
        return;

    envelope_list = module->envelope_list;
    envelopes     = module->envelopes;

    for (i = 0; i < envelopes; ++i) {
        if (envelope_list[i] == envelope)
            break;
    }

    if (envelopes && (i != envelopes)) {
        AVSequencerEnvelope *last_envelope = envelope_list[--envelopes];
        uint16_t j;

        for (j = 0; i < module->instruments; ++j) {
            AVSequencerInstrument *instrument = module->instrument_list[j];
            unsigned smp;

            if (instrument->volume_env == envelope) {
                if (last_envelope != envelope)
                    instrument->volume_env = envelope_list[j + 1];
                else if (i)
                    instrument->volume_env = envelope_list[j - 1];
                else
                    instrument->volume_env = NULL;
            }

            if (instrument->panning_env == envelope) {
                if (last_envelope != envelope)
                    instrument->panning_env = envelope_list[j + 1];
                else if (i)
                    instrument->panning_env = envelope_list[j - 1];
                else
                    instrument->panning_env = NULL;
            }

            if (instrument->slide_env == envelope) {
                if (last_envelope != envelope)
                    instrument->slide_env = envelope_list[j + 1];
                else if (i)
                    instrument->slide_env = envelope_list[j - 1];
                else
                    instrument->slide_env = NULL;
            }

            if (instrument->vibrato_env == envelope) {
                if (last_envelope != envelope)
                    instrument->vibrato_env = envelope_list[j + 1];
                else if (i)
                    instrument->vibrato_env = envelope_list[j - 1];
                else
                    instrument->vibrato_env = NULL;
            }

            if (instrument->tremolo_env == envelope) {
                if (last_envelope != envelope)
                    instrument->tremolo_env = envelope_list[j + 1];
                else if (i)
                    instrument->tremolo_env = envelope_list[j - 1];
                else
                    instrument->tremolo_env = NULL;
            }

            if (instrument->pannolo_env == envelope) {
                if (last_envelope != envelope)
                    instrument->pannolo_env = envelope_list[j + 1];
                else if (i)
                    instrument->pannolo_env = envelope_list[j - 1];
                else
                    instrument->pannolo_env = NULL;
            }

            if (instrument->channolo_env == envelope) {
                if (last_envelope != envelope)
                    instrument->channolo_env = envelope_list[j + 1];
                else if (i)
                    instrument->channolo_env = envelope_list[j - 1];
                else
                    instrument->channolo_env = NULL;
            }

            if (instrument->spenolo_env == envelope) {
                if (last_envelope != envelope)
                    instrument->spenolo_env = envelope_list[j + 1];
                else if (i)
                    instrument->spenolo_env = envelope_list[j - 1];
                else
                    instrument->spenolo_env = NULL;
            }

            for (smp = 0; smp < instrument->samples; ++smp) {
                AVSequencerSample *sample = instrument->sample_list[smp];

                if (sample->auto_vibrato_env == envelope) {
                    if (last_envelope != envelope)
                        sample->auto_vibrato_env = envelope_list[i + 1];
                    else if (i)
                        sample->auto_vibrato_env = envelope_list[i - 1];
                    else
                        sample->auto_vibrato_env = NULL;
                }

                if (sample->auto_tremolo_env == envelope) {
                    if (last_envelope != envelope)
                        sample->auto_tremolo_env = envelope_list[i + 1];
                    else if (i)
                        sample->auto_tremolo_env = envelope_list[i - 1];
                    else
                        sample->auto_tremolo_env = NULL;
                }

                if (sample->auto_pannolo_env == envelope) {
                    if (last_envelope != envelope)
                        sample->auto_pannolo_env = envelope_list[i + 1];
                    else if (i)
                        sample->auto_pannolo_env = envelope_list[i - 1];
                    else
                        sample->auto_pannolo_env = NULL;
                }
            }
        }

        if (!envelopes) {
            av_freep(&module->envelope_list);

            module->envelopes = 0;
        } else if (!(envelope_list = av_realloc(envelope_list, (envelopes * sizeof(AVSequencerEnvelope *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            const unsigned copy_envelopes = i + 1;

            envelope_list = module->envelope_list;

            if (copy_envelopes < envelopes)
                memmove(envelope_list + i, envelope_list + copy_envelopes, (envelopes - copy_envelopes) * sizeof(AVSequencerEnvelope *));

            envelope_list[envelopes - 1] = NULL;
        } else {
            const unsigned copy_envelopes = i + 1;

            if (copy_envelopes < envelopes) {
                memmove(envelope_list + i, envelope_list + copy_envelopes, (envelopes - copy_envelopes) * sizeof(AVSequencerEnvelope *));

                envelope_list[envelopes - 1] = last_envelope;
            }

            module->envelope_list = envelope_list;
            module->envelopes     = envelopes;
        }
    }

    avseq_envelope_data_close(envelope);
}

int avseq_envelope_data_open(AVSequencerContext *avctx, AVSequencerEnvelope *envelope,
                             uint32_t points, uint32_t type, uint32_t scale,
                             uint32_t y_offset, uint32_t nodes)
{
    uint32_t scale_type;
    void (**create_env_func)(AVSequencerContext *avctx, int16_t *data, uint32_t points, uint32_t scale, uint32_t scale_type, uint32_t y_offset);
    int16_t *data;

    if (!envelope)
        return AVERROR_INVALIDDATA;

    if (!points)
        points = 64;

    if (points >= 0x10000)
        return AVERROR_INVALIDDATA;

    data = envelope->data;

    if (!(data = av_realloc(data, (points * sizeof(int16_t)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(envelope, AV_LOG_ERROR, "Cannot allocate envelope points.\n");
        return AVERROR(ENOMEM);
    }

    if (!type) {
        if (points > envelope->points)
            memset(data + envelope->points, 0, (points - envelope->points) * sizeof(int16_t));

        if (nodes) {
            uint16_t *node;
            uint16_t old_nodes;

            if (!nodes)
                nodes = 12;

            if (nodes == 1)
                nodes++;

            if (nodes >= 0x10000)
                return AVERROR_INVALIDDATA;

            old_nodes = envelope->nodes;
            node      = envelope->node_points;

            if (!(node = av_realloc(node, (nodes * sizeof(uint16_t)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
                av_free(data);
                av_log(envelope, AV_LOG_ERROR, "Cannot allocate envelope node data.\n");
                return AVERROR(ENOMEM);
            }

            envelope->node_points = node;
            envelope->nodes       = nodes;

            if (nodes > old_nodes) {
                uint32_t node_div, node_mod, value, count, i;

                if (old_nodes)
                    value = node[old_nodes - 1];
                else
                    value = 0;

                nodes -= old_nodes;
                count  = 0;

                if (nodes > points)
                    nodes = points;

                node    += old_nodes;
                node_div = points / nodes;
                node_mod = points % nodes;

                for (i = nodes; i > 0; i--) {
                    *node++ = value;
                    value  += node_div;
                    count  += node_mod;

                    if (count >= nodes) {
                        count -= nodes;
                        value++;
                    }
                }

                *--node = points - 1;
            } else {
                node[nodes - 1] = points - 1;
            }
        }
    } else {
        if (type > 7)
            type = 0;
        else
            type--;

        scale_type = scale & 0x80000000;
        scale     &= 0x7FFFFFFF;

        if (scale > 0x7FFF)
            scale = 0x7FFF;

        create_env_func = (void *) &(create_env_lut);
        create_env_func[type](avctx, data, points, scale, scale_type, y_offset);

        if (nodes) {
            uint32_t node_div, node_mod, value = 0, count = 0, i;
            uint16_t *node;

            if (!nodes)
                nodes = 12;

            if (nodes == 1)
                nodes++;

            if (nodes >= 0x10000)
                return AVERROR_INVALIDDATA;

            node = envelope->node_points;

            if (!(node = av_realloc(node, (nodes * sizeof(uint16_t)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
                av_free(data);
                av_log(envelope, AV_LOG_ERROR, "Cannot allocate envelope node data.\n");
                return AVERROR(ENOMEM);
            }

            envelope->node_points = node;
            envelope->nodes       = nodes;

            if (nodes > points)
                nodes = points;

            node_div = points / nodes;
            node_mod = points % nodes;

            for (i = nodes; i > 0; i--) {
                *node++ = value;
                value  += node_div;
                count  += node_mod;

                if (count >= nodes) {
                    count -= nodes;
                    value++;
                }
            }

            *--node = points - 1;
        }
    }

    envelope->data   = data;
    envelope->points = points;

    return 0;
}

void avseq_envelope_data_close(AVSequencerEnvelope *envelope)
{
    if (envelope) {
        av_freep(&envelope->node_points);
        av_freep(&envelope->data);

        envelope->nodes         = 0;
        envelope->points        = 0;
        envelope->sustain_start = 0;
        envelope->sustain_end   = 0;
        envelope->loop_start    = 0;
        envelope->loop_end      = 0;
    }
}

AVSequencerEnvelope *avseq_envelope_get_address(AVSequencerModule *module, uint32_t envelope)
{
    if (!(module && envelope))
        return NULL;

    if (envelope > module->envelopes)
        return NULL;

    return module->envelope_list[--envelope];
}

CREATE_ENVELOPE(empty)
{
    uint32_t i;

    for (i = points; i > 0; i--) {
        *data++ = y_offset;
    }
}

/** Sine table for very fast sine calculation. Value is
   sin(x)*32767 with one element being one degree.  */
static const int16_t sine_lut[] = {
         0,    571,   1143,   1714,   2285,   2855,   3425,   3993,   4560,   5125,   5689,   6252,  6812,    7370,   7927,  8480,
      9031,   9580,  10125,  10667,  11206,  11742,  12274,  12803,  13327,  13847,  14364,  14875,  15383,  15885,  16383,  16876,
     17363,  17846,  18323,  18794,  19259,  19719,  20173,  20620,  21062,  21497,  21925,  22347,  22761,  23169,  23570,  23964,
     24350,  24729,  25100,  25464,  25820,  26168,  26509,  26841,  27165,  27480,  27787,  28086,  28377,  28658,  28931,  29195,
     29450,  29696,  29934,  30162,  30381,  30590,  30790,  30981,  31163,  31335,  31497,  31650,  31793,  31927,  32050,  32164,
     32269,  32363,  32448,  32522,  32587,  32642,  32687,  32722,  32747,  32762,  32767,  32762,  32747,  32722,  32687,  32642,
     32587,  32522,  32448,  32363,  32269,  32164,  32050,  31927,  31793,  31650,  31497,  31335,  31163,  30981,  30790,  30590,
     30381,  30162,  29934,  29696,  29450,  29195,  28931,  28658,  28377,  28086,  27787,  27480,  27165,  26841,  26509,  26168,
     25820,  25464,  25100,  24729,  24350,  23964,  23570,  23169,  22761,  22347,  21925,  21497,  21062,  20620,  20173,  19719,
     19259,  18794,  18323,  17846,  17363,  16876,  16383,  15885,  15383,  14875,  14364,  13847,  13327,  12803,  12274,  11742,
     11206,  10667,  10125,   9580,   9031,   8480,   7927,   7370,   6812,   6252,   5689,   5125,   4560,   3993,   3425,   2855,
      2285,   1714,   1143,    571,      0,   -571,  -1143,  -1714,  -2285,  -2855,  -3425,  -3993,  -4560,  -5125,  -5689,  -6252,
     -6812,  -7370,  -7927,  -8480,  -9031,  -9580, -10125, -10667, -11206, -11742, -12274, -12803, -13327, -13847, -14364, -14875,
    -15383, -15885, -16383, -16876, -17363, -17846, -18323, -18794, -19259, -19719, -20173, -20620, -21062, -21497, -21925, -22347,
    -22761, -23169, -23570, -23964, -24350, -24729, -25100, -25464, -25820, -26168, -26509, -26841, -27165, -27480, -27787, -28086,
    -28377, -28658, -28931, -29195, -29450, -29696, -29934, -30162, -30381, -30590, -30790, -30981, -31163, -31335, -31497, -31650,
    -31793, -31927, -32050, -32164, -32269, -32363, -32448, -32522, -32587, -32642, -32687, -32722, -32747, -32762, -32767, -32762,
    -32747, -32722, -32687, -32642, -32587, -32522, -32448, -32363, -32269, -32164, -32050, -31927, -31793, -31650, -31497, -31335,
    -31163, -30981, -30790, -30590, -30381, -30162, -29934, -29696, -29450, -29195, -28931, -28658, -28377, -28086, -27787, -27480,
    -27165, -26841, -26509, -26168, -25820, -25464, -25100, -24729, -24350, -23964, -23570, -23169, -22761, -22347, -21925, -21497,
    -21062, -20620, -20173, -19719, -19259, -18794, -18323, -17846, -17363, -16876, -16383, -15885, -15383, -14875, -14364, -13847,
    -13327, -12803, -12274, -11742, -11206, -10667, -10125,  -9580,  -9031,  -8480,  -7927,  -7370,  -6812,  -6252,  -5689,  -5125,
     -4560,  -3993,  -3425,  -2855,  -2285,  -1714,  -1143,   -571
};

CREATE_ENVELOPE(sine)
{
    uint32_t i, sine_div, sine_mod, pos = 0, count = 0;
    int32_t value = 0;
    const int16_t *const lut = (avctx->sine_lut ? avctx->sine_lut : sine_lut);

    sine_div = 360 / points;
    sine_mod = 360 % points;

    for (i = points; i > 0; i--) {
        value = lut[pos];

        if (scale_type)
            value = -value;

        pos   += sine_div;
        value *= (int32_t) scale;
        value /= 32767;
        value += y_offset;
        count += sine_mod;

        if (count >= points) {
            count -= points;
            pos++;
        }

        *data++ = value;
    }
}

CREATE_ENVELOPE(cosine)
{
    uint32_t i, sine_div, sine_mod, count = 0;
    int32_t pos = 90, value = 0;
    const int16_t *const lut = (avctx->sine_lut ? avctx->sine_lut : sine_lut);

    sine_div = 360 / points;
    sine_mod = 360 % points;

    for (i = points; i > 0; i--) {
        value = lut[pos];

        if (scale_type)
            value = -value;

        if ((pos -= sine_div) < 0)
            pos += 360;

        value *= (int32_t) scale;
        value /= 32767;
        value += y_offset;
        count += sine_mod;

        if (count >= points) {
            count -= points;
            pos--;

            if (pos < 0)
                pos += 360;
        }

        *data++ = value;
    }
}

CREATE_ENVELOPE(ramp)
{
    uint32_t i, start_scale = -scale, ramp_points, scale_div, scale_mod, scale_count = 0, value;

    if (!(ramp_points = points >> 1))
        ramp_points = 1;

    scale_div = scale / ramp_points;
    scale_mod = scale % ramp_points;

    for (i = points; i > 0; i--) {
        value   = start_scale;
        start_scale += scale_div;
        scale_count += scale_mod;

        if (scale_count >= points) {
            scale_count -= points;
            start_scale++;
        }

        if (scale_type)
            value = -value;

        value      += y_offset;
        *data++     = value;
    }
}

CREATE_ENVELOPE(square)
{
    unsigned i;
    uint32_t j, value;

    if (scale_type)
        scale = -scale;

    for (i = 2; i > 0; i--) {
        scale = -scale;
        value = (scale + y_offset);

        for (j = points >> 1; j > 0; j--) {
            *data++ = value;
        }
    }
}

CREATE_ENVELOPE(triangle)
{
    uint32_t i, value, pos = 0, down_pos, triangle_points, scale_div, scale_mod, scale_count = 0;

    if (!(triangle_points = points >> 2))
        triangle_points = 1;

    scale_div = scale / triangle_points;
    scale_mod = scale % triangle_points;
    down_pos  = points - triangle_points;

    for (i = points; i > 0; i--) {
        value = pos;

        if (down_pos >= i) {
            if (down_pos == i) {
                scale_count += scale_mod;
                scale_div    = -scale_div;
            }

            if (triangle_points >= i) {
                scale_count += scale_mod;
                scale_div    = -scale_div;
                down_pos     = 0;
            }

            pos         += scale_div;
            scale_count += scale_mod;

            if (scale_count >= points) {
                scale_count -= points;
                pos--;
            }
        } else {
            pos         += scale_div;
            scale_count += scale_mod;

            if (scale_count >= points) {
                scale_count -= points;
                pos++;
            }
        }

        if (scale_type)
            value = -value;

        value  += y_offset;
        *data++ = value;
    }
}

CREATE_ENVELOPE(sawtooth)
{
    uint32_t i, value, pos = scale, down_pos, sawtooth_points, scale_div, scale_mod, scale_count = 0;

    down_pos = points >> 1;

    if (!(sawtooth_points = points >> 2))
        sawtooth_points = 1;

    scale_div = -(scale / sawtooth_points);
    scale_mod = scale % sawtooth_points;

    for (i = points; i > 0; i--) {
        value = pos;

        if (down_pos >= i) {
            if (down_pos == i) {
                scale_count += scale_mod;
                scale_div    = -scale_div;
            }

            pos         += scale_div;
            scale_count += scale_mod;

            if (scale_count >= points) {
                scale_count -= points;
                pos++;
            }
        } else {
            pos         += scale_div;
            scale_count += scale_mod;

            if (scale_count >= points) {
                scale_count -= points;
                pos--;
            }
        }

        if (scale_type)
            value = -value;

        value  += y_offset;
        *data++ = value;
    }
}

AVSequencerKeyboard *avseq_keyboard_create(void)
{
    return av_mallocz(sizeof(AVSequencerKeyboard) + FF_INPUT_BUFFER_PADDING_SIZE);
}

void avseq_keyboard_destroy(AVSequencerKeyboard *keyboard)
{
    av_free(keyboard);
}

int avseq_keyboard_open(AVSequencerModule *module, AVSequencerKeyboard *keyboard)
{
    AVSequencerKeyboard **keyboard_list;
    uint16_t keyboards;
    unsigned i;

    if (!module)
        return AVERROR_INVALIDDATA;

    keyboard_list = module->keyboard_list;
    keyboards     = module->keyboards;

    if (!(keyboard && ++keyboards)) {
        return AVERROR_INVALIDDATA;
    } else if (!(keyboard_list = av_realloc(keyboard_list, (keyboards * sizeof(AVSequencerKeyboard *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(module, AV_LOG_ERROR, "Cannot allocate keyboard definition list storage container.\n");
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < 120; ++i) {
        keyboard->key[i].sample = 0;
        keyboard->key[i].octave = i / 12;
        keyboard->key[i].note   = (i % 12) + 1;
    }

    keyboard_list[keyboards - 1] = keyboard;
    module->keyboard_list        = keyboard_list;
    module->keyboards            = keyboards;

    return 0;
}

void avseq_keyboard_close(AVSequencerModule *module, AVSequencerKeyboard *keyboard)
{
    AVSequencerKeyboard **keyboard_list;
    uint16_t keyboards, i;

    if (!(module && keyboard))
        return;

    keyboard_list = module->keyboard_list;
    keyboards     = module->keyboards;

    for (i = 0; i < keyboards; ++i) {
        if (keyboard_list[i] == keyboard)
            break;
    }

    if (keyboards && (i != keyboards)) {
        AVSequencerKeyboard *last_keyboard = keyboard_list[--keyboards];

        if (!keyboards) {
            av_freep(&module->keyboard_list);

            module->keyboards = 0;
        } else if (!(keyboard_list = av_realloc(keyboard_list, (keyboards * sizeof(AVSequencerKeyboard *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            const unsigned copy_keyboards = i + 1;

            keyboard_list = module->keyboard_list;

            if (copy_keyboards < keyboards)
                memmove(keyboard_list + i, keyboard_list + copy_keyboards, (keyboards - copy_keyboards) * sizeof(AVSequencerKeyboard *));

            keyboard_list[keyboards - 1] = NULL;
        } else {
            const unsigned copy_keyboards = i + 1;

            if (copy_keyboards < keyboards) {
                memmove(keyboard_list + i, keyboard_list + copy_keyboards, (keyboards - copy_keyboards) * sizeof(AVSequencerKeyboard *));

                keyboard_list[keyboards - 1] = last_keyboard;
            }

            module->keyboard_list = keyboard_list;
            module->keyboards     = keyboards;
        }
    }
}

AVSequencerKeyboard *avseq_keyboard_get_address(AVSequencerModule *module, uint32_t keyboard)
{
    if (!(module && keyboard))
        return NULL;

    if (keyboard > module->keyboards)
        return NULL;

    return module->keyboard_list[--keyboard];
}

static const char *arpeggio_name(void *p)
{
    AVSequencerArpeggio *arpeggio = p;
    AVMetadataTag *tag            = av_metadata_get(arpeggio->metadata, "title", NULL, AV_METADATA_IGNORE_SUFFIX);

    if (tag)
        return tag->value;

    return "AVSequencer Arpeggio";
}

static const AVClass avseq_arpeggio_class = {
    "AVSequencer Arpeggio",
    arpeggio_name,
    NULL,
    LIBAVUTIL_VERSION_INT,
};

AVSequencerArpeggio *avseq_arpeggio_create(void)
{
    return av_mallocz(sizeof(AVSequencerArpeggio) + FF_INPUT_BUFFER_PADDING_SIZE);
}

void avseq_arpeggio_destroy(AVSequencerArpeggio *arpeggio)
{
    if (arpeggio)
        av_metadata_free(&arpeggio->metadata);

    av_free(arpeggio);
}

int avseq_arpeggio_open(AVSequencerModule *module, AVSequencerArpeggio *arpeggio,
                        uint32_t entries)
{
    AVSequencerArpeggio **arpeggio_list;
    uint16_t arpeggios;
    int res;

    if (!module)
        return AVERROR_INVALIDDATA;

    arpeggio_list = module->arpeggio_list;
    arpeggios     = module->arpeggios;

    if (!(arpeggio && ++arpeggios)) {
        return AVERROR_INVALIDDATA;
    } else if (!(arpeggio_list = av_realloc(arpeggio_list, (arpeggios * sizeof(AVSequencerArpeggio *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(module, AV_LOG_ERROR, "Cannot allocate arpeggio structure storage container.\n");
        return AVERROR(ENOMEM);
    }

    arpeggio->av_class = &avseq_arpeggio_class;

    if ((res = avseq_arpeggio_data_open(arpeggio, entries)) < 0) {
        av_free(arpeggio_list);
        return res;
    }

    arpeggio_list[arpeggios - 1] = arpeggio;
    module->arpeggio_list        = arpeggio_list;
    module->arpeggios            = arpeggios;

    return 0;
}

void avseq_arpeggio_close(AVSequencerModule *module, AVSequencerArpeggio *arpeggio)
{
    AVSequencerArpeggio **arpeggio_list;
    uint16_t arpeggios, i;

    if (!(module && arpeggio))
        return;

    arpeggio_list = module->arpeggio_list;
    arpeggios     = module->arpeggios;

    for (i = 0; i < arpeggios; ++i) {
        if (arpeggio_list[i] == arpeggio)
            break;
    }

    if (arpeggios && (i != arpeggios)) {
        AVSequencerArpeggio *last_arpeggio = arpeggio_list[--arpeggios];

        if (!arpeggios) {
            av_freep(&module->arpeggio_list);

            module->arpeggios = 0;
        } else if (!(arpeggio_list = av_realloc(arpeggio_list, (arpeggios * sizeof(AVSequencerEnvelope *)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
            const unsigned copy_arpeggios = i + 1;

            arpeggio_list = module->arpeggio_list;

            if (copy_arpeggios < arpeggios)
                memmove(arpeggio_list + i, arpeggio_list + copy_arpeggios, (arpeggios - copy_arpeggios) * sizeof(AVSequencerArpeggio *));

            arpeggio_list[arpeggios - 1] = NULL;
        } else {
            const unsigned copy_arpeggios = i + 1;

            if (copy_arpeggios < arpeggios) {
                memmove(arpeggio_list + i, arpeggio_list + copy_arpeggios, (arpeggios - copy_arpeggios) * sizeof(AVSequencerArpeggio *));

                arpeggio_list[arpeggios - 1] = last_arpeggio;
            }

            module->arpeggio_list = arpeggio_list;
            module->arpeggios     = arpeggios;
        }
    }

    avseq_arpeggio_data_close(arpeggio);
}

int avseq_arpeggio_data_open(AVSequencerArpeggio *arpeggio, uint32_t entries)
{
    AVSequencerArpeggioData *data;

    if (!arpeggio)
        return AVERROR_INVALIDDATA;

    data = arpeggio->data;

    if (!entries)
        entries = 3;

    if (entries >= 0x10000) {
        return AVERROR_INVALIDDATA;
    } else if (!(data = av_realloc(data, (entries * sizeof(AVSequencerArpeggioData)) + FF_INPUT_BUFFER_PADDING_SIZE))) {
        av_log(arpeggio, AV_LOG_ERROR, "Cannot allocate arpeggio structure data.\n");
        return AVERROR(ENOMEM);
    } else if (entries > arpeggio->entries) {
        memset(data + arpeggio->entries, 0, (entries - arpeggio->entries) * sizeof(AVSequencerArpeggioData));
    } else if (!arpeggio->data) {
        memset(data, 0, entries * sizeof(AVSequencerArpeggioData));
    }

    arpeggio->data    = data;
    arpeggio->entries = entries;

    return 0;
}

void avseq_arpeggio_data_close(AVSequencerArpeggio *arpeggio)
{
    if (arpeggio) {
        av_freep(&arpeggio->data);

        arpeggio->entries       = 0;
        arpeggio->sustain_start = 0;
        arpeggio->sustain_end   = 0;
        arpeggio->loop_start    = 0;
        arpeggio->loop_end      = 0;
    }
}

AVSequencerArpeggio *avseq_arpeggio_get_address(AVSequencerModule *module, uint32_t arpeggio)
{
    if (!(module && arpeggio))
        return NULL;

    if (arpeggio > module->arpeggios)
        return NULL;

    return module->arpeggio_list[--arpeggio];
}
