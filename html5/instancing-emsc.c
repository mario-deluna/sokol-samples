//------------------------------------------------------------------------------
//  instancing-emsc.c
//------------------------------------------------------------------------------
#include <stddef.h>
#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#define HANDMADE_MATH_IMPLEMENTATION
#define HANDMADE_MATH_NO_SSE
#include "HandmadeMath.h"
#define SOKOL_IMPL
#define SOKOL_GLES2
#include "sokol_gfx.h"

const int WIDTH = 640;
const int HEIGHT = 480;
const int MAX_PARTICLES = 512 * 1024;
const int NUM_PARTICLES_EMITTED_PER_FRAME = 10;

/* clear to black */
sg_pass_action pass_action = {
    .colors[0] = { .action = SG_ACTION_CLEAR, .val = { 0.0f, 0.0f, 0.0f, 1.0f } }
};

sg_draw_state draw_state;
float roty = 0.0f;
hmm_mat4 view_proj;

typedef struct {
    hmm_mat4 mvp;
} vs_params_t;

/* particle positions and velocity */
int cur_num_particles = 0;
hmm_vec3 pos[MAX_PARTICLES];
hmm_vec3 vel[MAX_PARTICLES];

void draw();

int main() {
    /* setup WebGL context */
    emscripten_set_canvas_element_size("#canvas", WIDTH, HEIGHT);
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx;
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    ctx = emscripten_webgl_create_context(0, &attrs);
    emscripten_webgl_make_context_current(ctx);

    /* setup sokol_gfx */
    sg_desc desc = {0};
    sg_setup(&desc);
    assert(sg_isvalid());
    
    /* vertex buffer for static geometry (goes into vertex buffer bind slot 0) */
    const float r = 0.05f;
    const float vertices[] = {
        // positions            colors
        0.0f,   -r, 0.0f,       1.0f, 0.0f, 0.0f, 1.0f,
           r, 0.0f, r,          0.0f, 1.0f, 0.0f, 1.0f,
           r, 0.0f, -r,         0.0f, 0.0f, 1.0f, 1.0f,
          -r, 0.0f, -r,         1.0f, 1.0f, 0.0f, 1.0f,
          -r, 0.0f, r,          0.0f, 1.0f, 1.0f, 1.0f,
        0.0f,    r, 0.0f,       1.0f, 0.0f, 1.0f, 1.0f
    };
    sg_buffer_desc geom_vbuf_desc = {
        .size = sizeof(vertices),
        .content = vertices,
    };

    /* index buffer for static geometry */
    const uint16_t indices[] = {
        0, 1, 2,    0, 2, 3,    0, 3, 4,    0, 4, 1,
        5, 1, 2,    5, 2, 3,    5, 3, 4,    5, 4, 1
    };
    sg_buffer_desc ibuf_desc = {
        .type = SG_BUFFERTYPE_INDEXBUFFER,
        .size = sizeof(indices),
        .content = indices,
    };
    
    /* empty, dynamic instance-data vertex buffer (goes into vertex buffer bind slot 1) */
    sg_buffer_desc inst_vbuf_desc = {
        .size = MAX_PARTICLES * sizeof(hmm_vec3),
        .usage = SG_USAGE_STREAM
    };

    /* create a shader */
    sg_shader_desc shd_desc = {
        .vs.uniform_blocks[0] = {
            .size = sizeof(vs_params_t),
            .uniforms = {
                [0] = { .name="mvp", .offset=offsetof(vs_params_t,mvp), SG_UNIFORMTYPE_MAT4 }
            }
        },
        .vs.source =
            "uniform mat4 mvp;\n"
            "attribute vec3 position;\n"
            "attribute vec4 color0;\n"
            "attribute vec3 instance_pos;\n"
            "varying vec4 color;\n"
            "void main() {\n"
            "  vec4 pos = vec4(position + instance_pos, 1.0);"
            "  gl_Position = mvp * pos;\n"
            "  color = color0;\n"
            "}\n",
        .fs.source =
            "precision mediump float;\n"
            "varying vec4 color;\n"
            "void main() {\n"
            "  gl_FragColor = color;\n"
            "}\n"
    };

    /* pipeline state object, note the vertex attribute definition */
    sg_pipeline_desc pip_desc = {
        .vertex_layouts = {
            [0] = {
                .stride = 28,
                .attrs = {
                    [0] = { .name="position", .offset=0, .format=SG_VERTEXFORMAT_FLOAT3 },
                    [1] = { .name="color0", .offset=12, .format=SG_VERTEXFORMAT_FLOAT4 }
                },
            },
            [1] = {
                .stride = 12,
                .step_func = SG_VERTEXSTEP_PER_INSTANCE,
                .attrs = {
                    [0] = { .name="instance_pos", .offset=0, .format=SG_VERTEXFORMAT_FLOAT3 }
                }
            }
        },
        .shader = sg_make_shader(&shd_desc),
        .index_type = SG_INDEXTYPE_UINT16,
        .depth_stencil = {
            .depth_compare_func = SG_COMPAREFUNC_LESS_EQUAL,
            .depth_write_enabled = true
        },
        .rasterizer.cull_mode = SG_CULLMODE_NONE
    };

    /* setup draw state with resource bindings */
    draw_state = (sg_draw_state){
        .pipeline = sg_make_pipeline(&pip_desc),
        .vertex_buffers = {
            [0] = sg_make_buffer(&geom_vbuf_desc),
            [1] = sg_make_buffer(&inst_vbuf_desc)
        },
        .index_buffer = sg_make_buffer(&ibuf_desc)
    };

    /* view-projection matrix */
    hmm_mat4 proj = HMM_Perspective(60.0f, (float)WIDTH/(float)HEIGHT, 0.01f, 50.0f);
    hmm_mat4 view = HMM_LookAt(HMM_Vec3(0.0f, 1.5f, 12.0f), HMM_Vec3(0.0f, 0.0f, 0.0f), HMM_Vec3(0.0f, 1.0f, 0.0f));
    view_proj = HMM_MultiplyMat4(proj, view);

    /* hand off control to browser loop */
    emscripten_set_main_loop(draw, 0, 1);
    return 0;
}

/* draw one frame */ 
void draw() {
    const float frame_time = 1.0f / 60.0f;

    /* emit new particles */
    for (int i = 0; i < NUM_PARTICLES_EMITTED_PER_FRAME; i++) {
        if (cur_num_particles < MAX_PARTICLES) {
            pos[cur_num_particles] = HMM_Vec3(0.0, 0.0, 0.0);
            vel[cur_num_particles] = HMM_Vec3(
                ((float)(rand() & 0xFFFF) / 0xFFFF) - 0.5f,
                ((float)(rand() & 0xFFFF) / 0xFFFF) * 0.5f + 2.0f,
                ((float)(rand() & 0xFFFF) / 0xFFFF) - 0.5f);
            cur_num_particles++;
        }
        else {
            break;
        }
    }

    /* update particle positions */
    for (int i = 0; i < cur_num_particles; i++) {
        vel[i].Y -= 1.0f * frame_time;
        pos[i].X += vel[i].X * frame_time;
        pos[i].Y += vel[i].Y * frame_time;
        pos[i].Z += vel[i].Z * frame_time;
        /* bounce back from 'ground' */
        if (pos[i].Y < -2.0f) {
            pos[i].Y = -1.8f;
            vel[i].Y = -vel[i].Y;
            vel[i].X *= 0.8f; vel[i].Y *= 0.8f; vel[i].Z *= 0.8f;
        }
    }

    /* update instance data */
    sg_update_buffer(draw_state.vertex_buffers[1], pos, cur_num_particles*sizeof(hmm_vec3));

    /* model-view-projection matrix */
    roty += 1.0f;
    vs_params_t vs_params;
    vs_params.mvp = HMM_MultiplyMat4(view_proj, HMM_Rotate(roty, HMM_Vec3(0.0f, 1.0f, 0.0f)));;

    /* and the actual draw pass... */
    sg_begin_default_pass(&pass_action, WIDTH, HEIGHT);
    sg_apply_draw_state(&draw_state);
    sg_apply_uniform_block(SG_SHADERSTAGE_VS, 0, &vs_params, sizeof(vs_params));
    sg_draw(0, 24, cur_num_particles);
    sg_end_pass();
    sg_commit();
}
