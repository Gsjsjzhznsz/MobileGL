// MobileGL - MG_Backend/DirectGLES/FSR1.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header
#pragma once

// mg-3backends FSR1 for the DirectGLES backend: Arm Accuracy Super Resolution
// (Arm's mobile-optimized FidelityFX FSR 1.0.2) on the same two-pass pipeline
// the MobileGlues GLES backend uses - EASU upscales the redirect target to the
// surface size, RCAS sharpens straight into the real framebuffer 0.
//
// While FSR1 is on, every bind of the logical default framebuffer inside the
// backend (BindFramebufferId's id == 0) is redirected to an owned render FBO at
// render resolution, and the render-state sync maps application viewports and
// scissor rectangles onto that FBO. Present() runs the two passes before the
// swap. Toggle: MOBILEGL_FSR1 env (0=off, 1..4 = UltraQuality..Performance),
// matching the documented launcher-side override for the MobileGL backends.

#include <Includes.h>

namespace MobileGL::MG_Backend::DirectGLES::FSR1Impl {

    // MOBILEGL_FSR1 != 0. Parsed once.
    Bool IsEnabled();

    // The FBO the logical default framebuffer redirects to (lazily created on
    // first use; requires a current context). 0 while FSR1 is disabled.
    Uint RedirectedDefaultFBO();

    // Latched from Present()'s surface query. A change recreates the targets at
    // the new surface size before the next frame renders.
    void UpdateSurfaceSize(Int width, Int height);

    // Render-state sync hooks: while the draw target is the redirected default
    // framebuffer, a full-bleed application viewport would clip to the redirect's
    // smaller extent, so it is remapped onto the render size; scissor rectangles
    // are framebuffer pixels and scale by the same ratio. No-op on any other
    // target or while disabled.
    void MapViewport(Int& x, Int& y, Int& w, Int& h, Bool onDefaultDraw);
    void MapScissor(Int& x, Int& y, Int& w, Int& h, Bool onDefaultDraw);

    // EASU + RCAS into the real framebuffer 0, driver state saved and restored
    // around the passes. Called from Present() right before the swap.
    void RunUpscalePasses();

} // namespace MobileGL::MG_Backend::DirectGLES::FSR1Impl
