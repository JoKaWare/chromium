// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/active_tab_permission_granter.h"

#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/process_manager.h"
#include "extensions/common/extension_messages.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/permissions/permission_set.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/common/user_script.h"
#include "url/gurl.h"

namespace extensions {

namespace {

using CreateMessageFunction = base::Callback<IPC::Message*(bool)>;

// Creates a new IPC message for updating tab-specific permissions.
IPC::Message* CreateUpdateMessage(const GURL& visible_url,
                                  const std::string& extension_id,
                                  const URLPatternSet& new_hosts,
                                  int tab_id,
                                  bool update_whitelist) {
  return new ExtensionMsg_UpdateTabSpecificPermissions(
      visible_url, extension_id, new_hosts, update_whitelist, tab_id);
}

// Creates a new IPC message for clearing tab-specific permissions.
IPC::Message* CreateClearMessage(const std::vector<std::string>& ids,
                                 int tab_id,
                                 bool update_whitelist) {
  return new ExtensionMsg_ClearTabSpecificPermissions(
      ids, update_whitelist, tab_id);
}

// Sends a message exactly once to each render process host owning one of the
// given |frame_hosts| and |tab_process|. If |tab_process| doesn't own any of
// the |frame_hosts|, it will not be signaled to update its origin whitelist.
void SendMessageToProcesses(
    const std::set<content::RenderFrameHost*>& frame_hosts,
    content::RenderProcessHost* tab_process,
    const CreateMessageFunction& create_message) {
  std::set<content::RenderProcessHost*> sent_to_hosts;
  for (content::RenderFrameHost* frame_host : frame_hosts) {
    content::RenderProcessHost* process_host = frame_host->GetProcess();
    if (sent_to_hosts.count(process_host) == 0) {
      // Extension processes have to update the origin whitelists.
      process_host->Send(create_message.Run(true));
      sent_to_hosts.insert(frame_host->GetProcess());
    }
  }
  // If the tab wasn't one of those processes already updated (it likely
  // wasn't), update it. Tabs don't need to update the origin whitelist.
  if (sent_to_hosts.count(tab_process) == 0)
    tab_process->Send(create_message.Run(false));
}

}  // namespace

ActiveTabPermissionGranter::ActiveTabPermissionGranter(
    content::WebContents* web_contents,
    int tab_id,
    Profile* profile)
    : content::WebContentsObserver(web_contents),
      tab_id_(tab_id),
      extension_registry_observer_(this) {
  extension_registry_observer_.Add(ExtensionRegistry::Get(profile));
}

ActiveTabPermissionGranter::~ActiveTabPermissionGranter() {}

void ActiveTabPermissionGranter::GrantIfRequested(const Extension* extension) {
  if (granted_extensions_.Contains(extension->id()))
    return;

  APIPermissionSet new_apis;
  URLPatternSet new_hosts;

  const PermissionsData* permissions_data = extension->permissions_data();

  // If the extension requested all-hosts but has had it withheld, we grant it
  // active tab-style permissions, even if it doesn't have the activeTab
  // permission in the manifest.
  if (permissions_data->HasAPIPermission(APIPermission::kActiveTab) ||
      permissions_data->HasWithheldImpliedAllHosts()) {
    new_hosts.AddOrigin(UserScript::ValidUserScriptSchemes(),
                        web_contents()->GetVisibleURL().GetOrigin());
    new_apis.insert(APIPermission::kTab);
  }

  if (permissions_data->HasAPIPermission(APIPermission::kTabCapture))
    new_apis.insert(APIPermission::kTabCaptureForTab);

  if (!new_apis.empty() || !new_hosts.is_empty()) {
    granted_extensions_.Insert(extension);
    PermissionSet new_permissions(new_apis, ManifestPermissionSet(), new_hosts,
                                  URLPatternSet());
    permissions_data->UpdateTabSpecificPermissions(tab_id_, new_permissions);
    const content::NavigationEntry* navigation_entry =
        web_contents()->GetController().GetVisibleEntry();
    if (navigation_entry) {
      // We update all extension render views with the new tab permissions, and
      // also the tab itself.
      CreateMessageFunction update_message =
          base::Bind(&CreateUpdateMessage,
                     navigation_entry->GetURL(),
                     extension->id(),
                     new_hosts,
                     tab_id_);
      SendMessageToProcesses(
          ProcessManager::Get(web_contents()->GetBrowserContext())->
              GetRenderFrameHostsForExtension(extension->id()),
          web_contents()->GetRenderProcessHost(),
          update_message);

      // If more things ever need to know about this, we should consider making
      // an observer class.
      // It's important that this comes after the IPC is sent to the renderer,
      // so that any tasks executing in the renderer occur after it has the
      // updated permissions.
      ExtensionActionRunner::GetForWebContents(web_contents())
          ->OnActiveTabPermissionGranted(extension);
    }
  }
}

void ActiveTabPermissionGranter::RevokeForTesting() {
  ClearActiveExtensionsAndNotify();
}

void ActiveTabPermissionGranter::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  // Important: sub-frames don't get granted!
  if (!navigation_handle->IsInMainFrame() ||
      !navigation_handle->HasCommitted() ||
      navigation_handle->IsSamePage()) {
    return;
  }

  // Only clear the granted permissions for cross-origin navigations.
  //
  // See http://crbug.com/404243 for why. Currently we only differentiate
  // between same-origin and cross-origin navigations when the
  // script-require-action flag is on. It's not clear it's good for general
  // activeTab consumption (we likely need to build some UI around it first).
  // However, the scripts-require-action feature is all-but unusable without
  // this behaviour.
  if (FeatureSwitch::scripts_require_action()->IsEnabled()) {
    const content::NavigationEntry* navigation_entry =
        web_contents()->GetController().GetVisibleEntry();
    if (!navigation_entry ||
        (navigation_entry->GetURL().GetOrigin() !=
            navigation_handle->GetPreviousURL().GetOrigin())) {
      ClearActiveExtensionsAndNotify();
    }
  } else {
    ClearActiveExtensionsAndNotify();
  }
}

void ActiveTabPermissionGranter::WebContentsDestroyed() {
  ClearActiveExtensionsAndNotify();
}

void ActiveTabPermissionGranter::OnExtensionUnloaded(
    content::BrowserContext* browser_context,
    const Extension* extension,
    UnloadedExtensionInfo::Reason reason) {
  // Note: don't need to clear the permissions (nor tell the renderer about it)
  // because it's being unloaded anyway.
  granted_extensions_.Remove(extension->id());
}

void ActiveTabPermissionGranter::ClearActiveExtensionsAndNotify() {
  if (granted_extensions_.is_empty())
    return;

  std::set<content::RenderFrameHost*> frame_hosts;
  std::vector<std::string> extension_ids;
  ProcessManager* process_manager =
      ProcessManager::Get(web_contents()->GetBrowserContext());
  for (const scoped_refptr<const Extension>& extension : granted_extensions_) {
    extension->permissions_data()->ClearTabSpecificPermissions(tab_id_);
    extension_ids.push_back(extension->id());
    std::set<content::RenderFrameHost*> extension_frame_hosts =
        process_manager->GetRenderFrameHostsForExtension(extension->id());
    frame_hosts.insert(extension_frame_hosts.begin(),
                       extension_frame_hosts.end());
  }

  CreateMessageFunction clear_message =
      base::Bind(&CreateClearMessage, extension_ids, tab_id_);
  SendMessageToProcesses(frame_hosts,
                         web_contents()->GetRenderProcessHost(),
                         clear_message);

  granted_extensions_.Clear();
}

}  // namespace extensions
