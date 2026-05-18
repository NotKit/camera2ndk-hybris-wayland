// Minimal JNI stub for compiling Android NDK headers outside of Android.
// ACameraMetadata_fromCameraMetadata (the only user) is never called here.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct _jobject;
typedef struct _jobject* jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;

struct JNINativeInterface_;
struct JNIEnv_;
#ifdef __cplusplus
typedef JNIEnv_ JNIEnv;
#else
typedef const struct JNINativeInterface_* JNIEnv;
#endif

struct JNINativeInterface_ {
    void* reserved0;
    void* reserved1;
    void* reserved2;
    void* reserved3;
};

struct JNIEnv_ {
    const struct JNINativeInterface_* functions;
};

#ifdef __cplusplus
}
#endif
