/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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
#include "WebExtensionAlarm.h"
#include <wtf/TZoneMallocInlines.h>

#if ENABLE(WK_WEB_EXTENSIONS)

#include "Logging.h"

namespace WebKit {

using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebExtensionAlarm);

void WebExtensionAlarm::schedule()
{
    m_parameters.nextScheduledTime = MonotonicTime::now() + initialInterval();

    RELEASE_LOG_DEBUG(Extensions, "Scheduled alarm; initial = %{public}f seconds; repeat = %{public}f seconds", initialInterval().seconds(), repeatInterval().seconds());

    m_timer = makeUnique<RunLoop::Timer>(RunLoop::currentSingleton(), "WebExtensionAlarm::Timer"_s, this, &WebExtensionAlarm::fire);
    m_timer->startOneShot(initialInterval());
}

void WebExtensionAlarm::fire()
{
    // Calculate the next scheduled time now, so the handler's work time does not count against it.
    auto nextScheduledTime = MonotonicTime::now() + repeatInterval();
    if (!m_hasFiredInitialTimer) {
        m_hasFiredInitialTimer = true;
        m_timer->startRepeating(repeatInterval());
    }

    m_handler(*this);

    if (!repeatInterval()) {
        m_timer = nullptr;
        return;
    }

    m_parameters.nextScheduledTime = nextScheduledTime;
}

} // namespace WebKit

#endif // ENABLE(WK_WEB_EXTENSIONS)
