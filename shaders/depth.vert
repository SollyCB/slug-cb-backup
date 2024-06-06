#version 450
#extension GL_EXT_debug_printf : enable

layout(location = 0) in vec3 pos;

layout(push_constant) uniform pc {
    mat4 m;
    mat4 v;
    mat4 p;
    // mat4 mvp;
};

void pmat(mat4 m) {
    debugPrintfEXT("[%f, %f, %f, %f,\n %f, %f, %f, %f,\n %f, %f, %f, %f,\n %f, %f, %f, %f]\n",
    m[0][0], m[1][0], m[2][0], m[3][0],
    m[0][1], m[1][1], m[2][1], m[3][1],
    m[0][2], m[1][2], m[2][2], m[3][2],
    m[0][3], m[1][3], m[2][3], m[3][3]);
}

void pv4(vec4 v) {
    debugPrintfEXT("[%f, %f, %f, %f,]\n", v.x, v.y, v.z, v.w);
}

void main() {
    gl_Position = p * v * m * vec4(pos, 1);
    // pmat(m);
    // pv4(gl_Position);
    // pv4(m * v * vec4(pos, 1));
}
