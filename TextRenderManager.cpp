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
#include <string>

#define PI_GODOT 3.14159265358979323846

using namespace godot;
using namespace jenova::sdk;

// 1. Хранит только текстуру и материал (1 на уникальный дизайн стены)
struct TextStyle {
	SubViewport* viewport;
	Ref<StandardMaterial3D> material;
	Ref<QuadMesh> quad;
};

// 2. Хранит массив инстансов для конкретного 3D-сектора (Чанка)
struct TextChunk {
	MultiMeshInstance3D* mmi;
	Ref<MultiMesh> multimesh;
	std::vector<Transform3D> instances;
};

std::unordered_map<std::string, TextStyle> styles;
std::unordered_map<std::string, TextChunk> chunks;
bool collected = false;

JENOVA_CLASS_NAME("text_render_manager")
JENOVA_SCRIPT_BEGIN

JENOVA_PROPERTY(double, chunk_size, 32.0) // Размер сектора в метрах!

TextStyle& get_or_create_style(Node3D* manager_node, Node* wall_node, const std::string& key) {
	auto it = styles.find(key);
	if (it != styles.end()) return it->second;

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

	if (fw <= 0) fw = 256;
	if (fh <= 0) fh = 256;
	if (t_scale <= 0.01) t_scale = 1.0;

	double quad_h = t_scale;
	double quad_w = t_scale * ((double)fw / (double)fh);

	TextStyle s;

	// СБОРКА ТЕКСТУРЫ
	s.viewport = memnew(SubViewport);
	s.viewport->set_size(Vector2i(fw, fh));
	s.viewport->set_transparent_background(true);
	s.viewport->set_update_mode(SubViewport::UPDATE_ALWAYS);
	manager_node->add_child(s.viewport);

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

	lbl->add_theme_font_size_override("font_size", font_size);
	lbl->add_theme_color_override("font_color", font_color);
	lbl->add_theme_constant_override("line_spacing", line_spacing);
	lbl->add_theme_color_override("font_outline_color", outline_color);
	lbl->add_theme_constant_override("outline_size", outline_size);

	if (enable_shadow) {
		lbl->add_theme_color_override("font_shadow_color", shadow_color);
		lbl->add_theme_constant_override("shadow_offset_x", shadow_x);
		lbl->add_theme_constant_override("shadow_offset_y", shadow_y);
		lbl->add_theme_constant_override("shadow_outline_size", shadow_blur);
	}

	if (!font_path.is_empty()) {
		Ref<Font> custom_font = ResourceLoader::get_singleton()->load(font_path);
		if (custom_font.is_valid()) lbl->add_theme_font_override("font", custom_font);
	}
	
	s.viewport->add_child(lbl);

	// СБОРКА МАТЕРИАЛА И MESH
	s.material.instantiate();
	s.material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, s.viewport->get_texture());
	s.material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
	s.material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	s.material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	s.material->set_billboard_mode(is_billboard ? BaseMaterial3D::BILLBOARD_ENABLED : BaseMaterial3D::BILLBOARD_DISABLED);

	s.quad.instantiate();
	s.quad->set_size(Vector2(quad_w, quad_h));
	s.quad->set_material(s.material);

	styles[key] = s;
	return styles[key];
}

// Получить или создать ЧАНК для расстановки MultiMesh
TextChunk& get_or_create_chunk(Node3D* manager_node, const std::string& chunk_key, TextStyle& style) {
	auto it = chunks.find(chunk_key);
	if (it != chunks.end()) return it->second;

	TextChunk c;
	c.multimesh.instantiate();
	c.multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
	c.multimesh->set_mesh(style.quad); // Используем Mesh из стиля!

	c.mmi = memnew(MultiMeshInstance3D);
	c.mmi->set_multimesh(c.multimesh);
	
	// Включаем встроенный Culling от Godot, чтобы не рендерить невидимые чанки!
	c.mmi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	
	manager_node->add_child(c.mmi);

	chunks[chunk_key] = c;
	return chunks[chunk_key];
}

void generate_face(Node3D* manager_node, const std::string& style_key, TextStyle& style, double c_size, 
				   const Transform3D& parent_xform, Vector3 offset, Vector3 euler_rot, 
				   double w, double h, double quad_w, double quad_h, bool is_billboard) {
	
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
			
			if (is_billboard) local_transform.basis = Basis(); 
			else local_transform.basis = face_rot;

			local_transform.origin = offset + face_rot.xform(local_pos);
			
			// Получаем ГЛОБАЛЬНУЮ позицию именно этого слова
			Transform3D final_xform = parent_xform * local_transform;
			Vector3 global_pos = final_xform.origin;

			// Вычисляем координаты чанка (Сектора)
			int cx = (int)std::floor(global_pos.x / c_size);
			int cy = (int)std::floor(global_pos.y / c_size);
			int cz = (int)std::floor(global_pos.z / c_size);

			// Собираем ключ: Стиль + Координаты чанка
			char chunk_key_buf[512];
			std::snprintf(chunk_key_buf, sizeof(chunk_key_buf), "%s|chunk_%d_%d_%d", style_key.c_str(), cx, cy, cz);
			std::string chunk_key(chunk_key_buf);

			// Добавляем слово в нужный сектор!
			TextChunk& chunk = get_or_create_chunk(manager_node, chunk_key, style);
			chunk.instances.push_back(final_xform);
		}
	}
}

void OnAwake(Caller* instance) {
	styles.clear();
	chunks.clear();
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

	// Получаем размер чанка
	double c_size = chunk_size;
	if (c_size <= 1.0) c_size = 32.0;

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

		// Уникальный ключ СТИЛЯ
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
						 String::num_int64((int)raw_node->get_meta("shadow_blur")) + "_" +
						 String::num_int64(fw) + "_" + String::num_int64(fh);
						 
		std::string unique_style_key = key_str.utf8().get_data();

		// 1. Берем Стиль (Viewport + Material)
		TextStyle& style = get_or_create_style(manager_node, raw_node, unique_style_key);
		
		// 2. Раскидываем экземпляры по Чанкам
		if (size.x >= quad_w && size.y >= quad_h) {
			if (size.z < quad_h) {
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(0, 0, 0), Vector3(0, 0, 0), size.x, size.y, quad_w, quad_h, is_billboard);
			} else {
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(0, 0, size.z / 2.0), Vector3(0, 0, 0), size.x, size.y, quad_w, quad_h, is_billboard);
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(0, 0, -size.z / 2.0), Vector3(0, PI_GODOT, 0), size.x, size.y, quad_w, quad_h, is_billboard);
			}
		}

		if (size.z >= quad_w && size.y >= quad_h) {
			if (size.x < quad_h) {
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), size.z, size.y, quad_w, quad_h, is_billboard);
			} else {
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(size.x / 2.0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), size.z, size.y, quad_w, quad_h, is_billboard);
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(-size.x / 2.0, 0, 0), Vector3(0, PI_GODOT / 2.0, 0), size.z, size.y, quad_w, quad_h, is_billboard);
			}
		}

		if (size.x >= quad_w && size.z >= quad_h) {
			if (size.y < quad_h) {
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(0, 0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), size.x, size.z, quad_w, quad_h, is_billboard);
			} else {
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(0, size.y / 2.0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), size.x, size.z, quad_w, quad_h, is_billboard);
				generate_face(manager_node, unique_style_key, style, c_size, parent_xform, Vector3(0, -size.y / 2.0, 0), Vector3(PI_GODOT / 2.0, 0, 0), size.x, size.z, quad_w, quad_h, is_billboard);
			}
		}
	}

	// 3. Финализация: загружаем все разделенные сектора в видеокарту
	if (processed_any) {
		collected = true;
		for (auto& kv : chunks) {
			TextChunk& c = kv.second;
			c.multimesh->set_instance_count((int)c.instances.size());
			for (int i = 0; i < (int)c.instances.size(); i++) {
				c.multimesh->set_instance_transform(i, c.instances[i]);
			}
		}
		Output("[Manager] Успех! Создано стилей: %d | Сгенерировано Чанков: %d", (int)styles.size(), (int)chunks.size());
	}
}

JENOVA_SCRIPT_END
