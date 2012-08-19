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

#include "ffcamapi.h"

#include <fcntl.h>
#include <sys/stat.h>

typedef struct
{
    bool running;
    pthread_mutex_t reading_mutex;
    pthread_cond_t read_cond;
    int frame_count;
    std::deque<camera_buffer_t*> frames;
    void (*write_callback)(ffcamera_context *ffc_context, const uint8_t *buf, ssize_t size, void *arg);
    void *write_callback_arg;
    void (*close_callback)(ffcamera_context *ffc_context, void *arg);
    void *close_callback_arg;
} ffcamera_reserved;

void* encoding_thread(void* arg);

void ffcamera_init(ffcamera_context *ffc_context)
{
    ffcamera_reserved *ffc_reserved = (ffcamera_reserved*) ffc_context->reserved;
    if (!ffc_reserved) ffc_reserved = (ffcamera_reserved*) malloc(sizeof(ffcamera_reserved));
    memset(ffc_reserved, 0, sizeof(ffcamera_reserved));

    memset(ffc_context, 0, sizeof(ffcamera_context));
    ffc_context->reserved = ffc_reserved;

    pthread_mutex_init(&ffc_reserved->reading_mutex, 0);
    pthread_cond_init(&ffc_reserved->read_cond, 0);
}

ffcamera_error ffcamera_set_close_callback(ffcamera_context *ffc_context,
        void (*close_callback)(ffcamera_context *ffc_context, void *arg),
        void *arg)
{
    ffcamera_reserved *ffc_reserved = (ffcamera_reserved*) ffc_context->reserved;
    if (!ffc_reserved) return FFCAMERA_NOT_INITIALIZED;
    ffc_reserved->close_callback = close_callback;
    ffc_reserved->close_callback_arg = arg;
    return FFCAMERA_OK;
}

ffcamera_error ffcamera_set_write_callback(ffcamera_context *ffc_context,
        void (*write_callback)(ffcamera_context *ffc_context, const uint8_t *buf, ssize_t size, void *arg),
        void *arg)
{
    ffcamera_reserved *ffc_reserved = (ffcamera_reserved*) ffc_context->reserved;
    if (!ffc_reserved) return FFCAMERA_NOT_INITIALIZED;
    ffc_reserved->write_callback = write_callback;
    ffc_reserved->write_callback_arg = arg;
    return FFCAMERA_OK;
}

ffcamera_error ffcamera_close(ffcamera_context *ffc_context)
{
    AVCodecContext *codec_context = ffc_context->codec_context;

    if (codec_context)
    {
        if (avcodec_is_open(codec_context))
        {
            avcodec_close(codec_context);
        }

        av_free(codec_context);
        codec_context = ffc_context->codec_context = NULL;
    }

    return FFCAMERA_OK;
}

ffcamera_error ffcamera_free(ffcamera_context *ffc_context)
{
    ffcamera_reserved *ffc_reserved = (ffcamera_reserved*) ffc_context->reserved;
    pthread_mutex_destroy(&ffc_reserved->reading_mutex);
    pthread_cond_destroy(&ffc_reserved->read_cond);
    free(ffc_reserved);
    ffc_reserved = (ffcamera_reserved*) NULL;
    ffc_context->reserved = NULL;
    free(ffc_context);
    return FFCAMERA_OK;
}

ffcamera_error ffcamera_start(ffcamera_context *ffc_context)
{
    ffcamera_reserved *ffc_reserved = (ffcamera_reserved*) ffc_context->reserved;
    if (!ffc_reserved) return FFCAMERA_NOT_INITIALIZED;
    if (ffc_reserved->running) return FFCAMERA_ALREADY_RUNNING;
    if (!ffc_context->codec_context) return FFCAMERA_NO_CODEC_SPECIFIED;

    ffc_reserved->frame_count = 0;
    ffc_reserved->running = true;
    ffc_reserved->frames.clear();

    pthread_t pthread;
    pthread_create(&pthread, 0, &encoding_thread, ffc_context);

    return FFCAMERA_OK;
}

ffcamera_error ffcamera_stop(ffcamera_context *ffc_context)
{
    ffcamera_reserved *ffc_reserved = (ffcamera_reserved*) ffc_context->reserved;
    if (!ffc_reserved) return FFCAMERA_NOT_INITIALIZED;
    if (!ffc_reserved->running) return FFCAMERA_ALREADY_STOPPED;

    ffc_reserved->running = false;

    pthread_cond_signal(&ffc_reserved->read_cond);

    return FFCAMERA_OK;
}

ssize_t write_all(int fd, const uint8_t *buf, ssize_t size)
{
    ssize_t i = 0;
    do
    {
        ssize_t j = write(fd, buf + i, size - i);
        if (j < 0) return j;
        i += j;
    }
    while (i < size);
    return i;
}

void* encoding_thread(void* arg)
{
    ffcamera_context* ffc_context = (ffcamera_context*) arg;
    ffcamera_reserved *ffc_reserved = (ffcamera_reserved*) ffc_context->reserved;

    int encode_buffer_len = 10000000;
    uint8_t *encode_buffer = (uint8_t *) av_malloc(encode_buffer_len);

    AVFrame *frame = avcodec_alloc_frame();

    AVPacket packet;
    int got_packet;
    int success;

    while (ffc_reserved->running || !ffc_reserved->frames.empty())
    {
        if (ffc_reserved->frames.empty())
        {
            pthread_mutex_lock(&ffc_reserved->reading_mutex);
            pthread_cond_wait(&ffc_reserved->read_cond, &ffc_reserved->reading_mutex);
            pthread_mutex_unlock(&ffc_reserved->reading_mutex);
            continue;
        }

        camera_buffer_t *buf = ffc_reserved->frames.front();
        ffc_reserved->frames.pop_front();

        int frame_position = ffc_reserved->frame_count++;

        int64_t uv_offset = buf->framedesc.nv12.uv_offset;
        uint32_t height = buf->framedesc.nv12.height;
        uint32_t width = buf->framedesc.nv12.width;

        frame->pts = frame_position;

        frame->linesize[0] = width;
        frame->linesize[1] = width / 2;
        frame->linesize[2] = width / 2;

        frame->data[0] = buf->framebuf;
        frame->data[1] = &buf->framebuf[uv_offset];
        frame->data[2] = &buf->framebuf[uv_offset + ((width * height) / 4)];

        // reset the AVPacket
        av_init_packet(&packet);
        packet.data = encode_buffer;
        packet.size = encode_buffer_len;

        got_packet = 0;
        success = avcodec_encode_video2(ffc_context->codec_context, &packet, frame, &got_packet);

        if (success == 0 && got_packet > 0)
        {
            if (!ffc_reserved->write_callback) write_all(ffc_context->fd, packet.data, packet.size);
            else ffc_reserved->write_callback(ffc_context, packet.data, packet.size,
                    ffc_reserved->write_callback_arg);
        }

        free(buf->framebuf);
        free(buf);
        buf = NULL;
    }

    av_free(frame);
    frame = NULL;

    do
    {
        ffc_reserved->frame_count++;

        // reset the AVPacket
        av_init_packet(&packet);
        packet.data = encode_buffer;
        packet.size = encode_buffer_len;

        got_packet = 0;
        success = avcodec_encode_video2(ffc_context->codec_context, &packet, NULL, &got_packet);

        if (success == 0 && got_packet > 0)
        {
            if (!ffc_reserved->write_callback) write_all(ffc_context->fd, packet.data, packet.size);
            else ffc_reserved->write_callback(ffc_context, packet.data, packet.size,
                    ffc_reserved->write_callback_arg);
        }
    }
    while (got_packet > 0);

    av_free(encode_buffer);
    encode_buffer = NULL;
    encode_buffer_len = 0;

    if (ffc_reserved->close_callback)
    ffc_reserved->close_callback(ffc_context, ffc_reserved->close_callback_arg);

    return 0;
}

void ffcamera_vfcallback(camera_handle_t handle, camera_buffer_t* buf, void* arg)
{
    if (buf->frametype != CAMERA_FRAMETYPE_NV12) return;

    ffcamera_context* ffc_context = (ffcamera_context*) arg;
    ffcamera_reserved *ffc_reserved = (ffcamera_reserved*) ffc_context->reserved;

    if (!ffc_reserved || !ffc_reserved->running) return;

    int64_t uv_offset = buf->framedesc.nv12.uv_offset;
    uint32_t height = buf->framedesc.nv12.height;
    uint32_t width = buf->framedesc.nv12.width;
    uint32_t stride = buf->framedesc.nv12.stride;

    uint32_t _stride = width; // remove stride
    int64_t _uv_offset = _stride * height; // recompute and pack planes

    camera_buffer_t* _buf = (camera_buffer_t*) malloc(buf->framesize);
    memcpy(_buf, buf, buf->framesize);
    _buf->framedesc.nv12.stride = _stride;
    _buf->framedesc.nv12.uv_offset = _uv_offset;
    _buf->framebuf = (uint8_t*) malloc(width * height * 3 / 2);

    for (uint32_t i = 0; i < height; i++)
    {
        int64_t doff = i * _stride;
        int64_t soff = i * stride;
        memcpy(&_buf->framebuf[doff], &buf->framebuf[soff], width);
    }

    uint8_t *srcuv = &buf->framebuf[uv_offset];
    uint8_t *destu = &_buf->framebuf[_uv_offset];
    uint8_t *destv = &_buf->framebuf[_uv_offset + ((width * height) / 4)];

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

    ffc_reserved->frames.push_back(_buf);

    pthread_cond_signal(&ffc_reserved->read_cond);
}

