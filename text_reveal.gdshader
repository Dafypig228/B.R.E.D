shader_type spatial;
render_mode unshaded, depth_draw_opaque, cull_disabled, blend_mix;

uniform sampler2D albedo_tex : source_color, filter_linear;
uniform float wipe_softness  = 0.02;
uniform float alpha_cut      : hint_range(0.0, 1.0) = 0.05;
uniform bool  billboard_mode = true;

// Per-instance — каждый MMI задаёт своё значение через set_instance_shader_parameter()
instance uniform float chunk_progress = 1.0;

void vertex() {
    if (billboard_mode) {
        // Канонический Godot billboard: оси MODELVIEW = identity-scaled в view-space.
        // Это ровно то, что делает StandardMaterial3D с BILLBOARD_ENABLED.
        MODELVIEW_MATRIX = VIEW_MATRIX * MODEL_MATRIX;
        MODELVIEW_MATRIX[0] = vec4(length(MODEL_MATRIX[0].xyz), 0.0, 0.0, 0.0);
        MODELVIEW_MATRIX[1] = vec4(0.0, length(MODEL_MATRIX[1].xyz), 0.0, 0.0);
        MODELVIEW_MATRIX[2] = vec4(0.0, 0.0, length(MODEL_MATRIX[2].xyz), 0.0);
        MODELVIEW_NORMAL_MATRIX = mat3(MODELVIEW_MATRIX);
    }
}

void fragment() {
    vec4 t = texture(albedo_tex, UV);
    float reveal = 1.0 - smoothstep(chunk_progress - wipe_softness, chunk_progress, UV.x);
    ALBEDO = t.rgb;
    ALPHA  = t.a * reveal;
    if (ALPHA < alpha_cut) discard;
}
