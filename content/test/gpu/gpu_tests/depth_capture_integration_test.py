# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

from gpu_tests import gpu_integration_test
from gpu_tests import depth_capture_expectations
from gpu_tests import path_util

data_path = os.path.join(
    path_util.GetChromiumSrcDir(), 'content', 'test', 'data', 'media')

wait_timeout = 60  # seconds

harness_script = r"""
  var domAutomationController = {};

  domAutomationController._succeeded = false;
  domAutomationController._finished = false;
  domAutomationController._error_msg = "";

  domAutomationController.setAutomationId = function(id) {}

  domAutomationController.send = function(msg) {
    if (msg == "OK") {
      if (!domAutomationController._finished) {
        domAutomationController._succeeded = true;
      }
      domAutomationController._finished = true;
    } else {
      domAutomationController._succeeded = false;
      domAutomationController._finished = true;
      domAutomationController._error_msg = msg;
    }
  }

  domAutomationController.reset = function() {
    domAutomationController._succeeded = false;
    domAutomationController._finished = false;
  }

  window.domAutomationController = domAutomationController;
  console.log("Harness injected.");
"""

class DepthCaptureIntegrationTest(gpu_integration_test.GpuIntegrationTest):

  @classmethod
  def Name(cls):
    return 'depth_capture'

  @classmethod
  def CustomizeOptions(cls):
    options = cls._finder_options.browser_options
    options.AppendExtraBrowserArgs('--disable-domain-blocking-for-3d-apis')
    options.AppendExtraBrowserArgs('--enable-es3-apis')
    options.AppendExtraBrowserArgs('--use-fake-ui-for-media-stream')
    options.AppendExtraBrowserArgs(
        '--use-fake-device-for-media-stream=device-count=2')
    # Required for about:gpucrash handling from Telemetry.
    options.AppendExtraBrowserArgs('--enable-gpu-benchmarking')

  @classmethod
  def GenerateGpuTests(cls, options):
    tests = (('DepthCapture_depthStreamToRGBAUint8Texture',
              'getusermedia-depth-capture.html?query=RGBAUint8'),
             ('DepthCapture_depthStreamToRGBAFloatTexture',
              'getusermedia-depth-capture.html?query=RGBAFloat'),
             ('DepthCapture_depthStreamToR32FloatTexture',
              'getusermedia-depth-capture.html?query=R32Float'))
    for t in tests:
      yield (t[0], t[1], ('_' + t[0]))

  def RunActualGpuTest(self, test_path, *args):
    url = self.UrlOfStaticFilePath(test_path)
    tab = self.tab
    tab.Navigate(url, script_to_evaluate_on_commit=harness_script)
    tab.action_runner.WaitForJavaScriptCondition2(
      'domAutomationController._finished', timeout=60)
    if not tab.EvaluateJavaScript2('domAutomationController._succeeded'):
      self.fail('page indicated test failure:' +
                tab.EvaluateJavaScript2('domAutomationController._error_msg'))

  @classmethod
  def _CreateExpectations(cls):
    return depth_capture_expectations.DepthCaptureExpectations()

  @classmethod
  def setUpClass(cls):
    super(cls, DepthCaptureIntegrationTest).setUpClass()
    cls.CustomizeOptions()
    cls.SetBrowserOptions(cls._finder_options)
    cls.StartBrowser()
    cls.SetStaticServerDirs([data_path])

def load_tests(loader, tests, pattern):
  del loader, tests, pattern  # Unused.
  return gpu_integration_test.LoadAllTestsInModule(sys.modules[__name__])
