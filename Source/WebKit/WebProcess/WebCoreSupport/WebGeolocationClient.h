/*
 * Copyright (C) 2011 Apple Inc. All rights reserved.
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

#include "WebPage.h"
#include <WebCore/GeolocationClient.h>
#include <wtf/Ref.h>
#include <wtf/RefCounted.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakRef.h>

namespace WebKit {

class WebGeolocationClient final : public WebCore::GeolocationClient, public RefCounted<WebGeolocationClient> {
    WTF_MAKE_TZONE_ALLOCATED(WebGeolocationClient);
public:
    static Ref<WebGeolocationClient> create(WebPage& webPage) { return adoptRef(*new WebGeolocationClient(webPage)); }
    virtual ~WebGeolocationClient();

    void ref() const final { RefCounted::ref(); }
    void deref() const final { RefCounted::deref(); }

private:
    explicit WebGeolocationClient(WebPage&);

    void geolocationDestroyed() final;

    void startUpdating(const String& authorizationToken, bool needsHighAccuracy) final;
    void stopUpdating() final;
    void revokeAuthorizationToken(const String&) final;
    void setEnableHighAccuracy(bool) final;

    std::optional<WebCore::GeolocationPositionData> lastPosition() final;

    void requestPermission(WebCore::Geolocation&) final;
    void cancelPermissionRequest(WebCore::Geolocation&) final;

    WeakPtr<WebPage> m_page;
};

} // namespace WebKit
