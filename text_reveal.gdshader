shader_type spatial;
render_mode unshaded, depth_draw_opaque, cull_disabled, blend_mix;

uniform sampler2D albedo_tex : source_color, filter_linear;

uniform float wipe_softness  = 0.02;
uniform float alpha_cut      : hint_range(0.0, 1.0) = 0.05;
uniform bool  billboard_mode = true;

// === Color ===
uniform vec4  tint           : source_color = vec4(1.0, 1.0, 1.0, 1.0);

// === Glow (HDR boost — для real-bloom включи GLOW в WorldEnvironment) ===
uniform float glow_strength  = 0.0;
uniform vec4  glow_color     : source_color = vec4(1.0, 1.0, 1.0, 1.0);

// === Pulse (мягкое мерцание яркости + альфы) ===
uniform float pulse_speed    = 0.0;     // Hz; 0 = off
uniform float pulse_amount   : hint_range(0.0, 1.0) = 0.5;

// === Flicker (резкие случайные провалы альфы — broken sign) ===
uniform float flicker_speed  = 0.0;     // Hz; 0 = off
uniform float flicker_amount : hint_range(0.0, 1.0) = 0.5;

// === Wave (вертикальная синусоида в vertex) ===
uniform float wave_amp       = 0.0;     // амплитуда в мировых единицах
uniform float wave_speed     = 1.0;     // Hz
uniform float wave_freq      = 0.3;     // частота по позиции

// === Rainbow (вращение оттенка) ===
uniform float rainbow_speed  = 0.0;     // Hz; 0 = off

// === Scanlines (горизонтальные линии — голограмма / CRT) ===
uniform float scanline_strength : hint_range(0.0, 1.0) = 0.0;
uniform float scanline_count    = 100.0;

// === Hover (плавный подъём-падение) ===
uniform float hover_amp      = 0.0;     // амплитуда
uniform float hover_speed    = 0.5;     // Hz

// Per-instance reveal
instance uniform float chunk_progress = 1.0;

// HSV → RGB
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Псевдо-hash от 3D позиции для per-chunk фазы
float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

void vertex() {
    vec3 instance_origin = MODEL_MATRIX[3].xyz;
    float chunk_phase = hash13(instance_origin) * 6.28318;

    // Wave displacement
    if (wave_amp > 0.0) {
        float w = sin(TIME * wave_speed * 6.28318 + chunk_phase + instance_origin.x * wave_freq);
        VERTEX.y += w * wave_amp;
    }

    // Hover (общий float-up/down для всех чанков, с per-chunk дисинхронизацией)
    if (hover_amp > 0.0) {
        float h = sin(TIME * hover_speed * 6.28318 + chunk_phase);
        VERTEX.y += h * hover_amp;
    }

    if (billboard_mode) {
        MODELVIEW_MATRIX = VIEW_MATRIX * MODEL_MATRIX;
        MODELVIEW_MATRIX[0] = vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0);
        MODELVIEW_MATRIX[1] = vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0);
        MODELVIEW_MATRIX[2] = vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0);
        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
    }
}

void fragment() {
    vec4 t = texture(albedo_tex, UV);
    float chunk_phase = hash13(MODEL_MATRIX[3].xyz) * 6.28318;

    // Маска печати (slot 1 — UV-wipe)
    float reveal = 1.0 - smoothstep(chunk_progress - wipe_softness, chunk_progress, UV.x);

    // Базовый цвет с тинтом
    vec3 col = t.rgb * tint.rgb;
    float alpha = t.a * reveal * tint.a;

    // Радуга (hue rotation, сохраняя яркость)
    if (rainbow_speed > 0.0) {
        float hue = fract(TIME * rainbow_speed + chunk_phase / 6.28318);
        float lum = max(dot(col, vec3(0.299, 0.587, 0.114)), 0.001);
        col = hsv2rgb(vec3(hue, 1.0, 1.0)) * lum;
    }

    // Pulse (мягкое мерцание)
    if (pulse_speed > 0.0) {
        float p = 0.5 + 0.5 * sin(TIME * pulse_speed * 6.28318 + chunk_phase);
        float mul = mix(1.0 - pulse_amount, 1.0, p);
        col   *= mul;
        alpha *= mix(1.0 - pulse_amount * 0.5, 1.0, p);
    }

    // Flicker (резкие случайные провалы)
    if (flicker_speed > 0.0) {
        float t_seed = floor(TIME * flicker_speed);
        float r = fract(sin(t_seed * 12.9898 + chunk_phase * 78.233) * 43758.5453);
        if (r < flicker_amount) {
            alpha *= 1.0 - flicker_amount;
        }
    }

    // Scanlines (горизонтальные тёмные полосы)
    if (scanline_strength > 0.0) {
        float scan = 0.5 + 0.5 * sin(UV.y * scanline_count * 6.28318);
        col *= mix(1.0, scan, scanline_strength);
    }

    // Glow: HDR boost (требует GLOW post-effect в WorldEnvironment для bloom)
    if (glow_strength > 0.0) {
        col = mix(col, glow_color.rgb, 0.3) * (1.0 + glow_strength);
    }

    ALBEDO = col;
    ALPHA  = alpha;
    if (ALPHA < alpha_cut) discard;
}
