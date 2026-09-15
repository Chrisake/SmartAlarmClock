/**
 * @file face_detector_esp.cpp
 *
 * Faces on the clock: frames from the board's MIPI-CSI camera, through
 * esp_video's V4L2 device, and a small YOLO face model run with ESP-DL.
 *
 * TODO: untested on the clock. What it assumes, to check first:
 *
 * - The camera module is brought up by esp_video: esp_video_init() with the
 *   board's CSI and SCCB settings from the BSP, before the first open. It
 *   gives RGB565 frames on /dev/video0 (ESP_VIDEO_MIPI_CSI_DEVICE_NAME).
 * - The model is a one-class YOLO face detector -- YOLOv8n-face or YOLO11n
 *   trained on WIDER FACE -- at a small input such as 224x224, quantised to
 *   int8 for the ESP32-P4 with ESP-PPQ and flashed to a "face_model" data
 *   partition. The larger the input, the slower each frame.
 * - The class names below are ESP-DL 3.x's: dl::Model, the image
 *   preprocessor and the YOLO11 detection post-processor, with the anchor
 *   strides of a 3-head YOLO. Check them, and the post-processor's
 *   constructor, against the ESP-DL version pinned in idf_component.yml.
 *
 * Only a yes or no is needed, so the post-processor keeps the single best box
 * and its threshold is what matters: raise SCORE_MIN if curtains and pillows
 * wake the screen, lower it if faces are missed in the dark.
 */

#if defined(ESP_PLATFORM)

/*********************
 *      INCLUDES
 *********************/

#include "presence/face_detector.h"

#include "dl_detect_yolo11_postprocessor.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_model_base.hpp"
#include "esp_log.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"

#include <fcntl.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/*********************
 *      DEFINES
 *********************/

#define TAG "face"

#define MODEL_PARTITION "face_model"

/** Camera buffers: one being filled, one being looked at. */
#define BUFFER_COUNT 2

/** Box confidence taken for a face, overlap for merging boxes, and boxes kept. */
#define SCORE_MIN 0.5f
#define NMS_IOU   0.45f
#define TOP_K     1

/**********************
 *  STATIC VARIABLES
 **********************/

static int camera = -1;

static struct {
    uint8_t * data;
    size_t    size;
} buffers[BUFFER_COUNT];

static uint32_t frame_width;
static uint32_t frame_height;

static dl::Model *                       model;
static dl::image::ImagePreprocessor *    preprocessor;
static dl::detect::yolo11PostProcessor * postprocessor;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static bool camera_open(void);
static void camera_close(void);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

bool face_detector_open(void)
{
    if(camera >= 0) return true;
    if(!camera_open()) return false;

    model = new dl::Model(MODEL_PARTITION, fbs::MODEL_LOCATION_IN_FLASH_PARTITION);
    if(!model) {
        camera_close();
        return false;
    }

    /*The model takes 0..1 input: no mean, and a scale of 255.*/
    preprocessor  = new dl::image::ImagePreprocessor(model, {0, 0, 0}, {255, 255, 255});
    postprocessor = new dl::detect::yolo11PostProcessor(model, SCORE_MIN, NMS_IOU, TOP_K,
                                                        {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}});
    return true;
}

bool face_detector_detect(bool * face)
{
    *face = false;
    if(camera < 0) return false;

    struct v4l2_buffer buf = {};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if(ioctl(camera, VIDIOC_DQBUF, &buf) != 0) return false;

    dl::image::img_t image = {};
    image.data     = buffers[buf.index].data;
    image.width    = (uint16_t)frame_width;
    image.height   = (uint16_t)frame_height;
    image.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565;

    preprocessor->preprocess(image);
    model->run();

    postprocessor->clear_result();
    postprocessor->set_resize_scale_x(preprocessor->get_resize_scale_x());
    postprocessor->set_resize_scale_y(preprocessor->get_resize_scale_y());
    postprocessor->postprocess();
    *face = !postprocessor->get_result(image.width, image.height).empty();

    /*Back to the driver for the next frame.*/
    ioctl(camera, VIDIOC_QBUF, &buf);
    return true;
}

void face_detector_close(void)
{
    delete postprocessor;
    delete preprocessor;
    delete model;
    postprocessor = nullptr;
    preprocessor  = nullptr;
    model         = nullptr;

    camera_close();
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/** Open the camera, map its buffers and start it streaming, at whatever size it gives. */
static bool camera_open(void)
{
    camera = open(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, O_RDONLY);
    if(camera < 0) {
        ESP_LOGW(TAG, "no camera at %s", ESP_VIDEO_MIPI_CSI_DEVICE_NAME);
        return false;
    }

    struct v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(ioctl(camera, VIDIOC_G_FMT, &format) != 0) goto fail;

    format.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
    if(ioctl(camera, VIDIOC_S_FMT, &format) != 0) goto fail;
    frame_width  = format.fmt.pix.width;
    frame_height = format.fmt.pix.height;

    {
        struct v4l2_requestbuffers request = {};
        request.count  = BUFFER_COUNT;
        request.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        request.memory = V4L2_MEMORY_MMAP;
        if(ioctl(camera, VIDIOC_REQBUFS, &request) != 0) goto fail;
    }

    for(int i = 0; i < BUFFER_COUNT; i++) {
        struct v4l2_buffer buf = {};
        buf.index  = i;
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if(ioctl(camera, VIDIOC_QUERYBUF, &buf) != 0) goto fail;

        buffers[i].data = (uint8_t *)mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, camera, buf.m.offset);
        buffers[i].size = buf.length;
        if(!buffers[i].data || ioctl(camera, VIDIOC_QBUF, &buf) != 0) goto fail;
    }

    {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if(ioctl(camera, VIDIOC_STREAMON, &type) != 0) goto fail;
    }

    ESP_LOGI(TAG, "camera at %" PRIu32 "x%" PRIu32, frame_width, frame_height);
    return true;

fail:
    ESP_LOGW(TAG, "the camera could not be started");
    camera_close();
    return false;
}

static void camera_close(void)
{
    if(camera < 0) return;

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(camera, VIDIOC_STREAMOFF, &type);

    for(int i = 0; i < BUFFER_COUNT; i++) {
        if(buffers[i].data) munmap(buffers[i].data, buffers[i].size);
        buffers[i].data = nullptr;
    }

    close(camera);
    camera = -1;
}

#endif /*defined(ESP_PLATFORM)*/
