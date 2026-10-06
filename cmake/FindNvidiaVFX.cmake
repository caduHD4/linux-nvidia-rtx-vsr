include_guard(GLOBAL)

set(_NvidiaVFX_roots)
if(DEFINED VFXSDK_ROOT AND NOT VFXSDK_ROOT STREQUAL "")
  list(APPEND _NvidiaVFX_roots "${VFXSDK_ROOT}")
elseif(DEFINED ENV{VFXSDK_ROOT} AND NOT "$ENV{VFXSDK_ROOT}" STREQUAL "")
  list(APPEND _NvidiaVFX_roots "$ENV{VFXSDK_ROOT}")
else()
  list(APPEND _NvidiaVFX_roots "/usr/local/VideoFX")
endif()

find_file(NvidiaVFX_VIDEO_EFFECTS_HEADER
  NAMES nvVideoEffects.h
  HINTS ${_NvidiaVFX_roots}
  PATH_SUFFIXES include include/nvvfx
  NO_DEFAULT_PATH
)
find_file(NvidiaVFX_CV_IMAGE_HEADER
  NAMES nvCVImage.h
  HINTS ${_NvidiaVFX_roots}
  PATH_SUFFIXES include include/nvvfx
  NO_DEFAULT_PATH
)
find_file(NvidiaVFX_VIDEO_SUPER_RES_HEADER
  NAMES nvVFXVideoSuperRes.h
  HINTS ${_NvidiaVFX_roots}
  PATH_SUFFIXES
    include
    include/nvvfx
    features/nvvfxvideosuperres/include
  NO_DEFAULT_PATH
)

foreach(component IN ITEMS VideoFX NVCVImage nvVFXVideoSuperRes)
  find_library(NvidiaVFX_${component}_LIBRARY
    NAMES ${component}
    HINTS ${_NvidiaVFX_roots}
    PATH_SUFFIXES
      lib
      lib64
      features/nvvfxvideosuperres/lib
    NO_DEFAULT_PATH
  )
endforeach()

set(_NvidiaVFX_missing)
foreach(pair IN ITEMS
    "NvidiaVFX_VIDEO_EFFECTS_HEADER|nvVideoEffects.h"
    "NvidiaVFX_CV_IMAGE_HEADER|nvCVImage.h"
    "NvidiaVFX_VIDEO_SUPER_RES_HEADER|nvVFXVideoSuperRes.h"
    "NvidiaVFX_VideoFX_LIBRARY|libVideoFX.so"
    "NvidiaVFX_NVCVImage_LIBRARY|libNVCVImage.so"
    "NvidiaVFX_nvVFXVideoSuperRes_LIBRARY|libnvVFXVideoSuperRes.so")
  string(REPLACE "|" ";" pair_parts "${pair}")
  list(GET pair_parts 0 variable)
  list(GET pair_parts 1 display_name)
  if(NOT ${variable})
    list(APPEND _NvidiaVFX_missing "${display_name}")
  endif()
endforeach()

if(_NvidiaVFX_missing)
  set(NvidiaVFX_FOUND FALSE)
  string(JOIN ", " _NvidiaVFX_missing_text ${_NvidiaVFX_missing})
  if(NvidiaVFX_FIND_REQUIRED)
    message(FATAL_ERROR
      "NVIDIA VFX SDK not found under VFXSDK_ROOT='${_NvidiaVFX_roots}'. "
      "Missing: ${_NvidiaVFX_missing_text}. Install the official SDK Core "
      "and VideoSuperRes feature, then set VFXSDK_ROOT.")
  endif()
  return()
endif()

set(_NvidiaVFX_required_version "1.3.0.0")
file(READ "${NvidiaVFX_VIDEO_SUPER_RES_HEADER}"
  _NvidiaVFX_video_super_res_header_text)
string(REGEX MATCH
  "#[ \t]*define[ \t]+NVVFXVIDEOSUPERRES_VERSION[ \t]+\"([^\"]+)\""
  _NvidiaVFX_version_match "${_NvidiaVFX_video_super_res_header_text}")
set(NvidiaVFX_VERSION "${CMAKE_MATCH_1}")
if(NOT NvidiaVFX_VERSION STREQUAL _NvidiaVFX_required_version)
  set(NvidiaVFX_FOUND FALSE)
  if(NvidiaVFX_FIND_REQUIRED)
    message(FATAL_ERROR
      "NVIDIA VideoSuperRes ${_NvidiaVFX_required_version} is required, but "
      "'${NvidiaVFX_VIDEO_SUPER_RES_HEADER}' reports "
      "'${NvidiaVFX_VERSION}'. Install matching SDK Core and feature packages.")
  endif()
  return()
endif()

get_filename_component(NvidiaVFX_INCLUDE_DIR
  "${NvidiaVFX_VIDEO_EFFECTS_HEADER}" DIRECTORY)
get_filename_component(NvidiaVFX_VIDEO_SUPER_RES_INCLUDE_DIR
  "${NvidiaVFX_VIDEO_SUPER_RES_HEADER}" DIRECTORY)
set(NvidiaVFX_INCLUDE_DIRS
  "${NvidiaVFX_INCLUDE_DIR}"
  "${NvidiaVFX_VIDEO_SUPER_RES_INCLUDE_DIR}"
)
list(REMOVE_DUPLICATES NvidiaVFX_INCLUDE_DIRS)
set(NvidiaVFX_LIBRARIES
  "${NvidiaVFX_VideoFX_LIBRARY}"
  "${NvidiaVFX_NVCVImage_LIBRARY}"
  "${NvidiaVFX_nvVFXVideoSuperRes_LIBRARY}"
)
set(NvidiaVFX_RUNTIME_DIRS)
foreach(library IN LISTS NvidiaVFX_LIBRARIES)
  get_filename_component(library_directory "${library}" DIRECTORY)
  list(APPEND NvidiaVFX_RUNTIME_DIRS "${library_directory}")
endforeach()
foreach(runtime_directory IN ITEMS
    "${_NvidiaVFX_roots}/external/cuda/lib"
    "${_NvidiaVFX_roots}/lib")
  if(IS_DIRECTORY "${runtime_directory}")
    list(APPEND NvidiaVFX_RUNTIME_DIRS "${runtime_directory}")
  endif()
endforeach()
list(REMOVE_DUPLICATES NvidiaVFX_RUNTIME_DIRS)

set(_NvidiaVFX_rpath_link_options)
foreach(runtime_directory IN LISTS NvidiaVFX_RUNTIME_DIRS)
  list(APPEND _NvidiaVFX_rpath_link_options
    "LINKER:-rpath-link,${runtime_directory}")
endforeach()
set(NvidiaVFX_FOUND TRUE)

if(NOT TARGET NvidiaVFX::VideoFX)
  add_library(NvidiaVFX::VideoFX INTERFACE IMPORTED)
  set_target_properties(NvidiaVFX::VideoFX PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${NvidiaVFX_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "${NvidiaVFX_LIBRARIES}"
    INTERFACE_LINK_OPTIONS "${_NvidiaVFX_rpath_link_options}"
  )
endif()

mark_as_advanced(
  NvidiaVFX_VIDEO_EFFECTS_HEADER
  NvidiaVFX_CV_IMAGE_HEADER
  NvidiaVFX_VIDEO_SUPER_RES_HEADER
  NvidiaVFX_VideoFX_LIBRARY
  NvidiaVFX_NVCVImage_LIBRARY
  NvidiaVFX_nvVFXVideoSuperRes_LIBRARY
)
