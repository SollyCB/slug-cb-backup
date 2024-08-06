#ifndef SOL_GLFW_H_INCLUDE_GUARD_
#define SOL_GLFW_H_INCLUDE_GUARD_

extern float POS_X;
extern float POS_Y;

#include "sol_vulkan.h"
#include "GLFW/glfw3.h"

struct window {
    GLFWwindow *window;
    int width, height;
};

struct camera;
void init_glfw(struct window *glfw, struct camera *cam);

void shutdown_glfw(struct window *glfw);

void glfw_poll_and_get_input(struct window *glfw);
static inline void poll_glfw() {glfwPollEvents();}

static inline void cursorpos(struct window *w, double *x, double *y)
{
    glfwGetCursorPos(w->window, x, y);
}

#endif // include guard
