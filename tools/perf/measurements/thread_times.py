# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
from measurements import timeline_controller
from metrics import timeline
from telemetry.core.platform import tracing_category_filter
from telemetry.page import page_test

class ThreadTimes(page_test.PageTest):
  def __init__(self):
    super(ThreadTimes, self).__init__('RunSmoothness')
    self._timeline_controller = None

  @classmethod
  def AddCommandLineArgs(cls, parser):
    parser.add_option('--report-silk-details', action='store_true',
                      help='Report details relevant to silk.')

  def WillNavigateToPage(self, page, tab):
    self._timeline_controller = timeline_controller.TimelineController()
    if self.options.report_silk_details:
      # We need the other traces in order to have any details to report.
      self._timeline_controller.trace_categories = None
    else:
      self._timeline_controller.trace_categories = \
          tracing_category_filter.CreateNoOverheadFilter().filter_string
    self._timeline_controller.SetUp(page, tab)

  def WillRunActions(self, page, tab):
    self._timeline_controller.Start(tab)

  def DidRunActions(self, page, tab):
    self._timeline_controller.Stop(tab)

  def ValidateAndMeasurePage(self, page, tab, results):
    metric = timeline.ThreadTimesTimelineMetric()
    renderer_thread = \
        self._timeline_controller.model.GetRendererThreadFromTabId(tab.id)
    if self.options.report_silk_details:
      metric.details_to_report = timeline.ReportSilkDetails
    metric.AddResults(self._timeline_controller.model, renderer_thread,
                      self._timeline_controller.smooth_records, results)

  def CleanUpAfterPage(self, _, tab):
    self._timeline_controller.CleanUp(tab)
