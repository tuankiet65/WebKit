/*
 * Copyright (C) 2011 University of Szeged
 * Copyright (C) 2011 Gabor Loki <loki@webkit.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY UNIVERSITY OF SZEGED ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL UNIVERSITY OF SZEGED OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ParallelJobsGeneric_h
#define ParallelJobsGeneric_h

#if ENABLE(THREADING_GENERIC)

#include <wtf/Condition.h>
#include <wtf/Lock.h>
#include <wtf/RefCounted.h>
#include <wtf/Threading.h>

namespace WTF {

class ParallelEnvironment {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(ParallelEnvironment);
public:
    typedef void (*ThreadFunction)(void*);

    WTF_EXPORT_PRIVATE ParallelEnvironment(ThreadFunction, size_t sizeOfParameter, int requestedJobNumber);

    int numberOfJobs()
    {
        return m_numberOfJobs;
    }

    WTF_EXPORT_PRIVATE void execute(void* parameters);

    class ThreadPrivate : public RefCounted<ThreadPrivate> {
    public:
        bool tryLockFor(ParallelEnvironment*);

        void execute(ThreadFunction, void*);

        void waitForFinish();

        static Ref<ThreadPrivate> create()
        {
            return adoptRef(*new ThreadPrivate());
        }

    private:
        mutable Lock m_lock;
        Condition m_threadCondition;

        RefPtr<Thread> m_thread;
        bool m_running { false };
        ParallelEnvironment* m_parent WTF_GUARDED_BY_LOCK(m_lock) { nullptr };

        ThreadFunction m_threadFunction { nullptr };
        void* m_parameters { nullptr };
    };

private:
    ThreadFunction m_threadFunction;
    size_t m_sizeOfParameter;
    int m_numberOfJobs;

    Vector< RefPtr<ThreadPrivate> > m_threads;
    static Vector< RefPtr<ThreadPrivate> >* s_threadPool;
};

} // namespace WTF

#endif // ENABLE(THREADING_GENERIC)


#endif // ParallelJobsGeneric_h
