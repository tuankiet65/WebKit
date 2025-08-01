/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebGPUCompositorIntegrationImpl.h"

#if HAVE(WEBGPU_IMPLEMENTATION)

#include "WebGPUConvertToBackingContext.h"
#include "WebGPUDevice.h"
#include "WebGPUQueue.h"
#include "WebGPUTextureFormat.h"
#include <CoreFoundation/CoreFoundation.h>
#include <WebCore/IOSurface.h>
#include <WebCore/NativeImage.h>
#include <WebGPU/WebGPUExt.h>
#include <pal/spi/cg/CoreGraphicsSPI.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/spi/cocoa/IOSurfaceSPI.h>

namespace WebCore::WebGPU {

WTF_MAKE_TZONE_ALLOCATED_IMPL(CompositorIntegrationImpl);

CompositorIntegrationImpl::CompositorIntegrationImpl(ConvertToBackingContext& convertToBackingContext)
    : m_convertToBackingContext(convertToBackingContext)
{
}

CompositorIntegrationImpl::~CompositorIntegrationImpl() = default;

void CompositorIntegrationImpl::prepareForDisplay(uint32_t frameIndex, CompletionHandler<void()>&& completionHandler)
{
    if (RefPtr presentationContext = m_presentationContext)
        presentationContext->present(frameIndex);

    m_onSubmittedWorkScheduledCallback(WTFMove(completionHandler));
}

void CompositorIntegrationImpl::updateContentsHeadroom(float headroom)
{
#if HAVE(SUPPORT_HDR_DISPLAY)
    for (auto& ioSurface : m_renderBuffers)
        ioSurface->setContentEDRHeadroom(headroom);
#else
    UNUSED_PARAM(headroom);
#endif
}

#if PLATFORM(COCOA)
Vector<MachSendRight> CompositorIntegrationImpl::recreateRenderBuffers(int width, int height, WebCore::DestinationColorSpace&& colorSpace, WebCore::AlphaPremultiplication alphaMode, TextureFormat textureFormat, Device& device)
{
    m_renderBuffers.clear();
    m_device = device;

    if (RefPtr presentationContext = m_presentationContext) {
        static_cast<PresentationContext*>(presentationContext.get())->unconfigure();
        presentationContext->setSize(width, height);
    }

    constexpr int max2DTextureSize = 16384;
    width = std::max(1, std::min(max2DTextureSize, width));
    height = std::max(1, std::min(max2DTextureSize, height));
    IOSurface::Format colorFormat;
    switch (textureFormat) {
    case TextureFormat::Rgba8unorm:
    case TextureFormat::Rgba8unormSRGB:
        colorFormat = alphaMode == AlphaPremultiplication::Unpremultiplied ? IOSurface::Format::RGBX : IOSurface::Format::RGBA;
        break;
#if ENABLE(PIXEL_FORMAT_RGBA16F)
    case TextureFormat::Rgba16float:
        colorFormat = IOSurface::Format::RGBA16F;
        break;
#endif
    default:
        colorFormat = alphaMode == AlphaPremultiplication::Unpremultiplied ? IOSurface::Format::BGRX : IOSurface::Format::BGRA;
        break;
    }

    if (auto buffer = WebCore::IOSurface::create(nullptr, WebCore::IntSize(width, height), colorSpace, IOSurface::Name::WebGPU, colorFormat))
        m_renderBuffers.append(makeUniqueRefFromNonNullUniquePtr(WTFMove(buffer)));
    if (auto buffer = WebCore::IOSurface::create(nullptr, WebCore::IntSize(width, height), colorSpace, IOSurface::Name::WebGPU, colorFormat))
        m_renderBuffers.append(makeUniqueRefFromNonNullUniquePtr(WTFMove(buffer)));
    if (auto buffer = WebCore::IOSurface::create(nullptr, WebCore::IntSize(width, height), colorSpace, IOSurface::Name::WebGPU, colorFormat))
        m_renderBuffers.append(makeUniqueRefFromNonNullUniquePtr(WTFMove(buffer)));

    {
        auto renderBuffers = adoptCF(CFArrayCreateMutable(kCFAllocatorDefault, m_renderBuffers.size(), &kCFTypeArrayCallBacks));
        for (auto& ioSurface : m_renderBuffers)
            CFArrayAppendValue(renderBuffers.get(), ioSurface->surface());
        m_renderBuffersWereRecreatedCallback(static_cast<CFArrayRef>(renderBuffers));
    }

    return m_renderBuffers.map([](const auto& renderBuffer) {
        return renderBuffer->createSendRight();
    });
}
#endif

void CompositorIntegrationImpl::withDisplayBufferAsNativeImage(uint32_t bufferIndex, Function<void(WebCore::NativeImage*)> completion)
{
    if (!m_renderBuffers.size() || bufferIndex >= m_renderBuffers.size() || !m_device.get())
        return completion(nullptr);

    RefPtr<NativeImage> displayImage;
    bool isIOSurfaceSupportedFormat = false;
    if (RefPtr presentationContextPtr = m_presentationContext)
        displayImage = presentationContextPtr->getMetalTextureAsNativeImage(bufferIndex, isIOSurfaceSupportedFormat);

    if (!displayImage) {
        if (!isIOSurfaceSupportedFormat)
            return completion(nullptr);

        auto& renderBuffer = m_renderBuffers[bufferIndex];
        std::optional<CGImageAlphaInfo> alphaInfo;
#if ENABLE(PIXEL_FORMAT_RGBA16F)
        if (renderBuffer->pixelFormat() == IOSurface::Format::RGBA16F)
            alphaInfo = kCGImageAlphaNoneSkipLast;
#endif
        RetainPtr<CGContextRef> cgContext = renderBuffer->createPlatformContext(0, alphaInfo);

        if (cgContext)
            displayImage = NativeImage::create(renderBuffer->createImage(cgContext.get()));
    }

    if (!displayImage)
        return completion(nullptr);

    CGImageSetCachingFlags(displayImage->platformImage().get(), kCGImageCachingTransient);
    completion(displayImage.get());
}

void CompositorIntegrationImpl::paintCompositedResultsToCanvas(WebCore::ImageBuffer&, uint32_t)
{
    ASSERT_NOT_REACHED();
}

} // namespace WebCore::WebGPU

#endif // HAVE(WEBGPU_IMPLEMENTATION)
