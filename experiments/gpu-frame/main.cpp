// SPDX-License-Identifier: GPL-3.0-or-later
// Isolated experiment: no player, audio, catalog or existing VideoOut owner.
#include "hardware_frame_probe.hpp"
#include "failure_log.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
extern "C" int sceSysmoduleLoadModule(unsigned);
extern "C" int sceKernelUsleep(unsigned);
extern "C" std::uint64_t sceKernelGetProcessTime();
namespace
{
int result = 0, samples = 0, differences = 0;
const char *stage = "START";
EGLDisplay display = EGL_NO_DISPLAY;
EGLSurface surface = EGL_NO_SURFACE;
bool context_ready = false;
GLuint program = 0, vao = 0, textures[2]{};
unsigned presented = 0, late_frames = 0;
std::uint64_t started = 0, upload_max_us = 0, draw_max_us = 0, swap_max_us = 0;
const char *vertex_source = "#version 330 core\n"
                            "out vec2 uv;\n"
                            "void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                            "uv=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0,1); }\n";
const char *pixel_source =
    "#version 330 core\n"
    "in vec2 uv;out vec4 color;uniform sampler2D planeY;uniform sampler2D planeUV;\n"
    "void main(){vec3 yuv=vec3(texture(planeY,uv).r,texture(planeUV,uv).rg);"
    "float y=max(0.0,yuv.x*255.0-16.0)/255.0;"
    "float u=(yuv.y*255.0-128.0)/255.0;"
    "float v=(yuv.z*255.0-128.0)/255.0;"
    "color=vec4(clamp(vec3((298.0*y+459.0*v)/256.0,"
    "(298.0*y-55.0*u-136.0*v)/256.0,"
    "(298.0*y+541.0*u)/256.0),0.0,1.0),1.0); }\n";
void log(const char *next)
{
    stage = next;
    char text[768];
    const int size = std::snprintf(
        text, sizeof(text),
        "TUBE5 GPU PRESENTATION V2\nBUILD %s\nSTAGE %s RESULT %d GL ERROR %u\n"
        "SAMPLES %d COMPONENT ERRORS %d\n1080P NV12 GPU CONVERSION - CPU TEXTURE UPLOAD\n"
        "PRESENTED %u LATE %u MAX UPLOAD/DRAW/SWAP US %llu/%llu/%llu\n"
        "ISOLATED EXPERIMENT - NO PLAYER CHANGES - NOT ZERO COPY\n",
        tube5::failure_log_build_label(), stage, result,
        context_ready ? static_cast<unsigned>(glGetError()) : 0u, samples, differences, presented,
        late_frames, static_cast<unsigned long long>(upload_max_us),
        static_cast<unsigned long long>(draw_max_us), static_cast<unsigned long long>(swap_max_us));
    if (size > 0)
#ifdef TUBE5_GPU_CLIP
        tube5::write_failure_log("/download0/tube5-gpu-clip.log", text,
#else
        tube5::write_failure_log("/download0/tube5-gpu-frame.log", text,
#endif
                                 std::min<std::size_t>(size, sizeof(text) - 1));
}
GLuint shader(GLenum type, const char *source)
{
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        result = -10;
        log("SHADER FAILED");
        return 0;
    }
    return id;
}
int setup_graphics()
{
    log("COMPILE OWN SHADERS");
    GLuint vs = shader(GL_VERTEX_SHADER, vertex_source),
           ps = shader(GL_FRAGMENT_SHADER, pixel_source);
    if (!vs || !ps)
        return -10;
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, ps);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return -11;
    glDeleteShader(vs);
    glDeleteShader(ps);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenTextures(2, textures);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int plane = 0; plane < 2; ++plane)
    {
        glActiveTexture(GL_TEXTURE0 + plane);
        glBindTexture(GL_TEXTURE_2D, textures[plane]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, plane ? GL_RG8 : GL_R8, plane ? 960 : 1920,
                     plane ? 540 : 1080, 0, plane ? GL_RG : GL_RED, GL_UNSIGNED_BYTE, nullptr);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "planeY"), 0);
    glUniform1i(glGetUniformLocation(program, "planeUV"), 1);
    glViewport(0, 0, 1920, 1080);
    return glGetError() == GL_NO_ERROR ? 0 : -16;
}
int consume(const tube5::VideoOutput &frame, void *) noexcept
{
    if (!frame.buffer || frame.width != 1920 || frame.height < 1080 || frame.height > 1088 ||
        (frame.height & 1u) || frame.format != 0 || frame.pitch_bytes < 1920 ||
        frame.pitch_bytes > 4096 || (frame.pitch_bytes & 1u))
        return -22;
    const auto *y = static_cast<const unsigned char *>(frame.buffer);
    const auto *uv = y + static_cast<std::size_t>(frame.pitch_bytes) * frame.height;
    const auto upload_start = sceKernelGetProcessTime();
    for (int plane = 0; plane < 2; ++plane)
    {
        glActiveTexture(GL_TEXTURE0 + plane);
        glBindTexture(GL_TEXTURE_2D, textures[plane]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.pitch_bytes / (plane ? 2 : 1));
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, plane ? 960 : 1920, plane ? 540 : 1080,
                        plane ? GL_RG : GL_RED, GL_UNSIGNED_BYTE, plane ? uv : y);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    upload_max_us = std::max(upload_max_us, sceKernelGetProcessTime() - upload_start);
#ifdef TUBE5_GPU_CLIP
    const auto target = started + static_cast<std::uint64_t>(presented) * 1000000 / 30;
    const auto now = sceKernelGetProcessTime();
    if (now < target)
        sceKernelUsleep(static_cast<unsigned>(target - now));
    else if (now - target > 1000000 / 30)
        ++late_frames;
#endif
    const auto draw_start = sceKernelGetProcessTime();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    draw_max_us = std::max(draw_max_us, sceKernelGetProcessTime() - draw_start);
    if (glGetError() != GL_NO_ERROR)
        return -12;
    // Each read is from the rendered target, before swapping. Native source
    // stays owned by the decoder until upload, draw and readback have finished.
    constexpr int points[5][2] = {{47, 47}, {400, 270}, {960, 540}, {1500, 800}, {1870, 1030}};
    const bool check_samples = !presented || (tube5::hardware_clip_status().active &&
                                              tube5::hardware_clip_status().decoded ==
                                                  tube5::hardware_clip_status().frames);
    if (check_samples)
        for (const auto &point : points)
        {
            unsigned char rgba[4]{};
            glReadPixels(point[0], point[1], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            const unsigned row = 1079 - point[1], col = point[0];
            const double yy = std::max(0, static_cast<int>(y[row * frame.pitch_bytes + col]) - 16);
            const double uu = uv[(row / 2) * frame.pitch_bytes + (col / 2) * 2] - 128;
            const double vv = uv[(row / 2) * frame.pitch_bytes + (col / 2) * 2 + 1] - 128;
            const double rgb[3] = {(298.0 * yy + 459.0 * vv) / 256.0,
                                   (298.0 * yy - 55.0 * uu - 136.0 * vv) / 256.0,
                                   (298.0 * yy + 541.0 * uu) / 256.0};
            for (int component = 0; component < 3; ++component)
            {
                const int expected = static_cast<int>(std::clamp(rgb[component], 0.0, 255.0) + 0.5);
                const int delta = static_cast<int>(rgba[component]) - expected;
                differences += delta > 3 || delta < -3;
            }
            ++samples;
        }
    glFinish();
    if (glGetError() != GL_NO_ERROR)
        return -13;
    const auto swap_start = sceKernelGetProcessTime();
    if (!eglSwapBuffers(display, surface))
        return -14;
    glFinish();
    swap_max_us = std::max(swap_max_us, sceKernelGetProcessTime() - swap_start);
    if (glGetError() != GL_NO_ERROR)
        return -17;
    ++presented;
    if (presented == 1 || presented % 30 == 0 || differences)
        log(differences ? "PICTURE MISMATCH" : "FRAME PRESENTED - SAMPLES OK");
    // GL keeps independent uploaded planes; native decoder can now close.
    return differences ? -15 : 0;
}
} // namespace
int main()
{
#ifdef TUBE5_GPU_CLIP
    tube5::failure_log_build_label("BOOT-DIAG-67-GPUCLIP");
#else
    tube5::failure_log_build_label("BOOT-DIAG-66-GPUFRAME");
#endif
    log("EGL DISPLAY");
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0, count = 0;
    const EGLint config_args[] = {EGL_SURFACE_TYPE,
                                  EGL_WINDOW_BIT,
                                  EGL_RENDERABLE_TYPE,
                                  EGL_OPENGL_BIT,
                                  EGL_RED_SIZE,
                                  8,
                                  EGL_GREEN_SIZE,
                                  8,
                                  EGL_BLUE_SIZE,
                                  8,
                                  EGL_ALPHA_SIZE,
                                  8,
                                  EGL_NONE};
    const EGLint context_args[] = {EGL_CONTEXT_MAJOR_VERSION_KHR,
                                   3,
                                   EGL_CONTEXT_MINOR_VERSION_KHR,
                                   3,
                                   EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                                   EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                                   EGL_NONE};
    EGLConfig config = nullptr;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor) ||
        !eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(display, config_args, &config, 1, &count) ||
        count != 1)
    {
        result = -1;
        log("EGL INIT FAILED");
        return 1;
    }
    log("EGL WINDOW");
    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, nullptr);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_args);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context))
    {
        result = -2;
        log("EGL CONTEXT FAILED");
        return 1;
    }
    context_ready = true;
    EGLint width = 0, height = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    if (width != 1920 || height != 1080)
    {
        result = -3;
        log("UNEXPECTED OUTPUT SIZE");
        return 1;
    }
    result = setup_graphics();
    if (result)
    {
        log("GRAPHICS SETUP FAILED");
        return 1;
    }
    eglSwapInterval(display, 1);
    result = sceSysmoduleLoadModule(207);
    if (result)
    {
        log("VIDEO MODULE FAILED");
        return 1;
    }
#ifdef TUBE5_GPU_CLIP
    log("OPEN OWN B FRAME CLIP");
    result = tube5::hardware_clip_begin("/app0/assets/hw-bclip.t5hc",
                                        "/download0/tube5-gpu-clip-decoder.log");
    started = sceKernelGetProcessTime();
    while (!result && tube5::hardware_clip_status().active)
    {
        const int step = tube5::hardware_clip_step(consume, nullptr);
        if (step == 0)
            tube5::hardware_clip_presented(0);
        else if (step != 2)
        {
            if (step != 1)
                result = step;
            break;
        }
    }
    const int cleanup = tube5::hardware_clip_close(false, result);
    if (!result)
        result = cleanup;
    glFinish();
    glUseProgram(0);
    glBindVertexArray(0);
    glDeleteTextures(2, textures);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glFinish();
    if (!result && glGetError() != GL_NO_ERROR)
        result = -18;
    log(result ? "CLIP FAILED" : "CLIP COMPLETE - DECODER CLOSED - GL RESOURCES RELEASED");
#else
    log("DECODE OWN FRAME");
    result = tube5::hardware_frame_probe("/app0/assets/hw-frame.h264",
                                         "/download0/tube5-gpu-decoder.log", consume, nullptr);
    log(result ? "FRAME FAILED" : "READY - FRAME HELD - DECODER CLOSED");
#endif
    // Stay alive for FTP evidence and visual inspection; PS menu closes app.
    for (;;)
        sceKernelUsleep(16000);
}
