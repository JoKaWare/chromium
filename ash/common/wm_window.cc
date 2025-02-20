// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/wm_window.h"

#include "ash/aura/aura_layout_manager_adapter.h"
#include "ash/aura/wm_shell_aura.h"
#include "ash/common/ash_constants.h"
#include "ash/common/shelf/shelf_item_types.h"
#include "ash/common/wm/window_state.h"
#include "ash/common/wm_layout_manager.h"
#include "ash/common/wm_transient_window_observer.h"
#include "ash/common/wm_window_observer.h"
#include "ash/common/wm_window_property.h"
#include "ash/public/cpp/shell_window_ids.h"
#include "ash/public/cpp/window_properties.h"
#include "ash/root_window_controller.h"
#include "ash/screen_util.h"
#include "ash/shell.h"
#include "ash/wm/resize_handle_window_targeter.h"
#include "ash/wm/resize_shadow_controller.h"
#include "ash/wm/window_animations.h"
#include "ash/wm/window_mirror_view.h"
#include "ash/wm/window_properties.h"
#include "ash/wm/window_state_aura.h"
#include "ash/wm/window_util.h"
#include "base/memory/ptr_util.h"
#include "services/ui/public/interfaces/window_manager_constants.mojom.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/env.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/mus/window_manager_delegate.h"
#include "ui/aura/mus/window_mus.h"
#include "ui/aura/mus/window_tree_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/class_property.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/wm/core/easy_resize_window_targeter.h"
#include "ui/wm/core/transient_window_manager.h"
#include "ui/wm/core/visibility_controller.h"
#include "ui/wm/core/window_util.h"

DECLARE_UI_CLASS_PROPERTY_TYPE(ash::WmWindow*);

namespace ash {

DEFINE_OWNED_UI_CLASS_PROPERTY_KEY(WmWindow, kWmWindowKey, nullptr);

static_assert(aura::Window::kInitialId == kShellWindowId_Invalid,
              "ids must match");

namespace {

// A tentative class to set the bounds on the window.
// TODO(oshima): Once all logic is cleaned up, move this to the real layout
// manager with proper friendship.
class BoundsSetter : public aura::LayoutManager {
 public:
  BoundsSetter() {}
  ~BoundsSetter() override {}

  // aura::LayoutManager overrides:
  void OnWindowResized() override {}
  void OnWindowAddedToLayout(aura::Window* child) override {}
  void OnWillRemoveWindowFromLayout(aura::Window* child) override {}
  void OnWindowRemovedFromLayout(aura::Window* child) override {}
  void OnChildWindowVisibilityChanged(aura::Window* child,
                                      bool visible) override {}
  void SetChildBounds(aura::Window* child,
                      const gfx::Rect& requested_bounds) override {}

  void SetBounds(aura::Window* window, const gfx::Rect& bounds) {
    SetChildBoundsDirect(window, bounds);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(BoundsSetter);
};

}  // namespace

// static
bool WmWindow::default_use_empty_minimum_size_for_testing_ = false;

WmWindow::~WmWindow() {
  if (added_transient_observer_)
    ::wm::TransientWindowManager::Get(window_)->RemoveObserver(this);

  window_->RemoveObserver(this);
}

// static
const WmWindow* WmWindow::Get(const aura::Window* window) {
  if (!window)
    return nullptr;

  const WmWindow* wm_window = window->GetProperty(kWmWindowKey);
  if (wm_window)
    return wm_window;
  // WmWindow is owned by the aura::Window.
  // TODO(sky): fix constness.
  return new WmWindow(const_cast<aura::Window*>(window));
}

// static
std::vector<WmWindow*> WmWindow::FromAuraWindows(
    const std::vector<aura::Window*>& aura_windows) {
  std::vector<WmWindow*> result(aura_windows.size());
  for (size_t i = 0; i < aura_windows.size(); ++i)
    result[i] = Get(aura_windows[i]);
  return result;
}

// static
std::vector<aura::Window*> WmWindow::ToAuraWindows(
    const std::vector<WmWindow*>& windows) {
  std::vector<aura::Window*> result(windows.size());
  for (size_t i = 0; i < windows.size(); ++i)
    result[i] = WmWindow::GetAuraWindow(windows[i]);
  return result;
}

// static
const aura::Window* WmWindow::GetAuraWindow(const WmWindow* wm_window) {
  return wm_window ? static_cast<const WmWindow*>(wm_window)->aura_window()
                   : nullptr;
}

bool WmWindow::ShouldUseExtendedHitRegion() const {
  const WmWindow* parent = Get(window_->parent());
  return parent &&
         static_cast<const WmWindow*>(parent)
             ->children_use_extended_hit_region_;
}

void WmWindow::Destroy() {
  delete window_;
  // WARNING: this has been deleted.
}

const WmWindow* WmWindow::GetRootWindow() const {
  return Get(window_->GetRootWindow());
}

RootWindowController* WmWindow::GetRootWindowController() {
  aura::Window* root = window_->GetRootWindow();
  return root ? RootWindowController::ForWindow(root) : nullptr;
}

WmShell* WmWindow::GetShell() const {
  return WmShell::Get();
}

void WmWindow::SetName(const char* name) {
  window_->SetName(name);
}

std::string WmWindow::GetName() const {
  return window_->GetName();
}

void WmWindow::SetTitle(const base::string16& title) {
  window_->SetTitle(title);
}

base::string16 WmWindow::GetTitle() const {
  return window_->GetTitle();
}

void WmWindow::SetShellWindowId(int id) {
  window_->set_id(id);
}

int WmWindow::GetShellWindowId() const {
  return window_->id();
}

ui::wm::WindowType WmWindow::GetType() const {
  return window_->type();
}

int WmWindow::GetAppType() const {
  return window_->GetProperty(aura::client::kAppType);
}

void WmWindow::SetAppType(int app_type) const {
  window_->SetProperty(aura::client::kAppType, app_type);
}

ui::Layer* WmWindow::GetLayer() {
  return window_->layer();
}

bool WmWindow::GetLayerTargetVisibility() {
  return GetLayer()->GetTargetVisibility();
}

bool WmWindow::GetLayerVisible() {
  return GetLayer()->visible();
}

display::Display WmWindow::GetDisplayNearestWindow() {
  return display::Screen::GetScreen()->GetDisplayNearestWindow(window_);
}

bool WmWindow::HasNonClientArea() {
  return window_->delegate() ? true : false;
}

int WmWindow::GetNonClientComponent(const gfx::Point& location) {
  return window_->delegate()
             ? window_->delegate()->GetNonClientComponent(location)
             : HTNOWHERE;
}

gfx::Point WmWindow::ConvertPointToTarget(const WmWindow* target,
                                          const gfx::Point& point) const {
  gfx::Point result(point);
  aura::Window::ConvertPointToTarget(window_, GetAuraWindow(target), &result);
  return result;
}

gfx::Point WmWindow::ConvertPointToScreen(const gfx::Point& point) const {
  gfx::Point result(point);
  ::wm::ConvertPointToScreen(window_, &result);
  return result;
}

gfx::Point WmWindow::ConvertPointFromScreen(const gfx::Point& point) const {
  gfx::Point result(point);
  ::wm::ConvertPointFromScreen(window_, &result);
  return result;
}

gfx::Rect WmWindow::ConvertRectToScreen(const gfx::Rect& rect) const {
  return ScreenUtil::ConvertRectToScreen(window_, rect);
}

gfx::Rect WmWindow::ConvertRectFromScreen(const gfx::Rect& rect) const {
  return ScreenUtil::ConvertRectFromScreen(window_, rect);
}

gfx::Size WmWindow::GetMinimumSize() const {
  return window_->delegate() && !use_empty_minimum_size_for_testing_
             ? window_->delegate()->GetMinimumSize()
             : gfx::Size();
}

gfx::Size WmWindow::GetMaximumSize() const {
  return window_->delegate() ? window_->delegate()->GetMaximumSize()
                             : gfx::Size();
}

bool WmWindow::GetTargetVisibility() const {
  return window_->TargetVisibility();
}

bool WmWindow::IsVisible() const {
  return window_->IsVisible();
}

void WmWindow::SetOpacity(float opacity) {
  window_->layer()->SetOpacity(opacity);
}

float WmWindow::GetTargetOpacity() const {
  return window_->layer()->GetTargetOpacity();
}

gfx::Rect WmWindow::GetMinimizeAnimationTargetBoundsInScreen() const {
  return ash::GetMinimizeAnimationTargetBoundsInScreen(window_);
}

void WmWindow::SetTransform(const gfx::Transform& transform) {
  window_->SetTransform(transform);
}

gfx::Transform WmWindow::GetTargetTransform() const {
  return window_->layer()->GetTargetTransform();
}

bool WmWindow::IsSystemModal() const {
  return window_->GetProperty(aura::client::kModalKey) == ui::MODAL_TYPE_SYSTEM;
}

bool WmWindow::GetBoolProperty(WmWindowProperty key) {
  switch (key) {
    case WmWindowProperty::DRAW_ATTENTION:
      return window_->GetProperty(aura::client::kDrawAttentionKey);

    case WmWindowProperty::PANEL_ATTACHED:
      return window_->GetProperty(kPanelAttachedKey);

    case WmWindowProperty::SNAP_CHILDREN_TO_PIXEL_BOUNDARY:
      return window_->GetProperty(kSnapChildrenToPixelBoundary);

    case WmWindowProperty::ALWAYS_ON_TOP:
      return window_->GetProperty(aura::client::kAlwaysOnTopKey);

    default:
      NOTREACHED();
      break;
  }

  return false;
}

void WmWindow::SetBoolProperty(WmWindowProperty key, bool value) {
  switch (key) {
    case WmWindowProperty::PANEL_ATTACHED:
      window_->SetProperty(kPanelAttachedKey, value);
      break;
    default:
      NOTREACHED();
      break;
  }
}

SkColor WmWindow::GetColorProperty(WmWindowProperty key) {
  if (key == WmWindowProperty::TOP_VIEW_COLOR)
    return window_->GetProperty(aura::client::kTopViewColor);

  NOTREACHED();
  return 0;
}

void WmWindow::SetColorProperty(WmWindowProperty key, SkColor value) {
  if (key == WmWindowProperty::TOP_VIEW_COLOR) {
    window_->SetProperty(aura::client::kTopViewColor, value);
    return;
  }

  NOTREACHED();
}

int WmWindow::GetIntProperty(WmWindowProperty key) {
  if (key == WmWindowProperty::MODAL_TYPE)
    return window_->GetProperty(aura::client::kModalKey);

  if (key == WmWindowProperty::SHELF_ID)
    return window_->GetProperty(kShelfIDKey);

  if (key == WmWindowProperty::SHELF_ITEM_TYPE) {
    if (aura::Env::GetInstance()->mode() == aura::Env::Mode::LOCAL ||
        window_->GetProperty(kShelfItemTypeKey) != TYPE_UNDEFINED) {
      return window_->GetProperty(kShelfItemTypeKey);
    }
    // Mash provides a default shelf item type for non-ignored windows.
    return GetWindowState()->ignored_by_shelf() ? TYPE_UNDEFINED : TYPE_APP;
  }

  if (key == WmWindowProperty::TOP_VIEW_INSET)
    return window_->GetProperty(aura::client::kTopViewInset);

  NOTREACHED();
  return 0;
}

void WmWindow::SetIntProperty(WmWindowProperty key, int value) {
  if (key == WmWindowProperty::SHELF_ID) {
    window_->SetProperty(kShelfIDKey, value);
    return;
  }
  if (key == WmWindowProperty::SHELF_ITEM_TYPE) {
    window_->SetProperty(kShelfItemTypeKey, value);
    return;
  }
  if (key == WmWindowProperty::TOP_VIEW_INSET) {
    window_->SetProperty(aura::client::kTopViewInset, value);
    return;
  }

  NOTREACHED();
}

std::string WmWindow::GetStringProperty(WmWindowProperty key) {
  if (key == WmWindowProperty::APP_ID) {
    std::string* value = window_->GetProperty(aura::client::kAppIdKey);
    return value ? *value : std::string();
  }

  NOTREACHED();
  return std::string();
}

void WmWindow::SetStringProperty(WmWindowProperty key,
                                 const std::string& value) {
  if (key == WmWindowProperty::APP_ID) {
    window_->SetProperty(aura::client::kAppIdKey, new std::string(value));
    return;
  }

  NOTREACHED();
}

gfx::ImageSkia WmWindow::GetWindowIcon() {
  gfx::ImageSkia* image = window_->GetProperty(aura::client::kWindowIconKey);
  return image ? *image : gfx::ImageSkia();
}

gfx::ImageSkia WmWindow::GetAppIcon() {
  gfx::ImageSkia* image = window_->GetProperty(aura::client::kAppIconKey);
  return image ? *image : gfx::ImageSkia();
}

const wm::WindowState* WmWindow::GetWindowState() const {
  return ash::wm::GetWindowState(window_);
}

WmWindow* WmWindow::GetToplevelWindow() {
  return Get(window_->GetToplevelWindow());
}

WmWindow* WmWindow::GetToplevelWindowForFocus() {
  return Get(::wm::GetToplevelWindow(window_));
}

void WmWindow::SetParentUsingContext(WmWindow* context,
                                     const gfx::Rect& screen_bounds) {
  aura::client::ParentWindowWithContext(window_, GetAuraWindow(context),
                                        screen_bounds);
}

void WmWindow::AddChild(WmWindow* window) {
  window_->AddChild(GetAuraWindow(window));
}

void WmWindow::RemoveChild(WmWindow* child) {
  window_->RemoveChild(GetAuraWindow(child));
}

const WmWindow* WmWindow::GetParent() const {
  return Get(window_->parent());
}

const WmWindow* WmWindow::GetTransientParent() const {
  return Get(::wm::GetTransientParent(window_));
}

std::vector<WmWindow*> WmWindow::GetTransientChildren() {
  return FromAuraWindows(::wm::GetTransientChildren(window_));
}

bool WmWindow::MoveToEventRoot(const ui::Event& event) {
  return ash::wm::MoveWindowToEventRoot(window_, event);
}

void WmWindow::SetLayoutManager(
    std::unique_ptr<WmLayoutManager> layout_manager) {
  // See ~AuraLayoutManagerAdapter for why SetLayoutManager(nullptr) is called.
  window_->SetLayoutManager(nullptr);
  if (!layout_manager)
    return;

  // |window_| takes ownership of AuraLayoutManagerAdapter.
  window_->SetLayoutManager(
      new AuraLayoutManagerAdapter(window_, std::move(layout_manager)));
}

WmLayoutManager* WmWindow::GetLayoutManager() {
  AuraLayoutManagerAdapter* adapter = AuraLayoutManagerAdapter::Get(window_);
  return adapter ? adapter->wm_layout_manager() : nullptr;
}

void WmWindow::SetVisibilityChangesAnimated() {
  ::wm::SetWindowVisibilityChangesAnimated(window_);
}

void WmWindow::SetVisibilityAnimationType(int type) {
  ::wm::SetWindowVisibilityAnimationType(window_, type);
}

void WmWindow::SetVisibilityAnimationDuration(base::TimeDelta delta) {
  ::wm::SetWindowVisibilityAnimationDuration(window_, delta);
}

void WmWindow::SetVisibilityAnimationTransition(
    ::wm::WindowVisibilityAnimationTransition transition) {
  ::wm::SetWindowVisibilityAnimationTransition(window_, transition);
}

void WmWindow::Animate(::wm::WindowAnimationType type) {
  ::wm::AnimateWindow(window_, type);
}

void WmWindow::StopAnimatingProperty(
    ui::LayerAnimationElement::AnimatableProperty property) {
  window_->layer()->GetAnimator()->StopAnimatingProperty(property);
}

void WmWindow::SetChildWindowVisibilityChangesAnimated() {
  ::wm::SetChildWindowVisibilityChangesAnimated(window_);
}

void WmWindow::SetMasksToBounds(bool value) {
  window_->layer()->SetMasksToBounds(value);
}

void WmWindow::SetBounds(const gfx::Rect& bounds) {
  window_->SetBounds(bounds);
}

void WmWindow::SetBoundsWithTransitionDelay(const gfx::Rect& bounds,
                                            base::TimeDelta delta) {
  if (::wm::WindowAnimationsDisabled(window_)) {
    window_->SetBounds(bounds);
    return;
  }

  ui::ScopedLayerAnimationSettings settings(window_->layer()->GetAnimator());
  settings.SetTransitionDuration(delta);
  window_->SetBounds(bounds);
}

void WmWindow::SetBoundsDirect(const gfx::Rect& bounds) {
  BoundsSetter().SetBounds(window_, bounds);
  wm::SnapWindowToPixelBoundary(window_);
}

void WmWindow::SetBoundsDirectAnimated(const gfx::Rect& bounds) {
  const int kBoundsChangeSlideDurationMs = 120;

  ui::Layer* layer = window_->layer();
  ui::ScopedLayerAnimationSettings slide_settings(layer->GetAnimator());
  slide_settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  slide_settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kBoundsChangeSlideDurationMs));
  SetBoundsDirect(bounds);
}

void WmWindow::SetBoundsDirectCrossFade(const gfx::Rect& bounds) {
  const gfx::Rect old_bounds = window_->bounds();

  // Create fresh layers for the window and all its children to paint into.
  // Takes ownership of the old layer and all its children, which will be
  // cleaned up after the animation completes.
  // Specify |set_bounds| to true here to keep the old bounds in the child
  // windows of |window|.
  std::unique_ptr<ui::LayerTreeOwner> old_layer_owner =
      ::wm::RecreateLayers(window_);
  ui::Layer* old_layer = old_layer_owner->root();
  DCHECK(old_layer);
  ui::Layer* new_layer = window_->layer();

  // Resize the window to the new size, which will force a layout and paint.
  SetBoundsDirect(bounds);

  // Ensure the higher-resolution layer is on top.
  bool old_on_top = (old_bounds.width() > bounds.width());
  if (old_on_top)
    old_layer->parent()->StackBelow(new_layer, old_layer);
  else
    old_layer->parent()->StackAbove(new_layer, old_layer);

  CrossFadeAnimation(window_, std::move(old_layer_owner), gfx::Tween::EASE_OUT);
}

void WmWindow::SetBoundsInScreen(const gfx::Rect& bounds_in_screen,
                                 const display::Display& dst_display) {
  window_->SetBoundsInScreen(bounds_in_screen, dst_display);
}

gfx::Rect WmWindow::GetBoundsInScreen() const {
  return window_->GetBoundsInScreen();
}

const gfx::Rect& WmWindow::GetBounds() const {
  return window_->bounds();
}

gfx::Rect WmWindow::GetTargetBounds() {
  return window_->GetTargetBounds();
}

void WmWindow::ClearRestoreBounds() {
  window_->ClearProperty(aura::client::kRestoreBoundsKey);
}

void WmWindow::SetRestoreBoundsInScreen(const gfx::Rect& bounds) {
  window_->SetProperty(aura::client::kRestoreBoundsKey, new gfx::Rect(bounds));
}

gfx::Rect WmWindow::GetRestoreBoundsInScreen() const {
  gfx::Rect* bounds = window_->GetProperty(aura::client::kRestoreBoundsKey);
  return bounds ? *bounds : gfx::Rect();
}

bool WmWindow::Contains(const WmWindow* other) const {
  return other ? window_->Contains(static_cast<const WmWindow*>(other)->window_)
               : false;
}

void WmWindow::SetShowState(ui::WindowShowState show_state) {
  window_->SetProperty(aura::client::kShowStateKey, show_state);
}

ui::WindowShowState WmWindow::GetShowState() const {
  return window_->GetProperty(aura::client::kShowStateKey);
}

void WmWindow::SetPreMinimizedShowState(ui::WindowShowState show_state) {
  window_->SetProperty(aura::client::kPreMinimizedShowStateKey, show_state);
}

ui::WindowShowState WmWindow::GetPreMinimizedShowState() const {
  return window_->GetProperty(aura::client::kPreMinimizedShowStateKey);
}

void WmWindow::SetPreFullscreenShowState(ui::WindowShowState show_state) {
  // We should never store the ui::SHOW_STATE_MINIMIZED as the show state before
  // fullscreen.
  DCHECK_NE(show_state, ui::SHOW_STATE_MINIMIZED);
  window_->SetProperty(aura::client::kPreFullscreenShowStateKey, show_state);
}

void WmWindow::SetRestoreOverrides(const gfx::Rect& bounds_override,
                                   ui::WindowShowState window_state_override) {
  if (bounds_override.IsEmpty()) {
    window_->ClearProperty(kRestoreShowStateOverrideKey);
    window_->ClearProperty(kRestoreBoundsOverrideKey);
    return;
  }
  window_->SetProperty(kRestoreShowStateOverrideKey, window_state_override);
  window_->SetProperty(kRestoreBoundsOverrideKey,
                       new gfx::Rect(bounds_override));
}

void WmWindow::SetLockedToRoot(bool value) {
  window_->SetProperty(kLockedToRootKey, value);
}

bool WmWindow::IsLockedToRoot() const {
  return window_->GetProperty(kLockedToRootKey);
}

void WmWindow::SetCapture() {
  window_->SetCapture();
}

bool WmWindow::HasCapture() {
  return window_->HasCapture();
}

void WmWindow::ReleaseCapture() {
  window_->ReleaseCapture();
}

bool WmWindow::HasRestoreBounds() const {
  return window_->GetProperty(aura::client::kRestoreBoundsKey) != nullptr;
}

bool WmWindow::CanMaximize() const {
  return (window_->GetProperty(aura::client::kResizeBehaviorKey) &
          ui::mojom::kResizeBehaviorCanMaximize) != 0;
}

bool WmWindow::CanMinimize() const {
  return (window_->GetProperty(aura::client::kResizeBehaviorKey) &
          ui::mojom::kResizeBehaviorCanMinimize) != 0;
}

bool WmWindow::CanResize() const {
  return (window_->GetProperty(aura::client::kResizeBehaviorKey) &
          ui::mojom::kResizeBehaviorCanResize) != 0;
}

bool WmWindow::CanActivate() const {
  // TODO(sky): for aura-mus need to key off CanFocus() as well, which is not
  // currently mirrored to ash.
  return ::wm::CanActivateWindow(window_);
}

void WmWindow::StackChildAtTop(WmWindow* child) {
  window_->StackChildAtTop(GetAuraWindow(child));
}

void WmWindow::StackChildAtBottom(WmWindow* child) {
  window_->StackChildAtBottom(GetAuraWindow(child));
}

void WmWindow::StackChildAbove(WmWindow* child, WmWindow* target) {
  window_->StackChildAbove(GetAuraWindow(child), GetAuraWindow(target));
}

void WmWindow::StackChildBelow(WmWindow* child, WmWindow* target) {
  window_->StackChildBelow(GetAuraWindow(child), GetAuraWindow(target));
}

void WmWindow::SetPinned(bool trusted) {
  if (aura::Env::GetInstance()->mode() == aura::Env::Mode::MUS) {
    // TODO: fix, see http://crbug.com/622486. With aura-mus pinning may just
    // work.
    NOTIMPLEMENTED();
    return;
  }
  wm::PinWindow(window_, trusted);
}

void WmWindow::SetAlwaysOnTop(bool value) {
  window_->SetProperty(aura::client::kAlwaysOnTopKey, value);
}

bool WmWindow::IsAlwaysOnTop() const {
  return window_->GetProperty(aura::client::kAlwaysOnTopKey);
}

void WmWindow::Hide() {
  window_->Hide();
}

void WmWindow::Show() {
  window_->Show();
}

views::Widget* WmWindow::GetInternalWidget() {
  return window_->GetProperty(kWidgetCreationTypeKey) ==
                 WidgetCreationType::INTERNAL
             ? views::Widget::GetWidgetForNativeView(window_)
             : nullptr;
}

void WmWindow::CloseWidget() {
  if (aura::Env::GetInstance()->mode() == aura::Env::Mode::MUS &&
      aura_window()->GetProperty(kWidgetCreationTypeKey) ==
          WidgetCreationType::FOR_CLIENT) {
    // NOTE: in the FOR_CLIENT case there is not necessarily a widget associated
    // with the window. Mash only creates widgets for top level windows if mash
    // renders the non-client frame.
    DCHECK(Shell::window_manager_client());
    Shell::window_manager_client()->RequestClose(aura_window());
    return;
  }
  DCHECK(GetInternalWidget());
  GetInternalWidget()->Close();
}

void WmWindow::SetFocused() {
  aura::client::GetFocusClient(window_)->FocusWindow(window_);
}

bool WmWindow::IsFocused() const {
  return window_->HasFocus();
}

bool WmWindow::IsActive() const {
  return wm::IsActiveWindow(window_);
}

void WmWindow::Activate() {
  wm::ActivateWindow(window_);
}

void WmWindow::Deactivate() {
  wm::DeactivateWindow(window_);
}

void WmWindow::SetFullscreen(bool fullscreen) {
  ::wm::SetWindowFullscreen(window_, fullscreen);
}

void WmWindow::Maximize() {
  window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MAXIMIZED);
}

void WmWindow::Minimize() {
  window_->SetProperty(aura::client::kShowStateKey, ui::SHOW_STATE_MINIMIZED);
}

void WmWindow::Unminimize() {
  window_->SetProperty(
      aura::client::kShowStateKey,
      window_->GetProperty(aura::client::kPreMinimizedShowStateKey));
  window_->ClearProperty(aura::client::kPreMinimizedShowStateKey);
}

std::vector<WmWindow*> WmWindow::GetChildren() {
  return FromAuraWindows(window_->children());
}

WmWindow* WmWindow::GetChildByShellWindowId(int id) {
  return Get(window_->GetChildById(id));
}

void WmWindow::ShowResizeShadow(int component) {
  if (aura::Env::GetInstance()->mode() == aura::Env::Mode::MUS) {
    // TODO: http://crbug.com/640773.
    return;
  }
  ResizeShadowController* resize_shadow_controller =
      Shell::GetInstance()->resize_shadow_controller();
  if (resize_shadow_controller)
    resize_shadow_controller->ShowShadow(window_, component);
}

void WmWindow::HideResizeShadow() {
  if (aura::Env::GetInstance()->mode() == aura::Env::Mode::MUS) {
    // TODO: http://crbug.com/640773.
    return;
  }
  ResizeShadowController* resize_shadow_controller =
      Shell::GetInstance()->resize_shadow_controller();
  if (resize_shadow_controller)
    resize_shadow_controller->HideShadow(window_);
}

void WmWindow::InstallResizeHandleWindowTargeter(
    ImmersiveFullscreenController* immersive_fullscreen_controller) {
  window_->SetEventTargeter(base::MakeUnique<ResizeHandleWindowTargeter>(
      window_, immersive_fullscreen_controller));
}

void WmWindow::SetBoundsInScreenBehaviorForChildren(
    BoundsInScreenBehavior behavior) {
  window_->SetProperty(
      kUsesScreenCoordinatesKey,
      behavior == BoundsInScreenBehavior::USE_SCREEN_COORDINATES);
}

void WmWindow::SetSnapsChildrenToPhysicalPixelBoundary() {
  wm::SetSnapsChildrenToPhysicalPixelBoundary(window_);
}

void WmWindow::SnapToPixelBoundaryIfNecessary() {
  wm::SnapWindowToPixelBoundary(window_);
}

void WmWindow::SetChildrenUseExtendedHitRegion() {
  children_use_extended_hit_region_ = true;
  gfx::Insets mouse_extend(-kResizeOutsideBoundsSize, -kResizeOutsideBoundsSize,
                           -kResizeOutsideBoundsSize,
                           -kResizeOutsideBoundsSize);
  gfx::Insets touch_extend =
      mouse_extend.Scale(kResizeOutsideBoundsScaleForTouch);
  // TODO: EasyResizeWindowTargeter makes it so children get events outside
  // their bounds. This only works in mash when mash is providing the non-client
  // frame. Mus needs to support an api for the WindowManager that enables
  // events to be dispatched to windows outside the windows bounds that this
  // function calls into. http://crbug.com/679056.
  window_->SetEventTargeter(base::MakeUnique<::wm::EasyResizeWindowTargeter>(
      window_, mouse_extend, touch_extend));
}

std::unique_ptr<views::View> WmWindow::CreateViewWithRecreatedLayers() {
  return base::MakeUnique<wm::WindowMirrorView>(this);
}

void WmWindow::AddObserver(WmWindowObserver* observer) {
  observers_.AddObserver(observer);
}

void WmWindow::RemoveObserver(WmWindowObserver* observer) {
  observers_.RemoveObserver(observer);
}

bool WmWindow::HasObserver(const WmWindowObserver* observer) const {
  return observers_.HasObserver(observer);
}

void WmWindow::AddTransientWindowObserver(WmTransientWindowObserver* observer) {
  if (!added_transient_observer_) {
    added_transient_observer_ = true;
    ::wm::TransientWindowManager::Get(window_)->AddObserver(this);
  }
  transient_observers_.AddObserver(observer);
}

void WmWindow::RemoveTransientWindowObserver(
    WmTransientWindowObserver* observer) {
  transient_observers_.RemoveObserver(observer);
  if (added_transient_observer_ &&
      !transient_observers_.might_have_observers()) {
    added_transient_observer_ = false;
    ::wm::TransientWindowManager::Get(window_)->RemoveObserver(this);
  }
}

void WmWindow::AddLimitedPreTargetHandler(ui::EventHandler* handler) {
  // In mus AddPreTargetHandler() only works for windows created by this client.
  DCHECK(aura::Env::GetInstance()->mode() == aura::Env::Mode::LOCAL ||
         Shell::window_tree_client()->WasCreatedByThisClient(
             aura::WindowMus::Get(window_)));
  window_->AddPreTargetHandler(handler);
}

void WmWindow::RemoveLimitedPreTargetHandler(ui::EventHandler* handler) {
  window_->RemovePreTargetHandler(handler);
}

WmWindow::WmWindow(aura::Window* window)
    : window_(window),
      // Mirrors that of aura::Window.
      observers_(base::ObserverList<WmWindowObserver>::NOTIFY_EXISTING_ONLY),
      use_empty_minimum_size_for_testing_(
          default_use_empty_minimum_size_for_testing_) {
  window_->AddObserver(this);
  window_->SetProperty(kWmWindowKey, this);
}

void WmWindow::OnWindowHierarchyChanging(const HierarchyChangeParams& params) {
  WmWindowObserver::TreeChangeParams wm_params;
  wm_params.target = Get(params.target);
  wm_params.new_parent = Get(params.new_parent);
  wm_params.old_parent = Get(params.old_parent);
  for (auto& observer : observers_)
    observer.OnWindowTreeChanging(this, wm_params);
}

void WmWindow::OnWindowHierarchyChanged(const HierarchyChangeParams& params) {
  WmWindowObserver::TreeChangeParams wm_params;
  wm_params.target = Get(params.target);
  wm_params.new_parent = Get(params.new_parent);
  wm_params.old_parent = Get(params.old_parent);
  for (auto& observer : observers_)
    observer.OnWindowTreeChanged(this, wm_params);
}

void WmWindow::OnWindowStackingChanged(aura::Window* window) {
  for (auto& observer : observers_)
    observer.OnWindowStackingChanged(this);
}

void WmWindow::OnWindowPropertyChanged(aura::Window* window,
                                       const void* key,
                                       intptr_t old) {
  if (key == aura::client::kShowStateKey) {
    ash::wm::GetWindowState(window_)->OnWindowShowStateChanged();
    return;
  }
  if (key == aura::client::kImmersiveFullscreenKey) {
    bool enable = window_->GetProperty(aura::client::kImmersiveFullscreenKey);
    GetWindowState()->set_in_immersive_fullscreen(enable);
    return;
  }
  WmWindowProperty wm_property;
  if (key == aura::client::kAlwaysOnTopKey) {
    wm_property = WmWindowProperty::ALWAYS_ON_TOP;
  } else if (key == aura::client::kAppIconKey) {
    wm_property = WmWindowProperty::APP_ICON;
  } else if (key == aura::client::kDrawAttentionKey) {
    wm_property = WmWindowProperty::DRAW_ATTENTION;
  } else if (key == aura::client::kModalKey) {
    wm_property = WmWindowProperty::MODAL_TYPE;
  } else if (key == kPanelAttachedKey) {
    wm_property = WmWindowProperty::PANEL_ATTACHED;
  } else if (key == kShelfIDKey) {
    wm_property = WmWindowProperty::SHELF_ID;
  } else if (key == kShelfItemTypeKey) {
    wm_property = WmWindowProperty::SHELF_ITEM_TYPE;
  } else if (key == kSnapChildrenToPixelBoundary) {
    wm_property = WmWindowProperty::SNAP_CHILDREN_TO_PIXEL_BOUNDARY;
  } else if (key == aura::client::kTopViewInset) {
    wm_property = WmWindowProperty::TOP_VIEW_INSET;
  } else if (key == aura::client::kWindowIconKey) {
    wm_property = WmWindowProperty::WINDOW_ICON;
  } else {
    return;
  }
  for (auto& observer : observers_)
    observer.OnWindowPropertyChanged(this, wm_property);
}

void WmWindow::OnWindowBoundsChanged(aura::Window* window,
                                     const gfx::Rect& old_bounds,
                                     const gfx::Rect& new_bounds) {
  for (auto& observer : observers_)
    observer.OnWindowBoundsChanged(this, old_bounds, new_bounds);
}

void WmWindow::OnWindowDestroying(aura::Window* window) {
  for (auto& observer : observers_)
    observer.OnWindowDestroying(this);
}

void WmWindow::OnWindowDestroyed(aura::Window* window) {
  for (auto& observer : observers_)
    observer.OnWindowDestroyed(this);
}

void WmWindow::OnWindowVisibilityChanging(aura::Window* window, bool visible) {
  DCHECK_EQ(window, window_);
  for (auto& observer : observers_)
    observer.OnWindowVisibilityChanging(this, visible);
}

void WmWindow::OnWindowVisibilityChanged(aura::Window* window, bool visible) {
  for (auto& observer : observers_)
    observer.OnWindowVisibilityChanged(Get(window), visible);
}

void WmWindow::OnWindowTitleChanged(aura::Window* window) {
  for (auto& observer : observers_)
    observer.OnWindowTitleChanged(this);
}

void WmWindow::OnTransientChildAdded(aura::Window* window,
                                     aura::Window* transient) {
  for (auto& observer : transient_observers_)
    observer.OnTransientChildAdded(this, Get(transient));
}

void WmWindow::OnTransientChildRemoved(aura::Window* window,
                                       aura::Window* transient) {
  for (auto& observer : transient_observers_)
    observer.OnTransientChildRemoved(this, Get(transient));
}

}  // namespace ash
