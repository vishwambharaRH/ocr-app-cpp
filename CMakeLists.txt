cmake_minimum_required(VERSION 3.16)
project(QtQuickTessApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Allow user to pass TESSDATA_DIR (optional)
if(NOT DEFINED TESSDATA_DIR)
  set(TESSDATA_DIR "")
endif()

find_package(Qt6 COMPONENTS Quick Widgets Network Pdf REQUIRED)

# Find Tesseract and Leptonica
find_path(LEPT_HEADERS leptonica/allheaders.h)
find_library(LEPT_LIB leptonica)
find_path(TESS_HEADERS tesseract/baseapi.h)
find_library(TESS_LIB tesseract)

if(NOT LEPT_HEADERS OR NOT LEPT_LIB OR NOT TESS_HEADERS OR NOT TESS_LIB)
  message(FATAL_ERROR "Tesseract/Leptonica dev packages not found. Install them first.")
endif()

include_directories(${LEPT_HEADERS} ${TESS_HEADERS} ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(SOURCES
  src/main.cpp
  src/OcrProcessor.cpp
)

qt_add_executable(QtQuickTessApp
  ${SOURCES}
)

qt6_add_qml_module(QtQuickTessApp
    URI QtQuickTessApp
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(QtQuickTessApp
  PRIVATE Qt6::Quick Qt6::Widgets Qt6::Network Qt6::Pdf
          ${TESS_LIB} ${LEPT_LIB}
)

# Add compile definitions with TESSDATA_DIR if given
if(TESSDATA_DIR)
  target_compile_definitions(QtQuickTessApp PRIVATE APP_TESSDATA_DIR=\"${TESSDATA_DIR}\")
endif()
