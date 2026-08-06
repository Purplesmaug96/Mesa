# Mesa3D - src/mesa/glapi/glapi/gen/ (mirror of src/mesa/glapi/glapi/gen/meson.build)
# SPDX-License-Identifier: MIT

set(_gen "${CMAKE_BINARY_DIR}/src/mesa/glapi/glapi/gen")

# glapi_mapi_tmp.h
add_custom_command(
  OUTPUT "${_gen}/glapi_mapi_tmp.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../mapi_abi.py --printer glapi --gl_symbols ${CMAKE_SOURCE_DIR}/src/glx/libgl-symbols.txt ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml > ${_gen}/glapi_mapi_tmp.h
  DEPENDS ${glapi_xml_py_deps} ../../mapi_abi.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "glapi_mapi_tmp.h")
add_custom_target(gen_glapi_mapi_tmp_h DEPENDS "${_gen}/glapi_mapi_tmp.h")
set(glapi_mapi_tmp_h "${_gen}/glapi_mapi_tmp.h" PARENT_SCOPE)

# glapi_gentable.c
add_custom_command(
  OUTPUT "${_gen}/glapi_gentable.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gl_gentable.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml > ${_gen}/glapi_gentable.c
  DEPENDS ${glapi_xml_py_deps} gl_gentable.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "glapi_gentable.c")
add_custom_target(gen_glapi_gentable_c DEPENDS "${_gen}/glapi_gentable.c")
set(glapi_gentable_c "${_gen}/glapi_gentable.c" PARENT_SCOPE)

# enums.c
add_custom_command(
  OUTPUT "${_gen}/enums.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gl_enums.py -f ${CMAKE_CURRENT_SOURCE_DIR}/../registry/gl.xml > ${_gen}/enums.c
  DEPENDS ${glapi_xml_py_deps} gl_enums.py
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "enums.c")
add_custom_target(gen_main_enums_c DEPENDS "${_gen}/enums.c")
set(main_enums_c "${_gen}/enums.c" PARENT_SCOPE)

# api_exec_init.c
add_custom_command(
  OUTPUT "${_gen}/api_exec_init.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/api_exec_init.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml > ${_gen}/api_exec_init.c
  DEPENDS ${glapi_xml_py_deps} api_exec_init.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "api_exec_init.c")
add_custom_target(gen_main_api_exec_c DEPENDS "${_gen}/api_exec_init.c")
set(main_api_exec_c "${_gen}/api_exec_init.c" PARENT_SCOPE)

# api_exec_decl.h
add_custom_command(
  OUTPUT "${_gen}/api_exec_decl.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/api_exec_decl_h.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml > ${_gen}/api_exec_decl.h
  DEPENDS ${glapi_xml_py_deps} api_exec_decl_h.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "api_exec_decl.h")
add_custom_target(gen_main_api_exec_decl_h DEPENDS "${_gen}/api_exec_decl.h")
set(main_api_exec_decl_h "${_gen}/api_exec_decl.h" PARENT_SCOPE)

# api_save_init.h
add_custom_command(
  OUTPUT "${_gen}/api_save_init.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/api_save_init_h.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml > ${_gen}/api_save_init.h
  DEPENDS ${glapi_xml_py_deps} api_save_init_h.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "api_save_init.h")
add_custom_target(gen_main_api_save_init_h DEPENDS "${_gen}/api_save_init.h")
set(main_api_save_init_h "${_gen}/api_save_init.h" PARENT_SCOPE)

# api_save.h
add_custom_command(
  OUTPUT "${_gen}/api_save.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/api_save_h.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml > ${_gen}/api_save.h
  DEPENDS ${glapi_xml_py_deps} api_save_h.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "api_save.h")
add_custom_target(gen_main_api_save_h DEPENDS "${_gen}/api_save.h")
set(main_api_save_h "${_gen}/api_save.h" PARENT_SCOPE)

# api_beginend_init.h
add_custom_command(
  OUTPUT "${_gen}/api_beginend_init.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/api_beginend_init_h.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml > ${_gen}/api_beginend_init.h
  DEPENDS ${glapi_xml_py_deps} api_beginend_init_h.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "api_beginend_init.h")
add_custom_target(gen_main_api_beginend_init_h DEPENDS "${_gen}/api_beginend_init.h")
set(main_api_beginend_init_h "${_gen}/api_beginend_init.h" PARENT_SCOPE)

# api_hw_select_init.h
add_custom_command(
  OUTPUT "${_gen}/api_hw_select_init.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/api_hw_select_init_h.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_API.xml > ${_gen}/api_hw_select_init.h
  DEPENDS ${glapi_xml_py_deps} api_hw_select_init_h.py gl_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "api_hw_select_init.h")
add_custom_target(gen_main_api_hw_select_init_h DEPENDS "${_gen}/api_hw_select_init.h")
set(main_api_hw_select_init_h "${_gen}/api_hw_select_init.h" PARENT_SCOPE)

# dispatch.h
add_custom_command(
  OUTPUT "${_gen}/dispatch.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gl_table.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml -m dispatch > ${_gen}/dispatch.h
  DEPENDS ${glapi_xml_py_deps} gl_table.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "dispatch.h")
add_custom_target(gen_main_dispatch_h DEPENDS "${_gen}/dispatch.h")
set(main_dispatch_h "${_gen}/dispatch.h" PARENT_SCOPE)
add_library(idep_dispatch_h INTERFACE)
target_include_directories(idep_dispatch_h INTERFACE "${_gen}")
add_dependencies(idep_dispatch_h gen_main_dispatch_h)

# marshal_generated.h
add_custom_command(
  OUTPUT "${_gen}/marshal_generated.h"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/marshal_generated_h.py ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml ${sizeof_pointer} > ${_gen}/marshal_generated.h
  DEPENDS ${glapi_xml_py_deps} marshal_generated_h.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "marshal_generated.h")
add_custom_target(gen_main_marshal_generated_h DEPENDS "${_gen}/marshal_generated.h")
set(main_marshal_generated_h "${_gen}/marshal_generated.h" PARENT_SCOPE)

# unmarshal_table.c
add_custom_command(
  OUTPUT "${_gen}/unmarshal_table.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/unmarshal_table_c.py ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml ${sizeof_pointer} > ${_gen}/unmarshal_table.c
  DEPENDS ${glapi_xml_py_deps} unmarshal_table_c.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "unmarshal_table.c")
add_custom_target(gen_main_unmarshal_table_c DEPENDS "${_gen}/unmarshal_table.c")
set(main_unmarshal_table_c "${_gen}/unmarshal_table.c" PARENT_SCOPE)

# api_trace.c
add_custom_command(
  OUTPUT "${_gen}/api_trace.c"
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/api_trace_c.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml > ${_gen}/api_trace.c
  DEPENDS ${glapi_xml_py_deps} api_trace_c.py gl_and_es_API.xml
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "api_trace.c")
add_custom_target(gen_main_api_trace_c DEPENDS "${_gen}/api_trace.c")
set(main_api_trace_c "${_gen}/api_trace.c" PARENT_SCOPE)

# marshal_generated0..7.c
set(main_marshal_generated_c)
set(_mg_targets)
foreach(_i RANGE 0 7)
  add_custom_command(
    OUTPUT "${_gen}/marshal_generated${_i}.c"
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/marshal_generated_c.py ${CMAKE_CURRENT_SOURCE_DIR}/gl_and_es_API.xml ${_i} 8 ${sizeof_pointer} > ${_gen}/marshal_generated${_i}.c
    DEPENDS ${glapi_xml_py_deps} marshal_generated_c.py gl_and_es_API.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "marshal_generated${_i}.c")
  add_custom_target(gen_marshal_generated${_i} DEPENDS "${_gen}/marshal_generated${_i}.c")
  list(APPEND _mg_targets gen_marshal_generated${_i})
  list(APPEND main_marshal_generated_c "${_gen}/marshal_generated${_i}.c")
endforeach()
set(main_marshal_generated_c "${main_marshal_generated_c}" PARENT_SCOPE)

# glx_generated (needed by src/glx)
set(glx_generated)
foreach(_g IN ITEMS "indirect.c proto" "indirect.h init_h" "indirect_init.c init_c")
  separate_arguments(_g_ NATIVE_COMMAND "${_g}")
  list(GET _g_ 0 _out)
  list(GET _g_ 1 _mode)
  add_custom_command(
    OUTPUT "${_gen}/${_out}"
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/glX_proto_send.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_API.xml -m ${_mode} -s ${CMAKE_SOURCE_DIR}/src/glx/libgl-symbols.txt > ${_gen}/${_out}
    DEPENDS ${glapi_xml_py_deps} glX_proto_send.py gl_API.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "${_out}")
  add_custom_target(gen_glx_${_out} DEPENDS "${_gen}/${_out}")
  list(APPEND glx_generated "${_gen}/${_out}")
endforeach()
foreach(_g IN ITEMS "indirect_size.h size_h _INDIRECT_SIZE_H_" "indirect_size.c size_c _")
  separate_arguments(_g_ NATIVE_COMMAND "${_g}")
  list(GET _g_ 0 _out)
  list(GET _g_ 1 _mode)
  list(GET _g_ 2 _tag)
  set(_tag_args)
  if(NOT _tag STREQUAL "_")
    set(_tag_args "--header-tag" "${_tag}")
  endif()
  add_custom_command(
    OUTPUT "${_gen}/${_out}"
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/glX_proto_size.py -f ${CMAKE_CURRENT_SOURCE_DIR}/gl_API.xml --only-set -m ${_mode} ${_tag_args} > ${_gen}/${_out}
    DEPENDS ${glapi_xml_py_deps} glX_proto_size.py gl_API.xml
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "${_out}")
  add_custom_target(gen_glx_${_out} DEPENDS "${_gen}/${_out}")
  list(APPEND glx_generated "${_gen}/${_out}")
endforeach()
list(GET glx_generated 3 glx_indirect_size_h)
set(glx_generated "${glx_generated}" PARENT_SCOPE)
set(glx_indirect_size_h "${glx_indirect_size_h}" PARENT_SCOPE)
