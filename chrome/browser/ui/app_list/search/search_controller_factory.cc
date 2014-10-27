// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/search_controller_factory.h"

#include "base/command_line.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/app_list/search/app_search_provider.h"
#include "chrome/browser/ui/app_list/search/history_factory.h"
#include "chrome/browser/ui/app_list/search/omnibox_provider.h"
#include "chrome/browser/ui/app_list/search/people/people_provider.h"
#include "chrome/browser/ui/app_list/search/webstore/webstore_provider.h"
#include "chrome/common/chrome_switches.h"
#include "ui/app_list/search/mixer.h"
#include "ui/app_list/search_controller.h"

namespace app_list {

scoped_ptr<SearchController> CreateSearchController(
    Profile* profile,
    SearchBoxModel* search_box,
    AppListModel::SearchResults* results,
    AppListControllerDelegate* list_controller) {
  scoped_ptr<SearchController> controller(new SearchController(
      search_box, results, HistoryFactory::GetForBrowserContext(profile)));

  controller->AddProvider(Mixer::MAIN_GROUP,
                          scoped_ptr<SearchProvider>(
                              new AppSearchProvider(profile, list_controller)));
  controller->AddProvider(Mixer::OMNIBOX_GROUP,
                          scoped_ptr<SearchProvider>(
                              new OmniboxProvider(profile, list_controller)));
  controller->AddProvider(Mixer::WEBSTORE_GROUP,
                          scoped_ptr<SearchProvider>(
                              new WebstoreProvider(profile, list_controller)));
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
            switches::kDisablePeopleSearch)) {
    controller->AddProvider(Mixer::PEOPLE_GROUP,
                            scoped_ptr<SearchProvider>(
                                new PeopleProvider(profile, list_controller)));
  }

  return controller.Pass();
}

}  // namespace app_list
