// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/message_center_notification_manager.h"

#include <memory>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/api/notification_provider/notification_provider_api.h"
#include "chrome/browser/notifications/extension_welcome_notification.h"
#include "chrome/browser/notifications/extension_welcome_notification_factory.h"
#include "chrome/browser/notifications/fullscreen_notification_blocker.h"
#include "chrome/browser/notifications/message_center_settings_controller.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/notifications/notification_conversion_helper.h"
#include "chrome/browser/notifications/profile_notification.h"
#include "chrome/browser/notifications/screen_lock_notification_blocker.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/notification_provider.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension_set.h"
#include "extensions/common/permissions/permissions_data.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/message_center/message_center_style.h"
#include "ui/message_center/message_center_tray.h"
#include "ui/message_center/message_center_types.h"
#include "ui/message_center/notifier_settings.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/notifications/login_state_notification_blocker_chromeos.h"
#endif

#if defined(USE_ASH)
#include "ash/common/system/web_notification/web_notification_tray.h"
#include "ash/shell.h"
#endif

using message_center::NotifierId;

MessageCenterNotificationManager::MessageCenterNotificationManager(
    message_center::MessageCenter* message_center,
    std::unique_ptr<message_center::NotifierSettingsProvider> settings_provider)
    : message_center_(message_center),
      settings_provider_(std::move(settings_provider)),
      system_observer_(this),
      stats_collector_(message_center),
      google_now_stats_collector_(message_center) {
  message_center_->AddObserver(this);
  message_center_->SetNotifierSettingsProvider(settings_provider_.get());

#if defined(OS_CHROMEOS)
  blockers_.push_back(
      base::MakeUnique<LoginStateNotificationBlockerChromeOS>(message_center));
#else
  blockers_.push_back(
      base::MakeUnique<ScreenLockNotificationBlocker>(message_center));
#endif
  blockers_.push_back(
      base::MakeUnique<FullscreenNotificationBlocker>(message_center));

#if defined(OS_WIN) || defined(OS_MACOSX) \
  || (defined(OS_LINUX) && !defined(OS_CHROMEOS))
  // On Windows, Linux and Mac, the notification manager owns the tray icon and
  // views.Other platforms have global ownership and Create will return NULL.
  tray_.reset(message_center::CreateMessageCenterTray());
#endif
}

MessageCenterNotificationManager::~MessageCenterNotificationManager() {
  message_center_->SetNotifierSettingsProvider(nullptr);
  message_center_->RemoveObserver(this);

  profile_notifications_.clear();
}

////////////////////////////////////////////////////////////////////////////////
// NotificationUIManager

void MessageCenterNotificationManager::Add(const Notification& notification,
                                           Profile* profile) {
  if (Update(notification, profile))
    return;

  std::unique_ptr<ProfileNotification> profile_notification_ptr =
      base::MakeUnique<ProfileNotification>(profile, notification);
  ProfileNotification* profile_notification = profile_notification_ptr.get();

  ExtensionWelcomeNotificationFactory::GetForBrowserContext(profile)->
      ShowWelcomeNotificationIfNecessary(profile_notification->notification());

  // WARNING: You MUST use AddProfileNotification or update the message center
  // via the notification within a ProfileNotification object or the profile ID
  // will not be correctly set for ChromeOS.
  // Takes ownership of profile_notification.
  AddProfileNotification(std::move(profile_notification_ptr));

  // TODO(liyanhou): Change the logic to only send notifications to one party.
  // Currently, if there is an app with notificationProvider permission,
  // notifications will go to both message center and the app.
  // Change it to send notifications to message center only when the user chose
  // default message center (extension_id.empty()).

  // If there exist apps/extensions that have notificationProvider permission,
  // route notifications to one of the apps/extensions.
  std::string extension_id = GetExtensionTakingOverNotifications(profile);
  if (!extension_id.empty())
    AddNotificationToAlternateProvider(profile_notification->notification(),
                                       profile, extension_id);

  message_center_->AddNotification(
      base::MakeUnique<message_center::Notification>(
          profile_notification->notification()));
}

bool MessageCenterNotificationManager::Update(const Notification& notification,
                                              Profile* profile) {
  const std::string& tag = notification.tag();
  if (tag.empty())
    return false;

  const GURL origin_url = notification.origin_url();
  DCHECK(origin_url.is_valid());

  // Since tag is provided by arbitrary JS, we need to use origin_url
  // (which is an app url in case of app/extension) to scope the tags
  // in the given profile.
  for (auto iter = profile_notifications_.begin();
       iter != profile_notifications_.end(); ++iter) {
    ProfileNotification* old_notification = (*iter).second.get();
    if (old_notification->notification().tag() == tag &&
        old_notification->notification().origin_url() == origin_url &&
        old_notification->profile_id() ==
            NotificationUIManager::GetProfileID(profile)) {
      // Changing the type from non-progress to progress does not count towards
      // the immediate update allowed in the message center.
      std::string old_id = old_notification->notification().id();

      // Add/remove notification in the local list but just update the same
      // one in MessageCenter.
      std::unique_ptr<ProfileNotification> new_notification =
          base::MakeUnique<ProfileNotification>(profile, notification);
      const Notification& notification = new_notification->notification();
      // Delete the old one after the new one is created to ensure we don't run
      // out of KeepAlives.
      profile_notifications_.erase(old_id);
      profile_notifications_[notification.id()] = std::move(new_notification);

      // TODO(liyanhou): Add routing updated notifications to alternative
      // providers.

      // Non-persistent Web Notifications rely on receiving the Display() event
      // to inform the developer, even when replacing a previous notification.
      if (notification.notifier_id().type == NotifierId::WEB_PAGE)
        notification.delegate()->Display();

      // WARNING: You MUST use AddProfileNotification or update the message
      // center via the notification within a ProfileNotification object or the
      // profile ID will not be correctly set for ChromeOS.
      message_center_->UpdateNotification(
          old_id, base::MakeUnique<message_center::Notification>(notification));

      return true;
    }
  }
  return false;
}

const Notification* MessageCenterNotificationManager::FindById(
    const std::string& delegate_id,
    ProfileID profile_id) const {
  // The profile pointer can be weak, the instance may have been destroyed, so
  // no profile method should be called inside this function.

  std::string profile_notification_id =
      ProfileNotification::GetProfileNotificationId(delegate_id, profile_id);
  auto iter = profile_notifications_.find(profile_notification_id);
  if (iter == profile_notifications_.end())
    return nullptr;
  return &(iter->second->notification());
}

bool MessageCenterNotificationManager::CancelById(
    const std::string& delegate_id,
    ProfileID profile_id) {
  // The profile pointer can be weak, the instance may have been destroyed, so
  // no profile method should be called inside this function.

  std::string profile_notification_id =
      ProfileNotification::GetProfileNotificationId(delegate_id, profile_id);
  // See if this ID hasn't been shown yet.
  // If it has been shown, remove it.
  auto iter = profile_notifications_.find(profile_notification_id);
  if (iter == profile_notifications_.end())
    return false;

  RemoveProfileNotification(iter->first);
  message_center_->RemoveNotification(profile_notification_id,
                                      /* by_user */ false);
  return true;
}

std::set<std::string>
MessageCenterNotificationManager::GetAllIdsByProfileAndSourceOrigin(
    ProfileID profile_id,
    const GURL& source) {
  std::set<std::string> delegate_ids;
  for (const auto& pair : profile_notifications_) {
    const Notification& notification = pair.second->notification();
    if (pair.second->profile_id() == profile_id &&
        notification.origin_url() == source) {
      delegate_ids.insert(notification.delegate_id());
    }
  }

  return delegate_ids;
}

std::set<std::string> MessageCenterNotificationManager::GetAllIdsByProfile(
    ProfileID profile_id) {
  std::set<std::string> delegate_ids;
  for (const auto& pair : profile_notifications_) {
    if (pair.second->profile_id() == profile_id)
      delegate_ids.insert(pair.second->notification().delegate_id());
  }

  return delegate_ids;
}

bool MessageCenterNotificationManager::CancelAllBySourceOrigin(
    const GURL& source) {
  // Same pattern as CancelById, but more complicated than the above
  // because there may be multiple notifications from the same source.
  bool removed = false;

  for (auto loopiter = profile_notifications_.begin();
       loopiter != profile_notifications_.end();) {
    auto curiter = loopiter++;
    if ((*curiter).second->notification().origin_url() == source) {
      const std::string id = curiter->first;
      RemoveProfileNotification(id);
      message_center_->RemoveNotification(id, /* by_user */ false);
      removed = true;
    }
  }
  return removed;
}

bool MessageCenterNotificationManager::CancelAllByProfile(
    ProfileID profile_id) {
  // Same pattern as CancelAllBySourceOrigin.
  bool removed = false;

  for (auto loopiter = profile_notifications_.begin();
       loopiter != profile_notifications_.end();) {
    auto curiter = loopiter++;
    if (profile_id == (*curiter).second->profile_id()) {
      const std::string id = curiter->first;
      RemoveProfileNotification(id);
      message_center_->RemoveNotification(id, /* by_user */ false);
      removed = true;
    }
  }
  return removed;
}

void MessageCenterNotificationManager::CancelAll() {
  message_center_->RemoveAllNotifications(
      false /* by_user */, message_center::MessageCenter::RemoveType::ALL);
}

////////////////////////////////////////////////////////////////////////////////
// MessageCenter::Observer
void MessageCenterNotificationManager::OnNotificationRemoved(
    const std::string& id,
    bool by_user) {
  auto iter = profile_notifications_.find(id);
  if (iter != profile_notifications_.end())
    RemoveProfileNotification(iter->first);
}

void MessageCenterNotificationManager::OnCenterVisibilityChanged(
    message_center::Visibility visibility) {
}

void MessageCenterNotificationManager::OnNotificationUpdated(
    const std::string& id) {
}

void MessageCenterNotificationManager::EnsureMessageCenterClosed() {
  if (tray_.get() && tray_->GetMessageCenterTray())
    tray_->GetMessageCenterTray()->HideMessageCenterBubble();

#if defined(USE_ASH)
  if (ash::Shell::HasInstance()) {
    ash::WebNotificationTray* tray =
        ash::Shell::GetInstance()->GetWebNotificationTray();
    if (tray)
      tray->GetMessageCenterTray()->HideMessageCenterBubble();
  }
#endif
}

void MessageCenterNotificationManager::SetMessageCenterTrayDelegateForTest(
    message_center::MessageCenterTrayDelegate* delegate) {
  tray_.reset(delegate);
}

std::string
MessageCenterNotificationManager::GetMessageCenterNotificationIdForTest(
    const std::string& delegate_id,
    Profile* profile) {
  return ProfileNotification::GetProfileNotificationId(delegate_id,
                                                       GetProfileID(profile));
}

////////////////////////////////////////////////////////////////////////////////
// private

void MessageCenterNotificationManager::AddNotificationToAlternateProvider(
    const Notification& notification,
    Profile* profile,
    const std::string& extension_id) const {
  // Convert data from Notification type to NotificationOptions type.
  extensions::api::notifications::NotificationOptions options;
  NotificationConversionHelper::NotificationToNotificationOptions(notification,
                                                                  &options);

  // Send the notification to the alternate provider extension/app.
  extensions::NotificationProviderEventRouter event_router(profile);
  event_router.CreateNotification(extension_id,
                                  notification.notifier_id().id,
                                  notification.delegate_id(),
                                  options);
}

void MessageCenterNotificationManager::AddProfileNotification(
    std::unique_ptr<ProfileNotification> profile_notification) {
  const Notification& notification = profile_notification->notification();
  std::string id = notification.id();
  // Notification ids should be unique.
  DCHECK(profile_notifications_.find(id) == profile_notifications_.end());
  profile_notifications_[id] = std::move(profile_notification);
}

void MessageCenterNotificationManager::RemoveProfileNotification(
    const std::string& notification_id) {
  auto it = profile_notifications_.find(notification_id);
  if (it == profile_notifications_.end())
    return;

  // Delay destruction of the ProfileNotification until after all the work
  // removing it from |profile_notifications_| is complete. This must be done
  // because this ProfileNotification might have the one ScopedKeepAlive object
  // that was keeping the browser alive, and destroying it would result in a re-
  // entrant call to this class. Because every method in this class touches
  // |profile_notifications_|, |profile_notifications_| must always be in a
  // self-consistent state in moments where re-entrance might happen.
  // https://crbug.com/649971
  std::unique_ptr<ProfileNotification> notification = std::move(it->second);
  profile_notifications_.erase(it);
  // Now that the map modifications are complete, going out of scope will
  // destroy the notification.
}

ProfileNotification* MessageCenterNotificationManager::FindProfileNotification(
    const std::string& id) const {
  auto iter = profile_notifications_.find(id);
  if (iter == profile_notifications_.end())
    return nullptr;

  return (*iter).second.get();
}

std::string
MessageCenterNotificationManager::GetExtensionTakingOverNotifications(
    Profile* profile) {
  // TODO(liyanhou): When additional settings in Chrome Settings is implemented,
  // change choosing the last app with permission to a user selected app.
  extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(profile);
  DCHECK(registry);
  std::string extension_id;
  for (extensions::ExtensionSet::const_iterator it =
           registry->enabled_extensions().begin();
       it != registry->enabled_extensions().end();
       ++it) {
    if ((*it->get()).permissions_data()->HasAPIPermission(
            extensions::APIPermission::ID::kNotificationProvider)) {
      extension_id = (*it->get()).id();
      return extension_id;
    }
  }
  return extension_id;
}
