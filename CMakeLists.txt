cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_TESTS "Enable tests" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_qt(COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

set(USE_SYSTEM_CURL
    OFF
    CACHE STRING "Use system cURL")

if(USE_SYSTEM_CURL)
  find_package(CURL REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${CURL_LIBRARIES}")
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${CURL_INCLUDE_DIRS}")
else()
  include(cmake/BuildMyCurl.cmake)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE libcurl)
endif()

if(WIN32)
  if(DEFINED ENV{ACCELERATION})
    set(ACCELERATION
        $ENV{ACCELERATION}
        CACHE STRING "Acceleration to use" FORCE)
  endif()
  if(NOT DEFINED ACCELERATION)
    set(ACCELERATION
        "cpu"
        CACHE STRING "Acceleration to use")
  endif()
  set_property(CACHE ACCELERATION PROPERTY STRINGS "cpu" "hipblas" "cuda" "vulkan")
endif()

include(cmake/BuildWhispercpp.cmake)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Whispercpp)

include(cmake/BuildCTranslate2.cmake)
include(cmake/BuildSentencepiece.cmake)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ct2 sentencepiece)

set(USE_SYSTEM_ONNXRUNTIME
    OFF
    CACHE STRING "Use system ONNX Runtime")

set(DISABLE_ONNXRUNTIME_GPU
    OFF
    CACHE STRING "Disables GPU support of ONNX Runtime (Only valid on Linux)")

if(DISABLE_ONNXRUNTIME_GPU)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE DISABLE_ONNXRUNTIME_GPU)
endif()

if(USE_SYSTEM_ONNXRUNTIME)
  if(OS_LINUX)
    find_package(Onnxruntime 1.16.3 REQUIRED)
    set(Onnxruntime_INCLUDE_PATH
        ${Onnxruntime_INCLUDE_DIR} ${Onnxruntime_INCLUDE_DIR}/onnxruntime
        ${Onnxruntime_INCLUDE_DIR}/onnxruntime/core/session ${Onnxruntime_INCLUDE_DIR}/onnxruntime/core/providers/cpu)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${Onnxruntime_LIBRARIES}")
    target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${Onnxruntime_INCLUDE_PATH}")
  else()
    message(FATAL_ERROR "System ONNX Runtime is only supported on Linux!")
  endif()
else()
  include(cmake/FetchOnnxruntime.cmake)
endif()

include(cmake/BuildICU.cmake)
# Add ICU to the target
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE ICU)
target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC ${ICU_INCLUDE_DIR})

# check env var for extra verbose logging
if(DEFINED ENV{LOCALVOCAL_EXTRA_VERBOSE})
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE LOCALVOCAL_EXTRA_VERBOSE)
endif()

option(ENABLE_WEBVTT "Enable WebVTT embedding" ON)

if(ENABLE_WEBVTT)
  include(cmake/BuildWebVTT.cmake)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE c_webvtt_in_video_stream)
  target_compile_definitions(c_webvtt_in_video_stream INTERFACE ENABLE_WEBVTT)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.c
          src/transcription-filter.cpp
          src/transcription-filter.c
          src/transcription-filter-callbacks.cpp
          src/transcription-filter-properties.cpp
          src/transcription-filter-utils.cpp
          src/transcription-utils.cpp
          src/model-utils/model-downloader.cpp
          src/model-utils/model-downloader-ui.cpp
          src/model-utils/model-infos.cpp
          src/model-utils/model-find-utils.cpp
          src/whisper-utils/whisper-processing.cpp
          src/whisper-utils/whisper-utils.cpp
          src/whisper-utils/whisper-model-utils.cpp
          src/whisper-utils/whisper-params.cpp
          src/whisper-utils/silero-vad-onnx.cpp
          src/whisper-utils/token-buffer-thread.cpp
          src/whisper-utils/vad-processing.cpp
          src/translation/language_codes.cpp
          src/translation/translation.cpp
          src/translation/translation-utils.cpp
          src/ui/filter-replace-utils.cpp
          src/translation/translation-language-utils.cpp
          src/ui/filter-replace-dialog.cpp)

add_subdirectory(src/translation/cloud-translation)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

if(ENABLE_TESTS)
  add_subdirectory(src/tests)
endif()
