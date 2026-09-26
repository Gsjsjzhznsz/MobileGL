// MobileGL - MG_Backend/DirectGLES/FSR1.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
//
// See FSR1.h. Pass constants are computed on the CPU with the same formulas as
// Arm's ffxFsrPopulateEasuConstants()/FsrRcasCon() (the functions were exercised
// bit-for-bit against the vendored Arm CPU headers in the MobileGlues tree; the
// transcription here matches, e.g. for render 960x540 -> surface 1280x720:
// con0 = 0.75 0.75 -0.125 -0.125 as float bits).
#include "FSR1.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <MG_Backend/BackendObject.h>
#include "DirectGLES.h"
#include "Managers.h"
#include "FSRShaderSourceESSL.h"

namespace MobileGL::MG_Backend::DirectGLES::FSR1Impl {
    namespace {

        struct FSRSizes {
            GLsizei surfaceW = 0, surfaceH = 0;
            GLsizei renderW = 0, renderH = 0;
        };

        // One GL context owns these (the backend drives a single EGL context;
        // g_Display/g_Surface in DirectGLES.cpp are statics for the same reason).
        bool s_enabled = false;
        float s_scale = 1.5f;
        // RCAS sharpness in stops. Default 0.2 reproduces the pre-slider
        // hardcoded behavior; MOBILEGL_FSR1_SHARPNESS (0-100 percent,
        // higher = sharper, bridged from config.json by the dispatcher)
        // remaps it with the same formula the MobileGlues core uses:
        // stops = (100 - percent) / 100 * 2.
        float s_sharpnessStops = 0.2f;
        bool s_initTried = false;
        bool s_resourcesOk = false;

        GLuint s_renderFBO = 0, s_renderTexture = 0, s_depthStencilRBO = 0;
        GLuint s_intermediateFBO = 0, s_intermediateTexture = 0;
        GLuint s_quadVAO = 0, s_quadVBO = 0;
        GLuint s_easuProgram = 0, s_rcasProgram = 0;
        GLint s_easuConLoc[4] = {-1, -1, -1, -1};
        GLint s_rcasConLoc = -1;

        FSRSizes s_sizes;           // applied sizes
        FSRSizes s_pending;         // surface size seen by the last Present
        bool s_surfaceDirty = true; // apply pending at the next pass

        GLuint s_cachedEasuCon[4][4];
        GLsizei s_cachedInputW = 0, s_cachedInputH = 0, s_cachedOutputW = 0, s_cachedOutputH = 0;

        GLuint BitCastF32(float f) {
            GLuint u;
            std::memcpy(&u, &f, sizeof(u));
            return u;
        }

        void RefreshConstants() {
            if (s_cachedInputW == s_sizes.renderW && s_cachedInputH == s_sizes.renderH &&
                s_cachedOutputW == s_sizes.surfaceW && s_cachedOutputH == s_sizes.surfaceH && s_cachedOutputW != 0) {
                return;
            }

            // ffxFsrPopulateEasuConstants (Arm ASR, MIT), CPU transcription.
            const float inW = static_cast<float>(s_sizes.renderW);
            const float inH = static_cast<float>(s_sizes.renderH);
            const float outW = static_cast<float>(s_sizes.surfaceW);
            const float outH = static_cast<float>(s_sizes.surfaceH);
            const GLuint con0[4] = {BitCastF32(inW / outW), BitCastF32(inH / outH),
                                    BitCastF32(0.5f * inW / outW - 0.5f), BitCastF32(0.5f * inH / outH - 0.5f)};
            const GLuint con1[4] = {BitCastF32(1.0f / inW), BitCastF32(1.0f / inH), BitCastF32(1.0f / inW),
                                    BitCastF32(-1.0f / inH)};
            const GLuint con2[4] = {BitCastF32(-1.0f / inW), BitCastF32(2.0f / inH), BitCastF32(1.0f / inW),
                                    BitCastF32(2.0f / inH)};
            const GLuint con3[4] = {BitCastF32(0.0f / inW), BitCastF32(4.0f / inH), 0, 0};
            std::memcpy(s_cachedEasuCon[0], con0, sizeof(con0));
            std::memcpy(s_cachedEasuCon[1], con1, sizeof(con1));
            std::memcpy(s_cachedEasuCon[2], con2, sizeof(con2));
            std::memcpy(s_cachedEasuCon[3], con3, sizeof(con3));

            s_cachedInputW = s_sizes.renderW;
            s_cachedInputH = s_sizes.renderH;
            s_cachedOutputW = s_sizes.surfaceW;
            s_cachedOutputH = s_sizes.surfaceH;
        }

        void CreateTexture2D(GLuint* texture, GLsizei width, GLsizei height) {
            g_GLESFuncs.glGenTextures(1, texture);
            g_GLESFuncs.glBindTexture(GL_TEXTURE_2D, *texture);
            g_GLESFuncs.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            g_GLESFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            g_GLESFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            g_GLESFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            g_GLESFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            g_GLESFuncs.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        }

        void DeleteTargets() {
            if (s_renderFBO) g_GLESFuncs.glDeleteFramebuffers(1, &s_renderFBO);
            if (s_renderTexture) g_GLESFuncs.glDeleteTextures(1, &s_renderTexture);
            if (s_depthStencilRBO) g_GLESFuncs.glDeleteRenderbuffers(1, &s_depthStencilRBO);
            if (s_intermediateFBO) g_GLESFuncs.glDeleteFramebuffers(1, &s_intermediateFBO);
            if (s_intermediateTexture) g_GLESFuncs.glDeleteTextures(1, &s_intermediateTexture);
            s_renderFBO = s_renderTexture = s_depthStencilRBO = 0;
            s_intermediateFBO = s_intermediateTexture = 0;
        }

        void CreateTargets() {
            CreateTexture2D(&s_renderTexture, s_sizes.renderW, s_sizes.renderH);
            g_GLESFuncs.glGenRenderbuffers(1, &s_depthStencilRBO);
            g_GLESFuncs.glBindRenderbuffer(GL_RENDERBUFFER, s_depthStencilRBO);
            g_GLESFuncs.glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, s_sizes.renderW, s_sizes.renderH);
            g_GLESFuncs.glGenFramebuffers(1, &s_renderFBO);
            g_GLESFuncs.glBindFramebuffer(GL_FRAMEBUFFER, s_renderFBO);
            g_GLESFuncs.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_renderTexture,
                                               0);
            g_GLESFuncs.glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                                  s_depthStencilRBO);

            CreateTexture2D(&s_intermediateTexture, s_sizes.surfaceW, s_sizes.surfaceH);
            g_GLESFuncs.glGenFramebuffers(1, &s_intermediateFBO);
            g_GLESFuncs.glBindFramebuffer(GL_FRAMEBUFFER, s_intermediateFBO);
            g_GLESFuncs.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                               s_intermediateTexture, 0);

            // Park the driver on the redirect: the shadow's logical 0 must map to
            // the render FBO while FSR1 owns the default framebuffer.
            g_GLESFuncs.glBindFramebuffer(GL_FRAMEBUFFER, s_renderFBO);
            MGLOG_I("FSR1 targets ready: render %dx%d -> surface %dx%d", s_sizes.renderW, s_sizes.renderH,
                    s_sizes.surfaceW, s_sizes.surfaceH);
        }

        GLuint CompileProgram(const char* vsSource, const char* fsSource) {
            const GLuint vs = g_GLESFuncs.glCreateShader(GL_VERTEX_SHADER);
            const GLuint fs = g_GLESFuncs.glCreateShader(GL_FRAGMENT_SHADER);
            g_GLESFuncs.glShaderSource(vs, 1, &vsSource, nullptr);
            g_GLESFuncs.glCompileShader(vs);
            g_GLESFuncs.glShaderSource(fs, 1, &fsSource, nullptr);
            g_GLESFuncs.glCompileShader(fs);
            GLint vsStatus = GL_FALSE, fsStatus = GL_FALSE;
            g_GLESFuncs.glGetShaderiv(vs, GL_COMPILE_STATUS, &vsStatus);
            g_GLESFuncs.glGetShaderiv(fs, GL_COMPILE_STATUS, &fsStatus);
            GLuint program = 0;
            if (vsStatus && fsStatus) {
                program = g_GLESFuncs.glCreateProgram();
                g_GLESFuncs.glAttachShader(program, vs);
                g_GLESFuncs.glAttachShader(program, fs);
                g_GLESFuncs.glLinkProgram(program);
                GLint linkStatus = GL_FALSE;
                g_GLESFuncs.glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
                if (!linkStatus) program = 0;
            }
            g_GLESFuncs.glDeleteShader(vs);
            g_GLESFuncs.glDeleteShader(fs);
            if (program == 0) {
                char log[512] = {0};
                g_GLESFuncs.glGetShaderInfoLog(fs, sizeof(log) - 1, nullptr, log);
                MGLOG_E("FSR1 shader compile/link failed: %s", log);
            }
            return program;
        }

        void CreateResources() {
            s_easuProgram = CompileProgram(FSR_VS_ESSL, FSR_EASU_FS_ESSL);
            s_rcasProgram = CompileProgram(FSR_VS_ESSL, FSR_RCAS_FS_ESSL);
            if (s_easuProgram == 0 || s_rcasProgram == 0) {
                MGLOG_E("FSR1 disabled: programs failed to build");
                return;
            }
            for (int i = 0; i < 4; ++i) {
                char name[32];
                snprintf(name, sizeof(name), "uEasuCon%d", i);
                s_easuConLoc[i] = g_GLESFuncs.glGetUniformLocation(s_easuProgram, name);
            }
            s_rcasConLoc = g_GLESFuncs.glGetUniformLocation(s_rcasProgram, "uRcasCon");

            const float quadVertices[] = {-1.0f, 1.0f,  0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, 1.0f, 0.0f,
                                          -1.0f, 1.0f,  0.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 0.0f, 1.0f,  1.0f,  1.0f, 1.0f};
            g_GLESFuncs.glGenVertexArrays(1, &s_quadVAO);
            g_GLESFuncs.glGenBuffers(1, &s_quadVBO);
            g_GLESFuncs.glBindVertexArray(s_quadVAO);
            g_GLESFuncs.glBindBuffer(GL_ARRAY_BUFFER, s_quadVBO);
            g_GLESFuncs.glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
            g_GLESFuncs.glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            g_GLESFuncs.glEnableVertexAttribArray(0);
            g_GLESFuncs.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                              (void*)(2 * sizeof(float)));
            g_GLESFuncs.glEnableVertexAttribArray(1);
            g_GLESFuncs.glBindBuffer(GL_ARRAY_BUFFER, 0);
            g_GLESFuncs.glBindVertexArray(0);

            // Samplers and the RCAS constant are program state: set once, ever.
            g_GLESFuncs.glUseProgram(s_easuProgram);
            g_GLESFuncs.glUniform1i(g_GLESFuncs.glGetUniformLocation(s_easuProgram, "uInputTex"), 0);
            g_GLESFuncs.glUseProgram(s_rcasProgram);
            g_GLESFuncs.glUniform1i(g_GLESFuncs.glGetUniformLocation(s_rcasProgram, "uInputTex"), 0);
            // FsrRcasCon: con.x = exp2(-sharpness) as float bits; the fp32 RCAS path
            // reads only con.x. The stops come from MOBILEGL_FSR1_SHARPNESS (see
            // IsEnabled); 0.2 stops = the 90% default.
            const GLuint rcasCon[4] = {BitCastF32(std::exp2(-s_sharpnessStops)), 0, 0, 0};
            g_GLESFuncs.glUniform4uiv(s_rcasConLoc, 1, rcasCon);
            g_GLESFuncs.glUseProgram(0);

            s_sizes.surfaceW = s_pending.surfaceW = 1280;
            s_sizes.surfaceH = s_pending.surfaceH = 720;
            s_sizes.renderW = s_pending.renderW = 960;
            s_sizes.renderH = s_pending.renderH = 540;
            s_surfaceDirty = true;
            s_resourcesOk = true;
        }

        void ApplySurfaceSize() {
            if (!s_surfaceDirty) return;
            s_surfaceDirty = false;
            s_sizes.surfaceW = s_pending.surfaceW;
            s_sizes.surfaceH = s_pending.surfaceH;
            // CalculateRenderResolution: render = surface / preset scale, even-rounded.
            s_sizes.renderW = static_cast<GLsizei>(static_cast<float>(s_sizes.surfaceW) / s_scale) & ~1;
            s_sizes.renderH = static_cast<GLsizei>(static_cast<float>(s_sizes.surfaceH) / s_scale) & ~1;
            DeleteTargets();
            CreateTargets();
            RefreshConstants();
        }

        // Driver-level save/restore around the passes. Every field is re-pushed
        // raw: the backend's shadows keep claiming whatever the application last
        // set, so putting the driver exactly back keeps them all truthful.
        struct PassStateGuard {
            GLint program = 0, vao = 0, arrayBuffer = 0, activeTexture = GL_TEXTURE0, unit0Texture = 0;
            GLboolean scissor = GL_FALSE, blend = GL_FALSE, depth = GL_FALSE, cull = GL_FALSE;
            GLboolean mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
            GLint viewport[4] = {0, 0, 0, 0};

            explicit PassStateGuard(bool active) {
                if (!active) return;
                g_GLESFuncs.glGetIntegerv(GL_CURRENT_PROGRAM, &program);
                g_GLESFuncs.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
                g_GLESFuncs.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
                g_GLESFuncs.glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
                g_GLESFuncs.glActiveTexture(GL_TEXTURE0);
                g_GLESFuncs.glGetIntegerv(GL_TEXTURE_BINDING_2D, &unit0Texture);
                scissor = g_GLESFuncs.glIsEnabled(GL_SCISSOR_TEST);
                blend = g_GLESFuncs.glIsEnabled(GL_BLEND);
                depth = g_GLESFuncs.glIsEnabled(GL_DEPTH_TEST);
                cull = g_GLESFuncs.glIsEnabled(GL_CULL_FACE);
                g_GLESFuncs.glDisable(GL_SCISSOR_TEST);
                g_GLESFuncs.glDisable(GL_BLEND);
                g_GLESFuncs.glDisable(GL_DEPTH_TEST);
                g_GLESFuncs.glDisable(GL_CULL_FACE);
                g_GLESFuncs.glGetBooleanv(GL_COLOR_WRITEMASK, mask);
                g_GLESFuncs.glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                g_GLESFuncs.glGetIntegerv(GL_VIEWPORT, viewport);
            }

            ~PassStateGuard() {
                if (!s_resourcesOk) return;
                g_GLESFuncs.glColorMask(mask[0], mask[1], mask[2], mask[3]);
                cull ? g_GLESFuncs.glEnable(GL_CULL_FACE) : g_GLESFuncs.glDisable(GL_CULL_FACE);
                depth ? g_GLESFuncs.glEnable(GL_DEPTH_TEST) : g_GLESFuncs.glDisable(GL_DEPTH_TEST);
                blend ? g_GLESFuncs.glEnable(GL_BLEND) : g_GLESFuncs.glDisable(GL_BLEND);
                scissor ? g_GLESFuncs.glEnable(GL_SCISSOR_TEST) : g_GLESFuncs.glDisable(GL_SCISSOR_TEST);
                g_GLESFuncs.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
                g_GLESFuncs.glActiveTexture(GL_TEXTURE0);
                g_GLESFuncs.glBindTexture(GL_TEXTURE_2D, unit0Texture);
                g_GLESFuncs.glActiveTexture(activeTexture);
                g_GLESFuncs.glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
                g_GLESFuncs.glBindVertexArray(vao);
                g_GLESFuncs.glUseProgram(program);
            }
        };
    } // namespace

    Bool IsEnabled() {
        if (!s_initTried) {
            s_initTried = true;
            const char* env = std::getenv("MOBILEGL_FSR1");
            if (env && env[0] >= '1' && env[0] <= '4') {
                s_enabled = true;
                constexpr float kScales[4] = {1.3f, 1.5f, 1.7f, 2.0f};
                s_scale = kScales[env[0] - '1'];
                const char* sharpEnv = std::getenv("MOBILEGL_FSR1_SHARPNESS");
                if (sharpEnv && *sharpEnv) {
                    int percent = std::atoi(sharpEnv);
                    if (percent < 0) percent = 0;
                    if (percent > 100) percent = 100;
                    s_sharpnessStops = (100.0f - static_cast<float>(percent)) / 100.0f * 2.0f;
                }
                MGLOG_I("FSR1 enabled via MOBILEGL_FSR1=%c (scale %.1f, sharpness %.2f stops)", env[0], s_scale,
                        s_sharpnessStops);
            }
        }
        return s_enabled;
    }

    Uint RedirectedDefaultFBO() {
        if (!IsEnabled()) return 0;
        if (!s_resourcesOk) CreateResources();
        return s_resourcesOk ? s_renderFBO : 0;
    }

    void UpdateSurfaceSize(Int width, Int height) {
        if (!IsEnabled() || width <= 0 || height <= 0) return;
        if (width == s_pending.surfaceW && height == s_pending.surfaceH && !s_surfaceDirty) return;
        s_pending.surfaceW = static_cast<GLsizei>(width);
        s_pending.surfaceH = static_cast<GLsizei>(height);
        s_surfaceDirty = true;
    }

    void MapViewport(Int& x, Int& y, Int& w, Int& h, Bool onDefaultDraw) {
        if (!IsEnabled() || !s_resourcesOk || !onDefaultDraw) return;
        // The application thinks its window is the surface; the pixels land in the
        // render-sized redirect. Remap the full viewport so the frame covers it.
        x = 0;
        y = 0;
        w = s_sizes.renderW;
        h = s_sizes.renderH;
    }

    void MapScissor(Int& x, Int& y, Int& w, Int& h, Bool onDefaultDraw) {
        if (!IsEnabled() || !s_resourcesOk || !onDefaultDraw) return;
        if (s_sizes.renderW == s_sizes.surfaceW && s_sizes.renderH == s_sizes.surfaceH) return;
        // Surface pixels -> render pixels (GLdouble: GLsizei products overflow at
        // 4K-plus sizes).
        const double scaleX = static_cast<double>(s_sizes.renderW) / s_sizes.surfaceW;
        const double scaleY = static_cast<double>(s_sizes.renderH) / s_sizes.surfaceH;
        x = static_cast<Int>(x * scaleX);
        y = static_cast<Int>(y * scaleY);
        w = static_cast<Int>(w * scaleX);
        h = static_cast<Int>(h * scaleY);
    }

    void RunUpscalePasses() {
        if (!IsEnabled()) return;
        if (!s_resourcesOk) CreateResources();
        if (!s_resourcesOk) return;
        ApplySurfaceSize();
        RefreshConstants();

        PassStateGuard guard(true);

        // ---- pass 1: EASU, redirect color -> intermediate at surface size ----
        g_GLESFuncs.glBindFramebuffer(GL_FRAMEBUFFER, s_intermediateFBO);
        g_GLESFuncs.glViewport(0, 0, s_sizes.surfaceW, s_sizes.surfaceH);
        g_GLESFuncs.glUseProgram(s_easuProgram);
        g_GLESFuncs.glActiveTexture(GL_TEXTURE0);
        g_GLESFuncs.glBindTexture(GL_TEXTURE_2D, s_renderTexture);
        g_GLESFuncs.glUniform4uiv(s_easuConLoc[0], 1, s_cachedEasuCon[0]);
        g_GLESFuncs.glUniform4uiv(s_easuConLoc[1], 1, s_cachedEasuCon[1]);
        g_GLESFuncs.glUniform4uiv(s_easuConLoc[2], 1, s_cachedEasuCon[2]);
        g_GLESFuncs.glUniform4uiv(s_easuConLoc[3], 1, s_cachedEasuCon[3]);
        g_GLESFuncs.glBindVertexArray(s_quadVAO);
        g_GLESFuncs.glDrawArrays(GL_TRIANGLES, 0, 6);

        // ---- pass 2: RCAS, intermediate -> the real framebuffer 0 ----
        g_GLESFuncs.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_GLESFuncs.glUseProgram(s_rcasProgram);
        g_GLESFuncs.glBindTexture(GL_TEXTURE_2D, s_intermediateTexture);
        g_GLESFuncs.glDrawArrays(GL_TRIANGLES, 0, 6);

        // The guard restores everything it saved except the framebuffer bindings:
        // those follow the binding shadow. Whatever the application's logical bind
        // is, the driver has to end on its physical equivalent - which for the
        // redirected logical default is the render FBO, not name 0.
        const Uint shadowDraw = FramebufferImpl::CurrentFramebufferBinding(FramebufferTarget::Draw);
        const Uint shadowRead = FramebufferImpl::CurrentFramebufferBinding(FramebufferTarget::Read);
        // The shadow holds PHYSICAL ids (BindFramebufferId redirects before recording),
        // so its renderFBO entry means "the application's framebuffer 0". A raw 0 can
        // only come from the pre-FSR pin or a MakeCurrent probe - same physical answer.
        const Uint physicalDraw = (shadowDraw == 0 || shadowDraw == s_renderFBO) ? s_renderFBO : shadowDraw;
        const Uint physicalRead = (shadowRead == 0 || shadowRead == s_renderFBO) ? s_renderFBO : shadowRead;
        g_GLESFuncs.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, physicalDraw);
        g_GLESFuncs.glBindFramebuffer(GL_READ_FRAMEBUFFER, physicalRead);
    }

} // namespace MobileGL::MG_Backend::DirectGLES::FSR1Impl
