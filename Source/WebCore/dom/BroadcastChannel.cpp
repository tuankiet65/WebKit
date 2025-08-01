/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
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
#include "BroadcastChannel.h"

#include "BroadcastChannelRegistry.h"
#include "Document.h"
#include "DocumentInlines.h"
#include "EventNames.h"
#include "EventTargetInlines.h"
#include "ExceptionOr.h"
#include "MessageEvent.h"
#include "Page.h"
#include "PartitionedSecurityOrigin.h"
#include "SecurityOrigin.h"
#include "SerializedScriptValue.h"
#include "WorkerGlobalScope.h"
#include "WorkerLoaderProxy.h"
#include "WorkerThread.h"
#include <wtf/CallbackAggregator.h>
#include <wtf/HashMap.h>
#include <wtf/Identified.h>
#include <wtf/MainThread.h>
#include <wtf/Scope.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(BroadcastChannel);

static Lock allBroadcastChannelsLock;
static HashMap<BroadcastChannelIdentifier, ThreadSafeWeakPtr<BroadcastChannel>>& allBroadcastChannels() WTF_REQUIRES_LOCK(allBroadcastChannelsLock)
{
    static NeverDestroyed<HashMap<BroadcastChannelIdentifier, ThreadSafeWeakPtr<BroadcastChannel>>> map;
    return map;
}

static HashMap<BroadcastChannelIdentifier, ScriptExecutionContextIdentifier>& channelToContextIdentifier()
{
    ASSERT(isMainThread());
    static NeverDestroyed<HashMap<BroadcastChannelIdentifier, ScriptExecutionContextIdentifier>> map;
    return map;
}

static PartitionedSecurityOrigin partitionedSecurityOriginFromContext(ScriptExecutionContext& context)
{
    return { context.topOrigin(), context.protectedSecurityOrigin().releaseNonNull() };
}

class BroadcastChannel::MainThreadBridge : public ThreadSafeRefCounted<MainThreadBridge, WTF::DestructionThread::Main>, public Identified<BroadcastChannelIdentifier> {
public:
    static Ref<MainThreadBridge> create(BroadcastChannel& channel, const String& name)
    {
        return adoptRef(*new MainThreadBridge(channel, name));
    }

    void registerChannel();
    void unregisterChannel();
    void postMessage(Ref<SerializedScriptValue>&&);
    void detach() { m_broadcastChannel = nullptr; }

    String name() const { return m_name.isolatedCopy(); }

private:
    MainThreadBridge(BroadcastChannel&, const String& name);

    void ensureOnMainThread(Function<void(Page*)>&&);

    WeakPtr<BroadcastChannel, WeakPtrImplWithEventTargetData> m_broadcastChannel;
    const String m_name; // Main thread only.
    PartitionedSecurityOrigin m_origin; // Main thread only.
};

BroadcastChannel::MainThreadBridge::MainThreadBridge(BroadcastChannel& channel, const String& name)
    : m_broadcastChannel(channel)
    , m_name(name.isolatedCopy())
    , m_origin(partitionedSecurityOriginFromContext(*channel.protectedScriptExecutionContext()).isolatedCopy())
{
}

void BroadcastChannel::MainThreadBridge::ensureOnMainThread(Function<void(Page*)>&& task)
{
    ASSERT(m_broadcastChannel);
    if (!m_broadcastChannel)
        return;

    RefPtr context = m_broadcastChannel->scriptExecutionContext();
    if (!context)
        return;
    ASSERT(context->isContextThread());

    if (RefPtr document = dynamicDowncast<Document>(*context)) {
        task(document->protectedPage().get());
        return;
    }

    CheckedPtr workerLoaderProxy = downcast<WorkerGlobalScope>(*context).thread().workerLoaderProxy();
    if (!workerLoaderProxy)
        return;

    workerLoaderProxy->postTaskToLoader([task = WTFMove(task)](auto& context) {
        task(downcast<Document>(context).protectedPage().get());
    });
}

void BroadcastChannel::MainThreadBridge::registerChannel()
{
    ensureOnMainThread([this, contextIdentifier = m_broadcastChannel->scriptExecutionContext()->identifier()](auto* page) mutable {
        if (page)
            page->protectedBroadcastChannelRegistry()->registerChannel(m_origin, m_name, identifier());
        channelToContextIdentifier().add(identifier(), contextIdentifier);
    });
}

void BroadcastChannel::MainThreadBridge::unregisterChannel()
{
    ensureOnMainThread([this](auto* page) {
        if (page)
            page->protectedBroadcastChannelRegistry()->unregisterChannel(m_origin, m_name, identifier());
        channelToContextIdentifier().remove(identifier());
    });
}

void BroadcastChannel::MainThreadBridge::postMessage(Ref<SerializedScriptValue>&& message)
{
    ensureOnMainThread([this, message = WTFMove(message)](auto* page) mutable {
        if (!page)
            return;

        auto blobHandles = message->blobHandles();
        page->protectedBroadcastChannelRegistry()->postMessage(m_origin, m_name, identifier(), WTFMove(message), [blobHandles = WTFMove(blobHandles)] {
            // Keeps Blob data inside messageData alive until the message has been delivered.
        });
    });
}

BroadcastChannel::BroadcastChannel(ScriptExecutionContext& context, const String& name)
    : ActiveDOMObject(&context)
    , m_mainThreadBridge(MainThreadBridge::create(*this, name))
{
    Ref mainThreadBridge = m_mainThreadBridge;
    {
        Locker locker { allBroadcastChannelsLock };
        allBroadcastChannels().add(mainThreadBridge->identifier(), *this);
    }
    mainThreadBridge->registerChannel();
}

BroadcastChannel::~BroadcastChannel()
{
    close();
    m_mainThreadBridge->detach();
    {
        Locker locker { allBroadcastChannelsLock };
        allBroadcastChannels().remove(m_mainThreadBridge->identifier());
    }
}

BroadcastChannelIdentifier BroadcastChannel::identifier() const
{
    return m_mainThreadBridge->identifier();
}

String BroadcastChannel::name() const
{
    return m_mainThreadBridge->name();
}

ExceptionOr<void> BroadcastChannel::postMessage(JSC::JSGlobalObject& globalObject, JSC::JSValue message)
{
    if (!isEligibleForMessaging())
        return { };

    if (m_isClosed)
        return Exception { ExceptionCode::InvalidStateError, "This BroadcastChannel is closed"_s };

    Vector<Ref<MessagePort>> ports;
    auto messageData = SerializedScriptValue::create(globalObject, message, { }, ports, SerializationForStorage::No, SerializationContext::WorkerPostMessage);
    if (messageData.hasException())
        return messageData.releaseException();
    ASSERT(ports.isEmpty());

    m_mainThreadBridge->postMessage(messageData.releaseReturnValue());
    return { };
}

void BroadcastChannel::close()
{
    if (m_isClosed)
        return;

    m_isClosed = true;
    m_mainThreadBridge->unregisterChannel();
}

void BroadcastChannel::dispatchMessageTo(BroadcastChannelIdentifier channelIdentifier, Ref<SerializedScriptValue>&& message, CompletionHandler<void()>&& completionHandler)
{
    ASSERT(isMainThread());
    auto completionHandlerCallingScope = makeScopeExit([completionHandler = WTFMove(completionHandler)]() mutable {
        callOnMainThread(WTFMove(completionHandler));
    });

    auto contextIdentifier = channelToContextIdentifier().get(channelIdentifier);
    if (!contextIdentifier)
        return;

    ScriptExecutionContext::ensureOnContextThread(contextIdentifier, [channelIdentifier, message = WTFMove(message), completionHandlerCallingScope = WTFMove(completionHandlerCallingScope)](auto&) mutable {
        RefPtr<BroadcastChannel> channel;
        {
            Locker locker { allBroadcastChannelsLock };
            channel = allBroadcastChannels().get(channelIdentifier).get();
        }
        if (channel)
            channel->dispatchMessage(WTFMove(message));
    });
}

void BroadcastChannel::dispatchMessage(Ref<SerializedScriptValue>&& message)
{
    if (!isEligibleForMessaging())
        return;

    if (m_isClosed)
        return;

    queueTaskKeepingObjectAlive(*this, TaskSource::PostedMessageQueue, [message = WTFMove(message)](auto& channel) mutable {
        if (channel.m_isClosed || !channel.scriptExecutionContext())
            return;

        auto* globalObject = channel.scriptExecutionContext()->globalObject();
        if (!globalObject)
            return;

        auto& vm = globalObject->vm();
        auto scope = DECLARE_CATCH_SCOPE(vm);
        auto event = MessageEvent::create(*globalObject, WTFMove(message), channel.scriptExecutionContext()->securityOrigin()->toString());
        if (scope.exception()) [[unlikely]] {
            // Currently, we assume that the only way we can get here is if we have a termination.
            RELEASE_ASSERT(vm.hasPendingTerminationException());
            return;
        }

        channel.dispatchEvent(event.event);
    });
}

void BroadcastChannel::eventListenersDidChange()
{
    m_hasRelevantEventListener = hasEventListeners(eventNames().messageEvent);
}

bool BroadcastChannel::virtualHasPendingActivity() const
{
    return !m_isClosed && m_hasRelevantEventListener;
}

// https://html.spec.whatwg.org/#eligible-for-messaging
bool BroadcastChannel::isEligibleForMessaging() const
{
    RefPtr context = scriptExecutionContext();
    if (!context)
        return false;

    if (RefPtr document = dynamicDowncast<Document>(*context))
        return document->isFullyActive();

    return !downcast<WorkerGlobalScope>(*context).isClosing();
}

} // namespace WebCore
