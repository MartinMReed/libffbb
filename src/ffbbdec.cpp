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

typedef struct
{
    screen_context_t screen_context;
    screen_window_t screen_window;
    screen_buffer_t screen_buffer[1];
    screen_buffer_t screen_pixel_buffer;
    int stride;
} ffdec_view;

typedef struct
{
    bool running;
    bool open;
    int frame_count;
    ffdec_view *view;
    void (*frame_callback)(ffdec_context *ffd_context, AVFrame *frame, int i, void *arg);
    void *frame_callback_arg;
    int (*read_callback)(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg);
    void *read_callback_arg;
    void (*close_callback)(ffdec_context *ffd_context, void *arg);
    void *close_callback_arg;
} ffdec_reserved;

void* decoding_thread(void* arg);
void display_frame(ffdec_context *ffd_context, AVFrame *frame);

ffdec_context *ffdec_alloc()
{
    ffdec_context *ffd_context = (ffdec_context*) malloc(sizeof(ffdec_context));
    memset(ffd_context, 0, sizeof(ffdec_context));

    ffdec_reset(ffd_context);

    return ffd_context;
}

void ffdec_reset(ffdec_context *ffd_context)
{
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;

    // don't carry over the view, it needs to be recreated
    if (ffd_reserved && ffd_reserved->view) free(ffd_reserved->view);

    if (!ffd_reserved) ffd_reserved = (ffdec_reserved*) malloc(sizeof(ffdec_reserved));
    memset(ffd_reserved, 0, sizeof(ffdec_reserved));

    memset(ffd_context, 0, sizeof(ffdec_context));
    ffd_context->reserved = ffd_reserved;
}

ffdec_error ffdec_set_close_callback(ffdec_context *ffd_context,
        void (*close_callback)(ffdec_context *ffd_context, void *arg),
        void *arg)
{
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;
    if (!ffd_reserved) return FFDEC_NOT_INITIALIZED;
    ffd_reserved->close_callback = close_callback;
    ffd_reserved->close_callback_arg = arg;
    return FFDEC_OK;
}

ffdec_error ffdec_set_read_callback(ffdec_context *ffd_context,
        int (*read_callback)(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg),
        void *arg)
{
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;
    if (!ffd_reserved) return FFDEC_NOT_INITIALIZED;
    ffd_reserved->read_callback = read_callback;
    ffd_reserved->read_callback_arg = arg;
    return FFDEC_OK;
}

ffdec_error ffdec_set_frame_callback(ffdec_context *ffd_context,
        void (*frame_callback)(ffdec_context *ffd_context, AVFrame *frame, int i, void *arg),
        void *arg)
{
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;
    if (!ffd_reserved) return FFDEC_NOT_INITIALIZED;
    ffd_reserved->frame_callback = frame_callback;
    ffd_reserved->frame_callback_arg = arg;
    return FFDEC_OK;
}

ffdec_error ffdec_close(ffdec_context *ffd_context)
{
    AVCodecContext *codec_context = ffd_context->codec_context;

    if (codec_context)
    {
        if (avcodec_is_open(codec_context))
        {
            avcodec_close(codec_context);
        }

        av_free(codec_context);
        codec_context = ffd_context->codec_context = NULL;
    }

    return FFDEC_OK;
}

ffdec_error ffdec_free(ffdec_context *ffd_context)
{
    if (ffd_context->reserved)
    {
        ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;

        if (ffd_reserved->view) free(ffd_reserved->view);
        ffd_reserved->view = NULL;

        free(ffd_context->reserved);
        ffd_context->reserved = NULL;
    }

    free(ffd_context);

    return FFDEC_OK;
}

ffdec_error ffdec_start(ffdec_context *ffd_context)
{
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;
    if (!ffd_reserved) return FFDEC_NOT_INITIALIZED;
    if (ffd_reserved->running) return FFDEC_ALREADY_RUNNING;
    if (!ffd_context->codec_context) return FFDEC_NO_CODEC_SPECIFIED;

    ffd_reserved->frame_count = 0;
    ffd_reserved->running = true;

    pthread_t pthread;
    pthread_create(&pthread, 0, &decoding_thread, ffd_context);

    return FFDEC_OK;
}

ffdec_error ffdec_stop(ffdec_context *ffd_context)
{
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;
    if (!ffd_reserved) return FFDEC_NOT_INITIALIZED;
    if (!ffd_reserved->running) return FFDEC_ALREADY_STOPPED;

    ffd_reserved->running = false;

    return FFDEC_OK;
}

void* decoding_thread(void* arg)
{
    ffdec_context *ffd_context = (ffdec_context*) arg;
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;
    AVCodecContext *codec_context = ffd_context->codec_context;

    AVPacket packet;
    int got_frame;

    int decode_buffer_length = 4096;
    uint8_t decode_buffer[decode_buffer_length + FF_INPUT_BUFFER_PADDING_SIZE];
    memset(decode_buffer + decode_buffer_length, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    AVFrame *frame = avcodec_alloc_frame();

    while (ffd_reserved->running)
    {
        if (ffd_reserved->read_callback) packet.size = ffd_reserved->read_callback(ffd_context,
                decode_buffer, decode_buffer_length, ffd_reserved->read_callback_arg);

        if (packet.size <= 0) break;

        packet.data = decode_buffer;

        while (ffd_reserved->running && packet.size > 0)
        {
            // reset the AVPacket
            av_init_packet(&packet);

            got_frame = 0;
            int decode_result = avcodec_decode_video2(codec_context, frame, &got_frame, &packet);

            if (decode_result < 0)
            {
                fprintf(stderr, "Error while decoding frame %d\n", ffd_reserved->frame_count);
                exit(1);
            }

            if (got_frame)
            {
                if (ffd_reserved->frame_callback) ffd_reserved->frame_callback(
                        ffd_context, frame, ffd_reserved->frame_count,
                        ffd_reserved->frame_callback_arg);

                display_frame(ffd_context, frame);

                ffd_reserved->frame_count++;
            }

            packet.size -= decode_result;
            packet.data += decode_result;
        }
    }

    if (ffd_reserved->running)
    {
        // reset the AVPacket
        av_init_packet(&packet);
        packet.data = NULL;
        packet.size = 0;

        got_frame = 0;
        avcodec_decode_video2(codec_context, frame, &got_frame, &packet);

        if (got_frame)
        {
            if (ffd_reserved->frame_callback) ffd_reserved->frame_callback(
                    ffd_context, frame, ffd_reserved->frame_count,
                    ffd_reserved->frame_callback_arg);

            display_frame(ffd_context, frame);

            ffd_reserved->frame_count++;
        }
    }

    av_free(frame);
    frame = NULL;

    if (ffd_reserved->close_callback) ffd_reserved->close_callback(
            ffd_context, ffd_reserved->close_callback_arg);

    return 0;
}

ffdec_error ffdec_create_view(ffdec_context *ffd_context, QString group, QString id, screen_window_t *window)
{
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;
    if (!ffd_reserved) return FFDEC_NOT_INITIALIZED;

    if (ffd_reserved->view)
    {
        *window = ffd_reserved->view->screen_window;
        return FFDEC_OK;
    }

    AVCodecContext *codec_context = ffd_context->codec_context;
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
    ffd_reserved->view = view;

    return FFDEC_OK;
}

void display_frame(ffdec_context *ffd_context, AVFrame *frame)
{
    ffdec_reserved *ffd_reserved = (ffdec_reserved*) ffd_context->reserved;
    ffdec_view *view = ffd_reserved->view;
    if (!view) return;

    screen_window_t screen_window = view->screen_window;
    screen_buffer_t screen_pixel_buffer = view->screen_pixel_buffer;
    screen_context_t screen_context = view->screen_context;
    int stride = view->stride;

    unsigned char *ptr = NULL;
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
        memcpy(&y[doff], &srcy[soff], frame->width);
    }

    for (int i = 0; i < height / 2; i++)
    {
        int doff = i * stride / 2;
        int soff = i * frame->linesize[1];
        memcpy(&u[doff], &srcu[soff], frame->width / 2);
    }

    for (int i = 0; i < height / 2; i++)
    {
        int doff = i * stride / 2;
        int soff = i * frame->linesize[2];
        memcpy(&v[doff], &srcv[soff], frame->width / 2);
    }

    screen_buffer_t screen_buffer;
    screen_get_window_property_pv(screen_window, SCREEN_PROPERTY_RENDER_BUFFERS, (void**) &screen_buffer);

    int attribs[] = { SCREEN_BLIT_SOURCE_WIDTH, width, SCREEN_BLIT_SOURCE_HEIGHT, height, SCREEN_BLIT_END };
    screen_blit(screen_context, screen_buffer, screen_pixel_buffer, attribs);

    int dirty_rects[] = { 0, 0, width, height };
    screen_post_window(screen_window, screen_buffer, 1, dirty_rects, 0);
}
