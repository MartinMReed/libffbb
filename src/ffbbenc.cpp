/* Copyright (c) 2012 Martin M Reed
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ffbbenc.h"

#include <deque>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef struct
{
    bool running;
    pthread_mutex_t reading_mutex;
    pthread_cond_t read_cond;
    std::deque<AVFrame*> frames;
    void (*write_callback)(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg);
    void *write_callback_arg;
    void (*close_callback)(ffenc_context *ffe_context, void *arg);
    void *close_callback_arg;
} ffenc_reserved;

void* encoding_thread(void* arg);

ffenc_context *ffenc_alloc()
{
    ffenc_context *ffe_context = (ffenc_context*) malloc(sizeof(ffenc_context));
    memset(ffe_context, 0, sizeof(ffenc_context));

    ffenc_reset(ffe_context);

    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    pthread_mutex_init(&ffe_reserved->reading_mutex, 0);
    pthread_cond_init(&ffe_reserved->read_cond, 0);

    return ffe_context;
}

void ffenc_reset(ffenc_context *ffe_context)
{
    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    if (!ffe_reserved) ffe_reserved = (ffenc_reserved*) malloc(sizeof(ffenc_reserved));
    memset(ffe_reserved, 0, sizeof(ffenc_reserved));

    memset(ffe_context, 0, sizeof(ffenc_context));
    ffe_context->reserved = ffe_reserved;
}

ffenc_error ffenc_set_close_callback(ffenc_context *ffe_context,
        void (*close_callback)(ffenc_context *ffe_context, void *arg),
        void *arg)
{
    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    if (!ffe_reserved) return FFENC_NOT_INITIALIZED;
    ffe_reserved->close_callback = close_callback;
    ffe_reserved->close_callback_arg = arg;
    return FFENC_OK;
}

ffenc_error ffenc_set_write_callback(ffenc_context *ffe_context,
        void (*write_callback)(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg),
        void *arg)
{
    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    if (!ffe_reserved) return FFENC_NOT_INITIALIZED;
    ffe_reserved->write_callback = write_callback;
    ffe_reserved->write_callback_arg = arg;
    return FFENC_OK;
}

ffenc_error ffenc_close(ffenc_context *ffe_context)
{
    AVCodecContext *codec_context = ffe_context->codec_context;

    if (codec_context)
    {
        if (avcodec_is_open(codec_context))
        {
            avcodec_close(codec_context);
        }

        av_free(codec_context);
        codec_context = ffe_context->codec_context = NULL;
    }

    return FFENC_OK;
}

ffenc_error ffenc_free(ffenc_context *ffe_context)
{
    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    pthread_mutex_destroy(&ffe_reserved->reading_mutex);
    pthread_cond_destroy(&ffe_reserved->read_cond);
    free(ffe_reserved);
    ffe_reserved = (ffenc_reserved*) NULL;
    ffe_context->reserved = NULL;
    free(ffe_context);
    return FFENC_OK;
}

ffenc_error ffenc_start(ffenc_context *ffe_context)
{
    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    if (!ffe_reserved) return FFENC_NOT_INITIALIZED;
    if (ffe_reserved->running) return FFENC_ALREADY_RUNNING;
    if (!ffe_context->codec_context) return FFENC_NO_CODEC_SPECIFIED;

    ffe_reserved->running = true;
    ffe_reserved->frames.clear();

    pthread_t pthread;
    pthread_create(&pthread, 0, &encoding_thread, ffe_context);

    return FFENC_OK;
}

ffenc_error ffenc_stop(ffenc_context *ffe_context)
{
    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    if (!ffe_reserved) return FFENC_NOT_INITIALIZED;
    if (!ffe_reserved->running) return FFENC_ALREADY_STOPPED;

    ffe_reserved->running = false;

    pthread_cond_signal(&ffe_reserved->read_cond);

    return FFENC_OK;
}

void* encoding_thread(void* arg)
{
    ffenc_context* ffe_context = (ffenc_context*) arg;
    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    AVCodecContext *codec_context = ffe_context->codec_context;

    int encode_buffer_len = 10000000;
    uint8_t *encode_buffer = (uint8_t *) av_malloc(encode_buffer_len);

    AVPacket packet;
    int got_packet;

    while (ffe_reserved->running || !ffe_reserved->frames.empty())
    {
        if (ffe_reserved->frames.empty())
        {
            pthread_mutex_lock(&ffe_reserved->reading_mutex);
            pthread_cond_wait(&ffe_reserved->read_cond, &ffe_reserved->reading_mutex);
            pthread_mutex_unlock(&ffe_reserved->reading_mutex);
            continue;
        }

        AVFrame *frame = ffe_reserved->frames.front();
        ffe_reserved->frames.pop_front();

        // reset the AVPacket
        av_init_packet(&packet);
        packet.data = encode_buffer;
        packet.size = encode_buffer_len;

        got_packet = 0;
        int encode_result = avcodec_encode_video2(codec_context, &packet, frame, &got_packet);

        if (encode_result == 0 && got_packet > 0)
        {
            if (ffe_reserved->write_callback) ffe_reserved->write_callback(ffe_context,
                    packet.data, packet.size, ffe_reserved->write_callback_arg);
        }

        free(frame->data[0]);
        av_free(frame);
        frame = NULL;
    }

    do
    {
        // reset the AVPacket
        av_init_packet(&packet);
        packet.data = encode_buffer;
        packet.size = encode_buffer_len;

        got_packet = 0;
        int encode_result = avcodec_encode_video2(codec_context, &packet, NULL, &got_packet);

        if (encode_result == 0 && got_packet > 0)
        {
            if (ffe_reserved->write_callback) ffe_reserved->write_callback(ffe_context,
                    packet.data, packet.size, ffe_reserved->write_callback_arg);
        }
    }
    while (got_packet > 0);

    av_free(encode_buffer);
    encode_buffer = NULL;
    encode_buffer_len = 0;

    if (ffe_reserved->close_callback) ffe_reserved->close_callback(
            ffe_context, ffe_reserved->close_callback_arg);

    return 0;
}

ffenc_error ffenc_add_frame(ffenc_context *ffe_context, AVFrame *frame)
{
    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    if (!ffe_reserved) return FFENC_NOT_INITIALIZED;
    if (!ffe_reserved->running) return FFENC_NOT_RUNNING;
    ffe_reserved->frames.push_back(frame);
    pthread_cond_signal(&ffe_reserved->read_cond);
    return FFENC_OK;
}

ffenc_error ffenc_add_frame(ffenc_context *ffe_context, camera_buffer_t* buf)
{
    if (buf->frametype != CAMERA_FRAMETYPE_NV12) return FFENC_FRAME_NOT_SUPPORTED;

    ffenc_reserved *ffe_reserved = (ffenc_reserved*) ffe_context->reserved;
    if (!ffe_reserved) return FFENC_NOT_INITIALIZED;
    if (!ffe_reserved->running) return FFENC_NOT_RUNNING;

    int64_t uv_offset = buf->framedesc.nv12.uv_offset;
    uint32_t height = buf->framedesc.nv12.height;
    uint32_t width = buf->framedesc.nv12.width;
    uint32_t stride = buf->framedesc.nv12.stride;

    uint32_t _stride = width;
    int64_t _uv_offset = _stride * height;

    AVFrame *frame = avcodec_alloc_frame();

    frame->pts = buf->frametimestamp;

    frame->linesize[0] = _stride;
    frame->linesize[1] = _stride / 2;
    frame->linesize[2] = _stride / 2;

    frame->data[0] = (uint8_t*) malloc(width * height * 3 / 2);
    frame->data[1] = &frame->data[0][_uv_offset];
    frame->data[2] = &frame->data[0][_uv_offset + ((width * height) / 4)];

    for (uint32_t i = 0; i < height; i++)
    {
        int64_t doff = i * _stride;
        int64_t soff = i * stride;
        memcpy(&frame->data[0][doff], &buf->framebuf[soff], width);
    }

    uint8_t *srcuv = &buf->framebuf[uv_offset];
    uint8_t *destu = frame->data[1];
    uint8_t *destv = frame->data[2];

    for (uint32_t i = 0; i < height / 2; i++)
    {
        uint8_t* curuv = srcuv;
        for (uint32_t j = 0; j < width / 2; j++)
        {
            *destu++ = *curuv++;
            *destv++ = *curuv++;
        }
        srcuv += stride;
    }

    ffe_reserved->frames.push_back(frame);

    pthread_cond_signal(&ffe_reserved->read_cond);

    return FFENC_OK;
}

