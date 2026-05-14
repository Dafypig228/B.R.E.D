#include <Godot/godot.hpp>
#include <Godot/classes/character_body3d.hpp>
#include <Godot/classes/mesh_instance3d.hpp>
#include <Godot/classes/camera3d.hpp>
#include <Godot/classes/input.hpp>
#include <Godot/classes/input_event_mouse_motion.hpp>

using namespace godot;
using namespace jenova::sdk;

CharacterBody3D* self = nullptr;
MeshInstance3D* head = nullptr;
Camera3D* camera = nullptr;

JENOVA_SCRIPT_BEGIN

// Constants/Config
const float WALK_SPEED = 5.0f;
const float SPRINT_SPEED = 8.0f;
const float JUMP_VELOCITY = 4.8f;
const float SENSITIVITY = 0.004f;
const float BOB_FREQ = 2.4f;
const float BOB_AMP = 0.08f;
const float BASE_FOV = 75.0f;
const float FOV_CHANGE = 1.5f;
const float GRAVITY = 9.8f;

float t_bob = 0.0f;

Vector3 _headbob(float time) {
	Vector3 pos = Vector3(0, 0, 0);
	pos.y = Math::sin(time * BOB_FREQ) * BOB_AMP;
	pos.x = Math::cos(time * BOB_FREQ / 2.0f) * BOB_AMP;
	return pos;
}

void OnAwake(Caller* instance) {
	self = GetSelf<CharacterBody3D>(instance);
	head = self->get_node<MeshInstance3D>("Head");
	camera = head->get_node<Camera3D>("Camera3D");
	Input::get_singleton()->set_mouse_mode(Input::MOUSE_MODE_CAPTURED);
}

void _unhandled_input(Caller* instance, InputEvent* event) {
	if (InputEventMouseMotion* motion = Object::cast_to<InputEventMouseMotion>(event)) {
		// Rotate head Y
		head->rotate_y(-motion->get_relative().x * SENSITIVITY);
		// Rotate camera X
		camera->rotate_x(-motion->get_relative().y * SENSITIVITY);
		// Clamp camera X
		Vector3 rot = camera->get_rotation();
		rot.x = Math::clamp(rot.x, (real_t)Math::deg_to_rad(-40.0), (real_t)Math::deg_to_rad(60.0));
		camera->set_rotation(rot);
	}
}

void OnPhysicsProcess(Caller* instance, double delta) {
	Vector3 velocity = self->get_velocity();

	// Gravity
	if (!self->is_on_floor()) {
		velocity.y -= GRAVITY * (float)delta;
	}

	// Jump
	if (Input::get_singleton()->is_action_just_pressed("Jump") && self->is_on_floor()) {
		velocity.y = JUMP_VELOCITY;
	}

	// Sprint Logic
	float speed = Input::get_singleton()->is_action_pressed("Sprint") ? SPRINT_SPEED : WALK_SPEED;

	// Movement
	Vector2 input_dir = Input::get_singleton()->get_vector("MoveLeft", "MoveRight", "MoveForward", "MoveBackward");
	// Note: head->get_transform().basis * self->get_transform().basis is your GDScript logic
	Vector3 direction = (head->get_transform().basis * self->get_transform().basis).xform(Vector3(input_dir.x, 0, input_dir.y)).normalized();

	if (self->is_on_floor()) {
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
	t_bob += (float)delta * velocity.length() * (self->is_on_floor() ? 1.0f : 0.0f);
	camera->set_position(_headbob(t_bob));

	// FOV
	float velocity_clamped = Math::clamp(velocity.length(), 0.5f, SPRINT_SPEED * 2.0f);
	float target_fov = BASE_FOV + FOV_CHANGE * velocity_clamped;
	camera->set_fov(Math::lerp(camera->get_fov(), (real_t)target_fov, (real_t)delta * 8.0f));

	self->set_velocity(velocity);
	self->move_and_slide();
}

JENOVA_SCRIPT_END
