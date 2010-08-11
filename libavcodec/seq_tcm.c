/*
 * IFF-TCM1 audio sequencer decoder
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
 * IFF-TCM1 audio sequencer decoder.
 */

#include "libavutil/log.h"
#include "libavsequencer/avsequencer.h"
#include "avcodec.h"

/** decoder context */
typedef struct IFFTCM1Context {
    AVSequencerContext *avctx;
} IFFTCM1Context;

#include "libavutil/avstring.h"
#include "libavsequencer/avsequencer.h"

/** decode a frame */
static int iff_tcm1_decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                                 AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    IFFTCM1Context *iff_tcm1 = avctx->priv_data;
    int16_t *out_data = data;
    int consumed = buf_size;
    const uint8_t *buf_end = buf + buf_size;

    if((*data_size >> 2) < buf_size)
        return -1;

    if(avctx->frame_number == 0) {
//        iff_tcm1->fib_acc = buf[1] << 8;
        buf_size -= 2;
        buf += 2;
    }

    *data_size = buf_size << 2;

    while(buf < buf_end) {
        uint8_t d = *buf++;
//        iff_tcm1->fib_acc += iff_tcm1->table[d & 0x0f];
//        *out_data++ = iff_tcm1->fib_acc;
//        iff_tcm1->fib_acc += iff_tcm1->table[d >> 4];
//        *out_data++ = iff_tcm1->fib_acc;
    }

    return consumed;
}

/** initialize IFF-TCM1 decoder */
static av_cold int iff_tcm1_decode_init(AVCodecContext *avctx)
{
    IFFTCM1Context *iff_tcm1 = avctx->priv_data;

    switch(avctx->codec->id) {
        case CODEC_ID_IFF_TCM1:
          // TODO: Sequencer to audio conversation
          break;
        case CODEC_ID_SEQ_TCM1:
          // TODO: Sequencer to sequencer conversation
          break;
        default:
          return -1;
    }
    avctx->sample_fmt = SAMPLE_FMT_S32;
    return 0;
}

AVCodec iff_tcm1_decoder = {
  .name           = "iff_tcm1",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_IFF_TCM1,
  .priv_data_size = sizeof (IFFTCM1Context),
  .init           = iff_tcm1_decode_init,
  .decode         = iff_tcm1_decode_frame,
  .long_name      = NULL_IF_CONFIG_SMALL("IFF-TCM1 audio"),
};

AVCodec seq_tcm1_decoder = {
  .name           = "seq_tcm1",
//  .type           = AVMEDIA_TYPE_SEQUENCER,
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_SEQ_TCM1,
  .priv_data_size = sizeof (IFFTCM1Context),
  .init           = iff_tcm1_decode_init,
  .decode         = iff_tcm1_decode_frame,
  .long_name      = NULL_IF_CONFIG_SMALL("IFF-TCM1 sequencer"),
};
