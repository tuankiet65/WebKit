list(APPEND WebCore_PRIVATE_INCLUDE_DIRECTORIES
    "${WEBCORE_DIR}/platform/graphics/harfbuzz"
    "${WEBCORE_DIR}/platform/graphics/filters/skia"
    "${WEBCORE_DIR}/platform/graphics/skia"
)

list(APPEND WebCore_UNIFIED_SOURCE_LIST_FILES
    "platform/SourcesSkia.txt"
)

list(APPEND WebCore_PRIVATE_FRAMEWORK_HEADERS
    platform/graphics/harfbuzz/HbUniquePtr.h

    platform/graphics/skia/FontCascadeSkiaInlines.h
    platform/graphics/skia/GraphicsContextSkia.h
    platform/graphics/skia/ImageBufferSkiaBackend.h
    platform/graphics/skia/SkiaHarfBuzzFont.h
    platform/graphics/skia/SkiaHarfBuzzFontCache.h
    platform/graphics/skia/SkiaPaintingEngine.h
    platform/graphics/skia/SkiaRecordingResult.h
    platform/graphics/skia/SkiaReplayCanvas.h
    platform/graphics/skia/SkiaSpanExtras.h
    platform/graphics/skia/SkiaSystemFallbackFontCache.h
)

list(APPEND WebCore_LIBRARIES
    HarfBuzz::HarfBuzz
    HarfBuzz::ICU
    Skia::Skia
)
