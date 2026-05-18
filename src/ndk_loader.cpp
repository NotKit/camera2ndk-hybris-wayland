#include "ndk_loader.h"
extern "C" {
#include <hybris/common/binding.h>
}
#include <dlfcn.h>
#include <cstdio>

#define LOAD_SYM(handle, st, name) \
    do { \
        (st).name = (decltype((st).name)) android_dlsym(handle, #name); \
        if (!(st).name) { \
            fprintf(stderr, "ndk_loader: missing symbol %s: %s\n", \
                    #name, android_dlerror()); \
            ok = false; \
        } \
    } while (0)

bool load_camera2ndk(Camera2NDK& out) {
    void* lib = android_dlopen("libcamera2ndk.so", RTLD_LAZY);
    if (!lib) {
        fprintf(stderr, "ndk_loader: failed to open libcamera2ndk.so: %s\n",
                android_dlerror());
        return false;
    }
    bool ok = true;
    LOAD_SYM(lib, out, ACameraManager_create);
    LOAD_SYM(lib, out, ACameraManager_delete);
    LOAD_SYM(lib, out, ACameraManager_getCameraIdList);
    LOAD_SYM(lib, out, ACameraManager_deleteCameraIdList);
    LOAD_SYM(lib, out, ACameraManager_getCameraCharacteristics);
    LOAD_SYM(lib, out, ACameraManager_openCamera);
    LOAD_SYM(lib, out, ACameraMetadata_getConstEntry);
    LOAD_SYM(lib, out, ACameraMetadata_free);
    LOAD_SYM(lib, out, ACameraDevice_close);
    LOAD_SYM(lib, out, ACameraDevice_createCaptureSession);
    LOAD_SYM(lib, out, ACameraDevice_createCaptureRequest);
    LOAD_SYM(lib, out, ACaptureSessionOutputContainer_create);
    LOAD_SYM(lib, out, ACaptureSessionOutputContainer_free);
    LOAD_SYM(lib, out, ACaptureSessionOutputContainer_add);
    LOAD_SYM(lib, out, ACaptureSessionOutput_create);
    LOAD_SYM(lib, out, ACaptureSessionOutput_free);
    LOAD_SYM(lib, out, ACameraOutputTarget_create);
    LOAD_SYM(lib, out, ACameraOutputTarget_free);
    LOAD_SYM(lib, out, ACaptureRequest_addTarget);
    LOAD_SYM(lib, out, ACaptureRequest_setEntry_i32);
    LOAD_SYM(lib, out, ACaptureRequest_setEntry_i64);
    LOAD_SYM(lib, out, ACaptureRequest_setEntry_u8);
    LOAD_SYM(lib, out, ACaptureRequest_free);
    LOAD_SYM(lib, out, ACameraCaptureSession_setRepeatingRequest);
    LOAD_SYM(lib, out, ACameraCaptureSession_capture);
    LOAD_SYM(lib, out, ACameraCaptureSession_stopRepeating);
    LOAD_SYM(lib, out, ACameraCaptureSession_close);
    return ok;
}

bool start_binder_thread_pool() {
    void* lib = android_dlopen("libbinder_ndk.so", RTLD_LAZY);
    if (!lib) {
        fprintf(stderr, "ndk_loader: failed to open libbinder_ndk.so: %s\n",
                android_dlerror());
        return false;
    }
    auto start = (void(*)()) android_dlsym(lib, "ABinderProcess_startThreadPool");
    auto setMax = (void(*)(uint32_t)) android_dlsym(lib,
                      "ABinderProcess_setThreadPoolMaxThreadCount");
    if (!start) {
        fprintf(stderr, "ndk_loader: missing ABinderProcess_startThreadPool: %s\n",
                android_dlerror());
        return false;
    }
    if (setMax) setMax(4);
    start();
    return true;
}

bool load_mediandk(MediaNDK& out) {
    void* lib = android_dlopen("libmediandk.so", RTLD_LAZY);
    if (!lib) {
        fprintf(stderr, "ndk_loader: failed to open libmediandk.so: %s\n",
                android_dlerror());
        return false;
    }
    bool ok = true;
    LOAD_SYM(lib, out, AImageReader_new);
    LOAD_SYM(lib, out, AImageReader_newWithUsage);
    LOAD_SYM(lib, out, AImageReader_delete);
    LOAD_SYM(lib, out, AImageReader_getWindow);
    LOAD_SYM(lib, out, AImageReader_setImageListener);
    LOAD_SYM(lib, out, AImageReader_acquireLatestImage);
    LOAD_SYM(lib, out, AImageReader_acquireNextImage);
    LOAD_SYM(lib, out, AImage_delete);
    LOAD_SYM(lib, out, AImage_getHardwareBuffer);
    LOAD_SYM(lib, out, AImage_getWidth);
    LOAD_SYM(lib, out, AImage_getHeight);
    LOAD_SYM(lib, out, AImage_getNumberOfPlanes);
    LOAD_SYM(lib, out, AImage_getPlaneData);
    LOAD_SYM(lib, out, AImage_getPlaneRowStride);
    LOAD_SYM(lib, out, AImage_getPlanePixelStride);
    LOAD_SYM(lib, out, AImage_getTimestamp);
    return ok;
}
