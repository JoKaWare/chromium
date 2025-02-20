// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREFS_PROFILE_PREF_STORE_MANAGER_H_
#define CHROME_BROWSER_PREFS_PROFILE_PREF_STORE_MANAGER_H_

#include <stddef.h>

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "components/user_prefs/tracked/pref_hash_filter.h"

class HashStoreContents;
class PersistentPrefStore;
class PrefHashStore;
class PrefService;
class TrackedPreferenceValidationDelegate;

namespace base {
class DictionaryValue;
class SequencedTaskRunner;
}  // namespace base

namespace user_prefs {
class PrefRegistrySyncable;
}  // namespace user_prefs

// Provides a facade through which the user preference store may be accessed and
// managed.
class ProfilePrefStoreManager {
 public:
  // Instantiates a ProfilePrefStoreManager with the configuration required to
  // manage the user preferences of the profile at |profile_path|.
  // |tracking_configuration| is used for preference tracking.
  // |reporting_ids_count| is the count of all possible tracked preference IDs
  // (possibly greater than |tracking_configuration.size()|).
  // |seed| and |legacy_device_id| are used to track preference value changes
  // and must be the same on each launch in order to verify loaded preference
  // values.
  ProfilePrefStoreManager(
      const base::FilePath& profile_path,
      const std::vector<PrefHashFilter::TrackedPreferenceMetadata>&
          tracking_configuration,
      size_t reporting_ids_count,
      const std::string& seed,
      const std::string& legacy_device_id,
      PrefService* local_state);

  ~ProfilePrefStoreManager();

  static const bool kPlatformSupportsPreferenceTracking;

  // Register user prefs used by the profile preferences system.
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Retrieves the time of the last preference reset event, if any, for
  // |pref_service|. Assumes that |pref_service| is backed by a PrefStore that
  // was built by ProfilePrefStoreManager.
  // If no reset has occurred, returns a null |Time|.
  static base::Time GetResetTime(PrefService* pref_service);

  // Clears the time of the last preference reset event, if any, for
  // |pref_service|. Assumes that |pref_service| is backed by a PrefStore that
  // was built by ProfilePrefStoreManager.
  static void ClearResetTime(PrefService* pref_service);

#if defined(OS_WIN)
  // Call before startup tasks kick in to use a different registry path for
  // storing and validating tracked preference MACs. Callers are responsible
  // for ensuring that the key is deleted on shutdown. For testing only.
  static void SetPreferenceValidationRegistryPathForTesting(
      const base::string16* path);
#endif

  // Creates a PersistentPrefStore providing access to the user preferences of
  // the managed profile. If |on_reset| is provided, it will be invoked if a
  // reset occurs as a result of loading the profile's prefs.
  // An optional |validation_delegate| will be notified
  // of the status of each tracked preference as they are checked.
  PersistentPrefStore* CreateProfilePrefStore(
      const scoped_refptr<base::SequencedTaskRunner>& io_task_runner,
      const base::Closure& on_reset_on_load,
      TrackedPreferenceValidationDelegate* validation_delegate);

  // Initializes the preferences for the managed profile with the preference
  // values in |master_prefs|. Acts synchronously, including blocking IO.
  // Returns true on success.
  bool InitializePrefsFromMasterPrefs(
      const base::DictionaryValue& master_prefs);

  // Creates a single-file PrefStore as was used in M34 and earlier. Used only
  // for testing migration.
  PersistentPrefStore* CreateDeprecatedCombinedProfilePrefStore(
      const scoped_refptr<base::SequencedTaskRunner>& io_task_runner);

 private:
  // Returns a PrefHashStore for the managed profile. Should only be called
  // if |kPlatformSupportsPreferenceTracking|. |use_super_mac| determines
  // whether the returned object will calculate, store, and validate super MACs
  // (and, by extension, accept non-null newly protected preferences as
  // TrustedInitialized).
  std::unique_ptr<PrefHashStore> GetPrefHashStore(bool use_super_mac);

  // Returns a PrefHashStore and HashStoreContents which can be be used for
  // extra out-of-band verifications, or nullptrs if not available on this
  // platform.
  std::pair<std::unique_ptr<PrefHashStore>, std::unique_ptr<HashStoreContents>>
  GetExternalVerificationPrefHashStorePair();

  const base::FilePath profile_path_;
  const std::vector<PrefHashFilter::TrackedPreferenceMetadata>
      tracking_configuration_;
  const size_t reporting_ids_count_;
  const std::string seed_;
  const std::string legacy_device_id_;
  PrefService* local_state_;

  DISALLOW_COPY_AND_ASSIGN(ProfilePrefStoreManager);
};

#endif  // CHROME_BROWSER_PREFS_PROFILE_PREF_STORE_MANAGER_H_
