// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/api/quic_url_utils.h"

#include "testing/gtest/include/gtest/gtest.h"

using std::string;

namespace net {
namespace test {
namespace {

TEST(QuicUrlUtilsTest, IsValidSNI) {
  // IP as SNI.
  EXPECT_FALSE(QuicUrlUtils::IsValidSNI("192.168.0.1"));
  // SNI without any dot.
  EXPECT_FALSE(QuicUrlUtils::IsValidSNI("somedomain"));
  // Invalid by RFC2396 but unfortunately domains of this form exist.
  EXPECT_TRUE(QuicUrlUtils::IsValidSNI("some_domain.com"));
  // An empty string must be invalid otherwise the QUIC client will try sending
  // it.
  EXPECT_FALSE(QuicUrlUtils::IsValidSNI(""));

  // Valid SNI
  EXPECT_TRUE(QuicUrlUtils::IsValidSNI("test.google.com"));
}

TEST(QuicUrlUtilsTest, NormalizeHostname) {
  struct {
    const char *input, *expected;
  } tests[] = {
      {
          "www.google.com", "www.google.com",
      },
      {
          "WWW.GOOGLE.COM", "www.google.com",
      },
      {
          "www.google.com.", "www.google.com",
      },
      {
          "www.google.COM.", "www.google.com",
      },
      {
          "www.google.com..", "www.google.com",
      },
      {
          "www.google.com........", "www.google.com",
      },
  };

  for (size_t i = 0; i < arraysize(tests); ++i) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", tests[i].input);
    EXPECT_EQ(string(tests[i].expected), QuicUrlUtils::NormalizeHostname(buf));
  }
}

}  // namespace
}  // namespace test
}  // namespace net
