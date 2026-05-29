#include "video.h"
#include "config.h" 
#include "../fast3d/glad/glad.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MAX_EXT_FLARES 128

// --- Global Variable Definitions ---
s32 g_FlaresEnabled = 1;
s32 g_FlareCoreEnabled = 1;
s32 g_FlareStreakEnabled = 1;
s32 g_FlareGhostsEnabled = 1;

f32 g_FlareCoreBrightness = 1.0f;
f32 g_FlareStreakBrightness = 1.2f;
f32 g_FlareGhostBrightness = 1.2f;

f32 g_FlareStreakWidth = 2.2f;
f32 g_FlareStreakHeight = 1.5f;
f32 g_FlareStreakDrift = 0.45f;
f32 g_FlareStreakDriftBoost = -0.3f;
f32 g_FlareStreakScatter = 0.05f;

f32 g_FlareGhostDriftClose = 0.45f;
f32 g_FlareGhostDriftFar = -0.4f;

f32 g_FlareGhostBloom = 1.4f;
f32 g_FlareGhostFade = 0.55f;
s32 g_FlareGhostCount = 3;

typedef struct {
    f32 x, y, depth; // Added depth!
    u8 r, g, b, a;
    f32 scale_x, scale_y;
} ExtFlare;

static ExtFlare s_flares[MAX_EXT_FLARES];
static int s_flare_count = 0;

static GLuint flare_shader_program = 0;
static GLuint flare_vao = 0;
static GLuint flare_vbo = 0;

static GLint loc_uPos;
static GLint loc_uScale;
static GLint loc_uColor;
static GLint loc_uType;
static GLint loc_uDepth; // Added uniform for depth!

static const char* vertex_shader_src = 
    "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "uniform vec2 uPos;\n"
    "uniform vec2 uScale;\n"
    "uniform float uDepth;\n" // We pass the exact NDC depth of the light source
    "out vec2 uv;\n"
    "void main() {\n"
    "    uv = aPos * 0.5 + 0.5;\n"
    "    // Place the quad at its correct 3D depth in the OpenGL depth buffer!\n"
    "    gl_Position = vec4(uPos + (aPos * uScale), uDepth, 1.0);\n" 
    "}\n";

static const char* fragment_shader_src = 
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 FragColor;\n"
    "uniform vec4 uColor;\n"
    "uniform int uType;\n" 
    "void main() {\n"
    "    vec2 n_uv = uv - vec2(0.5);\n"
    "    float dist = length(n_uv);\n"
    "    float intensity = 0.0;\n"
    "\n"
    "    if (uType == 0) {\n"
    "        float core = exp(-dist * 18.0) * 3.5;\n"
    "        float ray1 = exp(-abs(n_uv.x + n_uv.y) * 16.0) * 0.35;\n"
    "        float ray2 = exp(-abs(n_uv.x - n_uv.y) * 16.0) * 0.35;\n"
    "        intensity = core + ray1 + ray2;\n"
    "    } \n"
    "    else if (uType == 1) {\n"
    "        vec2 streak_uv = vec2(n_uv.x * 0.03, n_uv.y * 12.0);\n"
    "        intensity = exp(-length(streak_uv) * 10.0) * 1.8;\n"
    "    } \n"
    "    else if (uType == 2) {\n"
    "        float halo = exp(-dist * 6.0) * 0.35;\n"
    "        float ring_dist = abs(dist - 0.3);\n"
    "        float ring = exp(-ring_dist * 35.0) * 0.22;\n"
    "        intensity = halo + ring;\n"
    "    }\n"
    "\n"
    "    float edge_fade = smoothstep(0.5, 0.38, dist);\n"
    "    FragColor = vec4(uColor.rgb * intensity, uColor.a * edge_fade);\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    return shader;
}

void ext_flares_init(void) {
    s_flare_count = 0;

    GLint last_vao, last_vbo;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_vbo);

    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);

    flare_shader_program = glCreateProgram();
    glAttachShader(flare_shader_program, vertex_shader);
    glAttachShader(flare_shader_program, fragment_shader);
    glLinkProgram(flare_shader_program);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    loc_uPos = glGetUniformLocation(flare_shader_program, "uPos");
    loc_uScale = glGetUniformLocation(flare_shader_program, "uScale");
    loc_uColor = glGetUniformLocation(flare_shader_program, "uColor");
    loc_uType = glGetUniformLocation(flare_shader_program, "uType");
    loc_uDepth = glGetUniformLocation(flare_shader_program, "uDepth"); // Get Depth location

    float quad_vertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
         1.0f, -1.0f
    };

    glGenVertexArrays(1, &flare_vao);
    glGenBuffers(1, &flare_vbo);
    glBindVertexArray(flare_vao);
    glBindBuffer(GL_ARRAY_BUFFER, flare_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, last_vbo);
    glBindVertexArray(last_vao);
}

void ext_flares_push(f32 x, f32 y, f32 depth, u8 r, u8 g, u8 b, u8 a, f32 scale_x, f32 scale_y) {
    if (s_flare_count < MAX_EXT_FLARES) {
        s_flares[s_flare_count].x = x;
        s_flares[s_flare_count].y = y;
        s_flares[s_flare_count].depth = depth; // Store depth
        s_flares[s_flare_count].r = r;
        s_flares[s_flare_count].g = g;
        s_flares[s_flare_count].b = b;
        s_flares[s_flare_count].a = a;
        s_flares[s_flare_count].scale_x = scale_x;
        s_flares[s_flare_count].scale_y = scale_y;
        s_flare_count++;
    }
}

void ext_flares_render(void) {
    if (!g_FlaresEnabled || s_flare_count == 0) {
        s_flare_count = 0;
        return;
    }

    // STEALTH MODE: Backup everything
    GLint last_program, last_vao, last_vbo;
    glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_vbo);

    GLboolean last_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_blend = glIsEnabled(GL_BLEND);
    GLboolean last_cull = glIsEnabled(GL_CULL_FACE);
    GLboolean last_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean depth_mask;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

    GLint last_viewport[4];
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLfloat last_clear_color[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, last_clear_color);

    // FLARER RENDER STATE
    glEnable(GL_DEPTH_TEST); // ENABLE DEPTH TESTING! (Let the GPU handle clipping!)
    glDepthFunc(GL_LEQUAL);  // Draw if closer or equal to the geometry in the depth buffer
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); 
    glDepthMask(GL_FALSE); // Read from depth buffer, but do NOT write to it
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); 

    glUseProgram(flare_shader_program);
    glBindVertexArray(flare_vao);

    float native_w = (float)videoGetNativeWidth();
    float native_h = (float)videoGetNativeHeight();

    for (int i = 0; i < s_flare_count; ++i) {
        ExtFlare* f = &s_flares[i];

        float gl_x = (f->x / native_w) * 2.0f - 1.0f;
        float gl_y = -((f->y / native_h) * 2.0f - 1.0f); 
        
        float gl_scale_x = (f->scale_x / native_w);
        float gl_scale_y = (f->scale_y / native_h);

        float visibility = f->a / 255.0f;

        float dist_from_center = sqrtf(gl_x * gl_x + gl_y * gl_y);
        if (dist_from_center > 1.0f) dist_from_center = 1.0f;

        float streak_bell = sinf(dist_from_center * 3.14159265f);

        float ghost_scale_mult = 1.0f + (dist_from_center * g_FlareGhostBloom);
        float ghost_alpha_mult = 1.0f - (dist_from_center * g_FlareGhostFade);
        if (ghost_alpha_mult < 0.0f) ghost_alpha_mult = 0.0f;

        // --- OPTICAL ELEMENT 1: THE STATIONARY BASE CORE ---
        if (g_FlareCoreEnabled && visibility > 0.001f) {
            glUniform1i(loc_uType, 0); 
            glUniform2f(loc_uPos, gl_x, gl_y);
            glUniform1f(loc_uDepth, f->depth); // Pass actual depth of the core!
            glUniform2f(loc_uScale, gl_scale_x * 0.45f * visibility, gl_scale_y * 0.45f * visibility);
            glUniform4f(loc_uColor, f->r / 255.0f, f->g / 255.0f, f->b / 255.0f, visibility * g_FlareCoreBrightness);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        // --- OPTICAL ELEMENT 2: THE DETACHING ANAMORPHIC STREAK(S) ---
        if (g_FlareStreakEnabled && streak_bell > 0.001f && visibility > 0.001f) {
            glUniform1i(loc_uType, 1);

            // Primary Streak
            glUniform2f(loc_uPos, gl_x * (g_FlareStreakDrift + g_FlareStreakDriftBoost), gl_y); 
            glUniform1f(loc_uDepth, f->depth); // Pass depth!
            glUniform2f(loc_uScale, gl_scale_x * g_FlareStreakWidth * streak_bell * visibility, gl_scale_y * g_FlareStreakHeight * visibility); 
            glUniform4f(loc_uColor, f->r / 255.0f, f->g / 255.0f, f->b / 255.0f, (visibility * 0.80f * g_FlareStreakBrightness * streak_bell)); 
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            // Double Dispersion Streak
            if (g_FlareStreakScatter > 0.001f) {
                glUniform2f(loc_uPos, gl_x * (g_FlareStreakDrift + g_FlareStreakDriftBoost + g_FlareStreakScatter), gl_y); 
                glUniform1f(loc_uDepth, f->depth); // Pass depth!
                glUniform2f(loc_uScale, gl_scale_x * g_FlareStreakWidth * 0.75f * streak_bell * visibility, gl_scale_y * g_FlareStreakHeight * 0.85f * visibility); 
                glUniform4f(loc_uColor, f->r / 255.0f, f->g / 255.0f, f->b / 255.0f, (visibility * 0.45f * g_FlareStreakBrightness * streak_bell)); 
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
        }

        // --- OPTICAL ELEMENT 3: THE DYNAMIC LENS GHOST CHAIN ---
        if (g_FlareGhostsEnabled && g_FlareGhostCount > 0 && visibility > 0.001f) {
            glUniform1i(loc_uType, 2);

            for (int j = 0; j < g_FlareGhostCount; ++j) {
                float t = 0.0f;
                if (g_FlareGhostCount > 1) {
                    t = (float)j / (float)(g_FlareGhostCount - 1);
                }

                float drift = g_FlareGhostDriftClose + t * (g_FlareGhostDriftFar - g_FlareGhostDriftClose);
                float size_factor = (0.6f + t * 1.4f) * ghost_scale_mult;
                float opacity_factor = (0.35f - t * 0.15f) * ghost_alpha_mult * g_FlareGhostBrightness;

                glUniform2f(loc_uPos, gl_x * drift, gl_y * drift); 
                glUniform1f(loc_uDepth, f->depth); // Pass depth!
                glUniform2f(loc_uScale, gl_scale_x * size_factor * visibility, gl_scale_y * size_factor * visibility); 
                glUniform4f(loc_uColor, f->r / 255.0f, f->g / 255.0f, f->b / 255.0f, visibility * opacity_factor); 
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
        }
    }

    // STEALTH MODE: Restore everything
    glUseProgram(last_program);
    glBindVertexArray(last_vao);
    glBindBuffer(GL_ARRAY_BUFFER, last_vbo);

    if (last_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (last_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (last_cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (last_scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    glDepthMask(depth_mask);
    glBlendFuncSeparate(last_blend, last_blend, last_blend, last_blend);

    s_flare_count = 0;
}

// --- SELF-CONTAINED CONFIG REGISTRATION ---
void ext_flares_register_config(void) {
	configRegisterInt("Video.FlaresEnabled", &g_FlaresEnabled, 0, 1);
	configRegisterInt("Video.FlareCoreEnabled", &g_FlareCoreEnabled, 0, 1);
	configRegisterInt("Video.FlareStreakEnabled", &g_FlareStreakEnabled, 0, 1);
	configRegisterInt("Video.FlareGhostsEnabled", &g_FlareGhostsEnabled, 0, 1);
	configRegisterFloat("Video.FlareCoreBrightness", &g_FlareCoreBrightness, 0.0f, 10.0f);
	configRegisterFloat("Video.FlareStreakBrightness", &g_FlareStreakBrightness, 0.0f, 10.0f);
	configRegisterFloat("Video.FlareGhostBrightness", &g_FlareGhostBrightness, 0.0f, 10.0f);
	configRegisterFloat("Video.FlareStreakWidth", &g_FlareStreakWidth, 0.0f, 20.0f);
	configRegisterFloat("Video.FlareStreakHeight", &g_FlareStreakHeight, 0.0f, 10.0f);
	configRegisterFloat("Video.FlareStreakDrift", &g_FlareStreakDrift, 0.0f, 2.0f);
	configRegisterFloat("Video.FlareStreakDriftBoost", &g_FlareStreakDriftBoost, -2.0f, 2.0f);
	configRegisterFloat("Video.FlareStreakScatter", &g_FlareStreakScatter, 0.0f, 1.0f);
	configRegisterFloat("Video.FlareGhostDriftClose", &g_FlareGhostDriftClose, -2.0f, 2.0f);
	configRegisterFloat("Video.FlareGhostDriftFar", &g_FlareGhostDriftFar, -2.0f, 2.0f);
	configRegisterFloat("Video.FlareGhostBloom", &g_FlareGhostBloom, 0.0f, 10.0f);
	configRegisterFloat("Video.FlareGhostFade", &g_FlareGhostFade, 0.0f, 1.0f);
	configRegisterInt("Video.FlareGhostCount", &g_FlareGhostCount, 0, 10);
}
