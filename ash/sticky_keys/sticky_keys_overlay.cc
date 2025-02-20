// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/sticky_keys/sticky_keys_overlay.h"

#include "ash/public/cpp/shell_window_ids.h"
#include "ash/shell.h"
#include "ash/sticky_keys/sticky_keys_controller.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "grit/ash_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace ash {

namespace {

// Horizontal offset of the overlay from the top left of the screen.
const int kHorizontalOverlayOffset = 18;

// Vertical offset of the overlay from the top left of the screen.
const int kVerticalOverlayOffset = 18;

// Font style used for modifier key labels.
const ui::ResourceBundle::FontStyle kKeyLabelFontStyle =
    ui::ResourceBundle::LargeFont;

// Duration of slide animation when overlay is shown or hidden.
const int kSlideAnimationDurationMs = 100;
}

///////////////////////////////////////////////////////////////////////////////
//  StickyKeyOverlayLabel
class StickyKeyOverlayLabel : public views::Label {
 public:
  explicit StickyKeyOverlayLabel(const std::string& key_name);

  ~StickyKeyOverlayLabel() override;

  StickyKeyState state() const { return state_; }

  void SetKeyState(StickyKeyState state);

 private:
  StickyKeyState state_;

  DISALLOW_COPY_AND_ASSIGN(StickyKeyOverlayLabel);
};

StickyKeyOverlayLabel::StickyKeyOverlayLabel(const std::string& key_name)
    : state_(STICKY_KEY_STATE_DISABLED) {
  ui::ResourceBundle* rb = &ui::ResourceBundle::GetSharedInstance();

  SetText(base::UTF8ToUTF16(key_name));
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetFontList(rb->GetFontList(kKeyLabelFontStyle));
  SetAutoColorReadabilityEnabled(false);
  SetEnabledColor(SkColorSetARGB(0x80, 0xFF, 0xFF, 0xFF));
  SetDisabledColor(SkColorSetARGB(0x80, 0xFF, 0xFF, 0xFF));
  SetSubpixelRenderingEnabled(false);
}

StickyKeyOverlayLabel::~StickyKeyOverlayLabel() {}

void StickyKeyOverlayLabel::SetKeyState(StickyKeyState state) {
  state_ = state;
  SkColor label_color;
  int style;
  switch (state) {
    case STICKY_KEY_STATE_ENABLED:
      style = gfx::Font::NORMAL;
      label_color = SkColorSetA(enabled_color(), 0xFF);
      break;
    case STICKY_KEY_STATE_LOCKED:
      style = gfx::Font::UNDERLINE;
      label_color = SkColorSetA(enabled_color(), 0xFF);
      break;
    default:
      style = gfx::Font::NORMAL;
      label_color = SkColorSetA(enabled_color(), 0x80);
  }

  SetEnabledColor(label_color);
  SetDisabledColor(label_color);
  SetFontList(font_list().DeriveWithStyle(style));
}

///////////////////////////////////////////////////////////////////////////////
//  StickyKeysOverlayView
class StickyKeysOverlayView : public views::View {
 public:
  // This object is not owned by the views hiearchy or by the widget. The
  // StickyKeysOverlay is the only class that should create and destroy this
  // view.
  StickyKeysOverlayView();

  ~StickyKeysOverlayView() override;

  // views::View overrides:
  void OnPaint(gfx::Canvas* canvas) override;

  void SetKeyState(ui::EventFlags modifier, StickyKeyState state);

  StickyKeyState GetKeyState(ui::EventFlags modifier);

  void SetModifierVisible(ui::EventFlags modifier, bool visible);
  bool GetModifierVisible(ui::EventFlags modifier);

 private:
  void AddKeyLabel(ui::EventFlags modifier, const std::string& key_label);

  typedef std::map<ui::EventFlags, StickyKeyOverlayLabel*> ModifierLabelMap;
  ModifierLabelMap modifier_label_map_;

  DISALLOW_COPY_AND_ASSIGN(StickyKeysOverlayView);
};

StickyKeysOverlayView::StickyKeysOverlayView() {
  // The parent StickyKeysOverlay object owns this view.
  set_owned_by_client();

  const gfx::Font& font =
      ui::ResourceBundle::GetSharedInstance().GetFont(kKeyLabelFontStyle);
  int font_size = font.GetFontSize();
  int font_padding = font.GetHeight() - font.GetBaseline();

  // Text should have a margin of 0.5 times the font size on each side, so
  // the spacing between two labels will be the same as the font size.
  int horizontal_spacing = font_size / 2;
  int vertical_spacing = font_size / 2 - font_padding;
  int child_spacing = font_size - 2 * font_padding;

  SetLayoutManager(new views::BoxLayout(views::BoxLayout::kVertical,
                                        horizontal_spacing, vertical_spacing,
                                        child_spacing));
  AddKeyLabel(ui::EF_CONTROL_DOWN,
              l10n_util::GetStringUTF8(IDS_ASH_CONTROL_KEY));
  AddKeyLabel(ui::EF_ALT_DOWN, l10n_util::GetStringUTF8(IDS_ASH_ALT_KEY));
  AddKeyLabel(ui::EF_SHIFT_DOWN, l10n_util::GetStringUTF8(IDS_ASH_SHIFT_KEY));
  AddKeyLabel(ui::EF_COMMAND_DOWN,
              l10n_util::GetStringUTF8(IDS_ASH_SEARCH_KEY));
  AddKeyLabel(ui::EF_ALTGR_DOWN, l10n_util::GetStringUTF8(IDS_ASH_ALTGR_KEY));
  AddKeyLabel(ui::EF_MOD3_DOWN, l10n_util::GetStringUTF8(IDS_ASH_MOD3_KEY));
}

StickyKeysOverlayView::~StickyKeysOverlayView() {}

void StickyKeysOverlayView::OnPaint(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(SkColorSetARGB(0xB3, 0x55, 0x55, 0x55));
  canvas->DrawRoundRect(GetLocalBounds(), 2, flags);
  views::View::OnPaint(canvas);
}

void StickyKeysOverlayView::SetKeyState(ui::EventFlags modifier,
                                        StickyKeyState state) {
  ModifierLabelMap::iterator it = modifier_label_map_.find(modifier);
  DCHECK(it != modifier_label_map_.end());
  if (it != modifier_label_map_.end()) {
    StickyKeyOverlayLabel* label = it->second;
    label->SetKeyState(state);
  }
}

StickyKeyState StickyKeysOverlayView::GetKeyState(ui::EventFlags modifier) {
  ModifierLabelMap::iterator it = modifier_label_map_.find(modifier);
  DCHECK(it != modifier_label_map_.end());
  return it->second->state();
}

void StickyKeysOverlayView::SetModifierVisible(ui::EventFlags modifier,
                                               bool visible) {
  ModifierLabelMap::iterator it = modifier_label_map_.find(modifier);
  DCHECK(it != modifier_label_map_.end());
  it->second->SetVisible(visible);
}

bool StickyKeysOverlayView::GetModifierVisible(ui::EventFlags modifier) {
  ModifierLabelMap::iterator it = modifier_label_map_.find(modifier);
  DCHECK(it != modifier_label_map_.end());
  return it->second->visible();
}

void StickyKeysOverlayView::AddKeyLabel(ui::EventFlags modifier,
                                        const std::string& key_label) {
  StickyKeyOverlayLabel* label = new StickyKeyOverlayLabel(key_label);
  AddChildView(label);
  modifier_label_map_[modifier] = label;
}

///////////////////////////////////////////////////////////////////////////////
//  StickyKeysOverlay
StickyKeysOverlay::StickyKeysOverlay()
    : is_visible_(false),
      overlay_view_(new StickyKeysOverlayView),
      widget_size_(overlay_view_->GetPreferredSize()) {
  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_POPUP;
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.accept_events = false;
  params.keep_on_top = true;
  params.remove_standard_frame = true;
  params.bounds = CalculateOverlayBounds();
  params.parent = Shell::GetContainer(Shell::GetTargetRootWindow(),
                                      kShellWindowId_OverlayContainer);
  overlay_widget_.reset(new views::Widget);
  overlay_widget_->Init(params);
  overlay_widget_->SetVisibilityChangedAnimationsEnabled(false);
  overlay_widget_->SetContentsView(overlay_view_.get());
  overlay_widget_->GetNativeView()->SetName("StickyKeysOverlay");
}

StickyKeysOverlay::~StickyKeysOverlay() {
  // Remove ourself from the animator to avoid being re-entrantly called in
  // |overlay_widget_|'s destructor.
  ui::Layer* layer = overlay_widget_->GetLayer();
  if (layer) {
    ui::LayerAnimator* animator = layer->GetAnimator();
    if (animator)
      animator->RemoveObserver(this);
  }
}

void StickyKeysOverlay::Show(bool visible) {
  if (is_visible_ == visible)
    return;

  is_visible_ = visible;
  if (is_visible_)
    overlay_widget_->Show();
  overlay_widget_->SetBounds(CalculateOverlayBounds());

  ui::LayerAnimator* animator = overlay_widget_->GetLayer()->GetAnimator();
  animator->AddObserver(this);

  // Ensure transform is correct before beginning animation.
  if (!animator->is_animating()) {
    int sign = is_visible_ ? -1 : 1;
    gfx::Transform transform;
    transform.Translate(
        sign * (widget_size_.width() + kHorizontalOverlayOffset), 0);
    overlay_widget_->GetLayer()->SetTransform(transform);
  }

  ui::ScopedLayerAnimationSettings settings(animator);
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  settings.SetTweenType(visible ? gfx::Tween::EASE_OUT : gfx::Tween::EASE_IN);
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kSlideAnimationDurationMs));

  overlay_widget_->GetLayer()->SetTransform(gfx::Transform());
}

void StickyKeysOverlay::SetModifierVisible(ui::EventFlags modifier,
                                           bool visible) {
  overlay_view_->SetModifierVisible(modifier, visible);
  widget_size_ = overlay_view_->GetPreferredSize();
}

bool StickyKeysOverlay::GetModifierVisible(ui::EventFlags modifier) {
  return overlay_view_->GetModifierVisible(modifier);
}

void StickyKeysOverlay::SetModifierKeyState(ui::EventFlags modifier,
                                            StickyKeyState state) {
  overlay_view_->SetKeyState(modifier, state);
}

StickyKeyState StickyKeysOverlay::GetModifierKeyState(ui::EventFlags modifier) {
  return overlay_view_->GetKeyState(modifier);
}

views::Widget* StickyKeysOverlay::GetWidgetForTesting() {
  return overlay_widget_.get();
}

gfx::Rect StickyKeysOverlay::CalculateOverlayBounds() {
  int x = is_visible_ ? kHorizontalOverlayOffset : -widget_size_.width();
  return gfx::Rect(gfx::Point(x, kVerticalOverlayOffset), widget_size_);
}

void StickyKeysOverlay::OnLayerAnimationEnded(
    ui::LayerAnimationSequence* sequence) {
  ui::LayerAnimator* animator = overlay_widget_->GetLayer()->GetAnimator();
  if (animator)
    animator->RemoveObserver(this);
  if (!is_visible_)
    overlay_widget_->Hide();
}

void StickyKeysOverlay::OnLayerAnimationAborted(
    ui::LayerAnimationSequence* sequence) {
  ui::LayerAnimator* animator = overlay_widget_->GetLayer()->GetAnimator();
  if (animator)
    animator->RemoveObserver(this);
}

void StickyKeysOverlay::OnLayerAnimationScheduled(
    ui::LayerAnimationSequence* sequence) {}

}  // namespace ash
