function(dsk_parse_youtube_oauth_config config_json out_client_id out_client_secret out_format)
  string(JSON publisher_client_id ERROR_VARIABLE publisher_client_id_error
    GET "${config_json}" youtube clientId)
  string(JSON publisher_client_secret ERROR_VARIABLE publisher_client_secret_error
    GET "${config_json}" youtube clientSecret)

  if(publisher_client_id_error STREQUAL "NOTFOUND" AND
     publisher_client_secret_error STREQUAL "NOTFOUND")
    set(client_id "${publisher_client_id}")
    set(client_secret "${publisher_client_secret}")
    set(config_format "publisher")
  else()
    string(JSON installed_client_id ERROR_VARIABLE installed_client_id_error
      GET "${config_json}" installed client_id)
    string(JSON installed_client_secret ERROR_VARIABLE installed_client_secret_error
      GET "${config_json}" installed client_secret)
    if(NOT installed_client_id_error STREQUAL "NOTFOUND" OR
       NOT installed_client_secret_error STREQUAL "NOTFOUND")
      message(FATAL_ERROR
        "DSK_OAUTH_APP_CONFIG must contain youtube.clientId/youtube.clientSecret "
        "or installed.client_id/installed.client_secret")
    endif()

    set(client_id "${installed_client_id}")
    set(client_secret "${installed_client_secret}")
    set(config_format "google-installed")
  endif()

  string(LENGTH "${client_id}" client_id_length)
  string(LENGTH "${client_secret}" client_secret_length)
  if(client_id_length LESS 20 OR client_id_length GREATER 300 OR
     NOT client_id MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "YouTube OAuth client ID in DSK_OAUTH_APP_CONFIG has an invalid format")
  endif()
  if(client_secret_length LESS 10 OR client_secret_length GREATER 300 OR
     NOT client_secret MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR "YouTube OAuth client secret in DSK_OAUTH_APP_CONFIG has an invalid format")
  endif()

  set("${out_client_id}" "${client_id}" PARENT_SCOPE)
  set("${out_client_secret}" "${client_secret}" PARENT_SCOPE)
  set("${out_format}" "${config_format}" PARENT_SCOPE)
endfunction()

function(dsk_load_youtube_oauth_config config_path out_client_id out_client_secret out_format)
  if(NOT EXISTS "${config_path}")
    message(FATAL_ERROR "DSK_OAUTH_APP_CONFIG does not exist: ${config_path}")
  endif()

  file(READ "${config_path}" config_json)
  dsk_parse_youtube_oauth_config(
    "${config_json}"
    parsed_client_id
    parsed_client_secret
    parsed_format
  )
  set("${out_client_id}" "${parsed_client_id}" PARENT_SCOPE)
  set("${out_client_secret}" "${parsed_client_secret}" PARENT_SCOPE)
  set("${out_format}" "${parsed_format}" PARENT_SCOPE)
endfunction()
