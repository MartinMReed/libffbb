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
#include <deque>
#include <pthread.h>

#if !OSX_PLATFORM
#include <camera/camera_api.h>
#elif OSX_PLATFORM
#import <CoreVideo/CoreVideo.h>
#endif

typedef enum
{
    FFENC_OK = 0,
    FFENC_NO_CODEC_SPECIFIED,
    FFENC_FRAME_NOT_SUPPORTED,
    FFENC_NOT_RUNNING,
    FFENC_ALREADY_RUNNING,
    FFENC_ALREADY_STOPPED
} ffenc_error;

class ffenc_context
{
    friend void* encoding_thread(void* arg);

public:

    ffenc_context();
    virtual ~ffenc_context();

    /**
     * The codec context to use for encoding.
     */
    AVCodecContext *codec_context;

    /**
     * Reset the context with default values.
     */
    void reset();

    ffenc_error set_frame_callback(bool (*frame_callback)(ffenc_context *ffe_context, AVFrame *frame, int index, void *arg),
            void *arg);

    ffenc_error set_write_callback(void (*write_callback)(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg),
            void *arg);

    ffenc_error set_close_callback(void (*close_callback)(ffenc_context *ffe_context, void *arg),
            void *arg);

    /**
     * Start recording and encoding the camera frames.
     * Encoding will begin on a background thread.
     */
    ffenc_error start();

    /**
     * Stop recording frames. Once all recorded frames have been encoded
     * the background thread will die.
     */
    ffenc_error stop();

    /**
     * Close the context.
     * This will also close the AVCodecContext if not already closed.
     */
    ffenc_error close();

    /**
     * Add an AVFrame. The frame and frame->data[0] passed into this
     * method will be freed by the encoding thread.
     */
    ffenc_error add_frame(AVFrame *frame);

#if !OSX_PLATFORM
    /**
     * Add a frame from the native camera API.
     * This should have a buf->frametype of CAMERA_FRAMETYPE_NV12.
     */
    ffenc_error add_frame(camera_buffer_t* buf);
    #elif OSX_PLATFORM
    /**
     * Add a frame from CoreVideo.
     * This should have a pixel format type of kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange.
     */
    ffenc_error add_frame(CVImageBufferRef pixelBuffer);
#endif

private:

    void free_frames();
    void encoding_thread();

    bool running;
    pthread_mutex_t reading_mutex;
    pthread_cond_t read_cond;
    std::deque<AVFrame*> frames;
    int frame_index;

    bool (*frame_callback)(ffenc_context *ffe_context, AVFrame *frame, int index, void *arg);
    void *frame_callback_arg;

    void (*write_callback)(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg);
    void *write_callback_arg;

    void (*close_callback)(ffenc_context *ffe_context, void *arg);
    void *close_callback_arg;
};

#endif
