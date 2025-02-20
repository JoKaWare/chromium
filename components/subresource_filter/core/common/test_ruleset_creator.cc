// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/core/common/test_ruleset_creator.h"

#include <string>

#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "components/subresource_filter/core/common/indexed_ruleset.h"
#include "components/subresource_filter/core/common/proto/rules.pb.h"
#include "components/subresource_filter/core/common/test_ruleset_utils.h"
#include "components/subresource_filter/core/common/unindexed_ruleset.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/protobuf/src/google/protobuf/io/zero_copy_stream_impl_lite.h"

namespace subresource_filter {

namespace {

// The methods below assume that char and uint8_t are interchangeable.
static_assert(CHAR_BIT == 8, "Assumed char was 8 bits.");

void WriteRulesetContents(const std::vector<uint8_t>& contents,
                          base::FilePath path) {
  int ruleset_size_as_int = base::checked_cast<int>(contents.size());
  int num_bytes_written =
      base::WriteFile(path, reinterpret_cast<const char*>(contents.data()),
                      ruleset_size_as_int);
  ASSERT_EQ(ruleset_size_as_int, num_bytes_written);
}

std::vector<uint8_t> SerializeUnindexedRulesetWithMultipleRules(
    const std::vector<proto::UrlRule>& rules) {
  std::string ruleset_contents;
  google::protobuf::io::StringOutputStream output(&ruleset_contents);
  UnindexedRulesetWriter ruleset_writer(&output);
  for (const auto& rule : rules)
    ruleset_writer.AddUrlRule(rule);
  ruleset_writer.Finish();

  auto data = reinterpret_cast<const uint8_t*>(ruleset_contents.data());
  return std::vector<uint8_t>(data, data + ruleset_contents.size());
}

std::vector<uint8_t> SerializeUnindexedRulesetWithSingleRule(
    const proto::UrlRule& rule) {
  return SerializeUnindexedRulesetWithMultipleRules({rule});
}

std::vector<uint8_t> SerializeIndexedRulesetWithMultipleRules(
    const std::vector<proto::UrlRule>& rules) {
  RulesetIndexer indexer;
  for (const auto& rule : rules)
    EXPECT_TRUE(indexer.AddUrlRule(rule));
  indexer.Finish();
  return std::vector<uint8_t>(indexer.data(), indexer.data() + indexer.size());
}

}  // namespace

namespace testing {

// TestRuleset -----------------------------------------------------------------

TestRuleset::TestRuleset() = default;
TestRuleset::~TestRuleset() = default;

// static
base::File TestRuleset::Open(const TestRuleset& ruleset) {
  base::File file;
  file.Initialize(ruleset.path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                                    base::File::FLAG_SHARE_DELETE);
  return file;
}

// static
void TestRuleset::CorruptByTruncating(const TestRuleset& ruleset,
                                      size_t tail_size) {
  ASSERT_GT(tail_size, 0u);
  ASSERT_FALSE(ruleset.contents.empty());
  std::vector<uint8_t> new_contents = ruleset.contents;

  const size_t new_size =
      tail_size < new_contents.size() ? new_contents.size() - tail_size : 0;
  new_contents.resize(new_size);
  WriteRulesetContents(new_contents, ruleset.path);
}

// static
void TestRuleset::CorruptByFilling(const TestRuleset& ruleset,
                                   size_t from,
                                   size_t to,
                                   uint8_t fill_with) {
  ASSERT_LT(from, to);
  ASSERT_LE(to, ruleset.contents.size());

  std::vector<uint8_t> new_contents = ruleset.contents;
  for (size_t i = from; i < to; ++i)
    new_contents[i] = fill_with;
  WriteRulesetContents(new_contents, ruleset.path);
}

// TestRulesetPair -------------------------------------------------------------

TestRulesetPair::TestRulesetPair() = default;
TestRulesetPair::~TestRulesetPair() = default;

// TestRulesetCreator ----------------------------------------------------------

TestRulesetCreator::TestRulesetCreator() = default;
TestRulesetCreator::~TestRulesetCreator() = default;

void TestRulesetCreator::CreateRulesetToDisallowURLsWithPathSuffix(
    base::StringPiece suffix,
    TestRulesetPair* test_ruleset_pair) {
  DCHECK(test_ruleset_pair);
  proto::UrlRule suffix_rule = CreateSuffixRule(suffix);
  CreateRulesetWithRules({suffix_rule}, test_ruleset_pair);
}

void TestRulesetCreator::CreateUnindexedRulesetToDisallowURLsWithPathSuffix(
    base::StringPiece suffix,
    TestRuleset* test_unindexed_ruleset) {
  DCHECK(test_unindexed_ruleset);
  proto::UrlRule suffix_rule = CreateSuffixRule(suffix);
  ASSERT_NO_FATAL_FAILURE(CreateTestRulesetFromContents(
      SerializeUnindexedRulesetWithSingleRule(suffix_rule),
      test_unindexed_ruleset));
}

void TestRulesetCreator::CreateRulesetToDisallowURLsWithManySuffixes(
    base::StringPiece suffix,
    int num_of_suffixes,
    TestRulesetPair* test_ruleset_pair) {
  DCHECK(test_ruleset_pair);

  std::vector<proto::UrlRule> rules;
  for (int i = 0; i < num_of_suffixes; ++i) {
    std::string current_suffix =
        suffix.as_string() + '_' + base::IntToString(i);
    rules.push_back(CreateSuffixRule(current_suffix));
  }
  CreateRulesetWithRules(rules, test_ruleset_pair);
}

void TestRulesetCreator::CreateRulesetWithRules(
    const std::vector<proto::UrlRule>& rules,
    TestRulesetPair* test_ruleset_pair) {
  ASSERT_NO_FATAL_FAILURE(CreateTestRulesetFromContents(
      SerializeUnindexedRulesetWithMultipleRules(rules),
      &test_ruleset_pair->unindexed));
  ASSERT_NO_FATAL_FAILURE(CreateTestRulesetFromContents(
      SerializeIndexedRulesetWithMultipleRules(rules),
      &test_ruleset_pair->indexed));
}

void TestRulesetCreator::GetUniqueTemporaryPath(base::FilePath* path) {
  DCHECK(path);
  ASSERT_TRUE(scoped_temp_dir_.IsValid() ||
              scoped_temp_dir_.CreateUniqueTempDir());
  *path = scoped_temp_dir_.GetPath().AppendASCII(
      base::IntToString(next_unique_file_suffix++));
}

void TestRulesetCreator::CreateTestRulesetFromContents(
    std::vector<uint8_t> ruleset_contents,
    TestRuleset* ruleset) {
  DCHECK(ruleset);

  ruleset->contents = std::move(ruleset_contents);
  ASSERT_NO_FATAL_FAILURE(GetUniqueTemporaryPath(&ruleset->path));
  WriteRulesetContents(ruleset->contents, ruleset->path);
}

}  // namespace testing
}  // namespace subresource_filter
