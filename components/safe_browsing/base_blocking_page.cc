// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/base_blocking_page.h"

#include "base/bind.h"
#include "base/lazy_instance.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "components/safe_browsing_db/safe_browsing_prefs.h"
#include "components/security_interstitials/content/security_interstitial_controller_client.h"
#include "components/security_interstitials/core/metrics_helper.h"
#include "content/public/browser/interstitial_page.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/user_metrics.h"
#include "content/public/browser/web_contents.h"

using base::UserMetricsAction;
using content::InterstitialPage;
using content::WebContents;
using security_interstitials::SafeBrowsingErrorUI;
using security_interstitials::SecurityInterstitialControllerClient;

namespace safe_browsing {

namespace {

base::LazyInstance<BaseBlockingPage::UnsafeResourceMap>::Leaky
    g_unsafe_resource_map = LAZY_INSTANCE_INITIALIZER;

}  // namespace

BaseBlockingPage::BaseBlockingPage(
    BaseUIManager* ui_manager,
    WebContents* web_contents,
    const GURL& main_frame_url,
    const UnsafeResourceList& unsafe_resources,
    std::unique_ptr<SecurityInterstitialControllerClient> controller_client,
    const SafeBrowsingErrorUI::SBErrorDisplayOptions& display_options)
    : SecurityInterstitialPage(web_contents,
                               unsafe_resources[0].url,
                               std::move(controller_client)),
      ui_manager_(ui_manager),
      main_frame_url_(main_frame_url),
      navigation_entry_index_to_remove_(
          IsMainPageLoadBlocked(unsafe_resources) ?
              -1 :
              web_contents->GetController().GetLastCommittedEntryIndex()),
      unsafe_resources_(unsafe_resources),
      sb_error_ui_(base::MakeUnique<SafeBrowsingErrorUI>(
                       unsafe_resources_[0].url, main_frame_url_,
                       GetInterstitialReason(unsafe_resources_),
                       display_options,
                       ui_manager->app_locale(),
                       base::Time::NowFromSystemTime(),
                       controller())),
      proceeded_(false) {}

BaseBlockingPage::~BaseBlockingPage() {}

// static
const SafeBrowsingErrorUI::SBErrorDisplayOptions
BaseBlockingPage::CreateDefaultDisplayOptions() {
  return SafeBrowsingErrorUI::SBErrorDisplayOptions(
      true,    // IsMainPageLoadBlocked()
      false,   // kSafeBrowsingExtendedReportingOptInAllowed
      false,   // is_off_the_record
      false,   // is_extended_reporting
      false,   // is_scout
      false);  // kSafeBrowsingProceedAnywayDisabled
}

// static
void BaseBlockingPage::ShowBlockingPage(
    BaseUIManager* ui_manager,
    const UnsafeResource& unsafe_resource) {
  WebContents* web_contents = unsafe_resource.web_contents_getter.Run();

  if (!InterstitialPage::GetInterstitialPage(web_contents) ||
      !unsafe_resource.is_subresource) {
    // There is no interstitial currently showing in that tab, or we are about
    // to display a new one for the main frame. If there is already an
    // interstitial, showing the new one will automatically hide the old one.
    content::NavigationEntry* entry =
        unsafe_resource.GetNavigationEntryForResource();
    const UnsafeResourceList resources{unsafe_resource};
    BaseBlockingPage* blocking_page =
        new BaseBlockingPage(
            ui_manager, web_contents,
            entry ? entry->GetURL() : GURL(),
            resources,
            CreateControllerClient(
                web_contents, resources,
                ui_manager->history_service(web_contents),
                ui_manager->app_locale(),
                ui_manager->default_safe_page()),
                CreateDefaultDisplayOptions());
    blocking_page->Show();
    return;
  }

  // This is an interstitial for a page's resource, let's queue it.
  UnsafeResourceMap* unsafe_resource_map = GetUnsafeResourcesMap();
  (*unsafe_resource_map)[web_contents].push_back(unsafe_resource);
}

// static
bool BaseBlockingPage::IsMainPageLoadBlocked(
    const UnsafeResourceList& unsafe_resources) {
  // If there is more than one unsafe resource, the main page load must not be
  // blocked. Otherwise, check if the one resource is.
  return unsafe_resources.size() == 1 &&
         unsafe_resources[0].IsMainPageLoadBlocked();
}

void BaseBlockingPage::OnProceed() {
  proceeded_ = true;

  ui_manager_->OnBlockingPageDone(unsafe_resources_, true /* proceed */,
                                  web_contents(), main_frame_url_);
}

void BaseBlockingPage::OnDontProceed() {
  // We could have already called Proceed(), in which case we must not notify
  // the SafeBrowsingUIManager again, as the client has been deleted.
  if (proceeded_)
    return;

  UpdateMetricsAfterSecurityInterstitial();
  if (!sb_error_ui_->is_proceed_anyway_disabled()) {
    controller()->metrics_helper()->RecordUserDecision(
        security_interstitials::MetricsHelper::DONT_PROCEED);
  }

  // Send the malware details, if we opted to.
  FinishThreatDetails(base::TimeDelta(), false /* did_proceed */,
                      controller()->metrics_helper()->NumVisits());  // No delay

  ui_manager_->OnBlockingPageDone(unsafe_resources_, false /* proceed */,
                                  web_contents(), main_frame_url_);

  // The user does not want to proceed, clear the queued unsafe resources
  // notifications we received while the interstitial was showing.
  UnsafeResourceMap* unsafe_resource_map = GetUnsafeResourcesMap();
  UnsafeResourceMap::iterator iter = unsafe_resource_map->find(web_contents());
  if (iter != unsafe_resource_map->end() && !iter->second.empty()) {
    ui_manager_->OnBlockingPageDone(iter->second, false, web_contents(),
                                    main_frame_url_);
    unsafe_resource_map->erase(iter);
  }

  // We don't remove the navigation entry if the tab is being destroyed as this
  // would trigger a navigation that would cause trouble as the render view host
  // for the tab has by then already been destroyed.  We also don't delete the
  // current entry if it has been committed again, which is possible on a page
  // that had a subresource warning.
  const int last_committed_index =
      web_contents()->GetController().GetLastCommittedEntryIndex();
  if (navigation_entry_index_to_remove_ != -1 &&
      navigation_entry_index_to_remove_ != last_committed_index &&
      !web_contents()->IsBeingDestroyed()) {
    CHECK(web_contents()->GetController().RemoveEntryAtIndex(
        navigation_entry_index_to_remove_));
  }
}

void BaseBlockingPage::CommandReceived(
    const std::string& page_cmd) {
  if (page_cmd == "\"pageLoadComplete\"") {
    // content::WaitForRenderFrameReady sends this message when the page
    // load completes. Ignore it.
    return;
  }

  int command = 0;
  bool retval = base::StringToInt(page_cmd, &command);
  DCHECK(retval) << page_cmd;

  sb_error_ui_->HandleCommand(
      static_cast<security_interstitials::SecurityInterstitialCommands>(
          command));
}

bool BaseBlockingPage::ShouldCreateNewNavigation() const {
  return sb_error_ui_->is_main_frame_load_blocked();
}

void BaseBlockingPage::PopulateInterstitialStrings(
    base::DictionaryValue* load_time_data) {
  sb_error_ui_->PopulateStringsForHTML(load_time_data);
}

void BaseBlockingPage::FinishThreatDetails(const base::TimeDelta& delay,
                                           bool did_proceed,
                                           int num_visits) {}

// static
BaseBlockingPage::UnsafeResourceMap*
BaseBlockingPage::GetUnsafeResourcesMap() {
  return g_unsafe_resource_map.Pointer();
}

// static
std::string BaseBlockingPage::GetMetricPrefix(
    const UnsafeResourceList& unsafe_resources,
    SafeBrowsingErrorUI::SBInterstitialReason interstitial_reason) {
  bool primary_subresource = unsafe_resources[0].is_subresource;
  switch (interstitial_reason) {
    case SafeBrowsingErrorUI::SB_REASON_MALWARE:
      return primary_subresource ? "malware_subresource" : "malware";
    case SafeBrowsingErrorUI::SB_REASON_HARMFUL:
      return primary_subresource ? "harmful_subresource" : "harmful";
    case SafeBrowsingErrorUI::SB_REASON_PHISHING:
      ThreatPatternType threat_pattern_type =
          unsafe_resources[0].threat_metadata.threat_pattern_type;
      if (threat_pattern_type == ThreatPatternType::PHISHING ||
          threat_pattern_type == ThreatPatternType::NONE)
        return primary_subresource ? "phishing_subresource" : "phishing";
      else if (threat_pattern_type == ThreatPatternType::SOCIAL_ENGINEERING_ADS)
        return primary_subresource ? "social_engineering_ads_subresource"
                                   : "social_engineering_ads";
      else if (threat_pattern_type ==
               ThreatPatternType::SOCIAL_ENGINEERING_LANDING)
        return primary_subresource ? "social_engineering_landing_subresource"
                                   : "social_engineering_landing";
  }
  NOTREACHED();
  return "unkown_metric_prefix";
}

// We populate a parallel set of metrics to differentiate some threat sources.
// static
std::string BaseBlockingPage::GetExtraMetricsSuffix(
    const UnsafeResourceList& unsafe_resources) {
  switch (unsafe_resources[0].threat_source) {
    case safe_browsing::ThreatSource::DATA_SAVER:
      return "from_data_saver";
    case safe_browsing::ThreatSource::REMOTE:
    case safe_browsing::ThreatSource::LOCAL_PVER3:
      // REMOTE and LOCAL_PVER3 can be distinguished in the logs
      // by platform type: Remote is mobile, local_pver3 is desktop.
      return "from_device";
    case safe_browsing::ThreatSource::LOCAL_PVER4:
      return "from_device_v4";
    case safe_browsing::ThreatSource::CLIENT_SIDE_DETECTION:
      return "from_client_side_detection";
    case safe_browsing::ThreatSource::UNKNOWN:
      break;
  }
  NOTREACHED();
  return std::string();
}

// static
SafeBrowsingErrorUI::SBInterstitialReason
BaseBlockingPage::GetInterstitialReason(
    const UnsafeResourceList& unsafe_resources) {
  bool harmful = false;
  for (UnsafeResourceList::const_iterator iter = unsafe_resources.begin();
       iter != unsafe_resources.end(); ++iter) {
    const BaseUIManager::UnsafeResource& resource = *iter;
    safe_browsing::SBThreatType threat_type = resource.threat_type;
    if (threat_type == SB_THREAT_TYPE_URL_MALWARE ||
        threat_type == SB_THREAT_TYPE_CLIENT_SIDE_MALWARE_URL) {
      return SafeBrowsingErrorUI::SB_REASON_MALWARE;
    } else if (threat_type == SB_THREAT_TYPE_URL_UNWANTED) {
      harmful = true;
    } else {
      DCHECK(threat_type == SB_THREAT_TYPE_URL_PHISHING ||
             threat_type == SB_THREAT_TYPE_CLIENT_SIDE_PHISHING_URL);
    }
  }

  if (harmful)
    return SafeBrowsingErrorUI::SB_REASON_HARMFUL;
  return SafeBrowsingErrorUI::SB_REASON_PHISHING;
}

BaseUIManager* BaseBlockingPage::ui_manager() const {
  return ui_manager_;
}

const GURL BaseBlockingPage::main_frame_url() const {
  return main_frame_url_;
}

BaseBlockingPage::UnsafeResourceList
BaseBlockingPage::unsafe_resources() const {
  return unsafe_resources_;
}

SafeBrowsingErrorUI* BaseBlockingPage::sb_error_ui() const {
  return sb_error_ui_.get();
}

void BaseBlockingPage::set_proceeded(bool proceeded) {
  proceeded_ = proceeded;
}

// static
std::unique_ptr<SecurityInterstitialControllerClient>
BaseBlockingPage::CreateControllerClient(
    content::WebContents* web_contents,
    const UnsafeResourceList& unsafe_resources,
    history::HistoryService* history_service,
    const std::string& app_locale,
    const GURL& default_safe_page) {
  SafeBrowsingErrorUI::SBInterstitialReason interstitial_reason =
      GetInterstitialReason(unsafe_resources);
  security_interstitials::MetricsHelper::ReportDetails reporting_info;
  reporting_info.metric_prefix =
      GetMetricPrefix(unsafe_resources, interstitial_reason);
  reporting_info.extra_suffix = GetExtraMetricsSuffix(unsafe_resources);

  std::unique_ptr<security_interstitials::MetricsHelper> metrics_helper =
      base::MakeUnique<security_interstitials::MetricsHelper>(
          unsafe_resources[0].url, reporting_info, history_service);

  return base::MakeUnique<SecurityInterstitialControllerClient>(
      web_contents, std::move(metrics_helper), nullptr, app_locale,
      default_safe_page);
}

}  // namespace safe_browsing
