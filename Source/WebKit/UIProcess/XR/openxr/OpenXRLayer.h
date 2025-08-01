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

#include "OpenXRSwapchain.h"
#include "OpenXRUtils.h"

#include <WebCore/PlatformXR.h>
#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/UniqueRef.h>
#include <wtf/Vector.h>

namespace WebKit {

struct XRDeviceLayer;

class OpenXRLayer {
    WTF_MAKE_TZONE_ALLOCATED(OpenXRLayer);
    WTF_MAKE_NONCOPYABLE(OpenXRLayer);
public:
    virtual ~OpenXRLayer() = default;

    virtual std::optional<PlatformXR::FrameData::LayerData> startFrame() = 0;
    virtual XrCompositionLayerBaseHeader* endFrame(const XRDeviceLayer&, XrSpace, const Vector<XrView>&) = 0;

protected:
    OpenXRLayer() = default;
};

class OpenXRLayerProjection final: public OpenXRLayer  {
    WTF_MAKE_TZONE_ALLOCATED(OpenXRLayerProjection);
    WTF_MAKE_NONCOPYABLE(OpenXRLayerProjection);
public:
    static std::unique_ptr<OpenXRLayerProjection> create(XrInstance, XrSession, uint32_t width, uint32_t height, int64_t format, uint32_t sampleCount);
private:
    explicit OpenXRLayerProjection(UniqueRef<OpenXRSwapchain>&&);

    std::optional<PlatformXR::FrameData::LayerData> startFrame() final;
    XrCompositionLayerBaseHeader* endFrame(const XRDeviceLayer&, XrSpace, const Vector<XrView>&) final;

    UniqueRef<OpenXRSwapchain> m_swapchain;
    XrCompositionLayerProjection m_layerProjection;
    Vector<XrCompositionLayerProjectionView> m_projectionViews;
};

} // namespace WebKit

#endif // ENABLE(WEBXR) && USE(OPENXR)
