// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/feature_policy/feature_policy.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace content {

namespace {

// This is an example of a feature which should be enabled by default in all
// frames.
const FeaturePolicy::Feature kDefaultOnFeatureDfn{
    "default-on", FeaturePolicy::FeatureDefault::EnableForAll};

// This is an example of a feature which should be enabled in top-level frames,
// and same-origin child-frames, but must be delegated to all cross-origin
// frames explicitly.
const FeaturePolicy::Feature kDefaultSelfFeatureDfn{
    "default-self", FeaturePolicy::FeatureDefault::EnableForSelf};

// This is an example of a feature which should be disabled by default, both in
// top-level and nested frames.
const FeaturePolicy::Feature kDefaultOffFeatureDfn{
    "default-off", FeaturePolicy::FeatureDefault::DisableForAll};

// Define the three new features for testing
blink::WebFeaturePolicyFeature kDefaultOnFeature =
    static_cast<blink::WebFeaturePolicyFeature>(
        static_cast<int>(blink::WebFeaturePolicyFeature::LAST_FEATURE) + 1);

blink::WebFeaturePolicyFeature kDefaultSelfFeature =
    static_cast<blink::WebFeaturePolicyFeature>(
        static_cast<int>(blink::WebFeaturePolicyFeature::LAST_FEATURE) + 2);

blink::WebFeaturePolicyFeature kDefaultOffFeature =
    static_cast<blink::WebFeaturePolicyFeature>(
        static_cast<int>(blink::WebFeaturePolicyFeature::LAST_FEATURE) + 3);

}  // namespace

class FeaturePolicyTest : public ::testing::Test {
 protected:
  FeaturePolicyTest()
      : feature_list_({{kDefaultOnFeature, &kDefaultOnFeatureDfn},
                       {kDefaultSelfFeature, &kDefaultSelfFeatureDfn},
                       {kDefaultOffFeature, &kDefaultOffFeatureDfn}}) {}

  ~FeaturePolicyTest() override {}

  std::unique_ptr<FeaturePolicy> CreateFromParentPolicy(
      const FeaturePolicy* parent,
      const url::Origin& origin) {
    return FeaturePolicy::CreateFromParentPolicy(parent, origin, feature_list_);
  }

  url::Origin origin_a_ = url::Origin(GURL("https://example.com/"));
  url::Origin origin_b_ = url::Origin(GURL("https://example.net/"));
  url::Origin origin_c_ = url::Origin(GURL("https://example.org/"));

 private:
  // Contains the list of controlled features, so that we are guaranteed to
  // have at least one of each kind of default behaviour represented.
  FeaturePolicy::FeatureList feature_list_;
};

TEST_F(FeaturePolicyTest, TestInitialPolicy) {
  // +-------------+
  // |(1)Origin A  |
  // |No Policy    |
  // +-------------+
  // Default-on and top-level-only features should be enabled in top-level
  // frame. Default-off features should be disabled.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(FeaturePolicyTest, TestInitialSameOriginChildPolicy) {
  // +-----------------+
  // |(1)Origin A      |
  // |No Policy        |
  // | +-------------+ |
  // | |(2)Origin A  | |
  // | |No Policy    | |
  // | +-------------+ |
  // +-----------------+
  // Default-on and Default-self features should be enabled in a same-origin
  // child frame. Default-off features should be disabled.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_a_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(FeaturePolicyTest, TestInitialCrossOriginChildPolicy) {
  // +-----------------+
  // |(1)Origin A      |
  // |No Policy        |
  // | +-------------+ |
  // | |(2)Origin B  | |
  // | |No Policy    | |
  // | +-------------+ |
  // +-----------------+
  // Default-on features should be enabled in child frame. Default-self and
  // Default-off features should be disabled.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(FeaturePolicyTest, TestCrossOriginChildCannotEnableFeature) {
  // +---------------------------------------+
  // |(1) Origin A                           |
  // |No Policy                              |
  // | +-----------------------------------+ |
  // | |(2) Origin B                       | |
  // | |Policy: {"default-self": ["self"]} | |
  // | +-----------------------------------+ |
  // +---------------------------------------+
  // Default-self feature should be disabled in cross origin frame, even if no
  // policy was specified in the parent frame.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  policy2->SetHeaderPolicy({{{"default-self", false, {origin_b_}}}});
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestFrameSelfInheritance) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Policy: {"default-self": ["self"]}        |
  // | +-----------------+  +-----------------+ |
  // | |(2) Origin A     |  |(4) Origin B     | |
  // | |No Policy        |  |No Policy        | |
  // | | +-------------+ |  | +-------------+ | |
  // | | |(3)Origin A  | |  | |(5)Origin B  | | |
  // | | |No Policy    | |  | |No Policy    | | |
  // | | +-------------+ |  | +-------------+ | |
  // | +-----------------+  +-----------------+ |
  // +------------------------------------------+
  // Feature should be enabled at the top-level, and through the chain of
  // same-origin frames 2 and 3. It should be disabled in frames 4 and 5, as
  // they are at a different origin.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-self", false, {origin_a_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_a_);
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_a_);
  std::unique_ptr<FeaturePolicy> policy4 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy5 =
      CreateFromParentPolicy(policy4.get(), origin_b_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy5->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestReflexiveFrameSelfInheritance) {
  // +-----------------------------------+
  // |(1) Origin A                       |
  // |Policy: {"default-self": ["self"]} |
  // | +-----------------+               |
  // | |(2) Origin B     |               |
  // | |No Policy        |               |
  // | | +-------------+ |               |
  // | | |(3)Origin A  | |               |
  // | | |No Policy    | |               |
  // | | +-------------+ |               |
  // | +-----------------+               |
  // +-----------------------------------+
  // Feature which is enabled at top-level should be disabled in frame 3, as
  // it is embedded by frame 2, for which the feature is not enabled.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-self", false, {origin_a_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestSelectiveFrameInheritance) {
  // +------------------------------------------+
  // |(1) Origin A                              |
  // |Policy: {"default-self": ["Origin B"]}    |
  // | +-----------------+  +-----------------+ |
  // | |(2) Origin B     |  |(3) Origin C     | |
  // | |No Policy        |  |No Policy        | |
  // | |                 |  | +-------------+ | |
  // | |                 |  | |(4)Origin B  | | |
  // | |                 |  | |No Policy    | | |
  // | |                 |  | +-------------+ | |
  // | +-----------------+  +-----------------+ |
  // +------------------------------------------+
  // Feature should be enabled in second level Origin B frame, but disabled in
  // Frame 4, because it is embedded by frame 3, where the feature is not
  // enabled.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-self", false, {origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy1.get(), origin_c_);
  std::unique_ptr<FeaturePolicy> policy4 =
      CreateFromParentPolicy(policy3.get(), origin_b_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestPolicyCanBlockSelf) {
  // +----------------------------+
  // |(1)Origin A                 |
  // |Policy: {"default-on": []}  |
  // +----------------------------+
  // Default-on feature should be disabled in top-level frame.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy(
      {{{"default-on", false, std::vector<url::Origin>()}}});
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(FeaturePolicyTest, TestParentPolicyBlocksSameOriginChildPolicy) {
  // +----------------------------+
  // |(1)Origin A                 |
  // |Policy: {"default-on": []}  |
  // | +-------------+            |
  // | |(2)Origin A  |            |
  // | |No Policy    |            |
  // | +-------------+            |
  // +----------------------------+
  // Feature should be disabled in child frame.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy(
      {{{"default-on", false, std::vector<url::Origin>()}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_a_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(FeaturePolicyTest, TestChildPolicyCanBlockSelf) {
  // +--------------------------------+
  // |(1)Origin A                     |
  // |No Policy                       |
  // | +----------------------------+ |
  // | |(2)Origin B                 | |
  // | |Policy: {"default-on": []}  | |
  // | +----------------------------+ |
  // +--------------------------------+
  // Default-on feature should be disabled by cross-origin child frame.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  policy2->SetHeaderPolicy(
      {{{"default-on", false, std::vector<url::Origin>()}}});
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(FeaturePolicyTest, TestChildPolicyCanBlockChildren) {
  // +--------------------------------------+
  // |(1)Origin A                           |
  // |No Policy                             |
  // | +----------------------------------+ |
  // | |(2)Origin B                       | |
  // | |Policy: {"default-on": ["self"]}  | |
  // | | +-------------+                  | |
  // | | |(3)Origin C  |                  | |
  // | | |No Policy    |                  | |
  // | | +-------------+                  | |
  // | +----------------------------------+ |
  // +--------------------------------------+
  // Default-on feature should be enabled in frames 1 and 2; disabled in frame
  // 3 by child frame policy.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  policy2->SetHeaderPolicy({{{"default-on", false, {origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(FeaturePolicyTest, TestParentPolicyBlocksCrossOriginChildPolicy) {
  // +----------------------------+
  // |(1)Origin A                 |
  // |Policy: {"default-on": []}  |
  // | +-------------+            |
  // | |(2)Origin B  |            |
  // | |No Policy    |            |
  // | +-------------+            |
  // +----------------------------+
  // Default-on feature should be disabled in cross-origin child frame.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy(
      {{{"default-on", false, std::vector<url::Origin>()}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(FeaturePolicyTest, TestEnableForAllOrigins) {
  // +--------------------------------+
  // |(1) Origin A                    |
  // |Policy: {"default-self": ["*"]} |
  // | +-----------------+            |
  // | |(2) Origin B     |            |
  // | |No Policy        |            |
  // | | +-------------+ |            |
  // | | |(3)Origin A  | |            |
  // | | |No Policy    | |            |
  // | | +-------------+ |            |
  // | +-----------------+            |
  // +--------------------------------+
  // Feature should be enabled in top and second level; disabled in frame 3.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy(
      {{{"default-self", true, std::vector<url::Origin>()}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestDefaultOnEnablesForAllAncestors) {
  // +---------------------------------------+
  // |(1) Origin A                           |
  // |Policy: {"default-on": ["Origin B"]}   |
  // | +-----------------------------------+ |
  // | |(2) Origin B                       | |
  // | |No Policy                          | |
  // | | +-------------+   +-------------+ | |
  // | | |(3)Origin B  |   |(4)Origin C  | | |
  // | | |No Policy    |   |No Policy    | | |
  // | | +-------------+   +-------------+ | |
  // | +-----------------------------------+ |
  // +---------------------------------------+
  // Feature should be disabled in frame 1; enabled in frames 2, 3 and 4.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-on", false, {origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy4->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(FeaturePolicyTest, TestDefaultSelfRespectsSameOriginEmbedding) {
  // +---------------------------------------+
  // |(1) Origin A                           |
  // |Policy: {"default-self": ["Origin B"]} |
  // | +-----------------------------------+ |
  // | |(2) Origin B                       | |
  // | |No Policy                          | |
  // | | +-------------+   +-------------+ | |
  // | | |(3)Origin B  |   |(4)Origin C  | | |
  // | | |No Policy    |   |No Policy    | | |
  // | | +-------------+   +-------------+ | |
  // | +-----------------------------------+ |
  // +---------------------------------------+
  // Feature should be disabled in frames 1 and 4; enabled in frames 2 and 3.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-self", false, {origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestDefaultOffMustBeDelegatedToAllCrossOriginFrames) {
  // +------------------------------------------------------------+
  // |(1) Origin A                                                |
  // |Policy: {"default-off": ["Origin B"]}                       |
  // | +--------------------------------------------------------+ |
  // | |(2) Origin B                                            | |
  // | |Policy: {"default-off": ["self"]}                       | |
  // | | +-------------+   +----------------------------------+ | |
  // | | |(3)Origin B  |   |(4)Origin C                       | | |
  // | | |No Policy    |   |Policy: {"default-off": ["self"]} | | |
  // | | +-------------+   +----------------------------------+ | |
  // | +--------------------------------------------------------+ |
  // +------------------------------------------------------------+
  // Feature should be disabled in frames 1, 3 and 4; enabled in frame 2 only.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-off", false, {origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  policy2->SetHeaderPolicy({{{"default-off", false, {origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  policy4->SetHeaderPolicy({{{"default-off", false, {origin_c_}}}});
  EXPECT_FALSE(policy1->IsFeatureEnabled(kDefaultOffFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOffFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultOffFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultOffFeature));
}

TEST_F(FeaturePolicyTest, TestReenableForAllOrigins) {
  // +------------------------------------+
  // |(1) Origin A                        |
  // |Policy: {"default-self": ["*"]}     |
  // | +--------------------------------+ |
  // | |(2) Origin B                    | |
  // | |Policy: {"default-self": ["*"]} | |
  // | | +-------------+                | |
  // | | |(3)Origin A  |                | |
  // | | |No Policy    |                | |
  // | | +-------------+                | |
  // | +--------------------------------+ |
  // +------------------------------------+
  // Feature should be enabled in all frames.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy(
      {{{"default-self", true, std::vector<url::Origin>()}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  policy2->SetHeaderPolicy(
      {{{"default-self", true, std::vector<url::Origin>()}}});
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_a_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestBlockedFrameCannotReenable) {
  // +--------------------------------------+
  // |(1)Origin A                           |
  // |Policy: {"default-self": ["self"]}    |
  // | +----------------------------------+ |
  // | |(2)Origin B                       | |
  // | |Policy: {"default-self": ["*"]}   | |
  // | | +-------------+  +-------------+ | |
  // | | |(3)Origin A  |  |(4)Origin C  | | |
  // | | |No Policy    |  |No Policy    | | |
  // | | +-------------+  +-------------+ | |
  // | +----------------------------------+ |
  // +--------------------------------------+
  // Feature should be enabled at the top level; disabled in all other frames.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-self", false, {origin_a_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  policy2->SetHeaderPolicy(
      {{{"default-self", true, std::vector<url::Origin>()}}});
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_a_);
  std::unique_ptr<FeaturePolicy> policy4 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestEnabledFrameCanDelegate) {
  // +---------------------------------------------------+
  // |(1) Origin A                                       |
  // |Policy: {"default-self": ["self", "Origin B"]}     |
  // | +-----------------------------------------------+ |
  // | |(2) Origin B                                   | |
  // | |Policy: {"default-self": ["self", "Origin C"]} | |
  // | | +-------------+                               | |
  // | | |(3)Origin C  |                               | |
  // | | |No Policy    |                               | |
  // | | +-------------+                               | |
  // | +-----------------------------------------------+ |
  // +---------------------------------------------------+
  // Feature should be enabled in all frames.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-self", false, {origin_a_, origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  policy2->SetHeaderPolicy({{{"default-self", false, {origin_b_, origin_c_}}}});
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestEnabledFrameCanDelegateByDefault) {
  // +-----------------------------------------------+
  // |(1) Origin A                                   |
  // |Policy: {"default-on": ["self", "Origin B"]}   |
  // | +--------------------+ +--------------------+ |
  // | |(2) Origin B        | | (4) Origin C       | |
  // | |No Policy           | | No Policy          | |
  // | | +-------------+    | |                    | |
  // | | |(3)Origin C  |    | |                    | |
  // | | |No Policy    |    | |                    | |
  // | | +-------------+    | |                    | |
  // | +--------------------+ +--------------------+ |
  // +-----------------------------------------------+
  // Feature should be enabled in frames 1, 2, and 3, and disabled in frame 4.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-on", false, {origin_a_, origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  std::unique_ptr<FeaturePolicy> policy4 =
      CreateFromParentPolicy(policy1.get(), origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(FeaturePolicyTest, TestNonNestedFeaturesDontDelegateByDefault) {
  // +-----------------------------------------------+
  // |(1) Origin A                                   |
  // |Policy: {"default-self": ["self", "Origin B"]} |
  // | +--------------------+ +--------------------+ |
  // | |(2) Origin B        | | (4) Origin C       | |
  // | |No Policy           | | No Policy          | |
  // | | +-------------+    | |                    | |
  // | | |(3)Origin C  |    | |                    | |
  // | | |No Policy    |    | |                    | |
  // | | +-------------+    | |                    | |
  // | +--------------------+ +--------------------+ |
  // +-----------------------------------------------+
  // Feature should be enabled in frames 1 and 2, and disabled in frames 3 and
  // 4.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-self", false, {origin_a_, origin_b_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  std::unique_ptr<FeaturePolicy> policy4 =
      CreateFromParentPolicy(policy1.get(), origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy4->IsFeatureEnabled(kDefaultSelfFeature));
}

TEST_F(FeaturePolicyTest, TestFeaturesAreIndependent) {
  // +-----------------------------------------------+
  // |(1) Origin A                                   |
  // |Policy: {"default-self": ["self", "Origin B"], |
  // |         "default-on": ["self"]}               |
  // | +-------------------------------------------+ |
  // | |(2) Origin B                               | |
  // | |Policy: {"default-self": ["*"],            | |
  // | |         "default-on": ["*"]}              | |
  // | | +-------------+                           | |
  // | | |(3)Origin C  |                           | |
  // | | |No Policy    |                           | |
  // | | +-------------+                           | |
  // | +-------------------------------------------+ |
  // +-----------------------------------------------+
  // Default-self feature should be enabled in all frames; Default-on feature
  // should be enabled in frame 1, and disabled in frames 2 and 3.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-self", false, {origin_a_, origin_b_}},
                             {"default-on", false, {origin_a_}}}});
  std::unique_ptr<FeaturePolicy> policy2 =
      CreateFromParentPolicy(policy1.get(), origin_b_);
  policy2->SetHeaderPolicy(
      {{{"default-self", true, std::vector<url::Origin>()},
        {"default-on", true, std::vector<url::Origin>()}}});
  std::unique_ptr<FeaturePolicy> policy3 =
      CreateFromParentPolicy(policy2.get(), origin_c_);
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_TRUE(policy1->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy2->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy2->IsFeatureEnabled(kDefaultOnFeature));
  EXPECT_TRUE(policy3->IsFeatureEnabled(kDefaultSelfFeature));
  EXPECT_FALSE(policy3->IsFeatureEnabled(kDefaultOnFeature));
}

TEST_F(FeaturePolicyTest, TestFeatureEnabledForOrigin) {
  // +-----------------------------------------------+
  // |(1) Origin A                                   |
  // |Policy: {"default-off": ["self", "Origin B"]}  |
  // +-----------------------------------------------+
  // Features should be enabled by the policy in frame 1 for origins A and B,
  // and disabled for origin C.
  std::unique_ptr<FeaturePolicy> policy1 =
      CreateFromParentPolicy(nullptr, origin_a_);
  policy1->SetHeaderPolicy({{{"default-off", false, {origin_a_, origin_b_}}}});
  EXPECT_TRUE(
      policy1->IsFeatureEnabledForOrigin(kDefaultOffFeature, origin_a_));
  EXPECT_TRUE(
      policy1->IsFeatureEnabledForOrigin(kDefaultOffFeature, origin_b_));
  EXPECT_FALSE(
      policy1->IsFeatureEnabledForOrigin(kDefaultOffFeature, origin_c_));
}

}  // namespace content
