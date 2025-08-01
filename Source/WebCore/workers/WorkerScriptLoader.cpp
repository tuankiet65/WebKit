/*
 * Copyright (C) 2009-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2009, 2011 Google Inc. All rights reserved.
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

#include "config.h"
#include "WorkerScriptLoader.h"

#include "ContentSecurityPolicy.h"
#include "Document.h"
#include "Exception.h"
#include "FetchIdioms.h"
#include "MIMETypeRegistry.h"
#include "ResourceResponse.h"
#include "ScriptExecutionContext.h"
#include "ServiceWorker.h"
#include "ServiceWorkerContextData.h"
#include "ServiceWorkerGlobalScope.h"
#include "ServiceWorkerProvider.h"
#include "TextResourceDecoder.h"
#include "WorkerFetchResult.h"
#include "WorkerGlobalScope.h"
#include "WorkerSWClientConnection.h"
#include "WorkerScriptLoaderClient.h"
#include "WorkerThreadableLoader.h"
#include <wtf/Ref.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(WorkerScriptLoader);

static Lock workerScriptLoaderControlledCallbackMapLock;
static void accessWorkerScriptLoaderMap(CompletionHandler<void(HashMap<ScriptExecutionContextIdentifier, Ref<WorkerScriptLoader::ServiceWorkerDataManager>>&)>&& callback)
{
    Locker locker { workerScriptLoaderControlledCallbackMapLock };
    static NeverDestroyed<HashMap<ScriptExecutionContextIdentifier, Ref<WorkerScriptLoader::ServiceWorkerDataManager>>> map;
    callback(map.get());
}

WorkerScriptLoader::WorkerScriptLoader()
    : m_script(ScriptBuffer::empty())
{
}

WorkerScriptLoader::~WorkerScriptLoader()
{
    if (m_didAddToWorkerScriptLoaderMap)
        accessWorkerScriptLoaderMap([clientIdentifier = *m_clientIdentifier](auto& map) { map.remove(clientIdentifier); });
}

std::optional<Exception> WorkerScriptLoader::loadSynchronously(ScriptExecutionContext* scriptExecutionContext, const URL& url, Source source, FetchOptions::Mode mode, FetchOptions::Cache cachePolicy, ContentSecurityPolicyEnforcement contentSecurityPolicyEnforcement, const String& initiatorIdentifier)
{
    ASSERT(scriptExecutionContext);
    auto& workerGlobalScope = downcast<WorkerGlobalScope>(*scriptExecutionContext);

    m_url = url;
    m_source = source;
    m_destination = FetchOptions::Destination::Script;
    m_isCOEPEnabled = scriptExecutionContext->settingsValues().crossOriginEmbedderPolicyEnabled;
    m_advancedPrivacyProtections = scriptExecutionContext->advancedPrivacyProtections();

    RefPtr serviceWorkerGlobalScope = dynamicDowncast<ServiceWorkerGlobalScope>(workerGlobalScope);
    if (serviceWorkerGlobalScope) {
        if (auto* scriptResource = serviceWorkerGlobalScope->scriptResource(url)) {
            m_script = scriptResource->script;
            m_responseURL = scriptResource->responseURL;
            m_responseMIMEType = scriptResource->mimeType;
            return std::nullopt;
        }
        auto state = serviceWorkerGlobalScope->serviceWorker().state();
        if (state != ServiceWorkerState::Parsed && state != ServiceWorkerState::Installing)
            return Exception { ExceptionCode::NetworkError, "Importing a script from a service worker that is past installing state"_s };
    }

    std::unique_ptr<ResourceRequest> request(createResourceRequest(initiatorIdentifier));
    if (!request)
        return std::nullopt;

    ASSERT_WITH_SECURITY_IMPLICATION(is<WorkerGlobalScope>(scriptExecutionContext));

    // Only used for importScripts that prescribes NoCors mode.
    ASSERT(mode == FetchOptions::Mode::NoCors);
    request->setRequester(ResourceRequestRequester::ImportScripts);

    ThreadableLoaderOptions options;
    options.credentials = FetchOptions::Credentials::Include;
    options.mode = mode;
    options.cache = cachePolicy;
    options.sendLoadCallbacks = SendCallbackPolicy::SendCallbacks;
    options.contentSecurityPolicyEnforcement = contentSecurityPolicyEnforcement;
    options.destination = m_destination;

    WorkerThreadableLoader::loadResourceSynchronously(workerGlobalScope, WTFMove(*request), *this, options);

    // If the fetching attempt failed, throw a NetworkError exception and abort all these steps.
    if (failed())
        return Exception { ExceptionCode::NetworkError, m_error.sanitizedDescription() };

    if (serviceWorkerGlobalScope) {
        if (!MIMETypeRegistry::isSupportedJavaScriptMIMEType(responseMIMEType()))
            return Exception { ExceptionCode::NetworkError, "mime type is not a supported JavaScript mime type"_s };

        serviceWorkerGlobalScope->setScriptResource(url, ServiceWorkerContextData::ImportedScript { script(), m_responseURL, m_responseMIMEType });
    }
    return std::nullopt;
}

void WorkerScriptLoader::loadAsynchronously(ScriptExecutionContext& scriptExecutionContext, ResourceRequest&& scriptRequest, Source source, FetchOptions&& fetchOptions, ContentSecurityPolicyEnforcement contentSecurityPolicyEnforcement, ServiceWorkersMode serviceWorkerMode, WorkerScriptLoaderClient& client, String&& taskMode, std::optional<ScriptExecutionContextIdentifier> clientIdentifier)
{
    m_client = client;
    m_url = scriptRequest.url();
    m_source = source;
    m_destination = fetchOptions.destination;
    m_isCOEPEnabled = scriptExecutionContext.settingsValues().crossOriginEmbedderPolicyEnabled;
    m_clientIdentifier = clientIdentifier;
    m_advancedPrivacyProtections = scriptExecutionContext.advancedPrivacyProtections();

    ASSERT(scriptRequest.httpMethod() == "GET"_s);

    auto request = makeUnique<ResourceRequest>(WTFMove(scriptRequest));
    if (!request)
        return;

    ThreadableLoaderOptions options { WTFMove(fetchOptions) };
    options.sendLoadCallbacks = SendCallbackPolicy::SendCallbacks;
    options.contentSecurityPolicyEnforcement = contentSecurityPolicyEnforcement;
    if (fetchOptions.destination == FetchOptions::Destination::Serviceworker)
        options.certificateInfoPolicy = CertificateInfoPolicy::IncludeCertificateInfo;

    // FIXME: We should drop the sameOriginDataURLFlag flag and implement the latest Fetch specification.
    options.sameOriginDataURLFlag = SameOriginDataURLFlag::Set;

    // A service worker job can be executed from a worker context or a document context.
    options.serviceWorkersMode = serviceWorkerMode;
    if (scriptExecutionContext.settingsValues().serviceWorkersEnabled && clientIdentifier) {
        ASSERT(m_destination == FetchOptions::Destination::Worker || m_destination == FetchOptions::Destination::Sharedworker);
        m_topOriginForServiceWorkerRegistration = SecurityOriginData { scriptExecutionContext.topOrigin().data() };
        options.clientIdentifier = scriptExecutionContext.identifier().object();
        options.resultingClientIdentifier = clientIdentifier->object();
        Ref serviceWorkerDataManager = ServiceWorkerDataManager::create(*clientIdentifier);
        m_serviceWorkerDataManager = serviceWorkerDataManager.copyRef();
        m_context = scriptExecutionContext;

        // In case of blob URLs, we reuse the context controlling service worker.
        if (request->url().protocolIsBlob() && scriptExecutionContext.activeServiceWorker())
            setControllingServiceWorker(ServiceWorkerData { scriptExecutionContext.activeServiceWorker()->data() });
        else {
            accessWorkerScriptLoaderMap([clientIdentifier = *clientIdentifier, serviceWorkerDataManager = WTFMove(serviceWorkerDataManager)](auto& map) mutable {
                map.add(clientIdentifier, serviceWorkerDataManager);
            });
            m_didAddToWorkerScriptLoaderMap = true;
        }
    } else if (auto* activeServiceWorker = scriptExecutionContext.activeServiceWorker())
        options.serviceWorkerRegistrationIdentifier = activeServiceWorker->registrationIdentifier();

    if (m_destination == FetchOptions::Destination::Sharedworker)
        m_userAgentForSharedWorker = scriptExecutionContext.userAgent(m_url);

    // During create, callbacks may happen which remove the last reference to this object.
    Ref<WorkerScriptLoader> protectedThis(*this);
    m_threadableLoader = ThreadableLoader::create(scriptExecutionContext, *this, WTFMove(*request), options, { }, WTFMove(taskMode));
}

const URL& WorkerScriptLoader::responseURL() const
{
    ASSERT(!failed());
    return m_responseURL;
}

std::unique_ptr<ResourceRequest> WorkerScriptLoader::createResourceRequest(const String& initiatorIdentifier)
{
    auto request = makeUnique<ResourceRequest>(URL { m_url });
    request->setHTTPMethod("GET"_s);
    request->setInitiatorIdentifier(initiatorIdentifier);
    return request;
}

static ResourceError constructJavaScriptMIMETypeError(const ResourceResponse& response)
{
    auto message = makeString("Refused to execute "_s, response.url().stringCenterEllipsizedToLength(), " as script because "_s, response.mimeType(), " is not a script MIME type."_s);
    return { errorDomainWebKitInternal, 0, response.url(), WTFMove(message), ResourceError::Type::AccessControl };
}

ResourceError WorkerScriptLoader::validateWorkerResponse(const ResourceResponse& response, Source source, FetchOptions::Destination destination)
{
    if (response.httpStatusCode() / 100 != 2 && response.httpStatusCode())
        return { errorDomainWebKitInternal, 0, response.url(), "Response is not 2xx"_s, ResourceError::Type::General };

    if (!isScriptAllowedByNosniff(response)) {
        auto message = makeString("Refused to execute "_s, response.url().stringCenterEllipsizedToLength(), " as script because \"X-Content-Type-Options: nosniff\" was given and its Content-Type is not a script MIME type."_s);
        return { errorDomainWebKitInternal, 0, response.url(), WTFMove(message), ResourceError::Type::General };
    }

    switch (source) {
    case Source::ClassicWorkerScript:
        // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-classic-worker-script (Step 5)
        // This is the result a dedicated / shared / service worker script fetch.
        if (response.url().protocolIsInHTTPFamily() && !MIMETypeRegistry::isSupportedJavaScriptMIMEType(response.mimeType()))
            return constructJavaScriptMIMETypeError(response);
        break;
    case Source::ClassicWorkerImport:
        // https://html.spec.whatwg.org/multipage/webappapis.html#fetch-a-classic-worker-imported-script (Step 5).
        // This is the result of an importScripts() call.
        if (!MIMETypeRegistry::isSupportedJavaScriptMIMEType(response.mimeType()))
            return constructJavaScriptMIMETypeError(response);
        break;
    case Source::ModuleScript:
        if (shouldBlockResponseDueToMIMEType(response, destination))
            return constructJavaScriptMIMETypeError(response);
        break;
    }

    return { };
}

void WorkerScriptLoader::didReceiveResponse(ScriptExecutionContextIdentifier mainContext, std::optional<ResourceLoaderIdentifier> identifier, const ResourceResponse& response)
{
    m_error = validateWorkerResponse(response, m_source, m_destination);
    if (!m_error.isNull()) {
        m_failed = true;
        return;
    }

    m_responseURL = response.url();
    m_certificateInfo = response.certificateInfo() ? *response.certificateInfo() : CertificateInfo();
    m_responseMIMEType = response.mimeType();
    m_responseSource = response.source();
    m_responseTainting = response.tainting();
    m_isRedirected = response.isRedirected();
    m_contentSecurityPolicy = ContentSecurityPolicyResponseHeaders { response };
    if (m_isCOEPEnabled)
        m_crossOriginEmbedderPolicy = obtainCrossOriginEmbedderPolicy(response, nullptr);
    m_referrerPolicy = response.httpHeaderField(HTTPHeaderName::ReferrerPolicy);

    if (m_topOriginForServiceWorkerRegistration && response.source() == ResourceResponse::Source::MemoryCache && m_context) {
        m_isMatchingServiceWorkerRegistration = true;
        RefPtr worker = dynamicDowncast<WorkerGlobalScope>(*m_context);
        Ref swConnection = worker ? static_cast<SWClientConnection&>(worker->swClientConnection()) : ServiceWorkerProvider::singleton().serviceWorkerConnection();
        swConnection->matchRegistration(WTFMove(*m_topOriginForServiceWorkerRegistration), response.url(), [this, protectedThis = Ref { *this }, response, mainContext, identifier](auto&& registrationData) mutable {
            m_isMatchingServiceWorkerRegistration = false;
            if (registrationData && registrationData->activeWorker)
                setControllingServiceWorker(WTFMove(*registrationData->activeWorker));

            if (!m_client)
                return;

            m_client->didReceiveResponse(mainContext, identifier, response);
            if (m_client && m_finishing)
                m_client->notifyFinished(mainContext);
        });
        return;
    }

    if (m_client)
        m_client->didReceiveResponse(mainContext, identifier, response);
}

void WorkerScriptLoader::didReceiveData(const SharedBuffer& buffer)
{
    if (m_failed)
        return;

#if ENABLE(WEBASSEMBLY)
    if (MIMETypeRegistry::isSupportedWebAssemblyMIMEType(m_responseMIMEType)) {
        m_script.append(buffer);
        return;
    }
#endif

    if (!m_decoder)
        lazyInitialize(m_decoder, TextResourceDecoder::create("text/javascript"_s, "UTF-8"_s));

    if (buffer.isEmpty())
        return;

    m_script.append(m_decoder->decode(buffer.span()));
}

void WorkerScriptLoader::didFinishLoading(ScriptExecutionContextIdentifier mainContext, std::optional<ResourceLoaderIdentifier> identifier, const NetworkLoadMetrics&)
{
    if (m_failed) {
        notifyError(mainContext);
        return;
    }

    if (m_decoder)
        m_script.append(m_decoder->flush());

    m_identifier = identifier;
    notifyFinished(mainContext);
}

void WorkerScriptLoader::didFail(std::optional<ScriptExecutionContextIdentifier> mainContext, const ResourceError& error)
{
    m_error = error;
    notifyError(mainContext);
}

void WorkerScriptLoader::notifyError(std::optional<ScriptExecutionContextIdentifier> mainContext)
{
    m_failed = true;
    if (m_error.isNull())
        m_error = { errorDomainWebKitInternal, 0, url(), "Failed to load script"_s, ResourceError::Type::General };
    notifyFinished(mainContext);
}

void WorkerScriptLoader::notifyFinished(std::optional<ScriptExecutionContextIdentifier> mainContext)
{
    m_threadableLoader = nullptr;
    if (!m_client || m_finishing)
        return;

    m_finishing = true;
    if (m_isMatchingServiceWorkerRegistration)
        return;

    m_client->notifyFinished(mainContext);
}

void WorkerScriptLoader::cancel()
{
    RefPtr threadableLoader = m_threadableLoader;
    if (!threadableLoader)
        return;

    m_client = nullptr;
    threadableLoader->cancel();
    m_threadableLoader = nullptr;
}

WorkerFetchResult WorkerScriptLoader::fetchResult() const
{
    if (m_failed)
        return workerFetchError(error());
    return { script(), responseURL(), certificateInfo(), contentSecurityPolicy(), crossOriginEmbedderPolicy(), referrerPolicy(), { } };
}

std::optional<ServiceWorkerData> WorkerScriptLoader::takeServiceWorkerData()
{
    if (RefPtr serviceWorkerDataManager = m_serviceWorkerDataManager)
        return serviceWorkerDataManager->takeData();
    return { };
}

RefPtr<WorkerScriptLoader::ServiceWorkerDataManager> WorkerScriptLoader::serviceWorkerDataManagerFromIdentifier(ScriptExecutionContextIdentifier identifier)
{
    RefPtr<ServiceWorkerDataManager> result;
    accessWorkerScriptLoaderMap([identifier, &result](auto& map) {
        result = map.get(identifier);
    });
    return result;
}

void WorkerScriptLoader::setControllingServiceWorker(ServiceWorkerData&& activeServiceWorkerData)
{
    Ref { *m_serviceWorkerDataManager }->setData(WTFMove(activeServiceWorkerData));
}

WorkerScriptLoader::ServiceWorkerDataManager::~ServiceWorkerDataManager()
{
    if (!m_activeServiceWorkerData)
        return;
    if (RefPtr serviceWorkerConnection = ServiceWorkerProvider::singleton().existingServiceWorkerConnection())
        serviceWorkerConnection->unregisterServiceWorkerClient(m_clientIdentifier);
}

void WorkerScriptLoader::ServiceWorkerDataManager::setData(ServiceWorkerData&& data)
{
    Locker lock(m_activeServiceWorkerDataLock);
    m_activeServiceWorkerData = WTFMove(data).isolatedCopy();
}

std::optional<ServiceWorkerData> WorkerScriptLoader::ServiceWorkerDataManager::takeData()
{
    Locker lock(m_activeServiceWorkerDataLock);
    return std::exchange(m_activeServiceWorkerData, std::nullopt);
}

} // namespace WebCore
