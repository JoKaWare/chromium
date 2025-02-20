/*
 * Copyright (C) Research In Motion Limited 2009-2010. All rights reserved.
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

#ifndef LayoutSVGResourcePaintServer_h
#define LayoutSVGResourcePaintServer_h

#include "core/layout/svg/LayoutSVGResourceContainer.h"
#include "platform/graphics/Color.h"
#include "platform/graphics/Gradient.h"
#include "platform/graphics/Pattern.h"
#include "platform/transforms/AffineTransform.h"
#include "wtf/Allocator.h"

namespace blink {

enum LayoutSVGResourceMode {
  ApplyToFillMode,
  ApplyToStrokeMode,
};

class LayoutObject;
class LayoutSVGResourcePaintServer;
class ComputedStyle;

class SVGPaintServer {
  STACK_ALLOCATED();

 public:
  explicit SVGPaintServer(Color);
  SVGPaintServer(PassRefPtr<Gradient>, const AffineTransform&);
  SVGPaintServer(PassRefPtr<Pattern>, const AffineTransform&);

  static SVGPaintServer requestForLayoutObject(const LayoutObject&,
                                               const ComputedStyle&,
                                               LayoutSVGResourceMode);
  static bool existsForLayoutObject(const LayoutObject&,
                                    const ComputedStyle&,
                                    LayoutSVGResourceMode);

  // TODO(enne): rename this
  void applyToSkPaint(PaintFlags&, float paintAlpha);

  static SVGPaintServer invalid() {
    return SVGPaintServer(Color(Color::transparent));
  }
  bool isValid() const { return m_color != Color::transparent; }

  bool isTransformDependent() const { return m_gradient || m_pattern; }
  void prependTransform(const AffineTransform&);

 private:
  RefPtr<Gradient> m_gradient;
  RefPtr<Pattern> m_pattern;
  AffineTransform m_transform;  // Used for gradient/pattern shaders.
  Color m_color;
};

// If |SVGPaintDescription::hasFallback| is true, |SVGPaintDescription::color|
// is set to a fallback color.
struct SVGPaintDescription {
  STACK_ALLOCATED();
  SVGPaintDescription()
      : resource(nullptr), isValid(false), hasFallback(false) {}
  SVGPaintDescription(Color color)
      : resource(nullptr), color(color), isValid(true), hasFallback(false) {}
  SVGPaintDescription(LayoutSVGResourcePaintServer* resource)
      : resource(resource), isValid(true), hasFallback(false) {
    ASSERT(resource);
  }
  SVGPaintDescription(LayoutSVGResourcePaintServer* resource,
                      Color fallbackColor)
      : resource(resource),
        color(fallbackColor),
        isValid(true),
        hasFallback(true) {
    ASSERT(resource);
  }

  LayoutSVGResourcePaintServer* resource;
  Color color;
  bool isValid;
  bool hasFallback;
};

class LayoutSVGResourcePaintServer : public LayoutSVGResourceContainer {
 public:
  LayoutSVGResourcePaintServer(SVGElement*);
  ~LayoutSVGResourcePaintServer() override;

  virtual SVGPaintServer preparePaintServer(const LayoutObject&) = 0;

  // Helper utilities used in to access the underlying resources for DRT.
  static SVGPaintDescription requestPaintDescription(const LayoutObject&,
                                                     const ComputedStyle&,
                                                     LayoutSVGResourceMode);
};

DEFINE_TYPE_CASTS(LayoutSVGResourcePaintServer,
                  LayoutSVGResourceContainer,
                  resource,
                  resource->isSVGPaintServer(),
                  resource.isSVGPaintServer());

}  // namespace blink

#endif
