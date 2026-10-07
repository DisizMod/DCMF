#pragma once

#include <rapidjson/document.h>

#include <string>
#include <string_view>

// Changes to how an older DCMF wrote a mod, applied to the file's JSON as it is read: each step names the version it
// brings the file to, and runs on a file written before it
namespace Migrations
{
	// The version a file says it was written with; 1.0.0 when it says none
	[[nodiscard]] REL::Version VersionOf(const rapidjson::Value& a_doc);

	// The version this build writes, as a file records it: "major.minor.patch"
	[[nodiscard]] std::string CurrentVersionText();

	// Every step newer than a_from run on the mod's JSON, in order, and the JSON marked as this build's; what changed
	// logged once per step
	void Apply(rapidjson::Document& a_doc, REL::Version a_from, std::string_view a_mod);
}
