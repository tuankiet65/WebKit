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

#ifndef WKBundleScriptWorld_h
#define WKBundleScriptWorld_h

#include <WebKit/WKBase.h>

#ifdef __cplusplus
extern "C" {
#endif

WK_EXPORT WKTypeID WKBundleScriptWorldGetTypeID();

WK_EXPORT WKBundleScriptWorldRef WKBundleScriptWorldCreateWorld();
WK_EXPORT WKBundleScriptWorldRef WKBundleScriptWorldNormalWorld();
WK_EXPORT void WKBundleScriptWorldClearWrappers(WKBundleScriptWorldRef scriptWorld);
WK_EXPORT void WKBundleScriptWorldMakeAllShadowRootsOpen(WKBundleScriptWorldRef scriptWorld);
WK_EXPORT void WKBundleScriptWorldExposeClosedShadowRootsForExtensions(WKBundleScriptWorldRef scriptWorld);
WK_EXPORT void WKBundleScriptWorldDisableOverrideBuiltinsBehavior(WKBundleScriptWorldRef scriptWorld);
WK_EXPORT void WKBundleScriptWorldSetAllowElementUserInfo(WKBundleScriptWorldRef scriptWorld);
WK_EXPORT WKStringRef WKBundleScriptWorldCopyName(WKBundleScriptWorldRef scriptWorld);

#ifdef __cplusplus
}
#endif

#endif /* WKBundleScriptWorld_h */
