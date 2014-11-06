// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CONTENT_RENDERER_PASSWORD_AUTOFILL_AGENT_H_
#define COMPONENTS_AUTOFILL_CONTENT_RENDERER_PASSWORD_AUTOFILL_AGENT_H_

#include <map>
#include <vector>

#include "base/memory/linked_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/autofill/core/common/password_form_fill_data.h"
#include "content/public/renderer/render_view_observer.h"
#include "third_party/WebKit/public/web/WebInputElement.h"

namespace blink {
class WebInputElement;
class WebKeyboardEvent;
class WebSecurityOrigin;
class WebView;
}

namespace autofill {

// This class is responsible for filling password forms.
// There is one PasswordAutofillAgent per RenderView.
class PasswordAutofillAgent : public content::RenderViewObserver {
 public:
  explicit PasswordAutofillAgent(content::RenderView* render_view);
  ~PasswordAutofillAgent() override;

  // WebViewClient editor related calls forwarded by the RenderView.
  // If they return true, it indicates the event was consumed and should not
  // be used for any other autofill activity.
  bool TextFieldDidEndEditing(const blink::WebInputElement& element);
  bool TextDidChangeInTextField(const blink::WebInputElement& element);
  bool TextFieldHandlingKeyDown(const blink::WebInputElement& element,
                                const blink::WebKeyboardEvent& event);

  // Fills the username and password fields of this form with the given values.
  // Returns true if the fields were filled, false otherwise.
  bool FillSuggestion(const blink::WebNode& node,
                      const blink::WebString& username,
                      const blink::WebString& password);

  // Previews the username and password fields of this form with the given
  // values. Returns true if the fields were previewed, false otherwise.
  bool PreviewSuggestion(const blink::WebNode& node,
                         const blink::WebString& username,
                         const blink::WebString& password);

  // Clears the preview for the username and password fields, restoring both to
  // their previous filled state. Return false if no login information was
  // found for the form.
  bool DidClearAutofillSelection(const blink::WebNode& node);
  // Shows an Autofill popup with username suggestions for |element|. If
  // |show_all| is |true|, will show all possible suggestions for that element,
  // otherwise shows suggestions based on current value of |element|.
  // Returns true if any suggestions were shown, false otherwise.
  bool ShowSuggestions(const blink::WebInputElement& element, bool show_all);

  // Called when new form controls are inserted.
  void OnDynamicFormsSeen(blink::WebFrame* frame);

  // Called when the user first interacts with the page after a load. This is a
  // signal to make autofilled values of password input elements accessible to
  // JavaScript.
  void FirstUserGestureObserved();

 protected:
  virtual bool OriginCanAccessPasswordManager(
      const blink::WebSecurityOrigin& origin);

 private:
  enum OtherPossibleUsernamesUsage {
    NOTHING_TO_AUTOFILL,
    OTHER_POSSIBLE_USERNAMES_ABSENT,
    OTHER_POSSIBLE_USERNAMES_PRESENT,
    OTHER_POSSIBLE_USERNAME_SHOWN,
    OTHER_POSSIBLE_USERNAME_SELECTED,
    OTHER_POSSIBLE_USERNAMES_MAX
  };

  // Ways to restrict which passwords are saved in ProvisionallySavePassword.
  enum ProvisionallySaveRestriction {
    RESTRICTION_NONE,
    RESTRICTION_NON_EMPTY_PASSWORD
  };

  struct PasswordInfo {
    blink::WebInputElement password_field;
    PasswordFormFillData fill_data;
    bool backspace_pressed_last;
    // The user manually edited the password more recently than the username was
    // changed.
    bool password_was_edited_last;
    PasswordInfo();
  };
  typedef std::map<blink::WebElement, PasswordInfo> LoginToPasswordInfoMap;
  typedef std::map<blink::WebElement, int> LoginToPasswordInfoKeyMap;
  typedef std::map<blink::WebElement, blink::WebElement> PasswordToLoginMap;
  typedef std::map<blink::WebFrame*,
                   linked_ptr<PasswordForm> > FrameToPasswordFormMap;

  // This class keeps track of autofilled password input elements and makes sure
  // the autofilled password value is not accessible to JavaScript code until
  // the user interacts with the page.
  class PasswordValueGatekeeper {
   public:
    PasswordValueGatekeeper();
    ~PasswordValueGatekeeper();

    // Call this for every autofilled password field, so that the gatekeeper
    // protects the value accordingly.
    void RegisterElement(blink::WebInputElement* element);

    // Call this to notify the gatekeeper that the user interacted with the
    // page.
    void OnUserGesture();

    // Call this to reset the gatekeeper on a new page navigation.
    void Reset();

   private:
    // Make the value of |element| accessible to JavaScript code.
    void ShowValue(blink::WebInputElement* element);

    bool was_user_gesture_seen_;
    std::vector<blink::WebInputElement> elements_;

    DISALLOW_COPY_AND_ASSIGN(PasswordValueGatekeeper);
  };

  // RenderViewObserver:
  bool OnMessageReceived(const IPC::Message& message) override;
  void DidStartProvisionalLoad(blink::WebLocalFrame* frame) override;
  void DidStartLoading() override;
  void DidFinishDocumentLoad(blink::WebLocalFrame* frame) override;
  void DidFinishLoad(blink::WebLocalFrame* frame) override;
  void DidStopLoading() override;
  void FrameDetached(blink::WebFrame* frame) override;
  void FrameWillClose(blink::WebFrame* frame) override;
  void WillSendSubmitEvent(blink::WebLocalFrame* frame,
                           const blink::WebFormElement& form) override;
  void WillSubmitForm(blink::WebLocalFrame* frame,
                      const blink::WebFormElement& form) override;

  // RenderView IPC handlers:
  void OnFillPasswordForm(int key, const PasswordFormFillData& form_data);
  void OnSetLoggingState(bool active);

  // Scans the given frame for password forms and sends them up to the browser.
  // If |only_visible| is true, only forms visible in the layout are sent.
  void SendPasswordForms(blink::WebFrame* frame, bool only_visible);

  bool ShowSuggestionPopup(const PasswordFormFillData& fill_data,
                           const blink::WebInputElement& user_input,
                           bool show_all);

  // Fills |login_input| and |password| with the most relevant suggestion from
  // |fill_data| and shows a popup with other suggestions.
  void PerformInlineAutocomplete(
      const blink::WebInputElement& username,
      const blink::WebInputElement& password,
      const PasswordFormFillData& fill_data);

  // Invoked when the passed frame is closing.  Gives us a chance to clear any
  // reference we may have to elements in that frame.
  void FrameClosing(const blink::WebFrame* frame);

  // Finds login information for a |node| that was previously filled.
  bool FindLoginInfo(const blink::WebNode& node,
                     blink::WebInputElement* found_input,
                     PasswordInfo** found_password);

  // Clears the preview for the username and password fields, restoring both to
  // their previous filled state.
  void ClearPreview(blink::WebInputElement* username,
                    blink::WebInputElement* password);

  // If |provisionally_saved_forms_| contains a form for |current_frame| or its
  // children, return such frame.
  blink::WebFrame* CurrentOrChildFrameWithSavedForms(
      const blink::WebFrame* current_frame);

  // Extracts a PasswordForm from |form| and saves it as
  // |provisionally_saved_forms_[frame]|, as long as it satisfies |restriction|.
  void ProvisionallySavePassword(blink::WebLocalFrame* frame,
                                 const blink::WebFormElement& form,
                                 ProvisionallySaveRestriction restriction);

  // The logins we have filled so far with their associated info.
  LoginToPasswordInfoMap login_to_password_info_;
  // And the keys under which PasswordAutofillManager can find the same info.
  LoginToPasswordInfoKeyMap login_to_password_info_key_;
  // A (sort-of) reverse map to |login_to_password_info_|.
  PasswordToLoginMap password_to_username_;

  // Used for UMA stats.
  OtherPossibleUsernamesUsage usernames_usage_;

  // Pointer to the WebView. Used to access page scale factor.
  blink::WebView* web_view_;

  // Set if the user might be submitting a password form on the current page,
  // but the submit may still fail (i.e. doesn't pass JavaScript validation).
  FrameToPasswordFormMap provisionally_saved_forms_;

  PasswordValueGatekeeper gatekeeper_;

  // True indicates that user debug information should be logged.
  bool logging_state_active_;

  // True indicates that the username field was autofilled, false otherwise.
  bool was_username_autofilled_;
  // True indicates that the password field was autofilled, false otherwise.
  bool was_password_autofilled_;

  // Records original starting point of username element's selection range
  // before preview.
  int username_selection_start_;

  // True indicates that all frames in a page have been rendered.
  bool did_stop_loading_;

  base::WeakPtrFactory<PasswordAutofillAgent> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PasswordAutofillAgent);
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CONTENT_RENDERER_PASSWORD_AUTOFILL_AGENT_H_
