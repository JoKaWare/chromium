# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os
import time
import utils

from telemetry import page
from telemetry import story
from telemetry.core import exceptions


class CastPage(page.Page):
  """Abstract Cast page for Media Router Telemetry tests."""

  def ChooseSink(self, tab, sink_name):
    """Chooses a specific sink in the list."""

    tab.ExecuteJavaScript2("""
        var sinks = window.document.getElementById("media-router-container").
            shadowRoot.getElementById("sink-list").getElementsByTagName("span");
        for (var i=0; i<sinks.length; i++) {
          if(sinks[i].textContent.trim() == {{ sink_name }}) {
            sinks[i].click();
            break;
        }}
        """,
        sink_name=sink_name)

  def CloseDialog(self, tab):
    """Closes media router dialog."""

    try:
      tab.ExecuteJavaScript2(
          'window.document.getElementById("media-router-container").' +
          'shadowRoot.getElementById("container-header").shadowRoot.' +
          'getElementById("close-button").click();')
    except exceptions.DevtoolsTargetCrashException:
      # Ignore the crash exception, this exception is caused by the js
      # code which closes the dialog, it is expected.
      pass

  def CloseExistingRoute(self, action_runner, sink_name):
    """Closes the existing route if it exists, otherwise does nothing."""

    action_runner.TapElement(selector='#start_session_button')
    action_runner.Wait(5)
    for tab in action_runner.tab.browser.tabs:
      if tab.url == 'chrome://media-router/':
        if self.CheckIfExistingRoute(tab, sink_name):
          self.ChooseSink(tab, sink_name)
          tab.ExecuteJavaScript2(
              "window.document.getElementById('media-router-container')."
              "shadowRoot.getElementById('route-details').shadowRoot."
              "getElementById('close-route-button').click();")
    self.CloseDialog(tab)
    # Wait for 5s to make sure the route is closed.
    action_runner.Wait(5)

  def CheckIfExistingRoute(self, tab, sink_name):
    """"Checks if there is existing route for the specific sink."""

    tab.ExecuteJavaScript2("""
        var sinks = window.document.getElementById('media-router-container').
          allSinks;
        var sink_id = null;
        for (var i=0; i<sinks.length; i++) {
          if (sinks[i].name == {{ sink_name }}) {
            console.info('sink id: ' + sinks[i].id);
            sink_id = sinks[i].id;
            break;
          }
        }
        var routes = window.document.getElementById('media-router-container').
          routeList;
        for (var i=0; i<routes.length; i++) {
          if (!!sink_id && routes[i].sinkId == sink_id) {
            window.__telemetry_route_id = routes[i].id;
            break;
          }
        }""",
        sink_name=sink_name)
    route = tab.EvaluateJavaScript2('!!window.__telemetry_route_id')
    logging.info('Is there existing route? ' + str(route))
    return route

  def ExecuteAsyncJavaScript(self, action_runner, script, verify_func,
                             error_message, timeout=5):
    """Executes async javascript function and waits until it finishes."""

    action_runner.ExecuteJavaScript2(script)
    self._WaitForResult(action_runner, verify_func, error_message,
                        timeout=timeout)

  def WaitUntilDialogLoaded(self, action_runner, tab):
    """Waits until dialog is fully loaded."""

    self._WaitForResult(
        action_runner,
        lambda: tab.EvaluateJavaScript2(
             '!!window.document.getElementById('
             '"media-router-container") &&'
             'window.document.getElementById('
             '"media-router-container").sinksToShow_ &&'
             'window.document.getElementById('
             '"media-router-container").sinksToShow_.length'),
        'The dialog is not fully loaded within 15s.',
         timeout=15)

  def _WaitForResult(self, action_runner, verify_func, error_message,
                     timeout=5):
    """Waits until the function finishes or timeout."""

    start_time = time.time()
    while (not verify_func() and
           time.time() - start_time < timeout):
      action_runner.Wait(1)
    if not verify_func():
      raise page.page_test.Failure(error_message)

  def _GetDeviceName(self):
    """Gets device name from environment variable RECEIVER_NAME."""

    if 'RECEIVER_IP' not in os.environ or not os.environ.get('RECEIVER_IP'):
      raise page.page_test.Failure(
          'Your test machine is not set up correctly, '
          'RECEIVER_IP enviroment variable is missing.')
    return utils.GetDeviceName(os.environ.get('RECEIVER_IP'))
