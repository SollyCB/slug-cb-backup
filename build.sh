#!/bin/bash

{set -xe} 2>/dev/null

CF="-g -mbmi -msse4.1 -pthread -mlzcnt -fstack-protector-all -O0 -lm"

SHADERC="/home/solly/vulkan/1.3.261.1/x86_64/lib/libshaderc_combined.a"
L="-lglfw -lvulkan"

if gcc -std=c99 -c source.c -o source.o $CF $L ; then
    echo "Compiled source"
else
    echo "Build failed: compile source"
    exit 1
fi

# omg I want to get rid of this shaderc dependency
if g++ source.o -o exe $CF $SHADERC $L ; then
    echo "Linked shaderc"
else
    echo "Build failed: link shaderc"
    exit 1
fi

if [[ -f source.o ]]; then
    rm source.o
fi

echo "C Build Success"

shader_files=("manual.vert" "manual.frag" "floor.frag" "floor.vert")

for f in ${shader_files[@]}; do
   (cd shaders && glslang -V $f -o $f.spv)
done
