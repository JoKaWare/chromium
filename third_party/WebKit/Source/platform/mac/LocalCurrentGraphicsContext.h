/*
 * Copyright (C) 2006 Apple Computer, Inc.
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

#include "platform/PlatformExport.h"
#include "platform/geometry/IntRect.h"
#include "platform/graphics/paint/PaintCanvas.h"
#include "skia/ext/skia_utils_mac.h"
#include "wtf/Noncopyable.h"

OBJC_CLASS NSGraphicsContext;

namespace blink {

class GraphicsContext;

// This class automatically saves and restores the current NSGraphicsContext for
// functions which call out into AppKit and rely on the currentContext being set
class PLATFORM_EXPORT LocalCurrentGraphicsContext {
  WTF_MAKE_NONCOPYABLE(LocalCurrentGraphicsContext);

 public:
  LocalCurrentGraphicsContext(GraphicsContext&, const IntRect& dirtyRect);
  LocalCurrentGraphicsContext(PaintCanvas*,
                              float deviceScaleFactor,
                              const IntRect& dirtyRect);
  ~LocalCurrentGraphicsContext();
  CGContextRef cgContext();

 private:
  PaintCanvas* m_savedCanvas;
  NSGraphicsContext* m_savedNSGraphicsContext;
  bool m_didSetGraphicsContext;
  IntRect m_inflatedDirtyRect;
  skia::SkiaBitLocker m_skiaBitLocker;
};
}
