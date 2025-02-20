// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_CHROMEOS_IME_CANDIDATE_VIEW_H_
#define UI_CHROMEOS_IME_CANDIDATE_VIEW_H_

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "ui/base/ime/candidate_window.h"
#include "ui/chromeos/ui_chromeos_export.h"
#include "ui/views/controls/button/custom_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/view.h"

namespace ui {
namespace ime {

// CandidateView renderes a row of a candidate.
class UI_CHROMEOS_EXPORT CandidateView : public views::CustomButton {
 public:
  CandidateView(views::ButtonListener* listener,
                ui::CandidateWindow::Orientation orientation);
  ~CandidateView() override {}

  void GetPreferredWidths(int* shortcut_width,
                          int* candidate_width);

  void SetWidths(int shortcut_width,
                 int candidate_width);

  void SetEntry(const ui::CandidateWindow::Entry& entry);

  // Sets infolist icon.
  void SetInfolistIcon(bool enable);

  void SetHighlighted(bool highlighted);

 private:
  friend class CandidateWindowViewTest;
  FRIEND_TEST_ALL_PREFIXES(CandidateWindowViewTest, ShortcutSettingTest);

  // Overridden from views::CustomButton:
  void StateChanged(ButtonState old_state) override;

  // Overridden from View:
  const char* GetClassName() const override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void Layout() override;
  gfx::Size GetPreferredSize() const override;

  // The orientation of the candidate view.
  ui::CandidateWindow::Orientation orientation_;

  // Views created in the class will be part of tree of |this|, so these
  // child views will be deleted when |this| is deleted.

  // The shortcut label renders shortcut numbers like 1, 2, and 3.
  views::Label* shortcut_label_;
  // The candidate label renders candidates.
  views::Label* candidate_label_;
  // The annotation label renders annotations.
  views::Label* annotation_label_;
  // The infolist icon.
  views::View* infolist_icon_;

  int shortcut_width_;
  int candidate_width_;
  bool highlighted_;

  DISALLOW_COPY_AND_ASSIGN(CandidateView);
};

}  // namespace ime
}  // namespace ui

#endif  // UI_CHROMEOS_IME_CANDIDATE_VIEW_H_
