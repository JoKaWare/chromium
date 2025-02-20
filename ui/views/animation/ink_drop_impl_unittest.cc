// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "ui/views/animation/ink_drop_impl.h"

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/test/gtest_util.h"
#include "base/test/test_simple_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/views/animation/test/ink_drop_impl_test_api.h"
#include "ui/views/animation/test/test_ink_drop_host.h"
#include "ui/views/test/views_test_base.h"

namespace views {

// NOTE: The InkDropImpl class is also tested by the InkDropFactoryTest tests.
class InkDropImplTest : public testing::Test {
 public:
  InkDropImplTest();
  ~InkDropImplTest() override;

 protected:
  TestInkDropHost* ink_drop_host() { return ink_drop_host_.get(); }

  InkDropImpl* ink_drop() { return ink_drop_.get(); }

  test::InkDropImplTestApi* test_api() { return test_api_.get(); }

  // Runs all the pending tasks in |task_runner_|. This can be used to progress
  // timers. e.g. HideHighlightOnRippleHiddenState's
  // |highlight_after_ripple_timer_|.
  void RunPendingTasks();

  // Returns true if the ink drop layers have been added to |ink_drop_host_|.
  bool AreLayersAddedToHost() const;

  // Destroys the |ink_drop_| and associated |test_api_|.
  void DestroyInkDrop();

  // Used to control the tasks scheduled by the InkDropImpl's Timer.
  scoped_refptr<base::TestSimpleTaskRunner> task_runner_;

  // Required by base::Timer's.
  std::unique_ptr<base::ThreadTaskRunnerHandle> thread_task_runner_handle_;

 private:
  std::unique_ptr<TestInkDropHost> ink_drop_host_;

  // The test target.
  std::unique_ptr<InkDropImpl> ink_drop_;

  // Allows privileged access to the the |ink_drop_highlight_|.
  std::unique_ptr<test::InkDropImplTestApi> test_api_;

  DISALLOW_COPY_AND_ASSIGN(InkDropImplTest);
};

InkDropImplTest::InkDropImplTest()
    : task_runner_(new base::TestSimpleTaskRunner),
      thread_task_runner_handle_(
          new base::ThreadTaskRunnerHandle(task_runner_)),
      ink_drop_host_(base::MakeUnique<TestInkDropHost>()),
      ink_drop_(
          base::MakeUnique<InkDropImpl>(ink_drop_host_.get(), gfx::Size())),
      test_api_(base::MakeUnique<test::InkDropImplTestApi>(ink_drop_.get())) {
  ink_drop_host_->set_disable_timers_for_test(true);
}

InkDropImplTest::~InkDropImplTest() {}

void InkDropImplTest::RunPendingTasks() {
  task_runner_->RunPendingTasks();
  EXPECT_FALSE(task_runner_->HasPendingTask());
}

bool InkDropImplTest::AreLayersAddedToHost() const {
  return ink_drop_host_->num_ink_drop_layers() >= 1;
}

void InkDropImplTest::DestroyInkDrop() {
  test_api_.reset();
  ink_drop_.reset();
}

// AutoHighlightMode parameterized test fixture.
class InkDropImplAutoHighlightTest
    : public InkDropImplTest,
      public testing::WithParamInterface<
          testing::tuple<InkDropImpl::AutoHighlightMode>> {
 public:
  InkDropImplAutoHighlightTest();
  ~InkDropImplAutoHighlightTest() override;

  InkDropImpl::AutoHighlightMode GetAutoHighlightMode() const;

 private:
  DISALLOW_COPY_AND_ASSIGN(InkDropImplAutoHighlightTest);
};

InkDropImplAutoHighlightTest::InkDropImplAutoHighlightTest()
    : InkDropImplTest() {
  ink_drop()->SetAutoHighlightMode(GetAutoHighlightMode());
}

InkDropImplAutoHighlightTest::~InkDropImplAutoHighlightTest() {}

InkDropImpl::AutoHighlightMode
InkDropImplAutoHighlightTest::GetAutoHighlightMode() const {
  return testing::get<0>(GetParam());
}

////////////////////////////////////////////////////////////////////////////////
//
// InkDropImpl tests
//

TEST_F(InkDropImplTest, ShouldHighlight) {
  ink_drop()->SetShowHighlightOnHover(false);
  ink_drop()->SetHovered(false);
  ink_drop()->SetShowHighlightOnFocus(false);
  ink_drop()->SetFocused(false);
  EXPECT_FALSE(test_api()->ShouldHighlight());

  ink_drop()->SetShowHighlightOnHover(true);
  ink_drop()->SetHovered(false);
  ink_drop()->SetShowHighlightOnFocus(false);
  ink_drop()->SetFocused(false);
  EXPECT_FALSE(test_api()->ShouldHighlight());

  ink_drop()->SetShowHighlightOnHover(false);
  ink_drop()->SetHovered(true);
  ink_drop()->SetShowHighlightOnFocus(false);
  ink_drop()->SetFocused(false);
  EXPECT_FALSE(test_api()->ShouldHighlight());

  ink_drop()->SetShowHighlightOnHover(false);
  ink_drop()->SetHovered(false);
  ink_drop()->SetShowHighlightOnFocus(true);
  ink_drop()->SetFocused(false);
  EXPECT_FALSE(test_api()->ShouldHighlight());

  ink_drop()->SetShowHighlightOnHover(false);
  ink_drop()->SetHovered(false);
  ink_drop()->SetShowHighlightOnFocus(false);
  ink_drop()->SetFocused(true);
  EXPECT_FALSE(test_api()->ShouldHighlight());

  ink_drop()->SetShowHighlightOnHover(true);
  ink_drop()->SetHovered(true);
  ink_drop()->SetShowHighlightOnFocus(false);
  ink_drop()->SetFocused(false);
  EXPECT_TRUE(test_api()->ShouldHighlight());

  ink_drop()->SetShowHighlightOnHover(false);
  ink_drop()->SetHovered(false);
  ink_drop()->SetShowHighlightOnFocus(true);
  ink_drop()->SetFocused(true);
  EXPECT_TRUE(test_api()->ShouldHighlight());

  test_api()->SetShouldHighlight(false);
  EXPECT_FALSE(test_api()->ShouldHighlight());

  test_api()->SetShouldHighlight(true);
  EXPECT_TRUE(test_api()->ShouldHighlight());
}

TEST_F(InkDropImplTest,
       VerifyInkDropLayersRemovedWhenPresentDuringDestruction) {
  test_api()->SetShouldHighlight(true);
  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  EXPECT_TRUE(AreLayersAddedToHost());
  DestroyInkDrop();
  EXPECT_FALSE(AreLayersAddedToHost());
}

// Test that (re-)hiding or un-hovering a hidden ink drop doesn't add layers.
TEST_F(InkDropImplTest, AlwaysHiddenInkDropHasNoLayers) {
  EXPECT_FALSE(AreLayersAddedToHost());

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  EXPECT_FALSE(AreLayersAddedToHost());

  ink_drop()->SetHovered(false);
  EXPECT_FALSE(AreLayersAddedToHost());
}

TEST_F(InkDropImplTest, LayersRemovedFromHostAfterHighlight) {
  EXPECT_FALSE(AreLayersAddedToHost());

  test_api()->SetShouldHighlight(true);
  EXPECT_TRUE(AreLayersAddedToHost());

  test_api()->CompleteAnimations();

  test_api()->SetShouldHighlight(false);
  test_api()->CompleteAnimations();
  EXPECT_FALSE(AreLayersAddedToHost());
}

TEST_F(InkDropImplTest, LayersRemovedFromHostAfterInkDrop) {
  EXPECT_FALSE(AreLayersAddedToHost());

  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  EXPECT_TRUE(AreLayersAddedToHost());

  test_api()->CompleteAnimations();

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  EXPECT_TRUE(AreLayersAddedToHost());

  test_api()->CompleteAnimations();
  EXPECT_FALSE(AreLayersAddedToHost());
}

TEST_F(InkDropImplTest, LayersArentRemovedWhenPreemptingFadeOut) {
  EXPECT_FALSE(AreLayersAddedToHost());

  test_api()->SetShouldHighlight(true);
  EXPECT_TRUE(AreLayersAddedToHost());

  test_api()->CompleteAnimations();

  ink_drop()->SetHovered(false);
  EXPECT_TRUE(AreLayersAddedToHost());

  ink_drop()->SetHovered(true);
  EXPECT_TRUE(AreLayersAddedToHost());
}

TEST_F(InkDropImplTest,
       SettingHighlightStateDuringStateExitIsntAllowedDeathTest) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";

  test::InkDropImplTestApi::SetStateOnExitHighlightState::Install(
      test_api()->state_factory());
  EXPECT_DCHECK_DEATH(
      test::InkDropImplTestApi::AccessFactoryOnExitHighlightState::Install(
          test_api()->state_factory()));
  // Need to set the |highlight_state_| directly because the
  // SetStateOnExitHighlightState will recursively try to set it during tear
  // down and cause a stack overflow.
  test_api()->SetHighlightState(nullptr);
}

// Verifies there is no use after free errors.
TEST_F(InkDropImplTest,
       TearingDownHighlightStateThatAccessesTheStateFactoryIsSafe) {
  test::InkDropImplTestApi::AccessFactoryOnExitHighlightState::Install(
      test_api()->state_factory());
  test::InkDropImplTestApi::AccessFactoryOnExitHighlightState::Install(
      test_api()->state_factory());
}

// Tests that if during destruction, a rippl animation is successfully ended, no
// crash happens (see https://crbug.com/663579).
TEST_F(InkDropImplTest, SuccessfulAnimationEndedDuringDestruction) {
  // Start a ripple animation with non-zero duration.
  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  {
    // Start another ripple animation with zero duration that would be queued
    // until the previous one is finished/aborted.
    ui::ScopedAnimationDurationScaleMode duration_mode(
        ui::ScopedAnimationDurationScaleMode::ZERO_DURATION);
    ink_drop()->AnimateToState(InkDropState::ACTION_TRIGGERED);
  }
  // Abort the first animation, so that the queued animation is started (and
  // finished immediately since it has zero duration). No crash should happen.
  DestroyInkDrop();
}

////////////////////////////////////////////////////////////////////////////////
//
// Common AutoHighlightMode tests
//

typedef InkDropImplAutoHighlightTest InkDropImplCommonAutoHighlightTest;
// Note: First argument is optional and intentionally left blank.
// (it's a prefix for the generated test cases)
INSTANTIATE_TEST_CASE_P(
    ,
    InkDropImplCommonAutoHighlightTest,
    testing::Values(InkDropImpl::AutoHighlightMode::NONE,
                    InkDropImpl::AutoHighlightMode::HIDE_ON_RIPPLE,
                    InkDropImpl::AutoHighlightMode::SHOW_ON_RIPPLE));

// Verifies InkDropImplTestApi::SetShouldHighlight() works as expected.
TEST_P(InkDropImplCommonAutoHighlightTest,
       ShouldHighlightCausesHighlightToBeVisible) {
  test_api()->SetShouldHighlight(true);
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  test_api()->SetShouldHighlight(false);
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplCommonAutoHighlightTest,
       HighlightVisibilityForFocusAndHoverStates) {
  ink_drop()->SetShowHighlightOnHover(true);
  ink_drop()->SetShowHighlightOnFocus(true);

  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->SetFocused(true);
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->SetHovered(false);
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->SetHovered(true);
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->SetFocused(false);
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->SetHovered(false);
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
}

////////////////////////////////////////////////////////////////////////////////
//
// InkDropImpl::AutoHighlightMode::NONE specific tests
//

typedef InkDropImplAutoHighlightTest InkDropImplNoAutoHighlightTest;
// Note: First argument is optional and intentionally left blank.
// (it's a prefix for the generated test cases)
INSTANTIATE_TEST_CASE_P(,
                        InkDropImplNoAutoHighlightTest,
                        testing::Values(InkDropImpl::AutoHighlightMode::NONE));

TEST_P(InkDropImplNoAutoHighlightTest, VisibleHighlightDuringRippleAnimations) {
  test_api()->SetShouldHighlight(true);

  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  test_api()->CompleteAnimations();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplNoAutoHighlightTest, HiddenHighlightDuringRippleAnimations) {
  test_api()->SetShouldHighlight(false);

  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  test_api()->CompleteAnimations();
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
}

////////////////////////////////////////////////////////////////////////////////
//
// InkDropImpl::AutoHighlightMode::HIDE_ON_RIPPLE specific tests
//

typedef InkDropImplAutoHighlightTest InkDropImplHideAutoHighlightTest;
// Note: First argument is optional and intentionally left blank.
// (it's a prefix for the generated test cases)
INSTANTIATE_TEST_CASE_P(
    ,
    InkDropImplHideAutoHighlightTest,
    testing::Values(InkDropImpl::AutoHighlightMode::HIDE_ON_RIPPLE));

TEST_P(InkDropImplHideAutoHighlightTest,
       VisibleHighlightDuringRippleAnimations) {
  test_api()->SetShouldHighlight(true);

  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  test_api()->CompleteAnimations();
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();
  RunPendingTasks();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplHideAutoHighlightTest,
       HiddenHighlightDuringRippleAnimations) {
  test_api()->SetShouldHighlight(false);

  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  test_api()->CompleteAnimations();
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();
  RunPendingTasks();
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplHideAutoHighlightTest, HighlightIsHiddenOnSnapToActivated) {
  test_api()->SetShouldHighlight(true);

  ink_drop()->SnapToActivated();
  test_api()->CompleteAnimations();
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();
  RunPendingTasks();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplHideAutoHighlightTest,
       HighlightDoesntFadeInAfterAnimationIfHighlightNotSet) {
  test_api()->SetShouldHighlight(true);

  ink_drop()->AnimateToState(InkDropState::ACTION_TRIGGERED);
  test_api()->CompleteAnimations();
  test_api()->SetShouldHighlight(false);
  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();
  RunPendingTasks();

  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplHideAutoHighlightTest,
       HighlightFadesInAfterAnimationIfHovered) {
  ink_drop()->SetShowHighlightOnHover(true);
  ink_drop()->SetHovered(true);

  ink_drop()->AnimateToState(InkDropState::ACTION_TRIGGERED);
  test_api()->CompleteAnimations();
  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();

  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
  EXPECT_TRUE(task_runner_->HasPendingTask());

  RunPendingTasks();

  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplHideAutoHighlightTest,
       HighlightSnapsInAfterAnimationWhenHostIsFocused) {
  ink_drop()->SetShowHighlightOnFocus(true);
  ink_drop()->SetFocused(true);

  ink_drop()->AnimateToState(InkDropState::ACTION_TRIGGERED);
  test_api()->CompleteAnimations();
  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();

  EXPECT_FALSE(task_runner_->HasPendingTask());
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplHideAutoHighlightTest, DeactivatedAnimatesWhenNotFocused) {
  test_api()->SetShouldHighlight(false);

  ink_drop()->AnimateToState(InkDropState::ACTIVATED);
  test_api()->CompleteAnimations();

  ink_drop()->AnimateToState(InkDropState::DEACTIVATED);
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
  EXPECT_TRUE(test_api()->HasActiveAnimations());
}

TEST_P(InkDropImplHideAutoHighlightTest,
       DeactivatedAnimationSkippedWhenFocused) {
  ink_drop()->SetShowHighlightOnFocus(true);
  ink_drop()->SetFocused(true);

  ink_drop()->AnimateToState(InkDropState::ACTIVATED);
  test_api()->CompleteAnimations();

  ink_drop()->AnimateToState(InkDropState::DEACTIVATED);
  EXPECT_TRUE(AreLayersAddedToHost());

  test_api()->CompleteAnimations();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());
  EXPECT_EQ(InkDropState::HIDDEN, ink_drop()->GetTargetInkDropState());
}

TEST_P(InkDropImplHideAutoHighlightTest,
       FocusAndHoverChangesDontShowHighlightWhenRippleIsVisible) {
  test_api()->SetShouldHighlight(true);
  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  test_api()->CompleteAnimations();
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->SetHovered(false);
  ink_drop()->SetFocused(false);

  ink_drop()->SetHovered(true);
  ink_drop()->SetFocused(true);

  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
  EXPECT_TRUE(test_api()->ShouldHighlight());
}

// Verifies there is no crash when animations are started during the destruction
// of the InkDropRipple. See https://crbug.com/663335.
TEST_P(InkDropImplHideAutoHighlightTest, NoCrashDuringRippleTearDown) {
  ink_drop()->SetShowHighlightOnFocus(true);
  ink_drop()->SetFocused(true);
  ink_drop()->AnimateToState(InkDropState::ACTIVATED);
  ink_drop()->AnimateToState(InkDropState::DEACTIVATED);
  ink_drop()->AnimateToState(InkDropState::DEACTIVATED);
  DestroyInkDrop();
}

////////////////////////////////////////////////////////////////////////////////
//
// InkDropImpl::AutoHighlightMode::SHOW_ON_RIPPLE specific tests
//

typedef InkDropImplAutoHighlightTest InkDropImplShowAutoHighlightTest;
// Note: First argument is optional and intentionally left blank.
// (it's a prefix for the generated test cases)
INSTANTIATE_TEST_CASE_P(
    ,
    InkDropImplShowAutoHighlightTest,
    testing::Values(InkDropImpl::AutoHighlightMode::SHOW_ON_RIPPLE));

TEST_P(InkDropImplShowAutoHighlightTest,
       VisibleHighlightDuringRippleAnimations) {
  test_api()->SetShouldHighlight(true);

  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  test_api()->CompleteAnimations();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplShowAutoHighlightTest,
       HiddenHighlightDuringRippleAnimations) {
  test_api()->SetShouldHighlight(false);

  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  test_api()->CompleteAnimations();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->AnimateToState(InkDropState::HIDDEN);
  test_api()->CompleteAnimations();
  EXPECT_FALSE(test_api()->IsHighlightFadingInOrVisible());
}

TEST_P(InkDropImplShowAutoHighlightTest,
       FocusAndHoverChangesDontHideHighlightWhenRippleIsVisible) {
  test_api()->SetShouldHighlight(true);
  ink_drop()->AnimateToState(InkDropState::ACTION_PENDING);
  test_api()->CompleteAnimations();
  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());

  ink_drop()->SetHovered(false);
  ink_drop()->SetFocused(false);

  EXPECT_TRUE(test_api()->IsHighlightFadingInOrVisible());
  EXPECT_FALSE(test_api()->ShouldHighlight());
}

}  // namespace views
