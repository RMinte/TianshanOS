# Inspect the effective IDF configuration, not sdkconfig.defaults.
if(NOT TS_DEVELOPMENT_BUILD AND NOT ROOT_PASSWORD_RESET AND CONFIG_TS_WEBUI_ENABLE)
    if(NOT CONFIG_TS_WEBUI_AUTH_REQUIRED OR NOT CONFIG_TS_SECURITY_ENABLE)
        message(FATAL_ERROR "Standard WebUI firmware requires authentication and security. Use explicit TS_DEVELOPMENT_BUILD=ON only for development firmware.")
    endif()
endif()
foreach(flag TS_DEVELOPMENT_BUILD ROOT_PASSWORD_RESET CONFIG_TS_WEBUI_ENABLE CONFIG_TS_WEBUI_AUTH_REQUIRED CONFIG_TS_SECURITY_ENABLE )
    if(${flag})
        set(profile_${flag} true)
    else()
        set(profile_${flag} false)
    endif()
endforeach()
set(profile_json [=[{
  "schema": 2,
  "development": @profile_TS_DEVELOPMENT_BUILD@,
  "recovery": @profile_ROOT_PASSWORD_RESET@,
  "webui_enabled": @profile_CONFIG_TS_WEBUI_ENABLE@,
  "auth_required": @profile_CONFIG_TS_WEBUI_AUTH_REQUIRED@,
  "security_enabled": @profile_CONFIG_TS_SECURITY_ENABLE@,
  "webui_tls": false
}
]=])
string(CONFIGURE "${profile_json}" configured_profile @ONLY)
file(WRITE "${CMAKE_BINARY_DIR}/security-build.json" "${configured_profile}")
