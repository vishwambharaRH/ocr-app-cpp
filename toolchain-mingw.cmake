# CMake toolchain file for cross-compiling to Windows with MinGW64 on macOS

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Set compilers
set(CMAKE_C_COMPILER /opt/homebrew/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /opt/homebrew/bin/x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER /opt/homebrew/bin/x86_64-w64-mingw32-windres)

# Search paths
set(CMAKE_FIND_ROOT_PATH /opt/homebrew/opt/mingw-w64)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Qt6 configuration for MinGW
# You'll need to specify this path or install Qt for MinGW
set(Qt6_DIR /opt/homebrew/opt/qt/lib/cmake/Qt6 CACHE PATH "Qt6 CMake config")

# Tesseract and Leptonica from Homebrew
set(LEPT_INCLUDE_DIR /opt/homebrew/opt/leptonica/include)
set(LEPT_LIB /opt/homebrew/opt/leptonica/lib/libleptonica.a)
set(TESS_INCLUDE_DIR /opt/homebrew/opt/tesseract/include)
set(TESS_LIB /opt/homebrew/opt/tesseract/lib/libtesseract.a)

message(STATUS "Cross-compiling for Windows using MinGW64")
