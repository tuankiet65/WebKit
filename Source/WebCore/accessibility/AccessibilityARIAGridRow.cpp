/*
 * Copyright (C) 2009 Apple Inc. All rights reserved.
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
#include "AccessibilityARIAGridRow.h"

#include "AccessibilityObject.h"
#include "AccessibilityTable.h"

namespace WebCore {
    
AccessibilityARIAGridRow::AccessibilityARIAGridRow(AXID axID, RenderObject& renderer, AXObjectCache& cache)
    : AccessibilityTableRow(axID, renderer, cache)
{
}

AccessibilityARIAGridRow::AccessibilityARIAGridRow(AXID axID, Node& node, AXObjectCache& cache)
    : AccessibilityTableRow(axID, node, cache)
{
}

AccessibilityARIAGridRow::~AccessibilityARIAGridRow() = default;

Ref<AccessibilityARIAGridRow> AccessibilityARIAGridRow::create(AXID axID, RenderObject& renderer, AXObjectCache& cache)
{
    return adoptRef(*new AccessibilityARIAGridRow(axID, renderer, cache));
}

Ref<AccessibilityARIAGridRow> AccessibilityARIAGridRow::create(AXID axID, Node& node, AXObjectCache& cache)
{
    return adoptRef(*new AccessibilityARIAGridRow(axID, node, cache));
}

bool AccessibilityARIAGridRow::isARIATreeGridRow() const
{
    RefPtr parent = parentTable();
    if (!parent)
        return false;
    
    return parent->isTreeGrid();
}
    
AXCoreObject::AccessibilityChildrenVector AccessibilityARIAGridRow::disclosedRows()
{
    AccessibilityChildrenVector disclosedRows;
    // The contiguous disclosed rows will be the rows in the table that 
    // have an aria-level of plus 1 from this row.
    Ref parent = *parentObjectUnignored();
    if (RefPtr axTable = dynamicDowncast<AccessibilityTable>(parent); !axTable || !axTable->isExposable())
        return disclosedRows;

    // Search for rows that match the correct level. 
    // Only take the subsequent rows from this one that are +1 from this row's level.
    int rowIndex = this->rowIndex();
    if (rowIndex < 0)
        return disclosedRows;

    unsigned level = hierarchicalLevel();
    auto allRows = parent->rows();
    int rowCount = allRows.size();
    for (int k = rowIndex + 1; k < rowCount; ++k) {
        Ref row = allRows[k];
        // Stop at the first row that doesn't match the correct level.
        if (row->hierarchicalLevel() != level + 1)
            break;

        disclosedRows.append(row);
    }
    return disclosedRows;
}
    
AccessibilityObject* AccessibilityARIAGridRow::disclosedByRow() const
{
    // The row that discloses this one is the row in the table
    // that is aria-level subtract 1 from this row.
    RefPtr parent = dynamicDowncast<AccessibilityTable>(parentObjectUnignored());
    if (!parent || !parent->isExposable())
        return nullptr;

    // If the level is 1 or less, than nothing discloses this row.
    unsigned level = hierarchicalLevel();
    if (level <= 1)
        return nullptr;

    // Search for the previous row that matches the correct level.
    int index = rowIndex();
    auto allRows = parent->rows();
    int rowCount = allRows.size();
    if (index >= rowCount)
        return nullptr;

    for (int k = index - 1; k >= 0; --k) {
        Ref row = allRows[k];
        if (row->hierarchicalLevel() == level - 1)
            return downcast<AccessibilityObject>(row).ptr();
    }
    return nullptr;
}

AccessibilityTable* AccessibilityARIAGridRow::parentTable() const
{
    // The parent table might not be the direct ancestor of the row unfortunately. ARIA states that role="grid" should
    // only have "row" elements, but if not, we still should handle it gracefully by finding the right table.
    return downcast<AccessibilityTable>(Accessibility::findAncestor<AccessibilityObject>(*this, false, [this] (const auto& ancestor) {
        // The parent table for an ARIA grid row should be an ARIA table.
        // Unless the row is a native tr element.
        if (auto* ancestorTable = dynamicDowncast<AccessibilityTable>(ancestor))
            return ancestorTable->isExposable() && (ancestorTable->isAriaTable() || elementName() == ElementName::HTML_tr);

        return false;
    }));
}

} // namespace WebCore
