// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/input/single_scrollbar_animation_controller_thinning.h"

#include "cc/layers/solid_color_scrollbar_layer_impl.h"
#include "cc/test/fake_impl_task_runner_provider.h"
#include "cc/test/fake_layer_tree_host_impl.h"
#include "cc/test/geometry_test_utils.h"
#include "cc/test/test_task_graph_runner.h"
#include "cc/trees/layer_tree_impl.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::AtLeast;
using testing::Mock;
using testing::NiceMock;
using testing::_;

namespace cc {
namespace {

// These constants are hard-coded and should match the values in
// single_scrollbar_animation_controller_thinning.cc.
const float kIdleThicknessScale = 0.4f;
const float kDefaultMouseMoveDistanceToTriggerAnimation = 25.f;

class MockSingleScrollbarAnimationControllerClient
    : public ScrollbarAnimationControllerClient {
 public:
  explicit MockSingleScrollbarAnimationControllerClient(
      LayerTreeHostImpl* host_impl)
      : host_impl_(host_impl) {}
  virtual ~MockSingleScrollbarAnimationControllerClient() {}

  ScrollbarSet ScrollbarsFor(int scroll_layer_id) const override {
    return host_impl_->ScrollbarsFor(scroll_layer_id);
  }

  MOCK_METHOD2(PostDelayedScrollbarAnimationTask,
               void(const base::Closure& start_fade, base::TimeDelta delay));
  MOCK_METHOD0(SetNeedsRedrawForScrollbarAnimation, void());
  MOCK_METHOD0(SetNeedsAnimateForScrollbarAnimation, void());
  MOCK_METHOD0(DidChangeScrollbarVisibility, void());
  MOCK_METHOD0(start_fade, base::Closure());
  MOCK_METHOD0(delay, base::TimeDelta());

 private:
  LayerTreeHostImpl* host_impl_;
};

class SingleScrollbarAnimationControllerThinningTest : public testing::Test {
 public:
  SingleScrollbarAnimationControllerThinningTest()
      : host_impl_(&task_runner_provider_, &task_graph_runner_),
        client_(&host_impl_) {}

 protected:
  const base::TimeDelta kThinningDuration = base::TimeDelta::FromSeconds(2);

  void SetUp() override {
    std::unique_ptr<LayerImpl> scroll_layer =
        LayerImpl::Create(host_impl_.active_tree(), 1);
    std::unique_ptr<LayerImpl> clip =
        LayerImpl::Create(host_impl_.active_tree(), 3);
    clip_layer_ = clip.get();
    scroll_layer->SetScrollClipLayer(clip_layer_->id());
    LayerImpl* scroll_layer_ptr = scroll_layer.get();

    const int kId = 2;
    const int kThumbThickness = 10;
    const int kTrackStart = 0;
    const bool kIsLeftSideVerticalScrollbar = false;
    const bool kIsOverlayScrollbar = true;

    std::unique_ptr<SolidColorScrollbarLayerImpl> scrollbar =
        SolidColorScrollbarLayerImpl::Create(
            host_impl_.active_tree(), kId, HORIZONTAL, kThumbThickness,
            kTrackStart, kIsLeftSideVerticalScrollbar, kIsOverlayScrollbar);
    scrollbar_layer_ = scrollbar.get();

    scroll_layer->test_properties()->AddChild(std::move(scrollbar));
    clip_layer_->test_properties()->AddChild(std::move(scroll_layer));
    host_impl_.active_tree()->SetRootLayerForTesting(std::move(clip));

    scrollbar_layer_->SetScrollLayerId(scroll_layer_ptr->id());
    scrollbar_layer_->test_properties()->opacity_can_animate = true;
    clip_layer_->SetBounds(gfx::Size(100, 100));
    scroll_layer_ptr->SetBounds(gfx::Size(200, 200));
    host_impl_.active_tree()->BuildLayerListAndPropertyTreesForTesting();

    scrollbar_controller_ = SingleScrollbarAnimationControllerThinning::Create(
        scroll_layer_ptr->id(), HORIZONTAL, &client_, kThinningDuration);
  }

  FakeImplTaskRunnerProvider task_runner_provider_;
  TestTaskGraphRunner task_graph_runner_;
  FakeLayerTreeHostImpl host_impl_;
  std::unique_ptr<SingleScrollbarAnimationControllerThinning>
      scrollbar_controller_;
  LayerImpl* clip_layer_;
  SolidColorScrollbarLayerImpl* scrollbar_layer_;
  NiceMock<MockSingleScrollbarAnimationControllerClient> client_;
};

// Check initialization of scrollbar. Should start thin.
TEST_F(SingleScrollbarAnimationControllerThinningTest, Idle) {
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());
}

// Move the pointer near the scrollbar. Confirm it gets thick and narrow when
// moved away.
TEST_F(SingleScrollbarAnimationControllerThinningTest, MouseNear) {
  base::TimeTicks time;
  time += base::TimeDelta::FromSeconds(1);

  scrollbar_controller_->DidMouseMoveNear(1);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  // Should animate to thickened.
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Subsequent moves within the nearness threshold should not change anything.
  scrollbar_controller_->DidMouseMoveNear(2);
  scrollbar_controller_->Animate(time);
  time += base::TimeDelta::FromSeconds(10);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Now move away from bar.
  scrollbar_controller_->DidMouseMoveNear(
      kDefaultMouseMoveDistanceToTriggerAnimation);
  scrollbar_controller_->Animate(time);
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());
}

// Move the pointer over the scrollbar. Make sure it gets thick that it gets
// thin when moved away.
TEST_F(SingleScrollbarAnimationControllerThinningTest, MouseOver) {
  base::TimeTicks time;
  time += base::TimeDelta::FromSeconds(1);

  scrollbar_controller_->DidMouseMoveNear(0);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  // Should animate to thickened.
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Subsequent moves should not change anything.
  scrollbar_controller_->DidMouseMoveNear(0);
  scrollbar_controller_->Animate(time);
  time += base::TimeDelta::FromSeconds(10);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Moving off the scrollbar but still withing the "near" threshold should do
  // nothing.
  scrollbar_controller_->DidMouseMoveNear(
      kDefaultMouseMoveDistanceToTriggerAnimation - 1.f);
  scrollbar_controller_->Animate(time);
  time += base::TimeDelta::FromSeconds(10);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Now move away from bar.
  scrollbar_controller_->DidMouseMoveNear(
      kDefaultMouseMoveDistanceToTriggerAnimation);
  scrollbar_controller_->Animate(time);
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());
}

// First move the pointer over the scrollbar off of it. Make sure the thinning
// animation kicked off in DidMouseMoveOffScrollbar gets overridden by the
// thickening animation in the DidMouseMoveNear call.
TEST_F(SingleScrollbarAnimationControllerThinningTest,
       MouseNearThenAwayWhileAnimating) {
  base::TimeTicks time;
  time += base::TimeDelta::FromSeconds(1);

  scrollbar_controller_->DidMouseMoveNear(0);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  // Should animate to thickened.
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // This is tricky. The DidMouseLeave() is sent before the
  // subsequent DidMouseMoveNear(), if the mouse moves in that direction.
  // This results in the thumb thinning. We want to make sure that when the
  // thumb starts expanding it doesn't first narrow to the idle thinness.
  time += base::TimeDelta::FromSeconds(1);
  scrollbar_controller_->DidMouseLeave();
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Let the animation run half of the way through the thinning animation.
  time += kThinningDuration / 2;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f - (1.0f - kIdleThicknessScale) / 2.0f,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  // Now we get a notification for the mouse moving over the scroller. The
  // animation is reset to the thickening direction but we won't start
  // thickening until the new animation catches up to the current thickness.
  scrollbar_controller_->DidMouseMoveNear(1);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f - (1.0f - kIdleThicknessScale) / 2.0f,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  // Until we reach the half way point, the animation will have no effect.
  time += kThinningDuration / 4;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f - (1.0f - kIdleThicknessScale) / 2.0f,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  time += kThinningDuration / 4;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f - (1.0f - kIdleThicknessScale) / 2.0f,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  // We're now at three quarters of the way through so it should now started
  // thickening again.
  time += kThinningDuration / 4;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale + 3 * (1.0f - kIdleThicknessScale) / 4.0f,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  // And all the way to the end.
  time += kThinningDuration / 4;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());
}

// First move the pointer on the scrollbar, then press it, then away.
// Confirm that the bar gets thick. Then mouse up. Confirm that
// the bar gets thin.
TEST_F(SingleScrollbarAnimationControllerThinningTest,
       MouseCaptureAndReleaseOutOfBar) {
  base::TimeTicks time;
  time += base::TimeDelta::FromSeconds(1);

  // Move over the scrollbar.
  scrollbar_controller_->DidMouseMoveNear(0);
  scrollbar_controller_->Animate(time);
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Capture
  scrollbar_controller_->DidMouseDown();
  time += base::TimeDelta::FromSeconds(1);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Should stay thick for a while.
  time += base::TimeDelta::FromSeconds(10);
  scrollbar_controller_->Animate(time);

  // Move outside the "near" threshold. Because the scrollbar is captured it
  // should remain thick.
  scrollbar_controller_->DidMouseMoveNear(
      kDefaultMouseMoveDistanceToTriggerAnimation);
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Release.
  scrollbar_controller_->DidMouseUp();

  // Should become thin.
  time += base::TimeDelta::FromSeconds(1);
  scrollbar_controller_->Animate(time);
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());
}

// First move the pointer on the scrollbar, then press it, then away.  Confirm
// that the bar gets thick. Then move point on the scrollbar and mouse up.
// Confirm that the bar stays thick.
TEST_F(SingleScrollbarAnimationControllerThinningTest,
       MouseCaptureAndReleaseOnBar) {
  base::TimeTicks time;
  time += base::TimeDelta::FromSeconds(1);

  // Move over scrollbar.
  scrollbar_controller_->DidMouseMoveNear(0);
  scrollbar_controller_->Animate(time);
  time += kThinningDuration;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Capture. Nothing should change.
  scrollbar_controller_->DidMouseDown();
  time += base::TimeDelta::FromSeconds(1);
  scrollbar_controller_->Animate(time);
  time += base::TimeDelta::FromSeconds(10);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Move away from scrollbar. Nothing should change.
  scrollbar_controller_->DidMouseMoveNear(
      kDefaultMouseMoveDistanceToTriggerAnimation);
  time += base::TimeDelta::FromSeconds(1);
  scrollbar_controller_->Animate(time);
  time += base::TimeDelta::FromSeconds(10);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Move over scrollbar and release. Since we're near the scrollbar, it should
  // remain thick.
  scrollbar_controller_->DidMouseMoveNear(0);
  scrollbar_controller_->DidMouseUp();
  time += base::TimeDelta::FromSeconds(1);
  scrollbar_controller_->Animate(time);
  time += base::TimeDelta::FromSeconds(10);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());
}

// Tests that the thickening/thinning effects are animated.
TEST_F(SingleScrollbarAnimationControllerThinningTest, ThicknessAnimated) {
  base::TimeTicks time;
  time += base::TimeDelta::FromSeconds(1);

  // Move mouse near scrollbar. Test that at half the duration time, the
  // thickness is half way through its animation.
  scrollbar_controller_->DidMouseMoveNear(1);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  time += kThinningDuration / 2;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale + (1.0f - kIdleThicknessScale) / 2.0f,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  time += kThinningDuration / 2;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  // Move mouse away from scrollbar. Same check.
  time += base::TimeDelta::FromSeconds(1);
  scrollbar_controller_->DidMouseMoveNear(
      kDefaultMouseMoveDistanceToTriggerAnimation);
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f, scrollbar_layer_->thumb_thickness_scale_factor());

  time += kThinningDuration / 2;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(1.0f - (1.0f - kIdleThicknessScale) / 2.0f,
                  scrollbar_layer_->thumb_thickness_scale_factor());

  time += kThinningDuration / 2;
  scrollbar_controller_->Animate(time);
  EXPECT_FLOAT_EQ(kIdleThicknessScale,
                  scrollbar_layer_->thumb_thickness_scale_factor());
}

}  // namespace
}  // namespace cc
