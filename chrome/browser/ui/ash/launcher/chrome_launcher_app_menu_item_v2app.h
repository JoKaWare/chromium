// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ASH_LAUNCHER_CHROME_LAUNCHER_APP_MENU_ITEM_V2APP_H_
#define CHROME_BROWSER_UI_ASH_LAUNCHER_CHROME_LAUNCHER_APP_MENU_ITEM_V2APP_H_

#include <string>

#include "ash/public/cpp/shelf_application_menu_item.h"
#include "base/macros.h"

class ChromeLauncherController;

// A shelf application menu item for a running V2 application.
class ChromeLauncherAppMenuItemV2App : public ash::ShelfApplicationMenuItem {
 public:
  ChromeLauncherAppMenuItemV2App(const base::string16 title,
                                 const gfx::Image* icon,
                                 const std::string& app_id,
                                 ChromeLauncherController* launcher_controller,
                                 int app_index);
  ~ChromeLauncherAppMenuItemV2App() override;

  // ash::ShelfApplicationMenuItem:
  void Execute(int event_flags) override;

 private:
  // The owning class which can be used to validate the controller.
  ChromeLauncherController* launcher_controller_;

  // The application ID.
  const std::string app_id_;

  // The index for the given application.
  const int app_index_;

  DISALLOW_COPY_AND_ASSIGN(ChromeLauncherAppMenuItemV2App);
};

#endif  // CHROME_BROWSER_UI_ASH_LAUNCHER_CHROME_LAUNCHER_APP_MENU_ITEM_V2APP_H_
