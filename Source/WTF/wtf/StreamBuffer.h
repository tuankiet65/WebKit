/*
* Copyright (C) 2012 Google Inc. All rights reserved.
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
*/

#pragma once

#include <wtf/Deque.h>
#include <wtf/text/ParsingUtilities.h>

namespace WTF {

template <typename T, size_t BlockSize>
class StreamBuffer {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(StreamBuffer);
private:
    typedef Vector<T> Block;
public:
    StreamBuffer()
        : m_size(0)
        , m_readOffset(0)
    {
    }

    ~StreamBuffer()
    {
    }

    bool isEmpty() const { return !size(); }

    void append(std::span<const T> data)
    {
        if (!data.size())
            return;

        m_size += data.size();
        while (data.size()) {
            if (!m_buffer.size() || m_buffer.last()->size() == BlockSize)
                m_buffer.append(makeUnique<Block>());
            size_t appendSize = std::min(BlockSize - m_buffer.last()->size(), data.size());
            m_buffer.last()->append(consumeSpan(data, appendSize));
        }
    }

    void append(const T* data, size_t size) { append(std::span { data, size }); }

    // This function consume data in the fist block.
    // Specified size must be less than over equal to firstBlockSize().
    void consume(size_t size)
    {
        ASSERT(m_size >= size);
        if (!m_size)
            return;

        ASSERT(m_buffer.size() > 0);
        ASSERT(m_readOffset + size <= m_buffer.first()->size());
        m_readOffset += size;
        m_size -= size;
        if (m_readOffset >= m_buffer.first()->size()) {
            m_readOffset = 0;
            m_buffer.removeFirst();
        }
    }

    size_t size() const { return m_size; }

    const T* firstBlockData() const
    {
        if (!m_size)
            return 0;
        ASSERT(m_buffer.size() > 0);
        return &m_buffer.first()->at(m_readOffset);
    }

    size_t firstBlockSize() const
    {
        if (!m_size)
            return 0;
        ASSERT(m_buffer.size() > 0);
        return m_buffer.first()->size() - m_readOffset;
    }

    std::span<const T> firstBlockSpan() const { return std::span { firstBlockData(), firstBlockSize() }; }

private:
    size_t m_size;
    size_t m_readOffset;
    Deque<std::unique_ptr<Block>> m_buffer;
};

} // namespace WTF

using WTF::StreamBuffer;
