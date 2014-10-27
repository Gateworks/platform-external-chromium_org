// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "athena/system/network_selector.h"

#include "athena/screen/public/screen_manager.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/network/network_configuration_handler.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_handler.h"
#include "chromeos/network/network_profile_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_state_handler_observer.h"
#include "chromeos/network/network_type_pattern.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "ui/aura/window.h"
#include "ui/chromeos/network/network_icon.h"
#include "ui/chromeos/network/network_info.h"
#include "ui/chromeos/network/network_list.h"
#include "ui/chromeos/network/network_list_delegate.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/blue_button.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/scrollbar/overlay_scroll_bar.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

using chromeos::NetworkConfigurationHandler;
using chromeos::NetworkConnectionHandler;
using chromeos::NetworkHandler;
using chromeos::NetworkProfileHandler;
using chromeos::NetworkState;

namespace {

// The View for the user to enter the password for connceting to a network. This
// view also shows an error message if the network connection fails.
class PasswordView : public views::View, public views::ButtonListener {
 public:
  PasswordView(const ui::NetworkInfo& network,
               const base::Callback<void(bool)>& callback)
      : network_(network),
        callback_(callback),
        connect_(nullptr),
        cancel_(nullptr),
        textfield_(nullptr),
        error_msg_(nullptr),
        weak_ptr_(this) {
    const int kHorizontal = 5;
    const int kVertical = 0;
    const int kPadding = 0;

    views::BoxLayout* layout = new views::BoxLayout(
        views::BoxLayout::kVertical, kHorizontal, kVertical, kPadding);
    layout->set_main_axis_alignment(
        views::BoxLayout::MAIN_AXIS_ALIGNMENT_START);
    layout->set_cross_axis_alignment(
        views::BoxLayout::CROSS_AXIS_ALIGNMENT_STRETCH);
    SetLayoutManager(layout);

    views::View* container = new views::View;
    layout = new views::BoxLayout(
        views::BoxLayout::kHorizontal, kHorizontal, kVertical, kPadding);
    layout->set_main_axis_alignment(
        views::BoxLayout::MAIN_AXIS_ALIGNMENT_START);
    container->SetLayoutManager(layout);

    textfield_ = new views::Textfield();
    textfield_->set_placeholder_text(base::ASCIIToUTF16("Password"));
    textfield_->SetTextInputType(ui::TEXT_INPUT_TYPE_PASSWORD);
    textfield_->set_default_width_in_chars(35);
    container->AddChildView(textfield_);

    connect_ = new views::BlueButton(this, base::ASCIIToUTF16("Connect"));
    container->AddChildView(connect_);

    cancel_ = new views::LabelButton(this, base::ASCIIToUTF16("Cancel"));
    cancel_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    container->AddChildView(cancel_);

    AddChildView(container);
  }

  ~PasswordView() override {}

 private:
  void CloseDialog(bool successful) { callback_.Run(successful); }

  void OnKnownError(const std::string& error_name,
                    scoped_ptr<base::DictionaryValue> error_data) {
    std::string message;
    if (!error_data->GetString(chromeos::network_handler::kDbusErrorMessage,
                               &message))
      message = error_name;
    if (message.empty())
      message = std::string("Unknown error.");
    if (!error_msg_) {
      error_msg_ = new views::Label();
      error_msg_->SetFontList(
          error_msg_->font_list().Derive(0, gfx::Font::BOLD));
      error_msg_->SetEnabledColor(SK_ColorRED);
    }
    error_msg_->SetText(base::UTF8ToUTF16(message));
    if (!error_msg_->parent()) {
      AddChildView(error_msg_);
      InvalidateLayout();
      GetWidget()->GetRootView()->Layout();
      ScrollRectToVisible(error_msg_->bounds());
    }
    connect_->SetEnabled(true);
  }

  void OnSetProfileSucceed(const base::string16& password) {
    base::DictionaryValue properties;
    properties.SetStringWithoutPathExpansion(shill::kPassphraseProperty,
                                             textfield_->text());
    NetworkHandler::Get()->network_configuration_handler()->SetProperties(
        network_.service_path,
        properties,
        base::Bind(&PasswordView::OnSetPropertiesSucceed,
                   weak_ptr_.GetWeakPtr()),
        base::Bind(&PasswordView::OnKnownError, weak_ptr_.GetWeakPtr()));
  }

  void OnSetPropertiesSucceed() {
    const bool check_error_state = false;
    NetworkHandler::Get()->network_connection_handler()->ConnectToNetwork(
        network_.service_path,
        base::Bind(&PasswordView::OnConnectionSucceed, weak_ptr_.GetWeakPtr()),
        base::Bind(&PasswordView::OnKnownError, weak_ptr_.GetWeakPtr()),
        check_error_state);
  }

  void OnConnectionSucceed() { CloseDialog(true); }

  // views::View:
  virtual void ViewHierarchyChanged(
      const views::View::ViewHierarchyChangedDetails& details) override {
    if (details.is_add && details.child == this)
      textfield_->RequestFocus();
  }

  // views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender,
                             const ui::Event& event) override {
    if (sender == connect_) {
      if (error_msg_) {
        RemoveChildView(error_msg_);
        delete error_msg_;
        error_msg_ = nullptr;
      }
      connect_->SetEnabled(false);
      NetworkHandler::Get()->network_configuration_handler()->SetNetworkProfile(
          network_.service_path,
          NetworkProfileHandler::GetSharedProfilePath(),
          base::Bind(&PasswordView::OnSetProfileSucceed,
                     weak_ptr_.GetWeakPtr(),
                     textfield_->text()),
          base::Bind(&PasswordView::OnKnownError, weak_ptr_.GetWeakPtr()));
    } else if (sender == cancel_) {
      CloseDialog(false);
    } else {
      NOTREACHED();
    }
  }

  ui::NetworkInfo network_;
  base::Callback<void(bool)> callback_;

  views::BlueButton* connect_;
  views::LabelButton* cancel_;
  views::Textfield* textfield_;
  views::Label* error_msg_;
  base::WeakPtrFactory<PasswordView> weak_ptr_;

  DISALLOW_COPY_AND_ASSIGN(PasswordView);
};

// A View that represents a single row in the network list. This row also
// contains the View for taking password for password-protected networks.
class NetworkRow : public views::View {
 public:
  NetworkRow(const ui::NetworkInfo& network)
      : network_(network), weak_ptr_(this) {
    SetBorder(views::Border::CreateEmptyBorder(10, 5, 10, 5));
    SetLayoutManager(
        new views::BoxLayout(views::BoxLayout::kHorizontal, 0, 0, 10));
    Update(network);
  }

  ~NetworkRow() override {}

  void Update(const ui::NetworkInfo& network) {
    network_ = network;
    views::ImageView* icon = new views::ImageView();
    icon->SetImage(network.image);
    icon->SetBounds(0, 0, network.image.width(), network.image.height());

    views::Label* label = new views::Label(network.label);
    if (network.highlight)
      label->SetFontList(label->font_list().Derive(0, gfx::Font::BOLD));
    AddChildView(icon);
    AddChildView(label);
    if (password_view_)
      AddChildView(password_view_.get());
  }

  bool has_password_view() const { return password_view_; }

 private:
  void OnPasswordComplete(bool successful) {
    password_view_.reset();
    InvalidateLayout();
    GetWidget()->GetRootView()->Layout();
    ScrollRectToVisible(GetContentsBounds());
  }

  void ShowPasswordView(const std::string& service_path) {
    const NetworkState* network =
        NetworkHandler::Get()->network_state_handler()->GetNetworkState(
            service_path);
    if (!network)
      return;

    // If this is not a wifi network that needs a password, then ignore.
    if (network->type() != shill::kTypeWifi ||
        network->security() == shill::kSecurityNone) {
      return;
    }

    password_view_.reset(new PasswordView(
        network_,
        base::Bind(&NetworkRow::OnPasswordComplete, weak_ptr_.GetWeakPtr())));
    password_view_->set_owned_by_client();
    AddChildView(password_view_.get());
    PreferredSizeChanged();
    GetWidget()->GetRootView()->Layout();
    ScrollRectToVisible(password_view_->bounds());
  }

  void OnNetworkConnectionError(const std::string& service_path,
                                const std::string& error_name,
                                scoped_ptr<base::DictionaryValue> error_data) {
    if (error_name == NetworkConnectionHandler::kErrorConnectCanceled)
      return;
    if (error_name == shill::kErrorBadPassphrase ||
        error_name == NetworkConnectionHandler::kErrorPassphraseRequired ||
        error_name == NetworkConnectionHandler::kErrorConfigurationRequired ||
        error_name == NetworkConnectionHandler::kErrorAuthenticationRequired) {
      ShowPasswordView(service_path);
    }
  }

  void ActivateNetwork() {
    const chromeos::NetworkState* network =
        NetworkHandler::Get()->network_state_handler()->GetNetworkState(
            network_.service_path);
    if (!network)
      return;
    if (network->IsConnectedState()) {
      NetworkHandler::Get()->network_connection_handler()->DisconnectNetwork(
          network_.service_path,
          base::Closure(),
          chromeos::network_handler::ErrorCallback());
    } else if (!network->IsConnectingState()) {
      // |network| is not connected, and not already trying to connect.
      NetworkHandler::Get()->network_connection_handler()->ConnectToNetwork(
          network_.service_path,
          base::Closure(),
          base::Bind(&NetworkRow::OnNetworkConnectionError,
                     weak_ptr_.GetWeakPtr(),
                     network_.service_path),
          false);
    }
  }

  // views::View:
  virtual void OnMouseEvent(ui::MouseEvent* event) override {
    if (event->type() != ui::ET_MOUSE_PRESSED)
      return;
    ActivateNetwork();
    event->SetHandled();
  }

  virtual void OnGestureEvent(ui::GestureEvent* gesture) override {
    if (gesture->type() != ui::ET_GESTURE_TAP)
      return;
    ActivateNetwork();
    gesture->SetHandled();
  }

  ui::NetworkInfo network_;
  scoped_ptr<views::View> password_view_;
  base::WeakPtrFactory<NetworkRow> weak_ptr_;

  DISALLOW_COPY_AND_ASSIGN(NetworkRow);
};

class NetworkSelector : public ui::NetworkListDelegate,
                        public chromeos::NetworkStateHandlerObserver,
                        public views::DialogDelegate {
 public:
  NetworkSelector()
      : scroll_content_(nullptr), scroller_(nullptr), network_list_(this) {
    CreateNetworkList();
    CreateWidget();

    NetworkHandler::Get()->network_state_handler()->RequestScan();
    NetworkHandler::Get()->network_state_handler()->AddObserver(this,
                                                                FROM_HERE);
  }

  ~NetworkSelector() override {
    NetworkHandler::Get()->network_state_handler()->RemoveObserver(this,
                                                                   FROM_HERE);
  }

 private:
  void CreateWidget() {
    // Same as CreateDialogWidgetWithBounds() with an empty |bounds|.
    views::Widget* widget = views::DialogDelegate::CreateDialogWidget(
        this, athena::ScreenManager::Get()->GetContext(), nullptr);
    widget->Show();
    widget->CenterWindow(gfx::Size(400, 400));
  }

  void CreateNetworkList() {
    const int kListHeight = 400;
    scroller_ = new views::ScrollView();
    scroller_->set_background(
        views::Background::CreateSolidBackground(SK_ColorWHITE));

    scroll_content_ = new views::View;
    scroll_content_->SetLayoutManager(
        new views::BoxLayout(views::BoxLayout::kVertical, 0, 0, 0));
    scroller_->SetContents(scroll_content_);

    scroller_->ClipHeightTo(kListHeight, kListHeight);
    scroller_->SetVerticalScrollBar(new views::OverlayScrollBar(false));

    network_list_.set_content_view(scroll_content_);
  }

  void UpdateNetworkList() { network_list_.UpdateNetworkList(); }

  // ui::NetworkListDelegate:
  virtual views::View* CreateViewForNetwork(
      const ui::NetworkInfo& info) override {
    return new NetworkRow(info);
  }

  virtual bool IsViewHovered(views::View* view) override {
    return static_cast<NetworkRow*>(view)->has_password_view();
  }

  virtual chromeos::NetworkTypePattern GetNetworkTypePattern() const override {
    return chromeos::NetworkTypePattern::NonVirtual();
  }

  virtual void UpdateViewForNetwork(views::View* view,
                                    const ui::NetworkInfo& info) override {
    static_cast<NetworkRow*>(view)->Update(info);
  }

  virtual views::Label* CreateInfoLabel() override {
    views::Label* label = new views::Label();
    return label;
  }

  virtual void RelayoutScrollList() override { scroller_->Layout(); }

  // chromeos::NetworkStateHandlerObserver:
  virtual void NetworkListChanged() override { UpdateNetworkList(); }

  virtual void DeviceListChanged() override {}

  virtual void DefaultNetworkChanged(
      const chromeos::NetworkState* network) override {}

  virtual void NetworkConnectionStateChanged(
      const chromeos::NetworkState* network) override {}

  virtual void NetworkPropertiesUpdated(
      const chromeos::NetworkState* network) override {}

  // views::DialogDelegate:
  virtual ui::ModalType GetModalType() const override {
    return ui::MODAL_TYPE_SYSTEM;
  }
  virtual void DeleteDelegate() override { delete this; }
  virtual views::Widget* GetWidget() override { return scroller_->GetWidget(); }
  virtual const views::Widget* GetWidget() const override {
    return scroller_->GetWidget();
  }
  virtual views::View* GetContentsView() override { return scroller_; }
  virtual int GetDialogButtons() const override { return ui::DIALOG_BUTTON_OK; }
  virtual bool Close() override { return true; }

  views::View* scroll_content_;
  views::ScrollView* scroller_;

  views::View* connect_;

  ui::NetworkListView network_list_;

  DISALLOW_COPY_AND_ASSIGN(NetworkSelector);
};

}  // namespace

namespace athena {

void CreateNetworkSelector() {
  new NetworkSelector();
}

}  // namespace athena
