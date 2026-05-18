#pragma once

#include "ndk_loader.h"
#include <string>
#include <functional>

// Manages the Camera2 NDK device + capture session lifecycle.
// Ported from the NDK sample's NDKCamera / CameraManager classes,
// with JNI callbacks removed and function pointers used throughout.
class CameraSession {
public:
    explicit CameraSession(const Camera2NDK& ndk);
    ~CameraSession();

    CameraSession(const CameraSession&) = delete;
    CameraSession& operator=(const CameraSession&) = delete;

    // Enumerate cameras and open the first back-facing one (or any camera).
    // Returns false on failure.
    bool Open();

    // Create a capture session with one preview output and one JPEG output.
    // Both ANativeWindow* come from ImageReader::GetWindow().
    bool CreateSession(ANativeWindow* preview_window, ANativeWindow* jpeg_window);

    // Start the repeating preview request.
    bool StartPreview();

    // Trigger a single JPEG capture.
    bool TakePhoto();

    void Close();

    // Exposure time in nanoseconds (clamped to sensor range).
    void SetExposure(int64_t ns);
    // ISO sensitivity.
    void SetSensitivity(int32_t iso);

    bool IsReady() const { return session_ != nullptr; }

private:
    static void OnDeviceDisconnected(void* ctx, ACameraDevice* dev);
    static void OnDeviceError(void* ctx, ACameraDevice* dev, int error);
    static void OnSessionReady(void* ctx, ACameraCaptureSession* session);
    static void OnSessionClosed(void* ctx, ACameraCaptureSession* session);
    static void OnSessionActive(void* ctx, ACameraCaptureSession* session);

    void QueryExposureRange();

    const Camera2NDK& ndk_;

    ACameraManager*              manager_          = nullptr;
    ACameraDevice*               device_           = nullptr;
    ACameraCaptureSession*       session_          = nullptr;
    ACaptureSessionOutputContainer* output_container_ = nullptr;
    ACaptureSessionOutput*       preview_output_   = nullptr;
    ACaptureSessionOutput*       jpeg_output_      = nullptr;
    ACaptureRequest*             preview_request_  = nullptr;
    ACaptureRequest*             jpeg_request_     = nullptr;
    ACameraOutputTarget*         preview_target_   = nullptr;
    ACameraOutputTarget*         jpeg_target_      = nullptr;

    int64_t  exposure_ns_    = 0;
    int32_t  sensitivity_    = 0;
    int64_t  exposure_min_   = 0;
    int64_t  exposure_max_   = 0;
    int32_t  sensitivity_min_ = 0;
    int32_t  sensitivity_max_ = 0;

    std::string camera_id_;
    bool manual_ae_ = false;
};
