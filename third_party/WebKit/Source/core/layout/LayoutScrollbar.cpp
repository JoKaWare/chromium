/*
 * Copyright (C) 2008, 2009 Apple Inc. All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/layout/LayoutScrollbar.h"

#include "core/css/PseudoStyleRequest.h"
#include "core/frame/FrameView.h"
#include "core/layout/LayoutPart.h"
#include "core/layout/LayoutScrollbarPart.h"
#include "core/layout/LayoutScrollbarTheme.h"
#include "core/layout/LayoutView.h"
#include "core/layout/api/LayoutAPIShim.h"
#include "core/layout/api/LayoutPartItem.h"
#include "core/paint/ObjectPaintInvalidator.h"
#include "platform/graphics/GraphicsContext.h"

namespace blink {

Scrollbar* LayoutScrollbar::createCustomScrollbar(
    ScrollableArea* scrollableArea,
    ScrollbarOrientation orientation,
    Element* styleSource) {
  return new LayoutScrollbar(scrollableArea, orientation, styleSource);
}

LayoutScrollbar::LayoutScrollbar(ScrollableArea* scrollableArea,
                                 ScrollbarOrientation orientation,
                                 Element* styleSource)
    : Scrollbar(scrollableArea,
                orientation,
                RegularScrollbar,
                nullptr,
                LayoutScrollbarTheme::layoutScrollbarTheme()),
      m_styleSource(styleSource) {
  DCHECK(styleSource);

  // FIXME: We need to do this because LayoutScrollbar::styleChanged is called
  // as soon as the scrollbar is created.

  // Update the scrollbar size.
  IntRect rect(0, 0, 0, 0);
  updateScrollbarPart(ScrollbarBGPart);
  if (LayoutScrollbarPart* part = m_parts.get(ScrollbarBGPart)) {
    part->layout();
    rect.setSize(flooredIntSize(part->size()));
  } else if (this->orientation() == HorizontalScrollbar) {
    rect.setWidth(this->width());
  } else {
    rect.setHeight(this->height());
  }

  setFrameRect(rect);
}

LayoutScrollbar::~LayoutScrollbar() {
  if (m_parts.isEmpty())
    return;

  // When a scrollbar is detached from its parent (causing all parts removal)
  // and ready to be destroyed, its destruction can be delayed because of
  // RefPtr maintained in other classes such as EventHandler
  // (m_lastScrollbarUnderMouse).
  // Meanwhile, we can have a call to updateScrollbarPart which recreates the
  // scrollbar part. So, we need to destroy these parts since we don't want them
  // to call on a destroyed scrollbar. See webkit bug 68009.
  updateScrollbarParts(true);
}

DEFINE_TRACE(LayoutScrollbar) {
  visitor->trace(m_styleSource);
  Scrollbar::trace(visitor);
}

LayoutBox* LayoutScrollbar::styleSource() const {
  return m_styleSource && m_styleSource->layoutObject()
             ? m_styleSource->layoutObject()->enclosingBox()
             : 0;
}

void LayoutScrollbar::setParent(Widget* parent) {
  Scrollbar::setParent(parent);
  if (!parent) {
    // Destroy all of the scrollbar's LayoutBoxes.
    updateScrollbarParts(true);
  }
}

void LayoutScrollbar::setEnabled(bool e) {
  bool wasEnabled = enabled();
  Scrollbar::setEnabled(e);
  if (wasEnabled != e)
    updateScrollbarParts();
}

void LayoutScrollbar::styleChanged() {
  updateScrollbarParts();
}

void LayoutScrollbar::setHoveredPart(ScrollbarPart part) {
  if (part == m_hoveredPart)
    return;

  ScrollbarPart oldPart = m_hoveredPart;
  m_hoveredPart = part;

  updateScrollbarPart(oldPart);
  updateScrollbarPart(m_hoveredPart);

  updateScrollbarPart(ScrollbarBGPart);
  updateScrollbarPart(TrackBGPart);
}

void LayoutScrollbar::setPressedPart(ScrollbarPart part) {
  ScrollbarPart oldPart = m_pressedPart;
  Scrollbar::setPressedPart(part);

  updateScrollbarPart(oldPart);
  updateScrollbarPart(part);

  updateScrollbarPart(ScrollbarBGPart);
  updateScrollbarPart(TrackBGPart);
}

PassRefPtr<ComputedStyle> LayoutScrollbar::getScrollbarPseudoStyle(
    ScrollbarPart partType,
    PseudoId pseudoId) {
  if (!styleSource())
    return nullptr;

  return styleSource()->getUncachedPseudoStyle(
      PseudoStyleRequest(pseudoId, this, partType), styleSource()->style());
}

void LayoutScrollbar::updateScrollbarParts(bool destroy) {
  updateScrollbarPart(ScrollbarBGPart, destroy);
  updateScrollbarPart(BackButtonStartPart, destroy);
  updateScrollbarPart(ForwardButtonStartPart, destroy);
  updateScrollbarPart(BackTrackPart, destroy);
  updateScrollbarPart(ThumbPart, destroy);
  updateScrollbarPart(ForwardTrackPart, destroy);
  updateScrollbarPart(BackButtonEndPart, destroy);
  updateScrollbarPart(ForwardButtonEndPart, destroy);
  updateScrollbarPart(TrackBGPart, destroy);

  if (destroy)
    return;

  // See if the scrollbar's thickness changed.  If so, we need to mark our
  // owning object as needing a layout.
  bool isHorizontal = orientation() == HorizontalScrollbar;
  int oldThickness = isHorizontal ? height() : width();
  int newThickness = 0;
  LayoutScrollbarPart* part = m_parts.get(ScrollbarBGPart);
  if (part) {
    part->layout();
    newThickness =
        (isHorizontal ? part->size().height() : part->size().width()).toInt();
  }

  if (newThickness != oldThickness) {
    setFrameRect(
        IntRect(location(), IntSize(isHorizontal ? width() : newThickness,
                                    isHorizontal ? newThickness : height())));
    if (LayoutBox* box = styleSource()) {
      if (box->isLayoutBlock())
        toLayoutBlock(box)->notifyScrollbarThicknessChanged();
      box->setChildNeedsLayout();
      if (m_scrollableArea)
        m_scrollableArea->setScrollCornerNeedsPaintInvalidation();
    }
  }
}

static PseudoId pseudoForScrollbarPart(ScrollbarPart part) {
  switch (part) {
    case BackButtonStartPart:
    case ForwardButtonStartPart:
    case BackButtonEndPart:
    case ForwardButtonEndPart:
      return PseudoIdScrollbarButton;
    case BackTrackPart:
    case ForwardTrackPart:
      return PseudoIdScrollbarTrackPiece;
    case ThumbPart:
      return PseudoIdScrollbarThumb;
    case TrackBGPart:
      return PseudoIdScrollbarTrack;
    case ScrollbarBGPart:
      return PseudoIdScrollbar;
    case NoPart:
    case AllParts:
      break;
  }
  ASSERT_NOT_REACHED();
  return PseudoIdScrollbar;
}

void LayoutScrollbar::updateScrollbarPart(ScrollbarPart partType,
                                          bool destroy) {
  if (partType == NoPart)
    return;

  RefPtr<ComputedStyle> partStyle =
      !destroy
          ? getScrollbarPseudoStyle(partType, pseudoForScrollbarPart(partType))
          : PassRefPtr<ComputedStyle>(nullptr);

  bool needLayoutObject =
      !destroy && partStyle && partStyle->display() != EDisplay::None;

  if (needLayoutObject && partStyle->display() != EDisplay::Block) {
    // See if we are a button that should not be visible according to OS
    // settings.
    WebScrollbarButtonsPlacement buttonsPlacement = theme().buttonsPlacement();
    switch (partType) {
      case BackButtonStartPart:
        needLayoutObject =
            (buttonsPlacement == WebScrollbarButtonsPlacementSingle ||
             buttonsPlacement == WebScrollbarButtonsPlacementDoubleStart ||
             buttonsPlacement == WebScrollbarButtonsPlacementDoubleBoth);
        break;
      case ForwardButtonStartPart:
        needLayoutObject =
            (buttonsPlacement == WebScrollbarButtonsPlacementDoubleStart ||
             buttonsPlacement == WebScrollbarButtonsPlacementDoubleBoth);
        break;
      case BackButtonEndPart:
        needLayoutObject =
            (buttonsPlacement == WebScrollbarButtonsPlacementDoubleEnd ||
             buttonsPlacement == WebScrollbarButtonsPlacementDoubleBoth);
        break;
      case ForwardButtonEndPart:
        needLayoutObject =
            (buttonsPlacement == WebScrollbarButtonsPlacementSingle ||
             buttonsPlacement == WebScrollbarButtonsPlacementDoubleEnd ||
             buttonsPlacement == WebScrollbarButtonsPlacementDoubleBoth);
        break;
      default:
        break;
    }
  }

  LayoutScrollbarPart* partLayoutObject = m_parts.get(partType);
  if (!partLayoutObject && needLayoutObject && m_scrollableArea) {
    partLayoutObject = LayoutScrollbarPart::createAnonymous(
        &styleSource()->document(), m_scrollableArea, this, partType);
    m_parts.set(partType, partLayoutObject);
    setNeedsPaintInvalidation(partType);
  } else if (partLayoutObject && !needLayoutObject) {
    m_parts.erase(partType);
    partLayoutObject->destroy();
    partLayoutObject = nullptr;
    if (!destroy)
      setNeedsPaintInvalidation(partType);
  }

  if (partLayoutObject)
    partLayoutObject->setStyleWithWritingModeOfParent(std::move(partStyle));
}

IntRect LayoutScrollbar::buttonRect(ScrollbarPart partType) const {
  LayoutScrollbarPart* partLayoutObject = m_parts.get(partType);
  if (!partLayoutObject)
    return IntRect();

  partLayoutObject->layout();

  bool isHorizontal = orientation() == HorizontalScrollbar;
  if (partType == BackButtonStartPart)
    return IntRect(
        location(),
        IntSize(
            isHorizontal ? partLayoutObject->pixelSnappedWidth() : width(),
            isHorizontal ? height() : partLayoutObject->pixelSnappedHeight()));
  if (partType == ForwardButtonEndPart) {
    return IntRect(
        isHorizontal ? x() + width() - partLayoutObject->pixelSnappedWidth()
                     : x(),
        isHorizontal ? y()
                     : y() + height() - partLayoutObject->pixelSnappedHeight(),
        isHorizontal ? partLayoutObject->pixelSnappedWidth() : width(),
        isHorizontal ? height() : partLayoutObject->pixelSnappedHeight());
  }

  if (partType == ForwardButtonStartPart) {
    IntRect previousButton = buttonRect(BackButtonStartPart);
    return IntRect(
        isHorizontal ? x() + previousButton.width() : x(),
        isHorizontal ? y() : y() + previousButton.height(),
        isHorizontal ? partLayoutObject->pixelSnappedWidth() : width(),
        isHorizontal ? height() : partLayoutObject->pixelSnappedHeight());
  }

  IntRect followingButton = buttonRect(ForwardButtonEndPart);
  return IntRect(
      isHorizontal
          ? x() + width() - followingButton.width() -
                partLayoutObject->pixelSnappedWidth()
          : x(),
      isHorizontal ? y()
                   : y() + height() - followingButton.height() -
                         partLayoutObject->pixelSnappedHeight(),
      isHorizontal ? partLayoutObject->pixelSnappedWidth() : width(),
      isHorizontal ? height() : partLayoutObject->pixelSnappedHeight());
}

IntRect LayoutScrollbar::trackRect(int startLength, int endLength) const {
  LayoutScrollbarPart* part = m_parts.get(TrackBGPart);
  if (part)
    part->layout();

  if (orientation() == HorizontalScrollbar) {
    int marginLeft = part ? part->marginLeft().toInt() : 0;
    int marginRight = part ? part->marginRight().toInt() : 0;
    startLength += marginLeft;
    endLength += marginRight;
    int totalLength = startLength + endLength;
    return IntRect(x() + startLength, y(), width() - totalLength, height());
  }

  int marginTop = part ? part->marginTop().toInt() : 0;
  int marginBottom = part ? part->marginBottom().toInt() : 0;
  startLength += marginTop;
  endLength += marginBottom;
  int totalLength = startLength + endLength;

  return IntRect(x(), y() + startLength, width(), height() - totalLength);
}

IntRect LayoutScrollbar::trackPieceRectWithMargins(
    ScrollbarPart partType,
    const IntRect& oldRect) const {
  LayoutScrollbarPart* partLayoutObject = m_parts.get(partType);
  if (!partLayoutObject)
    return oldRect;

  partLayoutObject->layout();

  IntRect rect = oldRect;
  if (orientation() == HorizontalScrollbar) {
    rect.setX((rect.x() + partLayoutObject->marginLeft()).toInt());
    rect.setWidth((rect.width() - partLayoutObject->marginWidth()).toInt());
  } else {
    rect.setY((rect.y() + partLayoutObject->marginTop()).toInt());
    rect.setHeight((rect.height() - partLayoutObject->marginHeight()).toInt());
  }
  return rect;
}

int LayoutScrollbar::minimumThumbLength() const {
  LayoutScrollbarPart* partLayoutObject = m_parts.get(ThumbPart);
  if (!partLayoutObject)
    return 0;
  partLayoutObject->layout();
  return (orientation() == HorizontalScrollbar
              ? partLayoutObject->size().width()
              : partLayoutObject->size().height())
      .toInt();
}

void LayoutScrollbar::invalidateDisplayItemClientsOfScrollbarParts() {
  for (auto& part : m_parts)
    ObjectPaintInvalidator(*part.value)
        .invalidateDisplayItemClientsIncludingNonCompositingDescendants(
            PaintInvalidationScroll);
}

}  // namespace blink
