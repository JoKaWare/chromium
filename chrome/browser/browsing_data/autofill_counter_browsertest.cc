// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/browsing_data/core/counters/autofill_counter.h"

#include "base/guid.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/platform_thread.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/web_data_service_factory.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/autofill/core/browser/autofill_profile.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/credit_card.h"
#include "components/autofill/core/browser/webdata/autofill_webdata_service.h"
#include "components/browsing_data/core/browsing_data_utils.h"
#include "components/browsing_data/core/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browser_thread.h"

namespace {

class AutofillCounterTest : public InProcessBrowserTest {
 public:
  AutofillCounterTest() {}
  ~AutofillCounterTest() override {}

  void SetUpOnMainThread() override {
    web_data_service_ = WebDataServiceFactory::GetAutofillWebDataForProfile(
        browser()->profile(), ServiceAccessType::IMPLICIT_ACCESS);

    SetAutofillDeletionPref(true);
    SetDeletionPeriodPref(browsing_data::ALL_TIME);
  }

  // Autocomplete suggestions --------------------------------------------------

  void AddAutocompleteSuggestion(const std::string& name,
                                 const std::string& value) {
    autofill::FormFieldData field;
    field.name = base::ASCIIToUTF16(name);
    field.value = base::ASCIIToUTF16(value);

    std::vector<autofill::FormFieldData> form_fields;
    form_fields.push_back(field);
    web_data_service_->AddFormFields(form_fields);
  }

  void RemoveAutocompleteSuggestion(const std::string& name,
                                    const std::string& value) {
    web_data_service_->RemoveFormValueForElementName(
        base::ASCIIToUTF16(name),
        base::ASCIIToUTF16(value));
  }

  void ClearAutocompleteSuggestions() {
    web_data_service_->RemoveFormElementsAddedBetween(
        base::Time(), base::Time::Max());
  }

  // Credit cards --------------------------------------------------------------

  void AddCreditCard(const char* card_number,
                     const char* exp_month,
                     const char* exp_year) {
    autofill::CreditCard card;
    autofill::test::SetCreditCardInfo(&card, nullptr, card_number, exp_month,
                                      exp_year);
    credit_card_ids_.push_back(card.guid());
    web_data_service_->AddCreditCard(card);
  }

  void RemoveLastCreditCard() {
    web_data_service_->RemoveCreditCard(credit_card_ids_.back());
    credit_card_ids_.pop_back();
  }

  // Addresses -----------------------------------------------------------------

  void AddAddress(const std::string& name,
                  const std::string& surname,
                  const std::string& address) {
    autofill::AutofillProfile profile;
    std::string id = base::GenerateGUID();
    address_ids_.push_back(id);
    profile.set_guid(id);
    profile.SetInfo(autofill::AutofillType(autofill::NAME_FIRST),
                    base::ASCIIToUTF16(name), "en-US");
    profile.SetInfo(autofill::AutofillType(autofill::NAME_LAST),
                    base::ASCIIToUTF16(surname), "en-US");
    profile.SetInfo(autofill::AutofillType(autofill::ADDRESS_HOME_LINE1),
                    base::ASCIIToUTF16(address), "en-US");
    web_data_service_->AddAutofillProfile(profile);
  }

  void RemoveLastAddress() {
    web_data_service_->RemoveAutofillProfile(address_ids_.back());
    address_ids_.pop_back();
  }

  // Other autofill utils ------------------------------------------------------

  void ClearCreditCardsAndAddresses() {
    web_data_service_->RemoveAutofillDataModifiedBetween(
        base::Time(), base::Time::Max());
  }

  void CallbackFromDBThread() {
    content::BrowserThread::PostTask(
        content::BrowserThread::UI, FROM_HERE,
        base::Bind(&base::RunLoop::Quit,
                   base::Unretained(run_loop_.get())));
  }

  void WaitForDBThread() {
    run_loop_.reset(new base::RunLoop());

    content::BrowserThread::PostTask(
        content::BrowserThread::DB, FROM_HERE,
        base::Bind(&AutofillCounterTest::CallbackFromDBThread,
                   base::Unretained(this)));

    run_loop_->Run();
  }

  // Other utils ---------------------------------------------------------------

  void SetAutofillDeletionPref(bool value) {
    browser()->profile()->GetPrefs()->SetBoolean(
        browsing_data::prefs::kDeleteFormData, value);
  }

  void SetDeletionPeriodPref(browsing_data::TimePeriod period) {
    browser()->profile()->GetPrefs()->SetInteger(
        browsing_data::prefs::kDeleteTimePeriod, static_cast<int>(period));
  }

  scoped_refptr<autofill::AutofillWebDataService> GetWebDataService() {
    return web_data_service_;
  }

  // Callback and result retrieval ---------------------------------------------

  void WaitForCounting() {
    run_loop_.reset(new base::RunLoop());
    run_loop_->Run();
  }

  browsing_data::BrowsingDataCounter::ResultInt GetNumSuggestions() {
    DCHECK(finished_);
    return num_suggestions_;
  }

  browsing_data::BrowsingDataCounter::ResultInt GetNumCreditCards() {
    DCHECK(finished_);
    return num_credit_cards_;
  }

  browsing_data::BrowsingDataCounter::ResultInt GetNumAddresses() {
    DCHECK(finished_);
    return num_addresses_;
  }

  void Callback(
      std::unique_ptr<browsing_data::BrowsingDataCounter::Result> result) {
    finished_ = result->Finished();

    if (finished_) {
      browsing_data::AutofillCounter::AutofillResult* autofill_result =
          static_cast<browsing_data::AutofillCounter::AutofillResult*>(
              result.get());

      num_suggestions_ = autofill_result->Value();
      num_credit_cards_ = autofill_result->num_credit_cards();
      num_addresses_ = autofill_result->num_addresses();
    }

    if (run_loop_ && finished_)
      run_loop_->Quit();
  }

 private:
  std::unique_ptr<base::RunLoop> run_loop_;

  std::vector<std::string> credit_card_ids_;
  std::vector<std::string> address_ids_;

  scoped_refptr<autofill::AutofillWebDataService> web_data_service_;

  bool finished_;
  browsing_data::BrowsingDataCounter::ResultInt num_suggestions_;
  browsing_data::BrowsingDataCounter::ResultInt num_credit_cards_;
  browsing_data::BrowsingDataCounter::ResultInt num_addresses_;

  DISALLOW_COPY_AND_ASSIGN(AutofillCounterTest);
};

// Tests that we count the correct number of autocomplete suggestions.
IN_PROC_BROWSER_TEST_F(AutofillCounterTest, AutocompleteSuggestions) {
  Profile* profile = browser()->profile();
  browsing_data::AutofillCounter counter(GetWebDataService());
  counter.Init(profile->GetPrefs(), base::Bind(&AutofillCounterTest::Callback,
                                               base::Unretained(this)));
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(0, GetNumSuggestions());

  AddAutocompleteSuggestion("email", "example@example.com");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(1, GetNumSuggestions());

  AddAutocompleteSuggestion("tel", "+123456789");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(2, GetNumSuggestions());

  AddAutocompleteSuggestion("tel", "+987654321");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(3, GetNumSuggestions());

  RemoveAutocompleteSuggestion("email", "example@example.com");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(2, GetNumSuggestions());

  ClearAutocompleteSuggestions();
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(0, GetNumSuggestions());
}

// Tests that we count the correct number of credit cards.
IN_PROC_BROWSER_TEST_F(AutofillCounterTest, CreditCards) {
  Profile* profile = browser()->profile();
  browsing_data::AutofillCounter counter(GetWebDataService());
  counter.Init(profile->GetPrefs(), base::Bind(&AutofillCounterTest::Callback,
                                               base::Unretained(this)));
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(0, GetNumCreditCards());

  AddCreditCard("0000-0000-0000-0000", "1", "2015");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(1, GetNumCreditCards());

  AddCreditCard("0123-4567-8910-1112", "10", "2015");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(2, GetNumCreditCards());

  AddCreditCard("1211-1098-7654-3210", "10", "2030");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(3, GetNumCreditCards());

  RemoveLastCreditCard();
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(2, GetNumCreditCards());

  ClearCreditCardsAndAddresses();
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(0, GetNumCreditCards());
}

// Tests that we count the correct number of addresses.
IN_PROC_BROWSER_TEST_F(AutofillCounterTest, Addresses) {
  Profile* profile = browser()->profile();
  browsing_data::AutofillCounter counter(GetWebDataService());
  counter.Init(profile->GetPrefs(), base::Bind(&AutofillCounterTest::Callback,
                                               base::Unretained(this)));
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(0, GetNumAddresses());

  AddAddress("John", "Doe", "Main Street 12345");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(1, GetNumAddresses());

  AddAddress("Jane", "Smith", "Main Street 12346");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(2, GetNumAddresses());

  AddAddress("John", "Smith", "Side Street 47");
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(3, GetNumAddresses());

  RemoveLastAddress();
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(2, GetNumAddresses());

  ClearCreditCardsAndAddresses();
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(0, GetNumAddresses());
}

// Tests that we return the correct complex result when counting more than
// one type of items.
IN_PROC_BROWSER_TEST_F(AutofillCounterTest, ComplexResult) {
  AddAutocompleteSuggestion("email", "example@example.com");
  AddAutocompleteSuggestion("zip", "12345");
  AddAutocompleteSuggestion("tel", "+123456789");
  AddAutocompleteSuggestion("tel", "+987654321");
  AddAutocompleteSuggestion("city", "Munich");

  AddCreditCard("0000-0000-0000-0000", "1", "2015");
  AddCreditCard("1211-1098-7654-3210", "10", "2030");

  AddAddress("John", "Doe", "Main Street 12345");
  AddAddress("Jane", "Smith", "Main Street 12346");
  AddAddress("John", "Smith", "Side Street 47");

  Profile* profile = browser()->profile();
  browsing_data::AutofillCounter counter(GetWebDataService());
  counter.Init(profile->GetPrefs(), base::Bind(&AutofillCounterTest::Callback,
                                               base::Unretained(this)));
  counter.Restart();
  WaitForCounting();
  EXPECT_EQ(5, GetNumSuggestions());
  EXPECT_EQ(2, GetNumCreditCards());
  EXPECT_EQ(3, GetNumAddresses());
}

// Tests that the counting respects time ranges.
IN_PROC_BROWSER_TEST_F(AutofillCounterTest, TimeRanges) {
  // This test makes time comparisons that are precise to a microsecond, but the
  // database uses the time_t format which is only precise to a second.
  // Make sure we use timestamps rounded to a second.
  base::Time time1 = base::Time::FromTimeT(base::Time::Now().ToTimeT());

  AddAutocompleteSuggestion("email", "example@example.com");
  AddCreditCard("0000-0000-0000-0000", "1", "2015");
  AddAddress("John", "Doe", "Main Street 12345");
  WaitForDBThread();

  // Skip at least a second has passed and add another batch.
  base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(1));
  base::Time time2 = base::Time::FromTimeT(base::Time::Now().ToTimeT());

  AddCreditCard("0123-4567-8910-1112", "10", "2015");
  AddAddress("Jane", "Smith", "Main Street 12346");
  AddAddress("John", "Smith", "Side Street 47");
  WaitForDBThread();

  // Skip at least a second has passed and add another batch.
  base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(1));
  base::Time time3 = base::Time::FromTimeT(base::Time::Now().ToTimeT());

  AddAutocompleteSuggestion("tel", "+987654321");
  AddCreditCard("1211-1098-7654-3210", "10", "2030");
  WaitForDBThread();

  // Test the results for different starting points.
  struct TestCase {
    const base::Time period_start;
    const browsing_data::BrowsingDataCounter::ResultInt
        expected_num_suggestions;
    const browsing_data::BrowsingDataCounter::ResultInt
        expected_num_credit_cards;
    const browsing_data::BrowsingDataCounter::ResultInt expected_num_addresses;
  } test_cases[] = {
    { base::Time(), 2, 3, 3},
    { time1,        2, 3, 3},
    { time2,        1, 2, 2},
    { time3,        1, 1, 0}
  };

  Profile* profile = browser()->profile();
  browsing_data::AutofillCounter counter(GetWebDataService());
  counter.Init(profile->GetPrefs(), base::Bind(&AutofillCounterTest::Callback,
                                               base::Unretained(this)));

  for (const TestCase& test_case : test_cases) {
    counter.SetPeriodStartForTesting(test_case.period_start);
    counter.Restart();
    WaitForCounting();
    EXPECT_EQ(test_case.expected_num_suggestions, GetNumSuggestions());
    EXPECT_EQ(test_case.expected_num_credit_cards, GetNumCreditCards());
    EXPECT_EQ(test_case.expected_num_addresses, GetNumAddresses());
  }
}

}  // namespace
