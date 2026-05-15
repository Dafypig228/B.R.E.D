shader_type spatial;
render_mode unshaded, depth_draw_opaque, cull_disabled, blend_mix;

uniform sampler2D albedo_tex : source_color, filter_linear;
uniform float alpha_cut : hint_range(0.0, 1.0) = 0.5;

void vertex() {
    // Per-instance world position
    vec3 origin = MODEL_MATRIX[3].xyz;

    // Per-instance scale (lengths of basis vectors)
    vec3 scale = vec3(
        length(MODEL_MATRIX[0].xyz),
        length(MODEL_MATRIX[1].xyz),
        length(MODEL_MATRIX[2].xyz)
    );

    // Build a modelview that faces the camera but keeps position + scale
    MODELVIEW_MATRIX = VIEW_MATRIX * mat4(
        vec4(scale.x, 0.0, 0.0, 0.0),
        vec4(0.0, scale.y, 0.0, 0.0),
        vec4(0.0, 0.0, scale.z, 0.0),
        vec4(origin, 1.0)
    );
}

void fragment() {
    vec4 t = texture(albedo_tex, UV);
    ALBEDO = t.rgb;
    ALPHA = t.a;
    ALPHA_SCISSOR_THRESHOLD = alpha_cut;
}