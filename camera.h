#ifndef SOL_CAMERA_H_INCLUDE_GUARD_
#define SOL_CAMERA_H_INCLUDE_GUARD_

#include "defs.h"
#include "glfw.h"
#include "math.h"

enum {
    CAMERA_MOVED  = 0x01,
    CAMERA_ACTIVE = 0x02,
};

enum {
    CAMERA_MODE_NORMAL,
    CAMERA_MODE_FLY,
    CAMERA_MODE_LIGHT,
};

struct camera {
    vector pos;
    vector dir;
    uint flags;
    uint mode;
    uint csi;
    float fov;
    float sens,speed;
    float pitch,yaw;
    float x,y;
    float zoom;
};

static inline void camera_look(struct camera *c, float r, float u, float dt)
{

    if (!flag_check(c->flags, CAMERA_MOVED)) {
        c->flags |= CAMERA_MOVED;
        c->x = r;
        c->y = u;
        return;
    }

    float x = (r - c->x) * c->sens * dt;
    float y = (u - c->y) * c->sens * dt;

    c->yaw   += x;
    c->pitch -= y;
    c->pitch  = clamp(c->pitch, -0.9, 0.9);

    float pot = PI / 2;

    c->dir.x = cosf(c->yaw   * pot) * cosf(c->pitch * pot);
    c->dir.y = sinf(c->pitch * pot);
    c->dir.z = sinf(c->yaw   * pot) * cosf(c->pitch * pot);

    c->dir = normalize(c->dir);

    c->x = r;
    c->y = u;

    #if 0
    static float time = 0;
    time += dt;
    if (time > 1) {
        time = 0;
        print("dir: ");
        print_vector(c->dir);
        print(", pos: ");
        println_vector(c->pos);
    }
    #endif
}

static inline void camera_move(struct camera *c, float f, float r, float u, float dt)
{
    vector vf = normalize(vector3(c->dir.x, 0, c->dir.z));
    vector vr = normalize(cross(vf, vector3(0, 1, 0)));

    c->pos.x += vf.x * f * c->speed * dt;
    c->pos.z += vf.z * f * c->speed * dt;

    c->pos.x += vr.x * r * c->speed * dt;
    c->pos.z += vr.z * r * c->speed * dt;

    c->pos.y += u * c->speed * dt;
}

static inline void center_camera_and_cursor(struct camera *c, struct window *w)
{
    c->dir = vector3(0, 0, 1);

    double x,y;
    glfwGetCursorPos(w->window, &x, &y);
    c->x = x;
    c->y = y;
}

static inline void update_camera(struct camera *c, struct window *w, float dt)
{
    if (c->mode == CAMERA_MODE_FLY) {
        double x,y;
        glfwGetCursorPos(w->window, &x, &y);
        camera_look(c, x, y, dt);

        static float time = 0;
        time += dt;

        if (time > 1) {
            time = 0;
        }

        c->x = x;
        c->y = y;
    }

    float fwd = 0;
    float right = 0;
    float up = 0;

    if (glfwGetKey(w->window, GLFW_KEY_C) == GLFW_PRESS)
        center_camera_and_cursor(c, w);

    if (glfwGetKey(w->window, GLFW_KEY_W) == GLFW_PRESS)
        fwd = 1;
    if (glfwGetKey(w->window, GLFW_KEY_A) == GLFW_PRESS)
        right = -1;
    if (glfwGetKey(w->window, GLFW_KEY_S) == GLFW_PRESS)
        fwd = -1;
    if (glfwGetKey(w->window, GLFW_KEY_D) == GLFW_PRESS)
        right = 1;
    if (glfwGetKey(w->window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        up = 1;
        c->mode = CAMERA_MODE_FLY;
        glfwSetInputMode(w->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
    if (glfwGetKey(w->window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        up = -1;


    if (c->mode != CAMERA_MODE_LIGHT) {
        if (glfwGetKey(w->window, GLFW_KEY_1) == GLFW_PRESS)
            c->speed /= 2;
        if (glfwGetKey(w->window, GLFW_KEY_2) == GLFW_PRESS)
            c->speed *= 2;
    }

    if (glfwGetKey(w->window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        up = 1;
        c->mode = CAMERA_MODE_NORMAL;
        glfwSetInputMode(w->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    if (glfwGetKey(w->window, GLFW_KEY_L) == GLFW_PRESS)
        c->mode = CAMERA_MODE_LIGHT;
    if (glfwGetKey(w->window, GLFW_KEY_Q) == GLFW_PRESS)
        exit(0);

    if (c->mode == CAMERA_MODE_LIGHT) {
        if (glfwGetKey(w->window, GLFW_KEY_1) == GLFW_PRESS)
            c->csi = 0;
        if (glfwGetKey(w->window, GLFW_KEY_2) == GLFW_PRESS)
            c->csi = 1;
        if (glfwGetKey(w->window, GLFW_KEY_3) == GLFW_PRESS)
            c->csi = 2;
        if (glfwGetKey(w->window, GLFW_KEY_4) == GLFW_PRESS)
            c->csi = 3;
    }

    camera_move(c, fwd, right, up, dt);
}

#endif // include guard
