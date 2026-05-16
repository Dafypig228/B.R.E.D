#include <Godot/godot.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/multi_mesh_instance3d.hpp>
#include <Godot/classes/multi_mesh.hpp>
#include <Godot/classes/quad_mesh.hpp>
#include <Godot/classes/sub_viewport.hpp>
#include <Godot/classes/rich_text_label.hpp>
#include <Godot/classes/collision_shape3d.hpp>
#include <Godot/classes/resource_loader.hpp>
#include <Godot/classes/font.hpp>
#include <Godot/classes/viewport_texture.hpp>
#include <Godot/classes/texture2d.hpp>
#include <Godot/classes/camera3d.hpp>
#include <Godot/classes/viewport.hpp>
#include <Godot/classes/shader.hpp>
#include <Godot/classes/shader_material.hpp>
#include <Godot/classes/scene_tree.hpp>

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

JENOVA_PROPERTY(int,    text_effect,      0)   // 0 = всегда виден, 1 = печать по триггеру, 2 = заполнение по дистанции
JENOVA_PROPERTY(double, typing_speed,     1.5)
JENOVA_PROPERTY(double, trigger_distance, 8.0) // для effect=1: радиус старта; для effect=2: дистанция полного заполнения
JENOVA_PROPERTY(double, min_progress,     0.0) // для effect=2: минимум прогресса даже на максимальной дистанции (0..1)

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

// === РАЗМЕР И ПЛОТНОСТЬ ТАЙЛОВ — три независимых регулятора ===
// tile_size:      визуальная высота тайла в метрах. Контролирует ТОЛЬКО размер, не количество.
// tile_spacing:   расстояние между центрами тайлов в метрах. МЕНЬШЕ → БОЛЬШЕ ТАЙЛОВ. Независимо от размера.
//                 tile_size > tile_spacing → тайлы перекрываются (плотный «ковёр» текста).
//                 tile_size < tile_spacing → между тайлами видны промежутки.
// tile_aspect:    соотношение width/height тайла. 2.0 = широкий, 1.0 = квадрат.
// render_quality: высота текстуры в пикселях (256-1024). Только резкость, ни на что больше не влияет.
JENOVA_PROPERTY(double, tile_size,      1.0)
JENOVA_PROPERTY(double, tile_spacing,   1.0)
JENOVA_PROPERTY(double, tile_aspect,    2.0)
JENOVA_PROPERTY(int,    render_quality, 256)
JENOVA_PROPERTY(bool,   is_billboard,   true)

JENOVA_PROPERTY(double, chunk_size, 16.0)
JENOVA_PROPERTY(double, render_distance, 0.0)

// === РЕЖИМ РАСПРЕДЕЛЕНИЯ ТЕКСТА ===
// 0 = SURFACE — тайлы строго на поверхности шейпа. Подходит для стен, полов, столбов.
// 1 = VOLUME  — тайлы заполняют ОБЪЁМ шейпа. Подходит для листвы, облаков, "плотных" объектов.
JENOVA_PROPERTY(int,    distribution_mode, 0)

// === ВИЗУАЛЬНЫЕ ЭФФЕКТЫ (живые, без rebuild, стакаются) ===
JENOVA_PROPERTY(Color,  tint,              Color(1, 1, 1, 1))
JENOVA_PROPERTY(double, glow_strength,     0.0)               // 0 = выкл; >0 = HDR boost
JENOVA_PROPERTY(Color,  glow_color,        Color(1, 1, 1, 1))
JENOVA_PROPERTY(double, pulse_speed,       0.0)               // Hz; 0 = выкл
JENOVA_PROPERTY(double, pulse_amount,      0.5)               // 0..1
JENOVA_PROPERTY(double, flicker_speed,     0.0)               // Hz; 0 = выкл
JENOVA_PROPERTY(double, flicker_amount,    0.5)               // 0..1
JENOVA_PROPERTY(double, wave_amp,          0.0)               // амплитуда вертекс-волны
JENOVA_PROPERTY(double, wave_speed,        1.0)
JENOVA_PROPERTY(double, wave_freq,         0.3)
JENOVA_PROPERTY(double, rainbow_speed,     0.0)               // Hz; 0 = выкл
JENOVA_PROPERTY(double, scanline_strength, 0.0)               // 0..1
JENOVA_PROPERTY(double, scanline_count,    100.0)
JENOVA_PROPERTY(double, hover_amp,         0.0)               // амплитуда float-up/down
JENOVA_PROPERTY(double, hover_speed,       0.5)

// === WIND (непрерывный дрейф) ===
// Тайлы НЕПРЕРЫВНО летят по wind_dir на wind_distance метров, потом fade-out и появляются у начала.
JENOVA_PROPERTY(Vector3, wind_dir,         Vector3(1, 0, 0))  // куда дует
JENOVA_PROPERTY(double,  wind_strength,    0.0)               // 0 = выкл; >0 = вкл (любое значение)
JENOVA_PROPERTY(double,  wind_distance,    2.0)               // полная дистанция полёта тайла в метрах
JENOVA_PROPERTY(double,  wind_speed,       0.3)               // циклов в секунду
JENOVA_PROPERTY(double,  wind_gusts,       0.5)               // 0..1; модуляция скорости (рваные порывы)
JENOVA_PROPERTY(double,  wind_chaos,       1.0)               // 0..1; per-chunk desync фазы

static String wrap_align(const String& s, int align) {
	if (align == 1) return "[center]" + s + "[/center]";
	if (align == 2) return "[right]"  + s + "[/right]";
	if (align == 3) return "[fill]"   + s + "[/fill]";
	return s;
}

void OnProcess(Caller* instance, double delta) {
	Node3D* self = GetSelf<Node3D>(instance);
	if (!self) return;

	String name_str = self->get_name();
	const char* name_cstr = name_str.utf8().get_data();

	// 1) Источники формы — ВСЕ CollisionShape3D-дети с поддерживаемым шейпом.
	//    Текст применяется к каждому шейпу со своим transform.
	struct ShapeInfo {
		String cls;
		Vector3 box_size;
		double  radius;
		double  height;
		Transform3D xform;
	};
	std::vector<ShapeInfo> shapes;

	for (int c = 0; c < self->get_child_count(); c++) {
		Node* child = self->get_child(c);
		if (!child || !child->is_class("CollisionShape3D")) continue;
		Variant shape_var = child->get("shape");
		if (shape_var.get_type() != Variant::OBJECT) continue;

		Variant cls_var = shape_var.call("get_class");
		String cls = cls_var;
		ShapeInfo si{};
		si.cls = cls;
		si.xform = child->get("transform");

		if (cls == "BoxShape3D" && shape_var.has_method("get_size")) {
			si.box_size = shape_var.call("get_size");
		} else if (cls == "SphereShape3D" && shape_var.has_method("get_radius")) {
			si.radius = shape_var.call("get_radius");
		} else if ((cls == "CylinderShape3D" || cls == "CapsuleShape3D")
			&& shape_var.has_method("get_radius") && shape_var.has_method("get_height")) {
			si.radius = shape_var.call("get_radius");
			si.height = shape_var.call("get_height");
		} else {
			continue; // неподдерживаемый шейп — пропустить
		}
		shapes.push_back(si);
	}

	if (shapes.empty()) {
		int t = self->get_meta("noshape_warn_tick", 0);
		if (t <= 0) { TW_LOGE("no supported CollisionShape3D found (Box/Sphere/Cylinder/Capsule)"); self->set_meta("noshape_warn_tick", 180); }
		else        { self->set_meta("noshape_warn_tick", t - 1); }
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
	double p_min_progress     = self->get("min_progress");
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
	double p_tile_size        = self->get("tile_size");
	double p_tile_spacing     = self->get("tile_spacing");
	double p_tile_aspect      = self->get("tile_aspect");
	int    p_render_quality   = self->get("render_quality");
	bool   p_is_billboard     = self->get("is_billboard");

	if (p_tile_size <= 0.01)    p_tile_size = 1.0;
	if (p_tile_spacing <= 0.01) p_tile_spacing = p_tile_size; // fallback: spacing = size
	if (p_tile_aspect <= 0.01)  p_tile_aspect = 2.0;
	if (p_render_quality < 32)  p_render_quality = 256;
	// Производные значения: VP всегда сохраняет аспект тайла → текст не растягивается
	int p_frame_height = p_render_quality;
	int p_frame_width  = (int)std::max(32.0, (double)p_render_quality * p_tile_aspect);
	double p_chunk_size       = self->get("chunk_size");
	double p_render_distance  = self->get("render_distance");
	int    p_dist_mode        = self->get("distribution_mode");

	// Визуальные эффекты (live, не в hash)
	Color  p_tint             = self->get("tint");
	double p_glow_strength    = self->get("glow_strength");
	Color  p_glow_color       = self->get("glow_color");
	double p_pulse_speed      = self->get("pulse_speed");
	double p_pulse_amount     = self->get("pulse_amount");
	double p_flicker_speed    = self->get("flicker_speed");
	double p_flicker_amount   = self->get("flicker_amount");
	double p_wave_amp         = self->get("wave_amp");
	double p_wave_speed       = self->get("wave_speed");
	double p_wave_freq        = self->get("wave_freq");
	double p_rainbow_speed    = self->get("rainbow_speed");
	double p_scanline_strength = self->get("scanline_strength");
	double p_scanline_count   = self->get("scanline_count");
	double p_hover_amp        = self->get("hover_amp");
	double p_hover_speed      = self->get("hover_speed");
	Vector3 p_wind_dir        = self->get("wind_dir");
	double p_wind_strength    = self->get("wind_strength");
	double p_wind_distance    = self->get("wind_distance");
	double p_wind_speed       = self->get("wind_speed");
	double p_wind_gusts       = self->get("wind_gusts");
	double p_wind_chaos       = self->get("wind_chaos");

	// === Глобальный override через TextWallGlobalSettings ===
	// Любая Node со скриптом TextWallGlobalSettings (она автоматически добавляется в группу
	// "text_wall_global_settings" в OnAwake). Если значение в глобальной ноде >= 0 — оно перебивает
	// per-wall значение. Если < 0 — per-wall остаётся.
	{
		SceneTree* tree = self->get_tree();
		if (tree) {
			TypedArray<Node> gs_nodes = tree->get_nodes_in_group("text_wall_global_settings");
			if (gs_nodes.size() > 0) {
				Node* gs = Object::cast_to<Node>(gs_nodes[0]);
				if (gs) {
					double v;
					int    iv;
					v  = gs->get("render_distance");   if (v  >= 0.0) p_render_distance  = v;
					v  = gs->get("chunk_size");        if (v  >= 0.0) p_chunk_size       = v;
					v  = gs->get("trigger_distance");  if (v  >= 0.0) p_trigger_distance = v;
					v  = gs->get("tile_spacing");      if (v  >= 0.0) p_tile_spacing     = v;
					iv = gs->get("distribution_mode"); if (iv >= 0)   p_dist_mode        = iv;
					iv = gs->get("text_effect");       if (iv >= 0)   p_text_effect      = iv;
					v  = gs->get("typing_speed");      if (v  >= 0.0) p_typing_speed     = v;
					v  = gs->get("min_progress");      if (v  >= 0.0) p_min_progress     = v;
					v  = gs->get("wipe_softness");     if (v  >= 0.0) p_wipe_softness    = v;
				}
			}
		}
	}

	if (p_chunk_size < 1.0)   p_chunk_size = 16.0;
	if (p_shader_path.is_empty()) p_shader_path = "res://text_reveal.gdshader";

	Viewport* vp_root = self->get_viewport();
	Camera3D* cam = vp_root ? vp_root->get_camera_3d() : nullptr;
	if (!cam) {
		int t = self->get_meta("nocam_warn_tick", 0);
		if (t <= 0) { TW_LOGI("WARN: no active Camera3D found - typing trigger will not fire"); self->set_meta("nocam_warn_tick", 300); }
		else        { self->set_meta("nocam_warn_tick", t - 1); }
	}
	Vector3 player_pos = cam ? cam->get_global_position() : Vector3();

	// 3) Два хэша:
	//   geom_hash — геометрия чанков + RTL стайлинг + шейдер. Меняется = полный REBUILD.
	//   text_hash — только содержимое текста. Меняется = быстрое обновление RTL + redraw.
	String geom_hash = p_font_path + "_" + String::num_int64(p_font_size) + "_" +
					   p_font_color.to_html() + "_" + String::num_int64(p_line_spacing) + "_" +
					   String::num_int64(p_align_h) + "_" + String::num_int64(p_autowrap) + "_" +
					   String::num_int64(p_outline_size) + "_" + p_outline_color.to_html() + "_" +
					   String::num_int64(p_enable_shadow) + "_" + p_shadow_color.to_html() + "_" +
					   String::num_int64(p_shadow_x) + "_" + String::num_int64(p_shadow_y) + "_" +
					   String::num_int64(p_shadow_blur) + "_" + String::num_int64(p_render_quality) + "_" +
					   String::num(p_tile_aspect) + "_" + String::num(p_tile_size) + "_" +
					   String::num(p_chunk_size) + "_" + String::num_int64(p_is_billboard) + "_" +
					   String::num(p_render_distance) + "_" + p_shader_path;

	String text_hash = p_block_text + "_" + String::num_int64(p_uppercase);

	// Hash includes all shapes + mode + density
	String shape_hash = String::num_int64((int64_t)shapes.size());
	shape_hash += String("x");
	for (const auto& s : shapes) {
		shape_hash += s.cls;
		shape_hash += String(":");
		shape_hash += String::num(s.box_size.x); shape_hash += String(",");
		shape_hash += String::num(s.box_size.y); shape_hash += String(",");
		shape_hash += String::num(s.box_size.z); shape_hash += String(",");
		shape_hash += String::num(s.radius);     shape_hash += String(",");
		shape_hash += String::num(s.height);     shape_hash += String(",");
		shape_hash += String::num(s.xform.origin.x); shape_hash += String(",");
		shape_hash += String::num(s.xform.origin.y); shape_hash += String(",");
		shape_hash += String::num(s.xform.origin.z); shape_hash += String("|");
	}
	shape_hash += String("|m");
	shape_hash += String::num_int64(p_dist_mode);
	shape_hash += String("_d");
	shape_hash += String::num(p_tile_spacing);

	String last_geom  = self->get_meta("last_geom_hash", "");
	String last_text  = self->get_meta("last_text_hash", "");
	String last_shape = self->get_meta("last_shape_hash", "");
	bool needs_rebuild     = (geom_hash != last_geom || shape_hash != last_shape);
	bool needs_text_update = !needs_rebuild && (text_hash != last_text);

	SubViewport* vp = Object::cast_to<SubViewport>(self->get_node_or_null("InternalViewport"));
	Node3D* chunks_root = Object::cast_to<Node3D>(self->get_node_or_null("ChunksRoot"));

	// ==========================================
	// ЛЁГКОЕ ОБНОВЛЕНИЕ ТЕКСТА (без rebuild геометрии)
	// ==========================================
	if (needs_text_update && vp) {
		RichTextLabel* rtl_upd = Object::cast_to<RichTextLabel>(vp->get_node_or_null("InternalLabel"));
		if (rtl_upd) {
			String new_text = p_block_text;
			if (p_uppercase) new_text = new_text.to_upper();
			rtl_upd->set_text(wrap_align(new_text, p_align_h));
			rtl_upd->set_visible_characters(-1);
			vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
			self->set_meta("update_ticks", 5);
			self->set_meta("last_text_hash", text_hash);
			TW_LOGI("TEXT UPDATE (no rebuild): '%s'", p_block_text.utf8().get_data());
		}
	}

	// ==========================================
	// PER-FRAME: per-instance прогресс
	// ==========================================
	if (!needs_rebuild && chunks_root) {
		int update_ticks = self->get_meta("update_ticks", 0);
		if (vp) {
			if (update_ticks > 0) {
				vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
				self->set_meta("update_ticks", update_ticks - 1);
				if (update_ticks == 1) TW_LOGI("viewport rendered, switching to UPDATE_DISABLED");
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

			double progress = mmi->get_meta("progress", (p_text_effect == 0) ? 1.0 : 0.0);

			if (p_text_effect == 0) {
				// Текст всегда полностью виден
				progress = 1.0;
			} else if (p_text_effect == 1 && cam) {
				// Печать-триггер: при входе в radius прогресс плавно идёт к 1, при выходе — к 0
				double dist = mmi->get_global_position().distance_to(player_pos);
				bool chunk_near = (dist <= p_trigger_distance);
				if (chunk_near) near_count++;
				double target = chunk_near ? 1.0 : 0.0;

				bool was_near = mmi->get_meta("was_near", false);
				if (chunk_near != was_near) {
					mmi->set_meta("was_near", chunk_near);
					TW_LOGI("chunk[%d] %s, dist=%.2f trigger=%.2f", i, chunk_near ? "ENTER" : "EXIT", dist, p_trigger_distance);
				}

				double step_amount = progress_speed * delta;
				if      (progress < target) progress = std::min(target, progress + step_amount);
				else if (progress > target) progress = std::max(target, progress - step_amount);
				mmi->set_meta("progress", progress);
			} else if (p_text_effect == 2 && cam) {
				// Заполнение по дистанции: чем ближе игрок, тем больше букв.
				// dist = 0           -> progress = 1
				// dist = trigger     -> progress = min_progress
				// dist > trigger     -> progress = min_progress (clamp)
				double dist = mmi->get_global_position().distance_to(player_pos);
				double t = (p_trigger_distance > 0.001) ? (1.0 - dist / p_trigger_distance) : 1.0;
				if (t < 0.0) t = 0.0;
				if (t > 1.0) t = 1.0;
				progress = p_min_progress + (1.0 - p_min_progress) * t;
				if (dist <= p_trigger_distance) near_count++;
				mmi->set_meta("progress", progress);
			} else {
				// Нет камеры или неизвестный effect — оставить прошлое значение
			}

			// per-instance uniform — Godot НЕ дублирует материал
			mmi->call("set_instance_shader_parameter", "chunk_progress", progress);
		}

		// Per-frame uniforms на общий материал (живые настройки)
		if (self->has_meta("shared_mat")) {
			Ref<ShaderMaterial> smat = self->get_meta("shared_mat");
			if (smat.is_valid()) {
				smat->set_shader_parameter("billboard_mode",    p_is_billboard);
				smat->set_shader_parameter("wipe_softness",     p_wipe_softness);
				smat->set_shader_parameter("tint",              p_tint);
				smat->set_shader_parameter("glow_strength",     p_glow_strength);
				smat->set_shader_parameter("glow_color",        p_glow_color);
				smat->set_shader_parameter("pulse_speed",       p_pulse_speed);
				smat->set_shader_parameter("pulse_amount",      p_pulse_amount);
				smat->set_shader_parameter("flicker_speed",     p_flicker_speed);
				smat->set_shader_parameter("flicker_amount",    p_flicker_amount);
				smat->set_shader_parameter("wave_amp",          p_wave_amp);
				smat->set_shader_parameter("wave_speed",        p_wave_speed);
				smat->set_shader_parameter("wave_freq",         p_wave_freq);
				smat->set_shader_parameter("rainbow_speed",     p_rainbow_speed);
				smat->set_shader_parameter("scanline_strength", p_scanline_strength);
				smat->set_shader_parameter("scanline_count",    p_scanline_count);
				smat->set_shader_parameter("hover_amp",         p_hover_amp);
				smat->set_shader_parameter("hover_speed",       p_hover_speed);
				smat->set_shader_parameter("wind_dir",          p_wind_dir);
				smat->set_shader_parameter("wind_strength",     p_wind_strength);
				smat->set_shader_parameter("wind_distance",     p_wind_distance);
				smat->set_shader_parameter("wind_speed",        p_wind_speed);
				smat->set_shader_parameter("wind_gusts",        p_wind_gusts);
				smat->set_shader_parameter("wind_chaos",        p_wind_chaos);
			}
		}

		TW_LOGV("per-frame: chunks=%d near=%d effect=%d", chunk_count, near_count, p_text_effect);
		return;
	}

	// ==========================================
	// REBUILD
	// ==========================================
	TW_LOGI("=== REBUILD === shapes=%d mode=%s spacing=%.2f effect=%d trigger=%.2f text='%s'",
		(int)shapes.size(),
		p_dist_mode == 1 ? "VOLUME" : "SURFACE",
		p_tile_spacing, p_text_effect, p_trigger_distance, p_block_text.utf8().get_data());

	self->set_meta("last_geom_hash",  geom_hash);
	self->set_meta("last_text_hash",  text_hash);
	self->set_meta("last_shape_hash", shape_hash);
	self->set_meta("update_ticks", 5);

	// --- общий SubViewport + RTL ---
	if (!vp) {
		vp = memnew(SubViewport);
		vp->set_name("InternalViewport");
		vp->set_transparent_background(true);
		self->add_child(vp);
		TW_LOGI("created InternalViewport");
	}
	vp->set_size(Vector2i(p_frame_width, p_frame_height));
	vp->set_update_mode(SubViewport::UPDATE_ALWAYS);

	RichTextLabel* rtl = Object::cast_to<RichTextLabel>(vp->get_node_or_null("InternalLabel"));
	if (!rtl) {
		rtl = memnew(RichTextLabel);
		rtl->set_name("InternalLabel");
		vp->add_child(rtl);
		TW_LOGI("created InternalLabel");
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
		else                        TW_LOGE("failed to load font '%s'", p_font_path.utf8().get_data());
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
		TW_LOGE("SHADER NOT LOADED: '%s' - text will not be visible", p_shader_path.utf8().get_data());
	} else {
		TW_LOGI("shader loaded: %s", p_shader_path.utf8().get_data());
	}

	// --- Один общий ShaderMaterial ---
	Ref<ShaderMaterial> smat;
	if (self->has_meta("shared_mat")) smat = self->get_meta("shared_mat");
	if (smat.is_null()) {
		smat.instantiate();
		self->set_meta("shared_mat", smat);
		TW_LOGI("created shared ShaderMaterial");
	}
	if (shader_res.is_valid()) smat->set_shader(shader_res);
	smat->set_shader_parameter("albedo_tex",        vp->get_texture());
	smat->set_shader_parameter("billboard_mode",    p_is_billboard);
	smat->set_shader_parameter("wipe_softness",     p_wipe_softness);
	smat->set_shader_parameter("tint",              p_tint);
	smat->set_shader_parameter("glow_strength",     p_glow_strength);
	smat->set_shader_parameter("glow_color",        p_glow_color);
	smat->set_shader_parameter("pulse_speed",       p_pulse_speed);
	smat->set_shader_parameter("pulse_amount",      p_pulse_amount);
	smat->set_shader_parameter("flicker_speed",     p_flicker_speed);
	smat->set_shader_parameter("flicker_amount",    p_flicker_amount);
	smat->set_shader_parameter("wave_amp",          p_wave_amp);
	smat->set_shader_parameter("wave_speed",        p_wave_speed);
	smat->set_shader_parameter("wave_freq",         p_wave_freq);
	smat->set_shader_parameter("rainbow_speed",     p_rainbow_speed);
	smat->set_shader_parameter("scanline_strength", p_scanline_strength);
	smat->set_shader_parameter("scanline_count",    p_scanline_count);
	smat->set_shader_parameter("hover_amp",         p_hover_amp);
	smat->set_shader_parameter("hover_speed",       p_hover_speed);
	smat->set_shader_parameter("wind_dir",          p_wind_dir);
	smat->set_shader_parameter("wind_strength",     p_wind_strength);
	smat->set_shader_parameter("wind_distance",     p_wind_distance);
	smat->set_shader_parameter("wind_speed",        p_wind_speed);
	smat->set_shader_parameter("wind_gusts",        p_wind_gusts);
	smat->set_shader_parameter("wind_chaos",        p_wind_chaos);

	Ref<Texture2D> vtex = vp->get_texture();
	if (vtex.is_null()) TW_LOGE("vp->get_texture() returned null - material will draw nothing");

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

	double quad_h = p_tile_size;
	double quad_w = p_tile_size * p_tile_aspect;
	double c_size = p_chunk_size;

	std::unordered_map<std::string, std::vector<Transform3D>> chunk_map;
	int total_quads = 0;

	auto basis_from_normal = [&](const Vector3& n_in) -> Basis {
		Vector3 n = n_in.normalized();
		Vector3 up(0, 1, 0);
		if (std::abs(n.dot(up)) > 0.99) up = Vector3(1, 0, 0);
		Vector3 right = up.cross(n).normalized();
		Vector3 newUp = n.cross(right).normalized();
		return Basis(right, newUp, n);
	};

	// Текущий transform шейпа — обновляется в цикле dispatch ниже, submit капчурит по ссылке
	Transform3D current_shape_xform;

	auto submit = [&](const Transform3D& local_inst) {
		Transform3D final_xform = current_shape_xform * local_inst;
		int cx = (int)std::floor(final_xform.origin.x / c_size);
		int cy = (int)std::floor(final_xform.origin.y / c_size);
		int cz = (int)std::floor(final_xform.origin.z / c_size);
		char buf[128];
		std::snprintf(buf, sizeof(buf), "%d_%d_%d", cx, cy, cz);
		chunk_map[std::string(buf)].push_back(final_xform);
		total_quads++;
	};

	// Шаг сетки = расстояние между центрами тайлов. НЕ зависит от размера тайла.
	double step_h = p_tile_spacing;
	double step_w = p_tile_spacing * p_tile_aspect;

	// === SURFACE / BOX: сетка на грани, ceil + uniform spacing → края покрыты ===
	auto add_box_face = [&](Vector3 offset, Vector3 euler, double w, double h) {
		int nx = std::max(1, (int)std::ceil(w / step_w));
		int ny = std::max(1, (int)std::ceil(h / step_h));
		double sx = w / nx, sy = h / ny;
		Basis face_rot = Basis::from_euler(euler);
		for (int x = 0; x < nx; x++) {
			for (int y = 0; y < ny; y++) {
				Vector3 local_pos(-w * 0.5 + (x + 0.5) * sx, -h * 0.5 + (y + 0.5) * sy, 0);
				Transform3D local_inst;
				local_inst.basis  = p_is_billboard ? Basis() : face_rot;
				local_inst.origin = offset + face_rot.xform(local_pos);
				submit(local_inst);
			}
		}
	};

	// === SURFACE / SPHERE: лат-долготная сетка, плотность ∝ sin(phi) ===
	auto add_sphere_surface = [&](double R) {
		if (R <= 0.0) return;
		int phi_steps = std::max(2, (int)std::ceil(PI_GODOT * R / step_h));
		for (int pi = 0; pi < phi_steps; pi++) {
			double phi = PI_GODOT * (pi + 0.5) / phi_steps;
			double sphi = std::sin(phi), cphi = std::cos(phi);
			int theta_steps = std::max(1, (int)std::ceil(2.0 * PI_GODOT * R * std::abs(sphi) / step_w));
			for (int ti = 0; ti < theta_steps; ti++) {
				double theta = 2.0 * PI_GODOT * ti / theta_steps;
				Vector3 normal(sphi * std::cos(theta), cphi, sphi * std::sin(theta));
				Transform3D local_inst;
				local_inst.basis  = p_is_billboard ? Basis() : basis_from_normal(normal);
				local_inst.origin = normal * R;
				submit(local_inst);
			}
		}
	};

	// === SURFACE / CYLINDER side ===
	auto add_cylinder_surface = [&](double R, double H) {
		if (R <= 0.0 || H <= 0.0) return;
		int theta_steps = std::max(3, (int)std::ceil(2.0 * PI_GODOT * R / step_w));
		int y_steps = std::max(1, (int)std::ceil(H / step_h));
		for (int ti = 0; ti < theta_steps; ti++) {
			double theta = 2.0 * PI_GODOT * ti / theta_steps;
			double ct = std::cos(theta), st = std::sin(theta);
			Vector3 normal(ct, 0, st);
			for (int yi = 0; yi < y_steps; yi++) {
				double y = -H / 2.0 + H * (yi + 0.5) / y_steps;
				Transform3D local_inst;
				local_inst.basis  = p_is_billboard ? Basis() : basis_from_normal(normal);
				local_inst.origin = Vector3(R * ct, y, R * st);
				submit(local_inst);
			}
		}
	};

	auto add_hemisphere_surface = [&](double R, double y_offset, int up_dir) {
		if (R <= 0.0) return;
		int phi_steps = std::max(2, (int)std::ceil(0.5 * PI_GODOT * R / step_h));
		for (int pi = 0; pi < phi_steps; pi++) {
			double phi = (up_dir > 0)
				? (PI_GODOT * 0.5 * (pi + 0.5) / phi_steps)
				: (PI_GODOT * 0.5 + PI_GODOT * 0.5 * (pi + 0.5) / phi_steps);
			double sphi = std::sin(phi), cphi = std::cos(phi);
			int theta_steps = std::max(1, (int)std::ceil(2.0 * PI_GODOT * R * std::abs(sphi) / step_w));
			for (int ti = 0; ti < theta_steps; ti++) {
				double theta = 2.0 * PI_GODOT * ti / theta_steps;
				Vector3 normal(sphi * std::cos(theta), cphi, sphi * std::sin(theta));
				Transform3D local_inst;
				local_inst.basis  = p_is_billboard ? Basis() : basis_from_normal(normal);
				local_inst.origin = Vector3(R * normal.x, R * normal.y + y_offset, R * normal.z);
				submit(local_inst);
			}
		}
	};

	// === VOLUME: 3D-сетка внутри объёма шейпа, тайлы как биллборды ===
	auto add_volume_box = [&](Vector3 size) {
		int nx = std::max(1, (int)std::ceil(size.x / step_w));
		int ny = std::max(1, (int)std::ceil(size.y / step_h));
		int nz = std::max(1, (int)std::ceil(size.z / step_w));
		double sx = size.x / nx, sy = size.y / ny, sz = size.z / nz;
		for (int x = 0; x < nx; x++)
			for (int y = 0; y < ny; y++)
				for (int z = 0; z < nz; z++) {
					Transform3D local_inst;
					local_inst.basis = Basis(); // объёмные облака → всегда биллборд
					local_inst.origin = Vector3(
						-size.x * 0.5 + (x + 0.5) * sx,
						-size.y * 0.5 + (y + 0.5) * sy,
						-size.z * 0.5 + (z + 0.5) * sz);
					submit(local_inst);
				}
	};

	auto add_volume_sphere = [&](double R) {
		if (R <= 0.0) return;
		double D = 2.0 * R;
		int n = std::max(1, (int)std::ceil(D / step_w));
		double s = D / n;
		double R2 = R * R;
		for (int x = 0; x < n; x++)
			for (int y = 0; y < n; y++)
				for (int z = 0; z < n; z++) {
					double px = -R + (x + 0.5) * s;
					double py = -R + (y + 0.5) * s;
					double pz = -R + (z + 0.5) * s;
					if (px * px + py * py + pz * pz > R2) continue;
					Transform3D local_inst;
					local_inst.basis = Basis();
					local_inst.origin = Vector3(px, py, pz);
					submit(local_inst);
				}
	};

	auto add_volume_cylinder = [&](double R, double H) {
		if (R <= 0.0 || H <= 0.0) return;
		double D = 2.0 * R;
		int nx = std::max(1, (int)std::ceil(D / step_w));
		int ny = std::max(1, (int)std::ceil(H / step_h));
		int nz = std::max(1, (int)std::ceil(D / step_w));
		double sx = D / nx, sy = H / ny, sz = D / nz;
		double R2 = R * R;
		for (int x = 0; x < nx; x++)
			for (int y = 0; y < ny; y++)
				for (int z = 0; z < nz; z++) {
					double px = -R + (x + 0.5) * sx;
					double pz = -R + (z + 0.5) * sz;
					if (px * px + pz * pz > R2) continue;
					double py = -H * 0.5 + (y + 0.5) * sy;
					Transform3D local_inst;
					local_inst.basis = Basis();
					local_inst.origin = Vector3(px, py, pz);
					submit(local_inst);
				}
	};

	auto add_volume_capsule = [&](double R, double H) {
		if (R <= 0.0) return;
		double D = 2.0 * R;
		double cyl_h = std::max(0.0, H - 2.0 * R);
		double total_h = cyl_h + 2.0 * R; // = H
		int nx = std::max(1, (int)std::ceil(D / step_w));
		int ny = std::max(1, (int)std::ceil(total_h / step_h));
		int nz = std::max(1, (int)std::ceil(D / step_w));
		double sx = D / nx, sy = total_h / ny, sz = D / nz;
		double R2 = R * R;
		double y_top    =  cyl_h * 0.5; // выше — верхняя полусфера
		double y_bottom = -cyl_h * 0.5; // ниже — нижняя полусфера
		for (int x = 0; x < nx; x++)
			for (int y = 0; y < ny; y++)
				for (int z = 0; z < nz; z++) {
					double px = -R + (x + 0.5) * sx;
					double py = -total_h * 0.5 + (y + 0.5) * sy;
					double pz = -R + (z + 0.5) * sz;
					double r2_xz = px * px + pz * pz;
					bool inside;
					if (py >= y_bottom && py <= y_top) {
						inside = (r2_xz <= R2);                        // цилиндрическая часть
					} else if (py > y_top) {
						double dy = py - y_top;
						inside = (r2_xz + dy * dy <= R2);              // верхняя полусфера
					} else {
						double dy = py - y_bottom;
						inside = (r2_xz + dy * dy <= R2);              // нижняя полусфера
					}
					if (!inside) continue;
					Transform3D local_inst;
					local_inst.basis = Basis();
					local_inst.origin = Vector3(px, py, pz);
					submit(local_inst);
				}
	};

	// === Dispatch по каждому шейпу ===
	for (const auto& s : shapes) {
		current_shape_xform = s.xform;
		int quads_before = total_quads;

		if (p_dist_mode == 1) {
			// VOLUME
			if      (s.cls == "BoxShape3D")      add_volume_box(s.box_size);
			else if (s.cls == "SphereShape3D")   add_volume_sphere(s.radius);
			else if (s.cls == "CylinderShape3D") add_volume_cylinder(s.radius, s.height);
			else if (s.cls == "CapsuleShape3D")  add_volume_capsule(s.radius, s.height);
		} else {
			// SURFACE
			if (s.cls == "BoxShape3D") {
				const Vector3& bs = s.box_size;
				if (bs.x >= step_w && bs.y >= step_h) {
					if (bs.z < step_h) add_box_face(Vector3(0, 0, 0), Vector3(0, 0, 0), bs.x, bs.y);
					else {
						add_box_face(Vector3(0, 0,  bs.z / 2.0), Vector3(0, 0, 0), bs.x, bs.y);
						add_box_face(Vector3(0, 0, -bs.z / 2.0), Vector3(0, PI_GODOT, 0), bs.x, bs.y);
					}
				}
				if (bs.z >= step_w && bs.y >= step_h) {
					if (bs.x < step_h) add_box_face(Vector3(0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), bs.z, bs.y);
					else {
						add_box_face(Vector3( bs.x / 2.0, 0, 0), Vector3(0, -PI_GODOT / 2.0, 0), bs.z, bs.y);
						add_box_face(Vector3(-bs.x / 2.0, 0, 0), Vector3(0,  PI_GODOT / 2.0, 0), bs.z, bs.y);
					}
				}
				if (bs.x >= step_w && bs.z >= step_h) {
					if (bs.y < step_h) add_box_face(Vector3(0, 0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), bs.x, bs.z);
					else {
						add_box_face(Vector3(0,  bs.y / 2.0, 0), Vector3(-PI_GODOT / 2.0, 0, 0), bs.x, bs.z);
						add_box_face(Vector3(0, -bs.y / 2.0, 0), Vector3( PI_GODOT / 2.0, 0, 0), bs.x, bs.z);
					}
				}
			} else if (s.cls == "SphereShape3D") {
				add_sphere_surface(s.radius);
			} else if (s.cls == "CylinderShape3D") {
				add_cylinder_surface(s.radius, s.height);
			} else if (s.cls == "CapsuleShape3D") {
				double cyl_h = std::max(0.0, s.height - 2.0 * s.radius);
				add_cylinder_surface(s.radius, cyl_h);
				add_hemisphere_surface(s.radius,  cyl_h / 2.0, +1);
				add_hemisphere_surface(s.radius, -cyl_h / 2.0, -1);
			}
		}

		TW_LOGV("shape %s: +%d quads", s.cls.utf8().get_data(), total_quads - quads_before);
	}

	if (chunk_map.empty()) {
		TW_LOGE("no chunks generated - all shapes too small for spacing=(%.2f,%.2f)", step_w, step_h);
	} else {
		TW_LOGI("total %d quads on %d shape(s) (mode=%s tile_size=%.2f spacing=%.2fx%.2f)",
			total_quads, (int)shapes.size(),
			p_dist_mode == 1 ? "VOLUME" : "SURFACE",
			p_tile_size, step_w, step_h);
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

		double initial_progress;
		if      (p_text_effect == 0) initial_progress = 1.0;
		else if (p_text_effect == 2) initial_progress = p_min_progress;
		else                         initial_progress = 0.0; // effect 1 — стартует с пустого, заполняется при подходе
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
