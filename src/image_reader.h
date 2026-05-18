#pragma once

#include "ndk_loader.h"
#include <atomic>
#include <mutex>
#include <cstdint>

// Wraps AImageReader for a single stream (preview or JPEG).
// Thread-safe: the image-available callback fires on an Android thread;
// ConsumeLatestHardwareBuffer() is called from the render thread.
class ImageReader {
public:
    ImageReader(const MediaNDK& media, int32_t width, int32_t height,
                int32_t format, uint64_t usage, int32_t maxImages);
    ~ImageReader();

    // Not copyable/movable (holds Android-side resources)
    ImageReader(const ImageReader&) = delete;
    ImageReader& operator=(const ImageReader&) = delete;

    // The ANativeWindow to pass to ACaptureSessionOutput_create.
    // Owned by the AImageReader; do not release it.
    ANativeWindow* GetWindow() const { return window_; }

    // Acquire the latest available image and return its AHardwareBuffer.
    // Returns true if a new buffer was obtained; the caller must call
    // ReleaseCurrentImage() after the GPU is done with the EGLImage.
    // Returns false if no new frame is available.
    bool AcquireLatestHardwareBuffer(AHardwareBuffer** out_ahb);

    // Release the AImage currently held (if any).  Must be called after
    // the GPU has finished using the corresponding EGLImage.
    void ReleaseCurrentImage();

    bool HasNewFrame() const { return has_new_frame_.load(std::memory_order_relaxed); }

private:
    static void OnImageAvailable(void* ctx, AImageReader* reader);

    const MediaNDK& media_;
    AImageReader*   reader_  = nullptr;
    ANativeWindow*  window_  = nullptr;
    AImage*         current_image_ = nullptr;
    std::mutex      lock_;
    std::atomic<bool> has_new_frame_{false};
};
