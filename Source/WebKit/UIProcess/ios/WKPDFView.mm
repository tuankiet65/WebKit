/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
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

#import "config.h"
#import "WKPDFView.h"

#if ENABLE(WKPDFVIEW)

#import "APIUIClient.h"
#import "FindClient.h"
#import "PDFKitSPI.h"
#import "PickerDismissalReason.h"
#import "ProcessTerminationReason.h"
#import "UIKitSPI.h"
#import "WKActionSheetAssistant.h"
#import "WKKeyboardScrollingAnimator.h"
#import "WKScrollView.h"
#import "WKUIDelegatePrivate.h"
#import "WKWebEvent.h"
#import "WKWebViewIOS.h"
#import "WebPageProxy.h"
#import "_WKWebViewPrintFormatterInternal.h"
#import <MobileCoreServices/MobileCoreServices.h>
#import <WebCore/DataDetection.h>
#import <WebCore/ShareData.h>
#import <wtf/BlockPtr.h>
#import <wtf/MainThread.h>
#import <wtf/RetainPtr.h>
#import <wtf/StdLibExtras.h>
#import <wtf/WeakObjCPtr.h>
#import <wtf/cocoa/Entitlements.h>
#import <wtf/cocoa/NSURLExtras.h>
#import <wtf/cocoa/TypeCastsCocoa.h>

#if PLATFORM(APPLETV)
#import "PDFKitSoftLink.h"
#define PDFHostViewControllerClass WebKit::getPDFHostViewControllerClass()
#else
#define PDFHostViewControllerClass PDFHostViewController
#endif

#if HAVE(UIFINDINTERACTION)

@interface WKPDFFoundTextRange : UITextRange

@property (nonatomic) NSUInteger index;

+ (WKPDFFoundTextRange *)foundTextRangeWithIndex:(NSUInteger)index;

@end

@interface WKPDFFoundTextPosition : UITextPosition

@property (nonatomic) NSUInteger index;

+ (WKPDFFoundTextPosition *)textPositionWithIndex:(NSUInteger)index;

@end

@implementation WKPDFFoundTextRange

+ (WKPDFFoundTextRange *)foundTextRangeWithIndex:(NSUInteger)index
{
    auto range = adoptNS([[WKPDFFoundTextRange alloc] init]);
    [range setIndex:index];
    return range.autorelease();
}

- (WKPDFFoundTextPosition *)start
{
    WKPDFFoundTextPosition *position = [WKPDFFoundTextPosition textPositionWithIndex:self.index];
    return position;
}

- (UITextPosition *)end
{
    return self.start;
}

- (BOOL)isEmpty
{
    return NO;
}

@end

@implementation WKPDFFoundTextPosition

+ (WKPDFFoundTextPosition *)textPositionWithIndex:(NSUInteger)index
{
    auto position = adoptNS([[WKPDFFoundTextPosition alloc] init]);
    [position setIndex:index];
    return position.autorelease();
}

@end

#endif // HAVE(UIFINDINTERACTION)

#if ENABLE(OVERLAY_REGIONS_IN_EVENT_REGION)
static void* kvoContext = &kvoContext;
#endif

@interface WKPDFView () <PDFHostViewControllerDelegate, WKActionSheetAssistantDelegate
#if HAVE(UIFINDINTERACTION)
    , UITextSearching
#endif
>
@end

@implementation WKPDFView {
    RetainPtr<WKActionSheetAssistant> _actionSheetAssistant;
    RetainPtr<NSData> _data;
    RetainPtr<CGPDFDocumentRef> _documentForPrinting;
    BlockPtr<void()> _findCompletion;
    RetainPtr<NSString> _findString;
    NSUInteger _findStringCount;
    NSUInteger _findStringMaxCount;
    RetainPtr<UIView> _fixedOverlayView;
    std::optional<NSUInteger> _focusedSearchResultIndex;
    NSInteger _focusedSearchResultPendingOffset;
    RetainPtr<PDFHostViewController> _hostViewController;
    CGSize _overlaidAccessoryViewsInset;
    RetainPtr<UIView> _pageNumberIndicator;
    CString _passwordForPrinting;
    WebKit::InteractionInformationAtPosition _positionInformation;
    RetainPtr<NSString> _suggestedFilename;
    WeakObjCPtr<WKWebView> _webView;
    RetainPtr<WKKeyboardScrollViewAnimator> _keyboardScrollingAnimator;
#if HAVE(SHARE_SHEET_UI)
    RetainPtr<WKShareSheet> _shareSheet;
#endif
    BOOL _isShowingPasswordView;
#if HAVE(UIFINDINTERACTION)
    RetainPtr<id<UITextSearchAggregator>> _searchAggregator;
    RetainPtr<NSString> _searchString;
#endif
}

+ (BOOL)platformSupportsPDFView
{
#if PLATFORM(APPLETV)
    return WebKit::isPDFKitFrameworkAvailable();
#else
    return YES;
#endif
}

- (void)dealloc
{
#if HAVE(SHARE_SHEET_UI)
    if (_shareSheet) {
        [_shareSheet dismissIfNeededWithReason:WebKit::PickerDismissalReason::ProcessExited];
        _shareSheet = nil;
    }
#endif
    [_actionSheetAssistant cleanupSheet];
    [[_hostViewController view] removeFromSuperview];
    [_pageNumberIndicator removeFromSuperview];
    [_keyboardScrollingAnimator invalidate];
    secureMemsetSpan(_passwordForPrinting.mutableSpan(), 0);
#if HAVE(UIFINDINTERACTION)
    _searchAggregator = nil;
    _searchString = nil;
#endif

#if ENABLE(OVERLAY_REGIONS_IN_EVENT_REGION)
    [[_webView _wkScrollView] removeObserver:self forKeyPath:@"contentSize" context:kvoContext];
#endif

    [super dealloc];
}

- (BOOL)web_handleKeyEvent:(::UIEvent *)event
{
    auto webEvent = adoptNS([[WKWebEvent alloc] initWithEvent:event]);

    if ([_keyboardScrollingAnimator beginWithEvent:webEvent.get() scrollView:[_webView _scrollViewInternal]])
        return YES;

    [_keyboardScrollingAnimator handleKeyEvent:webEvent.get()];
    return NO;
}

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer
{
    return [_hostViewController gestureRecognizerShouldBegin:gestureRecognizer];
}


#pragma mark WKApplicationStateTrackingView

- (UIView *)_contentView
{
    return _hostViewController ? [_hostViewController view] : self;
}


#pragma mark WKWebViewContentProvider

- (instancetype)web_initWithFrame:(CGRect)frame webView:(WKWebView *)webView mimeType:(NSString *)mimeType
{
    if (!(self = [super initWithFrame:frame webView:webView]))
        return nil;

    _keyboardScrollingAnimator = adoptNS([[WKKeyboardScrollViewAnimator alloc] init]);
    _webView = webView;

#if ENABLE(OVERLAY_REGIONS_IN_EVENT_REGION)
    [[_webView _wkScrollView] addObserver:self forKeyPath:@"contentSize" options:NSKeyValueObservingOptionNew context:kvoContext];
#endif

    [self updateBackgroundColor];

    return self;
}

- (void)updateBackgroundColor
{
    UIColor *backgroundColor = [PDFHostViewControllerClass backgroundColor];

#if PLATFORM(VISION)
    if (_isShowingPasswordView)
        backgroundColor = UIColor.clearColor;
#endif

    self.backgroundColor = backgroundColor;
    [[_webView _wkScrollView] _setBackgroundColorInternal:backgroundColor];
}

- (void)web_setContentProviderData:(NSData *)data suggestedFilename:(NSString *)filename
{
    _data = adoptNS([data copy]);
    _suggestedFilename = adoptNS([filename copy]);

#if HAVE(SETUSEIOSURFACEFORTILES)
    [PDFHostViewControllerClass setUseIOSurfaceForTiles:false];
#endif

    [PDFHostViewControllerClass createHostView:[self, weakSelf = WeakObjCPtr<WKPDFView>(self)](PDFHostViewController *hostViewController) {
        ASSERT(isMainRunLoop());

        WKPDFView *autoreleasedSelf = weakSelf.getAutoreleased();
        if (!autoreleasedSelf)
            return;

        WKWebView *webView = _webView.getAutoreleased();
        if (!webView)
            return;

        if (!hostViewController)
            return;
        _hostViewController = hostViewController;

        UIView *hostView = hostViewController.view;
        hostView.frame = webView.bounds;

        UIScrollView *scrollView = webView.scrollView;
        [self removeFromSuperview];
        [scrollView addSubview:hostView];

        _actionSheetAssistant = adoptNS([[WKActionSheetAssistant alloc] initWithView:hostView]);
        [_actionSheetAssistant setDelegate:self];

        _pageNumberIndicator = hostViewController.pageNumberIndicator;
        [_fixedOverlayView addSubview:_pageNumberIndicator.get()];

        hostViewController.delegate = self;
        [hostViewController setDocumentData:_data.get() withScrollView:scrollView];
    } forExtensionIdentifier:nil];
}

- (CGPoint)_offsetForPageNumberIndicator
{
    WKWebView *webView = _webView.getAutoreleased();
    if (!webView)
        return CGPointZero;

    UIEdgeInsets insets = UIEdgeInsetsAdd(webView._computedUnobscuredSafeAreaInset, webView._computedObscuredInset, UIRectEdgeAll);
    return CGPointMake(insets.left, insets.top + _overlaidAccessoryViewsInset.height);
}

- (void)_movePageNumberIndicatorToPoint:(CGPoint)point animated:(BOOL)animated
{
    void (^setFrame)() = ^{
        static const CGFloat margin = 20;
        const CGRect frame = { CGPointMake(point.x + margin, point.y + margin), [_pageNumberIndicator frame].size };
        [_pageNumberIndicator setFrame:frame];
    };

    if (animated) {
        static const NSTimeInterval duration = 0.3;
        [UIView animateWithDuration:duration animations:setFrame];
        return;
    }

    setFrame();
}

- (void)_updateLayoutAnimated:(BOOL)animated
{
    [_hostViewController updatePDFViewLayout];
    [self _movePageNumberIndicatorToPoint:self._offsetForPageNumberIndicator animated:animated];
}

- (void)web_setMinimumSize:(CGSize)size
{
    self.frame = { self.frame.origin, size };
    [self _updateLayoutAnimated:NO];
}

- (void)web_setOverlaidAccessoryViewsInset:(CGSize)inset
{
    _overlaidAccessoryViewsInset = inset;
    [self _updateLayoutAnimated:YES];
}

- (void)web_computedContentInsetDidChange
{
    [self _updateLayoutAnimated:NO];
}

- (void)web_setFixedOverlayView:(UIView *)fixedOverlayView
{
    _fixedOverlayView = fixedOverlayView;
}

- (void)_scrollToURLFragment:(NSString *)fragment
{
    NSInteger pageIndex = 0;
    if ([fragment hasPrefix:@"page"])
        pageIndex = [[fragment substringFromIndex:4] integerValue] - 1;

    if (pageIndex >= 0 && pageIndex < [_hostViewController pageCount] && pageIndex != [_hostViewController currentPageIndex])
        [_hostViewController goToPageIndex:pageIndex];
}

- (void)web_didSameDocumentNavigation:(WKSameDocumentNavigationType)navigationType
{
    if (navigationType == kWKSameDocumentNavigationSessionStatePop)
        [self _scrollToURLFragment:[_webView URL].fragment];
}

static NSStringCompareOptions stringCompareOptions(_WKFindOptions findOptions)
{
    NSStringCompareOptions compareOptions = 0;
    if (findOptions & _WKFindOptionsBackwards)
        compareOptions |= NSBackwardsSearch;
    if (findOptions & _WKFindOptionsCaseInsensitive)
        compareOptions |= NSCaseInsensitiveSearch;
    return compareOptions;
}

- (void)_resetFind
{
    if (_findCompletion)
        [_hostViewController cancelFindString];

    _findCompletion = nil;
    _findString = nil;
    _findStringCount = 0;
    _findStringMaxCount = 0;
    _focusedSearchResultIndex = std::nullopt;
    _focusedSearchResultPendingOffset = 0;
}

- (void)_findString:(NSString *)string withOptions:(_WKFindOptions)options maxCount:(NSUInteger)maxCount completion:(void(^)())completion
{
    [self _resetFind];

    _findCompletion = completion;
    _findString = adoptNS([string copy]);
    _findStringMaxCount = maxCount;
    [_hostViewController findString:_findString.get() withOptions:stringCompareOptions(options)];
}

- (void)web_countStringMatches:(NSString *)string options:(_WKFindOptions)options maxCount:(NSUInteger)maxCount
{
    [self _findString:string withOptions:options maxCount:maxCount completion:^{
        ASSERT([_findString isEqualToString:string]);
        if (auto page = [_webView _page])
            page->findClient().didCountStringMatches(page, _findString.get(), _findStringCount);
    }];
}

- (BOOL)_computeFocusedSearchResultIndexWithOptions:(_WKFindOptions)options didWrapAround:(BOOL *)didWrapAround
{
    BOOL isBackwards = options & _WKFindOptionsBackwards;
    NSInteger singleOffset = isBackwards ? -1 : 1;

    if (_findCompletion) {
        ASSERT(!_focusedSearchResultIndex);
        _focusedSearchResultPendingOffset += singleOffset;
        return NO;
    }

    if (!_findStringCount)
        return NO;

    NSInteger newIndex;
    if (_focusedSearchResultIndex) {
        ASSERT(!_focusedSearchResultPendingOffset);
        newIndex = *_focusedSearchResultIndex + singleOffset;
    } else {
        newIndex = isBackwards ? _findStringCount - 1 : 0;
        newIndex += std::exchange(_focusedSearchResultPendingOffset, 0);
    }

    if (newIndex < 0 || static_cast<NSUInteger>(newIndex) >= _findStringCount) {
        if (!(options & _WKFindOptionsWrapAround))
            return NO;

        NSUInteger wrappedIndex = std::abs(newIndex) % _findStringCount;
        if (newIndex < 0)
            wrappedIndex = _findStringCount - wrappedIndex;
        newIndex = wrappedIndex;
        *didWrapAround = YES;
    }

    _focusedSearchResultIndex = newIndex;
    ASSERT(*_focusedSearchResultIndex < _findStringCount);
    return YES;
}

- (void)_focusOnSearchResultWithOptions:(_WKFindOptions)options
{
    auto page = [_webView _page];
    if (!page)
        return;

    BOOL didWrapAround = NO;
    if (![self _computeFocusedSearchResultIndexWithOptions:options didWrapAround:&didWrapAround]) {
        if (!_findCompletion)
            page->findClient().didFailToFindString(page, _findString.get());
        return;
    }

    auto focusedIndex = *_focusedSearchResultIndex;
    [_hostViewController focusOnSearchResultAtIndex:focusedIndex];
    page->findClient().didFindString(page, _findString.get(), { }, _findStringCount, focusedIndex, didWrapAround);
}

- (void)web_findString:(NSString *)string options:(_WKFindOptions)options maxCount:(NSUInteger)maxCount
{
    if ([_findString isEqualToString:string]) {
        [self _focusOnSearchResultWithOptions:options];
        return;
    }

    [self _findString:string withOptions:options maxCount:maxCount completion:^{
        ASSERT([_findString isEqualToString:string]);
        [self _focusOnSearchResultWithOptions:options];
    }];
}

- (void)web_hideFindUI
{
    [self _resetFind];
}

- (UIView *)web_contentView
{
    return self._contentView;
}

+ (BOOL)web_requiresCustomSnapshotting
{
    static bool hasGlobalCaptureEntitlement = WTF::processHasEntitlement("com.apple.QuartzCore.global-capture"_s);
    return !hasGlobalCaptureEntitlement;
}

- (void)web_scrollViewDidScroll:(UIScrollView *)scrollView
{
    [_hostViewController updatePDFViewLayout];
}

- (void)web_scrollViewWillBeginZooming:(UIScrollView *)scrollView withView:(UIView *)view
{
    [_hostViewController updatePDFViewLayout];
}

- (void)web_scrollViewDidEndZooming:(UIScrollView *)scrollView withView:(UIView *)view atScale:(CGFloat)scale
{
    [_hostViewController updatePDFViewLayout];
}

- (void)web_scrollViewDidZoom:(UIScrollView *)scrollView
{
    [_hostViewController updatePDFViewLayout];
}

- (void)web_beginAnimatedResizeWithUpdates:(void (^)(void))updateBlock
{
    [_hostViewController beginPDFViewRotation];
    updateBlock();
    [_hostViewController endPDFViewRotation];
}

- (void)web_snapshotRectInContentViewCoordinates:(CGRect)rectInContentViewCoordinates snapshotWidth:(CGFloat)snapshotWidth completionHandler:(void (^)(CGImageRef))completionHandler
{
    CGRect rectInHostViewCoordinates = [self._contentView convertRect:rectInContentViewCoordinates toView:[_hostViewController view]];
    [_hostViewController snapshotViewRect:rectInHostViewCoordinates snapshotWidth:@(snapshotWidth) afterScreenUpdates:NO withResult:^(UIImage *image) {
        completionHandler(image.CGImage);
    }];
}

- (NSData *)web_dataRepresentation
{
    return _data.get();
}

- (NSString *)web_suggestedFilename
{
    return _suggestedFilename.get();
}

- (BOOL)web_isBackground
{
    return self.isBackground;
}

#pragma mark KVO

#if ENABLE(OVERLAY_REGIONS_IN_EVENT_REGION)
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary<NSString *, id> *)change context:(void*)context
{
    if (context != kvoContext) {
        [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
        return;
    }

    ASSERT(object == [_webView _wkScrollView]);

    [_webView _updateOverlayRegionsForCustomContentView];
}
#endif

#pragma mark PDFHostViewControllerDelegate

- (void)pdfHostViewController:(PDFHostViewController *)controller updatePageCount:(NSInteger)pageCount
{
    [self _scrollToURLFragment:[_webView URL].fragment];
}

- (void)pdfHostViewControllerDocumentDidRequestPassword:(PDFHostViewController *)controller
{
    [_webView _didRequestPasswordForDocument];

    _isShowingPasswordView = YES;
    [self updateBackgroundColor];
}

- (void)pdfHostViewController:(PDFHostViewController *)controller documentDidUnlockWithPassword:(NSString *)password
{
    _passwordForPrinting = [password UTF8String];
    [_webView _didStopRequestingPasswordForDocument];

    _isShowingPasswordView = NO;
    [self updateBackgroundColor];
}

- (void)pdfHostViewController:(PDFHostViewController *)controller findStringUpdate:(NSUInteger)numFound done:(BOOL)done
{
#if HAVE(UIFINDINTERACTION)
    if (_searchAggregator) {
        if (!done)
            return;

        for (NSUInteger index = 0; index < numFound; index++) {
            WKPDFFoundTextRange *range = [WKPDFFoundTextRange foundTextRangeWithIndex:index];
            [_searchAggregator foundRange:range forSearchString:_searchString.get() inDocument:nil];
        }

        [_searchAggregator finishedSearching];

        _searchAggregator = nil;
        _searchString = nil;
        return;
    }
#endif

    if (numFound > _findStringMaxCount && !done) {
        [controller cancelFindStringWithHighlightsCleared:NO];
        done = YES;
    }
    
    if (!done)
        return;
    
    if (auto findCompletion = std::exchange(_findCompletion, nil)) {
        _findStringCount = numFound;
        findCompletion();
    }
}

- (NSURL *)_URLWithPageIndex:(NSInteger)pageIndex
{
    return [NSURL URLWithString:adoptNS([[NSString alloc] initWithFormat:@"#page%ld", (long)pageIndex + 1]).get() relativeToURL:[_webView URL]];
}

- (void)_goToURL:(NSURL *)url atLocation:(CGPoint)location
{
    auto page = [_webView _page];
    if (!page)
        return;

    UIView *hostView = [_hostViewController view];
    CGPoint locationInScreen = [hostView.window convertPoint:[hostView convertPoint:location toView:nil] toWindow:nil];
    page->navigateToPDFLinkWithSimulatedClick(url.absoluteString, WebCore::roundedIntPoint(location), WebCore::roundedIntPoint(locationInScreen));
}

- (void)pdfHostViewController:(PDFHostViewController *)controller goToURL:(NSURL *)url
{
    // FIXME: We'd use the real tap location if we knew it.
    [self _goToURL:url atLocation:CGPointMake(0, 0)];
}

- (void)pdfHostViewController:(PDFHostViewController *)controller goToPageIndex:(NSInteger)pageIndex withViewFrustum:(CGRect)documentViewRect
{
    [self _goToURL:[self _URLWithPageIndex:pageIndex] atLocation:documentViewRect.origin];
}

- (void)_showActionSheetForURL:(NSURL *)url atLocation:(CGPoint)location withAnnotationRect:(CGRect)annotationRect
{
    WKWebView *webView = _webView.getAutoreleased();
    if (!webView)
        return;

    WebKit::InteractionInformationAtPosition positionInformation;
    positionInformation.bounds = WebCore::roundedIntRect(annotationRect);
    positionInformation.request.point = WebCore::roundedIntPoint(location);
    positionInformation.url = url;

    _positionInformation = WTFMove(positionInformation);

#if ENABLE(DATA_DETECTION)
    if (WebCore::DataDetection::canBePresentedByDataDetectors(_positionInformation.url)) {
        [_actionSheetAssistant showDataDetectorsUIForPositionInformation:positionInformation];
        return;
    }
#endif

    [_actionSheetAssistant showLinkSheet];
}

- (void)pdfHostViewController:(PDFHostViewController *)controller didLongPressURL:(NSURL *)url atLocation:(CGPoint)location withAnnotationRect:(CGRect)annotationRect
{
    [self _showActionSheetForURL:url atLocation:location withAnnotationRect:annotationRect];
}

- (void)pdfHostViewController:(PDFHostViewController *)controller didLongPressPageIndex:(NSInteger)pageIndex atLocation:(CGPoint)location withAnnotationRect:(CGRect)annotationRect
{
    [self _showActionSheetForURL:[self _URLWithPageIndex:pageIndex] atLocation:location withAnnotationRect:annotationRect];
}

- (void)pdfHostViewControllerExtensionProcessDidCrash:(PDFHostViewController *)controller
{
    // FIXME 40916725: PDFKit should dispatch this message to the main thread like it does for other delegate messages.
    RunLoop::mainSingleton().dispatch([webView = _webView] {
        if (auto page = [webView _page])
            page->dispatchProcessDidTerminate(page->legacyMainFrameProcess(), WebKit::ProcessTerminationReason::Crash);
    });
}


#pragma mark WKActionSheetAssistantDelegate

- (std::optional<WebKit::InteractionInformationAtPosition>)positionInformationForActionSheetAssistant:(WKActionSheetAssistant *)assistant
{
    return _positionInformation;
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant performAction:(WebKit::SheetAction)action
{
    if (action != WebKit::SheetAction::Copy)
        return;

ALLOW_DEPRECATED_DECLARATIONS_BEGIN
    NSDictionary *representations = @{
        bridge_cast(kUTTypeUTF8PlainText) : _positionInformation.url.string().createNSString().get(),
        bridge_cast(kUTTypeURL) : _positionInformation.url.createNSURL().get(),
    };
ALLOW_DEPRECATED_DECLARATIONS_END

    [UIPasteboard _performAsDataOwner:[_webView _effectiveDataOwner:self._dataOwnerForCopy] block:^{
        UIPasteboard.generalPasteboard.items = @[ representations ];
    }];
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant openElementAtLocation:(CGPoint)location
{
    [self _goToURL:_positionInformation.url.createNSURL().get() atLocation:location];
}

- (void)actionSheetAssistant:(WKActionSheetAssistant *)assistant shareElementWithURL:(NSURL *)url rect:(CGRect)boundingRect
{
    WKWebView *webView = _webView.getAutoreleased();
    if (!webView)
        return;

    WebCore::ShareDataWithParsedURL shareData;
    shareData.url = { url };
    shareData.originator = WebCore::ShareDataOriginator::User;

#if HAVE(SHARE_SHEET_UI)
    [_shareSheet dismissIfNeededWithReason:WebKit::PickerDismissalReason::ResetState];

    _shareSheet = adoptNS([[WKShareSheet alloc] initWithView:webView]);
    [_shareSheet setDelegate:self];
    [_shareSheet presentWithParameters:shareData inRect: { [[_hostViewController view] convertRect:boundingRect toView:webView] } completionHandler:[] (bool success) { }];
#endif
}

#if HAVE(SHARE_SHEET_UI)
- (void)shareSheetDidDismiss:(WKShareSheet *)shareSheet
{
    ASSERT(_shareSheet == shareSheet);
    
    [_shareSheet setDelegate:nil];
    _shareSheet = nil;
}
#endif

#if HAVE(APP_LINKS)
- (BOOL)actionSheetAssistant:(WKActionSheetAssistant *)assistant shouldIncludeAppLinkActionsForElement:(_WKActivatedElementInfo *)element
{
    auto page = [_webView _page];
    if (!page)
        return NO;

    return page->uiClient().shouldIncludeAppLinkActionsForElement(element);
}
#endif

- (RetainPtr<NSArray>)actionSheetAssistant:(WKActionSheetAssistant *)assistant decideActionsForElement:(_WKActivatedElementInfo *)element defaultActions:(RetainPtr<NSArray>)defaultActions
{
    auto page = [_webView _page];
    if (!page)
        return nil;

    return page->uiClient().actionsForElement(element, WTFMove(defaultActions));
}

- (NSDictionary *)dataDetectionContextForActionSheetAssistant:(WKActionSheetAssistant *)assistant positionInformation:(const WebKit::InteractionInformationAtPosition&)positionInformation
{
    auto webView = _webView.getAutoreleased();
    if (!webView)
        return nil;

    id <WKUIDelegatePrivate> uiDelegate = static_cast<id <WKUIDelegatePrivate>>(webView.UIDelegate);
    if (![uiDelegate respondsToSelector:@selector(_dataDetectionContextForWebView:)])
        return nil;

    return [uiDelegate _dataDetectionContextForWebView:webView];
}

#pragma mark UITextSearching

#if HAVE(UIFINDINTERACTION)

- (UITextRange *)selectedTextRange
{
    return nil;
}

- (BOOL)supportsTextReplacement
{
    return NO;
}

- (void)scrollRangeToVisible:(UITextRange *)range inDocument:(UITextSearchDocumentIdentifier)document
{
    // Intentionally empty. PDFHostViewController has a single method for decoration and scrolling, so scrolling is performed in `decorateFoundTextRange`.
}

- (NSComparisonResult)compareFoundRange:(UITextRange *)fromRange toRange:(UITextRange *)toRange inDocument:(UITextSearchDocumentIdentifier)document
{
    auto from = dynamic_objc_cast<WKPDFFoundTextPosition>(fromRange.start);
    if (!from)
        return NSOrderedSame;

    auto to = dynamic_objc_cast<WKPDFFoundTextPosition>(toRange.start);
    if (!to)
        return NSOrderedSame;

    if (from.index < to.index)
        return NSOrderedAscending;

    if (from.index > to.index)
        return NSOrderedDescending;

    return NSOrderedSame;
}

- (void)performTextSearchWithQueryString:(NSString *)string usingOptions:(UITextSearchOptions *)options resultAggregator:(id<UITextSearchAggregator>)aggregator
{
    [_hostViewController cancelFindString];
    _searchAggregator = aggregator;
    _searchString = string;
    [_hostViewController findString:string withOptions:options.stringCompareOptions];
}

- (void)decorateFoundTextRange:(UITextRange *)range inDocument:(UITextSearchDocumentIdentifier)document usingStyle:(UITextSearchFoundTextStyle)style
{
    if (style != UITextSearchFoundTextStyleHighlighted)
        return;

    auto foundTextRange = dynamic_objc_cast<WKPDFFoundTextRange>(range);
    if (!foundTextRange)
        return;

    [_hostViewController focusOnSearchResultAtIndex:foundTextRange.index];
}

- (void)clearAllDecoratedFoundText
{
    [_hostViewController cancelFindString];
    _searchAggregator = nil;
}

#endif // HAVE(UIFINDINTERACTION)

@end


#pragma mark _WKWebViewPrintProvider

@interface WKPDFView (_WKWebViewPrintFormatter) <_WKWebViewPrintProvider>
@end

@implementation WKPDFView (_WKWebViewPrintFormatter)

- (CGPDFDocumentRef)_ensureDocumentForPrinting
{
    if (_documentForPrinting)
        return _documentForPrinting.get();

    auto dataProvider = adoptCF(CGDataProviderCreateWithCFData((CFDataRef)_data.get()));
    auto pdfDocument = adoptCF(CGPDFDocumentCreateWithProvider(dataProvider.get()));
    if (!CGPDFDocumentIsUnlocked(pdfDocument.get())) {
        if (!CGPDFDocumentUnlockWithPassword(pdfDocument.get(), _passwordForPrinting.data()))
            return nullptr;
    }

    if (!CGPDFDocumentAllowsPrinting(pdfDocument.get()))
        return nullptr;

    _documentForPrinting = WTFMove(pdfDocument);
    return _documentForPrinting.get();
}

- (BOOL)_wk_printFormatterRequiresMainThread
{
    return YES;
}

- (NSUInteger)_wk_pageCountForPrintFormatter:(_WKWebViewPrintFormatter *)printFormatter
{
    CGPDFDocumentRef documentForPrinting = [self _ensureDocumentForPrinting];
    if (!documentForPrinting)
        return 0;

    size_t pageCount = CGPDFDocumentGetNumberOfPages(documentForPrinting);
    if (printFormatter.snapshotFirstPage)
        return std::min<NSUInteger>(pageCount, 1);
    return pageCount;
}

- (void)_wk_requestDocumentForPrintFormatter:(_WKWebViewPrintFormatter *)printFormatter
{
    [printFormatter _setPrintedDocument:[self _ensureDocumentForPrinting]];
}

@end

#endif // ENABLE(WKPDFVIEW)
