/*
 * Copyright (c) 2012 Stefano Sabatini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * Demuxing and decoding example.
 *
 * Show how to use the libavformat and libavcodec API to demux and
 * decode audio and video data.
 * @example demuxing_decoding.c
 */
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>

#include "thread_bind_core.h"
#include "mq_ring.h"


static AVFormatContext *fmt_ctx = NULL;
static AVCodecContext *video_dec_ctx = NULL;
static int width, height;
static enum AVPixelFormat pix_fmt;
static AVStream *video_stream = NULL;
static const char *src_filename = NULL;

static uint8_t *video_dst_data[4] = {NULL};
static int      video_dst_linesize[4];
static int video_dst_bufsize;

static int video_stream_idx = -1, audio_stream_idx = -1;
static AVFrame *frame = NULL; /* AV_PIX_FMT_YUV420P */

static AVPacket pkt;
static int video_frame_count = 0;
static int jpg_counter       = 0; 

static struct SwsContext *swsContext = NULL;
static AVFrame *dstframe = NULL; /* AV_PIX_FMT_YUYV422 */

extern ring_t* jpg_msgr;
static uint8_t ring_temp_buff[640*480] ={0};

/* Enable or disable frame reference counting. You are not supposed to support
 * both paths in your application but pick the one most appropriate to your
 * needs. Look for the use of refcount in this example to see what are the
 * differences of API usage between them. */
static int refcount = 0;

static void save_jpg_pic_to_ring(uint8_t *data, uint32_t size)
{
    while (enring(jpg_msgr, data, size) == false)
    {
        printf("save_jpg_pic_to_ring fail sleep\n");
        usleep(2000000);
    }
    printf("ring->in:%d, ring->out:%d, ring->size:%d\n", jpg_msgr->in, jpg_msgr->out, jpg_msgr->size);
    printf("save_jpg_pic_to_ring succeeded , size=%d   %d\n", size, jpg_counter);

}

static int
uvc_jpg_fill_img(char *img_name)
{
    int fd = -1;
    unsigned int imgsize = 0;
    fd = open(img_name, O_RDONLY);
    if (fd == -1) {
        printf("Unable to open MJPEG image '%s'\n", img_name);
        return -1;
    }

    imgsize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    read(fd, ring_temp_buff, imgsize);
    close(fd);
    save_jpg_pic_to_ring(ring_temp_buff, imgsize);
    return 0;
}

/**
 * 将AVFrame(YUV420格式)保存为JPEG格式的图片
 *
 * @param width YUV420的宽
 * @param height YUV42的高
 *
 */

//extern AVCodec ff_mjpeg_vaapi_encoder;
static int MyWriteJPEG(int *got_frame, int cached)
{

    int ret = 0;
    int decoded = pkt.size;

    *got_frame = 0;

    if (pkt.stream_index == video_stream_idx) 
    {
        /* decode video frame */
        ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame)
        {
            // 输出文件路径
            char out_file[128] = {0};
            sprintf(out_file, "/var/temp/aa%04d.jpg",jpg_counter++);
            printf("out_file:%s\n", out_file);
            // 分配AVFormatContext对象
            AVFormatContext *pFormatCtx = avformat_alloc_context();

            // 设置输出文件格式
            pFormatCtx->oformat = av_guess_format("mjpeg", NULL, NULL);
            // 创建并初始化一个和该url相关的AVIOContext
            if (avio_open(&pFormatCtx->pb, out_file, AVIO_FLAG_READ_WRITE) < 0) {
                printf("Couldn't open output file.\n");
                return -1;
            }

            // 构建一个新stream
            AVStream *pAVStream = avformat_new_stream(pFormatCtx, 0);
            if (pAVStream == NULL) {
                return -1;
            }

            // 设置该stream的信息
            AVCodecContext *pCodecCtx = pAVStream->codec;

            pCodecCtx->codec_id = pFormatCtx->oformat->video_codec;
            pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
            pCodecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
            pCodecCtx->width = width;
            pCodecCtx->height = height;
            pCodecCtx->time_base.num = 1;
            pCodecCtx->time_base.den = 25;

            // Begin Output some information
            av_dump_format(pFormatCtx, 0, out_file, 1);
            // End Output some information

            // 查找解码器
            printf("AV_CODEC_ID_MJPEG:%d, codec_id:%d\n", AV_CODEC_ID_MJPEG, pCodecCtx->codec_id);
            AVCodec *pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
            //AVCodec *pCodec = &ff_mjpeg_vaapi_encoder;
            if (!pCodec) {
                printf("Codec not found.\n");
                return -1;
            }
            // 设置pCodecCtx的解码器为pCodec
            if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
                printf("Could not open codec.\n");
                return -1;
            }

            //Write Header
            avformat_write_header(pFormatCtx, NULL);

            int y_size = pCodecCtx->width * pCodecCtx->height;

            //Encode
            // 给AVPacket分配足够大的空间
            AVPacket pkt1;
            av_new_packet(&pkt1, y_size * 3);

            //
            int got_picture = 0;
            int ret = avcodec_encode_video2(pCodecCtx, &pkt1, frame, &got_picture);
            if (ret < 0) {
                printf("Encode Error.\n");
                return -1;
            }
            if (got_picture == 1) {
                //pkt.stream_index = pAVStream->index;
                ret = av_write_frame(pFormatCtx, &pkt1);
            }

            av_free_packet(&pkt1);

            //Write Trailer
            av_write_trailer(pFormatCtx);

            printf("Encode Successful.\n");

            if (pAVStream) {
                avcodec_close(pAVStream->codec);
            }
            avio_close(pFormatCtx->pb);
            avformat_free_context(pFormatCtx);
            //uvc_jpg_fill_img(out_file);

        }
    } 
    else if (pkt.stream_index == audio_stream_idx) 
    {
    }

    /* If we use frame reference counting, we own the data and need
     * to de-reference it when we don't use it anymore */
    if (*got_frame && refcount)
        av_frame_unref(frame);

    return decoded;
}

static int open_codec_context(int *stream_idx,
                              AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret, stream_index;
    AVStream *st;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), src_filename);
        return ret;
    } else {
        stream_index = ret;
        st = fmt_ctx->streams[stream_index];

        /* find decoder for the stream */
        dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Allocate a codec context for the decoder */
        *dec_ctx = avcodec_alloc_context3(dec);
        if (!*dec_ctx) {
            fprintf(stderr, "Failed to allocate the %s codec context\n",
                    av_get_media_type_string(type));
            return AVERROR(ENOMEM);
        }

        /* Copy codec parameters from input stream to output codec context */
        if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0) {
            fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                    av_get_media_type_string(type));
            return ret;
        }

        /* Init the decoders, with or without reference counting */
        av_dict_set(&opts, "refcounted_frames", refcount ? "1" : "0", 0);
        if ((ret = avcodec_open2(*dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
        *stream_idx = stream_index;
    }

    return 0;
}

int clear_jpg_dir(void)
{
    FILE *fp; /* FILE stream for popen */
    char *cmdstring = "rm -rf /var/temp/ && mkdir -p /var/temp";

    /* Create the pipe */
    if ((fp = popen(cmdstring, "r")) == NULL) {
        printf("popen fail\n");
        return -1;
    }

    /* Close and frap the exit status */
    pclose(fp);
    return 0;
}

#define BUFSZ PIPE_BUF
int gen_jpg(void)
{
    FILE *fp; /* FILE stream for popen */
    char *cmdstring = "ffmpeg -i /home/pi/work/uvc-gadget/test-640x480.mp4 -vf fps=30 /var/temp/aa%04d.jpg";
    char buf[BUFSZ];

    /* Create the pipe */
    if ((fp = popen(cmdstring, "r")) == NULL) {
        printf("gen jpg fail\n");
        return -1;
    }

    /* Read cmdstring's output */
    while ((fgets(buf, BUFSZ, fp)) != NULL) {
        printf("%s", buf);
    }

    /* Close and frap the exit status */
    pclose(fp);
    return 0;
}

int uvc_jpg_main(void *arg)
{
    int ret = 0, got_frame;

    int *thread_id = (int *)arg; 

    thread_bind_core(*thread_id);

    if (clear_jpg_dir() < 0)
    {
        printf("clear jpg dir fail\n");
        exit(1);
    }

    if (gen_jpg() < 0)
    {
        printf("gen jpg  fail\n");
        exit(1);
    }

    return 0;

    src_filename = "/home/pi/work/uvc-gadget/test-640x480.mp4";
    //src_filename = "/home/pi/work/uvc-gadget/test-320x240.mp4";



    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, src_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", src_filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    if (open_codec_context(&video_stream_idx, &video_dec_ctx, fmt_ctx, AVMEDIA_TYPE_VIDEO) >= 0) {
        video_stream = fmt_ctx->streams[video_stream_idx];


        /* allocate image where the decoded image will be put */
        width = video_dec_ctx->width;
        height = video_dec_ctx->height;
        pix_fmt = video_dec_ctx->pix_fmt;

        ret = av_image_alloc(video_dst_data, video_dst_linesize,
                             width, height, AV_PIX_FMT_YUYV422, 1);
        if (ret < 0) {
            fprintf(stderr, "Could not allocate raw video buffer\n");
            goto end;
        }
        video_dst_bufsize = ret;
    }


    /* dump input information to stderr */
    av_dump_format(fmt_ctx, 0, src_filename, 0);

    if (!video_stream) {
        fprintf(stderr, "Could not find audio or video stream in the input, aborting\n");
        ret = 1;
        goto end;
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    dstframe = av_frame_alloc();
    if (dstframe == NULL)
    {
        fprintf(stderr, "Could not allocate dstframe\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* initialize packet, set data to NULL, let the demuxer fill it */
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    if (video_stream)
        printf("Demuxing video from file '%s'\n", src_filename);

    /* read frames from the file */
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            printf("aaaaa, got_frame:%d\n",got_frame);
            ret = MyWriteJPEG(&got_frame, 0);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
        printf("bbbbb, got_frame:%d\n",got_frame);

    }

    /* flush cached frames */
    pkt.data = NULL;
    pkt.size = 0;
    do {
        MyWriteJPEG(&got_frame, 1);
        printf("cccc, got_frame:%d\n",got_frame);

    } while (got_frame);

    printf("Demuxing succeeded.\n");

end:
    avcodec_free_context(&video_dec_ctx);
    //avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_free(video_dst_data[0]);

    return ret < 0;
}
