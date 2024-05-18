#ifndef SOL_GLFW_H_INCLUDE_GUARD_
#define SOL_GLFW_H_INCLUDE_GUARD_

#include "sol_vulkan.h"
#include "GLFW/glfw3.h"

struct glfw {
    GLFWwindow *window;
    int width, height;
};

void init_glfw(struct glfw *glfw);

void shutdown_glfw(struct glfw *glfw);

void glfw_poll_and_get_input(struct glfw *glfw);
static inline void poll_glfw() {glfwPollEvents();}


#endif // include guard
