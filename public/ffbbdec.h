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

#include <screen/screen.h>
#include <QString>

typedef enum
{
    FFDEC_OK = 0,
    FFDEC_NOT_INITIALIZED,
    FFDEC_CODEC_NOT_OPEN,
    FFDEC_NO_CODEC_SPECIFIED,
    FFDEC_ALREADY_RUNNING,
    FFDEC_ALREADY_STOPPED
} ffdec_error;

typedef struct
{
    /**
     * The codec context to use for decoding.
     */
    AVCodecContext *codec_context;

    /**
     * For internal use. Do not use.
     */
    void *reserved;
} ffdec_context;

/**
 * Allocate the context with default values.
 */
ffdec_context *ffdec_alloc(void);

/**
 * Reset the context with default values.
 */
void ffdec_reset(ffdec_context *ffd_context);

ffdec_error ffdec_set_frame_callback(ffdec_context *ffd_context,
        void (*frame_callback)(ffdec_context *ffd_context, AVFrame *frame, int i, void *arg),
        void *arg);

ffdec_error ffdec_set_read_callback(ffdec_context *ffd_context,
        int (*read_callback)(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg),
        void *arg);

ffdec_error ffdec_set_close_callback(ffdec_context *ffd_context,
        void (*close_callback)(ffdec_context *ffd_context, void *arg),
        void *arg);

/**
 * Close the context.
 * This will also close the AVCodecContext if not already closed.
 */
ffdec_error ffdec_close(ffdec_context *ffd_context);

/**
 * Free the context.
 */
ffdec_error ffdec_free(ffdec_context *ffd_context);

/**
 * Start decoding the camera frames.
 * Decoding will begin on a background thread.
 */
ffdec_error ffdec_start(ffdec_context *ffd_context);

/**
 * Stop decoding frames and kill the background thread.
 */
ffdec_error ffdec_stop(ffdec_context *ffd_context);

ffdec_error ffdec_create_view(ffdec_context *ffd_context, QString group, QString id, screen_window_t *window);

#endif
