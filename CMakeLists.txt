cmake_minimum_required(VERSION 3.8)
if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

project(blah2)
include(CMakePrintHelpers)
include(CTest)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")

# Dragon Labs CR-8 (optional — only available when libdlcr is installed on the system)
find_library(DLCR_LIBRARY dlcr)
if(DLCR_LIBRARY)
  message(STATUS "libdlcr found: ${DLCR_LIBRARY} — CR-8 support enabled")
else()
  message(STATUS "libdlcr not found — CR-8 support disabled (use Dockerfile.dev to enable)")
endif()

find_package(Threads REQUIRED)
find_package(asio  REQUIRED)
find_path(RAPIDJSON_INCLUDE_DIRS "rapidjson/allocators.h")
find_package(ryml CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(Armadillo CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

set(CMAKE_PREFIX_PATH "/opt/uhd" ${CMAKE_PREFIX_PATH})
find_package(UHD "4.8.0.0" CONFIG)
if(UHD_FOUND)
  message(STATUS "UHD found: ${UHD_VERSION} — USRP support enabled")
else()
  message(STATUS "UHD not found — USRP support disabled (install libuhd-dev to enable)")
endif()

set(PROJECT_ROOT "${PROJECT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_ROOT}/bin")
set(PROJECT_BINARY_TEST_DIR "${PROJECT_ROOT}/bin/test")
set(PROJECT_BINARY_TEST_UNIT_DIR "${PROJECT_BINARY_TEST_DIR}/unit")
set(PROJECT_BINARY_TEST_FUNCTIONAL_DIR "${PROJECT_BINARY_TEST_DIR}/functional")
set(PROJECT_BINARY_TEST_COMPARISON_DIR "${PROJECT_BINARY_TEST_DIR}/comparison")

message("Binary path: ${PROJECT_BINARY_DIR}")
message("Binary test path: ${PROJECT_BINARY_TEST_DIR}")

# include from top-level src dir
include_directories(src)
if(UHD_FOUND)
  include_directories(${UHD_INCLUDE_DIRS})
endif()

# TODO: create FindSdrplay.cmake for this
add_library(sdrplay /usr/local/include/sdrplay_api.h)
set_target_properties(sdrplay PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(sdrplay PUBLIC /usr/local/lib/libsdrplay_api.so.3.15)

# TODO: Move to separate src/CMakeLists.txt
set(BLAH2_SOURCES
  src/blah2.cpp
  src/capture/Capture.cpp
  src/capture/Source.cpp
  src/capture/rspduo/RspDuo.cpp
  src/capture/hackrf/HackRf.cpp
  src/capture/kraken/Kraken.cpp
  src/process/ambiguity/Ambiguity.cpp
  src/process/clutter/WienerHopf.cpp
  src/process/detection/CfarDetector1D.cpp
  src/process/detection/Centroid.cpp
  src/process/detection/Interpolate.cpp
  src/process/tracker/Tracker.cpp
  src/process/spectrum/SpectrumAnalyser.cpp
  src/process/meta/HammingNumber.cpp
  src/process/utility/Socket.cpp
  src/data/IqData.cpp
  src/data/Map.cpp
  src/data/Detection.cpp
  src/data/Track.cpp
  src/data/meta/Timing.cpp
)
if(UHD_FOUND)
  list(APPEND BLAH2_SOURCES src/capture/usrp/Usrp.cpp)
endif()
if(DLCR_LIBRARY)
  list(APPEND BLAH2_SOURCES src/capture/dlcr/Dlcr.cpp)
endif()

add_executable(blah2 ${BLAH2_SOURCES})

target_link_libraries(blah2 PRIVATE
  Threads::Threads
  asio::asio
  ryml::ryml
  httplib::httplib
  armadillo
  fftw3
  fftw3_threads
  sdrplay
  hackrf
  rtlsdr
)
if(UHD_FOUND)
  target_link_libraries(blah2 PRIVATE ${UHD_LIBRARIES})
  target_compile_definitions(blah2 PRIVATE HAVE_UHD)
endif()
if(DLCR_LIBRARY)
  target_link_libraries(blah2 PRIVATE ${DLCR_LIBRARY})
  target_compile_definitions(blah2 PRIVATE HAVE_DLCR)
endif()
target_compile_options(blah2 PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -ffast-math>
)
target_include_directories(blah2 PRIVATE RAPIDJSON_INCLUDE_DIRS "rapidjson/allocators.h")

# unit tests
add_executable(testAmbiguity
  test/unit/process/ambiguity/TestAmbiguity.cpp
  src/data/IqData.cpp
  src/data/Map.cpp
  src/process/ambiguity/Ambiguity.cpp
  src/process/meta/HammingNumber.cpp
)
target_link_libraries(testAmbiguity PRIVATE
  Catch2::Catch2WithMain
  fftw3
  fftw3_threads
)
target_compile_options(testAmbiguity PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3 -march=native -ffast-math>
)
set_target_properties(testAmbiguity PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_TEST_UNIT_DIR}")

add_executable(testTracker
  test/unit/process/tracker/TestTracker.cpp
  src/data/Detection.cpp
  src/data/Track.cpp
  src/process/tracker/Tracker.cpp
)
target_link_libraries(testTracker PRIVATE 
  Catch2::Catch2WithMain
)
set_target_properties(testTracker PROPERTIES 
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_TEST_UNIT_DIR}")

add_executable(testHammingNumber
  test/unit/process/meta/TestHammingNumber.cpp
  src/process/meta/HammingNumber.cpp
)
target_link_libraries(testHammingNumber PRIVATE 
  Catch2::Catch2WithMain
)
set_target_properties(testHammingNumber PROPERTIES 
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_TEST_UNIT_DIR}")

# TODO: Unsure if will be using CTest.
add_test(NAME testAmbiguity COMMAND testAmbiguity)
add_test(NAME testTracker COMMAND testTracker)
