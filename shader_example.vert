float heaviside(float x) {
    return x > 0 ? 1 : 0;
}

float square(float x) {
    return x * x;
}

float microfacet_distribution(float r_sq, vec3 n_dot_h) {
    float top = r_sq * heaviside(n_dot_h);
    float bottom = pi * square(square(n_dot_h) * (r_sq - 1) + 1));
    return top / bottom;
}

float masking_shadowing(float r_sq, vec3 n_dot_l, vec3 h_dot_l, vec3 n_dot_v, vec3 h_dot_v) {
    float top_left = 2 * abs(n_dot_l) * heaviside(h_dot_l);
    float bottom_left = abs(n_dot_l) + sqrt(r_sq + (1 - r_sq) * square(n_dot_l))
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

float material_brdf(vec4 base_color, float metallic, float roughness, vec3 eye_pos, vec3 light_pos, vec3 surface_normal) {
    const black = 0;

    vec3 half_vector = normalize(light_pos + eye_pos);
    float v_dot_h = dot(eye_pos, half_vector);
    float f0 = lerp(base_color.rgb, metallic, 0.04);
    float fresnel = f0 + (1 - f0) * pow((1 - abs(v_dot_h)), 5);

    float c_diff = mix(black, metallic, base_color.rgb)
    float roughness_sq = roughness * roughness;

    float diffuse = (1 - fresnel) * (1 / pi) * c_diff;
    float specular = fresnel * specular_brdf(r_sq, eye_pos, light_pos, surface_normal, half_vector);

    return diffuse + specular;
}
