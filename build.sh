#!/bin/bash

{set -xe} 2>/dev/null

CF="-g -mbmi -msse4.1 -pthread -mlzcnt -fstack-protector-all -O0 -lm -Wall -Wextra -Wnarrowing -Werror \
    -Wno-unused -Wunused-value -Wno-missing-field-initializers -Wno-cast-function-type"

# why the fuck do I have to specify the full path for shaderc?? Because its static???
L="-lglfw -lvulkan -L/home/solly/vulkan/1.3.283.0/x86_64/lib/ -lshaderc_combined"

if gcc -c -std=c99 source.c $CF; then
    echo "Compiled source"
else
    echo "Build failed: compile source"
    exit 1
fi

if g++ source.o -o exe $CF $L; then
    echo "Linked shaderc"
else
    echo "Build failed: link shaderc"
    exit 1
fi

if [[ -f source.o ]]; then
    rm source.o
fi

echo "Build complete"

# SHADER_C=0
#
# if [[ !$SHADER_C ]]; then
#     if gcc -DSHADER_C=0 -I/home/solly/vulkan/1.3.283.0/x86_64/include -std=c99 source.c -o exe $CF $L ; then
#         echo "Compiled source"
#     else
#         echo "Build failed: compile source"
#         exit 1
#     fi
# else
#     if gcc -DSHADER_C=1 -I/home/solly/vulkan/1.3.283.0/x86_64/include -std=c99 -c source.c -o source.o $CF $L ; then
#         echo "Compiled source"
#     else
#         echo "Build failed: compile source"
#         exit 1
#     fi
#
#     # omg I want to get rid of this shaderc dependency
#     if g++ source.o -o exe $CF $LIB_SHADERC $L ; then
#         echo "Linked shaderc"
#     else
#         echo "Build failed: link shaderc"
#         exit 1
#     fi
# fi
#
# if [[ -f source.o ]]; then
#     rm source.o
# fi
#
# echo "C Build Success"
#
# shader_files=("manual.vert" "manual.frag" "floor.frag" "floor.vert" "depth.vert")
#
# for f in ${shader_files[@]}; do
#    (cd shaders && glslang -g -V $f -o $f.spv)
# done
