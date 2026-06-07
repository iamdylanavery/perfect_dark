#ifndef EXT_CAMO_SHADER_H
#define EXT_CAMO_SHADER_H

#define EXT_CAMO_APPEND_UNIFORMS(fs_buf, fs_len) \
    append_line(fs_buf, fs_len, "uniform int u_CamoActive;"); \
    append_line(fs_buf, fs_len, "uniform float u_CamoAlpha;"); \
    append_line(fs_buf, fs_len, "uniform sampler2D u_CamoGrabTex;"); \
    append_line(fs_buf, fs_len, "uniform vec2 u_ViewportSize;"); \
    append_line(fs_buf, fs_len, "uniform float u_CamoProgress;"); \
    append_line(fs_buf, fs_len, "uniform int u_CamoIsNPC;"); \
    append_line(fs_buf, fs_len, "uniform float u_CamoDistPlayer;"); \
    append_line(fs_buf, fs_len, "uniform float u_CamoDistNPC;"); \
    append_line(fs_buf, fs_len, "uniform float u_CamoAberration;"); \
    append_line(fs_buf, fs_len, "uniform vec2 u_CamoOffset;"); \
    append_line(fs_buf, fs_len, "uniform vec3 u_CamoShimmerRGB;"); \
    append_line(fs_buf, fs_len, "uniform float u_CamoShimmerThickness;"); 

#define EXT_CAMO_APPEND_LOGIC(fs_buf, fs_len, opt_alpha) \
    append_line(fs_buf, fs_len, "    if (u_CamoActive == 1) {"); \
    append_line(fs_buf, fs_len, "        vec2 screenUV = gl_FragCoord.xy / u_ViewportSize;"); \
    append_line(fs_buf, fs_len, "        float sweepY = 1.0 - u_CamoProgress;"); \
    append_line(fs_buf, fs_len, "        if (screenUV.y >= sweepY) {"); \
    append_line(fs_buf, fs_len, "            float distMult = (u_CamoIsNPC == 1) ? u_CamoDistNPC : u_CamoDistPlayer;"); \
    append_line(fs_buf, fs_len, "            vec2 distortion = (texel.rg - vec2(0.5)) * distMult;"); \
    append_line(fs_buf, fs_len, "            vec2 distR = distortion * (1.0 + 0.4 * u_CamoAberration);"); \
    append_line(fs_buf, fs_len, "            vec2 distG = distortion;"); \
    append_line(fs_buf, fs_len, "            vec2 distB = distortion * (1.0 - 0.4 * u_CamoAberration);"); \
    append_line(fs_buf, fs_len, "            vec2 baseUV = screenUV + u_CamoOffset;"); \
    append_line(fs_buf, fs_len, "            float r = SAMPLE_TEX(u_CamoGrabTex, baseUV + distR).r;"); \
    append_line(fs_buf, fs_len, "            float g = SAMPLE_TEX(u_CamoGrabTex, baseUV + distG).g;"); \
    append_line(fs_buf, fs_len, "            float b = SAMPLE_TEX(u_CamoGrabTex, baseUV + distB).b;"); \
    append_line(fs_buf, fs_len, "            float glare = max(texel.r, max(texel.g, texel.b)) * 0.15;"); \
    append_line(fs_buf, fs_len, "            vec3 glassPixel = vec3(r, g, b) + glare;"); \
    append_line(fs_buf, fs_len, "            if (u_CamoIsNPC == 1) {"); \
    append_line(fs_buf, fs_len, "                glassPixel = mix(glassPixel, texel.rgb, 0.01);"); \
    append_line(fs_buf, fs_len, "            }"); \
    if (opt_alpha) { \
        append_line(fs_buf, fs_len, "            texel = vec4(glassPixel, 1.0);"); \
    } else { \
        append_line(fs_buf, fs_len, "            texel = glassPixel;"); \
    } \
    append_line(fs_buf, fs_len, "        }"); \
    append_line(fs_buf, fs_len, "        float distToLine = abs(screenUV.y - sweepY);"); \
    append_line(fs_buf, fs_len, "        if (distToLine < u_CamoShimmerThickness && u_CamoProgress > 0.01 && u_CamoProgress < 0.99) {"); \
    append_line(fs_buf, fs_len, "            float glowIntensity = 1.0 - (distToLine / u_CamoShimmerThickness);"); \
    append_line(fs_buf, fs_len, "            texel.rgb = mix(texel.rgb, u_CamoShimmerRGB, glowIntensity);"); \
    if (opt_alpha) { \
        append_line(fs_buf, fs_len, "            texel.a = 1.0;"); \
    } \
    append_line(fs_buf, fs_len, "        }"); \
    if (opt_alpha) { \
        append_line(fs_buf, fs_len, "        OUTPUT_COLOR = texel;"); \
    } else { \
        append_line(fs_buf, fs_len, "        OUTPUT_COLOR = vec4(texel, 1.0);"); \
    } \
    append_line(fs_buf, fs_len, "        return;"); \
    append_line(fs_buf, fs_len, "    }");

#endif // EXT_CAMO_SHADER_H