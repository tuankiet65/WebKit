/*
 * Copyright (C) 2006-2025 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies)
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved. (http://www.torchmobile.com/)
 * Copyright (C) 2008 Alp Toker <alp@atoker.com>
 * Copyright (C) Research In Motion Limited 2009. All rights reserved.
 * Copyright (C) 2011 Kris Jordan <krisjordan@gmail.com>
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "FrameLoader.h"

#include "AXObjectCache.h"
#include "ApplicationCacheHost.h"
#include "BackForwardCache.h"
#include "BackForwardController.h"
#include "BeforeUnloadEvent.h"
#include "CachePolicy.h"
#include "CachedPage.h"
#include "CachedResourceLoader.h"
#include "Chrome.h"
#include "ChromeClient.h"
#include "CommonVM.h"
#include "ContainerNodeInlines.h"
#include "ContentFilter.h"
#include "ContentRuleListResults.h"
#include "ContentSecurityPolicy.h"
#include "CrossOriginAccessControl.h"
#include "CrossOriginEmbedderPolicy.h"
#include "DNS.h"
#include "DatabaseManager.h"
#include "DiagnosticLoggingClient.h"
#include "DiagnosticLoggingKeys.h"
#include "DiagnosticLoggingResultType.h"
#include "DocumentInlines.h"
#include "DocumentLoader.h"
#include "Editor.h"
#include "EditorClient.h"
#include "ElementInlines.h"
#include "Event.h"
#include "EventHandler.h"
#include "EventNames.h"
#include "FloatRect.h"
#include "FormState.h"
#include "FormSubmission.h"
#include "FrameLoadRequest.h"
#include "FrameNetworkingContext.h"
#include "FrameTree.h"
#include "GCController.h"
#include "HTMLAnchorElement.h"
#include "HTMLFormElement.h"
#include "HTMLIFrameElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "HTMLObjectElement.h"
#include "HTMLParserIdioms.h"
#include "HTTPHeaderNames.h"
#include "HTTPHeaderValues.h"
#include "HTTPParsers.h"
#include "HistoryController.h"
#include "HistoryItem.h"
#include "InspectorController.h"
#include "InspectorInstrumentation.h"
#include "IntegrityPolicy.h"
#include "LinkLoader.h"
#include "LoaderStrategy.h"
#include "LocalDOMWindow.h"
#include "LocalFrame.h"
#include "LocalFrameLoaderClient.h"
#include "LocalFrameView.h"
#include "Logging.h"
#include "MemoryCache.h"
#include "MemoryRelease.h"
#include "Navigation.h"
#include "NavigationActivation.h"
#include "NavigationDisabler.h"
#include "NavigationNavigationType.h"
#include "NavigationScheduler.h"
#include "Node.h"
#include "OriginAccessPatterns.h"
#include "Page.h"
#include "PageTransitionEvent.h"
#include "Performance.h"
#include "PerformanceLogging.h"
#include "PermissionsPolicy.h"
#include "PlatformStrategies.h"
#include "PluginData.h"
#include "PluginDocument.h"
#include "PolicyChecker.h"
#include "ProgressTracker.h"
#include "Quirks.h"
#include "RemoteFrame.h"
#include "RenderWidgetInlines.h"
#include "ReportingScope.h"
#include "ResourceLoadInfo.h"
#include "ResourceLoadObserver.h"
#include "ResourceRequest.h"
#include "SVGLocatable.h"
#include "SVGNames.h"
#include "SVGViewElement.h"
#include "SVGViewSpec.h"
#include "ScriptController.h"
#include "ScriptSourceCode.h"
#include "ScrollAnimator.h"
#include "SecurityOrigin.h"
#include "SecurityPolicy.h"
#include "SegmentedString.h"
#include "SerializedScriptValue.h"
#include "Settings.h"
#include "ShouldTreatAsContinuingLoad.h"
#include "StyleTreeResolver.h"
#include "SubframeLoader.h"
#include "SubresourceLoader.h"
#include "TextResourceDecoder.h"
#include "UnloadCountIncrementer.h"
#include "UserContentController.h"
#include "UserGestureIndicator.h"
#include "WindowFeatures.h"
#include "XMLDocumentParser.h"
#include <dom/ScriptDisallowedScope.h>
#include <wtf/CheckedPtr.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Ref.h>
#include <wtf/SetForScope.h>
#include <wtf/StdLibExtras.h>
#include <wtf/SystemTracing.h>
#include <wtf/URL.h>
#include <wtf/text/CString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

#if ENABLE(WEB_ARCHIVE) || ENABLE(MHTML)
#include "Archive.h"
#endif

#if ENABLE(DATA_DETECTION)
#include "DataDetection.h"
#include "DataDetectionResultsStorage.h"
#endif

#if PLATFORM(COCOA)
#include <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#endif

#if PLATFORM(IOS_FAMILY)
#include "DocumentType.h"
#include "ResourceLoader.h"
#include <wtf/RuntimeApplicationChecks.h>
#endif

#define PAGE_ID (pageID() ? pageID()->toUInt64() : 0)
#define FRAME_ID (frameID().toUInt64())
#define FRAMELOADER_RELEASE_LOG(channel, fmt, ...) RELEASE_LOG(channel, "%p - [pageID=%" PRIu64 ", frameID=%" PRIu64 ", isMainFrame=%d] FrameLoader::" fmt, this, PAGE_ID, FRAME_ID, m_frame->isMainFrame(), ##__VA_ARGS__)
#define FRAMELOADER_RELEASE_LOG_FORWARDABLE(fmt, ...) RELEASE_LOG_FORWARDABLE(ResourceLoading, fmt, PAGE_ID, FRAME_ID, m_frame->isMainFrame(), ##__VA_ARGS__)
#define FRAMELOADER_RELEASE_LOG_ERROR(channel, fmt, ...) RELEASE_LOG_ERROR(channel, "%p - [pageID=%" PRIu64 ", frameID=%" PRIu64 ", isMainFrame=%d] FrameLoader::" fmt, this, PAGE_ID, FRAME_ID, m_frame->isMainFrame(), ##__VA_ARGS__)

#if USE(APPLE_INTERNAL_SDK) && __has_include(<WebKitAdditions/FrameLoaderAdditions.h>)
#import <WebKitAdditions/FrameLoaderAdditions.h>
#else
namespace WebCore {

static void verifyUserAgent(const String&)
{
}

}
#endif

namespace WebCore {

using namespace HTMLNames;
using namespace SVGNames;

bool isBackForwardLoadType(FrameLoadType type)
{
    switch (type) {
    case FrameLoadType::Standard:
    case FrameLoadType::Reload:
    case FrameLoadType::ReloadFromOrigin:
    case FrameLoadType::ReloadExpiredOnly:
    case FrameLoadType::Same:
    case FrameLoadType::RedirectWithLockedBackForwardList:
    case FrameLoadType::Replace:
        return false;
    case FrameLoadType::Back:
    case FrameLoadType::Forward:
    case FrameLoadType::IndexedBackForward:
        return true;
    }
    ASSERT_NOT_REACHED();
    return false;
}

bool isReload(FrameLoadType type)
{
    switch (type) {
    case FrameLoadType::Reload:
    case FrameLoadType::ReloadFromOrigin:
    case FrameLoadType::ReloadExpiredOnly:
        return true;
    case FrameLoadType::Standard:
    case FrameLoadType::Same:
    case FrameLoadType::RedirectWithLockedBackForwardList:
    case FrameLoadType::Replace:
    case FrameLoadType::Back:
    case FrameLoadType::Forward:
    case FrameLoadType::IndexedBackForward:
        return false;
    }
    ASSERT_NOT_REACHED();
    return false;
}

// This is not in the FrameLoader class to emphasize that it does not depend on
// private FrameLoader data, and to avoid increasing the number of public functions
// with access to private data.  Since only this .cpp file needs it, making it
// non-member lets us exclude it from the header file, thus keeping FrameLoader.h's
// API simpler.
//
static bool isDocumentSandboxed(LocalFrame& frame, SandboxFlag flag)
{
    return frame.document() && frame.document()->isSandboxed(flag);
}

static bool isInVisibleAndActivePage(const LocalFrame& frame)
{
    RefPtr page = frame.page();
    return page && page->isVisibleAndActive();
}

class PageLevelForbidScope {
protected:
    explicit PageLevelForbidScope(Page* page)
        : m_page(page)
    {
    }

    ~PageLevelForbidScope() = default;

    WeakPtr<Page> m_page;
};

struct ForbidPromptsScope : public PageLevelForbidScope {
    explicit ForbidPromptsScope(Page* page)
        : PageLevelForbidScope(page)
    {
        if (RefPtr page = m_page.get())
            page->forbidPrompts();
    }

    ~ForbidPromptsScope()
    {
        if (RefPtr page = m_page.get())
            page->allowPrompts();
    }
};

struct ForbidSynchronousLoadsScope : public PageLevelForbidScope {
    explicit ForbidSynchronousLoadsScope(Page* page)
        : PageLevelForbidScope(page)
    {
        if (RefPtr page = m_page.get())
            page->forbidSynchronousLoads();
    }

    ~ForbidSynchronousLoadsScope()
    {
        if (RefPtr page = m_page.get())
            page->allowSynchronousLoads();
    }
};

struct ForbidCopyPasteScope : public PageLevelForbidScope {
    explicit ForbidCopyPasteScope(Page* page)
        : PageLevelForbidScope(page)
        , m_oldDOMPasteAllowed(page->settings().domPasteAllowed())
        , m_oldJavaScriptCanAccessClipboard(page->settings().javaScriptCanAccessClipboard())
        , m_oldClipboardAccessPolicy(page->settings().clipboardAccessPolicy())
    {
        if (m_page) {
            m_page->settings().setDOMPasteAllowed(false);
            m_page->settings().setJavaScriptCanAccessClipboard(false);
            m_page->settings().setClipboardAccessPolicy(ClipboardAccessPolicy::Deny);
        }
    }

    ~ForbidCopyPasteScope()
    {
        if (m_page) {
            m_page->settings().setDOMPasteAllowed(m_oldDOMPasteAllowed);
            m_page->settings().setJavaScriptCanAccessClipboard(m_oldJavaScriptCanAccessClipboard);
            m_page->settings().setClipboardAccessPolicy(m_oldClipboardAccessPolicy);
        }
    }
private:
    bool m_oldDOMPasteAllowed;
    bool m_oldJavaScriptCanAccessClipboard;
    ClipboardAccessPolicy m_oldClipboardAccessPolicy;
};


class FrameLoader::FrameProgressTracker final : public CanMakeCheckedPtr<FrameLoader::FrameProgressTracker> {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED_WITH_HEAP_IDENTIFIER(FrameProgressTracker, Loader);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(FrameProgressTracker);
public:
    explicit FrameProgressTracker(LocalFrame& frame)
        : m_frame(frame)
    {
    }

    ~FrameProgressTracker()
    {
        if (m_inProgress && m_frame->page()) {
            Ref frame = m_frame.get();
            frame->protectedPage()->checkedProgress()->progressCompleted(frame);
        }
    }

    void progressStarted()
    {
        ASSERT(m_frame->page());
        if (!m_inProgress) {
            Ref frame = m_frame.get();
            frame->protectedPage()->checkedProgress()->progressStarted(frame);
        }
        m_inProgress = true;
    }

    void progressCompleted()
    {
        ASSERT(m_inProgress);
        ASSERT(m_frame->page());
        m_inProgress = false;
        Ref frame = m_frame.get();
        RefPtr page = frame->page();
        page->checkedProgress()->progressCompleted(frame);
        platformStrategies()->loaderStrategy()->pageLoadCompleted(*page);
    }

private:
    WeakRef<LocalFrame> m_frame;
    bool m_inProgress { false };
};

FrameLoader::FrameLoader(LocalFrame& frame, CompletionHandler<UniqueRef<LocalFrameLoaderClient>(LocalFrame&, FrameLoader&)>&& clientCreator)
    : m_frame(frame)
    , m_client(clientCreator(frame, *this))
    , m_policyChecker(makeUniqueRef<PolicyChecker>(frame))
    , m_history(makeUniqueRefWithoutRefCountedCheck<HistoryController>(frame))
    , m_notifier(frame)
    , m_subframeLoader(makeUniqueRef<SubframeLoader>(frame))
    , m_state(FrameState::Provisional)
    , m_loadType(FrameLoadType::Standard)
    , m_checkTimer(*this, &FrameLoader::checkTimerFired)
{
}

FrameLoader::~FrameLoader()
{
    Ref frame = m_frame.get();
    frame->disownOpener();
    frame->detachFromAllOpenedFrames();

    if (RefPtr networkingContext = m_networkingContext)
        networkingContext->invalidate();
}

void FrameLoader::ref() const
{
    m_frame->ref();
}

void FrameLoader::deref() const
{
    m_frame->deref();
}

LocalFrame& FrameLoader::frame() const
{
    return m_frame;
}

Ref<LocalFrame> FrameLoader::protectedFrame() const
{
    return m_frame.get();
}

void FrameLoader::init()
{
    // This somewhat odd set of steps gives the frame an initial empty document.
    setPolicyDocumentLoader(m_client->createDocumentLoader(ResourceRequest(URL({ }, emptyString())), SubstituteData()));
    setProvisionalDocumentLoader(m_policyDocumentLoader.copyRef());
    protectedProvisionalDocumentLoader()->startLoadingMainResource();
    setPolicyDocumentLoader(nullptr);

    Ref frame = m_frame.get();
    frame->protectedDocument()->cancelParsing();
    m_stateMachine.advanceTo(FrameLoaderStateMachine::DisplayingInitialEmptyDocument);

    m_networkingContext = m_client->createNetworkingContext();
    m_progressTracker = makeUnique<FrameProgressTracker>(frame);
}

void FrameLoader::initForSynthesizedDocument(const URL&)
{
    // FIXME: We need to initialize the document URL to the specified URL. Currently the URL is empty and hence
    // FrameLoader::checkCompleted() will overwrite the URL of the document to be activeDocumentLoader()->documentURL().

    Ref frame = m_frame.get();
    {
        Ref loader = m_client->createDocumentLoader(ResourceRequest(URL({ }, emptyString())), SubstituteData());
        loader->attachToFrame(frame);
        loader->setResponse(ResourceResponse(URL(), String { textHTMLContentTypeAtom() }, 0, String()));
        loader->setCommitted(true);
        setDocumentLoader(WTFMove(loader));
    }

    m_stateMachine.advanceTo(FrameLoaderStateMachine::DisplayingInitialEmptyDocument);
    m_stateMachine.advanceTo(FrameLoaderStateMachine::DisplayingInitialEmptyDocumentPostCommit);
    m_stateMachine.advanceTo(FrameLoaderStateMachine::CommittedFirstRealLoad);
    m_client->transitionToCommittedForNewPage(m_documentLoader && m_documentLoader->isInFinishedLoadingOfEmptyDocument() ?
        LocalFrameLoaderClient::InitializingIframe::Yes : LocalFrameLoaderClient::InitializingIframe::No);

    m_didCallImplicitClose = true;
    m_isComplete = true;
    m_state = FrameState::Complete;
    m_needsClear = true;

    m_networkingContext = m_client->createNetworkingContext();
    m_progressTracker = makeUnique<FrameProgressTracker>(frame);
}

std::optional<PageIdentifier> FrameLoader::pageID() const
{
    return m_frame->page() ? m_frame->page()->identifier() : std::nullopt;
}

FrameIdentifier FrameLoader::frameID() const
{
    return m_frame->frameID();
}

void FrameLoader::setDefersLoading(bool defers)
{
    if (RefPtr documentLoader = m_documentLoader)
        documentLoader->setDefersLoading(defers);
    if (RefPtr provisionalDocumentLoader = m_provisionalDocumentLoader)
        provisionalDocumentLoader->setDefersLoading(defers);
    if (RefPtr policyDocumentLoader = m_policyDocumentLoader)
        policyDocumentLoader->setDefersLoading(defers);
    history().setDefersLoading(defers);

    if (!defers) {
        protectedFrame()->protectedNavigationScheduler()->startTimer();
        startCheckCompleteTimer();
    }
}

void FrameLoader::checkContentPolicy(const ResourceResponse& response, ContentPolicyDecisionFunction&& function)
{
    if (!activeDocumentLoader()) {
        // Load was cancelled
        function(PolicyAction::Ignore);
        return;
    }

    // FIXME: Validate the policy check identifier.
    protectedClient()->dispatchDecidePolicyForResponse(response, activeDocumentLoader()->request(), activeDocumentLoader()->downloadAttribute(), WTFMove(function));
}

void FrameLoader::changeLocation(const URL& url, const AtomString& passedTarget, Event* triggeringEvent, const ReferrerPolicy& referrerPolicy, ShouldOpenExternalURLsPolicy shouldOpenExternalURLsPolicy, std::optional<NewFrameOpenerPolicy> openerPolicy, const AtomString& downloadAttribute, std::optional<PrivateClickMeasurement>&& privateClickMeasurement, NavigationHistoryBehavior historyBehavior, Element* sourceElement)
{
    RefPtr frame = lexicalFrameFromCommonVM();
    auto initiatedByMainFrame = frame && frame->isMainFrame() ? InitiatedByMainFrame::Yes : InitiatedByMainFrame::Unknown;

    Ref document = *m_frame->document();
    NewFrameOpenerPolicy newFrameOpenerPolicy = openerPolicy.value_or(referrerPolicy == ReferrerPolicy::NoReferrer ? NewFrameOpenerPolicy::Suppress : NewFrameOpenerPolicy::Allow);
    FrameLoadRequest frameLoadRequest(document.copyRef(), document->securityOrigin(), { URL { url } }, passedTarget, initiatedByMainFrame, downloadAttribute);
    frameLoadRequest.setNewFrameOpenerPolicy(newFrameOpenerPolicy);
    frameLoadRequest.setReferrerPolicy(referrerPolicy);
    frameLoadRequest.setShouldOpenExternalURLsPolicy(shouldOpenExternalURLsPolicy);
    frameLoadRequest.disableShouldReplaceDocumentIfJavaScriptURL();
    frameLoadRequest.setNavigationHistoryBehavior(historyBehavior);
    frameLoadRequest.setSourceElement(sourceElement);
    changeLocation(WTFMove(frameLoadRequest), triggeringEvent, WTFMove(privateClickMeasurement));
}

void FrameLoader::changeLocation(FrameLoadRequest&& frameRequest, Event* triggeringEvent, std::optional<PrivateClickMeasurement>&& privateClickMeasurement)
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_CHANGELOCATION);
    ASSERT(frameRequest.resourceRequest().httpMethod() == "GET"_s);

    Ref frame = m_frame.get();

    if (frameRequest.frameName().isEmpty())
        frameRequest.setFrameName(frame->document()->baseTarget());

    if (RefPtr document = frame->document())
        document->checkedContentSecurityPolicy()->upgradeInsecureRequestIfNeeded(frameRequest.resourceRequest(), ContentSecurityPolicy::InsecureRequestType::Navigation);

    loadFrameRequest(WTFMove(frameRequest), triggeringEvent, { }, WTFMove(privateClickMeasurement));
}

void FrameLoader::submitForm(Ref<FormSubmission>&& submission)
{
    ASSERT(submission->method() == FormSubmission::Method::Post || submission->method() == FormSubmission::Method::Get);

    // FIXME: Find a good spot for these.
    ASSERT(!submission->state().sourceDocument().frame() || submission->state().sourceDocument().frame() == m_frame.ptr());

    Ref frame = m_frame.get();
    if (!frame->page())
        return;

    if (submission->action().isEmpty())
        return;

    RefPtr document = frame->document();
    if (isDocumentSandboxed(frame, SandboxFlag::Forms)) {
        // FIXME: This message should be moved off the console once a solution to https://bugs.webkit.org/show_bug.cgi?id=103274 exists.
        document->addConsoleMessage(MessageSource::Security, MessageLevel::Error, makeString("Blocked form submission to '"_s, submission->action().stringCenterEllipsizedToLength(), "' because the form's frame is sandboxed and the 'allow-forms' permission is not set."_s));
        return;
    }

    URL formAction = submission->action();
    if (!document->checkedContentSecurityPolicy()->allowFormAction(formAction))
        return;

    RefPtr targetFrame = findFrameForNavigation(submission->target(), &submission->state().sourceDocument());
    if (!targetFrame) {
        if (!LocalDOMWindow::allowPopUp(frame) && !UserGestureIndicator::processingUserGesture())
            return;

        // FIXME: targetFrame can be null for two distinct reasons:
        // 1. The frame was not found by name, so we should try opening a new window.
        // 2. The frame was found, but navigating it was not allowed, e.g. by HTML5 sandbox or by origin checks.
        // Continuing form submission makes no sense in the latter case.
        // There is a repeat check after timer fires, so this is not a correctness issue.

        targetFrame = frame.copyRef();
    } else
        submission->clearTarget();

    if (!targetFrame->page())
        return;

    if (frame->tree().isDescendantOf(targetFrame.get()))
        m_submittedFormURL = submission->requestURL();

    submission->setReferrer(outgoingReferrer());
    submission->setOrigin(SecurityPolicy::generateOriginHeader(frame->document()->referrerPolicy(), submission->requestURL(), frame->protectedDocument()->protectedSecurityOrigin(), OriginAccessPatternsForWebProcess::singleton()));

    targetFrame->protectedNavigationScheduler()->scheduleFormSubmission(WTFMove(submission));
}

void FrameLoader::stopLoading(UnloadEventPolicy unloadEventPolicy)
{
    Ref frame = m_frame.get();

    if (RefPtr parser = frame->document() ? frame->document()->parser() : nullptr)
        parser->stopParsing();

    if (unloadEventPolicy != UnloadEventPolicy::None)
        dispatchUnloadEvents(unloadEventPolicy);

    m_isComplete = true; // to avoid calling completed() in finishedParsing()
    m_didCallImplicitClose = true; // don't want that one either

    if (RefPtr document = frame->document(); document && document->parsing()) {
        finishedParsing();
        document->setParsing(false);
    }

    if (RefPtr document = frame->document()) {
        // FIXME: Should the DatabaseManager watch for something like ActiveDOMObject::stop() rather than being special-cased here?
        DatabaseManager::singleton().stopDatabases(*document, nullptr);

        if (document->settings().navigationAPIEnabled() && !m_doNotAbortNavigationAPI && unloadEventPolicy != UnloadEventPolicy::UnloadAndPageHide) {
            RefPtr window = frame->document()->window();
            window->protectedNavigation()->abortOngoingNavigationIfNeeded();
        }
    }

    policyChecker().stopCheck();

    // FIXME: This will cancel redirection timer, which really needs to be restarted when restoring the frame from b/f cache.
    frame->protectedNavigationScheduler()->cancel();
}

void FrameLoader::stop()
{
    // http://bugs.webkit.org/show_bug.cgi?id=10854
    // The frame's last ref may be removed and it will be deleted by checkCompleted().
    Ref frame = m_frame.get();

    if (RefPtr parser = frame->document()->parser()) {
        parser->stopParsing();
        parser->finish();
    }
}

void FrameLoader::closeURL()
{
    history().saveDocumentState();

    RefPtr currentDocument = m_frame->document();
    UnloadEventPolicy unloadEventPolicy;
    if (m_frame->page() && m_frame->page()->chrome().client().isSVGImageChromeClient()) {
        // If this is the SVGDocument of an SVGImage, no need to dispatch events or recalcStyle.
        unloadEventPolicy = UnloadEventPolicy::None;
    } else {
        // Should only send the pagehide event here if the current document exists and has not been placed in the back/forward cache.
        unloadEventPolicy = currentDocument && currentDocument->backForwardCacheState() == Document::NotInBackForwardCache ? UnloadEventPolicy::UnloadAndPageHide : UnloadEventPolicy::UnloadOnly;
    }

    stopLoading(unloadEventPolicy);
    
    if (currentDocument)
        currentDocument->protectedEditor()->clearUndoRedoOperations();
}

bool FrameLoader::didOpenURL()
{
    Ref frame = m_frame.get();
    if (frame->protectedNavigationScheduler()->redirectScheduledDuringLoad()) {
        // A redirect was scheduled before the document was created.
        // This can happen when one frame changes another frame's location.
        return false;
    }

    frame->protectedNavigationScheduler()->cancel();

    m_isComplete = false;
    m_didCallImplicitClose = false;

    started();

    return true;
}

void FrameLoader::didExplicitOpen()
{
    m_isComplete = false;
    m_didCallImplicitClose = false;

    // Calling document.open counts as committing the first real document load.
    if (!m_stateMachine.committedFirstRealDocumentLoad())
        m_stateMachine.advanceTo(FrameLoaderStateMachine::DisplayingInitialEmptyDocumentPostCommit);

    if (RefPtr document = m_frame->document())
        m_client->dispatchDidExplicitOpen(document->url(), document->contentType());
    
    // Prevent window.open(url) -- eg window.open("about:blank") -- from blowing away results
    // from a subsequent window.document.open / window.document.write call. 
    // Canceling redirection here works for all cases because document.open 
    // implicitly precedes document.write.
    protectedFrame()->protectedNavigationScheduler()->cancel();
}

static inline bool shouldClearWindowName(const LocalFrame& frame, const Document& newDocument)
{
    if (!frame.isMainFrame())
        return false;

    if (frame.opener())
        return false;

    return !newDocument.protectedSecurityOrigin()->isSameOriginAs(frame.protectedDocument()->protectedSecurityOrigin());
}

void FrameLoader::clear(RefPtr<Document>&& newDocument, bool clearWindowProperties, bool clearScriptObjects, bool clearFrameView, Function<void()>&& handleDOMWindowCreation)
{
    bool neededClear = m_needsClear;
    m_needsClear = false;

    Ref<LocalFrame> frame = m_frame.get();

    RefPtr document = frame->document();
    if (neededClear)
        document->transferViewTransitionParams(*newDocument);

    if (neededClear && document->backForwardCacheState() != Document::InBackForwardCache) {
        document->cancelParsing();
        document->stopActiveDOMObjects();
        bool hadLivingRenderTree = document->hasLivingRenderTree();
        document->willBeRemovedFromFrame();
        if (hadLivingRenderTree)
            document->adjustFocusedNodeOnNodeRemoval(*document);
    }

    if (handleDOMWindowCreation)
        handleDOMWindowCreation();

    if (!neededClear)
        return;
    
    // Do this after detaching the document so that the unload event works.
    if (clearWindowProperties) {
        InspectorInstrumentation::frameWindowDiscarded(frame, document->protectedWindow().get());
        document->protectedWindow()->resetUnlessSuspendedForDocumentSuspension();
        frame->protectedWindowProxy()->clearJSWindowProxiesNotMatchingDOMWindow(newDocument->protectedWindow().get(), frame->document()->backForwardCacheState() == Document::AboutToEnterBackForwardCache);

        if (shouldClearWindowName(frame, *newDocument))
            frame->tree().setSpecifiedName(nullAtom());
    }

    frame->eventHandler().clear();

    if (clearFrameView && frame->view())
        frame->protectedView()->clear();

    // Do not drop the document before the ScriptController and view are cleared
    // as some destructors might still try to access the document.
    frame->setDocument(nullptr);

    subframeLoader().clear();

    if (clearWindowProperties)
        frame->protectedWindowProxy()->setDOMWindow(newDocument->protectedWindow().get());

    if (clearScriptObjects)
        frame->checkedScript()->clearScriptObjects();

    if (CheckedPtr newDocumentCSP = newDocument->contentSecurityPolicy()) {
        bool enableEvalValue = newDocumentCSP->evalErrorMessage().isNull();
        bool enableWASMValue = newDocumentCSP->webAssemblyErrorMessage().isNull();
        CheckedRef script = frame->script();
        script->setEvalEnabled(enableEvalValue, newDocumentCSP->evalErrorMessage());
        script->setWebAssemblyEnabled(enableWASMValue, newDocumentCSP->webAssemblyErrorMessage());
    }

    frame->protectedNavigationScheduler()->clear();

    m_checkTimer.stop();
    m_shouldCallCheckCompleted = false;
    m_shouldCallCheckLoadComplete = false;

    if (m_stateMachine.isDisplayingInitialEmptyDocument() && m_stateMachine.committedFirstRealDocumentLoad())
        m_stateMachine.advanceTo(FrameLoaderStateMachine::CommittedFirstRealLoad);
}

void FrameLoader::receivedFirstData()
{
    Ref frame = m_frame.get();
    
    dispatchDidCommitLoad(std::nullopt, std::nullopt, std::nullopt);
    dispatchDidClearWindowObjectsInAllWorlds();
    dispatchGlobalObjectAvailableInAllWorlds();

    RefPtr documentLoader = m_documentLoader;
    if (!documentLoader)
        return;

    auto& title = documentLoader->title();
    if (!title.string.isNull())
        m_client->dispatchDidReceiveTitle(title);

    ASSERT(frame->document());
    Ref document = *frame->document();

    LinkLoader::loadLinksFromHeader(documentLoader->response().httpHeaderField(HTTPHeaderName::Link), document->url(), document, LinkLoader::MediaAttributeCheck::MediaAttributeEmpty);

    scheduleRefreshIfNeeded(document, documentLoader->response().httpHeaderField(HTTPHeaderName::Refresh), IsMetaRefresh::No);
}

void FrameLoader::setOutgoingReferrer(const URL& url)
{
    auto result = url.strippedForUseAsReferrer();
    m_outgoingReferrer = WTFMove(result.string);
    if (result.stripped)
        m_outgoingReferrerURL = { };
    else
        m_outgoingReferrerURL = url;
}

static AtomString extractContentLanguageFromHeader(const String& header)
{
    auto commaIndex = header.find(',');
    if (commaIndex == notFound)
        return AtomString { header.trim(isASCIIWhitespace) };
    return StringView(header).left(commaIndex).trim(isASCIIWhitespace<char16_t>).toAtomString();
}

void FrameLoader::didBeginDocument(bool dispatch, LocalDOMWindow* previousWindow)
{
    m_needsClear = true;
    m_isComplete = false;
    m_didCallImplicitClose = false;
    Ref frame = m_frame.get();
    Ref document = *frame->document();
    document->setReadyState(Document::ReadyState::Loading);

    if (dispatch)
        dispatchDidClearWindowObjectsInAllWorlds();

    updateFirstPartyForCookies();
    document->initContentSecurityPolicy();

    Ref settings = frame->settings();
    document->protectedCachedResourceLoader()->setImagesEnabled(settings->areImagesEnabled());
    document->protectedCachedResourceLoader()->setAutoLoadImages(settings->loadsImagesAutomatically());

    std::optional<NavigationNavigationType> navigationType;

    if (RefPtr documentLoader = m_documentLoader) {
        // The DocumentLoader may have already parsed the CSP header to do some checks. If so, reuse the already parsed version instead of parsing again.
        if (CheckedPtr contentSecurityPolicy = documentLoader->contentSecurityPolicy())
            document->checkedContentSecurityPolicy()->didReceiveHeaders(*contentSecurityPolicy, ContentSecurityPolicy::ReportParsingErrors::No);
        else
            document->checkedContentSecurityPolicy()->didReceiveHeaders(ContentSecurityPolicyResponseHeaders(documentLoader->response()), referrer(), ContentSecurityPolicy::ReportParsingErrors::No);

        if (document->url().protocolIsBlob())
            document->checkedContentSecurityPolicy()->updateSourceSelf(SecurityOrigin::create(document->url()));

        if (document->url().protocolIsInHTTPFamily() || document->url().protocolIsBlob())
            document->setCrossOriginEmbedderPolicy(obtainCrossOriginEmbedderPolicy(documentLoader->response(), document.ptr()));

        String referrerPolicy = documentLoader->response().httpHeaderField(HTTPHeaderName::ReferrerPolicy);
        if (!referrerPolicy.isNull())
            document->processReferrerPolicy(referrerPolicy, ReferrerPolicySource::HTTPHeader);

        String headerContentLanguage = documentLoader->response().httpHeaderField(HTTPHeaderName::ContentLanguage);
        if (!headerContentLanguage.isEmpty()) {
            auto contentLanguage = extractContentLanguageFromHeader(headerContentLanguage);
            if (!contentLanguage.isEmpty())
                document->setContentLanguage(WTFMove(contentLanguage));
        }

        String reportingEndpoints = documentLoader->response().httpHeaderField(HTTPHeaderName::ReportingEndpoints);
        if (!reportingEndpoints.isEmpty())
            document->protectedReportingScope()->parseReportingEndpoints(reportingEndpoints, documentLoader->response().url());

        // https://html.spec.whatwg.org/multipage/browsing-the-web.html#initialise-the-document-object (Step 7)
        if (frame->isMainFrame()) {
            if (auto crossOriginOpenerPolicy = documentLoader->crossOriginOpenerPolicy())
                document->setCrossOriginOpenerPolicy(WTFMove(*crossOriginOpenerPolicy));
        }

        if (auto integrityPolicy = documentLoader->integrityPolicy())
            document->setIntegrityPolicy(WTFMove(integrityPolicy));

        if (auto integrityPolicyReportOnly = documentLoader->integrityPolicyReportOnly())
            document->setIntegrityPolicyReportOnly(WTFMove(integrityPolicyReportOnly));

        navigationType = m_documentLoader->triggeringAction().navigationAPIType();
    }

    if (document->settings().navigationAPIEnabled() && document->window() && !document->protectedSecurityOrigin()->isOpaque())
        document->protectedWindow()->protectedNavigation()->initializeForNewWindow(navigationType, previousWindow);

    history().restoreDocumentState();
}

void FrameLoader::finishedParsing()
{
    LOG(Loading, "WebCoreLoading frame %" PRIu64 ": Finished parsing", m_frame->frameID().toUInt64());

    Ref<LocalFrame> frame = m_frame.get();

    frame->injectUserScripts(UserScriptInjectionTime::DocumentEnd);

    if (m_stateMachine.creatingInitialEmptyDocument())
        return;

    m_client->dispatchDidFinishDocumentLoad();

    scrollToFragmentWithParentBoundary(frame->document()->url());

    checkCompleted();

    RefPtr view = frame->view();
    if (!view)
        return; // We are being destroyed by something checkCompleted called.

    // Check if the scrollbars are really needed for the content.
    // If not, remove them, relayout, and repaint.
    view->restoreScrollbar();
}

void FrameLoader::loadDone(LoadCompletionType type)
{
    if (type == LoadCompletionType::Finish)
        checkCompleted();
    else
        scheduleCheckCompleted();
}

void FrameLoader::subresourceLoadDone(LoadCompletionType type)
{
    if (type == LoadCompletionType::Finish)
        checkLoadComplete();
    else
        scheduleCheckLoadComplete();
}

bool FrameLoader::allChildrenAreComplete() const
{
    for (RefPtr child = m_frame->tree().firstChild(); child; child = child->tree().nextSibling()) {
        if (child->preventsParentFromBeingComplete())
            return false;
    }
    return true;
}

bool FrameLoader::allAncestorsAreComplete() const
{
    for (RefPtr<Frame> ancestor = m_frame.ptr(); ancestor; ancestor = ancestor->tree().parent()) {
        auto* localAncestor = dynamicDowncast<LocalFrame>(*ancestor);
        if (!localAncestor)
            continue;
        if (!localAncestor->loader().m_isComplete)
            return false;
    }
    return true;
}

void FrameLoader::checkCompleted()
{
    RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(ScriptDisallowedScope::InMainThread::isScriptAllowed());
    m_shouldCallCheckCompleted = false;

    // Have we completed before?
    if (m_isComplete)
        return;

    Ref<LocalFrame> frame = m_frame.get();
    Ref<Document> document = *frame->document();

    // FIXME: It would be better if resource loads were kicked off after render tree update (or didn't complete synchronously).
    //        https://bugs.webkit.org/show_bug.cgi?id=171729
    if (document->inRenderTreeUpdate()) {
        scheduleCheckCompleted();
        return;
    }

    // Are we still parsing?
    if (document->parsing())
        return;

    // Still waiting for images/scripts?
    if (document->cachedResourceLoader().requestCount())
        return;

    // Still waiting for elements that don't go through a FrameLoader?
    if (document->isDelayingLoadEvent())
        return;

    RefPtr scriptableParser = document->scriptableDocumentParser();
    if (scriptableParser && scriptableParser->hasScriptsWaitingForStylesheets())
        return;

    // Any frame that hasn't completed yet?
    if (!allChildrenAreComplete())
        return;

    // OK, completed.
    m_isComplete = true;
    m_requestedHistoryItem = nullptr;
    document->setReadyState(Document::ReadyState::Complete);

    checkCallImplicitClose(); // if we didn't do it before

    frame->protectedNavigationScheduler()->startTimer();

    completed();
    if (frame->page())
        checkLoadComplete();
}

void FrameLoader::checkTimerFired()
{
    checkCompletenessNow();
}

void FrameLoader::checkCompletenessNow()
{
    Ref frame = m_frame.get();

    if (RefPtr page = frame->page()) {
        if (page->defersLoading())
            return;
    }
    if (m_shouldCallCheckCompleted)
        checkCompleted();
    if (m_shouldCallCheckLoadComplete)
        checkLoadComplete();
}

void FrameLoader::startCheckCompleteTimer()
{
    if (!(m_shouldCallCheckCompleted || m_shouldCallCheckLoadComplete))
        return;
    if (m_checkTimer.isActive())
        return;
    m_checkTimer.startOneShot(0_s);
}

void FrameLoader::scheduleCheckCompleted()
{
    m_shouldCallCheckCompleted = true;
    startCheckCompleteTimer();
}

void FrameLoader::scheduleCheckLoadComplete()
{
    m_shouldCallCheckLoadComplete = true;
    startCheckCompleteTimer();
}

void FrameLoader::checkCallImplicitClose()
{
    if (m_didCallImplicitClose)
        return;

    Ref document = *m_frame->document();
    if (document->parsing() || document->isDelayingLoadEvent())
        return;

    if (!allChildrenAreComplete())
        return; // still got a frame running -> too early

    m_didCallImplicitClose = true;
    m_wasUnloadEventEmitted = false;
    document->implicitClose();
}

void FrameLoader::loadURLIntoChildFrame(const URL& url, const String& referer, LocalFrame& childFrame)
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_LOADURLINTOCHILDFRAME);

#if ENABLE(WEB_ARCHIVE) || ENABLE(MHTML)
    if (RefPtr activeLoader = activeDocumentLoader()) {
        if (RefPtr subframeArchive = activeLoader->popArchiveForSubframe(childFrame.tree().uniqueName(), url)) {
            childFrame.loader().loadArchive(subframeArchive.releaseNonNull());
            return;
        }
    }
#endif

    // If we're moving in the back/forward list, we might want to replace the content
    // of this child frame with whatever was there at that point.
    RefPtr parentItem = history().currentItem();
    if (parentItem && parentItem->children().size() && isBackForwardLoadType(loadType()) && !m_frame->document()->loadEventFinished()) {
        if (RefPtr childItem = parentItem->childItemWithTarget(childFrame.tree().uniqueName())) {
            Ref childLoader = childFrame.loader();
            childItem->setFrameID(childFrame.frameID());
            childLoader->m_requestedHistoryItem = childItem;
            childLoader->loadDifferentDocumentItem(*childItem, nullptr, loadType(), MayAttemptCacheOnlyLoadForFormSubmissionItem, ShouldTreatAsContinuingLoad::No);
            return;
        }
    }

    RefPtr lexicalFrame = lexicalFrameFromCommonVM();
    auto initiatedByMainFrame = lexicalFrame && lexicalFrame->isMainFrame() ? InitiatedByMainFrame::Yes : InitiatedByMainFrame::Unknown;

    RefPtr document = m_frame->document();
    FrameLoadRequest frameLoadRequest { *document, document->securityOrigin(), { URL { url } }, selfTargetFrameName(), initiatedByMainFrame };
    frameLoadRequest.setNewFrameOpenerPolicy(NewFrameOpenerPolicy::Suppress);
    frameLoadRequest.setLockBackForwardList(LockBackForwardList::Yes);
    frameLoadRequest.setIsInitialFrameSrcLoad(true);
    childFrame.loader().loadURL(WTFMove(frameLoadRequest), referer, FrameLoadType::RedirectWithLockedBackForwardList, nullptr, { }, std::nullopt, [] { });
}

#if ENABLE(WEB_ARCHIVE) || ENABLE(MHTML)

void FrameLoader::loadArchive(Ref<Archive>&& archive)
{
    FRAMELOADER_RELEASE_LOG(ResourceLoading, "loadArchive: frame load started");

    RefPtr mainResource = archive->mainResource();
    ASSERT(mainResource);
    if (!mainResource)
        return;

    ResourceResponse response(URL(), String { mainResource->mimeType() }, mainResource->data().size(), String { mainResource->textEncoding() });
    SubstituteData substituteData(&mainResource->data(), URL(), WTFMove(response), SubstituteData::SessionHistoryVisibility::Hidden);
    
    ResourceRequest request(URL { mainResource->url() });

    Ref documentLoader = m_client->createDocumentLoader(WTFMove(request), WTFMove(substituteData));
    documentLoader->setArchive(WTFMove(archive));
    load(documentLoader, nullptr);
}

#endif // ENABLE(WEB_ARCHIVE) || ENABLE(MHTML)

RefPtr<LocalFrame> FrameLoader::nonSrcdocFrame() const
{
    // See http://www.whatwg.org/specs/web-apps/current-work/#fetching-resources
    // for why we walk the parent chain for srcdoc documents.
    RefPtr<Frame> frame = m_frame.ptr();
    while (frame && is<LocalFrame>(*frame) && downcast<LocalFrame>(*frame).document()->isSrcdocDocument()) {
        frame = frame->tree().parent();
        // Srcdoc documents cannot be top-level documents, by definition,
        // because they need to be contained in iframes with the srcdoc.
        ASSERT(frame);
    }
    if (!frame)
        return nullptr;
    return dynamicDowncast<LocalFrame>(*frame);
}

String FrameLoader::outgoingReferrer() const
{
    RefPtr localFrame = nonSrcdocFrame();
    if (!localFrame)
        return emptyString();
    return localFrame->loader().m_outgoingReferrer;
}

URL FrameLoader::outgoingReferrerURL()
{
    RefPtr localFrame = nonSrcdocFrame();
    if (!localFrame)
        return URL { emptyString() };
    auto& loader = localFrame->loader();

    if (loader.m_outgoingReferrerURL.isValid())
        return loader.m_outgoingReferrerURL;
    URL result { loader.m_outgoingReferrer };
    loader.m_outgoingReferrerURL = result;
    return result;
}

String FrameLoader::outgoingOrigin() const
{
    return protectedFrame()->protectedDocument()->protectedSecurityOrigin()->toString();
}

bool FrameLoader::checkIfFormActionAllowedByCSP(const URL& url, bool didReceiveRedirectResponse, const URL& preRedirectURL) const
{
    if (m_submittedFormURL.isEmpty())
        return true;

    auto redirectResponseReceived = didReceiveRedirectResponse ? ContentSecurityPolicy::RedirectResponseReceived::Yes : ContentSecurityPolicy::RedirectResponseReceived::No;
    return m_frame->protectedDocument()->checkedContentSecurityPolicy()->allowFormAction(url, redirectResponseReceived, preRedirectURL);
}

void FrameLoader::provisionalLoadStarted()
{
    if (m_stateMachine.firstLayoutDone())
        m_stateMachine.advanceTo(FrameLoaderStateMachine::CommittedFirstRealLoad);
    Ref frame = m_frame.get();
    frame->protectedNavigationScheduler()->cancel(NewLoadInProgress::Yes);
    m_client->provisionalLoadStarted();

    if (frame->isMainFrame()) {
        tracePoint(MainResourceLoadDidStartProvisional, PAGE_ID);

        if (RefPtr page = frame->page())
            page->didStartProvisionalLoad();
    }
}

void FrameLoader::resetMultipleFormSubmissionProtection()
{
    m_submittedFormURL = URL();
}

void FrameLoader::updateFirstPartyForCookies()
{
    if (RefPtr page = m_frame->page())
        setFirstPartyForCookies(page->mainFrameURL());
}

void FrameLoader::setFirstPartyForCookies(const URL& url)
{
    Ref frame = m_frame.get();
    for (RefPtr<Frame> descendantFrame = frame.ptr(); descendantFrame; descendantFrame = descendantFrame->tree().traverseNext(frame.ptr())) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*descendantFrame))
            localFrame->protectedDocument()->setFirstPartyForCookies(url);
    }

    RegistrableDomain registrableDomain(url);
    for (RefPtr<Frame> descendantFrame = frame.ptr(); descendantFrame; descendantFrame = descendantFrame->tree().traverseNext(frame.ptr())) {
        RefPtr localFrame = dynamicDowncast<LocalFrame>(*descendantFrame);
        if (!localFrame)
            continue;
        if (SecurityPolicy::shouldInheritSecurityOriginFromOwner(localFrame->document()->url()) || registrableDomain.matches(localFrame->document()->url()))
            localFrame->protectedDocument()->setSiteForCookies(url);
    }
}

static NavigationNavigationType determineNavigationType(FrameLoadType loadType, NavigationHistoryBehavior historyHandling)
{
    if (historyHandling == NavigationHistoryBehavior::Push)
        return NavigationNavigationType::Push;
    if (historyHandling == NavigationHistoryBehavior::Replace)
        return NavigationNavigationType::Replace;
    if (historyHandling == NavigationHistoryBehavior::Reload)
        return NavigationNavigationType::Reload;

    if (isBackForwardLoadType(loadType))
        return NavigationNavigationType::Traverse;
    if (isReload(loadType))
        return NavigationNavigationType::Reload;
    if (loadType == FrameLoadType::Replace)
        return NavigationNavigationType::Replace;

    return NavigationNavigationType::Push;
}

// https://html.spec.whatwg.org/multipage/browsing-the-web.html#url-and-history-update-steps
void FrameLoader::updateURLAndHistory(const URL& newURL, RefPtr<SerializedScriptValue>&& stateObject, NavigationHistoryBehavior historyHandling)
{
    ASSERT(m_frame->document() && documentLoader());

    if (documentLoader()->isInitialAboutBlank())
        historyHandling = NavigationHistoryBehavior::Replace;

    Ref history = m_history.get();

    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#restore-the-history-object-state
    // FIXME: Implement "restore the history object state" deserializing (step 2).
    // Note: Implement "otherwise activeEntry's classic history API state" (step 3) if a caller needs that (so far
    // callers always set stateObject explicitly).

    m_frame->protectedDocument()->updateURLForPushOrReplaceState(newURL);

    if (historyHandling == NavigationHistoryBehavior::Replace) {
        history->replaceState(WTFMove(stateObject), newURL.string());
        protectedClient()->dispatchDidReplaceStateWithinPage();
    } else {
        history->pushState(WTFMove(stateObject), newURL.string());
        protectedClient()->dispatchDidPushStateWithinPage();
    }
}

// This does the same kind of work that didOpenURL does, except it relies on the fact
// that a higher level already checked that the URLs match and the scrolling is the right thing to do.
void FrameLoader::loadInSameDocument(URL url, RefPtr<SerializedScriptValue> stateObject, const SecurityOrigin* requesterOrigin, bool isNewNavigation, NavigationHistoryBehavior historyHandling)
{
    FRAMELOADER_RELEASE_LOG(ResourceLoading, "loadInSameDocument: frame load started");

    // If we have a state object, we cannot also be a new navigation.
    ASSERT(!stateObject || (stateObject && !isNewNavigation));

    m_errorOccurredInLoading = false;

    RefPtr document = m_frame->document();
    // Update the data source's request with the new URL to fake the URL change
    URL oldURL = document->url();

    document->setURL(URL { url });
    setOutgoingReferrer(url);
    protectedDocumentLoader()->replaceRequestURLForSameDocumentNavigation(url);
    if (isNewNavigation && !shouldTreatURLAsSameAsCurrent(requesterOrigin, url) && !stateObject) {
        // NB: must happen after replaceRequestURLForSameDocumentNavigation(), since we add 
        // based on the current request. Must also happen before we openURL and displace the 
        // scroll position, since adding the BF item will save away scroll state.
        
        // NB2: If we were loading a long, slow doc, and the user fragment navigated before
        // it was done, currItem is now set the that slow doc, and prevItem is whatever was
        // before it.  Adding the b/f item will bump the slow doc down to prevItem, even
        // though its load is not yet done.  I think this all works out OK, for one because
        // we have already saved away the scroll and doc state for the long slow load,
        // but it's not an obvious case.

        std::optional<WTF::UUID> uuid;
        if (historyHandling == NavigationHistoryBehavior::Replace) {
            if (RefPtr currentItem = history().currentItem())
                uuid = currentItem->uuidIdentifier();
        }
        history().updateBackForwardListForFragmentScroll();
        if (uuid)
            history().currentItem()->setUUIDIdentifier(*uuid);

        if (!document->hasRecentUserInteractionForNavigationFromJS() && !documentLoader()->triggeringAction().isRequestFromClientOrUserInput()) {
            if (RefPtr currentItem = history().currentItem())
                currentItem->setWasCreatedByJSWithoutUserInteraction(true);
        }
    }

    bool hashChange = equalIgnoringFragmentIdentifier(url, oldURL) && !equalRespectingNullity(url.fragmentIdentifier(), oldURL.fragmentIdentifier());

    history().updateForSameDocumentNavigation();

    auto navigationType = determineNavigationType(m_loadType, historyHandling);
    if (document->settings().navigationAPIEnabled() && document->window() && history().currentItem())
        document->protectedWindow()->protectedNavigation()->updateForNavigation(*history().currentItem(), navigationType, ShouldCopyStateObjectFromCurrentEntry::Yes);

    // If we were in the autoscroll/panScroll mode we want to stop it before following the link to the anchor
    if (hashChange)
        protectedFrame()->eventHandler().stopAutoscrollTimer();
    
    // It's important to model this as a load that starts and immediately finishes.
    // Otherwise, the parent frame may think we never finished loading.
    started();

    if (RefPtr ownerElement = m_frame->ownerElement()) {
        CheckedPtr ownerRenderer = dynamicDowncast<RenderWidget>(ownerElement->renderer());
        RefPtr view = m_frame->view();
        if (ownerRenderer && view)
            ownerRenderer->setWidget(WTFMove(view));
    }

    // We need to scroll to the fragment whether or not a hash change occurred, since
    // the user might have scrolled since the previous navigation.
    scrollToFragmentWithParentBoundary(url, isNewNavigation);
    
    m_isComplete = false;
    checkCompleted();

    if (isNewNavigation) {
        // This will clear previousItem from the rest of the frame tree that didn't
        // doing any loading. We need to make a pass on this now, since for fragment
        // navigation we'll not go through a real load and reach Completed state.
        checkLoadComplete();
    }

    m_client->dispatchDidNavigateWithinPage();

    document->statePopped(stateObject ? stateObject.releaseNonNull() : SerializedScriptValue::nullValue());
    m_client->dispatchDidPopStateWithinPage();
    
    if (hashChange) {
        document->enqueueHashchangeEvent(oldURL.string(), url.string());
        m_client->dispatchDidChangeLocationWithinPage();
    }

    RefPtr parentFrame = m_frame->tree().parent();
    RefPtr localParentFrame = dynamicDowncast<LocalFrame>(parentFrame.get());
    if (parentFrame
        && (document->processingLoadEvent() || document->loadEventFinished())
        && (!localParentFrame || !document->protectedSecurityOrigin()->isSameOriginAs(localParentFrame->protectedDocument()->protectedSecurityOrigin())))
        protectedFrame()->dispatchLoadEventToParent();

    // LocalFrameLoaderClient::didFinishLoad() tells the internal load delegate the load finished with no error
    m_client->didFinishLoad();
}

bool FrameLoader::isComplete() const
{
    return m_isComplete;
}

void FrameLoader::completed()
{
    Ref frame = m_frame.get();

    for (RefPtr descendant = frame->tree().traverseNext(frame.ptr()); descendant; descendant = descendant->tree().traverseNext(frame.ptr()))
        descendant->protectedNavigationScheduler()->startTimer();

    if (RefPtr parent = frame->tree().parent()) {
        if (RefPtr localParent = dynamicDowncast<LocalFrame>(parent.releaseNonNull()))
            localParent->loader().checkCompleted();
    }

    if (RefPtr view = frame->view())
        view->maintainScrollPositionAtAnchor(nullptr);
}

void FrameLoader::started()
{
    for (RefPtr<Frame> frame = m_frame.ptr(); frame; frame = frame->tree().parent()) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame))
            localFrame->loader().m_isComplete = false;
    }
}

void FrameLoader::prepareForLoadStart()
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_PREPAREFORLOADSTART);

    m_progressTracker->progressStarted();
    m_client->dispatchDidStartProvisionalLoad();

    if (AXObjectCache::accessibilityEnabled()) {
        if (CheckedPtr cache = m_frame->protectedDocument()->existingAXObjectCache()) {
            auto loadingEvent = loadType() == FrameLoadType::Reload ? AXLoadingEvent::Reloaded : AXLoadingEvent::Started;
            cache->frameLoadingEventNotification(protectedFrame().ptr(), loadingEvent);
        }
    }
}

void FrameLoader::setupForReplace()
{
    m_client->revertToProvisionalState(protectedDocumentLoader().get());
    setState(FrameState::Provisional);
    m_provisionalDocumentLoader = m_documentLoader;
    FRAMELOADER_RELEASE_LOG(ResourceLoading, "setupForReplace: Setting provisional document loader (m_provisionalDocumentLoader=%p)", m_provisionalDocumentLoader.get());
    m_documentLoader = nullptr;
    detachChildren();
}

void FrameLoader::loadFrameRequest(FrameLoadRequest&& request, Event* event, RefPtr<FormState>&& formState, std::optional<PrivateClickMeasurement>&& privateClickMeasurement)
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_LOADFRAMEREQUEST_FRAME_LOAD_STARTED);

    m_errorOccurredInLoading = false;

    // Protect frame from getting blown away inside dispatchBeforeLoadEvent in loadWithDocumentLoader.
    Ref frame = m_frame.get();

    URL url = request.resourceRequest().url();

    ASSERT(frame->document());
    if (!request.protectedRequesterSecurityOrigin()->canDisplay(url, OriginAccessPatternsForWebProcess::singleton())) {
        FRAMELOADER_RELEASE_LOG(ResourceLoading, "loadFrameRequest: canceling - Not allowed to load local resource");
        reportLocalLoadFailed(frame.ptr(), url.stringCenterEllipsizedToLength());
        return;
    }

    if (!portAllowed(url)) {
        FRAMELOADER_RELEASE_LOG(ResourceLoading, "loadFrameRequest: canceling - port not allowed");
        reportBlockedLoadFailed(frame, url);
        return;
    }

    if (isIPAddressDisallowed(url)) {
        FRAMELOADER_RELEASE_LOG(ResourceLoading, "loadFrameRequest: canceling - IP address is not allowed");
        reportBlockedLoadFailed(frame, url);
        return;
    }
    
    URL argsReferrer;
    String argsReferrerString = request.resourceRequest().httpReferrer();
    if (argsReferrerString.isEmpty())
        argsReferrer = outgoingReferrerURL();
    else
        argsReferrer = URL { argsReferrerString };

    ReferrerPolicy referrerPolicy = request.referrerPolicy();
    if (referrerPolicy == ReferrerPolicy::EmptyString)
        referrerPolicy = frame->document()->referrerPolicy();
    String referrer = SecurityPolicy::generateReferrerHeader(referrerPolicy, url, argsReferrer, OriginAccessPatternsForWebProcess::singleton());

    FrameLoadType loadType;
    if (request.resourceRequest().cachePolicy() == ResourceRequestCachePolicy::ReloadIgnoringCacheData)
        loadType = FrameLoadType::Reload;
    else if (request.lockBackForwardList() == LockBackForwardList::Yes)
        loadType = FrameLoadType::RedirectWithLockedBackForwardList;
    else
        loadType = FrameLoadType::Standard;

    auto completionHandler = [frame, formState = WeakPtr { formState }, frameName = request.frameName()] {
        // FIXME: It's possible this targetFrame will not be the same frame that was targeted by the actual
        // load if frame names have changed.
        RefPtr sourceFrame = formState ? formState->sourceDocument().frame() : frame.ptr();
        if (!sourceFrame)
            sourceFrame = frame.ptr();
        RefPtr targetFrame = sourceFrame->loader().findFrameForNavigation(frameName);
        if (targetFrame && targetFrame != sourceFrame) {
            if (RefPtr page = targetFrame->page(); page && isInVisibleAndActivePage(*sourceFrame))
                page->chrome().focus();
        }
    };

    auto finishLoadFrameRequest = [referrer, event = RefPtr { event }, loadType] (Ref<LocalFrame>&& frame, FrameLoadRequest&& request, RefPtr<FormState>&& formState, std::optional<PrivateClickMeasurement>&& privateClickMeasurement, CompletionHandler<void()>&& completionHandler) mutable {
        if (request.resourceRequest().httpMethod() == "POST"_s)
            frame->loader().loadPostRequest(WTFMove(request), referrer, loadType, event.get(), WTFMove(formState), WTFMove(completionHandler));
        else
            frame->loader().loadURL(WTFMove(request), referrer, loadType, event.get(), WTFMove(formState), WTFMove(privateClickMeasurement), WTFMove(completionHandler));
    };

    if (loadType == FrameLoadType::Reload) {
        if (m_frame->document() && m_frame->document()->settings().navigationAPIEnabled()) {
            if (RefPtr window = frame->document()->window()) {
                RefPtr<SerializedScriptValue> stateObject;
                if (RefPtr currentItem = history().currentItem())
                    stateObject = currentItem->navigationAPIStateObject();
                RefPtr sourceElement = event ? dynamicDowncast<Element>(event->target()) : nullptr;
                if (!dispatchNavigateEvent(url, loadType, request.downloadAttribute(), request.navigationHistoryBehavior(), false, formState.get(), stateObject.get(), sourceElement.get()))
                    return;
                if (!frame->page())
                    return;
                finishLoadFrameRequest(WTFMove(frame), WTFMove(request), WTFMove(formState), WTFMove(privateClickMeasurement), WTFMove(completionHandler));
            }
            return;
        }
    }

    finishLoadFrameRequest(WTFMove(frame), WTFMove(request), WTFMove(formState), WTFMove(privateClickMeasurement), WTFMove(completionHandler));
}

static ShouldOpenExternalURLsPolicy shouldOpenExternalURLsPolicyToApply(LocalFrame& currentFrame, InitiatedByMainFrame initiatedByMainFrame, ShouldOpenExternalURLsPolicy propagatedPolicy)
{
    if (UserGestureIndicator::processingUserGesture())
        return ShouldOpenExternalURLsPolicy::ShouldAllow;

    if (initiatedByMainFrame == InitiatedByMainFrame::Yes)
        return propagatedPolicy;

    if (!currentFrame.isMainFrame())
        return ShouldOpenExternalURLsPolicy::ShouldNotAllow;

    return propagatedPolicy;
}

static ShouldOpenExternalURLsPolicy shouldOpenExternalURLsPolicyToApply(LocalFrame& currentFrame, const FrameLoadRequest& frameLoadRequest)
{
    return shouldOpenExternalURLsPolicyToApply(currentFrame, frameLoadRequest.initiatedByMainFrame(), frameLoadRequest.shouldOpenExternalURLsPolicy());
}

static void applyShouldOpenExternalURLsPolicyToNewDocumentLoader(LocalFrame& frame, DocumentLoader& documentLoader, InitiatedByMainFrame initiatedByMainFrame, ShouldOpenExternalURLsPolicy propagatedPolicy)
{
    documentLoader.setShouldOpenExternalURLsPolicy(shouldOpenExternalURLsPolicyToApply(frame, initiatedByMainFrame, propagatedPolicy));
}

static void applyShouldOpenExternalURLsPolicyToNewDocumentLoader(LocalFrame& frame, DocumentLoader& documentLoader, const FrameLoadRequest& frameLoadRequest)
{
    documentLoader.setShouldOpenExternalURLsPolicy(shouldOpenExternalURLsPolicyToApply(frame, frameLoadRequest));
}

bool FrameLoader::isNavigationAllowed() const
{
    return m_pageDismissalEventBeingDispatched == PageDismissalType::None && !m_frame->script().willReplaceWithResultOfExecutingJavascriptURL() && NavigationDisabler::isNavigationAllowed(protectedFrame());
}

bool FrameLoader::isStopLoadingAllowed() const
{
    return m_pageDismissalEventBeingDispatched == PageDismissalType::None;
}

void FrameLoader::loadURL(FrameLoadRequest&& frameLoadRequest, const String& referrer, FrameLoadType newLoadType, Event* event, RefPtr<FormState>&& formState, std::optional<PrivateClickMeasurement>&& privateClickMeasurement, CompletionHandler<void()>&& completionHandler)
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_LOAD_URL);
    ASSERT(frameLoadRequest.resourceRequest().httpMethod() == "GET"_s);

    m_errorOccurredInLoading = false;

    CompletionHandlerCallingScope completionHandlerCaller(WTFMove(completionHandler));
    if (m_inStopAllLoaders || m_inClearProvisionalLoadForPolicyCheck)
        return;

    Ref frame = m_frame.get();

    // Anchor target is ignored when the download attribute is set since it will download the hyperlink rather than follow it.
    auto effectiveFrameName = frameLoadRequest.downloadAttribute().isNull() ? frameLoadRequest.frameName() : nullAtom();
    bool isFormSubmission = formState;

    // The search for a target frame is done earlier in the case of form submission.
    RefPtr effectiveTargetFrame = findFrameForNavigation(effectiveFrameName);
    if (is<RemoteFrame>(effectiveTargetFrame)) {
        updateRequestAndAddExtraFields(*effectiveTargetFrame, frameLoadRequest.resourceRequest(), IsMainResource::Yes, newLoadType, ShouldUpdateAppInitiatedValue::Yes, FrameLoader::IsServiceWorkerNavigationLoad::No, WillOpenInNewWindow::No, frameLoadRequest.protectedRequester().ptr());
        effectiveTargetFrame->changeLocation(WTFMove(frameLoadRequest));
        return;
    }

    RefPtr targetFrame = isFormSubmission ? nullptr : dynamicDowncast<LocalFrame>(effectiveTargetFrame);
    if (targetFrame && targetFrame != frame.ptr()) {
        frameLoadRequest.setFrameName(selfTargetFrameName());
        targetFrame->loader().loadURL(WTFMove(frameLoadRequest), referrer, newLoadType, event, WTFMove(formState), WTFMove(privateClickMeasurement), completionHandlerCaller.release());
        return;
    }

    const URL& newURL = frameLoadRequest.resourceRequest().url();
    ResourceRequest request(URL { newURL });
    if (!referrer.isEmpty())
        request.setHTTPReferrer(referrer);

    auto willOpenInNewWindow = !targetFrame && !effectiveFrameName.isEmpty() ? WillOpenInNewWindow::Yes : WillOpenInNewWindow::No;
    updateRequestAndAddExtraFields(request, IsMainResource::Yes, newLoadType, ShouldUpdateAppInitiatedValue::Yes, FrameLoader::IsServiceWorkerNavigationLoad::No, willOpenInNewWindow, frameLoadRequest.protectedRequester().ptr());

    ASSERT(newLoadType != FrameLoadType::Same);

    if (!isNavigationAllowed())
        return;

    NavigationAction action { frameLoadRequest.requester(), request, frameLoadRequest.initiatedByMainFrame(), frameLoadRequest.isRequestFromClientOrUserInput(), newLoadType, isFormSubmission, event, frameLoadRequest.shouldOpenExternalURLsPolicy(), frameLoadRequest.downloadAttribute(), frameLoadRequest.sourceElement() };
    action.setLockHistory(frameLoadRequest.lockHistory());
    action.setLockBackForwardList(frameLoadRequest.lockBackForwardList());
    action.setShouldReplaceDocumentIfJavaScriptURL(frameLoadRequest.shouldReplaceDocumentIfJavaScriptURL());
    action.setIsInitialFrameSrcLoad(frameLoadRequest.isInitialFrameSrcLoad());
    action.setIsFromNavigationAPI(frameLoadRequest.isFromNavigationAPI());
    action.setNewFrameOpenerPolicy(frameLoadRequest.newFrameOpenerPolicy());
    auto historyHandling = frameLoadRequest.navigationHistoryBehavior();
    RefPtr document = m_frame->document();
    bool isSameOrigin = frameLoadRequest.protectedRequesterSecurityOrigin()->isSameOriginDomain(document->protectedSecurityOrigin().get());
    if (!isReload(newLoadType)) {
        if (historyHandling == NavigationHistoryBehavior::Auto) {
            if ((document->url() == newURL || document->readyState() != Document::ReadyState::Complete) && isSameOrigin)
                historyHandling = NavigationHistoryBehavior::Replace;
            else
                historyHandling = NavigationHistoryBehavior::Push;
        }
        if (newURL.protocolIsJavaScript() || (documentLoader() && documentLoader()->isInitialAboutBlank()))
            historyHandling = NavigationHistoryBehavior::Replace;
    }
    action.setNavigationAPIType(determineNavigationType(newLoadType, historyHandling));
    if (privateClickMeasurement && frame->isMainFrame())
        action.setPrivateClickMeasurement(WTFMove(*privateClickMeasurement));

    NewFrameOpenerPolicy openerPolicy = frameLoadRequest.newFrameOpenerPolicy();
    AllowNavigationToInvalidURL allowNavigationToInvalidURL = frameLoadRequest.allowNavigationToInvalidURL();
    if (!targetFrame && !effectiveFrameName.isEmpty()) {
        action = action.copyWithShouldOpenExternalURLsPolicy(shouldOpenExternalURLsPolicyToApply(frame, frameLoadRequest));

        // https://html.spec.whatwg.org/#the-rules-for-choosing-a-browsing-context-given-a-browsing-context-name (Step 8.2)
        if (frameLoadRequest.protectedRequester()->shouldForceNoOpenerBasedOnCOOP()) {
            effectiveFrameName = blankTargetFrameName();
            openerPolicy = NewFrameOpenerPolicy::Suppress;
        }

        if (frameLoadRequest.resourceRequest().url().protocolIsBlob() && !document->protectedSecurityOrigin()->isSameOriginAs(document->protectedTopOrigin())) {
            effectiveFrameName = blankTargetFrameName();
            openerPolicy = NewFrameOpenerPolicy::Suppress;
        }

        policyChecker().checkNewWindowPolicy(WTFMove(action), WTFMove(request), WTFMove(formState), effectiveFrameName, [this, protectedThis = Ref { *this }, allowNavigationToInvalidURL, openerPolicy, completionHandler = completionHandlerCaller.release()] (ResourceRequest&& request, WeakPtr<FormState>&& weakFormState, const AtomString& frameName, const NavigationAction& action, ShouldContinuePolicyCheck shouldContinue) mutable {
            continueLoadAfterNewWindowPolicy(WTFMove(request), RefPtr { weakFormState.get() }.get(), frameName, action, shouldContinue, allowNavigationToInvalidURL, openerPolicy);
            completionHandler();
        });
        return;
    }

    RefPtr oldDocumentLoader = m_documentLoader;

    bool sameURL = shouldTreatURLAsSameAsCurrent(frameLoadRequest.protectedRequesterSecurityOrigin().ptr(), newURL);
    const String& httpMethod = request.httpMethod();

    // Make sure to do scroll to fragment processing even if the URL is
    // exactly the same so pages with '#' links and DHTML side effects
    // work properly.
    if (shouldPerformFragmentNavigation(isFormSubmission, httpMethod, newLoadType, newURL)) {
        RefPtr sourceElement = event ? dynamicDowncast<Element>(event->target()) : nullptr;
        if (!dispatchNavigateEvent(newURL, newLoadType, action.downloadAttribute(), historyHandling, true, formState.get(), nullptr, sourceElement.get()))
            return;

        oldDocumentLoader->setTriggeringAction(WTFMove(action));
        oldDocumentLoader->setLastCheckedRequest(ResourceRequest());
        policyChecker().stopCheck();
        policyChecker().setLoadType(newLoadType);
        RELEASE_ASSERT(!isBackForwardLoadType(newLoadType) || history().provisionalItem());
        policyChecker().checkNavigationPolicy(WTFMove(request), ResourceResponse { } /* redirectResponse */, oldDocumentLoader.get(), WTFMove(formState), [this, protectedThis = Ref { *this }, requesterOrigin = Ref { frameLoadRequest.requesterSecurityOrigin() }, historyHandling] (const ResourceRequest& request, WeakPtr<FormState>&&, NavigationPolicyDecision navigationPolicyDecision) {
            continueFragmentScrollAfterNavigationPolicy(request, requesterOrigin.ptr(), navigationPolicyDecision == NavigationPolicyDecision::ContinueLoad, historyHandling);
        }, PolicyDecisionMode::Synchronous);
        return;
    }

    if (isSameOrigin && newLoadType != FrameLoadType::Reload) {
        RefPtr sourceElement = event ? dynamicDowncast<Element>(event->target()) : nullptr;
        if (!dispatchNavigateEvent(newURL, newLoadType, action.downloadAttribute(), historyHandling, false, formState.get(), nullptr, sourceElement.get()))
            return;
    }

    // Must grab this now, since this load may stop the previous load and clear this flag.
    bool isRedirect = m_quickRedirectComing;
    loadWithNavigationAction(WTFMove(request), WTFMove(action), newLoadType, WTFMove(formState), allowNavigationToInvalidURL, frameLoadRequest.shouldTreatAsContinuingLoad(), [this, protectedThis = Ref { *this }, isRedirect, sameURL, newLoadType, completionHandler = completionHandlerCaller.release()] () mutable {
        if (isRedirect) {
            m_quickRedirectComing = false;
            if (RefPtr provisionalDocumentLoader = m_provisionalDocumentLoader)
                provisionalDocumentLoader->setIsClientRedirect(true);
            else if (RefPtr policyDocumentLoader = m_policyDocumentLoader)
                policyDocumentLoader->setIsClientRedirect(true);
        } else if (sameURL && !isReload(newLoadType)) {
            // Example of this case are sites that reload the same URL with a different cookie
            // driving the generated content, or a master frame with links that drive a target
            // frame, where the user has clicked on the same link repeatedly.
            m_loadType = FrameLoadType::Same;
        }
        completionHandler();
    });
}

SubstituteData FrameLoader::defaultSubstituteDataForURL(const URL& url)
{
    if (!shouldTreatURLAsSrcdocDocument(url))
        return SubstituteData();
    RefPtr iframeElement = dynamicDowncast<HTMLIFrameElement>(m_frame->ownerElement());
    if (!iframeElement)
        return SubstituteData();

    auto& srcdoc = iframeElement->attributeWithoutSynchronization(srcdocAttr);
    ASSERT(!srcdoc.isNull());
    CString encodedSrcdoc = srcdoc.string().utf8();

    ResourceResponse response(URL(), String { textHTMLContentTypeAtom() }, encodedSrcdoc.length(), "UTF-8"_s);
    return SubstituteData(SharedBuffer::create(encodedSrcdoc.span()), URL(), WTFMove(response), iframeElement->srcdocSessionHistoryVisibility());
}

void FrameLoader::load(FrameLoadRequest&& request)
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_LOAD_FRAMELOADREQUEST);

    m_errorOccurredInLoading = false;

    if (m_inStopAllLoaders || m_inClearProvisionalLoadForPolicyCheck)
        return;

    if (!request.frameName().isEmpty()) {
        if (RefPtr frame = dynamicDowncast<LocalFrame>(findFrameForNavigation(request.frameName()))) {
            request.setShouldCheckNewWindowPolicy(false);
            if (&frame->loader() != this) {
                frame->loader().load(WTFMove(request));
                return;
            }
        }
    }

    m_provisionalLoadHappeningInAnotherProcess = false;

    if (request.shouldCheckNewWindowPolicy()) {
        NavigationAction action { request.requester(), request.resourceRequest(), InitiatedByMainFrame::Unknown, request.isRequestFromClientOrUserInput(), NavigationType::Other, request.shouldOpenExternalURLsPolicy() };
        action.setNewFrameOpenerPolicy(request.newFrameOpenerPolicy());
        policyChecker().checkNewWindowPolicy(WTFMove(action), WTFMove(request.resourceRequest()), { }, request.frameName(), [this, protectedThis = Ref { *this }] (ResourceRequest&& request, WeakPtr<FormState>&& weakFormState, const AtomString& frameName, const NavigationAction& action, ShouldContinuePolicyCheck shouldContinue) {
            continueLoadAfterNewWindowPolicy(WTFMove(request), RefPtr { weakFormState.get() }.get(), frameName, action, shouldContinue, AllowNavigationToInvalidURL::Yes, NewFrameOpenerPolicy::Suppress);
        });

        return;
    }

    if (!request.hasSubstituteData())
        request.setSubstituteData(defaultSubstituteDataForURL(request.resourceRequest().url()));

    Ref loader = m_client->createDocumentLoader(request.takeResourceRequest(), request.takeSubstituteData());
    loader->setIsContentRuleListRedirect(request.isContentRuleListRedirect());
    loader->setIsRequestFromClientOrUserInput(request.isRequestFromClientOrUserInput());
    loader->setIsContinuingLoadAfterProvisionalLoadStarted(request.shouldTreatAsContinuingLoad() == ShouldTreatAsContinuingLoad::YesAfterProvisionalLoadStarted);
    if (auto advancedPrivacyProtections = request.advancedPrivacyProtections())
        loader->setOriginatorAdvancedPrivacyProtections(*advancedPrivacyProtections);
    addSameSiteInfoToRequestIfNeeded(loader->request());
    applyShouldOpenExternalURLsPolicyToNewDocumentLoader(protectedFrame(), loader, request);
    loader->setIsHandledByAboutSchemeHandler(request.isHandledByAboutSchemeHandler());

    if (request.shouldTreatAsContinuingLoad() != ShouldTreatAsContinuingLoad::No) {
        loader->setClientRedirectSourceForHistory(request.clientRedirectSourceForHistory());
        if (request.lockBackForwardList() == LockBackForwardList::Yes) {
            loader->setIsClientRedirect(true);
            m_loadType = FrameLoadType::RedirectWithLockedBackForwardList;
        }
    }

    SetForScope continuingLoadGuard(m_currentLoadContinuingState, request.shouldTreatAsContinuingLoad() != ShouldTreatAsContinuingLoad::No ? LoadContinuingState::ContinuingWithRequest : LoadContinuingState::NotContinuing);
    load(loader.get(), request.protectedRequesterSecurityOrigin().ptr());
}

void FrameLoader::loadWithNavigationAction(ResourceRequest&& request, NavigationAction&& action, FrameLoadType type, RefPtr<FormState>&& formState, AllowNavigationToInvalidURL allowNavigationToInvalidURL, ShouldTreatAsContinuingLoad shouldTreatAsContinuingLoad, CompletionHandler<void()>&& completionHandler)
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_LOADWITHNAVIGATIONACTION);

    m_errorOccurredInLoading = false;
    if (request.url().protocolIsJavaScript() && !action.isInitialFrameSrcLoad()) {
        if (auto requester = action.requester(); requester && requester->documentIdentifier) {
            if (RefPtr requestingDocument = Document::allDocumentsMap().get(requester->documentIdentifier); requestingDocument && requestingDocument->contentSecurityPolicy()) {
                if (!requestingDocument->contentSecurityPolicy()->allowJavaScriptURLs(protectedFrame()->document()->url().string(), { }, request.url().string(), nullptr))
                    return completionHandler();
            }
        }
        executeJavaScriptURL(request.url(), action);
        return completionHandler();
    }

    auto&& substituteData = defaultSubstituteDataForURL(request.url());
    Ref loader = m_client->createDocumentLoader(WTFMove(request), WTFMove(substituteData));
    applyShouldOpenExternalURLsPolicyToNewDocumentLoader(protectedFrame(), loader, action.initiatedByMainFrame(), action.shouldOpenExternalURLsPolicy());
    loader->setIsContinuingLoadAfterProvisionalLoadStarted(shouldTreatAsContinuingLoad == ShouldTreatAsContinuingLoad::YesAfterProvisionalLoadStarted);
    loader->setIsRequestFromClientOrUserInput(action.isRequestFromClientOrUserInput());

    if (action.lockHistory() == LockHistory::Yes) {
        if (RefPtr documentLoader = m_documentLoader)
            loader->setClientRedirectSourceForHistory(documentLoader->didCreateGlobalHistoryEntry() ? documentLoader->urlForHistory().string() : documentLoader->clientRedirectSourceForHistory());
    }

    loader->setTriggeringAction(WTFMove(action));
    if (m_documentLoader)
        loader->setOverrideEncoding(m_documentLoader->overrideEncoding());

    loadWithDocumentLoader(loader.ptr(), type, WTFMove(formState), allowNavigationToInvalidURL, WTFMove(completionHandler));
}

void FrameLoader::load(DocumentLoader& newDocumentLoader, const SecurityOrigin* requesterOrigin)
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_LOAD_DOCUMENTLOADER);

    m_errorOccurredInLoading = false;

    ResourceRequest& r = newDocumentLoader.request();
    // FIXME: Using m_loadType seems wrong here.
    // If we are only preparing to load the main resource, that is previous load's load type!
    updateRequestAndAddExtraFields(r, IsMainResource::Yes, m_loadType, ShouldUpdateAppInitiatedValue::No);
    FrameLoadType type;

    if (shouldTreatURLAsSameAsCurrent(requesterOrigin, newDocumentLoader.originalRequest().url())) {
        r.setCachePolicy(ResourceRequestCachePolicy::ReloadIgnoringCacheData);
        type = FrameLoadType::Same;
    } else if (shouldTreatURLAsSameAsCurrent(requesterOrigin, newDocumentLoader.unreachableURL()) && isReload(m_loadType))
        type = m_loadType;
    else if (m_loadType == FrameLoadType::RedirectWithLockedBackForwardList && ((!newDocumentLoader.unreachableURL().isEmpty() && newDocumentLoader.substituteData().isValid()) || shouldTreatCurrentLoadAsContinuingLoad()))
        type = FrameLoadType::RedirectWithLockedBackForwardList;
    else
        type = FrameLoadType::Standard;

    if (m_documentLoader)
        newDocumentLoader.setOverrideEncoding(m_documentLoader->overrideEncoding());
    
    // When we loading alternate content for an unreachable URL that we're
    // visiting in the history list, we treat it as a reload so the history list 
    // is appropriately maintained.
    //
    // FIXME: This seems like a dangerous overloading of the meaning of "FrameLoadType::Reload" ...
    // shouldn't a more explicit type of reload be defined, that means roughly 
    // "load without affecting history" ? 
    if (shouldReloadToHandleUnreachableURL(newDocumentLoader)) {
        // shouldReloadToHandleUnreachableURL returns true only when the original load type is back-forward.
        // In this case we should save the document state now. Otherwise the state can be lost because load type is
        // changed and updateForBackForwardNavigation() will not be called when loading is committed.
        history().saveDocumentAndScrollState();

        ASSERT(type == FrameLoadType::Standard);
        type = FrameLoadType::Reload;
    }

    loadWithDocumentLoader(&newDocumentLoader, type, nullptr, AllowNavigationToInvalidURL::Yes);
}

void FrameLoader::loadWithDocumentLoader(DocumentLoader* loader, FrameLoadType type, RefPtr<FormState>&& formState, AllowNavigationToInvalidURL allowNavigationToInvalidURL, CompletionHandler<void()>&& completionHandler)
{
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_LOADWITHDOCUMENTLOADER_FRAME_LOAD_STARTED);

    m_errorOccurredInLoading = false;

    Ref frame = m_frame.get();

    CompletionHandlerCallingScope completionHandlerCaller(WTFMove(completionHandler));

    ASSERT(m_client->hasWebView());

    // Unfortunately the view must be non-nil, this is ultimately due
    // to parser requiring a FrameView.  We should fix this dependency.

    ASSERT(frame->view());

    if (!isNavigationAllowed())
        return;

    if (RefPtr page = frame->page(); page && page->isInSwipeAnimation())
        loader->setLoadStartedDuringSwipeAnimation();

    if (frame->document())
        m_previousURL = frame->document()->url();

    const URL& newURL = loader->request().url();

    // Only the first iframe navigation or the first iframe navigation after about:blank should be reported.
    // https://www.w3.org/TR/resource-timing-2/#resources-included-in-the-performanceresourcetiming-interface
    if (m_shouldReportResourceTimingToParentFrame && !m_previousURL.isNull() && m_previousURL != aboutBlankURL())
        m_shouldReportResourceTimingToParentFrame = false;

    // Log main frame navigation types.
    if (frame->isMainFrame()) {
        if (RefPtr page = frame->page()) {
            FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_LOADWITHDOCUMENTLOADER_MAIN_FRAME_LOAD_STARTED);
            page->mainFrameLoadStarted(newURL, type);
            page->performanceLogging().didReachPointOfInterest(PerformanceLogging::MainFrameLoadStarted);
        }
    }

    policyChecker().setLoadType(type);
    RELEASE_ASSERT(!isBackForwardLoadType(type) || history().provisionalItem());
    bool isFormSubmission = formState;

    const String& httpMethod = loader->request().httpMethod();

    if (shouldPerformFragmentNavigation(isFormSubmission, httpMethod, policyChecker().loadType(), newURL)) {

        RefPtr oldDocumentLoader = m_documentLoader;
        NavigationAction action { frame->protectedDocument().releaseNonNull(), loader->request(), InitiatedByMainFrame::Unknown, loader->isRequestFromClientOrUserInput(), policyChecker().loadType(), isFormSubmission };
        action.setNavigationAPIType(determineNavigationType(type, NavigationHistoryBehavior::Auto));
        oldDocumentLoader->setTriggeringAction(WTFMove(action));
        oldDocumentLoader->setLastCheckedRequest(ResourceRequest());
        policyChecker().stopCheck();
        RELEASE_ASSERT(!isBackForwardLoadType(policyChecker().loadType()) || history().provisionalItem());
        RefPtr<SecurityOrigin> requesterOrigin;
        if (auto& requester = loader->triggeringAction().requester())
            requesterOrigin = requester->securityOrigin.copyRef();
        policyChecker().checkNavigationPolicy(ResourceRequest(loader->request()), ResourceResponse { }  /* redirectResponse */, oldDocumentLoader.get(), WTFMove(formState), [this, protectedThis = Ref { *this }, requesterOrigin = WTFMove(requesterOrigin)] (const ResourceRequest& request, WeakPtr<FormState>&&, NavigationPolicyDecision navigationPolicyDecision) {
            continueFragmentScrollAfterNavigationPolicy(request, requesterOrigin.get(), navigationPolicyDecision == NavigationPolicyDecision::ContinueLoad, NavigationHistoryBehavior::Auto);
        }, PolicyDecisionMode::Synchronous);
        return;
    }

    if (RefPtr parent = dynamicDowncast<LocalFrame>(frame->tree().parent()))
        loader->setOverrideEncoding(parent->loader().documentLoader()->overrideEncoding());

    policyChecker().stopCheck();
    setPolicyDocumentLoader(loader);
    if (loader->triggeringAction().isEmpty()) {
        NavigationAction action { frame->protectedDocument().releaseNonNull(), loader->request(), InitiatedByMainFrame::Unknown, loader->isRequestFromClientOrUserInput(), policyChecker().loadType(), isFormSubmission };
        action.setIsContentRuleListRedirect(loader->isContentRuleListRedirect());
        action.setNavigationAPIType(determineNavigationType(type, NavigationHistoryBehavior::Auto));
        loader->setTriggeringAction(WTFMove(action));
    }

    frame->protectedNavigationScheduler()->cancel(NewLoadInProgress::Yes);

    if (shouldTreatCurrentLoadAsContinuingLoad()) {
        continueLoadAfterNavigationPolicy(loader->request(), formState.get(), NavigationPolicyDecision::ContinueLoad, allowNavigationToInvalidURL);
        return;
    }

    auto policyDecisionMode = loader->triggeringAction().isFromNavigationAPI() ? PolicyDecisionMode::Synchronous : PolicyDecisionMode::Asynchronous;
    RELEASE_ASSERT(!isBackForwardLoadType(policyChecker().loadType()) || history().provisionalItem());
    policyChecker().checkNavigationPolicy(ResourceRequest(loader->request()), ResourceResponse { } /* redirectResponse */, loader, WTFMove(formState), [this, protectedThis = Ref { *this }, allowNavigationToInvalidURL, completionHandler = completionHandlerCaller.release()] (const ResourceRequest& request, WeakPtr<FormState>&& weakFormState, NavigationPolicyDecision navigationPolicyDecision) mutable {
        continueLoadAfterNavigationPolicy(request, RefPtr { weakFormState.get() }.get(), navigationPolicyDecision, allowNavigationToInvalidURL);
        completionHandler();
    }, policyDecisionMode);
}

void FrameLoader::clearProvisionalLoadForPolicyCheck()
{
    if (!m_policyDocumentLoader || !m_provisionalDocumentLoader || m_inClearProvisionalLoadForPolicyCheck)
        return;

    SetForScope change(m_inClearProvisionalLoadForPolicyCheck, true);
    protectedProvisionalDocumentLoader()->stopLoading();
    FRAMELOADER_RELEASE_LOG(ResourceLoading, "clearProvisionalLoadForPolicyCheck: Clearing provisional document loader (m_provisionalDocumentLoader=%p)", m_provisionalDocumentLoader.get());
    setProvisionalDocumentLoader(nullptr);
}

bool FrameLoader::hasOpenedFrames() const
{
    return protectedFrame()->hasOpenedFrames();
}

void FrameLoader::reportLocalLoadFailed(LocalFrame* frame, const String& url)
{
    ASSERT(!url.isEmpty());
    if (!frame)
        return;

    frame->protectedDocument()->addConsoleMessage(MessageSource::Security, MessageLevel::Error, makeString("Not allowed to load local resource: "_s, url));
}

void FrameLoader::reportBlockedLoadFailed(LocalFrame& frame, const URL& url)
{
    ASSERT(!url.isEmpty());
    auto restrictedHostPort = isIPAddressDisallowed(url) ? makeString("host \""_s, url.host(), "\""_s) : makeString("port "_s, url.port().value());
    auto message = makeString("Not allowed to use restricted network "_s, restrictedHostPort, ": "_s, url.stringCenterEllipsizedToLength());
    frame.protectedDocument()->addConsoleMessage(MessageSource::Security, MessageLevel::Error, message);
}

bool FrameLoader::willLoadMediaElementURL(URL& url, Node& initiatorNode)
{
#if PLATFORM(IOS_FAMILY)
    // MobileStore depends on the iOS 4.0 era client delegate method because webView:resource:willSendRequest:redirectResponse:fromDataSource
    // doesn't let them tell when a load request is coming from a media element. See <rdar://problem/8266916> for more details.
    if (WTF::IOSApplication::isMobileStore())
        return m_client->shouldLoadMediaElementURL(url);
#endif

    ResourceRequest request(URL { url });
    request.setInspectorInitiatorNodeIdentifier(InspectorInstrumentation::identifierForNode(initiatorNode));
    if (m_documentLoader)
        request.setIsAppInitiated(m_documentLoader->lastNavigationWasAppInitiated());

    ResourceError error;
    auto identifier = requestFromDelegate(request, IsMainResourceLoad::No, error);
    notifier().sendRemainingDelegateMessages(protectedDocumentLoader().get(), IsMainResourceLoad::No, identifier, request, ResourceResponse(URL { url }, String(), -1, String()), nullptr, -1, -1, error);

    url = request.url();

    return error.isNull();
}

bool FrameLoader::shouldReloadToHandleUnreachableURL(DocumentLoader& docLoader)
{
    URL unreachableURL = docLoader.unreachableURL();

    if (unreachableURL.isEmpty())
        return false;

    if (!isBackForwardLoadType(policyChecker().loadType()))
        return false;

    // We only treat unreachableURLs specially during the delegate callbacks
    // for provisional load errors and navigation policy decisions. The former
    // case handles well-formed URLs that can't be loaded, and the latter
    // case handles malformed URLs and unknown schemes. Loading alternate content
    // at other times behaves like a standard load.
    if (policyChecker().delegateIsDecidingNavigationPolicy() || policyChecker().delegateIsHandlingUnimplementablePolicy())
        return m_policyDocumentLoader && unreachableURL == m_policyDocumentLoader->request().url();

    return unreachableURL == m_provisionalLoadErrorBeingHandledURL;
}

void FrameLoader::reloadWithOverrideEncoding(const String& encoding)
{
    RefPtr documentLoader = m_documentLoader;
    if (!documentLoader)
        return;

    FRAMELOADER_RELEASE_LOG(ResourceLoading, "reloadWithOverrideEncoding: frame load started");

    ResourceRequest request = documentLoader->request();
    URL unreachableURL = documentLoader->unreachableURL();
    if (!unreachableURL.isEmpty())
        request.setURL(WTFMove(unreachableURL));

    // FIXME: If the resource is a result of form submission and is not cached, the form will be silently resubmitted.
    // We should ask the user for confirmation in this case.
    request.setCachePolicy(ResourceRequestCachePolicy::ReturnCacheDataElseLoad);

    auto&& substituteData = defaultSubstituteDataForURL(request.url());
    Ref loader = m_client->createDocumentLoader(WTFMove(request), WTFMove(substituteData));
    applyShouldOpenExternalURLsPolicyToNewDocumentLoader(protectedFrame(), loader, InitiatedByMainFrame::Unknown, documentLoader->shouldOpenExternalURLsPolicyToPropagate());

    setPolicyDocumentLoader(loader.copyRef());

    loader->setOverrideEncoding(encoding);

    loadWithDocumentLoader(loader.ptr(), FrameLoadType::Reload, { }, AllowNavigationToInvalidURL::Yes);
}

void FrameLoader::reload(OptionSet<ReloadOption> options, bool isRequestFromClientOrUserInput)
{
    RefPtr documentLoader = m_documentLoader;
    if (!documentLoader)
        return;

    // If a window is created by javascript, its main frame can have an empty but non-nil URL.
    // Reloading in this case will lose the current contents (see 4151001).
    if (documentLoader->request().url().isEmpty())
        return;

    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_RELOAD);

    // Replace error-page URL with the URL we were trying to reach.
    ResourceRequest initialRequest = documentLoader->request();
    URL unreachableURL = documentLoader->unreachableURL();
    if (!unreachableURL.isEmpty())
        initialRequest.setURL(WTFMove(unreachableURL));

    // Create a new document loader for the reload, this will become m_documentLoader eventually,
    // but first it has to be the "policy" document loader, and then the "provisional" document loader.
    auto&& substituteData = defaultSubstituteDataForURL(initialRequest.url());
    Ref loader = m_client->createDocumentLoader(WTFMove(initialRequest), WTFMove(substituteData));
    loader->setIsRequestFromClientOrUserInput(documentLoader->isRequestFromClientOrUserInput() || isRequestFromClientOrUserInput);
    applyShouldOpenExternalURLsPolicyToNewDocumentLoader(protectedFrame(), loader, InitiatedByMainFrame::Unknown, documentLoader->shouldOpenExternalURLsPolicyToPropagate());

    loader->setContentExtensionEnablement({ options.contains(ReloadOption::DisableContentBlockers) ? ContentExtensionDefaultEnablement::Disabled : ContentExtensionDefaultEnablement::Enabled, { } });
    
    ResourceRequest& request = loader->request();

    // FIXME: We don't have a mechanism to revalidate the main resource without reloading at the moment.
    request.setCachePolicy(ResourceRequestCachePolicy::ReloadIgnoringCacheData);

    addSameSiteInfoToRequestIfNeeded(request);

    // If we're about to re-post, set up action so the application can warn the user.
    if (request.httpMethod() == "POST"_s)
        loader->setTriggeringAction({ m_frame->protectedDocument().releaseNonNull(), request, InitiatedByMainFrame::Unknown, loader->isRequestFromClientOrUserInput(), NavigationType::FormResubmitted });

    loader->setOverrideEncoding(documentLoader->overrideEncoding());

    auto frameLoadTypeForReloadOptions = [] (auto options) {
        if (options & ReloadOption::FromOrigin)
            return FrameLoadType::ReloadFromOrigin;
        if (options & ReloadOption::ExpiredOnly)
            return FrameLoadType::ReloadExpiredOnly;
        return FrameLoadType::Reload;
    };

    loadWithDocumentLoader(loader.ptr(), frameLoadTypeForReloadOptions(options), { }, AllowNavigationToInvalidURL::Yes);
}

void FrameLoader::stopAllLoaders(ClearProvisionalItem clearProvisionalItem, StopLoadingPolicy stopLoadingPolicy)
{
    if (m_frame->document() && m_frame->document()->backForwardCacheState() == Document::InBackForwardCache)
        return;

    if (stopLoadingPolicy == StopLoadingPolicy::PreventDuringUnloadEvents && !isStopLoadingAllowed())
        return;

    // If this method is called from within this method, infinite recursion can occur (3442218). Avoid this.
    if (m_inStopAllLoaders)
        return;

    // This method might dispatch events.
    RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(ScriptDisallowedScope::InMainThread::isScriptAllowed());

    // Calling stopLoading() on the provisional document loader can blow away
    // the frame from underneath.
    Ref frame = m_frame.get();

    m_inStopAllLoaders = true;

    policyChecker().stopCheck();

    // If no new load is in progress, we should clear the provisional item from history
    // before we call stopLoading.
    if (clearProvisionalItem == ClearProvisionalItem::Yes)
        history().setProvisionalItem(nullptr);

    for (RefPtr child = frame->tree().firstChild(); child; child = child->tree().nextSibling()) {
        if (RefPtr localChild = dynamicDowncast<LocalFrame>(child.get()))
            localChild->loader().stopAllLoaders(clearProvisionalItem);
    }

    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_STOPALLLOADERS, (uint64_t)m_provisionalDocumentLoader.get(), (uint64_t)m_documentLoader.get());

    if (RefPtr provisionalDocumentLoader = m_provisionalDocumentLoader)
        provisionalDocumentLoader->stopLoading();
    if (RefPtr documentLoader = m_documentLoader)
        documentLoader->stopLoading();
    if (frame->page() && !frame->page()->chrome().client().isSVGImageChromeClient())
        platformStrategies()->loaderStrategy()->browsingContextRemoved(frame);

    setProvisionalDocumentLoader(nullptr);

    m_inStopAllLoaders = false;    
}

void FrameLoader::stopForBackForwardCache()
{
    ASSERT(!m_inStopForBackForwardCache);
    SetForScope<bool> inStopForBackForwardCache { m_inStopForBackForwardCache, true };
    // Stop provisional loads in subframes (The one in the main frame is about to be committed).
    if (!m_frame->isMainFrame()) {
        if (RefPtr provisionalDocumentLoader = m_provisionalDocumentLoader)
            provisionalDocumentLoader->stopLoading();
        FRAMELOADER_RELEASE_LOG(ResourceLoading, "stopForBackForwardCache: Clearing provisional document loader (m_provisionalDocumentLoader=%p)", m_provisionalDocumentLoader.get());
        setProvisionalDocumentLoader(nullptr);
    }

    // Stop current loads.
    if (RefPtr documentLoader = m_documentLoader)
        documentLoader->stopLoading();

    for (RefPtr child = m_frame->tree().firstChild(); child; child = child->tree().nextSibling())
        child->stopForBackForwardCache();

    // We cancel pending navigations & policy checks *after* cancelling loads because cancelling loads might end up
    // running script, which could schedule new navigations.
    policyChecker().stopCheck();
    protectedFrame()->protectedNavigationScheduler()->cancel();
}

void FrameLoader::stopAllLoadersAndCheckCompleteness()
{
    stopAllLoaders();

    if (!m_checkTimer.isActive())
        return;

    m_checkTimer.stop();
    m_checkingLoadCompleteForDetachment = true;
    checkCompletenessNow();
    m_checkingLoadCompleteForDetachment = false;
}

void FrameLoader::stopForUserCancel(bool deferCheckLoadComplete)
{
    if (m_inStopForBackForwardCache)
        return;
    // Calling stopAllLoaders can cause the frame to be deallocated, including the frame loader.
    Ref frame = m_frame.get();

    stopAllLoaders();

    if (m_frame->document()->settings().navigationAPIEnabled()) {
        RefPtr window = m_frame->document()->window();
        window->protectedNavigation()->abortOngoingNavigationIfNeeded();
    }

#if PLATFORM(IOS_FAMILY)
    // Lay out immediately when stopping to immediately clear the old page if we just committed this one
    // but haven't laid out/painted yet.
    // FIXME: Is this behavior specific to iOS? Or should we expose a setting to toggle this behavior?
    if (frame->view() && !frame->view()->didFirstLayout())
        frame->protectedView()->layoutContext().layout();
#endif

    if (deferCheckLoadComplete)
        scheduleCheckLoadComplete();
    else if (frame->page())
        checkLoadComplete();
}

DocumentLoader* FrameLoader::activeDocumentLoader() const
{
    if (m_state == FrameState::Provisional)
        return m_provisionalDocumentLoader.get();
    return m_documentLoader.get();
}

RefPtr<DocumentLoader> FrameLoader::protectedActiveDocumentLoader() const
{
    return activeDocumentLoader();
}

bool FrameLoader::isLoading() const
{
    RefPtr documentLoader = activeDocumentLoader();
    return documentLoader && documentLoader->isLoading();
}

bool FrameLoader::frameHasLoaded() const
{
    return m_stateMachine.committedFirstRealDocumentLoad() || (m_provisionalDocumentLoader && !m_stateMachine.creatingInitialEmptyDocument()); 
}

void FrameLoader::setDocumentLoader(RefPtr<DocumentLoader>&& loader)
{
    if (loader == m_documentLoader)
        return;

    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_SETDOCUMENTLOADER, (uint64_t)loader.get(), (uint64_t)m_documentLoader.get());
    
    RELEASE_ASSERT(!loader || loader->frameLoader() == this);

    m_client->prepareForDataSourceReplacement();
    detachChildren();

    // detachChildren() can trigger this frame's unload event, and therefore
    // script can run and do just about anything. For example, an unload event that calls
    // document.write("") on its parent frame can lead to a recursive detachChildren()
    // invocation for this frame. In that case, we can end up at this point with a
    // loader that hasn't been deleted but has been detached from its frame. Such a
    // DocumentLoader has been sufficiently detached that we'll end up in an inconsistent
    // state if we try to use it.
    if (loader && !loader->frame())
        return;

    if (RefPtr documentLoader = m_documentLoader)
        documentLoader->detachFromFrame(LoadWillContinueInAnotherProcess::No);

    m_documentLoader = WTFMove(loader);
}

void FrameLoader::setPolicyDocumentLoader(RefPtr<DocumentLoader>&& loader, LoadWillContinueInAnotherProcess loadWillContinueInAnotherProcess)
{
    if (m_policyDocumentLoader == loader)
        return;

    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_SETPOLICYDOCUMENTLOADER, (uint64_t)loader.get(), (uint64_t)m_policyDocumentLoader.get());

    history().clearPolicyItem();

    if (loader)
        loader->attachToFrame(protectedFrame());

    if (RefPtr policyDocumentLoader = m_policyDocumentLoader; policyDocumentLoader
        && policyDocumentLoader != m_provisionalDocumentLoader
        && policyDocumentLoader != m_documentLoader) {
        policyDocumentLoader->detachFromFrame(loadWillContinueInAnotherProcess);
    }

    m_policyDocumentLoader = WTFMove(loader);
}

void FrameLoader::setProvisionalDocumentLoader(RefPtr<DocumentLoader>&& loader)
{
    if (m_provisionalDocumentLoader == loader)
        return;

    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_SETPROVISIONALDOCUMENTLOADER, (uint64_t)loader.get(), (uint64_t)m_provisionalDocumentLoader.get());

    ASSERT(!loader || !m_provisionalDocumentLoader);
    RELEASE_ASSERT(!loader || loader->frameLoader() == this);

    if (RefPtr provisionalDocumentLoader = m_provisionalDocumentLoader; provisionalDocumentLoader && provisionalDocumentLoader != m_documentLoader)
        provisionalDocumentLoader->detachFromFrame(LoadWillContinueInAnotherProcess::No);

    m_provisionalDocumentLoader = WTFMove(loader);
}

void FrameLoader::setState(FrameState newState)
{
    FrameState oldState = m_state;
    m_state = newState;
    
    if (newState == FrameState::Provisional)
        provisionalLoadStarted();
    else if (newState == FrameState::Complete) {
        frameLoadCompleted();
        if (RefPtr documentLoader = m_documentLoader)
            documentLoader->stopRecordingResponses();
        if (m_frame->isMainFrame() && oldState != newState) {
            FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_SETSTATE_MAIN_FRAME_LOAD_COMPLETED);
            protectedFrame()->protectedPage()->performanceLogging().didReachPointOfInterest(PerformanceLogging::MainFrameLoadCompleted);
        }
    }
}

void FrameLoader::clearProvisionalLoad()
{
    FRAMELOADER_RELEASE_LOG(ResourceLoading, "clearProvisionalLoad: Clearing provisional document loader (m_provisionalDocumentLoader=%p)", m_provisionalDocumentLoader.get());
    setProvisionalDocumentLoader(nullptr);
    if (CheckedPtr progressTracker = m_progressTracker.get())
        progressTracker->progressCompleted();
    setState(FrameState::Complete);
}

void FrameLoader::provisionalLoadFailedInAnotherProcess()
{
    m_provisionalLoadHappeningInAnotherProcess = false;
    if (RefPtr localParent = dynamicDowncast<LocalFrame>(m_frame->tree().parent()))
        localParent->loader().checkLoadComplete();
}

void FrameLoader::commitProvisionalLoad()
{
    RefPtr pdl = m_provisionalDocumentLoader;
    Ref frame = m_frame.get();

    std::unique_ptr<CachedPage> cachedPage;
    if (m_loadingFromCachedPage && history().provisionalItem())
        cachedPage = BackForwardCache::singleton().take(*history().protectedProvisionalItem(), frame->protectedPage().get());

    LOG(BackForwardCache, "WebCoreLoading frame %" PRIu64 ": About to commit provisional load from previous URL '%s' to new URL '%s' with cached page %p", m_frame->frameID().toUInt64(),
        frame->document() ? frame->document()->url().stringCenterEllipsizedToLength().utf8().data() : "",
        pdl ? pdl->url().stringCenterEllipsizedToLength().utf8().data() : "<no provisional DocumentLoader>", cachedPage.get());

    if (RefPtr document = m_frame->document()) {
        bool canTriggerCrossDocumentViewTransition = false;
        RefPtr<NavigationActivation> activation;
        if (pdl) {
            canTriggerCrossDocumentViewTransition = pdl->navigationCanTriggerCrossDocumentViewTransition(*document, !!cachedPage);

            RefPtr window = document->window();
            auto navigationAPIType = pdl->triggeringAction().navigationAPIType();
            if (window && navigationAPIType) {
                // FIXME: The NavigationActivation for pageswap should be created after the global
                // history update, but before the unload event (which might be delayed). Those steps
                // are currently intertwined, so this creates a fake/detached new history entry to
                // use for this purpose.
                RefPtr<HistoryItem> newItem;
                if (RefPtr page = frame->page(); page && *navigationAPIType != NavigationNavigationType::Reload)
                    newItem = history().createItemWithLoader(page->historyItemClient(), pdl.get());

                activation = window->protectedNavigation()->createForPageswapEvent(newItem.get(), pdl.get(), !!cachedPage);
            }
        }
        document->dispatchPageswapEvent(canTriggerCrossDocumentViewTransition, WTFMove(activation));

        // https://html.spec.whatwg.org/multipage/browsing-the-web.html#deactivate-a-document-for-a-cross-document-navigation
        // FIXME: If the pageswap event resulted in starting a view-transition, then the
        // 'proceedWithNavigationAfterViewTransitionCapture' steps should proceed after the next
        // rendering update (which includes firing the unload event for the old Document).
    }

    if (RefPtr document = frame->document()) {
        // In the case where we're restoring from a cached page, our document will not
        // be connected to its frame yet, so the following call with be a no-op. We will
        // attempt to confirm any active composition once again in this scenario after we
        // finish restoring from the cached page.
        document->protectedEditor()->confirmOrCancelCompositionAndNotifyClient();
    }

    if (!frame->tree().parent() && history().currentItem() && (!history().provisionalItem() || history().currentItem()->itemID() != history().provisionalItem()->itemID())) {
        // Check to see if we need to cache the page we are navigating away from into the back/forward cache.
        // We are doing this here because we know for sure that a new page is about to be loaded.
        BackForwardCache::singleton().addIfCacheable(*history().protectedCurrentItem(), frame->protectedPage().get());
        
        WebCore::jettisonExpensiveObjectsOnTopLevelNavigation();
    }

    if (m_loadType != FrameLoadType::Replace)
        closeOldDataSources();

    if (!cachedPage && !m_stateMachine.creatingInitialEmptyDocument())
        m_client->makeRepresentation(pdl.get());

    transitionToCommitted(cachedPage.get());

    if (pdl && m_documentLoader) {
        // Check if the destination page is allowed to access the previous page's timing information.
        Ref securityOrigin = SecurityOrigin::create(pdl->request().url());
        protectedDocumentLoader()->timing().setHasSameOriginAsPreviousDocument(securityOrigin->canRequest(m_previousURL, OriginAccessPatternsForWebProcess::singleton()));
    }

    // Call clientRedirectCancelledOrFinished() here so that the frame load delegate is notified that the redirect's
    // status has changed, if there was a redirect.  The frame load delegate may have saved some state about
    // the redirect in its -webView:willPerformClientRedirectToURL:delay:fireDate:forFrame:.  Since we are
    // just about to commit a new page, there cannot possibly be a pending redirect at this point.
    if (m_sentRedirectNotification)
        clientRedirectCancelledOrFinished(NewLoadInProgress::No);
    
    if (cachedPage && cachedPage->document()) {
#if PLATFORM(IOS_FAMILY)
        // FIXME: CachedPage::restore() would dispatch viewport change notification. However UIKit expects load
        // commit to happen before any changes to viewport arguments and dealing with this there is difficult.
        frame->protectedPage()->chrome().setDispatchViewportDataDidChangeSuppressed(true);
#endif
        willRestoreFromCachedPage();

        // Start request for the main resource and dispatch didReceiveResponse before the load is committed for
        // consistency with all other loads. See https://bugs.webkit.org/show_bug.cgi?id=150927.
        ResourceError mainResouceError;
        ResourceRequest mainResourceRequest(cachedPage->documentLoader()->request());
        auto mainResourceIdentifier = requestFromDelegate(mainResourceRequest, IsMainResourceLoad::Yes, mainResouceError);
        notifier().dispatchDidReceiveResponse(cachedPage->protectedDocumentLoader().get(), mainResourceIdentifier, cachedPage->documentLoader()->response());

        auto hasInsecureContent = cachedPage->cachedMainFrame()->hasInsecureContent();
        auto usedLegacyTLS = cachedPage->cachedMainFrame()->usedLegacyTLS();
        auto privateRelayed = cachedPage->cachedMainFrame()->wasPrivateRelayed();

        dispatchDidCommitLoad(hasInsecureContent, usedLegacyTLS, privateRelayed);

        // FIXME: This API should be turned around so that we ground CachedPage into the Page.
        RefPtr page = frame->page();
        cachedPage->restore(*page);

#if PLATFORM(IOS_FAMILY)
        page->chrome().setDispatchViewportDataDidChangeSuppressed(false);
#endif
        if (RefPtr framePage = frame->page()) {
#if PLATFORM(IOS_FAMILY)
            page->chrome().dispatchViewportPropertiesDidChange(framePage->viewportArguments());
#endif
            page->chrome().dispatchDisabledAdaptationsDidChange(framePage->disabledAdaptations());
        }

        if (RefPtr documentLoader = m_documentLoader) {
            auto& title = documentLoader->title();
            if (!title.string.isNull())
                m_client->dispatchDidReceiveTitle(title);

            // Send remaining notifications for the main resource.
            notifier().sendRemainingDelegateMessages(documentLoader.get(), IsMainResourceLoad::Yes, mainResourceIdentifier, mainResourceRequest, ResourceResponse(), nullptr, static_cast<int>(documentLoader->response().expectedContentLength()), 0, mainResouceError);
        }

        Vector<Ref<LocalFrame>> targetFrames;
        targetFrames.append(frame);
        for (RefPtr child = frame->tree().firstChild(); child; child = child->tree().traverseNext(frame.ptr())) {
            if (RefPtr localChild = dynamicDowncast<LocalFrame>(child))
                targetFrames.append(localChild.releaseNonNull());
        }

        for (auto& frame : targetFrames)
            frame->loader().checkCompleted();
    } else
        didOpenURL();

    if (RefPtr document = frame->document())
        document->protectedEditor()->confirmOrCancelCompositionAndNotifyClient();

IGNORE_GCC_WARNINGS_BEGIN("format-overflow")
    LOG(Loading, "WebCoreLoading frame %" PRIu64 ": Finished committing provisional load to URL %s", frame->frameID().toUInt64(),
        frame->document() ? frame->document()->url().stringCenterEllipsizedToLength().utf8().data() : "");
IGNORE_GCC_WARNINGS_END

    if (m_loadType == FrameLoadType::Standard && m_documentLoader && m_documentLoader->isClientRedirect())
        history().updateForClientRedirect();

    if (m_loadingFromCachedPage) {
        // Note, didReceiveDocType is expected to be called for cached pages. See <rdar://problem/5906758> for more details.
        if (RefPtr page = frame->page())
            page->chrome().didReceiveDocType(frame);
        frame->protectedDocument()->resume(ReasonForSuspension::BackForwardCache);

        // Force a layout to update view size and thereby update scrollbars.
#if PLATFORM(IOS_FAMILY)
        if (!m_client->forceLayoutOnRestoreFromBackForwardCache())
            frame->protectedView()->forceLayout();
#else
        frame->protectedView()->forceLayout();
#endif

        // Main resource delegates were already sent, so we skip the first response here.
        RefPtr documentLoader = m_documentLoader;
        unsigned responsesSize = documentLoader ? documentLoader->responses().size() : 0;
        for (unsigned i = 1; i < responsesSize; ++i) {
            const auto& response = documentLoader->responses()[i];
            // FIXME: If the WebKit client changes or cancels the request, this is not respected.
            ResourceError error;
            ResourceRequest request(URL { response.url() });
            request.setIsAppInitiated(documentLoader->lastNavigationWasAppInitiated());
            auto identifier = requestFromDelegate(request, IsMainResourceLoad::Yes, error);
            // FIXME: If we get a resource with more than 2B bytes, this code won't do the right thing.
            // However, with today's computers and networking speeds, this won't happen in practice.
            // Could be an issue with a giant local file.
            notifier().sendRemainingDelegateMessages(documentLoader.get(), IsMainResourceLoad::Yes, identifier, request, response, nullptr, static_cast<int>(response.expectedContentLength()), 0, error);
        }

        // FIXME: Why only this frame and not parent frames?
        checkLoadCompleteForThisFrame(LoadWillContinueInAnotherProcess::No);
    }
}

void FrameLoader::transitionToCommitted(CachedPage* cachedPage)
{
    ASSERT(m_client->hasWebView());
    ASSERT(m_state == FrameState::Provisional);

    if (m_state != FrameState::Provisional)
        return;

    if (RefPtr view = m_frame->view()) {
        if (auto* scrollAnimator = view->existingScrollAnimator())
            scrollAnimator->cancelAnimations();
    }

    m_client->setCopiesOnScroll();
    history().updateForCommit();

    // The call to closeURL() invokes the unload event handler, which can execute arbitrary
    // JavaScript. If the script initiates a new load, we need to abandon the current load,
    // or the two will stomp each other.
    RefPtr originalProvisionalDocumentLoader = m_provisionalDocumentLoader;
    if (m_documentLoader)
        closeURL();
    if (originalProvisionalDocumentLoader != m_provisionalDocumentLoader)
        return;

    if (RefPtr documentLoader = m_documentLoader)
        documentLoader->stopLoadingSubresources();
    if (RefPtr documentLoader = m_documentLoader)
        documentLoader->stopLoadingPlugIns();

    // Setting our document loader invokes the unload event handler of our child frames.
    // Script can do anything. If the script initiates a new load, we need to abandon the
    // current load or the two will stomp each other.
    setDocumentLoader(m_provisionalDocumentLoader.copyRef());
    if (originalProvisionalDocumentLoader != m_provisionalDocumentLoader)
        return;
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_TRANSITIONTOCOMMITTED, (uint64_t)m_provisionalDocumentLoader.get());
    setProvisionalDocumentLoader(nullptr);

    // Nothing else can interrupt this commit - set the Provisional->Committed transition in stone
    setState(FrameState::CommittedPage);

    // Handle adding the URL to the back/forward list.
    RefPtr documentLoader = m_documentLoader;

    switch (m_loadType) {
    case FrameLoadType::Forward:
    case FrameLoadType::Back:
    case FrameLoadType::IndexedBackForward:
        if (m_frame->page()) {
            // If the first load within a frame is a navigation within a back/forward list that was attached
            // without any of the items being loaded then we need to update the history in a similar manner as
            // for a standard load with the exception of updating the back/forward list (<rdar://problem/8091103>).
            if (!m_stateMachine.committedFirstRealDocumentLoad() && m_frame->isMainFrame())
                history().updateForStandardLoad(HistoryController::UpdateAllExceptBackForwardList);

            history().updateForBackForwardNavigation();

            // Create a document view for this document, or used the cached view.
            if (cachedPage) {
                ASSERT(cachedPage->documentLoader());
                cachedPage->protectedDocumentLoader()->attachToFrame(protectedFrame());
                m_client->transitionToCommittedFromCachedFrame(cachedPage->cachedMainFrame());
            } else
                m_client->transitionToCommittedForNewPage(m_documentLoader && m_documentLoader->isInFinishedLoadingOfEmptyDocument() ?
                    LocalFrameLoaderClient::InitializingIframe::Yes : LocalFrameLoaderClient::InitializingIframe::No);
        }
        break;

    case FrameLoadType::Reload:
    case FrameLoadType::ReloadFromOrigin:
    case FrameLoadType::ReloadExpiredOnly:
    case FrameLoadType::Same:
    case FrameLoadType::Replace:
        history().updateForReload();
        m_client->transitionToCommittedForNewPage(m_documentLoader && m_documentLoader->isInFinishedLoadingOfEmptyDocument() ?
            LocalFrameLoaderClient::InitializingIframe::Yes : LocalFrameLoaderClient::InitializingIframe::No);
        break;

    case FrameLoadType::Standard:
        history().updateForStandardLoad();
        if (RefPtr view = m_frame->view())
            view->setScrollbarsSuppressed(true);
        m_client->transitionToCommittedForNewPage(m_documentLoader && m_documentLoader->isInFinishedLoadingOfEmptyDocument() ?
            LocalFrameLoaderClient::InitializingIframe::Yes : LocalFrameLoaderClient::InitializingIframe::No);
        break;

    case FrameLoadType::RedirectWithLockedBackForwardList:
        history().updateForRedirectWithLockedBackForwardList();
        m_client->transitionToCommittedForNewPage(m_documentLoader && m_documentLoader->isInFinishedLoadingOfEmptyDocument() ?
            LocalFrameLoaderClient::InitializingIframe::Yes : LocalFrameLoaderClient::InitializingIframe::No);
        break;
    }

    if (documentLoader)
        documentLoader->writer().setMIMEType(documentLoader->responseMIMEType());

    if (m_stateMachine.creatingInitialEmptyDocument())
        return;

    if (!m_stateMachine.committedFirstRealDocumentLoad())
        m_stateMachine.advanceTo(FrameLoaderStateMachine::DisplayingInitialEmptyDocumentPostCommit);
}

void FrameLoader::clientRedirectCancelledOrFinished(NewLoadInProgress newLoadInProgress)
{
    // Note that -webView:didCancelClientRedirectForFrame: is called on the frame load delegate even if
    // the redirect succeeded.  We should either rename this API, or add a new method, like
    // -webView:didFinishClientRedirectForFrame:
    m_client->dispatchDidCancelClientRedirect();

    if (newLoadInProgress == NewLoadInProgress::No)
        m_quickRedirectComing = false;

    m_sentRedirectNotification = false;
}

void FrameLoader::clientRedirected(const URL& url, double seconds, WallTime fireDate, LockBackForwardList lockBackForwardList)
{
    m_client->dispatchWillPerformClientRedirect(url, seconds, fireDate, lockBackForwardList);
    
    // Remember that we sent a redirect notification to the frame load delegate so that when we commit
    // the next provisional load, we can send a corresponding -webView:didCancelClientRedirectForFrame:
    m_sentRedirectNotification = true;
    
    // If a "quick" redirect comes in, we set a special mode so we treat the next
    // load as part of the original navigation. If we don't have a document loader, we have
    // no "original" load on which to base a redirect, so we treat the redirect as a normal load.
    // Loads triggered by JavaScript form submissions never count as quick redirects.
    m_quickRedirectComing = (lockBackForwardList == LockBackForwardList::Yes || history().currentItemShouldBeReplaced()) && m_documentLoader && !m_isExecutingJavaScriptFormAction;
}

bool FrameLoader::shouldReload(const URL& currentURL, const URL& destinationURL)
{
    // This function implements the rule: "Don't reload if navigating by fragment within
    // the same URL, but do reload if going to a new URL or to the same URL with no
    // fragment identifier at all."
    if (!destinationURL.hasFragmentIdentifier())
        return true;
    return !equalIgnoringFragmentIdentifier(currentURL, destinationURL);
}

void FrameLoader::closeOldDataSources()
{
    // FIXME: Is it important for this traversal to be postorder instead of preorder?
    // If so, add helpers for postorder traversal, and use them. If not, then lets not
    // use a recursive algorithm here.
    for (RefPtr child = m_frame->tree().firstChild(); child; child = child->tree().nextSibling()) {
        if (RefPtr localChild = dynamicDowncast<LocalFrame>(child))
            localChild->loader().closeOldDataSources();
    }
    
    if (m_documentLoader)
        m_client->dispatchWillClose();

    m_client->setMainFrameDocumentReady(false); // stop giving out the actual DOMDocument to observers
}

void FrameLoader::willRestoreFromCachedPage()
{
    ASSERT(!m_frame->tree().parent());
    ASSERT(m_frame->page());
    ASSERT(m_frame->isMainFrame());

    protectedFrame()->protectedNavigationScheduler()->cancel();

    // We still have to close the previous part page.
    closeURL();
}

void FrameLoader::open(CachedFrameBase& cachedFrame)
{
    // Don't re-emit the load event.
    m_didCallImplicitClose = true;

    URL url = cachedFrame.url();

    // FIXME: I suspect this block of code doesn't do anything.
    if (url.protocolIsInHTTPFamily() && !url.host().isEmpty() && url.path().isEmpty())
        url.setPath("/"_s);

    started();
    Ref document = *cachedFrame.document();
    ASSERT(document->window());

    clear(document.ptr(), true, true, cachedFrame.isMainFrame());

    document->attachToCachedFrame(cachedFrame);
    document->setBackForwardCacheState(Document::NotInBackForwardCache);

    m_needsClear = true;
    m_isComplete = false;
    m_didCallImplicitClose = false;
    setOutgoingReferrer(url);

    RefPtr view = cachedFrame.view();
    
    // When navigating to a CachedFrame its FrameView should never be null.  If it is we'll crash in creative ways downstream.
    ASSERT(view);
    if (RefPtr localView = dynamicDowncast<LocalFrameView>(view.get()))
        localView->setLastUserScrollType(std::nullopt);

    Ref frame = m_frame.get();
    std::optional<IntRect> previousViewFrameRect = frame->view() ?  frame->protectedView()->frameRect() : std::optional<IntRect>(std::nullopt);
    if (RefPtr localView = dynamicDowncast<LocalFrameView>(view.get()))
        frame->setView(localView.releaseNonNull());

    // Use the previous ScrollView's frame rect.
    if (previousViewFrameRect)
        view->setFrameRect(previousViewFrameRect.value());

    // Setting the document builds the render tree and runs post style resolution callbacks that can do anything,
    // including loading a child frame before its been re-attached to the frame tree as part of this restore.
    // For example, the HTML object element may load its content into a frame in a post style resolution callback.
    Style::PostResolutionCallbackDisabler disabler(document.get());
    WidgetHierarchyUpdatesSuspensionScope suspendWidgetHierarchyUpdates;
    NavigationDisabler disableNavigation { frame.ptr() };
    
    frame->setDocument(document.copyRef());

    document->protectedWindow()->resumeFromBackForwardCache();

    updateFirstPartyForCookies();

    cachedFrame.restore();
}

bool FrameLoader::isReplacing() const
{
    return m_loadType == FrameLoadType::Replace;
}

void FrameLoader::setReplacing()
{
    m_loadType = FrameLoadType::Replace;
}

bool FrameLoader::subframeIsLoading() const
{
    // It's most likely that the last added frame is the last to load so we walk backwards.
    for (RefPtr child = m_frame->tree().lastChild(); child; child = child->tree().previousSibling()) {
        RefPtr localChild = dynamicDowncast<LocalFrame>(*child);
        if (!localChild) {
            if (child->preventsParentFromBeingComplete())
                return true;
            continue;
        }
        Ref childLoader = localChild->loader();
        RefPtr documentLoader = childLoader->documentLoader();
        if (documentLoader && documentLoader->isLoadingInAPISense())
            return true;
        documentLoader = childLoader->provisionalDocumentLoader();
        if (childLoader->m_provisionalLoadHappeningInAnotherProcess)
            return true;
        if (documentLoader && documentLoader->isLoadingInAPISense())
            return true;
        documentLoader = childLoader->policyDocumentLoader();
        if (documentLoader)
            return true;
    }
    return false;
}

void FrameLoader::willChangeTitle(DocumentLoader* loader)
{
    m_client->willChangeTitle(loader);
}

FrameLoadType FrameLoader::loadType() const
{
    return m_loadType;
}
    
CachePolicy FrameLoader::subresourceCachePolicy(const URL& url) const
{
    if (RefPtr page = m_frame->page()) {
        if (page->isResourceCachingDisabledByWebInspector())
            return CachePolicy::Reload;
    }

    if (m_isComplete)
        return CachePolicy::Verify;

    if (m_loadType == FrameLoadType::ReloadFromOrigin)
        return CachePolicy::Reload;

    if (RefPtr parentFrame = dynamicDowncast<LocalFrame>(m_frame->tree().parent())) {
        CachePolicy parentCachePolicy = parentFrame->loader().subresourceCachePolicy(url);
        if (parentCachePolicy != CachePolicy::Verify)
            return parentCachePolicy;
    }
    
    switch (m_loadType) {
    case FrameLoadType::Reload:
        return CachePolicy::Revalidate;
    case FrameLoadType::Back:
    case FrameLoadType::Forward:
    case FrameLoadType::IndexedBackForward:
        return CachePolicy::HistoryBuffer;
    case FrameLoadType::ReloadFromOrigin:
        ASSERT_NOT_REACHED(); // Already handled above.
        return CachePolicy::Reload;
    case FrameLoadType::RedirectWithLockedBackForwardList:
    case FrameLoadType::Replace:
    case FrameLoadType::Same:
    case FrameLoadType::Standard:
        return CachePolicy::Verify;
    case FrameLoadType::ReloadExpiredOnly:
        // We know about expiration for HTTP and data. Do a normal reload otherwise.
        if (!url.protocolIsInHTTPFamily() && !url.protocolIsData())
            return CachePolicy::Reload;
        return CachePolicy::Verify;
    }

    RELEASE_ASSERT_NOT_REACHED();
    return CachePolicy::Verify;
}

void FrameLoader::dispatchDidFailProvisionalLoad(DocumentLoader& provisionalDocumentLoader, const ResourceError& error, WillInternallyHandleFailure willInternallyHandleFailure)
{
    m_provisionalLoadErrorBeingHandledURL = provisionalDocumentLoader.url();
    m_errorOccurredInLoading = true;

#if ENABLE(CONTENT_FILTERING)
    auto contentFilterWillContinueLoading = false;
#endif

    auto willContinueLoading = willInternallyHandleFailure == WillInternallyHandleFailure::Yes ? WillContinueLoading::Yes : WillContinueLoading::No;
    if (history().provisionalItem())
        willContinueLoading = WillContinueLoading::Yes;
#if ENABLE(CONTENT_FILTERING)
    if (provisionalDocumentLoader.contentFilterWillHandleProvisionalLoadFailure(error)) {
        willContinueLoading = WillContinueLoading::Yes;
        contentFilterWillContinueLoading = true;
    }
#endif

    m_client->dispatchDidFailProvisionalLoad(error, willContinueLoading, willInternallyHandleFailure);

#if ENABLE(CONTENT_FILTERING)
    if (contentFilterWillContinueLoading)
        provisionalDocumentLoader.contentFilterHandleProvisionalLoadFailure(error);
#endif

    m_provisionalLoadErrorBeingHandledURL = { };
}

void FrameLoader::checkLoadCompleteForThisFrame(LoadWillContinueInAnotherProcess loadWillContinueInAnotherProcess)
{
    ASSERT(m_client->hasWebView());

    // FIXME: Should this check be done in checkLoadComplete instead of here?
    // FIXME: Why does this one check need to be repeated here, and not the many others from checkCompleted?
    if (m_frame->document()->isDelayingLoadEvent())
        return;

    switch (m_state) {
    case FrameState::Provisional: {
        // FIXME: Prohibiting any provisional load failures from being sent to clients
        // while handling provisional load failures is too heavy. For example, the current
        // load will fail to cancel another ongoing load. That might prevent clients' page
        // load state being handled properly.
        if (!m_provisionalLoadErrorBeingHandledURL.isEmpty())
            return;

        RefPtr provisionalDocumentLoader = m_provisionalDocumentLoader;
        if (!provisionalDocumentLoader)
            return;

        // If we've received any errors we may be stuck in the provisional state and actually complete.
        auto& error = provisionalDocumentLoader->mainDocumentError();
        if (error.isNull())
            return;

        bool isHTTPSByDefaultEnabled { false };
        // Check all children first.
        RefPtr<HistoryItem> item;
        if (RefPtr page = m_frame->page()) {
            if (isBackForwardLoadType(loadType())) {
                // Reset the back forward list to the last committed history item at the top level.
                if (RefPtr localMainFrame = page->localMainFrame())
                    item = localMainFrame->loader().history().currentItem();
            }

            isHTTPSByDefaultEnabled = page->settings().httpsByDefault();
        }

        bool isHTTPSFirstApplicable = (isHTTPSByDefaultEnabled || provisionalDocumentLoader->httpsByDefaultMode() == HTTPSByDefaultMode::UpgradeWithAutomaticFallback)
            && provisionalDocumentLoader->httpsByDefaultMode() != HTTPSByDefaultMode::UpgradeWithUserMediatedFallback
            && !isHTTPFallbackInProgress()
            && provisionalDocumentLoader->request().wasSchemeOptimisticallyUpgraded();

        // Only reset if we aren't already going to a new provisional item.
        bool shouldReset = !history().provisionalItem();
        if (!provisionalDocumentLoader->isLoadingInAPISense() || provisionalDocumentLoader->isStopping()) {
            FRAMELOADER_RELEASE_LOG(ResourceLoading, "checkLoadCompleteForThisFrame: Failed provisional load (isTimeout = %d, isCancellation = %d, errorCode = %d, httpsFirstApplicable = %d)", error.isTimeout(), error.isCancellation(), error.errorCode(), isHTTPSFirstApplicable);

            if (loadWillContinueInAnotherProcess == LoadWillContinueInAnotherProcess::No) {
                auto willInternallyHandleFailure = (error.errorRecoveryMethod() == ResourceError::ErrorRecoveryMethod::NoRecovery || (error.errorRecoveryMethod() == ResourceError::ErrorRecoveryMethod::HTTPFallback && (!isHTTPSFirstApplicable || isHTTPFallbackInProgress()))) ? WillInternallyHandleFailure::No : WillInternallyHandleFailure::Yes;
                dispatchDidFailProvisionalLoad(*provisionalDocumentLoader, error, willInternallyHandleFailure);
            }

            ASSERT(!provisionalDocumentLoader->isLoading());

            // If we're in the middle of loading multipart data, we need to restore the document loader.
            if (isReplacing() && !m_documentLoader)
                setDocumentLoader(provisionalDocumentLoader.copyRef());

            // Finish resetting the load state, but only if another load hasn't been started by the
            // delegate callback.
            if (provisionalDocumentLoader == m_provisionalDocumentLoader)
                clearProvisionalLoad();
            else if (activeDocumentLoader()) {
                URL unreachableURL = activeDocumentLoader()->unreachableURL();
                if (!unreachableURL.isEmpty() && unreachableURL == provisionalDocumentLoader->request().url())
                    shouldReset = false;
            }
        }
        if (shouldReset && item) {
            if (RefPtr page = m_frame->page())
                page->checkedBackForward()->setCurrentItem(*item);
        }

        return;
    }
        
    case FrameState::CommittedPage: {
        RefPtr documentLoader = m_documentLoader;
        if (!documentLoader)
            return;
        if (documentLoader->isLoadingInAPISense() && !documentLoader->isStopping() && !m_checkingLoadCompleteForDetachment)
            return;

        setState(FrameState::Complete);

        // FIXME: Is this subsequent work important if we already navigated away?
        // Maybe there are bugs because of that, or extra work we can skip because
        // the new page is ready.

        m_client->forceLayoutForNonHTML();
             
        // If the user had a scroll point, scroll to it, overriding the anchor point if any.
        if (m_frame->page()) {
            if (isBackForwardLoadType(m_loadType) || isReload(m_loadType))
                history().restoreScrollPositionAndViewState();
        }

        if (m_stateMachine.creatingInitialEmptyDocument() || !m_stateMachine.committedFirstRealDocumentLoad())
            return;

        m_progressTracker->progressCompleted();
        if (RefPtr page = m_frame->page()) {
            if (m_frame->isMainFrame()) {
                tracePoint(MainResourceLoadDidEnd, PAGE_ID);
                page->didFinishLoad();
            }
        }

        if (RefPtr window = m_frame->document() ? m_frame->document()->window() : nullptr)
            window->protectedPerformance()->scheduleNavigationObservationTaskIfNeeded();

        auto& error = documentLoader->mainDocumentError();

        auto loadingEvent = AXLoadingEvent::Failed;
        if (!error.isNull()) {
            FRAMELOADER_RELEASE_LOG(ResourceLoading, "checkLoadCompleteForThisFrame: Finished frame load with error (isTimeout = %d, isCancellation = %d, errorCode = %d)", error.isTimeout(), error.isCancellation(), error.errorCode());
            m_client->dispatchDidFailLoad(error);
            loadingEvent = AXLoadingEvent::Failed;
            m_errorOccurredInLoading = true;
        } else {
            FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_CHECKLOADCOMPLETEFORTHISFRAME);
#if ENABLE(DATA_DETECTION)
            RefPtr document = m_frame->document();
            auto types = OptionSet<DataDetectorType>::fromRaw(enumToUnderlyingType(m_frame->settings().dataDetectorTypes()));

            if (document && types) {
                DataDetection::detectContentInFrame(protectedFrame().ptr(), types, m_client->dataDetectionReferenceDate(), [weakThis = WeakPtr { *this }](NSArray *results) {
                    RefPtr protectedThis = weakThis.get();
                    if (!protectedThis)
                        return;

                    Ref frame = protectedThis->frame();
                    frame->dataDetectionResults().setDocumentLevelResults(results);
                    if (frame->isMainFrame())
                        protectedThis->m_client->dispatchDidFinishDataDetection(results);
                });
            }
#endif
            m_client->dispatchDidFinishLoad();
            loadingEvent = AXLoadingEvent::Finished;
        }

        // Notify accessibility.
        if (RefPtr document = m_frame->document()) {
            if (CheckedPtr cache = document->existingAXObjectCache())
                cache->frameLoadingEventNotification(protectedFrame().ptr(), loadingEvent);
        }

        // The above calls to dispatchDidFinishLoad() might have detached the Frame
        // from its Page and also might have caused Page to be deleted.
        // Don't assume 'page' is still available to use.
        if (m_frame->isMainFrame() && m_frame->page()) {
            ASSERT(&m_frame->page()->mainFrame() == m_frame.ptr());
            protectedFrame()->protectedPage()->diagnosticLoggingClient().logDiagnosticMessageWithResult(DiagnosticLoggingKeys::pageLoadedKey(), emptyString(), error.isNull() ? DiagnosticLoggingResultPass : DiagnosticLoggingResultFail, ShouldSample::Yes);
        }

        m_shouldSkipHTTPSUpgradeForSameSiteNavigation = m_isHTTPFallbackInProgress;
        setHTTPFallbackInProgress(false);

        return;
    }
        
    case FrameState::Complete:
        m_loadType = FrameLoadType::Standard;
        frameLoadCompleted();
        return;
    }

    ASSERT_NOT_REACHED();
}

void FrameLoader::setOriginalURLForDownloadRequest(ResourceRequest& request)
{
    // FIXME: Rename firstPartyForCookies back to mainDocumentURL. It was a mistake to think that it was only used for cookies.
    // The originalURL is defined as the URL of the page where the download was initiated.
    URL originalURL;
    RefPtr initiator = m_frame->document();
    if (initiator) {
        originalURL = initiator->firstPartyForCookies();
        // If there is no main document URL, it means that this document is newly opened and just for download purpose.
        // In this case, we need to set the originalURL to this document's opener's main document URL.
        if (originalURL.isEmpty()) {
            if (RefPtr localOpener = dynamicDowncast<LocalFrame>(m_frame->opener()); localOpener && localOpener->document()) {
                originalURL = localOpener->document()->firstPartyForCookies();
                initiator = localOpener->document();
            }
        }
    }
    // If the originalURL is the same as the requested URL, we are processing a download
    // initiated directly without a page and do not need to specify the originalURL.
    if (originalURL == request.url())
        request.setFirstPartyForCookies(URL());
    else
        request.setFirstPartyForCookies(originalURL);
    addSameSiteInfoToRequestIfNeeded(request, initiator.get());
}

void FrameLoader::didReachLayoutMilestone(OptionSet<LayoutMilestone> milestones)
{
    ASSERT(m_frame->isMainFrame());

    m_client->dispatchDidReachLayoutMilestone(milestones);
}

void FrameLoader::didFirstLayout()
{
#if PLATFORM(IOS_FAMILY)
    // Only send layout-related delegate callbacks synchronously for the main frame to
    // avoid reentering layout for the main frame while delivering a layout-related delegate
    // callback for a subframe.
    if (m_frame.ptr() != &m_frame->page()->mainFrame())
        return;
#endif
    if (m_frame->page() && isBackForwardLoadType(m_loadType))
        restoreScrollPositionAndViewStateSoon();

    if (m_stateMachine.committedFirstRealDocumentLoad() && !m_stateMachine.isDisplayingInitialEmptyDocument() && !m_stateMachine.firstLayoutDone())
        m_stateMachine.advanceTo(FrameLoaderStateMachine::FirstLayoutDone);
}

void FrameLoader::restoreScrollPositionAndViewStateSoon()
{
    if (m_shouldRestoreScrollPositionAndViewState)
        return;
    m_shouldRestoreScrollPositionAndViewState = true;
    if (RefPtr document = m_frame->document())
        document->scheduleRenderingUpdate(RenderingUpdateStep::RestoreScrollPositionAndViewState);
}

static bool scrollingSuppressedByNavigationAPI(Document* document)
{
    if (!document || !document->settings().navigationAPIEnabled())
        return false;

    RefPtr window = document->window();
    return window && window->navigation().suppressNormalScrollRestoration();
}

void FrameLoader::restoreScrollPositionAndViewStateNowIfNeeded()
{
    if (!m_shouldRestoreScrollPositionAndViewState)
        return;
    m_shouldRestoreScrollPositionAndViewState = false;
    history().restoreScrollPositionAndViewState();
}

void FrameLoader::didReachVisuallyNonEmptyState()
{
    ASSERT(m_frame->isRootFrame());
    m_client->dispatchDidReachVisuallyNonEmptyState();
}

void FrameLoader::frameLoadCompleted()
{
    // Note: Can be called multiple times.

    m_client->frameLoadCompleted();

    history().updateForFrameLoadCompleted();

    // After a canceled provisional load, firstLayoutDone is false.
    // Reset it to true if we're displaying a page.
    if (m_documentLoader && m_stateMachine.committedFirstRealDocumentLoad() && !m_stateMachine.isDisplayingInitialEmptyDocument() && !m_stateMachine.firstLayoutDone())
        m_stateMachine.advanceTo(FrameLoaderStateMachine::FirstLayoutDone);
}

void FrameLoader::detachChildren()
{
    // detachChildren() will fire the unload event in each subframe and the
    // HTML specification states that the parent document's ignore-opens-during-unload counter while
    // this event is being fired in its subframes:
    // https://html.spec.whatwg.org/multipage/browsers.html#unload-a-document
    UnloadCountIncrementer UnloadCountIncrementer(m_frame->document());

    // detachChildren() will fire the unload event in each subframe and the
    // HTML specification states that navigations should be prevented during the prompt to unload algorithm:
    // https://html.spec.whatwg.org/multipage/browsing-the-web.html#navigate
    std::unique_ptr<NavigationDisabler> navigationDisabler;
    if (m_frame->isMainFrame())
        navigationDisabler = makeUnique<NavigationDisabler>(protectedFrame().ptr());

    // Any subframe inserted by unload event handlers executed in the loop below will not get unloaded
    // because we create a copy of the subframes list before looping. Therefore, it would be unsafe to
    // allow loading of subframes at this point.
    SubframeLoadingDisabler subframeLoadingDisabler(m_frame->protectedDocument().get());

    Vector<Ref<LocalFrame>, 16> childrenToDetach;
    childrenToDetach.reserveInitialCapacity(m_frame->tree().childCount());
    for (RefPtr child = m_frame->tree().lastChild(); child; child = child->tree().previousSibling()) {
        if (RefPtr localChild = dynamicDowncast<LocalFrame>(child))
            childrenToDetach.append(localChild.releaseNonNull());
    }
    for (auto& child : childrenToDetach)
        child->loader().detachFromParent();
}

void FrameLoader::closeAndRemoveChild(LocalFrame& child)
{
    child.tree().detachFromParent();

    child.setView(nullptr);
    child.willDetachPage();
    child.detachFromPage();

    protectedFrame()->tree().removeChild(child);
}

// Called every time a resource is completely loaded or an error is received.
void FrameLoader::checkLoadComplete(LoadWillContinueInAnotherProcess loadWillContinueInAnotherProcess)
{
    m_shouldCallCheckLoadComplete = false;

    if (!m_frame->page())
        return;

    ASSERT(m_client->hasWebView());
    
    // FIXME: Always traversing the entire frame tree is a bit inefficient, but 
    // is currently needed in order to null out the previous history item for all frames.
    Vector<Ref<LocalFrame>, 16> frames;
    for (RefPtr frame = m_frame->mainFrame(); frame; frame = frame->tree().traverseNext()) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(frame))
            frames.append(localFrame.releaseNonNull());
    }

    // Provisional frames that are not in the frame tree need to be included to report provisional load failures.
    if (m_frame->settings().siteIsolationEnabled()) {
        bool containsThisFrame = std::ranges::any_of(frames, [thisFrame = Ref { m_frame.get() }] (auto& frame) {
            return frame.ptr() == thisFrame.ptr();
        });
        if (!containsThisFrame)
            frames.append(m_frame);
    }

    // To process children before their parents, iterate the vector backwards.
    for (Ref frame : makeReversedRange(frames)) {
        if (frame->page())
            frame->loader().checkLoadCompleteForThisFrame(loadWillContinueInAnotherProcess);
    }
}

int FrameLoader::numPendingOrLoadingRequests(bool recurse) const
{
    Ref frame = m_frame.get();
    if (!recurse)
        return frame->protectedDocument()->cachedResourceLoader().requestCount();

    int count = 0;
    for (RefPtr<Frame> descendantFrame = frame.ptr(); descendantFrame; descendantFrame = descendantFrame->tree().traverseNext(frame.ptr())) {
        if (RefPtr localFrame = dynamicDowncast<LocalFrame>(descendantFrame))
            count += localFrame->protectedDocument()->cachedResourceLoader().requestCount();
    }
    return count;
}

String FrameLoader::userAgent(const URL& url) const
{
    String userAgent;
    if (RefPtr document = m_frame->document()) {
        if (auto userAgentQuirk = document->quirks().storageAccessUserAgentStringQuirkForDomain(url); !userAgentQuirk.isEmpty())
            userAgent = userAgentQuirk;
    }

    if (userAgent.isEmpty()) {
        Ref mainFrame = m_frame->mainFrame();
        if (m_frame->settings().needsSiteSpecificQuirks())
            userAgent = mainFrame->customUserAgentAsSiteSpecificQuirks();
        if (userAgent.isEmpty())
            userAgent = mainFrame->customUserAgent();
    }

    InspectorInstrumentation::applyUserAgentOverride(protectedFrame(), userAgent);

    if (userAgent.isEmpty() || m_client->hasCustomUserAgent())
        userAgent = m_client->userAgent(url);

    if (m_frame->settings().needsSiteSpecificQuirks()) {
        if (RefPtr document = m_frame->document()) {
            auto topFullURL = document->topURL();
            auto topFullURLPath = topFullURL.path();
            if (RegistrableDomain(topFullURL).string() == "easyjet.com"_s && topFullURLPath.contains("routemap"_s)) {
                auto urlDomainString = RegistrableDomain(url).string();
                if (urlDomainString == "bing.com") {
                    // FIXME: Move this to a proper UA override singular mechanism
                    // https://bugs.webkit.org/show_bug.cgi?id=274374
                    userAgent = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:135.0) Gecko/20100101 Firefox/135.0"_s;
                }
            }
        }
    }

    verifyUserAgent(userAgent);

    return userAgent;
}

String FrameLoader::navigatorPlatform() const
{
    auto customNavigatorPlatform = m_frame->protectedMainFrame()->customNavigatorPlatform();
    if (!customNavigatorPlatform.isEmpty())
        return customNavigatorPlatform;
    return String();
}

void FrameLoader::dispatchOnloadEvents()
{
    m_client->dispatchDidDispatchOnloadEvents();

    if (RefPtr documentLoader = this->documentLoader())
        documentLoader->dispatchOnloadEvents();
}

void FrameLoader::frameDetached()
{
    // Calling stopAllLoadersAndCheckCompleteness() can cause the frame to be deallocated, including the frame loader.
    Ref frame = m_frame.get();

    if (m_checkTimer.isActive()) {
        m_checkTimer.stop();
        checkCompletenessNow();
    }

    if (frame->document()->backForwardCacheState() != Document::InBackForwardCache)
        stopAllLoadersAndCheckCompleteness();

    detachFromParent();

    if (frame->document()->backForwardCacheState() != Document::InBackForwardCache)
        frame->protectedDocument()->stopActiveDOMObjects();
}

void FrameLoader::detachFromParent()
{
    // Calling stopAllLoaders() can cause the frame to be deallocated, including the frame loader.
    Ref frame = m_frame.get();

    closeURL();
    history().saveScrollPositionAndViewStateToItem(history().protectedCurrentItem().get());
    detachChildren();
    if (frame->document()->backForwardCacheState() != Document::InBackForwardCache) {
        // stopAllLoaders() needs to be called after detachChildren() if the document is not in the back/forward cache,
        // because detachedChildren() will trigger the unload event handlers of any child frames, and those event
        // handlers might start a new subresource load in this frame.
        stopAllLoaders(ClearProvisionalItem::Yes, StopLoadingPolicy::AlwaysStopLoading);
    }

    InspectorInstrumentation::frameDetachedFromParent(frame);

    detachViewsAndDocumentLoader();

    m_progressTracker = nullptr;

    if (RefPtr parent = dynamicDowncast<LocalFrame>(frame->tree().parent())) {
        Ref parentLoader = parent->loader();
        parentLoader->closeAndRemoveChild(frame);
        parentLoader->scheduleCheckCompleted();
        parentLoader->scheduleCheckLoadComplete();
    } else {
        if (RefPtr parent = frame->tree().parent())
            parent->tree().removeChild(frame);
        frame->setView(nullptr);
        frame->willDetachPage();
        frame->detachFromPage();
    }
}

void FrameLoader::detachViewsAndDocumentLoader()
{
    m_client->detachedFromParent2();
    setDocumentLoader(nullptr);
    m_client->detachedFromParent3();
}

ResourceRequestCachePolicy FrameLoader::defaultRequestCachingPolicy(const ResourceRequest& request, FrameLoadType loadType, bool isMainResource)
{
    if (m_overrideCachePolicyForTesting)
        return m_overrideCachePolicyForTesting.value();

    if (isMainResource) {
        if (isReload(loadType) || request.isConditional())
            return ResourceRequestCachePolicy::ReloadIgnoringCacheData;

        return ResourceRequestCachePolicy::UseProtocolCachePolicy;
    }

    if (request.isConditional())
        return ResourceRequestCachePolicy::ReloadIgnoringCacheData;

    RefPtr documentLoader = this->documentLoader();
    if (documentLoader && documentLoader->isLoadingInAPISense()) {
        // If we inherit cache policy from a main resource, we use the DocumentLoader's
        // original request cache policy for two reasons:
        // 1. For POST requests, we mutate the cache policy for the main resource,
        //    but we do not want this to apply to subresources
        // 2. Delegates that modify the cache policy using willSendRequest: should
        //    not affect any other resources. Such changes need to be done
        //    per request.
        ResourceRequestCachePolicy mainDocumentOriginalCachePolicy = documentLoader->originalRequest().cachePolicy();
        // Back-forward navigations try to load main resource from cache only to avoid re-submitting form data, and start over (with a warning dialog) if that fails.
        // This policy is set on initial request too, but should not be inherited.
        return (mainDocumentOriginalCachePolicy == ResourceRequestCachePolicy::ReturnCacheDataDontLoad) ? ResourceRequestCachePolicy::ReturnCacheDataElseLoad : mainDocumentOriginalCachePolicy;
    }

    return ResourceRequestCachePolicy::UseProtocolCachePolicy;
}

void FrameLoader::updateRequestAndAddExtraFields(ResourceRequest& request, IsMainResource mainResource, FrameLoadType loadType, ShouldUpdateAppInitiatedValue shouldUpdate, IsServiceWorkerNavigationLoad isServiceWorkerNavigationLoad, WillOpenInNewWindow willOpenInNewWindow, Document* initiator)
{
    updateRequestAndAddExtraFields(protectedFrame(), request, mainResource, loadType, shouldUpdate, isServiceWorkerNavigationLoad, willOpenInNewWindow, initiator);
}

void FrameLoader::updateRequestAndAddExtraFields(Frame& targetFrame, ResourceRequest& request, IsMainResource mainResource, FrameLoadType loadType, ShouldUpdateAppInitiatedValue shouldUpdate, IsServiceWorkerNavigationLoad isServiceWorkerNavigationLoad, WillOpenInNewWindow willOpenInNewWindow, Document* initiator)
{
    ASSERT(isServiceWorkerNavigationLoad == IsServiceWorkerNavigationLoad::No || mainResource != IsMainResource::Yes);

    // If the request came from a previous process due to process-swap-on-navigation then we should not modify the request.
    if (m_currentLoadContinuingState == LoadContinuingState::ContinuingWithRequest)
        return;

    auto* localFrame = dynamicDowncast<LocalFrame>(targetFrame);
    RefPtr document = localFrame ? localFrame->document() : nullptr;
    // Don't set the cookie policy URL if it's already been set.
    // But make sure to set it on all requests regardless of protocol, as it has significance beyond the cookie policy (<rdar://problem/6616664>).
    bool isMainResource = mainResource == IsMainResource::Yes;
    bool isMainFrameMainResource = isMainResource && (targetFrame.isMainFrame() || willOpenInNewWindow == WillOpenInNewWindow::Yes);
    if (request.firstPartyForCookies().isEmpty()) {
        if (isMainFrameMainResource)
            request.setFirstPartyForCookies(request.url());
        else if (document)
            request.setFirstPartyForCookies(document->firstPartyForCookies());
    }

    RefPtr page = targetFrame.page();
    if (request.isSameSiteUnspecified()) {
        RefPtr updatedInitiator = initiator;
        if (!updatedInitiator && document) {
            updatedInitiator = document.get();
            if (isMainResource) {
                RefPtr ownerFrame = dynamicDowncast<LocalFrame>(localFrame->tree().parent());
                if (!ownerFrame && m_stateMachine.isDisplayingInitialEmptyDocument()) {
                    if (RefPtr localOpener = dynamicDowncast<LocalFrame>(localFrame->opener()))
                        ownerFrame = WTFMove(localOpener);
                }
                if (ownerFrame)
                    updatedInitiator = ownerFrame->document();
                ASSERT(ownerFrame || localFrame->isMainFrame() || localFrame->settings().siteIsolationEnabled());
            }
        }
        addSameSiteInfoToRequestIfNeeded(request, updatedInitiator.get());
    }

    // In case of service worker navigation load, we inherit isTopSite from the FetchEvent request directly.
    if (isServiceWorkerNavigationLoad == IsServiceWorkerNavigationLoad::No)
        request.setIsTopSite(isMainFrameMainResource);

    bool hasSpecificCachePolicy = request.cachePolicy() != ResourceRequestCachePolicy::UseProtocolCachePolicy;
    if (page && page->isResourceCachingDisabledByWebInspector()) {
        request.setCachePolicy(ResourceRequestCachePolicy::ReloadIgnoringCacheData);
        loadType = FrameLoadType::ReloadFromOrigin;
    } else if (!hasSpecificCachePolicy)
        request.setCachePolicy(defaultRequestCachingPolicy(request, loadType, isMainResource));

    // The remaining modifications are only necessary for HTTP and HTTPS.
    if (!request.url().isEmpty() && !request.url().protocolIsInHTTPFamily())
        return;

    if (!hasSpecificCachePolicy && request.cachePolicy() == ResourceRequestCachePolicy::ReloadIgnoringCacheData) {
        if (loadType == FrameLoadType::Reload)
            request.setHTTPHeaderField(HTTPHeaderName::CacheControl, HTTPHeaderValues::maxAge0());
        else if (loadType == FrameLoadType::ReloadFromOrigin) {
            request.setHTTPHeaderField(HTTPHeaderName::CacheControl, HTTPHeaderValues::noCache());
            request.setHTTPHeaderField(HTTPHeaderName::Pragma, HTTPHeaderValues::noCache());
        }
    }

    if (m_overrideResourceLoadPriorityForTesting)
        request.setPriority(m_overrideResourceLoadPriorityForTesting.value());

    // Make sure we send the Origin header.
    addHTTPOriginIfNeeded(request, String());

    applyUserAgentIfNeeded(request);

    if (isMainResource)
        request.setHTTPHeaderField(HTTPHeaderName::Accept, CachedResourceRequest::acceptHeaderValueFromType(CachedResource::Type::MainResource, request.url().protocolIsSecure()));

    if (document && localFrame->settings().privateTokenUsageByThirdPartyEnabled() && !localFrame->loader().client().isRemoteWorkerFrameLoaderClient())
        request.setIsPrivateTokenUsageByThirdPartyAllowed(PermissionsPolicy::isFeatureEnabled(PermissionsPolicy::Feature::PrivateToken, *document, PermissionsPolicy::ShouldReportViolation::No));

    // Only set fallback array if it's still empty (later attempts may be incorrect, see bug 117818).
    if (document && request.responseContentDispositionEncodingFallbackArray().isEmpty()) {
        // Always try UTF-8. If that fails, try frame encoding (if any) and then the default.
        request.setResponseContentDispositionEncodingFallbackArray("UTF-8"_s, document->encoding(), localFrame->settings().defaultTextEncodingName());
    }

    if (RefPtr localMainFrame = dynamicDowncast<LocalFrame>(targetFrame.mainFrame())) {
        if (shouldUpdate == ShouldUpdateAppInitiatedValue::Yes) {
            if (RefPtr documentLoader = localMainFrame->loader().documentLoader())
                request.setIsAppInitiated(documentLoader->lastNavigationWasAppInitiated());
        }
    }

    if (page && isMainResource) {
        auto [filteredURL, didFilter] = page->chrome().client().applyLinkDecorationFilteringWithResult(request.url(), LinkDecorationFilteringTrigger::Navigation);
        request.setURL(WTFMove(filteredURL), didFilter == DidFilterLinkDecoration::Yes);
    }
}

void FrameLoader::scheduleRefreshIfNeeded(Document& document, const String& content, IsMetaRefresh isMetaRefresh)
{
    double delay = 0;
    String urlString;
    if (parseMetaHTTPEquivRefresh(content, delay, urlString)) {
        auto completedURL = urlString.isEmpty() ? document.url() : document.completeURL(urlString);
        if (!completedURL.protocolIsJavaScript())
            protectedFrame()->protectedNavigationScheduler()->scheduleRedirect(document, delay, WTFMove(completedURL), isMetaRefresh);
        else {
            auto message = makeString("Refused to refresh "_s, document.url().stringCenterEllipsizedToLength(), " to a javascript: URL"_s);
            document.addConsoleMessage(MessageSource::Security, MessageLevel::Error, message);
        }
    }
}

void FrameLoader::addHTTPOriginIfNeeded(ResourceRequest& request, const String& origin)
{
    if (!request.httpOrigin().isEmpty())
        return;  // Request already has an Origin header.

    // Don't send an Origin header for GET or HEAD to avoid privacy issues.
    // For example, if an intranet page has a hyperlink to an external web
    // site, we don't want to include the Origin of the request because it
    // will leak the internal host name. Similar privacy concerns have lead
    // to the widespread suppression of the Referer header at the network
    // layer.
    if (request.httpMethod() == "GET"_s || request.httpMethod() == "HEAD"_s)
        return;

    // FIXME: take referrer-policy into account.
    // https://bugs.webkit.org/show_bug.cgi?id=209066

    // For non-GET and non-HEAD methods, always send an Origin header so the
    // server knows we support this feature.

    if (origin.isEmpty()) {
        // If we don't know what origin header to attach, we attach the value
        // for an opaque origin.
        request.setHTTPOrigin(SecurityOrigin::createOpaque()->toString());
        return;
    }

    request.setHTTPOrigin(origin);
}

// Implements the "'Same-site' and 'cross-site' Requests" algorithm from <https://tools.ietf.org/html/draft-ietf-httpbis-cookie-same-site-00#section-2.1>.
// The algorithm is ammended to treat URLs that inherit their security origin from their owner (e.g. about:blank)
// as same-site. This matches the behavior of Chrome and Firefox.
void FrameLoader::addSameSiteInfoToRequestIfNeeded(ResourceRequest& request, const Document* initiator)
{
    if (!request.isSameSiteUnspecified())
        return;
    if (!initiator) {
        request.setIsSameSite(true);
        return;
    }
    if (SecurityPolicy::shouldInheritSecurityOriginFromOwner(request.url())) {
        request.setIsSameSite(true);
        return;
    }

    request.setIsSameSite(initiator->isSameSiteForCookies(request.url()));
}

Ref<const LocalFrameLoaderClient> FrameLoader::protectedClient() const
{
    return m_client.get();
}

Ref<LocalFrameLoaderClient> FrameLoader::protectedClient()
{
    return m_client.get();
}

void FrameLoader::loadPostRequest(FrameLoadRequest&& request, const String& referrer, FrameLoadType loadType, Event* event, RefPtr<FormState>&& formState, CompletionHandler<void()>&& completionHandler)
{
    FRAMELOADER_RELEASE_LOG(ResourceLoading, "loadPostRequest: frame load started");

    m_errorOccurredInLoading = false;

    Ref frame = m_frame.get();
    auto frameName = request.frameName();
    LockHistory lockHistory = request.lockHistory();
    AllowNavigationToInvalidURL allowNavigationToInvalidURL = request.allowNavigationToInvalidURL();
    NewFrameOpenerPolicy openerPolicy = request.newFrameOpenerPolicy();

    const ResourceRequest& inRequest = request.resourceRequest();
    const URL& url = inRequest.url();
    const String& contentType = inRequest.httpContentType();
    String origin = inRequest.httpOrigin();

    ResourceRequest workingResourceRequest(URL { url });

    if (!referrer.isEmpty())
        workingResourceRequest.setHTTPReferrer(referrer);
    workingResourceRequest.setHTTPOrigin(origin);
    workingResourceRequest.setHTTPMethod("POST"_s);
    workingResourceRequest.setHTTPBody(inRequest.httpBody());
    workingResourceRequest.setHTTPContentType(contentType);

    RefPtr targetFrame = formState || frameName.isEmpty() ? nullptr : dynamicDowncast<LocalFrame>(findFrameForNavigation(frameName));

    auto willOpenInNewWindow = !frameName.isEmpty() && !targetFrame ? WillOpenInNewWindow::Yes : WillOpenInNewWindow::No;
    updateRequestAndAddExtraFields(workingResourceRequest, IsMainResource::Yes, loadType, ShouldUpdateAppInitiatedValue::Yes, FrameLoader::IsServiceWorkerNavigationLoad::No, willOpenInNewWindow, request.protectedRequester().ptr());

    if (RefPtr document = frame->document())
        document->checkedContentSecurityPolicy()->upgradeInsecureRequestIfNeeded(workingResourceRequest, ContentSecurityPolicy::InsecureRequestType::Load);

    NavigationAction action { request.requester(), workingResourceRequest, request.initiatedByMainFrame(), request.isRequestFromClientOrUserInput(), loadType, true, event, request.shouldOpenExternalURLsPolicy(), { } };
    action.setLockHistory(lockHistory);
    action.setLockBackForwardList(request.lockBackForwardList());
    action.setShouldReplaceDocumentIfJavaScriptURL(request.shouldReplaceDocumentIfJavaScriptURL());
    action.setNewFrameOpenerPolicy(request.newFrameOpenerPolicy());

    if (!frameName.isEmpty()) {
        // The search for a target frame is done earlier in the case of form submission.
        if (targetFrame) {
            targetFrame->loader().loadWithNavigationAction(WTFMove(workingResourceRequest), WTFMove(action), loadType, WTFMove(formState), allowNavigationToInvalidURL, request.shouldTreatAsContinuingLoad(), WTFMove(completionHandler));
            return;
        }

        // https://html.spec.whatwg.org/#the-rules-for-choosing-a-browsing-context-given-a-browsing-context-name (Step 8.2)
        if (request.protectedRequester()->shouldForceNoOpenerBasedOnCOOP()) {
            frameName = blankTargetFrameName();
            openerPolicy = NewFrameOpenerPolicy::Suppress;
        }

        RefPtr document = frame->document();
        if (request.resourceRequest().url().protocolIsBlob() && !document->protectedSecurityOrigin()->isSameOriginAs(document->protectedTopOrigin())) {
            frameName = blankTargetFrameName();
            openerPolicy = NewFrameOpenerPolicy::Suppress;
        }

        policyChecker().checkNewWindowPolicy(WTFMove(action), WTFMove(workingResourceRequest), WTFMove(formState), frameName, [this, protectedThis = Ref { *this }, allowNavigationToInvalidURL, openerPolicy, completionHandler = WTFMove(completionHandler)] (ResourceRequest&& request, WeakPtr<FormState>&& weakFormState, const AtomString& frameName, const NavigationAction& action, ShouldContinuePolicyCheck shouldContinue) mutable {
            continueLoadAfterNewWindowPolicy(WTFMove(request), RefPtr { weakFormState.get() }.get(), frameName, action, shouldContinue, allowNavigationToInvalidURL, openerPolicy);
            completionHandler();
        });
        return;
    }

    if (request.protectedRequesterSecurityOrigin()->isSameOriginDomain(frame->protectedDocument()->protectedSecurityOrigin().get())) {
        if (!dispatchNavigateEvent(url, loadType, action.downloadAttribute(), request.navigationHistoryBehavior(), false, formState.get()))
            return completionHandler();
    }

    // must grab this now, since this load may stop the previous load and clear this flag
    bool isRedirect = m_quickRedirectComing;
    loadWithNavigationAction(WTFMove(workingResourceRequest), WTFMove(action), loadType, WTFMove(formState), allowNavigationToInvalidURL, request.shouldTreatAsContinuingLoad(), [this, protectedThis = Ref { *this }, isRedirect, completionHandler = WTFMove(completionHandler)] () mutable {
        if (isRedirect) {
            m_quickRedirectComing = false;
            if (RefPtr provisionalDocumentLoader = m_provisionalDocumentLoader)
                provisionalDocumentLoader->setIsClientRedirect(true);
            else if (RefPtr policyDocumentLoader = m_policyDocumentLoader)
                policyDocumentLoader->setIsClientRedirect(true);
        }
        completionHandler();
    });
}

ResourceLoaderIdentifier FrameLoader::loadResourceSynchronously(const ResourceRequest& request, ClientCredentialPolicy clientCredentialPolicy, const FetchOptions& options, const HTTPHeaderMap& originalRequestHeaders, ResourceError& error, ResourceResponse& response, RefPtr<SharedBuffer>& data)
{
    ASSERT(m_frame->document());
    String referrer = SecurityPolicy::generateReferrerHeader(m_frame->document()->referrerPolicy(), request.url(), outgoingReferrerURL(), OriginAccessPatternsForWebProcess::singleton());
    
    ResourceRequest initialRequest = request;
    initialRequest.setTimeoutInterval(10);
    
    if (!referrer.isEmpty())
        initialRequest.setHTTPReferrer(referrer);
    addHTTPOriginIfNeeded(initialRequest, outgoingOrigin());

    if (RefPtr page = m_frame->page())
        initialRequest.setFirstPartyForCookies(page->mainFrameURL());
    
    updateRequestAndAddExtraFields(initialRequest, IsMainResource::No);

    applyUserAgentIfNeeded(initialRequest);

    URL initialRequestURL = initialRequest.url();
    ResourceRequest newRequest = WTFMove(initialRequest);
    auto identifier = requestFromDelegate(newRequest, IsMainResourceLoad::No, error);

#if ENABLE(CONTENT_EXTENSIONS)
    if (error.isNull()) {
        if (RefPtr page = m_frame->page()) {
            if (RefPtr documentLoader = m_documentLoader) {
                auto results = page->protectedUserContentProvider()->processContentRuleListsForLoad(*page, newRequest.url(), ContentExtensions::ResourceType::Fetch, *documentLoader);
                ContentExtensions::applyResultsToRequest(WTFMove(results), page.get(), newRequest);
                if (results.shouldBlock()) {
                    newRequest = { };
                    error = ResourceError(errorDomainWebKitInternal, 0, WTFMove(initialRequestURL), emptyString());
                    response = { };
                    data = nullptr;
                }
            }
        }
    }
#endif

    m_frame->protectedDocument()->checkedContentSecurityPolicy()->upgradeInsecureRequestIfNeeded(newRequest, ContentSecurityPolicy::InsecureRequestType::Load);

    if (error.isNull()) {
        ASSERT(!newRequest.isNull());

        RefPtr documentLoader = this->documentLoader();
        if (!documentLoader->applicationCacheHost().maybeLoadSynchronously(newRequest, error, response, data)) {
            Vector<uint8_t> buffer;
            platformStrategies()->loaderStrategy()->loadResourceSynchronously(*this, identifier, newRequest, clientCredentialPolicy, options, originalRequestHeaders, error, response, buffer);
            data = SharedBuffer::create(WTFMove(buffer));
            documentLoader->applicationCacheHost().maybeLoadFallbackSynchronously(newRequest, error, response, data);
            ResourceLoadObserver::shared().logSubresourceLoading(protectedFrame().ptr(), newRequest, response,
                (isScriptLikeDestination(options.destination) ? ResourceLoadObserver::FetchDestinationIsScriptLike::Yes : ResourceLoadObserver::FetchDestinationIsScriptLike::No));
        }
    }

    notifier().sendRemainingDelegateMessages(protectedDocumentLoader().get(), IsMainResourceLoad::No, identifier, request, response, data.get(), data ? data->size() : 0, -1, error);
    return identifier;
}

void FrameLoader::receivedMainResourceError(const ResourceError& error, LoadWillContinueInAnotherProcess loadWillContinueInAnotherProcess)
{
    // Retain because the stop may release the last reference to it.
    Ref frame = m_frame.get();

    RefPtr loader = activeDocumentLoader();
    // FIXME: Don't want to do this if an entirely new load is going, so should check
    // that both data sources on the frame are either this or nil.
    stop();
    if (m_client->shouldFallBack(error)) {
        if (RefPtr owner = dynamicDowncast<HTMLObjectElement>(frame->ownerElement()))
            owner->renderFallbackContent();
    }

    if (m_state == FrameState::Provisional && m_provisionalDocumentLoader) {
        if (m_submittedFormURL == m_provisionalDocumentLoader->originalRequestCopy().url())
            m_submittedFormURL = URL();
            
        // We might have made a back/forward cache item, but now we're bailing out due to an error before we ever
        // transitioned to the new page (before WebFrameState == commit).  The goal here is to restore any state
        // so that the existing view (that wenever got far enough to replace) can continue being used.
        history().invalidateCurrentItemCachedPage();
        
        // Call clientRedirectCancelledOrFinished here so that the frame load delegate is notified that the redirect's
        // status has changed, if there was a redirect. The frame load delegate may have saved some state about
        // the redirect in its -webView:willPerformClientRedirectToURL:delay:fireDate:forFrame:. Since we are definitely
        // not going to use this provisional resource, as it was cancelled, notify the frame load delegate that the redirect
        // has ended.
        if (m_sentRedirectNotification)
            clientRedirectCancelledOrFinished(NewLoadInProgress::No);
    }

    checkCompleted();
    if (frame->page())
        checkLoadComplete(loadWillContinueInAnotherProcess);
}

void FrameLoader::continueFragmentScrollAfterNavigationPolicy(const ResourceRequest& request, const SecurityOrigin* requesterOrigin, bool shouldContinue, NavigationHistoryBehavior historyHandling)
{
    m_quickRedirectComing = false;

    if (!shouldContinue)
        return;

    // Calling stopLoading() on the provisional document loader can cause the underlying
    // frame to be deallocated.
    Ref frame = m_frame.get();

    // If we have a provisional request for a different document, a fragment scroll should cancel it.
    if (m_provisionalDocumentLoader && !equalIgnoringFragmentIdentifier(m_provisionalDocumentLoader->request().url(), request.url())) {
        protectedProvisionalDocumentLoader()->stopLoading();
        FRAMELOADER_RELEASE_LOG(ResourceLoading, "continueFragmentScrollAfterNavigationPolicy: Clearing provisional document loader (m_provisionalDocumentLoader=%p)", m_provisionalDocumentLoader.get());
        setProvisionalDocumentLoader(nullptr);
    }

    bool isRedirect = m_quickRedirectComing || policyChecker().loadType() == FrameLoadType::RedirectWithLockedBackForwardList;
    loadInSameDocument(request.url(), nullptr, requesterOrigin, !isRedirect, historyHandling);
}

bool FrameLoader::shouldPerformFragmentNavigation(bool isFormSubmission, const String& httpMethod, FrameLoadType loadType, const URL& url)
{
    // We don't do this if we are submitting a form with method other than "GET", explicitly reloading,
    // currently displaying a frameset, or if the URL does not have a fragment.
    // These rules were originally based on what KHTML was doing in KHTMLPart::openURL.

    // FIXME: What about load types other than Standard and Reload?

    return (!isFormSubmission || equalLettersIgnoringASCIICase(httpMethod, "get"_s))
        && !isReload(loadType)
        && loadType != FrameLoadType::Same
        && m_frame->document()->backForwardCacheState() != Document::InBackForwardCache
        && !shouldReload(m_frame->document()->url(), url)
        // We don't want to just scroll if a link from within a
        // frameset is trying to reload the frameset into _top.
        && !m_frame->protectedDocument()->isFrameSet()
        && !stateMachine().isDisplayingInitialEmptyDocument();
}

static bool itemAllowsScrollRestoration(HistoryItem* historyItem, FrameLoadType loadType)
{
    if (!historyItem)
        return true;

    switch (loadType) {
    case FrameLoadType::Back:
    case FrameLoadType::Forward:
    case FrameLoadType::IndexedBackForward:
        return historyItem->shouldRestoreScrollPosition();
    default:
        break;
    }
    return true;
}

static bool isSameDocumentReload(bool isNewNavigation, FrameLoadType loadType)
{
    return !isNewNavigation && !isBackForwardLoadType(loadType);
}

void FrameLoader::scrollToFragmentWithParentBoundary(const URL& url, bool isNewNavigation)
{
    RefPtr view = m_frame->view();
    RefPtr document = m_frame->document();
    if (!view || !document)
        return;

    if (isSameDocumentReload(isNewNavigation, m_loadType) || itemAllowsScrollRestoration(history().protectedCurrentItem().get(), m_loadType)) {
        // https://html.spec.whatwg.org/multipage/browsing-the-web.html#try-to-scroll-to-the-fragment
        if (!document->haveStylesheetsLoaded())
            document->setGotoAnchorNeededAfterStylesheetsLoad(true);
        else
            view->scrollToFragment(url);
    }
}

bool FrameLoader::shouldClose()
{
    Ref frame = m_frame.get();
    RefPtr page = frame->page();
    if (!page)
        return true;
    if (!page->chrome().canRunBeforeUnloadConfirmPanel())
        return true;

    // Store all references to each subframe in advance since beforeunload's event handler may modify frame
    Vector<Ref<LocalFrame>, 16> targetFrames;
    targetFrames.append(frame.copyRef());
    for (RefPtr child = frame->tree().firstChild(); child; child = child->tree().traverseNext(frame.ptr())) {
        if (RefPtr localChild = dynamicDowncast<LocalFrame>(*child))
            targetFrames.append(localChild.releaseNonNull());
    }

    bool shouldClose = false;
    {
        NavigationDisabler navigationDisabler(frame.ptr());
        UnloadCountIncrementer UnloadCountIncrementer(frame->protectedDocument().get());
        size_t i;

        for (i = 0; i < targetFrames.size(); i++) {
            if (!targetFrames[i]->tree().isDescendantOf(frame.ptr()))
                continue;
            if (!Ref { targetFrames[i] }->loader().dispatchBeforeUnloadEvent(page->chrome(), this))
                break;
        }

        if (i == targetFrames.size())
            shouldClose = true;
    }

    if (!shouldClose)
        m_submittedFormURL = URL();

    m_currentNavigationHasShownBeforeUnloadConfirmPanel = false;
    return shouldClose;
}

void FrameLoader::dispatchUnloadEvents(UnloadEventPolicy unloadEventPolicy)
{
    if (!m_frame->document())
        return;

    if (m_pageDismissalEventBeingDispatched != PageDismissalType::None)
        return;

    // We store the frame's page in a local variable because the frame might get detached inside dispatchEvent.
    ForbidPromptsScope forbidPrompts(m_frame->page());
    ForbidSynchronousLoadsScope forbidSynchronousLoads(m_frame->page());
    UnloadCountIncrementer UnloadCountIncrementer(m_frame->document());

    if (m_didCallImplicitClose && !m_wasUnloadEventEmitted) {
        if (RefPtr input = dynamicDowncast<HTMLInputElement>(m_frame->document()->focusedElement()))
            input->endEditing();
        if (m_pageDismissalEventBeingDispatched == PageDismissalType::None) {
            Ref document = *m_frame->document();
            if (unloadEventPolicy == UnloadEventPolicy::UnloadAndPageHide) {
                m_pageDismissalEventBeingDispatched = PageDismissalType::PageHide;
                document->dispatchPagehideEvent(document->backForwardCacheState() == Document::AboutToEnterBackForwardCache ? PageshowEventPersistence::Persisted : PageshowEventPersistence::NotPersisted);
            }

            // This takes care of firing the visibilitychange event and making sure the document is reported as hidden.
            document->setVisibilityHiddenDueToDismissal(true);

            if (document->backForwardCacheState() == Document::NotInBackForwardCache) {
                Ref unloadEvent = Event::create(eventNames().unloadEvent, Event::CanBubble::No, Event::IsCancelable::No);
                // The DocumentLoader (and thus its DocumentLoadTiming) might get destroyed
                // while dispatching the event, so protect it to prevent writing the end
                // time into freed memory.
                RefPtr documentLoader = m_provisionalDocumentLoader;
                auto* timing = documentLoader ? &documentLoader->timing() : nullptr;
                m_pageDismissalEventBeingDispatched = PageDismissalType::Unload;
                if (timing && !timing->unloadEventStart())
                    timing->markUnloadEventStart();
                document->protectedWindow()->dispatchEvent(unloadEvent, document.ptr());
                if (timing && !timing->unloadEventEnd())
                    timing->markUnloadEventEnd();
            }
        }
        m_pageDismissalEventBeingDispatched = PageDismissalType::None;
        m_wasUnloadEventEmitted = true;
    }

    // Dispatching the unload event could have made m_frame->document() null.
    if (!m_frame->document())
        return;

    if (m_frame->document()->backForwardCacheState() != Document::NotInBackForwardCache)
        return;

    // Don't remove event listeners from a transitional empty document (see bug 28716 for more information).
    bool shouldKeepEventListeners = m_stateMachine.isDisplayingInitialEmptyDocument() && m_provisionalDocumentLoader
        && m_frame->document()->isSecureTransitionTo(Ref { *m_provisionalDocumentLoader }->url());

    if (!shouldKeepEventListeners)
        m_frame->protectedDocument()->removeAllEventListeners();
}

static bool shouldAskForNavigationConfirmation(Document& document, const BeforeUnloadEvent& event)
{
    // Confirmation dialog should not be displayed when the allow-modals flag is not set.
    if (document.isSandboxed(SandboxFlag::Modals))
        return false;

    RefPtr page = document.page();
    bool userDidInteractWithPage = page ? page->userDidInteractWithPage() : false;

    // Web pages can request we ask for confirmation before navigating by:
    // - Cancelling the BeforeUnloadEvent (modern way)
    // - Setting the returnValue attribute on the BeforeUnloadEvent to a non-empty string.
    // - Returning a non-empty string from the event handler, which is then set as returnValue
    //   attribute on the BeforeUnloadEvent.
    return userDidInteractWithPage && (event.defaultPrevented() || !event.returnValue().isEmpty());
}

bool FrameLoader::dispatchBeforeUnloadEvent(Chrome& chrome, FrameLoader* frameLoaderBeingNavigated)
{
    RefPtr window = m_frame->document()->window();
    if (!window)
        return true;

    RefPtr document = m_frame->document();
    if (!document->bodyOrFrameset())
        return true;
    
    Ref beforeUnloadEvent = BeforeUnloadEvent::create();

    {
        SetForScope change(m_pageDismissalEventBeingDispatched, PageDismissalType::BeforeUnload);
        ForbidPromptsScope forbidPrompts(m_frame->protectedPage().get());
        ForbidSynchronousLoadsScope forbidSynchronousLoads(m_frame->page());
        ForbidCopyPasteScope forbidCopyPaste(m_frame->page());
        window->dispatchEvent(beforeUnloadEvent, window->protectedDocument().get());
    }

    if (!beforeUnloadEvent->defaultPrevented())
        document->defaultEventHandler(beforeUnloadEvent.get());

    if (!shouldAskForNavigationConfirmation(*document, beforeUnloadEvent))
        return true;

    // If the navigating FrameLoader has already shown a beforeunload confirmation panel for the current navigation attempt,
    // this frame is not allowed to cause another one to be shown.
    if (frameLoaderBeingNavigated->m_currentNavigationHasShownBeforeUnloadConfirmPanel) {
        document->addConsoleMessage(MessageSource::JS, MessageLevel::Error, "Blocked attempt to show multiple beforeunload confirmation dialogs for the same navigation."_s);
        return true;
    }

    // We should only display the beforeunload dialog for an iframe if its SecurityOrigin matches all
    // ancestor frame SecurityOrigins up through the navigating FrameLoader.
    if (frameLoaderBeingNavigated != this) {
        RefPtr parentFrame = dynamicDowncast<LocalFrame>(m_frame->tree().parent());
        while (parentFrame) {
            RefPtr parentDocument = parentFrame->document();
            if (!parentDocument)
                return true;
            if (RefPtr document = m_frame->document(); !document || !document->protectedSecurityOrigin()->isSameOriginDomain(parentDocument->protectedSecurityOrigin())) {
                document->addConsoleMessage(MessageSource::JS, MessageLevel::Error, "Blocked attempt to show beforeunload confirmation dialog on behalf of a frame with different security origin. Protocols, domains, and ports must match."_s);
                return true;
            }
            
            if (&parentFrame->loader() == frameLoaderBeingNavigated)
                break;
            
            parentFrame = dynamicDowncast<LocalFrame>(parentFrame->tree().parent());
        }
        
        // The navigatingFrameLoader should always be in our ancestory.
        ASSERT(parentFrame);
        ASSERT(&parentFrame->loader() == frameLoaderBeingNavigated);
    }

    frameLoaderBeingNavigated->m_currentNavigationHasShownBeforeUnloadConfirmPanel = true;

    String text = document->displayStringModifiedByEncoding(beforeUnloadEvent->returnValue());
    return chrome.runBeforeUnloadConfirmPanel(WTFMove(text), protectedFrame());
}

void FrameLoader::executeJavaScriptURL(const URL& url, const NavigationAction& action)
{
    ASSERT(url.protocolIsJavaScript());

    bool isFirstNavigationInFrame = false;
    if (!m_stateMachine.committedFirstRealDocumentLoad()) {
        m_stateMachine.advanceTo(FrameLoaderStateMachine::DisplayingInitialEmptyDocumentPostCommit);
        isFirstNavigationInFrame = true;
    }

    RefPtr ownerDocument = m_frame->ownerElement() ? &m_frame->ownerElement()->document() : nullptr;
    if (ownerDocument)
        ownerDocument->incrementLoadEventDelayCount();

    bool didReplaceDocument = false;
    bool requesterSandboxedFromScripts = action.requester() && action.requester()->sandboxFlags.contains(SandboxFlag::Scripts);
    if (requesterSandboxedFromScripts) {
        // FIXME: This message should be moved off the console once a solution to https://bugs.webkit.org/show_bug.cgi?id=103274 exists.
        // This message is identical to the message in ScriptController::canExecuteScripts.
        if (RefPtr document = m_frame->document())
            document->addConsoleMessage(MessageSource::Security, MessageLevel::Error, makeString("Blocked script execution in '"_s, action.requester()->url.stringCenterEllipsizedToLength(), "' because the document's frame is sandboxed and the 'allow-scripts' permission is not set."_s));
    } else
        protectedFrame()->checkedScript()->executeJavaScriptURL(url, action, didReplaceDocument);

    // We need to communicate that a load happened, even if the JavaScript URL execution didn't end up replacing the document.
    if (RefPtr document = m_frame->document(); isFirstNavigationInFrame && !didReplaceDocument)
        document->dispatchWindowLoadEvent();

    checkCompleted();

    if (ownerDocument)
        ownerDocument->decrementLoadEventDelayCount();

    m_quickRedirectComing = false;
}

void FrameLoader::continueLoadAfterNavigationPolicy(const ResourceRequest& request, FormState* formState, NavigationPolicyDecision navigationPolicyDecision, AllowNavigationToInvalidURL allowNavigationToInvalidURL)
{
    // If we loaded an alternate page to replace an unreachableURL, we'll get in here with a
    // nil policyDataSource because loading the alternate page will have passed
    // through this method already, nested; otherwise, policyDataSource should still be set.
    ASSERT(m_policyDocumentLoader || !m_provisionalDocumentLoader->unreachableURL().isEmpty());

    Ref frame = m_frame.get();

    bool urlIsDisallowed = allowNavigationToInvalidURL == AllowNavigationToInvalidURL::No && !request.url().isValid();

    // For Navigation API traversal navigation, dispatch navigate event AFTER beforeunload.
    bool navigateEventAborted = false;
    bool shouldCloseResult = true;

    if (m_navigationAPITraversalInProgress && m_pendingNavigationAPIItem) {
        // Only call shouldClose() early for Navigation API traversals
        shouldCloseResult = shouldClose();

        if (shouldCloseResult && frame->document() && frame->document()->settings().navigationAPIEnabled()) {
            if (RefPtr window = frame->document()->window()) {
                if (RefPtr navigation = window->navigation(); navigation->frame()) {
                    if (navigation->dispatchTraversalNavigateEvent(Ref { *m_pendingNavigationAPIItem }) == Navigation::DispatchResult::Aborted)
                        navigateEventAborted = true;
                }
            }
        }

        m_pendingNavigationAPIItem = nullptr;
        m_navigationAPITraversalInProgress = false;
    } else {
        // For non-Navigation API traversals, use original behavior with short-circuit evaluation
        shouldCloseResult = (navigationPolicyDecision == NavigationPolicyDecision::ContinueLoad && !urlIsDisallowed) ? shouldClose() : false;
    }

    bool canContinue = navigationPolicyDecision == NavigationPolicyDecision::ContinueLoad && shouldCloseResult && !navigateEventAborted && !urlIsDisallowed;
    bool isTargetItem = frame->loader().history().provisionalItem() ? frame->loader().history().provisionalItem()->isTargetItem() : false;

    if (!canContinue) {
        FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_CONTINUELOADAFTERNAVIGATIONPOLICY_CANNOT_CONTINUE, static_cast<int>(allowNavigationToInvalidURL), request.url().isValid(), static_cast<int>(navigationPolicyDecision));

        // If we were waiting for a quick redirect, but the policy delegate decided to ignore it, then we 
        // need to report that the client redirect was cancelled.
        // FIXME: The client should be told about ignored non-quick redirects, too.
        if (m_quickRedirectComing)
            clientRedirectCancelledOrFinished(NewLoadInProgress::No);

        if (navigationPolicyDecision == NavigationPolicyDecision::LoadWillContinueInAnotherProcess) {
            stopAllLoaders();
            m_checkTimer.stop();
        }

        setPolicyDocumentLoader(nullptr, navigationPolicyDecision == NavigationPolicyDecision::LoadWillContinueInAnotherProcess ? LoadWillContinueInAnotherProcess::Yes : LoadWillContinueInAnotherProcess::No);
        if (frame->isMainFrame() || navigationPolicyDecision != NavigationPolicyDecision::LoadWillContinueInAnotherProcess)
            checkCompleted();
        else {
            // Don't call checkCompleted until RemoteFrame::didFinishLoadInAnotherProcess,
            // to prevent onload from happening until iframes finish loading in other processes.
            ASSERT(frame->settings().siteIsolationEnabled());
            m_provisionalLoadHappeningInAnotherProcess = true;
        }

        if (navigationPolicyDecision != NavigationPolicyDecision::LoadWillContinueInAnotherProcess)
            checkLoadComplete();

        // If the navigation request came from the back/forward menu, and we punt on it, we have the
        // problem that we have optimistically moved the b/f cursor already, so move it back. For sanity,
        // we only do this when punting a navigation for the target frame or top-level frame.
        if ((isTargetItem || frame->isMainFrame()) && isBackForwardLoadType(policyChecker().loadType())) {
            if (RefPtr page = frame->page()) {
                if (RefPtr localMainFrame = frame->localMainFrame()) {
                    if (RefPtr resetItem = localMainFrame->loader().history().currentItem())
                        page->checkedBackForward()->setCurrentItem(*resetItem);
                }
            }
        }
        return;
    }

    if (request.url().protocolIsJavaScript()) {
        auto action = m_policyDocumentLoader->triggeringAction();
        setPolicyDocumentLoader(nullptr);
        executeJavaScriptURL(request.url(), action);
        return;
    }

    FrameLoadType type = policyChecker().loadType();

    {
        SetForScope<bool> doNotAbortNavigationAPI { m_doNotAbortNavigationAPI, m_policyDocumentLoader && m_policyDocumentLoader->triggeringAction().isFromNavigationAPI() };

        // A new navigation is in progress, so don't clear the history's provisional item.
        stopAllLoaders(ClearProvisionalItem::No);
    }

    // <rdar://problem/6250856> - In certain circumstances on pages with multiple frames, stopAllLoaders()
    // might detach the current FrameLoader, in which case we should bail on this newly defunct load. 
    if (!frame->page()) {
        FRAMELOADER_RELEASE_LOG(ResourceLoading, "continueLoadAfterNavigationPolicy: can't continue loading frame because it became defunct");
        return;
    }

    setProvisionalDocumentLoader(m_policyDocumentLoader.copyRef());
    FRAMELOADER_RELEASE_LOG_FORWARDABLE(FRAMELOADER_CONTINUELOADAFTERNAVIGATIONPOLICY, (uint64_t)m_provisionalDocumentLoader.get());
    m_loadType = type;
    setState(FrameState::Provisional);

    setPolicyDocumentLoader(nullptr);

    if (isBackForwardLoadType(type)) {
        auto& diagnosticLoggingClient = frame->protectedPage()->diagnosticLoggingClient();
        if (RefPtr provisionalItem = history().provisionalItem(); provisionalItem && provisionalItem->isInBackForwardCache()) {
            diagnosticLoggingClient.logDiagnosticMessageWithResult(DiagnosticLoggingKeys::backForwardCacheKey(), DiagnosticLoggingKeys::retrievalKey(), DiagnosticLoggingResultPass, ShouldSample::Yes);
            loadProvisionalItemFromCachedPage();
            FRAMELOADER_RELEASE_LOG(ResourceLoading, "continueLoadAfterNavigationPolicy: can't continue loading frame because it will be loaded from cache");
            return;
        }
        diagnosticLoggingClient.logDiagnosticMessageWithResult(DiagnosticLoggingKeys::backForwardCacheKey(), DiagnosticLoggingKeys::retrievalKey(), DiagnosticLoggingResultFail, ShouldSample::Yes);
    }

    CompletionHandler<void()> completionHandler = [this, protectedThis = Ref { *this }] () mutable {
        if (!m_provisionalDocumentLoader) {
            FRAMELOADER_RELEASE_LOG(ResourceLoading, "continueLoadAfterNavigationPolicy (completionHandler): Frame load canceled - no provisional document loader before prepareForLoadStart");
            return;
        }
        
        prepareForLoadStart();

        // The load might be cancelled inside of prepareForLoadStart(), nulling out the m_provisionalDocumentLoader,
        // so we need to null check it again.
        if (!m_provisionalDocumentLoader) {
            FRAMELOADER_RELEASE_LOG(ResourceLoading, "continueLoadAfterNavigationPolicy (completionHandler): Frame load canceled - no provisional document loader after prepareForLoadStart");
            return;
        }
        
        RefPtr activeDocLoader = activeDocumentLoader();
        if (activeDocLoader && activeDocLoader->isLoadingMainResource()) {
            FRAMELOADER_RELEASE_LOG(ResourceLoading, "continueLoadAfterNavigationPolicy (completionHandler): Main frame already being loaded");
            return;
        }
        
        m_loadingFromCachedPage = false;

        protectedProvisionalDocumentLoader()->startLoadingMainResource();
    };
    
    if (!formState) {
        completionHandler();
        return;
    }

    m_client->dispatchWillSubmitForm(*formState, WTFMove(completionHandler));
}

void FrameLoader::continueLoadAfterNewWindowPolicy(ResourceRequest&& request,
    FormState* formState, const AtomString& frameName, const NavigationAction& action, ShouldContinuePolicyCheck shouldContinue, AllowNavigationToInvalidURL allowNavigationToInvalidURL, NewFrameOpenerPolicy openerPolicy)
{
    if (shouldContinue != ShouldContinuePolicyCheck::Yes)
        return;

    Ref frame = m_frame.get();

    if (request.url().protocolIsJavaScript() && !frame->protectedDocument()->checkedContentSecurityPolicy()->allowJavaScriptURLs(frame->document()->url().string(), { }, request.url().string(), nullptr))
        return;

    RefPtr mainFrame = m_client->dispatchCreatePage(action, openerPolicy);
    if (!mainFrame)
        return;

    Ref mainFrameLoader = mainFrame->loader();

    if (!isBlankTargetFrameName(frameName))
        mainFrame->tree().setSpecifiedName(frameName);

    mainFrame->protectedPage()->setOpenedByDOM();
    mainFrameLoader->m_client->dispatchShow();
    if (openerPolicy == NewFrameOpenerPolicy::Allow) {
        ASSERT(mainFrame->opener() == frame.ptr());
        mainFrame->page()->setOpenedByDOMWithOpener(true);
        mainFrame->protectedDocument()->setReferrerPolicy(frame->document()->referrerPolicy());
    }

    NavigationAction newAction { frame->protectedDocument().releaseNonNull(), request, InitiatedByMainFrame::Unknown, action.isRequestFromClientOrUserInput(), NavigationType::Other, action.shouldOpenExternalURLsPolicy(), nullptr, action.downloadAttribute() };
    newAction.setShouldReplaceDocumentIfJavaScriptURL(action.shouldReplaceDocumentIfJavaScriptURL());
    mainFrameLoader->loadWithNavigationAction(WTFMove(request), WTFMove(newAction), FrameLoadType::Standard, formState, allowNavigationToInvalidURL, ShouldTreatAsContinuingLoad::No);
}

ResourceLoaderIdentifier FrameLoader::requestFromDelegate(ResourceRequest& request, IsMainResourceLoad isMainResourceLoad, ResourceError& error)
{
    ASSERT(!request.isNull());

    auto identifier = ResourceLoaderIdentifier::generate();
    RefPtr documentLoader = m_documentLoader;
    notifier().assignIdentifierToInitialRequest(identifier, isMainResourceLoad, documentLoader.get(), request);

    ResourceRequest newRequest(request);
    notifier().dispatchWillSendRequest(documentLoader.get(), identifier, newRequest, ResourceResponse(), nullptr);

    if (newRequest.isNull())
        error = cancelledError(request);
    else
        error = ResourceError();

    request = newRequest;
    return identifier;
}

void FrameLoader::loadedResourceFromMemoryCache(CachedResource& resource, ResourceRequest& newRequest, ResourceError& error)
{
    RefPtr page = m_frame->page();
    if (!page)
        return;

    RefPtr documentloader = m_documentLoader;
    if (!resource.shouldSendResourceLoadCallbacks() || documentloader->haveToldClientAboutLoad(resource.url().string()))
        return;

    // Main resource delegate messages are synthesized in MainResourceLoader, so we must not send them here.
    if (resource.type() == CachedResource::Type::MainResource)
        return;

    if (!page->areMemoryCacheClientCallsEnabled()) {
        InspectorInstrumentation::didLoadResourceFromMemoryCache(*page, documentloader.get(), &resource);
        documentloader->recordMemoryCacheLoadForFutureClientNotification(resource.resourceRequest());
        documentloader->didTellClientAboutLoad(resource.url().string());
        page->setHasPendingMemoryCacheLoadNotifications(true);
        return;
    }

    if (m_client->dispatchDidLoadResourceFromMemoryCache(documentloader.get(), newRequest, resource.response(), resource.encodedSize())) {
        InspectorInstrumentation::didLoadResourceFromMemoryCache(*page, documentloader.get(), &resource);
        documentloader->didTellClientAboutLoad(resource.url().string());
        return;
    }

    auto identifier = requestFromDelegate(newRequest, IsMainResourceLoad::No, error);

    ResourceResponse response = resource.response();
    response.setSource(ResourceResponse::Source::MemoryCache);
    notifier().sendRemainingDelegateMessages(documentloader.get(), IsMainResourceLoad::No, identifier, newRequest, response, nullptr, resource.encodedSize(), 0, error);
}

void FrameLoader::applyUserAgentIfNeeded(ResourceRequest& request)
{
    if (!request.hasHTTPHeaderField(HTTPHeaderName::UserAgent)) {
        String userAgent = this->userAgent(request.url());
        ASSERT(!userAgent.isNull());
        request.setHTTPUserAgent(userAgent);
    }
}

bool FrameLoader::shouldInterruptLoadForXFrameOptions(const String& content, const URL& url, ResourceLoaderIdentifier requestIdentifier)
{
    if (m_frame->settings().ignoreIframeEmbeddingProtectionsEnabled())
        return false;

    RefPtr topFrame = dynamicDowncast<LocalFrame>(m_frame->tree().top());
    if (m_frame.ptr() == topFrame)
        return false;

    XFrameOptionsDisposition disposition = parseXFrameOptionsHeader(content);

    switch (disposition) {
    case XFrameOptionsDisposition::SameOrigin: {
        Ref origin = SecurityOrigin::create(url);
        if (!topFrame || !origin->isSameSchemeHostPort(topFrame->protectedDocument()->protectedSecurityOrigin()))
            return true;
        for (RefPtr frame = m_frame->tree().parent(); frame; frame = frame->tree().parent()) {
            RefPtr localFrame = dynamicDowncast<LocalFrame>(*frame);
            if (!localFrame || !origin->isSameSchemeHostPort(localFrame->protectedDocument()->protectedSecurityOrigin()))
                return true;
        }
        return false;
    }
    case XFrameOptionsDisposition::Deny:
        return true;
    case XFrameOptionsDisposition::AllowAll:
        return false;
    case XFrameOptionsDisposition::Conflict:
        m_frame->protectedDocument()->addConsoleMessage(MessageSource::JS, MessageLevel::Error, makeString("Multiple 'X-Frame-Options' headers with conflicting values ('"_s, content, "') encountered when loading '"_s, url.stringCenterEllipsizedToLength(), "'. Falling back to 'DENY'."_s), requestIdentifier.toUInt64());
        return true;
    case XFrameOptionsDisposition::Invalid:
        m_frame->protectedDocument()->addConsoleMessage(MessageSource::JS, MessageLevel::Error, makeString("Invalid 'X-Frame-Options' header encountered when loading '"_s, url.stringCenterEllipsizedToLength(), "': '"_s, content, "' is not a recognized directive. The header will be ignored."_s), requestIdentifier.toUInt64());
        return false;
    case XFrameOptionsDisposition::None:
        return false;
    }
    ASSERT_NOT_REACHED();
    return false;
}

void FrameLoader::loadProvisionalItemFromCachedPage()
{
    RefPtr provisionalLoader = provisionalDocumentLoader();
    LOG(BackForwardCache, "FrameLoader::loadProvisionalItemFromCachedPage Loading provisional DocumentLoader %p with URL '%s' from CachedPage", provisionalDocumentLoader(), provisionalDocumentLoader()->url().stringCenterEllipsizedToLength().utf8().data());

    prepareForLoadStart();

    m_loadingFromCachedPage = true;
    
    // Should have timing data from previous time(s) the page was shown.
    ASSERT(provisionalLoader->timing().startTime());
    provisionalLoader->resetTiming();
    provisionalLoader->timing().markStartTime();
    
    provisionalLoader->setCommitted(true);
    commitProvisionalLoad();
}

bool FrameLoader::shouldTreatURLAsSameAsCurrent(const SecurityOrigin* requesterOrigin, const URL& url) const
{
    RefPtr currentHistoryItem = history().currentItem();
    if (!currentHistoryItem)
        return false;
    if (requesterOrigin && (!m_frame->document() || !requesterOrigin->isSameOriginAs(m_frame->protectedDocument()->protectedSecurityOrigin())))
        return false;
    return url == currentHistoryItem->url();
}

bool FrameLoader::shouldTreatURLAsSrcdocDocument(const URL& url) const
{
    if (!url.isAboutSrcDoc())
        return false;
    RefPtr ownerElement = m_frame->ownerElement();
    if (!ownerElement)
        return false;
    if (!ownerElement->hasTagName(iframeTag))
        return false;
    return ownerElement->hasAttributeWithoutSynchronization(srcdocAttr);
}

RefPtr<Frame> FrameLoader::findFrameForNavigation(const AtomString& name, Document* rawActiveDocument)
{
    // FIXME: Eventually all callers should supply the actual activeDocument so we can call canNavigate with the right document.
    RefPtr activeDocument = rawActiveDocument;
    if (!activeDocument)
        activeDocument = m_frame->document();

    if (!activeDocument)
        return nullptr;

    RefPtr frame = protectedFrame()->tree().findBySpecifiedName(name, activeDocument->frame() ? *activeDocument->protectedFrame() : protectedFrame().get());
    if (activeDocument->canNavigate(frame.get()) != CanNavigateState::Able)
        return nullptr;

    return frame;
}

bool FrameLoader::dispatchNavigateEvent(const URL& newURL, FrameLoadType loadType, const AtomString& downloadAttribute, NavigationHistoryBehavior historyHandling, bool isSameDocument, FormState* formState, SerializedScriptValue* classicHistoryAPIState, Element* sourceElement)
{
    RefPtr document = m_frame->document();
    if (!document || !document->settings().navigationAPIEnabled())
        return true;
    RefPtr window = document->window();
    if (!window)
        return true;
    // Download events are handled later in PolicyChecker::checkNavigationPolicy().
    if (!downloadAttribute.isNull())
        return true;
    if (!isSameDocument && !newURL.hasFetchScheme())
        return true;

    auto navigationType = determineNavigationType(loadType, historyHandling);

    if (m_policyDocumentLoader && m_policyDocumentLoader->triggeringAction().isFromNavigationAPI()) {
        auto& action = m_policyDocumentLoader->triggeringAction();
        auto apiType = action.navigationAPIType();
        // If this is from Navigation API and should be a traverse, dispatch proper traverse event.
        if (apiType && *apiType == NavigationNavigationType::Traverse)
            return true;
    }

    // Traversals are handled earlier, in loadItem().
    if (navigationType == NavigationNavigationType::Traverse)
        return true;

    // If sourceElement is from a different frame, it should be null.
    if (sourceElement && sourceElement->document().frame() != m_frame.ptr())
        sourceElement = nullptr;

    return window->protectedNavigation()->dispatchPushReplaceReloadNavigateEvent(newURL, navigationType, isSameDocument, formState, classicHistoryAPIState, sourceElement);
}

void FrameLoader::loadSameDocumentItem(HistoryItem& item)
{
    ASSERT(item.documentSequenceNumber() == history().currentItem()->documentSequenceNumber());

    Ref frame = m_frame.get();
    Ref history = m_history.get();

    // Save user view state to the current history item here since we don't do a normal load.
    // FIXME: Does form state need to be saved here too?
    history->saveScrollPositionAndViewStateToItem(history->protectedCurrentItem().get());
    if (RefPtr view = frame->view())
        view->setLastUserScrollType(std::nullopt);

    history->setCurrentItem(item);
        
    // loadInSameDocument() actually changes the URL and notifies load delegates of a "fake" load
    loadInSameDocument(item.url(), item.stateObject(), nullptr, false);

    // Restore user view state from the current history item here since we don't do a normal load.
    if (!scrollingSuppressedByNavigationAPI(frame->protectedDocument().get()))
        history->restoreScrollPositionAndViewState();
}

// FIXME: This function should really be split into a couple pieces, some of
// which should be methods of HistoryController and some of which should be
// methods of FrameLoader.
void FrameLoader::loadDifferentDocumentItem(HistoryItem& item, HistoryItem* fromItem, FrameLoadType loadType, FormSubmissionCacheLoadPolicy cacheLoadPolicy, ShouldTreatAsContinuingLoad shouldTreatAsContinuingLoad)
{
    FRAMELOADER_RELEASE_LOG(ResourceLoading, "loadDifferentDocumentItem: frame load started");

    Ref frame = m_frame.get();

    // History items should not be reported to the parent.
    m_shouldReportResourceTimingToParentFrame = false;

    // Remember this item so we can traverse any child items as child frames load
    history().setProvisionalItem(&item);

    auto initiatedByMainFrame = InitiatedByMainFrame::Unknown;

    SetForScope continuingLoadGuard(m_currentLoadContinuingState, shouldTreatAsContinuingLoad != ShouldTreatAsContinuingLoad::No ? LoadContinuingState::ContinuingWithHistoryItem : LoadContinuingState::NotContinuing);

    if (CheckedPtr cachedPage = BackForwardCache::singleton().get(item, frame->protectedPage().get())) {
        RefPtr documentLoader = cachedPage->documentLoader();
        m_client->updateCachedDocumentLoader(*documentLoader);

        auto action = NavigationAction { frame->protectedDocument().releaseNonNull(), documentLoader->request(), initiatedByMainFrame, documentLoader->isRequestFromClientOrUserInput(), loadType, false };
        action.setTargetBackForwardItem(item);
        action.setSourceBackForwardItem(fromItem);
        action.setNavigationAPIType(determineNavigationType(loadType, NavigationHistoryBehavior::Auto));
        documentLoader->setTriggeringAction(WTFMove(action));

        documentLoader->setLastCheckedRequest(ResourceRequest());
        cachedPage = nullptr; // Call to loadWithDocumentLoader() below may destroy the CachedPage.
        loadWithDocumentLoader(documentLoader.get(), loadType, { }, AllowNavigationToInvalidURL::Yes);
        return;
    }

    URL itemURL = item.url();
    URL itemOriginalURL = item.originalURL();
    URL currentURL;
    if (RefPtr loader = documentLoader())
        currentURL = loader->url();
    RefPtr formData = item.formData();

    ResourceRequest request(WTFMove(itemURL));

    if (!item.referrer().isNull())
        request.setHTTPReferrer(item.referrer());

    ShouldOpenExternalURLsPolicy shouldOpenExternalURLsPolicy = shouldOpenExternalURLsPolicyToApply(frame, initiatedByMainFrame, item.shouldOpenExternalURLsPolicy());
    bool isFormSubmission = false;

    if (RefPtr localFrame = dynamicDowncast<LocalFrame>(frame->mainFrame())) {
        if (RefPtr documentLoader = localFrame->loader().documentLoader())
            request.setIsAppInitiated(documentLoader->lastNavigationWasAppInitiated());
    }

    // If this was a repost that failed the page cache, we might try to repost the form.
    NavigationAction action;
    if (formData) {
        request.setHTTPMethod("POST"_s);
        request.setHTTPBody(WTFMove(formData));
        request.setHTTPContentType(item.formContentType());
        auto securityOrigin = SecurityOrigin::createFromString(item.referrer());
        addHTTPOriginIfNeeded(request, securityOrigin->toString());

        updateRequestAndAddExtraFields(request, IsMainResource::Yes, loadType);
        
        // FIXME: Slight hack to test if the NSURL cache contains the page we're going to.
        // We want to know this before talking to the policy delegate, since it affects whether 
        // we show the DoYouReallyWantToRepost nag.
        //
        // This trick has a small bug (3123893) where we might find a cache hit, but then
        // have the item vanish when we try to use it in the ensuing nav.  This should be
        // extremely rare, but in that case the user will get an error on the navigation.
        
        if (cacheLoadPolicy == MayAttemptCacheOnlyLoadForFormSubmissionItem) {
            request.setCachePolicy(ResourceRequestCachePolicy::ReturnCacheDataDontLoad);
            action = { frame->protectedDocument().releaseNonNull(), request, initiatedByMainFrame, false, loadType, isFormSubmission, nullptr, shouldOpenExternalURLsPolicy };
        } else {
            request.setCachePolicy(ResourceRequestCachePolicy::ReturnCacheDataElseLoad);
            action = { frame->protectedDocument().releaseNonNull(), request, initiatedByMainFrame, false, NavigationType::FormResubmitted, shouldOpenExternalURLsPolicy, nullptr };
        }
    } else {
        switch (loadType) {
        case FrameLoadType::Reload:
        case FrameLoadType::ReloadFromOrigin:
        case FrameLoadType::ReloadExpiredOnly:
            request.setCachePolicy(ResourceRequestCachePolicy::ReloadIgnoringCacheData);
            break;
        case FrameLoadType::Back:
        case FrameLoadType::Forward:
        case FrameLoadType::IndexedBackForward: {
#if PLATFORM(COCOA)
            bool allowStaleData = true;
#else
            bool allowStaleData = !item.wasRestoredFromSession();
#endif
            if (allowStaleData)
                request.setCachePolicy(ResourceRequestCachePolicy::ReturnCacheDataElseLoad);
            item.setWasRestoredFromSession(false);
            break;
        }
        case FrameLoadType::Standard:
        case FrameLoadType::RedirectWithLockedBackForwardList:
            break;
        case FrameLoadType::Same:
        case FrameLoadType::Replace:
            ASSERT_NOT_REACHED();
        }

        updateRequestAndAddExtraFields(request, IsMainResource::Yes, loadType);

        ResourceRequest requestForOriginalURL(request);
        requestForOriginalURL.setURL(WTFMove(itemOriginalURL));
        action = { frame->protectedDocument().releaseNonNull(), requestForOriginalURL, initiatedByMainFrame, request.isAppInitiated(), loadType, isFormSubmission, nullptr, shouldOpenExternalURLsPolicy };
    }

    action.setTargetBackForwardItem(item);
    action.setSourceBackForwardItem(fromItem);
    action.setNavigationAPIType(determineNavigationType(loadType, NavigationHistoryBehavior::Auto));

    loadWithNavigationAction(WTFMove(request), WTFMove(action), loadType, { }, AllowNavigationToInvalidURL::Yes, shouldTreatAsContinuingLoad);
}

// Loads content into this frame, as specified by history item
void FrameLoader::loadItem(HistoryItem& item, HistoryItem* fromItem, FrameLoadType loadType, ShouldTreatAsContinuingLoad shouldTreatAsContinuingLoad)
{
    m_requestedHistoryItem = item;
    RefPtr currentItem = history().currentItem();

    bool sameDocumentNavigation = currentItem && item.shouldDoSameDocumentNavigationTo(*currentItem);

    // If we're continuing this history navigation in a new process, then doing a same document navigation never makes sense.
    ASSERT(!sameDocumentNavigation || shouldTreatAsContinuingLoad == ShouldTreatAsContinuingLoad::No);

    // For Navigation API navigation, handle navigate event
    if (frame().document() && frame().document()->settings().navigationAPIEnabled() && fromItem && SecurityOrigin::create(item.url())->isSameOriginAs(SecurityOrigin::create(fromItem->url()))) {
        if (sameDocumentNavigation) {
            // For same-document navigation, dispatch navigate event immediately.
            if (RefPtr window = frame().document()->window()) {
                if (RefPtr navigation = window->navigation(); navigation->frame()) {
                    if (navigation->dispatchTraversalNavigateEvent(item) == Navigation::DispatchResult::Aborted)
                        return;
                    // In case the event detached the frame.
                    if (!navigation->frame())
                        return;
                }
            }
        } else {
            // For cross-document navigation, save the item for later dispatch.
            // Navigate event will be dispatched after beforeunload.
            m_pendingNavigationAPIItem = &item;
            m_navigationAPITraversalInProgress = true;
        }
    }

    if (sameDocumentNavigation) {
        m_loadType = loadType;
        loadSameDocumentItem(item);
    } else
        loadDifferentDocumentItem(item, fromItem, loadType, MayAttemptCacheOnlyLoadForFormSubmissionItem, shouldTreatAsContinuingLoad);
}

void FrameLoader::retryAfterFailedCacheOnlyMainResourceLoad()
{
    ASSERT(m_state == FrameState::Provisional);
    ASSERT(!m_loadingFromCachedPage);
    ASSERT(history().provisionalItem());
    ASSERT(history().provisionalItem()->formData());
    ASSERT(history().provisionalItem() == m_requestedHistoryItem.get());

    FrameLoadType loadType = m_loadType;
    RefPtr item = history().provisionalItem();

    stopAllLoaders(ClearProvisionalItem::No);
    if (item)
        loadDifferentDocumentItem(*item, history().protectedCurrentItem().get(), loadType, MayNotAttemptCacheOnlyLoadForFormSubmissionItem, ShouldTreatAsContinuingLoad::No);
    else {
        ASSERT_NOT_REACHED();
        FRAMELOADER_RELEASE_LOG_ERROR(ResourceLoading, "retryAfterFailedCacheOnlyMainResourceLoad: Retrying load after failed cache-only main resource load failed because there is no provisional history item.");
    }
}

ResourceError FrameLoader::cancelledError(const ResourceRequest& request)
{
    ResourceError error = platformStrategies()->loaderStrategy()->cancelledError(request);
    error.setType(ResourceError::Type::Cancellation);
    return error;
}

ResourceError FrameLoader::blockedByContentBlockerError(const ResourceRequest& request)
{
    return platformStrategies()->loaderStrategy()->blockedByContentBlockerError(request);
}

ResourceError FrameLoader::blockedError(const ResourceRequest& request)
{
    ResourceError error = platformStrategies()->loaderStrategy()->blockedError(request);
    error.setType(ResourceError::Type::Cancellation);
    return error;
}

#if ENABLE(CONTENT_FILTERING)
ResourceError FrameLoader::blockedByContentFilterError(const ResourceRequest& request)
{
    ResourceError error = platformStrategies()->loaderStrategy()->blockedByContentFilterError(request);
    error.setType(ResourceError::Type::General);
    return error;
}
#endif

#if PLATFORM(IOS_FAMILY)
RetainPtr<CFDictionaryRef> FrameLoader::connectionProperties(ResourceLoader* loader)
{
    return m_client->connectionProperties(loader->documentLoader(), *loader->identifier());
}
#endif

ReferrerPolicy FrameLoader::effectiveReferrerPolicy() const
{
    if (RefPtr parentFrame = dynamicDowncast<LocalFrame>(m_frame->tree().parent()))
        return parentFrame->document()->referrerPolicy();
    if (RefPtr opener = dynamicDowncast<LocalFrame>(m_frame->opener()))
        return opener->document()->referrerPolicy();
    return ReferrerPolicy::Default;
}

String FrameLoader::referrer() const
{
    return m_documentLoader ? m_documentLoader->request().httpReferrer() : emptyString();
}

void FrameLoader::dispatchDidClearWindowObjectsInAllWorlds()
{
    if (!protectedFrame()->checkedScript()->canExecuteScripts(ReasonForCallingCanExecuteScripts::NotAboutToExecuteScript))
        return;

    Vector<Ref<DOMWrapperWorld>> worlds;
    ScriptController::getAllWorlds(worlds);
    for (auto& world : worlds)
        dispatchDidClearWindowObjectInWorld(world);
}

void FrameLoader::dispatchDidClearWindowObjectInWorld(DOMWrapperWorld& world)
{
    Ref frame = m_frame.get();
    if (!frame->checkedScript()->canExecuteScripts(ReasonForCallingCanExecuteScripts::NotAboutToExecuteScript) || !frame->protectedWindowProxy()->existingJSWindowProxy(world))
        return;

    m_client->dispatchDidClearWindowObjectInWorld(world);

    if (RefPtr page = frame->page())
        page->inspectorController().didClearWindowObjectInWorld(frame, world);

    InspectorInstrumentation::didClearWindowObjectInWorld(frame, world);
}

void FrameLoader::dispatchGlobalObjectAvailableInAllWorlds()
{
    Vector<Ref<DOMWrapperWorld>> worlds;
    ScriptController::getAllWorlds(worlds);
    for (auto& world : worlds)
        m_client->dispatchGlobalObjectAvailable(world);
}

void FrameLoader::didChangeTitle(DocumentLoader* loader)
{
    m_client->didChangeTitle(loader);

    if (loader == m_documentLoader) {
        // Must update the entries in the back-forward list too.
        history().setCurrentItemTitle(loader->title());
        // This must go through the WebFrame because it has the right notion of the current b/f item.
        m_client->setTitle(loader->title(), loader->urlForHistory());
        m_client->setMainFrameDocumentReady(true); // update observers with new DOMDocument
        m_client->dispatchDidReceiveTitle(loader->title());
    }

#if ENABLE(REMOTE_INSPECTOR)
    if (m_frame->isMainFrame())
        protectedFrame()->protectedPage()->remoteInspectorInformationDidChange();
#endif
}

void FrameLoader::dispatchDidCommitLoad(std::optional<HasInsecureContent> initialHasInsecureContent, std::optional<UsedLegacyTLS> initialUsedLegacyTLS, std::optional<WasPrivateRelayed> initialWasPrivateRelayed)
{
    if (m_stateMachine.creatingInitialEmptyDocument())
        return;

    m_client->dispatchDidCommitLoad(initialHasInsecureContent, initialUsedLegacyTLS, initialWasPrivateRelayed);

    if (RefPtr page = m_frame->page(); page && m_frame->isMainFrame())
        page->didCommitLoad();

    InspectorInstrumentation::didCommitLoad(protectedFrame(), protectedDocumentLoader().get());

#if ENABLE(REMOTE_INSPECTOR)
    if (RefPtr page = m_frame->page(); page && m_frame->isMainFrame())
        page->remoteInspectorInformationDidChange();
#endif
}

void FrameLoader::tellClientAboutPastMemoryCacheLoads()
{
    RefPtr page = m_frame->page();
    ASSERT(page);
    ASSERT(page->areMemoryCacheClientCallsEnabled());

    if (!m_documentLoader)
        return;

    RefPtr documentLoader = m_documentLoader;
    Vector<ResourceRequest> pastLoads;
    documentLoader->takeMemoryCacheLoadsForClientNotification(pastLoads);

    for (auto& pastLoad : pastLoads) {
        CachedResourceHandle resource = MemoryCache::singleton().resourceForRequest(pastLoad, page->sessionID());

        // FIXME: These loads, loaded from cache, but now gone from the cache by the time
        // Page::setMemoryCacheClientCallsEnabled(true) is called, will not be seen by the client.
        // Consider if there's some efficient way of remembering enough to deliver this client call.
        // We have the URL, but not the rest of the response or the length.
        if (!resource)
            continue;

        ResourceRequest request(URL { resource->url() });
        m_client->dispatchDidLoadResourceFromMemoryCache(documentLoader.get(), request, resource->response(), resource->encodedSize());
    }
}

NetworkingContext* FrameLoader::networkingContext() const
{
    return m_networkingContext.get();
}

RefPtr<NetworkingContext> FrameLoader::protectedNetworkingContext() const
{
    return m_networkingContext;
}

void FrameLoader::loadProgressingStatusChanged()
{
    if (RefPtr localFrame = dynamicDowncast<LocalFrame>(m_frame->mainFrame())) {
        if (RefPtr view = localFrame->view())
            view->loadProgressingStatusChanged();
    }
}

void FrameLoader::completePageTransitionIfNeeded()
{
    m_client->completePageTransitionIfNeeded();
}

void FrameLoader::setDocumentVisualUpdatesAllowed(bool allowed)
{
    m_client->setDocumentVisualUpdatesAllowed(allowed);
}

void FrameLoader::clearTestingOverrides()
{
    m_overrideCachePolicyForTesting = std::nullopt;
    m_overrideResourceLoadPriorityForTesting = std::nullopt;
    m_isStrictRawResourceValidationPolicyDisabledForTesting = false;
}

bool LocalFrameLoaderClient::hasHTMLView() const
{
    return true;
}

std::pair<RefPtr<Frame>, CreatedNewPage> createWindow(LocalFrame& openerFrame, FrameLoadRequest&& request, WindowFeatures&& features)
{
    ASSERT(!features.dialog || request.frameName().isEmpty());
    ASSERT(request.resourceRequest().httpMethod() == "GET"_s);

    // FIXME: Provide line number information with respect to the opener's document.
    if (request.resourceRequest().url().protocolIsJavaScript() && !openerFrame.protectedDocument()->checkedContentSecurityPolicy()->allowJavaScriptURLs(openerFrame.document()->url().string(), { }, request.resourceRequest().url().string(), nullptr))
        return { nullptr, CreatedNewPage::No };

    if (!request.frameName().isEmpty() && !isBlankTargetFrameName(request.frameName())) {
        if (RefPtr frame = openerFrame.loader().findFrameForNavigation(request.frameName(), openerFrame.protectedDocument().get())) {
            if (!isSelfTargetFrameName(request.frameName())) {
                if (RefPtr page = frame->page(); page && isInVisibleAndActivePage(openerFrame))
                    page->chrome().focus();
            }
            frame->updateOpener(openerFrame);
            return { frame, CreatedNewPage::No };
        }
    }

    // Sandboxed frames cannot open new auxiliary browsing contexts.
    if (isDocumentSandboxed(openerFrame, SandboxFlag::Popups)) {
        // FIXME: This message should be moved off the console once a solution to https://bugs.webkit.org/show_bug.cgi?id=103274 exists.
        openerFrame.protectedDocument()->addConsoleMessage(MessageSource::Security, MessageLevel::Error, makeString("Blocked opening '"_s, request.resourceRequest().url().stringCenterEllipsizedToLength(), "' in a new window because the request was made in a sandboxed frame whose 'allow-popups' permission is not set."_s));
        return { nullptr, CreatedNewPage::No };
    }

    // FIXME: Setting the referrer should be the caller's responsibility.
    String referrer = features.wantsNoReferrer() ? String() :  SecurityPolicy::generateReferrerHeader(openerFrame.document()->referrerPolicy(), request.resourceRequest().url(), openerFrame.loader().outgoingReferrerURL(), OriginAccessPatternsForWebProcess::singleton());
    if (!referrer.isEmpty())
        request.resourceRequest().setHTTPReferrer(referrer);
    FrameLoader::addSameSiteInfoToRequestIfNeeded(request.resourceRequest(), openerFrame.protectedDocument().get());

    RefPtr oldPage = openerFrame.page();
    if (!oldPage)
        return { nullptr, CreatedNewPage::No };

#if PLATFORM(GTK)
    features.oldWindowRect = oldPage->chrome().windowRect();
#endif

    String openedMainFrameName = isBlankTargetFrameName(request.frameName()) ? String() : request.frameName();
    ShouldOpenExternalURLsPolicy shouldOpenExternalURLsPolicy = shouldOpenExternalURLsPolicyToApply(openerFrame, request);
    NavigationAction action { request.requester(), request.resourceRequest(), request.initiatedByMainFrame(), request.isRequestFromClientOrUserInput(), NavigationType::Other, shouldOpenExternalURLsPolicy };
    action.setNewFrameOpenerPolicy(features.wantsNoOpener() ? NewFrameOpenerPolicy::Suppress : NewFrameOpenerPolicy::Allow);
    RefPtr page = oldPage->chrome().createWindow(openerFrame, openedMainFrameName, features, action);
    if (!page)
        return { nullptr, CreatedNewPage::No };

    Ref frame = page->mainFrame();

    if (!frame->page())
        return { nullptr, CreatedNewPage::No };

    return { WTFMove(frame), CreatedNewPage::Yes };
}

// At the moment, we do not actually create a new browsing context / frame. We merely make it so that existing windowProxy for the
// current browsing context lose their browsing context. We also clear properties of the frame (opener, openees, name), so that it
// appears the same as a new browsing context.
void FrameLoader::switchBrowsingContextsGroup()
{
    // Disown opener.
    Ref frame = m_frame.get();
    frame->disownOpener();
    if (RefPtr page = m_frame->page())
        page->setOpenedByDOMWithOpener(false);

    frame->detachFromAllOpenedFrames();

    frame->tree().clearName();

    // Make sure we use fresh Window proxies. The old window proxies will keep pointing to the old window which will be frameless when
    // a new window is created for this frame.
    frame->resetScript();

    // On same-origin navigation from the initial empty document, we normally reuse the window for the new document. We need to prevent
    // this when we want to isolate so old window proxies will indeed start pointing to a frameless window and appear closed.
    if (RefPtr window = frame->window())
        window->setMayReuseForNavigation(false);
}

bool FrameLoader::shouldSuppressTextInputFromEditing() const
{
    return m_frame->settings().shouldSuppressTextInputFromEditingDuringProvisionalNavigation() && m_state == FrameState::Provisional;
}

void FrameLoader::advanceStatePastInitialEmptyDocument()
{
    if (stateMachine().committingFirstRealLoad())
        stateMachine().advanceTo(FrameLoaderStateMachine::DisplayingInitialEmptyDocumentPostCommit);
    if (stateMachine().isDisplayingInitialEmptyDocument() && stateMachine().committedFirstRealDocumentLoad())
        stateMachine().advanceTo(FrameLoaderStateMachine::CommittedFirstRealLoad);
}

RefPtr<DocumentLoader> FrameLoader::protectedDocumentLoader() const
{
    return m_documentLoader;
}

RefPtr<DocumentLoader> FrameLoader::protectedProvisionalDocumentLoader() const
{
    return m_provisionalDocumentLoader;
}

RefPtr<DocumentLoader> FrameLoader::loaderForWebsitePolicies(CanIncludeCurrentDocumentLoader canIncludeCurrentDocumentLoader) const
{
    RefPtr loader = policyDocumentLoader();
    if (!loader)
        loader = provisionalDocumentLoader();
    if (!loader && canIncludeCurrentDocumentLoader == CanIncludeCurrentDocumentLoader::Yes)
        loader = documentLoader();
    return loader;
}

void FrameLoader::prefetchDNSIfNeeded(const URL& url)
{
#if ENABLE(CONTENT_EXTENSIONS)
    RefPtr page = m_frame->page();
    if (!page)
        return;

    RefPtr documentLoader = m_documentLoader;
    if (!documentLoader)
        return;

    auto results = page->protectedUserContentProvider()->processContentRuleListsForLoad(*page, url, ContentExtensions::ResourceType::Ping, *documentLoader);
    if (results.shouldBlock())
        return;
#endif

    if (url.isValid() && !url.isEmpty() && url.protocolIsInHTTPFamily())
        client().prefetchDNS(url.host().toString());
}

} // namespace WebCore

#undef PAGE_ID
#undef FRAME_ID
