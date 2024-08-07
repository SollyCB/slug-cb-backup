#include "glfw.h"
#include "log.h"
#include "defs.h"

#include "camera.h"

float POS_X = 0;
float POS_Y = 0;

static void glfw_mouse_callback(GLFWwindow *window, double x, double y)
{
    POS_X = x;
    POS_Y = y;
}

static void glfw_cursor_enter_callback(GLFWwindow* window, int entered)
{
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    FOV -= 10 * yoffset;
}


static void glfw_error_callback(int e, const char *desc)
{
    log_print_error("glfw error code %u, %s", e, desc);
}

void init_glfw(struct window *glfw, struct camera *c) {
    if (!glfwInit())
        log_print_error("failed to init glfw");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfw->width = SCR_W;
    glfw->height = SCR_H;
    glfw->window = glfwCreateWindow(glfw->width, glfw->height, "Glfw Window", NULL, NULL);
    log_print_error_if(!glfw->window, "failed to create glfw window");

    glfwSetWindowUserPointer(glfw->window, c);

    glfwSetErrorCallback(glfw_error_callback);
    glfwSetCursorEnterCallback(glfw->window, glfw_cursor_enter_callback);
    glfwSetScrollCallback(glfw->window, scroll_callback);
}

void shutdown_glfw(struct window *glfw) {
    glfwDestroyWindow(glfw->window);
    glfwTerminate();
}

enum InputValues {
    INPUT_CLOSE_WINDOW  = GLFW_KEY_Q,
    INPUT_MOVE_FORWARD  = GLFW_KEY_W,
    INPUT_MOVE_BACKWARD = GLFW_KEY_S,
    INPUT_MOVE_RIGHT    = GLFW_KEY_D,
    INPUT_MOVE_LEFT     = GLFW_KEY_A,
    INPUT_JUMP          = GLFW_KEY_SPACE,
};

void glfw_poll_and_get_input(struct window *glfw) {
    glfwPollEvents();

    int right = 0;
    int forward = 0;
    if (glfwGetKey(glfw->window, (int)INPUT_CLOSE_WINDOW) == GLFW_PRESS) {
        glfwSetWindowShouldClose(glfw->window, GLFW_TRUE);
        println("Close Window Key Pressed...");
    }
    if (glfwGetKey(glfw->window, (int)INPUT_MOVE_FORWARD) == GLFW_PRESS)
        forward = 1;
    if (glfwGetKey(glfw->window, (int)INPUT_MOVE_BACKWARD) == GLFW_PRESS)
        forward = -1;
    if (glfwGetKey(glfw->window, (int)INPUT_MOVE_RIGHT) == GLFW_PRESS)
        right = 1;
    if (glfwGetKey(glfw->window, (int)INPUT_MOVE_LEFT) == GLFW_PRESS)
        right = -1;

    // camera_move(get_camera_instance(), forward, right);
}

