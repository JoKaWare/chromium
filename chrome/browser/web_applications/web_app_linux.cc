// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/web_app.h"

#include <utility>

#include "base/environment.h"
#include "base/logging.h"
#include "build/build_config.h"
#include "chrome/browser/shell_integration_linux.h"
#include "content/public/browser/browser_thread.h"

namespace web_app {

void UpdateShortcutsForAllApps(Profile* profile,
                               const base::Closure& callback) {
  callback.Run();
}

namespace internals {

bool CreatePlatformShortcuts(
    const base::FilePath& web_app_path,
    std::unique_ptr<ShortcutInfo> shortcut_info,
    const ShortcutLocations& creation_locations,
    ShortcutCreationReason /*creation_reason*/) {
#if !defined(OS_CHROMEOS)
  DCHECK_CURRENTLY_ON(content::BrowserThread::FILE);
  return shell_integration_linux::CreateDesktopShortcut(*shortcut_info,
                                                        creation_locations);
#else
  return false;
#endif
}

void DeletePlatformShortcuts(const base::FilePath& web_app_path,
                             std::unique_ptr<ShortcutInfo> shortcut_info) {
#if !defined(OS_CHROMEOS)
  shell_integration_linux::DeleteDesktopShortcuts(shortcut_info->profile_path,
                                                  shortcut_info->extension_id);
#endif
}

void UpdatePlatformShortcuts(const base::FilePath& web_app_path,
                             const base::string16& /*old_app_title*/,
                             std::unique_ptr<ShortcutInfo> shortcut_info) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::FILE);

  std::unique_ptr<base::Environment> env(base::Environment::Create());

  // Find out whether shortcuts are already installed.
  ShortcutLocations creation_locations =
      shell_integration_linux::GetExistingShortcutLocations(
          env.get(), shortcut_info->profile_path, shortcut_info->extension_id);

  // Always create a hidden shortcut in applications if a visible one is not
  // being created. This allows the operating system to identify the app, but
  // not show it in the menu.
  if (creation_locations.applications_menu_location == APP_MENU_LOCATION_NONE)
    creation_locations.applications_menu_location = APP_MENU_LOCATION_HIDDEN;

  CreatePlatformShortcuts(web_app_path, std::move(shortcut_info),
                          creation_locations, SHORTCUT_CREATION_AUTOMATED);
}

void DeleteAllShortcutsForProfile(const base::FilePath& profile_path) {
#if !defined(OS_CHROMEOS)
  shell_integration_linux::DeleteAllDesktopShortcuts(profile_path);
#endif
}

}  // namespace internals

}  // namespace web_app
