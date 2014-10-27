// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_HISTORY_CORE_BROWSER_HISTORY_BACKEND_OBSERVER_H_
#define COMPONENTS_HISTORY_CORE_BROWSER_HISTORY_BACKEND_OBSERVER_H_

#include "base/macros.h"
#include "components/history/core/browser/history_types.h"

namespace history {

class HistoryBackend;

class HistoryBackendObserver {
 public:
  HistoryBackendObserver() {}
  virtual ~HistoryBackendObserver() {}

  // Called when user visits an URL.
  //
  // The |row| ID will be set to the value that is currently in effect in the
  // main history database. |redirects| is the list of redirects leading up to
  // the URL. If we have a redirect chain A -> B -> C and user is visiting C,
  // then |redirects[0]=B| and |redirects[1]=A|. If there are no redirects,
  // |redirects| is an empty vector.
  virtual void OnURLVisited(HistoryBackend* history_backend,
                            ui::PageTransition transition,
                            const URLRow& row,
                            const RedirectList& redirects,
                            base::Time visit_time) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(HistoryBackendObserver);
};

}  // namespace history

#endif  // COMPONENTS_HISTORY_CORE_BROWSER_HISTORY_BACKEND_OBSERVER_H_
