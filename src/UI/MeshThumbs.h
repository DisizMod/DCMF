#pragma once

#include "scan/Scan.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

struct ID3D11Device;
struct ID3D11DeviceContext;

// A picture of a head part on the head it was made for: the part's model over the face model of the first of its
// valid races, drawn offscreen from Present, one a frame, and kept while the panel is open
namespace UI::MeshThumbs
{
	// The face model a part is shown on: the first of its valid races with a face for its sex, the Nord's when it
	// names none; empty, with why, when there is none
	[[nodiscard]] std::string MannequinOf(RE::FormID a_part, std::string* a_why = nullptr);

	// The picture, or 0 while it loads, renders or failed; asking is what queues the work
	[[nodiscard]] ImTextureID Card(RE::FormID a_part);
	[[nodiscard]] bool Failed(RE::FormID a_part, std::string* a_why = nullptr);

	// The picture at a size, or a placeholder saying loading or failed; hovered, the picture at full size
	void DrawCard(RE::FormID a_part, float a_size, bool a_bTooltip = true);

	// An overlay texture, relative to Data/Textures, painted on a scanned naked model of the part for that layout,
	// tinted so a decal reads on grey; the same card, placeholder and hover as a head part's
	void DrawOverlayCard(const std::string& a_texture, Scan::SkinPart a_part, std::string_view a_uv, float a_size, bool a_bTooltip = true);

	// Every scanned part of the type (any of the slots' when none) matching the filter, as cards with their names, a
	// labelled run per plugin; true when one was clicked, in a_picked. Hovered, a card is shown three times larger
	// With a reference actor, a part its race or sex cannot wear is greyed, still pickable
	bool DrawGrid(std::optional<RE::BGSHeadPart::HeadPartType> a_type, std::string_view a_filter, RE::FormID a_current, RE::FormID& a_picked, float a_size, RE::Actor* a_reference);

	// The Pick button and the popup it opens: a search over the grid, sized for four cards across and five down
	bool PickerButton(std::optional<RE::BGSHeadPart::HeadPartType> a_type, RE::FormID a_current, RE::FormID& a_picked, RE::Actor* a_reference);

	// Render thread, from Present: renders one waiting card
	void Advance(ID3D11Device* a_device, ID3D11DeviceContext* a_context);

	// Drops everything at the next frame; the panel closing
	void Clear();
}
