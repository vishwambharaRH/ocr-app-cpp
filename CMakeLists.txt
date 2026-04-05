cmake_minimum_required(VERSION 3.22)
project(OCRLLMProcessor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt setup (IMPORTANT)
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    Widgets
    Network
    Pdf
)

qt_standard_project_setup()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Optional Tesseract
set(HAVE_TESSERACT FALSE)

find_path(LEPT_HEADERS leptonica/allheaders.h)
find_library(LEPT_LIB leptonica lept)

find_path(TESS_HEADERS tesseract/baseapi.h)
find_library(TESS_LIB tesseract)

if(LEPT_HEADERS AND LEPT_LIB AND TESS_HEADERS AND TESS_LIB)
    set(HAVE_TESSERACT TRUE)
    message(STATUS "Tesseract ENABLED")
else()
    message(WARNING "Tesseract NOT found → building without OCR")
endif()

# Sources (FIXED CASE)
set(SOURCES
    src/main.cpp
    src/ocrprocessor.cpp
    src/utils.cpp
)

set(HEADERS
    src/ocrprocessor.h
    src/utils.h
)

# Windows icon
if(WIN32)
    list(APPEND SOURCES resources/app.rc)
endif()

# Create executable
qt_add_executable(OCRLLMProcessor
    ${SOURCES}
    ${HEADERS}
)

# QML module
qt_add_qml_module(OCRLLMProcessor
    URI App
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

# OpenSSL
find_package(OpenSSL REQUIRED)

target_link_libraries(OCRLLMProcessor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Widgets
    Qt6::Network
    Qt6::Pdf
    OpenSSL::Crypto
)

# Optional OCR
if(HAVE_TESSERACT)
    target_include_directories(OCRLLMProcessor PRIVATE
        ${LEPT_HEADERS}
        ${TESS_HEADERS}
    )
    target_link_libraries(OCRLLMProcessor PRIVATE
        ${TESS_LIB}
        ${LEPT_LIB}
    )
    target_compile_definitions(OCRLLMProcessor PRIVATE HAVE_TESSERACT=1)
else()
    target_compile_definitions(OCRLLMProcessor PRIVATE HAVE_TESSERACT=0)
endif()

# Windows GUI app
if(WIN32)
    set_target_properties(OCRLLMProcessor PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()