/*
 * Copyright (C) 2008-2024 Apple Inc. All rights reserved.
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
 *
 */

#include "config.h"
#include "ActiveDOMObject.h"

#include "ContextDestructionObserverInlines.h"
#include "Document.h"
#include "Event.h"
#include "EventLoop.h"
#include "EventTargetInlines.h"
#include "ScriptExecutionContextInlines.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

static inline ScriptExecutionContext* suitableScriptExecutionContext(ScriptExecutionContext* scriptExecutionContext)
{
    // For detached documents, make sure we observe their context document instead.
    if (auto* document = dynamicDowncast<Document>(scriptExecutionContext))
        return &document->contextDocument();
    return scriptExecutionContext;
}

inline ActiveDOMObject::ActiveDOMObject(ScriptExecutionContext* context, CheckedScriptExecutionContextType)
    : ContextDestructionObserver(context)
{
    ASSERT(!is<Document>(context) || &downcast<Document>(context)->contextDocument() == downcast<Document>(context));
    if (!context)
        return;

    ASSERT(context->isContextThread());
    context->didCreateActiveDOMObject(*this);
}

ActiveDOMObject::ActiveDOMObject(ScriptExecutionContext* scriptExecutionContext)
    : ActiveDOMObject(suitableScriptExecutionContext(scriptExecutionContext), CheckedScriptExecutionContext)
{
}

ActiveDOMObject::ActiveDOMObject(Document* document)
    : ActiveDOMObject(document ? &document->contextDocument() : nullptr, CheckedScriptExecutionContext)
{
}

ActiveDOMObject::ActiveDOMObject(Document& document)
    : ActiveDOMObject(&document.contextDocument(), CheckedScriptExecutionContext)
{
}

ActiveDOMObject::~ActiveDOMObject()
{
    ASSERT(canCurrentThreadAccessThreadLocalData(m_creationThread));

    // ActiveDOMObject may be inherited by a sub-class whose life-cycle
    // exceeds that of the associated ScriptExecutionContext. In those cases,
    // m_scriptExecutionContext would/should have been nullified by
    // ContextDestructionObserver::contextDestroyed() (which we implement /
    // inherit). Hence, we should ensure that this is not 0 before use it
    // here.

    RefPtr<ScriptExecutionContext> context = scriptExecutionContext();
    if (!context)
        return;

    ASSERT(m_suspendIfNeededWasCalled);
    ASSERT(context->isContextThread());
    context->willDestroyActiveDOMObject(*this);
}

void ActiveDOMObject::suspendIfNeeded()
{
#if ASSERT_ENABLED
    ASSERT(!m_suspendIfNeededWasCalled);
    m_suspendIfNeededWasCalled = true;
#endif
    if (RefPtr<ScriptExecutionContext> context = scriptExecutionContext())
        context->suspendActiveDOMObjectIfNeeded(*this);
}

#if ASSERT_ENABLED

void ActiveDOMObject::assertSuspendIfNeededWasCalled() const
{
    ASSERT(m_suspendIfNeededWasCalled);
}

#endif // ASSERT_ENABLED

void ActiveDOMObject::didMoveToNewDocument(Document& newDocument)
{
    if (RefPtr context = scriptExecutionContext())
        context->willDestroyActiveDOMObject(*this);
    Ref newScriptExecutionContext = newDocument.contextDocument();
    observeContext(newScriptExecutionContext.ptr());
    newScriptExecutionContext->didCreateActiveDOMObject(*this);
}

void ActiveDOMObject::suspend(ReasonForSuspension)
{
}

void ActiveDOMObject::resume()
{
}

void ActiveDOMObject::stop()
{
}

bool ActiveDOMObject::isContextStopped() const
{
    return !scriptExecutionContext() || scriptExecutionContext()->activeDOMObjectsAreStopped();
}

bool ActiveDOMObject::isAllowedToRunScript() const
{
    return scriptExecutionContext() && !scriptExecutionContext()->activeDOMObjectsAreStopped() && !scriptExecutionContext()->activeDOMObjectsAreSuspended();
}

void ActiveDOMObject::queueTaskInEventLoop(TaskSource source, Function<void ()>&& function)
{
    RefPtr context = scriptExecutionContext();
    if (!context)
        return;
    context->checkedEventLoop()->queueTask(source, WTFMove(function));
}

class ActiveDOMObjectEventDispatchTask : public EventLoopTask {
    WTF_MAKE_TZONE_ALLOCATED(ActiveDOMObjectEventDispatchTask);
public:
    ActiveDOMObjectEventDispatchTask(TaskSource source, EventLoopTaskGroup& group, ActiveDOMObject& object, Function<void()>&& dispatchEvent)
        : EventLoopTask(source, group)
        , m_object(object)
        , m_dispatchEvent(WTFMove(dispatchEvent))
    {
        ++m_object->m_pendingActivityInstanceCount;
    }

    ~ActiveDOMObjectEventDispatchTask()
    {
        ASSERT(m_object->m_pendingActivityInstanceCount);
        --m_object->m_pendingActivityInstanceCount;
    }

    void execute() final
    {
        // If this task executes after the script execution context has been stopped, don't
        // actually dispatch the event.
        if (m_object->isAllowedToRunScript())
            m_dispatchEvent();
    }

private:
    const Ref<ActiveDOMObject> m_object;
    Function<void()> m_dispatchEvent;
};

WTF_MAKE_TZONE_ALLOCATED_IMPL(ActiveDOMObjectEventDispatchTask);

void ActiveDOMObject::queueTaskToDispatchEventInternal(EventTarget& target, TaskSource source, Ref<Event>&& event)
{
    ASSERT(!event->target() || &target == event->target());
    RefPtr context = scriptExecutionContext();
    if (!context)
        return;
    auto& eventLoopTaskGroup = context->eventLoop();
    auto task = makeUnique<ActiveDOMObjectEventDispatchTask>(source, eventLoopTaskGroup, *this, [target = Ref { target }, event = WTFMove(event)] {
        target->dispatchEvent(event);
    });
    eventLoopTaskGroup.queueTask(WTFMove(task));
}

void ActiveDOMObject::queueCancellableTaskToDispatchEventInternal(EventTarget& target, TaskSource source, TaskCancellationGroup& cancellationGroup, Ref<Event>&& event)
{
    ASSERT(!event->target() || &target == event->target());
    RefPtr context = scriptExecutionContext();
    if (!context)
        return;
    auto& eventLoopTaskGroup = context->eventLoop();
    auto task = makeUnique<ActiveDOMObjectEventDispatchTask>(source, eventLoopTaskGroup, *this, CancellableTask(cancellationGroup, [target = Ref { target }, event = WTFMove(event)] {
        target->dispatchEvent(event);
    }));
    eventLoopTaskGroup.queueTask(WTFMove(task));
}

} // namespace WebCore
