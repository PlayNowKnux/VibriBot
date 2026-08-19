#!/usr/bin/env sh
set -eu
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
mkdir -p ../bin
cp build/mojib_tts ../bin/mojib_tts
chmod +x ../bin/mojib_tts
echo "Built ../bin/mojib_tts"
