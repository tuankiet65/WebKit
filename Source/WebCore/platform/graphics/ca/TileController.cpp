/*
 * Copyright (C) 2011-2014 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "TileController.h"

#if USE(CG)

#include "GraphicsLayer.h"
#include "IntRect.h"
#include "Logging.h"
#include "PlatformCALayer.h"
#include "Region.h"
#include "TileCoverageMap.h"
#include "TileGrid.h"
#include "VelocityData.h"
#include <utility>
#include <wtf/MainThread.h>
#include <wtf/MemoryPressureHandler.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/TextStream.h>

#if HAVE(IOSURFACE)
#include "IOSurface.h"
#endif

#if PLATFORM(IOS_FAMILY)
#include "TileControllerMemoryHandlerIOS.h"
#endif

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(TileController);

static const Seconds tileSizeUpdateDelay { 500_ms };

String TileController::tileGridContainerLayerName()
{
    return "TileGrid container"_s;
}

String TileController::zoomedOutTileGridContainerLayerName()
{
    return "Zoomed-out TileGrid container"_s;
}

TileController::TileController(PlatformCALayer* rootPlatformLayer, AllowScrollPerformanceLogging shouldLogScrollingPerformance)
    : m_tileCacheLayer(rootPlatformLayer)
    , m_deviceScaleFactor(owningGraphicsLayer()->platformCALayerDeviceScaleFactor())
    , m_tileGrid(makeUnique<TileGrid>(*this))
    , m_tileRevalidationTimer(*this, &TileController::tileRevalidationTimerFired)
    , m_tileSizeChangeTimer(*this, &TileController::tileSizeChangeTimerFired, tileSizeUpdateDelay)
    , m_marginEdges(false, false, false, false)
    , m_shouldAllowScrollPerformanceLogging(shouldLogScrollingPerformance)
{
}

TileController::~TileController()
{
    ASSERT(isMainThread());

#if PLATFORM(IOS_FAMILY)
    tileControllerMemoryHandler().removeTileController(this);
#endif
}


void TileController::setClient(TiledBackingClient* client)
{
    if (client) {
        m_client = *client;
        return;
    }

    m_client = nullptr;
}

PlatformLayerIdentifier TileController::layerIdentifier() const
{
    return owningGraphicsLayer()->platformCALayerIdentifier();
}

TileGridIdentifier TileController::primaryGridIdentifier() const
{
    return tileGrid().identifier();
}

std::optional<TileGridIdentifier> TileController::secondaryGridIdentifier() const
{
    if (m_zoomedOutTileGrid)
        m_zoomedOutTileGrid->identifier();

    return { };
}

void TileController::tileCacheLayerBoundsChanged()
{
    ASSERT(owningGraphicsLayer()->isCommittingChanges());
    setNeedsRevalidateTiles();
    notePendingTileSizeChange();
}

void TileController::setNeedsDisplay()
{
    tileGrid().setNeedsDisplay();
    clearZoomedOutTileGrid();
}

void TileController::setNeedsDisplayInRect(const IntRect& rect)
{
    tileGrid().setNeedsDisplayInRect(rect);
    if (m_zoomedOutTileGrid)
        m_zoomedOutTileGrid->dropTilesInRect(rect);
    updateTileCoverageMap();
}

void TileController::setContentsScale(float contentsScale)
{
    ASSERT(owningGraphicsLayer()->isCommittingChanges());

    float deviceScaleFactor = owningGraphicsLayer()->platformCALayerDeviceScaleFactor();
    // The scale we get is the product of the page scale factor and device scale factor.
    // Divide by the device scale factor so we'll get the page scale factor.
    float scale = contentsScale / deviceScaleFactor;
    
    LOG_WITH_STREAM(Tiling, stream << "TileController " << this << " setContentsScale " << contentsScale << " computed scale " << scale << " (deviceScaleFactor " << deviceScaleFactor << ")");

    if (tileGrid().scale() == scale && m_deviceScaleFactor == deviceScaleFactor && !m_hasTilesWithTemporaryScaleFactor)
        return;

    m_hasTilesWithTemporaryScaleFactor = false;
    m_deviceScaleFactor = deviceScaleFactor;

    if (m_coverageMap)
        m_coverageMap->setDeviceScaleFactor(deviceScaleFactor);

    if (m_zoomedOutTileGrid && m_zoomedOutTileGrid->scale() == scale) {
        if (m_tileGrid && m_client)
            m_client->willRemoveGrid(*this, m_tileGrid->identifier());

        m_tileGrid = std::exchange(m_zoomedOutTileGrid, nullptr);
        m_tileGrid->setIsZoomedOutTileGrid(false);
        m_tileGrid->revalidateTiles();
        tileGridsChanged();
        return;
    }

    if (m_zoomedOutContentsScale && m_zoomedOutContentsScale == tileGrid().scale() && tileGrid().scale() != scale && !m_hasTilesWithTemporaryScaleFactor) {
        if (m_zoomedOutTileGrid && m_client)
            m_client->willRemoveGrid(*this, m_zoomedOutTileGrid->identifier());

        m_zoomedOutTileGrid = std::exchange(m_tileGrid, nullptr);
        m_zoomedOutTileGrid->setIsZoomedOutTileGrid(true);
        m_tileGrid = makeUnique<TileGrid>(*this);

        if (m_client)
            m_client->didAddGrid(*this, m_tileGrid->identifier());

        tileGridsChanged();
    }

    auto oldScale = tileGrid().scale();
    tileGrid().setScale(scale);

    bool notifyClient = m_client && scale != oldScale;
    if (notifyClient)
        m_client->willRepaintTilesAfterScaleFactorChange(*this, tileGrid().identifier());

    tileGrid().setNeedsDisplay();

    if (notifyClient)
        m_client->didRepaintTilesAfterScaleFactorChange(*this, tileGrid().identifier());
}

float TileController::contentsScale() const
{
    return tileGrid().scale() * m_deviceScaleFactor;
}

float TileController::tilingScaleFactor() const
{
    return tileGrid().scale();
}

float TileController::zoomedOutContentsScale() const
{
    return m_zoomedOutContentsScale * m_deviceScaleFactor;
}

void TileController::setZoomedOutContentsScale(float scale)
{
    ASSERT(owningGraphicsLayer()->isCommittingChanges());

    float deviceScaleFactor = owningGraphicsLayer()->platformCALayerDeviceScaleFactor();
    scale /= deviceScaleFactor;

    if (m_zoomedOutContentsScale == scale)
        return;

    m_zoomedOutContentsScale = scale;

    if (m_zoomedOutTileGrid && m_zoomedOutTileGrid->scale() != m_zoomedOutContentsScale)
        clearZoomedOutTileGrid();
}

void TileController::setAcceleratesDrawing(bool acceleratesDrawing)
{
    if (m_acceleratesDrawing == acceleratesDrawing)
        return;

    m_acceleratesDrawing = acceleratesDrawing;
    tileGrid().updateTileLayerProperties();
}

#if HAVE(SUPPORT_HDR_DISPLAY)
bool TileController::setNeedsDisplayIfEDRHeadroomExceeds(float headroom)
{
    return tileGrid().setNeedsDisplayIfEDRHeadroomExceeds(headroom);
}

void TileController::setTonemappingEnabled(bool enabled)
{
    if (m_tonemappingEnabled == enabled)
        return;

    m_tonemappingEnabled = enabled;
    tileGrid().updateTileLayerProperties();
}

bool TileController::tonemappingEnabled() const
{
    return m_tonemappingEnabled;
}
#endif

void TileController::setContentsFormat(ContentsFormat contentsFormat)
{
    if (m_contentsFormat == contentsFormat)
        return;

    m_contentsFormat = contentsFormat;
    tileGrid().updateTileLayerProperties();
}

void TileController::setTilesOpaque(bool opaque)
{
    if (opaque == m_tilesAreOpaque)
        return;

    m_tilesAreOpaque = opaque;
    tileGrid().updateTileLayerProperties();
}

void TileController::setVisibleRect(const FloatRect& rect)
{
    if (rect == m_visibleRect)
        return;

    m_visibleRect = rect;
    updateTileCoverageMap();
}

void TileController::setLayoutViewportRect(std::optional<FloatRect> rect)
{
    if (rect == m_layoutViewportRect)
        return;

    m_layoutViewportRect = rect;
    updateTileCoverageMap();
}

void TileController::setCoverageRect(const FloatRect& rect)
{
    ASSERT(owningGraphicsLayer()->isCommittingChanges());
    if (m_coverageRect == rect)
        return;

    m_coverageRect = rect;
    setNeedsRevalidateTiles();

    if (!m_client)
        return;

    m_client->coverageRectDidChange(*this, m_coverageRect);
}

bool TileController::tilesWouldChangeForCoverageRect(const FloatRect& rect) const
{
    if (bounds().isEmpty())
        return false;

    return tileGrid().tilesWouldChangeForCoverageRect(rect);
}

void TileController::setVelocity(const VelocityData& velocity)
{
    bool changeAffectsTileCoverage = m_velocity.velocityOrScaleIsChanging() || velocity.velocityOrScaleIsChanging();

    m_velocity = velocity;
    m_haveExternalVelocityData = true;

    if (changeAffectsTileCoverage)
        setNeedsRevalidateTiles();
}

void TileController::setScrollability(OptionSet<Scrollability> scrollability)
{
    if (scrollability == m_scrollability)
        return;
    
    m_scrollability = scrollability;
    notePendingTileSizeChange();
}

void TileController::setObscuredContentInsets(const FloatBoxExtent& obscuredContentInsets)
{
    m_obscuredContentInsets = obscuredContentInsets;
    setTiledScrollingIndicatorPosition({ obscuredContentInsets.left(), obscuredContentInsets.top() });
}

void TileController::setTiledScrollingIndicatorPosition(const FloatPoint& position)
{
    if (!m_coverageMap)
        return;

    m_coverageMap->setPosition(position);
    updateTileCoverageMap();
}

void TileController::prepopulateRect(const FloatRect& rect)
{
    if (tileGrid().prepopulateRect(rect))
        setNeedsRevalidateTiles();
}

void TileController::setIsInWindow(bool isInWindow)
{
    if (m_isInWindow == isInWindow)
        return;

    m_isInWindow = isInWindow;

    if (m_isInWindow)
        setNeedsRevalidateTiles();
    else {
        const Seconds tileRevalidationTimeout = 4_s;
        scheduleTileRevalidation(tileRevalidationTimeout);
    }
}

void TileController::setTileCoverage(TileCoverage coverage)
{
    if (coverage == m_tileCoverage)
        return;

    m_tileCoverage = coverage;
    setNeedsRevalidateTiles();
}

void TileController::revalidateTiles()
{
    ASSERT(owningGraphicsLayer()->isCommittingChanges());
    tileGrid().revalidateTiles();
}

void TileController::setTileDebugBorderWidth(float borderWidth)
{
    if (m_tileDebugBorderWidth == borderWidth)
        return;
    m_tileDebugBorderWidth = borderWidth;

    tileGrid().updateTileLayerProperties();
}

void TileController::setTileDebugBorderColor(Color borderColor)
{
    if (m_tileDebugBorderColor == borderColor)
        return;
    m_tileDebugBorderColor = borderColor;

    tileGrid().updateTileLayerProperties();
}

void TileController::setTileSizeUpdateDelayDisabledForTesting(bool value)
{
    m_isTileSizeUpdateDelayDisabledForTesting = value;
}

IntRect TileController::boundsForSize(const FloatSize& size) const
{
    IntPoint boundsOriginIncludingMargin(-leftMarginWidth(), -topMarginHeight());
    IntSize boundsSizeIncludingMargin = expandedIntSize(size);
    boundsSizeIncludingMargin.expand(leftMarginWidth() + rightMarginWidth(), topMarginHeight() + bottomMarginHeight());

    return IntRect(boundsOriginIncludingMargin, boundsSizeIncludingMargin);
}

IntRect TileController::bounds() const
{
    return boundsForSize(m_tileCacheLayer->bounds().size());
}

IntRect TileController::boundsWithoutMargin() const
{
    return IntRect(IntPoint(), expandedIntSize(m_tileCacheLayer->bounds().size()));
}

IntRect TileController::boundsAtLastRevalidateWithoutMargin() const
{
    IntRect boundsWithoutMargin = IntRect(IntPoint(), m_boundsAtLastRevalidate.size());
    boundsWithoutMargin.contract(IntSize(leftMarginWidth() + rightMarginWidth(), topMarginHeight() + bottomMarginHeight()));
    return boundsWithoutMargin;
}

FloatRect TileController::adjustTileCoverageRect(const FloatRect& coverageRect, const FloatRect& previousVisibleRect, const FloatRect& currentVisibleRect, bool sizeChanged)
{
    if (sizeChanged || MemoryPressureHandler::singleton().isUnderMemoryPressure())
        return unionRect(coverageRect, currentVisibleRect);

    return GraphicsLayer::adjustCoverageRectForMovement(coverageRect, previousVisibleRect, currentVisibleRect);
}

#if !PLATFORM(IOS_FAMILY)
// Coverage expansion for less memory-constrained devices.
// Kept separate to preserve historical behavior; should be merged with adjustTileCoverageWithScrollingVelocity eventually.
FloatRect TileController::adjustTileCoverageForDesktopPageScrolling(const FloatRect& coverageRect, const FloatSize& newSize, const FloatRect& previousVisibleRect, const FloatRect& visibleRect) const
{
    // FIXME: look at how far the document can scroll in each dimension.
    FloatSize coverageSize = visibleRect.size();

    bool largeVisibleRectChange = !previousVisibleRect.isEmpty() && !visibleRect.intersects(previousVisibleRect);

    // Inflate the coverage rect so that it covers 2x of the visible width and 3x of the visible height.
    // These values were chosen because it's more common to have tall pages and to scroll vertically,
    // so we keep more tiles above and below the current area.
    float widthScale = 1;
    float heightScale = 1;

    if (m_tileCoverage & CoverageForHorizontalScrolling && !largeVisibleRectChange)
        widthScale = 2;

    if (m_tileCoverage & CoverageForVerticalScrolling && !largeVisibleRectChange)
        heightScale = 3;

    coverageSize.scale(widthScale, heightScale);

    FloatRect coverageBounds = boundsForSize(newSize);

    // Return 'rect' padded evenly on all sides to achieve 'newSize', but make the padding uneven to contain within constrainingRect.
    auto expandRectWithinRect = [](const FloatRect& rect, const FloatSize& newSize, const FloatRect& constrainingRect) {
        ASSERT(newSize.width() >= rect.width() && newSize.height() >= rect.height());

        FloatSize extraSize = newSize - rect.size();
        
        FloatRect expandedRect = rect;
        expandedRect.inflateX(extraSize.width() / 2);
        expandedRect.inflateY(extraSize.height() / 2);

        if (expandedRect.x() < constrainingRect.x())
            expandedRect.setX(constrainingRect.x());
        else if (expandedRect.maxX() > constrainingRect.maxX())
            expandedRect.setX(constrainingRect.maxX() - expandedRect.width());
        
        if (expandedRect.y() < constrainingRect.y())
            expandedRect.setY(constrainingRect.y());
        else if (expandedRect.maxY() > constrainingRect.maxY())
            expandedRect.setY(constrainingRect.maxY() - expandedRect.height());
        
        return intersection(expandedRect, constrainingRect);
    };

    FloatRect coverage = expandRectWithinRect(visibleRect, coverageSize, coverageBounds);
    LOG_WITH_STREAM(Tiling, stream << "TileController " << this << " adjustTileCoverageForDesktopPageScrolling newSize=" << newSize << " mode " << m_tileCoverage << " expanded to " << coverageSize << " bounds with margin " << coverageBounds << " coverage " << coverage);
    return unionRect(coverageRect, coverage);
}
#endif

FloatRect TileController::adjustTileCoverageWithScrollingVelocity(const FloatRect& coverageRect, const FloatSize& newSize, const FloatRect& visibleRect, float contentsScale, MonotonicTime timestamp) const
{
    if (m_tileCoverage == CoverageForVisibleArea || MemoryPressureHandler::singleton().isUnderMemoryPressure())
        return visibleRect;

    double horizontalMargin = kDefaultTileSize / contentsScale;
    double verticalMargin = kDefaultTileSize / contentsScale;

    Seconds timeDelta = timestamp - m_velocity.lastUpdateTime;

    FloatRect futureRect = visibleRect;
    futureRect.setLocation(FloatPoint(
        futureRect.location().x() + timeDelta.value() * m_velocity.horizontalVelocity,
        futureRect.location().y() + timeDelta.value() * m_velocity.verticalVelocity));

    if (m_velocity.horizontalVelocity) {
        futureRect.setWidth(futureRect.width() + horizontalMargin);
        if (m_velocity.horizontalVelocity < 0)
            futureRect.setX(futureRect.x() - horizontalMargin);
    }

    if (m_velocity.verticalVelocity) {
        futureRect.setHeight(futureRect.height() + verticalMargin);
        if (m_velocity.verticalVelocity < 0)
            futureRect.setY(futureRect.y() - verticalMargin);
    }

    if (!m_velocity.horizontalVelocity && !m_velocity.verticalVelocity) {
        if (m_velocity.scaleChangeRate > 0) {
            LOG_WITH_STREAM(Tiling, stream << "TileController " << this << " computeTileCoverageRect - zooming, coverage is visible rect " << coverageRect);
            return visibleRect;
        }
        futureRect.setWidth(futureRect.width() + horizontalMargin);
        futureRect.setHeight(futureRect.height() + verticalMargin);
        futureRect.setX(futureRect.x() - horizontalMargin / 2);
        futureRect.setY(futureRect.y() - verticalMargin / 2);
    }

    // Can't use m_tileCacheLayer->bounds() here, because the size of the underlying platform layer
    // hasn't been updated for the current commit.
    IntSize contentSize = expandedIntSize(newSize);
    if (futureRect.maxX() > contentSize.width())
        futureRect.setX(contentSize.width() - futureRect.width());
    if (futureRect.maxY() > contentSize.height())
        futureRect.setY(contentSize.height() - futureRect.height());
    if (futureRect.x() < 0)
        futureRect.setX(0);
    if (futureRect.y() < 0)
        futureRect.setY(0);

    LOG_WITH_STREAM(Tiling, stream << "TileController " << this << " adjustTileCoverageForScrolling - coverage " << coverageRect << " expanded to " << unionRect(coverageRect, futureRect) << " velocity " << m_velocity);

    return unionRect(coverageRect, futureRect);
}

FloatRect TileController::adjustTileCoverageRectForScrolling(const FloatRect& coverageRect, const FloatSize& newSize, const FloatRect& previousVisibleRect, const FloatRect& visibleRect, float contentsScale)
{
    // If the page is not in a window (for example if it's in a background tab), we limit the tile coverage rect to the visible rect.
    if (!m_isInWindow)
        return visibleRect;

#if !PLATFORM(IOS_FAMILY)
    if (m_tileCacheLayer->isPageTiledBackingLayer())
        return adjustTileCoverageForDesktopPageScrolling(coverageRect, newSize, previousVisibleRect, visibleRect);
#else
    UNUSED_PARAM(previousVisibleRect);
#endif

    MonotonicTime currentTime = MonotonicTime::now();

    auto computeVelocityIfNecessary = [&](FloatPoint scrollOffset) {
        if (m_haveExternalVelocityData)
            return;

        if (!m_historicalVelocityData)
            m_historicalVelocityData = makeUnique<HistoricalVelocityData>();

        m_velocity = m_historicalVelocityData->velocityForNewData(scrollOffset, contentsScale, currentTime);
    };
    
    computeVelocityIfNecessary(visibleRect.location());

    return adjustTileCoverageWithScrollingVelocity(coverageRect, newSize, visibleRect, contentsScale, currentTime);
}

void TileController::scheduleTileRevalidation(Seconds interval)
{
    if (m_tileRevalidationTimer.isActive() && m_tileRevalidationTimer.nextFireInterval() < interval)
        return;

    m_tileRevalidationTimer.startOneShot(interval);
}

bool TileController::shouldAggressivelyRetainTiles() const
{
    return owningGraphicsLayer()->platformCALayerShouldAggressivelyRetainTiles(m_tileCacheLayer);
}

bool TileController::shouldTemporarilyRetainTileCohorts() const
{
    return owningGraphicsLayer()->platformCALayerShouldTemporarilyRetainTileCohorts(m_tileCacheLayer);
}

void TileController::willStartLiveResize()
{
    m_inLiveResize = true;
}

void TileController::didEndLiveResize()
{
    m_inLiveResize = false;
    m_tileSizeLocked = false; // Let the end of a live resize update the tiles.
}

void TileController::willRepaintTile(TileGrid& tileGrid, TileIndex tileIndex, const FloatRect& tileClip, const FloatRect& paintDirtyRect)
{
    if (!m_client)
        return;

    m_client->willRepaintTile(*this, tileGrid.identifier(), tileIndex, tileClip, paintDirtyRect);
}

void TileController::willRemoveTile(TileGrid& tileGrid, TileIndex tileIndex)
{
    if (!m_client)
        return;

    m_client->willRemoveTile(*this, tileGrid.identifier(), tileIndex);
}

void TileController::willRepaintAllTiles(TileGrid& tileGrid)
{
    if (!m_client)
        return;

    m_client->willRepaintAllTiles(*this, tileGrid.identifier());
}

void TileController::notePendingTileSizeChange()
{
    if (m_isTileSizeUpdateDelayDisabledForTesting)
        tileSizeChangeTimerFired();
    else
        m_tileSizeChangeTimer.restart();
}

void TileController::tileSizeChangeTimerFired()
{
    if (!owningGraphicsLayer())
        return;

    m_tileSizeLocked = false;
    setNeedsRevalidateTiles();
}

IntSize TileController::tileSize() const
{
    return tileGrid().tileSize();
}

FloatRect TileController::rectForTile(TileIndex tileIndex) const
{
    return tileGrid().rectForTile(tileIndex);
}

IntSize TileController::computeTileSize()
{
    if (m_inLiveResize || m_tileSizeLocked)
        return tileGrid().tileSize();

    const int kLowestCommonDenominatorMaxTileSize = 4 * 1024;
    IntSize maxTileSize(kLowestCommonDenominatorMaxTileSize, kLowestCommonDenominatorMaxTileSize);

#if HAVE(IOSURFACE)
    IntSize surfaceSizeLimit = IOSurface::maximumSize();
    surfaceSizeLimit.scale(1 / m_deviceScaleFactor);
    maxTileSize = maxTileSize.shrunkTo(surfaceSizeLimit);
#endif
    
    if (owningGraphicsLayer()->platformCALayerUseGiantTiles())
        return maxTileSize;

    IntSize tileSize(kDefaultTileSize, kDefaultTileSize);

    if (m_scrollability == Scrollability::NotScrollable) {
        IntSize scaledSize = expandedIntSize(boundsWithoutMargin().size() * tileGrid().scale());
        tileSize = scaledSize.constrainedBetween(IntSize(kDefaultTileSize, kDefaultTileSize), maxTileSize);
    } else if (m_scrollability == Scrollability::VerticallyScrollable)
        tileSize.setWidth(std::min(std::max<int>(ceilf(boundsWithoutMargin().width() * tileGrid().scale()), kDefaultTileSize), maxTileSize.width()));

    LOG_WITH_STREAM(Tiling, stream << "TileController::tileSize newSize=" << tileSize);

    m_tileSizeLocked = true;
    return tileSize;
}

void TileController::clearZoomedOutTileGrid()
{
    m_zoomedOutTileGrid = nullptr;
    tileGridsChanged();
}

void TileController::tileGridsChanged()
{
    return owningGraphicsLayer()->platformCALayerCustomSublayersChanged(m_tileCacheLayer);
}

void TileController::tileRevalidationTimerFired()
{
    if (!owningGraphicsLayer())
        return;

    if (m_isInWindow) {
        setNeedsRevalidateTiles();
        return;
    }
    // If we are not visible get rid of the zoomed-out tiles.
    clearZoomedOutTileGrid();

    tileGrid().revalidateTiles(shouldAggressivelyRetainTiles()
        ? OptionSet { TileGrid::PruneSecondaryTiles, TileGrid::UnparentAllTiles }
        : OptionSet { TileGrid::UnparentAllTiles });
}

void TileController::willRevalidateTiles(TileGrid& tileGrid, TileRevalidationType revalidationType)
{
    if (m_client)
        m_client->willRevalidateTiles(*this, tileGrid.identifier(), revalidationType);
}

void TileController::didRevalidateTiles(TileGrid& tileGrid, TileRevalidationType revalidationType, const HashSet<TileIndex>& tilesNeedingDisplay)
{
    m_boundsAtLastRevalidate = bounds();

    LOG_WITH_STREAM(Tiling, stream << "TileController " << this << " (bounds " << bounds() << ") didRevalidateTiles - tileCoverageRect " << tileCoverageRect() << " grid extent " << tileGridExtent() << " memory use " << (retainedTileBackingStoreMemory() / (1024 * 1024)) << "MB");

    updateTileCoverageMap();

    if (m_client)
        m_client->didRevalidateTiles(*this, tileGrid.identifier(), revalidationType, tilesNeedingDisplay);
}

unsigned TileController::blankPixelCount() const
{
    return tileGrid().blankPixelCount();
}

unsigned TileController::blankPixelCountForTiles(const PlatformLayerList& tiles, const FloatRect& visibleRect, const IntPoint& tileTranslation)
{
    Region paintedVisibleTiles;

    for (auto& tileLayer : tiles) {
        FloatRect visiblePart(CGRectOffset(PlatformCALayer::frameForLayer(tileLayer.get()), tileTranslation.x(), tileTranslation.y()));
        visiblePart.intersect(visibleRect);

        if (!visiblePart.isEmpty())
            paintedVisibleTiles.unite(enclosingIntRect(visiblePart));
    }

    Region uncoveredRegion(enclosingIntRect(visibleRect));
    uncoveredRegion.subtract(paintedVisibleTiles);

    return static_cast<unsigned>(uncoveredRegion.totalArea());
}

void TileController::setNeedsRevalidateTiles()
{
    owningGraphicsLayer()->platformCALayerSetNeedsToRevalidateTiles();
}

void TileController::updateTileCoverageMap()
{
    if (m_coverageMap)
        m_coverageMap->setNeedsUpdate();
}

IntRect TileController::tileGridExtent() const
{
    return tileGrid().extent();
}

double TileController::retainedTileBackingStoreMemory() const
{
    double bytes = tileGrid().retainedTileBackingStoreMemory();
    if (m_zoomedOutTileGrid)
        bytes += m_zoomedOutTileGrid->retainedTileBackingStoreMemory();
    return bytes;
}

// Return the rect in layer coords, not tile coords.
IntRect TileController::tileCoverageRect() const
{
    return tileGrid().tileCoverageRect();
}

PlatformCALayer* TileController::tiledScrollingIndicatorLayer()
{
    if (!m_coverageMap)
        m_coverageMap = makeUnique<TileCoverageMap>(*this);

    return &m_coverageMap->layer();
}

void TileController::setScrollingModeIndication(ScrollingModeIndication scrollingMode)
{
    if (scrollingMode == m_indicatorMode)
        return;

    m_indicatorMode = scrollingMode;

    updateTileCoverageMap();
}

void TileController::setHasMargins(bool marginTop, bool marginBottom, bool marginLeft, bool marginRight)
{
    RectEdges<bool> marginEdges(marginTop, marginRight, marginBottom, marginLeft);
    if (marginEdges == m_marginEdges)
        return;
    
    m_marginEdges = marginEdges;
    setNeedsRevalidateTiles();
}

void TileController::setMarginSize(int marginSize)
{
    if (marginSize == m_marginSize)
        return;
    
    m_marginSize = marginSize;
    setNeedsRevalidateTiles();
}

bool TileController::hasMargins() const
{
    return m_marginSize && (m_marginEdges.top() || m_marginEdges.bottom() || m_marginEdges.left() || m_marginEdges.right());
}

bool TileController::hasHorizontalMargins() const
{
    return m_marginSize && (m_marginEdges.left() || m_marginEdges.right());
}

bool TileController::hasVerticalMargins() const
{
    return m_marginSize && (m_marginEdges.top() || m_marginEdges.bottom());
}

int TileController::topMarginHeight() const
{
    return (m_marginSize * m_marginEdges.top()) / tileGrid().scale();
}

int TileController::bottomMarginHeight() const
{
    return (m_marginSize * m_marginEdges.bottom()) / tileGrid().scale();
}

int TileController::leftMarginWidth() const
{
    return (m_marginSize * m_marginEdges.left()) / tileGrid().scale();
}

int TileController::rightMarginWidth() const
{
    return (m_marginSize * m_marginEdges.right()) / tileGrid().scale();
}

Ref<PlatformCALayer> TileController::createTileLayer(const IntRect& tileRect, TileGrid& grid)
{
    float temporaryScaleFactor = owningGraphicsLayer()->platformCALayerContentsScaleMultiplierForNewTiles(m_tileCacheLayer);
    m_hasTilesWithTemporaryScaleFactor |= temporaryScaleFactor != 1;

    auto layer = m_tileCacheLayer->createCompatibleLayerOrTakeFromPool(PlatformCALayer::LayerType::LayerTypeTiledBackingTileLayer, &grid, tileRect.size());
    layer->setAnchorPoint(FloatPoint3D());
    layer->setPosition(tileRect.location());
    layer->setBorderColor(m_tileDebugBorderColor);
    layer->setBorderWidth(m_tileDebugBorderWidth);
    layer->setAntialiasesEdges(false);
    layer->setOpaque(m_tilesAreOpaque);
    layer->setName(makeString("tile at "_s, tileRect.location().x(), ',', tileRect.location().y()));
    layer->setContentsScale(m_deviceScaleFactor * temporaryScaleFactor);
    layer->setAcceleratesDrawing(m_acceleratesDrawing);
    layer->setContentsFormat(m_contentsFormat);
#if HAVE(SUPPORT_HDR_DISPLAY)
    layer->setTonemappingEnabled(m_tonemappingEnabled);
#endif
    layer->setNeedsDisplay();
    return layer;
}

Vector<RefPtr<PlatformCALayer>> TileController::containerLayers()
{
    Vector<RefPtr<PlatformCALayer>> layerList;
    if (m_zoomedOutTileGrid)
        layerList.append(&m_zoomedOutTileGrid->containerLayer());
    layerList.append(&tileGrid().containerLayer());
    return layerList;
}

#if PLATFORM(IOS_FAMILY)
unsigned TileController::numberOfUnparentedTiles() const
{
    unsigned count = tileGrid().numberOfUnparentedTiles();
    if (m_zoomedOutTileGrid)
        count += m_zoomedOutTileGrid->numberOfUnparentedTiles();
    return count;
}

void TileController::removeUnparentedTilesNow()
{
    tileGrid().removeUnparentedTilesNow();
    if (m_zoomedOutTileGrid)
        m_zoomedOutTileGrid->removeUnparentedTilesNow();

    updateTileCoverageMap();
}
#endif

void TileController::logFilledVisibleFreshTile(unsigned blankPixelCount)
{
    if (m_shouldAllowScrollPerformanceLogging == AllowScrollPerformanceLogging::Yes)
        owningGraphicsLayer()->platformCALayerLogFilledVisibleFreshTile(blankPixelCount);
}

#if ENABLE(RE_DYNAMIC_CONTENT_SCALING)
std::optional<DynamicContentScalingDisplayList> TileController::dynamicContentScalingDisplayListForTile(const TileGrid& tileGrid, TileIndex index)
{
    if (!m_client)
        return std::nullopt;
    return m_client->dynamicContentScalingDisplayListForTile(*this, tileGrid.identifier(), index);
}
#endif

FloatRect TileController::adjustedTileClipRectForObscuredInsets(const FloatRect& clipRect) const
{
    auto delta = m_obscuredInsetsDelta;
    if (!delta)
        return clipRect;
    FloatSize sizeAdjustment { delta->left() + delta->right(), delta->top() + delta->bottom() };
    return { clipRect.location(), clipRect.size() + sizeAdjustment.expandedTo({ }) };
}


} // namespace WebCore

#endif // USE(CG)
