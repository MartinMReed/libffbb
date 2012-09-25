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

#include <fcntl.h>
#include <sys/stat.h>

void* encoding_thread(void* arg);

ffenc_context::ffenc_context()
{
    codec_context = 0;

    pthread_mutex_init(&reading_mutex, 0);
    pthread_cond_init(&read_cond, 0);

    reset();
}

ffenc_context::~ffenc_context()
{
    pthread_mutex_destroy(&reading_mutex);
    pthread_cond_destroy(&read_cond);

    free_frames();
}

void ffenc_context::free_frames()
{
    while (frames.size())
    {
        AVFrame *frame = frames.front();
        frames.pop_front();
        free(frame->data[0]);
        av_free(frame);
    }
}

void ffenc_context::reset()
{
    free_frames();

    running = false;
    frame_index = 0;

    frame_callback = 0;
    frame_callback_arg = 0;

    write_callback = 0;
    write_callback_arg = 0;

    close_callback = 0;
    close_callback_arg = 0;
}

ffenc_error ffenc_context::set_frame_callback(
        bool (*frame_callback)(ffenc_context *ffe_context, AVFrame *frame, int index, void *arg),
        void *arg)
{
    this->frame_callback = frame_callback;
    frame_callback_arg = arg;
    return FFENC_OK;
}

ffenc_error ffenc_context::set_write_callback(
        void (*write_callback)(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg),
        void *arg)
{
    this->write_callback = write_callback;
    write_callback_arg = arg;
    return FFENC_OK;
}

ffenc_error ffenc_context::set_close_callback(
        void (*close_callback)(ffenc_context *ffe_context, void *arg),
        void *arg)
{
    this->close_callback = close_callback;
    close_callback_arg = arg;
    return FFENC_OK;
}

ffenc_error ffenc_context::close()
{
    stop();

    if (codec_context)
    {
        if (avcodec_is_open(codec_context))
        {
            avcodec_close(codec_context);
        }

        av_free(codec_context);
        codec_context = 0;
    }

    return FFENC_OK;
}

ffenc_error ffenc_context::start()
{
    if (running) return FFENC_ALREADY_RUNNING;
    if (!codec_context) return FFENC_NO_CODEC_SPECIFIED;

    running = true;

    free_frames();

    pthread_t pthread;
    pthread_create(&pthread, 0, &::encoding_thread, this);

    return FFENC_OK;
}

ffenc_error ffenc_context::stop()
{
    if (!running) return FFENC_ALREADY_STOPPED;

    running = false;

    pthread_cond_signal(&read_cond);

    return FFENC_OK;
}

void* encoding_thread(void* arg)
{
    ffenc_context* ffe_context = (ffenc_context*) arg;
    ffe_context->encoding_thread();
    return 0;
}

void ffenc_context::encoding_thread()
{
    int encode_buffer_len = 10000000;
    uint8_t *encode_buffer = (uint8_t *) av_malloc(encode_buffer_len);

    AVPacket packet;
    int got_packet;

    while (running || !frames.empty())
    {
        if (frames.empty())
        {
            pthread_mutex_lock(&reading_mutex);
            pthread_cond_wait(&read_cond, &reading_mutex);
            pthread_mutex_unlock(&reading_mutex);
            continue;
        }

        AVFrame *frame = frames.front();
        frames.pop_front();

        int frame_index = frame_index + 1;

        bool encode_frame = true;

        if (frame_callback) encode_frame = frame_callback(this, frame, frame_index, frame_callback_arg);

        if (encode_frame)
        {
            this->frame_index = frame_index;

            // reset the AVPacket
            av_init_packet(&packet);
            packet.data = encode_buffer;
            packet.size = encode_buffer_len;

            got_packet = 0;
            int encode_result = avcodec_encode_video2(codec_context, &packet, frame, &got_packet);

            if (encode_result == 0 && got_packet > 0)
            {
                if (write_callback) write_callback(this, packet.data, packet.size, write_callback_arg);
            }
        }

        free(frame->data[0]);
        av_free(frame);
        frame = 0;
    }

    do
    {
        // reset the AVPacket
        av_init_packet(&packet);
        packet.data = encode_buffer;
        packet.size = encode_buffer_len;

        got_packet = 0;
        int encode_result = avcodec_encode_video2(codec_context, &packet, 0, &got_packet);

        if (encode_result == 0 && got_packet > 0)
        {
            if (write_callback) write_callback(this, packet.data, packet.size, write_callback_arg);
        }
    }
    while (got_packet > 0);

    av_free(encode_buffer);
    encode_buffer = 0;
    encode_buffer_len = 0;

    if (close_callback) close_callback(this, close_callback_arg);
}

ffenc_error ffenc_context::add_frame(AVFrame *frame)
{
    if (!running) return FFENC_NOT_RUNNING;
    frames.push_back(frame);
    pthread_cond_signal(&read_cond);
    return FFENC_OK;
}

#if !OSX_PLATFORM
ffenc_error ffenc_context::add_frame(camera_buffer_t* buf)
{
    if (buf->frametype != CAMERA_FRAMETYPE_NV12) return FFENC_FRAME_NOT_SUPPORTED;

    if (!running) return FFENC_NOT_RUNNING;

    int64_t uv_offset = buf->framedesc.nv12.uv_offset;
    uint32_t height = buf->framedesc.nv12.height;
    uint32_t width = buf->framedesc.nv12.width;
    uint32_t stride = buf->framedesc.nv12.stride;

    uint32_t _stride = width;
    int64_t _uv_offset = _stride * height;

    AVFrame *frame = avcodec_alloc_frame();

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

    frames.push_back(frame);

    pthread_cond_signal(&read_cond);

    return FFENC_OK;
}
#elif OSX_PLATFORM
ffenc_error ffenc_context::add_frame(CVImageBufferRef pixelBuffer)
{
    OSType formatType = CVPixelBufferGetPixelFormatType(pixelBuffer);
    if (formatType != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
    {
        return FFENC_FRAME_NOT_SUPPORTED;
    }

    if (!running) return FFENC_NOT_RUNNING;

    CVPixelBufferLockBaseAddress(pixelBuffer, 0);

    int height = CVPixelBufferGetHeight(pixelBuffer);
    int width = CVPixelBufferGetWidth(pixelBuffer);

    uint32_t stride = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);
    uint32_t uv_stride = CVPixelBufferGetWidthOfPlane(pixelBuffer, 0);

    uint32_t _stride = width;
    int64_t _uv_offset = _stride * height;

    AVFrame *frame = avcodec_alloc_frame();

    frame->linesize[0] = _stride;
    frame->linesize[1] = _stride / 2;
    frame->linesize[2] = _stride / 2;

    frame->data[0] = (uint8_t*)malloc(width * height * 3 / 2);
    frame->data[1] = &frame->data[0][_uv_offset];
    frame->data[2] = &frame->data[0][_uv_offset + ((width * height) / 4)];

    uint8_t *srcy = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);

    for (uint32_t i = 0; i < height; i++)
    {
        int64_t doff = i * _stride;
        int64_t soff = i * stride;
        memcpy(&frame->data[0][doff], &srcy[soff], width);
    }

    uint8_t *srcuv = (uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
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
        srcuv += uv_stride;
    }

    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);

    frames.push_back(frame);

    pthread_cond_signal(&read_cond);

    return FFENC_OK;
}
#endif
