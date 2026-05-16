#include <Godot/godot.hpp>
#include <Godot/classes/static_body3d.hpp>
#include <Godot/classes/multi_mesh_instance3d.hpp>
#include <Godot/classes/multi_mesh.hpp>
#include <Godot/classes/quad_mesh.hpp>
#include <Godot/classes/standard_material3d.hpp>
#include <Godot/classes/sub_viewport.hpp>
#include <Godot/classes/rich_text_label.hpp> // <-- ТЕПЕРЬ МЫ ИСПОЛЬЗУЕМ RICH TEXT!
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/box_shape3d.hpp>
#include <Godot/classes/resource_loader.hpp>
#include <Godot/classes/font.hpp>
#include <Godot/classes/viewport_texture.hpp>
#include <Godot/classes/texture2d.hpp>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm> 

#define PI_GODOT 3.14159265358979323846

using namespace godot;
using namespace jenova::sdk;

JENOVA_CLASS_NAME("TextWall")
JENOVA_SCRIPT_BEGIN

// === ФИГМА: КОНТЕНТ ===
JENOVA_PROPERTY(String, block_text, "Typography")
JENOVA_PROPERTY(bool,   uppercase, false)

// === 🔥 ЭФФЕКТЫ И ПЕЧАТЬ (НОВОЕ!) 🔥 ===
JENOVA_PROPERTY(double, typing_speed, 0.0)      // Скорость печати (симв/сек). 0.0 = Мгновенно
JENOVA_PROPERTY(int,    text_effect, 0)         // 0-Нет, 1-Волна(Прыжки), 2-Торнадо, 3-Тряска
JENOVA_PROPERTY(double, effect_intensity, 50.0) // Сила прыжков букв

// === ФИГМА: ТИПОГРАФИКА ===
JENOVA_PROPERTY(String, font_path, "")
JENOVA_PROPERTY(int,    font_size, 64)
JENOVA_PROPERTY(Color,  font_color, Color(1, 1, 1, 1))
JENOVA_PROPERTY(int,    align_h, 1) // 0-Лево, 1-Центр, 2-Право, 3-Ширина
JENOVA_PROPERTY(bool,   autowrap, false)

// === ФИГМА: STROKE & SHADOW ===
JENOVA_PROPERTY(int,    outline_size, 0)
JENOVA_PROPERTY(Color,  outline_color, Color(0, 0, 0, 1))
JENOVA_PROPERTY(bool,   enable_shadow, false)
JENOVA_PROPERTY(Color,  shadow_color, Color(0, 0, 0, 0.5))
JENOVA_PROPERTY(int,    shadow_x, 2)
JENOVA_PROPERTY(int,    shadow_y, 2)
JENOVA_PROPERTY(int,    shadow_blur, 0)

// === РАЗРЕШЕНИЕ И 3D ГАБАРИТЫ ===
JENOVA_PROPERTY(int,    frame_width, 512)
JENOVA_PROPERTY(int,    frame_height, 256)
JENOVA_PROPERTY(double, text_scale, 2.0)
JENOVA_PROPERTY(double, chunk_size, 32.0) 
JENOVA_PROPERTY(bool,   is_billboard, true)

// === МАЙНКРАФТ ===
JENOVA_PROPERTY(double, render_distance, 0.0) 
JENOVA_PROPERTY(double, fade_margin, 0.0)     

void OnProcess(Caller* instance, double delta) {
	StaticBody3D* self = GetSelf<StaticBody3D>(instance);
	if (!self) return;

	bool found_box = false;
	Vector3 box_size;
	Transform3D coll_xform;

	for (int c = 0; c < self->get_child_count(); c++) {
		Node* child = self->get_child(c);
		if (child && child->is_class("CollisionShape3D")) {
			Variant shape_var = child->get("shape");
			if (shape_var.get_type() != Variant::NIL && shape_var.has_method("get_size")) {
				box_size = shape_var.call("get_size");
				coll_xform = child->get("transform"); 
				found_box = true;
				break;
			}
		}
	}

	if (!found_box) return;

	String p_block_text = self->get("block_text");
	bool p_uppercase = self->get("uppercase");
	
	double p_typing_speed = self->get("typing_speed");
	int p_text_effect = self->get("text_effect");
	double p_effect_intensity = self->get("effect_intensity");

	String p_font_path = self->get("font_path");
	int p_font_size = self->get("font_size");
	Color p_font_color = self->get("font_color");
	int p_align_h = self->get("align_h");
	bool p_autowrap = self->get("autowrap");

	int p_outline_size = self->get("outline_size");
	Color p_outline_color = self->get("outline_color");
	bool p_enable_shadow = self->get("enable_shadow");
	Color p_shadow_color = self->get("shadow_color");
	int p_shadow_x = self->get("shadow_x");
	int p_shadow_y = self->get("shadow_y");
	int p_shadow_blur = self->get("shadow_blur");

	int p_frame_width = self->get("frame_width");
	int p_frame_height = self->get("frame_height");
	double p_text_scale = self->get("text_scale");
	double p_chunk_size = self->get("chunk_size");
	bool p_is_billboard = self->get("is_billboard");
	
	double p_render_distance = self->get("render_distance");
	double p_fade_margin = self->get("fade_margin");

	if (p_frame_width <= 0) p_frame_width = 256;
	if (p_frame_height <= 0) p_frame_height = 256;
	if (p_text_scale <= 0.01) p_text_scale = 1.0;
	if (p_chunk_size < 1.0) p_chunk_size = 32.0;

	String current_hash = p_block_text + "_" + String::num_int64(p_uppercase) + "_" + p_font_path + "_" +
						  String::num_int64(p_font_size) + "_" + p_font_color.to_html() + "_" +
						  String::num_int64(p_align_h) + "_" + String::num_int64(p_autowrap) + "_" +
						  String::num_int64(p_outline_size) + "_" + p_outline_color.to_html() + "_" +
						  String::num_int64(p_enable_shadow) + "_" + p_shadow_color.to_html() + "_" +
						  String::num_int64(p_shadow_x) + "_" + String::num_int64(p_shadow_y) + "_" +
						  String::num_int64(p_shadow_blur) + "_" + String::num_int64(p_frame_width) + "_" +
						  String::num_int64(p_frame_height) + "_" + String::num(p_text_scale) + "_" +
						  String::num(p_chunk_size) + "_" + String::num_int64(p_is_billboard) + "_" +
						  String::num(p_render_distance) + "_" + String::num(p_fade_margin) + "_" +
						  String::num(p_typing_speed) + "_" + String::num_int64(p_text_effect) + "_" + 
						  String::num(p_effect_intensity);

	String last_hash = self->get_meta("last_hash", "");
	Vector3 last_box_size = self->get_meta("last_box_size", Vector3());
	bool needs_rebuild = false;

	if (current_hash != last_hash || box_size != last_box_size) {
		needs_rebuild = true;
		self->set_meta("last_hash", current_hash);
		self->set_meta("last_box_size", box_size);
		self->set_meta("update_ticks", 5); 
	}

	SubViewport* vp = Object::cast_to<SubViewport>(self->get_node_or_null("InternalViewport"));

	// ==========================================
	// ЛОГИКА ПЕЧАТАНИЯ И ЭФФЕКТОВ (Каждый кадр)
	// ==========================================
	bool is_typing = self->get_meta("is_typing", false);
	bool has_effect = (p_text_effect > 0);

	if (!needs_rebuild) {
		if (is_typing || has_effect) {
			if (vp) vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
			
			if (is_typing && vp) {
				RichTextLabel* rtl = Object::cast_to<RichTextLabel>(vp->get_node_or_null("InternalLabel"));
				if (rtl) {
					double current_chars = self->get_meta("current_chars", 0.0);
					current_chars += p_typing_speed * delta;
					rtl->set_visible_characters((int)current_chars);
					self->set_meta("current_chars", current_chars);

					if (current_chars >= p_block_text.length()) {
						self->set_meta("is_typing", false);
						rtl->set_visible_characters(-1); // Показать всё
					}
				}
			}
		} else {
			int update_ticks = self->get_meta("update_ticks", 0);
			if (update_ticks > 0) {
				if (vp) vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
				self->set_meta("update_ticks", update_ticks - 1);
			} else {
				if (vp) vp->set_update_mode(SubViewport::UPDATE_DISABLED); 
			}
		}
		return; 
	}

	// ==========================================
	// ГЕНЕРАЦИЯ СТЕНЫ (Только при изменении настроек)
	// ==========================================
	if (!vp) {
		vp = memnew(SubViewport);
		vp->set_name("InternalViewport");
		vp->set_transparent_background(true);
		self->add_child(vp);
	}
	vp->set_size(Vector2i(p_frame_width, p_frame_height));
	vp->set_update_mode(SubViewport::UPDATE_ALWAYS); 

	RichTextLabel* rtl = Object::cast_to<RichTextLabel>(vp->get_node_or_null("InternalLabel"));
	if (!rtl) {
		rtl = memnew(RichTextLabel);
		rtl->set_name("InternalLabel");
		vp->add_child(rtl);
	}
	
	rtl->set_use_bbcode(true);
	rtl->set_scroll_active(false); // Отключаем полосу прокрутки
	rtl->set_autowrap_mode(p_autowrap ? TextServer::AUTOWRAP_WORD_SMART : TextServer::AUTOWRAP_OFF);
	rtl->set_anchors_preset(Control::PRESET_FULL_RECT);

	// Сборка BBCode (Спецэффекты текста)
	String final_text = p_block_text;
	if (p_uppercase) final_text = final_text.to_upper();

	// Добавляем эффекты
	if (p_text_effect == 1) final_text = "[wave amp=" + String::num(p_effect_intensity) + " freq=5.0 connected=1]" + final_text + "[/wave]";
	else if (p_text_effect == 2) final_text = "[tornado radius=" + String::num(p_effect_intensity) + " freq=2.0 connected=1]" + final_text + "[/tornado]";
	else if (p_text_effect == 3) final_text = "[shake rate=20.0 level=" + String::num(p_effect_intensity) + " connected=1]" + final_text + "[/shake]";

	// Добавляем выравнивание
	if (p_align_h == 1) final_text = "[center]" + final_text + "[/center]";
	else if (p_align_h == 2) final_text = "[right]" + final_text + "[/right]";
	else if (p_align_h == 3) final_text = "[fill]" + final_text + "[/fill]";

	rtl->set_text(final_text);

	// Перезапуск анимации печати
	if (p_typing_speed > 0.1) {
		rtl->set_visible_characters(0);
		self->set_meta("current_chars", 0.0);
		self->set_meta("is_typing", true);
	} else {
		rtl->set_visible_characters(-1);
		self->set_meta("is_typing", false);
	}

	// Применение стилей
	rtl->add_theme_font_size_override("normal_font_size", p_font_size);
	rtl->add_theme_color_override("default_color", p_font_color);
	rtl->add_theme_color_override("font_outline_color", p_outline_color);
	rtl->add_theme_constant_override("outline_size", p_outline_size);

	if (p_enable_shadow) {
		rtl->add_theme_color_override("font_shadow_color", p_shadow_color);
		rtl->add_theme_constant_override("shadow_offset_x", p_shadow_x);
		rtl->add_theme_constant_override("shadow_offset_y", p_shadow_y);
		rtl->add_theme_constant_override("shadow_outline_size", p_shadow_blur);
	} else {
		rtl->remove_theme_color_override("font_shadow_color");
	}

	if (!p_font_path.is_empty()) {
		Ref<Font> custom_font = ResourceLoader::get_singleton()->load(p_font_path);
		if (custom_font.is_valid()) rtl->add_theme_font_override("normal_font", custom_font);
	} else {
		rtl->remove_theme_font_override("normal_font");
	}

	// ==========================================
	// СБОРКА ЧАНКОВ
	// ==========================================
	Node3D* chunks_root = Object::cast_to<Node3D>(self->get_node_or_null("ChunksRoot"));
	if (!chunks_root) {
		chunks_root = memnew(Node3D);
		chunks_root->set_name("ChunksRoot");
		self->add_child(chunks_root);
	} else {
		for (int i = chunks_root->get_child_count() - 1; i >= 0; i--) {
			Node* c = chunks_root->get_child(i);
			chunks_root->remove_child(c);
			c->queue_free();
		}
	}

	Ref<StandardMaterial3D> mat = self->get_meta("internal_mat");
	if (mat.is_null()) {
		mat.instantiate();
		mat->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, vp->get_texture());
		mat->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
		mat->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		mat->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		self->set_meta("internal_mat", mat);
	}
	mat->set_billboard_mode(p_is_billboard ? BaseMaterial3D::BILLBOARD_ENABLED : BaseMaterial3D::BILLBOARD_DISABLED);

	double quad_h = p_text_scale;
	double quad_w = p_text_scale * ((double)p_frame_width / (double)p_frame_height);
	double c_size = p_chunk_size;

	std::unordered_map<std::string, std::vector<Transform3D>> chunk_map;

	auto add_face = [&](Vector3 offset, Vector3 euler, double w, double h) {
		int count_x = (int)std::floor(w / quad_w);
		int count_y = (int)std::floor(h / quad_h);
		if (count_x <= 0 || count_y <= 0) return;

		double start_x = -(count_x - 1) * quad_w * 0.5;
		double start_y = -(count_y - 1) * quad_h * 0.5;
		Basis face_rot = Basis::from_euler(euler);

		for (int x = 0; x < count_x; x++) {
			for (int y = 0; y < count_y; y++) {
				Vector3 local_pos = Vector3(start_x + x * quad_w, start_y + y * quad_h, 0); 
				Transform3D local_transform;
				if (p_is_billboard) local_transform.basis = Basis();
				else local_transform.basis = face_rot;
				local_transform.origin = offset + face_rot.xform(local_pos);
				
				Transform3D final_xform = coll_xform * local_transform;
				
				int cx = (int)std::floor(final_xform.origin.x / c_size);
				int cy = (int)std::floor(final_xform.origin.y / c_size);
				int cz = (int)std::floor(final_xform.origin.z / c_size);
				
				char buf[128];
				std::snprintf(buf, sizeof(buf), "%d_%d_%d", cx, cy, cz);
				chunk_map[std::string(buf)].push_back(final_xform);
			}
		}
	};

	if (box_size.x >= quad_w && box_size.y >= quad_h) {
		if (box_size.z < quad_h) add_face(Vector3(0, 0, 0), Vector3(0, 0, 0), box_size.x, box_size.y);
		else {
			add_face(Vector3(0, 0, box_size.z / 2.0), Vector3(0, 0, 0), box_size.x, box_size.y);
			add_face(Vector3(0, 0, -box_size.z / 2.0), Vector3(0, PI_GODOT, 0), box_size.x, box_size.y);
		}
	}
	if (box_size.z >= quad_w && box_size.y >= quad_h) {
		if (box_size.x < quad_h) add_face(Vector3(0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), box_size.z, box_size.y);
		else {
			add_face(Vector3(box_size.x / 2.0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), box_size.z, box_size.y);
			add_face(Vector3(-box_size.x / 2.0, 0, 0), Vector3(0, PI_GODOT / 2.0, 0), box_size.z, box_size.y);
		}
	}
	if (box_size.x >= quad_w && box_size.z >= quad_h) {
		if (box_size.y < quad_h) add_face(Vector3(0, 0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), box_size.x, box_size.z);
		else {
			add_face(Vector3(0, box_size.y / 2.0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), box_size.x, box_size.z);
			add_face(Vector3(0, -box_size.y / 2.0, 0), Vector3(PI_GODOT / 2.0, 0, 0), box_size.x, box_size.z);
		}
	}

	for(auto& kv : chunk_map) {
		Vector3 centroid = Vector3(0, 0, 0);
		for(const auto& xf : kv.second) centroid += xf.origin;
		centroid /= (double)kv.second.size();

		Ref<QuadMesh> quad; 
		quad.instantiate();
		quad->set_size(Vector2(quad_w, quad_h));
		quad->set_material(mat);

		Ref<MultiMesh> mm; 
		mm.instantiate();
		mm->set_transform_format(MultiMesh::TRANSFORM_3D);
		mm->set_mesh(quad);
		mm->set_instance_count((int)kv.second.size());
		
		Vector3 min_pos = Vector3(0, 0, 0);
		Vector3 max_pos = Vector3(0, 0, 0);

		for(int i = 0; i < (int)kv.second.size(); i++) {
			Transform3D local_inst = kv.second[i];
			local_inst.origin -= centroid; 
			mm->set_instance_transform(i, local_inst);

			if (i == 0) {
				min_pos = local_inst.origin;
				max_pos = local_inst.origin;
			} else {
				Vector3 p = local_inst.origin;
				min_pos.x = std::min(min_pos.x, p.x);
				min_pos.y = std::min(min_pos.y, p.y);
				min_pos.z = std::min(min_pos.z, p.z);
				max_pos.x = std::max(max_pos.x, p.x);
				max_pos.y = std::max(max_pos.y, p.y);
				max_pos.z = std::max(max_pos.z, p.z);
			}
		}

		Vector3 size_vec = max_pos - min_pos;
		AABB chunk_aabb(min_pos, size_vec);
		double padding = std::max(quad_w, quad_h) * 1.5;
		chunk_aabb.position -= Vector3(padding, padding, padding);
		chunk_aabb.size += Vector3(padding * 2.0, padding * 2.0, padding * 2.0);
		mm->set_custom_aabb(chunk_aabb); 

		MultiMeshInstance3D* mmi = memnew(MultiMeshInstance3D);
		mmi->set_multimesh(mm);
		mmi->set_position(centroid); 
		mmi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		
		if (p_render_distance > 0.1) {
			mmi->set_visibility_range_end(p_render_distance);
			mmi->set_visibility_range_end_margin(p_fade_margin); 
		}

		chunks_root->add_child(mmi);
	}
}

JENOVA_SCRIPT_END
