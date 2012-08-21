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

#ifndef FFBBENC_H
#define FFBBENC_H

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

#include <camera/camera_api.h>

typedef enum
{
    FFENC_OK = 0,
    FFENC_NOT_INITIALIZED,
    FFENC_NO_CODEC_SPECIFIED,
    FFENC_FRAME_NOT_SUPPORTED,
    FFENC_NOT_RUNNING,
    FFENC_ALREADY_RUNNING,
    FFENC_ALREADY_STOPPED
} ffenc_error;

typedef struct
{
    /**
     * The codec context to use for encoding.
     */
    AVCodecContext *codec_context;

    /**
     * For internal use. Do not use.
     */
    void *reserved;
} ffenc_context;

/**
 * Allocate the context with default values.
 */
ffenc_context *ffenc_alloc(void);

/**
 * Reset the context with default values.
 */
void ffenc_reset(ffenc_context *ffe_context);

ffenc_error ffenc_set_write_callback(ffenc_context *ffe_context,
        void (*write_callback)(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg),
        void *arg);

ffenc_error ffenc_set_close_callback(ffenc_context *ffe_context,
        void (*close_callback)(ffenc_context *ffe_context, void *arg),
        void *arg);

/**
 * Close the context.
 * This will also close the AVCodecContext if not already closed.
 */
ffenc_error ffenc_close(ffenc_context *ffe_context);

/**
 * Free the context.
 */
ffenc_error ffenc_free(ffenc_context *ffe_context);

/**
 * Start recording and encoding the camera frames.
 * Encoding will begin on a background thread.
 */
ffenc_error ffenc_start(ffenc_context *ffe_context);

/**
 * Stop recording frames. Once all recorded frames have been encoded
 * the background thread will die.
 */
ffenc_error ffenc_stop(ffenc_context *ffe_context);

/**
 * Add an AVFrame. The frame and frame->data[0] passed into this
 * method will be freed by the encoding thread.
 */
ffenc_error ffenc_add_frame(ffenc_context *ffe_context, AVFrame *frame);

/**
 * Add a frame from the native camera API.
 * This should have a buf->frametype of CAMERA_FRAMETYPE_NV12.
 */
ffenc_error ffenc_add_frame(ffenc_context *ffe_context, camera_buffer_t* buf);

#endif
