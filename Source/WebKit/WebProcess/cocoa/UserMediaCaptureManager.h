/*
 * Copyright (C) 2017-2025 Apple Inc. All rights reserved.
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

#if PLATFORM(COCOA) && ENABLE(MEDIA_STREAM)

#include "RemoteCaptureSampleManager.h"
#include "WebProcessSupplement.h"
#include <WebCore/DisplayCaptureManager.h>
#include <WebCore/RealtimeMediaSource.h>
#include <WebCore/RealtimeMediaSourceCenter.h>
#include <WebCore/RealtimeMediaSourceFactory.h>
#include <WebCore/RealtimeMediaSourceIdentifier.h>
#include <WebCore/SharedMemory.h>
#include <wtf/CheckedPtr.h>
#include <wtf/HashMap.h>
#include <wtf/TZoneMalloc.h>

namespace IPC {
class Decoder;
class Encoder;

template<> struct ArgumentCoder<WebCore::RealtimeMediaSourceCenter::ValidDevices> {
    static void encode(Encoder&, const WebCore::RealtimeMediaSourceCenter::ValidDevices&);
    static std::optional<WebCore::RealtimeMediaSourceCenter::ValidDevices> decode(Decoder&);
};
}

namespace WebCore {
class CAAudioStreamDescription;
}

namespace WebKit {

class RemoteRealtimeAudioSource;
class RemoteRealtimeVideoSource;
class WebProcess;

class UserMediaCaptureManager final : public WebProcessSupplement, public IPC::MessageReceiver, public CanMakeCheckedPtr<UserMediaCaptureManager> {
    WTF_MAKE_TZONE_ALLOCATED(UserMediaCaptureManager);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(UserMediaCaptureManager);
public:
    explicit UserMediaCaptureManager(WebProcess&);
    ~UserMediaCaptureManager();

    void ref() const final;
    void deref() const final;

    static ASCIILiteral supplementName();

    void didReceiveMessageFromGPUProcess(IPC::Connection& connection, IPC::Decoder& decoder) { didReceiveMessage(connection, decoder); }
    void setupCaptureProcesses(bool shouldCaptureAudioInUIProcess, bool shouldCaptureAudioInGPUProcess, bool shouldCaptureVideoInUIProcess, bool shouldCaptureVideoInGPUProcess, bool shouldCaptureDisplayInUIProcess, bool shouldCaptureDisplayInGPUProcess, bool shouldUseGPUProcessRemoteFrames);

    void addSource(Ref<RemoteRealtimeAudioSource>&&);
    void addSource(Ref<RemoteRealtimeVideoSource>&&);
    void removeSource(WebCore::RealtimeMediaSourceIdentifier);

    Ref<RemoteCaptureSampleManager> protectedRemoteCaptureSampleManager() { return m_remoteCaptureSampleManager; }
    RemoteCaptureSampleManager& remoteCaptureSampleManager() { return m_remoteCaptureSampleManager; }
    bool shouldUseGPUProcessRemoteFrames() const { return m_shouldUseGPUProcessRemoteFrames; }

private:
    // WebCore::RealtimeMediaSource factories
    class AudioFactory : public WebCore::AudioCaptureFactory {
    public:
        explicit AudioFactory(UserMediaCaptureManager& manager) : m_manager(manager) { }
        void setShouldCaptureInGPUProcess(bool);

    private:
        WebCore::CaptureSourceOrError createAudioCaptureSource(const WebCore::CaptureDevice&, WebCore::MediaDeviceHashSalts&&, const WebCore::MediaConstraints*, std::optional<WebCore::PageIdentifier>) final;
        WebCore::CaptureDeviceManager& audioCaptureDeviceManager() final { return m_manager->m_noOpCaptureDeviceManager; }
        const Vector<WebCore::CaptureDevice>& speakerDevices() const final { return m_speakerDevices; }

        const CheckedRef<UserMediaCaptureManager> m_manager;
        bool m_shouldCaptureInGPUProcess { false };
        Vector<WebCore::CaptureDevice> m_speakerDevices;
    };
    class VideoFactory : public WebCore::VideoCaptureFactory {
    public:
        explicit VideoFactory(UserMediaCaptureManager& manager) : m_manager(manager) { }
        void setShouldCaptureInGPUProcess(bool);

    private:
        WebCore::CaptureSourceOrError createVideoCaptureSource(const WebCore::CaptureDevice&, WebCore::MediaDeviceHashSalts&&, const WebCore::MediaConstraints*, std::optional<WebCore::PageIdentifier>) final;
        WebCore::CaptureDeviceManager& videoCaptureDeviceManager() final { return m_manager->m_noOpCaptureDeviceManager; }

        const CheckedRef<UserMediaCaptureManager> m_manager;
        bool m_shouldCaptureInGPUProcess { false };
    };
    class DisplayFactory : public WebCore::DisplayCaptureFactory {
    public:
        explicit DisplayFactory(UserMediaCaptureManager& manager) : m_manager(manager) { }
        void setShouldCaptureInGPUProcess(bool);

    private:
        WebCore::CaptureSourceOrError createDisplayCaptureSource(const WebCore::CaptureDevice&, WebCore::MediaDeviceHashSalts&&, const WebCore::MediaConstraints*, std::optional<WebCore::PageIdentifier>) final;
        WebCore::DisplayCaptureManager& displayCaptureDeviceManager() final { return m_manager->m_noOpCaptureDeviceManager; }

        const CheckedRef<UserMediaCaptureManager> m_manager;
        bool m_shouldCaptureInGPUProcess { false };
    };

    class NoOpCaptureDeviceManager : public WebCore::DisplayCaptureManager {
    public:
        NoOpCaptureDeviceManager() = default;

    private:
        const Vector<WebCore::CaptureDevice>& captureDevices() final
        {
            ASSERT_NOT_REACHED();
            return m_emptyDevices;
        }
        Vector<WebCore::CaptureDevice> m_emptyDevices;
    };

    // IPC::MessageReceiver
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;

    // Messages::UserMediaCaptureManager
    void sourceStopped(WebCore::RealtimeMediaSourceIdentifier, bool didFail);
    void sourceMutedChanged(WebCore::RealtimeMediaSourceIdentifier, bool muted, bool interrupted);

    void sourceSettingsChanged(WebCore::RealtimeMediaSourceIdentifier, WebCore::RealtimeMediaSourceSettings&&);
    void sourceConfigurationChanged(WebCore::RealtimeMediaSourceIdentifier, String&&, WebCore::RealtimeMediaSourceSettings&&, WebCore::RealtimeMediaSourceCapabilities&&);

    void applyConstraintsSucceeded(WebCore::RealtimeMediaSourceIdentifier, WebCore::RealtimeMediaSourceSettings&&);
    void applyConstraintsFailed(WebCore::RealtimeMediaSourceIdentifier, WebCore::MediaConstraintType, String&&);

    const CheckedRef<WebProcess> m_process;
    using Source = Variant<std::nullptr_t, Ref<RemoteRealtimeAudioSource>, Ref<RemoteRealtimeVideoSource>>;
    HashMap<WebCore::RealtimeMediaSourceIdentifier, Source> m_sources;
    NoOpCaptureDeviceManager m_noOpCaptureDeviceManager;
    AudioFactory m_audioFactory;
    VideoFactory m_videoFactory;
    DisplayFactory m_displayFactory;
    RemoteCaptureSampleManager m_remoteCaptureSampleManager;
    bool m_shouldUseGPUProcessRemoteFrames { false };
};

} // namespace WebKit

#endif
