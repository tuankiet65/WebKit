/*
 * Copyright (C) 2018-2023 Apple Inc. All rights reserved.
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
#include "AuthenticatorManager.h"

#if ENABLE(WEB_AUTHN)

#include "APIUIClient.h"
#include "APIWebAuthenticationPanel.h"
#include "APIWebAuthenticationPanelClient.h"
#include "AuthenticatorPresenterCoordinator.h"
#include "LocalService.h"
#include "NfcService.h"
#include "WebFrameProxy.h"
#include "WebPageProxy.h"
#include "WebPreferencesKeys.h"
#include "WebProcessProxy.h"
#include <WebCore/AuthenticatorAssertionResponse.h>
#include <WebCore/AuthenticatorAttachment.h>
#include <WebCore/AuthenticatorSelectionCriteria.h>
#include <WebCore/AuthenticatorTransport.h>
#include <WebCore/EventRegion.h>
#include <WebCore/ExceptionCode.h>
#include <WebCore/MediationRequirement.h>
#include <WebCore/PublicKeyCredentialCreationOptions.h>
#include <WebCore/WebAuthenticationConstants.h>
#include <wtf/MonotonicTime.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {
using namespace WebCore;

namespace {

// Suggested by WebAuthN spec as of 7 August 2018.
const unsigned maxTimeOutValue = 120000;

// FIXME(188625): Support BLE authenticators.
static AuthenticatorManager::TransportSet collectTransports(const std::optional<AuthenticatorSelectionCriteria>& authenticatorSelection)
{
    AuthenticatorManager::TransportSet result;
    if (!authenticatorSelection || !authenticatorSelection->authenticatorAttachment) {
        auto addResult = result.add(AuthenticatorTransport::Internal);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::Usb);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::Nfc);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::Ble);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::SmartCard);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        return result;
    }

    if (authenticatorSelection->authenticatorAttachment == AuthenticatorAttachment::Platform) {
        auto addResult = result.add(AuthenticatorTransport::Internal);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        return result;
    }
    if (authenticatorSelection->authenticatorAttachment == AuthenticatorAttachment::CrossPlatform) {
        auto addResult = result.add(AuthenticatorTransport::Usb);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::Nfc);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::Ble);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::SmartCard);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        return result;
    }

    ASSERT_NOT_REACHED();
    return result;
}

// FIXME(188625): Support BLE authenticators.
// The goal is to find a union of different transports from allowCredentials.
// If it is not specified or any of its credentials doesn't specify its own. We should discover all.
// This is a variant of Step. 18.*.4 from https://www.w3.org/TR/webauthn/#discover-from-external-source
// as of 7 August 2018.
static AuthenticatorManager::TransportSet collectTransports(const Vector<PublicKeyCredentialDescriptor>& allowCredentials, const std::optional<AuthenticatorAttachment>& authenticatorAttachment)
{
    AuthenticatorManager::TransportSet result;
    if (allowCredentials.isEmpty()) {
        auto addResult = result.add(AuthenticatorTransport::Internal);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::Usb);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::Nfc);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::Ble);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
        addResult = result.add(AuthenticatorTransport::SmartCard);
        ASSERT_UNUSED(addResult, addResult.isNewEntry);
    }

    for (auto& allowCredential : allowCredentials) {
        if (allowCredential.transports.isEmpty()) {
            result.add(AuthenticatorTransport::Internal);
            result.add(AuthenticatorTransport::Usb);
            result.add(AuthenticatorTransport::Nfc);
            result.add(AuthenticatorTransport::Ble);
            result.add(AuthenticatorTransport::SmartCard);

            break;
        }

        for (const auto& transport : allowCredential.transports) {
            if (transport == AuthenticatorTransport::Ble)
                continue;

            result.add(transport);

            if (result.size() >= AuthenticatorManager::maxTransportNumber)
                break;
        }
    }

    if (authenticatorAttachment) {
        if (authenticatorAttachment == AuthenticatorAttachment::Platform) {
            result.remove(AuthenticatorTransport::Usb);
            result.remove(AuthenticatorTransport::Nfc);
            result.remove(AuthenticatorTransport::Ble);
            result.remove(AuthenticatorTransport::SmartCard);
        }

        if (authenticatorAttachment == AuthenticatorAttachment::CrossPlatform)
            result.remove(AuthenticatorTransport::Internal);
    }

    ASSERT(result.size() <= AuthenticatorManager::maxTransportNumber);
    return result;
}

static String getRpId(const Variant<PublicKeyCredentialCreationOptions, PublicKeyCredentialRequestOptions>& options)
{
    if (std::holds_alternative<PublicKeyCredentialCreationOptions>(options)) {
        auto& creationOptions = std::get<PublicKeyCredentialCreationOptions>(options);
        ASSERT(creationOptions.rp.id);
        return creationOptions.rp.id;
    }
    return std::get<PublicKeyCredentialRequestOptions>(options).rpId;
}

static String getUserName(const Variant<PublicKeyCredentialCreationOptions, PublicKeyCredentialRequestOptions>& options)
{
    if (std::holds_alternative<PublicKeyCredentialCreationOptions>(options))
        return std::get<PublicKeyCredentialCreationOptions>(options).user.name;
    return emptyString();
}

} // namespace

const size_t AuthenticatorManager::maxTransportNumber = 5;

WTF_MAKE_TZONE_ALLOCATED_IMPL(AuthenticatorManager);

Ref<AuthenticatorManager> AuthenticatorManager::create()
{
    return adoptRef(*new AuthenticatorManager);
}

AuthenticatorManager::AuthenticatorManager()
    : m_requestTimeOutTimer(RunLoop::mainSingleton(), "AuthenticatorManager::RequestTimeOutTimer"_s, this, &AuthenticatorManager::timeOutTimerFired)
{
}

void AuthenticatorManager::handleRequest(WebAuthenticationRequestData&& data, Callback&& callback)
{
    if (m_pendingCompletionHandler) {
        invokePendingCompletionHandler(ExceptionData { ExceptionCode::NotAllowedError, "This request has been cancelled by a new request."_s });
        m_requestTimeOutTimer.stop();
    }
    clearState();

    // 1. Save request for async operations.
    m_pendingRequestData = WTFMove(data);
    m_pendingCompletionHandler = WTFMove(callback);

    // 2. Ask clients to show appropriate UI if any and then start the request.
    initTimeOutTimer();

    // FIXME<rdar://problem/70822834>: The m_mode is used to determine whether or not we are in the UIProcess.
    // If so, continue to the old route. Otherwise, use the modern WebAuthn process way.
    if (m_mode == Mode::Compatible) {
        runPanel();
        return;
    }
    runPresenter();
}

void AuthenticatorManager::cancelRequest(const PageIdentifier& pageID, const std::optional<FrameIdentifier>& frameID)
{
    if (!m_pendingCompletionHandler)
        return;
    if (auto pendingFrameID = m_pendingRequestData.globalFrameID) {
        if (pendingFrameID->pageID != pageID)
            return;
        if (frameID && frameID != pendingFrameID->frameID)
            return;
    }
    cancelRequest();
}

// The following implements part of Step 20. of https://www.w3.org/TR/webauthn/#createCredential
// and part of Step 18. of https://www.w3.org/TR/webauthn/#getAssertion as of 4 March 2019:
// "If the user exercises a user agent user-interface option to cancel the process,".
void AuthenticatorManager::cancelRequest(const API::WebAuthenticationPanel& panel)
{
    RELEASE_ASSERT(RunLoop::isMain());
    if (!m_pendingCompletionHandler || m_pendingRequestData.panel.get() != &panel)
        return;
    cancelRequest();
}

void AuthenticatorManager::cancel()
{
    RELEASE_ASSERT(RunLoop::isMain());
    if (!m_pendingCompletionHandler)
        return;
    cancelRequest();
}

void AuthenticatorManager::enableNativeSupport()
{
    m_mode = Mode::Native;
}

void AuthenticatorManager::clearStateAsync()
{
    RunLoop::mainSingleton().dispatch([weakThis = WeakPtr { *this }] {
        if (!weakThis)
            return;
        weakThis->clearState();
    });
}

void AuthenticatorManager::clearState()
{
    if (m_pendingCompletionHandler)
        return;
    m_authenticators.clear();
    m_services.clear();
    m_pendingRequestData = { };
    m_presenter = nullptr;
}

void AuthenticatorManager::authenticatorAdded(Ref<Authenticator>&& authenticator)
{
    ASSERT(RunLoop::isMain());
    authenticator->setObserver(*this);
    authenticator->handleRequest(m_pendingRequestData);
    auto addResult = m_authenticators.add(WTFMove(authenticator));
    ASSERT_UNUSED(addResult, addResult.isNewEntry);
}

void AuthenticatorManager::serviceStatusUpdated(WebAuthenticationStatus status)
{
    // This is for the new UI.
    if (RefPtr presenter = m_presenter) {
        presenter->updatePresenter(status);
        return;
    }

    dispatchPanelClientCall([status] (const API::WebAuthenticationPanel& panel) {
        panel.protectedClient()->updatePanel(status);
    });
}

void AuthenticatorManager::respondReceived(Respond&& respond)
{
    ASSERT(RunLoop::isMain());
    if (!m_requestTimeOutTimer.isActive() && (m_pendingRequestData.mediation != WebCore::MediationRequirement::Conditional || !m_pendingCompletionHandler))
        return;
    ASSERT(m_pendingCompletionHandler);

    auto shouldComplete = std::holds_alternative<Ref<AuthenticatorResponse>>(respond);
    if (!shouldComplete) {
        auto code = std::get<ExceptionData>(respond).code;
        shouldComplete = code == ExceptionCode::InvalidStateError || code == ExceptionCode::NotSupportedError;
    }
    if (shouldComplete) {
        invokePendingCompletionHandler(WTFMove(respond));
        clearStateAsync();
        m_requestTimeOutTimer.stop();
        return;
    }
    respondReceivedInternal(WTFMove(respond));
    restartDiscovery();
}

void AuthenticatorManager::downgrade(Authenticator& id, Ref<Authenticator>&& downgradedAuthenticator)
{
    RunLoop::mainSingleton().dispatch([weakThis = WeakPtr { *this }, id = Ref { id }] {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return;
        auto removed = protectedThis->m_authenticators.remove(id.ptr());
        ASSERT_UNUSED(removed, removed);
    });
    authenticatorAdded(WTFMove(downgradedAuthenticator));
}

void AuthenticatorManager::authenticatorStatusUpdated(WebAuthenticationStatus status)
{
    // Immediately invalidate the cache if the PIN is incorrect. A status update often means
    // an error. We don't really care what kind of error it really is.
    m_pendingRequestData.cachedPin = String();

    // This is for the new UI.
    if (RefPtr presenter = m_presenter) {
        presenter->updatePresenter(status);
        return;
    }

    dispatchPanelClientCall([status] (const API::WebAuthenticationPanel& panel) {
        panel.protectedClient()->updatePanel(status);
    });
}

void AuthenticatorManager::requestPin(uint64_t retries, CompletionHandler<void(const WTF::String&)>&& completionHandler)
{
    // Cache the PIN to improve NFC user experience so that a momentary movement of the NFC key away from the scanner doesn't
    // force the PIN entry to be re-entered.
    // We don't distinguish USB and NFC here becuase there is no harms to have this optimization for USB even though it is useless.
    if (!m_pendingRequestData.cachedPin.isNull()) {
        completionHandler(m_pendingRequestData.cachedPin);
        m_pendingRequestData.cachedPin = String();
        return;
    }

    auto callback = [weakThis = WeakPtr { *this }, completionHandler = WTFMove(completionHandler)] (const WTF::String& pin) mutable {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return;

        protectedThis->m_pendingRequestData.cachedPin = pin;
        completionHandler(pin);
    };

    // This is for the new UI.
    if (RefPtr presenter = m_presenter) {
        presenter->requestPin(retries, WTFMove(callback));
        return;
    }

    dispatchPanelClientCall([retries, callback = WTFMove(callback)] (const API::WebAuthenticationPanel& panel) mutable {
        panel.protectedClient()->requestPin(retries, WTFMove(callback));
    });
}

void AuthenticatorManager::requestNewPin(uint64_t minLength, CompletionHandler<void(const WTF::String&)>&& completionHandler)
{
    auto callback = [weakThis = WeakPtr { *this }, completionHandler = WTFMove(completionHandler)] (const WTF::String& pin) mutable {
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis)
            return;

        protectedThis->m_pendingRequestData.cachedPin = pin;
        completionHandler(pin);
    };

    // This is for the new UI.
    if (RefPtr presenter = m_presenter) {
        presenter->requestNewPin(minLength, WTFMove(callback));
        return;
    }

    dispatchPanelClientCall([minLength, callback = WTFMove(callback)] (const API::WebAuthenticationPanel& panel) mutable {
        panel.protectedClient()->requestNewPin(minLength, WTFMove(callback));
    });
}

void AuthenticatorManager::selectAssertionResponse(Vector<Ref<WebCore::AuthenticatorAssertionResponse>>&& responses, WebAuthenticationSource source, CompletionHandler<void(AuthenticatorAssertionResponse*)>&& completionHandler)
{
    // This is for the new UI.
    if (RefPtr presenter = m_presenter) {
        presenter->selectAssertionResponse(WTFMove(responses), source, WTFMove(completionHandler));
        return;
    }

    dispatchPanelClientCall([responses = WTFMove(responses), source, completionHandler = WTFMove(completionHandler)] (const API::WebAuthenticationPanel& panel) mutable {
        panel.protectedClient()->selectAssertionResponse(WTFMove(responses), source, WTFMove(completionHandler));
    });
}

void AuthenticatorManager::decidePolicyForLocalAuthenticator(CompletionHandler<void(LocalAuthenticatorPolicy)>&& completionHandler)
{
    dispatchPanelClientCall([completionHandler = WTFMove(completionHandler)] (const API::WebAuthenticationPanel& panel) mutable {
        panel.protectedClient()->decidePolicyForLocalAuthenticator(WTFMove(completionHandler));
    });
}

void AuthenticatorManager::requestLAContextForUserVerification(CompletionHandler<void(LAContext *)>&& completionHandler)
{
    if (RefPtr presenter = m_presenter) {
        presenter->requestLAContextForUserVerification(WTFMove(completionHandler));
        return;
    }

    dispatchPanelClientCall([completionHandler = WTFMove(completionHandler)] (const API::WebAuthenticationPanel& panel) mutable {
        panel.protectedClient()->requestLAContextForUserVerification(WTFMove(completionHandler));
    });
}

void AuthenticatorManager::cancelRequest()
{
    invokePendingCompletionHandler(ExceptionData { ExceptionCode::NotAllowedError, "This request has been cancelled by the user."_s });
    RELEASE_LOG_ERROR(WebAuthn, "Request cancelled due to AuthenticatorManager::cancelRequest being called.");
    clearState();
    m_requestTimeOutTimer.stop();
}

Ref<AuthenticatorTransportService> AuthenticatorManager::createService(AuthenticatorTransport transport, AuthenticatorTransportServiceObserver& observer) const
{
    return AuthenticatorTransportService::create(transport, observer);
}

void AuthenticatorManager::filterTransports(TransportSet& transports) const
{
    if (!NfcService::isAvailable())
        transports.remove(AuthenticatorTransport::Nfc);
    if (!LocalService::isAvailable())
        transports.remove(AuthenticatorTransport::Internal);
    transports.remove(AuthenticatorTransport::Ble);
}

void AuthenticatorManager::startDiscovery(const TransportSet& transports)
{
    ASSERT(RunLoop::isMain());
    ASSERT(m_services.isEmpty() && transports.size() <= maxTransportNumber);
    m_services = WTF::map(transports, [this](auto& transport) {
        Ref service = createService(transport, *this);
        service->startDiscovery();
        return service;
    });
}

void AuthenticatorManager::initTimeOutTimer()
{
    if (m_pendingRequestData.mediation == WebCore::MediationRequirement::Conditional)
        return;
    std::optional<unsigned> timeOutInMs;
    WTF::switchOn(m_pendingRequestData.options, [&](const PublicKeyCredentialCreationOptions& options) {
        timeOutInMs = options.timeout;
    }, [&](const PublicKeyCredentialRequestOptions& options) {
        timeOutInMs = options.timeout;
    });

    unsigned timeOutInMsValue = std::min(maxTimeOutValue, timeOutInMs.value_or(maxTimeOutValue));
    m_requestTimeOutTimer.startOneShot(Seconds::fromMilliseconds(timeOutInMsValue));
}

void AuthenticatorManager::timeOutTimerFired()
{
    invokePendingCompletionHandler((ExceptionData { ExceptionCode::NotAllowedError, "Operation timed out."_s }));
    clearState();
}

void AuthenticatorManager::runPanel()
{
    RefPtr page = m_pendingRequestData.page.get();
    if (!page)
        return;
    ASSERT(m_pendingRequestData.globalFrameID && page->webPageIDInMainFrameProcess() == m_pendingRequestData.globalFrameID->pageID);
    RefPtr frame = WebFrameProxy::webFrame(m_pendingRequestData.globalFrameID->frameID);
    if (!frame)
        return;
    if (!m_pendingRequestData.frameInfo)
        return;

    // Get available transports and start discovering authenticators on them.
    auto& options = m_pendingRequestData.options;
    auto transports = getTransports();
    if (transports.isEmpty()) {
        cancel();
        return;
    }

    m_pendingRequestData.panel = API::WebAuthenticationPanel::create(*this, getRpId(options), transports, getClientDataType(options), getUserName(options));
    Ref panel = *m_pendingRequestData.panel;
    page->uiClient().runWebAuthenticationPanel(*page, panel, *frame, FrameInfoData { *m_pendingRequestData.frameInfo }, [transports = WTFMove(transports), weakPanel = WeakPtr { panel.get() }, weakThis = WeakPtr { *this }] (WebAuthenticationPanelResult result) {
        // The panel address is used to determine if the current pending request is still the same.
        RefPtr protectedThis = weakThis.get();
        if (!protectedThis || !weakPanel
            || (result == WebAuthenticationPanelResult::DidNotPresent)
            || (weakPanel.get() != protectedThis->m_pendingRequestData.panel.get()))
            return;
        protectedThis->startDiscovery(transports);
    });
}

void AuthenticatorManager::runPresenter()
{
    // Get available transports and start discovering authenticators on them.
    auto transports = getTransports();
    if (transports.isEmpty()) {
        cancel();
        return;
    }

    startDiscovery(transports);

    // For native API support, we skip the UI part. The native API will handle that.
    if (m_mode == Mode::Native)
        return;

    runPresenterInternal(transports);
}

void AuthenticatorManager::runPresenterInternal(const TransportSet& transports)
{
    auto& options = m_pendingRequestData.options;
    m_presenter = AuthenticatorPresenterCoordinator::create(*this, getRpId(options), transports, getClientDataType(options), getUserName(options));
}

void AuthenticatorManager::invokePendingCompletionHandler(Respond&& respond)
{
    auto result = std::holds_alternative<Ref<AuthenticatorResponse>>(respond) ? WebAuthenticationResult::Succeeded : WebAuthenticationResult::Failed;

    // This is for the new UI.
    if (RefPtr presenter = m_presenter)
        presenter->dimissPresenter(result);
    else {
        dispatchPanelClientCall([result] (const API::WebAuthenticationPanel& panel) {
            panel.protectedClient()->dismissPanel(result);
        });
    }

    m_pendingCompletionHandler(WTFMove(respond));
}

void AuthenticatorManager::restartDiscovery()
{
    for (auto& service : m_services)
        service->restartDiscovery();
}

auto AuthenticatorManager::getTransports() const -> TransportSet
{
    TransportSet transports;
    WTF::switchOn(m_pendingRequestData.options, [&](const PublicKeyCredentialCreationOptions& options) {
        transports = collectTransports(options.authenticatorSelection);
    }, [&](const PublicKeyCredentialRequestOptions& options) {
        transports = collectTransports(options.allowCredentials, options.authenticatorAttachment);
    });
    filterTransports(transports);
    return transports;
}

void AuthenticatorManager::dispatchPanelClientCall(Function<void(const API::WebAuthenticationPanel&)>&& call) const
{
    auto weakPanel = m_pendingRequestData.weakPanel;
    if (!weakPanel)
        weakPanel = m_pendingRequestData.panel;
    if (!weakPanel)
        return;

    // Call delegates in the next run loop to prevent clients' reentrance that would potentially modify the state
    // of the current run loop in unexpected ways.
    RunLoop::mainSingleton().dispatch([weakPanel = WTFMove(weakPanel), call = WTFMove(call)] () {
        if (!weakPanel)
            return;
        call(*weakPanel);
    });
}

} // namespace WebKit

#endif // ENABLE(WEB_AUTHN)
