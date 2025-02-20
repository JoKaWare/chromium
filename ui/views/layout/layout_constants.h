// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_LAYOUT_LAYOUT_CONSTANTS_H_
#define UI_VIEWS_LAYOUT_LAYOUT_CONSTANTS_H_

#include "build/build_config.h"
#include "ui/views/layout/grid_layout.h"

// This file contains some constants we use to implement our standard panel
// layout.
// see: spec 21/4

namespace views {

// Left or right margin.
constexpr int kPanelHorizMargin = 13;

// Top or bottom margin.
constexpr int kPanelVertMargin = 13;

// When several controls are aligned vertically, the baseline should be spaced
// by the following number of pixels.
constexpr int kPanelVerticalSpacing = 32;

// Vertical spacing between sub UI.
constexpr int kPanelSubVerticalSpacing = 24;

// Vertical spacing between a label and some control.
constexpr int kLabelToControlVerticalSpacing = 8;

// Small horizontal spacing between controls that are logically related.
constexpr int kRelatedControlSmallHorizontalSpacing = 8;

// Horizontal spacing between controls that are logically related.
constexpr int kRelatedControlHorizontalSpacing = 8;

// Vertical spacing between controls that are logically related.
constexpr int kRelatedControlVerticalSpacing = 8;

// Small vertical spacing between controls that are logically related.
constexpr int kRelatedControlSmallVerticalSpacing = 4;

// Horizontal spacing between controls that are logically unrelated.
constexpr int kUnrelatedControlHorizontalSpacing = 12;

// Larger horizontal spacing between unrelated controls.
constexpr int kUnrelatedControlLargeHorizontalSpacing = 20;

// Vertical spacing between controls that are logically unrelated.
constexpr int kUnrelatedControlVerticalSpacing = 20;

// Larger vertical spacing between unrelated controls.
constexpr int kUnrelatedControlLargeVerticalSpacing = 30;

// Vertical spacing between the edge of the window and the
// top or bottom of a button.
constexpr int kButtonVEdgeMargin = 9;

// Horizontal spacing between the edge of the window and the
// left or right of a button.
constexpr int kButtonHEdgeMargin = 13;

// Vertical spacing between the edge of the window and the
// top or bottom of a button (when using new style dialogs).
constexpr int kButtonVEdgeMarginNew = 20;

// Horizontal spacing between the edge of the window and the
// left or right of a button (when using new style dialogs).
constexpr int kButtonHEdgeMarginNew = 20;

// Spacing between the edge of the window and the edge of the close button.
const int kCloseButtonMargin = 7;

// Horizontal spacing between buttons that are logically related.
constexpr int kRelatedButtonHSpacing = 6;

// Indent of checkboxes relative to related text.
constexpr int kCheckboxIndent = 10;

// Horizontal spacing between the end of an item (i.e. an icon or a checkbox)
// and the start of its corresponding text.
constexpr int kItemLabelSpacing = 10;

}  // namespace views

#endif  // UI_VIEWS_LAYOUT_LAYOUT_CONSTANTS_H_
