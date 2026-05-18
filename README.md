# Camera2 on Linux with Wayland

A native proof-of-concept for using Android's Camera2 NDK API on Linux-based systems via Halium/libhybris.

It loads Android camera libraries (`libcamera2ndk.so`) using libhybris's android_dlopen, captures frames, and renders them to a Wayland window with OpenGL.
