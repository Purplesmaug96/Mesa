# Mesa3D - src/mesa/glapi/ (mirror of src/mesa/glapi/meson.build)
# SPDX-License-Identifier: MIT

# glapi_xml_py_deps: all xml/py files the generators depend on (rebuild trigger)
set(glapi_xml_py_deps)
file(GLOB _glapi_xml ${CMAKE_CURRENT_SOURCE_DIR}/glapi/gen/*.xml)
file(GLOB _glapi_py ${CMAKE_CURRENT_SOURCE_DIR}/glapi/gen/*.py ${CMAKE_CURRENT_SOURCE_DIR}/glapi/new/genCommon.py)
file(GLOB _glapi_registry ${CMAKE_CURRENT_SOURCE_DIR}/glapi/registry/gl.xml)
set(glapi_xml_py_deps ${_glapi_xml} ${_glapi_py} ${_glapi_registry} "${CMAKE_SOURCE_DIR}/src/glx/libgl-symbols.txt")

add_subdirectory(glapi)
add_subdirectory(shared-glapi)

# forward generator outputs up to src/mesa/ (meson: subdir shares the scope)
foreach(_v
    glapi_mapi_tmp_h glapi_gentable_c main_enums_c main_api_exec_c
    main_api_exec_decl_h main_api_save_init_h main_api_save_h
    main_api_beginend_init_h main_api_hw_select_init_h main_dispatch_h
    main_marshal_generated_h main_unmarshal_table_c main_api_trace_c
    main_marshal_generated_c glx_generated glx_indirect_size_h inc_glapi)
  set(${_v} "${${_v}}" PARENT_SCOPE)
endforeach()
set(idep_dispatch_h idep_dispatch_h PARENT_SCOPE)
