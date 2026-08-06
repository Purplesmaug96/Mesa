# Mesa3D - src/loader/ (mirror of src/loader/meson.build)
# SPDX-License-Identifier: MIT

set(inc_loader "${CMAKE_CURRENT_SOURCE_DIR}")

set(wayland_protocols_xml)
if(with_platform_wayland)
  pkg_check_modules(PC_WAYLAND_PROTOCOLS IMPORTED_TARGET wayland-protocols)
  if(PC_WAYLAND_PROTOCOLS_FOUND)
    execute_process(COMMAND pkg-config --variable=pkgdatadir wayland-protocols
      OUTPUT_VARIABLE _wlp_dir OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_wlp
      "staging/fifo/fifo-v1.xml"
      "staging/commit-timing/commit-timing-v1.xml"
      "unstable/linux-dmabuf/linux-dmabuf-unstable-v1.xml"
      "stable/presentation-time/presentation-time.xml"
      "staging/tearing-control/tearing-control-v1.xml"
      "staging/linux-drm-syncobj/linux-drm-syncobj-v1.xml"
      "staging/color-management/color-management-v1.xml")
    set(_wlp_targets)
    foreach(_xml IN LISTS _wlp)
      get_filename_component(_base "${_xml}" NAME_WE)
      string(REPLACE ".xml" "" _stem "${_base}")
      set(_hdr_client "${CMAKE_BINARY_DIR}/src/loader/${_stem}-client-protocol.h")
      set(_hdr_server "${CMAKE_BINARY_DIR}/src/loader/${_stem}-server-protocol.h")
      set(_src "${CMAKE_BINARY_DIR}/src/loader/${_stem}-protocol.c")
      add_custom_command(
        OUTPUT "${_hdr_client}" "${_hdr_server}" "${_src}"
        COMMAND ${MESA_WAYLAND_SCANNER} client-header "${_wlp_dir}/${_xml}" "${_hdr_client}"
        COMMAND ${MESA_WAYLAND_SCANNER} server-header "${_wlp_dir}/${_xml}" "${_hdr_server}"
        COMMAND ${MESA_WAYLAND_SCANNER} private-code "${_wlp_dir}/${_xml}" "${_src}"
        DEPENDS "${_wlp_dir}/${_xml}"
        COMMENT "wayland protocol: ${_stem}")
      add_custom_target(gen_wp_${_stem} ALL DEPENDS "${_hdr_client}" "${_hdr_server}" "${_src}")
      list(APPEND _wlp_targets gen_wp_${_stem})
    endforeach()
    set(_presentation_h "${CMAKE_BINARY_DIR}/src/loader/presentation-time-client-protocol.h")
    set(_presentation_c "${CMAKE_BINARY_DIR}/src/loader/presentation-time-protocol.c")
    add_library(loader_wayland_helper STATIC
      "${CMAKE_CURRENT_SOURCE_DIR}/loader_wayland_helper.c" "${_presentation_c}")
    set_target_properties(loader_wayland_helper PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
    target_include_directories(loader_wayland_helper PRIVATE
      "${inc_include}" "${inc_src}" "${inc_gallium}" "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/src/loader")
    target_link_libraries(loader_wayland_helper PRIVATE idep_mesautil ${dep_wayland_client})
    add_dependencies(loader_wayland_helper ${_wlp_targets})
  endif()
endif()

# libloader
add_library(loader STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/loader_dri_helper.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/loader.c"
  "${CMAKE_BINARY_DIR}/src/git_sha1.h")
set_target_properties(loader PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden EXCLUDE_FROM_ALL ON)
target_compile_definitions(loader PRIVATE -DUSE_DRICONF)
target_include_directories(loader PRIVATE
  "${inc_include}" "${inc_src}" "${inc_util}" "${inc_gallium}" "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/src")
target_link_libraries(loader PRIVATE idep_mesautil ${dep_libdrm} ${dep_thread} ${dep_xcb} ${dep_xcb_xrandr})
add_dependencies(loader gen_git_sha1)
set(inc_loader "${inc_loader}" PARENT_SCOPE)
