# Mesa3D CMake port - helper macros mirroring meson idioms.
# Copyright © 2026
# SPDX-License-Identifier: MIT

# --- Tri-state feature option (meson 'feature' type) ---
# mesa_feature(<name> <default: auto|enabled|disabled> <description>)
# Creates cache var MESA_<name> = AUTO|ON|OFF and sets <name>=ON/OFF
function(mesa_feature NAME DEFAULT DESC)
  if("${DEFAULT}" STREQUAL "enabled")
    set(_def ON)
  elseif("${DEFAULT}" STREQUAL "disabled")
    set(_def OFF)
  else()
    set(_def AUTO)
  endif()
  set(_doc "${DESC} (mesa_feature: auto/enabled/disabled)")
  set(MESA_${NAME} "${_def}" CACHE STRING "${_doc}")
  set_property(CACHE MESA_${NAME} PROPERTY STRINGS "AUTO" "ON" "OFF" "auto" "enabled" "disabled")
  # normalize
  set(_val "${MESA_${NAME}}")
  if(_val MATCHES "^(AUTO|auto|$)")
    set(${NAME} AUTO PARENT_SCOPE)
  elseif(_val)
    set(${NAME} ON PARENT_SCOPE)
  else()
    set(${NAME} OFF PARENT_SCOPE)
  endif()
endfunction()

# Tri-state enable/disable with conditions, mimicking meson feature().require().
# mesa_require(<var> <condition> <error-message>)
# If <var>==ON and condition is false -> message(FATAL_ERROR msg)
# If <var>==AUTO and condition is false -> <var>=OFF
function(mesa_require VAR COND MSG)
  if(NOT COND)
    if(${VAR} STREQUAL "ON")
      message(FATAL_ERROR "mesa: ${MSG}")
    elseif(${VAR} STREQUAL "AUTO")
      set(${VAR} OFF PARENT_SCOPE)
    endif()
  endif()
endfunction()

# Like mesa_require but disables AUTO only when condition true (meson disable_auto_if)
function(mesa_disable_auto_if VAR COND)
  if(${VAR} STREQUAL "AUTO" AND COND)
    set(${VAR} OFF PARENT_SCOPE)
  endif()
endfunction()

# meson enable_if: if condition is false and var is ON -> error; if var is AUTO and cond -> ON
function(mesa_enable_if VAR COND MSG)
  if(NOT COND)
    if(${VAR} STREQUAL "ON")
      message(FATAL_ERROR "mesa: ${MSG}")
    endif()
  else()
    if(${VAR} STREQUAL "AUTO")
      set(${VAR} ON PARENT_SCOPE)
    endif()
  endif()
endfunction()

# Returns MESA_<NAME>_FOUND-style detection result for a feature that was never
# force-enabled: e.g. mesa_feature_found(VAR) -> VAR set to ON/OFF (AUTO->OFF).
function(mesa_feature_found VAR)
  if(${VAR} STREQUAL "ON")
    set(${VAR} ON PARENT_SCOPE)
  else()
    set(${VAR} OFF PARENT_SCOPE)
  endif()
endfunction()

# --- include directory variables (meson include_directories) ---
# mesa_inc(<var> <dir1> [dir2...]) appends dirs relative to current source dir
function(mesa_inc VAR)
  foreach(_d IN LISTS ARGN)
    list(APPEND ${VAR} "${CMAKE_CURRENT_SOURCE_DIR}/${_d}")
  endforeach()
  set(${VAR} "${${VAR}}" PARENT_SCOPE)
endfunction()

# --- Per-target common setup ---
# Mirrors: meson adds no global -I except what we list; generated files are in
# the build root mirror (see MESA_GEN_DIR conventions), so every target gets
# -I${CMAKE_BINARY_DIR} so quoted/#include of generated files resolve.
function(mesa_target_common TARGET)
  if(NOT MESA_NO_HIDE_${TARGET})
    set_target_properties(${TARGET} PROPERTIES
      C_VISIBILITY_PRESET hidden
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES)
  endif()
  target_include_directories(${TARGET} PRIVATE "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/src")
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${TARGET} PRIVATE -fno-math-errno -fno-trapping-math)
  endif()
endfunction()

# --- static_library wrapper ---
# mesa_static(<name>
#   SOURCES <list>          (source files relative to current dir, or absolute)
#   [INC <list>]            include dirs
#   [DEP <list>]            linked/interface deps (targets)
#   [GEN <list>]            custom targets that must be built first
#   [C_ARGS <list>]         extra C compile options
#   [CXX_ARGS <list>]       extra CXX compile options
#   [LINK <list>]           raw link libs
#   [VISIBLE]               do not hide symbols
#   [NO_PIC]                disable PIC (meson static_library default is pic=true)
# )
function(mesa_static NAME)
  cmake_parse_arguments(M "VISIBLE;NO_PIC" "" "SOURCES;INC;DEP;GEN;C_ARGS;CXX_ARGS;LINK" ${ARGN})
  if(NOT M_SOURCES)
    message(FATAL_ERROR "mesa_static(${NAME}): no SOURCES")
  endif()
  set(_srcs ${M_SOURCES})
  set(_srcs_abs)
  foreach(_s IN LISTS _srcs)
    if(IS_ABSOLUTE "${_s}")
      list(APPEND _srcs_abs "${_s}")
    else()
      list(APPEND _srcs_abs "${CMAKE_CURRENT_SOURCE_DIR}/${_s}")
    endif()
  endforeach()
  if(M_VISIBLE)
    set(MESA_NO_HIDE_${NAME} ON)
  endif()
  add_library(${NAME} STATIC ${_srcs_abs})
  if(M_NO_PIC OR NOT _mesa_need_pic)
    set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
  else()
    set_target_properties(${NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
  if(NOT M_VISIBLE)
    set_target_properties(${NAME} PROPERTIES C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
  endif()
  target_include_directories(${NAME} PRIVATE ${M_INC} "${CMAKE_BINARY_DIR}" "${CMAKE_BINARY_DIR}/src")
  if(M_DEP)
    target_link_libraries(${NAME} PRIVATE ${M_DEP})
  endif()
  if(M_C_ARGS)
    target_compile_options(${NAME} PRIVATE $<$<COMPILE_LANGUAGE:C>:${M_C_ARGS}>)
  endif()
  if(M_CXX_ARGS)
    target_compile_options(${NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${M_CXX_ARGS}>)
  endif()
  if(M_LINK)
    target_link_libraries(${NAME} PRIVATE ${M_LINK})
  endif()
  foreach(_g IN LISTS M_GEN)
    add_dependencies(${NAME} ${_g})
  endforeach()
  set_target_properties(${NAME} PROPERTIES EXCLUDE_FROM_ALL ON)
  # pass the target name out
  set(MESA_LAST_TARGET "${NAME}" PARENT_SCOPE)
endfunction()

# --- declare_dependency wrapper (idep_*) ---
# mesa_idep(<name>
#   [LINK <list>]      libs to link
#   [INC <list>]       include dirs
#   [C_ARGS <list>]    compile args (definitions/options) applied to consumers
#   [SOURCES <list>]   generated sources attached to consumers
#   [DEP <list>]       deps (link)
#   [GEN <list>]       custom targets to order before consumers
# )
function(mesa_idep NAME)
  cmake_parse_arguments(M "" "" "LINK;INC;C_ARGS;SOURCES;DEP;GEN" ${ARGN})
  add_library(${NAME} INTERFACE)
  if(M_LINK)
    target_link_libraries(${NAME} INTERFACE ${M_LINK})
  endif()
  if(M_INC)
    target_include_directories(${NAME} INTERFACE ${M_INC})
  endif()
  if(M_C_ARGS)
    target_compile_options(${NAME} INTERFACE ${M_C_ARGS})
  endif()
  if(M_DEP)
    target_link_libraries(${NAME} INTERFACE ${M_DEP})
  endif()
  if(M_SOURCES)
    target_sources(${NAME} INTERFACE ${M_SOURCES})
  endif()
  foreach(_g IN LISTS M_GEN)
    add_dependencies(${NAME} ${_g})
  endforeach()
  set(MESA_LAST_TARGET "${NAME}" PARENT_SCOPE)
endfunction()

# --- custom_target wrapper ---
# mesa_gen(<name> <output-name...> COMMAND <args...> [CAPTURE] [DEPENDS <list>])
# Outputs go to ${CMAKE_BINARY_DIR}<rel-path-of-current-source-dir>/<output-name>
# matching meson mirror layout. Creates custom target gen_<name>.
function(mesa_gen NAME)
  cmake_parse_arguments(M "CAPTURE" "" "COMMAND;DEPENDS" ${ARGN})
  if(NOT M_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "mesa_gen(${NAME}): no outputs")
  endif()
  if(NOT M_COMMAND)
    message(FATAL_ERROR "mesa_gen(${NAME}): no COMMAND")
  endif()
  set(M_OUTPUTS ${M_UNPARSED_ARGUMENTS})
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL "${CMAKE_SOURCE_DIR}")
    set(_rel ".")
  else()
    string(REPLACE "${CMAKE_SOURCE_DIR}/" "" _rel "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
  set(_outdir "${CMAKE_BINARY_DIR}/${_rel}")
  set(_outputs)
  foreach(_o IN LISTS M_OUTPUTS)
    list(APPEND _outputs "${_outdir}/${_o}")
  endforeach()
  set(_cmd ${M_COMMAND})
  if(M_CAPTURE)
    # capture: script writes to stdout -> redirect to first output via shell
    set(_cmdstr "")
    foreach(_a IN LISTS _cmd)
      if(NOT _cmdstr)
        set(_cmdstr "${_a}")
      else()
        set(_cmdstr "${_cmdstr} '${_a}'")
      endif()
    endforeach()
    set(_cmd sh -c "${_cmdstr} > '${_outputs}'")
  endif()
  add_custom_command(
    OUTPUT ${_outputs}
    COMMAND ${_cmd}
    DEPENDS ${M_DEPENDS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "mesa_gen: ${NAME}")
  add_custom_target(gen_${NAME} DEPENDS ${_outputs})
  set(MESA_GEN_${NAME}_OUTPUTS "${_outputs}" PARENT_SCOPE)
  set(MESA_GEN_${NAME}_TARGET "gen_${NAME}" PARENT_SCOPE)
  set(MESA_GEN_OUTPUT "${_outputs}" PARENT_SCOPE)
endfunction()

# --- configure_file copy (meson configure_file(copy: true)) ---
function(mesa_config_copy IN OUT)
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${IN}" "${CMAKE_BINARY_DIR}/${OUT}" COPYONLY)
endfunction()

# --- find a required program (meson find_program) ---
function(mesa_find_program VAR)
  find_program(${VAR} ${ARGN})
  if(NOT ${VAR})
    message(FATAL_ERROR "mesa: could not find program ${ARGN}")
  endif()
endfunction()

# --- foreach-style helper: split a cmake list like meson arrays ---
# meson arrays arrive as ;-separated CMake lists already; no helper needed.

# --- dependency helper for pkg-config modules (meson dependency) ---
function(mesa_pkg_dep VAR MOD)
  if(ARGC GREATER 2)
    pkg_check_modules(${VAR} ${ARGN} ${MOD})
  else()
    pkg_check_modules(${VAR} REQUIRED ${MOD})
  endif()
  if(NOT ${VAR}_FOUND)
    set(${VAR} "" PARENT_SCOPE)
    return()
  endif()
  add_library(dep_${VAR} INTERFACE)
  target_include_directories(dep_${VAR} INTERFACE ${${VAR}_INCLUDE_DIRS})
  target_link_libraries(dep_${VAR} INTERFACE ${${VAR}_LINK_LIBRARIES})
  if(${VAR}_CFLAGS_OTHER)
    target_compile_options(dep_${VAR} INTERFACE ${${VAR}_CFLAGS_OTHER})
  endif()
  if(${VAR}_LDFLAGS_OTHER)
    target_link_options(dep_${VAR} INTERFACE ${${VAR}_LDFLAGS_OTHER})
  endif()
  set(${VAR} "dep_${VAR}" PARENT_SCOPE)
endfunction()

# --- conditional add to a list (meson foreach +=) ---
function(mesa_add_if VAR COND)
  if(COND)
    list(APPEND ${VAR} ${ARGN})
    set(${VAR} "${${VAR}}" PARENT_SCOPE)
  endif()
endfunction()
