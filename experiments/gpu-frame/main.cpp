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
GLuint program = 0, vao = 0, textures[8]{}, nv12_buffers[8]{};
constexpr std::size_t buffer_capacity = 4096u * 1088u * 3u / 2u;
GLint buffer_limit = 0, texture_limit = 0;
unsigned buffer_chunks = 0;
std::size_t chunk_bytes = 0;
#ifdef TUBE5_GPU_LINEAR
constexpr unsigned texture_count = 8;
#else
constexpr unsigned texture_count = 2;
#endif
unsigned presented = 0, late_frames = 0;
std::uint64_t started = 0, upload_max_us = 0, draw_max_us = 0, swap_max_us = 0;
std::uint64_t first_presented = 0, last_presented = 0, startup_us = 0;
std::uint64_t intervals[300]{}, work[300]{};
std::uint64_t upload_times[300]{}, draw_times[300]{}, swap_times[300]{};
unsigned interval_count = 0, work_count = 0;
std::uint64_t percentile(const std::uint64_t *values, unsigned count, unsigned pct)
{
    if (!count)
        return 0;
    std::uint64_t sorted[300]{};
    std::copy_n(values, count, sorted);
    std::sort(sorted, sorted + count);
    return sorted[(count - 1) * pct / 100];
}
const char *vertex_source = "#version 330 core\n"
                            "out vec2 uv;\n"
                            "void main(){ vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
                            "uv=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0,1); }\n";
const char *pixel_source =
    "#version 330 core\n"
#ifdef TUBE5_GPU_LINEAR
    "in vec2 uv;out vec4 color;uniform usamplerBuffer nv12[8];uniform int sourcePitch;"
    "uniform int uvOffset;uniform int chunkTexels;\n"
    "float byteAt(int i){int t=i>>4;int c=t/chunkTexels;int p=t%chunkTexels;uvec4 v;"
    "if(c==0)v=texelFetch(nv12[0],p);else if(c==1)v=texelFetch(nv12[1],p);"
    "else if(c==2)v=texelFetch(nv12[2],p);else if(c==3)v=texelFetch(nv12[3],p);"
    "else if(c==4)v=texelFetch(nv12[4],p);else if(c==5)v=texelFetch(nv12[5],p);"
    "else if(c==6)v=texelFetch(nv12[6],p);else v=texelFetch(nv12[7],p);"
    "return float((v[(i>>2)&3]>>uint((i&3)*8))&255u)/255.0;}\n"
    "void main(){ivec2 p=clamp(ivec2(uv*vec2(1920.0,1080.0)),ivec2(0),ivec2(1919,1079));"
    "int c=uvOffset+(p.y/2)*sourcePitch+(p.x/2)*2;"
    "vec3 yuv=vec3(byteAt(p.y*sourcePitch+p.x),byteAt(c),byteAt(c+1));"
#else
    "in vec2 uv;out vec4 color;uniform sampler2D planeY;uniform sampler2D planeUV;\n"
    "void main(){vec3 yuv=vec3(texture(planeY,uv).r,texture(planeUV,uv).rg);"
#endif
    "float y=max(0.0,yuv.x*255.0-16.0)/255.0;"
    "float u=(yuv.y*255.0-128.0)/255.0;"
    "float v=(yuv.z*255.0-128.0)/255.0;"
    "color=vec4(clamp(vec3((298.0*y+459.0*v)/256.0,"
    "(298.0*y-55.0*u-136.0*v)/256.0,"
    "(298.0*y+541.0*u)/256.0),0.0,1.0),1.0); }\n";
void log(const char *next)
{
    stage = next;
    char text[1200];
    const int size = std::snprintf(
        text, sizeof(text),
        "TUBE5 GPU PRESENTATION V3\nBUILD %s\nSTAGE %s RESULT %d GL ERROR %u\n"
        "SAMPLES %d COMPONENT ERRORS %d\n"
#ifdef TUBE5_GPU_LINEAR
        "1080P NV12 GPU CONVERSION - CPU BULK BUFFER UPLOAD\n"
#else
        "1080P NV12 GPU CONVERSION - CPU TEXTURE UPLOAD\n"
#endif
        "PRESENTED %u MISS GT 1 FRAME %u MAX CALL UPLOAD/DRAW/SWAP FINISH US %llu/%llu/%llu\n"
        "STARTUP US %llu CONTINUOUS SPAN US %llu\n"
        "INTERVALS %u P50/P95/MAX US %llu/%llu/%llu\n"
        "WORK NO PACING/ORACLE %u P50/P95/MAX US %llu/%llu/%llu\n"
        "CALL P50 UPLOAD/DRAW/SWAP US %llu/%llu/%llu\n"
        "BUFFER LIMIT TEXELS %d TEXTURE UNITS %d CHUNKS %u CHUNK BYTES %llu RGBA32UI\n"
        "ISOLATED EXPERIMENT - NO PLAYER CHANGES - NOT ZERO COPY\n",
        tube5::failure_log_build_label(), stage, result,
        context_ready ? static_cast<unsigned>(glGetError()) : 0u, samples, differences, presented,
        late_frames, static_cast<unsigned long long>(upload_max_us),
        static_cast<unsigned long long>(draw_max_us), static_cast<unsigned long long>(swap_max_us),
        static_cast<unsigned long long>(startup_us),
        static_cast<unsigned long long>(last_presented - first_presented), interval_count,
        static_cast<unsigned long long>(percentile(intervals, interval_count, 50)),
        static_cast<unsigned long long>(percentile(intervals, interval_count, 95)),
        static_cast<unsigned long long>(percentile(intervals, interval_count, 100)), work_count,
        static_cast<unsigned long long>(percentile(work, work_count, 50)),
        static_cast<unsigned long long>(percentile(work, work_count, 95)),
        static_cast<unsigned long long>(percentile(work, work_count, 100)),
        static_cast<unsigned long long>(percentile(upload_times, work_count, 50)),
        static_cast<unsigned long long>(percentile(draw_times, work_count, 50)),
        static_cast<unsigned long long>(percentile(swap_times, work_count, 50)), buffer_limit,
        texture_limit, buffer_chunks, static_cast<unsigned long long>(chunk_bytes));
    if (size > 0)
#ifdef TUBE5_GPU_CLIP
        tube5::write_failure_log("/download0/tube5-gpu-clip.log", text,
#else
        tube5::write_failure_log("/download0/tube5-gpu-frame.log", text,
#endif
                                 std::min<std::size_t>(size, sizeof(text) - 1));
}
[[noreturn]] void hold_failure(const char *next)
{
    log(next);
    // Keep diagnostic data mounted. Returning from main would call exit(),
    // which produced a secondary SIGSYS in this homebrew sandbox.
    for (;;)
        sceKernelUsleep(16000);
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
    glGenTextures(texture_count, textures);
#ifdef TUBE5_GPU_LINEAR
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &buffer_limit);
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texture_limit);
    if (buffer_limit <= 0 || texture_limit < static_cast<GLint>(texture_count))
        return -19;
    chunk_bytes = std::min<std::size_t>(buffer_limit, buffer_capacity / 16) * 16;
    buffer_chunks = static_cast<unsigned>((buffer_capacity + chunk_bytes - 1) / chunk_bytes);
    if (buffer_chunks > texture_count)
        return -20;
    glGenBuffers(buffer_chunks, nv12_buffers);
    for (unsigned chunk = 0; chunk < buffer_chunks; ++chunk)
    {
        glBindBuffer(GL_TEXTURE_BUFFER, nv12_buffers[chunk]);
        const auto size = std::min(chunk_bytes, buffer_capacity - chunk * chunk_bytes);
        glBufferData(GL_TEXTURE_BUFFER, size, nullptr, GL_STREAM_DRAW);
    }
    for (unsigned unit = 0; unit < texture_count; ++unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_BUFFER, textures[unit]);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32UI, nv12_buffers[unit < buffer_chunks ? unit : 0]);
    }
#else
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
#endif
    glUseProgram(program);
#ifdef TUBE5_GPU_LINEAR
    const GLint units[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    glUniform1iv(glGetUniformLocation(program, "nv12[0]"), 8, units);
    glUniform1i(glGetUniformLocation(program, "chunkTexels"), chunk_bytes / 16);
#else
    glUniform1i(glGetUniformLocation(program, "planeY"), 0);
    glUniform1i(glGetUniformLocation(program, "planeUV"), 1);
#endif
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
#ifdef TUBE5_GPU_LINEAR
    const std::size_t bytes = static_cast<std::size_t>(frame.pitch_bytes) * frame.height * 3 / 2;
    if (bytes > buffer_capacity || bytes > frame.bytes)
        return -22;
    // Previous swap+finish retired all readers before this buffer is updated.
    for (std::size_t offset = 0; offset < bytes; offset += chunk_bytes)
    {
        const auto chunk = static_cast<unsigned>(offset / chunk_bytes);
        glBindBuffer(GL_TEXTURE_BUFFER, nv12_buffers[chunk]);
        glBufferSubData(GL_TEXTURE_BUFFER, 0, std::min(chunk_bytes, bytes - offset), y + offset);
    }
    glUniform1i(glGetUniformLocation(program, "sourcePitch"), frame.pitch_bytes);
    glUniform1i(glGetUniformLocation(program, "uvOffset"), frame.pitch_bytes * frame.height);
#else
    for (int plane = 0; plane < 2; ++plane)
    {
        glActiveTexture(GL_TEXTURE0 + plane);
        glBindTexture(GL_TEXTURE_2D, textures[plane]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.pitch_bytes / (plane ? 2 : 1));
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, plane ? 960 : 1920, plane ? 540 : 1080,
                        plane ? GL_RG : GL_RED, GL_UNSIGNED_BYTE, plane ? uv : y);
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif
    const auto upload_us = sceKernelGetProcessTime() - upload_start;
    upload_max_us = std::max(upload_max_us, upload_us);
    const auto draw_start = sceKernelGetProcessTime();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    const auto draw_us = sceKernelGetProcessTime() - draw_start;
    draw_max_us = std::max(draw_max_us, draw_us);
    if (glGetError() != GL_NO_ERROR)
        return -12;
    // Each read is from the rendered target, before swapping. Native source
    // stays owned by the decoder until upload, draw and readback have finished.
    constexpr int points[5][2] = {{47, 47}, {400, 270}, {960, 540}, {1500, 800}, {1870, 1030}};
    const bool check_samples = !presented || (tube5::hardware_clip_status().active &&
                                              tube5::hardware_clip_status().decoded ==
                                                  tube5::hardware_clip_status().frames);
    if (check_samples)
    {
        glFinish();
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
    }
    if (glGetError() != GL_NO_ERROR)
        return -13;
#ifdef TUBE5_GPU_CLIP
    const auto target = first_presented + static_cast<std::uint64_t>(presented) * 1000000 / 30;
    const auto now = sceKernelGetProcessTime();
    if (presented && now < target)
        sceKernelUsleep(static_cast<unsigned>(target - now));
#endif
    const auto swap_start = sceKernelGetProcessTime();
    if (!eglSwapBuffers(display, surface))
        return -14;
    glFinish();
    const auto finished = sceKernelGetProcessTime();
    const auto swap_us = finished - swap_start;
    swap_max_us = std::max(swap_max_us, swap_us);
    if (glGetError() != GL_NO_ERROR)
        return -17;
    if (!presented)
    {
        first_presented = finished;
        startup_us = started ? finished - started : 0;
    }
    else
    {
        if (interval_count < 300)
            intervals[interval_count++] = finished - last_presented;
#ifdef TUBE5_GPU_CLIP
        late_frames += finished > target + 1000000 / 30;
#endif
    }
    last_presented = finished;
    if (!check_samples && work_count < 300)
    {
        upload_times[work_count] = upload_us;
        draw_times[work_count] = draw_us;
        swap_times[work_count] = swap_us;
        work[work_count++] = upload_us + draw_us + swap_us;
    }
    ++presented;
    if (presented == 1 || presented % 30 == 0 || differences)
        log(differences ? "PICTURE MISMATCH" : "FRAME PRESENTED - SAMPLES OK");
    // GL keeps independent uploaded planes; native decoder can now close.
    return differences ? -15 : 0;
}
} // namespace
int main()
{
#ifdef TUBE5_GPU_LINEAR
    tube5::failure_log_build_label("BOOT-DIAG-70-GPUPACKED");
#elif defined(TUBE5_GPU_CLIP)
    tube5::failure_log_build_label("BOOT-DIAG-68-GPUPACE");
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
        hold_failure("EGL INIT FAILED");
    }
    log("EGL WINDOW");
    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, nullptr);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_args);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context))
    {
        result = -2;
        hold_failure("EGL CONTEXT FAILED");
    }
    context_ready = true;
    EGLint width = 0, height = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    if (width != 1920 || height != 1080)
    {
        result = -3;
        hold_failure("UNEXPECTED OUTPUT SIZE");
    }
    result = setup_graphics();
    if (result)
    {
        hold_failure("GRAPHICS SETUP FAILED");
    }
    eglSwapInterval(display, 1);
    result = sceSysmoduleLoadModule(207);
    if (result)
    {
        hold_failure("VIDEO MODULE FAILED");
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
    glDeleteTextures(texture_count, textures);
    if (buffer_chunks)
    {
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
        glDeleteBuffers(buffer_chunks, nv12_buffers);
    }
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
