#include <Godot/godot.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/multi_mesh_instance3d.hpp>
#include <Godot/classes/multi_mesh.hpp>
#include <Godot/classes/quad_mesh.hpp>
#include <Godot/classes/standard_material3d.hpp>
#include <Godot/classes/sub_viewport.hpp>
#include <Godot/classes/label.hpp>
#include <Godot/classes/scene_tree.hpp>

#include <Godot/classes/viewport_texture.hpp>
#include <Godot/classes/texture2d.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/box_shape3d.hpp>

#include <cmath>
#include <algorithm>
#include <vector>

#define PI_GODOT 3.14159265358979323846

using namespace godot;
using namespace jenova::sdk;

struct TextGroup {
	SubViewport* viewport;
	Ref<StandardMaterial3D> material;
	Ref<MultiMesh> multimesh;
	MultiMeshInstance3D* mmi;
	std::vector<Transform3D> instances;
};

std::unordered_map<std::string, TextGroup> groups;
bool collected = false;

JENOVA_CLASS_NAME("text_render_manager")
JENOVA_SCRIPT_BEGIN

JENOVA_PROPERTY(int, font_size, 64)
JENOVA_PROPERTY(int, texture_resolution, 256)

TextGroup& get_or_create_group(Node3D* manager_node, const String& text, double t_size) {
	std::string key = text.utf8().get_data();
	auto it = groups.find(key);
	if (it != groups.end()) return it->second;

	TextGroup g;

	g.viewport = memnew(SubViewport);
	g.viewport->set_size(Vector2i(texture_resolution, texture_resolution));
	g.viewport->set_transparent_background(true);
	g.viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	manager_node->add_child(g.viewport);

	Label* lbl = memnew(Label);
	lbl->set_text(text);
	lbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	lbl->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	lbl->set_anchors_preset(Control::PRESET_FULL_RECT);
	lbl->add_theme_font_size_override("font_size", font_size);
	g.viewport->add_child(lbl);

	g.material.instantiate();
	g.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
	g.material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
	g.material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	g.material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	g.material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, g.viewport->get_texture());

	Ref<QuadMesh> quad;
	quad.instantiate();
	quad->set_size(Vector2(t_size, t_size));
	quad->set_material(g.material);

	g.multimesh.instantiate();
	g.multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
	g.multimesh->set_mesh(quad);

	g.mmi = memnew(MultiMeshInstance3D);
	g.mmi->set_multimesh(g.multimesh);
	manager_node->add_child(g.mmi);

	auto [inserted, _] = groups.emplace(key, std::move(g));
	return inserted->second;
}

void generate_face(std::vector<Transform3D>& instances, const Transform3D& parent_xform, Vector3 offset, Vector3 euler_rot, double w, double h, double spacing_x, double spacing_y, double t_size) {
	
	int count_x = 0;
	int count_y = 0;

	if (w >= t_size) count_x = 1 + (int)std::floor((w - t_size) / spacing_x);
	if (h >= t_size) count_y = 1 + (int)std::floor((h - t_size) / spacing_y);

	if (count_x <= 0 || count_y <= 0) return;

	double start_x = -(count_x - 1) * spacing_x * 0.5;
	double start_y = -(count_y - 1) * spacing_y * 0.5;

	Basis rot = Basis::from_euler(euler_rot);

	for (int x = 0; x < count_x; x++) {
		for (int y = 0; y < count_y; y++) {
			Vector3 local_pos = Vector3(start_x + x * spacing_x, start_y + y * spacing_y, 0); 
			
			Transform3D local_transform;
			local_transform.basis = Basis(); 
			local_transform.origin = offset + rot.xform(local_pos);
			
			instances.push_back(parent_xform * local_transform);
		}
	}
}

void OnAwake(Caller* instance) {
	groups.clear();
	collected = false;
}

void OnProcess(Caller* instance, double delta) {
	if (collected) return;
	
	Node3D* manager_node = GetSelf<Node3D>(instance);
	if (!manager_node) return;

	SceneTree* tree = manager_node->get_tree();
	if (!tree) return;

	TypedArray<Node> nodes = tree->get_nodes_in_group("text_wall");
	if (nodes.size() == 0) return; 

	bool processed_any = false;

	for (int i = 0; i < nodes.size(); i++) {
		Node* raw_node = Object::cast_to<Node>(nodes[i]);
		if (!raw_node) continue;

		if (!raw_node->has_meta("block_text")) continue; 

		String text = raw_node->get_meta("block_text");
		double t_size = (double)raw_node->get_meta("text_size");
		double spacing_x = (double)raw_node->get_meta("text_spacing_x");
		double spacing_y = (double)raw_node->get_meta("text_spacing_y");

		if (t_size <= 0.1) t_size = 2.0;
		if (spacing_x <= 0.1) spacing_x = 2.0;
		if (spacing_y <= 0.1) spacing_y = 1.0;

		bool found_box = false;
		Vector3 size;
		Transform3D parent_xform;

		for (int c = 0; c < raw_node->get_child_count(); c++) {
			Node* child = raw_node->get_child(c);
			if (!child) continue;

			if (child->is_class("CollisionShape3D")) {
				Variant shape_var = child->get("shape");
				if (shape_var.get_type() != Variant::NIL && shape_var.has_method("get_size")) {
					size = shape_var.call("get_size");
					parent_xform = child->get("global_transform");
					found_box = true;
					break;
				}
			}
		}

		if (!found_box) continue;

		processed_any = true;
		TextGroup& g = get_or_create_group(manager_node, text, t_size);
		
		// --- НОВАЯ УМНАЯ ЛОГИКА ГЕНЕРАЦИИ СТЕН ---
		// Скрипт анализирует толщину коробки BoxShape. Если стена тоньше, чем размер одного слова, 
		// он не рисует заднюю стенку, избавляя нас от иллюзии двойного текста!

		// 1. Ось Z (Перед и Зад)
		if (size.x >= t_size && size.y >= t_size) {
			if (size.z < t_size) {
				// Стена тонкая: рисуем один слой ровно по центру
				generate_face(g.instances, parent_xform, Vector3(0, 0, 0), Vector3(0, 0, 0), size.x, size.y, spacing_x, spacing_y, t_size);
			} else {
				// Стена толстая (куб): рисуем две стороны
				generate_face(g.instances, parent_xform, Vector3(0, 0, size.z / 2.0), Vector3(0, 0, 0), size.x, size.y, spacing_x, spacing_y, t_size);
				generate_face(g.instances, parent_xform, Vector3(0, 0, -size.z / 2.0), Vector3(0, PI_GODOT, 0), size.x, size.y, spacing_x, spacing_y, t_size);
			}
		}

		// 2. Ось X (Право и Лево)
		if (size.z >= t_size && size.y >= t_size) {
			if (size.x < t_size) {
				generate_face(g.instances, parent_xform, Vector3(0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), size.z, size.y, spacing_x, spacing_y, t_size);
			} else {
				generate_face(g.instances, parent_xform, Vector3(size.x / 2.0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), size.z, size.y, spacing_x, spacing_y, t_size);
				generate_face(g.instances, parent_xform, Vector3(-size.x / 2.0, 0, 0), Vector3(0, PI_GODOT / 2.0, 0), size.z, size.y, spacing_x, spacing_y, t_size);
			}
		}

		// 3. Ось Y (Пол и Потолок)
		if (size.x >= t_size && size.z >= t_size) {
			if (size.y < t_size) {
				generate_face(g.instances, parent_xform, Vector3(0, 0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), size.x, size.z, spacing_x, spacing_x, t_size);
			} else {
				generate_face(g.instances, parent_xform, Vector3(0, size.y / 2.0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), size.x, size.z, spacing_x, spacing_x, t_size);
				generate_face(g.instances, parent_xform, Vector3(0, -size.y / 2.0, 0), Vector3(PI_GODOT / 2.0, 0, 0), size.x, size.z, spacing_x, spacing_x, t_size);
			}
		}
	}

	if (processed_any) {
		collected = true;
		for (auto& kv : groups) {
			TextGroup& g = kv.second;
			g.multimesh->set_instance_count((int)g.instances.size());
			for (int i = 0; i < (int)g.instances.size(); i++) {
				g.multimesh->set_instance_transform(i, g.instances[i]);
			}
		}
	}
}

JENOVA_SCRIPT_END
