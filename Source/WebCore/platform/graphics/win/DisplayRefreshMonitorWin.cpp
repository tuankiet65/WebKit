/*
 * Copyright (C) 2020 Sony Interactive Entertainment Inc.
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
#include "DisplayRefreshMonitorWin.h"

namespace WebCore {

constexpr FramesPerSecond DefaultFramesPerSecond = 60;

RefPtr<DisplayRefreshMonitorWin> DisplayRefreshMonitorWin::create(PlatformDisplayID displayID)
{
    return adoptRef(*new DisplayRefreshMonitorWin(displayID));
}

DisplayRefreshMonitorWin::DisplayRefreshMonitorWin(PlatformDisplayID displayID)
    : DisplayRefreshMonitor(displayID)
    , m_timer(RunLoop::mainSingleton(), "DisplayRefreshMonitorWin::Timer"_s, this, &DisplayRefreshMonitorWin::displayLinkCallbackFired)
    , m_currentUpdate({ 0, DefaultFramesPerSecond })
{
}

void DisplayRefreshMonitorWin::displayLinkCallbackFired()
{
    displayLinkFired(m_currentUpdate);
    m_currentUpdate = m_currentUpdate.nextUpdate();
}

bool DisplayRefreshMonitorWin::startNotificationMechanism()
{
    if (!m_timer.isActive())
        m_timer.startRepeating(16_ms);

    return true;
}

void DisplayRefreshMonitorWin::stopNotificationMechanism()
{
    m_timer.stop();
}

} // namespace WebCore
