#include <Godot/godot.hpp>
#include <Godot/classes/static_body3d.hpp>

using namespace godot;
using namespace jenova::sdk;

JENOVA_CLASS_NAME("TextWall")
JENOVA_SCRIPT_BEGIN

// Эти переменные теперь будут идеально работать из Инспектора!
JENOVA_PROPERTY(String, block_text, "text")
JENOVA_PROPERTY(double, text_size, 2.0)
JENOVA_PROPERTY(double, text_spacing_x, 2.0) // Шаг по горизонтали (между буквами)
JENOVA_PROPERTY(double, text_spacing_y, 1.0) // Шаг по вертикали (строки)

void OnAwake(Caller* instance) {
	StaticBody3D* self_node = GetSelf<StaticBody3D>(instance);
	if (self_node) {
		// ПРИНУДИТЕЛЬНО отправляем значения Инспектора в ядро Godot
		self_node->set_meta("block_text", Variant(block_text));
		self_node->set_meta("text_size", Variant(text_size));
		self_node->set_meta("spacing_x", Variant(text_spacing_x));
		self_node->set_meta("spacing_y", Variant(text_spacing_y));
		
		self_node->add_to_group("text_wall");
	}
}

JENOVA_SCRIPT_END
