// camera2-hybris-wayland: Camera2 NDK preview via libhybris on Ubuntu Touch.
// Wayland+EGL setup mirrors test_camera.cpp from libhybris.
// Frame delivery: AImageReader → AHardwareBuffer → EGLImageKHR → GL_TEXTURE_EXTERNAL_OES

#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <media/NdkImage.h>  // for AIMAGE_FORMAT_YUV_420_888

#include "ndk_loader.h"
#include "image_reader.h"
#include "camera_session.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <signal.h>

static constexpr int kCamWidth  = 1280;
static constexpr int kCamHeight = 720;
static int s_screen_w = 1080;
static int s_screen_h = 1920;

static struct wl_display*       s_display    = nullptr;
static struct wl_compositor*    s_compositor = nullptr;
static struct wl_shell*         s_shell      = nullptr;
static struct wl_output*        s_output     = nullptr;
static struct wl_surface*       s_surface    = nullptr;
static struct wl_shell_surface* s_shell_surf = nullptr;
static struct wl_egl_window*    s_egl_window = nullptr;
static volatile bool            s_running    = true;

static void output_geometry(void*, struct wl_output*, int32_t, int32_t, int32_t, int32_t,
                             int32_t, const char*, const char*, int32_t) {}
static void output_mode(void*, struct wl_output*, uint32_t flags,
                        int32_t w, int32_t h, int32_t) {
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        s_screen_w = w;
        s_screen_h = h;
        fprintf(stderr, "Screen mode: %dx%d\n", w, h);
    }
}
static void output_done(void*, struct wl_output*) {}
static void output_scale(void*, struct wl_output*, int32_t) {}
static void output_name(void*, struct wl_output*, const char*) {}
static void output_description(void*, struct wl_output*, const char*) {}

static const struct wl_output_listener kOutputListener = {
    output_geometry, output_mode, output_done, output_scale,
    output_name, output_description
};

static void registry_handler(void*, struct wl_registry* reg,
                              uint32_t id, const char* iface, uint32_t ver) {
    if (strcmp(iface, "wl_compositor") == 0) {
        s_compositor = (struct wl_compositor*)
            wl_registry_bind(reg, id, &wl_compositor_interface, 1);
    } else if (strcmp(iface, "wl_shell") == 0) {
        s_shell = (struct wl_shell*)
            wl_registry_bind(reg, id, &wl_shell_interface, 1);
    } else if (strcmp(iface, "wl_output") == 0 && !s_output) {
        uint32_t bind_ver = (ver >= 2) ? 2 : 1;
        s_output = (struct wl_output*)
            wl_registry_bind(reg, id, &wl_output_interface, bind_ver);
        wl_output_add_listener(s_output, &kOutputListener, nullptr);
    }
}
static void registry_remover(void*, struct wl_registry*, uint32_t) {}

static const struct wl_registry_listener kRegistryListener = {
    registry_handler, registry_remover
};

static void shell_surface_ping(void*, struct wl_shell_surface* ss, uint32_t serial) {
    wl_shell_surface_pong(ss, serial);
}
static void shell_surface_configure(void*, struct wl_shell_surface*,
                                    uint32_t, int32_t, int32_t) {}
static void shell_surface_popup_done(void*, struct wl_shell_surface*) {}

static const struct wl_shell_surface_listener kShellSurfListener = {
    shell_surface_ping, shell_surface_configure, shell_surface_popup_done
};

static bool wayland_init() {
    s_display = wl_display_connect(nullptr);
    if (!s_display) { fprintf(stderr, "Cannot connect to Wayland display\n"); return false; }

    struct wl_registry* reg = wl_display_get_registry(s_display);
    wl_registry_add_listener(reg, &kRegistryListener, nullptr);
    wl_display_dispatch(s_display);
    wl_display_roundtrip(s_display);
    // Second roundtrip flushes wl_output::mode events that arrive after binding.
    wl_display_roundtrip(s_display);

    if (!s_compositor || !s_shell) {
        fprintf(stderr, "Wayland compositor or shell not available\n");
        return false;
    }

    s_surface = wl_compositor_create_surface(s_compositor);
    s_shell_surf = wl_shell_get_shell_surface(s_shell, s_surface);
    wl_shell_surface_add_listener(s_shell_surf, &kShellSurfListener, nullptr);
    wl_shell_surface_set_toplevel(s_shell_surf);
    wl_shell_surface_set_title(s_shell_surf, "camera2-hybris-wayland");

    struct wl_region* region = wl_compositor_create_region(s_compositor);
    wl_region_add(region, 0, 0, s_screen_w, s_screen_h);
    wl_surface_set_opaque_region(s_surface, region);
    wl_region_destroy(region);

    return true;
}

// GLES shaders (copied from test_camera.cpp)
static const char* kVertSrc =
    "#extension GL_OES_EGL_image_external : require\n"
    "attribute vec4 a_position;\n"
    "attribute vec2 a_texCoord;\n"
    "varying vec2 v_texCoord;\n"
    "void main() {\n"
    "  gl_Position = a_position;\n"
    "  v_texCoord  = a_texCoord;\n"
    "}\n";

static const char* kFragSrc =
    "#extension GL_OES_EGL_image_external : require\n"
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform samplerExternalOES s_texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(s_texture, v_texCoord);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetShaderInfoLog(sh, sizeof(buf), nullptr, buf);
        fprintf(stderr, "Shader compile error: %s\n", buf);
    }
    return sh;
}

static GLuint create_program() {
    GLuint vert = compile_shader(GL_VERTEX_SHADER,   kVertSrc);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, kFragSrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

static void on_signal(int) { s_running = false; }

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    Camera2NDK ndk{};
    MediaNDK   media{};
    if (!load_camera2ndk(ndk)) {
        fprintf(stderr, "Failed to load libcamera2ndk.so\n");
        return 1;
    }
    if (!load_mediandk(media)) {
        fprintf(stderr, "Failed to load libmediandk.so\n");
        return 1;
    }
    // Start the binder thread pool so inbound binder calls from cameraserver
    // (e.g. dequeue/queueBuffer on our AImageReader's IGraphicBufferProducer)
    // are serviced. Without this, the HAL times out fetching output buffers.
    if (!start_binder_thread_pool()) {
        fprintf(stderr, "Failed to start binder thread pool\n");
        return 1;
    }
    fprintf(stderr, "NDK libraries loaded; binder thread pool started.\n");

    // Create ImageReaders (they provide ANativeWindow* for camera output)
    ImageReader preview_reader(media, kCamWidth, kCamHeight,
                               AIMAGE_FORMAT_YUV_420_888, 0 /*default usage*/, 4);
    ImageReader jpeg_reader(media, kCamWidth, kCamHeight,
                            AIMAGE_FORMAT_JPEG, 0, 2);

    if (!preview_reader.GetWindow() || !jpeg_reader.GetWindow()) {
        fprintf(stderr, "Failed to create ImageReaders\n");
        return 1;
    }
    fprintf(stderr, "ImageReaders ready: preview=%p jpeg=%p\n",
            (void*)preview_reader.GetWindow(), (void*)jpeg_reader.GetWindow());

    // Open camera and create session
    CameraSession session(ndk);
    if (!session.Open()) return 1;
    if (!session.CreateSession(preview_reader.GetWindow(),
                                jpeg_reader.GetWindow())) return 1;

    if (!wayland_init()) return 1;
    fprintf(stderr, "Wayland connected.\n");

    // EGL setup from test_camera.cpp
    EGLDisplay egl_disp = eglGetDisplay((EGLNativeDisplayType)s_display);
    assert(egl_disp != EGL_NO_DISPLAY);
    eglInitialize(egl_disp, nullptr, nullptr);

    EGLint attr[] = {
        EGL_BUFFER_SIZE, 32,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLConfig cfg; EGLint ncfg = 0;
    eglChooseConfig(egl_disp, attr, &cfg, 1, &ncfg);
    assert(ncfg > 0);

    s_egl_window = wl_egl_window_create(s_surface, s_screen_w, s_screen_h);
    EGLSurface egl_surf = eglCreateWindowSurface(egl_disp, cfg,
        (EGLNativeWindowType)s_egl_window, nullptr);
    assert(egl_surf != EGL_NO_SURFACE);

    EGLint ctxattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext egl_ctx = eglCreateContext(egl_disp, cfg, EGL_NO_CONTEXT, ctxattr);
    assert(egl_ctx != EGL_NO_CONTEXT);
    eglMakeCurrent(egl_disp, egl_surf, egl_surf, egl_ctx);
    fprintf(stderr, "EGL ready.\n");

    // Resolve EGL/GLES extension functions needed for AHardwareBuffer path
    auto pfn_eglGetNativeClientBuffer =
        (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
        eglGetProcAddress("eglGetNativeClientBufferANDROID");
    auto pfn_eglCreateImage =
        (PFNEGLCREATEIMAGEKHRPROC)
        eglGetProcAddress("eglCreateImageKHR");
    auto pfn_eglDestroyImage =
        (PFNEGLDESTROYIMAGEKHRPROC)
        eglGetProcAddress("eglDestroyImageKHR");
    auto pfn_glEGLImageTargetTexture2D =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");

    if (!pfn_eglGetNativeClientBuffer || !pfn_eglCreateImage ||
        !pfn_eglDestroyImage || !pfn_glEGLImageTargetTexture2D) {
        fprintf(stderr, "WARNING: EGL_ANDROID_get_native_client_buffer or "
                "GL_OES_EGL_image_external not available - rendering will not work\n");
    } else {
        fprintf(stderr, "EGL extensions for AHardwareBuffer path available.\n");
    }

    // GLES setup
    GLuint prog = create_program();
    GLint loc_pos  = glGetAttribLocation(prog,  "a_position");
    GLint loc_tc   = glGetAttribLocation(prog,  "a_texCoord");
    GLint loc_tex  = glGetUniformLocation(prog, "s_texture");

    GLuint preview_tex = 0;
    glGenTextures(1, &preview_tex);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, preview_tex);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Fullscreen quad: x,y,z, u,v
    // UVs are rotated 90° CCW to account for the landscape sensor on a portrait screen
    // (back camera SENSOR_ORIENTATION=90: rotate image 90° CCW to display upright).
    static const GLfloat kVerts[] = {
        -1.0f,  1.0f, 0.0f,   0.0f, 1.0f,   // screen TL → sensor BL
        -1.0f, -1.0f, 0.0f,   1.0f, 1.0f,   // screen BL → sensor BR
         1.0f, -1.0f, 0.0f,   1.0f, 0.0f,   // screen BR → sensor TR
         1.0f,  1.0f, 0.0f,   0.0f, 0.0f,   // screen TR → sensor TL
    };
    static const GLushort kIdx[] = { 0, 1, 2, 0, 2, 3 };

    glViewport(0, 0, s_screen_w, s_screen_h);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

    if (!session.StartPreview()) return 1;
    fprintf(stderr, "Camera preview started. Press Ctrl+C to exit.\n");

    // Render loop
    EGLImageKHR current_egl_image = EGL_NO_IMAGE_KHR;

    while (s_running) {
        // Poll Wayland events without blocking
        wl_display_dispatch_pending(s_display);
        wl_display_flush(s_display);

        glClear(GL_COLOR_BUFFER_BIT);

        // Try to update texture from latest camera frame
        if (pfn_eglGetNativeClientBuffer && pfn_eglCreateImage &&
            pfn_eglDestroyImage && pfn_glEGLImageTargetTexture2D) {

            AHardwareBuffer* ahb = nullptr;
            if (preview_reader.AcquireLatestHardwareBuffer(&ahb)) {
                // Destroy previous EGLImage before creating a new one
                if (current_egl_image != EGL_NO_IMAGE_KHR) {
                    pfn_eglDestroyImage(egl_disp, current_egl_image);
                    current_egl_image = EGL_NO_IMAGE_KHR;
                }

                EGLClientBuffer cb = pfn_eglGetNativeClientBuffer(ahb);
                if (cb) {
                    static const EGLint img_attr[] = { EGL_NONE };
                    current_egl_image = pfn_eglCreateImage(egl_disp,
                        EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, cb, img_attr);
                    if (current_egl_image != EGL_NO_IMAGE_KHR) {
                        glBindTexture(GL_TEXTURE_EXTERNAL_OES, preview_tex);
                        pfn_glEGLImageTargetTexture2D(GL_TEXTURE_EXTERNAL_OES,
                                                       current_egl_image);
                    } else {
                        fprintf(stderr, "eglCreateImageKHR failed (EGL error 0x%x)\n",
                                eglGetError());
                    }
                }
                // Release the AImage after EGLImage creation; the EGLImage
                // holds its own reference to the underlying gralloc buffer.
                preview_reader.ReleaseCurrentImage();
            }
        }

        // Draw textured quad
        if (current_egl_image != EGL_NO_IMAGE_KHR) {
            glUseProgram(prog);
            glEnableVertexAttribArray(loc_pos);
            glEnableVertexAttribArray(loc_tc);
            glVertexAttribPointer(loc_pos, 3, GL_FLOAT, GL_FALSE,
                                  5 * sizeof(GLfloat), kVerts);
            glVertexAttribPointer(loc_tc, 2, GL_FLOAT, GL_FALSE,
                                  5 * sizeof(GLfloat), kVerts + 3);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, preview_tex);
            glUniform1i(loc_tex, 0);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, kIdx);
            glDisableVertexAttribArray(loc_pos);
            glDisableVertexAttribArray(loc_tc);
        }

        eglSwapBuffers(egl_disp, egl_surf);
    }

    // Cleanup
    if (current_egl_image != EGL_NO_IMAGE_KHR)
        pfn_eglDestroyImage(egl_disp, current_egl_image);
    session.Close();
    eglMakeCurrent(egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(egl_disp, egl_ctx);
    eglDestroySurface(egl_disp, egl_surf);
    eglTerminate(egl_disp);
    wl_egl_window_destroy(s_egl_window);
    wl_display_disconnect(s_display);
    return 0;
}
