if(NOT OAUTH_CONFIG_MODULE OR NOT EXISTS "${OAUTH_CONFIG_MODULE}")
  message(FATAL_ERROR "OAUTH_CONFIG_MODULE is required")
endif()

include("${OAUTH_CONFIG_MODULE}")

set(publisher_json [=[
{
  "youtube": {
    "clientId": "publisher-client-id-1234567890.apps.googleusercontent.com",
    "clientSecret": "publisher_secret_1234567890"
  }
}
]=])
dsk_parse_youtube_oauth_config(
  "${publisher_json}"
  publisher_client_id
  publisher_client_secret
  publisher_format
)
if(NOT publisher_format STREQUAL "publisher" OR
   NOT publisher_client_id STREQUAL "publisher-client-id-1234567890.apps.googleusercontent.com" OR
   NOT publisher_client_secret STREQUAL "publisher_secret_1234567890")
  message(FATAL_ERROR "Publisher OAuth config format was not parsed correctly")
endif()

set(google_installed_json [=[
{
  "installed": {
    "client_id": "google-desktop-client-1234567890.apps.googleusercontent.com",
    "project_id": "example-project",
    "auth_uri": "https://accounts.google.com/o/oauth2/auth",
    "token_uri": "https://oauth2.googleapis.com/token",
    "client_secret": "google_secret_1234567890",
    "redirect_uris": ["http://localhost"]
  }
}
]=])
dsk_parse_youtube_oauth_config(
  "${google_installed_json}"
  google_client_id
  google_client_secret
  google_format
)
if(NOT google_format STREQUAL "google-installed" OR
   NOT google_client_id STREQUAL "google-desktop-client-1234567890.apps.googleusercontent.com" OR
   NOT google_client_secret STREQUAL "google_secret_1234567890")
  message(FATAL_ERROR "Google installed OAuth config format was not parsed correctly")
endif()
