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

#ifndef FFBBDEC_H
#define FFBBDEC_H

#ifndef OSX_PLATFORM
#define OSX_PLATFORM 0
#endif

// include math.h otherwise it will get included
// by avformat.h and cause duplicate definition
// errors because of C vs C++ functions
#include <math.h>

extern "C"
{
#define UINT64_C uint64_t
#define INT64_C int64_t
#include <libavformat/avformat.h>
}

#include <sys/types.h>

#if !OSX_PLATFORM
#include <screen/screen.h>
#include <QString>
#endif

void yuv_to_rgb(AVFrame *frame, unsigned char *rgb, int width, int height);

typedef enum
{
    FFDEC_OK = 0,
    FFDEC_NOT_INITIALIZED,
    FFDEC_CODEC_NOT_OPEN,
    FFDEC_NO_CODEC_SPECIFIED,
    FFDEC_ALREADY_RUNNING,
    FFDEC_ALREADY_STOPPED
} ffdec_error;

#if !OSX_PLATFORM
typedef struct
{
    screen_context_t screen_context;
    screen_window_t screen_window;
    screen_buffer_t screen_buffer[1];
    screen_buffer_t screen_pixel_buffer;
    int stride;
} ffdec_view;
#endif

class ffdec_context
{
    friend void* decoding_thread(void* arg);

public:

    ffdec_context();
    virtual ~ffdec_context();

    /**
     * Reset the context with default values.
     */
    void reset();

    ffdec_error set_frame_callback(
            void (*frame_callback)(ffdec_context *ffd_context, AVFrame *frame, int index, void *arg),
            void *arg);

    ffdec_error set_read_callback(
            int (*read_callback)(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg),
            void *arg);

    ffdec_error set_close_callback(
            void (*close_callback)(ffdec_context *ffd_context, void *arg),
            void *arg);

    /**
     * Start decoding the camera frames.
     * Decoding will begin on a background thread.
     */
    ffdec_error start();

    /**
     * Stop decoding frames and kill the background thread.
     */
    ffdec_error stop();

    /**
     * Close the context.
     * This will also close the AVCodecContext if not already closed.
     */
    ffdec_error close();

#if !OSX_PLATFORM
    ffdec_error create_view(QString group, QString id, screen_window_t *window);
    #endif

    /**
     * The codec context to use for decoding.
     */
    AVCodecContext *codec_context;

private:

    void decoding_thread();

#if !OSX_PLATFORM
    void display_frame(AVFrame *frame);
    #endif

    int frame_index;
    bool running;
    bool open;

#if !OSX_PLATFORM
    ffdec_view *view;
    #endif

    void (*frame_callback)(ffdec_context *ffd_context, AVFrame *frame, int index, void *arg);
    void *frame_callback_arg;

    int (*read_callback)(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg);
    void *read_callback_arg;

    void (*close_callback)(ffdec_context *ffd_context, void *arg);
    void *close_callback_arg;
};

#endif
