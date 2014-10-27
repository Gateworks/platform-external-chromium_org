# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from benchmarks import silk_flags
from measurements import thread_times
import page_sets
from telemetry import benchmark


@benchmark.Enabled('android')
class ThreadTimesKeySilkCases(benchmark.Benchmark):
  """Measures timeline metrics while performing smoothness action on key silk
  cases."""
  test = thread_times.ThreadTimes
  page_set = page_sets.KeySilkCasesPageSet


@benchmark.Disabled
class LegacySilkBenchmark(ThreadTimesKeySilkCases):
  """Same as thread_times.key_silk_cases but with the old name."""
  @classmethod
  def GetName(cls):
    return "silk.key_silk_cases"


@benchmark.Enabled('android')
class ThreadTimesFastPathMobileSites(benchmark.Benchmark):
  """Measures timeline metrics while performing smoothness action on
  key mobile sites labeled with fast-path tag.
  http://www.chromium.org/developers/design-documents/rendering-benchmarks"""
  test = thread_times.ThreadTimes
  page_set = page_sets.KeyMobileSitesPageSet
  options = {'page_label_filter' : 'fastpath'}


@benchmark.Enabled('android')
class ThreadTimesSimpleMobileSites(benchmark.Benchmark):
  """Measures timeline metric using smoothness action on simple mobile sites
  http://www.chromium.org/developers/design-documents/rendering-benchmarks"""
  test = thread_times.ThreadTimes
  page_set = page_sets.SimpleMobileSitesPageSet


class ThreadTimesCompositorCases(benchmark.Benchmark):
  """Measures timeline metrics while performing smoothness action on
  tough compositor cases, using software rasterization.

  http://www.chromium.org/developers/design-documents/rendering-benchmarks"""
  test = thread_times.ThreadTimes
  page_set = page_sets.ToughCompositorCasesPageSet
  def CustomizeBrowserOptions(self, options):
    silk_flags.CustomizeBrowserOptionsForSoftwareRasterization(options)

@benchmark.Enabled('android')
class ThreadTimesPolymer(benchmark.Benchmark):
  """Measures timeline metrics while performing smoothness action on
  Polymer cases."""
  test = thread_times.ThreadTimes
  page_set = page_sets.PolymerPageSet
