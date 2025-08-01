/*
 * Copyright (C) 2003-2025 Apple Inc. All rights reserved.
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

#pragma once

#include <wtf/Platform.h>

/*
   no namespaces because this file has to be includable from C and Objective-C

   Note, this file uses many GCC extensions, but it should be compatible with
   C, Objective C, C++, and Objective C++.

   For non-debug builds, everything is disabled by default except for "always
   on" logging. Defining any of the symbols explicitly prevents this from
   having any effect.
*/

#undef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <wtf/ExportMacros.h>

#if OS(ANDROID)
#include <android/log.h>
#endif

#if OS(DARWIN)
#include <wtf/spi/darwin/ReasonSPI.h>
#endif

#if USE(OS_LOG)
#include <os/log.h>
#endif

#if ENABLE(JOURNALD_LOG)
#define SD_JOURNAL_SUPPRESS_LOCATION
#include <systemd/sd-journal.h>
#endif

#ifdef __cplusplus
#include <cstdlib>
#include <span>
#include <type_traits>
#endif

#define _XSTRINGIFY(line) #line
#define _STRINGIFY(line) _XSTRINGIFY(line)

/* ASSERT_ENABLED is defined in PlatformEnable.h. */

#ifndef BACKTRACE_DISABLED
#define BACKTRACE_DISABLED !ASSERT_ENABLED
#endif

#ifndef ASSERT_MSG_DISABLED
#define ASSERT_MSG_DISABLED !ASSERT_ENABLED
#endif

#ifndef ASSERT_ARG_DISABLED
#define ASSERT_ARG_DISABLED !ASSERT_ENABLED
#endif

#ifndef FATAL_DISABLED
#define FATAL_DISABLED !ASSERT_ENABLED
#endif

#ifndef ERROR_DISABLED
#define ERROR_DISABLED !ASSERT_ENABLED
#endif

#ifndef LOG_DISABLED
#define LOG_DISABLED !ASSERT_ENABLED
#endif

#if ENABLE(RELEASE_LOG)
#define RELEASE_LOG_DISABLED 0
#else
#define RELEASE_LOG_DISABLED !(USE(OS_LOG) || ENABLE(JOURNALD_LOG) || OS(ANDROID))
#endif

#ifndef VERBOSE_RELEASE_LOG
#define VERBOSE_RELEASE_LOG ENABLE(JOURNALD_LOG)
#endif

#define WTF_PRETTY_FUNCTION __PRETTY_FUNCTION__

#if COMPILER(GCC_COMPATIBLE) && !defined(__OBJC__)
/* WTF logging functions can process %@ in the format string to log a NSObject* but the printf format attribute
   emits a warning when %@ is used in the format string.  Until <rdar://problem/5195437> is resolved we can't include
   the attribute when being used from Objective-C code in case it decides to use %@. */
#define WTF_ATTRIBUTE_PRINTF(formatStringArgument, extraArguments) __attribute__((__format__(printf, formatStringArgument, extraArguments)))
#else
#define WTF_ATTRIBUTE_PRINTF(formatStringArgument, extraArguments)
#endif

#if PLATFORM(IOS_FAMILY)
/* For a project that uses WTF but has no config.h, we need to explicitly set the export defines here. */
#ifndef WTF_EXPORT_PRIVATE
#define WTF_EXPORT_PRIVATE
#endif
#endif // PLATFORM(IOS_FAMILY)

/* These helper functions are always declared, but not necessarily always defined if the corresponding function is disabled. */

#ifdef __cplusplus
extern "C" {
#endif

/*
   This header file needs to be compatible with C code, [noreturn]] cannot be used here.
   Use [[noreturn]] in WebKit except this header file.
*/
#if !defined(NO_RETURN_FOR_C)
#define NO_RETURN_FOR_C __attribute((__noreturn__))
#endif

/* CRASH() - Raises a fatal error resulting in program termination and triggering either the debugger or the crash reporter.

   Use CRASH() in response to known, unrecoverable errors like out-of-memory.
   Macro is enabled in both debug and release mode.
   To test for unknown errors and verify assumptions, use ASSERT instead, to avoid impacting performance in release builds.

   Signals are ignored by the crash reporter on OS X so we must do better.
*/
#define NO_RETURN_DUE_TO_CRASH NO_RETURN_FOR_C

#ifdef __cplusplus
enum class WTFLogChannelState : uint8_t { Off, On, OnWithAccumulation };
#undef Always
enum class WTFLogLevel : uint8_t { Always, Error, Warning, Info, Debug };
namespace WTF {
class PrintStream;
}
#else
typedef uint8_t WTFLogChannelState;
typedef uint8_t WTFLogLevel;
#endif

typedef struct {
    WTFLogChannelState state;
    const char* name;
    WTFLogLevel level;
#if !RELEASE_LOG_DISABLED && USE(OS_LOG)
    const char* subsystem;
    __unsafe_unretained os_log_t osLogChannel;
#endif
} WTFLogChannel;

#define LOG_CHANNEL(name) JOIN_LOG_CHANNEL_WITH_PREFIX(LOG_CHANNEL_PREFIX, name)
#define LOG_CHANNEL_ADDRESS(name) &LOG_CHANNEL(name),
#define JOIN_LOG_CHANNEL_WITH_PREFIX(prefix, channel) JOIN_LOG_CHANNEL_WITH_PREFIX_LEVEL_2(prefix, channel)
#define JOIN_LOG_CHANNEL_WITH_PREFIX_LEVEL_2(prefix, channel) prefix ## channel

#if PLATFORM(GTK)
#define LOG_CHANNEL_WEBKIT_SUBSYSTEM "WebKitGTK"
#elif PLATFORM(WPE)
#define LOG_CHANNEL_WEBKIT_SUBSYSTEM "WPEWebKit"
#elif PLATFORM(PLAYSTATION)
#define LOG_CHANNEL_WEBKIT_SUBSYSTEM "SceNKWebKit"
#else
#define LOG_CHANNEL_WEBKIT_SUBSYSTEM "com.apple.WebKit"
#endif

#define DECLARE_LOG_CHANNEL(name) \
    extern WTFLogChannel LOG_CHANNEL(name);

#if !defined(DEFINE_LOG_CHANNEL)
#if USE(OS_LOG)
#define DEFINE_LOG_CHANNEL_WITH_DETAILS(name, initialState, level, subsystem) \
    WTFLogChannel LOG_CHANNEL(name) = { initialState, #name, level, subsystem, OS_LOG_DEFAULT };
#else
#define DEFINE_LOG_CHANNEL_WITH_DETAILS(name, initialState, level, subsystem) \
    WTFLogChannel LOG_CHANNEL(name) = { initialState, #name, level };
#endif
#endif

// This file is included from C (not C++) files, so we can't say things like WTFLogChannelState::Off.
static const WTFLogChannelState logChannelStateOff = (WTFLogChannelState)0;
static const WTFLogChannelState logChannelStateOn = (WTFLogChannelState)1;
static const WTFLogLevel logLevelError = (WTFLogLevel)1;
#define DEFINE_LOG_CHANNEL(name, subsystem) DEFINE_LOG_CHANNEL_WITH_DETAILS(name, logChannelStateOff, logLevelError, subsystem);

WTF_EXPORT_PRIVATE void WTFReportNotImplementedYet(const char* file, int line, const char* function);
WTF_EXPORT_PRIVATE void WTFReportAssertionFailure(const char* file, int line, const char* function, const char* assertion);
WTF_EXPORT_PRIVATE void WTFReportAssertionFailureWithMessage(const char* file, int line, const char* function, const char* assertion, const char* format, ...) WTF_ATTRIBUTE_PRINTF(5, 6);
WTF_EXPORT_PRIVATE void WTFReportArgumentAssertionFailure(const char* file, int line, const char* function, const char* argName, const char* assertion);
WTF_EXPORT_PRIVATE void WTFReportFatalError(const char* file, int line, const char* function, const char* format, ...) WTF_ATTRIBUTE_PRINTF(4, 5);
WTF_EXPORT_PRIVATE void WTFReportError(const char* file, int line, const char* function, const char* format, ...) WTF_ATTRIBUTE_PRINTF(4, 5);
WTF_EXPORT_PRIVATE void WTFLog(WTFLogChannel*, const char* format, ...) WTF_ATTRIBUTE_PRINTF(2, 3);
WTF_EXPORT_PRIVATE void WTFLogVerbose(const char* file, int line, const char* function, WTFLogChannel*, const char* format, ...) WTF_ATTRIBUTE_PRINTF(5, 6);
WTF_EXPORT_PRIVATE void WTFLogAlwaysV(const char* format, va_list) WTF_ATTRIBUTE_PRINTF(1, 0);
WTF_EXPORT_PRIVATE void WTFLogAlways(const char* format, ...) WTF_ATTRIBUTE_PRINTF(1, 2);
WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH void WTFLogAlwaysAndCrash(const char* format, ...) WTF_ATTRIBUTE_PRINTF(1, 2);
WTF_EXPORT_PRIVATE WTFLogChannel* WTFLogChannelByName(WTFLogChannel*[], size_t count, const char*);
WTF_EXPORT_PRIVATE void WTFInitializeLogChannelStatesFromString(WTFLogChannel*[], size_t count, const char*);
WTF_EXPORT_PRIVATE void WTFLogWithLevel(WTFLogChannel*, WTFLogLevel, const char* format, ...) WTF_ATTRIBUTE_PRINTF(3, 4);
WTF_EXPORT_PRIVATE void WTFSetLogChannelLevel(WTFLogChannel*, WTFLogLevel);
WTF_EXPORT_PRIVATE bool WTFWillLogWithLevel(WTFLogChannel*, WTFLogLevel);

WTF_EXPORT_PRIVATE NEVER_INLINE void WTFGetBacktrace(void** stack, int* size);
WTF_EXPORT_PRIVATE void WTFReportBacktraceWithPrefix(const char*);
WTF_EXPORT_PRIVATE void WTFReportBacktraceWithStackDepth(int);
WTF_EXPORT_PRIVATE void WTFReportBacktraceWithPrefixAndStackDepth(const char*, int);
WTF_EXPORT_PRIVATE void WTFReportBacktrace(void);
#ifdef __cplusplus
WTF_EXPORT_PRIVATE void WTFReportBacktraceWithPrefixAndPrintStream(WTF::PrintStream&, const char*);
WTF_EXPORT_PRIVATE void WTFPrintBacktraceWithPrefixAndPrintStream(WTF::PrintStream&, std::span<void* const> stack, const char* prefix);
WTF_EXPORT_PRIVATE void WTFPrintBacktrace(std::span<void* const> stack);
#endif

WTF_EXPORT_PRIVATE bool WTFIsDebuggerAttached(void);

#if CPU(X86_64) || CPU(X86)

#define WTF_FATAL_CRASH_INST "int3"

// This ordering was chosen to be consistent with JSC's JIT asserts. We probably shouldn't change this ordering
// since it would make tooling crash reports much harder. If, for whatever reason, we decide to change the ordering
// here we should update the abortWithuint64_t functions.
#define CRASH_ARG_GPR0 "rdi"
#define CRASH_ARG_GPR1 "rsi"
#define CRASH_ARG_GPR2 "rdx"
#define CRASH_ARG_GPR3 "rcx"

#define CRASH_GPR0 "r11"
#define CRASH_GPR1 "r10"
#define CRASH_GPR2 "r9"
#define CRASH_GPR3 "r8"
#define CRASH_GPR4 "r15"
#define CRASH_GPR5 "r14"
#define CRASH_GPR6 "r13"

#elif CPU(ARM64)

#if !defined(WTF_FATAL_CRASH_CODE)
#if ASAN_ENABLED
#define WTF_FATAL_CRASH_CODE 0x0
#else
#define WTF_FATAL_CRASH_CODE 0xc471
#endif
#endif

#if !defined(WTF_FATAL_CRASH_INST)
#if ASAN_ENABLED
#define WTF_FATAL_CRASH_INST "brk #0x0"
#else
#define WTF_FATAL_CRASH_INST "brk #0xc471"
#endif
#endif

// See comment above on the ordering.
#define CRASH_ARG_GPR0 "x0"
#define CRASH_ARG_GPR1 "x1"
#define CRASH_ARG_GPR2 "x2"
#define CRASH_ARG_GPR3 "x3"

#define CRASH_GPR0 "x16"
#define CRASH_GPR1 "x17"
#define CRASH_GPR2 "x19" // We skip x18, which is reserved on ARM64 for platform use.
#define CRASH_GPR3 "x20"
#define CRASH_GPR4 "x21"
#define CRASH_GPR5 "x22"
#define CRASH_GPR6 "x23"

#endif // CPU(ARM64)

#if ASAN_ENABLED
#define WTFBreakpointTrap()  __builtin_trap()
#elif CPU(X86_64) || CPU(X86)
#define WTFBreakpointTrap()  asm volatile (WTF_FATAL_CRASH_INST)
#elif CPU(ARM_THUMB2)
#define WTFBreakpointTrap()  asm volatile ("bkpt #0")
#elif CPU(ARM64)
#define WTFBreakpointTrap()  asm volatile (WTF_FATAL_CRASH_INST)
#else
#define WTFBreakpointTrap() WTFCrash() // Not implemented.
#endif

#define WTFBreakpointTrapUnderConstexprContext() __builtin_trap()

#ifndef CRASH

#if defined(NDEBUG) && (OS(DARWIN) || PLATFORM(PLAYSTATION))
// Crash with a SIGTRAP i.e EXC_BREAKPOINT.
// We are not using __builtin_trap because it is only guaranteed to abort, but not necessarily
// trigger a SIGTRAP. Instead, we use inline asm to ensure that we trigger the SIGTRAP.
#define CRASH() do { \
    WTFBreakpointTrap(); \
    __builtin_unreachable(); \
} while (0)
#define CRASH_UNDER_CONSTEXPR_CONTEXT() do { \
    WTFBreakpointTrapUnderConstexprContext(); \
    __builtin_unreachable(); \
} while (0)
#elif !ENABLE(DEVELOPER_MODE) && !OS(DARWIN)
#ifdef __cplusplus
#define CRASH() std::abort()
#define CRASH_UNDER_CONSTEXPR_CONTEXT() WTFBreakpointTrapUnderConstexprContext()
#else
#define CRASH() abort()
#define CRASH_UNDER_CONSTEXPR_CONTEXT() WTFBreakpointTrapUnderConstexprContext()
#endif // __cplusplus
#else
#define CRASH() WTFCrash()
#define CRASH_UNDER_CONSTEXPR_CONTEXT() WTFBreakpointTrapUnderConstexprContext()
#endif

#endif // !defined(CRASH)

WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH void WTFCrash(void);

#ifndef CRASH_WITH_SECURITY_IMPLICATION
#define CRASH_WITH_SECURITY_IMPLICATION() WTFCrashWithSecurityImplication()
#endif

#if ENABLE(CONJECTURE_ASSERT)
extern WTF_EXPORT_PRIVATE int wtfConjectureAssertIsEnabled;
WTF_EXPORT_PRIVATE NEVER_INLINE NO_RETURN_DUE_TO_CRASH void WTFCrashDueToConjectureAssert(const char* file, int line, const char* function, const char* assertion);
#endif

WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH void WTFCrashWithSecurityImplication(void);

#ifdef __cplusplus
}
#endif

/* BACKTRACE

  Print a backtrace to the same location as ASSERT messages.
*/

#if BACKTRACE_DISABLED

#define BACKTRACE() ((void)0)

#else

#define BACKTRACE() do { \
    WTFReportBacktrace(); \
} while(false)

#endif

/* ASSERT, ASSERT_NOT_REACHED, ASSERT_UNUSED

  These macros are compiled out of release builds.
  Expressions inside them are evaluated in debug builds only.
*/

#if OS(WINDOWS)
/* FIXME: Change to use something other than ASSERT to avoid this conflict with the underlying platform */
#undef ASSERT
#endif

/* This header may be used in C code so we cannot rely on [[unlikely]]. */
/* Use [[likely]] / [[unlikely]] in WebKit, do not call this macro outside this header. */
#if !defined(UNLIKELY_FOR_C_ASSERTIONS)
#define UNLIKELY_FOR_C_ASSERTIONS(x) __builtin_expect(!!(x), 0)
#endif

#if !ASSERT_ENABLED

#define ASSERT(assertion, ...) ((void)0)
#define ASSERT_UNDER_CONSTEXPR_CONTEXT(assertion) ((void)0)
#define ASSERT_AT(assertion, file, line, function) ((void)0)
#define ASSERT_NOT_REACHED(...) ((void)0)
#define ASSERT_NOT_REACHED_UNDER_CONSTEXPR_CONTEXT(...) ((void)0)
#define ASSERT_NOT_IMPLEMENTED_YET() ((void)0)
#define ASSERT_IMPLIES(condition, assertion) ((void)0)
#define NO_RETURN_DUE_TO_ASSERT

#define ASSERT_UNUSED(variable, assertion, ...) ((void)variable)

#if ENABLE(SECURITY_ASSERTIONS)
#define ASSERT_NOT_REACHED_WITH_SECURITY_IMPLICATION(...) CRASH_WITH_SECURITY_IMPLICATION_AND_INFO(__VA_ARGS__)
#define ASSERT_WITH_SECURITY_IMPLICATION(assertion) \
    (UNLIKELY_FOR_C_ASSERTIONS(!(assertion)) ? \
        (WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #assertion), \
         CRASH_WITH_SECURITY_IMPLICATION()) : \
        (void)0)

#define ASSERT_WITH_SECURITY_IMPLICATION_DISABLED 0
#define NO_RETURN_DUE_TO_ASSERT_WITH_SECURITY_IMPLICATION NO_RETURN_DUE_TO_CRASH
#else /* not ENABLE(SECURITY_ASSERTIONS) */
#define ASSERT_NOT_REACHED_WITH_SECURITY_IMPLICATION(...) ((void)0)
#define ASSERT_WITH_SECURITY_IMPLICATION(assertion) ((void)0)
#define ASSERT_WITH_SECURITY_IMPLICATION_DISABLED 1
#define NO_RETURN_DUE_TO_ASSERT_WITH_SECURITY_IMPLICATION
#endif /* ENABLE(SECURITY_ASSERTIONS) */

#else /* ASSERT_ENABLED */

#define ASSERT(assertion, ...) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) { \
        WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #assertion); \
        BACKTRACE(); \
        CRASH_WITH_INFO(__VA_ARGS__); \
    } \
} while (0)

#define ASSERT_UNDER_CONSTEXPR_CONTEXT(assertion) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) { \
        WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #assertion); \
        if (!std::is_constant_evaluated()) \
            BACKTRACE(); \
        CRASH_UNDER_CONSTEXPR_CONTEXT(); \
    } \
} while (0)

#define ASSERT_AT(assertion, file, line, function) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) { \
        WTFReportAssertionFailure(file, line, function, #assertion); \
        BACKTRACE(); \
        CRASH(); \
    } \
} while (0)

#define ASSERT_NOT_REACHED(...) do { \
    WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, 0); \
    BACKTRACE(); \
    CRASH_WITH_INFO(__VA_ARGS__); \
} while (0)

#define ASSERT_NOT_REACHED_WITH_SECURITY_IMPLICATION(...) do { \
    WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, 0); \
    BACKTRACE(); \
    CRASH_WITH_SECURITY_IMPLICATION_AND_INFO(__VA_ARGS__); \
} while (0)

#define ASSERT_NOT_REACHED_UNDER_CONSTEXPR_CONTEXT(...) do { \
    if (!std::is_constant_evaluated()) \
        BACKTRACE(); \
    CRASH_UNDER_CONSTEXPR_CONTEXT(); \
} while (0)

#define ASSERT_NOT_IMPLEMENTED_YET() do { \
    WTFReportNotImplementedYet(__FILE__, __LINE__, WTF_PRETTY_FUNCTION); \
    BACKTRACE(); \
    CRASH(); \
} while (0)

#define ASSERT_IMPLIES(condition, assertion) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS((condition) && !(assertion))) { \
        WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #condition " => " #assertion); \
        BACKTRACE(); \
        CRASH(); \
    } \
} while (0)

#define ASSERT_UNUSED(variable, assertion, ...) ASSERT(assertion, __VA_ARGS__)

#define NO_RETURN_DUE_TO_ASSERT NO_RETURN_DUE_TO_CRASH
#define NO_RETURN_DUE_TO_ASSERT_WITH_SECURITY_IMPLICATION NO_RETURN_DUE_TO_CRASH

/* ASSERT_WITH_SECURITY_IMPLICATION
 
   Failure of this assertion indicates a possible security vulnerability.
   Class of vulnerabilities that it tests include bad casts, out of bounds
   accesses, use-after-frees, etc. Please file a bug using the security
   template - https://bugs.webkit.org/enter_bug.cgi?product=Security.
 
*/
#define ASSERT_WITH_SECURITY_IMPLICATION(assertion) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) { \
        WTFReportAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #assertion); \
        BACKTRACE(); \
        CRASH_WITH_SECURITY_IMPLICATION(); \
    } \
} while (0)

#define ASSERT_WITH_SECURITY_IMPLICATION_DISABLED 0
#endif /* ASSERT_ENABLED */

/* ASSERT_WITH_MESSAGE */

#if ASSERT_MSG_DISABLED
#define ASSERT_WITH_MESSAGE(assertion, ...) ((void)0)
#else
#define ASSERT_WITH_MESSAGE(assertion, ...) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) { \
        WTFReportAssertionFailureWithMessage(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #assertion, __VA_ARGS__); \
        BACKTRACE(); \
        CRASH(); \
    } \
} while (0)
#endif

#ifdef __cplusplus
constexpr bool assertionFailureDueToUnreachableCode = false;
#define ASSERT_NOT_REACHED_WITH_MESSAGE(...) ASSERT_WITH_MESSAGE(assertionFailureDueToUnreachableCode, __VA_ARGS__)
#endif

/* ASSERT_WITH_MESSAGE_UNUSED */

#if ASSERT_MSG_DISABLED
#define ASSERT_WITH_MESSAGE_UNUSED(variable, assertion, ...) ((void)variable)
#else
#define ASSERT_WITH_MESSAGE_UNUSED(variable, assertion, ...) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) { \
        WTFReportAssertionFailureWithMessage(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #assertion, __VA_ARGS__); \
        BACKTRACE(); \
        CRASH(); \
    } \
} while (0)
#endif
                        
                        
/* ASSERT_ARG */

#if ASSERT_ARG_DISABLED

#define ASSERT_ARG(argName, assertion) ((void)0)

#else

#define ASSERT_ARG(argName, assertion) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) { \
        WTFReportArgumentAssertionFailure(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #argName, #assertion); \
        BACKTRACE(); \
        CRASH(); \
    } \
} while (0)

#endif

/* COMPILE_ASSERT */
#ifndef COMPILE_ASSERT
#if COMPILER_SUPPORTS(C_STATIC_ASSERT)
/* Unlike static_assert below, this also works in plain C code. */
#define COMPILE_ASSERT(exp, name) _Static_assert((exp), #name)
#else
#define COMPILE_ASSERT(exp, name) static_assert((exp), #name)
#endif
#endif

#ifdef __cplusplus
namespace WTF {
template <typename T>
static constexpr bool unreachableForType = false;
template <auto v>
static constexpr bool unreachableForValue = false;
} // namespace WTF
// This could be used in a function template, or a member function of a class template.
// It will trigger in code that gets instantiated when it shouldn't, for example a template function invocation, or a constexpr-if/else branch that is actually taken.
// The 1st parameter TYPE or COMPILE_TIME_VALUE is necessary as part of delaying the assertion evaluation until instantiation, and that parameter will be visible in compiler errors.
// The 2nd parameter is an optional explanation string.
#define STATIC_ASSERT_NOT_REACHED_FOR_TYPE(TYPE, ...) do { \
    static_assert(WTF::unreachableForType<TYPE>, ##__VA_ARGS__); \
    CRASH(); \
} while (0)

#define STATIC_ASSERT_NOT_REACHED_FOR_VALUE(COMPILE_TIME_VALUE, ...) do { \
    static_assert(WTF::unreachableForValue<COMPILE_TIME_VALUE>, ##__VA_ARGS__); \
    CRASH(); \
} while (0)
#endif // __cplusplus

/* FATAL */

#if FATAL_DISABLED
#define FATAL(...) ((void)0)
#else
#define FATAL(...) do { \
    WTFReportFatalError(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, __VA_ARGS__); \
    BACKTRACE(); \
    CRASH(); \
} while (0)
#endif

/* LOG_ERROR */

#if ERROR_DISABLED
#define LOG_ERROR(...) ((void)0)
#define LOG_ERROR_ONCE(...) ((void)0)
#else
#define LOG_ERROR(...) WTFReportError(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, __VA_ARGS__)
#define LOG_ERROR_ONCE(...) do { \
    static std::once_flag onceFlag; \
    std::call_once( \
        onceFlag, \
        [&] { \
            LOG_ERROR(__VA_ARGS__); \
        }); \
} while (0)
#endif

/* LOG */

#if LOG_DISABLED
#define LOG(channel, ...) ((void)0)
#define LOG_ONCE(channel, ...) ((void)0)
#else
#define LOG(channel, ...) do { \
        if (LOG_CHANNEL(channel).state != logChannelStateOff) \
            WTFLog(&LOG_CHANNEL(channel), __VA_ARGS__); \
    } while (0)
#define LOG_ONCE(channel, ...) do { \
    static std::once_flag onceFlag; \
    std::call_once( \
        onceFlag, \
        [&] { \
            LOG(channel, __VA_ARGS__); \
        }); \
} while (0)
#endif

/* LOG_VERBOSE */

#if LOG_DISABLED
#define LOG_VERBOSE(channel, ...) ((void)0)
#else
#define LOG_VERBOSE(channel, ...) do { \
        if (LOG_CHANNEL(channel).state != logChannelStateOff) \
            WTFLogVerbose(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, &LOG_CHANNEL(channel), __VA_ARGS__); \
    } while (0)
#endif

/* LOG_WITH_LEVEL */

#if LOG_DISABLED
#define LOG_WITH_LEVEL(channel, logLevel, ...) ((void)0)
#else
#define LOG_WITH_LEVEL(channel, logLevel, ...) do { \
        if  (LOG_CHANNEL(channel).state != logChannelStateOff && LOG_CHANNEL(channel).level >= (logLevel)) \
            WTFLogWithLevel(&LOG_CHANNEL(channel), logLevel, __VA_ARGS__); \
    } while (0)
#endif

/* LOG_WITH_STREAM */

#if LOG_DISABLED
#define LOG_WITH_STREAM(channel, commands) ((void)0)
#else
#define LOG_WITH_STREAM(channel, commands) do { \
        if (LOG_CHANNEL(channel).state != logChannelStateOff) { \
            WTF::TextStream stream(WTF::TextStream::LineMode::SingleLine); \
            commands; \
            WTFLog(&LOG_CHANNEL(channel), "%s", stream.release().utf8().data()); \
        } \
    } while (0)
#endif

/* RELEASE_LOG */

#if RELEASE_LOG_DISABLED

#define PUBLIC_LOG_STRING "s"
#define PRIVATE_LOG_STRING "s"
#define SENSITIVE_LOG_STRING "s"
#define RELEASE_LOG(channel, ...) ((void)0)
#define RELEASE_LOG_ERROR(channel, ...) LOG_ERROR(__VA_ARGS__)
#define RELEASE_LOG_FAULT(channel, ...) LOG_ERROR(__VA_ARGS__)
#define RELEASE_LOG_INFO(channel, ...) ((void)0)
#define RELEASE_LOG_DEBUG(channel, ...) ((void)0)

#define RELEASE_LOG_IF(isAllowed, channel, ...) ((void)0)
#define RELEASE_LOG_ERROR_IF(isAllowed, channel, ...) do { if (UNLIKELY_FOR_C_ASSERTIONS(isAllowed)) RELEASE_LOG_ERROR(channel, __VA_ARGS__); } while (0)
#define RELEASE_LOG_INFO_IF(isAllowed, channel, ...) ((void)0)
#define RELEASE_LOG_DEBUG_IF(isAllowed, channel, ...) ((void)0)

#define RELEASE_LOG_WITH_LEVEL(channel, level, ...) ((void)0)
#define RELEASE_LOG_WITH_LEVEL_IF(isAllowed, channel, level, ...) do { if (isAllowed) RELEASE_LOG_WITH_LEVEL(channel, level, __VA_ARGS__); } while (0)

#elif USE(OS_LOG)

#define PUBLIC_LOG_STRING "{public}s"
#define PRIVATE_LOG_STRING "{private}s"
#define SENSITIVE_LOG_STRING "{sensitive}s"
#define RELEASE_LOG(channel, ...) SUPPRESS_UNCOUNTED_LOCAL os_log(LOG_CHANNEL(channel).osLogChannel, __VA_ARGS__)
#define RELEASE_LOG_ERROR(channel, ...) SUPPRESS_UNCOUNTED_LOCAL os_log_error(LOG_CHANNEL(channel).osLogChannel, __VA_ARGS__)
#define RELEASE_LOG_FAULT(channel, ...) SUPPRESS_UNCOUNTED_LOCAL os_log_fault(LOG_CHANNEL(channel).osLogChannel, __VA_ARGS__)
#define RELEASE_LOG_INFO(channel, ...) SUPPRESS_UNCOUNTED_LOCAL os_log_info(LOG_CHANNEL(channel).osLogChannel, __VA_ARGS__)
#define RELEASE_LOG_DEBUG(channel, ...) SUPPRESS_UNCOUNTED_LOCAL os_log_debug(LOG_CHANNEL(channel).osLogChannel, __VA_ARGS__)
#define RELEASE_LOG_WITH_LEVEL(channel, logLevel, ...) do { \
    if (LOG_CHANNEL(channel).level >= (logLevel)) \
        SUPPRESS_UNCOUNTED_LOCAL os_log(LOG_CHANNEL(channel).osLogChannel, __VA_ARGS__); \
} while (0)

#define RELEASE_LOG_WITH_LEVEL_IF(isAllowed, channel, logLevel, ...) do { \
    if ((isAllowed) && LOG_CHANNEL(channel).level >= (logLevel)) \
        SUPPRESS_UNCOUNTED_LOCAL os_log(LOG_CHANNEL(channel).osLogChannel, __VA_ARGS__); \
} while (0)

#elif OS(ANDROID)

#define PUBLIC_LOG_STRING "s"
#define PRIVATE_LOG_STRING "s"
#define SENSITIVE_LOG_STRING "s"

#define LOG_ANDROID_SEND(channel, priority, fmt, ...) do { \
    auto& logChannel = LOG_CHANNEL(channel); \
    if (logChannel.state != WTFLogChannelState::Off) \
        __android_log_print(ANDROID_LOG_ ## priority, LOG_CHANNEL_WEBKIT_SUBSYSTEM, "[%s] " fmt, logChannel.name, ##__VA_ARGS__); \
} while (0)

#define RELEASE_LOG(channel, ...) LOG_ANDROID_SEND(channel, VERBOSE, __VA_ARGS__)
#define RELEASE_LOG_ERROR(channel, ...) LOG_ANDROID_SEND(channel, ERROR, __VA_ARGS__)
#define RELEASE_LOG_FAULT(channel, ...) LOG_ANDROID_SEND(channel, FATAL, __VA_ARGS__)
#define RELEASE_LOG_INFO(channel, ...) LOG_ANDROID_SEND(channel, INFO, __VA_ARGS__)
#define RELEASE_LOG_DEBUG(channel, ...) LOG_ANDROID_SEND(channel, DEBUG, __VA_ARGS__)

#define RELEASE_LOG_WITH_LEVEL(channel, logLevel, ...) do { \
    if (LOG_CHANNEL(channel).level >= (logLevel)) \
        LOG_ANDROID_SEND(channel, VERBOSE, __VA_ARGS__); \
} while (0)

#define RELEASE_LOG_WITH_LEVEL_IF(isAllowed, channel, logLevel, ...) do { \
    if ((isAllowed) && LOG_CHANNEL(channel).level >= (logLevel)) \
        LOG_ANDROID_SEND(channel, VERBOSE, __VA_ARGS__); \
} while (0)

#elif ENABLE(JOURNALD_LOG)

#define PUBLIC_LOG_STRING "s"
#define PRIVATE_LOG_STRING "s"
#define SENSITIVE_LOG_STRING "s"
#define SD_JOURNAL_SEND(channel, priority, file, line, function, ...) do { \
    if (LOG_CHANNEL(channel).state != WTFLogChannelState::Off) \
        sd_journal_send_with_location("CODE_FILE=" file, "CODE_LINE=" line, function, "WEBKIT_SUBSYSTEM=" LOG_CHANNEL_WEBKIT_SUBSYSTEM, "WEBKIT_CHANNEL=%s", LOG_CHANNEL(channel).name, "PRIORITY=%i", priority, "MESSAGE=" __VA_ARGS__, nullptr); \
} while (0)

#define RELEASE_LOG(channel, ...) SD_JOURNAL_SEND(channel, LOG_NOTICE, __FILE__, _STRINGIFY(__LINE__), __func__, __VA_ARGS__)
#define RELEASE_LOG_ERROR(channel, ...) SD_JOURNAL_SEND(channel, LOG_ERR, __FILE__, _STRINGIFY(__LINE__), __func__, __VA_ARGS__)
#define RELEASE_LOG_FAULT(channel, ...) SD_JOURNAL_SEND(channel, LOG_CRIT, __FILE__, _STRINGIFY(__LINE__), __func__, __VA_ARGS__)
#define RELEASE_LOG_INFO(channel, ...) SD_JOURNAL_SEND(channel, LOG_INFO, __FILE__, _STRINGIFY(__LINE__), __func__, __VA_ARGS__)
#define RELEASE_LOG_DEBUG(channel, ...) SD_JOURNAL_SEND(channel, LOG_DEBUG, __FILE__, _STRINGIFY(__LINE__), __func__, __VA_ARGS__)

#define RELEASE_LOG_WITH_LEVEL(channel, logLevel, ...) do { \
    if (LOG_CHANNEL(channel).level >= (logLevel)) \
        SD_JOURNAL_SEND(channel, LOG_INFO, __FILE__, _STRINGIFY(__LINE__), __func__, __VA_ARGS__); \
} while (0)

#define RELEASE_LOG_WITH_LEVEL_IF(isAllowed, channel, logLevel, ...) do { \
    if ((isAllowed) && LOG_CHANNEL(channel).level >= (logLevel)) \
        SD_JOURNAL_SEND(channel, LOG_INFO, __FILE__, _STRINGIFY(__LINE__), __func__, __VA_ARGS__); \
} while (0)

#else

#define PUBLIC_LOG_STRING "s"
#define PRIVATE_LOG_STRING "s"
#define SENSITIVE_LOG_STRING "s"
#define LOGF(channel, priority, fmt, ...) do { \
    auto& logChannel = LOG_CHANNEL(channel); \
    if (logChannel.state != WTFLogChannelState::Off) \
        SAFE_FPRINTF(stderr, "[" LOG_CHANNEL_WEBKIT_SUBSYSTEM ":%s:%i] " fmt "\n", logChannel.name, priority, ##__VA_ARGS__); \
} while (0)

#define RELEASE_LOG(channel, ...) LOGF(channel, 4, __VA_ARGS__)
#define RELEASE_LOG_ERROR(channel, ...) LOGF(channel, 1, __VA_ARGS__)
#define RELEASE_LOG_FAULT(channel, ...) LOGF(channel, 2, __VA_ARGS__)
#define RELEASE_LOG_INFO(channel, ...) LOGF(channel, 3, __VA_ARGS__)
#define RELEASE_LOG_DEBUG(channel, ...) LOGF(channel, 4, __VA_ARGS__)

#define RELEASE_LOG_WITH_LEVEL(channel, logLevel, ...) do { \
    if (LOG_CHANNEL(channel).level >= (logLevel)) \
        LOGF(channel, logLevel, __VA_ARGS__); \
} while (0)

#define RELEASE_LOG_WITH_LEVEL_IF(isAllowed, channel, logLevel, ...) do { \
    if ((isAllowed) && LOG_CHANNEL(channel).level >= (logLevel)) \
        LOGF(channel, logLevel, __VA_ARGS__); \
} while (0)

#endif

#if !RELEASE_LOG_DISABLED
#define RELEASE_LOG_IF(isAllowed, channel, ...) do { if (isAllowed) RELEASE_LOG(channel, __VA_ARGS__); } while (0)
#define RELEASE_LOG_ERROR_IF(isAllowed, channel, ...) do { if (UNLIKELY_FOR_C_ASSERTIONS(isAllowed)) RELEASE_LOG_ERROR(channel, __VA_ARGS__); } while (0)
#define RELEASE_LOG_INFO_IF(isAllowed, channel, ...) do { if (UNLIKELY_FOR_C_ASSERTIONS(isAllowed)) RELEASE_LOG_INFO(channel, __VA_ARGS__); } while (0)
#define RELEASE_LOG_DEBUG_IF(isAllowed, channel, ...) do { if (UNLIKELY_FOR_C_ASSERTIONS(isAllowed)) RELEASE_LOG_DEBUG(channel, __VA_ARGS__); } while (0)
#endif

/* ALWAYS_LOG */

#define ALWAYS_LOG_WITH_STREAM(commands) do { \
        WTF::TextStream stream(WTF::TextStream::LineMode::SingleLine); \
        commands; \
        WTFLogAlways("%s", stream.release().utf8().data()); \
    } while (0)

#define WTF_ALWAYS_LOG(commands) do { \
        WTF::TextStream stream(WTF::TextStream::LineMode::SingleLine); \
        stream << commands; \
        WTFLogAlways("%s", stream.release().utf8().data()); \
    } while (0)

/* RELEASE_ASSERT */

#if !ASSERT_ENABLED

#define RELEASE_ASSERT(assertion, ...) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) \
        CRASH_WITH_INFO(__VA_ARGS__); \
} while (0)
#define RELEASE_ASSERT_WITH_MESSAGE(assertion, ...) RELEASE_ASSERT(assertion)
#define RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(assertion) RELEASE_ASSERT(assertion)
#define RELEASE_ASSERT_NOT_REACHED(...) CRASH_WITH_INFO(__VA_ARGS__)
#define RELEASE_ASSERT_NOT_REACHED_UNDER_CONSTEXPR_CONTEXT() CRASH_UNDER_CONSTEXPR_CONTEXT();
#define RELEASE_ASSERT_UNDER_CONSTEXPR_CONTEXT(assertion) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS(!(assertion))) { \
        CRASH_UNDER_CONSTEXPR_CONTEXT(); \
    } \
} while (0)
#define RELEASE_ASSERT_IMPLIES(condition, assertion) do { \
    if (UNLIKELY_FOR_C_ASSERTIONS((condition) && !(assertion))) { \
        CRASH(); \
    } \
} while (0)

#else /* ASSERT_ENABLED */

#define RELEASE_ASSERT(assertion, ...) ASSERT(assertion, __VA_ARGS__)
#define RELEASE_ASSERT_WITH_MESSAGE(assertion, ...) ASSERT_WITH_MESSAGE(assertion, __VA_ARGS__)
#define RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(assertion) ASSERT_WITH_SECURITY_IMPLICATION(assertion)
#define RELEASE_ASSERT_NOT_REACHED(...) ASSERT_NOT_REACHED(__VA_ARGS__)
#define RELEASE_ASSERT_NOT_REACHED_UNDER_CONSTEXPR_CONTEXT() ASSERT_NOT_REACHED_UNDER_CONSTEXPR_CONTEXT()
#define RELEASE_ASSERT_UNDER_CONSTEXPR_CONTEXT(assertion) ASSERT_UNDER_CONSTEXPR_CONTEXT(assertion)
#define RELEASE_ASSERT_IMPLIES(condition, assertion) ASSERT_IMPLIES(condition, assertion)

#endif /* ASSERT_ENABLED */

/* CONJECTURE_ASSERT is only used to facilitate on-going analysis work to test conjectures
   about the code. We want to be able to land these in the code base for some time to enable
   extended testing.

   If the conjecture is proven false, then the CONJECTURE_ASSERT should either be removed or
   updated to test a new conjecture. If the conjecture is proven true, the CONJECTURE_ASSERT
   should either be promoted to an ASSERT or RELEASE_ASSERT as appropriate, or removed if
   deemed of low value.

   The number of CONJECTURE_ASSERTs should not be growing unboundedly, and they should not
   stay in the codebase perpetually.

   There is no EWS coverage for CONJECTURE_ASSERTs. So, if you add one, you are responsible
   for making sure it builds with ENABLE_CONJECTURE_ASSERT set to 1, and for running tests on
   your build to make sure that the assertion is not immediately failing.

   To run with CONJECTURE_ASSERTs enabled, you also need to define the environmental variable
   ENABLE_WEBKIT_CONJECTURE_ASSERT. Otherwise, the assertion will not be tested.
*/
#if ENABLE(CONJECTURE_ASSERT)
#define CONJECTURE_ASSERT(assertion, ...) do { \
        if (UNLIKELY_FOR_C_ASSERTIONS(wtfConjectureAssertIsEnabled && !(assertion))) \
            WTFCrashDueToConjectureAssert(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #assertion); \
    } while (false)
#define CONJECTURE_ASSERT_IMPLIES(condition, assertion)  do { \
        if (UNLIKELY_FOR_C_ASSERTIONS(wtfConjectureAssertIsEnabled && (condition) && !(assertion))) \
            WTFCrashDueToConjectureAssert(__FILE__, __LINE__, WTF_PRETTY_FUNCTION, #assertion); \
    } while (false)
#else
#define CONJECTURE_ASSERT(assertion, ...)
#define CONJECTURE_ASSERT_IMPLIES(condition, assertion)
#endif

#ifdef __cplusplus
#define RELEASE_ASSERT_NOT_REACHED_WITH_MESSAGE(...) RELEASE_ASSERT_WITH_MESSAGE(assertionFailureDueToUnreachableCode, __VA_ARGS__)

// The combination of line, file, function, and counter should be a unique number per call to this crash. This tricks the compiler into not coalescing calls to WTFCrashWithInfo.
// The easiest way to fill these values per translation unit is to pass __LINE__, __FILE__, WTF_PRETTY_FUNCTION, and __COUNTER__.
WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH NOT_TAIL_CALLED void WTFCrashWithInfoImpl(int line, const char* file, const char* function, int counter, uint64_t reason, uint64_t misc1, uint64_t misc2, uint64_t misc3, uint64_t misc4, uint64_t misc5, uint64_t misc6);
WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH NOT_TAIL_CALLED void WTFCrashWithInfoImpl(int line, const char* file, const char* function, int counter, uint64_t reason, uint64_t misc1, uint64_t misc2, uint64_t misc3, uint64_t misc4, uint64_t misc5);
WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH NOT_TAIL_CALLED void WTFCrashWithInfoImpl(int line, const char* file, const char* function, int counter, uint64_t reason, uint64_t misc1, uint64_t misc2, uint64_t misc3, uint64_t misc4);
WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH NOT_TAIL_CALLED void WTFCrashWithInfoImpl(int line, const char* file, const char* function, int counter, uint64_t reason, uint64_t misc1, uint64_t misc2, uint64_t misc3);
WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH NOT_TAIL_CALLED void WTFCrashWithInfoImpl(int line, const char* file, const char* function, int counter, uint64_t reason, uint64_t misc1, uint64_t misc2);
WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH NOT_TAIL_CALLED void WTFCrashWithInfoImpl(int line, const char* file, const char* function, int counter, uint64_t reason, uint64_t misc1);
WTF_EXPORT_PRIVATE NO_RETURN_DUE_TO_CRASH NOT_TAIL_CALLED void WTFCrashWithInfoImpl(int line, const char* file, const char* function, int counter, uint64_t reason);
#if !ASAN_ENABLED && (OS(DARWIN) || PLATFORM(PLAYSTATION)) && (CPU(X86_64) || CPU(ARM64))
NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter);
#else
NO_RETURN_DUE_TO_CRASH NOT_TAIL_CALLED void WTFCrashWithInfo(int line, const char* file, const char* function, int counter);
#endif

template<typename T>
ALWAYS_INLINE uint64_t wtfCrashArg(T* arg) { return reinterpret_cast<uintptr_t>(arg); }

template<typename T>
ALWAYS_INLINE uint64_t wtfCrashArg(T arg) { return static_cast<uint64_t>(arg); }

template<typename T>
NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter, T reason)
{
    WTFCrashWithInfoImpl(line, file, function, counter, wtfCrashArg(reason));
}

template<typename T, typename U>
NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter, T reason, U misc1)
{
    WTFCrashWithInfoImpl(line, file, function, counter, wtfCrashArg(reason), wtfCrashArg(misc1));
}

template<typename T, typename U, typename V>
NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter, T reason, U misc1, V misc2)
{
    WTFCrashWithInfoImpl(line, file, function, counter, wtfCrashArg(reason), wtfCrashArg(misc1), wtfCrashArg(misc2));
}

template<typename T, typename U, typename V, typename W>
NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter, T reason, U misc1, V misc2, W misc3)
{
    WTFCrashWithInfoImpl(line, file, function, counter, wtfCrashArg(reason), wtfCrashArg(misc1), wtfCrashArg(misc2), wtfCrashArg(misc3));
}

template<typename T, typename U, typename V, typename W, typename X>
NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter, T reason, U misc1, V misc2, W misc3, X misc4)
{
    WTFCrashWithInfoImpl(line, file, function, counter, wtfCrashArg(reason), wtfCrashArg(misc1), wtfCrashArg(misc2), wtfCrashArg(misc3), wtfCrashArg(misc4));
}

template<typename T, typename U, typename V, typename W, typename X, typename Y>
NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter, T reason, U misc1, V misc2, W misc3, X misc4, Y misc5)
{
    WTFCrashWithInfoImpl(line, file, function, counter, wtfCrashArg(reason), wtfCrashArg(misc1), wtfCrashArg(misc2), wtfCrashArg(misc3), wtfCrashArg(misc4), wtfCrashArg(misc5));
}

template<typename T, typename U, typename V, typename W, typename X, typename Y, typename Z>
NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter, T reason, U misc1, V misc2, W misc3, X misc4, Y misc5, Z misc6)
{
    WTFCrashWithInfoImpl(line, file, function, counter, wtfCrashArg(reason), wtfCrashArg(misc1), wtfCrashArg(misc2), wtfCrashArg(misc3), wtfCrashArg(misc4), wtfCrashArg(misc5), wtfCrashArg(misc6));
}

#if !ASAN_ENABLED && (OS(DARWIN) || PLATFORM(PLAYSTATION)) && (CPU(X86_64) || CPU(ARM64))

NO_RETURN_DUE_TO_CRASH ALWAYS_INLINE void WTFCrashWithInfo(int line, const char* file, const char* function, int counter)
{
    uint64_t x0Value = static_cast<uint64_t>(static_cast<int64_t>(line));
    uint64_t x1Value = reinterpret_cast<uintptr_t>(file);
    uint64_t x2Value = reinterpret_cast<uintptr_t>(function);
    uint64_t x3Value = static_cast<uint64_t>(static_cast<int64_t>(counter));
    register uint64_t x0GPR asm(CRASH_ARG_GPR0) = x0Value;
    register uint64_t x1GPR asm(CRASH_ARG_GPR1) = x1Value;
    register uint64_t x2GPR asm(CRASH_ARG_GPR2) = x2Value;
    register uint64_t x3GPR asm(CRASH_ARG_GPR3) = x3Value;
    __asm__ volatile (WTF_FATAL_CRASH_INST : : "r"(x0GPR), "r"(x1GPR), "r"(x2GPR), "r"(x3GPR));
    __builtin_unreachable();
}

#else

inline void WTFCrashWithInfo(int, const char*, const char*, int)
#if COMPILER(CLANG)
    __attribute__((optnone))
#endif
{
    CRASH();
}

#endif

namespace WTF {
inline void isIntegralOrPointerType() { }

template<typename T, typename... Types>
void isIntegralOrPointerType(T, Types... types)
{
    static_assert(std::is_integral<T>::value || std::is_enum<T>::value || std::is_pointer<T>::value, "All types need to be std::bit_cast-able to integral type for logging");
    isIntegralOrPointerType(types...);
}

#if PLATFORM(COCOA) || OS(ANDROID)
WTF_EXPORT_PRIVATE void disableForwardingVPrintfStdErrToOSLog();
#endif

} // namespace WTF

inline void compilerFenceForCrash()
{
    asm volatile("" ::: "memory");
}

#ifndef CRASH_WITH_INFO

#define PP_THIRD_ARG(a,b,c,...) c
#define VA_OPT_SUPPORTED_I(...) PP_THIRD_ARG(__VA_OPT__(,),true,false,)
#define VA_OPT_SUPPORTED VA_OPT_SUPPORTED_I(?)

// This is useful if you are going to stuff data into registers before crashing, like the
// crashWithInfo functions below.
#if !VA_OPT_SUPPORTED
#define CRASH_WITH_INFO(...) do { \
        WTF::isIntegralOrPointerType(__VA_ARGS__); \
        compilerFenceForCrash(); \
        WTFCrashWithInfo(__LINE__, __FILE__, WTF_PRETTY_FUNCTION, __COUNTER__, ##__VA_ARGS__); \
    } while (false)
#else
#define CRASH_WITH_INFO(...) do { \
        WTF::isIntegralOrPointerType(__VA_ARGS__); \
        compilerFenceForCrash(); \
        WTFCrashWithInfo(__LINE__, __FILE__, WTF_PRETTY_FUNCTION, __COUNTER__ __VA_OPT__(,) __VA_ARGS__); \
    } while (false)
#endif
#endif // CRASH_WITH_INFO

#ifndef CRASH_WITH_SECURITY_IMPLICATION_AND_INFO
#define CRASH_WITH_SECURITY_IMPLICATION_AND_INFO CRASH_WITH_INFO
#endif // CRASH_WITH_SECURITY_IMPLICATION_AND_INFO

#else /* not __cplusplus */

#ifndef CRASH_WITH_INFO
#define CRASH_WITH_INFO() CRASH()
#endif

#endif /* __cplusplus */

#if OS(DARWIN)
#define CRASH_WITH_EXTRA_SECURITY_IMPLICATION_AND_INFO(abortReason, abortMsg, ...) do { \
        if (g_wtfConfig.useSpecialAbortForExtraSecurityImplications) \
            abort_with_reason(OS_REASON_WEBKIT, abortReason, abortMsg, OS_REASON_FLAG_SECURITY_SENSITIVE); \
        CRASH_WITH_INFO(__VA_ARGS__); \
    } while (false)
#else
#define CRASH_WITH_EXTRA_SECURITY_IMPLICATION_AND_INFO(abortReason, abortMsg, ...) CRASH_WITH_INFO(__VA_ARGS__)
#endif

/* UNREACHABLE_FOR_PLATFORM */

#if COMPILER(CLANG)
// This would be a macro except that its use of #pragma works best around
// a function. Hence it uses macro naming convention.
IGNORE_WARNINGS_BEGIN("missing-noreturn")
static inline void UNREACHABLE_FOR_PLATFORM(void)
{
    // This *MUST* be a release assert. We use it in places where it's better to crash than to keep
    // going.
    RELEASE_ASSERT_NOT_REACHED();
}
IGNORE_WARNINGS_END
#else
#define UNREACHABLE_FOR_PLATFORM() RELEASE_ASSERT_NOT_REACHED()
#endif
