#ifndef SOL_CAMERA_H_INCLUDE_GUARD_
#define SOL_CAMERA_H_INCLUDE_GUARD_

#include "sol_math.h"
#include "builtin_wrappers.h"

struct camera {
    vec3 front;
    vec3 up;
    vec3 right;
    vec3 pos;

    float speed;
    float sens;

    float last_x = 320;
    float last_y = 240;

    bool have_turned; // feels lame but whatever for now, im sure there is a cooler method
};

inline static camera create_camera() {
    return {
        .front = {0,  0, -1},
        .up    = {0,  1,  0},
        .right = {1,  0,  0},
        .pos   = {0,  0,  3},
        .speed = 5,
        .sens  = 0.1,
    };
}

inline static mat4 camera_lookat(Camera *cam) {
    return look_at(&cam->right, &cam->up, &cam->front, &cam->pos);
}

inline static void camera_turn(Camera *cam, float dx, float dy) {
    float yaw   = dx * cam->sens;
    float pitch = dy * cam->sens;

    if (pitch > 89)
        pitch = 89;
    else if (pitch < -89)
        pitch = -89;

    float r_yaw = rad(yaw);
    float r_pitch = rad(pitch);

    cam->front.x = cosf(r_yaw) * cosf(r_pitch);
    cam->front.y = sinf(r_pitch);
    cam->front.z = sinf(r_yaw) * cosf(r_pitch);

    normalize_vec3(&cam->front);
    vec3 world_up = {0, 1, 0};

    cam->right = cross_vec3(&cam->front, &world_up);
    normalize_vec3(&cam->right);

    cam->up = cross_vec3(&cam->right, &cam->front);
    normalize_vec3(&cam->up);
}

inline static void camera_move(Camera *cam, int forwards, int right) {
    vec3 tmp;

    tmp = scalar_mul_vec3(&cam->front, cam->speed * forwards);
    cam->pos = add_vec3(&cam->pos, &tmp);

    tmp = scalar_mul_vec3(&cam->right, cam->speed * right);
    cam->pos = add_vec3(&cam->pos, &tmp);
}
