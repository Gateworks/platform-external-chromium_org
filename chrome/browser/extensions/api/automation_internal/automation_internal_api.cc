// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/automation_internal/automation_internal_api.h"

#include <vector>

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/extensions/api/automation_internal/automation_action_adapter.h"
#include "chrome/browser/extensions/api/automation_internal/automation_util.h"
#include "chrome/browser/extensions/api/tabs/tabs_constants.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/extensions/api/automation_internal.h"
#include "chrome/common/extensions/manifest_handlers/automation.h"
#include "content/public/browser/ax_event_notification_details.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/permissions/permissions_data.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/ui/ash/accessibility/automation_manager_ash.h"
#endif

namespace extensions {
class AutomationWebContentsObserver;
}  // namespace extensions

DEFINE_WEB_CONTENTS_USER_DATA_KEY(extensions::AutomationWebContentsObserver);

namespace {
const int kDesktopProcessID = 0;
const int kDesktopRoutingID = 0;

const char kCannotRequestAutomationOnPage[] =
    "Cannot request automation tree on url \"*\". "
    "Extension manifest must request permission to access this host.";
}  // namespace

namespace extensions {

bool CanRequestAutomation(const Extension* extension,
                          const AutomationInfo* automation_info,
                          const content::WebContents* contents) {
  if (automation_info->desktop)
    return true;

  const GURL& url = contents->GetURL();
  // TODO(aboxhall): check for webstore URL
  if (automation_info->matches.MatchesURL(url))
    return true;

  int tab_id = ExtensionTabUtil::GetTabId(contents);
  content::RenderProcessHost* process = contents->GetRenderProcessHost();
  int process_id = process ? process->GetID() : -1;
  std::string unused_error;
  return extension->permissions_data()->CanAccessPage(
      extension, url, url, tab_id, process_id, &unused_error);
}

// Helper class that receives accessibility data from |WebContents|.
class AutomationWebContentsObserver
    : public content::WebContentsObserver,
      public content::WebContentsUserData<AutomationWebContentsObserver> {
 public:
  ~AutomationWebContentsObserver() override {}

  // content::WebContentsObserver overrides.
  void AccessibilityEventReceived(
      const std::vector<content::AXEventNotificationDetails>& details)
      override {
    automation_util::DispatchAccessibilityEventsToAutomation(
        details, browser_context_,
        web_contents()->GetContainerBounds().OffsetFromOrigin());
  }

  void RenderFrameDeleted(
      content::RenderFrameHost* render_frame_host) override {
    automation_util::DispatchTreeDestroyedEventToAutomation(
        render_frame_host->GetProcess()->GetID(),
        render_frame_host->GetRoutingID(),
        browser_context_);
  }

 private:
  friend class content::WebContentsUserData<AutomationWebContentsObserver>;

  AutomationWebContentsObserver(
      content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents),
        browser_context_(web_contents->GetBrowserContext()) {}

  content::BrowserContext* browser_context_;

  DISALLOW_COPY_AND_ASSIGN(AutomationWebContentsObserver);
};

// Helper class that implements an action adapter for a |RenderFrameHost|.
class RenderFrameHostActionAdapter : public AutomationActionAdapter {
 public:
  explicit RenderFrameHostActionAdapter(content::RenderFrameHost* rfh)
      : rfh_(rfh) {}

  virtual ~RenderFrameHostActionAdapter() {}

  // AutomationActionAdapter implementation.
  void DoDefault(int32 id) override { rfh_->AccessibilityDoDefaultAction(id); }

  void Focus(int32 id) override { rfh_->AccessibilitySetFocus(id); }

  void MakeVisible(int32 id) override {
    rfh_->AccessibilityScrollToMakeVisible(id, gfx::Rect());
  }

  void SetSelection(int32 id, int32 start, int32 end) override {
    rfh_->AccessibilitySetTextSelection(id, start, end);
  }

 private:
  content::RenderFrameHost* rfh_;

  DISALLOW_COPY_AND_ASSIGN(RenderFrameHostActionAdapter);
};

ExtensionFunction::ResponseAction
AutomationInternalEnableTabFunction::Run() {
  const AutomationInfo* automation_info = AutomationInfo::Get(extension());
  EXTENSION_FUNCTION_VALIDATE(automation_info);

  using api::automation_internal::EnableTab::Params;
  scoped_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  content::WebContents* contents = NULL;
  if (params->tab_id.get()) {
    int tab_id = *params->tab_id;
    if (!ExtensionTabUtil::GetTabById(tab_id,
                                      GetProfile(),
                                      include_incognito(),
                                      NULL, /* browser out param*/
                                      NULL, /* tab_strip out param */
                                      &contents,
                                      NULL /* tab_index out param */)) {
      return RespondNow(
          Error(tabs_constants::kTabNotFoundError, base::IntToString(tab_id)));
    }
  } else {
    contents = GetCurrentBrowser()->tab_strip_model()->GetActiveWebContents();
    if (!contents)
      return RespondNow(Error("No active tab"));
  }
  content::RenderFrameHost* rfh = contents->GetMainFrame();
  if (!rfh)
    return RespondNow(Error("Could not enable accessibility for active tab"));

  if (!CanRequestAutomation(extension(), automation_info, contents)) {
    return RespondNow(
        Error(kCannotRequestAutomationOnPage, contents->GetURL().spec()));
  }
  AutomationWebContentsObserver::CreateForWebContents(contents);
  contents->EnableTreeOnlyAccessibilityMode();
  return RespondNow(
      ArgumentList(api::automation_internal::EnableTab::Results::Create(
          rfh->GetProcess()->GetID(), rfh->GetRoutingID())));
  }

ExtensionFunction::ResponseAction
AutomationInternalPerformActionFunction::Run() {
  const AutomationInfo* automation_info = AutomationInfo::Get(extension());
  EXTENSION_FUNCTION_VALIDATE(automation_info && automation_info->interact);

  using api::automation_internal::PerformAction::Params;
  scoped_ptr<Params> params(Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  if (params->args.process_id == kDesktopProcessID &&
      params->args.routing_id == kDesktopRoutingID) {
#if defined(OS_CHROMEOS)
    return RouteActionToAdapter(
        params.get(), AutomationManagerAsh::GetInstance());
#else
    NOTREACHED();
    return RespondNow(Error("Unexpected action on desktop automation tree;"
                            " platform does not support desktop automation"));
#endif  // defined(OS_CHROMEOS)
  }
  content::RenderFrameHost* rfh =
      content::RenderFrameHost::FromID(params->args.process_id,
                                       params->args.routing_id);
  if (!rfh)
    return RespondNow(Error("Ignoring action on destroyed node"));

  const content::WebContents* contents =
      content::WebContents::FromRenderFrameHost(rfh);
  if (!CanRequestAutomation(extension(), automation_info, contents)) {
    return RespondNow(
        Error(kCannotRequestAutomationOnPage, contents->GetURL().spec()));
  }

  RenderFrameHostActionAdapter adapter(rfh);
  return RouteActionToAdapter(params.get(), &adapter);
}

ExtensionFunction::ResponseAction
AutomationInternalPerformActionFunction::RouteActionToAdapter(
    api::automation_internal::PerformAction::Params* params,
    AutomationActionAdapter* adapter) {
  int32 automation_id = params->args.automation_node_id;
  switch (params->args.action_type) {
    case api::automation_internal::ACTION_TYPE_DODEFAULT:
      adapter->DoDefault(automation_id);
      break;
    case api::automation_internal::ACTION_TYPE_FOCUS:
      adapter->Focus(automation_id);
      break;
    case api::automation_internal::ACTION_TYPE_MAKEVISIBLE:
      adapter->MakeVisible(automation_id);
      break;
    case api::automation_internal::ACTION_TYPE_SETSELECTION: {
      api::automation_internal::SetSelectionParams selection_params;
      EXTENSION_FUNCTION_VALIDATE(
          api::automation_internal::SetSelectionParams::Populate(
              params->opt_args.additional_properties, &selection_params));
      adapter->SetSelection(automation_id,
                           selection_params.start_index,
                           selection_params.end_index);
      break;
    }
    default:
      NOTREACHED();
  }
  return RespondNow(NoArguments());
}

ExtensionFunction::ResponseAction
AutomationInternalEnableDesktopFunction::Run() {
#if defined(OS_CHROMEOS)
  const AutomationInfo* automation_info = AutomationInfo::Get(extension());
  if (!automation_info || !automation_info->desktop)
    return RespondNow(Error("desktop permission must be requested"));

  AutomationManagerAsh::GetInstance()->Enable(browser_context());
  return RespondNow(NoArguments());
#else
  return RespondNow(Error("getDesktop is unsupported by this platform"));
#endif  // defined(OS_CHROMEOS)
}

}  // namespace extensions
