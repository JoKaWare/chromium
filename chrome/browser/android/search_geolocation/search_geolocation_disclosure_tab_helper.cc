// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/search_geolocation/search_geolocation_disclosure_tab_helper.h"

#include "base/android/jni_android.h"
#include "base/android/jni_string.h"
#include "base/feature_list.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/android/search_geolocation/search_geolocation_disclosure_infobar_delegate.h"
#include "chrome/browser/android/search_geolocation/search_geolocation_service.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/web_contents.h"
#include "jni/GeolocationHeader_jni.h"
#include "jni/SearchGeolocationDisclosureTabHelper_jni.h"
#include "third_party/WebKit/public/platform/modules/permissions/permission_status.mojom.h"
#include "url/origin.h"

namespace {

const int kDefaultMaxShowCount = 3;
const int kDefaultDaysPerShow = 1;
const char kMaxShowCountVariation[] = "MaxShowCount";
const char kDaysPerShowVariation[] = "DaysPerShow";

bool gIgnoreUrlChecksForTesting = false;
int gDayOffsetForTesting = 0;

int GetMaxShowCount() {
  std::string variation = variations::GetVariationParamValueByFeature(
      features::kConsistentOmniboxGeolocation, kMaxShowCountVariation);
  int max_show;
  if (!variation.empty() && base::StringToInt(variation, &max_show))
    return max_show;

  return kDefaultMaxShowCount;
}

int GetDaysPerShow() {
  std::string variation = variations::GetVariationParamValueByFeature(
      features::kConsistentOmniboxGeolocation, kDaysPerShowVariation);
  int days_per_show;
  if (!variation.empty() && base::StringToInt(variation, &days_per_show))
    return days_per_show;

  return kDefaultDaysPerShow;
}

base::Time GetTimeNow() {
  return base::Time::Now() + base::TimeDelta::FromDays(gDayOffsetForTesting);
}

}  // namespace

DEFINE_WEB_CONTENTS_USER_DATA_KEY(SearchGeolocationDisclosureTabHelper);

SearchGeolocationDisclosureTabHelper::SearchGeolocationDisclosureTabHelper(
    content::WebContents* contents)
    : content::WebContentsObserver(contents) {
  consistent_geolocation_enabled_ =
      base::FeatureList::IsEnabled(features::kConsistentOmniboxGeolocation);
}

SearchGeolocationDisclosureTabHelper::~SearchGeolocationDisclosureTabHelper() {}

void SearchGeolocationDisclosureTabHelper::NavigationEntryCommitted(
    const content::LoadCommittedDetails& load_details) {
  if (consistent_geolocation_enabled_)
    MaybeShowDisclosure(web_contents()->GetVisibleURL());
}

void SearchGeolocationDisclosureTabHelper::MaybeShowDisclosure(
    const GURL& gurl) {
  if (!ShouldShowDisclosureForUrl(gurl))
    return;

  // Don't show the infobar if the user has dismissed it, or they've seen it
  // enough times already.
  PrefService* prefs = GetProfile()->GetPrefs();
  bool dismissed_already =
      prefs->GetBoolean(prefs::kSearchGeolocationDisclosureDismissed);
  int shown_count =
      prefs->GetInteger(prefs::kSearchGeolocationDisclosureShownCount);
  if (dismissed_already || shown_count >= GetMaxShowCount()) {
    // Record metrics for the state of permissions after the disclosure has been
    // shown. This is not done immediately after showing the last disclosure
    // (i.e. at the end of this function), but on the next omnibox search, to
    // allow the metric to capture changes to settings done by the user as a
    // result of clicking on the Settings link in the disclosure.
    RecordPostDisclosureMetrics(gurl);
    return;
  }

  // Or if it has been shown too recently.
  base::Time last_shown = base::Time::FromInternalValue(
      prefs->GetInt64(prefs::kSearchGeolocationDisclosureLastShowDate));
  if (GetTimeNow() - last_shown < base::TimeDelta::FromDays(GetDaysPerShow())) {
    return;
  }

  // Record metrics for the state of permissions before the disclosure has been
  // shown.
  RecordPreDisclosureMetrics(gurl);

  // Only show the disclosure if the geolocation permission is set ask (i.e. has
  // not been explicitly set or revoked).
  ContentSetting status =
      HostContentSettingsMapFactory::GetForProfile(GetProfile())
          ->GetContentSetting(gurl, gurl, CONTENT_SETTINGS_TYPE_GEOLOCATION,
                              std::string());
  if (status != CONTENT_SETTING_ASK)
    return;

  // And only show disclosure if the DSE geolocation setting is on.
  SearchGeolocationService* service =
      SearchGeolocationService::Factory::GetForBrowserContext(GetProfile());
  if (!service->GetDSEGeolocationSetting())
    return;

  // Check that the Chrome app has geolocation permission.
  JNIEnv* env = base::android::AttachCurrentThread();
  if (!Java_GeolocationHeader_hasGeolocationPermission(env))
    return;

  // All good, let's show the disclosure and increment the shown count.
  SearchGeolocationDisclosureInfoBarDelegate::Create(web_contents(), gurl);
  shown_count++;
  prefs->SetInteger(prefs::kSearchGeolocationDisclosureShownCount, shown_count);
  prefs->SetInt64(prefs::kSearchGeolocationDisclosureLastShowDate,
                  GetTimeNow().ToInternalValue());
}

// static
void SearchGeolocationDisclosureTabHelper::ResetDisclosure(Profile* profile) {
  PrefService* prefs = profile->GetPrefs();
  prefs->ClearPref(prefs::kSearchGeolocationDisclosureShownCount);
  prefs->ClearPref(prefs::kSearchGeolocationDisclosureLastShowDate);
  prefs->ClearPref(prefs::kSearchGeolocationDisclosureDismissed);
}

// static
void SearchGeolocationDisclosureTabHelper::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterBooleanPref(prefs::kSearchGeolocationDisclosureDismissed,
                                false);
  registry->RegisterIntegerPref(prefs::kSearchGeolocationDisclosureShownCount,
                                0);
  registry->RegisterInt64Pref(prefs::kSearchGeolocationDisclosureLastShowDate,
                              0);
  registry->RegisterBooleanPref(
      prefs::kSearchGeolocationPreDisclosureMetricsRecorded, false);
  registry->RegisterBooleanPref(
      prefs::kSearchGeolocationPostDisclosureMetricsRecorded, false);
}

// static
bool SearchGeolocationDisclosureTabHelper::Register(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

bool SearchGeolocationDisclosureTabHelper::ShouldShowDisclosureForUrl(
    const GURL& gurl) {
  SearchGeolocationService* service =
      SearchGeolocationService::Factory::GetForBrowserContext(GetProfile());

  // Check the service first, as we don't want to show the infobar even when
  // testing if it does not exist.
  if (!service)
    return false;

  if (gIgnoreUrlChecksForTesting)
    return true;

  return service->UseDSEGeolocationSetting(url::Origin(gurl));
}

void SearchGeolocationDisclosureTabHelper::RecordPreDisclosureMetrics(
    const GURL& gurl) {
  PrefService* prefs = GetProfile()->GetPrefs();
  if (!prefs->GetBoolean(
          prefs::kSearchGeolocationPreDisclosureMetricsRecorded)) {
    ContentSetting status =
        HostContentSettingsMapFactory::GetForProfile(GetProfile())
            ->GetContentSetting(gurl, gurl, CONTENT_SETTINGS_TYPE_GEOLOCATION,
                                std::string());
    UMA_HISTOGRAM_ENUMERATION(
        "GeolocationDisclosure.PreDisclosureContentSetting",
        static_cast<base::HistogramBase::Sample>(status),
        static_cast<base::HistogramBase::Sample>(CONTENT_SETTING_NUM_SETTINGS) +
            1);
    prefs->SetBoolean(prefs::kSearchGeolocationPreDisclosureMetricsRecorded,
                      true);
  }
}

void SearchGeolocationDisclosureTabHelper::RecordPostDisclosureMetrics(
    const GURL& gurl) {
  PrefService* prefs = GetProfile()->GetPrefs();
  if (!prefs->GetBoolean(
          prefs::kSearchGeolocationPostDisclosureMetricsRecorded)) {
    ContentSetting status =
        HostContentSettingsMapFactory::GetForProfile(GetProfile())
            ->GetContentSetting(gurl, gurl, CONTENT_SETTINGS_TYPE_GEOLOCATION,
                                std::string());
    UMA_HISTOGRAM_ENUMERATION(
        "GeolocationDisclosure.PostDisclosureContentSetting",
        static_cast<base::HistogramBase::Sample>(status),
        static_cast<base::HistogramBase::Sample>(CONTENT_SETTING_NUM_SETTINGS) +
            1);
    prefs->SetBoolean(prefs::kSearchGeolocationPostDisclosureMetricsRecorded,
                      true);
  }
}

Profile* SearchGeolocationDisclosureTabHelper::GetProfile() {
  return Profile::FromBrowserContext(web_contents()->GetBrowserContext());
}

// static
void SetIgnoreUrlChecksForTesting(
    JNIEnv* env,
    const base::android::JavaParamRef<jclass>& clazz) {
  gIgnoreUrlChecksForTesting = true;
}

// static
void SetDayOffsetForTesting(JNIEnv* env,
                            const base::android::JavaParamRef<jclass>& clazz,
                            jint days) {
  gDayOffsetForTesting = days;
}
