// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/examples/scroll_view_example.h"

#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/layout/grid_layout.h"
#include "ui/views/painter.h"
#include "ui/views/view.h"

using base::ASCIIToUTF16;

namespace views {
namespace examples {

// ScrollView's content, which draws gradient color on background.
// TODO(oshima): add child views as well.
class ScrollViewExample::ScrollableView : public View {
 public:
  ScrollableView() {
    SetColor(SK_ColorRED, SK_ColorCYAN);
    AddChildView(new LabelButton(NULL, ASCIIToUTF16("Button")));
    AddChildView(new RadioButton(ASCIIToUTF16("Radio Button"), 0));
  }

  gfx::Size GetPreferredSize() const override {
    return gfx::Size(width(), height());
  }

  void SetColor(SkColor from, SkColor to) {
    Background* background = Background::CreateBackgroundPainter(
        Painter::CreateVerticalGradient(from, to));
    background->SetNativeControlColor(
        color_utils::AlphaBlend(from, to, 128));
    set_background(background);
  }

  void PlaceChildY(int index, int y) {
    View* view = child_at(index);
    gfx::Size size = view->GetPreferredSize();
    view->SetBounds(0, y, size.width(), size.height());
  }

  void Layout() override {
    PlaceChildY(0, 0);
    PlaceChildY(1, height() / 2);
    SizeToPreferredSize();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ScrollableView);
};

ScrollViewExample::ScrollViewExample() : ExampleBase("Scroll View") {
}

ScrollViewExample::~ScrollViewExample() {
}

void ScrollViewExample::CreateExampleView(View* container) {
  wide_ = new LabelButton(this, ASCIIToUTF16("Wide"));
  tall_ = new LabelButton(this, ASCIIToUTF16("Tall"));
  big_square_ = new LabelButton(this, ASCIIToUTF16("Big Square"));
  small_square_ = new LabelButton(this, ASCIIToUTF16("Small Square"));
  scroll_to_ = new LabelButton(this, ASCIIToUTF16("Scroll to"));
  scrollable_ = new ScrollableView();
  scroll_view_ = new ScrollView();
  scroll_view_->SetContents(scrollable_);
  scrollable_->SetBounds(0, 0, 1000, 100);
  scrollable_->SetColor(SK_ColorYELLOW, SK_ColorCYAN);

  GridLayout* layout = new GridLayout(container);
  container->SetLayoutManager(layout);

  // Add scroll view.
  ColumnSet* column_set = layout->AddColumnSet(0);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                        GridLayout::USE_PREF, 0, 0);
  layout->StartRow(1, 0);
  layout->AddView(scroll_view_);

  // Add control buttons.
  column_set = layout->AddColumnSet(1);
  for (int i = 0; i < 5; i++) {
    column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                          GridLayout::USE_PREF, 0, 0);
  }
  layout->StartRow(0, 1);
  layout->AddView(wide_);
  layout->AddView(tall_);
  layout->AddView(big_square_);
  layout->AddView(small_square_);
  layout->AddView(scroll_to_);
}

void ScrollViewExample::ButtonPressed(Button* sender, const ui::Event& event) {
  if (sender == wide_) {
    scrollable_->SetBounds(0, 0, 1000, 100);
    scrollable_->SetColor(SK_ColorYELLOW, SK_ColorCYAN);
  } else if (sender == tall_) {
    scrollable_->SetBounds(0, 0, 100, 1000);
    scrollable_->SetColor(SK_ColorRED, SK_ColorCYAN);
  } else if (sender == big_square_) {
    scrollable_->SetBounds(0, 0, 1000, 1000);
    scrollable_->SetColor(SK_ColorRED, SK_ColorGREEN);
  } else if (sender == small_square_) {
    scrollable_->SetBounds(0, 0, 100, 100);
    scrollable_->SetColor(SK_ColorYELLOW, SK_ColorGREEN);
  } else if (sender == scroll_to_) {
    scroll_view_->contents()->ScrollRectToVisible(
        gfx::Rect(20, 500, 1000, 500));
  }
  scroll_view_->Layout();
}

}  // namespace examples
}  // namespace views
