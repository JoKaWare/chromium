// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/net/client_cert_store_chromeos.h"

#include <string>

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/test/scoped_task_scheduler.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/chromeos/certificate_provider/certificate_provider.h"
#include "crypto/scoped_test_nss_db.h"
#include "net/cert/x509_certificate.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/test/cert_test_util.h"
#include "net/test/test_data_directory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {

namespace {

// "CN=B CA" - DER encoded DN of the issuer of client_1.pem
const unsigned char kAuthority1DN[] = {0x30, 0x0f, 0x31, 0x0d, 0x30, 0x0b,
                                       0x06, 0x03, 0x55, 0x04, 0x03, 0x0c,
                                       0x04, 0x42, 0x20, 0x43, 0x41};

class TestCertFilter : public ClientCertStoreChromeOS::CertFilter {
 public:
  explicit TestCertFilter(bool init_finished) : init_finished_(init_finished) {}

  ~TestCertFilter() override {}

  // ClientCertStoreChromeOS::CertFilter:
  bool Init(const base::Closure& callback) override {
    init_called_ = true;
    if (init_finished_)
      return true;
    pending_callback_ = callback;
    return false;
  }

  bool IsCertAllowed(
      const scoped_refptr<net::X509Certificate>& cert) const override {
    if (not_allowed_cert_.get() && cert->Equals(not_allowed_cert_.get()))
      return false;
    return true;
  }

  bool init_called() { return init_called_; }

  void FinishInit() {
    init_finished_ = true;
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, pending_callback_);
    pending_callback_.Reset();
  }

  void SetNotAllowedCert(scoped_refptr<net::X509Certificate> cert) {
    not_allowed_cert_ = cert;
  }

 private:
  bool init_finished_;
  bool init_called_ = false;
  base::Closure pending_callback_;
  scoped_refptr<net::X509Certificate> not_allowed_cert_;
};

}  // namespace

class ClientCertStoreChromeOSTest : public ::testing::Test {
 public:
  ClientCertStoreChromeOSTest() {}

  scoped_refptr<net::X509Certificate> ImportCertToSlot(
      const std::string& cert_filename,
      const std::string& key_filename,
      PK11SlotInfo* slot) {
    return net::ImportClientCertAndKeyFromFile(
        net::GetTestCertsDirectory(), cert_filename, key_filename, slot);
  }

 private:
  base::test::ScopedTaskScheduler scoped_task_scheduler_;
};

// Ensure that cert requests, that are started before the filter is initialized,
// will wait for the initialization and succeed afterwards.
TEST_F(ClientCertStoreChromeOSTest, RequestWaitsForNSSInitAndSucceeds) {
  crypto::ScopedTestNSSDB test_db;
  ASSERT_TRUE(test_db.is_open());

  TestCertFilter* cert_filter =
      new TestCertFilter(false /* init asynchronously */);
  ClientCertStoreChromeOS store(
      nullptr /* no additional provider */, base::WrapUnique(cert_filter),
      ClientCertStoreChromeOS::PasswordDelegateFactory());

  scoped_refptr<net::X509Certificate> cert_1(
      ImportCertToSlot("client_1.pem", "client_1.pk8", test_db.slot()));
  ASSERT_TRUE(cert_1.get());

  // Request any client certificate, which is expected to match client_1.
  scoped_refptr<net::SSLCertRequestInfo> request_all(
      new net::SSLCertRequestInfo());

  base::RunLoop run_loop;
  store.GetClientCerts(*request_all, &request_all->client_certs,
                       run_loop.QuitClosure());

  {
    base::RunLoop run_loop_inner;
    run_loop_inner.RunUntilIdle();
    // GetClientCerts should wait for the initialization of the filter to
    // finish.
    ASSERT_EQ(0u, request_all->client_certs.size());
    EXPECT_TRUE(cert_filter->init_called());
  }
  cert_filter->FinishInit();
  run_loop.Run();

  ASSERT_EQ(1u, request_all->client_certs.size());
}

// Ensure that cert requests, that are started after the filter was initialized,
// will succeed.
TEST_F(ClientCertStoreChromeOSTest, RequestsAfterNSSInitSucceed) {
  crypto::ScopedTestNSSDB test_db;
  ASSERT_TRUE(test_db.is_open());

  ClientCertStoreChromeOS store(
      nullptr,  // no additional provider
      base::WrapUnique(new TestCertFilter(true /* init synchronously */)),
      ClientCertStoreChromeOS::PasswordDelegateFactory());

  scoped_refptr<net::X509Certificate> cert_1(
      ImportCertToSlot("client_1.pem", "client_1.pk8", test_db.slot()));
  ASSERT_TRUE(cert_1.get());

  scoped_refptr<net::SSLCertRequestInfo> request_all(
      new net::SSLCertRequestInfo());

  base::RunLoop run_loop;
  store.GetClientCerts(*request_all, &request_all->client_certs,
                       run_loop.QuitClosure());
  run_loop.Run();

  ASSERT_EQ(1u, request_all->client_certs.size());
}

TEST_F(ClientCertStoreChromeOSTest, Filter) {
  crypto::ScopedTestNSSDB test_db;
  ASSERT_TRUE(test_db.is_open());

  TestCertFilter* cert_filter =
      new TestCertFilter(true /* init synchronously */);
  ClientCertStoreChromeOS store(
      nullptr /* no additional provider */, base::WrapUnique(cert_filter),
      ClientCertStoreChromeOS::PasswordDelegateFactory());

  scoped_refptr<net::X509Certificate> cert_1(
      ImportCertToSlot("client_1.pem", "client_1.pk8", test_db.slot()));
  ASSERT_TRUE(cert_1.get());
  scoped_refptr<net::X509Certificate> cert_2(
      ImportCertToSlot("client_2.pem", "client_2.pk8", test_db.slot()));
  ASSERT_TRUE(cert_2.get());

  scoped_refptr<net::SSLCertRequestInfo> request_all(
      new net::SSLCertRequestInfo());

  {
    base::RunLoop run_loop;
    cert_filter->SetNotAllowedCert(cert_2);
    net::CertificateList selected_certs;
    store.GetClientCerts(*request_all, &selected_certs, run_loop.QuitClosure());
    run_loop.Run();

    ASSERT_EQ(1u, selected_certs.size());
    EXPECT_TRUE(cert_1->Equals(selected_certs[0].get()));
  }

  {
    base::RunLoop run_loop;
    cert_filter->SetNotAllowedCert(cert_1);
    net::CertificateList selected_certs;
    store.GetClientCerts(*request_all, &selected_certs, run_loop.QuitClosure());
    run_loop.Run();

    ASSERT_EQ(1u, selected_certs.size());
    EXPECT_TRUE(cert_2->Equals(selected_certs[0].get()));
  }
}

// Ensure that the delegation of the request matching to the base class is
// functional.
TEST_F(ClientCertStoreChromeOSTest, CertRequestMatching) {
  crypto::ScopedTestNSSDB test_db;
  ASSERT_TRUE(test_db.is_open());

  TestCertFilter* cert_filter =
      new TestCertFilter(true /* init synchronously */);
  ClientCertStoreChromeOS store(
      nullptr,  // no additional provider
      base::WrapUnique(cert_filter),
      ClientCertStoreChromeOS::PasswordDelegateFactory());

  scoped_refptr<net::X509Certificate> cert_1(
      ImportCertToSlot("client_1.pem", "client_1.pk8", test_db.slot()));
  ASSERT_TRUE(cert_1.get());
  scoped_refptr<net::X509Certificate> cert_2(
      ImportCertToSlot("client_2.pem", "client_2.pk8", test_db.slot()));
  ASSERT_TRUE(cert_2.get());

  std::vector<std::string> authority_1(
      1, std::string(reinterpret_cast<const char*>(kAuthority1DN),
                     sizeof(kAuthority1DN)));
  scoped_refptr<net::SSLCertRequestInfo> request(new net::SSLCertRequestInfo());
  request->cert_authorities = authority_1;

  base::RunLoop run_loop;
  net::CertificateList selected_certs;
  store.GetClientCerts(*request, &selected_certs, run_loop.QuitClosure());
  run_loop.Run();

  ASSERT_EQ(1u, selected_certs.size());
  EXPECT_TRUE(cert_1->Equals(selected_certs[0].get()));
}

}  // namespace chromeos
