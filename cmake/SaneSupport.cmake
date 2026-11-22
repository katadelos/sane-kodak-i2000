find_package(Patch REQUIRED)

set(sane_upstream "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sane-backends")
set(sane_build "${CMAKE_CURRENT_BINARY_DIR}/sane-support")
set(sane_include "${sane_build}/include")
set(endpoint_patch "${CMAKE_CURRENT_LIST_DIR}/patches/sane-explicit-bulk-endpoint.patch")

if(NOT EXISTS "${sane_upstream}/sanei/sanei_usb.c")
  message(FATAL_ERROR "Initialize the SANE submodule with git submodule update --init")
endif()

# Assemble the private SANE helpers in the build tree. Keep upstream's relative
# include layout so its source files can use the standalone configuration.
configure_file("${CMAKE_CURRENT_LIST_DIR}/sane-config.h"
               "${sane_include}/sane/config.h" COPYONLY)
foreach(header sanei sanei_backend sanei_config sanei_debug sanei_usb)
  configure_file("${sane_upstream}/include/sane/${header}.h"
                 "${sane_include}/sane/${header}.h" COPYONLY)
endforeach()

set(sane_sources)
foreach(module sanei_usb sanei_config sanei_constrain_value sanei_init_debug)
  set(source "sanei/${module}.c")
  configure_file("${sane_upstream}/${source}" "${sane_build}/${source}" COPYONLY)
  list(APPEND sane_sources "${sane_build}/${source}")
endforeach()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${endpoint_patch}")
execute_process(COMMAND "${Patch_EXECUTABLE}" -p1 -i "${endpoint_patch}"
                WORKING_DIRECTORY "${sane_build}"
                RESULT_VARIABLE patch_status OUTPUT_VARIABLE patch_output)
if(NOT patch_status EQUAL 0)
  message(FATAL_ERROR "Failed to apply the SANE duplex USB patch:\n${patch_output}")
endif()

add_library(kodak_support OBJECT ${sane_sources})
set_target_properties(kodak_support PROPERTIES POSITION_INDEPENDENT_CODE ON
                      C_VISIBILITY_PRESET hidden)
target_include_directories(kodak_support PRIVATE "${sane_include}")
target_link_libraries(kodak_support PRIVATE PkgConfig::SANE PkgConfig::USB)
target_compile_definitions(kodak_support PRIVATE
    PATH_SANE_CONFIG_DIR=${CMAKE_INSTALL_FULL_SYSCONFDIR}/sane.d)
