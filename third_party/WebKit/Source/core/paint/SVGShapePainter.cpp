// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/SVGShapePainter.h"

#include "core/layout/svg/LayoutSVGResourceMarker.h"
#include "core/layout/svg/LayoutSVGShape.h"
#include "core/layout/svg/SVGLayoutSupport.h"
#include "core/layout/svg/SVGMarkerData.h"
#include "core/layout/svg/SVGResources.h"
#include "core/layout/svg/SVGResourcesCache.h"
#include "core/paint/FloatClipRecorder.h"
#include "core/paint/LayoutObjectDrawingRecorder.h"
#include "core/paint/ObjectPainter.h"
#include "core/paint/PaintInfo.h"
#include "core/paint/SVGContainerPainter.h"
#include "core/paint/SVGPaintContext.h"
#include "core/paint/TransformRecorder.h"
#include "platform/graphics/GraphicsContextStateSaver.h"
#include "platform/graphics/paint/PaintRecord.h"
#include "platform/graphics/paint/SkPictureBuilder.h"
#include "wtf/Optional.h"

namespace blink {

static bool setupNonScalingStrokeContext(
    AffineTransform& strokeTransform,
    GraphicsContextStateSaver& stateSaver) {
  if (!strokeTransform.isInvertible())
    return false;

  stateSaver.save();
  stateSaver.context().concatCTM(strokeTransform.inverse());
  return true;
}

static SkPath::FillType fillRuleFromStyle(const PaintInfo& paintInfo,
                                          const SVGComputedStyle& svgStyle) {
  return WebCoreWindRuleToSkFillType(paintInfo.isRenderingClipPathAsMaskImage()
                                         ? svgStyle.clipRule()
                                         : svgStyle.fillRule());
}

void SVGShapePainter::paint(const PaintInfo& paintInfo) {
  if (paintInfo.phase != PaintPhaseForeground ||
      m_layoutSVGShape.style()->visibility() != EVisibility::kVisible ||
      m_layoutSVGShape.isShapeEmpty())
    return;

  FloatRect boundingBox = m_layoutSVGShape.visualRectInLocalSVGCoordinates();
  if (!paintInfo.cullRect().intersectsCullRect(
          m_layoutSVGShape.localSVGTransform(), boundingBox))
    return;

  PaintInfo paintInfoBeforeFiltering(paintInfo);
  // Shapes cannot have children so do not call updateCullRect.
  SVGTransformContext transformContext(paintInfoBeforeFiltering.context,
                                       m_layoutSVGShape,
                                       m_layoutSVGShape.localSVGTransform());
  {
    SVGPaintContext paintContext(m_layoutSVGShape, paintInfoBeforeFiltering);
    if (paintContext.applyClipMaskAndFilterIfNecessary() &&
        !LayoutObjectDrawingRecorder::useCachedDrawingIfPossible(
            paintContext.paintInfo().context, m_layoutSVGShape,
            paintContext.paintInfo().phase)) {
      LayoutObjectDrawingRecorder recorder(
          paintContext.paintInfo().context, m_layoutSVGShape,
          paintContext.paintInfo().phase, boundingBox);
      const SVGComputedStyle& svgStyle = m_layoutSVGShape.style()->svgStyle();

      bool shouldAntiAlias = svgStyle.shapeRendering() != SR_CRISPEDGES &&
                             svgStyle.shapeRendering() != SR_OPTIMIZESPEED;

      for (int i = 0; i < 3; i++) {
        switch (svgStyle.paintOrderType(i)) {
          case PT_FILL: {
            PaintFlags fillPaint;
            if (!SVGPaintContext::paintForLayoutObject(
                    paintContext.paintInfo(), m_layoutSVGShape.styleRef(),
                    m_layoutSVGShape, ApplyToFillMode, fillPaint))
              break;
            fillPaint.setAntiAlias(shouldAntiAlias);
            fillShape(paintContext.paintInfo().context, fillPaint,
                      fillRuleFromStyle(paintContext.paintInfo(), svgStyle));
            break;
          }
          case PT_STROKE:
            if (svgStyle.hasVisibleStroke()) {
              GraphicsContextStateSaver stateSaver(
                  paintContext.paintInfo().context, false);
              AffineTransform nonScalingTransform;
              const AffineTransform* additionalPaintServerTransform = 0;

              if (m_layoutSVGShape.hasNonScalingStroke()) {
                nonScalingTransform =
                    m_layoutSVGShape.nonScalingStrokeTransform();
                if (!setupNonScalingStrokeContext(nonScalingTransform,
                                                  stateSaver))
                  return;

                // Non-scaling stroke needs to reset the transform back to the
                // host transform.
                additionalPaintServerTransform = &nonScalingTransform;
              }

              PaintFlags strokePaint;
              if (!SVGPaintContext::paintForLayoutObject(
                      paintContext.paintInfo(), m_layoutSVGShape.styleRef(),
                      m_layoutSVGShape, ApplyToStrokeMode, strokePaint,
                      additionalPaintServerTransform))
                break;
              strokePaint.setAntiAlias(shouldAntiAlias);

              StrokeData strokeData;
              SVGLayoutSupport::applyStrokeStyleToStrokeData(
                  strokeData, m_layoutSVGShape.styleRef(), m_layoutSVGShape,
                  m_layoutSVGShape.dashScaleFactor());
              strokeData.setupPaint(&strokePaint);

              strokeShape(paintContext.paintInfo().context, strokePaint);
            }
            break;
          case PT_MARKERS:
            paintMarkers(paintContext.paintInfo(), boundingBox);
            break;
          default:
            ASSERT_NOT_REACHED();
            break;
        }
      }
    }
  }

  if (m_layoutSVGShape.style()->outlineWidth()) {
    PaintInfo outlinePaintInfo(paintInfoBeforeFiltering);
    outlinePaintInfo.phase = PaintPhaseSelfOutlineOnly;
    ObjectPainter(m_layoutSVGShape)
        .paintOutline(outlinePaintInfo, LayoutPoint(boundingBox.location()));
  }
}

class PathWithTemporaryWindingRule {
 public:
  PathWithTemporaryWindingRule(Path& path, SkPath::FillType fillType)
      : m_path(const_cast<SkPath&>(path.getSkPath())) {
    m_savedFillType = m_path.getFillType();
    m_path.setFillType(fillType);
  }
  ~PathWithTemporaryWindingRule() { m_path.setFillType(m_savedFillType); }

  const SkPath& getSkPath() const { return m_path; }

 private:
  SkPath& m_path;
  SkPath::FillType m_savedFillType;
};

void SVGShapePainter::fillShape(GraphicsContext& context,
                                const PaintFlags& paint,
                                SkPath::FillType fillType) {
  switch (m_layoutSVGShape.geometryCodePath()) {
    case RectGeometryFastPath:
      context.drawRect(m_layoutSVGShape.objectBoundingBox(), paint);
      break;
    case EllipseGeometryFastPath:
      context.drawOval(m_layoutSVGShape.objectBoundingBox(), paint);
      break;
    default: {
      PathWithTemporaryWindingRule pathWithWinding(m_layoutSVGShape.path(),
                                                   fillType);
      context.drawPath(pathWithWinding.getSkPath(), paint);
    }
  }
}

void SVGShapePainter::strokeShape(GraphicsContext& context,
                                  const PaintFlags& paint) {
  if (!m_layoutSVGShape.style()->svgStyle().hasVisibleStroke())
    return;

  switch (m_layoutSVGShape.geometryCodePath()) {
    case RectGeometryFastPath:
      context.drawRect(m_layoutSVGShape.objectBoundingBox(), paint);
      break;
    case EllipseGeometryFastPath:
      context.drawOval(m_layoutSVGShape.objectBoundingBox(), paint);
      break;
    default:
      DCHECK(m_layoutSVGShape.hasPath());
      Path* usePath = &m_layoutSVGShape.path();
      if (m_layoutSVGShape.hasNonScalingStroke())
        usePath = m_layoutSVGShape.nonScalingStrokePath(
            usePath, m_layoutSVGShape.nonScalingStrokeTransform());
      context.drawPath(usePath->getSkPath(), paint);
  }
}

void SVGShapePainter::paintMarkers(const PaintInfo& paintInfo,
                                   const FloatRect& boundingBox) {
  const Vector<MarkerPosition>* markerPositions =
      m_layoutSVGShape.markerPositions();
  if (!markerPositions || markerPositions->isEmpty())
    return;

  SVGResources* resources =
      SVGResourcesCache::cachedResourcesForLayoutObject(&m_layoutSVGShape);
  if (!resources)
    return;

  LayoutSVGResourceMarker* markerStart = resources->markerStart();
  LayoutSVGResourceMarker* markerMid = resources->markerMid();
  LayoutSVGResourceMarker* markerEnd = resources->markerEnd();
  if (!markerStart && !markerMid && !markerEnd)
    return;

  float strokeWidth = m_layoutSVGShape.strokeWidth();

  for (const MarkerPosition& markerPosition : *markerPositions) {
    if (const LayoutSVGResourceMarker* marker = SVGMarkerData::markerForType(
            markerPosition.type, markerStart, markerMid, markerEnd)) {
      SkPictureBuilder pictureBuilder(boundingBox, nullptr, &paintInfo.context);
      PaintInfo markerPaintInfo(pictureBuilder.context(), paintInfo);

      // It's expensive to track the transformed paint cull rect for each
      // marker so just disable culling. The shape paint call will already
      // be culled if it is outside the paint info cull rect.
      markerPaintInfo.m_cullRect.m_rect = LayoutRect::infiniteIntRect();

      paintMarker(markerPaintInfo, *marker, markerPosition, strokeWidth);
      pictureBuilder.endRecording()->playback(paintInfo.context.canvas());
    }
  }
}

void SVGShapePainter::paintMarker(const PaintInfo& paintInfo,
                                  const LayoutSVGResourceMarker& marker,
                                  const MarkerPosition& position,
                                  float strokeWidth) {
  if (!marker.shouldPaint())
    return;

  TransformRecorder transformRecorder(
      paintInfo.context, marker,
      marker.markerTransformation(position.origin, position.angle,
                                  strokeWidth));
  Optional<FloatClipRecorder> clipRecorder;
  if (SVGLayoutSupport::isOverflowHidden(&marker))
    clipRecorder.emplace(paintInfo.context, marker, paintInfo.phase,
                         marker.viewport());
  SVGContainerPainter(marker).paint(paintInfo);
}

}  // namespace blink
