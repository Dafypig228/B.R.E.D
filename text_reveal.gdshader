shader_type spatial;
render_mode unshaded, depth_draw_opaque, cull_back;

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

// === WIND (текст уносит потоком) ===
// При wind_strength > 0 текст срывается с сетки и летит потоком по wind_dir.
// Тайлы широко разбросаны → сетки нет; непрерывно дрейфуют в одну сторону.
// Рекайкл скрыт фейдом + разбросом. От камеры не зависит.
uniform vec3  wind_dir        = vec3(1.0, 0.0, 0.0);  // куда дует ветер (world space)
uniform float wind_strength   = 0.0;                  // 0 = выкл; ~1 = сильный ветер
uniform float wind_speed      = 1.0;                  // скорость потока
// Мировая позиция объекта. Сид ветра считаем как (тайл - объект), т.е. в
// ЛОКАЛЬНЫХ координатах — иначе на движущемся объекте мировая позиция тайла
// плывёт и per-tile рандом мерцает. Для статики = константа.
uniform vec3  wall_origin     = vec3(0.0);

// Per-instance reveal
instance uniform float chunk_progress = 1.0;

// Wind recycle fade (vertex → fragment)
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

    // === WIND — текст уносит потоком в МИРОВЫХ координатах ===
    vec3 wind_world_offset = vec3(0.0);
    v_wind_fade = 1.0;
    if (wind_strength > 0.0) {
        vec3 wdir = normalize(wind_dir);

        // per-tile сид. instance_origin = MODEL_MATRIX[3] — мировой центр тайла
        // (у MultiMesh MODEL_MATRIX поинстансный, как в grass-шейдерах).
        // Минус позиция объекта → ЛОКАЛЬНЫЙ сид, не плывёт при движении объекта.
        vec3 seed = instance_origin - wall_origin;
        float r1 = hash13(seed + vec3( 1.3,  7.7,  3.1));
        float r2 = hash13(seed + vec3(11.7, 23.1,  5.5));
        float r3 = hash13(seed + vec3(41.2,  7.9, 33.6));
        float r4 = hash13(seed + vec3(63.8, 51.4, 19.2));
        float r5 = hash13(seed + vec3(91.3, 12.8, 67.9));

        // Оси поперёк ветра
        vec3 up_ref = (abs(wdir.y) > 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
        vec3 lat1 = normalize(cross(wdir, up_ref));
        vec3 lat2 = normalize(cross(wdir, lat1));

        // Масштабы большие — текст реально уносит, сетка тонет в разбросе
        float spread = wind_strength * 8.0;    // ширина облака (метры)
        float travel = wind_strength * 12.0;   // длина дрейфа (метры)

        // Прогресс полёта: монотонный, у каждого тайла своя фаза и скорость
        float spd = wind_speed * 0.12 * (0.6 + r2 * 0.8);
        float flight = fract(TIME * spd + r1);

        // Большой случайный разброс поперёк ветра — ГЛАВНОЕ, что убивает сетку
        vec3 scatter = (lat1 * (r3 - 0.5) + lat2 * (r4 - 0.5)) * spread;

        // Полёт по ветру: случайный старт вдоль + дрейф (только вперёд)
        float along = (r5 - 0.5) * spread + flight * travel;

        wind_world_offset = wdir * along + scatter;

        // Фейд на стыке цикла — рекайкл не виден в случайном облаке
        v_wind_fade = smoothstep(0.0, 0.12, flight) * (1.0 - smoothstep(0.88, 1.0, flight));
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

    // Wind recycle fade — прячет рекайкл тайла на стыке цикла
    alpha *= v_wind_fade;

    ALBEDO = col;
    ALPHA  = alpha;
    // Alpha-scissor: текст рисуется как НЕПРОЗРАЧНЫЙ (z-тест отсекает overdraw).
    // Пиксель либо полностью виден, либо отброшен — без альфа-смешивания.
    ALPHA_SCISSOR_THRESHOLD = alpha_cut;
}
