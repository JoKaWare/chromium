// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SAFE_BROWSING_PING_MANAGER_H_
#define CHROME_BROWSER_SAFE_BROWSING_PING_MANAGER_H_

// A class that reports safebrowsing statistics to Google's SafeBrowsing
// servers.
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "chrome/browser/permissions/permission_uma_util.h"
#include "chrome/browser/safe_browsing/protocol_manager_helper.h"
#include "components/safe_browsing_db/hit_report.h"
#include "components/safe_browsing_db/util.h"
#include "content/public/browser/permission_type.h"
#include "net/log/net_log_with_source.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "url/gurl.h"

class Profile;
class SkBitmap;

namespace net {
class URLRequestContextGetter;
}  // namespace net

namespace safe_browsing {

class NotificationImageReporter;
class PermissionReporter;
class SafeBrowsingDatabaseManager;

class SafeBrowsingPingManager : public net::URLFetcherDelegate {
 public:
  ~SafeBrowsingPingManager() override;

  // Create an instance of the safe browsing ping manager.
  static std::unique_ptr<SafeBrowsingPingManager> Create(
      net::URLRequestContextGetter* request_context_getter,
      const SafeBrowsingProtocolConfig& config);

  // net::URLFetcherDelegate interface.
  void OnURLFetchComplete(const net::URLFetcher* source) override;

  // Report to Google when a SafeBrowsing warning is shown to the user.
  // |hit_report.threat_type| should be one of the types known by
  // SafeBrowsingtHitUrl.
  void ReportSafeBrowsingHit(const safe_browsing::HitReport& hit_report);

  // Users can opt-in on the SafeBrowsing interstitial to send detailed
  // threat reports. |report| is the serialized report.
  void ReportThreatDetails(const std::string& report);

  // Report permission action to SafeBrowsing servers.
  void ReportPermissionAction(const PermissionReportInfo& report_info);

  // Report notification content image to SafeBrowsing CSD server if necessary.
  void ReportNotificationImage(
      Profile* profile,
      const scoped_refptr<SafeBrowsingDatabaseManager>& database_manager,
      const GURL& origin,
      const SkBitmap& image);

 private:
  friend class NotificationImageReporterTest;
  friend class PermissionReporterBrowserTest;
  friend class SafeBrowsingPingManagerTest;
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingPingManagerTest,
                           TestSafeBrowsingHitUrl);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingPingManagerTest, TestThreatDetailsUrl);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingPingManagerTest,
                           TestReportThreatDetails);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingPingManagerTest,
                           TestReportSafeBrowsingHit);
  FRIEND_TEST_ALL_PREFIXES(SafeBrowsingPingManagerCertReportingTest,
                           UMAOnFailure);

  typedef std::set<std::unique_ptr<net::URLFetcher>> Reports;

  // Constructs a SafeBrowsingPingManager that issues network requests
  // using |request_context_getter|.
  SafeBrowsingPingManager(
      net::URLRequestContextGetter* request_context_getter,
      const SafeBrowsingProtocolConfig& config);

  // Generates URL for reporting safe browsing hits.
  GURL SafeBrowsingHitUrl(const safe_browsing::HitReport& hit_report) const;

  // Generates URL for reporting threat details for users who opt-in.
  GURL ThreatDetailsUrl() const;

  // Current product version sent in each request.
  std::string version_;

  // The safe browsing client name sent in each request.
  std::string client_name_;

  // The context we use to issue network requests.
  scoped_refptr<net::URLRequestContextGetter> request_context_getter_;

  // URL prefix where browser reports hits to the safebrowsing list and
  // sends detaild threat reports for UMA users.
  std::string url_prefix_;

  // Track outstanding SafeBrowsing report fetchers for clean up.
  // We add both "hit" and "detail" fetchers in this set.
  Reports safebrowsing_reports_;

  // Sends reports of permission actions.
  std::unique_ptr<PermissionReporter> permission_reporter_;

  // Sends reports of notification content images.
  std::unique_ptr<NotificationImageReporter> notification_image_reporter_;

  net::NetLogWithSource net_log_;

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingPingManager);
};

}  // namespace safe_browsing

#endif  // CHROME_BROWSER_SAFE_BROWSING_PING_MANAGER_H_
