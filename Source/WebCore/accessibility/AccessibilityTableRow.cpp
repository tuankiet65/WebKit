/*
 * Copyright (C) 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "AccessibilityTableRow.h"

#include "AXObjectCache.h"
#include "AccessibilityTable.h"
#include "AccessibilityTableCell.h"
#include "HTMLNames.h"
#include "RenderObject.h"

namespace WebCore {

using namespace HTMLNames;

AccessibilityTableRow::AccessibilityTableRow(AXID axID, RenderObject& renderer, AXObjectCache& cache)
    : AccessibilityRenderObject(axID, renderer, cache)
{
}

AccessibilityTableRow::AccessibilityTableRow(AXID axID, Node& node, AXObjectCache& cache)
    : AccessibilityRenderObject(axID, node, cache)
{
}

AccessibilityTableRow::~AccessibilityTableRow() = default;

Ref<AccessibilityTableRow> AccessibilityTableRow::create(AXID axID, RenderObject& renderer, AXObjectCache& cache)
{
    return adoptRef(*new AccessibilityTableRow(axID, renderer, cache));
}

Ref<AccessibilityTableRow> AccessibilityTableRow::create(AXID axID, Node& node, AXObjectCache& cache)
{
    return adoptRef(*new AccessibilityTableRow(axID, node, cache));
}

AccessibilityRole AccessibilityTableRow::determineAccessibilityRole()
{
    if (!isTableRow())
        return AccessibilityRenderObject::determineAccessibilityRole();

    if ((m_ariaRole = determineAriaRoleAttribute()) != AccessibilityRole::Unknown)
        return m_ariaRole;

    return AccessibilityRole::Row;
}

bool AccessibilityTableRow::isTableRow() const
{
    RefPtr table = parentTable();
    return table && table->isExposable();
}
    
AccessibilityObject* AccessibilityTableRow::observableObject() const
{
    // This allows the table to be the one who sends notifications about tables.
    return parentTable();
}
    
bool AccessibilityTableRow::computeIsIgnored() const
{    
    AccessibilityObjectInclusion decision = defaultObjectInclusion();
    if (decision == AccessibilityObjectInclusion::IncludeObject)
        return false;
    if (decision == AccessibilityObjectInclusion::IgnoreObject)
        return true;
    
    if (!isTableRow())
        return AccessibilityRenderObject::computeIsIgnored();

    return isRenderHidden() || ignoredFromPresentationalRole();
}
    
AccessibilityTable* AccessibilityTableRow::parentTable() const
{
    // The parent table might not be the direct ancestor of the row unfortunately. ARIA states that role="grid" should
    // only have "row" elements, but if not, we still should handle it gracefully by finding the right table.
    for (RefPtr parent = parentObject(); parent; parent = parent->parentObject()) {
        // If this is a non-anonymous table object, but not an accessibility table, we should stop because we don't want to
        // choose another ancestor table as this row's table.
        if (auto* parentTable = dynamicDowncast<AccessibilityTable>(*parent)) {
            if (parentTable->isExposable())
                return parentTable;
            if (parentTable->node())
                break;
        }
    }
    return nullptr;
}

void AccessibilityTableRow::setRowIndex(unsigned rowIndex)
{
    if (m_rowIndex == rowIndex)
        return;
    m_rowIndex = rowIndex;

#if ENABLE(ACCESSIBILITY_ISOLATED_TREE)
    if (auto* cache = axObjectCache())
        cache->rowIndexChanged(*this);
#endif
}

void AccessibilityTableRow::addChildren()
{
    // If the element specifies its cells through aria-owns, return that first.
    auto ownedObjects = this->ownedObjects();
    if (ownedObjects.size()) {
        for (auto& object : ownedObjects)
            addChild(downcast<AccessibilityObject>(object.get()), DescendIfIgnored::No);
        m_childrenInitialized = true;
        m_subtreeDirty = false;
    }
    else
        AccessibilityRenderObject::addChildren();

    // "ARIA 1.1, If the set of columns which is present in the DOM is contiguous, and if there are no cells which span more than one row or
    // column in that set, then authors may place aria-colindex on each row, setting the value to the index of the first column of the set."
    // Update child cells' axColIndex if there's an aria-colindex value set for the row. So the cell doesn't have to go through the siblings
    // to calculate the index.
    std::optional colIndex = axColumnIndex();
    if (!colIndex)
        return;

    unsigned index = 0;
    for (const auto& cell : unignoredChildren()) {
        if (RefPtr tableCell = dynamicDowncast<AccessibilityTableCell>(cell.get()))
            tableCell->setAXColIndexFromRow(*colIndex + index);
        index++;
    }

#ifndef NDEBUG
    verifyChildrenIndexInParent();
#endif
}

std::optional<unsigned> AccessibilityTableRow::axColumnIndex() const
{
    int value = integralAttribute(aria_colindexAttr);
    return value >= 1 ? std::optional(value) : std::nullopt;
}

std::optional<unsigned> AccessibilityTableRow::axRowIndex() const
{
    int value = integralAttribute(aria_rowindexAttr);
    return value >= 1 ? std::optional(value) : std::nullopt;
}
    
} // namespace WebCore
