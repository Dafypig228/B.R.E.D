
/* Jenova C++ Node Base Script (Meteora) */

// Godot SDK
#include <Godot/godot.hpp>
#include <Godot/classes/node.hpp>
#include <Godot/classes/node3d.hpp>
#include <Godot/variant/variant.hpp>

// Namespaces
using namespace godot;
using namespace jenova::sdk;

// Self Instance
Node3D* self = nullptr;

// Jenova Script Block Start
JENOVA_SCRIPT_BEGIN
JENOVA_PROPERTY(double, trainSpeed, 35.0)  
JENOVA_PROPERTY(double, MaxPose, 50.0) 

// Routines
void OnAwake(Caller* instance)
{
	// Called When Node Enters Scene Tree
	self = GetSelf<Node3D>(instance);
}
void OnDestroy(Caller* instance)
{
	// Called When Node Exits Scene Tree
	self = nullptr;
}
void OnReady(Caller* instance)
{
	// Called When Node and All It's Children Entered Scene Tree
}
void OnProcess(Caller* instance, double _delta)
{
	// в OnProcess поезда
Node3D* self = GetSelf<Node3D>(instance);
Vector3 pos = self->get_position();
pos.x -= trainSpeed * _delta;  // едет быстро
if (pos.x < -MaxPose) pos.x = MaxPose;
self->set_position(pos);
// Called On Every Frame
}

// Jenova Script Block End
JENOVA_SCRIPT_END
