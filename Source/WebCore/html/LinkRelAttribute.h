/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 * Copyright (C) 2016 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#pragma once

#include <wtf/Forward.h>
#include <wtf/Markable.h>

namespace WebCore {

class Document;
enum class LinkIconType : uint8_t;

struct LinkRelAttribute {
    Markable<LinkIconType> iconType;
    bool isStyleSheet : 1 { false };
    bool isAlternate : 1 { false };
    bool isDNSPrefetch : 1 { false };
    bool isLinkModulePreload : 1 { false };
    bool isLinkPreload : 1 { false };
    bool isLinkPreconnect : 1 { false };
    bool isLinkPrefetch : 1 { false };
#if ENABLE(APPLICATION_MANIFEST)
    bool isApplicationManifest : 1 { false };
#endif
    bool isInternalResourceLink : 1 { false };
#if ENABLE(WEB_PAGE_SPATIAL_BACKDROP)
    bool isSpatialBackdrop : 1 { false };
#endif

    LinkRelAttribute() = default;
    LinkRelAttribute(Document&, StringView);

    friend bool operator==(const LinkRelAttribute&, const LinkRelAttribute&) = default;

    static bool isSupported(Document&, StringView);
};

}
