cmake_minimum_required(VERSION 3.22)
project(OCRLLMProcessor VERSION 1.0 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC-specific runtime library setting
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Optimization flags for Release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(MSVC)
        add_compile_options(/O2 /GL)
        add_link_options(/LTCG)
    else()
        add_compile_options(-O3)
    endif()
endif()

# Optional TESSDATA_DIR - can be set via -DTESSDATA_DIR=/path/to/tessdata
if(NOT DEFINED TESSDATA_DIR)
    set(TESSDATA_DIR "")
endif()

# For cross-compilation to Windows, Qt6 needs to be pre-installed or skipped
# Check if we're cross-compiling to Windows
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Cross-compiling to Windows...")
    # For Docker cross-compilation builds
    # Tesseract and Leptonica are optional
    set(OPTIONAL_LIBS OFF)
else()
    set(OPTIONAL_LIBS ON)
endif()

# Find Qt6 components
if(NOT DEFINED Qt6_DIR AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Help find Qt6 on native Windows/macOS/Linux builds
    message(STATUS "Searching for Qt6...")
endif()

find_package(Qt6 COMPONENTS
    Core
    Gui
    Quick
    Widgets
    Network
    Pdf
    REQUIRED
)

# Try to find Tesseract and Leptonica (optional for cross-compilation)
# Windows users should install tesseract-ocr from: https://github.com/UB-Mannheim/tesseract/wiki
find_path(LEPT_HEADERS leptonica/allheaders.h
    HINTS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        /opt/mingw-libs/mingw/include
        "C:/Program Files/Tesseract-OCR/include"
        "C:/Program Files (x86)/Tesseract-OCR/include"
        "$ENV{PROGRAMFILES}/Tesseract-OCR/include"
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
)

find_library(LEPT_LIB leptonica lept
    HINTS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        /opt/mingw-libs/mingw/lib
        "C:/Program Files/Tesseract-OCR/lib"
        "C:/Program Files (x86)/Tesseract-OCR/lib"
        "$ENV{PROGRAMFILES}/Tesseract-OCR/lib"
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
)

find_path(TESS_HEADERS tesseract/baseapi.h
    HINTS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        /opt/mingw-libs/mingw/include
        "C:/Program Files/Tesseract-OCR/include"
        "C:/Program Files (x86)/Tesseract-OCR/include"
        "$ENV{PROGRAMFILES}/Tesseract-OCR/include"
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
)

find_library(TESS_LIB tesseract tesseract-main
    HINTS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        /opt/mingw-libs/mingw/lib
        "C:/Program Files/Tesseract-OCR/lib"
        "C:/Program Files (x86)/Tesseract-OCR/lib"
        "$ENV{PROGRAMFILES}/Tesseract-OCR/lib"
    PATH_SUFFIXES lib
    NO_DEFAULT_PATH
)

# Set Tesseract/Leptonica availability flag
if(LEPT_HEADERS AND LEPT_LIB AND TESS_HEADERS AND TESS_LIB)
    set(HAVE_TESSERACT TRUE)
    message(STATUS "Tesseract/Leptonica found - OCR support enabled")
else()
    set(HAVE_TESSERACT FALSE)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message(WARNING 
            "Tesseract/Leptonica development packages not found.\n"
            "Windows: Download from https://github.com/UB-Mannheim/tesseract/wiki\n"
            "macOS: brew install tesseract leptonica\n"
            "Linux: sudo apt-get install libtesseract-dev libleptonica-dev\n"
            "Building without OCR support..."
        )
    else()
        message(STATUS "Cross-compiling to Windows - OCR support disabled (optional)")
    endif()
endif()

message(STATUS "Found Leptonica headers: ${LEPT_HEADERS}")
message(STATUS "Found Leptonica library: ${LEPT_LIB}")
message(STATUS "Found Tesseract headers: ${TESS_HEADERS}")
message(STATUS "Found Tesseract library: ${TESS_LIB}")

# Include directories - only add Tesseract if found
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(HAVE_TESSERACT)
    include_directories(
        ${LEPT_HEADERS}
        ${TESS_HEADERS}
    )
endif()

# OpenSSL is required to sign JWT assertions for Google service-account auth
find_package(OpenSSL REQUIRED)

# Source files
set(SOURCES
    src/main.cpp
    src/OcrProcessor.cpp
)

set(HEADERS
    src/OcrProcessor.h
)

# Utility sources (testable logic)
list(APPEND SOURCES src/utils.cpp)
list(APPEND HEADERS src/utils.h)

# Icon and resource handling for different platforms

# macOS bundle icon
set(MACOS_ICON_FILE ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.icns)
if(APPLE AND EXISTS ${MACOS_ICON_FILE})
    set_source_files_properties(${MACOS_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    list(APPEND SOURCES ${MACOS_ICON_FILE})
endif()

# Windows resource file with embedded icon
set(WINDOWS_RC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc)
if(WIN32 AND EXISTS ${WINDOWS_RC_FILE})
    list(APPEND SOURCES ${WINDOWS_RC_FILE})
    message(STATUS "Added Windows resource file for icon embedding")
endif()

# Create executable
qt_add_executable(OCRLLMProcessor
    ${SOURCES}
    ${HEADERS}
)

# Add QML module
qt6_add_qml_module(OCRLLMProcessor
    URI App
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

# Link libraries
target_link_libraries(OCRLLMProcessor PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Widgets
    Qt6::Network
    Qt6::Pdf
    OpenSSL::Crypto
)

# Conditionally link Tesseract if available
if(HAVE_TESSERACT)
    target_link_libraries(OCRLLMProcessor PRIVATE
        ${TESS_LIB}
        ${LEPT_LIB}
    )
    target_compile_definitions(OCRLLMProcessor PRIVATE HAVE_TESSERACT=1)
else()
    target_compile_definitions(OCRLLMProcessor PRIVATE HAVE_TESSERACT=0)
endif()

# ------------------ Unit tests (GoogleTest) ------------------
include(CTest)
enable_testing()

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(test_utils
        tests/test_utils.cpp
        src/utils.cpp
)
target_link_libraries(test_utils PRIVATE GTest::gtest_main Qt6::Core)
target_include_directories(test_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
gtest_discover_tests(test_utils)


# Platform-specific linking
if(UNIX AND NOT APPLE)
    target_link_libraries(OCRLLMProcessor PRIVATE pthread)
endif()

# Add compile definition for TESSDATA_DIR if provided
if(TESSDATA_DIR)
    target_compile_definitions(OCRLLMProcessor PRIVATE
        APP_TESSDATA_DIR=\"${TESSDATA_DIR}\"
    )
    message(STATUS "TESSDATA_DIR set to: ${TESSDATA_DIR}")
endif()

# Installation rules
install(TARGETS OCRLLMProcessor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# macOS bundle configuration
if(APPLE)
    set_target_properties(OCRLLMProcessor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_GUI_IDENTIFIER com.ocrapp.OCRLLMProcessor
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_NAME "OCRLLMProcessor"
        MACOSX_BUNDLE_ICON_FILE "app.icns"
    )
    
    # Only set Info.plist if the file exists
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in)
        set_target_properties(OCRLLMProcessor PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
        )
    endif()
endif()

# Windows executable properties - creates GUI app without console window
if(WIN32)
    set_target_properties(OCRLLMProcessor PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

# Print build summary
message(STATUS "")
message(STATUS "========================================")
message(STATUS "OCR App Build Configuration")
message(STATUS "========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Qt version: ${Qt6_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Tesseract found: YES")
message(STATUS "Leptonica found: YES")
if(TESSDATA_DIR)
    message(STATUS "TESSDATA_DIR: ${TESSDATA_DIR}")
else()
    message(STATUS "TESSDATA_DIR: Will be auto-detected at runtime")
endif()
message(STATUS "========================================")
message(STATUS "")