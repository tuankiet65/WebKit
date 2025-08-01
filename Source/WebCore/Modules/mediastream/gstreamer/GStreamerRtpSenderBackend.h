/*
 *  Copyright (C) 2019-2022 Igalia S.L. All rights reserved.
 *  Copyright (C) 2022 Metrological Group B.V.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#if ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)

#include "GUniquePtrGStreamer.h"
#include "RTCRtpSenderBackend.h"
#include "RealtimeOutgoingAudioSourceGStreamer.h"
#include "RealtimeOutgoingVideoSourceGStreamer.h"
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakPtr.h>

namespace WebCore {
class GStreamerRtpSenderBackend;
}

namespace WTF {
template<typename T> struct IsDeprecatedWeakRefSmartPointerException;
template<> struct IsDeprecatedWeakRefSmartPointerException<WebCore::GStreamerRtpSenderBackend> : std::true_type { };
}

namespace WebCore {

class GStreamerPeerConnectionBackend;

class GStreamerRtpSenderBackend final : public RTCRtpSenderBackend {
    WTF_MAKE_TZONE_ALLOCATED(GStreamerRtpSenderBackend);
public:
    GStreamerRtpSenderBackend(WeakPtr<GStreamerPeerConnectionBackend>&&, GRefPtr<GstWebRTCRTPSender>&&);
    using Source = Variant<std::nullptr_t, Ref<RealtimeOutgoingAudioSourceGStreamer>, Ref<RealtimeOutgoingVideoSourceGStreamer>>;
    GStreamerRtpSenderBackend(WeakPtr<GStreamerPeerConnectionBackend>&&, GRefPtr<GstWebRTCRTPSender>&&, Source&&, GUniquePtr<GstStructure>&& initData);

    void setRTCSender(GRefPtr<GstWebRTCRTPSender>&& rtcSender) { m_rtcSender = WTFMove(rtcSender); }
    GstWebRTCRTPSender* rtcSender() { return m_rtcSender.get(); }

    RealtimeOutgoingAudioSourceGStreamer* audioSource()
    {
        return WTF::switchOn(m_source,
            [] (Ref<RealtimeOutgoingAudioSourceGStreamer>& source) { return source.ptr(); },
            [] (const auto&) -> RealtimeOutgoingAudioSourceGStreamer* { return nullptr; }
        );
    }

    ThreadSafeWeakPtr<RealtimeOutgoingAudioSourceGStreamer> audioSourceWeak()
    {
        return WTF::switchOn(m_source,
            [](Ref<RealtimeOutgoingAudioSourceGStreamer>& source) -> ThreadSafeWeakPtr<RealtimeOutgoingAudioSourceGStreamer> { return source.get(); },
            [](const auto&) -> ThreadSafeWeakPtr<RealtimeOutgoingAudioSourceGStreamer> { return nullptr; });
    }

    RealtimeOutgoingVideoSourceGStreamer* videoSource()
    {
        return WTF::switchOn(m_source,
            [] (Ref<RealtimeOutgoingVideoSourceGStreamer>& source) { return source.ptr(); },
            [] (const auto&) -> RealtimeOutgoingVideoSourceGStreamer* { return nullptr; }
        );
    }

    bool hasSource() const
    {
        return WTF::switchOn(m_source,
            [] (const std::nullptr_t&) { return false; },
            [] (const auto&) { return true; }
        );
    }

    void setSource(Source&&);
    void takeSource(GStreamerRtpSenderBackend&);

    void stopSource();
    void tearDown();

    void dispatchBitrateRequest(uint32_t bitrate);

private:
    void clearSource();
    bool replaceTrack(RTCRtpSender&, MediaStreamTrack*) final;
    RTCRtpSendParameters getParameters() const final;
    void setParameters(const RTCRtpSendParameters&, DOMPromiseDeferred<void>&&) final;
    std::unique_ptr<RTCDTMFSenderBackend> createDTMFBackend() final;
    Ref<RTCRtpTransformBackend> rtcRtpTransformBackend() final;
    void setMediaStreamIds(const FixedVector<String>&) final;
    std::unique_ptr<RTCDtlsTransportBackend> dtlsTransportBackend() final;

    void startSource();

    WeakPtr<GStreamerPeerConnectionBackend> m_peerConnectionBackend;
    GRefPtr<GstWebRTCRTPSender> m_rtcSender;
    Source m_source;
    GUniquePtr<GstStructure> m_initData;
    mutable GUniquePtr<GstStructure> m_currentParameters;
};

} // namespace WebCore

#endif // ENABLE(WEB_RTC) && USE(GSTREAMER_WEBRTC)
