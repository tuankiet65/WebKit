/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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
#include "WKBundleScriptWorld.h"

#include "InjectedBundleScriptWorld.h"
#include "WKAPICast.h"
#include "WKBundleAPICast.h"

WKTypeID WKBundleScriptWorldGetTypeID()
{
    return WebKit::toAPI(WebKit::InjectedBundleScriptWorld::APIType);
}

WKBundleScriptWorldRef WKBundleScriptWorldCreateWorld()
{
    RefPtr<WebKit::InjectedBundleScriptWorld> world = WebKit::InjectedBundleScriptWorld::create();
    return toAPI(world.leakRef());
}

WKBundleScriptWorldRef WKBundleScriptWorldNormalWorld()
{
    return toAPI(&WebKit::InjectedBundleScriptWorld::normalWorldSingleton());
}

void WKBundleScriptWorldClearWrappers(WKBundleScriptWorldRef scriptWorldRef)
{
    WebKit::toImpl(scriptWorldRef)->clearWrappers();
}

void WKBundleScriptWorldMakeAllShadowRootsOpen(WKBundleScriptWorldRef scriptWorldRef)
{
    WebKit::toImpl(scriptWorldRef)->makeAllShadowRootsOpen();
}

void WKBundleScriptWorldExposeClosedShadowRootsForExtensions(WKBundleScriptWorldRef scriptWorldRef)
{
    WebKit::toImpl(scriptWorldRef)->exposeClosedShadowRootsForExtensions();
}

void WKBundleScriptWorldDisableOverrideBuiltinsBehavior(WKBundleScriptWorldRef scriptWorldRef)
{
    WebKit::toImpl(scriptWorldRef)->disableOverrideBuiltinsBehavior();
}

void WKBundleScriptWorldSetAllowElementUserInfo(WKBundleScriptWorldRef scriptWorldRef)
{
    WebKit::toImpl(scriptWorldRef)->setAllowElementUserInfo();
}

WKStringRef WKBundleScriptWorldCopyName(WKBundleScriptWorldRef scriptWorldRef)
{
    return WebKit::toCopiedAPI(WebKit::toImpl(scriptWorldRef)->name());
}
