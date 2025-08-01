/*
 * Copyright (C) 2010-2025 Apple Inc. All rights reserved.
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
#include "WKPage.h"
#include "WKPagePrivate.h"

#include "APIArray.h"
#include "APIContextMenuClient.h"
#include "APIData.h"
#include "APIDictionary.h"
#include "APIFindClient.h"
#include "APIFindMatchesClient.h"
#include "APIFrameHandle.h"
#include "APIFrameInfo.h"
#include "APIGeometry.h"
#include "APIHitTestResult.h"
#include "APILoaderClient.h"
#include "APINavigation.h"
#include "APINavigationAction.h"
#include "APINavigationClient.h"
#include "APINavigationResponse.h"
#include "APIOpenPanelParameters.h"
#include "APIPageConfiguration.h"
#include "APIPolicyClient.h"
#include "APISerializedScriptValue.h"
#include "APISessionState.h"
#include "APIUIClient.h"
#include "APIWebAuthenticationPanel.h"
#include "APIWebAuthenticationPanelClient.h"
#include "APIWebsitePolicies.h"
#include "APIWindowFeatures.h"
#include "AuthenticationChallengeDisposition.h"
#include "AuthenticationChallengeProxy.h"
#include "AuthenticationDecisionListener.h"
#include "ContentAsStringIncludesChildFrames.h"
#include "DownloadProxy.h"
#include "GeolocationPermissionRequestProxy.h"
#include "JavaScriptEvaluationResult.h"
#include "LegacySessionStateCoding.h"
#include "Logging.h"
#include "MediaKeySystemPermissionRequest.h"
#include "MessageSenderInlines.h"
#include "NativeWebKeyboardEvent.h"
#include "NativeWebWheelEvent.h"
#include "NavigationActionData.h"
#include "NotificationPermissionRequest.h"
#include "PageClient.h"
#include "PageLoadState.h"
#include "PrintInfo.h"
#include "ProcessTerminationReason.h"
#include "QueryPermissionResultCallback.h"
#include "RunJavaScriptParameters.h"
#include "SpeechRecognitionPermissionRequest.h"
#include "UserMediaPermissionCheckProxy.h"
#include "UserMediaPermissionRequestProxy.h"
#include "WKAPICast.h"
#include "WKPagePolicyClientInternal.h"
#include "WKPageRenderingProgressEventsInternal.h"
#include "WKPluginInformation.h"
#include "WebBackForwardList.h"
#include "WebFormClient.h"
#include "WebFrameProxy.h"
#include "WebFullScreenManagerProxy.h"
#include "WebImage.h"
#include "WebInspectorUIProxy.h"
#include "WebOpenPanelResultListenerProxy.h"
#include "WebPageDiagnosticLoggingClient.h"
#include "WebPageGroup.h"
#include "WebPageMessages.h"
#include "WebPageProxy.h"
#include "WebPageProxyTesting.h"
#include "WebProcessPool.h"
#include "WebProcessProxy.h"
#include "WebProtectionSpace.h"
#include <WebCore/AuthenticatorAssertionResponse.h>
#include <WebCore/AutoplayEvent.h>
#include <WebCore/ContentRuleListResults.h>
#include <WebCore/FrameLoaderClient.h>
#include <WebCore/MockRealtimeMediaSourceCenter.h>
#include <WebCore/OrganizationStorageAccessPromptQuirk.h>
#include <WebCore/Page.h>
#include <WebCore/Permissions.h>
#include <WebCore/RunJavaScriptParameters.h>
#include <WebCore/SecurityOrigin.h>
#include <WebCore/SecurityOriginData.h>
#include <WebCore/SerializedCryptoKeyWrap.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/WindowFeatures.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMallocInlines.h>

#ifdef __BLOCKS__
#include <Block.h>
#endif

#if ENABLE(CONTEXT_MENUS)
#include "WebContextMenuItem.h"
#endif

#if PLATFORM(COCOA)
#include "DefaultWebBrowserChecks.h"
#include <wtf/cocoa/RuntimeApplicationChecksCocoa.h>
#endif

namespace API {
using namespace WebCore;
using namespace WebKit;

#if ENABLE(CONTEXT_MENUS)
static Vector<RefPtr<API::Object>> toAPIObjectVector(const Vector<Ref<WebContextMenuItem>>& menuItemsVector)
{
    return menuItemsVector.map([](auto& menuItem) -> RefPtr<API::Object> {
        return menuItem.ptr();
    });
}
#endif
    
template<> struct ClientTraits<WKPageLoaderClientBase> {
    typedef std::tuple<WKPageLoaderClientV0, WKPageLoaderClientV1, WKPageLoaderClientV2, WKPageLoaderClientV3, WKPageLoaderClientV4, WKPageLoaderClientV5, WKPageLoaderClientV6> Versions;
};

template<> struct ClientTraits<WKPageNavigationClientBase> {
    typedef std::tuple<WKPageNavigationClientV0, WKPageNavigationClientV1, WKPageNavigationClientV2, WKPageNavigationClientV3> Versions;
};

template<> struct ClientTraits<WKPagePolicyClientBase> {
    typedef std::tuple<WKPagePolicyClientV0, WKPagePolicyClientV1, WKPagePolicyClientInternal> Versions;
};

template<> struct ClientTraits<WKPageUIClientBase> {
    typedef std::tuple<WKPageUIClientV0, WKPageUIClientV1, WKPageUIClientV2, WKPageUIClientV3, WKPageUIClientV4, WKPageUIClientV5, WKPageUIClientV6, WKPageUIClientV7, WKPageUIClientV8, WKPageUIClientV9, WKPageUIClientV10, WKPageUIClientV11, WKPageUIClientV12, WKPageUIClientV13, WKPageUIClientV14, WKPageUIClientV15, WKPageUIClientV16, WKPageUIClientV17, WKPageUIClientV18, WKPageUIClientV19> Versions;
};

template<> struct ClientTraits<WKPageFullScreenClientBase> {
    typedef std::tuple<WKPageFullScreenClientV0> Versions;
};

#if ENABLE(CONTEXT_MENUS)
template<> struct ClientTraits<WKPageContextMenuClientBase> {
    typedef std::tuple<WKPageContextMenuClientV0, WKPageContextMenuClientV1, WKPageContextMenuClientV2, WKPageContextMenuClientV3, WKPageContextMenuClientV4> Versions;
};
#endif

template<> struct ClientTraits<WKPageFindClientBase> {
    typedef std::tuple<WKPageFindClientV0> Versions;
};

template<> struct ClientTraits<WKPageFindMatchesClientBase> {
    typedef std::tuple<WKPageFindMatchesClientV0> Versions;
};

template<> struct ClientTraits<WKPageStateClientBase> {
    typedef std::tuple<WKPageStateClientV0> Versions;
};
    
} // namespace API

using namespace WebKit;

NO_RETURN_DUE_TO_CRASH NEVER_INLINE static void crashBecausePageIsSuspended()
{
    WTFLogAlways("Error: Attempt to call WKPageRef API/SPI on a suspended page, this is a client bug.");
    CRASH();
}

#define CRASH_IF_SUSPENDED if (pageRef && toProtectedImpl(pageRef)->isSuspended()) [[unlikely]] \
    crashBecausePageIsSuspended()

WKTypeID WKPageGetTypeID()
{
    return toAPI(WebPageProxy::APIType);
}

WKContextRef WKPageGetContext(WKPageRef pageRef)
{
    return toAPI(toProtectedImpl(pageRef)->configuration().protectedProcessPool().ptr());
}

WKPageGroupRef WKPageGetPageGroup(WKPageRef pageRef)
{
    return nullptr;
}

WKPageConfigurationRef WKPageCopyPageConfiguration(WKPageRef pageRef)
{
    return toAPILeakingRef(toProtectedImpl(pageRef)->configuration().copy());
}

void WKPageLoadURL(WKPageRef pageRef, WKURLRef URLRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->loadRequest(URL { toWTFString(URLRef) });
}

void WKPageLoadURLWithShouldOpenExternalURLsPolicy(WKPageRef pageRef, WKURLRef URLRef, bool shouldOpenExternalURLs)
{
    CRASH_IF_SUSPENDED;
    WebCore::ShouldOpenExternalURLsPolicy shouldOpenExternalURLsPolicy = shouldOpenExternalURLs ? WebCore::ShouldOpenExternalURLsPolicy::ShouldAllow : WebCore::ShouldOpenExternalURLsPolicy::ShouldNotAllow;
    toProtectedImpl(pageRef)->loadRequest(URL { toWTFString(URLRef) }, shouldOpenExternalURLsPolicy);
}

void WKPageLoadURLWithUserData(WKPageRef pageRef, WKURLRef URLRef, WKTypeRef userDataRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->loadRequest(URL { toWTFString(URLRef) }, WebCore::ShouldOpenExternalURLsPolicy::ShouldNotAllow, WebCore::IsPerformingHTTPFallback::No, nullptr, toProtectedImpl(userDataRef).get());
}

void WKPageLoadURLRequest(WKPageRef pageRef, WKURLRequestRef urlRequestRef)
{
    CRASH_IF_SUSPENDED;
    auto resourceRequest = toProtectedImpl(urlRequestRef)->resourceRequest();
    toProtectedImpl(pageRef)->loadRequest(WTFMove(resourceRequest));
}

void WKPageLoadURLRequestWithUserData(WKPageRef pageRef, WKURLRequestRef urlRequestRef, WKTypeRef userDataRef)
{
    CRASH_IF_SUSPENDED;
    auto resourceRequest = toProtectedImpl(urlRequestRef)->resourceRequest();
    toProtectedImpl(pageRef)->loadRequest(WTFMove(resourceRequest), WebCore::ShouldOpenExternalURLsPolicy::ShouldNotAllow, WebCore::IsPerformingHTTPFallback::No, nullptr, toProtectedImpl(userDataRef).get());
}

void WKPageLoadFile(WKPageRef pageRef, WKURLRef fileURL, WKURLRef resourceDirectoryURL)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->loadFile(toWTFString(fileURL), toWTFString(resourceDirectoryURL));
}

void WKPageLoadFileWithUserData(WKPageRef pageRef, WKURLRef fileURL, WKURLRef resourceDirectoryURL, WKTypeRef userDataRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->loadFile(toWTFString(fileURL), toWTFString(resourceDirectoryURL), toProtectedImpl(userDataRef).get());
}

void WKPageLoadData(WKPageRef pageRef, WKDataRef dataRef, WKStringRef MIMETypeRef, WKStringRef encodingRef, WKURLRef baseURLRef)
{
    CRASH_IF_SUSPENDED;
    // FIXME: Use WebCore::DataSegment::Provider to remove this unnecessary copy.
    toProtectedImpl(pageRef)->loadData(WebCore::SharedBuffer::create(toProtectedImpl(dataRef)->span()), toWTFString(MIMETypeRef), toWTFString(encodingRef), toWTFString(baseURLRef));
}

void WKPageLoadDataWithUserData(WKPageRef pageRef, WKDataRef dataRef, WKStringRef MIMETypeRef, WKStringRef encodingRef, WKURLRef baseURLRef, WKTypeRef userDataRef)
{
    CRASH_IF_SUSPENDED;
    // FIXME: Use WebCore::DataSegment::Provider to remove this unnecessary copy.
    toProtectedImpl(pageRef)->loadData(WebCore::SharedBuffer::create(toProtectedImpl(dataRef)->span()), toWTFString(MIMETypeRef), toWTFString(encodingRef), toWTFString(baseURLRef), toProtectedImpl(userDataRef).get());
}

static String encodingOf(const String& string)
{
    if (string.isNull() || !string.is8Bit())
        return "utf-16"_s;
    return "latin1"_s;
}

static std::span<const uint8_t> dataFrom(const String& string)
{
    if (string.isNull() || !string.is8Bit())
        return asBytes(string.span16());
    return string.span8();
}

static Ref<WebCore::DataSegment> dataReferenceFrom(const String& string)
{
    return WebCore::DataSegment::create(WebCore::DataSegment::Provider { [span = dataFrom(string)]() {
        return span;
    } });
}

static void loadString(WKPageRef pageRef, WKStringRef stringRef, const String& mimeType, const String& baseURL, WKTypeRef userDataRef)
{
    String string = toWTFString(stringRef);
    // FIXME: Use WebCore::DataSegment::Provider to remove this unnecessary copy.
    toProtectedImpl(pageRef)->loadData(WebCore::SharedBuffer::create(dataFrom(string)), mimeType, encodingOf(string), baseURL, toProtectedImpl(userDataRef).get());
}

void WKPageLoadHTMLString(WKPageRef pageRef, WKStringRef htmlStringRef, WKURLRef baseURLRef)
{
    CRASH_IF_SUSPENDED;
    WKPageLoadHTMLStringWithUserData(pageRef, htmlStringRef, baseURLRef, nullptr);
}

void WKPageLoadHTMLStringWithUserData(WKPageRef pageRef, WKStringRef htmlStringRef, WKURLRef baseURLRef, WKTypeRef userDataRef)
{
    CRASH_IF_SUSPENDED;
    loadString(pageRef, htmlStringRef, "text/html"_s, toWTFString(baseURLRef), userDataRef);
}

void WKPageLoadAlternateHTMLString(WKPageRef pageRef, WKStringRef htmlStringRef, WKURLRef baseURLRef, WKURLRef unreachableURLRef)
{
    CRASH_IF_SUSPENDED;
    WKPageLoadAlternateHTMLStringWithUserData(pageRef, htmlStringRef, baseURLRef, unreachableURLRef, nullptr);
}

void WKPageLoadAlternateHTMLStringWithUserData(WKPageRef pageRef, WKStringRef htmlStringRef, WKURLRef baseURLRef, WKURLRef unreachableURLRef, WKTypeRef userDataRef)
{
    CRASH_IF_SUSPENDED;
    String string = toWTFString(htmlStringRef);
    toProtectedImpl(pageRef)->loadAlternateHTML(dataReferenceFrom(string), encodingOf(string), URL { toWTFString(baseURLRef) }, URL { toWTFString(unreachableURLRef) }, toProtectedImpl(userDataRef).get());
}

void WKPageLoadPlainTextString(WKPageRef pageRef, WKStringRef plainTextStringRef)
{
    CRASH_IF_SUSPENDED;
    WKPageLoadPlainTextStringWithUserData(pageRef, plainTextStringRef, nullptr);
}

void WKPageLoadPlainTextStringWithUserData(WKPageRef pageRef, WKStringRef plainTextStringRef, WKTypeRef userDataRef)
{
    CRASH_IF_SUSPENDED;
    loadString(pageRef, plainTextStringRef, "text/plain"_s, aboutBlankURL().string(), userDataRef);
}

void WKPageLoadWebArchiveData(WKPageRef, WKDataRef)
{
}

void WKPageLoadWebArchiveDataWithUserData(WKPageRef, WKDataRef, WKTypeRef)
{
}

void WKPageStopLoading(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->stopLoading();
}

void WKPageReload(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    OptionSet<WebCore::ReloadOption> reloadOptions;
#if PLATFORM(COCOA)
    if (linkedOnOrAfterSDKWithBehavior(SDKAlignedBehavior::ExpiredOnlyReloadBehavior))
        reloadOptions.add(WebCore::ReloadOption::ExpiredOnly);
#endif

    toProtectedImpl(pageRef)->reload(reloadOptions);
}

void WKPageReloadWithoutContentBlockers(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->reload(WebCore::ReloadOption::DisableContentBlockers);
}

void WKPageReloadFromOrigin(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->reload(WebCore::ReloadOption::FromOrigin);
}

void WKPageReloadExpiredOnly(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->reload(WebCore::ReloadOption::ExpiredOnly);
}

bool WKPageTryClose(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    return toProtectedImpl(pageRef)->tryClose();
}

void WKPagePermissionChanged(WKStringRef permissionName, WKStringRef originString)
{
    auto name = WebCore::Permissions::toPermissionName(toWTFString(permissionName));
    if (!name)
        return;

    auto topOrigin = WebCore::SecurityOrigin::createFromString(toWTFString(originString))->data();
    WebKit::WebProcessProxy::permissionChanged(*name, topOrigin);
}

void WKPageClose(WKPageRef pageRef)
{
    toProtectedImpl(pageRef)->close();
}

bool WKPageIsClosed(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->isClosed();
}

void WKPageGoForward(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->goForward();
}

bool WKPageCanGoForward(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->backForwardList().forwardItem();
}

void WKPageGoBack(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    Ref page = *toProtectedImpl(pageRef);
    if (RefPtr pageClient = page->pageClient(); pageClient->hasBrowsingWarning()) {
        WKPageReload(pageRef);
        return;
    }
    page->goBack();
}

bool WKPageCanGoBack(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->backForwardList().backItem();
}

void WKPageGoToBackForwardListItem(WKPageRef pageRef, WKBackForwardListItemRef itemRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->goToBackForwardItem(*toProtectedImpl(itemRef));
}

void WKPageTryRestoreScrollPosition(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->tryRestoreScrollPosition();
}

WKBackForwardListRef WKPageGetBackForwardList(WKPageRef pageRef)
{
    return toAPI(&toProtectedImpl(pageRef)->backForwardList());
}

bool WKPageWillHandleHorizontalScrollEvents(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->willHandleHorizontalScrollEvents();
}

void WKPageUpdateWebsitePolicies(WKPageRef pageRef, WKWebsitePoliciesRef websitePoliciesRef)
{
    CRASH_IF_SUSPENDED;
    RELEASE_ASSERT_WITH_MESSAGE(!toProtectedImpl(websitePoliciesRef)->websiteDataStore(), "Setting WebsitePolicies.websiteDataStore is only supported during WKFramePolicyListenerUseWithPolicies().");
    RELEASE_ASSERT_WITH_MESSAGE(!toProtectedImpl(websitePoliciesRef)->userContentController(), "Setting WebsitePolicies.userContentController is only supported during WKFramePolicyListenerUseWithPolicies().");
    auto data = toProtectedImpl(websitePoliciesRef)->data();
    toProtectedImpl(pageRef)->updateWebsitePolicies(WTFMove(data));
}

WKStringRef WKPageCopyTitle(WKPageRef pageRef)
{
    return toCopiedAPI(toProtectedImpl(pageRef)->protectedPageLoadState()->title());
}

WKFrameRef WKPageGetMainFrame(WKPageRef pageRef)
{
    return toAPI(toProtectedImpl(pageRef)->protectedMainFrame().get());
}

WKFrameRef WKPageGetFocusedFrame(WKPageRef pageRef)
{
    return toAPI(toProtectedImpl(pageRef)->protectedFocusedFrame().get());
}

WKFrameRef WKPageGetFrameSetLargestFrame(WKPageRef pageRef)
{
    return nullptr;
}

uint64_t WKPageGetRenderTreeSize(WKPageRef page)
{
    return toProtectedImpl(page)->renderTreeSize();
}

WKWebsiteDataStoreRef WKPageGetWebsiteDataStore(WKPageRef page)
{
    return toAPI(toProtectedImpl(page)->protectedWebsiteDataStore().ptr());
}

WKInspectorRef WKPageGetInspector(WKPageRef pageRef)
{
    return toAPI(toProtectedImpl(pageRef)->protectedInspector().get());
}

double WKPageGetEstimatedProgress(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->estimatedProgress();
}

WKStringRef WKPageCopyUserAgent(WKPageRef pageRef)
{
    return toCopiedAPI(toProtectedImpl(pageRef)->userAgent());
}

WKStringRef WKPageCopyApplicationNameForUserAgent(WKPageRef pageRef)
{
    return toCopiedAPI(toProtectedImpl(pageRef)->applicationNameForUserAgent());
}

void WKPageSetApplicationNameForUserAgent(WKPageRef pageRef, WKStringRef applicationNameRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setApplicationNameForUserAgent(toWTFString(applicationNameRef));
}

WKStringRef WKPageCopyCustomUserAgent(WKPageRef pageRef)
{
    return toCopiedAPI(toProtectedImpl(pageRef)->customUserAgent());
}

void WKPageSetCustomUserAgent(WKPageRef pageRef, WKStringRef userAgentRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setCustomUserAgent(toWTFString(userAgentRef));
}

bool WKPageSupportsTextEncoding(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->supportsTextEncoding();
}

WKStringRef WKPageCopyCustomTextEncodingName(WKPageRef pageRef)
{
    return toCopiedAPI(toProtectedImpl(pageRef)->customTextEncodingName());
}

void WKPageSetCustomTextEncodingName(WKPageRef pageRef, WKStringRef encodingNameRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setCustomTextEncodingName(toWTFString(encodingNameRef));
}

void WKPageTerminate(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    Ref<WebProcessProxy> protectedProcessProxy(toProtectedImpl(pageRef)->legacyMainFrameProcess());
    protectedProcessProxy->requestTermination(ProcessTerminationReason::RequestedByClient);
}

void WKPageResetStateBetweenTests(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    if (RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting())
        pageForTesting->resetStateBetweenTests();
}

WKStringRef WKPageGetSessionHistoryURLValueType()
{
    static NeverDestroyed<Ref<API::String>> sessionHistoryURLValueType = API::String::create("SessionHistoryURL"_s);
    return toAPI(sessionHistoryURLValueType.get().ptr());
}

WKStringRef WKPageGetSessionBackForwardListItemValueType()
{
    static NeverDestroyed<Ref<API::String>> sessionBackForwardListValueType = API::String::create("SessionBackForwardListItem"_s);
    return toAPI(sessionBackForwardListValueType.get().ptr());
}

WKTypeRef WKPageCopySessionState(WKPageRef pageRef, void* context, WKPageSessionStateFilterCallback filter)
{
    // FIXME: This is a hack to make sure we return a WKDataRef to maintain compatibility with older versions of Safari.
    bool shouldReturnData = !(reinterpret_cast<uintptr_t>(context) & 1);
    context = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(context) & ~1);

    auto sessionState = toProtectedImpl(pageRef)->sessionState([pageRef, context, filter](WebBackForwardListItem& item) {
        if (filter) {
            if (!filter(pageRef, WKPageGetSessionBackForwardListItemValueType(), toAPI(&item), context))
                return false;

            if (!filter(pageRef, WKPageGetSessionHistoryURLValueType(), toURLRef(item.originalURL().impl()), context))
                return false;
        }

        return true;
    });

    auto data = encodeLegacySessionState(sessionState);
    if (shouldReturnData)
        return toAPILeakingRef(WTFMove(data));

    return toAPILeakingRef(API::SessionState::create(WTFMove(sessionState)));
}

static void restoreFromSessionState(WKPageRef pageRef, WKTypeRef sessionStateRef, bool navigate)
{
    SessionState sessionState;

    // FIXME: This is for backwards compatibility with Safari. Remove it once Safari no longer depends on it.
    if (toProtectedImpl(sessionStateRef)->type() == API::Object::Type::Data) {
        if (!decodeLegacySessionState(toProtectedImpl(static_cast<WKDataRef>(sessionStateRef))->span(), sessionState))
            return;
    } else {
        ASSERT(toProtectedImpl(sessionStateRef)->type() == API::Object::Type::SessionState);

        sessionState = toProtectedImpl(static_cast<WKSessionStateRef>(sessionStateRef))->sessionState();
    }

    toProtectedImpl(pageRef)->restoreFromSessionState(WTFMove(sessionState), navigate);
}

void WKPageRestoreFromSessionState(WKPageRef pageRef, WKTypeRef sessionStateRef)
{
    CRASH_IF_SUSPENDED;
    restoreFromSessionState(pageRef, sessionStateRef, true);
}

void WKPageRestoreFromSessionStateWithoutNavigation(WKPageRef pageRef, WKTypeRef sessionStateRef)
{
    CRASH_IF_SUSPENDED;
    restoreFromSessionState(pageRef, sessionStateRef, false);
}

double WKPageGetTextZoomFactor(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->textZoomFactor();
}

double WKPageGetBackingScaleFactor(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->deviceScaleFactor();
}

void WKPageSetCustomBackingScaleFactor(WKPageRef pageRef, double customScaleFactor)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setCustomDeviceScaleFactor(customScaleFactor, [] { });
}

void WKPageSetCustomBackingScaleFactorWithCallback(WKPageRef pageRef, double customScaleFactor, void* context, WKPageSetCustomBackingScaleFactorFunction completionHandler)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setCustomDeviceScaleFactor(customScaleFactor, [context, completionHandler] {
        completionHandler(context);
    });
}

bool WKPageSupportsTextZoom(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->supportsTextZoom();
}

void WKPageSetTextZoomFactor(WKPageRef pageRef, double zoomFactor)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setTextZoomFactor(zoomFactor);
}

double WKPageGetPageZoomFactor(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->pageZoomFactor();
}

void WKPageSetPageZoomFactor(WKPageRef pageRef, double zoomFactor)
{
    CRASH_IF_SUSPENDED;
    if (zoomFactor <= 0)
        return;
    toProtectedImpl(pageRef)->setPageZoomFactor(zoomFactor);
}

void WKPageSetPageAndTextZoomFactors(WKPageRef pageRef, double pageZoomFactor, double textZoomFactor)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setPageAndTextZoomFactors(pageZoomFactor, textZoomFactor);
}

void WKPageSetScaleFactor(WKPageRef pageRef, double scale, WKPoint origin)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->scalePage(scale, toIntPoint(origin), [] { });
}

double WKPageGetScaleFactor(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->pageScaleFactor();
}

void WKPageSetUseFixedLayout(WKPageRef pageRef, bool fixed)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setUseFixedLayout(fixed);
}

void WKPageSetFixedLayoutSize(WKPageRef pageRef, WKSize size)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setFixedLayoutSize(toIntSize(size));
}

bool WKPageUseFixedLayout(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->useFixedLayout();
}

WKSize WKPageFixedLayoutSize(WKPageRef pageRef)
{
    return toAPI(toProtectedImpl(pageRef)->fixedLayoutSize());
}

void WKPageListenForLayoutMilestones(WKPageRef pageRef, WKLayoutMilestones milestones)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->listenForLayoutMilestones(toLayoutMilestones(milestones));
}

bool WKPageHasHorizontalScrollbar(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->hasHorizontalScrollbar();
}

bool WKPageHasVerticalScrollbar(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->hasVerticalScrollbar();
}

void WKPageSetSuppressScrollbarAnimations(WKPageRef pageRef, bool suppressAnimations)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setSuppressScrollbarAnimations(suppressAnimations);
}

bool WKPageAreScrollbarAnimationsSuppressed(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->areScrollbarAnimationsSuppressed();
}

bool WKPageIsPinnedToLeftSide(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->pinnedState().left();
}

bool WKPageIsPinnedToRightSide(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->pinnedState().right();
}

bool WKPageIsPinnedToTopSide(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->pinnedState().top();
}

bool WKPageIsPinnedToBottomSide(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->pinnedState().bottom();
}

bool WKPageRubberBandsAtLeft(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->rubberBandableEdges().left();
}

void WKPageSetRubberBandsAtLeft(WKPageRef pageRef, bool rubberBandsAtLeft)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setRubberBandsAtLeft(rubberBandsAtLeft);
}

bool WKPageRubberBandsAtRight(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->rubberBandableEdges().right();
}

void WKPageSetRubberBandsAtRight(WKPageRef pageRef, bool rubberBandsAtRight)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setRubberBandsAtRight(rubberBandsAtRight);
}

bool WKPageRubberBandsAtTop(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->rubberBandableEdges().top();
}

void WKPageSetRubberBandsAtTop(WKPageRef pageRef, bool rubberBandsAtTop)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setRubberBandsAtTop(rubberBandsAtTop);
}

bool WKPageRubberBandsAtBottom(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->rubberBandableEdges().bottom();
}

void WKPageSetRubberBandsAtBottom(WKPageRef pageRef, bool rubberBandsAtBottom)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setRubberBandsAtBottom(rubberBandsAtBottom);
}

bool WKPageVerticalRubberBandingIsEnabled(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->verticalRubberBandingIsEnabled();
}

void WKPageSetEnableVerticalRubberBanding(WKPageRef pageRef, bool enableVerticalRubberBanding)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setEnableVerticalRubberBanding(enableVerticalRubberBanding);
}

bool WKPageHorizontalRubberBandingIsEnabled(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->horizontalRubberBandingIsEnabled();
}

void WKPageSetEnableHorizontalRubberBanding(WKPageRef pageRef, bool enableHorizontalRubberBanding)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setEnableHorizontalRubberBanding(enableHorizontalRubberBanding);
}

void WKPageSetBackgroundExtendsBeyondPage(WKPageRef pageRef, bool backgroundExtendsBeyondPage)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setBackgroundExtendsBeyondPage(backgroundExtendsBeyondPage);
}

bool WKPageBackgroundExtendsBeyondPage(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->backgroundExtendsBeyondPage();
}

void WKPageSetPaginationMode(WKPageRef pageRef, WKPaginationMode paginationMode)
{
    CRASH_IF_SUSPENDED;
    WebCore::Pagination::Mode mode;
    switch (paginationMode) {
    case kWKPaginationModeUnpaginated:
        mode = WebCore::Pagination::Mode::Unpaginated;
        break;
    case kWKPaginationModeLeftToRight:
        mode = WebCore::Pagination::Mode::LeftToRightPaginated;
        break;
    case kWKPaginationModeRightToLeft:
        mode = WebCore::Pagination::Mode::RightToLeftPaginated;
        break;
    case kWKPaginationModeTopToBottom:
        mode = WebCore::Pagination::Mode::TopToBottomPaginated;
        break;
    case kWKPaginationModeBottomToTop:
        mode = WebCore::Pagination::Mode::BottomToTopPaginated;
        break;
    default:
        return;
    }
    toProtectedImpl(pageRef)->setPaginationMode(mode);
}

WKPaginationMode WKPageGetPaginationMode(WKPageRef pageRef)
{
    switch (toProtectedImpl(pageRef)->paginationMode()) {
    case WebCore::Pagination::Mode::Unpaginated:
        return kWKPaginationModeUnpaginated;
    case WebCore::Pagination::Mode::LeftToRightPaginated:
        return kWKPaginationModeLeftToRight;
    case WebCore::Pagination::Mode::RightToLeftPaginated:
        return kWKPaginationModeRightToLeft;
    case WebCore::Pagination::Mode::TopToBottomPaginated:
        return kWKPaginationModeTopToBottom;
    case WebCore::Pagination::Mode::BottomToTopPaginated:
        return kWKPaginationModeBottomToTop;
    }

    ASSERT_NOT_REACHED();
    return kWKPaginationModeUnpaginated;
}

void WKPageSetPaginationBehavesLikeColumns(WKPageRef pageRef, bool behavesLikeColumns)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setPaginationBehavesLikeColumns(behavesLikeColumns);
}

bool WKPageGetPaginationBehavesLikeColumns(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->paginationBehavesLikeColumns();
}

void WKPageSetPageLength(WKPageRef pageRef, double pageLength)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setPageLength(pageLength);
}

double WKPageGetPageLength(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->pageLength();
}

void WKPageSetGapBetweenPages(WKPageRef pageRef, double gap)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setGapBetweenPages(gap);
}

double WKPageGetGapBetweenPages(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->gapBetweenPages();
}

void WKPageSetPaginationLineGridEnabled(WKPageRef, bool)
{
}

bool WKPageGetPaginationLineGridEnabled(WKPageRef)
{
    return false;
}

unsigned WKPageGetPageCount(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->pageCount();
}

bool WKPageCanDelete(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->canDelete();
}

bool WKPageHasSelectedRange(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->hasSelectedRange();
}

bool WKPageIsContentEditable(WKPageRef pageRef)
{
    return toProtectedImpl(pageRef)->isContentEditable();
}

void WKPageSetMaintainsInactiveSelection(WKPageRef pageRef, bool newValue)
{
    CRASH_IF_SUSPENDED;
    return toProtectedImpl(pageRef)->setMaintainsInactiveSelection(newValue);
}

void WKPageCenterSelectionInVisibleArea(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    return toProtectedImpl(pageRef)->centerSelectionInVisibleArea();
}

void WKPageFindStringMatches(WKPageRef pageRef, WKStringRef string, WKFindOptions options, unsigned maxMatchCount)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->findStringMatches(toProtectedImpl(string)->string(), toFindOptions(options), maxMatchCount);
}

void WKPageGetImageForFindMatch(WKPageRef pageRef, int32_t matchIndex)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->getImageForFindMatch(matchIndex);
}

void WKPageSelectFindMatch(WKPageRef pageRef, int32_t matchIndex)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->selectFindMatch(matchIndex);
}

void WKPageIndicateFindMatch(WKPageRef pageRef, uint32_t matchIndex)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->indicateFindMatch(matchIndex);
}

void WKPageFindString(WKPageRef pageRef, WKStringRef string, WKFindOptions options, unsigned maxMatchCount)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->findString(toProtectedImpl(string)->string(), toFindOptions(options), maxMatchCount);
}

void WKPageHideFindUI(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->hideFindUI();
}

void WKPageCountStringMatches(WKPageRef pageRef, WKStringRef string, WKFindOptions options, unsigned maxMatchCount)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->countStringMatches(toProtectedImpl(string)->string(), toFindOptions(options), maxMatchCount);
}

void WKPageSetPageContextMenuClient(WKPageRef pageRef, const WKPageContextMenuClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(CONTEXT_MENUS)
    class ContextMenuClient final : public API::Client<WKPageContextMenuClientBase>, public API::ContextMenuClient {
        WTF_MAKE_TZONE_ALLOCATED_INLINE(ContextMenuClient);
    public:
        explicit ContextMenuClient(const WKPageContextMenuClientBase* client)
        {
            initialize(client);
        }

    private:
        void getContextMenuFromProposedMenu(WebPageProxy& page, Vector<Ref<WebKit::WebContextMenuItem>>&& proposedMenuVector, WebKit::WebContextMenuListenerProxy& contextMenuListener, const WebHitTestResultData& hitTestResultData, API::Object* userData) override
        {
            if (m_client.base.version >= 4 && m_client.getContextMenuFromProposedMenuAsync) {
                auto proposedMenuItems = toAPIObjectVector(proposedMenuVector);
                Ref webHitTestResult = API::HitTestResult::create(hitTestResultData, &page);
                m_client.getContextMenuFromProposedMenuAsync(toAPI(&page), toAPI(API::Array::create(WTFMove(proposedMenuItems)).ptr()), toAPI(&contextMenuListener), toAPI(webHitTestResult.ptr()), toAPI(userData), m_client.base.clientInfo);
                return;
            }
            
            if (!m_client.getContextMenuFromProposedMenu && !m_client.getContextMenuFromProposedMenu_deprecatedForUseWithV0) {
                contextMenuListener.useContextMenuItems(WTFMove(proposedMenuVector));
                return;
            }

            if (m_client.base.version >= 2 && !m_client.getContextMenuFromProposedMenu) {
                contextMenuListener.useContextMenuItems(WTFMove(proposedMenuVector));
                return;
            }

            auto proposedMenuItems = toAPIObjectVector(proposedMenuVector);

            WKArrayRef newMenu = nullptr;
            if (m_client.base.version >= 2) {
                Ref webHitTestResult = API::HitTestResult::create(hitTestResultData, &page);
                m_client.getContextMenuFromProposedMenu(toAPI(&page), toAPI(API::Array::create(WTFMove(proposedMenuItems)).ptr()), &newMenu, toAPI(webHitTestResult.ptr()), toAPI(userData), m_client.base.clientInfo);
            } else
                m_client.getContextMenuFromProposedMenu_deprecatedForUseWithV0(toAPI(&page), toAPI(API::Array::create(WTFMove(proposedMenuItems)).ptr()), &newMenu, toAPI(userData), m_client.base.clientInfo);

            RefPtr<API::Array> array = adoptRef(toImpl(newMenu));

            Vector<Ref<WebContextMenuItem>> customMenu;
            size_t newSize = array ? array->size() : 0;
            customMenu.reserveInitialCapacity(newSize);
            for (size_t i = 0; i < newSize; ++i) {
                RefPtr item = array->at<WebContextMenuItem>(i);
                if (!item) {
                    LOG(ContextMenu, "New menu entry at index %i is not a WebContextMenuItem", (int)i);
                    continue;
                }

                customMenu.append(item.releaseNonNull());
            }

            contextMenuListener.useContextMenuItems(WTFMove(customMenu));
        }

        void customContextMenuItemSelected(WebPageProxy& page, const WebContextMenuItemData& itemData) override
        {
            if (!m_client.customContextMenuItemSelected)
                return;

            m_client.customContextMenuItemSelected(toAPI(&page), toAPI(WebContextMenuItem::create(itemData).ptr()), m_client.base.clientInfo);
        }

        void showContextMenu(WebPageProxy& page, const WebCore::IntPoint& menuLocation, const Vector<Ref<WebContextMenuItem>>& menuItemsVector) override
        {
            if (!canShowContextMenu())
                return;

            auto menuItems = toAPIObjectVector(menuItemsVector);
            m_client.showContextMenu(toAPI(&page), toAPI(menuLocation), toAPI(API::Array::create(WTFMove(menuItems)).ptr()), m_client.base.clientInfo);
        }

        bool canShowContextMenu() const override
        {
            return m_client.showContextMenu;
        }

        bool hideContextMenu(WebPageProxy& page) override
        {
            if (!m_client.hideContextMenu)
                return false;

            m_client.hideContextMenu(toAPI(&page), m_client.base.clientInfo);

            return true;
        }
    };

    toProtectedImpl(pageRef)->setContextMenuClient(makeUnique<ContextMenuClient>(wkClient));
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(wkClient);
#endif
}

void WKPageSetPageDiagnosticLoggingClient(WKPageRef pageRef, const WKPageDiagnosticLoggingClientBase* wkClient)
{
    toProtectedImpl(pageRef)->setDiagnosticLoggingClient(makeUnique<WebPageDiagnosticLoggingClient>(wkClient));
}

void WKPageSetPageFindClient(WKPageRef pageRef, const WKPageFindClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
    class FindClient : public API::Client<WKPageFindClientBase>, public API::FindClient {
    public:
        explicit FindClient(const WKPageFindClientBase* client)
        {
            initialize(client);
        }

    private:
        void didFindString(WebPageProxy* page, const String& string, const Vector<WebCore::IntRect>&, uint32_t matchCount, int32_t, bool didWrapAround) override
        {
            if (!m_client.didFindString)
                return;
            
            m_client.didFindString(toAPI(page), toAPI(string.impl()), matchCount, m_client.base.clientInfo);
        }

        void didFailToFindString(WebPageProxy* page, const String& string) override
        {
            if (!m_client.didFailToFindString)
                return;
            
            m_client.didFailToFindString(toAPI(page), toAPI(string.impl()), m_client.base.clientInfo);
        }

        void didCountStringMatches(WebPageProxy* page, const String& string, uint32_t matchCount) override
        {
            if (!m_client.didCountStringMatches)
                return;

            m_client.didCountStringMatches(toAPI(page), toAPI(string.impl()), matchCount, m_client.base.clientInfo);
        }
    };

    toProtectedImpl(pageRef)->setFindClient(makeUnique<FindClient>(wkClient));
}

void WKPageSetPageFindMatchesClient(WKPageRef pageRef, const WKPageFindMatchesClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
    class FindMatchesClient : public API::Client<WKPageFindMatchesClientBase>, public API::FindMatchesClient {
    public:
        explicit FindMatchesClient(const WKPageFindMatchesClientBase* client)
        {
            initialize(client);
        }

    private:
        void didFindStringMatches(WebPageProxy* page, const String& string, const Vector<Vector<WebCore::IntRect>>& matchRects, int32_t index) override
        {
            if (!m_client.didFindStringMatches)
                return;

            auto matches = matchRects.map([](auto& rects) -> RefPtr<API::Object> {
                auto apiRects = rects.map([](auto& rect) -> RefPtr<API::Object> {
                    return API::Rect::create(toAPI(rect));
                });
                return API::Array::create(WTFMove(apiRects));
            });
            m_client.didFindStringMatches(toAPI(page), toAPI(string.impl()), toAPI(API::Array::create(WTFMove(matches)).ptr()), index, m_client.base.clientInfo);
        }

        void didGetImageForMatchResult(WebPageProxy* page, WebImage* image, int32_t index) override
        {
            if (!m_client.didGetImageForMatchResult)
                return;

            m_client.didGetImageForMatchResult(toAPI(page), toAPI(image), index, m_client.base.clientInfo);
        }
    };

    toProtectedImpl(pageRef)->setFindMatchesClient(makeUnique<FindMatchesClient>(wkClient));
}

void WKPageSetPageInjectedBundleClient(WKPageRef pageRef, const WKPageInjectedBundleClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setInjectedBundleClient(wkClient);
}

class CompletionListener : public API::ObjectImpl<API::Object::Type::CompletionListener> {
public:
    static Ref<CompletionListener> create(CompletionHandler<void()>&& completionHandler) { return adoptRef(*new CompletionListener(WTFMove(completionHandler))); }
    void complete() { m_completionHandler(); }

private:
    explicit CompletionListener(CompletionHandler<void()>&& completionHandler)
        : m_completionHandler(WTFMove(completionHandler)) { }

    CompletionHandler<void()> m_completionHandler;
};

SPECIALIZE_TYPE_TRAITS_BEGIN(CompletionListener)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::CompletionListener; }
SPECIALIZE_TYPE_TRAITS_END()

namespace WebKit {
WK_ADD_API_MAPPING(WKCompletionListenerRef, CompletionListener);
}

void WKCompletionListenerComplete(WKCompletionListenerRef listener)
{
    toProtectedImpl(listener)->complete();
}

void WKPageSetFullScreenClientForTesting(WKPageRef pageRef, const WKPageFullScreenClientBase* client)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(FULLSCREEN_API)
    class FullScreenClientForTesting : public API::Client<WKPageFullScreenClientBase>, public WebKit::WebFullScreenManagerProxyClient {
        WTF_DEPRECATED_MAKE_FAST_ALLOCATED(FullScreenClientForTesting);
        WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(FullScreenClientForTesting);
    public:
        FullScreenClientForTesting(const WKPageFullScreenClientBase* client, WebPageProxy* page)
            : m_page(page)
        {
            initialize(client);
        }

    private:
        void closeFullScreenManager() override { }
        bool isFullScreen() override { return false; }

        void enterFullScreen(WebCore::FloatSize, CompletionHandler<void(bool)>&& completionHandler) override
        {
            if (!m_client.willEnterFullScreen)
                return completionHandler(false);
            m_client.willEnterFullScreen(toAPI(protectedPage().get()), toAPI(CompletionListener::create([completionHandler = WTFMove(completionHandler)] mutable {
                completionHandler(true);
            }).ptr()), m_client.base.clientInfo);
        }

        void beganEnterFullScreen(const WebCore::IntRect& initialFrame, const WebCore::IntRect& finalFrame, CompletionHandler<void(bool)>&& completionHandler) override
        {
            if (!m_client.beganEnterFullScreen)
                return completionHandler(false);
            m_client.beganEnterFullScreen(toAPI(protectedPage().get()), toAPI(initialFrame), toAPI(finalFrame), m_client.base.clientInfo);
            completionHandler(true);
        }

        void exitFullScreen(CompletionHandler<void()>&& completionHandler) override
        {
            if (!m_client.exitFullScreen)
                return completionHandler();
            m_client.exitFullScreen(toAPI(protectedPage().get()), m_client.base.clientInfo);
            completionHandler();
        }

        void beganExitFullScreen(const WebCore::IntRect& initialFrame, const WebCore::IntRect& finalFrame, CompletionHandler<void()>&& completionHandler) override
        {
            if (!m_client.beganExitFullScreen)
                return completionHandler();
            m_client.beganExitFullScreen(toAPI(protectedPage().get()), toAPI(initialFrame), toAPI(finalFrame), toAPI(CompletionListener::create(WTFMove(completionHandler)).ptr()), m_client.base.clientInfo);
        }

#if ENABLE(QUICKLOOK_FULLSCREEN)
        void updateImageSource() override { }
#endif

        RefPtr<WebPageProxy> protectedPage() const { return m_page.get(); }

        WeakPtr<WebPageProxy> m_page;
    };
    [[maybe_unused]] FullScreenClientForTesting::WTFDidOverrideDeleteForCheckedPtr makeGCCHappy { 0 };

    auto fullscreenClient = client ? makeUnique<FullScreenClientForTesting>(client, toImpl(pageRef)) : nullptr;
    toProtectedImpl(pageRef)->setFullScreenClientForTesting(WTFMove(fullscreenClient));
#else
    UNUSED_PARAM(client);
#endif
}

void WKPageRequestExitFullScreen(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(FULLSCREEN_API)
    if (RefPtr manager = toProtectedImpl(pageRef)->fullScreenManager())
        manager->requestExitFullScreen();
#endif
}

void WKPageSetPageFormClient(WKPageRef pageRef, const WKPageFormClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setFormClient(makeUnique<WebFormClient>(wkClient));
}

void WKPageSetPageLoaderClient(WKPageRef pageRef, const WKPageLoaderClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
    class LoaderClient : public API::Client<WKPageLoaderClientBase>, public API::LoaderClient {
    public:
        explicit LoaderClient(const WKPageLoaderClientBase* client)
        {
            initialize(client);
            
            // WKPageSetPageLoaderClient is deprecated. Use WKPageSetPageNavigationClient instead.
            RELEASE_ASSERT(!m_client.didFinishDocumentLoadForFrame);
            RELEASE_ASSERT(!m_client.didSameDocumentNavigationForFrame);
            RELEASE_ASSERT(!m_client.didReceiveTitleForFrame);
            RELEASE_ASSERT(!m_client.didFirstLayoutForFrame);
            RELEASE_ASSERT(!m_client.didRemoveFrameFromHierarchy);
            RELEASE_ASSERT(!m_client.didDisplayInsecureContentForFrame);
            RELEASE_ASSERT(!m_client.didRunInsecureContentForFrame);
            RELEASE_ASSERT(!m_client.canAuthenticateAgainstProtectionSpaceInFrame);
            RELEASE_ASSERT(!m_client.didReceiveAuthenticationChallengeInFrame);
            RELEASE_ASSERT(!m_client.didStartProgress);
            RELEASE_ASSERT(!m_client.didChangeProgress);
            RELEASE_ASSERT(!m_client.didFinishProgress);
            RELEASE_ASSERT(!m_client.processDidBecomeUnresponsive);
            RELEASE_ASSERT(!m_client.processDidBecomeResponsive);
            RELEASE_ASSERT(!m_client.shouldGoToBackForwardListItem);
            RELEASE_ASSERT(!m_client.didFailToInitializePlugin_deprecatedForUseWithV0);
            RELEASE_ASSERT(!m_client.didDetectXSSForFrame);
            RELEASE_ASSERT(!m_client.didNewFirstVisuallyNonEmptyLayout_unavailable);
            RELEASE_ASSERT(!m_client.willGoToBackForwardListItem);
            RELEASE_ASSERT(!m_client.interactionOccurredWhileProcessUnresponsive);
            RELEASE_ASSERT(!m_client.pluginDidFail_deprecatedForUseWithV1);
            RELEASE_ASSERT(!m_client.didReceiveIntentForFrame_unavailable);
            RELEASE_ASSERT(!m_client.registerIntentServiceForFrame_unavailable);
            RELEASE_ASSERT(!m_client.pluginLoadPolicy_deprecatedForUseWithV2);
            RELEASE_ASSERT(!m_client.pluginDidFail);
            RELEASE_ASSERT(!m_client.pluginLoadPolicy);
            RELEASE_ASSERT(!m_client.navigationGestureDidBegin);
            RELEASE_ASSERT(!m_client.navigationGestureWillEnd);
            RELEASE_ASSERT(!m_client.navigationGestureDidEnd);
        }

    private:
        
        void didCommitLoadForFrame(WebPageProxy& page, WebFrameProxy& frame, API::Navigation*, API::Object* userData) override
        {
            if (!m_client.didCommitLoadForFrame)
                return;

            m_client.didCommitLoadForFrame(toAPI(&page), toAPI(&frame), toAPI(userData), m_client.base.clientInfo);
        }
        
        void didStartProvisionalLoadForFrame(WebPageProxy& page, WebFrameProxy& frame, API::Navigation*, API::Object* userData) override
        {
            if (!m_client.didStartProvisionalLoadForFrame)
                return;

            m_client.didStartProvisionalLoadForFrame(toAPI(&page), toAPI(&frame), toAPI(userData), m_client.base.clientInfo);
        }

        void didReceiveServerRedirectForProvisionalLoadForFrame(WebPageProxy& page, WebFrameProxy& frame, API::Navigation*, API::Object* userData) override
        {
            if (!m_client.didReceiveServerRedirectForProvisionalLoadForFrame)
                return;

            m_client.didReceiveServerRedirectForProvisionalLoadForFrame(toAPI(&page), toAPI(&frame), toAPI(userData), m_client.base.clientInfo);
        }

        void didFailProvisionalLoadWithErrorForFrame(WebPageProxy& page, WebFrameProxy& frame, API::Navigation*, const WebCore::ResourceError& error, API::Object* userData) override
        {
            if (!m_client.didFailProvisionalLoadWithErrorForFrame)
                return;

            m_client.didFailProvisionalLoadWithErrorForFrame(toAPI(&page), toAPI(&frame), toAPI(error), toAPI(userData), m_client.base.clientInfo);
        }

        void didFinishLoadForFrame(WebPageProxy& page, WebFrameProxy& frame, API::Navigation*, API::Object* userData) override
        {
            if (!m_client.didFinishLoadForFrame)
                return;

            m_client.didFinishLoadForFrame(toAPI(&page), toAPI(&frame), toAPI(userData), m_client.base.clientInfo);
        }

        void didFailLoadWithErrorForFrame(WebPageProxy& page, WebFrameProxy& frame, API::Navigation*, const WebCore::ResourceError& error, API::Object* userData) override
        {
            if (!m_client.didFailLoadWithErrorForFrame)
                return;

            m_client.didFailLoadWithErrorForFrame(toAPI(&page), toAPI(&frame), toAPI(error), toAPI(userData), m_client.base.clientInfo);
        }

        void didFirstVisuallyNonEmptyLayoutForFrame(WebPageProxy& page, WebFrameProxy& frame, API::Object* userData) override
        {
            if (!m_client.didFirstVisuallyNonEmptyLayoutForFrame)
                return;

            m_client.didFirstVisuallyNonEmptyLayoutForFrame(toAPI(&page), toAPI(&frame), toAPI(userData), m_client.base.clientInfo);
        }

        void didReachLayoutMilestone(WebPageProxy& page, OptionSet<WebCore::LayoutMilestone> milestones) override
        {
            if (!m_client.didLayout)
                return;

            m_client.didLayout(toAPI(&page), toWKLayoutMilestones(milestones), nullptr, m_client.base.clientInfo);
        }

        bool processDidCrash(WebPageProxy& page) override
        {
            if (!m_client.processDidCrash)
                return false;

            m_client.processDidCrash(toAPI(&page), m_client.base.clientInfo);
            return true;
        }

        void didChangeBackForwardList(WebPageProxy& page, WebBackForwardListItem* addedItem, Vector<Ref<WebBackForwardListItem>>&& removedItems) override
        {
            if (!m_client.didChangeBackForwardList)
                return;

            RefPtr<API::Array> removedItemsArray;
            if (!removedItems.isEmpty()) {
                auto removedItemsVector = WTF::map(WTFMove(removedItems), [](Ref<WebBackForwardListItem>&& removedItem) -> RefPtr<API::Object> {
                    return WTFMove(removedItem);
                });
                removedItemsArray = API::Array::create(WTFMove(removedItemsVector));
            }

            m_client.didChangeBackForwardList(toAPI(&page), toAPI(addedItem), toAPI(removedItemsArray.get()), m_client.base.clientInfo);
        }
        
        bool shouldKeepCurrentBackForwardListItemInList(WebKit::WebPageProxy& page, WebKit::WebBackForwardListItem& item) override
        {
            if (!m_client.shouldKeepCurrentBackForwardListItemInList)
                return true;

            return m_client.shouldKeepCurrentBackForwardListItemInList(toAPI(&page), toAPI(&item), m_client.base.clientInfo);
        }
    };

    RefPtr webPageProxy = toProtectedImpl(pageRef);

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    // GCC 10 gets confused here and warns that WKPageSetPagePolicyClient is deprecated when we call
    // makeUnique<LoaderClient>(). This seems to be a GCC bug. It's not really appropriate to use
    // ALLOW_DEPRECATED_DECLARATIONS_BEGIN/END here because we are not using a deprecated symbol.
    auto loaderClient = makeUnique<LoaderClient>(wkClient);
ALLOW_DEPRECATED_DECLARATIONS_END

    // It would be nice to get rid of this code and transition all clients to using didLayout instead of
    // didFirstLayoutInFrame and didFirstVisuallyNonEmptyLayoutInFrame. In the meantime, this is required
    // for backwards compatibility.
    OptionSet<WebCore::LayoutMilestone> milestones;
    if (loaderClient->client().didFirstLayoutForFrame)
        milestones.add(WebCore::LayoutMilestone::DidFirstLayout);
    if (loaderClient->client().didFirstVisuallyNonEmptyLayoutForFrame)
        milestones.add(WebCore::LayoutMilestone::DidFirstVisuallyNonEmptyLayout);

    if (milestones)
        webPageProxy->protectedLegacyMainFrameProcess()->send(Messages::WebPage::ListenForLayoutMilestones(milestones), webPageProxy->webPageIDInMainFrameProcess());

    webPageProxy->setLoaderClient(WTFMove(loaderClient));
}

void WKPageSetPagePolicyClient(WKPageRef pageRef, const WKPagePolicyClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
    class PolicyClient : public API::Client<WKPagePolicyClientBase>, public API::PolicyClient {
    public:
        explicit PolicyClient(const WKPagePolicyClientBase* client)
        {
            initialize(client);

            // This callback is unused and deprecated.
            RELEASE_ASSERT(!m_client.unableToImplementPolicy);
        }

    private:
        void decidePolicyForNavigationAction(WebPageProxy& page, WebFrameProxy* frame, Ref<API::NavigationAction>&& navigationAction, WebFrameProxy* originatingFrame, const WebCore::ResourceRequest& originalResourceRequest, const WebCore::ResourceRequest& resourceRequest, Ref<WebFramePolicyListenerProxy>&& listener) override
        {
            if (!m_client.decidePolicyForNavigationAction_deprecatedForUseWithV0 && !m_client.decidePolicyForNavigationAction_deprecatedForUseWithV1 && !m_client.decidePolicyForNavigationAction) {
                listener->use();
                return;
            }

            Ref<API::URLRequest> originalRequest = API::URLRequest::create(originalResourceRequest);
            Ref<API::URLRequest> request = API::URLRequest::create(resourceRequest);

            if (m_client.decidePolicyForNavigationAction_deprecatedForUseWithV0)
                m_client.decidePolicyForNavigationAction_deprecatedForUseWithV0(toAPI(&page), toAPI(frame), toAPI(navigationAction->data().navigationType), toAPI(navigationAction->data().modifiers), toAPI(navigationAction->data().mouseButton), toAPI(request.ptr()), toAPI(listener.ptr()), nullptr, m_client.base.clientInfo);
            else if (m_client.decidePolicyForNavigationAction_deprecatedForUseWithV1)
                m_client.decidePolicyForNavigationAction_deprecatedForUseWithV1(toAPI(&page), toAPI(frame), toAPI(navigationAction->data().navigationType), toAPI(navigationAction->data().modifiers), toAPI(navigationAction->data().mouseButton), toAPI(originatingFrame), toAPI(request.ptr()), toAPI(listener.ptr()), nullptr, m_client.base.clientInfo);
            else
                m_client.decidePolicyForNavigationAction(toAPI(&page), toAPI(frame), toAPI(navigationAction->data().navigationType), toAPI(navigationAction->data().modifiers), toAPI(navigationAction->data().mouseButton), toAPI(originatingFrame), toAPI(originalRequest.ptr()), toAPI(request.ptr()), toAPI(listener.ptr()), nullptr, m_client.base.clientInfo);
        }

        void decidePolicyForNewWindowAction(WebPageProxy& page, WebFrameProxy& frame, Ref<API::NavigationAction>&& navigationAction, const WebCore::ResourceRequest& resourceRequest, const String& frameName, Ref<WebFramePolicyListenerProxy>&& listener) override
        {
            if (!m_client.decidePolicyForNewWindowAction) {
                listener->use();
                return;
            }

            Ref<API::URLRequest> request = API::URLRequest::create(resourceRequest);

            m_client.decidePolicyForNewWindowAction(toAPI(&page), toAPI(&frame), toAPI(navigationAction->data().navigationType), toAPI(navigationAction->data().modifiers), toAPI(navigationAction->data().mouseButton), toAPI(request.ptr()), toAPI(frameName.impl()), toAPI(listener.ptr()), nullptr, m_client.base.clientInfo);
        }

        void decidePolicyForResponse(WebPageProxy& page, WebFrameProxy& frame, const WebCore::ResourceResponse& resourceResponse, const WebCore::ResourceRequest& resourceRequest, bool canShowMIMEType, Ref<WebFramePolicyListenerProxy>&& listener) override
        {
            if (!m_client.decidePolicyForResponse_deprecatedForUseWithV0 && !m_client.decidePolicyForResponse) {
                listener->use();
                return;
            }

            Ref<API::URLResponse> response = API::URLResponse::create(resourceResponse);
            Ref<API::URLRequest> request = API::URLRequest::create(resourceRequest);

            if (m_client.decidePolicyForResponse_deprecatedForUseWithV0)
                m_client.decidePolicyForResponse_deprecatedForUseWithV0(toAPI(&page), toAPI(&frame), toAPI(response.ptr()), toAPI(request.ptr()), toAPI(listener.ptr()), nullptr, m_client.base.clientInfo);
            else
                m_client.decidePolicyForResponse(toAPI(&page), toAPI(&frame), toAPI(response.ptr()), toAPI(request.ptr()), canShowMIMEType, toAPI(listener.ptr()), nullptr, m_client.base.clientInfo);
        }
    };

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    // GCC 10 gets confused here and warns that WKPageSetPagePolicyClient is deprecated when we call
    // makeUnique<PolicyClient>(). This seems to be a GCC bug. It's not really appropriate to use
    // ALLOW_DEPRECATED_DECLARATIONS_BEGIN/END here because we are not using a deprecated symbol.
    toProtectedImpl(pageRef)->setPolicyClient(makeUnique<PolicyClient>(wkClient));
ALLOW_DEPRECATED_DECLARATIONS_END
}

namespace WebKit {
using namespace WebCore;
    
class RunBeforeUnloadConfirmPanelResultListener : public API::ObjectImpl<API::Object::Type::RunBeforeUnloadConfirmPanelResultListener> {
public:
    static Ref<RunBeforeUnloadConfirmPanelResultListener> create(Function<void(bool)>&& completionHandler)
    {
        return adoptRef(*new RunBeforeUnloadConfirmPanelResultListener(WTFMove(completionHandler)));
    }

    virtual ~RunBeforeUnloadConfirmPanelResultListener()
    {
    }

    void call(bool result)
    {
        m_completionHandler(result);
    }

private:
    explicit RunBeforeUnloadConfirmPanelResultListener(Function<void (bool)>&& completionHandler)
        : m_completionHandler(WTFMove(completionHandler))
    {
    }

    Function<void (bool)> m_completionHandler;
};

class RunJavaScriptAlertResultListener : public API::ObjectImpl<API::Object::Type::RunJavaScriptAlertResultListener> {
public:
    static Ref<RunJavaScriptAlertResultListener> create(Function<void()>&& completionHandler)
    {
        return adoptRef(*new RunJavaScriptAlertResultListener(WTFMove(completionHandler)));
    }

    virtual ~RunJavaScriptAlertResultListener()
    {
    }

    void call()
    {
        m_completionHandler();
    }

private:
    explicit RunJavaScriptAlertResultListener(Function<void ()>&& completionHandler)
        : m_completionHandler(WTFMove(completionHandler))
    {
    }
    
    Function<void ()> m_completionHandler;
};

class RunJavaScriptConfirmResultListener : public API::ObjectImpl<API::Object::Type::RunJavaScriptConfirmResultListener> {
public:
    static Ref<RunJavaScriptConfirmResultListener> create(Function<void(bool)>&& completionHandler)
    {
        return adoptRef(*new RunJavaScriptConfirmResultListener(WTFMove(completionHandler)));
    }

    virtual ~RunJavaScriptConfirmResultListener()
    {
    }

    void call(bool result)
    {
        m_completionHandler(result);
    }

private:
    explicit RunJavaScriptConfirmResultListener(Function<void(bool)>&& completionHandler)
        : m_completionHandler(WTFMove(completionHandler))
    {
    }

    Function<void(bool)> m_completionHandler;
};

class RunJavaScriptPromptResultListener : public API::ObjectImpl<API::Object::Type::RunJavaScriptPromptResultListener> {
public:
    static Ref<RunJavaScriptPromptResultListener> create(Function<void(const String&)>&& completionHandler)
    {
        return adoptRef(*new RunJavaScriptPromptResultListener(WTFMove(completionHandler)));
    }

    virtual ~RunJavaScriptPromptResultListener()
    {
    }

    void call(const String& result)
    {
        m_completionHandler(result);
    }

private:
    explicit RunJavaScriptPromptResultListener(Function<void(const String&)>&& completionHandler)
        : m_completionHandler(WTFMove(completionHandler))
    {
    }

    Function<void (const String&)> m_completionHandler;
};

class RequestStorageAccessConfirmResultListener : public API::ObjectImpl<API::Object::Type::RequestStorageAccessConfirmResultListener> {
public:
    static Ref<RequestStorageAccessConfirmResultListener> create(CompletionHandler<void(bool)>&& completionHandler)
    {
        return adoptRef(*new RequestStorageAccessConfirmResultListener(WTFMove(completionHandler)));
    }
    
    virtual ~RequestStorageAccessConfirmResultListener()
    {
    }
    
    void call(bool result)
    {
        m_completionHandler(result);
    }
    
private:
    explicit RequestStorageAccessConfirmResultListener(CompletionHandler<void(bool)>&& completionHandler)
        : m_completionHandler(WTFMove(completionHandler))
    {
    }
    
    CompletionHandler<void(bool)> m_completionHandler;
};

WK_ADD_API_MAPPING(WKPageRunBeforeUnloadConfirmPanelResultListenerRef, RunBeforeUnloadConfirmPanelResultListener)
WK_ADD_API_MAPPING(WKPageRunJavaScriptAlertResultListenerRef, RunJavaScriptAlertResultListener)
WK_ADD_API_MAPPING(WKPageRunJavaScriptConfirmResultListenerRef, RunJavaScriptConfirmResultListener)
WK_ADD_API_MAPPING(WKPageRunJavaScriptPromptResultListenerRef, RunJavaScriptPromptResultListener)
WK_ADD_API_MAPPING(WKPageRequestStorageAccessConfirmResultListenerRef, RequestStorageAccessConfirmResultListener)

}

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::RunBeforeUnloadConfirmPanelResultListener)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::RunBeforeUnloadConfirmPanelResultListener; }
SPECIALIZE_TYPE_TRAITS_END()

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::RunJavaScriptAlertResultListener)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::RunJavaScriptAlertResultListener; }
SPECIALIZE_TYPE_TRAITS_END()

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::RunJavaScriptConfirmResultListener)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::RunJavaScriptConfirmResultListener; }
SPECIALIZE_TYPE_TRAITS_END()

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::RunJavaScriptPromptResultListener)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::RunJavaScriptPromptResultListener; }
SPECIALIZE_TYPE_TRAITS_END()

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::RequestStorageAccessConfirmResultListener)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::RequestStorageAccessConfirmResultListener; }
SPECIALIZE_TYPE_TRAITS_END()

WKTypeID WKPageRunBeforeUnloadConfirmPanelResultListenerGetTypeID()
{
    return toAPI(RunBeforeUnloadConfirmPanelResultListener::APIType);
}

void WKPageRunBeforeUnloadConfirmPanelResultListenerCall(WKPageRunBeforeUnloadConfirmPanelResultListenerRef listener, bool result)
{
    toProtectedImpl(listener)->call(result);
}

WKTypeID WKPageRunJavaScriptAlertResultListenerGetTypeID()
{
    return toAPI(RunJavaScriptAlertResultListener::APIType);
}

void WKPageRunJavaScriptAlertResultListenerCall(WKPageRunJavaScriptAlertResultListenerRef listener)
{
    toProtectedImpl(listener)->call();
}

WKTypeID WKPageRunJavaScriptConfirmResultListenerGetTypeID()
{
    return toAPI(RunJavaScriptConfirmResultListener::APIType);
}

void WKPageRunJavaScriptConfirmResultListenerCall(WKPageRunJavaScriptConfirmResultListenerRef listener, bool result)
{
    toProtectedImpl(listener)->call(result);
}

WKTypeID WKPageRunJavaScriptPromptResultListenerGetTypeID()
{
    return toAPI(RunJavaScriptPromptResultListener::APIType);
}

void WKPageRunJavaScriptPromptResultListenerCall(WKPageRunJavaScriptPromptResultListenerRef listener, WKStringRef result)
{
    toProtectedImpl(listener)->call(toWTFString(result));
}

WKTypeID WKPageRequestStorageAccessConfirmResultListenerGetTypeID()
{
    return toAPI(RequestStorageAccessConfirmResultListener::APIType);
}

void WKPageRequestStorageAccessConfirmResultListenerCall(WKPageRequestStorageAccessConfirmResultListenerRef listener, bool result)
{
    toProtectedImpl(listener)->call(result);
}

void WKPageSetPageUIClient(WKPageRef pageRef, const WKPageUIClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
    class UIClient : public API::Client<WKPageUIClientBase>, public API::UIClient {
    public:
        explicit UIClient(const WKPageUIClientBase* client)
        {
            initialize(client);
        }

    private:
        void createNewPage(WebPageProxy& page, Ref<API::PageConfiguration>&& configuration, Ref<API::NavigationAction>&& navigationAction, CompletionHandler<void(RefPtr<WebPageProxy>&&)>&& completionHandler) final
        {
            ASSERT(configuration->windowFeatures());
            auto& windowFeatures = *configuration->windowFeatures();
            if (m_client.createNewPage) {
                auto apiWindowFeatures = API::WindowFeatures::create(windowFeatures);
                return completionHandler(adoptRef(toImpl(m_client.createNewPage(toAPI(&page), toAPI(configuration.ptr()), toAPI(navigationAction.ptr()), toAPI(apiWindowFeatures.ptr()), m_client.base.clientInfo))));
            }
        
            if (m_client.createNewPage_deprecatedForUseWithV1 || m_client.createNewPage_deprecatedForUseWithV0) {
                API::Dictionary::MapType map;
                map.set("wantsPopup"_s, API::Boolean::create(windowFeatures.wantsPopup()));
                map.set("hasAdditionalFeatures"_s, API::Boolean::create(windowFeatures.hasAdditionalFeatures));
                if (windowFeatures.x)
                    map.set("x"_s, API::Double::create(*windowFeatures.x));
                if (windowFeatures.y)
                    map.set("y"_s, API::Double::create(*windowFeatures.y));
                if (windowFeatures.width)
                    map.set("width"_s, API::Double::create(*windowFeatures.width));
                if (windowFeatures.height)
                    map.set("height"_s, API::Double::create(*windowFeatures.height));
                if (windowFeatures.popup)
                    map.set("popup"_s, API::Boolean::create(*windowFeatures.popup));
                if (windowFeatures.menuBarVisible)
                    map.set("menuBarVisible"_s, API::Boolean::create(*windowFeatures.menuBarVisible));
                if (windowFeatures.statusBarVisible)
                    map.set("statusBarVisible"_s, API::Boolean::create(*windowFeatures.statusBarVisible));
                if (windowFeatures.toolBarVisible)
                    map.set("toolBarVisible"_s, API::Boolean::create(*windowFeatures.toolBarVisible));
                if (windowFeatures.locationBarVisible)
                    map.set("locationBarVisible"_s, API::Boolean::create(*windowFeatures.locationBarVisible));
                if (windowFeatures.scrollbarsVisible)
                    map.set("scrollbarsVisible"_s, API::Boolean::create(*windowFeatures.scrollbarsVisible));
                if (windowFeatures.resizable)
                    map.set("resizable"_s, API::Boolean::create(*windowFeatures.resizable));
                if (windowFeatures.fullscreen)
                    map.set("fullscreen"_s, API::Boolean::create(*windowFeatures.fullscreen));
                if (windowFeatures.dialog)
                    map.set("dialog"_s, API::Boolean::create(*windowFeatures.dialog));
                Ref<API::Dictionary> featuresMap = API::Dictionary::create(WTFMove(map));

                if (m_client.createNewPage_deprecatedForUseWithV1) {
                    Ref<API::URLRequest> request = API::URLRequest::create(navigationAction->request());
                    return completionHandler(adoptRef(toImpl(m_client.createNewPage_deprecatedForUseWithV1(toAPI(&page), toAPI(request.ptr()), toAPI(featuresMap.ptr()), toAPI(navigationAction->modifiers()), toAPI(navigationAction->mouseButton()), m_client.base.clientInfo))));
                }
    
                ASSERT(m_client.createNewPage_deprecatedForUseWithV0);
                return completionHandler(adoptRef(toImpl(m_client.createNewPage_deprecatedForUseWithV0(toAPI(&page), toAPI(featuresMap.ptr()), toAPI(navigationAction->modifiers()), toAPI(navigationAction->mouseButton()), m_client.base.clientInfo))));
            }

            completionHandler(nullptr);
        }

        void showPage(WebPageProxy* page) final
        {
            if (!m_client.showPage)
                return;

            m_client.showPage(toAPI(page), m_client.base.clientInfo);
        }

        void fullscreenMayReturnToInline(WebPageProxy* page) final
        {
            if (!m_client.fullscreenMayReturnToInline)
                return;

            m_client.fullscreenMayReturnToInline(toAPI(page), m_client.base.clientInfo);
        }
        
        void hasVideoInPictureInPictureDidChange(WebPageProxy* page, bool hasVideoInPictureInPicture) final
        {
            if (!m_client.hasVideoInPictureInPictureDidChange)
                return;
            
            m_client.hasVideoInPictureInPictureDidChange(toAPI(page), hasVideoInPictureInPicture, m_client.base.clientInfo);
        }

        void close(WebPageProxy* page) final
        {
            if (!m_client.close)
                return;

            m_client.close(toAPI(page), m_client.base.clientInfo);
        }

        bool takeFocus(WebPageProxy* page, WKFocusDirection direction) final
        {
            if (!m_client.takeFocus)
                return false;

            m_client.takeFocus(toAPI(page), direction, m_client.base.clientInfo);
            return true;
        }

        void focus(WebPageProxy* page) final
        {
            if (!m_client.focus)
                return;

            m_client.focus(toAPI(page), m_client.base.clientInfo);
        }

        void unfocus(WebPageProxy* page) final
        {
            if (!m_client.unfocus)
                return;

            m_client.unfocus(toAPI(page), m_client.base.clientInfo);
        }

        void runJavaScriptAlert(WebPageProxy& page, const String& message, WebFrameProxy* frame, FrameInfoData&& frameInfo, Function<void()>&& completionHandler) final
        {
            if (m_client.runJavaScriptAlert) {
                RefPtr<RunJavaScriptAlertResultListener> listener = RunJavaScriptAlertResultListener::create(WTFMove(completionHandler));
                RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::create(frameInfo.securityOrigin);
                m_client.runJavaScriptAlert(toAPI(&page), toAPI(message.impl()), toAPI(frame), toAPI(securityOrigin.get()), toAPI(listener.get()), m_client.base.clientInfo);
                return;
            }

            if (m_client.runJavaScriptAlert_deprecatedForUseWithV5) {
                RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::create(frameInfo.securityOrigin);
                m_client.runJavaScriptAlert_deprecatedForUseWithV5(toAPI(&page), toAPI(message.impl()), toAPI(frame), toAPI(securityOrigin.get()), m_client.base.clientInfo);
                completionHandler();
                return;
            }
            
            if (m_client.runJavaScriptAlert_deprecatedForUseWithV0) {
                m_client.runJavaScriptAlert_deprecatedForUseWithV0(toAPI(&page), toAPI(message.impl()), toAPI(frame), m_client.base.clientInfo);
                completionHandler();
                return;
            }


            completionHandler();
        }

        void runJavaScriptConfirm(WebPageProxy& page, const String& message, WebFrameProxy* frame, FrameInfoData&& frameInfo, Function<void(bool)>&& completionHandler) final
        {
            if (m_client.runJavaScriptConfirm) {
                RefPtr<RunJavaScriptConfirmResultListener> listener = RunJavaScriptConfirmResultListener::create(WTFMove(completionHandler));
                RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::create(frameInfo.securityOrigin);
                m_client.runJavaScriptConfirm(toAPI(&page), toAPI(message.impl()), toAPI(frame), toAPI(securityOrigin.get()), toAPI(listener.get()), m_client.base.clientInfo);
                return;
            }

            if (m_client.runJavaScriptConfirm_deprecatedForUseWithV5) {
                RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::create(frameInfo.securityOrigin);
                bool result = m_client.runJavaScriptConfirm_deprecatedForUseWithV5(toAPI(&page), toAPI(message.impl()), toAPI(frame), toAPI(securityOrigin.get()), m_client.base.clientInfo);
                
                completionHandler(result);
                return;
            }
            
            if (m_client.runJavaScriptConfirm_deprecatedForUseWithV0) {
                bool result = m_client.runJavaScriptConfirm_deprecatedForUseWithV0(toAPI(&page), toAPI(message.impl()), toAPI(frame), m_client.base.clientInfo);

                completionHandler(result);
                return;
            }
            
            completionHandler(false);
        }

        void runJavaScriptPrompt(WebPageProxy& page, const String& message, const String& defaultValue, WebFrameProxy* frame, FrameInfoData&& frameInfo, Function<void(const String&)>&& completionHandler) final
        {
            if (m_client.runJavaScriptPrompt) {
                RefPtr<RunJavaScriptPromptResultListener> listener = RunJavaScriptPromptResultListener::create(WTFMove(completionHandler));
                RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::create(frameInfo.securityOrigin);
                m_client.runJavaScriptPrompt(toAPI(&page), toAPI(message.impl()), toAPI(defaultValue.impl()), toAPI(frame), toAPI(securityOrigin.get()), toAPI(listener.get()), m_client.base.clientInfo);
                return;
            }

            if (m_client.runJavaScriptPrompt_deprecatedForUseWithV5) {
                RefPtr<API::SecurityOrigin> securityOrigin = API::SecurityOrigin::create(frameInfo.securityOrigin);
                RefPtr<API::String> string = adoptRef(toImpl(m_client.runJavaScriptPrompt_deprecatedForUseWithV5(toAPI(&page), toAPI(message.impl()), toAPI(defaultValue.impl()), toAPI(frame), toAPI(securityOrigin.get()), m_client.base.clientInfo)));
                
                if (string)
                    completionHandler(string->string());
                else
                    completionHandler(String());
                return;
            }
            
            if (m_client.runJavaScriptPrompt_deprecatedForUseWithV0) {
                RefPtr<API::String> string = adoptRef(toImpl(m_client.runJavaScriptPrompt_deprecatedForUseWithV0(toAPI(&page), toAPI(message.impl()), toAPI(defaultValue.impl()), toAPI(frame), m_client.base.clientInfo)));
                
                if (string)
                    completionHandler(string->string());
                else
                    completionHandler(String());
                return;
            }

            completionHandler(String());
        }

        void addMessageToConsoleForTesting(WebPageProxy& page, String&& message) final
        {
            if (!m_client.addMessageToConsole)
                return;
            m_client.addMessageToConsole(toAPI(&page), toAPI(message.impl()), m_client.base.clientInfo);
        }

        void setStatusText(WebPageProxy* page, const String& text) final
        {
            if (!m_client.setStatusText)
                return;

            m_client.setStatusText(toAPI(page), toAPI(text.impl()), m_client.base.clientInfo);
        }

        void mouseDidMoveOverElement(WebPageProxy& page, const WebHitTestResultData& data, OptionSet<WebKit::WebEventModifier> modifiers, API::Object* userData) final
        {
            if (!m_client.mouseDidMoveOverElement && !m_client.mouseDidMoveOverElement_deprecatedForUseWithV0)
                return;

            if (m_client.base.version > 0 && !m_client.mouseDidMoveOverElement)
                return;

            if (!m_client.base.version) {
                m_client.mouseDidMoveOverElement_deprecatedForUseWithV0(toAPI(&page), toAPI(modifiers), toAPI(userData), m_client.base.clientInfo);
                return;
            }

            Ref apiHitTestResult = API::HitTestResult::create(data, &page);
            m_client.mouseDidMoveOverElement(toAPI(&page), toAPI(apiHitTestResult.ptr()), toAPI(modifiers), toAPI(userData), m_client.base.clientInfo);
        }

        void didNotHandleKeyEvent(WebPageProxy* page, const NativeWebKeyboardEvent& event) final
        {
            if (!m_client.didNotHandleKeyEvent)
                return;
            m_client.didNotHandleKeyEvent(toAPI(page), event.nativeEvent(), m_client.base.clientInfo);
        }

        void didNotHandleWheelEvent(WebPageProxy* page, const NativeWebWheelEvent& event) final
        {
            if (!m_client.didNotHandleWheelEvent)
                return;
            m_client.didNotHandleWheelEvent(toAPI(page), event.nativeEvent(), m_client.base.clientInfo);
        }

        void toolbarsAreVisible(WebPageProxy& page, Function<void(bool)>&& completionHandler) final
        {
            if (!m_client.toolbarsAreVisible)
                return completionHandler(true);
            completionHandler(m_client.toolbarsAreVisible(toAPI(&page), m_client.base.clientInfo));
        }

        void setToolbarsAreVisible(WebPageProxy& page, bool visible) final
        {
            if (!m_client.setToolbarsAreVisible)
                return;
            m_client.setToolbarsAreVisible(toAPI(&page), visible, m_client.base.clientInfo);
        }

        void menuBarIsVisible(WebPageProxy& page, Function<void(bool)>&& completionHandler) final
        {
            if (!m_client.menuBarIsVisible)
                return completionHandler(true);
            completionHandler(m_client.menuBarIsVisible(toAPI(&page), m_client.base.clientInfo));
        }

        void setMenuBarIsVisible(WebPageProxy& page, bool visible) final
        {
            if (!m_client.setMenuBarIsVisible)
                return;
            m_client.setMenuBarIsVisible(toAPI(&page), visible, m_client.base.clientInfo);
        }

        void statusBarIsVisible(WebPageProxy& page, Function<void(bool)>&& completionHandler) final
        {
            if (!m_client.statusBarIsVisible)
                return completionHandler(true);
            completionHandler(m_client.statusBarIsVisible(toAPI(&page), m_client.base.clientInfo));
        }

        void setStatusBarIsVisible(WebPageProxy& page, bool visible) final
        {
            if (!m_client.setStatusBarIsVisible)
                return;
            m_client.setStatusBarIsVisible(toAPI(&page), visible, m_client.base.clientInfo);
        }

        void setIsResizable(WebPageProxy& page, bool resizable) final
        {
            if (!m_client.setIsResizable)
                return;
            m_client.setIsResizable(toAPI(&page), resizable, m_client.base.clientInfo);
        }

        void setWindowFrame(WebPageProxy& page, const FloatRect& frame) final
        {
            if (!m_client.setWindowFrame)
                return;

            m_client.setWindowFrame(toAPI(&page), toAPI(frame), m_client.base.clientInfo);
        }

        void windowFrame(WebPageProxy& page, Function<void(WebCore::FloatRect)>&& completionHandler) final
        {
            if (!m_client.getWindowFrame)
                return completionHandler({ });

            completionHandler(toFloatRect(m_client.getWindowFrame(toAPI(&page), m_client.base.clientInfo)));
        }

        bool canRunBeforeUnloadConfirmPanel() const final
        {
            return m_client.runBeforeUnloadConfirmPanel_deprecatedForUseWithV6 || m_client.runBeforeUnloadConfirmPanel;
        }

        void runBeforeUnloadConfirmPanel(WebKit::WebPageProxy& page, WTF::String&& message, WebKit::WebFrameProxy* frame, FrameInfoData&&, Function<void(bool)>&& completionHandler) final
        {
            if (m_client.runBeforeUnloadConfirmPanel) {
                RefPtr<RunBeforeUnloadConfirmPanelResultListener> listener = RunBeforeUnloadConfirmPanelResultListener::create(WTFMove(completionHandler));
                m_client.runBeforeUnloadConfirmPanel(toAPI(&page), toAPI(message.impl()), toAPI(frame), toAPI(listener.get()), m_client.base.clientInfo);
                return;
            }

            if (m_client.runBeforeUnloadConfirmPanel_deprecatedForUseWithV6) {
                bool result = m_client.runBeforeUnloadConfirmPanel_deprecatedForUseWithV6(toAPI(&page), toAPI(message.impl()), toAPI(frame), m_client.base.clientInfo);
                completionHandler(result);
                return;
            }

            completionHandler(true);
        }

        void pageDidScroll(WebPageProxy* page) final
        {
            if (!m_client.pageDidScroll)
                return;

            m_client.pageDidScroll(toAPI(page), m_client.base.clientInfo);
        }

        void exceededDatabaseQuota(WebPageProxy* page, WebFrameProxy* frame, API::SecurityOrigin* origin, const String& databaseName, const String& databaseDisplayName, unsigned long long currentQuota, unsigned long long currentOriginUsage, unsigned long long currentDatabaseUsage, unsigned long long expectedUsage, Function<void(unsigned long long)>&& completionHandler) final
        {
            if (!m_client.exceededDatabaseQuota) {
                completionHandler(currentQuota);
                return;
            }

            completionHandler(m_client.exceededDatabaseQuota(toAPI(page), toAPI(frame), toAPI(origin), toAPI(databaseName.impl()), toAPI(databaseDisplayName.impl()), currentQuota, currentOriginUsage, currentDatabaseUsage, expectedUsage, m_client.base.clientInfo));
        }

        bool runOpenPanel(WebPageProxy& page, WebFrameProxy* frame, FrameInfoData&&, API::OpenPanelParameters* parameters, WebOpenPanelResultListenerProxy* listener) final
        {
            if (!m_client.runOpenPanel)
                return false;

            m_client.runOpenPanel(toAPI(&page), toAPI(frame), toAPI(parameters), toAPI(listener), m_client.base.clientInfo);
            return true;
        }

        void decidePolicyForGeolocationPermissionRequest(WebPageProxy& page, WebFrameProxy& frame, const FrameInfoData& frameInfo, Function<void(bool)>& completionHandler) final
        {
            if (!m_client.decidePolicyForGeolocationPermissionRequest)
                return;

            auto origin = API::SecurityOrigin::create(frameInfo.securityOrigin);
            m_client.decidePolicyForGeolocationPermissionRequest(toAPI(&page), toAPI(&frame), toAPI(origin.ptr()), toAPI(GeolocationPermissionRequest::create(std::exchange(completionHandler, nullptr)).ptr()), m_client.base.clientInfo);
        }

        void decidePolicyForUserMediaPermissionRequest(WebPageProxy& page, WebFrameProxy& frame, API::SecurityOrigin& userMediaDocumentOrigin, API::SecurityOrigin& topLevelDocumentOrigin, UserMediaPermissionRequestProxy& permissionRequest) final
        {
            if (!m_client.decidePolicyForUserMediaPermissionRequest) {
                permissionRequest.deny();
                return;
            }

            m_client.decidePolicyForUserMediaPermissionRequest(toAPI(&page), toAPI(&frame), toAPI(&userMediaDocumentOrigin), toAPI(&topLevelDocumentOrigin), toAPI(&permissionRequest), m_client.base.clientInfo);
        }
        
        void decidePolicyForNotificationPermissionRequest(WebPageProxy& page, API::SecurityOrigin& origin, CompletionHandler<void(bool allowed)>&& completionHandler) final
        {
            if (!m_client.decidePolicyForNotificationPermissionRequest)
                return completionHandler(false);

            m_client.decidePolicyForNotificationPermissionRequest(toAPI(&page), toAPI(&origin), toAPI(NotificationPermissionRequest::create(WTFMove(completionHandler)).ptr()), m_client.base.clientInfo);
        }

        void requestStorageAccessConfirm(WebPageProxy& page, WebFrameProxy* frame, const WebCore::RegistrableDomain& requestingDomain, const WebCore::RegistrableDomain& currentDomain, std::optional<WebCore::OrganizationStorageAccessPromptQuirk>&&, CompletionHandler<void(bool)>&& completionHandler) final
        {
            if (!m_client.requestStorageAccessConfirm) {
                completionHandler(true);
                return;
            }

            auto listener = RequestStorageAccessConfirmResultListener::create(WTFMove(completionHandler));
            m_client.requestStorageAccessConfirm(toAPI(&page), toAPI(frame), toAPI(requestingDomain.string().impl()), toAPI(currentDomain.string().impl()), toAPI(listener.ptr()), m_client.base.clientInfo);
        }

#if ENABLE(DEVICE_ORIENTATION)
        void shouldAllowDeviceOrientationAndMotionAccess(WebPageProxy& page, WebFrameProxy& frame, FrameInfoData&& frameInfo, CompletionHandler<void(bool)>&& completionHandler) final
        {
            if (!m_client.shouldAllowDeviceOrientationAndMotionAccess)
                return completionHandler(false);

            auto origin = API::SecurityOrigin::create(SecurityOrigin::createFromString(page.protectedPageLoadState()->activeURL()).get());
            auto apiFrameInfo = API::FrameInfo::create(WTFMove(frameInfo), &page);
            completionHandler(m_client.shouldAllowDeviceOrientationAndMotionAccess(toAPI(&page), toAPI(origin.ptr()), toAPI(apiFrameInfo.ptr()), m_client.base.clientInfo));
        }
#endif

        // Printing.
        float headerHeight(WebPageProxy& page, WebFrameProxy& frame) final
        {
            if (!m_client.headerHeight)
                return 0;

            return m_client.headerHeight(toAPI(&page), toAPI(&frame), m_client.base.clientInfo);
        }

        float footerHeight(WebPageProxy& page, WebFrameProxy& frame) final
        {
            if (!m_client.footerHeight)
                return 0;

            return m_client.footerHeight(toAPI(&page), toAPI(&frame), m_client.base.clientInfo);
        }

        void drawHeader(WebPageProxy& page, WebFrameProxy& frame, WebCore::FloatRect&& rect) final
        {
            if (!m_client.drawHeader)
                return;

            m_client.drawHeader(toAPI(&page), toAPI(&frame), toAPI(rect), m_client.base.clientInfo);
        }

        void drawFooter(WebPageProxy& page, WebFrameProxy& frame, WebCore::FloatRect&& rect) final
        {
            if (!m_client.drawFooter)
                return;

            m_client.drawFooter(toAPI(&page), toAPI(&frame), toAPI(rect), m_client.base.clientInfo);
        }

        void printFrame(WebPageProxy& page, WebFrameProxy& frame, const WebCore::FloatSize&, CompletionHandler<void()>&& completionHandler) final
        {
            if (m_client.printFrame)
                m_client.printFrame(toAPI(&page), toAPI(&frame), m_client.base.clientInfo);
            completionHandler();
        }

        bool canRunModal() const final
        {
            return m_client.runModal;
        }

        void runModal(WebPageProxy& page) final
        {
            if (!m_client.runModal)
                return;

            m_client.runModal(toAPI(&page), m_client.base.clientInfo);
        }

        void saveDataToFileInDownloadsFolder(WebPageProxy* page, const String& suggestedFilename, const String& mimeType, const URL& originatingURL, API::Data& data) final
        {
            if (!m_client.saveDataToFileInDownloadsFolder)
                return;

            m_client.saveDataToFileInDownloadsFolder(toAPI(page), toAPI(suggestedFilename.impl()), toAPI(mimeType.impl()), toURLRef(originatingURL.string().impl()), toAPI(&data), m_client.base.clientInfo);
        }

        void pinnedStateDidChange(WebPageProxy& page) final
        {
            if (!m_client.pinnedStateDidChange)
                return;

            m_client.pinnedStateDidChange(toAPI(&page), m_client.base.clientInfo);
        }

        void isPlayingMediaDidChange(WebPageProxy& page) final
        {
            if (!m_client.isPlayingAudioDidChange)
                return;

            m_client.isPlayingAudioDidChange(toAPI(&page), m_client.base.clientInfo);
        }

        void didClickAutoFillButton(WebPageProxy& page, API::Object* userInfo) final
        {
            if (!m_client.didClickAutoFillButton)
                return;

            m_client.didClickAutoFillButton(toAPI(&page), toAPI(userInfo), m_client.base.clientInfo);
        }

        void didResignInputElementStrongPasswordAppearance(WebPageProxy& page, API::Object* userInfo) final
        {
            if (!m_client.didResignInputElementStrongPasswordAppearance)
                return;

            m_client.didResignInputElementStrongPasswordAppearance(toAPI(&page), toAPI(userInfo), m_client.base.clientInfo);
        }

#if ENABLE(POINTER_LOCK)
        void requestPointerLock(WebPageProxy* page, CompletionHandler<void(bool)>&& completionHandler) final
        {
            if (!m_client.requestPointerLock)
                return completionHandler(false);

            Ref listener = CompletionListener::create([completionHandler = WTFMove(completionHandler)] mutable { completionHandler(true); });
            m_client.requestPointerLock(toAPI(page), toAPI(listener.ptr()), m_client.base.clientInfo);
        }

        void didLosePointerLock(WebPageProxy* page) final
        {
            if (!m_client.didLosePointerLock)
                return;

            m_client.didLosePointerLock(toAPI(page), m_client.base.clientInfo);
        }
#endif

        static WKAutoplayEventFlags toWKAutoplayEventFlags(OptionSet<WebCore::AutoplayEventFlags> flags)
        {
            WKAutoplayEventFlags wkFlags = kWKAutoplayEventFlagsNone;
            if (flags.contains(WebCore::AutoplayEventFlags::HasAudio))
                wkFlags |= kWKAutoplayEventFlagsHasAudio;
            if (flags.contains(WebCore::AutoplayEventFlags::PlaybackWasPrevented))
                wkFlags |= kWKAutoplayEventFlagsPlaybackWasPrevented;
            if (flags.contains(WebCore::AutoplayEventFlags::MediaIsMainContent))
                wkFlags |= kWKAutoplayEventFlagsMediaIsMainContent;

            return wkFlags;
        }

        static WKAutoplayEvent toWKAutoplayEvent(WebCore::AutoplayEvent event)
        {
            switch (event) {
            case WebCore::AutoplayEvent::DidAutoplayMediaPastThresholdWithoutUserInterference:
                return kWKAutoplayEventDidAutoplayMediaPastThresholdWithoutUserInterference;
            case WebCore::AutoplayEvent::DidPlayMediaWithUserGesture:
                return kWKAutoplayEventDidPlayMediaWithUserGesture;
            case WebCore::AutoplayEvent::DidPreventMediaFromPlaying:
                return kWKAutoplayEventDidPreventFromAutoplaying;
            case WebCore::AutoplayEvent::UserDidInterfereWithPlayback:
                return kWKAutoplayEventUserDidInterfereWithPlayback;
            }

            RELEASE_ASSERT_NOT_REACHED();
        }

        void handleAutoplayEvent(WebPageProxy& page, WebCore::AutoplayEvent event, OptionSet<WebCore::AutoplayEventFlags> flags) final
        {
            if (!m_client.handleAutoplayEvent)
                return;

            m_client.handleAutoplayEvent(toAPI(&page), toWKAutoplayEvent(event), toWKAutoplayEventFlags(flags), m_client.base.clientInfo);
        }

#if ENABLE(WEB_AUTHN)
        // The current method is specialized for WebKitTestRunner.
        void runWebAuthenticationPanel(WebPageProxy&, API::WebAuthenticationPanel& panel, WebFrameProxy&, FrameInfoData&&, CompletionHandler<void(WebKit::WebAuthenticationPanelResult)>&& completionHandler) final
        {
            class PanelClient final : public API::WebAuthenticationPanelClient {
            public:
                static Ref<PanelClient> create()
                {
                    return adoptRef(*new PanelClient);
                }

                void selectAssertionResponse(Vector<Ref<WebCore::AuthenticatorAssertionResponse>>&& responses, WebKit::WebAuthenticationSource, CompletionHandler<void(WebCore::AuthenticatorAssertionResponse*)>&& completionHandler) const final
                {
                    ASSERT(!responses.isEmpty());
                    Ref firstResponse = responses[0];
                    completionHandler(firstResponse.ptr());
                }

                void decidePolicyForLocalAuthenticator(CompletionHandler<void(WebKit::LocalAuthenticatorPolicy)>&& completionHandler) const final
                {
                    completionHandler(WebKit::LocalAuthenticatorPolicy::Allow);
                }

            private:
                PanelClient() = default;
            };

            if (!m_client.runWebAuthenticationPanel) {
                completionHandler(WebKit::WebAuthenticationPanelResult::Unavailable);
                return;
            }

            panel.setClient(PanelClient::create());
            completionHandler(WebKit::WebAuthenticationPanelResult::Presented);
        }
#endif

        void decidePolicyForMediaKeySystemPermissionRequest(WebPageProxy& page, API::SecurityOrigin& origin, const String& keySystem, CompletionHandler<void(bool)>&& completionHandler) final
        {
            if (!m_client.decidePolicyForMediaKeySystemPermissionRequest) {
                completionHandler(false);
                return;
            }

            m_client.decidePolicyForMediaKeySystemPermissionRequest(toAPI(&page), toAPI(&origin), toAPI(API::String::create(keySystem).ptr()), toAPI(MediaKeySystemPermissionCallback::create(WTFMove(completionHandler)).ptr()));
        }

        void queryPermission(const WTF::String& permissionName, API::SecurityOrigin& origin, CompletionHandler<void(std::optional<WebCore::PermissionState>)>&& completionHandler) final
        {
            if (!m_client.queryPermission) {
                completionHandler({ });
                return;
            }
            m_client.queryPermission(toAPI(API::String::create(permissionName).ptr()), toAPI(&origin), toAPI(QueryPermissionResultCallback::create(WTFMove(completionHandler)).ptr()));
        }

        static WKScreenOrientationType toWKScreenOrientationType(WebCore::ScreenOrientationType orientation)
        {
            switch (orientation) {
            case WebCore::ScreenOrientationType::LandscapePrimary:
                return kWKScreenOrientationTypeLandscapePrimary;
            case WebCore::ScreenOrientationType::LandscapeSecondary:
                return kWKScreenOrientationTypeLandscapeSecondary;
            case WebCore::ScreenOrientationType::PortraitSecondary:
                return kWKScreenOrientationTypePortraitSecondary;
            case WebCore::ScreenOrientationType::PortraitPrimary:
                return kWKScreenOrientationTypePortraitPrimary;
            }
            ASSERT_NOT_REACHED();
            return kWKScreenOrientationTypePortraitPrimary;
        }

        bool lockScreenOrientation(WebPageProxy& page, WebCore::ScreenOrientationType orientation) final
        {
            if (!m_client.lockScreenOrientation)
                return false;

            m_client.lockScreenOrientation(toAPI(&page), toWKScreenOrientationType(orientation));
            return true;
        }

        void unlockScreenOrientation(WebPageProxy& page) final
        {
            if (m_client.unlockScreenOrientation)
                m_client.unlockScreenOrientation(toAPI(&page));
        }
    };

    toProtectedImpl(pageRef)->setUIClient(makeUnique<UIClient>(wkClient));
}

void WKPageSetPageNavigationClient(WKPageRef pageRef, const WKPageNavigationClientBase* wkClient)
{
    CRASH_IF_SUSPENDED;
    class NavigationClient : public API::Client<WKPageNavigationClientBase>, public API::NavigationClient {
    public:
        explicit NavigationClient(const WKPageNavigationClientBase* client)
        {
            initialize(client);
        }

    private:
        void decidePolicyForNavigationAction(WebPageProxy& page, Ref<API::NavigationAction>&& navigationAction, Ref<WebKit::WebFramePolicyListenerProxy>&& listener) final
        {
            if (!m_client.decidePolicyForNavigationAction) {
                listener->use();
                return;
            }
            m_client.decidePolicyForNavigationAction(toAPI(&page), toAPI(navigationAction.ptr()), toAPI(listener.ptr()), nullptr, m_client.base.clientInfo);
        }

        void decidePolicyForNavigationResponse(WebPageProxy& page, Ref<API::NavigationResponse>&& navigationResponse, Ref<WebKit::WebFramePolicyListenerProxy>&& listener) override
        {
            if (!m_client.decidePolicyForNavigationResponse) {
                listener->use();
                return;
            }
            m_client.decidePolicyForNavigationResponse(toAPI(&page), toAPI(navigationResponse.ptr()), toAPI(listener.ptr()), nullptr, m_client.base.clientInfo);
        }

        void didStartProvisionalNavigation(WebPageProxy& page, const ResourceRequest&, API::Navigation* navigation, API::Object* userData) override
        {
            if (m_client.didStartProvisionalNavigation)
                m_client.didStartProvisionalNavigation(toAPI(&page), toAPI(navigation), toAPI(userData), m_client.base.clientInfo);
        }

        void didReceiveServerRedirectForProvisionalNavigation(WebPageProxy& page, API::Navigation* navigation, API::Object* userData) override
        {
            if (!m_client.didReceiveServerRedirectForProvisionalNavigation)
                return;
            m_client.didReceiveServerRedirectForProvisionalNavigation(toAPI(&page), toAPI(navigation), toAPI(userData), m_client.base.clientInfo);
        }

        void didFailProvisionalNavigationWithError(WebPageProxy& page, FrameInfoData&& frameInfo, API::Navigation* navigation, const URL&, const WebCore::ResourceError& error, API::Object* userData) override
        {
            if (frameInfo.isMainFrame) {
                if (m_client.didFailProvisionalNavigation)
                    m_client.didFailProvisionalNavigation(toAPI(&page), toAPI(navigation), toAPI(error), toAPI(userData), m_client.base.clientInfo);
            } else {
                if (m_client.didFailProvisionalLoadInSubframe)
                    m_client.didFailProvisionalLoadInSubframe(toAPI(&page), toAPI(navigation), toAPI(API::FrameInfo::create(WTFMove(frameInfo), &page).ptr()), toAPI(error), toAPI(userData), m_client.base.clientInfo);
            }
        }

        void didCommitNavigation(WebPageProxy& page, API::Navigation* navigation, API::Object* userData) override
        {
            if (m_client.didCommitNavigation)
                m_client.didCommitNavigation(toAPI(&page), toAPI(navigation), toAPI(userData), m_client.base.clientInfo);
        }

        void didFinishNavigation(WebPageProxy& page, API::Navigation* navigation, API::Object* userData) override
        {
            if (m_client.didFinishNavigation)
                m_client.didFinishNavigation(toAPI(&page), toAPI(navigation), toAPI(userData), m_client.base.clientInfo);
        }

        void didFailNavigationWithError(WebPageProxy& page, const FrameInfoData&, API::Navigation* navigation, const URL&, const WebCore::ResourceError& error, API::Object* userData) override
        {
            if (m_client.didFailNavigation)
                m_client.didFailNavigation(toAPI(&page), toAPI(navigation), toAPI(error), toAPI(userData), m_client.base.clientInfo);
        }

        void didFinishDocumentLoad(WebPageProxy& page, API::Navigation* navigation, API::Object* userData) override
        {
            if (!m_client.didFinishDocumentLoad)
                return;
            m_client.didFinishDocumentLoad(toAPI(&page), toAPI(navigation), toAPI(userData), m_client.base.clientInfo);
        }

        void didSameDocumentNavigation(WebPageProxy& page, API::Navigation* navigation, WebKit::SameDocumentNavigationType navigationType, API::Object* userData) override
        {
            if (!m_client.didSameDocumentNavigation)
                return;
            m_client.didSameDocumentNavigation(toAPI(&page), toAPI(navigation), toAPI(navigationType), toAPI(userData), m_client.base.clientInfo);
        }
        
        void renderingProgressDidChange(WebPageProxy& page, OptionSet<WebCore::LayoutMilestone> milestones) override
        {
            if (!m_client.renderingProgressDidChange)
                return;
            m_client.renderingProgressDidChange(toAPI(&page), pageRenderingProgressEvents(milestones), nullptr, m_client.base.clientInfo);
        }
        
        void didReceiveAuthenticationChallenge(WebPageProxy& page, AuthenticationChallengeProxy& authenticationChallenge) override
        {
            if (m_client.canAuthenticateAgainstProtectionSpace && !m_client.canAuthenticateAgainstProtectionSpace(toAPI(&page), toAPI(WebProtectionSpace::create(authenticationChallenge.core().protectionSpace()).ptr()), m_client.base.clientInfo))
                return authenticationChallenge.listener().completeChallenge(WebKit::AuthenticationChallengeDisposition::RejectProtectionSpaceAndContinue);
            if (!m_client.didReceiveAuthenticationChallenge)
                return authenticationChallenge.listener().completeChallenge(WebKit::AuthenticationChallengeDisposition::PerformDefaultHandling);
            m_client.didReceiveAuthenticationChallenge(toAPI(&page), toAPI(&authenticationChallenge), m_client.base.clientInfo);
        }

        bool processDidTerminate(WebPageProxy& page, WebKit::ProcessTerminationReason reason) override
        {
            if (m_client.webProcessDidTerminate) {
                m_client.webProcessDidTerminate(toAPI(&page), toAPI(reason), m_client.base.clientInfo);
                return true;
            }

            if (m_client.webProcessDidCrash && reason != WebKit::ProcessTerminationReason::RequestedByClient) {
                m_client.webProcessDidCrash(toAPI(&page), m_client.base.clientInfo);
                return true;
            }

            return false;
        }

        void legacyWebCryptoMasterKey(WebKit::WebPageProxy& page, CompletionHandler<void(std::optional<Vector<uint8_t>>&&)>&& completionHandler) override
        {
            if (m_client.copyWebCryptoMasterKey) {
                if (auto data = adoptRef(toImpl(m_client.copyWebCryptoMasterKey(toAPI(&page), m_client.base.clientInfo))))
                    return completionHandler(Vector(data->span()));
            }
            return WebCore::getDefaultWebCryptoMasterKey(WTFMove(completionHandler));
        }

        void navigationActionDidBecomeDownload(WebKit::WebPageProxy& page, API::NavigationAction& action, WebKit::DownloadProxy& download) override
        {
            if (!m_client.navigationActionDidBecomeDownload)
                return;
            m_client.navigationActionDidBecomeDownload(toAPI(&page), toAPI(&action), toAPI(&download), m_client.base.clientInfo);
        }

        void navigationResponseDidBecomeDownload(WebKit::WebPageProxy& page, API::NavigationResponse& response, WebKit::DownloadProxy& download) override
        {
            if (!m_client.navigationResponseDidBecomeDownload)
                return;
            m_client.navigationResponseDidBecomeDownload(toAPI(&page), toAPI(&response), toAPI(&download), m_client.base.clientInfo);
        }

        void contextMenuDidCreateDownload(WebKit::WebPageProxy& page, WebKit::DownloadProxy& download) override
        {
            if (!m_client.contextMenuDidCreateDownload)
                return;
            m_client.contextMenuDidCreateDownload(toAPI(&page), toAPI(&download), m_client.base.clientInfo);
        }

        void didBeginNavigationGesture(WebPageProxy& page) override
        {
            if (!m_client.didBeginNavigationGesture)
                return;
            m_client.didBeginNavigationGesture(toAPI(&page), m_client.base.clientInfo);
        }

        void didEndNavigationGesture(WebPageProxy& page, bool willNavigate, WebKit::WebBackForwardListItem& item) override
        {
            if (!m_client.didEndNavigationGesture)
                return;
            m_client.didEndNavigationGesture(toAPI(&page), willNavigate ? toAPI(&item) : nullptr, m_client.base.clientInfo);
        }

        void willEndNavigationGesture(WebPageProxy& page, bool willNavigate, WebKit::WebBackForwardListItem& item) override
        {
            if (!m_client.willEndNavigationGesture)
                return;
            m_client.willEndNavigationGesture(toAPI(&page), willNavigate ? toAPI(&item) : nullptr, m_client.base.clientInfo);
        }

        void didRemoveNavigationGestureSnapshot(WebPageProxy& page) override
        {
            if (!m_client.didRemoveNavigationGestureSnapshot)
                return;
            m_client.didRemoveNavigationGestureSnapshot(toAPI(&page), m_client.base.clientInfo);
        }
        
#if ENABLE(CONTENT_EXTENSIONS)
        void contentRuleListNotification(WebPageProxy& page, URL&& url, ContentRuleListResults&& results) final
        {
            if (!m_client.contentRuleListNotification)
                return;

            Vector<RefPtr<API::Object>> apiListIdentifiers;
            Vector<RefPtr<API::Object>> apiNotifications;
            for (const auto& pair : results.results) {
                const String& listIdentifier = pair.first;
                const auto& result = pair.second;
                for (const String& notification : result.notifications) {
                    apiListIdentifiers.append(API::String::create(listIdentifier));
                    apiNotifications.append(API::String::create(notification));
                }
            }

            if (!apiNotifications.isEmpty())
                m_client.contentRuleListNotification(toAPI(&page), toURLRef(url.string().impl()), toAPI(API::Array::create(WTFMove(apiListIdentifiers)).ptr()), toAPI(API::Array::create(WTFMove(apiNotifications)).ptr()), m_client.base.clientInfo);
        }
#endif
    };

    toProtectedImpl(pageRef)->setNavigationClient(makeUniqueRef<NavigationClient>(wkClient));
}

class StateClient final : public RefCounted<StateClient>, public API::Client<WKPageStateClientBase>, public PageLoadState::Observer {
    WTF_MAKE_TZONE_ALLOCATED_INLINE(StateClient);
public:
    static Ref<StateClient> create(const WKPageStateClientBase* client)
    {
        return adoptRef(*new StateClient(client));
    }

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

private:
    explicit StateClient(const WKPageStateClientBase* client)
    {
        initialize(client);
    }

    void willChangeIsLoading() override
    {
        if (!m_client.willChangeIsLoading)
            return;
        m_client.willChangeIsLoading(m_client.base.clientInfo);
    }

    void didChangeIsLoading() override
    {
        if (!m_client.didChangeIsLoading)
            return;
        m_client.didChangeIsLoading(m_client.base.clientInfo);
    }

    void willChangeTitle() override
    {
        if (!m_client.willChangeTitle)
            return;
        m_client.willChangeTitle(m_client.base.clientInfo);
    }

    void didChangeTitle() override
    {
        if (!m_client.didChangeTitle)
            return;
        m_client.didChangeTitle(m_client.base.clientInfo);
    }

    void willChangeActiveURL() override
    {
        if (!m_client.willChangeActiveURL)
            return;
        m_client.willChangeActiveURL(m_client.base.clientInfo);
    }

    void didChangeActiveURL() override
    {
        if (!m_client.didChangeActiveURL)
            return;
        m_client.didChangeActiveURL(m_client.base.clientInfo);
    }

    void willChangeHasOnlySecureContent() override
    {
        if (!m_client.willChangeHasOnlySecureContent)
            return;
        m_client.willChangeHasOnlySecureContent(m_client.base.clientInfo);
    }

    void didChangeHasOnlySecureContent() override
    {
        if (!m_client.didChangeHasOnlySecureContent)
            return;
        m_client.didChangeHasOnlySecureContent(m_client.base.clientInfo);
    }

    void willChangeEstimatedProgress() override
    {
        if (!m_client.willChangeEstimatedProgress)
            return;
        m_client.willChangeEstimatedProgress(m_client.base.clientInfo);
    }

    void didChangeEstimatedProgress() override
    {
        if (!m_client.didChangeEstimatedProgress)
            return;
        m_client.didChangeEstimatedProgress(m_client.base.clientInfo);
    }

    void willChangeCanGoBack() override
    {
        if (!m_client.willChangeCanGoBack)
            return;
        m_client.willChangeCanGoBack(m_client.base.clientInfo);
    }

    void didChangeCanGoBack() override
    {
        if (!m_client.didChangeCanGoBack)
            return;
        m_client.didChangeCanGoBack(m_client.base.clientInfo);
    }

    void willChangeCanGoForward() override
    {
        if (!m_client.willChangeCanGoForward)
            return;
        m_client.willChangeCanGoForward(m_client.base.clientInfo);
    }

    void didChangeCanGoForward() override
    {
        if (!m_client.didChangeCanGoForward)
            return;
        m_client.didChangeCanGoForward(m_client.base.clientInfo);
    }

    void willChangeNetworkRequestsInProgress() override
    {
        if (!m_client.willChangeNetworkRequestsInProgress)
            return;
        m_client.willChangeNetworkRequestsInProgress(m_client.base.clientInfo);
    }

    void didChangeNetworkRequestsInProgress() override
    {
        if (!m_client.didChangeNetworkRequestsInProgress)
            return;
        m_client.didChangeNetworkRequestsInProgress(m_client.base.clientInfo);
    }

    void willChangeCertificateInfo() override
    {
        if (!m_client.willChangeCertificateInfo)
            return;
        m_client.willChangeCertificateInfo(m_client.base.clientInfo);
    }

    void didChangeCertificateInfo() override
    {
        if (!m_client.didChangeCertificateInfo)
            return;
        m_client.didChangeCertificateInfo(m_client.base.clientInfo);
    }

    void willChangeWebProcessIsResponsive() override
    {
        if (!m_client.willChangeWebProcessIsResponsive)
            return;
        m_client.willChangeWebProcessIsResponsive(m_client.base.clientInfo);
    }

    void didChangeWebProcessIsResponsive() override
    {
        if (!m_client.didChangeWebProcessIsResponsive)
            return;
        m_client.didChangeWebProcessIsResponsive(m_client.base.clientInfo);
    }

    void didSwapWebProcesses() override
    {
        if (!m_client.didSwapWebProcesses)
            return;
        m_client.didSwapWebProcesses(m_client.base.clientInfo);
    }
};

void WKPageSetPageStateClient(WKPageRef pageRef, WKPageStateClientBase* client)
{
    CRASH_IF_SUSPENDED;
    if (client)
        toProtectedImpl(pageRef)->setPageLoadStateObserver(StateClient::create(client));
    else
        toProtectedImpl(pageRef)->setPageLoadStateObserver(nullptr);
}

void WKPageEvaluateJavaScriptInMainFrame(WKPageRef pageRef, WKStringRef scriptRef, void* context, WKPageEvaluateJavaScriptFunction callback)
{
    CRASH_IF_SUSPENDED;

    toProtectedImpl(pageRef)->runJavaScriptInMainFrame(WebKit::RunJavaScriptParameters {
        toProtectedImpl(scriptRef)->string(),
        JSC::SourceTaintedOrigin::Untainted,
        URL { },
        WebCore::RunAsAsyncFunction::No,
        std::nullopt,
        WebCore::ForceUserGesture::Yes,
        RemoveTransientActivation::Yes
    }, !!callback, [context, callback] (auto&& result) {
        if (!callback)
            return;
        if (result)
            callback(result->toWK().get(), nullptr, context);
        else
            callback(nullptr, nullptr, context);
    });
}

static CompletionHandler<void(const String&)> toStringCallback(void* context, void(*callback)(WKStringRef, WKErrorRef, void*))
{
    return [context, callback] (const String& returnValue) {
        callback(toAPI(API::String::create(returnValue).ptr()), nullptr, context);
    };
}

void WKPageRenderTreeExternalRepresentation(WKPageRef pageRef, void* context, WKPageRenderTreeExternalRepresentationFunction callback)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->getRenderTreeExternalRepresentation(toStringCallback(context, callback));
}

void WKPageGetSourceForFrame(WKPageRef pageRef, WKFrameRef frameRef, void* context, WKPageGetSourceForFrameFunction callback)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->getSourceForFrame(toProtectedImpl(frameRef).get(), toStringCallback(context, callback));
}

void WKPageGetContentsAsString(WKPageRef pageRef, void* context, WKPageGetContentsAsStringFunction callback)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->getContentsAsString(ContentAsStringIncludesChildFrames::No, toStringCallback(context, callback));
}

void WKPageGetBytecodeProfile(WKPageRef pageRef, void* context, WKPageGetBytecodeProfileFunction callback)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->getBytecodeProfile(toStringCallback(context, callback));
}

void WKPageGetSamplingProfilerOutput(WKPageRef pageRef, void* context, WKPageGetSamplingProfilerOutputFunction callback)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->getSamplingProfilerOutput(toStringCallback(context, callback));
}

void WKPageGetSelectionAsWebArchiveData(WKPageRef pageRef, void* context, WKPageGetSelectionAsWebArchiveDataFunction callback)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->getSelectionAsWebArchiveData([context, callback] (API::Data* data) {
        callback(toAPI(data), nullptr, context);
    });
}

void WKPageGetContentsAsMHTMLData(WKPageRef pageRef, void* context, WKPageGetContentsAsMHTMLDataFunction callback)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(MHTML)
    toProtectedImpl(pageRef)->getContentsAsMHTMLData([context, callback] (API::Data* data) {
        callback(toAPI(data), nullptr, context);
    });
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(context);
    UNUSED_PARAM(callback);
#endif
}

void WKPageForceRepaint(WKPageRef pageRef, void* context, WKPageForceRepaintFunction callback)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->updateRenderingWithForcedRepaint([context, callback]() {
        callback(nullptr, context);
    });
}

WK_EXPORT WKURLRef WKPageCopyPendingAPIRequestURL(WKPageRef pageRef)
{
    const String& pendingAPIRequestURL = toProtectedImpl(pageRef)->pageLoadState().pendingAPIRequestURL();

    if (pendingAPIRequestURL.isNull())
        return nullptr;

    return toCopiedURLAPI(pendingAPIRequestURL);
}

WKURLRef WKPageCopyActiveURL(WKPageRef pageRef)
{
    return toCopiedURLAPI(toProtectedImpl(pageRef)->protectedPageLoadState()->activeURL());
}

WKURLRef WKPageCopyProvisionalURL(WKPageRef pageRef)
{
    return toCopiedURLAPI(toProtectedImpl(pageRef)->pageLoadState().provisionalURL());
}

WKURLRef WKPageCopyCommittedURL(WKPageRef pageRef)
{
    return toCopiedURLAPI(toProtectedImpl(pageRef)->pageLoadState().url());
}

WKStringRef WKPageCopyStandardUserAgentWithApplicationName(WKStringRef applicationName)
{
    return toCopiedAPI(WebPageProxy::standardUserAgent(toProtectedImpl(applicationName)->string()));
}

void WKPageValidateCommand(WKPageRef pageRef, WKStringRef command, void* context, WKPageValidateCommandCallback callback)
{
    CRASH_IF_SUSPENDED;
    auto commandName = toProtectedImpl(command)->string();
    toProtectedImpl(pageRef)->validateCommand(commandName, [context, callback, commandName](bool isEnabled, int32_t state) {
        callback(toAPI(API::String::create(commandName).ptr()), isEnabled, state, nullptr, context);
    });
}

void WKPageExecuteCommand(WKPageRef pageRef, WKStringRef command)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->executeEditCommand(toProtectedImpl(command)->string());
}

static PrintInfo printInfoFromWKPrintInfo(const WKPrintInfo& printInfo)
{
    PrintInfo result;
    result.pageSetupScaleFactor = printInfo.pageSetupScaleFactor;
    result.availablePaperWidth = printInfo.availablePaperWidth;
    result.availablePaperHeight = printInfo.availablePaperHeight;
    return result;
}

void WKPageComputePagesForPrinting(WKPageRef pageRef, WKFrameRef frame, WKPrintInfo printInfo, WKPageComputePagesForPrintingFunction callback, void* context)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->computePagesForPrinting(toProtectedImpl(frame)->frameID(), printInfoFromWKPrintInfo(printInfo), [context, callback](const Vector<WebCore::IntRect>& rects, double scaleFactor, const WebCore::FloatBoxExtent& computedPageMargin) {
        Vector<WKRect> wkRects(rects.size());
        for (size_t i = 0; i < rects.size(); ++i)
            wkRects[i] = toAPI(rects[i]);
        callback(wkRects.mutableSpan().data(), wkRects.size(), scaleFactor, nullptr, context);
    });
}

#if PLATFORM(COCOA)
void WKPageDrawPagesToPDF(WKPageRef pageRef, WKFrameRef frame, WKPrintInfo printInfo, uint32_t first, uint32_t count, WKPageDrawToPDFFunction callback, void* context)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->drawPagesToPDF(*toProtectedImpl(frame), printInfoFromWKPrintInfo(printInfo), first, count, [context, callback] (API::Data* data) {
        callback(toAPI(data), nullptr, context);
    });
}
#endif

void WKPageBeginPrinting(WKPageRef pageRef, WKFrameRef frame, WKPrintInfo printInfo)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->beginPrinting(toProtectedImpl(frame).get(), printInfoFromWKPrintInfo(printInfo));
}

void WKPageEndPrinting(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->endPrinting();
}

bool WKPageGetIsControlledByAutomation(WKPageRef page)
{
    return toProtectedImpl(page)->isControlledByAutomation();
}

void WKPageSetControlledByAutomation(WKPageRef pageRef, bool controlled)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setControlledByAutomation(controlled);
}

bool WKPageGetAllowsRemoteInspection(WKPageRef page)
{
#if ENABLE(REMOTE_INSPECTOR)
    return toProtectedImpl(page)->inspectable();
#else
    UNUSED_PARAM(page);
    return false;
#endif    
}

void WKPageSetAllowsRemoteInspection(WKPageRef pageRef, bool allow)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(REMOTE_INSPECTOR)
    toProtectedImpl(pageRef)->setInspectable(allow);
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(allow);
#endif
}

void WKPageShowWebInspectorForTesting(WKPageRef pageRef)
{
    RefPtr<WebInspectorUIProxy> inspector = toProtectedImpl(pageRef)->inspector();
    inspector->markAsUnderTest();
    inspector->show();
}

void WKPageSetMediaVolume(WKPageRef pageRef, float volume)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setMediaVolume(volume);
}

void WKPageSetMuted(WKPageRef pageRef, WKMediaMutedState mutedState)
{
    CRASH_IF_SUSPENDED;
    WebCore::MediaProducerMutedStateFlags coreState;

    if (mutedState & kWKMediaAudioMuted)
        coreState.add(WebCore::MediaProducerMutedState::AudioIsMuted);
    if (mutedState & kWKMediaCaptureDevicesMuted)
        coreState.add(WebCore::MediaProducer::AudioAndVideoCaptureIsMuted);

    if (mutedState & kWKMediaScreenCaptureMuted) {
        coreState.add(WebCore::MediaProducerMutedState::ScreenCaptureIsMuted);
        coreState.add(WebCore::MediaProducerMutedState::WindowCaptureIsMuted);
        coreState.add(WebCore::MediaProducerMutedState::SystemAudioCaptureIsMuted);
    }
    if (mutedState & kWKMediaCameraCaptureMuted)
        coreState.add(WebCore::MediaProducerMutedState::VideoCaptureIsMuted);
    if (mutedState & kWKMediaMicrophoneCaptureMuted)
        coreState.add(WebCore::MediaProducerMutedState::AudioCaptureIsMuted);

    if (mutedState & kWKMediaScreenCaptureUnmuted) {
        coreState.remove(WebCore::MediaProducerMutedState::ScreenCaptureIsMuted);
        coreState.remove(WebCore::MediaProducerMutedState::WindowCaptureIsMuted);
        coreState.remove(WebCore::MediaProducerMutedState::SystemAudioCaptureIsMuted);
    }
    if (mutedState & kWKMediaCameraCaptureUnmuted)
        coreState.remove(WebCore::MediaProducerMutedState::VideoCaptureIsMuted);
    if (mutedState & kWKMediaMicrophoneCaptureUnmuted)
        coreState.remove(WebCore::MediaProducerMutedState::AudioCaptureIsMuted);

    toProtectedImpl(pageRef)->setMuted(coreState, WebKit::WebPageProxy::FromApplication::Yes);
}

void WKPageSetMediaCaptureEnabled(WKPageRef pageRef, bool enabled)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setMediaCaptureEnabled(enabled);
}

bool WKPageGetMediaCaptureEnabled(WKPageRef page)
{
    return toProtectedImpl(page)->mediaCaptureEnabled();
}

void WKPageClearUserMediaState(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(MEDIA_STREAM)
    toProtectedImpl(pageRef)->clearUserMediaState();
#else
    UNUSED_PARAM(pageRef);
#endif
}

void WKPagePostMessageToInjectedBundle(WKPageRef pageRef, WKStringRef messageNameRef, WKTypeRef messageBodyRef)
{
    toProtectedImpl(pageRef)->postMessageToInjectedBundle(toProtectedImpl(messageNameRef)->string(), toProtectedImpl(messageBodyRef).get());
}

WKArrayRef WKPageCopyRelatedPages(WKPageRef pageRef)
{
    Vector<RefPtr<API::Object>> relatedPages;

    for (Ref page : toProtectedImpl(pageRef)->protectedLegacyMainFrameProcess()->pages()) {
        if (page.ptr() != toProtectedImpl(pageRef))
            relatedPages.append(WTFMove(page));
    }

    return toAPILeakingRef(API::Array::create(WTFMove(relatedPages)));
}

WKFrameRef WKPageLookUpFrameFromHandle(WKPageRef pageRef, WKFrameHandleRef handleRef)
{
    RefPtr page = toProtectedImpl(pageRef);
    RefPtr frame = WebFrameProxy::webFrame(toProtectedImpl(handleRef)->frameID());
    if (!frame || frame->page() != page.get())
        return nullptr;

    return toAPI(frame.get());
}

void WKPageSetMayStartMediaWhenInWindow(WKPageRef pageRef, bool mayStartMedia)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setMayStartMediaWhenInWindow(mayStartMedia);
}

void WKPageSelectContextMenuItem(WKPageRef pageRef, WKContextMenuItemRef item, WKFrameInfoRef frameInfo)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(CONTEXT_MENUS)
    toProtectedImpl(pageRef)->contextMenuItemSelected((toProtectedImpl(item)->data()), toProtectedImpl(frameInfo)->frameInfoData());
#else
    UNUSED_PARAM(pageRef);
    UNUSED_PARAM(item);
#endif
}

WKScrollPinningBehavior WKPageGetScrollPinningBehavior(WKPageRef page)
{
    ScrollPinningBehavior pinning = toProtectedImpl(page)->scrollPinningBehavior();
    
    switch (pinning) {
    case WebCore::ScrollPinningBehavior::DoNotPin:
        return kWKScrollPinningBehaviorDoNotPin;
    case WebCore::ScrollPinningBehavior::PinToTop:
        return kWKScrollPinningBehaviorPinToTop;
    case WebCore::ScrollPinningBehavior::PinToBottom:
        return kWKScrollPinningBehaviorPinToBottom;
    }
    
    ASSERT_NOT_REACHED();
    return kWKScrollPinningBehaviorDoNotPin;
}

void WKPageSetScrollPinningBehavior(WKPageRef pageRef, WKScrollPinningBehavior pinning)
{
    CRASH_IF_SUSPENDED;
    ScrollPinningBehavior corePinning = ScrollPinningBehavior::DoNotPin;

    switch (pinning) {
    case kWKScrollPinningBehaviorDoNotPin:
        corePinning = ScrollPinningBehavior::DoNotPin;
        break;
    case kWKScrollPinningBehaviorPinToTop:
        corePinning = ScrollPinningBehavior::PinToTop;
        break;
    case kWKScrollPinningBehaviorPinToBottom:
        corePinning = ScrollPinningBehavior::PinToBottom;
        break;
    default:
        ASSERT_NOT_REACHED();
    }
    
    toProtectedImpl(pageRef)->setScrollPinningBehavior(corePinning);
}

bool WKPageGetAddsVisitedLinks(WKPageRef page)
{
    return toProtectedImpl(page)->addsVisitedLinks();
}

void WKPageSetAddsVisitedLinks(WKPageRef pageRef, bool addsVisitedLinks)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setAddsVisitedLinks(addsVisitedLinks);
}

bool WKPageIsPlayingAudio(WKPageRef page)
{
    return toProtectedImpl(page)->isPlayingAudio();
}

WKMediaState WKPageGetMediaState(WKPageRef page)
{
    WebCore::MediaProducerMediaStateFlags coreState = toProtectedImpl(page)->reportedMediaState();
    WKMediaState state = kWKMediaIsNotPlaying;

    if (coreState & WebCore::MediaProducerMediaState::IsPlayingAudio)
        state |= kWKMediaIsPlayingAudio;
    if (coreState & WebCore::MediaProducerMediaState::IsPlayingVideo)
        state |= kWKMediaIsPlayingVideo;
    if (coreState & WebCore::MediaProducerMediaState::HasActiveAudioCaptureDevice)
        state |= kWKMediaHasActiveAudioCaptureDevice;
    if (coreState & WebCore::MediaProducerMediaState::HasActiveVideoCaptureDevice)
        state |= kWKMediaHasActiveVideoCaptureDevice;
    if (coreState & WebCore::MediaProducerMediaState::HasMutedAudioCaptureDevice)
        state |= kWKMediaHasMutedAudioCaptureDevice;
    if (coreState & WebCore::MediaProducerMediaState::HasMutedVideoCaptureDevice)
        state |= kWKMediaHasMutedVideoCaptureDevice;
    if (coreState & WebCore::MediaProducerMediaState::HasActiveScreenCaptureDevice)
        state |= kWKMediaHasActiveScreenCaptureDevice;
    if (coreState & WebCore::MediaProducerMediaState::HasMutedScreenCaptureDevice)
        state |= kWKMediaHasMutedScreenCaptureDevice;
    if (coreState & WebCore::MediaProducerMediaState::HasActiveWindowCaptureDevice)
        state |= kWKMediaHasActiveWindowCaptureDevice;
    if (coreState & WebCore::MediaProducerMediaState::HasMutedWindowCaptureDevice)
        state |= kWKMediaHasMutedWindowCaptureDevice;

    return state;
}

void WKPageClearWheelEventTestMonitor(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    if (RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting())
        pageForTesting->clearWheelEventTestMonitor();
}

void WKPageCallAfterNextPresentationUpdate(WKPageRef pageRef, void* context, WKPagePostPresentationUpdateFunction callback)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->callAfterNextPresentationUpdate([context, callback] {
        callback(nullptr, context);
    });
}

void WKPageSetIgnoresViewportScaleLimits(WKPageRef pageRef, bool ignoresViewportScaleLimits)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(META_VIEWPORT)
    toProtectedImpl(pageRef)->setForceAlwaysUserScalable(ignoresViewportScaleLimits);
#endif
}

void WKPageSetUseDarkAppearanceForTesting(WKPageRef pageRef, bool useDarkAppearance)
{
    toProtectedImpl(pageRef)->setUseDarkAppearanceForTesting(useDarkAppearance);
}

ProcessID WKPageGetProcessIdentifier(WKPageRef page)
{
    return toProtectedImpl(page)->legacyMainFrameProcessID();
}

ProcessID WKPageGetGPUProcessIdentifier(WKPageRef page)
{
#if ENABLE(GPU_PROCESS)
    RefPtr gpuProcess = toProtectedImpl(page)->configuration().processPool().gpuProcess();
    if (!gpuProcess)
        return 0;
    return gpuProcess->processID();
#else
    return 0;
#endif
}

void WKPageGetApplicationManifest(WKPageRef pageRef, void* context, WKPageGetApplicationManifestFunction function)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(APPLICATION_MANIFEST)
    toProtectedImpl(pageRef)->getApplicationManifest([function, context](const std::optional<WebCore::ApplicationManifest>& manifest) {
        function(context);
    });
#else
    UNUSED_PARAM(pageRef);
    function(context);
#endif
}

void WKPageDumpPrivateClickMeasurement(WKPageRef pageRef, WKPageDumpPrivateClickMeasurementFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(nullptr, callbackContext);

    pageForTesting->dumpPrivateClickMeasurement([callbackContext, callback] (const String& privateClickMeasurement) {
        callback(WebKit::toAPI(privateClickMeasurement.impl()), callbackContext);
    });
}

void WKPageClearPrivateClickMeasurement(WKPageRef pageRef, WKPageClearPrivateClickMeasurementFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->clearPrivateClickMeasurement([callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSetPrivateClickMeasurementOverrideTimerForTesting(WKPageRef pageRef, bool value, WKPageSetPrivateClickMeasurementOverrideTimerForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->setPrivateClickMeasurementOverrideTimer(value, [callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageMarkAttributedPrivateClickMeasurementsAsExpiredForTesting(WKPageRef pageRef, WKPageMarkAttributedPrivateClickMeasurementsAsExpiredForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->markAttributedPrivateClickMeasurementsAsExpired([callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSetPrivateClickMeasurementEphemeralMeasurementForTesting(WKPageRef pageRef, bool value, WKPageSetPrivateClickMeasurementEphemeralMeasurementForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->setPrivateClickMeasurementEphemeralMeasurement(value, [callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSimulatePrivateClickMeasurementSessionRestart(WKPageRef pageRef, WKPageSimulatePrivateClickMeasurementSessionRestartFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->simulatePrivateClickMeasurementSessionRestart([callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSetPrivateClickMeasurementTokenPublicKeyURLForTesting(WKPageRef pageRef, WKURLRef URLRef, WKPageSetPrivateClickMeasurementTokenPublicKeyURLForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->setPrivateClickMeasurementTokenPublicKeyURL(URL { toWTFString(URLRef) }, [callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSetPrivateClickMeasurementTokenSignatureURLForTesting(WKPageRef pageRef, WKURLRef URLRef, WKPageSetPrivateClickMeasurementTokenSignatureURLForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->setPrivateClickMeasurementTokenSignatureURL(URL { toWTFString(URLRef) }, [callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSetPrivateClickMeasurementAttributionReportURLsForTesting(WKPageRef pageRef, WKURLRef sourceURL, WKURLRef destinationURL, WKPageSetPrivateClickMeasurementAttributionReportURLsForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->setPrivateClickMeasurementAttributionReportURLs(URL { toWTFString(sourceURL) }, URL { toWTFString(destinationURL) }, [callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageMarkPrivateClickMeasurementsAsExpiredForTesting(WKPageRef pageRef, WKPageMarkPrivateClickMeasurementsAsExpiredForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->markPrivateClickMeasurementsAsExpired([callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSetPCMFraudPreventionValuesForTesting(WKPageRef pageRef, WKStringRef unlinkableToken, WKStringRef secretToken, WKStringRef signature, WKStringRef keyID, WKPageSetPCMFraudPreventionValuesForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->setPCMFraudPreventionValues(toWTFString(unlinkableToken), toWTFString(secretToken), toWTFString(signature), toWTFString(keyID), [callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSetPrivateClickMeasurementAppBundleIDForTesting(WKPageRef pageRef, WKStringRef appBundleIDForTesting, WKPageSetPrivateClickMeasurementAppBundleIDForTestingFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(callbackContext);

    pageForTesting->setPrivateClickMeasurementAppBundleID(toWTFString(appBundleIDForTesting), [callbackContext, callback] {
        callback(callbackContext);
    });
}

void WKPageSetMockCameraOrientationForTesting(WKPageRef pageRef, uint64_t rotation, WKStringRef persistentId)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setMediaCaptureRotationForTesting(rotation, toWTFString(persistentId));
}

bool WKPageIsMockRealtimeMediaSourceCenterEnabled(WKPageRef)
{
#if (PLATFORM(COCOA) || USE(GSTREAMER)) && ENABLE(MEDIA_STREAM)
    return MockRealtimeMediaSourceCenter::mockRealtimeMediaSourceCenterEnabled();
#else
    return false;
#endif
}

void WKPageSetMockCaptureDevicesInterrupted(WKPageRef pageRef, bool isCameraInterrupted, bool isMicrophoneInterrupted)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(MEDIA_STREAM) && ENABLE(GPU_PROCESS)
    Ref preferences = toProtectedImpl(pageRef)->preferences();
    if (preferences->useGPUProcessForMediaEnabled()) {
        Ref gpuProcess = toProtectedImpl(pageRef)->configuration().protectedProcessPool()->ensureGPUProcess();
        gpuProcess->setMockCaptureDevicesInterrupted(isCameraInterrupted, isMicrophoneInterrupted);
    }
#endif
#if ENABLE(MEDIA_STREAM) && USE(GSTREAMER)
    toProtectedImpl(pageRef)->setMockCaptureDevicesInterrupted(isCameraInterrupted, isMicrophoneInterrupted);
#endif
}

void WKPageTriggerMockCaptureConfigurationChange(WKPageRef pageRef, bool forCamera, bool forMicrophone, bool forDisplay)
{
    CRASH_IF_SUSPENDED;
#if ENABLE(MEDIA_STREAM)
#if USE(GSTREAMER)
    toProtectedImpl(pageRef)->triggerMockCaptureConfigurationChange(forCamera, forMicrophone, forDisplay);
#else
    MockRealtimeMediaSourceCenter::singleton().triggerMockCaptureConfigurationChange(forCamera, forMicrophone, forDisplay);
#endif // USE(GSTREAMER)

#if ENABLE(GPU_PROCESS)
    Ref preferences = toProtectedImpl(pageRef)->preferences();
    if (!preferences->useGPUProcessForMediaEnabled())
        return;

    Ref gpuProcess = toProtectedImpl(pageRef)->configuration().protectedProcessPool()->ensureGPUProcess();
    gpuProcess->triggerMockCaptureConfigurationChange(forCamera, forMicrophone, forDisplay);
#endif // ENABLE(GPU_PROCESS)

#endif // ENABLE(MEDIA_STREAM)
}

void WKPageLoadedSubresourceDomains(WKPageRef pageRef, WKPageLoadedSubresourceDomainsFunction callback, void* callbackContext)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->getLoadedSubresourceDomains([callbackContext, callback](Vector<RegistrableDomain>&& domains) {
        Vector<RefPtr<API::Object>> apiDomains = WTF::map(domains, [](auto& domain) -> RefPtr<API::Object> {
            return API::String::create(String(domain.string()));
        });
        callback(toAPI(API::Array::create(WTFMove(apiDomains)).ptr()), callbackContext);
    });
}

void WKPageClearLoadedSubresourceDomains(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->clearLoadedSubresourceDomains();
}

void WKPageSetMediaCaptureReportingDelayForTesting(WKPageRef pageRef, double delay)
{
    CRASH_IF_SUSPENDED;
    toProtectedImpl(pageRef)->setMediaCaptureReportingDelay(Seconds(delay));
}

void WKPageDispatchActivityStateUpdateForTesting(WKPageRef pageRef)
{
    CRASH_IF_SUSPENDED;
    if (RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting())
        pageForTesting->dispatchActivityStateUpdate();
}

void WKPageClearNotificationPermissionState(WKPageRef pageRef)
{
#if ENABLE(NOTIFICATIONS)
    toProtectedImpl(pageRef)->clearNotificationPermissionState();
#endif
}

void WKPageExecuteCommandForTesting(WKPageRef pageRef, WKStringRef command, WKStringRef value)
{
    toProtectedImpl(pageRef)->executeEditCommand(toProtectedImpl(command)->string(), toProtectedImpl(value)->string());
}

bool WKPageIsEditingCommandEnabledForTesting(WKPageRef pageRef, WKStringRef command)
{
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return false;

    return pageForTesting->isEditingCommandEnabled(toProtectedImpl(command)->string());
}

void WKPageSetPermissionLevelForTesting(WKPageRef pageRef, WKStringRef origin, bool allowed)
{
    if (RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting())
        pageForTesting->setPermissionLevel(toProtectedImpl(origin)->string(), allowed);
}

void WKPageSetObscuredContentInsetsForTesting(WKPageRef pageRef, float top, float right, float bottom, float left, void* context, WKPageSetObscuredContentInsetsForTestingFunction callback)
{
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return callback(context);

    pageForTesting->setObscuredContentInsets(top, right, bottom, left, [context, callback] {
        callback(context);
    });
}

void WKPageSetPageScaleFactorForTesting(WKPageRef pageRef, float scaleFactor, WKPoint point, void* context, WKPageSetPageScaleFactorForTestingFunction completionHandler)
{
    toProtectedImpl(pageRef)->scalePage(scaleFactor, toIntPoint(point), [context, completionHandler] {
        completionHandler(context);
    });
}

void WKPageClearBackForwardListForTesting(WKPageRef pageRef, void* context, WKPageClearBackForwardListForTestingFunction completionHandler)
{
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return completionHandler(context);

    pageForTesting->clearBackForwardList([context, completionHandler] {
        completionHandler(context);
    });
}

void WKPageSetTracksRepaintsForTesting(WKPageRef pageRef, void* context, bool trackRepaints, WKPageSetTracksRepaintsForTestingFunction completionHandler)
{
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return completionHandler(context);

    pageForTesting->setTracksRepaints(trackRepaints, [context, completionHandler] {
        completionHandler(context);
    });
}

void WKPageDisplayAndTrackRepaintsForTesting(WKPageRef pageRef, void* context, WKPageDisplayAndTrackRepaintsForTestingFunction completionHandler)
{
    RefPtr pageForTesting = toProtectedImpl(pageRef)->pageForTesting();
    if (!pageForTesting)
        return completionHandler(context);

    pageForTesting->displayAndTrackRepaints([context, completionHandler] {
        completionHandler(context);
    });
}

void WKPageFindStringForTesting(WKPageRef pageRef, void* context, WKStringRef string, WKFindOptions options, unsigned maxMatchCount, WKPageFindStringForTestingFunction completionHandler)
{
    toProtectedImpl(pageRef)->findString(toWTFString(string), toFindOptions(options), maxMatchCount, [context, completionHandler] (bool found) {
        completionHandler(found, context);
    });
}
