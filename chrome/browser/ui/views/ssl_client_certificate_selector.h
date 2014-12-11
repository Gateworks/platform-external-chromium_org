// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SSL_CLIENT_CERTIFICATE_SELECTOR_H_
#define CHROME_BROWSER_UI_VIEWS_SSL_CLIENT_CERTIFICATE_SELECTOR_H_

#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/strings/string16.h"
#include "chrome/browser/ssl/ssl_client_auth_observer.h"
#include "chrome/browser/ssl/ssl_client_certificate_selector.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/table/table_view_observer.h"
#include "ui/views/window/dialog_delegate.h"

// This header file exists only for testing.  Chrome should access the
// certificate selector only through the cross-platform interface
// chrome/browser/ssl_client_certificate_selector.h.

namespace net {
class SSLCertRequestInfo;
class X509Certificate;
}

namespace views {
class LabelButton;
class TableView;
class Widget;
}

class CertificateSelectorTableModel;

class SSLClientCertificateSelector : public SSLClientAuthObserver,
                                     public views::DialogDelegateView,
                                     public views::ButtonListener,
                                     public views::TableViewObserver {
 public:
  SSLClientCertificateSelector(
      content::WebContents* web_contents,
      const net::HttpNetworkSession* network_session,
      const scoped_refptr<net::SSLCertRequestInfo>& cert_request_info,
      const chrome::SelectCertificateCallback& callback);
  ~SSLClientCertificateSelector() override;

  void Init();

  net::X509Certificate* GetSelectedCert() const;

  // SSLClientAuthObserver implementation:
  void OnCertSelectedByNotification() override;

  // DialogDelegateView:
  bool CanResize() const override;
  base::string16 GetWindowTitle() const override;
  void DeleteDelegate() override;
  bool IsDialogButtonEnabled(ui::DialogButton button) const override;
  bool Cancel() override;
  bool Accept() override;
  views::View* GetInitiallyFocusedView() override;
  views::View* CreateExtraView() override;
  ui::ModalType GetModalType() const override;

  // views::ButtonListener:
  void ButtonPressed(views::Button* sender, const ui::Event& event) override;

  // views::TableViewObserver:
  void OnSelectionChanged() override;
  void OnDoubleClick() override;

 private:
  void CreateCertTable();

  // Callback after unlocking certificate slot.
  void Unlocked(net::X509Certificate* cert);

  scoped_ptr<CertificateSelectorTableModel> model_;

  content::WebContents* web_contents_;

  views::TableView* table_;
  views::LabelButton* view_cert_button_;

  DISALLOW_COPY_AND_ASSIGN(SSLClientCertificateSelector);
};

#endif  // CHROME_BROWSER_UI_VIEWS_SSL_CLIENT_CERTIFICATE_SELECTOR_H_
