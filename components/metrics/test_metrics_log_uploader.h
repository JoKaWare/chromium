// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_METRICS_TEST_METRICS_LOG_UPLOADER_H_
#define COMPONENTS_METRICS_TEST_METRICS_LOG_UPLOADER_H_

#include "components/metrics/metrics_log_uploader.h"

namespace metrics {

class TestMetricsLogUploader : public MetricsLogUploader {
 public:
  TestMetricsLogUploader(const std::string& server_url,
                         const std::string& mime_type,
                         const base::Callback<void(int)>& on_upload_complete);
  ~TestMetricsLogUploader() override;

  // Mark the current upload complete with the given response code.
  void CompleteUpload(int response_code);

  // Check if UploadLog has been called.
  bool is_uploading() const { return is_uploading_; }

 private:
  // MetricsLogUploader:
  void UploadLog(const std::string& compressed_log_data,
                 const std::string& log_hash) override;

  bool is_uploading_;

  DISALLOW_COPY_AND_ASSIGN(TestMetricsLogUploader);
};

}  // namespace metrics

#endif  // COMPONENTS_METRICS_TEST_METRICS_LOG_UPLOADER_H_
