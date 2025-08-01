/*
 * Copyright (C) 2005, 2006, 2008, 2013, 2016 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <initializer_list>
#include <wtf/Assertions.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/Vector.h>

namespace WTF {

template<typename Value, typename HashFunctions, typename Traits>
class HashCountedSet final {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(HashCountedSet);
private:
    using ImplType = HashMap<Value, unsigned, HashFunctions, Traits>;
public:
    using ValueType = Value;
    using iterator = typename ImplType::iterator;
    using const_iterator = typename ImplType::const_iterator;
    using ValuesIteratorRange = typename ImplType::KeysIteratorRange;
    using ValuesConstIteratorRange = typename ImplType::KeysConstIteratorRange;
    using AddResult = typename ImplType::AddResult;

    HashCountedSet()
    {
    }

    HashCountedSet(std::initializer_list<typename ImplType::KeyValuePairType> initializerList)
    {
        for (const auto& keyValuePair : initializerList)
            add(keyValuePair.key, keyValuePair.value);
    }

    HashCountedSet(std::initializer_list<typename ImplType::KeyType> initializerList)
    {
        for (const auto& value : initializerList)
            add(value);
    }
    
    void swap(HashCountedSet&);
    
    unsigned size() const;
    unsigned capacity() const;
    bool isEmpty() const;
    
    // Iterators iterate over pairs of values and counts.
    iterator begin() LIFETIME_BOUND;
    iterator end() LIFETIME_BOUND;
    const_iterator begin() const LIFETIME_BOUND;
    const_iterator end() const LIFETIME_BOUND;

    iterator random() { return m_impl.random(); }
    const_iterator random() const { return m_impl.random(); }

    ValuesIteratorRange values();
    const ValuesConstIteratorRange values() const;

    iterator find(const ValueType&);
    const_iterator find(const ValueType&) const;
    bool contains(const ValueType&) const;
    unsigned count(const ValueType&) const;

    // Increments the count if an equal value is already present.
    // The return value includes both an iterator to the value's location,
    // and an isNewEntry bool that indicates whether it is a new or existing entry.
    AddResult add(const ValueType&);
    AddResult add(ValueType&&);

    // Increments the count of a value by the passed amount.
    AddResult add(const ValueType&, unsigned);
    AddResult add(ValueType&&, unsigned);

    // Decrements the count of the value, and removes it if count goes down to zero.
    // Returns true if the value is removed.
    bool remove(const ValueType&);
    bool remove(iterator);

    // Removes the value, regardless of its count.
    // Returns true if a value was removed.
    bool removeAll(iterator);
    bool removeAll(const ValueType&);

    template<typename Functor>
    bool removeAllIf(const Functor&);

    // Clears the whole set.
    void clear();

    // Overloads for smart pointer keys that take the raw pointer type as the parameter.
    template<typename V = ValueType>
        requires IsSmartPtrV<V>
    iterator find(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>*);
    template<typename V = ValueType>
        requires IsSmartPtrV<V>
    const_iterator find(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>*) const;
    template<typename V = ValueType>
        requires IsSmartPtrV<V>
    bool contains(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>*) const;
    template<typename V = ValueType>
        requires IsSmartPtrV<V>
    unsigned count(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>*) const;
    template<typename V = ValueType>
        requires IsSmartPtrV<V>
    bool remove(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>*);

    // Overloads for non-nullable smart pointer values that take the raw reference type as the parameter.
    template<typename V = ValueType>
        requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
    iterator find(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>&);
    template<typename V = ValueType>
        requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
    const_iterator find(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>&) const;
    template<typename V = ValueType>
        requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
    bool contains(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>&) const;
    template<typename V = ValueType>
        requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
    unsigned count(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>&) const;
    template<typename V = ValueType>
        requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
    bool remove(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>&);

    static bool isValidValue(const ValueType&);

private:
    ImplType m_impl;
};


template<typename Value, typename HashFunctions, typename Traits>
inline void HashCountedSet<Value, HashFunctions, Traits>::swap(HashCountedSet& other)
{
    m_impl.swap(other.m_impl);
}

template<typename Value, typename HashFunctions, typename Traits>
inline unsigned HashCountedSet<Value, HashFunctions, Traits>::size() const
{
    return m_impl.size();
}

template<typename Value, typename HashFunctions, typename Traits>
inline unsigned HashCountedSet<Value, HashFunctions, Traits>::capacity() const
{
    return m_impl.capacity();
}

template<typename Value, typename HashFunctions, typename Traits>
inline bool HashCountedSet<Value, HashFunctions, Traits>::isEmpty() const
{
    return size() == 0;
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::begin() -> iterator
{
    return m_impl.begin();
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::end() -> iterator
{
    return m_impl.end();
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::begin() const -> const_iterator
{
    return m_impl.begin();
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::end() const -> const_iterator
{
    return m_impl.end();
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::values() -> ValuesIteratorRange
{
    return m_impl.keys();
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::values() const -> const ValuesConstIteratorRange
{
    return m_impl.keys();
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::find(const ValueType& value) -> iterator
{
    return m_impl.find(value);
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::find(const ValueType& value) const -> const_iterator
{
    return m_impl.find(value);
}

template<typename Value, typename HashFunctions, typename Traits>
inline bool HashCountedSet<Value, HashFunctions, Traits>::contains(const ValueType& value) const
{
    return m_impl.contains(value);
}

template<typename Value, typename HashFunctions, typename Traits>
inline unsigned HashCountedSet<Value, HashFunctions, Traits>::count(const ValueType& value) const
{
    return m_impl.get(value);
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::add(const ValueType &value) -> AddResult
{
    auto result = m_impl.add(value, 0);
    ++result.iterator->value;
    return result;
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::add(ValueType&& value) -> AddResult
{
    auto result = m_impl.add(std::forward<Value>(value), 0);
    ++result.iterator->value;
    return result;
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::add(const ValueType& value, unsigned count) -> AddResult
{
    auto result = m_impl.add(value, 0);
    result.iterator->value += count;
    return result;
}

template<typename Value, typename HashFunctions, typename Traits>
inline auto HashCountedSet<Value, HashFunctions, Traits>::add(ValueType&& value, unsigned count) -> AddResult
{
    auto result = m_impl.add(std::forward<Value>(value), 0);
    result.iterator->value += count;
    return result;
}

template<typename Value, typename HashFunctions, typename Traits>
inline bool HashCountedSet<Value, HashFunctions, Traits>::remove(const ValueType& value)
{
    return remove(find(value));
}

template<typename Value, typename HashFunctions, typename Traits>
inline bool HashCountedSet<Value, HashFunctions, Traits>::remove(iterator it)
{
    if (it == end())
        return false;

    unsigned oldVal = it->value;
    ASSERT(oldVal);
    unsigned newVal = oldVal - 1;
    if (newVal) {
        it->value = newVal;
        return false;
    }

    m_impl.remove(it);
    return true;
}

template<typename Value, typename HashFunctions, typename Traits>
inline bool HashCountedSet<Value, HashFunctions, Traits>::removeAll(const ValueType& value)
{
    return removeAll(find(value));
}

template<typename Value, typename HashFunctions, typename Traits>
inline bool HashCountedSet<Value, HashFunctions, Traits>::removeAll(iterator it)
{
    if (it == end())
        return false;

    m_impl.remove(it);
    return true;
}

template<typename Value, typename HashFunctions, typename Traits>
inline bool HashCountedSet<Value, HashFunctions, Traits>::removeAllIf(const auto& functor)
{
    return m_impl.removeIf(functor);
}

template<typename Value, typename HashFunctions, typename Traits>
inline void HashCountedSet<Value, HashFunctions, Traits>::clear()
{
    m_impl.clear();
}

template<typename Value, typename HashFunctions, typename Traits>
inline bool HashCountedSet<Value, HashFunctions, Traits>::isValidValue(const ValueType& value)
{
    if (Traits::isDeletedValue(value))
        return false;

    if (HashFunctions::safeToCompareToEmptyOrDeleted) {
        if (value == Traits::emptyValue())
            return false;
    } else {
        if (isHashTraitsEmptyValue<Traits>(value))
            return false;
    }

    return true;
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires IsSmartPtrV<V>
inline auto HashCountedSet<Value, HashFunctions, Traits>::find(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>* value) -> iterator
{
    return m_impl.find(value);
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires IsSmartPtrV<V>
inline auto HashCountedSet<Value, HashFunctions, Traits>::find(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>* value) const -> const_iterator
{
    return m_impl.find(value);
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires IsSmartPtrV<V>
inline auto HashCountedSet<Value, HashFunctions, Traits>::contains(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>* value) const -> bool
{
    return m_impl.contains(value);
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires IsSmartPtrV<V>
inline auto HashCountedSet<Value, HashFunctions, Traits>::count(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>* value) const -> unsigned
{
    return m_impl.get(value);
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires IsSmartPtrV<V>
inline auto HashCountedSet<Value, HashFunctions, Traits>::remove(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>* value) -> bool
{
    return remove(find(value));
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
inline auto HashCountedSet<Value, HashFunctions, Traits>::find(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>& value) -> iterator
{
    return find(&value);
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
inline auto HashCountedSet<Value, HashFunctions, Traits>::find(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>& value) const -> const_iterator
{
    return find(&value);
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
inline auto HashCountedSet<Value, HashFunctions, Traits>::contains(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>& value) const -> bool
{
    return contains(&value);
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
inline auto HashCountedSet<Value, HashFunctions, Traits>::count(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>& value) const -> unsigned
{
    return count(&value);
}

template<typename Value, typename HashFunctions, typename Traits>
template<typename V>
    requires (IsSmartPtrV<V> && !IsSmartPtrNullableV<V>)
inline auto HashCountedSet<Value, HashFunctions, Traits>::remove(std::add_const_t<typename GetPtrHelper<V>::UnderlyingType>& value) -> bool
{
    return remove(&value);
}

} // namespace WTF

using WTF::HashCountedSet;
