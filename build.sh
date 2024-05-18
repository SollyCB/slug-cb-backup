#!/bin/bash

{set -xe} 2>/dev/null

CF="-g -mbmi -msse4.1 -pthread -mlzcnt -fstack-protector-all -O0 -lm"

SHADERC="/home/solly/vulkan/1.3.261.1/x86_64/lib/libshaderc_combined.a"
L="-lglfw -lvulkan"

gcc -std=c99 -c source.c -o source.o $CF $L

# omg I want to get rid of this shaderc dependency
g++ source.o -o exe $CF $SHADERC $L

echo "build succeeded"

if [[ -f source.o ]]; then
    rm source.o
fi
