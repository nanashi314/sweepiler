# Official MSVC x64 release. No Python or Z3 CLI process is used by the app.
set(IMS_Z3_ROOT "" CACHE PATH "Existing z3-4.13.4-x64-win directory (include/ and bin/)")
if(NOT IMS_Z3_ROOT)
  FetchContent_Declare(z3_binary
    URL https://github.com/Z3Prover/z3/releases/download/z3-4.13.4/z3-4.13.4-x64-win.zip
    URL_HASH SHA256=0a36dd5654fe423d23a727c4570c9db9a315926a4301189b868b6eb9bfd5979d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(z3_binary)
  set(IMS_Z3_ROOT "${z3_binary_SOURCE_DIR}")
endif()
foreach(required include/z3++.h bin/libz3.lib bin/libz3.dll)
  if(NOT EXISTS "${IMS_Z3_ROOT}/${required}")
    message(FATAL_ERROR "Missing ${IMS_Z3_ROOT}/${required}")
  endif()
endforeach()
add_library(Z3::z3 SHARED IMPORTED GLOBAL)
set_target_properties(Z3::z3 PROPERTIES
  IMPORTED_IMPLIB "${IMS_Z3_ROOT}/bin/libz3.lib"
  IMPORTED_LOCATION "${IMS_Z3_ROOT}/bin/libz3.dll"
  INTERFACE_INCLUDE_DIRECTORIES "${IMS_Z3_ROOT}/include")
function(ims_copy_z3 target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Z3::z3>" "$<TARGET_FILE_DIR:${target}>")
endfunction()
