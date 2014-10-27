// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_THEMES_THEME_SERVICE_AURAX11_H_
#define CHROME_BROWSER_THEMES_THEME_SERVICE_AURAX11_H_

#include "chrome/browser/themes/theme_service.h"

// A subclass of ThemeService that manages the CustomThemeSupplier which
// provides the native X11 theme.
class ThemeServiceAuraX11 : public ThemeService {
 public:
  ThemeServiceAuraX11();
  virtual ~ThemeServiceAuraX11();

  // Overridden from ThemeService:
  virtual bool ShouldInitWithSystemTheme() const override;
  virtual void UseSystemTheme() override;
  virtual bool IsSystemThemeDistinctFromDefaultTheme() const override;
  virtual bool UsingDefaultTheme() const override;
  virtual bool UsingSystemTheme() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ThemeServiceAuraX11);
};

#endif  // CHROME_BROWSER_THEMES_THEME_SERVICE_AURAX11_H_
