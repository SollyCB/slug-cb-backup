#version 450

layout(location = 8) in vec3 in_normal;
layout(location = 9) in vec2 in_texcoord_0;

struct Directional_Light {
    vec3 color;
    vec3 position;
    vec4 light_space_frag_pos;
};
struct Point_Light {
    float attenuation;
    vec3 color;
    vec3 position;
    vec4 light_space_frag_pos;
};

layout(location = 12) flat in struct Fragment_Info {
    vec3 eye;
    vec3 frag_pos;
    vec3 ambient;
    uint dir_light_count;
    Directional_Light dir_lights[2];
    uint point_light_count;
    Point_Light point_lights[3];
} fs_info;

const float PI = 3.1415926;

float heaviside(float x) {
    return x > 0 ? 1 : 0;
}

float square(float x) {
    return x * x;
}

float microfacet_distribution(float r_sq, float n_dot_h) {
    float top = r_sq * heaviside(n_dot_h);
    float bottom = PI * square(square(n_dot_h) * (r_sq - 1) + 1);
    return top / bottom;
}

float masking_shadowing(float r_sq, float n_dot_l, float h_dot_l, float n_dot_v, float h_dot_v) {
    float top_left = 2 * abs(n_dot_l) * heaviside(h_dot_l);
    float bottom_left = abs(n_dot_l) + sqrt(r_sq + (1 - r_sq) * square(n_dot_l));
    float top_right = 2 * abs(n_dot_v) * heaviside(h_dot_v);
    float bottom_right = abs(n_dot_v) + sqrt(r_sq + (1 - r_sq) * square(n_dot_v));
    return (top_left / bottom_left) * (top_right / bottom_right);
}

float specular_brdf(float r_sq, vec3 eye_pos, vec3 light_pos, vec3 surface_normal, vec3 half_vector) {
    float D = microfacet_distribution(r_sq, dot(surface_normal, half_vector));
    float G = masking_shadowing(r_sq, dot(surface_normal, light_pos), dot(half_vector, light_pos), dot(surface_normal, eye_pos), dot(half_vector, eye_pos));
    float bottom = 4 * abs(dot(surface_normal, light_pos)) * abs(dot(surface_normal, eye_pos));
    return (G * D) / bottom;
}

vec3 material_brdf(vec3 base_color, float metallic, float roughness, vec3 eye_pos, vec3 light_pos, vec3 surface_normal) {
    vec3 half_vector = normalize(light_pos + eye_pos);
    float v_dot_h = dot(eye_pos, half_vector);
    vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);
    vec3 fresnel = f0 + (1 - f0) * pow((1 - abs(v_dot_h)), 5);

    vec3 c_diff = mix(base_color.rgb, vec3(0), metallic);
    float r_sq = roughness * roughness;

    vec3 diffuse = (1 - fresnel) * (1 / PI) * c_diff;
    vec3 specular = fresnel * specular_brdf(r_sq, eye_pos, light_pos, surface_normal, half_vector);

    return diffuse + specular;
}

layout(set = 1, binding = 0) uniform sampler2D directional_shadow_maps[2];
layout(set = 1, binding = 1) uniform sampler3D point_shadow_maps[3];

float zero_if_in_shadow_directional(uint light_index) {
    vec3 projection = fs_info.dir_lights[light_index].light_space_frag_pos.xyz /
            fs_info.dir_lights[light_index].light_space_frag_pos.w;
    projection = projection * 0.5 + 0.5;
    return float((projection.z - 0.05) < texture(directional_shadow_maps[light_index], projection.xy).r);
}

float zero_if_in_shadow_point(uint light_index) {
    vec3 projection = fs_info.point_lights[light_index].light_space_frag_pos.xyz /
            fs_info.point_lights[light_index].light_space_frag_pos.w;
    projection = projection * 0.5 + 0.5;
    return float((projection.z - 0.05) < texture(point_shadow_maps[light_index], projection).r);
}

layout(set = 0, binding = 1) uniform Material_Ubo {
    vec4 base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    vec3 emissive_factor;
    float alpha_cutoff;
} material_ubo;

layout(set = 2, binding = 0) uniform sampler2D textures[1];


layout(location = 0) out vec4 frag_color;

void main() {
    vec4 base_color = texture(textures[0], in_texcoord_0) * material_ubo.base_color_factor;
    float roughness = 1;
    float metallic = 0;
    vec3 normal = in_normal;
    float occlusion = 1;
    vec3 emissive = vec3(0,0,0);

    vec3 light = vec3(0);

    for(uint i = 0; i < fs_info.dir_light_count; ++i) {
        vec3 light_dir = normalize(fs_info.dir_lights[i].position - fs_info.frag_pos);
        vec3 brdf = material_brdf(base_color.xyz, metallic, roughness, fs_info.eye.xyz, light_dir, normal);

        light = light + zero_if_in_shadow_directional(i) * brdf;
    }

    for(uint i = 0; i < fs_info.point_light_count; ++i) {
        vec3 light_dir = normalize(fs_info.point_lights[i].position - fs_info.frag_pos);
        vec3 brdf = material_brdf(base_color.xyz, metallic, roughness, fs_info.eye.xyz, light_dir, normal);

        light = light + zero_if_in_shadow_point(i) * brdf * fs_info.point_lights[i].attenuation;
    }

    frag_color = vec4(light + emissive, base_color.w);
}
