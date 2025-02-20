// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_API_FILE_HANDLERS_APP_FILE_HANDLER_UTIL_H_
#define CHROME_BROWSER_EXTENSIONS_API_FILE_HANDLERS_APP_FILE_HANDLER_UTIL_H_

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/file_handler_info.h"

class Profile;

namespace extensions {

struct EntryInfo;
struct FileHandlerInfo;
struct GrantedFileEntry;

// TODO(benwells): move this to platform_apps namespace.
namespace app_file_handler_util {

extern const char kInvalidParameters[];
extern const char kSecurityError[];

// Returns the file handler with the specified |handler_id|, or NULL if there
// is no such handler.
const FileHandlerInfo* FileHandlerForId(const Extension& app,
                                        const std::string& handler_id);

// Returns the handlers that can handle all files in |entries|.
std::vector<const FileHandlerInfo*> FindFileHandlersForEntries(
    const Extension& extension,
    const std::vector<EntryInfo> entries);

bool FileHandlerCanHandleEntry(const FileHandlerInfo& handler,
                               const EntryInfo& entry);

// Creates a new file entry and allows |renderer_id| to access |path|. This
// registers a new file system for |path|.
GrantedFileEntry CreateFileEntry(Profile* profile,
                                 const Extension* extension,
                                 int renderer_id,
                                 const base::FilePath& path,
                                 bool is_directory);

// |directory_paths| contain the set of directories out of |paths|.
// For directories it makes sure they exist at their corresponding |paths|,
// while for regular files it makes sure they exist (i.e. not links) at |paths|,
// creating files if needed. If result is successful it calls |on_success|,
// otherwise calls |on_failure|.
void PrepareFilesForWritableApp(
    const std::vector<base::FilePath>& paths,
    Profile* profile,
    const std::set<base::FilePath>& directory_paths,
    const base::Closure& on_success,
    const base::Callback<void(const base::FilePath&)>& on_failure);

// Returns whether |extension| has the fileSystem.write permission.
bool HasFileSystemWritePermission(const Extension* extension);

// Validates a file entry and populates |file_path| with the absolute path if it
// is valid.
bool ValidateFileEntryAndGetPath(const std::string& filesystem_name,
                                 const std::string& filesystem_path,
                                 int render_process_id,
                                 base::FilePath* file_path,
                                 std::string* error);

}  // namespace app_file_handler_util

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_API_FILE_HANDLERS_APP_FILE_HANDLER_UTIL_H_
