/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if ENABLE(WEB_RTC) && USE(LIBWEBRTC)

#include "LibWebRTCMacros.h"
#include "LibWebRTCRefWrappers.h"
#include "RTCRtpTransformBackend.h"
#include <webrtc/api/scoped_refptr.h>
#include <wtf/Lock.h>
#include <wtf/StdUnorderedMap.h>

namespace WebCore {

class LibWebRTCRtpTransformBackend : public RTCRtpTransformBackend, public webrtc::FrameTransformerInterface {
protected:
    LibWebRTCRtpTransformBackend(MediaType, Side);
    void setInputCallback(Callback&&);

protected:
    MediaType mediaType() const final { return m_mediaType; }

private:
    void sendFrameToOutput(std::unique_ptr<webrtc::TransformableFrameInterface>&&);
    void addOutputCallback(Ref<webrtc::TransformedFrameCallback>&&, uint32_t ssrc);
    void removeOutputCallback(uint32_t ssrc);

    // RTCRtpTransformBackend
    void processTransformedFrame(RTCRtpTransformableFrame&) final;
    void clearTransformableFrameCallback() final;
    Side side() const final { return m_side; }

    // webrtc::FrameTransformerInterface
    void Transform(std::unique_ptr<webrtc::TransformableFrameInterface>) final;
    void RegisterTransformedFrameCallback(webrtc::scoped_refptr<webrtc::TransformedFrameCallback>) final;
    void RegisterTransformedFrameSinkCallback(webrtc::scoped_refptr<webrtc::TransformedFrameCallback>, uint32_t ssrc) final;
    void UnregisterTransformedFrameCallback() final;
    void UnregisterTransformedFrameSinkCallback(uint32_t ssrc) final;
    void AddRef() const final { ref(); }
    webrtc::RefCountReleaseStatus Release() const final;

    MediaType m_mediaType;
    Side m_side;

    Lock m_inputCallbackLock;
    Callback m_inputCallback WTF_GUARDED_BY_LOCK(m_inputCallbackLock);

    Lock m_outputCallbacksLock;
    StdUnorderedMap<uint32_t, Ref<webrtc::TransformedFrameCallback>> m_outputCallbacks WTF_GUARDED_BY_LOCK(m_outputCallbacksLock);
};

inline LibWebRTCRtpTransformBackend::LibWebRTCRtpTransformBackend(MediaType mediaType, Side side)
    : m_mediaType(mediaType)
    , m_side(side)
{
}

inline webrtc::RefCountReleaseStatus LibWebRTCRtpTransformBackend::Release() const
{
    deref();
    return webrtc::RefCountReleaseStatus::kOtherRefsRemained;
}

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(LIBWEBRTC)
