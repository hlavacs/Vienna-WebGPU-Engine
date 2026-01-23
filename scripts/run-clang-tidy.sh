#!/usr/bin/env bash
# run-clang-tidy.sh
# Usage: ./run-clang-tidy.sh [build_dir] [dry|fix]

BUILD_DIR=${1:-build/Windows/Debug}   # default build folder
MODE=${2:-fix}                        # default: apply fixes
HEADER_FILTER=".*"

echo "Running clang-tidy on all .cpp, .h, .hpp in src and include..."
echo "Excluding 'external' folders"
echo "Build dir: $BUILD_DIR"
echo "Mode: $MODE"

# Find all source and header files, exclude 'external'
FILES=$(find src include -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) ! -path "*/external/*")

for f in $FILES; do
    echo "Processing $f"
    if [ "$MODE" == "fix" ]; then
        clang-tidy "$f" -p "$BUILD_DIR" --header-filter="$HEADER_FILTER" -fix
    else
        clang-tidy "$f" -p "$BUILD_DIR" --header-filter="$HEADER_FILTER"
    fi
done
