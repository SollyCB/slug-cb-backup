#version 450

#extension GL_EXT_debug_printf : enable

layout(set = 1, binding = 0) uniform sampler2D shadow_maps[1];

layout(set = 3, binding = 0) uniform Material_Ubo {
    vec4 bbbb; // float base_color[4];
    vec4 mrno; // float metallic_factor; float roughness_factor; float normal_scale; float occlusion_strength;
    vec4 eeea; // float emissive_factor[3]; float alpha_cutoff;
} material_ubo;

layout(set = 4, binding = 0) uniform sampler2D material_textures[2];

layout(location = 0) out vec4 fc;

struct Directional_Light {
    vec3 color;
    vec3 ts_light_pos;
    vec3 ls_frag_pos;
};

layout(location = 0) flat in uint dir_light_count;

layout(location = 1) in struct Fragment_Info {
    vec2 texcoord;
    vec3 frag_pos;
    vec3 tang_normal;
    vec3 tang_frag_pos;
    vec3 tang_view_pos;
    vec3 ambient;

    Directional_Light dir_lights[2];
} fs_info;

float in_shadow(uint i) {
    vec3 pc = fs_info.dir_lights[i].ls_frag_pos.xyz;
    pc = pc * 0.5 + 0.5;
    float d = texture(shadow_maps[i], pc.xy).r;
    return pc.z > d ? 1 : 0;
}

const float PI = 3.1415926;

void pa(float a[4]) {
    debugPrintfEXT("%f, %f, %f, %f\n", a[0], a[1], a[2], a[3]);
}

float heaviside(float x) {
    return x > 0 ? 1 : 0;
}

float sq(float x) {
    return x * x;
}

void pmatubo() {
    debugPrintfEXT("base_color: %f, %f, %f, %f\nmetallic: %f\nroughness: %f\nnormal: %f\nocclusion: %f\nemissive: %f, %f, %f\nalpha_cutoff: %f\n",
            material_ubo.bbbb.x, material_ubo.bbbb.y, material_ubo.bbbb.z, material_ubo.bbbb.w,
            material_ubo.mrno.x, material_ubo.mrno.y, material_ubo.mrno.z, material_ubo.mrno.w,
            material_ubo.eeea.x, material_ubo.eeea.y, material_ubo.eeea.z, material_ubo.eeea.w);
}

void pv4(vec4 v) {
    debugPrintfEXT("%f, %f, %f, %f\n", v.x, v.y, v.z, v.w);
}

void pv3(vec3 v) {
    debugPrintfEXT("%f, %f, %f\n", v.x, v.y, v.z);
}

void main() {
    vec4 base_color = texture(material_textures[0], fs_info.texcoord) * material_ubo.bbbb;

    vec4 metallic_roughness = texture(material_textures[1], fs_info.texcoord);
    float metallic = metallic_roughness.b * material_ubo.mrno.x;
    float roughness = metallic_roughness.g * material_ubo.mrno.y;

    /* V is the normalized vector from the shading location to the eye
     * L is the normalized vector from the shading location to the light
     * N is the surface normal in the same space as the above values
     * H is the half vector, where H = normalize(L + V) */
    vec3 light = vec3(base_color.x * fs_info.ambient.x, base_color.y * fs_info.ambient.y, base_color.z * fs_info.ambient.z);

    vec3 V = normalize(fs_info.tang_view_pos.xyz - fs_info.tang_frag_pos);
    vec3 N = normalize(fs_info.tang_normal);

    float a = sq(roughness);

    for(uint i=0; i < dir_light_count; ++i) {
        vec3 L = normalize(fs_info.dir_lights[i].ts_light_pos - fs_info.tang_frag_pos);
        vec3 H = normalize(L + V);

        float Dtop = sq(a) * heaviside(dot(N, H));
        float Dbot = PI * sq(sq(dot(N, H)) * (sq(a) - 1) + 1);
        float D = Dtop / Dbot;

        float Gltop = 2 * abs(dot(N, L)) * heaviside(dot(H, L));
        float Glbot = abs(dot(N, L)) + sqrt(sq(a) + (1 - sq(a)) * sq(dot(N, L)));
        float Grtop = 2 * abs(dot(N, V)) * heaviside(dot(H, V));
        float Grbot = abs(dot(N, V)) + sqrt(sq(a) + (1 - sq(a)) * sq(dot(N, V)));
        float G = (Gltop / Glbot) * (Grtop / Grbot);

        vec3 f0 = mix(vec3(0.04), base_color.rgb, metallic);
        vec3 F = f0 + (vec3(1) - f0) * pow(1 - abs(dot(V, H)), 5);

        vec3 diff = (vec3(1) - F) * (1 / PI) * vec3(0.3) * mix(base_color.rgb, vec3(0), metallic);
        vec3 spec = (F * D * G) / (4 * abs(dot(V, N)) * abs(dot(L, N)));
        vec3 matbrdf = diff + spec;

        matbrdf *= 1 - in_shadow(i);

        light += vec3(fs_info.dir_lights[i].color.r * matbrdf.r,
                      fs_info.dir_lights[i].color.g * matbrdf.g,
                      fs_info.dir_lights[i].color.b * matbrdf.b);
    }

    fc = vec4(light, 1);
}
