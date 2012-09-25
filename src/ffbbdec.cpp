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

#include "ffbbdec.h"

#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>

void* decoding_thread(void* arg);

ffdec_context::ffdec_context()
{
    codec_context = 0;

#if !OSX_PLATFORM
    view = 0;
#endif

    reset();
}

ffdec_context::~ffdec_context()
{
#if !OSX_PLATFORM
    if (view) free(view);
#endif
}

void ffdec_context::reset()
{
    frame_index = 0;
    running = false;
    open = false;

#if !OSX_PLATFORM
    if (view)
    {
        free(view);
        view = 0;
    }
#endif

    frame_callback = 0;
    frame_callback_arg = 0;

    read_callback = 0;
    read_callback_arg = 0;

    close_callback = 0;
    close_callback_arg = 0;
}

ffdec_error ffdec_context::set_frame_callback(
        void (*frame_callback)(ffdec_context *ffd_context, AVFrame *frame, int index, void *arg),
        void *arg)
{
    this->frame_callback = frame_callback;
    frame_callback_arg = arg;
    return FFDEC_OK;
}

ffdec_error ffdec_context::set_read_callback(
        int (*read_callback)(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg),
        void *arg)
{
    this->read_callback = read_callback;
    read_callback_arg = arg;
    return FFDEC_OK;
}

ffdec_error ffdec_context::set_close_callback(
        void (*close_callback)(ffdec_context *ffd_context, void *arg),
        void *arg)
{
    this->close_callback = close_callback;
    close_callback_arg = arg;
    return FFDEC_OK;
}

ffdec_error ffdec_context::close()
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

    return FFDEC_OK;
}

ffdec_error ffdec_context::start()
{
    if (running) return FFDEC_ALREADY_RUNNING;
    if (!codec_context) return FFDEC_NO_CODEC_SPECIFIED;

    running = true;

    pthread_t pthread;
    pthread_create(&pthread, 0, &::decoding_thread, this);

    return FFDEC_OK;
}

ffdec_error ffdec_context::stop()
{
    if (!running) return FFDEC_ALREADY_STOPPED;

    running = false;

    return FFDEC_OK;
}

void* decoding_thread(void* arg)
{
    ffdec_context *ffd_context = (ffdec_context*) arg;
    ffd_context->decoding_thread();
    return 0;
}

void ffdec_context::decoding_thread()
{
    AVPacket packet;
    int got_frame;

    int decode_buffer_length = 4096;
    uint8_t decode_buffer[decode_buffer_length + FF_INPUT_BUFFER_PADDING_SIZE];
    memset(decode_buffer + decode_buffer_length, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    AVFrame *frame = avcodec_alloc_frame();

    while (running)
    {
        if (read_callback) packet.size = read_callback(this, decode_buffer, decode_buffer_length, read_callback_arg);

        if (packet.size <= 0) break;

        packet.data = decode_buffer;

        while (running && packet.size > 0)
        {
            // reset the AVPacket
            av_init_packet(&packet);

            got_frame = 0;
            int decode_result = avcodec_decode_video2(codec_context, frame, &got_frame, &packet);

            if (decode_result < 0)
            {
                fprintf(stderr, "Error while decoding video\n");
                running = false;
                break;
            }

            if (got_frame)
            {
                frame_index++;

                if (frame_callback) frame_callback(this, frame, frame_index, frame_callback_arg);

#if !OSX_PLATFORM
                display_frame(frame);
#endif
            }

            packet.size -= decode_result;
            packet.data += decode_result;
        }
    }

    if (running)
    {
        // reset the AVPacket
        av_init_packet(&packet);
        packet.data = 0;
        packet.size = 0;

        got_frame = 0;
        avcodec_decode_video2(codec_context, frame, &got_frame, &packet);

        if (got_frame)
        {
            frame_index++;

            if (frame_callback) frame_callback(this, frame, frame_index, frame_callback_arg);

#if !OSX_PLATFORM
            display_frame(frame);
#endif
        }
    }

    av_free(frame);
    frame = 0;

    if (close_callback) close_callback(this, close_callback_arg);
}

#if !OSX_PLATFORM
ffdec_error ffdec_context::create_view(QString group, QString id, screen_window_t *window)
{
    if (this->view)
    {
        *window = this->view->screen_window;
        return FFDEC_OK;
    }

    if (!codec_context) return FFDEC_NO_CODEC_SPECIFIED;
    if (!avcodec_is_open(codec_context)) return FFDEC_CODEC_NOT_OPEN;

    ffdec_view *view = (ffdec_view*) malloc(sizeof(ffdec_view));
    memset(view, 0, sizeof(ffdec_view));

    QByteArray groupArr = group.toAscii();
    QByteArray idArr = id.toAscii();

    screen_context_t screen_context;
    screen_create_context(&screen_context, SCREEN_APPLICATION_CONTEXT);

    screen_window_t screen_window;
    screen_create_window_type(&screen_window, screen_context, SCREEN_CHILD_WINDOW);
    screen_join_window_group(screen_window, groupArr.constData());
    screen_set_window_property_cv(screen_window, SCREEN_PROPERTY_ID_STRING, idArr.length(), idArr.constData());

    int usage = SCREEN_USAGE_NATIVE;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_USAGE, &usage);

    int video_size[] = { codec_context->width, codec_context->height };
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_BUFFER_SIZE, video_size);
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_SOURCE_SIZE, video_size);

    int z = -1;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_ZORDER, &z);

    int pos[] = { 0, 0 };
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_POSITION, pos);

    screen_create_window_buffers(screen_window, 1);

    screen_pixmap_t screen_pix;
    screen_create_pixmap(&screen_pix, screen_context);

    usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    screen_set_pixmap_property_iv(screen_pix, SCREEN_PROPERTY_USAGE, &usage);

    int format = SCREEN_FORMAT_YUV420;
    screen_set_pixmap_property_iv(screen_pix, SCREEN_PROPERTY_FORMAT, &format);

    screen_set_pixmap_property_iv(screen_pix, SCREEN_PROPERTY_BUFFER_SIZE, video_size);

    screen_create_pixmap_buffer(screen_pix);

    screen_buffer_t screen_pixel_buffer;
    screen_get_pixmap_property_pv(screen_pix, SCREEN_PROPERTY_RENDER_BUFFERS, (void**) &screen_pixel_buffer);

    int stride;
    screen_get_buffer_property_iv(screen_pixel_buffer, SCREEN_PROPERTY_STRIDE, &stride);

    view->screen_context = screen_context;
    *window = view->screen_window = screen_window;
    view->screen_pixel_buffer = screen_pixel_buffer;
    view->stride = stride;
    this->view = view;

    return FFDEC_OK;
}

void ffdec_context::display_frame(AVFrame *frame)
{
    if (!view) return;

    screen_window_t screen_window = view->screen_window;
    screen_buffer_t screen_pixel_buffer = view->screen_pixel_buffer;
    screen_context_t screen_context = view->screen_context;
    int stride = view->stride;

    unsigned char *ptr = 0;
    screen_get_buffer_property_pv(screen_pixel_buffer, SCREEN_PROPERTY_POINTER, (void**) &ptr);

    int width = frame->width;
    int height = frame->height;

    uint8_t *srcy = frame->data[0];
    uint8_t *srcu = frame->data[1];
    uint8_t *srcv = frame->data[2];

    unsigned char *y = ptr;
    unsigned char *u = y + (height * stride);
    unsigned char *v = u + (height * stride) / 4;

    for (int i = 0; i < height; i++)
    {
        int doff = i * stride;
        int soff = i * frame->linesize[0];
        memcpy(&y[doff], &srcy[soff], width);
    }

    for (int i = 0; i < height / 2; i++)
    {
        int doff = i * stride / 2;
        int soff = i * frame->linesize[1];
        memcpy(&u[doff], &srcu[soff], width / 2);
    }

    for (int i = 0; i < height / 2; i++)
    {
        int doff = i * stride / 2;
        int soff = i * frame->linesize[2];
        memcpy(&v[doff], &srcv[soff], width / 2);
    }

    screen_buffer_t screen_buffer;
    screen_get_window_property_pv(screen_window, SCREEN_PROPERTY_RENDER_BUFFERS, (void**) &screen_buffer);

    int attribs[] = { SCREEN_BLIT_SOURCE_WIDTH, width, SCREEN_BLIT_SOURCE_HEIGHT, height, SCREEN_BLIT_END };
    screen_blit(screen_context, screen_buffer, screen_pixel_buffer, attribs);

    int dirty_rects[] = { 0, 0, width, height };
    screen_post_window(screen_window, screen_buffer, 1, dirty_rects, 0);
}
#endif

int clamp(float f)
{
    if (f < 0) return 0;
    if (f > 255) return 255;
    return f;
}

void yuv_to_rgb(AVFrame *frame, unsigned char *rgb, int width, int height)
{
    uint8_t *data_y = frame->data[0];
    uint8_t *data_u = frame->data[1];
    uint8_t *data_v = frame->data[2];
    
    int stride_y = frame->linesize[0];
    int stride_u = frame->linesize[1];
    int stride_v = frame->linesize[2];
    
    for (int y = 0; y < height; y++)
    {
        for ( int x = 0; x < width; x++)
        {
            float Y = (float)data_y[ y    * stride_y +  x   ];
            float U = (float)data_u[(y/2) * stride_u + (x/2)];
            float V = (float)data_v[(y/2) * stride_v + (x/2)];
            
            float R = ((1.164 * (Y - 16)) + (1.596 * (V - 128)));
            float G = ((1.164 * (Y - 16)) - (0.813 * (V - 128)) - (0.391 * (U - 128)));
            float B = ((1.164 * (Y - 16)) + (2.018 * (U - 128)));
            
            *rgb++ = 0;
            *rgb++ = clamp(R);
            *rgb++ = clamp(G);
            *rgb++ = clamp(B);
        }
    }
}
