#version 460

layout(location = 0) in vec4 in_col;

layout(location = 0) out vec4 col;

void main() {
    col = in_col;
}
