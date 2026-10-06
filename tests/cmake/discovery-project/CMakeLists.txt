cmake_minimum_required(VERSION 3.25)
project(NvidiaVFXDiscoveryFixture LANGUAGES CXX)

if(NOT DEFINED PROJECT_ROOT)
  message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

list(PREPEND CMAKE_MODULE_PATH "${PROJECT_ROOT}/cmake")
find_package(NvidiaVFX REQUIRED)

if(NOT TARGET NvidiaVFX::VideoFX)
  message(FATAL_ERROR "NvidiaVFX::VideoFX was not created")
endif()

