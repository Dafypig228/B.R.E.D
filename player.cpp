#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/classes/input.hpp>
#include <Godot/classes/input_event.hpp>
#include <Godot/classes/input_event_mouse_motion.hpp>

using namespace godot;
using namespace jenova::sdk;

// Ноды держим как Node*: в этой сборке Jenova cast_to<>()/is_class() НЕ
// распознают корень инстансированной сцены (Player). Надёжно работают только
// get_class() (поиск) и call() (вызов методов через диспетчер движка).
static Node* self_n = nullptr;   // CharacterBody3D игрока
static Node* head_n = nullptr;   // нода рыскания (родитель камеры)
static Node* cam_n  = nullptr;   // Camera3D

JENOVA_SCRIPT_BEGIN

// Constants/Config
JENOVA_PROPERTY(double, WALK_SPEED, 5.0)
JENOVA_PROPERTY(double, SPRINT_SPEED, 10.0)
JENOVA_PROPERTY(double, JUMP_VELOCITY, 6.0)
JENOVA_PROPERTY(double, SENSITIVITY, 0.004)
JENOVA_PROPERTY(double, BOB_FREQ, 2.4)
JENOVA_PROPERTY(double, BOB_AMP, 0.08)
JENOVA_PROPERTY(double, BASE_FOV, 75.0)
JENOVA_PROPERTY(double, FOV_CHANGE, 1.5)
JENOVA_PROPERTY(double, GRAVITY, 9.8)

static float t_bob = 0.0f;

// Рекурсивный поиск ноды по строке get_class() — она в Jenova работает надёжно.
static Node* find_by_class_str(Node* root, const char* cls) {
	if (!root) return nullptr;
	if (root->get_class() == String(cls)) return root;
	for (int i = 0; i < root->get_child_count(); i++) {
		Node* found = find_by_class_str(root->get_child(i), cls);
		if (found) return found;
	}
	return nullptr;
}

// Диагностика: печатает дерево под нодой с классами.
static void dump_tree(Node* n, int depth) {
	if (!n) return;
	CharString nm = String(n->get_name()).utf8();
	CharString cl = n->get_class().utf8();
	Output("[player]   depth=%d  '%s'  [%s]", depth, nm.get_data(), cl.get_data());
	for (int i = 0; i < n->get_child_count(); i++)
		dump_tree(n->get_child(i), depth + 1);
}

// Резолвим ноды по классу. Не зависит от OnAwake (hot-reload обнуляет статики).
static bool resolve_nodes(Caller* instance) {
	if (self_n != nullptr && head_n != nullptr && cam_n != nullptr) return true;
	Node3D* ab = GetSelf<Node3D>(instance);
	if (!ab) return false;
	self_n = find_by_class_str(ab, "CharacterBody3D");
	cam_n  = self_n ? find_by_class_str(self_n, "Camera3D") : nullptr;
	head_n = cam_n  ? cam_n->get_parent() : nullptr;
	bool ok = (self_n != nullptr && head_n != nullptr && cam_n != nullptr);

	static int dumped = 0;
	if (!ok && dumped < 1) {
		dumped = 1;
		Output("[player] === НОДЫ НЕ НАЙДЕНЫ. Дерево под скрипт-нодой: ===");
		dump_tree(ab, 0);
	}
	return ok;
}

Vector3 _headbob(float time) {
	Vector3 pos = Vector3(0, 0, 0);
	pos.y = Math::sin(time * BOB_FREQ) * BOB_AMP;
	pos.x = Math::cos(time * BOB_FREQ / 2.0f) * BOB_AMP;
	return pos;
}

void OnAwake(Caller* instance) {
	resolve_nodes(instance);
	Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
}

void _unhandled_input(Caller* instance, InputEvent* event) {
	if (!resolve_nodes(instance)) return;

	if (InputEventMouseMotion* motion = Object::cast_to<InputEventMouseMotion>(event)) {
		head_n->call("rotate_y", -motion->get_relative().x * SENSITIVITY);
		cam_n->call("rotate_x", -motion->get_relative().y * SENSITIVITY);
		Vector3 rot = cam_n->call("get_rotation");
		rot.x = Math::clamp(rot.x,
			(real_t)Math::deg_to_rad(-60.0),
			(real_t)Math::deg_to_rad(80.0));
		cam_n->call("set_rotation", rot);
	}
	if (Input::get_singleton()->is_action_just_pressed("Pause")) {
		if (Input::get_singleton()->get_mouse_mode() == Input::MOUSE_MODE_CAPTURED) {
			Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_VISIBLE);
		} else {
			Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
		}
	}
}

void OnPhysicsProcess(Caller* instance, double delta) {
	if (!resolve_nodes(instance)) return;

	Vector3 velocity = self_n->call("get_velocity");
	bool on_floor = self_n->call("is_on_floor");

	// Gravity
	if (!on_floor) {
		velocity.y -= GRAVITY * (float)delta;
	}

	// Jump
	if (Input::get_singleton()->is_action_just_pressed("Jump") && on_floor) {
		velocity.y = JUMP_VELOCITY;
	}

	// Sprint Logic
	float speed = Input::get_singleton()->is_action_pressed("Sprint") ? SPRINT_SPEED : WALK_SPEED;

	// Movement
	Vector2 input_dir = Input::get_singleton()->get_vector("MoveLeft", "MoveRight", "MoveForward", "MoveBack");
	Transform3D head_tf = head_n->call("get_transform");
	Transform3D self_tf = self_n->call("get_transform");
	Vector3 direction = (head_tf.basis * self_tf.basis).xform(Vector3(input_dir.x, 0, input_dir.y)).normalized();

	if (on_floor) {
		if (direction != Vector3()) {
			velocity.x = direction.x * speed;
			velocity.z = direction.z * speed;
		} else {
			velocity.x = Math::lerp(velocity.x, 0.0f, (real_t)delta * 7.0f);
			velocity.z = Math::lerp(velocity.z, 0.0f, (real_t)delta * 7.0f);
		}
	} else {
		velocity.x = Math::lerp(velocity.x, direction.x * speed, (real_t)delta * 3.0f);
		velocity.z = Math::lerp(velocity.z, direction.z * speed, (real_t)delta * 3.0f);
	}

	// Bobbing
	t_bob += (float)delta * velocity.length() * (on_floor ? 1.0f : 0.0f);
	head_n->call("set_position", _headbob(t_bob));

	// FOV
	float velocity_clamped = Math::clamp(velocity.length(), 0.5f, (float)SPRINT_SPEED * 2.0f);
	float target_fov = BASE_FOV + FOV_CHANGE * velocity_clamped;
	double cur_fov = cam_n->call("get_fov");
	cam_n->call("set_fov", Math::lerp((real_t)cur_fov, (real_t)target_fov, (real_t)delta * 8.0f));

	self_n->call("set_velocity", velocity);
	self_n->call("move_and_slide");
}

JENOVA_SCRIPT_END
