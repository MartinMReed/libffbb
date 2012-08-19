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

#ifndef FFCAMAPI_H
#define FFCAMAPI_H

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

#include <camera/camera_api.h>

#include <pthread.h>
#include <deque>

typedef enum
{
    FFCAMERA_OK = 0,
    FFCAMERA_NOT_INITIALIZED,
    FFCAMERA_NO_CODEC_SPECIFIED,
    FFCAMERA_ALREADY_RUNNING,
    FFCAMERA_ALREADY_STOPPED
} ffcamera_error;

typedef struct
{
    /**
     * The codec context to use for encoding. You can create a default
     * context by calling ffcamera_default_codec(enum CodecID codec_id,
     *                                           int width, int height,
     *                                           AVCodecContext **codec_context)
     */
    AVCodecContext *codec_context;

    /**
     * File descriptor to write the encoded frames to automatically.
     * This is optional. You can choose to use the write_callback
     * and handle writing manually.
     */
    int fd;

    /**
     * For internal use. Do not use.
     */
    void *reserved;
} ffcamera_context;

/**
 * Initialize the context with default values.
 *
 * Example:
 * ffcamera_context ffc_context:
 * ffcamera_init(&ffc_context);
 */
void ffcamera_init(ffcamera_context *ffc_context);

ffcamera_error ffcamera_set_close_callback(ffcamera_context *ffc_context,
        void (*close_callback)(ffcamera_context *ffc_context, void *arg),
        void *arg);

ffcamera_error ffcamera_set_write_callback(ffcamera_context *ffc_context,
        void (*write_callback)(ffcamera_context *ffc_context, const uint8_t *buf, ssize_t size, void *arg),
        void *arg);

/**
 * Close the context.
 * This will also close the AVCodecContext if not already closed.
 */
ffcamera_error ffcamera_close(ffcamera_context *ffc_context);

/**
 * Free the context.
 */
ffcamera_error ffcamera_free(ffcamera_context *ffc_context);

/**
 * Start recording and encoding the camera frames.
 * Encoding will begin on a background thread.
 */
ffcamera_error ffcamera_start(ffcamera_context *ffc_context);

/**
 * Stop recording frames. Once all recorded frames have been encoded
 * the background thread will die.
 */
ffcamera_error ffcamera_stop(ffcamera_context *ffc_context);

/**
 * The ViewFinder callback to pass into camera_start_video_viewfinder.
 *
 * Example:
 * ffcamera_context ffc_context:
 * ffcamera_init(&ffc_context);
 * ffcamera_error err = ffcamera_default_codec(CODEC_ID_MPEG2VIDEO,
 *                                             288, 512,
 *                                             &ffc_context->codec_context);
 * camera_start_video_viewfinder(mCameraHandle, ffcamera_vfcallback, NULL, &ffc_context)
 */
void ffcamera_vfcallback(camera_handle_t handle, camera_buffer_t* buf, void* arg);

#endif
