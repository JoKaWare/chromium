// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/identity/identity_get_auth_token_function.h"

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/extensions/api/identity/identity_api.h"
#include "chrome/browser/extensions/api/identity/identity_constants.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/signin/chrome_signin_client_factory.h"
#include "chrome/browser/signin/profile_oauth2_token_service_factory.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/common/extensions/api/identity.h"
#include "components/signin/core/browser/profile_oauth2_token_service.h"
#include "components/signin/core/browser/signin_manager.h"
#include "components/signin/core/common/profile_management_switches.h"
#include "extensions/common/extension_l10n_util.h"
#include "google_apis/gaia/gaia_urls.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/app_mode/app_mode_utils.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/login/session/user_session_manager.h"
#include "chrome/browser/chromeos/policy/browser_policy_connector_chromeos.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service.h"
#include "chrome/browser/chromeos/settings/device_oauth2_token_service_factory.h"
#include "components/user_manager/user_manager.h"
#include "google_apis/gaia/gaia_constants.h"
#endif

namespace extensions {

namespace {

#if defined(OS_CHROMEOS)
// The list of apps that are allowed to use the Identity API to retrieve the
// token from the device robot account in a public session.
const char* const kPublicSessionAllowedOrigins[] = {
    // Chrome Remote Desktop - Chromium branding.
    "chrome-extension://ljacajndfccfgnfohlgkdphmbnpkjflk/",
    // Chrome Remote Desktop - Official branding.
    "chrome-extension://gbchcmhmhahfdphkhkmpfmihenigjmpp/"};
#endif

std::string GetPrimaryAccountId(content::BrowserContext* context) {
  SigninManagerBase* signin_manager =
      SigninManagerFactory::GetForProfile(Profile::FromBrowserContext(context));
  return signin_manager->GetAuthenticatedAccountId();
}

}  // namespace

namespace identity = api::identity;

IdentityGetAuthTokenFunction::IdentityGetAuthTokenFunction()
    : OAuth2TokenService::Consumer("extensions_identity_api"),
      interactive_(false),
      should_prompt_for_scopes_(false),
      should_prompt_for_signin_(false) {
}

IdentityGetAuthTokenFunction::~IdentityGetAuthTokenFunction() {
  TRACE_EVENT_ASYNC_END0("identity", "IdentityGetAuthTokenFunction", this);
}

bool IdentityGetAuthTokenFunction::RunAsync() {
  TRACE_EVENT_ASYNC_BEGIN1("identity",
                           "IdentityGetAuthTokenFunction",
                           this,
                           "extension",
                           extension()->id());

  if (GetProfile()->IsOffTheRecord()) {
    error_ = identity_constants::kOffTheRecord;
    return false;
  }

  std::unique_ptr<identity::GetAuthToken::Params> params(
      identity::GetAuthToken::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());
  interactive_ = params->details.get() &&
      params->details->interactive.get() &&
      *params->details->interactive;

  should_prompt_for_scopes_ = interactive_;
  should_prompt_for_signin_ = interactive_;

  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension());

  // Check that the necessary information is present in the manifest.
  oauth2_client_id_ = GetOAuth2ClientId();
  if (oauth2_client_id_.empty()) {
    error_ = identity_constants::kInvalidClientId;
    return false;
  }

  std::set<std::string> scopes(oauth2_info.scopes.begin(),
                               oauth2_info.scopes.end());

  std::string account_key = GetPrimaryAccountId(GetProfile());

  if (params->details.get()) {
    if (params->details->account.get()) {
      std::string detail_key =
          extensions::IdentityAPI::GetFactoryInstance()
              ->Get(GetProfile())
              ->FindAccountKeyByGaiaId(params->details->account->id);

      if (detail_key != account_key) {
        if (detail_key.empty() || !switches::IsExtensionsMultiAccount()) {
          // TODO(courage): should this be a different error?
          error_ = identity_constants::kUserNotSignedIn;
          return false;
        }

        account_key = detail_key;
      }
    }

    if (params->details->scopes.get()) {
      scopes = std::set<std::string>(params->details->scopes->begin(),
                                     params->details->scopes->end());
    }
  }

  if (scopes.size() == 0) {
    error_ = identity_constants::kInvalidScopes;
    return false;
  }

  token_key_.reset(
      new ExtensionTokenKey(extension()->id(), account_key, scopes));

  // From here on out, results must be returned asynchronously.
  StartAsyncRun();

#if defined(OS_CHROMEOS)
  policy::BrowserPolicyConnectorChromeOS* connector =
      g_browser_process->platform_part()->browser_policy_connector_chromeos();
  bool is_kiosk = user_manager::UserManager::Get()->IsLoggedInAsKioskApp();
  bool is_public_session =
      user_manager::UserManager::Get()->IsLoggedInAsPublicAccount();

  if (connector->IsEnterpriseManaged() && (is_kiosk || is_public_session)) {
    if (is_public_session && !IsOriginWhitelistedInPublicSession()) {
      CompleteFunctionWithError(identity_constants::kUserNotSignedIn);
      return true;
    }

    StartMintTokenFlow(IdentityMintRequestQueue::MINT_TYPE_NONINTERACTIVE);
    return true;
  }
#endif

  if (!HasLoginToken()) {
    if (!should_prompt_for_signin_) {
      CompleteFunctionWithError(identity_constants::kUserNotSignedIn);
      return true;
    }
    // Display a login prompt.
    StartSigninFlow();
  } else {
    StartMintTokenFlow(IdentityMintRequestQueue::MINT_TYPE_NONINTERACTIVE);
  }

  return true;
}

void IdentityGetAuthTokenFunction::StartAsyncRun() {
  // Balanced in CompleteAsyncRun
  AddRef();
  extensions::IdentityAPI::GetFactoryInstance()
      ->Get(GetProfile())
      ->set_get_auth_token_function(this);
}

void IdentityGetAuthTokenFunction::CompleteAsyncRun(bool success) {
  extensions::IdentityAPI::GetFactoryInstance()
      ->Get(GetProfile())
      ->set_get_auth_token_function(nullptr);

  SendResponse(success);
  Release();  // Balanced in StartAsyncRun
}

void IdentityGetAuthTokenFunction::CompleteFunctionWithResult(
    const std::string& access_token) {
  SetResult(base::MakeUnique<base::StringValue>(access_token));
  CompleteAsyncRun(true);
}

void IdentityGetAuthTokenFunction::CompleteFunctionWithError(
    const std::string& error) {
  TRACE_EVENT_ASYNC_STEP_PAST1("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "CompleteFunctionWithError",
                               "error",
                               error);
  error_ = error;
  CompleteAsyncRun(false);
}

void IdentityGetAuthTokenFunction::StartSigninFlow() {
  // All cached tokens are invalid because the user is not signed in.
  IdentityAPI* id_api =
      extensions::IdentityAPI::GetFactoryInstance()->Get(GetProfile());
  id_api->EraseAllCachedTokens();
  // Display a login prompt. If the subsequent mint fails, don't display the
  // login prompt again.
  should_prompt_for_signin_ = false;
  ShowLoginPopup();
}

void IdentityGetAuthTokenFunction::StartMintTokenFlow(
    IdentityMintRequestQueue::MintType type) {
  mint_token_flow_type_ = type;

  // Flows are serialized to prevent excessive traffic to GAIA, and
  // to consolidate UI pop-ups.
  IdentityAPI* id_api =
      extensions::IdentityAPI::GetFactoryInstance()->Get(GetProfile());

  if (!should_prompt_for_scopes_) {
    // Caller requested no interaction.

    if (type == IdentityMintRequestQueue::MINT_TYPE_INTERACTIVE) {
      // GAIA told us to do a consent UI.
      CompleteFunctionWithError(identity_constants::kNoGrant);
      return;
    }
    if (!id_api->mint_queue()->empty(
            IdentityMintRequestQueue::MINT_TYPE_INTERACTIVE, *token_key_)) {
      // Another call is going through a consent UI.
      CompleteFunctionWithError(identity_constants::kNoGrant);
      return;
    }
  }
  id_api->mint_queue()->RequestStart(type, *token_key_, this);
}

void IdentityGetAuthTokenFunction::CompleteMintTokenFlow() {
  IdentityMintRequestQueue::MintType type = mint_token_flow_type_;

  extensions::IdentityAPI::GetFactoryInstance()
      ->Get(GetProfile())
      ->mint_queue()
      ->RequestComplete(type, *token_key_, this);
}

void IdentityGetAuthTokenFunction::StartMintToken(
    IdentityMintRequestQueue::MintType type) {
  TRACE_EVENT_ASYNC_STEP_PAST1("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "StartMintToken",
                               "type",
                               type);

  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension());
  IdentityAPI* id_api = IdentityAPI::GetFactoryInstance()->Get(GetProfile());
  IdentityTokenCacheValue cache_entry = id_api->GetCachedToken(*token_key_);
  IdentityTokenCacheValue::CacheValueStatus cache_status =
      cache_entry.status();

  if (type == IdentityMintRequestQueue::MINT_TYPE_NONINTERACTIVE) {
    switch (cache_status) {
      case IdentityTokenCacheValue::CACHE_STATUS_NOTFOUND:
#if defined(OS_CHROMEOS)
        // Always force minting token for ChromeOS kiosk app and public session.
        if (user_manager::UserManager::Get()->IsLoggedInAsPublicAccount() &&
            !IsOriginWhitelistedInPublicSession()) {
          CompleteFunctionWithError(identity_constants::kUserNotSignedIn);
          return;
        }

        if (user_manager::UserManager::Get()->IsLoggedInAsKioskApp() ||
            user_manager::UserManager::Get()->IsLoggedInAsPublicAccount()) {
          gaia_mint_token_mode_ = OAuth2MintTokenFlow::MODE_MINT_TOKEN_FORCE;
          policy::BrowserPolicyConnectorChromeOS* connector =
              g_browser_process->platform_part()
                  ->browser_policy_connector_chromeos();
          if (connector->IsEnterpriseManaged()) {
            StartDeviceLoginAccessTokenRequest();
          } else {
            StartLoginAccessTokenRequest();
          }
          return;
        }
#endif

        if (oauth2_info.auto_approve)
          // oauth2_info.auto_approve is protected by a whitelist in
          // _manifest_features.json hence only selected extensions take
          // advantage of forcefully minting the token.
          gaia_mint_token_mode_ = OAuth2MintTokenFlow::MODE_MINT_TOKEN_FORCE;
        else
          gaia_mint_token_mode_ = OAuth2MintTokenFlow::MODE_MINT_TOKEN_NO_FORCE;
        StartLoginAccessTokenRequest();
        break;

      case IdentityTokenCacheValue::CACHE_STATUS_TOKEN:
        CompleteMintTokenFlow();
        CompleteFunctionWithResult(cache_entry.token());
        break;

      case IdentityTokenCacheValue::CACHE_STATUS_ADVICE:
        CompleteMintTokenFlow();
        should_prompt_for_signin_ = false;
        issue_advice_ = cache_entry.issue_advice();
        StartMintTokenFlow(IdentityMintRequestQueue::MINT_TYPE_INTERACTIVE);
        break;
    }
  } else {
    DCHECK(type == IdentityMintRequestQueue::MINT_TYPE_INTERACTIVE);

    if (cache_status == IdentityTokenCacheValue::CACHE_STATUS_TOKEN) {
      CompleteMintTokenFlow();
      CompleteFunctionWithResult(cache_entry.token());
    } else {
      ShowOAuthApprovalDialog(issue_advice_);
    }
  }
}

void IdentityGetAuthTokenFunction::OnMintTokenSuccess(
    const std::string& access_token, int time_to_live) {
  TRACE_EVENT_ASYNC_STEP_PAST0("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "OnMintTokenSuccess");

  IdentityTokenCacheValue token(access_token,
                                base::TimeDelta::FromSeconds(time_to_live));
  IdentityAPI::GetFactoryInstance()->Get(GetProfile())->SetCachedToken(
      *token_key_, token);

  CompleteMintTokenFlow();
  CompleteFunctionWithResult(access_token);
}

void IdentityGetAuthTokenFunction::OnMintTokenFailure(
    const GoogleServiceAuthError& error) {
  TRACE_EVENT_ASYNC_STEP_PAST1("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "OnMintTokenFailure",
                               "error",
                               error.ToString());
  CompleteMintTokenFlow();
  switch (error.state()) {
    case GoogleServiceAuthError::SERVICE_ERROR:
      if (interactive_) {
        StartSigninFlow();
        return;
      }
      break;
    case GoogleServiceAuthError::INVALID_GAIA_CREDENTIALS:
    case GoogleServiceAuthError::ACCOUNT_DELETED:
    case GoogleServiceAuthError::ACCOUNT_DISABLED:
      // TODO(courage): flush ticket and retry once
      if (should_prompt_for_signin_) {
        // Display a login prompt and try again (once).
        StartSigninFlow();
        return;
      }
      break;
    default:
      // Return error to caller.
      break;
  }

  CompleteFunctionWithError(
      std::string(identity_constants::kAuthFailure) + error.ToString());
}

void IdentityGetAuthTokenFunction::OnIssueAdviceSuccess(
    const IssueAdviceInfo& issue_advice) {
  TRACE_EVENT_ASYNC_STEP_PAST0("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "OnIssueAdviceSuccess");

  IdentityAPI::GetFactoryInstance()->Get(GetProfile())->SetCachedToken(
      *token_key_, IdentityTokenCacheValue(issue_advice));
  CompleteMintTokenFlow();

  should_prompt_for_signin_ = false;
  // Existing grant was revoked and we used NO_FORCE, so we got info back
  // instead. Start a consent UI if we can.
  issue_advice_ = issue_advice;
  StartMintTokenFlow(IdentityMintRequestQueue::MINT_TYPE_INTERACTIVE);
}

void IdentityGetAuthTokenFunction::SigninSuccess() {
  TRACE_EVENT_ASYNC_STEP_PAST0("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "SigninSuccess");

  // If there was no account associated this profile before the
  // sign-in, we may not have an account_id in the token_key yet.
  if (token_key_->account_id.empty()) {
    token_key_->account_id = GetPrimaryAccountId(GetProfile());
  }

  StartMintTokenFlow(IdentityMintRequestQueue::MINT_TYPE_NONINTERACTIVE);
}

void IdentityGetAuthTokenFunction::SigninFailed() {
  TRACE_EVENT_ASYNC_STEP_PAST0("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "SigninFailed");
  CompleteFunctionWithError(identity_constants::kUserNotSignedIn);
}

void IdentityGetAuthTokenFunction::OnGaiaFlowFailure(
    GaiaWebAuthFlow::Failure failure,
    GoogleServiceAuthError service_error,
    const std::string& oauth_error) {
  CompleteMintTokenFlow();
  std::string error;

  switch (failure) {
    case GaiaWebAuthFlow::WINDOW_CLOSED:
      error = identity_constants::kUserRejected;
      break;

    case GaiaWebAuthFlow::INVALID_REDIRECT:
      error = identity_constants::kInvalidRedirect;
      break;

    case GaiaWebAuthFlow::SERVICE_AUTH_ERROR:
      // If this is really an authentication error and not just a transient
      // network error, and this is an interactive request for a signed-in
      // user, then we show signin UI instead of failing.
      if (service_error.state() != GoogleServiceAuthError::CONNECTION_FAILED &&
          service_error.state() !=
              GoogleServiceAuthError::SERVICE_UNAVAILABLE &&
          interactive_ && HasLoginToken()) {
        StartSigninFlow();
        return;
      }
      error = std::string(identity_constants::kAuthFailure) +
          service_error.ToString();
      break;

    case GaiaWebAuthFlow::OAUTH_ERROR:
      error = MapOAuth2ErrorToDescription(oauth_error);
      break;

    case GaiaWebAuthFlow::LOAD_FAILED:
      error = identity_constants::kPageLoadFailure;
      break;

    default:
      NOTREACHED() << "Unexpected error from gaia web auth flow: " << failure;
      error = identity_constants::kInvalidRedirect;
      break;
  }

  CompleteFunctionWithError(error);
}

void IdentityGetAuthTokenFunction::OnGaiaFlowCompleted(
    const std::string& access_token,
    const std::string& expiration) {
  TRACE_EVENT_ASYNC_STEP_PAST0("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "OnGaiaFlowCompleted");
  int time_to_live;
  if (!expiration.empty() && base::StringToInt(expiration, &time_to_live)) {
    IdentityTokenCacheValue token_value(
        access_token, base::TimeDelta::FromSeconds(time_to_live));
    IdentityAPI::GetFactoryInstance()->Get(GetProfile())->SetCachedToken(
        *token_key_, token_value);
  }

  CompleteMintTokenFlow();
  CompleteFunctionWithResult(access_token);
}

void IdentityGetAuthTokenFunction::OnGetTokenSuccess(
    const OAuth2TokenService::Request* request,
    const std::string& access_token,
    const base::Time& expiration_time) {
  TRACE_EVENT_ASYNC_STEP_PAST1("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "OnGetTokenSuccess",
                               "account",
                               request->GetAccountId());
  login_token_request_.reset();
  StartGaiaRequest(access_token);
}

void IdentityGetAuthTokenFunction::OnGetTokenFailure(
    const OAuth2TokenService::Request* request,
    const GoogleServiceAuthError& error) {
  TRACE_EVENT_ASYNC_STEP_PAST1("identity",
                               "IdentityGetAuthTokenFunction",
                               this,
                               "OnGetTokenFailure",
                               "error",
                               error.ToString());
  login_token_request_.reset();
  OnGaiaFlowFailure(GaiaWebAuthFlow::SERVICE_AUTH_ERROR, error, std::string());
}

void IdentityGetAuthTokenFunction::Shutdown() {
  gaia_web_auth_flow_.reset();
  signin_flow_.reset();
  login_token_request_.reset();
  extensions::IdentityAPI::GetFactoryInstance()
      ->Get(GetProfile())
      ->mint_queue()
      ->RequestCancel(*token_key_, this);
  CompleteFunctionWithError(identity_constants::kCanceled);
}

#if defined(OS_CHROMEOS)
void IdentityGetAuthTokenFunction::StartDeviceLoginAccessTokenRequest() {
  chromeos::DeviceOAuth2TokenService* service =
      chromeos::DeviceOAuth2TokenServiceFactory::Get();
  // Since robot account refresh tokens are scoped down to [any-api] only,
  // request access token for [any-api] instead of login.
  OAuth2TokenService::ScopeSet scopes;
  scopes.insert(GaiaConstants::kAnyApiOAuth2Scope);
  login_token_request_ =
      service->StartRequest(service->GetRobotAccountId(),
                            scopes,
                            this);
}

bool IdentityGetAuthTokenFunction::IsOriginWhitelistedInPublicSession() {
  DCHECK(extension());
  GURL extension_url = extension()->url();
  for (size_t i = 0; i < arraysize(kPublicSessionAllowedOrigins); i++) {
    URLPattern allowed_origin(URLPattern::SCHEME_ALL,
                              kPublicSessionAllowedOrigins[i]);
    if (allowed_origin.MatchesSecurityOrigin(extension_url)) {
      return true;
    }
  }
  return false;
}
#endif

void IdentityGetAuthTokenFunction::StartLoginAccessTokenRequest() {
  ProfileOAuth2TokenService* service =
      ProfileOAuth2TokenServiceFactory::GetForProfile(GetProfile());
#if defined(OS_CHROMEOS)
  if (chrome::IsRunningInForcedAppMode()) {
    std::string app_client_id;
    std::string app_client_secret;
    if (chromeos::UserSessionManager::GetInstance()->
            GetAppModeChromeClientOAuthInfo(&app_client_id,
                                            &app_client_secret)) {
      login_token_request_ =
          service->StartRequestForClient(token_key_->account_id,
                                         app_client_id,
                                         app_client_secret,
                                         OAuth2TokenService::ScopeSet(),
                                         this);
      return;
    }
  }
#endif
  login_token_request_ = service->StartRequest(
      token_key_->account_id, OAuth2TokenService::ScopeSet(), this);
}

void IdentityGetAuthTokenFunction::StartGaiaRequest(
    const std::string& login_access_token) {
  DCHECK(!login_access_token.empty());
  mint_token_flow_.reset(CreateMintTokenFlow());
  mint_token_flow_->Start(GetProfile()->GetRequestContext(),
                          login_access_token);
}

void IdentityGetAuthTokenFunction::ShowLoginPopup() {
  signin_flow_.reset(new IdentitySigninFlow(this, GetProfile()));
  signin_flow_->Start();
}

void IdentityGetAuthTokenFunction::ShowOAuthApprovalDialog(
    const IssueAdviceInfo& issue_advice) {
  const std::string locale = extension_l10n_util::CurrentLocaleOrDefault();

  gaia_web_auth_flow_.reset(new GaiaWebAuthFlow(
      this, GetProfile(), token_key_.get(), oauth2_client_id_, locale));
  gaia_web_auth_flow_->Start();
}

OAuth2MintTokenFlow* IdentityGetAuthTokenFunction::CreateMintTokenFlow() {
  SigninClient* signin_client =
      ChromeSigninClientFactory::GetForProfile(GetProfile());
  std::string signin_scoped_device_id =
      signin_client->GetSigninScopedDeviceId();
  OAuth2MintTokenFlow* mint_token_flow = new OAuth2MintTokenFlow(
      this,
      OAuth2MintTokenFlow::Parameters(
          extension()->id(),
          oauth2_client_id_,
          std::vector<std::string>(token_key_->scopes.begin(),
                                   token_key_->scopes.end()),
          signin_scoped_device_id,
          gaia_mint_token_mode_));
  return mint_token_flow;
}

bool IdentityGetAuthTokenFunction::HasLoginToken() const {
  ProfileOAuth2TokenService* token_service =
      ProfileOAuth2TokenServiceFactory::GetForProfile(GetProfile());
  return token_service->RefreshTokenIsAvailable(token_key_->account_id);
}

std::string IdentityGetAuthTokenFunction::MapOAuth2ErrorToDescription(
    const std::string& error) {
  const char kOAuth2ErrorAccessDenied[] = "access_denied";
  const char kOAuth2ErrorInvalidScope[] = "invalid_scope";

  if (error == kOAuth2ErrorAccessDenied)
    return std::string(identity_constants::kUserRejected);
  else if (error == kOAuth2ErrorInvalidScope)
    return std::string(identity_constants::kInvalidScopes);
  else
    return std::string(identity_constants::kAuthFailure) + error;
}

std::string IdentityGetAuthTokenFunction::GetOAuth2ClientId() const {
  const OAuth2Info& oauth2_info = OAuth2Info::GetOAuth2Info(extension());
  std::string client_id = oauth2_info.client_id;

  // Component apps using auto_approve may use Chrome's client ID by
  // omitting the field.
  if (client_id.empty() && extension()->location() == Manifest::COMPONENT &&
      oauth2_info.auto_approve) {
    client_id = GaiaUrls::GetInstance()->oauth2_chrome_client_id();
  }
  return client_id;
}

}  // namespace extensions
