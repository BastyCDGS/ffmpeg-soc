/*
 * IFF (.iff) file demuxer
 * Copyright (c) 2008 Jaikrishnan Menon <realityman@gmx.net>
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * IFF file demuxer
 * by Jaikrishnan Menon
 * for more information on the .iff file format, visit:
 * http://wiki.multimedia.cx/index.php?title=IFF
 */

#include "libavutil/intreadwrite.h"
#include "libavcodec/iff.h"
#include "avformat.h"

#if CONFIG_AVSEQUENCER
#include "libavutil/avstring.h"
#include "libavsequencer/avsequencer.h"
#include "libavsequencer/player.h"

static const char *const metadata_tag_list[] = {
    "artist",
    "comment",
    "copyright",
    "file",
    "genre",
    "title"
};

/* Metadata string read */
static int seq_get_metadata(AVFormatContext *s, AVMetadata **const metadata, const char *const tag, const unsigned data_size);
static int open_tcm1_song(AVFormatContext *s, AVSequencerContext *avctx, AVSequencerModule *module, uint32_t data_size);
static int open_song_patt(AVFormatContext *s, AVSequencerSong *song, uint32_t data_size);
static int open_patt_trak(AVFormatContext *s, AVSequencerSong *song, uint32_t data_size);
static int open_song_posi(AVFormatContext *s, AVSequencerContext *avctx, AVSequencerSong *song, uint32_t data_size);
static int open_posi_post(AVFormatContext *s, AVSequencerSong *song, uint32_t channel, uint32_t data_size);
static int open_post_posl(AVFormatContext *s, AVSequencerOrderList *order_list, uint32_t data_size);

static int open_tcm1_insl(AVFormatContext *s, AVSequencerModule *module, const char *args, void *opaque, uint32_t data_size);
static int open_insl_inst(AVFormatContext *s, AVSequencerModule *module, const char *args, void *opaque, uint32_t data_size);
static int open_inst_samp(AVFormatContext *s, AVSequencerModule *module, AVSequencerInstrument *instrument, const char *args, void *opaque, uint32_t data_size);
static int open_samp_smpl(AVFormatContext *s, AVSequencerModule *module, AVSequencerInstrument *instrument, const char *args, void *opaque, uint32_t data_size);
static int open_smpl_snth(AVFormatContext *s, AVSequencerSample *sample, const char *args, void *opaque, uint32_t data_size);
static int open_snth_wfrm(AVFormatContext *s, AVSequencerSynth *synth, uint32_t data_size);
static int open_wfrm_wave(AVFormatContext *s, AVSequencerSynth *synth, uint32_t data_size);
static int open_snth_stab(AVFormatContext *s, AVSequencerSynth *synth, uint32_t data_size);
static int open_stab_smbl(AVFormatContext *s, AVSequencerSynth *synth, uint32_t data_size);

static int open_tcm1_envl(AVFormatContext *s, AVSequencerContext *avctx, AVSequencerModule *module, uint32_t data_size);
static int open_envl_envd(AVFormatContext *s, AVSequencerContext *avctx, AVSequencerModule *module, uint32_t data_size);

static int open_tcm1_keyb( AVFormatContext *s, AVSequencerModule *module, uint32_t data_size);

static int open_tcm1_arpl(AVFormatContext *s, AVSequencerModule *module, uint32_t data_size);
static int open_arpl_arpg(AVFormatContext *s, AVSequencerModule *module, uint32_t data_size);
static int open_arpg_arpe(AVFormatContext *s, AVSequencerArpeggio *arpeggio, uint32_t data_size);
#endif

#define ID_8SVX       MKTAG('8','S','V','X')
#define ID_VHDR       MKTAG('V','H','D','R')
#define ID_ATAK       MKTAG('A','T','A','K')
#define ID_RLSE       MKTAG('R','L','S','E')
#define ID_CHAN       MKTAG('C','H','A','N')
#define ID_PBM        MKTAG('P','B','M',' ')
#define ID_ILBM       MKTAG('I','L','B','M')
#define ID_BMHD       MKTAG('B','M','H','D')
#define ID_CMAP       MKTAG('C','M','A','P')

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
#define ID_POST       MKTAG('P','O','S','T')
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

#define LEFT    2
#define RIGHT   4
#define STEREO  6

#define PACKET_SIZE 1024

typedef enum {
    COMP_NONE,
    COMP_FIB,
    COMP_EXP
} svx8_compression_type;

typedef enum {
    BITMAP_RAW,
    BITMAP_BYTERUN1
} bitmap_compression_type;

typedef struct {
    uint64_t  body_pos;
    uint32_t  body_size;
    uint32_t  sent_bytes;
    uint32_t  audio_frame_count;
#if CONFIG_AVSEQUENCER
    AVSequencerContext *avctx;
#endif
} IffDemuxContext;


static void interleave_stereo(const uint8_t *src, uint8_t *dest, int size)
{
    uint8_t *end = dest + size;
    size = size>>1;

    while(dest < end) {
        *dest++ = *src;
        *dest++ = *(src+size);
        src++;
    }
}

/* Metadata string read */
static int get_metadata(AVFormatContext *s,
                        const char *const tag,
                        const unsigned data_size)
{
    uint8_t *buf = ((data_size + 1) == 0) ? NULL : av_malloc(data_size + 1);

    if (!buf)
        return AVERROR(ENOMEM);

    if (get_buffer(s->pb, buf, data_size) < 0) {
        av_free(buf);
        return AVERROR(EIO);
    }
    buf[data_size] = 0;
    av_metadata_set2(&s->metadata, tag, buf, AV_METADATA_DONT_STRDUP_VAL);
    return 0;
}

static int iff_probe(AVProbeData *p)
{
    const uint8_t *d = p->buf;

    if (AV_RL32(d) != ID_FORM)
        return 0;

    if (AV_RL32(d+8) == ID_8SVX || AV_RL32(d+8) == ID_PBM || AV_RL32(d+8) == ID_ILBM)
        return AVPROBE_SCORE_MAX;

#if CONFIG_AVSEQUENCER
    if (AV_RL32(d+8) == ID_TCM1)
        return AVPROBE_SCORE_MAX;
#endif

    return 0;
}

static int iff_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    IffDemuxContext *iff = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;
#if CONFIG_AVSEQUENCER
    AVSequencerModule *module = NULL;
    uint8_t buf[24];
    const char *args = "stereo=true; interpolation=0; real16bit=false; load_samples=true; samples_dir=; load_synth_code_symbols=true;";
    void *opaque = NULL;
    uint32_t tracks = 0, samples     = 0, synths    = 0;
    uint16_t songs  = 0, instruments = 0, envelopes = 0, keyboards = 0, arpeggios = 0;
    unsigned year, month, day, hour, min, sec, cts;
#endif
    uint32_t chunk_id, data_size;
    int compression = -1, res;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->channels = 1;
    url_fskip(pb, 8);
    // codec_tag used by ByteRun1 decoder to distinguish progressive (PBM) and interlaced (ILBM) content
    st->codec->codec_tag = get_le32(pb);

#if CONFIG_AVSEQUENCER
    if (st->codec->codec_tag == ID_TCM1) {
        if (!(iff->avctx = avsequencer_open(NULL, "")))
            return AVERROR(ENOMEM);

        if (!(module = avseq_module_create ()))
            return AVERROR(ENOMEM);

        if ((res = avseq_module_open(iff->avctx, module)) < 0) {
            av_free(module);
            return res;
        }

        avseq_module_set_channels(iff->avctx, module, 1);

        st->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
    }
#endif

    while(!url_feof(pb)) {
        uint64_t orig_pos;
        const char *metadata_tag = NULL;
        chunk_id = get_le32(pb);
        data_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
#if CONFIG_AVSEQUENCER
        case ID_MHDR:
            if (!module)
                break;

            if ((res = get_byte(pb)) != 1) { // version
                av_log(module, AV_LOG_ERROR, "Invalid version: %d.%d\n", res, get_byte(pb));
                return AVERROR_INVALIDDATA;
            }
            compression = get_byte(pb); // revision
            day         = get_byte(pb);
            month       = get_byte(pb);
            year        = get_be16(pb);
            hour        = get_byte(pb);
            min         = get_byte(pb);
            sec         = get_byte(pb);
            cts         = get_byte(pb);

            if (day || month || year || hour || min || sec || cts) {
                if (month == 0 || month > 12 || day == 0 || day > 31 || hour > 23 || min > 59 || sec > 59 || cts > 99) {
                    av_log(module, AV_LOG_WARNING, "Invalid begin composing date: %04d-%02d-%02d %02d:%02d:%02d.%02d\n", year, month, day, hour, min, sec, cts);
                } else {
                    snprintf(buf, 24, "%04d-%02d-%02d %02d:%02d:%02d.%02d", year, month, day, hour, min, sec, cts);
                    av_metadata_set2(&s->metadata, "begin_date", buf, 0);
                    av_metadata_set2(&module->metadata, "begin_date", buf, 0);
                }
            }

            day   = get_byte(pb);
            month = get_byte(pb);
            year  = get_be16(pb);
            hour  = get_byte(pb);
            min   = get_byte(pb);
            sec   = get_byte(pb);
            cts   = get_byte(pb);

            if (day || month || year || hour || min || sec || cts) {
                if (month == 0 || month > 12 || day == 0 || day > 31 || hour > 23 || min > 59 || sec > 59 || cts > 99) {
                    av_log(module, AV_LOG_WARNING, "Invalid finish composing date: %04d-%02d-%02d %02d:%02d:%02d.%02d\n", year, month, day, hour, min, sec, cts);
                } else {
                    snprintf(buf, 24, "%04d-%02d-%02d %02d:%02d:%02d.%02d", year, month, day, hour, min, sec, cts);
                    av_metadata_set2(&s->metadata, "date", buf, 0);
                    av_metadata_set2(&module->metadata, "date", buf, 0);
                }
            }

            hour = get_byte(pb);
            min  = get_byte(pb);
            sec  = get_byte(pb);
            cts  = get_byte(pb);

            if (min > 59 || sec > 59 || cts > 99) {
                av_log(module, AV_LOG_WARNING, "Invalid duration: %02d:%02d:%02d.%02d\n", hour, min, sec, cts);
            } else {
                s->duration             = (((uint64_t) (hour*360000+min*6000+sec*100+cts))*AV_TIME_BASE) / 100;
                module->forced_duration = (((uint64_t) (hour*360000+min*6000+sec*100+cts))*AV_TIME_BASE) / 100;
            }

            songs                   = get_be16(pb);
            tracks                  = get_be32(pb);
            instruments             = get_be16(pb);
            samples                 = get_be32(pb);
            synths                  = get_be32(pb);
            envelopes               = get_be16(pb);
            keyboards               = get_be16(pb);
            arpeggios               = get_be16(pb);

            avseq_module_set_channels(iff->avctx, module, get_be16(pb));

            if (get_be16(pb)) // compatibility flags and flags
                return AVERROR_INVALIDDATA;

            break;
        case ID_FORM:
            if (!module)
                break;

            switch (get_le32(pb)) {
            case ID_SONG:
                if ((res = open_tcm1_song(s, iff->avctx, module, data_size)) < 0)
                    return res;

                break;
            case ID_INSL:
                if ((res = open_tcm1_insl(s, module, args, opaque, data_size)) < 0)
                    return res;

                break;
            case ID_ENVL:
                if ((res = open_tcm1_envl(s, iff->avctx, module, data_size)) < 0)
                    return res;

                break;
            case ID_KEYB:
                if ((res = open_tcm1_keyb(s, module, data_size)) < 0)
                    return res;

                break;
            case ID_ARPL:
                if ((res = open_tcm1_arpl(s, module, data_size)) < 0)
                    return res;

                break;
            default:
                // TODO: Add unknown chunk

                break;
            }

            break;
        case ID_CMNT:
        case ID_MMSG:
            metadata_tag = "comment";
            break;
        case ID_FILE:
            metadata_tag = "file";
            break;

        case ID_STIL:
            metadata_tag = "genre";
            break;
#endif
        case ID_VHDR:
#if CONFIG_AVSEQUENCER
            if (module)
                goto read_unknown_chunk;
#endif
            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;

            if (data_size < 14)
                return AVERROR_INVALIDDATA;
            url_fskip(pb, 12);
            st->codec->sample_rate = get_be16(pb);
            if (data_size >= 16) {
                url_fskip(pb, 1);
                compression        = get_byte(pb);
            }
            break;

        case ID_BODY:
#if CONFIG_AVSEQUENCER
            if (module)
                goto read_unknown_chunk;
#endif
            iff->body_pos = url_ftell(pb);
            iff->body_size = data_size;
            break;

        case ID_CHAN:
#if CONFIG_AVSEQUENCER
            if (module)
                goto read_unknown_chunk;
#endif
            if (data_size < 4)
                return AVERROR_INVALIDDATA;
            st->codec->channels = (get_be32(pb) < 6) ? 1 : 2;
            break;

        case ID_CMAP:
#if CONFIG_AVSEQUENCER
            if (module)
                goto read_unknown_chunk;
#endif
            st->codec->extradata_size = data_size;
            st->codec->extradata      = av_malloc(data_size);
            if (!st->codec->extradata)
                return AVERROR(ENOMEM);
            if (get_buffer(pb, st->codec->extradata, data_size) < 0)
                return AVERROR(EIO);
            break;

        case ID_BMHD:
#if CONFIG_AVSEQUENCER
            if (module)
                goto read_unknown_chunk;
#endif
            st->codec->codec_type            = AVMEDIA_TYPE_VIDEO;
            if (data_size <= 8)
                return AVERROR_INVALIDDATA;
            st->codec->width                 = get_be16(pb);
            st->codec->height                = get_be16(pb);
            url_fskip(pb, 4); // x, y offset
            st->codec->bits_per_coded_sample = get_byte(pb);
            if (data_size >= 11) {
                url_fskip(pb, 1); // masking
                compression                  = get_byte(pb);
            }
            if (data_size >= 16) {
                url_fskip(pb, 3); // paddding, transparent
                st->sample_aspect_ratio.num  = get_byte(pb);
                st->sample_aspect_ratio.den  = get_byte(pb);
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

        case ID_NAME:
            metadata_tag = "title";
            break;
#if CONFIG_AVSEQUENCER
        default:
read_unknown_chunk:
            // TODO: Add unknown chunk

            break;
#endif
        }

        if (metadata_tag) {
            if ((res = get_metadata(s, metadata_tag, data_size)) < 0) {
                av_log(s, AV_LOG_ERROR, "cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
#if CONFIG_AVSEQUENCER
            if (module) {
                AVMetadataTag *tag = av_metadata_get(s->metadata, metadata_tag, NULL, AV_METADATA_IGNORE_SUFFIX);

                if (tag)
                    av_metadata_set2(&module->metadata, metadata_tag, tag->value, 0);
            }
#endif
        }
        url_fskip(pb, data_size - (url_ftell(pb) - orig_pos) + (data_size & 1));
    }

    url_fseek(pb, iff->body_pos, SEEK_SET);

    switch(st->codec->codec_type) {
    case AVMEDIA_TYPE_AUDIO:
        av_set_pts_info(st, 32, 1, st->codec->sample_rate);

        switch(compression) {
        case COMP_NONE:
            st->codec->codec_id = CODEC_ID_PCM_S8;
            break;
        case COMP_FIB:
            st->codec->codec_id = CODEC_ID_8SVX_FIB;
            break;
        case COMP_EXP:
            st->codec->codec_id = CODEC_ID_8SVX_EXP;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "unknown compression method\n");
            return -1;
        }

        st->codec->bits_per_coded_sample = 8;

#if CONFIG_AVSEQUENCER
        if (module) {
            AVMixerContext *mixctx;
            unsigned i;

            if (songs != module->songs) {
                av_log(module, AV_LOG_ERROR, "Number of attached sub-songs does not match actual reads (expected: %d, got: %d)!\n", module->songs, songs);
                return AVERROR_INVALIDDATA;
            }

            if (instruments != module->instruments) {
                av_log(module, AV_LOG_ERROR, "Number of attached instruments does not match actual reads (expected: %d, got: %d)!\n", module->instruments, instruments);
                return AVERROR_INVALIDDATA;
            }

            if (envelopes != module->envelopes) {
                av_log(module, AV_LOG_ERROR, "Number of attached envelopes does not match actual reads (expected: %d, got: %d)!\n", module->envelopes, envelopes);
                return AVERROR_INVALIDDATA;
            }

            if (keyboards != module->keyboards) {
                av_log(module, AV_LOG_ERROR, "Number of attached keyboard definitions does not match actual reads (expected: %d, got: %d)!\n", module->keyboards, keyboards);
                return AVERROR_INVALIDDATA;
            }

            if (arpeggios != module->arpeggios) {
                av_log(module, AV_LOG_ERROR, "Number of attached arpeggio structures does not match actual reads (expected: %d, got: %d)!\n", module->arpeggios, arpeggios);
                return AVERROR_INVALIDDATA;
            }

            for (i = 0; i < module->songs; ++i) {
                uint16_t channel;
                AVSequencerSong *song            = module->song_list[i];
                AVSequencerOrderList *order_list = song->order_list;

                tracks -= song->tracks;

                for (channel = 0; channel < song->channels; ++channel) {
                    uint16_t order;

                    for (order = 0; order < order_list->orders; ++order) {
                        AVSequencerOrderData *order_data = order_list->order_data[order];

                        order_data->track    = avseq_track_get_address(song, (uint32_t) order_data->track);
                        order_data->next_pos = avseq_order_get_address(song, channel, (uint32_t) order_data->next_pos);
                        order_data->prev_pos = avseq_order_get_address(song, channel, (uint32_t) order_data->prev_pos);
                    }

                    order_list++;
                }
            }

            if (tracks) {
                av_log(module, AV_LOG_ERROR, "Number of attached tracks does not match actual reads!\n");
                return AVERROR_INVALIDDATA;
            }

            for (i = 0; i < module->instruments; ++i) {
                AVSequencerInstrument *instrument = module->instrument_list[i];
                unsigned smp;

                instrument->volume_env    = avseq_envelope_get_address(module, (uint32_t) instrument->volume_env);
                instrument->panning_env   = avseq_envelope_get_address(module, (uint32_t) instrument->panning_env);
                instrument->slide_env     = avseq_envelope_get_address(module, (uint32_t) instrument->slide_env);
                instrument->vibrato_env   = avseq_envelope_get_address(module, (uint32_t) instrument->vibrato_env);
                instrument->tremolo_env   = avseq_envelope_get_address(module, (uint32_t) instrument->tremolo_env);
                instrument->pannolo_env   = avseq_envelope_get_address(module, (uint32_t) instrument->pannolo_env);
                instrument->channolo_env  = avseq_envelope_get_address(module, (uint32_t) instrument->channolo_env);
                instrument->spenolo_env   = avseq_envelope_get_address(module, (uint32_t) instrument->spenolo_env);
                instrument->arpeggio_ctrl = avseq_arpeggio_get_address(module, (uint32_t) instrument->arpeggio_ctrl);
                instrument->keyboard_defs = avseq_keyboard_get_address(module, (uint32_t) instrument->keyboard_defs);
                samples                  -= instrument->samples;

                for (smp = 0; smp < instrument->samples; ++smp) {
                    AVSequencerSample *sample = instrument->sample_list[smp];

                    sample->auto_vibrato_env = avseq_envelope_get_address(module, (uint32_t) sample->auto_vibrato_env);
                    sample->auto_tremolo_env = avseq_envelope_get_address(module, (uint32_t) sample->auto_tremolo_env);
                    sample->auto_pannolo_env = avseq_envelope_get_address(module, (uint32_t) sample->auto_pannolo_env);

                    if (sample->synth)
                        synths--;

                    if (sample->flags & AVSEQ_SAMPLE_FLAG_REDIRECT) {
                        unsigned j;
                        uint32_t origin = (uint32_t) sample->data;

                        for (j = 0; j < module->instruments; ++j) {
                            AVSequencerInstrument *origin_instrument = module->instrument_list[i];

                            if (origin < origin_instrument->samples)
                                sample->data = origin_instrument->sample_list[origin]->data;

                            origin -= origin_instrument->samples;
                        }
                    }
                }
            }

            if (samples) {
                av_log(module, AV_LOG_ERROR, "Number of attached samples does not match actual reads!\n");
                return AVERROR_INVALIDDATA;
            }

            if (synths) {
                av_log(module, AV_LOG_ERROR, "Number of attached synths does not match actual reads!\n");
                return AVERROR_INVALIDDATA;
            }

            if (songs == 1) {
                for (i = 0; i < sizeof (metadata_tag_list) / sizeof (char *); ++i) {
                    const char *metadata_tag = metadata_tag_list[i];
                    AVMetadataTag *tag = av_metadata_get(s->metadata, metadata_tag, NULL, AV_METADATA_IGNORE_SUFFIX);

                    if (!tag) {
                        tag = av_metadata_get(module->song_list[0]->metadata, metadata_tag, NULL, AV_METADATA_IGNORE_SUFFIX);

                        if (tag)
                            av_metadata_set2(&s->metadata, metadata_tag, tag->value, 0);
                    }
                }
            }

            if (!(mixctx = avseq_mixer_get_by_name("Low quality mixer"))) {
                if (!(mixctx = avseq_mixer_get_by_name("Null mixer"))) {
                    av_log(s, AV_LOG_ERROR, "No mixers found!\n");
                    return AVERROR(ENOMEM);
                }
            }

            st->codec->sample_rate = mixctx->frequency;
            st->codec->channels    = ((av_stristr(args, "stereo=true;") || av_stristr(args, "stereo=enabled;") || av_stristr(args, "stereo=1;")) && mixctx->flags & AVSEQ_MIXER_CONTEXT_FLAG_STEREO) ? 2 : 1;
            iff->body_size         = ((uint64_t) s->duration * st->codec->sample_rate * (st->codec->channels << 2)) / AV_TIME_BASE;

            if ((res = avseq_module_play(iff->avctx, mixctx, module, module->song_list[0], args, opaque, 0)) < 0)
                return res;

            if ((res = avseq_song_reset(iff->avctx, module->song_list[0])) < 0)
                return res;

            st->codec->bits_per_coded_sample = 32;
#if AV_HAVE_BIGENDIAN
            st->codec->codec_id              = CODEC_ID_PCM_S32BE;
#else
            st->codec->codec_id              = CODEC_ID_PCM_S32LE;
#endif
        }
#endif
        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate * st->codec->bits_per_coded_sample;
        st->codec->block_align = st->codec->channels * st->codec->bits_per_coded_sample;

        break;

    case AVMEDIA_TYPE_VIDEO:
        switch (compression) {
        case BITMAP_RAW:
            st->codec->codec_id = CODEC_ID_IFF_ILBM;
            break;
        case BITMAP_BYTERUN1:
            st->codec->codec_id = CODEC_ID_IFF_BYTERUN1;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "unknown compression method\n");
            return AVERROR_INVALIDDATA;
        }
        break;
    default:
        return -1;
    }

    return 0;
}

#if CONFIG_AVSEQUENCER
/* Metadata string read */
static int seq_get_metadata(AVFormatContext *s,
                            AVMetadata **const metadata,
                            const char *const tag,
                            const unsigned data_size)
{
    uint8_t *buf = ((data_size + 1) == 0) ? NULL : av_malloc(data_size + 1);

    if (!buf)
        return AVERROR(ENOMEM);

    if (get_buffer(s->pb, buf, data_size) < 0) {
        av_free(buf);
        return AVERROR(EIO);
    }
    buf[data_size] = 0;
    av_metadata_set2(metadata, tag, buf, AV_METADATA_DONT_STRDUP_VAL);
    return 0;
}

static int open_tcm1_song(AVFormatContext *s, AVSequencerContext *avctx, AVSequencerModule *module, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerSong *song;
    uint32_t iff_size = 4;
    uint16_t tracks = 0, channels = 1;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if (!(song = avseq_song_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_song_open(module, song)) < 0) {
        av_free(song);
        return res;
    }

    avseq_song_set_channels(avctx, song, 1);

    while (!url_feof(pb) && (data_size -= iff_size)) {
        unsigned year, month, day, hour, min, sec, cts;
        uint8_t buf[24];
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
            cts   = get_byte(pb);

            if (day || month || year || hour || min || sec || cts) {
                if (month == 0 || month > 12 || day == 0 || day > 31 || hour > 23 || min > 59 || sec > 59 || cts > 99) {
                    av_log(song, AV_LOG_WARNING, "Invalid begin composing date: %04d-%02d-%02d %02d:%02d:%02d.%02d\n", year, month, day, hour, min, sec, cts);
                } else {
                    snprintf(buf, 24, "%04d-%02d-%02d %02d:%02d:%02d.%02d", year, month, day, hour, min, sec, cts );
                    av_metadata_set2(&song->metadata, "begin_date", buf, 0);
                }
            }

            day   = get_byte(pb);
            month = get_byte(pb);
            year  = get_be16(pb);
            hour  = get_byte(pb);
            min   = get_byte(pb);
            sec   = get_byte(pb);
            cts   = get_byte(pb);

            if (day || month || year || hour || min || sec || cts) {
                if (month == 0 || month > 12 || day == 0 || day > 31 || hour > 23 || min > 59 || sec > 59 || cts > 99) {
                    av_log(song, AV_LOG_WARNING, "Invalid finish composing date: %04d-%02d-%02d %02d:%02d:%02d.%02d\n", year, month, day, hour, min, sec, cts);
                } else {
                    snprintf(buf, 24, "%04d-%02d-%02d %02d:%02d:%02d.%02d", year, month, day, hour, min, sec, cts );
                    av_metadata_set2(&song->metadata, "date", buf, 0);
                }
            }

            hour = get_byte(pb);
            min  = get_byte(pb);
            sec  = get_byte(pb);
            cts  = get_byte(pb);

            if (min > 59 || sec > 59 || cts > 99)
                av_log(song, AV_LOG_WARNING, "Invalid duration: %02d:%02d:%02d.%02d\n", hour, min, sec, cts);
            else
                song->duration           = (((uint64_t) (hour*360000+min*6000+sec*100+cts))*AV_TIME_BASE) / 100;

            tracks                   = get_be16(pb);
            song->gosub_stack_size   = get_be16(pb);
            song->loop_stack_size    = get_be16(pb);
            song->compat_flags       = get_byte(pb);
            song->flags              = get_byte(pb);
            channels                 = get_be16(pb);
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

            if ((res = avseq_song_set_channels(avctx, song, channels)) < 0)
                return res;

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_PATT:
                if ((res = open_song_patt(s, song, iff_size)) < 0)
                    return res;

                break;
            case ID_POSI:
                if ((res = open_song_posi(s, avctx, song, iff_size)) < 0)
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
            if ((res = seq_get_metadata(s, &song->metadata, metadata_tag, iff_size)) < 0) {
                av_log(song, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if (tracks != song->tracks) {
        av_log(song, AV_LOG_ERROR, "Number of attached tracks does not match actual reads (expected: %d, got: %d)!\n", song->tracks, tracks);
        return AVERROR_INVALIDDATA;
    }

    if (channels != song->channels) {
        av_log(song, AV_LOG_ERROR, "Number of attached channels does not match actual reads (expected: %d, got: %d)!\n", song->channels, channels);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int open_song_patt(AVFormatContext *s, AVSequencerSong *song, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
                if ((res = open_patt_trak(s, song, iff_size)) < 0)
                    return res;

                break;
            }

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_patt_trak(AVFormatContext *s, AVSequencerSong *song, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerTrack *track;
    uint8_t *buf = NULL;
    uint32_t last_row = 0, len = 0, iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if (!(track = avseq_track_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_track_open(song, track)) < 0) {
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
            last_row            = get_be16(pb);
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
            buf = av_malloc(iff_size + 1 + FF_INPUT_BUFFER_PADDING_SIZE);

            if (!buf)
                return AVERROR(ENOMEM);

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(&buf);
                return AVERROR(EIO);
            }

            buf[iff_size] = 0;

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
            if ((res = seq_get_metadata(s, &track->metadata, metadata_tag, iff_size)) < 0) {
                av_freep(&buf);
                av_log(track, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if ((res = avseq_track_data_open(track, last_row + 1)) < 0) {
        av_freep(&buf);
        return res;
    }

    if (buf && len) {
        if ((res = avseq_track_unpack(track, buf, len)) < 0) {
            av_freep(&buf);
            return res;
        }
    }

    av_freep(&buf);

    return 0;
}

static int open_song_posi(AVFormatContext *s, AVSequencerContext *avctx, AVSequencerSong *song, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    uint32_t iff_size = 4, channel = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_POST:
                if (channel >= song->channels) {
                    if ((res = avseq_song_set_channels(avctx, song, channel + 1)) < 0)
                        return res;
                }

                if ((res = open_posi_post(s, song, channel++, iff_size)) < 0)
                    return res;

                break;
            }

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_posi_post(AVFormatContext *s, AVSequencerSong *song, uint32_t channel, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerOrderList *order_list = song->order_list + channel;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
                if ((res = open_post_posl(s, order_list, iff_size)) < 0)
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
            if ((res = seq_get_metadata(s, &order_list->metadata, metadata_tag, iff_size)) < 0) {
                av_log(order_list, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_post_posl(AVFormatContext *s, AVSequencerOrderList *order_list, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerOrderData *order_data;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
            if ((res = seq_get_metadata(s, &order_list->metadata, metadata_tag, iff_size)) < 0) {
                av_log(order_list, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_tcm1_insl(AVFormatContext *s, AVSequencerModule *module, const char *args, void *opaque, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
                if ((res = open_insl_inst(s, module, args, opaque, iff_size)) < 0)
                    return res;

                break;
            }

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_insl_inst(AVFormatContext *s, AVSequencerModule *module, const char *args, void *opaque, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerInstrument *instrument;
    uint32_t iff_size = 4;
    uint8_t samples = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if (!(instrument = avseq_instrument_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_instrument_open(module, instrument, 0)) < 0) {
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
                if ((res = open_inst_samp(s, module, instrument, args, opaque, iff_size)) < 0)
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
            if ((res = seq_get_metadata(s, &instrument->metadata, metadata_tag, iff_size)) < 0) {
                av_log(instrument, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if (samples != instrument->samples) {
        av_log(instrument, AV_LOG_ERROR, "Number of attached samples does not match actual reads (expected: %d, got: %d)!\n", instrument->samples, samples);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int open_inst_samp(AVFormatContext *s, AVSequencerModule *module, AVSequencerInstrument *instrument, const char *args, void *opaque, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
                if ((res = open_samp_smpl(s, module, instrument, args, opaque, iff_size)) < 0)
                    return res;

                break;
            }

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_samp_smpl(AVFormatContext *s, AVSequencerModule *module, AVSequencerInstrument *instrument, const char *args, void *opaque, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerSample *sample;
    uint8_t *buf = NULL;
    uint32_t len = 0, iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if (!(sample = avseq_sample_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_sample_open(instrument, sample, NULL, 0)) < 0) {
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
            // TODO: Check if load samples parameter is true
            len = iff_size;
            buf = av_malloc(iff_size + FF_INPUT_BUFFER_PADDING_SIZE);

            if (!buf)
                return AVERROR(ENOMEM);

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(&buf);
                return AVERROR(EIO);
            }

            break;
        case ID_SMPR:
            sample->data = (int16_t *) get_be32(pb);

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_SNTH:
                if ((res = open_smpl_snth(s, sample, args, opaque, iff_size)) < 0) {
                    av_freep(&buf);
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
            if ((res = seq_get_metadata(s, &sample->metadata, metadata_tag, iff_size)) < 0) {
                av_freep(&buf);
                av_log(sample, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if (!sample->data) {
        // TODO: Load sample from demuxer/decoder pair
        if (!buf && sample->samples) {
            av_freep(&buf);
            av_log(sample, AV_LOG_ERROR, "No sample data found, but non-zero number of samples!\n");
            return AVERROR_INVALIDDATA;
        } else if (sample->bits_per_sample != 8) {
            if ((res = avseq_sample_data_open(sample, NULL, sample->samples)) < 0) {
                av_freep(&buf);
                return res;
            }

#if AV_HAVE_BIGENDIAN
            memcpy(sample->data, buf, len);
#else
            if (sample->bits_per_sample == 16) {
                int16_t *data    = sample->data;
                uint8_t *tmp_buf = buf;
                unsigned i       = FFALIGN(sample->samples, 4) >> 2;

                do {
                    int16_t v = AV_RB16(tmp_buf);
                    *data++   = v;
                    v         = AV_RB16(tmp_buf + 2);
                    *data++   = v;
                    v         = AV_RB16(tmp_buf + 4);
                    *data++   = v;
                    v         = AV_RB16(tmp_buf + 6);
                    *data++   = v;
                    tmp_buf   += 8;
                } while (--i);
            } else {
                int32_t *data    = (int32_t *) sample->data;
                uint8_t *tmp_buf = buf;
                unsigned i       = FFALIGN(sample->samples, 2) >> 1;

                do {
                    int32_t v = AV_RB32(tmp_buf);
                    *data++   = v;
                    v         = AV_RB32(tmp_buf + 4);
                    *data++   = v;
                    tmp_buf  += 8;
                } while (--i);
            }
#endif
        } else {
            if ((res = avseq_sample_data_open(sample, NULL, sample->samples)) < 0) {
                av_freep(&buf);
                return res;
            }

            memcpy(sample->data, buf, len);
        }

        if (sample->flags & 1) {
            sample->flags &= ~AVSEQ_SAMPLE_FLAG_REDIRECT;

            avseq_sample_decrunch(module, sample, 0);
        }
    }

    av_freep(&buf);

    return 0;
}

static int open_smpl_snth(AVFormatContext *s, AVSequencerSample *sample, const char *args, void *opaque, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerSynth *synth;
    uint8_t *buf = NULL;
    uint8_t *tmp_buf;
    uint32_t len = 0, iff_size = 4;
    uint16_t i, waveforms = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if ((res = avseq_synth_open(sample, 1, 0, 0)) < 0)
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
                av_freep(&buf);
                return AVERROR(EIO);
            }

            break;
        case ID_FORM:
            switch (get_le32(pb)) {
            case ID_WFRM:
                if ((res = open_snth_wfrm(s, synth, iff_size)) < 0) {
                    av_freep(&buf);
                    return res;
                }

                break;
            case ID_STAB:
                // TODO: Check if load synth sound symbols parameter is true
                if ((res = open_snth_stab(s, synth, iff_size)) < 0) {
                    av_freep(&buf);
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
            if ((res = seq_get_metadata(s, &synth->metadata, metadata_tag, iff_size)) < 0) {
                av_freep(&buf);
                av_log(synth, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if (waveforms != synth->waveforms) {
        av_freep(&buf);
        av_log(synth, AV_LOG_ERROR, "Number of attached waveforms does not match actual reads (expected: %d, got: %d)!\n", synth->waveforms, waveforms);
        return AVERROR_INVALIDDATA;
    }

    if (!buf) {
        av_freep(&buf);
        av_log(synth, AV_LOG_ERROR, "No synth sound code read!\n");
        return AVERROR_INVALIDDATA;
    } else if ((res = avseq_synth_code_open(synth, len >> 2)) < 0) {
        av_freep(&buf);
        return res;
    }

    tmp_buf = buf;

    for (i = 0; i < synth->size; ++i) {
        synth->code[i].instruction = *tmp_buf++;
        synth->code[i].src_dst_var = *tmp_buf++;
        synth->code[i].data        = AV_WN16A(&(synth->code[i].data), AV_RB16(tmp_buf));
        tmp_buf                   += 2;
    }

    av_freep(&buf);

    return 0;
}

static int open_snth_wfrm(AVFormatContext *s, AVSequencerSynth *synth, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
                if ((res = open_wfrm_wave(s, synth, iff_size)) < 0)
                    return res;

                break;
            }

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_wfrm_wave(AVFormatContext *s, AVSequencerSynth *synth, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerSynthWave *waveform;
    uint8_t *buf = NULL;
    uint32_t len = 0, iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if ((res = avseq_synth_waveform_open(synth, 1)) < 0)
        return res;

    waveform = synth->waveform_list[synth->waveforms - 1];

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
                av_freep(&buf);
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
            if ((res = seq_get_metadata(s, &waveform->metadata, metadata_tag, iff_size)) < 0) {
                av_freep(&buf);
                av_log(waveform, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if (!buf) {
        av_log(waveform, AV_LOG_ERROR, "No synth sound waveform data found!\n");
        return AVERROR_INVALIDDATA;
    } else if ((res = avseq_synth_waveform_data_open(waveform, (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT) ? len : len >> 1)) < 0) {
        av_freep(&buf);
        return res;
    }

#if AV_HAVE_BIGENDIAN
    memcpy(waveform->data, buf, len);
#else
    if (waveform->flags & AVSEQ_SYNTH_WAVE_FLAGS_8BIT) {
        memcpy(waveform->data, buf, len);
    } else {
        int16_t *data    = waveform->data;
        uint8_t *tmp_buf = buf;
        unsigned i       = FFALIGN(waveform->samples, 4) >> 2;

        do {
            int16_t v = AV_RB16(tmp_buf);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 2);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 4);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 6);
            *data++   = v;
            tmp_buf  += 8;
        } while (--i);
    }
#endif

    av_freep(&buf);

    return 0;
}

static int open_snth_stab(AVFormatContext *s, AVSequencerSynth *synth, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
                if ((res = open_stab_smbl(s, synth, iff_size)) < 0)
                    return res;

                break;
            }

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_stab_smbl(AVFormatContext *s, AVSequencerSynth *synth, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerSynthSymbolTable *symbol;
    uint8_t *buf = NULL;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if (!(symbol = avseq_synth_symbol_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_synth_symbol_open(synth, symbol, "UNNAMED")) < 0) {
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

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(&buf);
                return AVERROR(EIO);
            }

            buf[iff_size] = 0;

            break;
        default:
            // TODO: Add unknown chunk

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if ((res = avseq_synth_symbol_assign(synth, symbol, buf)) < 0) {
        av_freep(&buf);
        return res;
    }

    av_freep(&buf);

    return 0;
}

static int open_tcm1_envl(AVFormatContext *s, AVSequencerContext *avctx, AVSequencerModule *module, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
                if ((res = open_envl_envd(s, avctx, module, iff_size)) < 0)
                    return res;

                break;
            }

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_envl_envd(AVFormatContext *s, AVSequencerContext *avctx, AVSequencerModule *module, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerEnvelope *envelope;
    uint8_t *buf = NULL;
    uint8_t *node_buf = NULL;
    uint32_t len = 0, node_len = 0, iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if (!(envelope = avseq_envelope_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_envelope_open(avctx, module, envelope, 1, 0, 0, 0, 0)) < 0) {
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
                av_freep(&node_buf);
                return AVERROR(ENOMEM);
            }

            if (get_buffer(pb, buf, iff_size) < 0) {
                av_freep(&node_buf);
                av_freep(&buf);
                return AVERROR(EIO);
            }

            break;
        case ID_NODE:
            node_len = iff_size;
            node_buf = av_malloc(iff_size + FF_INPUT_BUFFER_PADDING_SIZE);

            if (!node_buf) {
                av_freep(&buf);
                return AVERROR(ENOMEM);
            }

            if (get_buffer(pb, node_buf, iff_size) < 0) {
                av_freep(&node_buf);
                av_freep(&buf);
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
            if ((res = seq_get_metadata(s, &envelope->metadata, metadata_tag, iff_size)) < 0) {
                av_freep(&node_buf);
                av_freep(&buf);
                av_log(envelope, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if (!buf && len) {
        av_freep(&node_buf);
        av_log(envelope, AV_LOG_ERROR, "No envelope data points found, but non-zero number of points!\n");
        return AVERROR_INVALIDDATA;
    } else if (!node_buf && node_len) {
        av_freep(&buf);
        av_log(envelope, AV_LOG_ERROR, "No envelope data node points found, but non-zero number of nodes!\n");
        return AVERROR_INVALIDDATA;
    } else if ((res = avseq_envelope_data_open(avctx, envelope, FFALIGN(len, 2) >> 1, 0, 0, 0, FFALIGN(node_len, 2) >> 1)) < 0) {
        av_freep(&node_buf);
        av_freep(&buf);
        return res;
    } else {
#if AV_HAVE_BIGENDIAN
        memcpy(envelope->data, buf, len);
        memcpy(envelope->node_points, node_buf, node_len);
#else
        int16_t *data    = envelope->data;
        uint8_t *tmp_buf = buf;
        unsigned i       = FFALIGN(envelope->points, 4) >> 2;

        do {
            int16_t v = AV_RB16(tmp_buf);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 2);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 4);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 6);
            *data++   = v;
            tmp_buf  += 8;
        } while (--i);

        data    = envelope->node_points;
        tmp_buf = node_buf;
        i       = FFALIGN(envelope->nodes, 4) >> 2;

        do {
            int16_t v = AV_RB16(tmp_buf);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 2);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 4);
            *data++   = v;
            v         = AV_RB16(tmp_buf + 6);
            *data++   = v;
            tmp_buf  += 8;
        } while (--i);
#endif
    }

    av_freep(&node_buf);
    av_freep(&buf);

    return 0;
}

static int open_tcm1_keyb(AVFormatContext *s, AVSequencerModule *module, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerKeyboard *keyboard;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id, keyboards;
        unsigned i;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_KBRD:
            if (!(keyboard = avseq_keyboard_create ()))
                return AVERROR(ENOMEM);

            if ((res = avseq_keyboard_open(module, keyboard)) < 0) {
                av_free(keyboard);
                return res;
            }

            keyboards = iff_size >> 2;

            if (keyboards > 120) {
                av_log(module, AV_LOG_ERROR, "Keyboard too large (maximum range of 10 octaves supported)!\n");
                return res;
            }

            for (i = 0; i < keyboards; ++i) {
                keyboard->key[i].sample = get_be16(pb);
                keyboard->key[i].octave = get_byte(pb);
                keyboard->key[i].note   = get_byte(pb);
            }

            break;
        default:
            // TODO: Add unknown chunk

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_tcm1_arpl(AVFormatContext *s, AVSequencerModule *module, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    uint32_t iff_size = 4;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

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
                if ((res = open_arpl_arpg(s, module, iff_size)) < 0)
                    return res;

                break;
            }

            break;
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    return 0;
}

static int open_arpl_arpg(AVFormatContext *s, AVSequencerModule *module, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerArpeggio *arpeggio;
    uint32_t iff_size = 4;
    uint16_t entries = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    if (!(arpeggio = avseq_arpeggio_create ()))
        return AVERROR(ENOMEM);

    if ((res = avseq_arpeggio_open(module, arpeggio, 1)) < 0) {
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
                if ((res = open_arpg_arpe(s, arpeggio, iff_size)) < 0)
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
            if ((res = seq_get_metadata(s, &arpeggio->metadata, metadata_tag, iff_size)) < 0) {
                av_log(arpeggio, AV_LOG_ERROR, "Cannot allocate metadata tag %s!\n", metadata_tag);
                return res;
            }
        }

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if (entries != arpeggio->entries) {
        av_log(arpeggio, AV_LOG_ERROR, "Number of attached arpeggio entries does not match actual reads (expected: %d, got: %d)!\n", arpeggio->entries, entries);
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

static int open_arpg_arpe(AVFormatContext *s, AVSequencerArpeggio *arpeggio, uint32_t data_size)
{
    ByteIOContext *pb = s->pb;
    AVSequencerArpeggioData *data;
    uint32_t iff_size = 4;
    uint16_t ticks = 0;
    int res;

    if (data_size < 4)
        return AVERROR_INVALIDDATA;

    data_size += data_size & 1;

    while (!url_feof(pb) && (data_size -= iff_size)) {
        uint64_t orig_pos;
        uint32_t chunk_id;

        chunk_id = get_le32(pb);
        iff_size = get_be32(pb);
        orig_pos = url_ftell(pb);

        switch(chunk_id) {
        case ID_ARPE:
            if (ticks) {
                if ((res = avseq_arpeggio_data_open(arpeggio, arpeggio->entries + 1)) < 0) {
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

        iff_size += iff_size & 1;
        url_fskip(pb, iff_size - (url_ftell(pb) - orig_pos));
        iff_size += 8;
    }

    if (!ticks) {
        av_log(arpeggio, AV_LOG_ERROR, "Attached arpeggio structure entries do not match actual reads!\n");
        return AVERROR_INVALIDDATA;
    }

    return 0;
}
#endif

static const char *nna_name[] = {"Cut", "Con", "Off", "Fde"};
static const char *note_name[] = {"--", "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
static const char *spec_note_name[] = {"END", "???", "???", "???", "???", "???", "???", "???", "???", "???", "???", "-\\-", "-|-", "===", "^^-", "^^^"};

static int iff_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    IffDemuxContext *iff = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st = s->streams[0];
    int ret;

    if(iff->sent_bytes >= iff->body_size)
        return AVERROR(EIO);
#if CONFIG_AVSEQUENCER
    if (iff->avctx) {
        int32_t row;
        uint16_t channel;
        AVSequencerPlayerGlobals *player_globals = iff->avctx->player_globals;
        AVSequencerPlayerHostChannel *player_host_channel = iff->avctx->player_host_channel;
        AVSequencerPlayerChannel *player_channel;
        char *buf;
        AVMixerData *mixer_data = iff->avctx->player_mixer_data;
        int size = st->codec->channels * mixer_data->mix_buf_size << 2;

        avseq_mixer_do_mix(mixer_data, NULL);

        if (!(buf = av_malloc(16 * iff->avctx->player_song->channels))) {
            av_log(s, AV_LOG_ERROR, "Cannot allocate pattern display buffer!\n");
            return AVERROR(ENOMEM);
        }

        snprintf(buf, 16 * iff->avctx->player_song->channels, "Row  ");

        for (channel = 0; channel < iff->avctx->player_song->channels; ++channel) {
            snprintf(buf + strlen(buf), 16 * iff->avctx->player_song->channels - strlen(buf), "%3d ", channel + 1);
        }

        av_log(NULL, AV_LOG_INFO, "\n\n\n %s\n", buf);

        for (row = FFMAX(FFMIN((int32_t) player_host_channel->row - 11, (int32_t) player_host_channel->max_row - 24), 0); row <= FFMIN(FFMAX((int32_t) player_host_channel->row + 12, 23), (int32_t) player_host_channel->max_row - 1); ++row) {
            if (row == player_host_channel->row)
                snprintf(buf, 16 * iff->avctx->player_song->channels, ">%04X", row);
            else
                snprintf(buf, 16 * iff->avctx->player_song->channels, " %04X", row);

            channel             = 0;

            do {
                if (player_host_channel->track) {
                    AVSequencerTrackRow *track_row = player_host_channel->track->data + row;

                    switch (track_row->note) {
                        case AVSEQ_TRACK_DATA_NOTE_NONE:
                            if (track_row->effects) {
                                AVSequencerTrackEffect *fx = track_row->effects_data[0];

                                if (fx->command || fx->data)
                                    snprintf(buf + strlen(buf), 16 * iff->avctx->player_song->channels - strlen(buf), "%02X%02X", fx->command, fx->data >> 8 ? fx->data >> 8 : fx->data & 0xFF);
                                else
                                    snprintf(buf + strlen(buf), 16 * iff->avctx->player_song->channels - strlen(buf), " ...");
                            } else {
                                snprintf(buf + strlen(buf), 16 * iff->avctx->player_song->channels - strlen(buf), " ...");
                            }

                            break;
                        case AVSEQ_TRACK_DATA_NOTE_C:
                        case AVSEQ_TRACK_DATA_NOTE_C_SHARP:
                        case AVSEQ_TRACK_DATA_NOTE_D:
                        case AVSEQ_TRACK_DATA_NOTE_D_SHARP:
                        case AVSEQ_TRACK_DATA_NOTE_E:
                        case AVSEQ_TRACK_DATA_NOTE_F:
                        case AVSEQ_TRACK_DATA_NOTE_F_SHARP:
                        case AVSEQ_TRACK_DATA_NOTE_G:
                        case AVSEQ_TRACK_DATA_NOTE_G_SHARP:
                        case AVSEQ_TRACK_DATA_NOTE_A:
                        case AVSEQ_TRACK_DATA_NOTE_A_SHARP:
                        case AVSEQ_TRACK_DATA_NOTE_B:
                            snprintf(buf + strlen(buf), 16 * iff->avctx->player_song->channels - strlen(buf), " %2s%1d", note_name[track_row->note], track_row->octave);

                            break;
                        case AVSEQ_TRACK_DATA_NOTE_KILL:
                        case AVSEQ_TRACK_DATA_NOTE_OFF:
                        case AVSEQ_TRACK_DATA_NOTE_KEYOFF:
                        case AVSEQ_TRACK_DATA_NOTE_HOLD_DELAY:
                        case AVSEQ_TRACK_DATA_NOTE_FADE:
                        case AVSEQ_TRACK_DATA_NOTE_END:
                            snprintf(buf + strlen(buf), 16 * iff->avctx->player_song->channels - strlen(buf), " %3s", spec_note_name[(uint8_t) track_row->note - 0xF0]);

                            break;
                        default:
                            snprintf(buf + strlen(buf), 16 * iff->avctx->player_song->channels - strlen(buf), " ???");

                            break;

                    }
                } else {
                    snprintf(buf + strlen(buf), 16 * iff->avctx->player_song->channels - strlen(buf), " ...");
                }

                player_host_channel++;
            } while (++channel < iff->avctx->player_song->channels);

            av_log(NULL, AV_LOG_INFO, "%s\n", buf);

            player_host_channel = iff->avctx->player_host_channel;
        }

        av_log(NULL, AV_LOG_INFO, "\nVch Frequency Position  Ch  Row  Tick Tm FVl Vl CV SV VE Fade Pn PE  NNA Tot\n");

        player_channel = iff->avctx->player_channel;
        channel        = 0;

        do {
            AVSequencerPlayerHostChannel *player_host_channel = iff->avctx->player_host_channel + player_channel->host_channel;

            if (player_channel->mixer.flags & AVSEQ_MIXER_CHANNEL_FLAG_PLAY) {
                if (player_channel->flags & AVSEQ_PLAYER_CHANNEL_FLAG_SURROUND)
                    av_log(NULL, AV_LOG_INFO, "%3d %9d %8d %3d  %04X %04X %02X %3d %02X %02X %02X %02X %04X Su %02X  %s %3d\n", channel + 1, player_channel->mixer.rate, player_channel->mixer.pos, player_channel->host_channel, player_host_channel->row, player_host_channel->tempo_counter, player_host_channel->tempo, player_channel->final_volume, player_channel->volume, player_host_channel->track_volume, player_channel->instr_volume / 255, (uint16_t) player_channel->vol_env.value / 256, player_channel->fade_out_count, (player_channel->pan_env.value >> 8) + 128, nna_name[player_host_channel->nna], player_host_channel->virtual_channels);
                else
                    av_log(NULL, AV_LOG_INFO, "%3d %9d %8d %3d  %04X %04X %02X %3d %02X %02X %02X %02X %04X %02X %02X  %s %3d\n", channel + 1, player_channel->mixer.rate, player_channel->mixer.pos, player_channel->host_channel, player_host_channel->row, player_host_channel->tempo_counter, player_host_channel->tempo, player_channel->final_volume, player_channel->volume, player_host_channel->track_volume, player_channel->instr_volume / 255, (uint16_t) player_channel->vol_env.value / 256, player_channel->fade_out_count, (uint8_t) player_channel->final_panning, (player_channel->pan_env.value >> 8) + 128, nna_name[player_host_channel->nna], player_host_channel->virtual_channels);
            } else {
                av_log(NULL, AV_LOG_INFO, "%3d                                                                  ---   0\n", channel + 1);
            }

            player_channel++;
        } while (++channel < FFMIN(iff->avctx->player_module->channels, 24));

        if (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_SPD_TIMING) {
            snprintf(buf, 16, "%d (%d)", player_globals->channels, player_globals->max_channels);

            if (player_globals->speed_mul < 2 && player_globals->speed_div < 2)
                 av_log(NULL, AV_LOG_INFO, "Active Channels: %-13s       Speed: %d (SPD)\n", buf, player_globals->spd_speed);
            else
                 av_log(NULL, AV_LOG_INFO, "Active Channels: %-13s       Speed: %d (%d/%d SPD)\n", buf, player_globals->spd_speed, player_globals->speed_mul, player_globals->speed_div);
        } else {
            snprintf(buf, 16, "%d (%d)", player_globals->channels, player_globals->max_channels);

            if (player_globals->speed_mul < 2 && player_globals->speed_div < 2)
                 av_log(NULL, AV_LOG_INFO, "Active Channels: %-13s       Speed: %d/%d (BpM)\n", buf, player_globals->bpm_speed, player_globals->bpm_tempo);
            else
                av_log(NULL, AV_LOG_INFO, "Active Channels: %-13s       Speed: %d/%d (%d/%d BpM)\n", buf, player_globals->bpm_speed, player_globals->bpm_tempo, player_globals->speed_mul, player_globals->speed_div);
        }

        av_free(buf);

        if (player_globals->flags & AVSEQ_PLAYER_GLOBALS_FLAG_SURROUND)
             av_log(NULL, AV_LOG_INFO, "  Global Volume: %3d        Global Panning: Su\n", player_globals->global_volume);
        else
             av_log(NULL, AV_LOG_INFO, "  Global Volume: %3d        Global Panning: %02X\n", player_globals->global_volume, (uint8_t) player_globals->global_panning);

        player_host_channel = iff->avctx->player_host_channel;

        av_log(NULL, AV_LOG_INFO, "\033[%dA\n", FFMIN(iff->avctx->player_module->channels, 24) + 33);

        if ((ret = av_new_packet(pkt, size)) < 0) {
            av_log(s, AV_LOG_ERROR, "Cannot allocate packet!\n");

            return ret;
        }

        memcpy(pkt->data, mixer_data->mix_buf, size);

        if (iff->sent_bytes == 0)
            pkt->flags |= AV_PKT_FLAG_KEY;

        iff->sent_bytes        += size;
        pkt->duration           = mixer_data->mix_buf_size;
        st->time_base           = (AVRational) {1, st->codec->sample_rate};
        pkt->stream_index       = 0;
        pkt->pts                = iff->audio_frame_count;
        iff->audio_frame_count += mixer_data->mix_buf_size;

        return size;
    }
#endif

    if(st->codec->channels == 2) {
        uint8_t sample_buffer[PACKET_SIZE];

        ret = get_buffer(pb, sample_buffer, PACKET_SIZE);
        if(av_new_packet(pkt, PACKET_SIZE) < 0) {
            av_log(s, AV_LOG_ERROR, "iff: cannot allocate packet \n");
            return AVERROR(ENOMEM);
        }
        interleave_stereo(sample_buffer, pkt->data, PACKET_SIZE);
    } else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        ret = av_get_packet(pb, pkt, iff->body_size);
    } else {
        ret = av_get_packet(pb, pkt, PACKET_SIZE);
    }

    if(iff->sent_bytes == 0)
        pkt->flags |= AV_PKT_FLAG_KEY;

    if(st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        iff->sent_bytes += PACKET_SIZE;
    } else {
        iff->sent_bytes = iff->body_size;
    }
    pkt->stream_index = 0;
    if(st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
        pkt->pts = iff->audio_frame_count;
        iff->audio_frame_count += ret / st->codec->channels;
    }
    return ret;
}

AVInputFormat iff_demuxer = {
    "IFF",
    NULL_IF_CONFIG_SMALL("IFF format"),
    sizeof(IffDemuxContext),
    iff_probe,
    iff_read_header,
    iff_read_packet,
};
