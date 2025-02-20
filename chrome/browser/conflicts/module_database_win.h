// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CONFLICTS_MODULE_DATABASE_WIN_H_
#define CHROME_BROWSER_CONFLICTS_MODULE_DATABASE_WIN_H_

#include <map>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/sequenced_task_runner.h"
#include "content/public/common/process_type.h"

// A class that keeps track of all modules loaded across Chrome processes.
// Drives the chrome://conflicts UI.
//
// This is effectively a singleton, but doesn't use base::Singleton. The intent
// is for the object to be created when Chrome is single-threaded, and for it
// be set as the process-wide singleton via SetInstance.
class ModuleDatabase {
 public:
  // Used as a unique identifier for a module in a ModuleSet.
  using ModuleId = int;

  // The type of certificate found for the module.
  enum CertificateType {
    // The module is not signed.
    NO_CERTIFICATE,
    // The module is signed and the certificate is in the module.
    CERTIFICATE_IN_FILE,
    // The module is signed and the certificate is in an external catalog.
    CERTIFICATE_IN_CATALOG,
  };

  // Structures for maintaining information about modules.
  struct ModuleInfoKey;
  struct CertificateInfo;
  struct ModuleInfoData;
  using ModuleMap = std::map<ModuleInfoKey, ModuleInfoData>;
  using ModuleInfo = ModuleMap::value_type;

  // Used for maintaing a list of modules loaded in a process. Maps module IDs
  // to load addresses.
  using ModuleLoadAddresses = std::vector<std::pair<ModuleId, uintptr_t>>;

  // Structures for maintaining information about running processes.
  struct ProcessInfoKey;
  struct ProcessInfoData;
  using ProcessMap = std::map<ProcessInfoKey, ProcessInfoData>;
  using ProcessInfo = ProcessMap::value_type;

  // A ModuleDatabase is by default bound to a provided sequenced task runner.
  // All calls must be made in the context of this task runner, unless
  // otherwise noted. For calls from other contexts this task runner is used to
  // bounce the call when appropriate.
  explicit ModuleDatabase(scoped_refptr<base::SequencedTaskRunner> task_runner);
  ~ModuleDatabase();

  // Retrieves the singleton global instance of the ModuleDatabase.
  static ModuleDatabase* GetInstance();

  // Sets the global instance of the ModuleDatabase. Ownership is passed to the
  // global instance and deliberately leaked, unless manually cleaned up. This
  // has no locking and should be called when Chrome is single threaded.
  static void SetInstance(std::unique_ptr<ModuleDatabase> module_database);

  // Indicates that process with the given type has started. This must be called
  // before any calls to OnModuleEvent or OnModuleUnload. Must be called in the
  // same sequence as |task_runner_|.
  void OnProcessStarted(uint32_t process_id,
                        uint64_t creation_time,
                        content::ProcessType process_type);

  // Indicates that a module has been loaded. The data passed to this function
  // is taken as gospel, so if it originates from a remote process it should be
  // independently validated first. (In practice, see ModuleEventSinkImpl for
  // details of where this happens.)
  void OnModuleLoad(uint32_t process_id,
                    uint64_t creation_time,
                    const base::FilePath& module_path,
                    uint32_t module_size,
                    uint32_t module_time_date_stamp,
                    uintptr_t module_load_address);

  // Indicates that the module at the given |load_address| in the specified
  // process is being unloaded. This need not be trusted data, as it will be
  // validated by the ModuleDatabase directly.
  void OnModuleUnload(uint32_t process_id,
                      uint64_t creation_time,
                      uintptr_t module_load_address);

  // Indicates that the given process has ended. This can be called from any
  // thread and will be bounced to the |task_runner_|. In practice it will be
  // invoked from the UI thread as the Mojo channel is torn down.
  void OnProcessEnded(uint32_t process_id, uint64_t creation_time);

  // TODO(chrisha): Module analysis code, and various accessors for use by
  // chrome://conflicts.

 private:
  friend class TestModuleDatabase;
  friend class ModuleDatabaseTest;
  friend class ModuleEventSinkImplTest;

  // Used by the FindLoadAddress* functions to indicate a load address has not
  // been found.
  static constexpr size_t kInvalidIndex = ~0u;

  // Converts a valid |process_type| to a bit for use in a bitmask of process
  // values. Exposed in the header for testing.
  static uint32_t ProcessTypeToBit(content::ProcessType process_type);

  // Converts a |bit_index| (which maps to the bit 1 << bit_index) to the
  // corresponding process type. Exposed in the header for testing.
  static content::ProcessType BitIndexToProcessType(uint32_t bit_index);

  // Performs a linear scan to find the index of a |module_id| or |load_address|
  // in a collection of modules. Returns kInvalidIndex if the index is not
  // found.
  static size_t FindLoadAddressIndexById(
      ModuleId module_id,
      const ModuleLoadAddresses& load_addresses);
  static size_t FindLoadAddressIndexByAddress(
      uintptr_t load_address,
      const ModuleLoadAddresses& load_addresses);

  // Inserts a module into a ModuleLoadAddress object.
  static void InsertLoadAddress(ModuleId module_id,
                                uintptr_t load_address,
                                ModuleLoadAddresses* load_addresses);

  // Removes a module from a ModuleLoadAddress object, either by the
  // |module_id| or the |index| in the collection.
  static void RemoveLoadAddressById(ModuleId module_id,
                                    ModuleLoadAddresses* load_addresses);
  static void RemoveLoadAddressByIndex(size_t index,
                                       ModuleLoadAddresses* load_addresses);

  // Finds or creates a mutable ModuleInfo entry.
  ModuleInfo* FindOrCreateModuleInfo(const base::FilePath& module_path,
                                     uint32_t module_size,
                                     uint32_t module_time_date_stamp);

  // Finds a process info entry. Returns nullptr if none is found.
  ProcessInfo* GetProcessInfo(uint32_t process_id, uint64_t creation_time);

  // Creates a process info entry.
  void CreateProcessInfo(uint32_t process_id,
                         uint64_t creation_time,
                         content::ProcessType process_type);

  // Deletes a process info entry.
  void DeleteProcessInfo(uint32_t process_id, uint64_t creation_time);

  // The task runner to which this object is bound.
  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  // A map of all known modules.
  ModuleMap modules_;

  // A map of all known running processes, and modules loaded/unloaded in
  // them.
  ProcessMap processes_;

  // Weak pointer factory for this object. This is used when bouncing
  // incoming events to |task_runner_|.
  base::WeakPtrFactory<ModuleDatabase> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ModuleDatabase);
};

// Maintains information about a module. Modules are permanent once added to
// the ModuleSet, so this structure grows monotonically. In practice this is
// not an issue as the modules themselves are vastly bigger than the minor
// amount of metadata tracked here.

// This is the constant portion of the module information, and acts as the key
// in a std::map.
struct ModuleDatabase::ModuleInfoKey {
  ModuleInfoKey(const base::FilePath& module_path,
                uint32_t module_size,
                uint32_t module_time_date_stamp,
                uint32_t module_id);

  // Less-than operator allowing this object to be used in std::map.
  bool operator<(const ModuleInfoKey& mi) const;

  // Full path to the module on disk. Part of the key for a ModuleInfo.
  base::FilePath module_path;

  // The module size. Part of the key for a ModuleInfo. This is taken from
  // SizeOfImage from the module's IMAGE_OPTIONAL_HEADER.
  uint32_t module_size;

  // The module time date stamp. Part of the key for a ModuleInfo. Taken from
  // TimeDateStamp from the module's IMAGE_FILE_HEADER.
  uint32_t module_time_date_stamp;

  // The ID of this module. This is a strictly incrementing value, and is used
  // to tie a module to the list of running processes in which it is found.
  // It is not part of the key for the module, but it is immutable. This is
  // simply the index of the module in the insertion order.
  ModuleId module_id;
};

// Information about the certificate of a file.
struct ModuleDatabase::CertificateInfo {
  CertificateInfo();

  // The type of signature encountered.
  CertificateType type;

  // Path to the file containing the certificate. Empty if |type| is
  // NO_CERTIFICATE.
  base::FilePath path;

  // The "Subject" name of the certificate. This is the signer (ie,
  // "Google Inc." or "Microsoft Inc.").
  base::string16 subject;
};

// This is the mutable portion of the module information, and is the storage
// type in a std::map.
struct ModuleDatabase::ModuleInfoData {
  ModuleInfoData();
  ModuleInfoData(const ModuleInfoData& others);
  ~ModuleInfoData();

  // Set of all process types in which this module has been seen (may not be
  // currently present in a process of that type). This is a conversion of
  // ProcessType enumeration to a bitfield. See "ProcessTypeToBit" and
  // "BitIndexToProcessType" for details.
  uint32_t process_types;

  // The following pieces of information are determined via a detailed
  // inspection of the module. This is relatively expensive and uses blocking
  // IO, so is performed in a background task.

  // The module path, not including the basename. This is cleaned and normalized
  // so that common paths are converted to their environment variable mappings
  // (ie, %systemroot%). This makes i18n localized paths easily comparable.
  base::string16 location;

  // The basename of the module.
  base::string16 basename;

  // The name of the product the module belongs to.
  base::string16 product_name;

  // The module file description.
  base::string16 description;

  // The module version. This is usually in the form a.b.c.d (where a, b, c and
  // d are integers), but may also have fewer than 4 components.
  base::string16 version;

  // The certificate info for the module.
  CertificateInfo certificate_info;
};

// Information about a running process. This ties modules in a ModuleSet to
// processes in which they are (or have been) loaded.

// This is the constant portion of the process information, and acts as the key
// in a std::map.
struct ModuleDatabase::ProcessInfoKey {
  ProcessInfoKey(uint32_t process_id,
                 uint64_t creation_time,
                 content::ProcessType process_type);
  ~ProcessInfoKey();

  // Less-than operator allowing this object to be used in std::map.
  bool operator<(const ProcessInfoKey& pi) const;

  // The process ID.
  uint32_t process_id;

  // The process creation time. A raw FILETIME value with full precision.
  // Combined with |process_id| this uniquely identifies a process on a Windows
  // system.
  uint64_t creation_time;

  // The type of the process.
  content::ProcessType process_type;
};

// This is the mutable portion of the process information, and is the storage
// type in a std::map.
struct ModuleDatabase::ProcessInfoData {
  ProcessInfoData();
  ProcessInfoData(const ProcessInfoData& other);
  ~ProcessInfoData();

  // The sets of modules that are loaded/unloaded in this process, by ID. This
  // is typically a small list so a linear cost is okay to pay for
  // lookup/deletion (storage is backed by a vector).
  //
  // These are modified by the various static *LoadAddress* helper functions in
  // ModuleDatabase. The vector maintains the invariant the element with maximum
  // module ID is always last. This ensures that the usual operation of loading
  // a module is O(1).
  ModuleLoadAddresses loaded_modules;
  ModuleLoadAddresses unloaded_modules;
};

#endif  // CHROME_BROWSER_CONFLICTS_MODULE_DATABASE_WIN_H_
