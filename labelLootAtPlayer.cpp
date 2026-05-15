
/* Jenova C++ Node Base Script (Meteora) */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/label3d.hpp>
#include <Godot/variant/variant.hpp>
#include <Godot/classes/character_body3d.hpp>

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Self Instance
CharacterBody3D* player = nullptr;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN

// Routines
void OnAwake(Caller* instance)
{
	// Called When Node Enters Scene Tree
	player = GetNode<CharacterBody3D>("/root/MainScene/Player/Head/Eyes/Camera3D"); 
}
void OnDestroy(Caller* instance)
{
	// Called When Node Exits Scene Tree
}
void OnReady(Caller* instance)
{
}
void OnProcess(Caller* instance, double _delta)
{
	if (player != nullptr)
	{
		Vector3 player_pos = player->get_global_position();
		
		GetSelf<Label3D>(instance)->look_at(player_pos, Vector3(0, 1, 0));
		
		GetSelf<Label3D>(instance)->rotate_y(Math::deg_to_rad(180.0));
	}
}

// Jenova Script Block End
JENOVA_SCRIPT_END
