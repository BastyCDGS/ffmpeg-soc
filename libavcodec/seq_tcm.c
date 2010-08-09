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

/* Metadata string read */
static int get_metadata (AVCodecContext *avctx, AVMetadata *const metadata, const char *const tag, const unsigned data_size);
static int open_tcm1_song ( AVCodecContext *avctx, AVSequencerModule *module, uint32_t data_size );
static int open_song_patt ( AVCodecContext *avctx, AVSequencerSong *song, uint32_t data_size );
static int open_patt_trak ( AVCodecContext *avctx, AVSequencerSong *song, uint32_t data_size );
static int open_song_posi ( AVCodecContext *avctx, AVSequencerSong *song, uint32_t data_size );
static int open_posi_post ( AVCodecContext *avctx, AVSequencerSong *song, uint32_t channel, uint32_t data_size );
static int open_post_posl ( AVCodecContext *avctx, AVSequencerOrderList *order_list, uint32_t data_size );

static int open_tcm1_insl ( AVCodecContext *avctx, AVSequencerModule *module, const char *args, void *opaque, uint32_t data_size );
static int open_insl_inst ( AVCodecContext *avctx, AVSequencerModule *module, const char *args, void *opaque, uint32_t data_size );
static int open_inst_samp ( AVCodecContext *avctx, AVSequencerModule *module, AVSequencerInstrument *instrument, const char *args, void *opaque, uint32_t data_size );
static int open_samp_smpl ( AVCodecContext *avctx, AVSequencerModule *module, AVSequencerInstrument *instrument, const char *args, void *opaque, uint32_t data_size );
static int open_smpl_snth ( AVCodecContext *avctx, AVSequencerSample *sample, const char *args, void *opaque, uint32_t data_size );
static int open_snth_wfrm ( AVCodecContext *avctx, AVSequencerSynth *synth, uint32_t data_size );
static int open_wfrm_wave ( AVCodecContext *avctx, AVSequencerSynth *synth, uint32_t data_size );
static int open_snth_stab ( AVCodecContext *avctx, AVSequencerSynth *synth, uint32_t data_size );
static int open_stab_smbl ( AVCodecContext *avctx, AVSequencerSynth *synth, uint32_t data_size );

static int open_tcm1_envl ( AVCodecContext *avctx, AVSequencerContext *seqctx, AVSequencerModule *module, uint32_t data_size );
static int open_envl_envd ( AVCodecContext *avctx, AVSequencerContext *seqctx, AVSequencerModule *module, uint32_t data_size );

static int open_tcm1_keyb ( AVCodecContext *avctx, AVSequencerModule *module, uint32_t data_size );

static int open_tcm1_arpl ( AVCodecContext *avctx, AVSequencerModule *module, uint32_t data_size );
static int open_arpl_arpg ( AVCodecContext *avctx, AVSequencerModule *module, uint32_t data_size );
static int open_arpg_arpe ( AVCodecContext *avctx, AVSequencerArpeggio *arpeggio, uint32_t data_size );

#define ID_AHDR       MKTAG('A','H','D','R')
#define ID_ARPE       MKTAG('A','R','P','E')
#define ID_ARPG       MKTAG('A','R','P','G')
#define ID_ARPL       MKTAG('A','R','P','L')
#define ID_CMNT       MKTAG('C','M','N','T')
#define ID_CODE       MKTAG('C','O','D','E')
#define ID_EHDR       MKTAG('E','H','D','R')
#define ID_ENVD       MKTAG('E','N','V','D')
#define ID_ENVL       MKTAG('E','N','V','L')
#define ID_FILE       MKTAG('F','I','L','E')
#define ID_IHDR       MKTAG('I','H','D','R')
#define ID_INSL       MKTAG('I','N','S','L')
#define ID_INST       MKTAG('I','N','S','T')
#define ID_KBRD       MKTAG('K','B','R','D')
#define ID_KEYB       MKTAG('K','E','Y','B')
#define ID_MHDR       MKTAG('M','H','D','R')
#define ID_MMSG       MKTAG('M','M','S','G')
#define ID_NODE       MKTAG('N','O','D','E')
#define ID_PATT       MKTAG('P','A','T','T')
#define ID_PDAT       MKTAG('P','D','A','T')
#define ID_PHDR       MKTAG('P','H','D','R')
#define ID_POSI       MKTAG('P','O','S','I')
#define ID_POSL       MKTAG('P','O','S','L')
#define ID_SAMP       MKTAG('S','A','M','P')
#define ID_SHDR       MKTAG('S','H','D','R')
#define ID_SMBL       MKTAG('S','M','B','L')
#define ID_SMPH       MKTAG('S','M','P','H')
#define ID_SMPL       MKTAG('S','M','P','L')
#define ID_SMPR       MKTAG('S','M','P','R')
#define ID_SMSG       MKTAG('S','M','S','G')
#define ID_SNTH       MKTAG('S','N','T','H')
#define ID_SONG       MKTAG('S','O','N','G')
#define ID_SREF       MKTAG('S','R','E','F')
#define ID_STAB       MKTAG('S','T','A','B')
#define ID_STIL       MKTAG('S','T','I','L')
#define ID_TCM1       MKTAG('T','C','M','1')
#define ID_THDR       MKTAG('T','H','D','R')
#define ID_TRAK       MKTAG('T','R','A','K')
#define ID_WAVE       MKTAG('W','A','V','E')
#define ID_WFRM       MKTAG('W','F','R','M')
#define ID_WHDR       MKTAG('W','H','D','R')
#define ID_YHDR       MKTAG('Y','H','D','R')

#define ID_FORM       MKTAG('F','O','R','M')
#define ID_ANNO       MKTAG('A','N','N','O')
#define ID_AUTH       MKTAG('A','U','T','H')
#define ID_CHRS       MKTAG('C','H','R','S')
#define ID_COPYRIGHT  MKTAG('(','c',')',' ')
#define ID_CSET       MKTAG('C','S','E','T')
#define ID_FVER       MKTAG('F','V','E','R')
#define ID_NAME       MKTAG('N','A','M','E')
#define ID_TEXT       MKTAG('T','E','X','T')
#define ID_BODY       MKTAG('B','O','D','Y')
#define ID_ANNO       MKTAG('A','N','N','O')

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
        iff_tcm1->fib_acc = buf[1] << 8;
        buf_size -= 2;
        buf += 2;
    }

    *data_size = buf_size << 2;

    while(buf < buf_end) {
        uint8_t d = *buf++;
        iff_tcm1->fib_acc += iff_tcm1->table[d & 0x0f];
        *out_data++ = iff_tcm1->fib_acc;
        iff_tcm1->fib_acc += iff_tcm1->table[d >> 4];
        *out_data++ = iff_tcm1->fib_acc;
    }

    return consumed;
}

/** initialize 8svx decoder */
static av_cold int iff_tcm1_decode_init(AVCodecContext *avctx)
{
    IFFTCM1Context *iff_tcm1 = avctx->priv_data;

    switch(avctx->codec->id) {
        case CODEC_ID_IFF_TCM1:
          iff_tcm1->table = fibonacci;
          break;
        case CODEC_ID_SEQ_TCM1:
          iff_tcm1->table = exponential;
          break;
        default:
          return -1;
    }
    avctx->sample_fmt = SAMPLE_FMT_S16;
    return 0;
}

AVCodec iff_tcm1_audio_decoder = {
  .name           = "iff_tcm1",
  .type           = AVMEDIA_TYPE_AUDIO,
  .id             = CODEC_ID_IFF_TCM1,
  .priv_data_size = sizeof (IFFTCM1Context),
  .init           = iff_tcm1_decode_init,
  .decode         = iff_tcm1_decode_frame,
  .long_name      = NULL_IF_CONFIG_SMALL("IFF-TCM1 audio"),
};

AVCodec iff_tcm1_seq_decoder = {
  .name           = "seq_tcm1",
  .type           = AVMEDIA_TYPE_SEQUENCER,
  .id             = CODEC_ID_SEQ_TCM1,
  .priv_data_size = sizeof (IFFTCM1Context),
  .init           = iff_tcm1_decode_init,
  .decode         = iff_tcm1_decode_frame,
  .long_name      = NULL_IF_CONFIG_SMALL("IFF-TCM1 sequencer"),
};

/* Metadata string read */
static int get_metadata(AVCodecContext *avctx,
                            AVMetadata *metadata,
                            const char *const tag,
                            const unsigned data_size) {
    uint8_t *buf = ((data_size + 1) == 0) ? NULL : av_malloc(data_size + 1);

    if (!buf)
        return AVERROR(ENOMEM);

    if (get_buffer(avctx->pb, buf, data_size) < 0) {
        av_free(buf);
        return AVERROR(EIO);
    }
    buf[data_size] = 0;
    av_metadata_set2(&metadata, tag, buf, AV_METADATA_DONT_STRDUP_VAL);
    return 0;
}

static int open_tcm1_song ( AVCodecContext *avctx, AVSequencerModule *module, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerSong *song;
    uint32_t iff_size;
    uint16_t tracks = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(song = avseq_song_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_song_open ( module, song )) < 0) {
        av_free(song);
        return res;
    }

    avseq_song_set_channels ( song, 1 );

    while (!url_feof(pb) && (data_size -= iff_size)) {
        unsigned year, month, day, hour, min, sec, ms;
        uint8_t *buf;
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_SHDR:
            day   = get_byte(pb);
            month = get_byte(pb);
            year  = get_be16(pb);
            hour  = get_byte(pb);
            min   = get_byte(pb);
            sec   = get_byte(pb);
            ms    = get_byte(pb);

            if (month == 0 || month > 12 || day == 0 || day > 7 || hour > 23 || min > 59 || sec > 59 || ms > 999) {
                av_log(song, AV_LOG_ERROR, "Invalid begin composing date: %04d-%02d,%02d %02d:%02d:%02d.%03d\n", year, month, day, hour, min, sec, ms);
                return AVERROR_INVALIDDATA;
            }

            if (!(buf = av_malloc(24)))
                return AVERROR(ENOMEM);

            av_strlcatf ( buf, 24, "%04d-%02d,%02d %02d:%02d:%02d.%03d", year, month, day, hour, min, sec, ms );
            av_metadata_set2(&song->metadata, "begin_date", buf, AV_METADATA_DONT_STRDUP_VAL);

            day   = get_byte(pb);
            month = get_byte(pb);
            year  = get_be16(pb);
            hour  = get_byte(pb);
            min   = get_byte(pb);
            sec   = get_byte(pb);
            ms    = get_byte(pb);

            if (month == 0 || month > 12 || day == 0 || day > 7 || hour > 23 || min > 59 || sec > 59 || ms > 999) {
                av_log(song, AV_LOG_ERROR, "Invalid finish composing date: %04d-%02d,%02d %02d:%02d:%02d.%03d\n", year, month, day, hour, min, sec, ms);
                return AVERROR_INVALIDDATA;
            }

            if (!(buf = av_malloc(24)))
                return AVERROR(ENOMEM);

            av_strlcatf ( buf, 24, "%04d-%02d,%02d %02d:%02d:%02d.%03d", year, month, day, hour, min, sec, ms );
            av_metadata_set2(&song->metadata, "end_date", buf, AV_METADATA_DONT_STRDUP_VAL);

            if (!(buf = av_malloc(5)))
                return AVERROR(ENOMEM);

            av_strlcatf ( buf, 5, "%d", year );
            av_metadata_set2(&song->metadata, "date", buf, AV_METADATA_DONT_STRDUP_VAL);

            hour = get_byte(pb);
            min  = get_byte(pb);
            sec  = get_byte(pb);
            ms   = get_byte(pb);

            if (min > 59 || sec > 59 || ms > 999) {
                av_log(song, AV_LOG_ERROR, "Invalid duration: %02d:%02d:%02d.%03d\n", hour, min, sec, ms);
                return AVERROR_INVALIDDATA;
            }

            song->duration           = (((uint64_t) (hour*3600000+min*60000+sec*1000+ms))*AV_TIME_BASE) / 1000;
            tracks                   = get_be16(pb);
            song->gosub_stack_size   = get_be16(pb);
            song->loop_stack_size    = get_be16(pb);
            song->compat_flags       = get_byte(pb);
            song->flags              = get_byte(pb);
            song->channels           = get_be16(pb);
            song->frames             = get_be16(pb);
            song->speed_mul          = get_byte(pb);
            song->speed_div          = get_byte(pb);
            song->spd_speed          = get_be16(pb);
            song->bpm_tempo          = get_be16(pb);
            song->bpm_speed          = get_be16(pb);
            song->frames_min         = get_be16(pb);
            song->frames_max         = get_be16(pb);
            song->spd_min            = get_be16(pb);
            song->spd_max            = get_be16(pb);
            song->bpm_tempo_min      = get_be16(pb);
            song->bpm_tempo_max      = get_be16(pb);
            song->bpm_speed_min      = get_be16(pb);
            song->bpm_speed_max      = get_be16(pb);
            song->global_volume      = get_byte(pb);
            song->global_sub_volume  = get_byte(pb);
            song->global_panning     = get_byte(pb);
            song->global_sub_panning = get_byte(pb);

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_PATT:
                if ((res = open_song_patt ( s, song, get_be32(pb))) < 0)
                    return res;

                break;
            case ID_POSI:
                if ((res = open_song_posi ( s, song, get_be32(pb))) < 0)
                    return res;

                break;
            default:
                // TODO: Add unknown chunk

                break;
            }

            break;
        case ID_ANNO:
        case ID_CMNT:
        case ID_SMSG:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_STIL:
            metadata_tag = "genre";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, song->metadata, metadata_tag, data_size)) < 0) {
                av_log(song, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (tracks != song->tracks) {
        av_log(song, AV_LOG_ERROR, "Number of attached tracks does not match actual reads!");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int open_song_patt ( AVCodecContext *avctx, AVSequencerSong *song, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_TRAK:
                if ((res = open_patt_trak ( s, song, get_be32(pb))) < 0)
                    return res;

                break;
            }

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_patt_trak ( AVCodecContext *avctx, AVSequencerSong *song, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerTrack *track;
    uint8_t *buf = NULL;
    uint32_t len, iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(track = avseq_track_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_track_open ( song, track )) < 0) {
        av_free(track);
        return res;
    }

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_THDR:
            track->last_row     = get_be16(pb);
            track->volume       = get_byte(pb);
            track->sub_volume   = get_byte(pb);
            track->panning      = get_byte(pb);
            track->sub_panning  = get_byte(pb);
            track->transpose    = get_byte(pb);
            track->compat_flags = get_byte(pb);
            track->flags        = get_be16(pb);
            track->frames       = get_be16(pb);
            track->speed_mul    = get_byte(pb);
            track->speed_div    = get_byte(pb);
            track->spd_speed    = get_be16(pb);
            track->bpm_tempo    = get_be16(pb);
            track->bpm_speed    = get_be16(pb);

            break;
        case ID_BODY:
            len = iff_size;
            buf = av_malloc(iff_size);

            if (!buf)
                return AVERROR(ENOMEM);

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(buf);
                return AVERROR(EIO);
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, track->metadata, metadata_tag, data_size)) < 0) {
                av_freep(buf);
                av_log(track, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if ((res = avseq_track_data_open ( track )) < 0) {
        av_freep(buf);
        return res;
    }

    if ((res = avseq_track_unpack ( track, buf, len )) < 0) {
        av_freep(buf);
        return res;
    }

    av_freep(buf);

    return 0;
}

static int open_song_posi ( AVCodecContext *avctx, AVSequencerSong *song, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    uint32_t iff_size, channel = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_POSI:
                if ((res = open_posi_post ( s, song, channel, get_be32(pb))) < 0)
                    return res;

                if (channel++ >= song->channels) {
                    if ((res = avseq_song_set_channels ( song, channel )) < 0)
                        return res;
                }

                break;
            }

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_posi_post ( AVCodecContext *avctx, AVSequencerSong *song, uint32_t channel, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerOrderList *order_list = song->order_list + channel;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_PHDR:
            order_list->length              = get_be16(pb);
            order_list->rep_start           = get_be16(pb);
            order_list->volume              = get_byte(pb);
            order_list->sub_volume          = get_byte(pb);
            order_list->track_panning       = get_byte(pb);
            order_list->track_sub_panning   = get_byte(pb);
            order_list->channel_panning     = get_byte(pb);
            order_list->channel_sub_panning = get_byte(pb);

            if (get_byte(pb)) // compatibility flags
                return AVERROR_INVALIDDATA;

            order_list->flags               = get_byte(pb);

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_POSL:
                if ((res = open_post_posl ( s, order_list, get_be32(pb))) < 0)
                    return res;

                break;
            default:
                // TODO: Add unknown chunk

                break;
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, order_list->metadata, metadata_tag, data_size)) < 0) {
                av_log(order_list, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_post_posl ( AVCodecContext *avctx, AVSequencerOrderList *order_list, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerOrderData *order_data;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_PDAT:
            if (!(order_data = avseq_order_data_create()))
                return AVERROR(ENOMEM);

            if ((res = avseq_order_data_open(order_list, order_data)) < 0) {
                av_free(order_data);
                return res;
            }

            order_data->track           = (AVSequencerTrack *) get_be16(pb);
            order_data->next_pos        = (AVSequencerOrderData *) get_be16(pb);
            order_data->prev_pos        = (AVSequencerOrderData *) get_be16(pb);
            order_data->next_row        = get_be16(pb);
            order_data->prev_row        = get_be16(pb);
            order_data->first_row       = get_be16(pb);
            order_data->last_row        = get_be16(pb);
            order_data->flags           = get_byte(pb);
            order_data->transpose       = get_byte(pb);
            order_data->instr_transpose = get_be16(pb);
            order_data->tempo           = get_be16(pb);
            order_data->volume          = get_byte(pb);
            order_data->sub_volume      = get_byte(pb);

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, order_data->metadata, metadata_tag, data_size)) < 0) {
                av_log(order_data, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_tcm1_insl ( AVCodecContext *avctx, AVSequencerModule *module, const char *args, void *opaque, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_INST:
                if ((res = open_insl_inst ( s, module, args, opaque, get_be32(pb))) < 0)
                    return res;

                break;
            }

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_insl_inst ( AVCodecContext *avctx, AVSequencerModule *module, const char *args, void *opaque, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerInstrument *instrument;
    uint32_t iff_size;
    uint8_t samples = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(instrument = avseq_instrument_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_instrument_open ( module, instrument )) < 0) {
        av_free(instrument);
        return res;
    }

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_IHDR:
            instrument->volume_env           = (AVSequencerEnvelope *) get_be16(pb);
            instrument->panning_env          = (AVSequencerEnvelope *) get_be16(pb);
            instrument->slide_env            = (AVSequencerEnvelope *) get_be16(pb);
            instrument->vibrato_env          = (AVSequencerEnvelope *) get_be16(pb);
            instrument->tremolo_env          = (AVSequencerEnvelope *) get_be16(pb);
            instrument->pannolo_env          = (AVSequencerEnvelope *) get_be16(pb);
            instrument->channolo_env         = (AVSequencerEnvelope *) get_be16(pb);
            instrument->spenolo_env          = (AVSequencerEnvelope *) get_be16(pb);
            instrument->arpeggio_ctrl        = (AVSequencerArpeggio *) get_be16(pb);
            instrument->keyboard_defs        = (AVSequencerKeyboard *) get_be16(pb);
            samples                          = get_byte(pb);
            instrument->global_volume        = get_byte(pb);
            instrument->nna                  = get_byte(pb);
            instrument->note_swing           = get_byte(pb);
            instrument->volume_swing         = get_be16(pb);
            instrument->panning_swing        = get_be16(pb);
            instrument->pitch_swing          = get_be32(pb);
            instrument->pitch_pan_separation = get_be16(pb);
            instrument->default_panning      = get_byte(pb);
            instrument->default_sub_pan      = get_byte(pb);
            instrument->dct                  = get_byte(pb);
            instrument->dna                  = get_byte(pb);
            instrument->compat_flags         = get_byte(pb);
            instrument->flags                = get_byte(pb);
            instrument->env_usage_flags      = get_be16(pb);
            instrument->env_proc_flags       = get_be16(pb);
            instrument->env_retrig_flags     = get_be16(pb);
            instrument->env_random_flags     = get_be16(pb);
            instrument->env_rnd_delay_flags  = get_be16(pb);
            instrument->fade_out             = get_be16(pb);
            instrument->hold                 = get_be16(pb);
            instrument->decay                = get_be16(pb);
            instrument->dca                  = get_byte(pb);
            instrument->pitch_pan_center     = get_byte(pb);
            instrument->midi_channel         = get_byte(pb);
            instrument->midi_program         = get_byte(pb);
            instrument->midi_flags           = get_byte(pb);
            instrument->midi_transpose       = get_byte(pb);
            instrument->midi_after_touch     = get_byte(pb);
            instrument->midi_pitch_bender    = get_byte(pb);

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_SAMP:
                if ((res = open_inst_samp ( s, module, instrument, args, opaque, get_be32(pb))) < 0)
                    return res;

                break;
            default:
                // TODO: Add unknown chunk

                break;
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, instrument->metadata, metadata_tag, data_size)) < 0) {
                av_log(instrument, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (samples != instrument->samples) {
        av_log(instrument, AV_LOG_ERROR, "Number of attached samples does not match actual reads!");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int open_inst_samp ( AVCodecContext *avctx, AVSequencerModule *module, AVSequencerInstrument *instrument, const char *args, void *opaque, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_SMPL:
                if ((res = open_samp_smpl ( s, module, instrument, args, opaque, get_be32(pb))) < 0)
                    return res;

                break;
            }

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_samp_smpl ( AVCodecContext *avctx, AVSequencerModule *module, AVSequencerInstrument *instrument, const char *args, void *opaque, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerSample *sample;
    uint8_t *buf = NULL;
    uint32_t len, iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(sample = avseq_sample_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_sample_open ( instrument, sample, NULL, 0 )) < 0) {
        av_free(sample);
        return res;
    }

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_SMPH:
            sample->samples             = get_be32(pb);
            sample->repeat              = get_be32(pb);
            sample->rep_len             = get_be32(pb);
            sample->rep_count           = get_be32(pb);
            sample->sustain_repeat      = get_be32(pb);
            sample->sustain_rep_len     = get_be32(pb);
            sample->sustain_rep_count   = get_be32(pb);
            sample->rate                = get_be32(pb);
            sample->start_offset        = get_be32(pb);
            sample->rate_min            = get_be32(pb);
            sample->rate_max            = get_be32(pb);
            sample->bits_per_sample     = get_byte(pb);
            sample->transpose           = get_byte(pb);
            sample->finetune            = get_byte(pb);
            sample->compat_flags        = get_byte(pb);
            sample->flags               = get_byte(pb);
            sample->repeat_mode         = get_byte(pb);
            sample->sustain_repeat_mode = get_byte(pb);
            sample->global_volume       = get_byte(pb);
            sample->volume              = get_byte(pb);
            sample->sub_volume          = get_byte(pb);
            sample->panning             = get_byte(pb);
            sample->sub_panning         = get_byte(pb);
            sample->auto_vibrato_env    = (AVSequencerEnvelope *) get_be16(pb);
            sample->auto_tremolo_env    = (AVSequencerEnvelope *) get_be16(pb);
            sample->auto_pannolo_env    = (AVSequencerEnvelope *) get_be16(pb);
            sample->env_usage_flags     = get_byte(pb);
            sample->env_proc_flags      = get_byte(pb);
            sample->env_retrig_flags    = get_byte(pb);
            sample->env_random_flags    = get_byte(pb);
            sample->vibrato_sweep       = get_be16(pb);
            sample->tremolo_sweep       = get_be16(pb);
            sample->pannolo_sweep       = get_be16(pb);
            sample->vibrato_depth       = get_byte(pb);
            sample->vibrato_rate        = get_byte(pb);
            sample->tremolo_depth       = get_byte(pb);
            sample->tremolo_rate        = get_byte(pb);
            sample->pannolo_depth       = get_byte(pb);
            sample->pannolo_rate        = get_byte(pb);

            break;
        case ID_BODY:
            len = iff_size;
            buf = av_malloc(iff_size + FF_INPUT_BUFFER_PADDING_SIZE);

            if (!buf)
                return AVERROR(ENOMEM);

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(buf);
                return AVERROR(EIO);
            }

            break;
        case ID_SMPR:
            sample->data = (int16_t *) get_be32(pb);

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_SNTH:
                if ((res = open_smpl_snth ( s, sample, args, opaque, get_be32(pb))) < 0) {
                    av_freep(buf);
                    return res;
                }

                break;
            default:
                // TODO: Add unknown chunk

                break;
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, sample->metadata, metadata_tag, data_size)) < 0) {
                av_freep(buf);
                av_log(sample, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (!sample->data) {
        if (sample->samples && !sample->size) {
            // TODO: Load sample from demuxer/decoder pair
        } else if (!buf && sample->samples) {
            av_freep(buf);
            av_log(sample, AV_LOG_ERROR, "no sample data found, but non-zero number of samples!");
            return AVERROR_INVALIDDATA;
        } else if (sample->bits_per_sample != 8) {
            if ((res = avseq_sample_data_open(sample, NULL, sample->samples)) < 0) {
                av_freep(buf);
                return res;
            }

#if AV_HAVE_BIGENDIAN
            memcpy ( sample->data, buf, len );
#else
            if (sample->bits_per_sample == 16) {
                int16_t *data = sample->data;
                unsigned i    = FFALIGN(sample->samples, 8) >> 3;

                do {
                    uint32_t v = AV_RB16(buf);
                    *data++    = v;
                    v          = AV_RB16(buf + 2);
                    *data++    = v;
                    v          = AV_RB16(buf + 4);
                    *data++    = v;
                    v          = AV_RB16(buf + 6);
                    *data++    = v;
                    buf       += 8;
                } while (--i);
            } else {
                int32_t *data = (int32_t *) sample->data;
                unsigned i    = FFALIGN(sample->samples, 2) >> 1;

                do {
                    uint32_t v = AV_RB32(buf);
                    *data++    = v;
                    v          = AV_RB32(buf + 4);
                    *data++    = v;
                    buf       += 8;
                } while (--i);
            }
#endif
        } else {
            if ((res = avseq_sample_data_open(sample, NULL, sample->samples)) < 0) {
                av_freep(buf);
                return res;
            }

            memcpy ( sample->data, buf, len );
        }

        if (sample->flags & 1)
            avseq_sample_decrunch ( module, sample, 0 );

        sample->flags &= ~AVSEQ_SAMPLE_FLAG_REDIRECT;
    }

    av_freep(buf);

    return 0;
}

static int open_smpl_snth ( AVCodecContext *avctx, AVSequencerSample *sample, const char *args, void *opaque, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerSynth *synth;
    uint8_t *buf = NULL;
    uint32_t len, iff_size;
    uint16_t i, waveforms = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if ((res = avseq_synth_open ( sample, 1, 0, 0 )) < 0)
        return res;

    synth = sample->synth;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;
        unsigned i;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_YHDR:
            waveforms             = get_be16(pb);
            synth->entry_pos[0]   = get_be16(pb);
            synth->entry_pos[1]   = get_be16(pb);
            synth->entry_pos[2]   = get_be16(pb);
            synth->entry_pos[3]   = get_be16(pb);
            synth->sustain_pos[0] = get_be16(pb);
            synth->sustain_pos[1] = get_be16(pb);
            synth->sustain_pos[2] = get_be16(pb);
            synth->sustain_pos[3] = get_be16(pb);
            synth->nna_pos[0]     = get_be16(pb);
            synth->nna_pos[1]     = get_be16(pb);
            synth->nna_pos[2]     = get_be16(pb);
            synth->nna_pos[3]     = get_be16(pb);
            synth->dna_pos[0]     = get_be16(pb);
            synth->dna_pos[1]     = get_be16(pb);
            synth->dna_pos[2]     = get_be16(pb);
            synth->dna_pos[3]     = get_be16(pb);

            for (i = 0; i < 16; ++i) {
                synth->variable[i] = get_be16(pb);
            }

            synth->cond_var[0]       = get_be16(pb);
            synth->cond_var[1]       = get_be16(pb);
            synth->cond_var[2]       = get_be16(pb);
            synth->cond_var[3]       = get_be16(pb);
            synth->use_nna_flags     = get_byte(pb);
            synth->use_sustain_flags = get_byte(pb);
            synth->pos_keep_mask     = get_byte(pb);
            synth->nna_pos_keep_mask = get_byte(pb);
            synth->var_keep_mask     = get_be16(pb);

            break;
        case ID_CODE:
            len = iff_size;
            buf = av_malloc(iff_size);

            if (!buf)
                return AVERROR(ENOMEM);

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(buf);
                return AVERROR(EIO);
            }

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_WFRM:
                if ((res = open_snth_wfrm ( s, synth, get_be32(pb))) < 0) {
                    av_freep(buf);
                    return res;
                }

                break;
            case ID_STAB:
                if ((res = open_snth_stab ( s, synth, get_be32(pb))) < 0) {
                    av_freep(buf);
                    return res;
                }

                break;
            default:
                // TODO: Add unknown chunk

                break;
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, synth->metadata, metadata_tag, data_size)) < 0) {
                av_freep(buf);
                av_log(synth, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (waveforms != synth->waveforms) {
        av_freep(buf);
        av_log(synth, AV_LOG_ERROR, "Number of attached waveforms does not match actual reads!");
        return AVERROR_INVALIDDATA;
    }

    if (!buf) {
        av_freep(buf);
        av_log(synth, AV_LOG_ERROR, "No synth sound code read!");
        return AVERROR_INVALIDDATA;
    } else if (!synth->size != (len >> 2)) {
        av_freep(buf);
        av_log(synth, AV_LOG_ERROR, "Number of synth sound code lines does not match actual reads!");
        return AVERROR_INVALIDDATA;
    }

    if ((res = avseq_synth_code_open(synth, synth->size)) < 0) {
        av_freep(buf);
        return res;
    }

    for (i = 0; i < synth->size; ++i) {
        synth->code[i].instruction = *buf++;
        synth->code[i].src_dst_var = *buf++;
        synth->code[i].data        = AV_WN16A(&(synth->code[i].data), AV_RB16(buf));
        buf += 2;
    }

    av_freep(buf);

    return 0;
}

static int open_snth_wfrm ( AVCodecContext *avctx, AVSequencerSynth *synth, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_WAVE:
                if ((res = open_wfrm_wave ( s, synth, get_be32(pb))) < 0)
                    return res;

                break;
            }

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_wfrm_wave ( AVCodecContext *avctx, AVSequencerSynth *synth, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerSynthWave *waveform;
    uint8_t *buf = NULL;
    uint32_t len, iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(waveform = avseq_synth_waveform_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_synth_waveform_open ( synth, 1 )) < 0) {
        av_free(waveform);
        return res;
    }

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_WHDR:
            waveform->repeat     = get_be32(pb);
            waveform->repeat_len = get_be32(pb);
            waveform->flags      = get_be16(pb);

            break;
        case ID_BODY:
            len = iff_size;
            buf = av_malloc(iff_size + FF_INPUT_BUFFER_PADDING_SIZE);

            if (!buf)
                return AVERROR(ENOMEM);

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(buf);
                return AVERROR(EIO);
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, waveform->metadata, metadata_tag, data_size)) < 0) {
                av_freep(buf);
                av_log(waveform, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (!buf && waveform->samples) {
        av_freep(buf);
        av_log(waveform, AV_LOG_ERROR, "no synth sound waveform data found, but non-zero number of samples!");
        return AVERROR_INVALIDDATA;
    }

    if ((res = avseq_synth_waveform_data_open(waveform, waveform->samples)) < 0) {
        av_freep(buf);
        return res;
    }

#if AV_HAVE_BIGENDIAN
    memcpy ( waveform->data, buf, len );
#else
    if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT) {
        memcpy ( waveform->data, buf, len );
    } else {
        int16_t *data = waveform->data;
        unsigned i    = FFALIGN(waveform->samples, 8) >> 3;

        do {
            uint32_t v = AV_RB16(buf);
            *data++    = v;
            v          = AV_RB16(buf + 2);
            *data++    = v;
            v          = AV_RB16(buf + 4);
            *data++    = v;
            v          = AV_RB16(buf + 6);
            *data++    = v;
            buf       += 8;
        } while (--i);
    }
#endif

    av_freep(buf);

    return 0;
}

static int open_snth_stab ( AVCodecContext *avctx, AVSequencerSynth *synth, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_SMBL:
                if ((res = open_stab_smbl ( s, synth, get_be32(pb))) < 0)
                    return res;

                break;
            }

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_stab_smbl ( AVCodecContext *avctx, AVSequencerSynth *synth, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerSynthSymbolTable *symbol;
    uint8_t *buf = NULL;
    uint32_t len, iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(symbol = avseq_synth_symbol_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_synth_symbol_open ( synth, symbol, "UNNAMED" )) < 0) {
        av_free(symbol);
        return res;
    }

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_SREF:
            symbol->symbol_value = get_be16(pb);
            symbol->line_min     = get_be16(pb);
            symbol->line_max     = get_be16(pb);
            symbol->type         = get_byte(pb);
            symbol->flags        = get_byte(pb);

            break;
        case ID_NAME:
            buf = ((iff_size + 1) == 0) ? NULL : av_malloc(iff_size + 1);

            if (!buf)
                return AVERROR(ENOMEM);

            len = iff_size;

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(buf);
                return AVERROR(EIO);
            }

            buf[iff_size] = 0;

            break;
        default:
            // TODO: Add unknown chunk

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if ((res = avseq_synth_symbol_open ( synth, symbol, (buf ? buf : (const uint8_t *) "UNNAMED"))) < 0) {
        av_freep(buf);
        return res;
    }

    av_freep(buf);

    return 0;
}

static int open_tcm1_envl ( AVCodecContext *avctx, AVSequencerContext *seqctx, AVSequencerModule *module, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_ENVD:
                if ((res = open_envl_envd ( s, seqctx, module, get_be32(pb))) < 0)
                    return res;

                break;
            }

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_envl_envd ( AVCodecContext *avctx, AVSequencerContext *seqctx, AVSequencerModule *module, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerEnvelope *envelope;
    uint8_t *buf = NULL;
    uint8_t *node_buf = NULL;
    uint32_t len, node_len, iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(envelope = avseq_envelope_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_envelope_open ( seqctx, module, envelope, 1, 0, 0, 0, 0 )) < 0) {
        av_free(envelope);
        return res;
    }

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_EHDR:
            envelope->flags         = get_be16(pb);
            envelope->tempo         = get_be16(pb);
            envelope->sustain_start = get_be16(pb);
            envelope->sustain_end   = get_be16(pb);
            envelope->sustain_count = get_be16(pb);
            envelope->loop_start    = get_be16(pb);
            envelope->loop_end      = get_be16(pb);
            envelope->loop_count    = get_be16(pb);
            envelope->value_min     = get_be16(pb);
            envelope->value_max     = get_be16(pb);

            break;
        case ID_BODY:
            len = iff_size;
            buf = av_malloc(iff_size + FF_INPUT_BUFFER_PADDING_SIZE);

            if (!buf) {
                av_freep(node_buf);
                return AVERROR(ENOMEM);
            }

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(node_buf);
                av_freep(buf);
                return AVERROR(EIO);
            }

            break;
        case ID_NODE:
            node_len = iff_size;
            node_buf = av_malloc(iff_size + FF_INPUT_BUFFER_PADDING_SIZE);

            if (!node_buf) {
                av_freep(buf);
                return AVERROR(ENOMEM);
            }

            if (get_buffer(pb, node_buf, iff_size) < 0) {
                av_freep(node_buf);
                av_freep(buf);
                return AVERROR(EIO);
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, envelope->metadata, metadata_tag, data_size)) < 0) {
                av_freep(node_buf);
                av_freep(buf);
                av_log(envelope, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (!buf && envelope->points) {
        av_freep(node_buf);
        av_log(envelope, AV_LOG_ERROR, "no envelope data points found, but non-zero number of points!");
        return AVERROR_INVALIDDATA;
    } else if (!node_buf && envelope->nodes) {
        av_freep(buf);
        av_log(envelope, AV_LOG_ERROR, "no envelope data node points found, but non-zero number of nodes!");
        return AVERROR_INVALIDDATA;
    } else if ((res = avseq_envelope_data_open(seqctx, envelope, envelope->points, 0, 0, 0, envelope->nodes)) < 0) {
        av_freep(node_buf);
        av_freep(buf);
        return res;
    } else {
#if AV_HAVE_BIGENDIAN
        memcpy ( envelope->data, buf, len );
        memcpy ( envelope->node_points, node_buf, node_len );
#else
        int16_t *data = envelope->data;
        unsigned i    = FFALIGN(envelope->points, 8) >> 3;

        do {
            uint32_t v = AV_RB16(buf);
            *data++    = v;
            v          = AV_RB16(buf + 2);
            *data++    = v;
            v          = AV_RB16(buf + 4);
            *data++    = v;
            v          = AV_RB16(buf + 6);
            *data++    = v;
            buf       += 8;
        } while (--i);

        data = envelope->node_points;
        i    = FFALIGN(envelope->nodes, 8) >> 3;

        do {
            uint32_t v = AV_RB16(buf);
            *data++    = v;
            v          = AV_RB16(buf + 2);
            *data++    = v;
            v          = AV_RB16(buf + 4);
            *data++    = v;
            v          = AV_RB16(buf + 6);
            *data++    = v;
            buf       += 8;
        } while (--i);
#endif
    }

    av_freep(node_buf);
    av_freep(buf);

    return 0;
}

static int open_tcm1_keyb ( AVCodecContext *avctx, AVSequencerModule *module, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerKeyboard *keyboard;
    uint32_t keyboards = 0, iff_size;
    unsigned i;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(keyboard = avseq_keyboard_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_keyboard_open ( module, keyboard )) < 0) {
        av_free(keyboard);
        return res;
    }

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_KBRD:
            keyboards = iff_size >> 2;

            if (keyboards > 120) {
                av_log(keyboard, AV_LOG_ERROR, "keyboard too large (max 10 octave range supported)!");
                return res;
            }

            for (i = 0; i < keyboards; ++i) {
                keyboard->key[i].sample = get_be16(pb);
                keyboard->key[i].octave = get_byte(pb);
                keyboard->key[i].note   = get_byte(pb);
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, keyboard->metadata, metadata_tag, data_size)) < 0) {
                av_log(keyboard, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (!keyboards) {
        av_log(keyboard, AV_LOG_ERROR, "Attached keyboard does not match actual reads!");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int open_tcm1_arpl ( AVCodecContext *avctx, AVSequencerModule *module, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    uint32_t iff_size;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_ARPG:
                if ((res = open_arpl_arpg ( s, module, get_be32(pb))) < 0)
                    return res;

                break;
            }

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    return 0;
}

static int open_arpl_arpg ( AVCodecContext *avctx, AVSequencerModule *module, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerArpeggio *arpeggio;
    uint32_t iff_size;
    uint16_t entries = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    if (!(arpeggio = avseq_arpeggio_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_arpeggio_open ( module, arpeggio, 1 )) < 0) {
        av_free(arpeggio);
        return res;
    }

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;
        const char *metadata_tag = NULL;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_AHDR:
            entries                 = get_be16(pb);
            arpeggio->flags         = get_be16(pb);
            arpeggio->sustain_start = get_be16(pb);
            arpeggio->sustain_end   = get_be16(pb);
            arpeggio->sustain_count = get_be16(pb);
            arpeggio->loop_start    = get_be16(pb);
            arpeggio->loop_end      = get_be16(pb);
            arpeggio->loop_count    = get_be16(pb);

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_ARPE:
                if ((res = open_arpg_arpe ( s, arpeggio, get_be32(pb))) < 0)
                    return res;

                break;
            default:
                // TODO: Add unknown chunk

                break;
            }

            break;
        case ID_ANNO:
        case ID_TEXT:
            metadata_tag = "comment";
            break;

        case ID_AUTH:
            metadata_tag = "artist";
            break;

        case ID_COPYRIGHT:
            metadata_tag = "copyright";
            break;

        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_NAME:
            metadata_tag = "title";
            break;

        default:
            // TODO: Add unknown chunk

            break;
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, arpeggio->metadata, metadata_tag, data_size)) < 0) {
                av_log(arpeggio, AV_LOG_ERROR, "cannot allocate metadata tag %s!", metadata_tag);
                return res;
            }
        }
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (entries != arpeggio->entries) {
        av_log(arpeggio, AV_LOG_ERROR, "Number of attached arpeggio entries does not match actual reads!");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int open_arpg_arpe ( AVCodecContext *avctx, AVSequencerArpeggio *arpeggio, uint32_t data_size ) {
    ByteIOContext *pb = s->pb;
    AVSequencerArpeggioData *data;
    uint32_t iff_size;
    uint16_t ticks = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size -= 4;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_ARPE:
            if (ticks) {
                if ((res = avseq_arpeggio_data_open ( arpeggio, arpeggio->entries + 1 )) < 0) {
                    return res;
                }
            }

            ticks            = arpeggio->entries;
            data             = &(arpeggio->data[ticks - 1]);
            data->tone       = get_byte(pb);
            data->transpose  = get_byte(pb);
            data->instrument = get_be16(pb);
            data->command[0] = get_byte(pb);
            data->command[1] = get_byte(pb);
            data->command[2] = get_byte(pb);
            data->command[3] = get_byte(pb);
            data->data[0]    = get_be16(pb);
            data->data[1]    = get_be16(pb);
            data->data[2]    = get_be16(pb);
            data->data[3]    = get_be16(pb);

            break;
        }

        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos) + (iff_size & 1));
    }

    if (!ticks) {
        av_log(arpeggio, AV_LOG_ERROR, "Attached arpeggio structure entries do not match actual reads!");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}
