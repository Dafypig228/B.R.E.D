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

// === WIND (непрерывный дрейф с порывами) ===
// Тайлы НЕПРЕРЫВНО летят в направлении wind_dir на расстояние wind_distance,
// потом fade-out и появляются у начала. Визуально — поток.
uniform vec3  wind_dir       = vec3(1.0, 0.0, 0.0);  // куда дует
uniform float wind_strength  = 0.0;                  // 0 = выкл; >0 = вкл
uniform float wind_distance  = 2.0;                  // полная дистанция полёта тайла перед reset (метры)
uniform float wind_speed     = 0.3;                  // циклов в секунду (как часто тайл проходит дистанцию)
uniform float wind_gusts     : hint_range(0.0, 1.0) = 0.5;  // 0 = ровный поток, 1 = рваные порывы (модуляция скорости)
uniform float wind_chaos     : hint_range(0.0, 1.0) = 1.0;  // 0 = все синхронно, 1 = полный desync

// Per-instance reveal
instance uniform float chunk_progress = 1.0;

// Varying для wind-fade на границах цикла (vertex → fragment)
varying float v_wind_fade;

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

    // === WIND — рандомизированный per-tile дрейф в МИРОВЫХ координатах ===
    vec3 wind_world_offset = vec3(0.0);
    v_wind_fade = 1.0;
    if (wind_strength > 0.0) {
        // Per-tile рандомы — стабильные между кадрами, через hash от мировой позиции
        float r_phase = hash13(instance_origin + vec3(11.7, 0.0, 23.1));
        float r_speed = hash13(instance_origin + vec3(3.1,  7.3, 13.5));
        float r_dist  = hash13(instance_origin + vec3(17.2, 5.5, 29.7));
        float r_dirX  = hash13(instance_origin + vec3(2.0,  0.0, 0.0)) - 0.5;
        float r_dirY  = hash13(instance_origin + vec3(0.0,  2.0, 0.0)) - 0.5;
        float r_dirZ  = hash13(instance_origin + vec3(0.0,  0.0, 2.0)) - 0.5;
        float r_curve = hash13(instance_origin + vec3(31.1, 41.3, 47.7));

        // Скорость per-tile: chaos=0 → все одинаково; chaos=1 → разброс 0.3..1.7x
        float speed_var = mix(1.0, 0.3 + r_speed * 1.4, wind_chaos);

        // Глобальный гаст модулирует всех (общие порывы)
        float gust = 1.0 + wind_gusts * sin(TIME * wind_speed * 6.28318 * 0.27 + r_phase * 6.28) * 0.7;
        gust = max(0.1, gust);

        float cycle = fract(TIME * wind_speed * gust * speed_var + r_phase);

        // Направление с per-tile jitter — каждый тайл летит в чуть свою сторону
        vec3 jittered_dir = wind_dir + vec3(r_dirX, r_dirY, r_dirZ) * length(wind_dir) * wind_chaos * 0.8;

        // Дистанция per-tile
        float dist_var = mix(1.0, 0.3 + r_dist * 1.4, wind_chaos);

        // Боковой синусоидальный изгиб — путь виляет, не строгая прямая
        vec3 up_ref = (abs(wind_dir.y) > 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
        vec3 lateral = normalize(cross(wind_dir, up_ref));
        float lateral_amt = sin(cycle * 6.28318 + r_curve * 6.28318) * wind_distance * 0.25 * wind_chaos;

        wind_world_offset = jittered_dir * wind_distance * dist_var * cycle + lateral * lateral_amt;

        // Fade — тайл невидим в момент reset
        v_wind_fade = smoothstep(0.0, 0.25, cycle) * (1.0 - smoothstep(0.75, 1.0, cycle));
    }

    // Wave displacement (вертикальная синусоида — в локальном space)
    if (wave_amp > 0.0) {
        float w = sin(TIME * wave_speed * 6.28318 + chunk_phase + instance_origin.x * wave_freq);
        VERTEX.y += w * wave_amp;
    }

    // Hover (локальный Y — экранный «вверх» после биллборда)
    if (hover_amp > 0.0) {
        float h = sin(TIME * hover_speed * 6.28318 + chunk_phase);
        VERTEX.y += h * hover_amp;
    }

    if (billboard_mode) {
        // Стандартный Godot-биллборд: identity rotation В ВИДОВЫХ ОСЯХ → квад смотрит на камеру.
        MODELVIEW_MATRIX = VIEW_MATRIX * MODEL_MATRIX;
        MODELVIEW_MATRIX[0] = vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0);
        MODELVIEW_MATRIX[1] = vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0);
        MODELVIEW_MATRIX[2] = vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0);
        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
    }

    // Wind в МИРОВЫХ координатах применяем после биллборда, через VIEW-преобразование смещения.
    // Это работает и для биллборда, и для не-биллборда одинаково.
    MODELVIEW_MATRIX[3].xyz += (VIEW_MATRIX * vec4(wind_world_offset, 0.0)).xyz;
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

    // Wind cycle fade (скрывает snap при reset тайла)
    alpha *= v_wind_fade;

    ALBEDO = col;
    ALPHA  = alpha;
    if (ALPHA < alpha_cut) discard;
}
