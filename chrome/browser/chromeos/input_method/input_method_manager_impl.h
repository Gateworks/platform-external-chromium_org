// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_
#define CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_

#include <map>
#include <string>
#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/observer_list.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/chromeos/input_method/candidate_window_controller.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chromeos/ime/input_method_manager.h"
#include "chromeos/ime/input_method_whitelist.h"

namespace chromeos {
class ComponentExtensionIMEManager;
class ComponentExtensionIMEManagerDelegate;
class InputMethodEngine;
namespace input_method {
class InputMethodDelegate;
class ImeKeyboard;

// The implementation of InputMethodManager.
class InputMethodManagerImpl : public InputMethodManager,
                               public CandidateWindowController::Observer {
 public:
  class StateImpl : public InputMethodManager::State {
   public:
    StateImpl(InputMethodManagerImpl* manager, Profile* profile);

    // Init new state as a copy of other.
    void InitFrom(const StateImpl& other);

    // Returns true if (manager_->state_ == this).
    bool IsActive() const;

    // Returns human-readable dump (for debug).
    std::string Dump() const;

    // Adds new input method to given list if possible
    bool EnableInputMethodImpl(
        const std::string& input_method_id,
        std::vector<std::string>* new_active_input_method_ids) const;

    // Returns true if |input_method_id| is in |active_input_method_ids|.
    bool InputMethodIsActivated(const std::string& input_method_id) const;

    // If |current_input_methodid_| is not in |input_method_ids|, switch to
    // input_method_ids[0]. If the ID is equal to input_method_ids[N], switch to
    // input_method_ids[N+1].
    void SwitchToNextInputMethodInternal(
        const std::vector<std::string>& input_method_ids,
        const std::string& current_input_methodid);

    // Returns true if given input method requires pending extension.
    bool MethodAwaitsExtensionLoad(const std::string& input_method_id) const;

    // InputMethodManager::State overrides.
    virtual scoped_refptr<InputMethodManager::State> Clone() const override;
    virtual void AddInputMethodExtension(
        const std::string& extension_id,
        const InputMethodDescriptors& descriptors,
        InputMethodEngineInterface* instance) override;
    virtual void RemoveInputMethodExtension(
        const std::string& extension_id) override;
    virtual void ChangeInputMethod(const std::string& input_method_id,
                                   bool show_message) override;
    virtual bool EnableInputMethod(
        const std::string& new_active_input_method_id) override;
    virtual void EnableLoginLayouts(
        const std::string& language_code,
        const std::vector<std::string>& initial_layouts) override;
    virtual void EnableLockScreenLayouts() override;
    virtual void GetInputMethodExtensions(
        InputMethodDescriptors* result) override;
    virtual scoped_ptr<InputMethodDescriptors> GetActiveInputMethods()
        const override;
    virtual const std::vector<std::string>& GetActiveInputMethodIds()
        const override;
    virtual const InputMethodDescriptor* GetInputMethodFromId(
        const std::string& input_method_id) const override;
    virtual size_t GetNumActiveInputMethods() const override;
    virtual void SetEnabledExtensionImes(
        std::vector<std::string>* ids) override;
    virtual void SetInputMethodLoginDefault() override;
    virtual void SetInputMethodLoginDefaultFromVPD(
        const std::string& locale,
        const std::string& layout) override;
    virtual bool SwitchToNextInputMethod() override;
    virtual bool SwitchToPreviousInputMethod(
        const ui::Accelerator& accelerator) override;
    virtual bool SwitchInputMethod(const ui::Accelerator& accelerator) override;
    virtual InputMethodDescriptor GetCurrentInputMethod() const override;
    virtual bool ReplaceEnabledInputMethods(
        const std::vector<std::string>& new_active_input_method_ids) override;

    // ------------------------- Data members.
    Profile* const profile;

    // The input method which was/is selected.
    InputMethodDescriptor previous_input_method;
    InputMethodDescriptor current_input_method;

    // The active input method ids cache.
    std::vector<std::string> active_input_method_ids;

    // The pending input method id for delayed 3rd party IME enabling.
    std::string pending_input_method_id;

    // The list of enabled extension IMEs.
    std::vector<std::string> enabled_extension_imes;

    // Extra input methods that have been explicitly added to the menu, such as
    // those created by extension.
    std::map<std::string, InputMethodDescriptor> extra_input_methods;

   private:
    InputMethodManagerImpl* const manager_;

   protected:
    friend base::RefCounted<chromeos::input_method::InputMethodManager::State>;
    virtual ~StateImpl();
  };

  // Constructs an InputMethodManager instance. The client is responsible for
  // calling |SetUISessionState| in response to relevant changes in browser
  // state.
  InputMethodManagerImpl(scoped_ptr<InputMethodDelegate> delegate,
                         bool enable_extension_loading);
  virtual ~InputMethodManagerImpl();

  // Receives notification of an InputMethodManager::UISessionState transition.
  void SetUISessionState(UISessionState new_ui_session);

  // InputMethodManager override:
  virtual UISessionState GetUISessionState() override;
  virtual void AddObserver(InputMethodManager::Observer* observer) override;
  virtual void AddCandidateWindowObserver(
      InputMethodManager::CandidateWindowObserver* observer) override;
  virtual void RemoveObserver(InputMethodManager::Observer* observer) override;
  virtual void RemoveCandidateWindowObserver(
      InputMethodManager::CandidateWindowObserver* observer) override;
  virtual scoped_ptr<InputMethodDescriptors>
      GetSupportedInputMethods() const override;
  virtual void ActivateInputMethodMenuItem(const std::string& key) override;
  virtual bool IsISOLevel5ShiftUsedByCurrentInputMethod() const override;
  virtual bool IsAltGrUsedByCurrentInputMethod() const override;

  virtual ImeKeyboard* GetImeKeyboard() override;
  virtual InputMethodUtil* GetInputMethodUtil() override;
  virtual ComponentExtensionIMEManager*
      GetComponentExtensionIMEManager() override;
  virtual bool IsLoginKeyboard(const std::string& layout) const override;

  virtual bool MigrateInputMethods(
      std::vector<std::string>* input_method_ids) override;

  virtual scoped_refptr<InputMethodManager::State> CreateNewState(
      Profile* profile) override;

  virtual scoped_refptr<InputMethodManager::State> GetActiveIMEState() override;
  virtual void SetState(
      scoped_refptr<InputMethodManager::State> state) override;

  // Sets |candidate_window_controller_|.
  void SetCandidateWindowControllerForTesting(
      CandidateWindowController* candidate_window_controller);
  // Sets |keyboard_|.
  void SetImeKeyboardForTesting(ImeKeyboard* keyboard);
  // Initialize |component_extension_manager_|.
  void InitializeComponentExtensionForTesting(
      scoped_ptr<ComponentExtensionIMEManagerDelegate> delegate);

 private:
  friend class InputMethodManagerImplTest;

  // CandidateWindowController::Observer overrides:
  virtual void CandidateClicked(int index) override;
  virtual void CandidateWindowOpened() override;
  virtual void CandidateWindowClosed() override;

  // Temporarily deactivates all input methods (e.g. Chinese, Japanese, Arabic)
  // since they are not necessary to input a login password. Users are still
  // able to use/switch active keyboard layouts (e.g. US qwerty, US dvorak,
  // French).
  void OnScreenLocked();

  // Resumes the original state by activating input methods and/or changing the
  // current input method as needed.
  void OnScreenUnlocked();

  // Returns true if the given input method config value is a string list
  // that only contains an input method ID of a keyboard layout.
  bool ContainsOnlyKeyboardLayout(const std::vector<std::string>& value);

  // Creates and initializes |candidate_window_controller_| if it hasn't been
  // done.
  void MaybeInitializeCandidateWindowController();

  // Returns Input Method that best matches given id.
  const InputMethodDescriptor* LookupInputMethod(
      const std::string& input_method_id,
      StateImpl* state);

  // Change system input method.
  void ChangeInputMethodInternal(const InputMethodDescriptor& descriptor,
                                 bool show_message,
                                 bool notify_menu);

  // Loads necessary component extensions.
  // TODO(nona): Support dynamical unloading.
  void LoadNecessaryComponentExtensions(StateImpl* state);

  // Starts or stops the system input method framework as needed.
  // (after list of enabled input methods has been updated).
  // If state is active, active input method is updated.
  void ReconfigureIMFramework(StateImpl* state);

  // Record input method usage histograms.
  void RecordInputMethodUsage(std::string input_method_id);

  scoped_ptr<InputMethodDelegate> delegate_;

  // The current UI session status.
  UISessionState ui_session_;

  // A list of objects that monitor the manager.
  ObserverList<InputMethodManager::Observer> observers_;
  ObserverList<CandidateWindowObserver> candidate_window_observers_;

  scoped_refptr<StateImpl> state_;

  // The candidate window.  This will be deleted when the APP_TERMINATING
  // message is sent.
  scoped_ptr<CandidateWindowController> candidate_window_controller_;

  // An object which provides miscellaneous input method utility functions. Note
  // that |util_| is required to initialize |keyboard_|.
  InputMethodUtil util_;

  // An object which provides component extension ime management functions.
  scoped_ptr<ComponentExtensionIMEManager> component_extension_ime_manager_;

  // An object for switching XKB layouts and keyboard status like caps lock and
  // auto-repeat interval.
  scoped_ptr<ImeKeyboard> keyboard_;


  // Whether load IME extensions.
  bool enable_extension_loading_;

  // The engine map from extension_id to an engine.
  typedef std::map<std::string, InputMethodEngineInterface*> EngineMap;
  EngineMap engine_map_;

  // The map from input method id to the input method stat id.
  // The stat id has the format: <category#><first char after prefix><index>
  // For example, Chinese Simplified Pinyin IME has the stat id:
  //   2,'p',1 -> 211201
  //   2 means it in INPUT_METHOD_CATEGORY_ZH;
  //   112 means the first char after prefix is 'p' of 'pinyin';
  //   01 means it's the second pinyin as the first pinyin is for Traditional
  //   Chinese Pinyin IME. Note "zh-hant-t-i0-pinyin" < "zh-t-i0-pinyin".
  std::map<std::string, int> stat_id_map_;

  DISALLOW_COPY_AND_ASSIGN(InputMethodManagerImpl);
};

}  // namespace input_method
}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_INPUT_METHOD_INPUT_METHOD_MANAGER_IMPL_H_
