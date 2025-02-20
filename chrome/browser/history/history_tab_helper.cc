// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/history/history_tab_helper.h"

#include <utility>

#include "build/build_config.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/prerender/prerender_contents.h"
#include "chrome/browser/prerender/prerender_manager.h"
#include "chrome/browser/prerender/prerender_manager_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/content/browser/history_context_helper.h"
#include "components/history/core/browser/history_service.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/frame_navigate_params.h"

#if !defined(OS_ANDROID)
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#endif

using content::NavigationEntry;
using content::WebContents;

DEFINE_WEB_CONTENTS_USER_DATA_KEY(HistoryTabHelper);

namespace {

// Referrer used for clicks on article suggestions on the NTP.
const char kChromeContentSuggestionsReferrer[] =
    "https://www.googleapis.com/auth/chrome-content-suggestions";

}  // namespace

HistoryTabHelper::HistoryTabHelper(WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      received_page_title_(false) {
}

HistoryTabHelper::~HistoryTabHelper() {
}

void HistoryTabHelper::UpdateHistoryForNavigation(
    const history::HistoryAddPageArgs& add_page_args) {
  history::HistoryService* hs = GetHistoryService();
  if (hs)
    GetHistoryService()->AddPage(add_page_args);
}

void HistoryTabHelper::UpdateHistoryPageTitle(const NavigationEntry& entry) {
  history::HistoryService* hs = GetHistoryService();
  if (hs)
    hs->SetPageTitle(entry.GetVirtualURL(), entry.GetTitleForDisplay());
}

history::HistoryAddPageArgs
HistoryTabHelper::CreateHistoryAddPageArgs(
    const GURL& virtual_url,
    base::Time timestamp,
    int nav_entry_id,
    content::NavigationHandle* navigation_handle) {
  // Clicks on content suggestions on the NTP should not contribute to the
  // Most Visited tiles in the NTP.
  const bool consider_for_ntp_most_visited =
      navigation_handle->GetReferrer().url != kChromeContentSuggestionsReferrer;

  history::HistoryAddPageArgs add_page_args(
      navigation_handle->GetURL(), timestamp,
      history::ContextIDForWebContents(web_contents()),
      nav_entry_id, navigation_handle->GetReferrer().url,
      navigation_handle->GetRedirectChain(),
      navigation_handle->GetPageTransition(), history::SOURCE_BROWSED,
      navigation_handle->DidReplaceEntry(), consider_for_ntp_most_visited);
  if (ui::PageTransitionIsMainFrame(navigation_handle->GetPageTransition()) &&
      virtual_url != navigation_handle->GetURL()) {
    // Hack on the "virtual" URL so that it will appear in history. For some
    // types of URLs, we will display a magic URL that is different from where
    // the page is actually navigated. We want the user to see in history what
    // they saw in the URL bar, so we add the virtual URL as a redirect.  This
    // only applies to the main frame, as the virtual URL doesn't apply to
    // sub-frames.
    add_page_args.url = virtual_url;
    if (!add_page_args.redirects.empty())
      add_page_args.redirects.back() = virtual_url;
  }
  return add_page_args;
}

void HistoryTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->HasCommitted())
    return;

  if (navigation_handle->IsInMainFrame()) {
    // Allow the new page to set the title again.
    received_page_title_ = false;
  }

  // Update history. Note that this needs to happen after the entry is complete,
  // which WillNavigate[Main,Sub]Frame will do before this function is called.
  if (!navigation_handle->ShouldUpdateHistory())
    return;

  // Most of the time, the displayURL matches the loaded URL, but for about:
  // URLs, we use a data: URL as the real value.  We actually want to save the
  // about: URL to the history db and keep the data: URL hidden. This is what
  // the WebContents' URL getter does.
  NavigationEntry* last_committed =
      web_contents()->GetController().GetLastCommittedEntry();
  const history::HistoryAddPageArgs& add_page_args =
      CreateHistoryAddPageArgs(
          web_contents()->GetURL(), last_committed->GetTimestamp(),
          last_committed->GetUniqueID(), navigation_handle);

  prerender::PrerenderManager* prerender_manager =
      prerender::PrerenderManagerFactory::GetForBrowserContext(
          web_contents()->GetBrowserContext());
  if (prerender_manager) {
    prerender::PrerenderContents* prerender_contents =
        prerender_manager->GetPrerenderContents(web_contents());
    if (prerender_contents) {
      prerender_contents->DidNavigate(add_page_args);
      return;
    }
  }

#if !defined(OS_ANDROID)
  // Don't update history if this web contents isn't associatd with a tab.
  Browser* browser = chrome::FindBrowserWithWebContents(web_contents());
  if (!browser || browser->is_app())
    return;
#endif

  UpdateHistoryForNavigation(add_page_args);
}

void HistoryTabHelper::TitleWasSet(NavigationEntry* entry, bool explicit_set) {
  if (received_page_title_)
    return;

  if (entry) {
    UpdateHistoryPageTitle(*entry);
    received_page_title_ = explicit_set;
  }
}

history::HistoryService* HistoryTabHelper::GetHistoryService() {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  if (profile->IsOffTheRecord())
    return NULL;

  return HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::IMPLICIT_ACCESS);
}

void HistoryTabHelper::WebContentsDestroyed() {
  // We update the history for this URL.
  WebContents* tab = web_contents();
  Profile* profile = Profile::FromBrowserContext(tab->GetBrowserContext());
  if (profile->IsOffTheRecord())
    return;

  history::HistoryService* hs = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::IMPLICIT_ACCESS);
  if (hs) {
    NavigationEntry* entry = tab->GetController().GetLastCommittedEntry();
    history::ContextID context_id = history::ContextIDForWebContents(tab);
    if (entry) {
      hs->UpdateWithPageEndTime(context_id, entry->GetUniqueID(), tab->GetURL(),
                                base::Time::Now());
    }
    hs->ClearCachedDataForContextID(context_id);
  }
}
