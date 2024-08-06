# 3D Vulkan Rendering Engine In C On Linux

Supports skinned animation, physically based rendering, multithreaded asset loading and model transformation, and cascaded shadow mapping.
Emphasises minimising dependencies.

## Example Renders

(replace via drag and drop)

## Project Break Down

### Vulkan 3D rendering engine in C on Linux supporting skinned animation, multithreaded asset loading and model transformation, physically based rendering, and cascaded shadow mapping.

This project was done entirely in my free time while I was studying PPE at Durham. I did it because I loved doing it: when I started programming I had
no intention of making a career, I just wanted to learn because it was fascinating. I used Vulkan because I wanted to have to understand how my
software would interact with hardware, I wanted to understand even the most esoteric and complex graphics APIs; I built my own tools and minimized library usage because I
wanted to understand the entire engine pipeline, because I wanted to practice every facet of C programming so that I could turn my hand to any
task that I could be presented with. **I feel that my rigorous practice has caused me to become a competent programmer with strong principles about
software functionality.**

#### Libraries used:

- vulkan 1.3
    - The only extension (other than those requisite for windowing) that I had enabled was descriptor buffers. It is currently disabled and unsupported since recent
      commits, but would be relatively trivial to support again. I was using descriptor buffers to better understand what descriptors really were, but once I had
      this understanding, I wanted to also learn to use descriptor pools effectively, as I assume that they are in common usage.
- google libshaderc
    - Shader compilation from C. This would be incredibly cool to implement myself, but I imagine this would be a project far larger than my engine itself.
- glfw
    - I had begun work on a Wayland native window library, but realised that this would be too significant of a sidetrack from the main engine for the time being.
- stb_image
    - I intend to write my own .png and .jpg loader to remove this dependency, as I did with other libraries over time.
      The fact that I did not feel the same urge to replace this dependency as I did with others is testament to the excellence of Sean Barrett's libraries.
- stb_sprintf
    - exclusively for printing floats, otherwise I use my own print/string format library. I also intend to implement my own float printing.
- wyhash
    - Hash technology seems pretty fascinating. It is currently very black box to me.

Code is sizeable due to an emphasis on minimizing dependencies - 17600 lines of C; 800 lines of GLSL.  I wrote my own json parser and gltf loader; SPIRV parser;
math library; threading library; hash map; type safe dynamic array in C (an STB hack); allocators for CPU and GPU memory; automatic ringbuffer using page mapping
techniques; and a testing library.

----------------------------------------------------------------------------------------------------------------------------------------------------------------
#### Notable engine files in order of their technical, api, and architectural significance:

**asset**

Load asset data from gltf struct, create vulkan resources (I would very much like to discuss this file with an industry professional to get critiques).
    - runs on worker thread
    - exposes information relevant to drawing, while resources are kept only visible to the worker (improves api)
    - calculates skeletal node transforms (including animation)
    - meshes used multiple times in a scene are drawn instanced, minimizing draw calls
    - single function call to prepare model
    - single function call to execute draw commands
    - allocates all memory requirements first to avoid work before failure point
    - deallocation runs via private queue, minimizing caller interaction with unnecessary resource information (improves api)

**thread**

Thread pool implementation.
    - minimal synchronisation usage (adding work is lock free and minimizes atomic operations per work item)
    - multiple priority queues minimizes contention (if acquire work lock is busy, the thread can try another queue)
    - workers feature private queues for adding work to be signalled later
        - an example of this is model resource deallocation, this work is done via thread private queues so that the
          main thread does not have to manage unnecessary information
    - simple circular FIFO buffer work queues

Note: when creating this file, I was much less experienced with multithreading. In fact I specifically wrote it to
get a better handle on multithreading. So there are some design decisions which I may revise today. However, as the
implementation is so simple and has already been through revisions and updates, it may entirely hold up. (This sentiment
of revision and improvement is true throughout the code base of course, as it always is, but in my head I always highlight this file.)

**ringbuffer**

Automatically wrapping buffer by mapping contiguous virtual pages to the same file.
    - file uses memfd_create, so lives in RAM in anonymous memory (not backed by file or device).

**hash_map**

C implementation of the rust HashBrown hash map (or google's Suisse Table depending on perspective).
    - slots represented by 16 byte wide groups of bit masks allowing for quickly searching slots with SIMD.
        - masks contain the top 7 bits of the hash, with the most significant bit indicating if the slot is in use.
        - unfortunately not typesafe (apparently Sean Barrett has a hack for making this typesafe, but I have not looked into it).

**math**

All mathematical operations.
    - fully SIMD, every vector and matrix operation (I am proud of my matrix inversion implementation)
    - vertex normal and tangent generation (if a model does not declare normals and tangents, they are generated from vertex data and texture coordinate data for lighting)
    - line and plane operations
    - quaternions
    - matrices
        - SIMD inversion via Gauss-Jordan elimination

**shadows**

Convert perspective matrix into points, partition frustum for cascades, calculate tight camera ortho.
   - perspective frustum via plane intersection
   - near and far planes for light ortho frustum via line and plane intersection (clipping scene bounding box against minimum and maximum perspective frustum x and y planes)
   - frustum partitioning in fit to scene and fit to cascade

**gpu**

Interact with vulkan.
    - manage gpu memory resources
        - buffers and texture memory are lock-free thread safe linear allocators requiring only an atomic add
        - descriptor pools are per thread
        - different descriptor pools pools for resource and sampler descriptors (the large size discrepancy between these descriptors can exacerbate fragmentation)
    - compile shaders via libshaderc
        - compilation configured via SHADERS table
        - shaders recompiled if their source code changes, or they declare an include and their include changes
    - descriptor and pipeline layouts created at startup
        - configured via PLLS table
        - reduces unnecessary api calls since all layouts are known
    - depth renderpass (shadow mapping), hdr color pass
    - although descriptor buffers are disabled and unsupported since recent commits, they could be supported again with trivial changes
      to 'asset.c', 'main.c', and 'gpu.c' (e.g. updating descriptor buffer code to use relocated struct members), and the complex offset code
      in 'asset.c' is functional.

**gltf**

Convert json struct to a gltf struct for better interaction from C.
    - generates tangent and normal data if not declared on a primitive
        - attributes are added to the primitive as if they were declared (improves api)
    - gltf structs are written to disk in binary format so '.gltf' files are not parsed unnecessarily
        - if '.gltf' file changes or 'gltf.c' source code changes, '.gltf' files will be parsed again, and the binary format on disk is overwritten
        - generated tangent and normal data is appended to the gltf model's '.bin' buffer file

**json and ascii**

Parse json files and convert to json struct (used for reading '.gltf' files); 'ascii.h' contains parsing algorithms.
    - good example of SIMD algorithm usage, e.g. maintaining scope despite matching 16 chars at a time

**allocator**

Manage CPU memory.
    - linear allocator for temporary allocations
    - arena allocator for persistent allocations
        - linked list of reference counted memory blocks
        - each block acts as it own linear allocator
        - block freed when reference count drops to 0

**test**

Test suite implementation.
    - used *extensively* in the development of gltf.c

**spirv**

SPIRV binary parser for allocatin required shader resources.
    - deprecated in favour of creating shaders around gltf.

**shader**

Automatic shader generation by according to gltf struct.
    - deprecated in favour of manually writing some template shader code and generating complete shader programs by combining templates in 'gpu.c - compile_shaders()'
