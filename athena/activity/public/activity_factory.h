// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATHENA_ACTIVITY_PUBLIC_ACTIVITY_FACTORY_H_
#define ATHENA_ACTIVITY_PUBLIC_ACTIVITY_FACTORY_H_

#include <string>

#include "athena/athena_export.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
class WebContents;
}

namespace views {
class WebView;
}

namespace athena {
class Activity;

class ATHENA_EXPORT ActivityFactory {
 public:
  // Registers the singleton factory.
  static void RegisterActivityFactory(ActivityFactory* factory);

  // Gets the registered singleton factory.
  static ActivityFactory* Get();

  // Shutdowns the factory.
  static void Shutdown();

  virtual ~ActivityFactory() {}

  // Create an activity of a web page. If |title| is empty, the title will be
  // obtained from the web contents.
  virtual Activity* CreateWebActivity(content::BrowserContext* browser_context,
                                      const base::string16& title,
                                      const GURL& url) = 0;

  // Create an activity with |contents|. The title is obtained from the web
  // contents.
  virtual Activity* CreateWebActivity(content::WebContents* contents) = 0;

  // Create an activity of an app with |app_id| and
  // |web_view| that will host the content.
  virtual Activity* CreateAppActivity(const std::string& app_id,
                                      views::WebView* web_view) = 0;
};

}  // namespace athena

#endif  // ATHENA_ACTIVITY_PUBLIC_ACTIVITY_FACTORY_H_
