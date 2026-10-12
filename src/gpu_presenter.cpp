// SPDX-License-Identifier: GPL-3.0-or-later
#ifdef TUBE5_GPU_PLAYER
#include "gpu_presenter.hpp"
#include "failure_log.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstddef>
extern "C" std::uint64_t sceKernelGetProcessTime();
namespace
{
constexpr std::size_t ui_bytes = 1920u * 1080u * 4u;
constexpr std::size_t video_bytes = 4096u * 1080u * 3u / 2u;
EGLDisplay display = EGL_NO_DISPLAY;
EGLSurface surface = EGL_NO_SURFACE;
GLuint vao = 0, programs[2]{}, buffers[2]{}, textures[2]{};
GLint video_size = -1, video_pitch_y = -1, video_pitch_uv = -1, video_uv = -1;
GLint video_matrix = -1, video_range = -1;
bool ready = false, video_valid = false, video_visible = false;
bool oracle_done = false, oracle_pending = false, oracle_bt709 = false, oracle_full = false;
int source_width = 0, source_height = 0, source_pitch_y = 0, source_pitch_uv = 0;
unsigned char center_yuv[3][3][3]{}, gpu_rgb[4]{};
int cpu_rgb[3]{}, oracle_errors = 0;
SDL_Rect video_area{};
char error[128]{};
unsigned flips = 0, uploads = 0, sessions = 0;
std::uint64_t upload_max = 0, present_max = 0;
const char *vertex = "#version 330 core\n"
                     "out vec2 uv;const vec2 corners[6]=vec2[6](vec2(0,0),vec2(1,0),"
                     "vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));"
                     "void main(){vec2 p=corners[gl_VertexID];"
                     "uv=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0,1);}";
const char *ui_pixel =
    "#version 330 core\n"
    "in vec2 uv;out vec4 color;uniform usamplerBuffer image;"
    "void main(){ivec2 p=clamp(ivec2(uv*vec2(1920,1080)),ivec2(0),ivec2(1919,1079));"
    "int i=p.y*1920+p.x;uvec4 v=texelFetch(image,i>>2);uint w=v[i&3];"
    "color=vec4(float(w&255u),float((w>>8)&255u),float((w>>16)&255u),"
    "float((w>>24)&255u))/255.0;}";
const char *video_pixel =
    "#version 330 core\n"
    "in vec2 uv;out vec4 color;uniform usamplerBuffer image;uniform ivec2 imageSize;"
    "uniform int pitchY;uniform int pitchUV;uniform int uvOffset;uniform int bt709;"
    "uniform int fullRange;"
    "float byteAt(int i){uvec4 v=texelFetch(image,i>>4);"
    "return float((v[(i>>2)&3]>>uint((i&3)*8))&255u)/255.0;}"
    "void main(){ivec2 p=clamp(ivec2(uv*vec2(imageSize)),ivec2(0),imageSize-ivec2(1));"
    "int c=uvOffset+(p.y/2)*pitchUV+(p.x/2)*2;"
    "float y=byteAt(p.y*pitchY+p.x),u=byteAt(c)-128.0/255.0,v=byteAt(c+1)-128.0/255.0;"
    "vec3 rgb;if(fullRange!=0){"
    "if(bt709!=0)rgb=vec3(y+1.5748*v,y-0.187324*u-0.468124*v,y+1.8556*u);"
    "else rgb=vec3(y+1.402*v,y-0.344136*u-0.714136*v,y+1.772*u);"
    "}else{y=max(0.0,y-16.0/255.0);"
    "if(bt709!=0)rgb=vec3(298.0*y+459.0*v,298.0*y-55.0*u-136.0*v,298.0*y+541.0*u)/256.0;"
    "else rgb=vec3(298.0*y+409.0*v,298.0*y-100.0*u-208.0*v,298.0*y+516.0*u)/256.0;}"
    "color=vec4(clamp(rgb,0.0,1.0),1.0);}";
void snapshot(const char *stage)
{
    char report[1024]{};
    const int n = std::snprintf(
        report, sizeof(report),
        "TUBE5 GPU PLAYER V1\nBUILD %s\nSTAGE %s ERROR %s\n"
        "EGL OWNS VIDEOOUT - SDL OFFSCREEN UI - NV12 PACKED GPU COLOR\n"
        "FLIPS %u UPLOADS %u CLOSES %u VIDEO VALID %u VISIBLE %u\n"
        "MAX VIDEO UPLOAD US %llu COMPOSE SWAP FINISH US %llu\n"
        "SOURCE %dx%d PITCH %d/%d BT709 %u FULL %u ORACLE %u ERRORS %d\n"
        "CPU RGB %d/%d/%d GPU RGB %u/%u/%u\n"
        "CPU AVFRAME AND BUFFER COPIES REMAIN - NOT ZERO COPY\n",
        tube5::failure_log_build_label(), stage, error[0] ? error : "NONE", flips, uploads,
        sessions, video_valid ? 1u : 0u, video_visible ? 1u : 0u,
        static_cast<unsigned long long>(upload_max), static_cast<unsigned long long>(present_max),
        source_width, source_height, source_pitch_y, source_pitch_uv, oracle_bt709 ? 1u : 0u,
        oracle_full ? 1u : 0u, oracle_done ? 1u : 0u, oracle_errors, cpu_rgb[0], cpu_rgb[1],
        cpu_rgb[2], gpu_rgb[0], gpu_rgb[1], gpu_rgb[2]);
    if (n > 0)
        (void)tube5::write_failure_log("/download0/tube5-gpu-player.log", report,
                                       std::min<std::size_t>(n, sizeof(report) - 1));
}
bool fail(const char *stage, unsigned code)
{
    std::snprintf(error, sizeof(error), "%s %08X", stage, code);
    snapshot(stage);
    return false;
}
GLuint compile(GLenum type, const char *source)
{
    const GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        glGetShaderInfoLog(id, sizeof(error), nullptr, error);
        snapshot("SHADER FAILED");
        glDeleteShader(id);
        return 0;
    }
    return id;
}
GLuint link(const char *pixel)
{
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex), fs = compile(GL_FRAGMENT_SHADER, pixel);
    if (!vs || !fs)
        return 0;
    const GLuint id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glLinkProgram(id);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glGetProgramInfoLog(id, sizeof(error), nullptr, error);
        snapshot("PROGRAM FAILED");
        return 0;
    }
    return id;
}
} // namespace
namespace tube5
{
bool gpu_presenter_init() noexcept
{
    snapshot("EGL INIT");
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint count = 0;
    EGLConfig config = nullptr;
    const EGLint attributes[] = {EGL_SURFACE_TYPE,
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
    const EGLint context_attributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR,
                                         3,
                                         EGL_CONTEXT_MINOR_VERSION_KHR,
                                         3,
                                         EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                                         EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                                         EGL_NONE};
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr) ||
        !eglBindAPI(EGL_OPENGL_API) || !eglChooseConfig(display, attributes, &config, 1, &count) ||
        count != 1)
        return fail("EGL INIT", eglGetError());
    surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType)0, nullptr);
    const EGLContext context =
        eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
    if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, surface, surface, context))
        return fail("EGL CONTEXT", eglGetError());
    EGLint width = 0, height = 0;
    if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &height) || width != 1920 || height != 1080)
        return fail("EGL SIZE", eglGetError());
    GLint limit = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &limit);
    if (limit <= 0 || static_cast<std::size_t>(limit) < std::max(ui_bytes, video_bytes) / 16)
        return fail("BUFFER LIMIT", limit);
    programs[0] = link(ui_pixel);
    programs[1] = link(video_pixel);
    if (!programs[0] || !programs[1])
        return false;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(2, buffers);
    glGenTextures(2, textures);
    for (unsigned i = 0; i < 2; ++i)
    {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindBuffer(GL_TEXTURE_BUFFER, buffers[i]);
        glBufferData(GL_TEXTURE_BUFFER, i ? video_bytes : ui_bytes, nullptr, GL_STREAM_DRAW);
        glBindTexture(GL_TEXTURE_BUFFER, textures[i]);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32UI, buffers[i]);
        glUseProgram(programs[i]);
        glUniform1i(glGetUniformLocation(programs[i], "image"), i);
    }
    video_size = glGetUniformLocation(programs[1], "imageSize");
    video_pitch_y = glGetUniformLocation(programs[1], "pitchY");
    video_pitch_uv = glGetUniformLocation(programs[1], "pitchUV");
    video_uv = glGetUniformLocation(programs[1], "uvOffset");
    video_matrix = glGetUniformLocation(programs[1], "bt709");
    video_range = glGetUniformLocation(programs[1], "fullRange");
    const GLenum status = glGetError();
    if (status != GL_NO_ERROR)
        return fail("GL SETUP", status);
    if (!eglSwapInterval(display, 1))
        return fail("SWAP INTERVAL", eglGetError());
    ready = true;
    snapshot("READY");
    return true;
}
void gpu_begin_frame() noexcept
{
    video_visible = false;
}
const char *gpu_presenter_error() noexcept
{
    return error;
}
void gpu_clear_video() noexcept
{
    video_valid = video_visible = false;
    if (ready)
    {
        ++sessions;
        snapshot("VIDEO CLOSED");
    }
    oracle_done = oracle_pending = false;
}
bool gpu_upload_nv12(unsigned char *const *planes, const int *pitch, int width, int height,
                     bool bt709, bool full_range) noexcept
{
    if (!ready || !planes || !pitch || !planes[0] || !planes[1] || width <= 0 || height <= 0 ||
        width > 1920 || height > 1080 || (width & 1) || (height & 1) || pitch[0] < width ||
        pitch[1] < width || pitch[0] > 4096 || pitch[1] > 4096)
        return fail("NV12 LAYOUT", 22);
    const std::size_t y_bytes = static_cast<std::size_t>(pitch[0]) * height;
    const std::size_t uv_bytes = static_cast<std::size_t>(pitch[1]) * (height / 2);
    if (y_bytes + uv_bytes > video_bytes)
        return fail("NV12 SIZE", 22);
    source_width = width;
    source_height = height;
    source_pitch_y = pitch[0];
    source_pitch_uv = pitch[1];
    if (!oracle_done && width >= 4 && height >= 4)
    {
        oracle_bt709 = bt709;
        oracle_full = full_range;
        oracle_errors = 0;
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const int y = height / 2 + row - 1, x = width / 2 + col - 1;
                center_yuv[row][col][0] = planes[0][y * pitch[0] + x];
                center_yuv[row][col][1] = planes[1][(y / 2) * pitch[1] + (x / 2) * 2];
                center_yuv[row][col][2] = planes[1][(y / 2) * pitch[1] + (x / 2) * 2 + 1];
            }
        oracle_pending = true;
    }
    // GL owns a copied buffer. Raw AVFrame may be unlocked after these calls;
    // prior GPU readers retired in gpu_present_ui before this next update.
    const auto start = sceKernelGetProcessTime();
    glBindBuffer(GL_TEXTURE_BUFFER, buffers[1]);
    glBufferSubData(GL_TEXTURE_BUFFER, 0, y_bytes, planes[0]);
    glBufferSubData(GL_TEXTURE_BUFFER, y_bytes, uv_bytes, planes[1]);
    glUseProgram(programs[1]);
    glUniform2i(video_size, width, height);
    glUniform1i(video_pitch_y, pitch[0]);
    glUniform1i(video_pitch_uv, pitch[1]);
    glUniform1i(video_uv, y_bytes);
    glUniform1i(video_matrix, bt709 ? 1 : 0);
    glUniform1i(video_range, full_range ? 1 : 0);
    upload_max = std::max(upload_max, sceKernelGetProcessTime() - start);
    const GLenum status = glGetError();
    if (status != GL_NO_ERROR)
        return fail("VIDEO UPLOAD", status);
    video_valid = true;
    ++uploads;
    return true;
}
void gpu_draw_video(const SDL_Rect &area) noexcept
{
    video_area = area;
    video_visible = video_valid && area.w > 0 && area.h > 0 && area.x >= 0 && area.y >= 0 &&
                    area.x + area.w <= 1920 && area.y + area.h <= 1080;
}
bool gpu_present_ui(const std::uint32_t *pixels) noexcept
{
    if (!ready || !pixels)
        return fail("UI PIXELS", 22);
    const auto start = sceKernelGetProcessTime();
    glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_TEXTURE_BUFFER, buffers[0]);
    glBufferSubData(GL_TEXTURE_BUFFER, 0, ui_bytes, pixels);
    glUseProgram(programs[0]);
    glBindTexture(GL_TEXTURE_BUFFER, textures[0]);
    glViewport(0, 0, 1920, 1080);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (video_visible)
    {
        glActiveTexture(GL_TEXTURE1);
        glUseProgram(programs[1]);
        glBindTexture(GL_TEXTURE_BUFFER, textures[1]);
        glViewport(video_area.x, 1080 - video_area.y - video_area.h, video_area.w, video_area.h);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        if (oracle_pending && !oracle_done)
        {
            glFinish();
            const int rx = video_area.w / 2, ry = video_area.h / 2;
            glReadPixels(video_area.x + rx, 1080 - video_area.y - video_area.h + ry, 1, 1, GL_RGBA,
                         GL_UNSIGNED_BYTE, gpu_rgb);
            const int x = (rx * 2 + 1) * source_width / (video_area.w * 2);
            const int y = (video_area.h * 2 - ry * 2 - 1) * source_height / (video_area.h * 2);
            const int col = x - source_width / 2 + 1, row = y - source_height / 2 + 1;
            if (row >= 0 && row < 3 && col >= 0 && col < 3)
            {
                const auto *sample = center_yuv[row][col];
                const double u = static_cast<int>(sample[1]) - 128;
                const double v = static_cast<int>(sample[2]) - 128;
                double rgb[3]{};
                if (oracle_full)
                {
                    const double luma = sample[0];
                    rgb[0] = luma + (oracle_bt709 ? 1.5748 : 1.402) * v;
                    rgb[1] = luma - (oracle_bt709 ? 0.187324 : 0.344136) * u -
                             (oracle_bt709 ? 0.468124 : 0.714136) * v;
                    rgb[2] = luma + (oracle_bt709 ? 1.8556 : 1.772) * u;
                }
                else
                {
                    const double luma = std::max(0, static_cast<int>(sample[0]) - 16);
                    rgb[0] = (298 * luma + (oracle_bt709 ? 459 : 409) * v) / 256;
                    rgb[1] = (298 * luma - (oracle_bt709 ? 55 : 100) * u -
                              (oracle_bt709 ? 136 : 208) * v) /
                             256;
                    rgb[2] = (298 * luma + (oracle_bt709 ? 541 : 516) * u) / 256;
                }
                for (unsigned c = 0; c < 3; ++c)
                {
                    cpu_rgb[c] = static_cast<int>(std::clamp(rgb[c], 0.0, 255.0) + 0.5);
                    const int delta = static_cast<int>(gpu_rgb[c]) - cpu_rgb[c];
                    oracle_errors += delta > 3 || delta < -3;
                }
            }
            else
                oracle_errors = -1;
            oracle_done = true;
            oracle_pending = false;
            snapshot("COLOR ORACLE");
        }
    }
    if (!eglSwapBuffers(display, surface))
        return fail("EGL SWAP", eglGetError());
    glFinish();
    const GLenum status = glGetError();
    if (status != GL_NO_ERROR)
        return fail("GPU PRESENT", status);
    present_max = std::max(present_max, sceKernelGetProcessTime() - start);
    if (++flips == 1 || flips % 600 == 0)
        snapshot("PRESENTING");
    return true;
}
} // namespace tube5
#endif
