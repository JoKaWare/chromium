// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/values.h"
#include "chrome/browser/chromeos/app_mode/arc/arc_kiosk_app_manager.h"
#include "chrome/browser/chromeos/ownership/fake_owner_settings_service.h"
#include "chrome/browser/chromeos/policy/device_local_account.h"
#include "chrome/browser/chromeos/settings/scoped_cros_settings_test_helper.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chromeos/settings/cros_settings_names.h"
#include "components/arc/arc_util.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace {

// The purpose of this waiter - wait for being notified some amount of times.
class NotificationWaiter
    : public ArcKioskAppManager::ArcKioskAppManagerObserver {
 public:
  // In constructor we provide instance of ArcKioskAppManager and subscribe for
  // notifications from it, and minimum amount of times we expect to get the
  // notification.
  NotificationWaiter(ArcKioskAppManager* manager, int expected_notifications)
      : manager_(manager), expected_notifications_(expected_notifications) {
    manager_->AddObserver(this);
  }

  ~NotificationWaiter() override { manager_->RemoveObserver(this); }

  void Wait() {
    if (notification_received_)
      return;
    run_loop_.reset(new base::RunLoop());
    run_loop_->Run();
  }

  // Returns if the waiter was notified at least expected_notifications_ times.
  bool was_notified() const { return notification_received_; }

 private:
  // ArcKioskAppManagerObserver:
  void OnArcKioskAppsChanged() override {
    --expected_notifications_;
    if (expected_notifications_ > 0)
      return;

    notification_received_ = true;
    if (run_loop_)
      run_loop_->Quit();
  }

  std::unique_ptr<base::RunLoop> run_loop_;
  ArcKioskAppManager* manager_;
  bool notification_received_ = false;
  int expected_notifications_;

  DISALLOW_COPY_AND_ASSIGN(NotificationWaiter);
};

std::string GenerateAccountId(std::string package_name) {
  return package_name + "@ark-kiosk-app";
}

}  // namespace

class ArcKioskAppManagerTest : public InProcessBrowserTest {
 public:
  ArcKioskAppManagerTest() : settings_helper_(false) {}
  ~ArcKioskAppManagerTest() override {}

  void SetUpCommandLine(base::CommandLine* command_line) override {
    InProcessBrowserTest::SetUpCommandLine(command_line);
    arc::SetArcAvailableCommandLineForTesting(command_line);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    settings_helper_.ReplaceProvider(kAccountsPrefDeviceLocalAccounts);
    owner_settings_service_ =
        settings_helper_.CreateOwnerSettingsService(browser()->profile());
  }

  void TearDownOnMainThread() override { settings_helper_.RestoreProvider(); }

  void SetApps(const std::vector<policy::ArcKioskAppBasicInfo>& apps,
               const std::string& auto_login_account) {
    base::ListValue device_local_accounts;
    for (const policy::ArcKioskAppBasicInfo& app : apps) {
      std::unique_ptr<base::DictionaryValue> entry(new base::DictionaryValue);
      entry->SetStringWithoutPathExpansion(
          kAccountsPrefDeviceLocalAccountsKeyId,
          GenerateAccountId(app.package_name()));
      entry->SetIntegerWithoutPathExpansion(
          kAccountsPrefDeviceLocalAccountsKeyType,
          policy::DeviceLocalAccount::TYPE_ARC_KIOSK_APP);
      entry->SetStringWithoutPathExpansion(
          kAccountsPrefDeviceLocalAccountsKeyArcKioskPackage,
          app.package_name());
      entry->SetStringWithoutPathExpansion(
          kAccountsPrefDeviceLocalAccountsKeyArcKioskClass, app.class_name());
      entry->SetStringWithoutPathExpansion(
          kAccountsPrefDeviceLocalAccountsKeyArcKioskAction, app.action());
      device_local_accounts.Append(std::move(entry));
    }
    owner_settings_service_->Set(kAccountsPrefDeviceLocalAccounts,
                                 device_local_accounts);

    if (!auto_login_account.empty()) {
      owner_settings_service_->SetString(
          kAccountsPrefDeviceLocalAccountAutoLoginId, auto_login_account);
    }
  }

  void CleanApps() {
    base::ListValue device_local_accounts;
    owner_settings_service_->Set(kAccountsPrefDeviceLocalAccounts,
                                 device_local_accounts);
  }

  ArcKioskAppManager* manager() const { return ArcKioskAppManager::Get(); }

 protected:
  ScopedCrosSettingsTestHelper settings_helper_;
  std::unique_ptr<FakeOwnerSettingsService> owner_settings_service_;

 private:
  DISALLOW_COPY_AND_ASSIGN(ArcKioskAppManagerTest);
};

IN_PROC_BROWSER_TEST_F(ArcKioskAppManagerTest, Basic) {
  policy::ArcKioskAppBasicInfo app1("com.package.name1", std::string(),
                                    std::string());
  policy::ArcKioskAppBasicInfo app2("com.package.name2", std::string(),
                                    std::string());
  std::vector<policy::ArcKioskAppBasicInfo> init_apps{app1, app2};

  // Set initial list of apps.
  {
    // Observer must be notified once: app list was updated.
    NotificationWaiter waiter(manager(), 1);
    SetApps(init_apps, std::string());
    waiter.Wait();
    EXPECT_TRUE(waiter.was_notified());

    ArcKioskAppManager::ArcKioskApps apps = manager()->GetAllApps();
    ASSERT_EQ(2u, apps.size());
    ASSERT_EQ(app1, apps[0].app_info());
    ASSERT_EQ(app2, apps[1].app_info());
    EXPECT_FALSE(manager()->GetAutoLaunchAccountId().is_valid());
  }

  // Set auto-launch app.
  {
    // Observer must be notified twice: for policy list update and for
    // auto-launch app update.
    NotificationWaiter waiter(manager(), 2);
    SetApps(init_apps, GenerateAccountId(app2.package_name()));
    waiter.Wait();
    EXPECT_TRUE(waiter.was_notified());

    EXPECT_TRUE(manager()->GetAutoLaunchAccountId().is_valid());
    ASSERT_EQ(2u, manager()->GetAllApps().size());

    ArcKioskAppManager::ArcKioskApps apps = manager()->GetAllApps();
    ASSERT_EQ(app1, apps[0].app_info());
    ASSERT_EQ(app2, apps[1].app_info());
    EXPECT_TRUE(manager()->GetAutoLaunchAccountId().is_valid());
    ASSERT_EQ(apps[1].account_id(), manager()->GetAutoLaunchAccountId());
  }

  // Create a new list of apps, where there is no app2 (is auto launch now),
  // and present a new app.
  policy::ArcKioskAppBasicInfo app3("com.package.name3", std::string(),
                                    std::string());
  std::vector<policy::ArcKioskAppBasicInfo> new_apps{app1, app3};
  {
    // Observer must be notified once: app list was updated.
    NotificationWaiter waiter(manager(), 1);
    SetApps(new_apps, std::string());
    waiter.Wait();
    EXPECT_TRUE(waiter.was_notified());

    ArcKioskAppManager::ArcKioskApps apps = manager()->GetAllApps();
    ASSERT_EQ(2u, apps.size());
    ASSERT_EQ(app1, apps[0].app_info());
    ASSERT_EQ(app3, apps[1].app_info());
    // Auto launch app must be reset.
    EXPECT_FALSE(manager()->GetAutoLaunchAccountId().is_valid());
  }

  // Claen the apps.
  {
    // Observer must be notified once: app list was updated.
    NotificationWaiter waiter(manager(), 1);
    CleanApps();
    waiter.Wait();
    EXPECT_TRUE(waiter.was_notified());

    ASSERT_EQ(0u, manager()->GetAllApps().size());
    EXPECT_FALSE(manager()->GetAutoLaunchAccountId().is_valid());
  }
}

}  // namespace chromeos
