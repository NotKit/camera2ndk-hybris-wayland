#include "image_reader.h"
#include <cstdio>
#include <cassert>

// AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE = 0x100 (from hardware_buffer.h)
// AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN   = 0x3
// We combine both so the buffer is usable on GPU and also CPU-lockable as fallback.
static constexpr uint64_t kDefaultUsage =
    (1ULL << 8)   // AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE
  | (3ULL << 0);  // AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN

ImageReader::ImageReader(const MediaNDK& media, int32_t width, int32_t height,
                         int32_t format, uint64_t usage, int32_t maxImages)
    : media_(media) {
    media_status_t ret;
    if (usage == 0) usage = kDefaultUsage;

    ret = media_.AImageReader_newWithUsage(width, height, format, usage, maxImages, &reader_);
    if (ret != AMEDIA_OK || !reader_) {
        fprintf(stderr, "ImageReader: AImageReader_newWithUsage failed (%d), "
                "falling back to AImageReader_new\n", ret);
        ret = media_.AImageReader_new(width, height, format, maxImages, &reader_);
        if (ret != AMEDIA_OK || !reader_) {
            fprintf(stderr, "ImageReader: AImageReader_new also failed (%d)\n", ret);
            return;
        }
    }

    ret = media_.AImageReader_getWindow(reader_, &window_);
    if (ret != AMEDIA_OK || !window_) {
        fprintf(stderr, "ImageReader: AImageReader_getWindow failed (%d)\n", ret);
        media_.AImageReader_delete(reader_);
        reader_ = nullptr;
        return;
    }

    AImageReader_ImageListener listener{this, OnImageAvailable};
    media_.AImageReader_setImageListener(reader_, &listener);
}

ImageReader::~ImageReader() {
    ReleaseCurrentImage();
    if (reader_) {
        // window_ is owned by reader_, deleted implicitly
        media_.AImageReader_delete(reader_);
    }
}

void ImageReader::OnImageAvailable(void* ctx, AImageReader*) {
    auto* self = static_cast<ImageReader*>(ctx);
    self->has_new_frame_.store(true, std::memory_order_relaxed);
}

bool ImageReader::AcquireLatestHardwareBuffer(AHardwareBuffer** out_ahb) {
    if (!has_new_frame_.exchange(false, std::memory_order_relaxed))
        return false;

    std::lock_guard<std::mutex> guard(lock_);
    ReleaseCurrentImage();  // drop any previously held image

    AImage* image = nullptr;
    media_status_t ret = media_.AImageReader_acquireLatestImage(reader_, &image);
    if (ret != AMEDIA_OK || !image) {
        // No frame available despite the flag — can happen if acquireLatest beat us
        has_new_frame_.store(false, std::memory_order_relaxed);
        return false;
    }
    current_image_ = image;

    AHardwareBuffer* ahb = nullptr;
    ret = media_.AImage_getHardwareBuffer(image, &ahb);
    if (ret != AMEDIA_OK || !ahb) {
        fprintf(stderr, "ImageReader: AImage_getHardwareBuffer failed (%d)\n", ret);
        media_.AImage_delete(image);
        current_image_ = nullptr;
        return false;
    }

    *out_ahb = ahb;
    return true;
}

void ImageReader::ReleaseCurrentImage() {
    if (current_image_) {
        media_.AImage_delete(current_image_);
        current_image_ = nullptr;
    }
}
