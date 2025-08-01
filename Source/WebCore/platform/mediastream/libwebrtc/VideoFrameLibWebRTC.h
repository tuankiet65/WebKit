/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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

#pragma once

#if PLATFORM(COCOA) && USE(LIBWEBRTC)

#include "LibWebRTCUtils.h"
#include "VideoFrame.h"

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN

#include <webrtc/api/video/video_frame.h>
#include <webrtc/webkit_sdk/WebKit/WebKitUtilities.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

typedef struct CF_BRIDGED_TYPE(id) __CVBuffer *CVPixelBufferRef;

namespace WebCore {

class VideoFrameLibWebRTC final : public VideoFrame {
public:
    using ConversionCallback = std::function<RetainPtr<CVPixelBufferRef>(webrtc::VideoFrameBuffer&)>;
    static RefPtr<VideoFrameLibWebRTC> create(MediaTime, bool isMirrored, Rotation, std::optional<PlatformVideoColorSpace>&&, Ref<webrtc::VideoFrameBuffer>&&, ConversionCallback&&);

    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer() const { return webrtc::scoped_refptr { m_buffer.ptr() }; }

    static std::optional<PlatformVideoColorSpace> colorSpaceFromFrame(const webrtc::VideoFrame&);

private:
    VideoFrameLibWebRTC(MediaTime, bool isMirrored, Rotation, PlatformVideoColorSpace&&, Ref<webrtc::VideoFrameBuffer>&&, ConversionCallback&&);

    // VideoFrame
    IntSize presentationSize() const final { return m_size; }
    uint32_t pixelFormat() const final { return m_videoPixelFormat; }
    CVPixelBufferRef pixelBuffer() const final;

    Ref<VideoFrame> clone() final;

    const Ref<webrtc::VideoFrameBuffer> m_buffer;
    IntSize m_size;
    uint32_t m_videoPixelFormat { 0 };

    mutable ConversionCallback m_conversionCallback;
    mutable RetainPtr<CVPixelBufferRef> m_pixelBuffer WTF_GUARDED_BY_LOCK(m_pixelBufferLock);
    mutable Lock m_pixelBufferLock;
};

}

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::VideoFrameLibWebRTC)
    static bool isType(const WebCore::VideoFrame& videoFrame) { return videoFrame.isLibWebRTC(); }
SPECIALIZE_TYPE_TRAITS_END()

#endif // PLATFORM(COCOA) && USE(LIBWEBRTC)
