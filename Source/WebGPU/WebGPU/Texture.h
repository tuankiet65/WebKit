/*
 * Copyright (c) 2021-2023 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#import <Metal/Metal.h>
#import <wtf/FastMalloc.h>
#import <wtf/HashMap.h>
#import <wtf/HashSet.h>
#import <wtf/Ref.h>
#import <wtf/RefCountedAndCanMakeWeakPtr.h>
#import <wtf/RetainReleaseSwift.h>
#import <wtf/TZoneMalloc.h>
#import <wtf/Vector.h>
#import <wtf/WeakHashSet.h>
#import <wtf/WeakPtr.h>

struct WGPUTextureImpl {
};

namespace WebGPU {

class CommandEncoder;
class Device;
class TextureView;

// https://gpuweb.github.io/gpuweb/#gputexture
class Texture : public RefCountedAndCanMakeWeakPtr<Texture>, public WGPUTextureImpl {
    WTF_MAKE_TZONE_ALLOCATED(Texture);
public:
    static Ref<Texture> create(id<MTLTexture> texture, const WGPUTextureDescriptor& descriptor, Vector<WGPUTextureFormat>&& viewFormats, Device& device)
    {
        return adoptRef(*new Texture(texture, descriptor, WTFMove(viewFormats), device));
    }
    static Ref<Texture> createInvalid(Device& device)
    {
        return adoptRef(*new Texture(device));
    }

    ~Texture();

    Ref<TextureView> createView(const WGPUTextureViewDescriptor&);
    void destroy();
    void setLabel(String&&);

    bool isValid() const;

    static uint32_t texelBlockWidth(WGPUTextureFormat); // Texels
    static uint32_t texelBlockHeight(WGPUTextureFormat); // Texels
    static NSUInteger bytesPerRow(WGPUTextureFormat, uint32_t textureWidth, uint32_t sampleCount);
    static WGPUExtent3D physicalTextureExtent(WGPUTextureDimension, WGPUTextureFormat, WGPUExtent3D logicalExtent);

    // For depth-stencil textures, the input value to texelBlockSize()
    // needs to be the output of aspectSpecificFormat().
    static Checked<uint32_t> texelBlockSize(WGPUTextureFormat); // Bytes
    static bool containsDepthAspect(WGPUTextureFormat);
    static bool containsStencilAspect(WGPUTextureFormat);
    static bool isDepthOrStencilFormat(WGPUTextureFormat);
    static WGPUTextureFormat aspectSpecificFormat(WGPUTextureFormat, WGPUTextureAspect);
    static NSString* errorValidatingImageCopyTexture(const WGPUImageCopyTexture&, const WGPUExtent3D&);
    static NSString* errorValidatingTextureCopyRange(const WGPUImageCopyTexture&, const WGPUExtent3D&);
    static bool refersToSingleAspect(WGPUTextureFormat, WGPUTextureAspect);
    static bool isValidDepthStencilCopySource(WGPUTextureFormat, WGPUTextureAspect);
    static bool isValidDepthStencilCopyDestination(WGPUTextureFormat, WGPUTextureAspect);
    static NSString* errorValidatingLinearTextureData(const WGPUTextureDataLayout&, uint64_t, WGPUTextureFormat, WGPUExtent3D);
    static MTLTextureUsage usage(WGPUTextureUsageFlags, WGPUTextureFormat);
    static MTLPixelFormat pixelFormat(WGPUTextureFormat);
    static WGPUTextureFormat textureFormat(MTLPixelFormat);
    static std::optional<MTLPixelFormat> depthOnlyAspectMetalFormat(WGPUTextureFormat);
    static std::optional<MTLPixelFormat> stencilOnlyAspectMetalFormat(WGPUTextureFormat);
    static WGPUTextureFormat removeSRGBSuffix(WGPUTextureFormat);
    static std::optional<WGPUTextureFormat> resolveTextureFormat(WGPUTextureFormat, WGPUTextureAspect);
    static bool isCompressedFormat(WGPUTextureFormat);
    enum class CompressFormat {
        ASTC, // NOLINT
        BC, // NOLINT
        ETC // NOLINT
    };
    static std::optional<CompressFormat> compressedFormatType(WGPUTextureFormat);
    static bool isRenderableFormat(WGPUTextureFormat, const Device&);
    static bool isColorRenderableFormat(WGPUTextureFormat, const Device&);
    static bool isDepthStencilRenderableFormat(WGPUTextureFormat, const Device&);
    static uint32_t renderTargetPixelByteCost(WGPUTextureFormat);
    static uint32_t renderTargetPixelByteAlignment(WGPUTextureFormat);

    WGPUExtent3D logicalMiplevelSpecificTextureExtent(uint32_t mipLevel);
    WGPUExtent3D physicalMiplevelSpecificTextureExtent(uint32_t mipLevel);

    id<MTLTexture> texture() const { return m_texture; }

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    uint32_t depthOrArrayLayers() const { return m_depthOrArrayLayers; }
    uint32_t mipLevelCount() const { return m_mipLevelCount; }
    uint32_t sampleCount() const { return m_sampleCount; }
    WGPUTextureDimension dimension() const { return m_dimension; }
    WGPUTextureFormat format() const { return m_format; }
    WGPUTextureUsageFlags usage() const { return m_usage; }

    Device& device() const { return m_device; }

    bool previouslyCleared(uint32_t mipLevel, uint32_t slice) const;
    void setPreviouslyCleared(uint32_t mipLevel, uint32_t slice, bool = true);
    bool isDestroyed() const { return m_destroyed; }

    static bool hasStorageBindingCapability(WGPUTextureFormat, const Device&, WGPUStorageTextureAccess = WGPUStorageTextureAccess_Undefined);
    static bool supportsMultisampling(WGPUTextureFormat, const Device&);
    static bool supportsResolve(WGPUTextureFormat, const Device&);
    static bool supportsBlending(WGPUTextureFormat, const Device&);
    void recreateIfNeeded();
    void makeCanvasBacking();
    void setCommandEncoder(CommandEncoder&) const;
    static ASCIILiteral formatToString(WGPUTextureFormat);
    bool isCanvasBacking() const { return m_canvasBacking; }

    bool waitForCommandBufferCompletion();
    void updateCompletionEvent(const std::pair<id<MTLSharedEvent>, uint64_t>&);
    id<MTLSharedEvent> sharedEvent() const;
    uint64_t sharedEventSignalValue() const;

private:
    Texture(id<MTLTexture>, const WGPUTextureDescriptor&, Vector<WGPUTextureFormat>&& viewFormats, Device&);
    Texture(Device&);

    std::optional<WGPUTextureViewDescriptor> resolveTextureViewDescriptorDefaults(const WGPUTextureViewDescriptor&) const;
    uint32_t arrayLayerCount() const;
    NSString* errorValidatingTextureViewCreation(const WGPUTextureViewDescriptor&) const;

    id<MTLTexture> m_texture { nil };

    const uint32_t m_width { 0 };
    const uint32_t m_height { 0 };
    const uint32_t m_depthOrArrayLayers { 0 };
    const uint32_t m_mipLevelCount { 0 };
    const uint32_t m_sampleCount { 0 };
    const WGPUTextureDimension m_dimension { WGPUTextureDimension_2D };
    const WGPUTextureFormat m_format { WGPUTextureFormat_Undefined };
    const WGPUTextureUsageFlags m_usage { WGPUTextureUsage_None };

    const Vector<WGPUTextureFormat> m_viewFormats;

    const Ref<Device> m_device;
    using ClearedToZeroInnerContainer = HashSet<uint32_t, DefaultHash<uint32_t>, WTF::UnsignedWithZeroKeyHashTraits<uint32_t>>;
    using ClearedToZeroContainer = HashMap<uint32_t, ClearedToZeroInnerContainer, DefaultHash<uint32_t>, WTF::UnsignedWithZeroKeyHashTraits<uint32_t>>;
    ClearedToZeroContainer m_clearedToZero;
    Vector<WeakPtr<TextureView>> m_textureViews;
    bool m_destroyed { false };
    bool m_canvasBacking { false };
    mutable Vector<uint64_t> m_commandEncoders;
    id<MTLSharedEvent> m_sharedEvent { nil };
    uint64_t m_sharedEventSignalValue { 0 };
// FIXME: remove @safe once rdar://151039766 lands
} __attribute__((swift_attr("@safe"))) SWIFT_SHARED_REFERENCE(refTexture, derefTexture);

} // namespace WebGPU

inline void refTexture(WebGPU::Texture* obj)
{
    WTF::ref(obj);
}

inline void derefTexture(WebGPU::Texture* obj)
{
    WTF::deref(obj);
}
