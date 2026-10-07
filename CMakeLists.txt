cmake_minimum_required(VERSION 3.25)
project(nvidia_vsr_linux VERSION 0.1.0 LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(NVVFX_VSR_BUILD_SMOKE "Build the real NVIDIA VFX smoke executable" ON)
set(NVVFX_VSR_REQUIRE_SDK "${NVVFX_VSR_BUILD_SMOKE}" CACHE BOOL
  "Fail configuration when the official NVIDIA VFX SDK is unavailable")

include(CTest)

if(NVVFX_VSR_REQUIRE_SDK)
  find_package(NvidiaVFX REQUIRED)
else()
  find_package(NvidiaVFX QUIET)
endif()

if(NVVFX_VSR_BUILD_SMOKE AND NvidiaVFX_FOUND)
  add_executable(vsr-smoke
    src/smoke/main.cpp
    src/smoke/nvidia_vfx_api.cpp
    src/smoke/smoke_config.cpp
    src/smoke/vfx_session.cpp
  )
  target_compile_features(vsr-smoke PRIVATE cxx_std_20)
  target_include_directories(vsr-smoke PRIVATE include)
  target_link_libraries(vsr-smoke PRIVATE NvidiaVFX::VideoFX)
  target_link_options(vsr-smoke PRIVATE "LINKER:--disable-new-dtags")
  set_target_properties(vsr-smoke PROPERTIES
    BUILD_RPATH "${NvidiaVFX_RUNTIME_DIRS}"
  )
  if(BUILD_TESTING)
    add_test(
      NAME vsr_smoke_gpu
      COMMAND vsr-smoke
        --quality high
        --input 1920x1080
        --output 3840x2160
    )
    set_tests_properties(vsr_smoke_gpu PROPERTIES
      LABELS gpu
      PASS_REGULAR_EXPRESSION
        "GPU: [^\n]+\nVSR quality: 3\n1920x1080 -> 3840x2160\n[0-9]+(\\.[0-9]+)? ms/frame\nPASS"
    )
  endif()
endif()

if(BUILD_TESTING)
  add_executable(smoke_config_test
    tests/unit/smoke_config_test.cpp
    src/smoke/smoke_config.cpp
  )
  target_compile_features(smoke_config_test PRIVATE cxx_std_20)
  target_include_directories(smoke_config_test PRIVATE include)
  add_test(NAME smoke_config COMMAND smoke_config_test)

  add_executable(vfx_session_test
    tests/unit/vfx_session_test.cpp
    src/smoke/vfx_session.cpp
  )
  target_compile_features(vfx_session_test PRIVATE cxx_std_20)
  target_include_directories(vfx_session_test PRIVATE include)
  add_test(NAME vfx_session COMMAND vfx_session_test)

  add_test(
    NAME nvidia_vfx_discovery
    COMMAND "${CMAKE_COMMAND}" -P
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/test_missing_sdk.cmake"
  )
endif()

add_library(nvvfx_vsr_policy src/core/processor_config.cpp src/core/frame_eligibility.cpp src/core/job_admission.cpp)
target_compile_features(nvvfx_vsr_policy PUBLIC cxx_std_20)
target_include_directories(nvvfx_vsr_policy PUBLIC include)
if(BUILD_TESTING)
  add_executable(processor_config_test tests/unit/processor_config_test.cpp)
  target_link_libraries(processor_config_test PRIVATE nvvfx_vsr_policy)
  add_test(NAME processor_config COMMAND processor_config_test)
  add_executable(frame_eligibility_test tests/unit/frame_eligibility_test.cpp)
  target_link_libraries(frame_eligibility_test PRIVATE nvvfx_vsr_policy)
  add_test(NAME frame_eligibility COMMAND frame_eligibility_test)
  add_executable(job_admission_test tests/unit/job_admission_test.cpp)
  target_link_libraries(job_admission_test PRIVATE nvvfx_vsr_policy)
  add_test(NAME job_admission COMMAND job_admission_test)
endif()

if(NvidiaVFX_FOUND)
  add_library(nvvfx_vsr_core src/core/vsr_processor.cpp src/core/image_descriptor.c src/core/gl_bridge.cpp)
  target_link_libraries(nvvfx_vsr_core PUBLIC nvvfx_vsr_policy PRIVATE ${CMAKE_DL_LIBS})
  target_include_directories(nvvfx_vsr_core PRIVATE ${NvidiaVFX_INCLUDE_DIRS} "${VFXSDK_ROOT}/external/cuda/include")
  if(BUILD_TESTING)
    add_executable(vsr_processor_test tests/gpu/vsr_processor_test.cpp)
    target_include_directories(vsr_processor_test PRIVATE "${VFXSDK_ROOT}/external/cuda/include")
    target_link_libraries(vsr_processor_test PRIVATE nvvfx_vsr_core cuda)
    add_test(NAME vsr_processor_gpu COMMAND vsr_processor_test "${VFXSDK_ROOT}")
    set_tests_properties(vsr_processor_gpu PROPERTIES LABELS gpu TIMEOUT 60)
  endif()
endif()

if(BUILD_TESTING)
  add_executable(presentation_cache_test tests/unit/presentation_cache_test.cpp)
  target_link_libraries(presentation_cache_test PRIVATE nvvfx_vsr_policy)
  add_test(NAME presentation_cache COMMAND presentation_cache_test)
endif()

if(NvidiaVFX_FOUND AND BUILD_TESTING)
  add_executable(gl_bridge_test tests/gpu/gl_bridge_test.cpp)
  target_include_directories(gl_bridge_test PRIVATE "${VFXSDK_ROOT}/external/cuda/include")
  target_link_libraries(gl_bridge_test PRIVATE nvvfx_vsr_core cuda EGL GL)
  add_test(NAME gl_bridge_gpu COMMAND gl_bridge_test "${VFXSDK_ROOT}")
  set_tests_properties(gl_bridge_gpu PROPERTIES LABELS gpu TIMEOUT 60
    ENVIRONMENT "__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json;EGL_PLATFORM=x11")
endif()

if(NvidiaVFX_FOUND AND BUILD_TESTING)
  add_test(NAME gl_bridge_gles_gpu COMMAND gl_bridge_test "${VFXSDK_ROOT}")
  set_tests_properties(gl_bridge_gles_gpu PROPERTIES LABELS gpu TIMEOUT 60
    ENVIRONMENT "__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json;EGL_PLATFORM=x11;NVVFX_TEST_GLES=1")
endif()

if(NvidiaVFX_FOUND AND BUILD_TESTING AND UNIX AND NOT APPLE)
  add_executable(gl_bridge_teardown_test tests/gpu/gl_bridge_teardown_test.cpp)
  target_include_directories(gl_bridge_teardown_test PRIVATE ${NvidiaVFX_INCLUDE_DIRS} "${VFXSDK_ROOT}/external/cuda/include")
  target_link_libraries(gl_bridge_teardown_test PRIVATE nvvfx_vsr_core cuda EGL GLESv2 ${CMAKE_DL_LIBS})
  target_link_options(gl_bridge_teardown_test PRIVATE "-Wl,--wrap=dlsym")
  add_test(NAME gl_bridge_teardown_gpu COMMAND gl_bridge_teardown_test "${VFXSDK_ROOT}")
  set_tests_properties(gl_bridge_teardown_gpu PROPERTIES LABELS gpu TIMEOUT 60
    ENVIRONMENT "__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json;EGL_PLATFORM=x11")
endif()
