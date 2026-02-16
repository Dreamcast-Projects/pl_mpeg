/**
 * \file example_3dtv.c
 * \brief 3D TV Example - Renders MPEG video onto a CRT TV model
 *
 * Demonstrates using mpeg_player_get_texture_hdr(),
 * mpeg_player_get_uv_scale(), and mpeg_player_get_dimensions()
 * to render the video texture onto custom geometry
 * (a Simpsons-style CRT television).
 *
 * The TV screen is a fixed 4:3 area. 4:3 videos fill it completely;
 * wider videos (e.g. 16:9) are letterboxed with black bars.
 */

#include <kos.h>
#include <math.h>
#include "mpeg.h"


#define SCALE    4.5f
#define MODEL_W  102.0f
#define MODEL_H  92.0f
#define OFFSET_X ((640.0f - MODEL_W * SCALE) / 2.0f)
#define OFFSET_Y ((480.0f - MODEL_H * SCALE) / 2.0f)

#define TX(x) (OFFSET_X + (x) * SCALE)
#define TY(y) (OFFSET_Y + (MODEL_H - (y)) * SCALE)

/*
 * 4:3 screen area within the TV body.
 */
#define SCR_MODEL_W    60.0f
#define SCR_MODEL_H    45.0f
#define SCR_MODEL_CX   43.0f
#define SCR_MODEL_CY   42.5f

#define SCREEN_LEFT   TX(SCR_MODEL_CX - SCR_MODEL_W / 2.0f)
#define SCREEN_TOP    TY(SCR_MODEL_CY + SCR_MODEL_H / 2.0f)
#define SCREEN_RIGHT  TX(SCR_MODEL_CX + SCR_MODEL_W / 2.0f)
#define SCREEN_BOTTOM TY(SCR_MODEL_CY - SCR_MODEL_H / 2.0f)

/* Depth layers */
#define Z_BODY   1.0f
#define Z_SCREEN 2.0f
#define Z_VIDEO  3.0f
#define Z_DETAIL 4.0f

/* Colors */
#define COLOR_BODY   PVR_PACK_COLOR(1.0f, 0.40f, 0.15f, 0.55f)
#define COLOR_DARK   PVR_PACK_COLOR(1.0f, 0.25f, 0.08f, 0.35f)
#define COLOR_KNOB   PVR_PACK_COLOR(1.0f, 0.20f, 0.06f, 0.28f)
#define COLOR_BLACK  PVR_PACK_COLOR(1.0f, 0.0f,  0.0f,  0.0f)
#define COLOR_WHITE  PVR_PACK_COLOR(1.0f, 1.0f,  1.0f,  1.0f)

#define CIRCLE_SEGS 12

static pvr_poly_hdr_t hdr_untextured;

static void init_untextured_hdr(void) {
    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    pvr_poly_compile(&hdr_untextured, &cxt);
}

static void draw_quad(float x0, float y0, float x1, float y1,
                      float z, uint32_t color) {
    pvr_vertex_t v;
    v.oargb = 0;
    v.u = 0.0f;
    v.v = 0.0f;
    v.argb = color;

    v.flags = PVR_CMD_VERTEX;
    v.x = x0; v.y = y0; v.z = z;
    pvr_prim(&v, sizeof(v));

    v.x = x1; v.y = y0;
    pvr_prim(&v, sizeof(v));

    v.x = x0; v.y = y1;
    pvr_prim(&v, sizeof(v));

    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = x1; v.y = y1;
    pvr_prim(&v, sizeof(v));
}

static void draw_circle(float cx, float cy, float rx, float ry,
                         float z, uint32_t color) {
    for(int i = 0; i < CIRCLE_SEGS; i++) {
        float a0 = (float)i       * (2.0f * F_PI / CIRCLE_SEGS);
        float a1 = (float)(i + 1) * (2.0f * F_PI / CIRCLE_SEGS);

        pvr_vertex_t v;
        v.oargb = 0;
        v.u = 0.0f;
        v.v = 0.0f;
        v.argb = color;
        v.z = z;

        v.flags = PVR_CMD_VERTEX;
        v.x = cx;
        v.y = cy;
        pvr_prim(&v, sizeof(v));

        v.x = cx + cosf(a0) * rx;
        v.y = cy + sinf(a0) * ry;
        pvr_prim(&v, sizeof(v));

        v.flags = PVR_CMD_VERTEX_EOL;
        v.x = cx + cosf(a1) * rx;
        v.y = cy + sinf(a1) * ry;
        pvr_prim(&v, sizeof(v));
    }
}

static void draw_tv_body(void) {
    pvr_prim(&hdr_untextured, sizeof(pvr_poly_hdr_t));

    /* Main body */
    draw_quad(TX(5.0f), TY(77.0f), TX(97.0f), TY(8.0f), Z_BODY, COLOR_BODY);

    /* Antenna dome — single ball on top-left, center (36, 88) */
    draw_circle(TX(36.0f), TY(88.0f), 10.0f * SCALE, 4.0f * SCALE,
                Z_BODY, COLOR_DARK);

    /* Antenna neck — connects dome to body top */
    draw_quad(TX(33.0f), TY(85.0f), TX(39.0f), TY(77.0f), Z_BODY, COLOR_DARK);

    /* Knob 1 */
    draw_circle(TX(91.0f), TY(65.5f), 6.0f * SCALE, 6.0f * SCALE,
                Z_DETAIL, COLOR_KNOB);

    /* Knob 2 */
    draw_circle(TX(91.0f), TY(50.5f), 6.0f * SCALE, 6.0f * SCALE,
                Z_DETAIL, COLOR_KNOB);

    /* Stand bar */
    draw_quad(TX(5.0f), TY(8.0f), TX(97.0f), TY(5.0f), Z_BODY, COLOR_DARK);

    /* Left foot */
    draw_quad(TX(15.0f), TY(5.0f), TX(25.0f), TY(0.0f), Z_BODY, COLOR_DARK);

    /* Right foot */
    draw_quad(TX(77.0f), TY(5.0f), TX(87.0f), TY(0.0f), Z_BODY, COLOR_DARK);

    /* Black screen background (for letterbox bars) */
    draw_quad(SCREEN_LEFT, SCREEN_TOP, SCREEN_RIGHT, SCREEN_BOTTOM,
              Z_SCREEN, COLOR_BLACK);
}

/*
 * Compute the video quad within the 4:3 screen area, preserving the
 * video's native aspect ratio. 4:3 video fills the screen exactly;
 * wider video (e.g. 16:9) gets letterboxed with black bars top/bottom;
 * taller video gets pillarboxed with black bars left/right.
 */
static void compute_video_rect(int vid_w, int vid_h,
                                float *vx0, float *vy0,
                                float *vx1, float *vy1) {
    float scr_w = SCREEN_RIGHT - SCREEN_LEFT;
    float scr_h = SCREEN_BOTTOM - SCREEN_TOP;
    float vid_aspect = (float)vid_w / (float)vid_h;
    float scr_aspect = scr_w / scr_h;

    float draw_w, draw_h;

    if(fabsf(vid_aspect - scr_aspect) < 0.001f) {
        /* Same aspect — fill entirely */
        draw_w = scr_w;
        draw_h = scr_h;
    } else if(vid_aspect > scr_aspect) {
        /* Video is wider — fit to width, letterbox top/bottom */
        draw_w = scr_w;
        draw_h = scr_w / vid_aspect;
    } else {
        /* Video is taller — fit to height, pillarbox left/right */
        draw_h = scr_h;
        draw_w = scr_h * vid_aspect;
    }

    *vx0 = SCREEN_LEFT + (scr_w - draw_w) / 2.0f;
    *vy0 = SCREEN_TOP  + (scr_h - draw_h) / 2.0f;
    *vx1 = *vx0 + draw_w;
    *vy1 = *vy0 + draw_h;
}

static void draw_video_screen(const pvr_poly_hdr_t *tex_hdr,
                               float u_max, float v_max,
                               float vx0, float vy0,
                               float vx1, float vy1) {
    pvr_prim((void *)tex_hdr, sizeof(pvr_poly_hdr_t));

    pvr_vertex_t v;
    v.oargb = 0;
    v.argb = COLOR_WHITE;

    v.flags = PVR_CMD_VERTEX;
    v.x = vx0;
    v.y = vy0;
    v.z = Z_VIDEO;
    v.u = 0.0f;
    v.v = 0.0f;
    pvr_prim(&v, sizeof(v));

    v.x = vx1;
    v.y = vy0;
    v.u = u_max;
    v.v = 0.0f;
    pvr_prim(&v, sizeof(v));

    v.x = vx0;
    v.y = vy1;
    v.u = 0.0f;
    v.v = v_max;
    pvr_prim(&v, sizeof(v));

    v.flags = PVR_CMD_VERTEX_EOL;
    v.x = vx1;
    v.y = vy1;
    v.u = u_max;
    v.v = v_max;
    pvr_prim(&v, sizeof(v));
}

int main(void) {
    pvr_init_defaults();
    snd_stream_init();

    init_untextured_hdr();

    mpeg_player_options_t options = MPEG_PLAYER_OPTIONS_INITIALIZER;
    options.loop = true;

    mpeg_player_t *player = mpeg_player_create_ex("/rd/sample.mpg", &options);
    if(!player)
        return -1;

    const pvr_poly_hdr_t *tex_hdr = mpeg_player_get_texture_hdr(player);

    float u_scale, v_scale;
    mpeg_player_get_uv_scale(player, &u_scale, &v_scale);

    int vid_w, vid_h;
    mpeg_player_get_dimensions(player, &vid_w, &vid_h);

    /* Compute video quad (letterboxed/pillarboxed within 4:3 screen) */
    float vx0, vy0, vx1, vy1;
    compute_video_rect(vid_w, vid_h, &vx0, &vy0, &vx1, &vy1);

    bool done = false;

    while(!done) {
        MAPLE_FOREACH_BEGIN(MAPLE_FUNC_CONTROLLER, cont_state_t, st)
            if(st->buttons & CONT_START)
                done = true;
        MAPLE_FOREACH_END()

        if(done)
            break;

        mpeg_decode_result_t result = mpeg_decode_step(player);
        if(result == MPEG_DECODE_EOF)
            break;

        pvr_wait_ready();
        pvr_scene_begin();

        if(result == MPEG_DECODE_FRAME)
            mpeg_upload_frame(player);

        pvr_list_begin(PVR_LIST_OP_POLY);

        draw_tv_body();
        draw_video_screen(tex_hdr, u_scale, v_scale, vx0, vy0, vx1, vy1);

        pvr_list_finish();
        pvr_scene_finish();
    }

    mpeg_player_destroy(player);
    return 0;
}
