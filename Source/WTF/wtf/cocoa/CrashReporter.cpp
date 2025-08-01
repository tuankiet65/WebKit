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
#include "CrashReporter.h"

#include <wtf/NeverDestroyed.h>
#include <wtf/spi/cocoa/CrashReporterClientSPI.h>
#include <wtf/text/CString.h>

#ifdef CRASHREPORTER_ANNOTATIONS_INITIALIZER
CRASHREPORTER_ANNOTATIONS_INITIALIZER()
#else
// Avoid having to link with libCrashReporterClient.a
CRASH_REPORTER_CLIENT_HIDDEN
struct crashreporter_annotations_t gCRAnnotations
    __attribute__((section("__DATA," CRASHREPORTER_ANNOTATIONS_SECTION)))
    = { CRASHREPORTER_ANNOTATIONS_VERSION, 0, 0, 0, 0, 0, 0, 0 };
#endif // CRASHREPORTER_ANNOTATIONS_INITIALIZER

namespace WTF {
void setCrashLogMessage(const char* message)
{
    // We have to copy the string because CRSetCrashLogMessage doesn't.
    CString copiedMessage = message;

    CRSetCrashLogMessage(copiedMessage.data());

    // Delete the message from last time, so we don't keep leaking messages.
    static NeverDestroyed<CString> previousCopiedCrashLogMessage;
    previousCopiedCrashLogMessage.get() = WTFMove(copiedMessage);
}
}
