/*
 * Copyright (C) 2025 Igalia, S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#if ENABLE(WEBXR) && USE(OPENXR)

#include "OpenXRUtils.h"
#include <WebCore/GraphicsTypesGL.h>
#include <WebCore/IntSize.h>
typedef void* EGLDisplay;
typedef void* EGLContext;
typedef void* EGLConfig;
typedef unsigned EGLenum;
#if defined(XR_USE_PLATFORM_EGL)
typedef void (*(*PFNEGLGETPROCADDRESSPROC)(const char *))(void);
#endif
#include <openxr/openxr_platform.h>
#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/Vector.h>

namespace WebKit {

class OpenXRSwapchain {
    WTF_MAKE_TZONE_ALLOCATED(OpenXRSwapchain);
    WTF_MAKE_NONCOPYABLE(OpenXRSwapchain);
public:
    static std::unique_ptr<OpenXRSwapchain> create(XrInstance, XrSession, const XrSwapchainCreateInfo&);
    ~OpenXRSwapchain();

    std::optional<PlatformGLObject> acquireImage();
    void releaseImage();
    XrSwapchain swapchain() const { return m_swapchain; }
    int32_t width() const { return m_createInfo.width; }
    int32_t height() const { return m_createInfo.height; }
    WebCore::IntSize size() const { return WebCore::IntSize(width(), height()); }

private:
    OpenXRSwapchain(XrInstance, XrSwapchain, const XrSwapchainCreateInfo&, Vector<XrSwapchainImageOpenGLESKHR>&&);

    XrInstance m_instance;
    XrSwapchain m_swapchain;
    XrSwapchainCreateInfo m_createInfo;
    Vector<XrSwapchainImageOpenGLESKHR> m_imageBuffers;
    PlatformGLObject m_acquiredTexture { 0 };
};

} // namespace WebKit

#endif // ENABLE(WEBXR) && USE(OPENXR)
