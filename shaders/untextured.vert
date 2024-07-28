#version 460

#extension GL_EXT_debug_printf : require

#define DIR_LIGHT_COUNT 1
#define CSM_COUNT 4
#define CSM_BLEND_BAND (2)
#define JOINT_COUNT 19
#define MORPH_WEIGHT_COUNT 1
#define SPLIT_SHADOW_MVP 0
#define SHADER_MAX_MESH_INSTANCE_COUNT 2

struct In_Directional_Light {
    vec4 position;
    vec4 color;
    mat4 space[CSM_COUNT];
};

struct Vertex_Info {
    vec4 eye_pos;
    vec4 ambient;
    vec4 cascade_boundaries;
    mat4 model;
    mat4 view;
    mat4 proj;

    In_Directional_Light dir_lights[DIR_LIGHT_COUNT];

    uvec4 dlcx; // dir light count, use light view, light view cascade index, null
};

struct Vertex_Transforms {
    mat4 joints[JOINT_COUNT];
    vec4 weights[(MORPH_WEIGHT_COUNT / 4) + 1];
};

layout(set = 0, binding = 0) uniform UBO_Vertex_Info { Vertex_Info vs_info; };
layout(set = 1, binding = 0) uniform UBO_Transforms { Vertex_Transforms transforms[SHADER_MAX_MESH_INSTANCE_COUNT]; };

layout(location = 0) in vec3  in_position;
layout(location = 1) in uvec4 in_joints;
layout(location = 2) in vec4  in_weights;

mat4 skin_calc() {
   return in_weights.x * transforms[gl_InstanceIndex].joints[in_joints.x] +
          in_weights.y * transforms[gl_InstanceIndex].joints[in_joints.y] +
          in_weights.z * transforms[gl_InstanceIndex].joints[in_joints.z] +
          in_weights.w * transforms[gl_InstanceIndex].joints[in_joints.w];
}

void pv4(vec4 v) {
    debugPrintfEXT("%f, %f, %f, %f\n", v.x, v.y, v.z, v.w);
}

void pmat(mat4 m) {
    debugPrintfEXT("[%f, %f, %f, %f,\n  %f, %f, %f, %f,\n  %f, %f, %f, %f,\n  %f, %f, %f, %f]\n",
    m[0][0], m[1][0], m[2][0], m[3][0],
    m[0][1], m[1][1], m[2][1], m[3][1],
    m[0][2], m[1][2], m[2][2], m[3][2],
    m[0][3], m[1][3], m[2][3], m[3][3]);
}

void main() {
    vec4 pos = vec4(in_position, 1);
    gl_Position = vs_info.proj * vs_info.view * vs_info.model * skin_calc() * pos;

    // pv4(skin_calc() * pos);
    // pmat(skin_calc());

}
