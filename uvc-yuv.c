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

extern ring_t* yuv_msgr;

/* Enable or disable frame reference counting. You are not supposed to support
 * both paths in your application but pick the one most appropriate to your
 * needs. Look for the use of refcount in this example to see what are the
 * differences of API usage between them. */
static int refcount = 0;

static void save_yuv_pic_to_file(uint8_t *data, uint32_t size)
{
    FILE *img_dst_file = NULL;
    char img_name[128]={0};
    sprintf(img_name, "/home/pi/work/video-test/aa%04d.yuv",  ++jpg_counter);
    printf("write img_name: %s\n", img_name);

    img_dst_file = fopen(img_name, "wb");
    if (!img_dst_file) 
    {
        fprintf(stderr, "Could not open destination file %s\n", img_name);
    }
    else
    {
        fwrite(data, 1, size, img_dst_file);
        fclose(img_dst_file);
    }
}

static void save_yuv_pic_to_ring(uint8_t *data, uint32_t size)
{
    while (enring(yuv_msgr, data, size) == false)
    {
        printf("save_yuv_pic_to_ring fail sleep\n");
        usleep(2000000);
    }
    printf("ring->in:%d, ring->out:%d, ring->size:%d\n", yuv_msgr->in, yuv_msgr->out, yuv_msgr->size);
    printf("save_yuv_pic_to_ring succeeded , %d\n", ++jpg_counter);

}

static int decode_packet(int *got_frame, int cached)
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

        if (*got_frame) {

            if (frame->width != width || frame->height != height ||
                frame->format != pix_fmt) {
                /* To handle this change, one could call av_image_alloc again and
                 * decode the following frames into another rawvideo file. */
                fprintf(stderr, "Error: Width, height and pixel format have to be "
                        "constant in a rawvideo file, but the width, height or "
                        "pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        frame->width, frame->height,
                        av_get_pix_fmt_name(frame->format));
                return -1;
            }
            #if 0
            printf("pixel format of the input video changed:\n"
                        "old: width = %d, height = %d, format = %s\n"
                        "new: width = %d, height = %d, format = %s\n",
                        width, height, av_get_pix_fmt_name(pix_fmt),
                        frame->width, frame->height,
                        av_get_pix_fmt_name(frame->format));
            #endif

            printf("srcframe linesize:%d, %d, %d, %d\n", frame->linesize[0],frame->linesize[1],
                frame->linesize[2],frame->linesize[3]);

            printf("video_frame%s n:%d coded_n:%d\n",
                   cached ? "(cached)" : "",
                   video_frame_count++, frame->coded_picture_number);

            if (!swsContext)
            {
                printf("swsContext init before\n");
                swsContext = sws_getContext(width, height, pix_fmt,
                                        width, height, AV_PIX_FMT_YUYV422,
                                        SWS_BILINEAR, NULL, NULL, NULL);
                printf("swsContext init after\n");
            }

            dstframe->format = AV_PIX_FMT_YUYV422; /* choose same format set on sws_getContext() */
            dstframe->width  = width; /* must match sizes as on sws_getContext() */
            dstframe->height = height; /* must match sizes as on sws_getContext() */
            
            int ret = av_frame_get_buffer(dstframe, 32);
            if (ret < 0)
            {
                fprintf(stderr, "Error: could not allocate the video frame data\n");
                return -1;
            }
            

            /* do the conversion */
            ret = sws_scale(swsContext,          /* SwsContext* on step (1) */
                            (const uint8_t * const *)frame->data, /* srcSlice[] from decoded AVFrame */
                            frame->linesize,     /* srcStride[] from decoded AVFrame */
                            0,                      /* srcSliceY   */
                            height,              /* srcSliceH  from decoded AVFrame */
                            dstframe->data,      /* dst[]       */
                            dstframe->linesize); /* dstStride[] */
            if (ret < 0)
            {
                /* error handling */
                fprintf(stderr, "Error: convert yuv from 420p to yuyv422 Failed!!!\n");
                return -1;

            }
            printf("dstframe linesize:%d, %d, %d, %d\n", dstframe->linesize[0],dstframe->linesize[1],
                dstframe->linesize[2],dstframe->linesize[3]);
            printf("video_dst_bufsize: %d\n", video_dst_bufsize);
            /* copy decoded frame to destination buffer:
             * this is required since rawvideo expects non aligned data */
            av_image_copy(video_dst_data, video_dst_linesize,
                          (const uint8_t **)(dstframe->data), dstframe->linesize,
                          dstframe->format, width, height);
            #if 0
            save_yuv_pic_to_file(video_dst_data[0], width*height*2);
            #else
            save_yuv_pic_to_ring(video_dst_data[0], width*height*2);
            #endif
            av_frame_unref(dstframe);
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

int uvc_yuv_main(void *arg)
{
    int ret = 0, got_frame;

    int *thread_id = (int *)arg; 

    thread_bind_core(*thread_id);

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
            ret = decode_packet(&got_frame, 0);
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
        decode_packet(&got_frame, 1);
        printf("aaaaa, got_frame:%d\n",got_frame);

    } while (got_frame);

    printf("Demuxing succeeded.\n");

end:
    avcodec_free_context(&video_dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_free(video_dst_data[0]);

    return ret < 0;
}
