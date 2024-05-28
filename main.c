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

#define DRAW_SUBPASS 0
#define HTP_SUBPASS  1

int FRAME_I = 0;
int SCR_W = 640;
int SCR_H = 480;
float FOV = PI / 4;

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
    struct glfw glfw;
} prog_resources;

static void prog_init(prog_resources *pr)
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
    init_glfw(&pr->glfw);
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
    prog_init(&pr);

    run_tests(&pr.allocs.heap);

    struct shader_config conf = {0};

    struct allocation model_size;
    gltf model = parse_gltf("models/cube-static/Cube.gltf", &pr.gpu.shader_dir,
                            &conf, &pr.allocs.temp, &pr.allocs.temp, &model_size);

    struct vs_info_descriptor vs_info_desc;
    struct vs_info *vs_info = init_vs_info(&pr.gpu, &vs_info_desc);

    VkCommandPool transfer_pool = create_transient_transfer_command_pool(&pr.gpu);
    VkCommandPool graphics_pool = create_transient_graphics_command_pool(&pr.gpu);

    VkFence fence = create_fence(&pr.gpu, false);
    VkSemaphore sem_have_swapchain_image = create_binary_semaphore(&pr.gpu);
    VkSemaphore sem_transfer_complete = create_binary_semaphore(&pr.gpu);
    VkSemaphore sem_graphics_complete = create_binary_semaphore(&pr.gpu);

    struct draw_box_rsc draw_box_rsc;

    bool32 t_cleanup[2] = {0};

    bool jumping = 0;
    float tim = 0;
    float s = 0;
    float u = 5;

    float t = 0;
    float dt = 0;

    struct box scene_bb;

    while(1) {
        allocator_reset_linear(&pr.allocs.temp);
        reset_gpu_buffers(&pr.gpu);

        {
            matrix mat_view;
            // view_matrix(get_vector(0, 0, 7), get_vector(sinf(t) / 2, 0, 1), &mat_view);
            view_matrix(get_vector(0, 0, 7, 0), get_vector(0, 0, 1, 0), &mat_view);

            dt = t;
            t = get_float_time_proc();
            dt = t - dt;

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

            matrix mat_model;
            struct trs model_trs;
            get_trs(
                // get_vector(0, s, 0),
                get_vector(0, 0, 0, 0),
                // quaternion(t, get_vector(0, 1, 0)),
                quaternion(PI/4, get_vector(1, 0, 0, 0)),
                // scalar_mul_vector(get_vector(sinf(t) + 1.5, sinf(t) + 1.5, sinf(t) + 1.5), 0.5),
                get_vector(1, 1, 1, 0),
                &model_trs
            );
            convert_trs(&model_trs, &mat_model);

            update_vs_info_mat_model(&pr.gpu, vs_info_desc.bb_offset, &mat_model);
            update_vs_info_mat_view(&pr.gpu, vs_info_desc.bb_offset, &mat_view);

            scene_bounding_box(1, &mat_model, &model, &scene_bb);
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
        create_shadow_renderpass(&pr.gpu, 1, &shadow_maps, &depth_rp);

        uint scene = 0;

        struct load_model_arg lma = {
            .flags = 0x0,
            .dsl_count = 2,
            .animation_count = 0,
            .scene_count = model.scene_count,
            .subpass_mask = LOAD_MODEL_SUBPASS_DRAW,
            .color_subpass = 0,
            .depth_pass_count = shadow_maps.count,
            .model = &model,
            .gpu = &pr.gpu,
            .animations = NULL,
            .scenes = &scene,
            .dsls[0] = vs_info_desc.dsl,
            .db_indices[0] = DESCRIPTOR_BUFFER_RESOURCE_BIND_INDEX,
            .db_offsets[0] = vs_info_desc.db_offset,
            .dsls[1] = shadow_maps.dsl,
            .db_indices[1] = DESCRIPTOR_BUFFER_SAMPLER_BIND_INDEX,
            .db_offsets[1] = shadow_maps.db_offset,
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
            begin_shadow_renderpass(draw_cmd, &depth_rp, &pr.gpu, shadow_maps.count);

            struct frustum f;
            get_frustum(FOV, 0.1, 100, &f);

            for(uint i=0; i < shadow_maps.count; ++i) {
                matrix v;
                view_matrix(vs_info->dir_lights[i].position, vs_info->dir_lights[i].direction, &v);

                struct minmax x,y;
                minmax_frustum_points(&f, &v, &x, &y);

                struct minmax nf = near_far(x, y, &scene_bb);

                matrix o;
                ortho_matrix(x.min, x.max, y.min, y.max, nf.min, nf.max, &o);

                mul_matrix(&o, &v, &vs_info->dir_lights[i].space);

                vk_cmd_push_constants(draw_cmd,
                                      lmr.draw_info->pipeline_layouts[lmr.draw_info->prim_count],
                                      VK_SHADER_STAGE_VERTEX_BIT,
                                      0,
                                      sizeof(matrix),
                                      &vs_info->dir_lights[i].space);

                draw_model_depth(draw_cmd, lmr.draw_info, i);

                if (i < shadow_maps.count - 1)
                    vk_cmd_next_subpass(draw_cmd, VK_SUBPASS_CONTENTS_INLINE);
            }

            end_renderpass(draw_cmd);

            bind_descriptor_buffers(draw_cmd, &pr.gpu);

            begin_color_renderpass(draw_cmd, &color_rp, pr.gpu.settings.scissor);

            draw_model_color(draw_cmd, lmr.draw_info);

            #if 0
            matrix m;
            mul_matrix(&vs_info->proj, &vs_info->view, &m);

            draw_box(draw_cmd, &pr.gpu, &scene_bb, true, color_rp.rp, 0, &draw_box_rsc, &m);
            #endif

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

            VkSemaphoreSubmitInfo sem_s;
            color_output_semaphore_submit_info(sem_graphics_complete, 0, &sem_s);

            VkSubmitInfo2 si;
            queue_submit_info(2, cmdi, 2, sem_w, 1, &sem_s, &si);

            graphics_queue_submit(&pr.gpu, 1, &si, fence);

            present_queue_submit(&pr.gpu, 1, &sem_graphics_complete);
        }

        fence_wait_secs_and_reset(&pr.gpu, fence, 3);
        signal_thread_true(&t_cleanup[FRAME_I]);

        draw_box_cleanup(&pr.gpu, &draw_box_rsc);

        destroy_renderpass(&pr.gpu, &color_rp);
        htp_free_resources(&pr.gpu, &htp_rsc);

        reset_command_pool(&pr.gpu, transfer_pool);
        reset_command_pool(&pr.gpu, graphics_pool);

        FRAME_I = (FRAME_I + 1) & 1;

        while(ONE_FRAME)
            ;
    }

    store_shader_dir(&pr.gpu.shader_dir);
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

