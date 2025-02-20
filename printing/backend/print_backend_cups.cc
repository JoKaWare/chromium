// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printing/backend/print_backend_cups.h"

#include <cups/ppd.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>

#include <string>

#include "base/debug/leak_annotations.h"
#include "base/files/file_util.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "base/values.h"
#include "printing/backend/cups_helper.h"
#include "printing/backend/print_backend_consts.h"
#include "url/gurl.h"

namespace printing {

namespace {

const char kCUPSPrinterInfoOpt[] = "printer-info";
const char kCUPSPrinterStateOpt[] = "printer-state";
const char kCUPSPrinterTypeOpt[] = "printer-type";

bool PrinterBasicInfoFromCUPS(const cups_dest_t& printer,
                              PrinterBasicInfo* printer_info) {
  // CUPS can have 'printers' that are actually scanners. (not MFC)
  // At least on Mac. Check for scanners and skip them.
  const char* type_str =
      cupsGetOption(kCUPSPrinterTypeOpt, printer.num_options, printer.options);
  if (type_str) {
    int type;
    if (base::StringToInt(type_str, &type) && (type & CUPS_PRINTER_SCANNER))
      return false;
  }

  printer_info->printer_name = printer.name;
  printer_info->is_default = printer.is_default;

  const char* info =
      cupsGetOption(kCUPSPrinterInfoOpt, printer.num_options, printer.options);
  if (info)
    printer_info->printer_description = info;

  const char* state =
      cupsGetOption(kCUPSPrinterStateOpt, printer.num_options, printer.options);
  if (state)
    base::StringToInt(state, &printer_info->printer_status);

  const char* drv_info = cupsGetOption(kDriverNameTagName,
                                       printer.num_options, printer.options);
  if (drv_info)
    printer_info->options[kDriverInfoTagName] = *drv_info;

  // Store printer options.
  for (int opt_index = 0; opt_index < printer.num_options; ++opt_index) {
    printer_info->options[printer.options[opt_index].name] =
        printer.options[opt_index].value;
  }
  return true;
}

}  // namespace

PrintBackendCUPS::PrintBackendCUPS(const GURL& print_server_url,
                                   http_encryption_t encryption,
                                   bool blocking)
    : print_server_url_(print_server_url),
      cups_encryption_(encryption),
      blocking_(blocking) {
}

bool PrintBackendCUPS::EnumeratePrinters(PrinterList* printer_list) {
  DCHECK(printer_list);
  printer_list->clear();

  cups_dest_t* destinations = nullptr;
  int num_dests = GetDests(&destinations);
  if (!num_dests && cupsLastError() > IPP_OK_EVENTS_COMPLETE) {
    VLOG(1) << "CUPS: Error getting printers from CUPS server"
            << ", server: " << print_server_url_
            << ", error: " << static_cast<int>(cupsLastError());
    return false;
  }

  for (int printer_index = 0; printer_index < num_dests; ++printer_index) {
    const cups_dest_t& printer = destinations[printer_index];

    PrinterBasicInfo printer_info;
    if (PrinterBasicInfoFromCUPS(printer, &printer_info))
      printer_list->push_back(printer_info);
  }

  cupsFreeDests(num_dests, destinations);

  VLOG(1) << "CUPS: Enumerated printers, server: " << print_server_url_
          << ", # of printers: " << printer_list->size();
  return true;
}

std::string PrintBackendCUPS::GetDefaultPrinterName() {
  // Not using cupsGetDefault() because it lies about the default printer.
  cups_dest_t* dests;
  int num_dests = GetDests(&dests);
  cups_dest_t* dest = cupsGetDest(nullptr, nullptr, num_dests, dests);
  std::string name = dest ? std::string(dest->name) : std::string();
  cupsFreeDests(num_dests, dests);
  return name;
}

bool PrintBackendCUPS::GetPrinterBasicInfo(const std::string& printer_name,
                                           PrinterBasicInfo* printer_info) {
  cups_dest_t* dest = GetNamedDest(printer_name);
  if (!dest)
    return false;

  DCHECK_EQ(printer_name, dest->name);
  bool ret = PrinterBasicInfoFromCUPS(*dest, printer_info);
  cupsFreeDests(1, dest);
  return ret;
}

bool PrintBackendCUPS::GetPrinterSemanticCapsAndDefaults(
    const std::string& printer_name,
    PrinterSemanticCapsAndDefaults* printer_info) {
  PrinterCapsAndDefaults info;
  if (!GetPrinterCapsAndDefaults(printer_name, &info) )
    return false;

  return ParsePpdCapabilities(
      printer_name, info.printer_capabilities, printer_info);
}

bool PrintBackendCUPS::GetPrinterCapsAndDefaults(
    const std::string& printer_name,
    PrinterCapsAndDefaults* printer_info) {
  DCHECK(printer_info);

  VLOG(1) << "CUPS: Getting caps and defaults, printer name: " << printer_name;

  base::FilePath ppd_path(GetPPD(printer_name.c_str()));
  // In some cases CUPS failed to get ppd file.
  if (ppd_path.empty()) {
    LOG(ERROR) << "CUPS: Failed to get PPD, printer name: " << printer_name;
    return false;
  }

  std::string content;
  bool res = base::ReadFileToString(ppd_path, &content);

  base::DeleteFile(ppd_path, false);

  if (res) {
    printer_info->printer_capabilities.swap(content);
    printer_info->caps_mime_type = "application/pagemaker";
    // In CUPS, printer defaults is a part of PPD file. Nothing to upload here.
    printer_info->printer_defaults.clear();
    printer_info->defaults_mime_type.clear();
  }

  return res;
}

std::string PrintBackendCUPS::GetPrinterDriverInfo(
    const std::string& printer_name) {
  std::string result;

  cups_dest_t* dest = GetNamedDest(printer_name);
  if (!dest)
    return result;

  DCHECK_EQ(printer_name, dest->name);
  const char* info =
      cupsGetOption(kDriverNameTagName, dest->num_options, dest->options);
  if (info)
    result = *info;
  cupsFreeDests(1, dest);
  return result;
}

bool PrintBackendCUPS::IsValidPrinter(const std::string& printer_name) {
  cups_dest_t* dest = GetNamedDest(printer_name);
  if (!dest)
    return false;

  cupsFreeDests(1, dest);
  return true;
}

#if !defined(OS_CHROMEOS)
scoped_refptr<PrintBackend> PrintBackend::CreateInstanceImpl(
    const base::DictionaryValue* print_backend_settings) {
  std::string print_server_url_str, cups_blocking;
  int encryption = HTTP_ENCRYPT_NEVER;
  if (print_backend_settings) {
    print_backend_settings->GetString(kCUPSPrintServerURL,
                                      &print_server_url_str);

    print_backend_settings->GetString(kCUPSBlocking,
                                      &cups_blocking);

    print_backend_settings->GetInteger(kCUPSEncryption, &encryption);
  }
  GURL print_server_url(print_server_url_str.c_str());
  return new PrintBackendCUPS(print_server_url,
                              static_cast<http_encryption_t>(encryption),
                              cups_blocking == kValueTrue);
}
#endif  // !defined(OS_CHROMEOS)

int PrintBackendCUPS::GetDests(cups_dest_t** dests) {
  if (print_server_url_.is_empty()) {  // Use default (local) print server.
    // GnuTLS has a genuine small memory leak that is easier to annotate
    // than suppress. See http://crbug.com/176888#c7
    // In theory any CUPS function can trigger this leak, but in
    // PrintBackendCUPS, this is the most likely spot.
    // TODO(eugenis): remove this once the leak is fixed.
    ANNOTATE_SCOPED_MEMORY_LEAK;
    return cupsGetDests(dests);
  }

  HttpConnectionCUPS http(print_server_url_, cups_encryption_);
  http.SetBlocking(blocking_);
  return cupsGetDests2(http.http(), dests);
}

base::FilePath PrintBackendCUPS::GetPPD(const char* name) {
  // cupsGetPPD returns a filename stored in a static buffer in CUPS.
  // Protect this code with lock.
  CR_DEFINE_STATIC_LOCAL(base::Lock, ppd_lock, ());
  base::AutoLock ppd_autolock(ppd_lock);
  base::FilePath ppd_path;
  const char* ppd_file_path = nullptr;
  if (print_server_url_.is_empty()) {  // Use default (local) print server.
    ppd_file_path = cupsGetPPD(name);
    if (ppd_file_path)
      ppd_path = base::FilePath(ppd_file_path);
  } else {
    // cupsGetPPD2 gets stuck sometimes in an infinite time due to network
    // configuration/issues. To prevent that, use non-blocking http connection
    // here.
    // Note: After looking at CUPS sources, it looks like non-blocking
    // connection will timeout after 10 seconds of no data period. And it will
    // return the same way as if data was completely and successfully
    // downloaded.
    HttpConnectionCUPS http(print_server_url_, cups_encryption_);
    http.SetBlocking(blocking_);
    ppd_file_path = cupsGetPPD2(http.http(), name);
    // Check if the get full PPD, since non-blocking call may simply return
    // normally after timeout expired.
    if (ppd_file_path) {
      // There is no reliable way right now to detect full and complete PPD
      // get downloaded. If we reach http timeout, it may simply return
      // downloaded part as a full response. It might be good enough to check
      // http->data_remaining or http->_data_remaining, unfortunately http_t
      // is an internal structure and fields are not exposed in CUPS headers.
      // httpGetLength or httpGetLength2 returning the full content size.
      // Comparing file size against that content length might be unreliable
      // since some http reponses are encoded and content_length > file size.
      // Let's just check for the obvious CUPS and http errors here.
      ppd_path = base::FilePath(ppd_file_path);
      ipp_status_t error_code = cupsLastError();
      int http_error = httpError(http.http());
      if (error_code > IPP_OK_EVENTS_COMPLETE || http_error != 0) {
        LOG(ERROR) << "Error downloading PPD file, name: " << name
                   << ", CUPS error: " << static_cast<int>(error_code)
                   << ", HTTP error: " << http_error;
        base::DeleteFile(ppd_path, false);
        ppd_path.clear();
      }
    }
  }
  return ppd_path;
}

cups_dest_t* PrintBackendCUPS::GetNamedDest(const std::string& printer_name) {
  // Use default (local) print server.
  if (print_server_url_.is_empty())
    return cupsGetNamedDest(CUPS_HTTP_DEFAULT, printer_name.c_str(), nullptr);

  HttpConnectionCUPS http(print_server_url_, cups_encryption_);
  http.SetBlocking(blocking_);
  return cupsGetNamedDest(http.http(), printer_name.c_str(), nullptr);
}

}  // namespace printing
