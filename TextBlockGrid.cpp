#include <Godot/godot.hpp>
#include <Godot/classes/static_body3d.hpp>
#include <Godot/classes/multi_mesh_instance3d.hpp>
#include <Godot/classes/multi_mesh.hpp>
#include <Godot/classes/quad_mesh.hpp>
#include <Godot/classes/sub_viewport.hpp>
#include <Godot/classes/rich_text_label.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/box_shape3d.hpp>
#include <Godot/classes/resource_loader.hpp>
#include <Godot/classes/font.hpp>
#include <Godot/classes/viewport_texture.hpp>
#include <Godot/classes/texture2d.hpp>
#include <Godot/classes/camera3d.hpp>
#include <Godot/classes/viewport.hpp>
#include <Godot/classes/shader.hpp>
#include <Godot/classes/shader_material.hpp>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

#define PI_GODOT 3.14159265358979323846

// === DEBUG ===
// 0 = тихо (только ошибки и события), 1 = подробно (rebuild + переходы), 2 = всё (включая per-frame distances)
#define TW_DEBUG 1

#define TW_LOGE(fmt, ...) Output("[TextWall ERROR %s] " fmt, name_cstr, ##__VA_ARGS__)
#define TW_LOGI(fmt, ...) do { if (TW_DEBUG >= 1) Output("[TextWall %s] " fmt, name_cstr, ##__VA_ARGS__); } while (0)
#define TW_LOGV(fmt, ...) do { if (TW_DEBUG >= 2) Output("[TextWall %s] " fmt, name_cstr, ##__VA_ARGS__); } while (0)

using namespace godot;
using namespace jenova::sdk;

JENOVA_CLASS_NAME("TextWall")
JENOVA_SCRIPT_BEGIN

JENOVA_PROPERTY(String, block_text, "Typography")
JENOVA_PROPERTY(bool,   uppercase, false)

JENOVA_PROPERTY(String, shader_path,   "res://text_reveal.gdshader")
JENOVA_PROPERTY(double, wipe_softness, 0.02)

JENOVA_PROPERTY(int,    text_effect,      0)   // 0 = всегда виден, 1 = per-chunk wipe по дистанции
JENOVA_PROPERTY(double, typing_speed,     1.5)
JENOVA_PROPERTY(double, trigger_distance, 8.0)

JENOVA_PROPERTY(String, font_path, "")
JENOVA_PROPERTY(int,    font_size, 64)
JENOVA_PROPERTY(Color,  font_color, Color(1, 1, 1, 1))
JENOVA_PROPERTY(int,    line_spacing, 0)
JENOVA_PROPERTY(int,    align_h, 1)
JENOVA_PROPERTY(bool,   autowrap, false)

JENOVA_PROPERTY(int,    outline_size, 0)
JENOVA_PROPERTY(Color,  outline_color, Color(0, 0, 0, 1))
JENOVA_PROPERTY(bool,   enable_shadow, false)
JENOVA_PROPERTY(Color,  shadow_color, Color(0, 0, 0, 0.5))
JENOVA_PROPERTY(int,    shadow_x, 2)
JENOVA_PROPERTY(int,    shadow_y, 2)
JENOVA_PROPERTY(int,    shadow_blur, 0)

JENOVA_PROPERTY(int,    frame_width, 512)
JENOVA_PROPERTY(int,    frame_height, 256)
JENOVA_PROPERTY(double, text_scale, 2.0)
JENOVA_PROPERTY(bool,   is_billboard, true)

JENOVA_PROPERTY(double, chunk_size, 16.0)
JENOVA_PROPERTY(double, render_distance, 0.0)

static String wrap_align(const String& s, int align) {
	if (align == 1) return "[center]" + s + "[/center]";
	if (align == 2) return "[right]"  + s + "[/right]";
	if (align == 3) return "[fill]"   + s + "[/fill]";
	return s;
}

void OnProcess(Caller* instance, double delta) {
	StaticBody3D* self = GetSelf<StaticBody3D>(instance);
	if (!self) return;

	String name_str = self->get_name();
	const char* name_cstr = name_str.utf8().get_data();

	// 1) CollisionShape3D
	bool found_box = false;
	Vector3 box_size;
	Transform3D coll_xform_local;
	for (int c = 0; c < self->get_child_count(); c++) {
		Node* child = self->get_child(c);
		if (child && child->is_class("CollisionShape3D")) {
			Variant shape_var = child->get("shape");
			if (shape_var.get_type() != Variant::NIL && shape_var.has_method("get_size")) {
				box_size = shape_var.call("get_size");
				coll_xform_local = child->get("transform");
				found_box = true;
				break;
			}
		}
	}
	if (!found_box) {
		// Логнём раз в ~3 сек, чтоб не спамить
		int t = self->get_meta("nobox_warn_tick", 0);
		if (t <= 0) { TW_LOGE("CollisionShape3D с BoxShape3D не найден — нечего рендерить"); self->set_meta("nobox_warn_tick", 180); }
		else        { self->set_meta("nobox_warn_tick", t - 1); }
		return;
	}

	// 2) Свойства
	String p_block_text       = self->get("block_text");
	bool   p_uppercase        = self->get("uppercase");
	String p_shader_path      = self->get("shader_path");
	double p_wipe_softness    = self->get("wipe_softness");
	int    p_text_effect      = self->get("text_effect");
	double p_typing_speed     = self->get("typing_speed");
	double p_trigger_distance = self->get("trigger_distance");
	String p_font_path        = self->get("font_path");
	int    p_font_size        = self->get("font_size");
	Color  p_font_color       = self->get("font_color");
	int    p_line_spacing     = self->get("line_spacing");
	int    p_align_h          = self->get("align_h");
	bool   p_autowrap         = self->get("autowrap");
	int    p_outline_size     = self->get("outline_size");
	Color  p_outline_color    = self->get("outline_color");
	bool   p_enable_shadow    = self->get("enable_shadow");
	Color  p_shadow_color     = self->get("shadow_color");
	int    p_shadow_x         = self->get("shadow_x");
	int    p_shadow_y         = self->get("shadow_y");
	int    p_shadow_blur      = self->get("shadow_blur");
	int    p_frame_width      = self->get("frame_width");
	int    p_frame_height     = self->get("frame_height");
	double p_text_scale       = self->get("text_scale");
	bool   p_is_billboard     = self->get("is_billboard");
	double p_chunk_size       = self->get("chunk_size");
	double p_render_distance  = self->get("render_distance");

	if (p_frame_width <= 0)   p_frame_width = 256;
	if (p_frame_height <= 0)  p_frame_height = 256;
	if (p_text_scale <= 0.01) p_text_scale = 1.0;
	if (p_chunk_size < 1.0)   p_chunk_size = 16.0;
	if (p_shader_path.is_empty()) p_shader_path = "res://text_reveal.gdshader";

	Viewport* vp_root = self->get_viewport();
	Camera3D* cam = vp_root ? vp_root->get_camera_3d() : nullptr;
	if (!cam) {
		int t = self->get_meta("nocam_warn_tick", 0);
		if (t <= 0) { TW_LOGI("WARN: активная Camera3D не найдена — печать триггериться не будет"); self->set_meta("nocam_warn_tick", 300); }
		else        { self->set_meta("nocam_warn_tick", t - 1); }
	}
	Vector3 player_pos = cam ? cam->get_global_position() : Vector3();

	// 3) Hash для определения rebuild
	String current_hash = p_block_text + "_" + String::num_int64(p_uppercase) + "_" + p_font_path + "_" +
						  String::num_int64(p_font_size) + "_" + p_font_color.to_html() + "_" +
						  String::num_int64(p_line_spacing) + "_" + String::num_int64(p_align_h) + "_" +
						  String::num_int64(p_autowrap) + "_" + String::num_int64(p_outline_size) + "_" +
						  p_outline_color.to_html() + "_" + String::num_int64(p_enable_shadow) + "_" +
						  p_shadow_color.to_html() + "_" + String::num_int64(p_shadow_x) + "_" +
						  String::num_int64(p_shadow_y) + "_" + String::num_int64(p_shadow_blur) + "_" +
						  String::num_int64(p_frame_width) + "_" + String::num_int64(p_frame_height) + "_" +
						  String::num(p_text_scale) + "_" + String::num(p_chunk_size) + "_" +
						  String::num_int64(p_is_billboard) + "_" + String::num(p_render_distance) + "_" +
						  p_shader_path;

	String last_hash = self->get_meta("last_hash", "");
	Vector3 last_box_size = self->get_meta("last_box_size", Vector3());
	bool needs_rebuild = (current_hash != last_hash || box_size != last_box_size);

	SubViewport* vp = Object::cast_to<SubViewport>(self->get_node_or_null("InternalViewport"));
	Node3D* chunks_root = Object::cast_to<Node3D>(self->get_node_or_null("ChunksRoot"));

	// ==========================================
	// PER-FRAME: per-instance прогресс
	// ==========================================
	if (!needs_rebuild && chunks_root) {
		int update_ticks = self->get_meta("update_ticks", 0);
		if (vp) {
			if (update_ticks > 0) {
				vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
				self->set_meta("update_ticks", update_ticks - 1);
				if (update_ticks == 1) TW_LOGI("viewport rendered, переключаю в UPDATE_DISABLED");
			} else {
				vp->set_update_mode(SubViewport::UPDATE_DISABLED);
			}
		}

		double progress_speed = (p_typing_speed > 0.001) ? p_typing_speed : 1.5;
		int chunk_count = chunks_root->get_child_count();
		int near_count = 0;

		for (int i = 0; i < chunk_count; i++) {
			MultiMeshInstance3D* mmi = Object::cast_to<MultiMeshInstance3D>(chunks_root->get_child(i));
			if (!mmi) continue;

			double target = 1.0;
			bool chunk_near = false;
			double dist = 0.0;
			if (p_text_effect == 1 && cam) {
				dist = mmi->get_global_position().distance_to(player_pos);
				chunk_near = (dist <= p_trigger_distance);
				if (chunk_near) near_count++;
				target = chunk_near ? 1.0 : 0.0;

				bool was_near = mmi->get_meta("was_near", false);
				if (chunk_near != was_near) {
					mmi->set_meta("was_near", chunk_near);
					TW_LOGI("chunk[%d] %s, dist=%.2f trigger=%.2f", i, chunk_near ? "ENTER" : "EXIT", dist, p_trigger_distance);
				}
			}

			double progress = mmi->get_meta("progress", (p_text_effect == 0) ? 1.0 : 0.0);
			if (p_text_effect == 0) {
				progress = 1.0;
			} else {
				double step_amount = progress_speed * delta;
				if      (progress < target) progress = std::min(target, progress + step_amount);
				else if (progress > target) progress = std::max(target, progress - step_amount);
				mmi->set_meta("progress", progress);
			}

			// per-instance uniform — Godot НЕ дублирует материал
			mmi->call("set_instance_shader_parameter", "chunk_progress", progress);
		}

		// Per-frame uniforms на общий материал (живые настройки)
		if (self->has_meta("shared_mat")) {
			Ref<ShaderMaterial> smat = self->get_meta("shared_mat");
			if (smat.is_valid()) {
				smat->set_shader_parameter("billboard_mode", p_is_billboard);
				smat->set_shader_parameter("wipe_softness",  p_wipe_softness);
			}
		}

		TW_LOGV("per-frame: chunks=%d near=%d effect=%d", chunk_count, near_count, p_text_effect);
		return;
	}

	// ==========================================
	// REBUILD
	// ==========================================
	TW_LOGI("=== REBUILD === box=(%.2f,%.2f,%.2f) effect=%d trigger=%.2f text='%s'",
		box_size.x, box_size.y, box_size.z, p_text_effect, p_trigger_distance,
		p_block_text.utf8().get_data());

	self->set_meta("last_hash", current_hash);
	self->set_meta("last_box_size", box_size);
	self->set_meta("update_ticks", 5);

	// --- общий SubViewport + RTL ---
	if (!vp) {
		vp = memnew(SubViewport);
		vp->set_name("InternalViewport");
		vp->set_transparent_background(true);
		self->add_child(vp);
		TW_LOGI("создал InternalViewport");
	}
	vp->set_size(Vector2i(p_frame_width, p_frame_height));
	vp->set_update_mode(SubViewport::UPDATE_ALWAYS);

	RichTextLabel* rtl = Object::cast_to<RichTextLabel>(vp->get_node_or_null("InternalLabel"));
	if (!rtl) {
		rtl = memnew(RichTextLabel);
		rtl->set_name("InternalLabel");
		vp->add_child(rtl);
		TW_LOGI("создал InternalLabel");
	}
	rtl->set_size(Vector2(p_frame_width, p_frame_height));
	rtl->set_clip_contents(false);
	rtl->set_use_bbcode(true);
	rtl->set_scroll_active(false);
	rtl->set_autowrap_mode(p_autowrap ? TextServer::AUTOWRAP_WORD_SMART : TextServer::AUTOWRAP_OFF);
	rtl->set_anchors_preset(Control::PRESET_FULL_RECT);
	rtl->add_theme_font_size_override("normal_font_size", p_font_size);
	rtl->add_theme_color_override("default_color", p_font_color);
	rtl->add_theme_constant_override("line_spacing", p_line_spacing);
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
		else                        TW_LOGE("не удалось загрузить шрифт '%s'", p_font_path.utf8().get_data());
	} else {
		rtl->remove_theme_font_override("normal_font");
	}
	String final_text = p_block_text;
	if (p_uppercase) final_text = final_text.to_upper();
	rtl->set_text(wrap_align(final_text, p_align_h));
	rtl->set_visible_characters(-1);
	TW_LOGI("RTL viewport=%dx%d text_len=%d font_size=%d", p_frame_width, p_frame_height, p_block_text.length(), p_font_size);

	// --- Шейдер ---
	Ref<Shader> shader_res = ResourceLoader::get_singleton()->load(p_shader_path);
	if (shader_res.is_null()) {
		TW_LOGE("ШЕЙДЕР НЕ ЗАГРУЖЕН: '%s' — текста не будет видно", p_shader_path.utf8().get_data());
	} else {
		TW_LOGI("шейдер загружен: %s", p_shader_path.utf8().get_data());
	}

	// --- Один общий ShaderMaterial ---
	Ref<ShaderMaterial> smat;
	if (self->has_meta("shared_mat")) smat = self->get_meta("shared_mat");
	if (smat.is_null()) {
		smat.instantiate();
		self->set_meta("shared_mat", smat);
		TW_LOGI("создал shared ShaderMaterial");
	}
	if (shader_res.is_valid()) smat->set_shader(shader_res);
	smat->set_shader_parameter("albedo_tex",     vp->get_texture());
	smat->set_shader_parameter("billboard_mode", p_is_billboard);
	smat->set_shader_parameter("wipe_softness",  p_wipe_softness);

	Ref<Texture2D> vtex = vp->get_texture();
	if (vtex.is_null()) TW_LOGE("vp->get_texture() вернул null — материал ничего не нарисует");

	// --- ChunksRoot ---
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

	double quad_h = p_text_scale;
	double quad_w = p_text_scale * ((double)p_frame_width / (double)p_frame_height);
	double c_size = p_chunk_size;

	std::unordered_map<std::string, std::vector<Transform3D>> chunk_map;
	int total_quads = 0;

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
				Transform3D final_xform = coll_xform_local * local_transform;
				int cx = (int)std::floor(final_xform.origin.x / c_size);
				int cy = (int)std::floor(final_xform.origin.y / c_size);
				int cz = (int)std::floor(final_xform.origin.z / c_size);
				char buf[128];
				std::snprintf(buf, sizeof(buf), "%d_%d_%d", cx, cy, cz);
				chunk_map[std::string(buf)].push_back(final_xform);
				total_quads++;
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

	if (chunk_map.empty()) {
		TW_LOGE("ни один чанк не сгенерирован — quad_size=(%.2f,%.2f) > box_size", quad_w, quad_h);
	}

	int chunk_idx = 0;
	for (auto& kv : chunk_map) {
		Vector3 centroid;
		for (const auto& xf : kv.second) centroid += xf.origin;
		centroid /= (double)kv.second.size();

		Ref<QuadMesh> quad;
		quad.instantiate();
		quad->set_size(Vector2(quad_w, quad_h));
		quad->set_material(smat); // ОДИН общий материал на все чанки

		Ref<MultiMesh> mm;
		mm.instantiate();
		mm->set_transform_format(MultiMesh::TRANSFORM_3D);
		mm->set_mesh(quad);
		mm->set_instance_count((int)kv.second.size());

		for (int i = 0; i < (int)kv.second.size(); i++) {
			Transform3D local_inst = kv.second[i];
			local_inst.origin -= centroid;
			mm->set_instance_transform(i, local_inst);
		}
		// custom AABB не ставим — Godot сам посчитает с учётом всех инстансов и mesh'а.
		// При билбординге через шейдер кастомный AABB культит чанки под некоторыми ракурсами.

		MultiMeshInstance3D* mmi = memnew(MultiMeshInstance3D);
		mmi->set_multimesh(mm);
		mmi->set_position(centroid);
		mmi->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		if (p_render_distance > 0.1) {
			mmi->set_visibility_range_end(p_render_distance);
			mmi->set_visibility_range_end_margin(0.0);
		}

		double initial_progress = (p_text_effect == 0) ? 1.0 : 0.0;
		mmi->set_meta("progress", initial_progress);
		mmi->set_meta("was_near", false);

		chunks_root->add_child(mmi);
		// per-instance uniform нужно ставить ПОСЛЕ add_child (когда нода уже в дереве)
		mmi->call("set_instance_shader_parameter", "chunk_progress", initial_progress);

		chunk_idx++;
	}

	TW_LOGI("=== REBUILD DONE === chunks=%d total_quads=%d quad=(%.2fx%.2f) initial_progress=%.1f",
		(int)chunk_map.size(), total_quads, quad_w, quad_h, (p_text_effect == 0) ? 1.0 : 0.0);
}

JENOVA_SCRIPT_END
