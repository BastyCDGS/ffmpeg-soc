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

#include "libavsequencer/api.h"

int avseq_instrument_open(AVSequencerModule *module, AVSequencerInstrument *instrument) {
    AVSequencerSample *sample;
    AVSequencerInstrument **instrument_list = module->instrument_list;
    uint16_t instruments                    = module->instruments;
    int res;

    if (!instrument || !++instruments) {
        return AVERROR_INVALIDDATA;
    } else if (!(instrument_list = av_realloc(instrument_list, instruments * sizeof(AVSequencerInstrument *)))) {
        av_log(instrument, AV_LOG_ERROR, "cannot allocate instrument storage container.\n");
        return AVERROR(ENOMEM);
    } else if (!(sample = avseq_sample_create())) {
        av_free(instrument_list);
        av_log(instrument, AV_LOG_ERROR, "cannot allocate first sample.\n");
        return AVERROR(ENOMEM);
    }

    instrument->fade_out         = 65535;
    instrument->pitch_pan_center = 4*12; // C-4
    instrument->global_volume    = 255;
    instrument->default_panning  = -128;
    instrument->env_usage_flags  = ~(AVSEQ_INSTRUMENT_FLAG_USE_VOLUME_ENV|AVSEQ_INSTRUMENT_FLAG_USE_PANNING_ENV|AVSEQ_INSTRUMENT_FLAG_USE_SLIDE_ENV|-0x2000);

    if ((res = avseq_sample_open(instrument, sample)) < 0) {
        av_free(sample);
        av_free(instrument_list);
        return res;
    }

    instrument_list[instruments] = instrument;
    module->instrument_list      = instrument_list;
    module->instruments          = instruments;

    return 0;
}

#define CREATE_ENVELOPE(env_type) \
    static void *create_##env_type##_envelope ( AVSequencerContext *avctx, \
                                                int16_t *data, \
                                                uint32_t points, \
                                                uint32_t scale, \
                                                uint32_t scale_type, \
                                                uint32_t y_offset )

CREATE_ENVELOPE(empty);
CREATE_ENVELOPE(sine);
CREATE_ENVELOPE(cosine);
CREATE_ENVELOPE(ramp);
CREATE_ENVELOPE(triangle);
CREATE_ENVELOPE(square);
CREATE_ENVELOPE(sawtooth);

static const void *create_envelope_lut[] = {
    create_empty_envelope,
    create_sine_envelope,
    create_cosine_envelope,
    create_ramp_envelope,
    create_triangle_envelope,
    create_square_envelope,
    create_sawtooth_envelope
};

int avseq_envelope_open(AVSequencerContext *avctx, AVSequencerModule *module,
                        AVSequencerEnvelope *envelope, uint32_t points,
                        uint32_t type, uint32_t scale,
                        uint32_t y_offset, uint32_t nodes) {
    AVSequencerEnvelope **envelope_list = module->envelope_list;
    uint16_t envelopes                  = module->envelopes;
    int res;

    if (!envelope || !++envelopes) {
        return AVERROR_INVALIDDATA;
    } else if (!(envelope_list = av_realloc(envelope_list, envelopes * sizeof(AVSequencerEnvelope *)))) {
        av_log(envelope, AV_LOG_ERROR, "cannot allocate envelope storage container.\n");
        return AVERROR(ENOMEM);
    }

    envelope->value_min = -scale;
    envelope->value_max = scale;
    envelope->tempo     = 1;

    if ((res = avseq_envelope_data_open(avctx, envelope, points, type, scale, y_offset, nodes)) < 0) {
        av_free(envelope_list);
        return res;
    }

    envelope_list[envelopes] = envelope;
    module->envelope_list    = envelope_list;
    module->envelopes        = envelopes;

    return 0;
}

int avseq_envelope_data_open(AVSequencerContext *avctx, AVSequencerEnvelope *envelope,
                             uint32_t points, uint32_t type, uint32_t scale,
                             uint32_t y_offset, uint32_t nodes) {
    uint32_t scale_type;
    void (**create_env_func)( AVSequencerContext *avctx, int16_t *data, uint32_t points, uint32_t scale, uint32_t scale_type, uint32_t y_offset );
    int16_t *data = envelope->data;

    if (!envelope) {
        return AVERROR_INVALIDDATA;
    } else if (!(data = av_realloc(data, points * sizeof(int16_t)))) {
        av_log(envelope, AV_LOG_ERROR, "cannot allocate envelope points.\n");
        return AVERROR(ENOMEM);
    }

    if (!points)
        points = 64;

    if (points >= 0x10000)
        return AVERROR_INVALIDDATA;

    scale_type = scale & 0x80000000;
    scale     &= 0x7FFFFFFF;

    if (scale > 0x7FFF)
        scale = 0x7FFF;

    if (type > 6)
        type = 0;

    create_env_func = (void *) &(create_env_lut);
    create_env_func[type] ( avctx, data, points, scale, scale_type, y_offset );

    if (nodes) {
        uint32_t node_div, node_mod, value = 0, count = 0, i;
        uint16_t *node;

        if (!nodes)
            nodes = 12;

        if (nodes == 1)
            nodes++;

        if (!(node = (uint16_t *) av_realloc (nodes * sizeof (uint16_t)))) {
            av_free(data);
            av_log(envelope, AV_LOG_ERROR, "cannot allocate envelope node data.\n");
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

            value += node_div;
            count += node_mod;

            if (count >= nodes) {
                count -= nodes;
                value++;
            }
        }

        *--node = points - 1;
    }

    envelope->data   = data;
    envelope->points = points;

    return 0;
}

CREATE_ENVELOPE(empty) {
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

CREATE_ENVELOPE(sine) {
    uint32_t i, sine_div, sine_mod, pos = 0, count = 0;
    int32_t value = 0;
    int16_t *sine_lut = (avctx->sine_lut ? avctx->sine_lut : sine_lut);

    sine_div = 360 / points;
    sine_mod = 360 % points;

    for (i = points; i > 0; i--) {
        value = sine_lut[pos];

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

CREATE_ENVELOPE(cosine) {
    uint32_t i, sine_div, sine_mod, count = 0;
    int32_t pos = 90, value = 0;
    int16_t *sine_lut = (avctx->sine_lut ? avctx->sine_lut : sine_lut);

    sine_div = 360 / points;
    sine_mod = 360 % points;

    for (i = points; i > 0; i--) {
        value = sine_lut[pos];

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

CREATE_ENVELOPE(ramp) {
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

CREATE_ENVELOPE(square) {
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

CREATE_ENVELOPE(triangle) {
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

CREATE_ENVELOPE(sawtooth) {
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

int avseq_keyboard_open(AVSequencerModule *module, AVSequencerKeyboard *keyboard) {
    AVSequencerKeyboard **keyboard_list = module->keyboard_list;
    uint16_t keyboards                  = module->keyboards;
    unsigned i;
    int res;

    if (!keyboard || !++keyboards) {
        return AVERROR_INVALIDDATA;
    } else if (!(keyboard_list = av_realloc(keyboard_list, instruments * sizeof(AVSequencerKeyboard *)))) {
        av_log(keyboard, AV_LOG_ERROR, "cannot allocate keyboard definition list storage container.\n");
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < 120; ++i) {
        keyboard->key[i].sample = 0;
        keyboard->key[i].octave = i / 12;
        keyboard->key[i].note   = (i % 12) + 1;
    }

    keyboard_list[keyboards] = keyboard;
    module->keyboard_list    = keyboard_list;
    module->keyboards        = keyboards;

    return 0;
}

int avseq_arpeggio_open(AVSequencerModule *module, AVSequencerApreggio *arpeggio,
                        uint32_t entries) {
    AVSequencerArpeggio **arpeggio_list = module->arpeggio_list;
    uint16_t arpeggios                  = module->arpeggios;
    int res;

    if (!arpeggio || !++arpeggios) {
        return AVERROR_INVALIDDATA;
    } else if (!(arpeggio_list = av_realloc(arpeggio_list, envelopes * sizeof(AVSequencerArpeggio *)))) {
        av_log(arpeggio, AV_LOG_ERROR, "cannot allocate arpeggio structure storage container.\n");
        return AVERROR(ENOMEM);
    }

    if ((res = avseq_arpeggio_data_open(arpeggio, entries)) < 0) {
        av_free(arpeggio_list);
        return res;
    }

    arpeggio_list[envelopes] = arpeggio;
    module->arpeggio_list    = arpeggio_list;
    module->arpeggios        = arpeggios;

    return 0;
}

int avseq_arpeggio_data_open(AVSequencerApreggio *arpeggio, uint32_t entries) {
    int16_t *data = arpeggio->data;

    if (!entries)
        entries = 3;

    if (!arpeggio || entries >= 0x10000) {
        return AVERROR_INVALIDDATA;
    } else if (!(data = av_realloc(data, entries * sizeof(AVSequencerArpeggioData)))) {
        av_log(arpeggio, AV_LOG_ERROR, "cannot allocate arpeggio structure data.\n");
        return AVERROR(ENOMEM);
    }

    arpeggio->data    = data;
    arpeggio->entries = entries;

    return 0;
}
