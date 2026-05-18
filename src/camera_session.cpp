#include "camera_session.h"
#include <camera/NdkCameraMetadataTags.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

CameraSession::CameraSession(const Camera2NDK& ndk) : ndk_(ndk) {}

CameraSession::~CameraSession() {
    Close();
}

bool CameraSession::Open() {
    manager_ = ndk_.ACameraManager_create();
    if (!manager_) {
        fprintf(stderr, "CameraSession: ACameraManager_create failed\n");
        return false;
    }

    ACameraIdList* id_list = nullptr;
    if (ndk_.ACameraManager_getCameraIdList(manager_, &id_list) != ACAMERA_OK || !id_list) {
        fprintf(stderr, "CameraSession: getCameraIdList failed\n");
        return false;
    }

    fprintf(stderr, "CameraSession: found %d camera(s)\n", id_list->numCameras);
    for (int i = 0; i < id_list->numCameras; i++) {
        fprintf(stderr, "  [%d] %s\n", i, id_list->cameraIds[i]);
    }

    // Prefer back-facing camera; fall back to first available.
    for (int i = 0; i < id_list->numCameras; i++) {
        ACameraMetadata* chars = nullptr;
        if (ndk_.ACameraManager_getCameraCharacteristics(manager_,
                id_list->cameraIds[i], &chars) != ACAMERA_OK)
            continue;

        ACameraMetadata_const_entry entry{};
        if (ndk_.ACameraMetadata_getConstEntry(chars,
                ACAMERA_LENS_FACING, &entry) == ACAMERA_OK) {
            if (entry.data.u8[0] == ACAMERA_LENS_FACING_BACK) {
                camera_id_ = id_list->cameraIds[i];
                ndk_.ACameraMetadata_free(chars);
                break;
            }
        }
        if (camera_id_.empty())
            camera_id_ = id_list->cameraIds[i];
        ndk_.ACameraMetadata_free(chars);
    }
    ndk_.ACameraManager_deleteCameraIdList(id_list);

    if (camera_id_.empty()) {
        fprintf(stderr, "CameraSession: no camera found\n");
        return false;
    }
    fprintf(stderr, "CameraSession: opening camera %s\n", camera_id_.c_str());

    ACameraDevice_StateCallbacks dev_cb{};
    dev_cb.context = this;
    dev_cb.onDisconnected = OnDeviceDisconnected;
    dev_cb.onError = OnDeviceError;

    if (ndk_.ACameraManager_openCamera(manager_, camera_id_.c_str(),
            &dev_cb, &device_) != ACAMERA_OK || !device_) {
        fprintf(stderr, "CameraSession: openCamera failed\n");
        return false;
    }

    QueryExposureRange();
    return true;
}

bool CameraSession::CreateSession(ANativeWindow* preview_window,
                                   ANativeWindow* jpeg_window) {
    if (!device_) return false;

    ndk_.ACaptureSessionOutputContainer_create(&output_container_);

    // Preview output
    ndk_.ACaptureSessionOutput_create(preview_window, &preview_output_);
    ndk_.ACaptureSessionOutputContainer_add(output_container_, preview_output_);

    // JPEG output
    ndk_.ACaptureSessionOutput_create(jpeg_window, &jpeg_output_);
    ndk_.ACaptureSessionOutputContainer_add(output_container_, jpeg_output_);

    ACameraCaptureSession_stateCallbacks session_cb{};
    session_cb.context = this;
    session_cb.onReady = OnSessionReady;
    session_cb.onActive = OnSessionActive;
    session_cb.onClosed = OnSessionClosed;

    if (ndk_.ACameraDevice_createCaptureSession(device_, output_container_,
            &session_cb, &session_) != ACAMERA_OK || !session_) {
        fprintf(stderr, "CameraSession: createCaptureSession failed\n");
        return false;
    }

    // Build preview request
    ndk_.ACameraDevice_createCaptureRequest(device_,
        TEMPLATE_PREVIEW, &preview_request_);
    ndk_.ACameraOutputTarget_create(preview_window, &preview_target_);
    ndk_.ACaptureRequest_addTarget(preview_request_, preview_target_);

    // Build JPEG request
    ndk_.ACameraDevice_createCaptureRequest(device_,
        TEMPLATE_STILL_CAPTURE, &jpeg_request_);
    ndk_.ACameraOutputTarget_create(jpeg_window, &jpeg_target_);
    ndk_.ACaptureRequest_addTarget(jpeg_request_, jpeg_target_);

    return true;
}

bool CameraSession::StartPreview() {
    if (!session_ || !preview_request_) return false;

    if (manual_ae_) {
        uint8_t ae_mode = ACAMERA_CONTROL_AE_MODE_OFF;
        ndk_.ACaptureRequest_setEntry_u8(preview_request_,
            ACAMERA_CONTROL_AE_MODE, 1, &ae_mode);
        ndk_.ACaptureRequest_setEntry_i64(preview_request_,
            ACAMERA_SENSOR_EXPOSURE_TIME, 1, &exposure_ns_);
        ndk_.ACaptureRequest_setEntry_i32(preview_request_,
            ACAMERA_SENSOR_SENSITIVITY, 1, &sensitivity_);
    }

    int seq_id = 0;
    camera_status_t ret = ndk_.ACameraCaptureSession_setRepeatingRequest(
        session_, nullptr, 1, &preview_request_, &seq_id);
    if (ret != ACAMERA_OK) {
        fprintf(stderr, "CameraSession: setRepeatingRequest failed (%d)\n", ret);
        return false;
    }
    fprintf(stderr, "CameraSession: preview started (seq %d)\n", seq_id);
    return true;
}

bool CameraSession::TakePhoto() {
    if (!session_ || !jpeg_request_) return false;
    int seq_id = 0;
    return ndk_.ACameraCaptureSession_capture(
        session_, nullptr, 1, &jpeg_request_, &seq_id) == ACAMERA_OK;
}

void CameraSession::SetExposure(int64_t ns) {
    if (exposure_min_ && exposure_max_)
        exposure_ns_ = std::clamp(ns, exposure_min_, exposure_max_);
    else
        exposure_ns_ = ns;
    manual_ae_ = true;
}

void CameraSession::SetSensitivity(int32_t iso) {
    if (sensitivity_min_ && sensitivity_max_)
        sensitivity_ = std::clamp(iso, sensitivity_min_, sensitivity_max_);
    else
        sensitivity_ = iso;
    manual_ae_ = true;
}

void CameraSession::QueryExposureRange() {
    if (!device_) return;
    ACameraMetadata* chars = nullptr;
    if (ndk_.ACameraManager_getCameraCharacteristics(manager_,
            camera_id_.c_str(), &chars) != ACAMERA_OK) return;

    ACameraMetadata_const_entry entry{};
    if (ndk_.ACameraMetadata_getConstEntry(chars,
            ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE, &entry) == ACAMERA_OK) {
        exposure_min_ = entry.data.i64[0];
        exposure_max_ = entry.data.i64[1];
        // Default: mid-range exposure
        exposure_ns_ = exposure_min_ + (exposure_max_ - exposure_min_) / 4;
        fprintf(stderr, "CameraSession: exposure range [%lld, %lld] ns\n",
                (long long)exposure_min_, (long long)exposure_max_);
    }
    if (ndk_.ACameraMetadata_getConstEntry(chars,
            ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE, &entry) == ACAMERA_OK) {
        sensitivity_min_ = entry.data.i32[0];
        sensitivity_max_ = entry.data.i32[1];
        sensitivity_ = sensitivity_min_ + (sensitivity_max_ - sensitivity_min_) / 10;
        fprintf(stderr, "CameraSession: ISO range [%d, %d]\n",
                sensitivity_min_, sensitivity_max_);
    }
    ndk_.ACameraMetadata_free(chars);
}

void CameraSession::Close() {
    if (session_) {
        ndk_.ACameraCaptureSession_stopRepeating(session_);
        ndk_.ACameraCaptureSession_close(session_);
        session_ = nullptr;
    }
    if (preview_request_) { ndk_.ACaptureRequest_free(preview_request_); preview_request_ = nullptr; }
    if (jpeg_request_)    { ndk_.ACaptureRequest_free(jpeg_request_);    jpeg_request_    = nullptr; }
    if (preview_target_)  { ndk_.ACameraOutputTarget_free(preview_target_); preview_target_  = nullptr; }
    if (jpeg_target_)     { ndk_.ACameraOutputTarget_free(jpeg_target_);    jpeg_target_     = nullptr; }
    if (preview_output_)  { ndk_.ACaptureSessionOutput_free(preview_output_); preview_output_ = nullptr; }
    if (jpeg_output_)     { ndk_.ACaptureSessionOutput_free(jpeg_output_);    jpeg_output_    = nullptr; }
    if (output_container_) { ndk_.ACaptureSessionOutputContainer_free(output_container_); output_container_ = nullptr; }
    if (device_)  { ndk_.ACameraDevice_close(device_);   device_  = nullptr; }
    if (manager_) { ndk_.ACameraManager_delete(manager_); manager_ = nullptr; }
}

void CameraSession::OnDeviceDisconnected(void* ctx, ACameraDevice*) {
    fprintf(stderr, "CameraSession: device disconnected\n");
    static_cast<CameraSession*>(ctx)->device_ = nullptr;
}

void CameraSession::OnDeviceError(void* ctx, ACameraDevice*, int error) {
    fprintf(stderr, "CameraSession: device error %d\n", error);
}

void CameraSession::OnSessionReady(void*, ACameraCaptureSession*) {
    fprintf(stderr, "CameraSession: session ready\n");
}

void CameraSession::OnSessionActive(void*, ACameraCaptureSession*) {
    fprintf(stderr, "CameraSession: session active\n");
}

void CameraSession::OnSessionClosed(void*, ACameraCaptureSession*) {
    fprintf(stderr, "CameraSession: session closed\n");
}
