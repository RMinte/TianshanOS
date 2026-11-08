# Change only this project's HTTPD objects, never the shared ESP-IDF checkout.
idf_component_get_property(httpd_lib esp_http_server COMPONENT_LIB)
idf_component_get_property(httpd_dir esp_http_server COMPONENT_DIR)
set(admission_dir "${CMAKE_BINARY_DIR}/httpd_admission")
set(admission_tool "${CMAKE_SOURCE_DIR}/tools/prepare_httpd_admission.py")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${admission_tool}"
            --idf-dir "$ENV{IDF_PATH}" --output "${admission_dir}"
            --trusted-origins "${TS_HTTP_TRUSTED_ORIGINS}" --with-https
    RESULT_VARIABLE admission_result
    ERROR_VARIABLE admission_error
)
if(NOT admission_result EQUAL 0)
    message(FATAL_ERROR "HTTPD connection protection unavailable: ${admission_error}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${admission_tool}" "${httpd_dir}/src/httpd_sess.c" "${httpd_dir}/src/httpd_main.c"
    "${httpd_dir}/src/httpd_uri.c" "${httpd_dir}/src/httpd_parse.c"
    "${httpd_dir}/src/httpd_txrx.c" "${httpd_dir}/src/httpd_ws.c")
get_target_property(httpd_sources ${httpd_lib} SOURCES)
foreach(name httpd_sess.c httpd_main.c httpd_uri.c httpd_parse.c httpd_txrx.c httpd_ws.c)
    set(found FALSE)
    set(updated_sources "")
    foreach(source IN LISTS httpd_sources)
        if(source STREQUAL "${httpd_dir}/src/${name}" OR source STREQUAL "src/${name}")
            list(APPEND updated_sources "${admission_dir}/${name}")
            set(found TRUE)
        else()
            list(APPEND updated_sources "${source}")
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "Cannot replace HTTPD ${name}; SDK component sources changed")
    endif()
    set(httpd_sources ${updated_sources})
endforeach()
set_property(TARGET ${httpd_lib} PROPERTY SOURCES ${httpd_sources})
target_include_directories(${httpd_lib} PRIVATE "${httpd_dir}/src")

# The same build-local server policy owns copied TLS startup material.
idf_component_get_property(https_lib esp_https_server COMPONENT_LIB)
idf_component_get_property(https_dir esp_https_server COMPONENT_DIR)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${https_dir}/src/https_server.c")
get_target_property(https_sources ${https_lib} SOURCES)
set(updated_https_sources "")
set(found_https FALSE)
foreach(source IN LISTS https_sources)
    if(source STREQUAL "${https_dir}/src/https_server.c" OR source STREQUAL "src/https_server.c")
        list(APPEND updated_https_sources "${admission_dir}/https_server.c")
        set(found_https TRUE)
    else()
        list(APPEND updated_https_sources "${source}")
    endif()
endforeach()
if(NOT found_https)
    message(FATAL_ERROR "Cannot replace HTTPS startup owner; SDK component sources changed")
endif()
set_property(TARGET ${https_lib} PROPERTY SOURCES ${updated_https_sources})
target_include_directories(${https_lib} PRIVATE "${https_dir}/src")
