// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_INPUT_SINGLE_SCROLLBAR_ANIMATION_CONTROLLER_THINNING_H_
#define CC_INPUT_SINGLE_SCROLLBAR_ANIMATION_CONTROLLER_THINNING_H_

#include <memory>

#include "base/macros.h"
#include "base/time/time.h"
#include "cc/base/cc_export.h"
#include "cc/input/scrollbar.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/scrollbar_layer_impl_base.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace cc {

class ScrollbarAnimationControllerClient;

// ScrollbarAnimationControllerThinning for one scrollbar
class CC_EXPORT SingleScrollbarAnimationControllerThinning {
 public:
  static std::unique_ptr<SingleScrollbarAnimationControllerThinning> Create(
      int scroll_layer_id,
      ScrollbarOrientation orientation,
      ScrollbarAnimationControllerClient* client,
      base::TimeDelta thinning_duration);

  ~SingleScrollbarAnimationControllerThinning() {}

  void set_mouse_move_distance_for_test(float distance) {
    mouse_move_distance_to_trigger_animation_ = distance;
  }
  bool mouse_is_over_scrollbar() const { return mouse_is_over_scrollbar_; }
  bool mouse_is_near_scrollbar() const { return mouse_is_near_scrollbar_; }
  bool captured() const { return captured_; }

  bool Animate(base::TimeTicks now);
  void StartAnimation();
  void StopAnimation();

  void UpdateThumbThicknessScale();

  void DidMouseDown();
  void DidMouseUp();
  void DidMouseLeave();
  void DidMouseMoveNear(float distance);

 private:
  SingleScrollbarAnimationControllerThinning(
      int scroll_layer_id,
      ScrollbarOrientation orientation,
      ScrollbarAnimationControllerClient* client,
      base::TimeDelta thinning_duration);

  float AnimationProgressAtTime(base::TimeTicks now);
  void RunAnimationFrame(float progress);
  const base::TimeDelta& Duration();

  // Describes whether the current animation should INCREASE (thicken)
  // a bar or DECREASE it (thin).
  enum AnimationChange { NONE, INCREASE, DECREASE };
  float ThumbThicknessScaleAt(float progress);

  float AdjustScale(float new_value,
                    float current_value,
                    AnimationChange animation_change,
                    float min_value,
                    float max_value);
  void ApplyThumbThicknessScale(float thumb_thickness_scale);

  ScrollbarAnimationControllerClient* client_;

  base::TimeTicks last_awaken_time_;
  bool is_animating_;

  int scroll_layer_id_;

  ScrollbarOrientation orientation_;
  bool captured_;
  bool mouse_is_over_scrollbar_;
  bool mouse_is_near_scrollbar_;
  // Are we narrowing or thickening the bars.
  AnimationChange thickness_change_;
  // How close should the mouse be to the scrollbar before we thicken it.
  float mouse_move_distance_to_trigger_animation_;

  base::TimeDelta thinning_duration_;

  DISALLOW_COPY_AND_ASSIGN(SingleScrollbarAnimationControllerThinning);
};

}  // namespace cc

#endif  // CC_INPUT_SINGLE_SCROLLBAR_ANIMATION_CONTROLLER_THINNING_H_
