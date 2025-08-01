/*
 * Copyright (C) 2012 Adobe Systems Incorporated. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "RectangleLayoutShape.h"

#include <wtf/MathExtras.h>

namespace WebCore {

static inline float ellipseXIntercept(float y, float rx, float ry)
{
    ASSERT(ry > 0);
    return rx * sqrt(1 - (y * y) / (ry * ry));
}

FloatRect RectangleLayoutShape::shapeMarginBounds() const
{
    ASSERT(shapeMargin() >= 0);
    if (!shapeMargin())
        return m_bounds;

    float boundsX = x() - shapeMargin();
    float boundsY = y() - shapeMargin();
    float boundsWidth = width() + shapeMargin() * 2;
    float boundsHeight = height() + shapeMargin() * 2;
    return FloatRect(boundsX, boundsY, boundsWidth, boundsHeight);
}

LineSegment RectangleLayoutShape::getExcludedInterval(LayoutUnit logicalTop, LayoutUnit logicalHeight) const
{
    auto bounds = shapeMarginBounds();
    if (bounds.isEmpty())
        return { };

    float y1 = logicalTop;
    float y2 = logicalTop + logicalHeight;

    if (y2 < bounds.y() || y1 >= bounds.maxY())
        return { };

    float x1 = bounds.x();
    float x2 = bounds.maxX();

    float marginRadiusX = rx() + shapeMargin();
    float marginRadiusY = ry() + shapeMargin();

    if (marginRadiusY > 0) {
        if (y2 < bounds.y() + marginRadiusY) {
            float yi = y2 - bounds.y() - marginRadiusY;
            float xi = ellipseXIntercept(yi, marginRadiusX, marginRadiusY);
            x1 = bounds.x() + marginRadiusX - xi;
            x2 = bounds.maxX() - marginRadiusX + xi;
        } else if (y1 > bounds.maxY() - marginRadiusY) {
            float yi =  y1 - (bounds.maxY() - marginRadiusY);
            float xi = ellipseXIntercept(yi, marginRadiusX, marginRadiusY);
            x1 = bounds.x() + marginRadiusX - xi;
            x2 = bounds.maxX() - marginRadiusX + xi;
        }
    }

    if (shouldFlipStartAndEndPoints(writingMode()))
        return { std::max(0.f, m_boxLogicalWidth - x2), std::max(0.f, m_boxLogicalWidth - x1) };
    return { x1, x2 };
}

void RectangleLayoutShape::buildDisplayPaths(DisplayPaths& paths) const
{
    paths.shape.addRoundedRect(m_bounds, FloatSize(m_radii.width(), m_radii.height()), PathRoundedRect::Strategy::PreferBezier);
    if (shapeMargin())
        paths.marginShape.addRoundedRect(shapeMarginBounds(), FloatSize(m_radii.width() + shapeMargin(), m_radii.height() + shapeMargin()), PathRoundedRect::Strategy::PreferBezier);
}

} // namespace WebCore
