/*
 * Copyright (C) 2021 Sony Interactive Entertainment Inc.
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
#include "PageClientImpl.h"

#include "DrawingAreaProxyCoordinatedGraphics.h"
#include "PlayStationWebView.h"
#include "WebPageProxy.h"
#include <WebCore/DOMPasteAccess.h>
#include <WebCore/NotImplemented.h>
#include <wtf/TZoneMallocInlines.h>

#if USE(GRAPHICS_LAYER_WC)
#include "DrawingAreaProxyWC.h"
#endif

#if USE(WPE_RENDERER)
#include <wpe/wpe.h>
#endif

namespace WebKit {

WTF_MAKE_TZONE_ALLOCATED_IMPL(PageClientImpl);

PageClientImpl::PageClientImpl(PlayStationWebView& view)
    : m_view(view)
{
}

#if USE(GRAPHICS_LAYER_WC) && USE(WPE_RENDERER)
uint64_t PageClientImpl::viewWidget()
{
    return 0;
}
#endif

// PageClient's pure virtual functions
Ref<DrawingAreaProxy> PageClientImpl::createDrawingAreaProxy(WebProcessProxy& webProcessProxy)
{
#if USE(GRAPHICS_LAYER_WC)
    return DrawingAreaProxyWC::create(*m_view.page(), webProcessProxy);
#else
    return DrawingAreaProxyCoordinatedGraphics::create(*m_view.page(), webProcessProxy);
#endif
}

void PageClientImpl::setViewNeedsDisplay(const WebCore::Region& region)
{
    m_view.setViewNeedsDisplay(region);
}

void PageClientImpl::requestScroll(const WebCore::FloatPoint& scrollPosition, const WebCore::IntPoint& scrollOrigin, WebCore::ScrollIsAnimated)
{
}

WebCore::FloatPoint PageClientImpl::viewScrollPosition()
{
    return WebCore::FloatPoint { };
}

WebCore::IntSize PageClientImpl::viewSize()
{
    return m_view.viewSize();
}

bool PageClientImpl::isViewWindowActive()
{
    return m_view.viewState().contains(WebCore::ActivityState::WindowIsActive);
}

bool PageClientImpl::isViewFocused()
{
    return m_view.viewState().contains(WebCore::ActivityState::IsFocused);
}

bool PageClientImpl::isActiveViewVisible()
{
    return m_view.viewState().contains(WebCore::ActivityState::IsVisible);
}

bool PageClientImpl::isViewInWindow()
{
    return m_view.viewState().contains(WebCore::ActivityState::IsInWindow);
}

void PageClientImpl::processDidExit()
{
}

void PageClientImpl::didRelaunchProcess()
{
}

void PageClientImpl::pageClosed()
{
}

void PageClientImpl::preferencesDidChange()
{
}

void PageClientImpl::toolTipChanged(const String&, const String&)
{
}

void PageClientImpl::didCommitLoadForMainFrame(const String& mimeType, bool useCustomContentProvider)
{
    notImplemented();
}

void PageClientImpl::didChangeContentSize(const WebCore::IntSize& size)
{
    notImplemented();
}

void PageClientImpl::setCursor(const WebCore::Cursor& cursor)
{
    m_view.setCursor(cursor);
}

void PageClientImpl::setCursorHiddenUntilMouseMoves(bool)
{
}

void PageClientImpl::registerEditCommand(Ref<WebEditCommandProxy>&&, UndoOrRedo)
{
}

void PageClientImpl::clearAllEditCommands()
{
}

bool PageClientImpl::canUndoRedo(UndoOrRedo)
{
    return false;
}

void PageClientImpl::executeUndoRedo(UndoOrRedo)
{
}

void PageClientImpl::wheelEventWasNotHandledByWebCore(const NativeWebWheelEvent&)
{
}

WebCore::FloatRect PageClientImpl::convertToDeviceSpace(const WebCore::FloatRect& rect)
{
    return rect;
}

WebCore::FloatRect PageClientImpl::convertToUserSpace(const WebCore::FloatRect& rect)
{
    return rect;
}

WebCore::IntPoint PageClientImpl::screenToRootView(const WebCore::IntPoint& point)
{
    return point;
}

WebCore::IntPoint PageClientImpl::rootViewToScreen(const WebCore::IntPoint& point)
{
    return point;
}

WebCore::IntRect PageClientImpl::rootViewToScreen(const WebCore::IntRect& rect)
{
    return rect;
}

WebCore::IntPoint PageClientImpl::accessibilityScreenToRootView(const WebCore::IntPoint& point)
{
    return screenToRootView(point);
}

WebCore::IntRect PageClientImpl::rootViewToAccessibilityScreen(const WebCore::IntRect& rect)
{
    return rootViewToScreen(rect);    
}

void PageClientImpl::doneWithKeyEvent(const NativeWebKeyboardEvent& event, bool wasEventHandled)
{
    notImplemented();
}

#if ENABLE(TOUCH_EVENTS)
void PageClientImpl::doneWithTouchEvent(const WebTouchEvent& event, bool wasEventHandled)
{
    notImplemented();
}
#endif // ENABLE(TOUCH_EVENTS)

RefPtr<WebPopupMenuProxy> PageClientImpl::createPopupMenuProxy(WebPageProxy&  pageProxy)
{
    notImplemented();
    return { };
}

#if USE(GRAPHICS_LAYER_WC)
bool PageClientImpl::usesOffscreenRendering() const
{
    return false;
}
#endif

void PageClientImpl::enterAcceleratedCompositingMode(const LayerTreeContext& context)
{
    notImplemented();
}

void PageClientImpl::exitAcceleratedCompositingMode()
{
    notImplemented();
}

void PageClientImpl::updateAcceleratedCompositingMode(const LayerTreeContext&)
{
    notImplemented();
}

#if ENABLE(FULLSCREEN_API)
WebFullScreenManagerProxyClient& PageClientImpl::fullScreenManagerProxyClient()
{
    return *(WebFullScreenManagerProxyClient*)this;
}

void PageClientImpl::setFullScreenClientForTesting(std::unique_ptr<WebFullScreenManagerProxyClient>&&)
{
}

void PageClientImpl::closeFullScreenManager()
{
    m_view.closeFullScreenManager();
}

bool PageClientImpl::isFullScreen()
{
    return m_view.isFullScreen();
}

void PageClientImpl::enterFullScreen(WebCore::FloatSize, CompletionHandler<void(bool)>&& completionHandler)
{
    m_view.enterFullScreen(WTFMove(completionHandler));
}

void PageClientImpl::exitFullScreen(CompletionHandler<void()>&& completionHandler)
{
    m_view.exitFullScreen(WTFMove(completionHandler));
}

void PageClientImpl::beganEnterFullScreen(const WebCore::IntRect& initialFrame, const WebCore::IntRect& finalFrame, CompletionHandler<void(bool)>&& completionHandler)
{
    m_view.beganEnterFullScreen(initialFrame, finalFrame, WTFMove(completionHandler));
}

void PageClientImpl::beganExitFullScreen(const WebCore::IntRect& initialFrame, const WebCore::IntRect& finalFrame, CompletionHandler<void()>&& completionHandler)
{
    m_view.beganExitFullScreen(initialFrame, finalFrame, WTFMove(completionHandler));
}
#endif

// Custom representations.
void PageClientImpl::didFinishLoadingDataForCustomContentProvider(const String& suggestedFilename, std::span<const uint8_t>)
{
}

void PageClientImpl::navigationGestureDidBegin()
{
}

void PageClientImpl::navigationGestureWillEnd(bool willNavigate, WebBackForwardListItem&)
{
}

void PageClientImpl::navigationGestureDidEnd(bool willNavigate, WebBackForwardListItem&)
{
}

void PageClientImpl::navigationGestureDidEnd()
{
}

void PageClientImpl::willRecordNavigationSnapshot(WebBackForwardListItem&)
{
}

void PageClientImpl::didRemoveNavigationGestureSnapshot()
{
}

void PageClientImpl::didFirstVisuallyNonEmptyLayoutForMainFrame()
{
}

void PageClientImpl::didFinishNavigation(API::Navigation*)
{
}

void PageClientImpl::didFailNavigation(API::Navigation*)
{
}

void PageClientImpl::didSameDocumentNavigationForMainFrame(SameDocumentNavigationType)
{
}

void PageClientImpl::didChangeBackgroundColor()
{
}

void PageClientImpl::isPlayingAudioWillChange()
{
}

void PageClientImpl::isPlayingAudioDidChange()
{
}

void PageClientImpl::refView()
{
}

void PageClientImpl::derefView()
{
}

void PageClientImpl::didRestoreScrollPosition()
{
}

WebCore::UserInterfaceLayoutDirection PageClientImpl::userInterfaceLayoutDirection()
{
    return WebCore::UserInterfaceLayoutDirection::LTR;
}

void PageClientImpl::requestDOMPasteAccess(WebCore::DOMPasteAccessCategory, WebCore::DOMPasteRequiresInteraction, const WebCore::IntRect&, const String&, CompletionHandler<void(WebCore::DOMPasteAccessResponse)>&& completionHandler)
{
    completionHandler(WebCore::DOMPasteAccessResponse::DeniedForGesture);
}

#if USE(WPE_RENDERER)
UnixFileDescriptor PageClientImpl::hostFileDescriptor()
{
    return UnixFileDescriptor { wpe_view_backend_get_renderer_host_fd(m_view.backend()), UnixFileDescriptor::Adopt };
}
#endif

} // namespace WebKit
