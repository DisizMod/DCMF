#pragma once

#include <string>
#include <string_view>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace UI::ColliderDraw
{
	// A CBPC collider: on a bone, its ends in the bone's frame, scaled as CBPC scales them; a sphere has one end
	struct Collider
	{
		RE::ActorHandle actor;
		std::string node;
		RE::NiPoint3 end1;
		float radius1 = 0.f;
		RE::NiPoint3 end2;
		float radius2 = 0.f;
		bool bCapsule = false;
		float scaleWeight = 1.f;
		bool bAffected = false;  // a part CBPC pushes, not one that pushes
		bool bBone = false;      // a bone's place, not a collider
		bool bReference = false;  // TODO: temporary, a hand-made config's collider shown for comparison
		std::string endNode;      // a capsule's far end at this node instead, end2 in its space: a line between two bones
	};

	// The colliders an owner shows, replacing what it showed before
	void Set(std::string_view a_owner, std::vector<Collider> a_colliders);

	// From Present: every owner's colliders over the frame, what the scene hides faint
	void Draw(ID3D11Device* a_device, ID3D11DeviceContext* a_context);
}
