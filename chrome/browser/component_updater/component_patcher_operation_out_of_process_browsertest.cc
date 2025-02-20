// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/base_paths.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/macros.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "chrome/browser/component_updater/component_patcher_operation_out_of_process.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/update_client/component_patcher_operation.h"
#include "content/public/browser/browser_thread.h"
#include "courgette/courgette.h"
#include "courgette/third_party/bsdiff/bsdiff.h"

class OutOfProcessPatchTest : public InProcessBrowserTest {
 public:
  OutOfProcessPatchTest() {
    EXPECT_TRUE(installed_dir_.CreateUniqueTempDir());
    EXPECT_TRUE(input_dir_.CreateUniqueTempDir());
    EXPECT_TRUE(unpack_dir_.CreateUniqueTempDir());
  }

  static base::FilePath TestFile(const char* name) {
    base::FilePath path;
    PathService::Get(base::DIR_SOURCE_ROOT, &path);
    return path.AppendASCII("components")
        .AppendASCII("test")
        .AppendASCII("data")
        .AppendASCII("update_client")
        .AppendASCII(name);
  }

  base::FilePath InputFilePath(const char* name) {
    base::FilePath path = installed_dir_.GetPath().AppendASCII(name);

    base::RunLoop run_loop;
    content::BrowserThread::PostBlockingPoolTaskAndReply(
        FROM_HERE,
        base::Bind(&OutOfProcessPatchTest::CopyFile, TestFile(name), path),
        run_loop.QuitClosure());

    run_loop.Run();
    return path;
  }

  base::FilePath PatchFilePath(const char* name) {
    base::FilePath path = input_dir_.GetPath().AppendASCII(name);

    base::RunLoop run_loop;
    content::BrowserThread::PostBlockingPoolTaskAndReply(
        FROM_HERE,
        base::Bind(&OutOfProcessPatchTest::CopyFile, TestFile(name), path),
        run_loop.QuitClosure());

    run_loop.Run();
    return path;
  }

  base::FilePath OutputFilePath(const char* name) {
    return unpack_dir_.GetPath().AppendASCII(name);
  }

  base::FilePath InvalidPath(const char* name) {
    return input_dir_.GetPath().AppendASCII("nonexistent").AppendASCII(name);
  }

  void RunPatchTest(const std::string& operation,
                    const base::FilePath& input,
                    const base::FilePath& patch,
                    const base::FilePath& output,
                    int expected_result) {
    base::RunLoop run_loop;
    quit_closure_ = run_loop.QuitClosure();
    done_called_ = false;

    content::BrowserThread::PostBlockingPoolSequencedTask(
        "OutOfProcessPatchTest::PatchOnBlockingPoolSequencedTaskRunner",
        FROM_HERE,
        base::Bind(
            &OutOfProcessPatchTest::PatchOnBlockingPoolSequencedTaskRunner,
            base::Unretained(this), operation, input, patch, output,
            expected_result));

    run_loop.Run();
    EXPECT_TRUE(done_called_);
  }

 private:
  void PatchOnBlockingPoolSequencedTaskRunner(const std::string& operation,
                                              const base::FilePath& input,
                                              const base::FilePath& patch,
                                              const base::FilePath& output,
                                              int expected_result) {
    scoped_refptr<base::SequencedTaskRunner> task_runner =
        base::SequencedTaskRunnerHandle::Get();

    scoped_refptr<update_client::OutOfProcessPatcher> patcher =
        make_scoped_refptr(new component_updater::ChromeOutOfProcessPatcher);

    patcher->Patch(operation, task_runner, input, patch, output,
                   base::Bind(&OutOfProcessPatchTest::PatchDone,
                              base::Unretained(this), expected_result));
  }

  void PatchDone(int expected, int result) {
    EXPECT_EQ(expected, result);
    done_called_ = true;
    content::BrowserThread::PostTask(content::BrowserThread::UI, FROM_HERE,
                                     quit_closure_);
  }

  static void CopyFile(const base::FilePath& source,
                       const base::FilePath& target) {
    EXPECT_TRUE(base::CopyFile(source, target));
  }

  base::ScopedTempDir installed_dir_;
  base::ScopedTempDir input_dir_;
  base::ScopedTempDir unpack_dir_;
  base::Closure quit_closure_;
  bool done_called_;

  DISALLOW_COPY_AND_ASSIGN(OutOfProcessPatchTest);
};

IN_PROC_BROWSER_TEST_F(OutOfProcessPatchTest, CheckBsdiffOperation) {
  constexpr int kExpectedResult = bsdiff::OK;

  base::FilePath input_file = InputFilePath("binary_input.bin");
  base::FilePath patch_file = PatchFilePath("binary_bsdiff_patch.bin");
  base::FilePath output_file = OutputFilePath("output.bin");

  RunPatchTest(update_client::kBsdiff, input_file, patch_file, output_file,
               kExpectedResult);

  EXPECT_TRUE(base::ContentsEqual(TestFile("binary_output.bin"), output_file));
}

IN_PROC_BROWSER_TEST_F(OutOfProcessPatchTest, CheckCourgetteOperation) {
  constexpr int kExpectedResult = courgette::C_OK;

  base::FilePath input_file = InputFilePath("binary_input.bin");
  base::FilePath patch_file = PatchFilePath("binary_courgette_patch.bin");
  base::FilePath output_file = OutputFilePath("output.bin");

  RunPatchTest(update_client::kCourgette, input_file, patch_file, output_file,
               kExpectedResult);

  EXPECT_TRUE(base::ContentsEqual(TestFile("binary_output.bin"), output_file));
}

IN_PROC_BROWSER_TEST_F(OutOfProcessPatchTest, InvalidInputFile) {
  constexpr int kInvalidInputFile = -1;

  base::FilePath invalid = InvalidPath("binary_input.bin");
  base::FilePath patch_file = PatchFilePath("binary_courgette_patch.bin");
  base::FilePath output_file = OutputFilePath("output.bin");

  RunPatchTest(update_client::kCourgette, invalid, patch_file, output_file,
               kInvalidInputFile);
}

IN_PROC_BROWSER_TEST_F(OutOfProcessPatchTest, InvalidPatchFile) {
  constexpr int kInvalidPatchFile = -1;

  base::FilePath input_file = InputFilePath("binary_input.bin");
  base::FilePath invalid = InvalidPath("binary_courgette_patch.bin");
  base::FilePath output_file = OutputFilePath("output.bin");

  RunPatchTest(update_client::kCourgette, input_file, invalid, output_file,
               kInvalidPatchFile);
}

IN_PROC_BROWSER_TEST_F(OutOfProcessPatchTest, InvalidOutputFile) {
  constexpr int kInvalidOutputFile = -1;

  base::FilePath input_file = InputFilePath("binary_input.bin");
  base::FilePath patch_file = PatchFilePath("binary_courgette_patch.bin");
  base::FilePath invalid = InvalidPath("output.bin");

  RunPatchTest(update_client::kCourgette, input_file, patch_file, invalid,
               kInvalidOutputFile);
}
