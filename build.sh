#!/bin/bash

{set -xe} 2>/dev/null

CF="-g -mbmi -msse4.1 -pthread -mlzcnt -fstack-protector-all -O3 -Wall -Wextra -Wnarrowing -Werror \
    -Wno-unused -Wunused-value -Wno-missing-field-initializers -Wno-cast-function-type -Wno-unused-parameter"

L="-lglfw -lvulkan -L/home/solly/vulkan/1.3.283.0/x86_64/lib/ -lshaderc_combined"

CL="gcc"
CLPP="g++"

if $CL -c -std=c99 source.c $CF; then
    echo "Compiled source"
else
    echo "Build failed: compile source"
    exit 1
fi

if $CLPP source.o -o exe $CF $L; then
    echo "Linked shaderc"
else
    echo "Build failed: link shaderc"
    exit 1
fi

if [[ -f source.o ]]; then
    rm source.o
fi

echo "Build complete"
