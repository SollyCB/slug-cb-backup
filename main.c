#include "defs.h"
#include "math.h"
#include "print.h"
#include "hash_map.h"
#include "array.h"
#include "sol_vulkan.h"
#include "string.h"
#include "file.h"
#include "ascii.h"
#include "json.h"
#include "gltf.h"
#include "test.h"
#include "array.h"
#include "allocator.h"
#include "thread.h"
#include "gpu.h"
#include "glfw.h"
#include "spirv.h"
#include "shader.h"
#include "asset.h"
#include "vulkan_errors.h"
#include "timer.h"
#include "shadows.h"
#include "camera.h"
#include "shader.h.glsl"

#define DRAW_SUBPASS 0
#define HTP_SUBPASS  1

int FRAME_I = 0;
int SCR_W = 640 * 2;
int SCR_H = 480 * 2;
float FOV = PI / 4;

int FRAMES_ELAPSED = 0;

#define MAIN_HEAP_ALLOCATOR_SIZE (48 * 1024 * 1024)
#define MAIN_TEMP_ALLOCATOR_SIZE (8 * 1024 * 1024)

#define THREAD_HEAP_ALLOCATOR_SIZE ((MAIN_HEAP_ALLOCATOR_SIZE >> 1) / THREAD_COUNT)
#define THREAD_TEMP_ALLOCATOR_SIZE ((MAIN_TEMP_ALLOCATOR_SIZE >> 1) / THREAD_COUNT)

typedef struct {
    allocator heap;
    allocator temp;
    void **to_free;
} allocator_info;

static void init_allocators(allocator_info *allocs);
static void init_threads(allocator_info *allocs, thread_pool *pool);

static void shutdown_allocators(allocator_info *allocs);
static void shutdown_threads(thread_pool *pool);

typedef struct {
    allocator_info allocs;
    thread_pool threads;
    struct gpu gpu;
    struct window glfw;
} prog_resources;

static void prog_init(prog_resources *pr, struct camera *c)
{
    init_allocators(&pr->allocs);
    #if MULTITHREADED
    init_threads(&pr->allocs, &pr->threads);
    #endif

    #if GPU
    struct init_gpu_args gpu_args = {
        &pr->glfw,
        &pr->threads,
        &pr->allocs.heap,
        &pr->allocs.temp
    };
    init_glfw(&pr->glfw, c);
    init_gpu(&pr->gpu, &gpu_args);
    #endif
}

#if CHECK_END_STATE
static void prog_shutdown(prog_resources *pr)
{
    struct timespec ts[2];
    ts[0] = get_time_cpu_proc();

    #if GPU
    shutdown_gpu(&pr->gpu);
    #endif

    #if MULTITHREADED
    shutdown_threads(&pr->threads);
    #endif
    shutdown_allocators(&pr->allocs);

    // These times seem about 8 secs too long...
    ts[1] = get_time_cpu_proc();
    print("\nProc time taken at shutdown: ");
    print_time(ts[0].tv_sec, ts[0].tv_nsec);
    print("Proc time taken including shutdown: ");
    print_time(ts[1].tv_sec, ts[1].tv_nsec);
}
#endif // CHECK_END_STATE

static void run_tests(allocator *alloc);
static void thread_tests(thread_pool *pool);
static void run_tests_thread(struct thread_work_arg *w);

#define ONE_FRAME 0

int main() {
    prog_resources pr;

    struct camera cam = {0};
    {
        cam.pos = vector4(0, 4, 7, 1);

        cam.fov   = FOV;
        cam.sens  = 0.03;
        cam.speed = 5;

        cam.dir = normalize(vector3(0, 0, -1));

        double r,u;
        cursorpos(&pr.glfw, &r, &u);
        cam.x =  r;
        cam.y = -u;

        cam.yaw   = -1;
        cam.pitch = -0.1;

        cam.mode = CAMERA_MODE_FLY;
    }
    prog_init(&pr, &cam);

    run_tests(&pr.allocs.heap);

    struct shader_config conf = {0};

    gltf model;
    load_gltf("models/cube-static/Cube.gltf", &pr.gpu.shader_dir,
            &conf, &pr.allocs.temp, &pr.allocs.heap, &model);

    // parse_gltf("models/cube-static/Cube.gltf", &pr.gpu.shader_dir,
    //         &conf, &pr.allocs.temp, &pr.allocs.heap, &model);

    struct vertex_info_descriptor vs_info_desc;
    Vertex_Info *vs_info = init_vs_info(&pr.gpu, cam.pos, cam.dir, &vs_info_desc);

    VkCommandPool transfer_pool = create_transient_transfer_command_pool(&pr.gpu);
    VkCommandPool graphics_pool = create_transient_graphics_command_pool(&pr.gpu);

    VkFence fence = create_fence(&pr.gpu, false);
    VkSemaphore sem_have_swapchain_image = create_binary_semaphore(&pr.gpu);
    VkSemaphore sem_transfer_complete = create_binary_semaphore(&pr.gpu);
    VkSemaphore sem_graphics_complete = create_binary_semaphore(&pr.gpu);

    struct draw_box_rsc vf_rsc[CSM_COUNT]; // view frustum resources
    struct draw_box_rsc sb_rsc; // scene bb frustum resources
    struct draw_box_rsc lf_rsc[CSM_COUNT]; // light ortho frustum resources
    struct draw_box_rsc lpos_rsc; // light position
    struct draw_floor_rsc df_rsc;

    float lbox_min = -0.1;
    float lbox_max =  0.1;
    struct box lbox = {
        .p = {
            vector4(lbox_min, lbox_min, lbox_min, 1),
            vector4(lbox_min, lbox_min, lbox_max, 1),
            vector4(lbox_max, lbox_min, lbox_max, 1),
            vector4(lbox_max, lbox_min, lbox_min, 1),

            vector4(lbox_min, lbox_max, lbox_min, 1),
            vector4(lbox_min, lbox_max, lbox_max, 1),
            vector4(lbox_max, lbox_max, lbox_max, 1),
            vector4(lbox_max, lbox_max, lbox_min, 1),
        },
    };

    matrix light_view;
    #if SPLIT_SHADOW_MVP
    matrix light_proj[CSM_COUNT];
    #else
    matrix light_space[CSM_COUNT];
    #endif

    bool32 t_cleanup[2] = {0};

    bool jumping = 0;
    float tim = 0;
    float s = 0;
    float u = 5;

    float t = 0;
    float dt = 0;

    struct box scene_bb;
    struct frustum camera_frustum;
    struct frustum sub_frusta[CSM_COUNT];
    struct minmax minmax_frustum_x[CSM_COUNT],minmax_frustum_y[CSM_COUNT], light_nearfar_planes[CSM_COUNT];

    // for(uint frame_index=0; frame_index < 2; ++frame_index) {
    while(1) {
        poll_glfw();

        allocator_reset_linear(&pr.allocs.temp);
        reset_gpu_buffers(&pr.gpu);

        matrix light_view_mat;
        { // update light view, pos
            vector q = quaternion(dt, vector3(0, 1, 0));
            matrix m;
            rotation_matrix(q, &m);
            // vs_info->dir_lights[0].position = mul_matrix_vector(&m, vs_info->dir_lights[0].position);

            vector light_tgt = vector4(0, 0, 0, 1);
            view_matrix(vs_info->dir_lights[0].position,
                        normalize(sub_vector(light_tgt, vs_info->dir_lights[0].position)),
                        vector3(0, 0, -1), &light_view_mat);
        }

        matrix lmvp;
        matrix mat_model;
        {
            dt = t;
            t = get_float_time_proc();
            dt = t - dt;

            update_camera(&cam, &pr.glfw, dt);

            matrix mat_view;
            view_matrix(cam.pos, cam.dir, vector3(0, 1, 0), &mat_view);

            vs_info->eye_pos = cam.pos;

            if (!jumping) {
                jumping = true;
                s = 0;
                tim = 0;
            } else {
                tim += dt;
                s = u * tim + 0.5 * -4.81 * (tim * tim);
                jumping = s > 0;
                clamp(s, 0, INFINITY);
            }

            struct trs model_trs;
            get_trs(
                vector3(3, 3, -3),
                quaternion(t, vector3(1, 1, 0)),
                // quaternion(0, vector3(1, 0, 0)),
                vector3(1, 1, 1),
                &model_trs
            );
            convert_trs(&model_trs, &mat_model);

            update_vs_info_mat_model(&pr.gpu, vs_info_desc.bb_offset, &mat_model);
            update_vs_info_mat_view(&pr.gpu, vs_info_desc.bb_offset, &mat_view);

            scene_bounding_box(&scene_bb);

            perspective_frustum(FOV, ASPECT_RATIO, PERSPECTIVE_NEAR, PERSPECTIVE_FAR, &camera_frustum);
            {
                matrix fm;
                move_to_camera(cam.pos, cam.dir, vector3(0, 1, 0), &fm);
                transform_frustum(&camera_frustum, &fm);
            }
            partition_frustum_s(&camera_frustum, carrlen(sub_frusta), sub_frusta);

            for(uint i=0; i < CSM_COUNT; ++i) {
                minmax_frustum_points(&sub_frusta[i], &light_view_mat, &minmax_frustum_x[i], &minmax_frustum_y[i]);

                // world units per texel (not totally sure if these are the correct numbers to use but it seems to work)
                float wupt_x = fabsf(minmax_frustum_x[i].max - minmax_frustum_x[i].min) / pr.gpu.settings.shadow_maps.width;
                float wupt_y = fabsf(minmax_frustum_y[i].max - minmax_frustum_y[i].min) / pr.gpu.settings.shadow_maps.height;

                // move light in texel sized increments
                minmax_frustum_x[i].max /= wupt_x;
                minmax_frustum_x[i].min /= wupt_x;
                minmax_frustum_x[i].max  = floorf(minmax_frustum_x[i].max);
                minmax_frustum_x[i].min  = floorf(minmax_frustum_x[i].min);
                minmax_frustum_x[i].max *= wupt_x;
                minmax_frustum_x[i].min *= wupt_x;

                minmax_frustum_y[i].max /= wupt_y;
                minmax_frustum_y[i].min /= wupt_y;
                minmax_frustum_y[i].max  = floorf(minmax_frustum_y[i].max);
                minmax_frustum_y[i].min  = floorf(minmax_frustum_y[i].min);
                minmax_frustum_y[i].max *= wupt_y;
                minmax_frustum_y[i].min *= wupt_y;
            }

            struct box ls_bb;
            for(uint i=0; i < carrlen(ls_bb.p); ++i)
                ls_bb.p[i] = mul_matrix_vector(&light_view_mat, scene_bb.p[i]);

            // mat clipspace;
            // mul_matrix(&mat_proj, &mat_view, &clip_space);

            for(uint i=0; i < CSM_COUNT; ++i) {
                light_nearfar_planes[i] = near_far(minmax_frustum_x[i], minmax_frustum_y[i], &ls_bb);

                float *cb = &vs_info->cascade_boundaries.x;
                // cb[i] = sub_frusta[i].bl_far.z;
                cb[i] = mul_matrix_vector(&mat_view, sub_frusta[i].bl_far).z;
                // cb[i] = mul_matrix_vector(&clip_space, sub_frusta[i].bl_far).z;

                ortho_matrix(minmax_frustum_x[i].min, minmax_frustum_x[i].max,
                             minmax_frustum_y[i].max, minmax_frustum_y[i].min,
                             light_nearfar_planes[i].min, light_nearfar_planes[i].max, &light_proj[i]);

                mul_matrix(&light_proj[i], &light_view_mat, &vs_info->dir_lights[0].space[i]);
            }

            #if 0
            update_vs_info_mat_model(&pr.gpu, vs_info_desc.bb_offset, &mat_model);
            update_vs_info_mat_view(&pr.gpu, vs_info_desc.bb_offset, &light_view_mat);
            memcpy(&vs_info->proj, &light_proj[cam.csi], sizeof(light_proj[0]));
            #endif

            if (cam.mode == CAMERA_MODE_LIGHT) {
                vs_info->dlcx[1] = true;
                vs_info->dlcx[2] = cam.csi;
            } else {
                vs_info->dlcx[1] = false;
            }
        }

        pr.gpu.swapchain.i = next_swapchain_image(&pr.gpu, sem_have_swapchain_image, fence);
        fence_wait_secs_and_reset(&pr.gpu, fence, 1); // Is there an optimal place to wait on this?

        struct renderpass color_rp;
        create_color_renderpass(&pr.gpu, &color_rp);

        VkCommandBuffer transfer_cmd;
        allocate_command_buffers(&pr.gpu, transfer_pool, 1, &transfer_cmd);

        VkCommandBuffer cmds_graphics[2];
        allocate_command_buffers(&pr.gpu, graphics_pool, 2, cmds_graphics);

        VkCommandBuffer graphics_cmd = cmds_graphics[0];
        VkCommandBuffer draw_cmd = cmds_graphics[1];
        begin_onetime_command_buffers(1, &transfer_cmd);
        begin_onetime_command_buffers(2, cmds_graphics);

        struct htp_rsc htp_rsc;
        if (!htp_allocate_resources(&pr.gpu, &color_rp, HTP_SUBPASS, transfer_cmd, graphics_cmd, &htp_rsc))
            return -1;

        struct shadow_maps shadow_maps = {.count = 1};
        if (!create_shadow_maps(&pr.gpu, transfer_cmd, graphics_cmd, &shadow_maps))
            return -1;

        struct renderpass depth_rp;
        create_shadow_renderpass(&pr.gpu, &shadow_maps, &depth_rp);

        uint scene = 0;

        struct load_model_arg lma = {
            #if 1
            .flags = LOAD_MODEL_BLIT_MIPMAPS_BIT,
            #else
            .flags = 0,
            #endif
            .dsl_count = 2,
            .animation_count = 0,
            .scene_count = model.scene_count,
            .subpass_mask = LOAD_MODEL_SUBPASS_DRAW,
            .color_subpass = 0,
            .depth_pass_count = shadow_maps.count * CSM_COUNT,
            .model = &model,
            .gpu = &pr.gpu,
            .animations = NULL,
            .scenes = &scene,
            .dsls[0] = vs_info_desc.dsl,
            .dsls[1] = shadow_maps.dsl,
            #if NO_DESCRIPTOR_BUFFER
            .d_sets[0] = vs_info_desc.d_set,
            .d_sets[1] = shadow_maps.d_set,
            #else
            .db_indices[0] = DESCRIPTOR_BUFFER_RESOURCE_BIND_INDEX,
            .db_offsets[0] = vs_info_desc.db_offset,
            .db_indices[1] = DESCRIPTOR_BUFFER_SAMPLER_BIND_INDEX,
            .db_offsets[1] = shadow_maps.db_offset,
            #endif
            .color_renderpass = color_rp.rp,
            .depth_renderpass = depth_rp.rp,
            .viewport = pr.gpu.settings.viewport,
            .scissor = pr.gpu.settings.scissor,
        };

        signal_thread_false(&t_cleanup[FRAME_I]);

        struct load_model_ret lmr = {
            .cmd_graphics = graphics_cmd,
            .cmd_transfer = transfer_cmd,
            .thread_cleanup_resources = &t_cleanup[FRAME_I],
        };

        struct load_model_info lmi = {
            .arg = &lma,
            .ret = &lmr,
        };
        struct thread_work w_load_model = {
            .fn = cast_work_fn(load_model_tf),
            .arg = cast_work_arg(&lmi),
        };
        thread_add_work_high(&pr.threads, 1, &w_load_model);

        while(lmi.ret->result == LOAD_MODEL_RESULT_INCOMPLETE)
            ;

        {
            #if 0
            insert_memory_barrier(draw_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
            #endif

            if (!FRAMES_ELAPSED) { // upload default texture on first frame
                uint upload_offset = 0;

                gpu_upload_images_with_base_offset(&pr.gpu, 1, &pr.gpu.defaults.texture.image,
                        0, &upload_offset, lmr.cmd_transfer, lmr.cmd_graphics);

                transition_texture_layouts(
                        draw_cmd,
                        false, // only 1px image anyway...
                        1,
                        &pr.gpu.defaults.texture.image,
                        &pr.allocs.temp);
            }

            struct shadow_pass_info spi = {
                .gpu = &pr.gpu,
                .rp = &depth_rp,
                .maps = &shadow_maps,
                .lmr = &lmr,
                #if SPLIT_SHADOW_MVP
                .light_model = &mat_model,
                .light_view = &light_view_mat,
                .light_proj = light_proj,
                #else
                .light_space = light_space,
                #endif
            };
            do_shadow_pass(draw_cmd, &spi, &pr.allocs.temp);

            #if DESCRIPTOR_BUFFER
            bind_descriptor_buffers(draw_cmd, &pr.gpu);
            #endif

            begin_color_renderpass(draw_cmd, &color_rp, pr.gpu.settings.scissor);

            #if NO_DESCRIPTOR_BUFFER
            draw_floor(draw_cmd, &pr.gpu, color_rp.rp, 0, 2, lma.dsls, lma.d_sets, &df_rsc);
            #else
            draw_floor(draw_cmd, &pr.gpu, color_rp.rp, 0, lma.dsls, lma.db_indices, lma.db_offsets, &df_rsc);
            #endif

            draw_model_color(draw_cmd, lmr.draw_info);

            {
                #define DLP 0
                #define DSB 0
                #define DLF 0
                #define DCF 0

                struct box vfb[CSM_COUNT];
                for(uint i=0; i < carrlen(sub_frusta); ++i)
                    frustum_to_box(&sub_frusta[i], &vfb[i]);

                matrix m;
                mul_matrix(&vs_info->proj, &vs_info->view, &m);

                #if DCF
                for(uint i=0; i < carrlen(vfb); ++i)
                    draw_box(draw_cmd, &pr.gpu, &vfb[i], true, color_rp.rp, 0, &vf_rsc[i], &m, vector4(1, 0, 0, 1));
                #endif

                #if DSB
                draw_box(draw_cmd, &pr.gpu, &scene_bb, true, color_rp.rp, 0, &sb_rsc, &m, vector4(0, 0, 1, 1));
                #endif

                struct frustum lf[CSM_COUNT];
                struct box lfb[CSM_COUNT];
                for(uint i=0; i < CSM_COUNT; ++i) {
                    ortho_frustum(minmax_frustum_x[i].min, minmax_frustum_x[i].max,
                                  minmax_frustum_y[i].min, minmax_frustum_y[i].max,
                                  light_nearfar_planes[i].min, light_nearfar_planes[i].max, &lf[i]);
                    frustum_to_box(&lf[i], &lfb[i]);
                }

                {
                    matrix lm;
                    struct trs ltrs;
                    get_trs(
                        vs_info->dir_lights[0].position,
                        vector4(0, 0, 0, 1),
                        vector3(1, 1, 1),
                        &ltrs
                    );
                    convert_trs(&ltrs, &lm);
                    mul_matrix(&m, &lm, &lm);

                    #if DLP
                    draw_box(draw_cmd, &pr.gpu, &lbox, false, color_rp.rp, 0, &lpos_rsc, &lm, vector4(50, 50, 50, 1));
                    #endif

                    #if DLF

                    matrix il;
                    invert(&light_view_mat, &il);
                    il.m[15] = 1;
                    mul_matrix(&m, &il, &il);

                    vector col = vector4(0, 0, 0, 1);
                    for(uint i=0; i < CSM_COUNT; ++i) {
                        switch(i) {
                            case 0:
                                col = vector4(1, 1, 0, 1);
                                break;
                            case 1:
                                col = vector4(1, 0, 1, 1);
                                break;
                            case 2:
                                col = vector4(0, 1, 1, 1);
                                break;
                            case 3:
                                col = vector4(0, 0, 1, 1);
                                break;
                            defauilt:
                                break;
                        }
                        draw_box(draw_cmd, &pr.gpu, &lfb[i], true, color_rp.rp, 0, &lf_rsc[i], &il, col);
                    }

                    #endif
                }
            }

            vk_cmd_next_subpass(draw_cmd, VK_SUBPASS_CONTENTS_INLINE);

            htp_commands(draw_cmd, &pr.gpu, &htp_rsc);

            end_renderpass(draw_cmd);

            end_command_buffer(draw_cmd);
            end_command_buffer(graphics_cmd);
        } {
            end_command_buffer(transfer_cmd);

            VkCommandBufferSubmitInfo cmdi;
            command_buffer_submit_info(transfer_cmd, &cmdi);

            VkSemaphoreSubmitInfo sem_s;
            transfer_semaphore_submit_info(sem_transfer_complete, 0, &sem_s);

            VkSubmitInfo2 si;
            queue_submit_info(1, &cmdi, 0, NULL, 1, &sem_s, &si);

            transfer_queue_submit(&pr.gpu, 1, &si, NULL);
        } {
            VkCommandBufferSubmitInfo cmdi[2];
            command_buffer_submit_info(graphics_cmd, &cmdi[0]);
            command_buffer_submit_info(draw_cmd, &cmdi[1]);

            VkSemaphoreSubmitInfo sem_w[2];
            vertex_input_semaphore_submit_info(sem_transfer_complete, 0, &sem_w[0]);
            color_output_semaphore_submit_info(sem_have_swapchain_image, 0, &sem_w[1]);

            // @Note @Review Why must all_graphics be used in place of color
            // attachment output in order to avoid validation warnings?
            VkSemaphoreSubmitInfo sem_s;
            all_graphics_semaphore_submit_info(sem_graphics_complete, 0, &sem_s);

            VkSubmitInfo2 si;
            queue_submit_info(2, cmdi, 2, sem_w, 1, &sem_s, &si);

            graphics_queue_submit(&pr.gpu, 1, &si, fence);

            present_queue_submit(&pr.gpu, 1, &sem_graphics_complete);
        }

        fence_wait_secs_and_reset(&pr.gpu, fence, 3);
        signal_thread_true(&t_cleanup[FRAME_I]);
        reset_descriptor_pools(&pr.gpu);

        draw_floor_cleanup(&pr.gpu, &df_rsc);

        #if DCF
        for(uint i=0; i < carrlen(vf_rsc); ++i)
            draw_box_cleanup(&pr.gpu, &vf_rsc[i]);
        #endif

        #if DSB
        draw_box_cleanup(&pr.gpu, &sb_rsc);
        #endif

        #if DLP
        draw_box_cleanup(&pr.gpu, &lpos_rsc);
        #endif

        #if DLF
        for(uint i=0; i < CSM_COUNT; ++i)
            draw_box_cleanup(&pr.gpu, &lf_rsc[i]);
        #endif

        destroy_renderpass(&pr.gpu, &color_rp);
        destroy_renderpass(&pr.gpu, &depth_rp);
        htp_free_resources(&pr.gpu, &htp_rsc);
        free_shadow_maps(&pr.gpu, &shadow_maps);

        reset_command_pool(&pr.gpu, transfer_pool);
        reset_command_pool(&pr.gpu, graphics_pool);

        FRAMES_ELAPSED++;
        FRAME_I = FRAMES_ELAPSED & 1;

        {
            #if 0 // Throttle frame rate
            struct timespec rq,rm;
            rq.tv_sec  = 0;
            rq.tv_nsec = 10 * 10e6;
            nanosleep(&rq, &rm);
            #endif
        }

        while(ONE_FRAME)
            ;
    }
    exit(0);

    vk_destroy_command_pool(pr.gpu.device, transfer_pool, GAC);
    vk_destroy_command_pool(pr.gpu.device, graphics_pool, GAC);

    #if SHADER_C
    store_shader_dir(&pr.gpu.shader_dir);
    #endif

    prog_shutdown(&pr);
    return 0;
}

static void run_tests(allocator *alloc)
{
    #if TEST
    test_suite suite = load_tests(alloc);

    test_gltf(&suite);
    test_spirv(&suite);

    end_tests(&suite);
    #endif
}

static void init_allocators(allocator_info *allocs)
{
    allocs->heap = new_heap_allocator(MAIN_HEAP_ALLOCATOR_SIZE, NULL);
    allocs->temp = new_linear_allocator(MAIN_TEMP_ALLOCATOR_SIZE, NULL);
    allocs->to_free = new_array(32, void*, &allocs->heap);
}

static void shutdown_allocators(allocator_info *allocs)
{
    for(uint i = 0; i < array_len(allocs->to_free); ++i)
        deallocate(&allocs->heap, allocs->to_free[i]);
    free_array(allocs->to_free);
    free_allocator(&allocs->temp);
    free_allocator(&allocs->heap);
}

static void init_threads(allocator_info *allocs, thread_pool *pool)
{
    thread_status("Initializing %u threads, heap allocator size %u, temp allocator size %u", THREAD_COUNT, THREAD_HEAP_ALLOCATOR_SIZE, THREAD_TEMP_ALLOCATOR_SIZE);
    struct allocation heap_buffers[THREAD_COUNT];
    struct allocation temp_buffers[THREAD_COUNT];
    uint i;
    for(i=0;i<THREAD_COUNT;++i) {
        heap_buffers[i].size = THREAD_HEAP_ALLOCATOR_SIZE;
        temp_buffers[i].size = THREAD_TEMP_ALLOCATOR_SIZE;
        heap_buffers[i].data = allocate(&allocs->heap, THREAD_HEAP_ALLOCATOR_SIZE);
        temp_buffers[i].data = allocate(&allocs->heap, THREAD_TEMP_ALLOCATOR_SIZE);
        array_add(allocs->to_free, heap_buffers[i].data);
        array_add(allocs->to_free, temp_buffers[i].data);
    }
    new_thread_pool(heap_buffers, temp_buffers, &allocs->heap, pool);
    thread_status("Thread initialization complete");
}

static void shutdown_threads(thread_pool *pool)
{
    thread_status("Begin thread shutdown");
    free_thread_pool(pool, true);
    thread_status("Thread shutdown complete");
}

static void thread_tests(thread_pool *pool)
{
    struct thread_work w[10];
    for(uint i = 0; i < 10; ++i) {
        w[i].fn = cast_work_fn(run_tests_thread);
        w[i].arg = (void*)0xcdcdcdcdcdcdcdcd;
    }
    for(uint i = 0; i < THREAD_COUNT; ++i)
        thread_add_work_high(pool, 10, w);
}

static void run_tests_thread(struct thread_work_arg *w)
{
    run_tests(w->allocs.temp);
}

