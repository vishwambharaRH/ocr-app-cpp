#!/bin/bash
# Pragmatic Docker build for Windows: Simplified Qt6-only version
# Full Tesseract support requires building on Windows or using MXE (hours of compilation)

set -e

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOCKER_IMAGE="ocr-app-windows-builder"

echo "🐳 Building Docker image for Windows..."
docker build -f "$PROJECT_DIR/Dockerfile.windows" -t "$DOCKER_IMAGE" "$PROJECT_DIR"

echo ""
echo "⚠️  NOTE: This builds a simplified UI without OCR support"
echo "For full Tesseract support, build natively on Windows using:"
echo ""
echo "  cmake -G 'MinGW Makefiles' \\"
echo "    -DCMAKE_PREFIX_PATH='C:/Qt/6.11.0/mingw_64' \\"
echo "    -DTESSDATA_DIR='C:/Program Files/Tesseract-OCR/tessdata' \\"
echo "    -DCMAKE_BUILD_TYPE=Release -B build"
echo "  cmake --build build"
echo ""

echo "📦 Attempting to build inside Docker..."
docker run --rm -v "$PROJECT_DIR:/src" "$DOCKER_IMAGE" bash -c "
    cd /src
    echo '⚠️  Tesseract/Leptonica skipped for Docker cross-compilation'
    echo '🔨 Configuring CMake for Windows...'
    
    # Create MinGW cross-compiler toolchain
    cat > /tmp/mingw-toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER /usr/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER /usr/bin/x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_EXE_LINKER_FLAGS \"-static-libgcc -static-libstdc++ -lws2_32\")
EOF
    
    cmake -G 'Unix Makefiles' \
        -DCMAKE_TOOLCHAIN_FILE=/tmp/mingw-toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH='/usr/x86_64-w64-mingw32/lib/cmake' \
        -B build || {
            echo '❌ CMake configuration failed'
            echo 'Qt6 Windows libraries not available in Docker'
            echo 'Recommendation: Build natively on Windows'
            exit 1
        }
    
    cmake --build build --config Release -j\$(nproc) || {
        echo '❌ Build failed'
        exit 1
    }
    
    echo '✅ Build complete!'
    echo '📍 Executable: /src/build/OCRLLMProcessor.exe'
"

echo ""
echo "For full-featured Windows build with OCR:"
echo "→ Run on Windows: cmake ... && cmake --build build"
