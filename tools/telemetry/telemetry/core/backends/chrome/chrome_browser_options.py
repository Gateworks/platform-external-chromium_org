# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from telemetry.core import browser_options
from telemetry.core import platform


def CreateChromeBrowserOptions(br_options):
  browser_type = br_options.browser_type

  if (platform.GetHostPlatform().GetOSName() == 'chromeos' or
      (browser_type and browser_type.startswith('cros'))):
    return CrosBrowserOptions(br_options)

  return br_options


class ChromeBrowserOptions(browser_options.BrowserOptions):
  """Chrome-specific browser options."""

  def __init__(self, br_options):
    super(ChromeBrowserOptions, self).__init__()
    # Copy to self.
    self.__dict__.update(br_options.__dict__)


class CrosBrowserOptions(ChromeBrowserOptions):
  """ChromeOS-specific browser options."""

  def __init__(self, br_options):
    super(CrosBrowserOptions, self).__init__(br_options)
    # Create a browser with oobe property.
    self.create_browser_with_oobe = False
    # Clear enterprise policy before logging in.
    self.clear_enterprise_policy = True
    # Disable GAIA/enterprise services.
    self.disable_gaia_services = True
    # Disable default apps.
    self.disable_default_apps = True

    self.auto_login = True
    self.gaia_login = False
    self.username = 'test@test.test'
    self.password = ''
