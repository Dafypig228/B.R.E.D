#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>

using namespace godot;
using namespace jenova::sdk;

// Глобальные настройки для ВСЕХ TextWall в сцене.
// Положи этот скрипт на любую Node в сцене — TextWall'ы подхватят значения автоматически.
//
// Семантика sentinel'ов:
//   double-свойства: значение >= 0 → переопределяет локальное значение в каждой стене
//                    отрицательное значение → не трогать, использовать локальное (per-wall)
//   int-свойства:    значение >= 0  → переопределяет
//                    -1            → не трогать

JENOVA_CLASS_NAME("TextWallGlobalSettings")
JENOVA_SCRIPT_BEGIN

JENOVA_PROPERTY(double, render_distance,   -1.0)
JENOVA_PROPERTY(double, chunk_size,        -1.0)
JENOVA_PROPERTY(double, trigger_distance,  -1.0)
JENOVA_PROPERTY(double, tile_spacing,      -1.0)
JENOVA_PROPERTY(int,    distribution_mode, -1)
JENOVA_PROPERTY(int,    text_effect,       -1)
JENOVA_PROPERTY(double, typing_speed,      -1.0)
JENOVA_PROPERTY(double, min_progress,      -1.0)
JENOVA_PROPERTY(double, wipe_softness,     -1.0)

void OnAwake(Caller* instance) {
	Node* self = GetSelf<Node>(instance);
	if (!self) return;
	if (!self->is_in_group("text_wall_global_settings")) {
		self->add_to_group("text_wall_global_settings");
	}
	Output("[TextWallGlobalSettings] registered as global override source");
}

JENOVA_SCRIPT_END
