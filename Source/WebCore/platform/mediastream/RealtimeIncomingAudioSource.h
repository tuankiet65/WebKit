/*
 * Copyright (C) 2017-2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of Ericsson nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#if USE(LIBWEBRTC)

#include "LibWebRTCMacros.h"
#include "RealtimeMediaSource.h"

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_BEGIN

#include <webrtc/api/media_stream_interface.h>

WTF_IGNORE_WARNINGS_IN_THIRD_PARTY_CODE_END

#include <wtf/RetainPtr.h>

namespace WebCore {

class LibWebRTCAudioModule;

class RealtimeIncomingAudioSource
    : public RealtimeMediaSource
    , private webrtc::AudioTrackSinkInterface
    , private webrtc::ObserverInterface
    , public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<RealtimeIncomingAudioSource, WTF::DestructionThread::MainRunLoop>
{
public:
    static Ref<RealtimeIncomingAudioSource> create(Ref<webrtc::AudioTrackInterface>&&, String&&);

    void setAudioModule(RefPtr<LibWebRTCAudioModule>&&);
    LibWebRTCAudioModule* audioModule() { return m_audioModule.get(); }

    WTF_ABSTRACT_THREAD_SAFE_REF_COUNTED_AND_CAN_MAKE_WEAK_PTR_IMPL;
    ~RealtimeIncomingAudioSource();
protected:
    RealtimeIncomingAudioSource(Ref<webrtc::AudioTrackInterface>&&, String&&);

#if !RELEASE_LOG_DISABLED
    ASCIILiteral logClassName() const final { return "RealtimeIncomingAudioSource"_s; }
#endif

    // RealtimeMediaSource API
    void startProducingData() override;
    void stopProducingData()  override;

private:
    // webrtc::AudioTrackSinkInterface API
    void OnData(const void* /* audioData */, int /* bitsPerSample */, int /* sampleRate */, size_t /* numberOfChannels */, size_t /* numberOfFrames */) override { };

    // webrtc::ObserverInterface API
    void OnChanged() final;

    const RealtimeMediaSourceCapabilities& capabilities() final;
    const RealtimeMediaSourceSettings& settings() final;

    bool isIncomingAudioSource() const final { return true; }

    RealtimeMediaSourceSettings m_currentSettings;
    const Ref<webrtc::AudioTrackInterface> m_audioTrack;
    RefPtr<LibWebRTCAudioModule> m_audioModule;

#if !RELEASE_LOG_DISABLED
    mutable RefPtr<const Logger> m_logger;
    uint64_t m_logIdentifier { 0 };
#endif
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::RealtimeIncomingAudioSource)
    static bool isType(const WebCore::RealtimeMediaSource& source) { return source.isIncomingAudioSource(); }
SPECIALIZE_TYPE_TRAITS_END()

#endif // USE(LIBWEBRTC)
