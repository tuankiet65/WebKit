/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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
#include "WebBackForwardCacheEntry.h"

#include "Logging.h"
#include "SuspendedPageProxy.h"
#include "WebBackForwardCache.h"
#include "WebProcessMessages.h"
#include "WebProcessProxy.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

static const Seconds expirationDelay { 30_min };

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebBackForwardCacheEntry);

Ref<WebBackForwardCacheEntry> WebBackForwardCacheEntry::create(WebBackForwardCache& backForwardCache, WebCore::BackForwardItemIdentifier backForwardItemID, WebCore::ProcessIdentifier processIdentifier, RefPtr<SuspendedPageProxy>&& suspendedPage)
{
    return adoptRef(*new WebBackForwardCacheEntry(backForwardCache, backForwardItemID, processIdentifier, WTFMove(suspendedPage)));
}

WebBackForwardCacheEntry::WebBackForwardCacheEntry(WebBackForwardCache& backForwardCache, WebCore::BackForwardItemIdentifier backForwardItemID, WebCore::ProcessIdentifier processIdentifier, RefPtr<SuspendedPageProxy>&& suspendedPage)
    : m_backForwardCache(backForwardCache)
    , m_processIdentifier(processIdentifier)
    , m_backForwardItemID(backForwardItemID)
    , m_suspendedPage(WTFMove(suspendedPage))
    , m_expirationTimer(RunLoop::mainSingleton(), "WebBackForwardCacheEntry::ExpirationTimer"_s, this, &WebBackForwardCacheEntry::expirationTimerFired)
{
    m_expirationTimer.startOneShot(expirationDelay);
}

WebBackForwardCacheEntry::~WebBackForwardCacheEntry()
{
    if (m_backForwardItemID && !m_suspendedPage) {
        if (auto process = this->process())
            process->sendWithAsyncReply(Messages::WebProcess::ClearCachedPage(*m_backForwardItemID), [] { });
    }
}

WebBackForwardCache* WebBackForwardCacheEntry::backForwardCache() const
{
    return m_backForwardCache.get();
}

Ref<SuspendedPageProxy> WebBackForwardCacheEntry::takeSuspendedPage()
{
    ASSERT(m_suspendedPage);
    m_backForwardItemID = std::nullopt;
    m_expirationTimer.stop();
    return std::exchange(m_suspendedPage, nullptr).releaseNonNull();
}

RefPtr<WebProcessProxy> WebBackForwardCacheEntry::process() const
{
    auto process = WebProcessProxy::processForIdentifier(m_processIdentifier);
    ASSERT(process);
    ASSERT(!m_suspendedPage || process == &m_suspendedPage->process());
    return process;
}

void WebBackForwardCacheEntry::expirationTimerFired()
{
    ASSERT(m_backForwardItemID);
    RELEASE_LOG(BackForwardCache, "%p - WebBackForwardCacheEntry::expirationTimerFired backForwardItemID=%s, hasSuspendedPage=%d", this, m_backForwardItemID->toString().utf8().data(), !!m_suspendedPage);
    RefPtr item = WebBackForwardListItem::itemForID(*m_backForwardItemID);
    ASSERT(item);
    if (RefPtr backForwardCache = m_backForwardCache.get())
        backForwardCache->removeEntry(*item);
}

} // namespace WebKit
