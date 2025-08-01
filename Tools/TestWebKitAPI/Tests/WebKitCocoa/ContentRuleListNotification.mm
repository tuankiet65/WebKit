/*
 * Copyright (C) 2017 Apple Inc. All rights reserved.
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

#import "HTTPServer.h"
#import "PlatformUtilities.h"
#import "Test.h"
#import "TestNavigationDelegate.h"
#import "TestUIDelegate.h"
#import "TestURLSchemeHandler.h"
#import "TestWKWebView.h"
#import <WebKit/WKContentRuleListPrivate.h>
#import <WebKit/WKContentRuleListStorePrivate.h>
#import <WebKit/WKNavigationDelegatePrivate.h>
#import <WebKit/WKURLSchemeHandler.h>
#import <WebKit/WKUserContentController.h>
#import <WebKit/WKWebView.h>
#import <WebKit/WKWebpagePreferencesPrivate.h>
#import <WebKit/WKWebsiteDataStorePrivate.h>
#import <WebKit/_WKContentRuleListAction.h>
#import <WebKit/_WKWebsiteDataStoreConfiguration.h>
#import <wtf/RetainPtr.h>
#import <wtf/URL.h>
#import <wtf/cocoa/VectorCocoa.h>
#import <wtf/text/WTFString.h>

static bool receivedNotification;
static bool receivedAlert;

struct Notification {
    String identifier;
    String url;
    bool blockedLoad { false };
    bool blockedCookies { false };
    bool madeHTTPS { false };
    Vector<String> notifications;
    
    bool operator==(const Notification& other) const
    {
        return identifier == other.identifier
            && url == other.url
            && blockedLoad == other.blockedLoad
            && blockedCookies == other.blockedCookies
            && madeHTTPS == other.madeHTTPS
            && notifications == other.notifications;
    }
};

static Vector<Notification> notificationList;
static RetainPtr<NSURL> notificationURL;
static RetainPtr<NSString> notificationIdentifier;

@interface ContentRuleListNotificationDelegate : NSObject <WKNavigationDelegatePrivate, WKURLSchemeHandler, WKUIDelegate>
@end

@implementation ContentRuleListNotificationDelegate

- (void)_webView:(WKWebView *)webView URL:(NSURL *)url contentRuleListIdentifiers:(NSArray<NSString *> *)identifiers notifications:(NSArray<NSString *> *)notifications
{
    notificationURL = url;
    EXPECT_EQ(identifiers.count, 1u);
    notificationIdentifier = [identifiers objectAtIndex:0];
    EXPECT_EQ(notifications.count, 1u);
    EXPECT_STREQ([notifications objectAtIndex:0].UTF8String, "testnotification");
    receivedNotification = true;
}

- (void)_webView:(WKWebView *)webView contentRuleListWithIdentifier:(NSString *)identifier performedAction:(_WKContentRuleListAction *)action forURL:(NSURL *)url
{
    notificationList.append({ identifier, url.absoluteString, !!action.blockedLoad, !!action.blockedCookies, !!action.madeHTTPS, makeVector<String>(action.notifications) });
}

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id <WKURLSchemeTask>)urlSchemeTask
{
    [urlSchemeTask didReceiveResponse:adoptNS([[NSURLResponse alloc] initWithURL:urlSchemeTask.request.URL MIMEType:@"text/html" expectedContentLength:0 textEncodingName:nil]).get()];
    [urlSchemeTask didFinish];
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id <WKURLSchemeTask>)urlSchemeTask
{
}

- (void)webView:(WKWebView *)webView runJavaScriptAlertPanelWithMessage:(NSString *)message initiatedByFrame:(WKFrameInfo *)frame completionHandler:(void (^)(void))completionHandler
{
    receivedAlert = true;
    completionHandler();
}

@end

static NSString *notificationSource = @"[{\"action\":{\"type\":\"notify\",\"notification\":\"testnotification\"},\"trigger\":{\"url-filter\":\"match\"}}]";

static RetainPtr<WKContentRuleList> makeContentRuleList(NSString *source, NSString *identifier = @"testidentifier")
{
    __block bool doneCompiling = false;
    __block RetainPtr<WKContentRuleList> contentRuleList;
    [[WKContentRuleListStore defaultStore] compileContentRuleListForIdentifier:identifier encodedContentRuleList:source completionHandler:^(WKContentRuleList *list, NSError *error) {
        EXPECT_TRUE(list);
        contentRuleList = list;
        doneCompiling = true;
    }];
    TestWebKitAPI::Util::run(&doneCompiling);
    return contentRuleList;
}

TEST(ContentRuleList, NotificationMainResource)
{
    auto delegate = adoptNS([[ContentRuleListNotificationDelegate alloc] init]);
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addContentRuleList:makeContentRuleList(notificationSource).get()];
    [configuration setURLSchemeHandler:delegate.get() forURLScheme:@"apitest"];
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    [webView setNavigationDelegate:delegate.get()];
    [webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"apitest:///match"]]];
    TestWebKitAPI::Util::run(&receivedNotification);
    EXPECT_STREQ([notificationURL absoluteString].UTF8String, "apitest:///match");
    EXPECT_STREQ([notificationIdentifier UTF8String], "testidentifier");
}

TEST(ContentRuleList, NotificationSubresource)
{
    auto delegate = adoptNS([[ContentRuleListNotificationDelegate alloc] init]);
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addContentRuleList:makeContentRuleList(notificationSource).get()];
    [configuration setURLSchemeHandler:delegate.get() forURLScheme:@"apitest"];
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    [webView setNavigationDelegate:delegate.get()];
    [webView setUIDelegate:delegate.get()];
    [webView loadHTMLString:@"<script>fetch('match').then(function(response){alert('fetch complete')})</script>" baseURL:[NSURL URLWithString:@"apitest:///"]];
    TestWebKitAPI::Util::run(&receivedAlert);
    EXPECT_TRUE(receivedNotification);
    EXPECT_STREQ([notificationURL absoluteString].UTF8String, "apitest:///match");
    EXPECT_STREQ([notificationIdentifier UTF8String], "testidentifier");
}

TEST(ContentRuleList, LoadHTMLStringDisplayNone)
{
    NSString *html = @"<a href='https://www.apple.com/'>link to Apple</a>";

    NSString *getLinkDisplay = @"window.getComputedStyle(document.querySelector('a')).getPropertyValue('display')";

    auto list = makeContentRuleList(@"["
        "{ \"action\": { \"type\" : \"css-display-none\", \"selector\": \"a[href*='apple.com']\" }, \"trigger\": { \"url-filter\": \".*\" }},"
        "{ \"action\": { \"type\" : \"block\" }, \"trigger\": { \"url-filter\": \"webkit.org\" }},"
        "{ \"action\": { \"type\" : \"ignore-previous-rules\" }, \"trigger\": { \"url-filter\": \"example.com\" }}"
    "]");

    auto configuration = adoptNS([WKWebViewConfiguration new]);
    [[configuration userContentController] addContentRuleList:list.get()];
    auto webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    [webView synchronouslyLoadHTMLString:html];
    EXPECT_WK_STREQ([webView objectByEvaluatingJavaScript:getLinkDisplay], "none");

    [webView synchronouslyLoadHTMLString:html baseURL:[NSURL URLWithString:@"https://webkit.org/"]];
    EXPECT_WK_STREQ([webView objectByEvaluatingJavaScript:getLinkDisplay], "none");

    [webView synchronouslyLoadHTMLString:html baseURL:[NSURL URLWithString:@"https://example.com/"]];
    EXPECT_WK_STREQ([webView objectByEvaluatingJavaScript:getLinkDisplay], "inline");

    auto list2 = makeContentRuleList(@"["
        "{ \"action\": { \"type\" : \"css-display-none\", \"selector\": \"a[href*='apple.com']\" }, \"trigger\": { \"url-filter\": \"webkit.org\" }}"
    "]", @"other extension");
    auto configuration2 = adoptNS([WKWebViewConfiguration new]);
    [[configuration2 userContentController] addContentRuleList:list2.get()];
    auto webView2 = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration2.get()]);

    [webView2 synchronouslyLoadHTMLString:html baseURL:[NSURL URLWithString:@"https://webkit.org/"]];
    EXPECT_WK_STREQ([webView2 objectByEvaluatingJavaScript:getLinkDisplay], "none");

    [webView2 synchronouslyLoadHTMLString:html baseURL:[NSURL URLWithString:@"https://example.com/"]];
    EXPECT_WK_STREQ([webView2 objectByEvaluatingJavaScript:getLinkDisplay], "inline");
}

TEST(ContentRuleList, DisplayNoneInSrcDocIFrame)
{
    NSString *html = @"<head></head><body></body>";

    auto list = makeContentRuleList(@"["
        "{ \"action\": { \"type\" : \"css-display-none\", \"selector\": \".header\" }, \"trigger\": { \"url-filter\": \".*\" }}"
    "]");

    auto configuration = adoptNS([WKWebViewConfiguration new]);
    [[configuration userContentController] addContentRuleList:list.get()];
    auto webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    [webView synchronouslyLoadHTMLString:html];

    [webView objectByEvaluatingJavaScript:@"var frame = document.createElement('iframe');"
        "frame.id = 'subframe'; frame.srcdoc = `<!DOCTYPE html><h1 class='header'>test</h1>`; document.body.appendChild(frame); true"];

    __block bool isDone = false;

    // Make sure the frame loads before checking the computed style.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.05 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        NSString *getHeaderDisplay = @"window.getComputedStyle(document.getElementById('subframe').contentDocument.querySelector('h1')).getPropertyValue('display')";
        EXPECT_WK_STREQ([webView objectByEvaluatingJavaScript:getHeaderDisplay], "none");

        isDone = true;
    });

    TestWebKitAPI::Util::run(&isDone);
}

TEST(ContentRuleList, DisplayNoneInAboutBlankIFrame)
{
    NSString *html = @"<head></head><body></body>";

    auto list = makeContentRuleList(@"["
        "{ \"action\": { \"type\" : \"css-display-none\", \"selector\": \"h1\" }, \"trigger\": { \"url-filter\": \".*\" }}"
    "]");

    auto configuration = adoptNS([WKWebViewConfiguration new]);
    [[configuration userContentController] addContentRuleList:list.get()];
    auto webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    [webView synchronouslyLoadHTMLString:html];

    __block bool isDone = false;

    NSString *createFrameScript = @"var subframe = document.createElement('iframe'); subframe.src = 'about:blank'; subframe.id = 'subframe'; document.body.appendChild(subframe);";
    NSString *createHeaderScript = @"document.getElementById('subframe').contentDocument.body.innerHTML = '<h1>test</h1>';";

    [webView evaluateJavaScript:createFrameScript completionHandler:^(id frameResult, NSError *frameError) {
        [webView evaluateJavaScript:createHeaderScript completionHandler:^(id headerResult, NSError *headerError) {
            NSString *getHeaderDisplay = @"window.getComputedStyle(document.getElementById('subframe').contentDocument.querySelector('h1')).getPropertyValue('display')";
            [webView evaluateJavaScript:getHeaderDisplay completionHandler:^(id displayResult, NSError *displayError) {
                EXPECT_WK_STREQ(displayResult, @"none");
                isDone = true;
            }];
        }];
    }];

    TestWebKitAPI::Util::run(&isDone);
}

TEST(ContentRuleList, DisplayNoneAfterIgnoreFollowingRules)
{
    NSString *html = @"<h1 id='A'></h1><h1 id='B'></h1>";

    NSString *headerADisplay = @"window.getComputedStyle(document.querySelector('h1[id*=\"A\"]')).getPropertyValue('display')";
    NSString *headerBDisplay = @"window.getComputedStyle(document.querySelector('h1[id*=\"B\"]')).getPropertyValue('display')";

    auto list = makeContentRuleList(@"["
        "{ \"action\": { \"type\" : \"css-display-none\", \"selector\": \"h1[id*='A']\" }, \"trigger\": { \"url-filter\": \".*\" }},"
        "{ \"action\": { \"type\" : \"ignore-following-rules\" }, \"trigger\": { \"url-filter\": \"webkit.org\" }},"
        "{ \"action\": { \"type\" : \"css-display-none\", \"selector\": \"h1[id*='B']\" }, \"trigger\": { \"url-filter\": \".*\" }}"
    "]");

    auto configuration = adoptNS([WKWebViewConfiguration new]);
    [[configuration userContentController] addContentRuleList:list.get()];
    auto webView = adoptNS([[TestWKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);

    [webView synchronouslyLoadHTMLString:html];
    EXPECT_WK_STREQ([webView objectByEvaluatingJavaScript:headerADisplay], "none");
    EXPECT_WK_STREQ([webView objectByEvaluatingJavaScript:headerBDisplay], "none");

    [webView synchronouslyLoadHTMLString:html baseURL:[NSURL URLWithString:@"https://webkit.org/"]];
    EXPECT_WK_STREQ([webView objectByEvaluatingJavaScript:headerADisplay], "none");
    EXPECT_WK_STREQ([webView objectByEvaluatingJavaScript:headerBDisplay], "block");
}

TEST(ContentRuleList, PerformedActionForURL)
{
    NSString *firstList = @"[{\"action\":{\"type\":\"notify\",\"notification\":\"testnotification\"},\"trigger\":{\"url-filter\":\"notify\"}}]";
    NSString *secondList = @"[{\"action\":{\"type\":\"block\"},\"trigger\":{\"url-filter\":\"block\"}}]";
    auto delegate = adoptNS([[ContentRuleListNotificationDelegate alloc] init]);
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addContentRuleList:makeContentRuleList(firstList, @"firstList").get()];
    [[configuration userContentController] addContentRuleList:makeContentRuleList(secondList, @"secondList").get()];
    [configuration setURLSchemeHandler:delegate.get() forURLScheme:@"apitest"];
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    [webView setNavigationDelegate:delegate.get()];
    [webView setUIDelegate:delegate.get()];
    [webView loadHTMLString:@"<script>fetch('notify').then(function(){fetch('block').then().catch(function(){alert('test complete')})})</script>" baseURL:[NSURL URLWithString:@"apitest:///"]];
    TestWebKitAPI::Util::run(&receivedAlert);
    while (notificationList.size() < 2)
        TestWebKitAPI::Util::spinRunLoop();

    Vector<Notification> expectedNotifications {
        { "firstList"_s, "apitest:///notify"_s, false, false, false, { "testnotification"_s } },
        { "secondList"_s, "apitest:///block"_s, true, false, false, { } }
    };
    EXPECT_TRUE(expectedNotifications == notificationList);
}

TEST(ContentRuleList, ResourceTypes)
{
    using namespace TestWebKitAPI;
    HTTPServer webSocketServer([](Connection connection) {
        connection.webSocketHandshake();
    });
    auto serverPort = webSocketServer.port();

    auto handler = [[TestURLSchemeHandler new] autorelease];
    handler.startURLSchemeTaskHandler = ^(WKWebView *, id<WKURLSchemeTask> task) {
        NSString *path = task.request.URL.path;
        if ([path isEqualToString:@"/checkWebSocket.html"])
            return respond(task, [NSString stringWithFormat:@"<script>var ws = new WebSocket('ws://localhost:%d/test');ws.onopen=()=>{alert('onopen')};ws.onerror=()=>{alert('onerror')}</script>", serverPort].UTF8String);
        if ([path isEqualToString:@"/checkFetch.html"])
            return respond(task, "<script>fetch('test:///fetchContent').then(()=>{alert('fetched')}).catch(()=>{alert('did not fetch')})</script>");
        if ([path isEqualToString:@"/fetchContent"])
            return respond(task, "hello");
        if ([path isEqualToString:@"/checkXHR.html"])
            return respond(task, "<script>var xhr = new XMLHttpRequest();xhr.open('GET', 'test:///fetchContent');xhr.onreadystatechange=()=>{if(xhr.readyState==4){setTimeout(()=>{alert('xhr finished')}, 0)}};xhr.onerror=()=>{alert('xhr error')};xhr.send()</script>");

        ASSERT_NOT_REACHED();
    };
    auto configuration = [[WKWebViewConfiguration new] autorelease];
    [configuration setURLSchemeHandler:handler forURLScheme:@"test"];
    configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    auto webView = [[[WKWebView alloc] initWithFrame:CGRectZero configuration:configuration] autorelease];

    auto listWithResourceType = [] (const char* type) {
        return makeContentRuleList([NSString stringWithFormat:@"[{\"action\":{\"type\":\"block\"},\"trigger\":{\"url-filter\":\".*test\",\"resource-type\":[\"%s\"]}}]", type]);
    };

    WKUserContentController *userContentController = webView.configuration.userContentController;
    [webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"test:///checkWebSocket.html"]]];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "onopen");
    [userContentController addContentRuleList:listWithResourceType("websocket").get()];
    [webView reload];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "onerror");

    [userContentController removeAllContentRuleLists];
    [webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"test:///checkFetch.html"]]];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "fetched");
    [userContentController addContentRuleList:listWithResourceType("fetch").get()];
    [webView reload];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "did not fetch");
    
    [webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"test:///checkXHR.html"]]];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "xhr error");
    EXPECT_WK_STREQ([webView _test_waitForAlert], "xhr finished");
    [userContentController removeAllContentRuleLists];
    [webView reload];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "xhr finished");
    
    HTTPServer beaconServer({
        { "/"_s, { "<script>navigator.sendBeacon('/testBeaconTarget', 'hello');fetch('/testFetchTarget').then(()=>{alert('fetch done')})</script>"_s } },
        { "/testBeaconTarget"_s, { "hi"_s } },
        { "/testFetchTarget"_s, { "hi"_s } },
    });
    [webView loadRequest:beaconServer.request()];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "fetch done");
    while (beaconServer.totalRequests() != 3)
        Util::spinRunLoop();
    [userContentController addContentRuleList:listWithResourceType("other").get()];
    [webView reload];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "fetch done");
    while (beaconServer.totalRequests() != 5)
        Util::spinRunLoop();
}

TEST(ContentRuleList, RequestMethods)
{
    NSArray<NSString *> *requestMethods = @[@"get", @"head", @"options", @"trace", @"put", @"delete", @"post", @"patch", @"connect"];
    auto listWithRequestMethod = [] (NSString *method) {
        return makeContentRuleList([NSString stringWithFormat:@"[{\"action\":{\"type\":\"block\"},\"trigger\":{\"url-filter\":\".*\", \"request-method\": \"%@\"}}]", method]);
    };

    auto delegate = adoptNS([[ContentRuleListNotificationDelegate alloc] init]);
    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [configuration setURLSchemeHandler:delegate.get() forURLScheme:@"apitest"];

    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 800, 600) configuration:configuration.get()]);
    [webView setNavigationDelegate:delegate.get()];
    [webView setUIDelegate:delegate.get()];

    for (NSString *requestMethodUnderTest in requestMethods) {
        Vector<Notification> expectedNotifications;
        [[configuration userContentController] addContentRuleList:listWithRequestMethod(requestMethodUnderTest).get()];

        for (NSString *requestMethod in requestMethods) {
            auto currentSize = notificationList.size();
            BOOL requestMethodMatchesMethodUnderTest = [requestMethod isEqualToString:requestMethodUnderTest];

            NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"apitest:///"]];
            [request setHTTPMethod:requestMethod.uppercaseString];
            [webView loadRequest:request];

            while (requestMethodMatchesMethodUnderTest && notificationList.size() == currentSize)
                TestWebKitAPI::Util::spinRunLoop();

            [webView stopLoading];

            if (requestMethodMatchesMethodUnderTest)
                expectedNotifications.append({ "testidentifier"_s, "apitest:///"_s, true, false, false, { } });
        }

        EXPECT_TRUE(expectedNotifications == notificationList);

        notificationList.clear();
        [[configuration userContentController] removeAllContentRuleLists];
    }
}

TEST(ContentRuleList, ThirdParty)
{
    auto handler = [[TestURLSchemeHandler new] autorelease];
    handler.startURLSchemeTaskHandler = ^(WKWebView *, id<WKURLSchemeTask> task) {
        NSString *path = task.request.URL.path;
        if ([path isEqualToString:@"/main.html"]) {
            return respond(task, "<script>"
                "function testWebKit() { fetch('test://webkit.org/resource.txt', {mode:'no-cors'}).then(()=>{alert('webkit.org loaded');}).catch(()=>{alert('webkit.org blocked');}) };"
                "fetch('test://sub.example.com/resource.txt', {mode:'no-cors'}).then(()=>{alert('sub.example.com loaded');testWebKit();}).catch(()=>{alert('sub.example.com blocked');testWebKit();})"
            "</script>");
        }
        if ([path isEqualToString:@"/resource.txt"])
            return respond(task, "hi");

        ASSERT_NOT_REACHED();
    };
    auto configuration = [[WKWebViewConfiguration new] autorelease];
    [configuration setURLSchemeHandler:handler forURLScheme:@"test"];
    configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    auto webView = [[[WKWebView alloc] initWithFrame:CGRectZero configuration:configuration] autorelease];

    auto listWithLoadType = [] (const char* type) {
        return makeContentRuleList([NSString stringWithFormat:@"[{\"action\":{\"type\":\"block\"},\"trigger\":{\"url-filter\":\"resource.txt\",\"load-type\":[\"%s\"]}}]", type]);
    };

    WKUserContentController *userContentController = webView.configuration.userContentController;
    [webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"test://example.com/main.html"]]];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "sub.example.com loaded");
    EXPECT_WK_STREQ([webView _test_waitForAlert], "webkit.org loaded");
    
    [userContentController addContentRuleList:listWithLoadType("third-party").get()];
    [webView reload];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "sub.example.com loaded");
    EXPECT_WK_STREQ([webView _test_waitForAlert], "webkit.org blocked");
    [userContentController removeAllContentRuleLists];
    [userContentController addContentRuleList:listWithLoadType("first-party").get()];
    [webView reload];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "sub.example.com blocked");
    EXPECT_WK_STREQ([webView _test_waitForAlert], "webkit.org loaded");
}

TEST(ContentRuleList, SupportsRegex)
{
    NSArray<NSString *> *allowed = @[
        @".*",
        @"a.*b"
    ];
    for (NSString *regex in allowed)
        EXPECT_TRUE([WKContentRuleList _supportsRegularExpression:regex]);
    
    NSArray<NSString *> *disallowed = @[
        @"Ä",
        @"\\d\\D\\w\\s\\v\\h\\i\\c",
        @"",
        @"(?<A>a)\\k<A>",
        @"a^",
        @"\\b",
        @"[\\d]",
        @"(?!)",
        @"this|that",
        @"$$",
        @"a{0,2}b"
    ];
    for (NSString *regex in disallowed)
        EXPECT_FALSE([WKContentRuleList _supportsRegularExpression:regex]);
}

TEST(ContentRuleList, ParseRuleList)
{
    NSArray<NSString *> *passingRuleLists = @[
        @"[ { \"action\": { \"type\" : \"css-display-none\", \"selector\": \"a[href*='apple.com']\" }, \"trigger\": { \"url-filter\": \".*\" }} ]",
        @"[ { \"action\": { \"type\" : \"block\" }, \"trigger\": { \"url-filter\": \"webkit.org\" }} ]",
        @"[ { \"action\": { \"type\" : \"ignore-previous-rules\" }, \"trigger\": { \"url-filter\": \"example.com\" }} ]",
    ];

    for (NSString *passingRuleList in passingRuleLists) {
        NSError *parsingError = [WKContentRuleList _parseRuleList:passingRuleList];
        EXPECT_NULL(parsingError);
    }

    NSArray<NSString *> *failingRuleLists = @[
        // Invalid JSON.
        @"{{ \"action\": { \"type\" : \"css-display-none\", \"selector\": \"a[href*='apple.com']\" }, \"trigger\": { \"url-filter\": \".*\" }}",

        // Top level object not an array.
        @"{ \"action\": { \"type\" : \"css-display-none\", \"selector\": \"a[href*='apple.com']\" }, \"trigger\": { \"url-filter\": \".*\" }}",

        // No trigger.
        @"[ { \"action\": { \"type\" : \"block\" }} ]",

        // No action.
        @"[ { \"trigger\": { \"url-filter\": \"webkit.org\" }} ]",

        // Fake action type.
        @"[ { \"action\": { \"type\" : \"dance\" }, \"trigger\": { \"url-filter\": \"webkit.org\" }} ]",
    ];

    for (NSString *failingRuleList in failingRuleLists) {
        NSError *parsingError = [WKContentRuleList _parseRuleList:failingRuleList];
        EXPECT_NOT_NULL(parsingError);
    }
}

TEST(ContentRuleList, TopFrameChildFrame)
{
    auto handler = [[TestURLSchemeHandler new] autorelease];
    __block bool loadedIFrame = false;
    handler.startURLSchemeTaskHandler = ^(WKWebView *, id<WKURLSchemeTask> task) {
        NSString *path = task.request.URL.path;
        if ([path isEqualToString:@"/main.html"])
            return respond(task, "<iframe src='frame.html'></iframe>");
        if ([path isEqualToString:@"/frame.html"]) {
            EXPECT_FALSE(loadedIFrame);
            loadedIFrame = true;
            return respond(task, "hi");
        }
        if ([path isEqualToString:@"/fetch_main.html"]) {
            return respond(task, "<script>"
                "function addiframe() { var iframe = document.createElement('iframe'); iframe.src = 'fetch_iframe.html'; document.body.appendChild(iframe); };"
                "function testfetch() { fetch('/fetched.txt').then(()=>{alert('main frame fetched successfully');addiframe()}).catch(()=>{alert('main frame fetch failed');addiframe();}) }"
                "</script><body onload='testfetch()'/>");
        }
        if ([path isEqualToString:@"/fetched.txt"])
            return respond(task, "hi");
        if ([path isEqualToString:@"/fetch_iframe.html"]) {
            return respond(task, "<script>"
                "function testfetch() { fetch('/fetched.txt').then(()=>{alert('iframe fetched successfully')}).catch(()=>{alert('iframe fetch failed');}) }"
                "</script><body onload='testfetch()'/>");
        }
        ASSERT_NOT_REACHED();
    };
    auto configuration = [[WKWebViewConfiguration new] autorelease];
    [configuration setURLSchemeHandler:handler forURLScheme:@"test"];
    configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    auto webView = [[[WKWebView alloc] initWithFrame:CGRectZero configuration:configuration] autorelease];
    WKUserContentController *userContentController = webView.configuration.userContentController;

    [webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"test:///main.html"]]];
    [webView _test_waitForDidFinishNavigation];
    EXPECT_TRUE(loadedIFrame);

    [userContentController addContentRuleList:makeContentRuleList(@"[{\"action\":{\"type\":\"block\"},\"trigger\":{\"url-filter\":\"test\",\"load-context\":[\"child-frame\"]}}]").get()];
    loadedIFrame = false;
    [webView reload];
    [webView _test_waitForDidFinishNavigation];
    EXPECT_FALSE(loadedIFrame);
    
    [userContentController removeAllContentRuleLists];
    
    [webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"test:///fetch_main.html"]]];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "main frame fetched successfully");
    EXPECT_WK_STREQ([webView _test_waitForAlert], "iframe fetched successfully");

    auto listWithLoadContext = [] (const char* type) {
        return makeContentRuleList([NSString stringWithFormat:@"[{\"action\":{\"type\":\"block\"},\"trigger\":{\"url-filter\":\"test\",\"load-context\":[\"%s\"],\"resource-type\":[\"fetch\"]}}]", type]);
    };
    [userContentController addContentRuleList:listWithLoadContext("child-frame").get()];
    [webView reload];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "main frame fetched successfully");
    EXPECT_WK_STREQ([webView _test_waitForAlert], "iframe fetch failed");

    [userContentController removeAllContentRuleLists];
    [userContentController addContentRuleList:listWithLoadContext("top-frame").get()];
    [webView reload];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "main frame fetch failed");
    EXPECT_WK_STREQ([webView _test_waitForAlert], "iframe fetched successfully");
}

TEST(ContentRuleList, CSPReport)
{
    TestWebKitAPI::HTTPServer server({ { "/"_s, { {
        { "Content-Security-Policy"_s, "frame-src 'none'; report-uri resources/save-report.py"_s }
    }, "<iframe src=\"https://webkit.org/\"></iframe>"_s } } });

    auto configuration = adoptNS([WKWebViewConfiguration new]);
    [[configuration userContentController] addContentRuleList:makeContentRuleList(@"[{\"action\":{\"type\":\"block\"},\"trigger\":{\"url-filter\":\".*\",\"resource-type\":[\"csp-report\"]}}]").get()];
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration.get()]);
    auto delegate = adoptNS([ContentRuleListNotificationDelegate new]);
    [webView setNavigationDelegate:delegate.get()];
    [webView loadRequest:server.request()];
    while (notificationList.isEmpty())
        TestWebKitAPI::Util::spinRunLoop();

    URL expectedURL = server.request().URL;
    expectedURL.setPath("/resources/save-report.py"_s);
    EXPECT_STREQ(expectedURL.string().utf8().data(), notificationList.first().url.utf8().data());
}

TEST(WebKit, RedirectToPlaintextHTTPSUpgrade)
{
    using namespace TestWebKitAPI;
    HTTPServer plaintextServer({ { "http://download/redirectTarget"_s, { "<script>alert('success!')</script>"_s } } });
    HTTPServer secureServer({ { "/originalRequest"_s, { 302, { { "Location"_s, "http://download/redirectTarget"_s } }, emptyString() } } }, HTTPServer::Protocol::HttpsProxy);

    auto storeConfiguration = adoptNS([[_WKWebsiteDataStoreConfiguration alloc] initNonPersistentConfiguration]);
    [storeConfiguration setHTTPProxy:[NSURL URLWithString:[NSString stringWithFormat:@"http://127.0.0.1:%d/", plaintextServer.port()]]];
    [storeConfiguration setHTTPSProxy:[NSURL URLWithString:[NSString stringWithFormat:@"https://127.0.0.1:%d/", secureServer.port()]]];
    [storeConfiguration setAllowsServerPreconnect:NO];
    auto viewConfiguration = adoptNS([WKWebViewConfiguration new]);
    [viewConfiguration setWebsiteDataStore:adoptNS([[WKWebsiteDataStore alloc] _initWithConfiguration:storeConfiguration.get()]).get()];
    auto webView = adoptNS([[WKWebView alloc] initWithFrame:CGRectMake(0, 0, 100, 100) configuration:viewConfiguration.get()]);
    auto delegate = adoptNS([TestNavigationDelegate new]);
    delegate.get().didReceiveAuthenticationChallenge = ^(WKWebView *, NSURLAuthenticationChallenge *challenge, void (^completionHandler)(NSURLSessionAuthChallengeDisposition, NSURLCredential *)) {
        completionHandler(NSURLSessionAuthChallengeUseCredential, [NSURLCredential credentialForTrust:challenge.protectionSpace.serverTrust]);
    };
    webView.get().navigationDelegate = delegate.get();
    [webView loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"https://download/originalRequest"]]];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "success!");
}

TEST(ContentRuleList, RedirectBeforeBlock)
{
    using namespace TestWebKitAPI;
    HTTPServer server({
        { "/to-be-blocked"_s, { "This content isn't going to load."_s } },
        { "/redirected"_s, { "<script>alert('Redirected!')</script>"_s } }
    });

    NSString *rules = [NSString stringWithFormat:@"[{\"action\":{\"type\":\"redirect\",\"redirect\":{\"url\":\"%@\"}},\"trigger\":{\"url-filter\":\".*\"}},{\"action\":{\"type\":\"block\"},\"trigger\":{\"url-filter\":\".*\"}}]", server.request("/redirected"_s).URL.absoluteString];

    auto configuration = adoptNS([[WKWebViewConfiguration alloc] init]);
    [[configuration userContentController] addContentRuleList:makeContentRuleList(rules).get()];

    auto webView = adoptNS([[WKWebView alloc] initWithFrame:CGRectZero configuration:configuration.get()]);
    auto delegate = adoptNS([TestNavigationDelegate new]);
    delegate.get().decidePolicyForNavigationActionWithPreferences = ^(WKNavigationAction *, WKWebpagePreferences *preferences, void (^decisionHandler)(WKNavigationActionPolicy, WKWebpagePreferences *)) {
        preferences._activeContentRuleListActionPatterns = @{
            @"testidentifier": [NSSet setWithObject:@"*://*/*"]
        };
        decisionHandler(WKNavigationActionPolicyAllow, preferences);
    };
    [delegate allowAnyTLSCertificate];
    webView.get().navigationDelegate = delegate.get();

    [webView loadRequest:server.request("/to-be-blocked"_s)];
    EXPECT_WK_STREQ([webView _test_waitForAlert], "Redirected!");
}
