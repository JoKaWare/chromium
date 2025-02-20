// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/test/integration/printers_helper.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/chromeos/printing/printer_pref_manager.h"
#include "chrome/browser/chromeos/printing/printer_pref_manager_factory.h"
#include "chrome/browser/sync/test/integration/sync_datatype_helper.h"
#include "chrome/browser/sync/test/integration/sync_test.h"

using sync_datatype_helper::test;

namespace printers_helper {

namespace {

using PrinterList = std::vector<std::unique_ptr<chromeos::Printer>>;

// Returns true if Printer#id, Printer#description, and Printer#uri all match.
bool PrintersAreMostlyEqual(const chromeos::Printer& left,
                            const chromeos::Printer& right) {
  return left.id() == right.id() && left.description() == right.description() &&
         left.uri() == right.uri();
}

// Returns true if both lists have the same elements irrespective of order.
bool ListsContainTheSamePrinters(const PrinterList& list_a,
                                 const PrinterList& list_b) {
  std::unordered_multimap<std::string, const chromeos::Printer*> map_b;
  for (const auto& b : list_b) {
    map_b.insert({b->id(), b.get()});
  }

  for (const auto& a : list_a) {
    auto range = map_b.equal_range(a->id());

    auto it = std::find_if(
        range.first, range.second,
        [&a](const std::pair<std::string, const chromeos::Printer*>& entry)
            -> bool { return PrintersAreMostlyEqual(*a, *(entry.second)); });

    if (it == range.second) {
      // Element in a does not match an element in b. Lists do not contain the
      // same elements.
      return false;
    }

    map_b.erase(it);
  }

  return map_b.empty();
}

std::string PrinterId(int index) {
  return base::StringPrintf("printer%d", index);
}

}  // namespace

void AddPrinter(chromeos::PrinterPrefManager* manager,
                const chromeos::Printer& printer) {
  manager->RegisterPrinter(base::MakeUnique<chromeos::Printer>(printer));
}

void RemovePrinter(chromeos::PrinterPrefManager* manager, int index) {
  chromeos::Printer testPrinter(CreateTestPrinter(index));
  manager->RemovePrinter(testPrinter.id());
}

bool EditPrinterDescription(chromeos::PrinterPrefManager* manager,
                            int index,
                            const std::string& description) {
  PrinterList printers = manager->GetPrinters();
  std::string printer_id = PrinterId(index);
  auto found = std::find_if(
      printers.begin(), printers.end(),
      [&printer_id](const std::unique_ptr<chromeos::Printer>& printer) -> bool {
        return printer->id() == printer_id;
      });

  if (found == printers.end())
    return false;

  (*found)->set_description(description);
  manager->RegisterPrinter(std::move(*found));

  return true;
}

chromeos::Printer CreateTestPrinter(int index) {
  chromeos::Printer printer(PrinterId(index));
  printer.set_description("Description");
  printer.set_uri(base::StringPrintf("ipp://192.168.1.%d", index));

  return printer;
}

chromeos::PrinterPrefManager* GetVerifierPrinterStore() {
  chromeos::PrinterPrefManager* manager =
      chromeos::PrinterPrefManagerFactory::GetForBrowserContext(
          sync_datatype_helper::test()->verifier());
  // Must wait for ModelTypeStore initialization.
  base::RunLoop().RunUntilIdle();

  return manager;
}

chromeos::PrinterPrefManager* GetPrinterStore(int index) {
  chromeos::PrinterPrefManager* manager =
      chromeos::PrinterPrefManagerFactory::GetForBrowserContext(
          sync_datatype_helper::test()->GetProfile(index));
  // Must wait for ModelTypeStore initialization.
  base::RunLoop().RunUntilIdle();

  return manager;
}

int GetVerifierPrinterCount() {
  return GetVerifierPrinterStore()->GetPrinters().size();
}

int GetPrinterCount(int index) {
  return GetPrinterStore(index)->GetPrinters().size();
}

bool AllProfilesContainSamePrinters() {
  auto reference_printers = GetPrinterStore(0)->GetPrinters();
  for (int i = 1; i < test()->num_clients(); ++i) {
    auto printers = GetPrinterStore(i)->GetPrinters();
    if (!ListsContainTheSamePrinters(reference_printers, printers)) {
      VLOG(1) << "Printers in client [" << i << "] don't match client 0";
      return false;
    }
  }

  return true;
}

bool ProfileContainsSamePrintersAsVerifier(int index) {
  return ListsContainTheSamePrinters(GetVerifierPrinterStore()->GetPrinters(),
                                     GetPrinterStore(index)->GetPrinters());
}

PrintersMatchChecker::PrintersMatchChecker()
    : AwaitMatchStatusChangeChecker(
          base::Bind(&printers_helper::AllProfilesContainSamePrinters),
          "All printers match") {}

PrintersMatchChecker::~PrintersMatchChecker() {}

}  // namespace printers_helper
