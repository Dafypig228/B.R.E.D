#include <Godot/godot.hpp>
#include <Godot/classes/static_body3d.hpp>

using namespace godot;
using namespace jenova::sdk;

JENOVA_CLASS_NAME("TextWall")
JENOVA_SCRIPT_BEGIN

// === ФИГМА: КОНТЕНТ ===
JENOVA_PROPERTY(String, block_text, "Typography")
JENOVA_PROPERTY(bool,   uppercase, false)

// === ФИГМА: ТИПОГРАФИКА ===
JENOVA_PROPERTY(String, font_path, "")
JENOVA_PROPERTY(int,    font_size, 64)
JENOVA_PROPERTY(Color,  font_color, Color(1, 1, 1, 1))
JENOVA_PROPERTY(int,    line_spacing, 0)
JENOVA_PROPERTY(int,    align_h, 1) // 0-Лево, 1-Центр, 2-Право, 3-Ширина
JENOVA_PROPERTY(int,    align_v, 1) // 0-Верх, 1-Центр, 2-Низ
JENOVA_PROPERTY(bool,   autowrap, false)

// === ФИГМА: STROKE & SHADOW ===
JENOVA_PROPERTY(int,    outline_size, 0)
JENOVA_PROPERTY(Color,  outline_color, Color(0, 0, 0, 1))
JENOVA_PROPERTY(bool,   enable_shadow, false)
JENOVA_PROPERTY(Color,  shadow_color, Color(0, 0, 0, 0.5))
JENOVA_PROPERTY(int,    shadow_x, 2)
JENOVA_PROPERTY(int,    shadow_y, 2)
JENOVA_PROPERTY(int,    shadow_blur, 0)

// === РАЗРЕШЕНИЕ И 3D ГАБАРИТЫ (Теперь они связаны!) ===
JENOVA_PROPERTY(int,    frame_width, 512)   // Ширина текстуры холста (в пикселях)
JENOVA_PROPERTY(int,    frame_height, 256)  // Высота текстуры холста (в пикселях)
JENOVA_PROPERTY(double, text_scale, 2.0)    // Физическая высота этого холста в 3D метрах 
JENOVA_PROPERTY(bool,   is_billboard, true) // Смотрит ли на камеру

void OnAwake(Caller* instance) {
	StaticBody3D* self_node = GetSelf<StaticBody3D>(instance);
	if (self_node) {
		self_node->set_meta("block_text", Variant(block_text));
		self_node->set_meta("uppercase", Variant(uppercase));
		
		self_node->set_meta("font_path", Variant(font_path));
		self_node->set_meta("font_size", Variant(font_size));
		self_node->set_meta("font_color", Variant(font_color));
		self_node->set_meta("line_spacing", Variant(line_spacing));
		self_node->set_meta("align_h", Variant(align_h));
		self_node->set_meta("align_v", Variant(align_v));
		self_node->set_meta("autowrap", Variant(autowrap));

		self_node->set_meta("outline_size", Variant(outline_size));
		self_node->set_meta("outline_color", Variant(outline_color));
		self_node->set_meta("enable_shadow", Variant(enable_shadow));
		self_node->set_meta("shadow_color", Variant(shadow_color));
		self_node->set_meta("shadow_x", Variant(shadow_x));
		self_node->set_meta("shadow_y", Variant(shadow_y));
		self_node->set_meta("shadow_blur", Variant(shadow_blur));

		self_node->set_meta("frame_width", Variant(frame_width));
		self_node->set_meta("frame_height", Variant(frame_height));
		self_node->set_meta("text_scale", Variant(text_scale));
		self_node->set_meta("is_billboard", Variant(is_billboard));

		self_node->add_to_group("text_wall");
	}
}

JENOVA_SCRIPT_END
