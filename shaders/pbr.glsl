const float PI = 3.1415926;

float sq(float x) {
    return x * x;
}

float heaviside(float x) {
    return x > 0 ? 1 : 0;
}

float microfacet_distribution(float a, vec3 V, vec3 L, vec3 H, vec3 N) {
    float Dtop = sq(a) * heaviside(dot(N, H));
    float Dbot = PI * sq(sq(dot(N, H)) * (sq(a) - 1) + 1);
    float D = Dtop / Dbot;
    return D;
}

float masking_shadowing(float a, vec3 V, vec3 L, vec3 H, vec3 N) {
    float Gltop = 2 * abs(dot(N, L)) * heaviside(dot(H, L));
    float Glbot = abs(dot(N, L)) + sqrt(sq(a) + (1 - sq(a)) * sq(dot(N, L)));
    float Grtop = 2 * abs(dot(N, V)) * heaviside(dot(H, V));
    float Grbot = abs(dot(N, V)) + sqrt(sq(a) + (1 - sq(a)) * sq(dot(N, V)));
    float G = (Gltop / Glbot) * (Grtop / Grbot);
    return G;
}

float visibility(float a, vec3 V, vec3 L, vec3 H, vec3 N) {
    float ms = masking_shadowing(a, V, L, H, N);
    float vs = ms / (4 * abs(dot(N, L)) * abs(dot(N, L)));
    return vs;
}

float specular_brdf(float a, vec3 V, vec3 L, vec3 H, vec3 N) {
    return visibility(a, V, L, H, N) * microfacet_distribution(a, V, L, H, N);
}

vec3 conductor_fresnel(vec3 base_color, float a, vec3 V, vec3 L, vec3 H, vec3 N) {
    float bsdf = specular_brdf(a, V, L, H, N);
    return bsdf * (base_color + (1 - base_color) * (1 - pow(abs(dot(V, H)), 5)));
}

vec3 diffuse_brdf(vec3 color) {
    return (1/PI) * color;
}

vec3 fresnel_mix(vec3 base_color, float a, vec3 V, vec3 L, vec3 H, vec3 N) {
    float f0 = pow((1 - 1.5) / (1 + 1.5), 2);
    float fr = f0 + (1 - f0) * pow(1 - abs(dot(V, H)), 5);
    return mix(base_color, vec3(specular_brdf(a, V, L, H, N)), fr);
}

vec3 material_brdf(vec3 base_color, float metallicness, float roughness,
                   vec3 V, vec3 L, vec3 H, vec3 N)
{
    float a = roughness * roughness;

    return mix(fresnel_mix(base_color, a, V, L, H, N),
               conductor_fresnel(base_color, a, V, L, H, N),
               metallicness);
}

