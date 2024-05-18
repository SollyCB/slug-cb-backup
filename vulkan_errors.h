#ifndef SOL_VULKAN_ERRORS_HPP_INCLUDE_GUARD_
#define SOL_VULKAN_ERRORS_HPP_INCLUDE_GUARD_

#include "defs.h"

#if DEBUG
#include "print.h"

const char* match_vk_error(int error);

#define DEBUG_VK_OBJ_CREATION(func, err)                                           \
    do {                                                                           \
        if (err) {                                                                 \
            log_print_error("OBJ CREATION ERROR: %s returned %s, (%s, %i)", #func, \
                match_vk_error(err), __FILE__, __LINE__);                          \
            asm("int $3");                                                         \
        }                                                                          \
    } while(0);

#else
    #define DEBUG_OBJ_CREATION(creation_func, err_code)
#endif // DEBUG

#endif // include guard
