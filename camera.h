#ifndef SOL_CAMERA_H_INCLUDE_GUARD_
#define SOL_CAMERA_H_INCLUDE_GUARD_

#include "defs.h"
#include "glfw.h"
#include "math.h"

struct camera {
    vector pos;
    vector dir;
    float fov;
    float sens;
    float speed;
    float x,y;
};


#endif // include guard
