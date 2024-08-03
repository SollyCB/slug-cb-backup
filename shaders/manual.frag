#extension GL_GOOGLE_include_directive : require

#define FRAG
#include "../shader.h.glsl"

layout(location = 25) in vec3 dbg_norm;
layout(location = 26) in vec4 dbg_tang;

layout(push_constant) uniform pc {
    uint material_flags;
};

void main() {
    vec4 base_color;
    vec4 metallic_roughness;
    vec3 normal;
    vec3 emissive;
    float occlusion;

    if (bool(material_flags & GLTF_MATERIAL_BASE_COLOR_TEXTURE_BIT))
        base_color = texture(material_textures[0], fs_info.texcoord);
    else
        base_color = vec4(1);

    if (bool(material_flags & GLTF_MATERIAL_METALLIC_ROUGHNESS_TEXTURE_BIT))
        metallic_roughness = texture(material_textures[1], fs_info.texcoord);
    else
        metallic_roughness = vec4(1);

    if (bool(material_flags & GLTF_MATERIAL_NORMAL_TEXTURE_BIT))
        normal = normalize(texture(material_textures[2], fs_info.texcoord).xyz * material_ubo.mrno.z);

    if (bool(material_flags & GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT))
        occlusion = 1 + material_ubo.mrno.w * (texture(material_textures[3], fs_info.texcoord).r - 1);

    if (bool(material_flags & GLTF_MATERIAL_EMISSIVE_TEXTURE_BIT))
        emissive = texture(material_textures[4], fs_info.texcoord).rgb;
    else
        emissive = vec3(1);

    float metallic  = metallic_roughness.b * material_ubo.mrno.x;
    float roughness = metallic_roughness.g * material_ubo.mrno.y;
    base_color *= material_ubo.bbbb;
    emissive   *= material_ubo.eeea.xyz;

    /* V is the normalized vector from the shading location to the eye
     * L is the normalized vector from the shading location to the light
     * N is the surface normal in the same space as the above values
     * H is the half vector, where H = normalize(L + V) */
    vec3 light = base_color.xyz * vs_info.ambient.xyz;

    vec3 V = normalize(fs_info.tang_eye_pos.xyz - fs_info.tang_frag_pos);
    vec3 N = bool(material_flags & GLTF_MATERIAL_NORMAL_TEXTURE_BIT) ? normal : fs_info.tang_normal;

    float a = sq(roughness);

    for(uint i=0; i < dir_light_count; ++i) {
        vec3 L = normalize(fs_info.dir_lights[i].ts_light_pos - fs_info.tang_frag_pos);
        vec3 H = normalize(L + V);

        // The below function seems to give brighter diffuse. Not sure why. (defined in pbr.glsl)
        // vec3 matbrdf = material_brdf(base_color.xyz, metallic, roughness, V, L, H, N);

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

        vec3 diff = (vec3(1) - F) * (1 / PI) * mix(base_color.rgb, vec3(0), metallic);
        vec3 spec = (F * D * G) / max(4 * abs(dot(V, N)) * abs(dot(L, N)), 0.000001); // @Optimise Trying to prevent divide by zero error when either dot product is zero with this max(). This seems pretty optimal???

        vec3 matbrdf = spec + diff;

        matbrdf *= in_shadow(i);
        matbrdf *= emissive;

        if (bool(material_flags & GLTF_MATERIAL_OCCLUSION_TEXTURE_BIT))
            matbrdf *= occlusion;

        light += fs_info.dir_lights[i].color * matbrdf * max(dot(N, L), 0);
    }

    if (bool(material_flags & GLTF_MATERIAL_ALPHA_MODE_MASK_BIT))
        fc = vec4(light, base_color.a > material_ubo.eeea.w);
    else if (bool(material_flags & GLTF_MATERIAL_ALPHA_MODE_BLEND_BIT))
        fc = vec4(light, base_color.a);
    else
        fc = vec4(light, 1);
}
