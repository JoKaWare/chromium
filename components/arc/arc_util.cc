// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/arc/arc_util.h"

#include <string>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "chromeos/chromeos_switches.h"
#include "components/user_manager/user_manager.h"

namespace arc {

namespace {

// This is for finch. See also crbug.com/633704 for details.
// TODO(hidehiko): More comments of the intention how this works, when
// we unify the commandline flags.
const base::Feature kEnableArcFeature{"EnableARC",
                                      base::FEATURE_DISABLED_BY_DEFAULT};

// Possible values for --arc-availability flag.
constexpr char kAvailabilityNone[] = "none";
constexpr char kAvailabilityInstalled[] = "installed";
constexpr char kAvailabilityInstalledOnlyKioskSupported[] =
    "installed-only-kiosk-supported";
constexpr char kAvailabilityOfficiallySupported[] = "officially-supported";
constexpr char kAvailabilityOfficiallySupportedWithActiveDirectory[] =
    "officially-supported-with-active-directory";

}  // namespace

bool IsArcAvailable() {
  const auto* command_line = base::CommandLine::ForCurrentProcess();

  if (command_line->HasSwitch(chromeos::switches::kArcAvailability)) {
    std::string value = command_line->GetSwitchValueASCII(
        chromeos::switches::kArcAvailability);
    DCHECK(value == kAvailabilityNone ||
           value == kAvailabilityInstalled ||
           value == kAvailabilityInstalledOnlyKioskSupported ||
           value == kAvailabilityOfficiallySupported ||
           value == kAvailabilityOfficiallySupportedWithActiveDirectory)
        << "Unknown flag value: " << value;
    return value == kAvailabilityOfficiallySupported ||
           value == kAvailabilityOfficiallySupportedWithActiveDirectory ||
           ((value == kAvailabilityInstalled ||
             value == kAvailabilityInstalledOnlyKioskSupported) &&
            base::FeatureList::IsEnabled(kEnableArcFeature));
  }

  // For transition, fallback to old flags.
  // TODO(hidehiko): Remove this and clean up whole this function, when
  // session_manager supports a new flag.
  return command_line->HasSwitch(chromeos::switches::kEnableArc) ||
      (command_line->HasSwitch(chromeos::switches::kArcAvailable) &&
       base::FeatureList::IsEnabled(kEnableArcFeature));
}

bool IsArcKioskAvailable() {
  const auto* command_line = base::CommandLine::ForCurrentProcess();

  if (command_line->HasSwitch(chromeos::switches::kArcAvailability)) {
    std::string value =
        command_line->GetSwitchValueASCII(chromeos::switches::kArcAvailability);
    if (value == kAvailabilityInstalledOnlyKioskSupported)
      return true;
  }

  // If not special kiosk device case, use general ARC check.
  return IsArcAvailable();
}

void SetArcAvailableCommandLineForTesting(base::CommandLine* command_line) {
  command_line->AppendSwitchASCII(chromeos::switches::kArcAvailability,
                                  kAvailabilityOfficiallySupported);
}

bool IsArcKioskMode() {
  return user_manager::UserManager::IsInitialized() &&
         user_manager::UserManager::Get()->IsLoggedInAsArcKioskApp();
}

bool IsArcAllowedForActiveDirectoryUsers() {
  const auto* command_line = base::CommandLine::ForCurrentProcess();

  if (!command_line->HasSwitch(chromeos::switches::kArcAvailability))
    return false;

  return command_line->GetSwitchValueASCII(
             chromeos::switches::kArcAvailability) ==
         kAvailabilityOfficiallySupportedWithActiveDirectory;
}

bool IsArcOptInVerificationDisabled() {
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  return command_line->HasSwitch(
      chromeos::switches::kDisableArcOptInVerification);
}

}  // namespace arc
