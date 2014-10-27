# Copyright 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from telemetry.core import possible_app


class PossibleBrowser(possible_app.PossibleApp):
  """A browser that can be controlled.

  Call Create() to launch the browser and begin manipulating it..
  """

  def __init__(self, browser_type, target_os, finder_options,
               supports_tab_control):
    super(PossibleBrowser, self).__init__(app_type=browser_type,
                                          target_os=target_os,
                                          finder_options=finder_options)
    self._supports_tab_control = supports_tab_control
    self._archive_path = None
    self._append_to_existing_wpr = False
    self._make_javascript_deterministic = True
    self._credentials_path = None

  def __repr__(self):
    return 'PossibleBrowser(app_type=%s)' % self.app_type

  @property
  def browser_type(self):
    return self.app_type

  @property
  def supports_tab_control(self):
    return self._supports_tab_control

  def _InitPlatformIfNeeded(self):
    raise NotImplementedError()

  def Create(self):
    raise NotImplementedError()

  def SupportsOptions(self, finder_options):
    """Tests for extension support."""
    raise NotImplementedError()

  def IsRemote(self):
    return False

  def RunRemote(self):
    pass

  def UpdateExecutableIfNeeded(self):
    pass

  def last_modification_time(self):
    return -1

  def SetReplayArchivePath(self, archive_path, append_to_existing_wpr,
                           make_javascript_deterministic):
    self._archive_path = archive_path
    self._append_to_existing_wpr = append_to_existing_wpr
    self._make_javascript_deterministic = make_javascript_deterministic

  def SetCredentialsPath(self, credentials_path):
    self._credentials_path = credentials_path
