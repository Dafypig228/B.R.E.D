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
#include <Godot/classes/resource_loader.hpp>
#include <Godot/classes/font.hpp>

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

TextGroup& get_or_create_group(Node3D* manager_node, Node* wall_node, const std::string& key) {
	auto it = groups.find(key);
	if (it != groups.end()) return it->second;

	// 1. Читаем параметры из узла стены
	String text = wall_node->get_meta("block_text");
	bool uppercase = wall_node->get_meta("uppercase");
	String font_path = wall_node->get_meta("font_path");
	int font_size = wall_node->get_meta("font_size");
	Color font_color = wall_node->get_meta("font_color");
	int line_spacing = wall_node->get_meta("line_spacing");
	int align_h = wall_node->get_meta("align_h");
	int align_v = wall_node->get_meta("align_v");
	bool autowrap = wall_node->get_meta("autowrap");
	
	int outline_size = wall_node->get_meta("outline_size");
	Color outline_color = wall_node->get_meta("outline_color");
	bool enable_shadow = wall_node->get_meta("enable_shadow");
	Color shadow_color = wall_node->get_meta("shadow_color");
	int shadow_x = wall_node->get_meta("shadow_x");
	int shadow_y = wall_node->get_meta("shadow_y");
	int shadow_blur = wall_node->get_meta("shadow_blur");
	
	int fw = wall_node->get_meta("frame_width");
	int fh = wall_node->get_meta("frame_height");
	double t_scale = wall_node->get_meta("text_scale");
	bool is_billboard = wall_node->get_meta("is_billboard");

	// Защита от крашей
	if (fw <= 0) fw = 256;
	if (fh <= 0) fh = 256;
	if (t_scale <= 0.01) t_scale = 1.0;

	// === МАГИЯ ФРЕЙМОВ ===
	// Физический размер 3D тайла рассчитывается напрямую из пропорций фрейма!
	double quad_h = t_scale;
	double quad_w = t_scale * ((double)fw / (double)fh);

	TextGroup g;

	// === ФИГМА: НАСТРОЙКА ХОЛСТА (FRAME) ===
	g.viewport = memnew(SubViewport);
	g.viewport->set_size(Vector2i(fw, fh));
	g.viewport->set_transparent_background(true);
	g.viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	manager_node->add_child(g.viewport);

	Label* lbl = memnew(Label);
	lbl->set_text(text);
	lbl->set_uppercase(uppercase);
	
	lbl->call("set_autowrap_mode", autowrap ? 3 : 0); 

	if (align_h == 0) lbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_LEFT);
	else if (align_h == 1) lbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	else if (align_h == 2) lbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
	else lbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_FILL); 

	if (align_v == 0) lbl->set_vertical_alignment(VERTICAL_ALIGNMENT_TOP);
	else if (align_v == 1) lbl->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	else lbl->set_vertical_alignment(VERTICAL_ALIGNMENT_BOTTOM);
	
	lbl->set_anchors_preset(Control::PRESET_FULL_RECT);

	// === ФИГМА: ТИПОГРАФИКА ===
	lbl->add_theme_font_size_override("font_size", font_size);
	lbl->add_theme_color_override("font_color", font_color);
	lbl->add_theme_constant_override("line_spacing", line_spacing);

	lbl->add_theme_color_override("font_outline_color", outline_color);
	lbl->add_theme_constant_override("outline_size", outline_size);

	if (enable_shadow) {
		lbl->add_theme_color_override("font_shadow_color", shadow_color);
		lbl->add_theme_constant_override("shadow_offset_x", shadow_x);
		lbl->add_theme_constant_override("shadow_offset_y", shadow_y);
		lbl->add_theme_constant_override("shadow_outline_size", shadow_blur); // Мягкая тень!
	}

	if (!font_path.is_empty()) {
		Ref<Font> custom_font = ResourceLoader::get_singleton()->load(font_path);
		if (custom_font.is_valid()) lbl->add_theme_font_override("font", custom_font);
	}
	
	g.viewport->add_child(lbl);

	// === 3D МАТЕРИАЛ ===
	g.material.instantiate();
	g.material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, g.viewport->get_texture());
	g.material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
	g.material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	g.material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);

	if (is_billboard) {
		g.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
	} else {
		g.material->set_billboard_mode(BaseMaterial3D::BILLBOARD_DISABLED);
	}

	Ref<QuadMesh> quad;
	quad.instantiate();
	// ВАЖНО: Размеры QuadMesh теперь идеально совпадают с пропорциями фрейма
	quad->set_size(Vector2(quad_w, quad_h));
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

// Теперь функция принимает только quad_w и quad_h. Они служат и размером, и отступом!
void generate_face(std::vector<Transform3D>& instances, const Transform3D& parent_xform, Vector3 offset, Vector3 euler_rot, double w, double h, double quad_w, double quad_h, bool is_billboard) {
	
	int count_x = (int)std::floor(w / quad_w);
	int count_y = (int)std::floor(h / quad_h);

	if (count_x <= 0 || count_y <= 0) return;

	double start_x = -(count_x - 1) * quad_w * 0.5;
	double start_y = -(count_y - 1) * quad_h * 0.5;

	Basis face_rot = Basis::from_euler(euler_rot);

	for (int x = 0; x < count_x; x++) {
		for (int y = 0; y < count_y; y++) {
			Vector3 local_pos = Vector3(start_x + x * quad_w, start_y + y * quad_h, 0); 
			Transform3D local_transform;
			
			if (is_billboard) {
				local_transform.basis = Basis(); 
			} else {
				local_transform.basis = face_rot;
			}

			local_transform.origin = offset + face_rot.xform(local_pos);
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
		if (!raw_node || !raw_node->has_meta("block_text")) continue; 

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

		int fw = raw_node->get_meta("frame_width");
		int fh = raw_node->get_meta("frame_height");
		double t_scale = raw_node->get_meta("text_scale");
		bool is_billboard = raw_node->get_meta("is_billboard");

		if (fw <= 0) fw = 256;
		if (fh <= 0) fh = 256;
		if (t_scale <= 0.01) t_scale = 1.0;

		double quad_h = t_scale;
		double quad_w = t_scale * ((double)fw / (double)fh);

		// Уникальный ключ стиля со всеми параметрами Фигмы
		String key_str = String(raw_node->get_meta("block_text")) + "_" + 
						 String::num_int64((bool)raw_node->get_meta("uppercase")) + "_" +
						 String(raw_node->get_meta("font_path")) + "_" + 
						 String::num_int64((int)raw_node->get_meta("font_size")) + "_" +
						 ((Color)raw_node->get_meta("font_color")).to_html() + "_" + 
						 String::num_int64((int)raw_node->get_meta("line_spacing")) + "_" +
						 String::num_int64((int)raw_node->get_meta("align_h")) + "_" + 
						 String::num_int64((int)raw_node->get_meta("align_v")) + "_" +
						 String::num_int64((bool)raw_node->get_meta("autowrap")) + "_" + 
						 String::num_int64((int)raw_node->get_meta("outline_size")) + "_" +
						 ((Color)raw_node->get_meta("outline_color")).to_html() + "_" + 
						 String::num_int64((bool)raw_node->get_meta("enable_shadow")) + "_" +
						 ((Color)raw_node->get_meta("shadow_color")).to_html() + "_" + 
						 String::num_int64((int)raw_node->get_meta("shadow_x")) + "_" +
						 String::num_int64((int)raw_node->get_meta("shadow_y")) + "_" + 
						 String::num_int64((int)raw_node->get_meta("shadow_blur")) + "_" +
						 String::num_int64(fw) + "_" + String::num_int64(fh) + "_" +
						 String::num(t_scale) + "_" + String::num_int64(is_billboard);
						 
		std::string unique_style_key = key_str.utf8().get_data();

		TextGroup& g = get_or_create_group(manager_node, raw_node, unique_style_key);
		
		// 1. Ось Z (Перед и Зад)
		if (size.x >= quad_w && size.y >= quad_h) {
			if (size.z < quad_h) {
				generate_face(g.instances, parent_xform, Vector3(0, 0, 0), Vector3(0, 0, 0), size.x, size.y, quad_w, quad_h, is_billboard);
			} else {
				generate_face(g.instances, parent_xform, Vector3(0, 0, size.z / 2.0), Vector3(0, 0, 0), size.x, size.y, quad_w, quad_h, is_billboard);
				generate_face(g.instances, parent_xform, Vector3(0, 0, -size.z / 2.0), Vector3(0, PI_GODOT, 0), size.x, size.y, quad_w, quad_h, is_billboard);
			}
		}

		// 2. Ось X (Право и Лево)
		if (size.z >= quad_w && size.y >= quad_h) {
			if (size.x < quad_h) {
				generate_face(g.instances, parent_xform, Vector3(0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), size.z, size.y, quad_w, quad_h, is_billboard);
			} else {
				generate_face(g.instances, parent_xform, Vector3(size.x / 2.0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), size.z, size.y, quad_w, quad_h, is_billboard);
				generate_face(g.instances, parent_xform, Vector3(-size.x / 2.0, 0, 0), Vector3(0, PI_GODOT / 2.0, 0), size.z, size.y, quad_w, quad_h, is_billboard);
			}
		}

		// 3. Ось Y (Пол и Потолок)
		// ВНИМАНИЕ: На полу слова ложатся вдоль осей X и Z. Ширина мапится на X, высота на Z.
		if (size.x >= quad_w && size.z >= quad_h) {
			if (size.y < quad_h) {
				generate_face(g.instances, parent_xform, Vector3(0, 0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), size.x, size.z, quad_w, quad_h, is_billboard);
			} else {
				generate_face(g.instances, parent_xform, Vector3(0, size.y / 2.0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), size.x, size.z, quad_w, quad_h, is_billboard);
				generate_face(g.instances, parent_xform, Vector3(0, -size.y / 2.0, 0), Vector3(PI_GODOT / 2.0, 0, 0), size.x, size.z, quad_w, quad_h, is_billboard);
			}
		}
	}

	// Загружаем всю сетку в видеокарту
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
