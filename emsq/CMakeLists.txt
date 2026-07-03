add_executable(emsq emsq.c)

set_target_properties(emsq PROPERTIES LINKER_LANGUAGE CXX C_STANDARD 99)
target_include_directories(emsq PRIVATE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(emsq PRIVATE squirrel_static sqstdlib_static)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set_target_properties(
    emsq
    PROPERTIES
      LINK_FLAGS
      "--no-entry -sMODULARIZE=1 -sEXPORT_NAME=createEmsqModule -sFILESYSTEM=1 -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_FUNCTIONS=['_emsq_init','_emsq_shutdown','_emsq_run_string','_emsq_run_file','_emsq_get_output','_emsq_clear_output','_emsq_get_error','_malloc','_free'] -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8','FS']"
  )
endif()
