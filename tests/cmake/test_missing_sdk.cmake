cmake_minimum_required(VERSION 3.25)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(TEST_ROOT "${PROJECT_ROOT}/build/cmake-discovery-test")
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/empty-sdk")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/discovery-project"
    -B "${TEST_ROOT}/missing-build"
    -DPROJECT_ROOT=${PROJECT_ROOT}
    -DVFXSDK_ROOT=${TEST_ROOT}/empty-sdk
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_stdout
  ERROR_VARIABLE missing_stderr
)

if(missing_result EQUAL 0)
  message(FATAL_ERROR "An empty VFXSDK_ROOT unexpectedly configured")
endif()

set(missing_output "${missing_stdout}\n${missing_stderr}")
foreach(required_text IN ITEMS
    "VFXSDK_ROOT"
    "nvVideoEffects.h"
    "nvCVImage.h"
    "nvVFXVideoSuperRes.h")
  string(FIND "${missing_output}" "${required_text}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR
      "Missing-SDK diagnostic did not mention ${required_text}:\n${missing_output}")
  endif()
endforeach()

set(fake_sdk "${TEST_ROOT}/fake-sdk")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/fake-sdk/" DESTINATION "${fake_sdk}")
file(MAKE_DIRECTORY "${fake_sdk}/lib")
foreach(library IN ITEMS VideoFX NVCVImage)
  file(WRITE "${fake_sdk}/lib/lib${library}.so" "")
endforeach()
set(fake_feature "${fake_sdk}/features/nvvfxvideosuperres")
file(MAKE_DIRECTORY "${fake_feature}/include" "${fake_feature}/lib")
file(RENAME
  "${fake_sdk}/include/nvVFXVideoSuperRes.h"
  "${fake_feature}/include/nvVFXVideoSuperRes.h")
file(WRITE "${fake_feature}/lib/libnvVFXVideoSuperRes.so" "")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/discovery-project"
    -B "${TEST_ROOT}/found-build"
    -DPROJECT_ROOT=${PROJECT_ROOT}
    -DVFXSDK_ROOT=${fake_sdk}
  RESULT_VARIABLE found_result
  OUTPUT_VARIABLE found_stdout
  ERROR_VARIABLE found_stderr
)

if(NOT found_result EQUAL 0)
  message(FATAL_ERROR
    "Complete fake SDK was not discovered:\n${found_stdout}\n${found_stderr}")
endif()

set(mismatched_sdk "${TEST_ROOT}/mismatched-sdk")
file(COPY "${fake_sdk}/" DESTINATION "${mismatched_sdk}")
set(mismatched_header
  "${mismatched_sdk}/features/nvvfxvideosuperres/include/nvVFXVideoSuperRes.h")
file(READ "${mismatched_header}" mismatched_header_text)
string(REPLACE "1.3.0.0" "1.2.0.0" mismatched_header_text
  "${mismatched_header_text}")
file(WRITE "${mismatched_header}" "${mismatched_header_text}")
execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${CMAKE_CURRENT_LIST_DIR}/discovery-project"
    -B "${TEST_ROOT}/mismatched-build"
    -DPROJECT_ROOT=${PROJECT_ROOT}
    -DVFXSDK_ROOT=${mismatched_sdk}
  RESULT_VARIABLE mismatched_result
  OUTPUT_VARIABLE mismatched_stdout
  ERROR_VARIABLE mismatched_stderr
)
if(mismatched_result EQUAL 0)
  message(FATAL_ERROR "VideoSuperRes 1.2.0.0 was accepted as SDK 1.3.0.0")
endif()
set(mismatched_output "${mismatched_stdout}\n${mismatched_stderr}")
if(NOT mismatched_output MATCHES "1.3.0.0")
  message(FATAL_ERROR
    "Version mismatch diagnostic omitted required version:\n${mismatched_output}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${PROJECT_ROOT}"
    -B "${TEST_ROOT}/top-level-build"
    -DVFXSDK_ROOT=${fake_sdk}
  RESULT_VARIABLE top_level_result
  OUTPUT_VARIABLE top_level_stdout
  ERROR_VARIABLE top_level_stderr
)
if(NOT top_level_result EQUAL 0)
  message(FATAL_ERROR
    "Top-level project did not configure with fake SDK:\n"
    "${top_level_stdout}\n${top_level_stderr}")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir
    "${TEST_ROOT}/top-level-build" -N -L gpu
  RESULT_VARIABLE list_result
  OUTPUT_VARIABLE list_stdout
  ERROR_VARIABLE list_stderr
)
if(NOT list_result EQUAL 0 OR NOT list_stdout MATCHES "vsr_smoke_gpu")
  message(FATAL_ERROR
    "A clean top-level configure did not register the GPU smoke test:\n"
    "${list_stdout}\n${list_stderr}")
endif()

message(STATUS "NVIDIA VFX SDK discovery behavior verified")
