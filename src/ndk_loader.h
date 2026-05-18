#pragma once

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCaptureRequest.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImageReader.h>
#include <media/NdkImage.h>

// All Camera2 NDK function pointers, loaded from libcamera2ndk.so via android_dlopen.
struct Camera2NDK {
    // ACameraManager
    ACameraManager*  (*ACameraManager_create)();
    void             (*ACameraManager_delete)(ACameraManager*);
    camera_status_t  (*ACameraManager_getCameraIdList)(ACameraManager*, ACameraIdList**);
    void             (*ACameraManager_deleteCameraIdList)(ACameraIdList*);
    camera_status_t  (*ACameraManager_getCameraCharacteristics)(ACameraManager*,
                         const char*, ACameraMetadata**);
    camera_status_t  (*ACameraManager_openCamera)(ACameraManager*, const char*,
                         ACameraDevice_StateCallbacks*, ACameraDevice**);

    // ACameraMetadata
    camera_status_t  (*ACameraMetadata_getConstEntry)(const ACameraMetadata*, uint32_t,
                         ACameraMetadata_const_entry*);
    void             (*ACameraMetadata_free)(ACameraMetadata*);

    // ACameraDevice
    camera_status_t  (*ACameraDevice_close)(ACameraDevice*);
    camera_status_t  (*ACameraDevice_createCaptureSession)(ACameraDevice*,
                         const ACaptureSessionOutputContainer*,
                         const ACameraCaptureSession_stateCallbacks*,
                         ACameraCaptureSession**);
    camera_status_t  (*ACameraDevice_createCaptureRequest)(ACameraDevice*,
                         ACameraDevice_request_template, ACaptureRequest**);

    // ACaptureSessionOutputContainer
    camera_status_t  (*ACaptureSessionOutputContainer_create)(ACaptureSessionOutputContainer**);
    void             (*ACaptureSessionOutputContainer_free)(ACaptureSessionOutputContainer*);
    camera_status_t  (*ACaptureSessionOutputContainer_add)(ACaptureSessionOutputContainer*,
                         const ACaptureSessionOutput*);

    // ACaptureSessionOutput
    camera_status_t  (*ACaptureSessionOutput_create)(ANativeWindow*, ACaptureSessionOutput**);
    void             (*ACaptureSessionOutput_free)(ACaptureSessionOutput*);

    // ACameraOutputTarget / ACaptureRequest
    camera_status_t  (*ACameraOutputTarget_create)(ANativeWindow*, ACameraOutputTarget**);
    void             (*ACameraOutputTarget_free)(ACameraOutputTarget*);
    camera_status_t  (*ACaptureRequest_addTarget)(ACaptureRequest*, const ACameraOutputTarget*);
    camera_status_t  (*ACaptureRequest_setEntry_i32)(ACaptureRequest*, uint32_t,
                         uint32_t, const int32_t*);
    camera_status_t  (*ACaptureRequest_setEntry_i64)(ACaptureRequest*, uint32_t,
                         uint32_t, const int64_t*);
    camera_status_t  (*ACaptureRequest_setEntry_u8)(ACaptureRequest*, uint32_t,
                         uint32_t, const uint8_t*);
    void             (*ACaptureRequest_free)(ACaptureRequest*);

    // ACameraCaptureSession
    camera_status_t  (*ACameraCaptureSession_setRepeatingRequest)(ACameraCaptureSession*,
                         ACameraCaptureSession_captureCallbacks*, int, ACaptureRequest**,
                         int*);
    camera_status_t  (*ACameraCaptureSession_capture)(ACameraCaptureSession*,
                         ACameraCaptureSession_captureCallbacks*, int, ACaptureRequest**,
                         int*);
    camera_status_t  (*ACameraCaptureSession_stopRepeating)(ACameraCaptureSession*);
    void             (*ACameraCaptureSession_close)(ACameraCaptureSession*);
};

// All AImageReader/AImage function pointers, loaded from libmediandk.so.
struct MediaNDK {
    media_status_t  (*AImageReader_new)(int32_t, int32_t, int32_t, int32_t, AImageReader**);
    media_status_t  (*AImageReader_newWithUsage)(int32_t, int32_t, int32_t,
                        uint64_t, int32_t, AImageReader**);
    void            (*AImageReader_delete)(AImageReader*);
    media_status_t  (*AImageReader_getWindow)(AImageReader*, ANativeWindow**);
    media_status_t  (*AImageReader_setImageListener)(AImageReader*,
                        AImageReader_ImageListener*);
    media_status_t  (*AImageReader_acquireLatestImage)(AImageReader*, AImage**);
    media_status_t  (*AImageReader_acquireNextImage)(AImageReader*, AImage**);

    void            (*AImage_delete)(AImage*);
    media_status_t  (*AImage_getHardwareBuffer)(const AImage*, AHardwareBuffer**);
    media_status_t  (*AImage_getWidth)(const AImage*, int32_t*);
    media_status_t  (*AImage_getHeight)(const AImage*, int32_t*);
    media_status_t  (*AImage_getNumberOfPlanes)(const AImage*, int32_t*);
    media_status_t  (*AImage_getPlaneData)(const AImage*, int32_t,
                        uint8_t**, int*);
    media_status_t  (*AImage_getPlaneRowStride)(const AImage*, int32_t, int32_t*);
    media_status_t  (*AImage_getPlanePixelStride)(const AImage*, int32_t, int32_t*);
    media_status_t  (*AImage_getTimestamp)(const AImage*, int64_t*);
};

// Load all function pointers. Returns false and prints errors on missing symbols.
bool load_camera2ndk(Camera2NDK& out);
bool load_mediandk(MediaNDK& out);

// Bring up the Android binder thread pool in this process. Required so that
// inbound binder calls (e.g. IGraphicBufferProducer::dequeueBuffer from the
// camera HAL into our AImageReader) are serviced — otherwise the HAL times
// out trying to fetch output buffers and the preview stalls.
bool start_binder_thread_pool();
