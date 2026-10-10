#include "Cbpc.h"

#include "ActorState.h"
#include "BodyMorph.h"
#include "BodyTypes.h"
#include "Settings.h"
#include "UI/ColliderDraw.h"

#include <chrono>
#include <future>
#include <filesystem>
#include <fstream>

namespace Apply::Cbpc
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr auto kConfigPath = "Data/SKSE/Plugins/CBPCollisionConfig_DCMF.txt";
		constexpr auto kScript = "CBPCPluginScript";
		constexpr std::uint32_t kIndex = 900;
		constexpr std::uint32_t kSentinelIndex = 901;
		constexpr auto kSentinelNode = "NPC Root [Root]";
		constexpr std::size_t kFewestVertices = 8;
		constexpr auto kCheckEvery = std::chrono::seconds(1);

		// One collider: on a bone, a sphere or a capsule; a pushed part pushes as well. A fixed one is the same on every body
		struct Bone
		{
			const char* node;
			bool bCapsule;
			bool bPushed;
			bool bFixed = false;
			float a[3]{};
			float radiusA = 0.f;
			float b[3]{};
			float radiusB = 0.f;
			bool bPushes = true;  // a pushed part that pushes nothing, as the anus, says false
		};

		constexpr std::array kLayout{
			Bone{ "L Breast01", false, true },
			Bone{ "R Breast01", false, true },
			Bone{ "L Breast02", false, true },
			Bone{ "R Breast02", false, true },
			Bone{ "L Breast03", false, true },
			Bone{ "R Breast03", false, true },
			Bone{ "NPC L Butt", false, true },
			Bone{ "NPC R Butt", false, true },
			Bone{ "NPC Belly", true, true },
			Bone{ .node = "NPC Head [Head]", .bCapsule = true, .bPushed = false, .bFixed = true, .a = { 0.f, 3.7f, 5.2f }, .radiusA = 6.f, .b = { 0.f, 5.f, 0.f }, .radiusB = 5.4f },
			Bone{ "NPC L UpperArm [LUar]", true, false },
			Bone{ "NPC R UpperArm [RUar]", true, false },
			Bone{ "NPC L Forearm [LLar]", true, false },
			Bone{ "NPC R Forearm [RLar]", true, false },
			Bone{ .node = "NPC L Hand [LHnd]", .bCapsule = true, .bPushed = false, .bFixed = true, .a = { -0.6f, -0.5f, 1.f }, .radiusA = 1.5f, .b = { -0.6f, -0.5f, 7.f }, .radiusB = 1.f },
			Bone{ .node = "NPC R Hand [RHnd]", .bCapsule = true, .bPushed = false, .bFixed = true, .a = { 0.6f, -0.5f, 1.f }, .radiusA = 1.5f, .b = { 0.6f, -0.5f, 7.f }, .radiusB = 1.f },
			Bone{ .node = "NPC L Finger02 [LF02]", .bCapsule = true, .bPushed = false, .bFixed = true, .a = { 0.f, 0.f, 0.f }, .radiusA = 1.f, .b = { 0.f, 0.f, 2.f }, .radiusB = .7f },
			Bone{ .node = "NPC R Finger02 [RF02]", .bCapsule = true, .bPushed = false, .bFixed = true, .a = { 0.f, 0.f, 0.f }, .radiusA = 1.f, .b = { 0.f, 0.f, 2.f }, .radiusB = .7f },
			Bone{ .node = "NPC L Finger12 [LF12]", .bCapsule = false, .bPushed = false, .bFixed = true, .a = { 0.f, .15f, 1.5f }, .radiusA = .8f },
			Bone{ .node = "NPC R Finger12 [RF12]", .bCapsule = false, .bPushed = false, .bFixed = true, .a = { 0.f, .15f, 1.5f }, .radiusA = .8f },
			Bone{ .node = "NPC L Finger22 [LF22]", .bCapsule = false, .bPushed = false, .bFixed = true, .a = { 0.f, 0.f, 5.f }, .radiusA = .5f },
			Bone{ .node = "NPC R Finger22 [RF22]", .bCapsule = false, .bPushed = false, .bFixed = true, .a = { 0.f, 0.f, 5.f }, .radiusA = .5f },
			Bone{ .node = "NPC L Finger42 [LF42]", .bCapsule = false, .bPushed = false, .bFixed = true, .a = { 0.f, 0.f, 1.2f }, .radiusA = .6f },
			Bone{ .node = "NPC R Finger42 [RF42]", .bCapsule = false, .bPushed = false, .bFixed = true, .a = { 0.f, 0.f, 1.2f }, .radiusA = .6f },
			Bone{ "NPC L Thigh [LThg]", true, false },
			Bone{ "NPC R Thigh [RThg]", true, false },
			Bone{ "NPC L Calf [LClf]", true, false },
			Bone{ "NPC R Calf [RClf]", true, false },
			Bone{ .node = "NPC L Foot [Lft ]", .bCapsule = true, .bPushed = false, .bFixed = true, .a = { -.5f, -3.f, 1.f }, .radiusA = 2.5f, .b = { 0.f, 2.f, 11.f }, .radiusB = 2.5f },
			Bone{ .node = "NPC R Foot [Rft ]", .bCapsule = true, .bPushed = false, .bFixed = true, .a = { .5f, -3.f, 1.f }, .radiusA = 2.5f, .b = { 0.f, 2.f, 11.f }, .radiusB = 2.5f },
			Bone{ .node = "NPC L Pussy02", .bCapsule = true, .bPushed = true, .bFixed = true, .a = { 1.5f, -3.25f, 0.f }, .radiusA = 2.f, .b = { 1.f, 1.5f, .5f }, .radiusB = 2.f },
			Bone{ .node = "NPC R Pussy02", .bCapsule = true, .bPushed = true, .bFixed = true, .a = { -1.5f, -3.25f, 0.f }, .radiusA = 2.f, .b = { -1.f, 1.5f, .5f }, .radiusB = 2.f },
			Bone{ .node = "Clitoral1", .bCapsule = false, .bPushed = true, .bFixed = true, .a = { 0.f, 1.f, 1.5f }, .radiusA = 2.f },
			Bone{ .node = "Anal", .bCapsule = false, .bPushed = true, .bFixed = true, .a = { 0.f, -7.5f, -2.3f }, .radiusA = .5f, .bPushes = false },
		};

		// The helper bones whose skin a limb's collider takes as its own: twists, fingers, the thigh's and calf's halves, toes
		std::string_view HelpersOf(std::string_view a_node)
		{
			if (a_node.find("UpperArm") != std::string_view::npos) {
				return "UpperarmTwist";
			}
			if (a_node.find("Forearm") != std::string_view::npos) {
				return "ForearmTwist";
			}
			if (a_node.find("Hand") != std::string_view::npos) {
				return "Finger";
			}
			if (a_node.find("Thigh") != std::string_view::npos) {
				return "Thigh";
			}
			if (a_node.find("Calf") != std::string_view::npos) {
				return "Calf";
			}
			if (a_node.find("Foot") != std::string_view::npos) {
				return "Toe";
			}
			return {};
		}

		bool ContainsNoCase(std::string_view a_text, std::string_view a_part)
		{
			return !a_part.empty() && std::ranges::search(a_text, a_part, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }).begin() != a_text.end();
		}

		// A bone of the shape belongs to a limb's collider when it is the collider's bone, or one of its helpers on the same side
		bool BelongsTo(std::string_view a_bone, const Bone& a_layout)
		{
			const std::string_view node = a_layout.node;
			if (a_bone.size() == node.size() && ContainsNoCase(a_bone, node)) {
				return true;
			}
			const auto helpers = HelpersOf(node);
			const bool bLeft = node.find(" L ") != std::string_view::npos;
			const bool bRight = node.find(" R ") != std::string_view::npos;
			if (helpers.empty() || (!bLeft && !bRight)) {
				return false;
			}
			return a_bone.find(bLeft ? " L " : " R ") != std::string_view::npos && ContainsNoCase(a_bone, helpers);
		}

		bool IsBreast(std::string_view a_node)
		{
			return a_node.find("Breast") != std::string_view::npos;
		}

		// The breast bones, drawn with the colliders
		constexpr std::array kBreastBones{ "NPC L Breast", "NPC R Breast", "L Breast01", "R Breast01", "L Breast02", "R Breast02", "L Breast03", "R Breast03" };

		// What a pushed part does not collide with, as 3BA's own collision config has it: an @ the actor's own, no @ anyone's.
		// Breast01 also leaves out the actor's own thighs and calves, which reach it when she sneaks
		std::string_view ExclusionsOf(std::string_view a_node)
		{
			if (a_node.find("Breast01") != std::string_view::npos) {
				return "@L Breast01,@L Breast02,@L Breast03,@R Breast01,@R Breast02,@R Breast03,@NPC L Thigh [LThg],@NPC R Thigh [RThg],@NPC L Calf [LClf],@NPC R Calf [RClf]";
			}
			if (a_node.find("Breast") != std::string_view::npos) {
				return "@L Breast01,@L Breast02,@L Breast03,@R Breast01,@R Breast02,@R Breast03";
			}
			if (a_node.find("Butt") != std::string_view::npos) {
				return "@NPC L Butt,@NPC R Butt,@NPC L Thigh [LThg],@NPC R Thigh [RThg],@NPC Genitals01 [Gen01],@L Breast01,@L Breast02,@L Breast03,@R Breast01,@R Breast02,@R Breast03";
			}
			if (a_node.find("Pussy") != std::string_view::npos) {
				return "@NPC L Butt,@NPC R Butt,@NPC Genitals01 [Gen01],@NPC L Thigh [LThg],@NPC R Thigh [RThg]";
			}
			if (a_node == "Clitoral1") {
				return "@NPC L Butt,@NPC R Butt,@NPC Genitals01 [Gen01],NPC L Thigh [LThg],NPC R Thigh [RThg]";
			}
			// The anus, and the belly as 3BA's pelvis and spine read
			return "@NPC L Butt,@NPC R Butt,@NPC L Thigh [LThg],@NPC R Thigh [RThg],@L Breast01,@L Breast02,@L Breast03,@R Breast01,@R Breast02,@R Breast03";
		}

		// The nodes and who collides with whom; no shape, every collider is one DCMF attaches
		std::string ConfigText()
		{
			std::string text = "# Written by DCMF while its CBPC patch is on: its colliders are fitted to each actor's body and attached at run time\n";
			text += "[Options]\nConditions = IsFemale() AND IsInFaction(DCMF.esp|000800)\nPriority = 1000\n";
			text += "[ExtraOptions]\nBellyBulge=0\n";
			text += "[AffectedNodes]\n";
			for (const auto& bone : kLayout) {
				if (bone.bPushed) {
					text += std::format("{} ({})\n", bone.node, ExclusionsOf(bone.node));
				}
			}
			text += "[ColliderNodes]\n";
			for (const auto& bone : kLayout) {
				if (bone.bPushes) {
					text += std::format("{}\n", bone.node);
				}
			}
			return text;
		}

		// ---- the fitting ----

		struct Fitted
		{
			std::string node;
			bool bCapsule = false;
			bool bPushed = false;
			RE::NiPoint3 a;
			RE::NiPoint3 b;
			float radiusA = 0.f;
			float radiusB = 0.f;
			bool bPushes = true;
		};

		// A vertex in a bone's space, with the weight the bone has on it
		struct Weighted
		{
			RE::NiPoint3 p;
			float w = 0.f;
			RE::NiPoint3 n;  // the skin's outward normal there; zero when the mesh keeps none
		};

		// The sphere whose centre lies straight in from every point at one depth: with a = p - mean(p) and b = n - mean(n),
		// the depth is sum(w a.b) / sum(w |b|^2) and the centre mean(p) - depth mean(n); none on a patch too flat to say
		std::optional<std::pair<RE::NiPoint3, float>> SphereInside(const std::vector<Weighted>& a_points)
		{
			float total = 0.f;
			RE::NiPoint3 meanP;
			RE::NiPoint3 meanN;
			// Every point of the patch counts the same: by weight, a bone's tip would fit its own small curve
			for (const auto& point : a_points) {
				if (point.n.SqrLength() < .25f) {
					return std::nullopt;
				}
				meanP += point.p;
				meanN += point.n;
				total += 1.f;
			}
			if (total <= 0.f) {
				return std::nullopt;
			}
			meanP = meanP / total;
			meanN = meanN / total;
			float along = 0.f;
			float spread = 0.f;
			for (const auto& point : a_points) {
				const auto a = point.p - meanP;
				const auto b = point.n - meanN;
				along += a.Dot(b);
				spread += b.SqrLength();
			}
			// The normals hardly turn over the patch: flat, no depth to find
			if (spread / total < .02f) {
				return std::nullopt;
			}
			const float depth = along / spread;
			if (!(depth > 0.f)) {
				return std::nullopt;
			}
			return std::pair{ meanP - meanN * depth, depth };
		}

		// The value under which half the weight lies
		float WeightedMedian(std::vector<std::pair<float, float>>& a_values)
		{
			std::ranges::sort(a_values, {}, &std::pair<float, float>::first);
			float total = 0.f;
			for (const auto& value : a_values) {
				total += value.second;
			}
			float below = 0.f;
			for (const auto& [value, weight] : a_values) {
				below += weight;
				if (below >= total * .5f) {
					return value;
				}
			}
			return a_values.back().first;
		}

		RE::NiPoint3 Mean(const std::vector<Weighted>& a_points)
		{
			RE::NiPoint3 sum;
			float total = 0.f;
			for (const auto& point : a_points) {
				sum += point.p * point.w;
				total += point.w;
			}
			return sum / total;
		}

		// The direction the points spread along most, by power iteration on their weighted covariance
		RE::NiPoint3 MainAxis(const std::vector<Weighted>& a_points, const RE::NiPoint3& a_mean)
		{
			float c[3][3]{};
			for (const auto& point : a_points) {
				const float d[3]{ point.p.x - a_mean.x, point.p.y - a_mean.y, point.p.z - a_mean.z };
				for (int i = 0; i < 3; ++i) {
					for (int j = 0; j < 3; ++j) {
						c[i][j] += point.w * d[i] * d[j];
					}
				}
			}
			RE::NiPoint3 axis{ 1.f, 1.f, 1.f };
			for (int step = 0; step < 24; ++step) {
				RE::NiPoint3 next{ c[0][0] * axis.x + c[0][1] * axis.y + c[0][2] * axis.z, c[1][0] * axis.x + c[1][1] * axis.y + c[1][2] * axis.z, c[2][0] * axis.x + c[2][1] * axis.y + c[2][2] * axis.z };
				const float length = next.Length();
				if (length <= 0.f) {
					break;
				}
				axis = next / length;
			}
			return axis;
		}

		// The sphere the points lie on, by weighted least squares: |p|^2 = 2c.p + k, k = r^2 - |c|^2; none when they lie on
		// no sphere
		std::optional<std::pair<RE::NiPoint3, float>> SphereThrough(const std::vector<Weighted>& a_points)
		{
			double m[4][5]{};
			for (const auto& [p, w, n] : a_points) {
				const double row[4]{ 2.0 * p.x, 2.0 * p.y, 2.0 * p.z, 1.0 };
				const double target = static_cast<double>(p.x) * p.x + static_cast<double>(p.y) * p.y + static_cast<double>(p.z) * p.z;
				for (int i = 0; i < 4; ++i) {
					for (int j = 0; j < 4; ++j) {
						m[i][j] += w * row[i] * row[j];
					}
					m[i][4] += w * row[i] * target;
				}
			}
			for (int col = 0; col < 4; ++col) {
				int pivot = col;
				for (int r = col + 1; r < 4; ++r) {
					if (std::fabs(m[r][col]) > std::fabs(m[pivot][col])) {
						pivot = r;
					}
				}
				if (std::fabs(m[pivot][col]) < 1e-9) {
					return std::nullopt;
				}
				std::swap(m[col], m[pivot]);
				for (int r = 0; r < 4; ++r) {
					if (r != col) {
						const double f = m[r][col] / m[col][col];
						for (int c = col; c < 5; ++c) {
							m[r][c] -= f * m[col][c];
						}
					}
				}
			}
			const RE::NiPoint3 centre{ static_cast<float>(m[0][4] / m[0][0]), static_cast<float>(m[1][4] / m[1][1]), static_cast<float>(m[2][4] / m[2][2]) };
			const double squared = m[3][4] / m[3][3] + static_cast<double>(centre.x) * centre.x + static_cast<double>(centre.y) * centre.y + static_cast<double>(centre.z) * centre.z;
			if (!(squared > 0.0)) {
				return std::nullopt;
			}
			return std::pair{ centre, static_cast<float>(std::sqrt(squared)) };
		}

		// A sphere with the skin's own roundness, the patch of skin lying on it; held between the patch's median distance
		// from its middle and three times that, which a flat patch would overreach, and centred among the points otherwise
		Fitted FitSphere(const Bone& a_bone, const std::vector<Weighted>& a_points, float a_offset)
		{
			const auto mean = Mean(a_points);
			std::vector<std::pair<float, float>> distances;
			distances.reserve(a_points.size());
			for (const auto& point : a_points) {
				distances.emplace_back((point.p - mean).Length(), point.w);
			}
			const float spread = WeightedMedian(distances);
			auto centre = mean;
			float radius = spread;
			if (const auto inside = SphereInside(a_points); inside && inside->second <= spread * 2.f) {
				centre = inside->first;
				radius = inside->second;
			} else if (const auto sphere = SphereThrough(a_points); sphere && sphere->second >= spread && sphere->second <= spread * 1.5f) {
				centre = sphere->first;
				radius = sphere->second;
			}
			radius = (std::max)(radius + a_offset, 0.1f);
			return { a_bone.node, false, a_bone.bPushed, centre, centre, radius, radius };
		}

		// A capsule along the part's length, as wide as the skin's median distance from its axis, its ends within the part
		Fitted FitCapsule(const Bone& a_bone, const std::vector<Weighted>& a_points, float a_offset)
		{
			// The belly lies across the body's middle, as its bone does: side to side on the midline, whichever way its paint
			// leans
			const bool bAcross = std::string_view(a_bone.node) == "NPC Belly";
			auto mean = Mean(a_points);
			if (bAcross) {
				mean.x = 0.f;
			}
			const auto axis = bAcross ? RE::NiPoint3{ 1.f, 0.f, 0.f } : MainAxis(a_points, mean);
			std::vector<float> along;
			std::vector<std::pair<float, float>> across;
			along.reserve(a_points.size());
			across.reserve(a_points.size());
			for (const auto& point : a_points) {
				const auto d = point.p - mean;
				const float t = d.Dot(axis);
				along.push_back(t);
				across.emplace_back((d - axis * t).Length(), point.w);
			}
			const float radius = (std::max)(WeightedMedian(across) + a_offset, 0.1f);
			std::vector<float> sorted = along;
			std::ranges::sort(sorted);
			const float low = sorted[sorted.size() / 20];
			const float high = sorted[sorted.size() - 1 - sorted.size() / 20];
			// Each end as wide as the skin over its own third of the length: a limb tapers
			const auto endRadius = [&](bool a_bLow) {
				std::vector<std::pair<float, float>> closeBy;
				const float third = (high - low) / 3.f;
				for (std::size_t i = 0; i < along.size(); ++i) {
					if (a_bLow ? along[i] <= low + third : along[i] >= high - third) {
						closeBy.push_back(across[i]);
					}
				}
				return closeBy.size() >= kFewestVertices ? (std::max)(WeightedMedian(closeBy) + a_offset, 0.1f) : radius;
			};
			// Only a limb tapers: the belly is as round at both ends
			const float radiusLow = bAcross ? radius : endRadius(true);
			const float radiusHigh = bAcross ? radius : endRadius(false);
			// The caps reach past the ends by the radius
			float from = low + radiusLow;
			float to = high - radiusHigh;
			if (from > to) {
				from = to = (low + high) * .5f;
			}
			return { a_bone.node, true, a_bone.bPushed, mean + axis * from, mean + axis * to, radiusLow, radiusHigh };
		}

		// A vertex of the shape a breast lies in: skin space, with its normal and how much its side's breast bones carry it
		struct CloudPoint
		{
			RE::NiPoint3 p;
			RE::NiPoint3 n;
			float left = 0.f;
			float right = 0.f;
			float own[6]{};  // what each breast collider's bone carries, in the layout's order: the first six are the breasts
		};

		struct Placed
		{
			RE::NiPoint3 p;
			RE::NiPoint3 n;
			bool bBreast = false;
		};

		// The biggest sphere under a patch of skin: along the line from the patch straight in, the point farthest from any
		// skin while the nearest skin is still the bone's own, so the sphere sits under its own patch and stops where another
		// part of the body, the next breast bone's or the chest, is nearer
		std::optional<std::pair<RE::NiPoint3, float>> UnderPatch(const std::vector<Placed>& a_cloud, const RE::NiPoint3& a_patch, const RE::NiPoint3& a_inward)
		{
			std::optional<std::pair<RE::NiPoint3, float>> best;
			for (float t = .5f; t <= 14.f; t += .25f) {
				const auto at = a_patch + a_inward * t;
				float nearest = std::numeric_limits<float>::max();
				const Placed* closest = nullptr;
				for (const auto& point : a_cloud) {
					if (const float d = (point.p - at).SqrLength(); d < nearest) {
						nearest = d;
						closest = &point;
					}
				}
				// Out of the body, or nearer other skin than the bone's own: past its part of the breast, into the chest
				if (!closest || !closest->bBreast || (at - closest->p).Dot(closest->n) > 0.f) {
					break;
				}
				if (const float r = std::sqrt(nearest); !best || r > best->second) {
					best = std::pair{ at, r };
				}
			}
			if (!best || best->second <= .3f) {
				return std::nullopt;
			}
			return best;
		}

		// One skinned shape of the actor's naked body as the fit reads it, from the body's file at both weights: the partitions
		// and the skin data the loaded model owns, its bones' names, and the TRI DCMF morphs it through
		struct ShapeInput
		{
			RE::NiPointer<RE::NiSkinPartition> partition;
			RE::NiPointer<RE::NiSkinPartition> partitionHigh;  // the weight 100 body's, none when the file has one weight
			RE::NiPointer<RE::NiSkinData> data;
			std::vector<RE::BSFixedString> bones;  // the engine's pooled names, shared rather than copied
			std::shared_ptr<const std::vector<Scan::Tri::BodyTriShapeDeltas>> tri;
			std::size_t triShape = 0;
			std::vector<std::pair<std::uint32_t, std::uint32_t>> morphs;
		};

		struct FitInput
		{
			RE::FormID actor = 0;
			std::vector<ShapeInput> shapes;
			std::vector<NumberSample> weights;
			std::array<bool, kLayout.size()> present{};  // whether the skeleton has each layout bone
			float weight = 0.f;                          // the actor's, 0 to 1, between the body's two files
			float offset = 0.f;
			float breastOffset = 0.f;
		};

		// The naked body's models, loaded once per path and kept: the fit reads them, never what the actor wears
		std::unordered_map<std::string, RE::NiPointer<RE::NiNode>> g_bodies;

		RE::NiNode* BodyModel(const std::string& a_path)
		{
			if (const auto it = g_bodies.find(a_path); it != g_bodies.end()) {
				return it->second.get();
			}
			// The model database takes paths under the meshes folder with backslashes
			std::string demanded = a_path.starts_with("meshes/") ? a_path.substr(7) : a_path;
			std::ranges::replace(demanded, '/', '\\');
			RE::NiPointer<RE::NiNode> model;
			RE::BSModelDB::DBTraits::ArgsType args;
			if (const auto code = RE::BSModelDB::Demand(demanded.c_str(), model, args); code != RE::BSResource::ErrorCode::kNone || !model) {
				logger::warn("cbpc: could not load the body '{}' ({})", a_path, static_cast<int>(code));
				model = nullptr;
			}
			return (g_bodies[a_path] = model).get();
		}

		// The body file's two weights: the path as the skin names it, and its other weight beside it
		std::pair<std::string, std::string> BodyFiles(const std::string& a_path)
		{
			const auto low = a_path.size() > 6 && a_path.ends_with("_1.nif") ? a_path.substr(0, a_path.size() - 6) + "_0.nif" : a_path;
			const auto high = low.size() > 6 && low.ends_with("_0.nif") ? low.substr(0, low.size() - 6) + "_1.nif" : std::string{};
			return { low, high };
		}

		// Main thread, once per parts change: which layout bones the skeleton has
		std::array<bool, kLayout.size()> PresentIn(RE::NiAVObject* a_root)
		{
			std::array<bool, kLayout.size()> present{};
			for (std::size_t l = 0; l < kLayout.size(); ++l) {
				present[l] = a_root->GetObjectByName(kLayout[l].node) != nullptr;
			}
			return present;
		}

		// Main thread: what a fit needs, by reference: the naked body's shapes from its file, and the actor's weight
		std::optional<FitInput> Gather(RE::Actor* a_actor, const std::string& a_body, const std::vector<NumberSample>& a_weights, const std::array<bool, kLayout.size()>& a_present)
		{
			const auto [lowPath, highPath] = BodyFiles(a_body);
			auto* low = BodyModel(lowPath);
			auto* high = highPath.empty() ? nullptr : BodyModel(highPath);
			if (!low) {
				return std::nullopt;
			}
			FitInput input{ .actor = a_actor->GetFormID(), .weights = a_weights, .present = a_present, .weight = std::clamp(a_actor->GetWeight() / 100.f, 0.f, 1.f), .offset = Settings::fPatchCbpcSkinOffset,
				.breastOffset = Settings::fPatchCbpcBreastOffset };
			const auto skinned = [](RE::BSGeometry* a_geometry) -> RE::NiSkinInstance* {
				auto* skin = a_geometry->GetGeometryRuntimeData().skinInstance.get();
				auto* partition = skin ? skin->skinPartition.get() : nullptr;
				if (!skin || !skin->skinData || !skin->bones || !partition || partition->numPartitions == 0 || !partition->partitions[0].buffData || !partition->partitions[0].buffData->rawVertexData) {
					return nullptr;
				}
				return partition->partitions[0].buffData->vertexDesc.HasFlag(RE::BSGraphics::Vertex::Flags::VF_SKINNED) ? skin : nullptr;
			};
			RE::BSVisit::TraverseScenegraphGeometries(low, [&](RE::BSGeometry* a_geometry) {
				auto* skin = skinned(a_geometry);
				if (!skin) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* partition = skin->skinPartition.get();
				ShapeInput shape{ .partition = RE::NiPointer(partition), .data = skin->skinData };
				// The same shape at weight 100, by name and vertex count
				if (high) {
					RE::BSVisit::TraverseScenegraphGeometries(high, [&](RE::BSGeometry* a_other) {
						auto* otherSkin = skinned(a_other);
						if (otherSkin && a_other->name == a_geometry->name && otherSkin->skinPartition->vertexCount == partition->vertexCount) {
							shape.partitionHigh = otherSkin->skinPartition;
							return RE::BSVisit::BSVisitControl::kStop;
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
				shape.bones.reserve(skin->skinData->GetBoneCount());
				for (std::uint32_t i = 0; i < skin->skinData->GetBoneCount(); ++i) {
					const auto* bone = skin->bones[i];
					shape.bones.push_back(bone ? bone->name : RE::BSFixedString());
				}
				if (auto morphable = BodyMorph::MorphableOf(a_geometry, partition->vertexCount)) {
					shape.tri = std::move(morphable->tri);
					shape.triShape = morphable->shape;
					shape.morphs = std::move(morphable->morphs);
				}
				input.shapes.push_back(std::move(shape));
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return input;
		}

		// Any thread: every layout bone's vertices in its own space, from the shape that has most of them, and the colliders
		// fitted to them
		std::vector<Fitted> Compute(const FitInput& a_input)
		{
			std::array<std::vector<Weighted>, kLayout.size()> best;
			std::array<std::vector<Weighted>, kLayout.size()> found;
			// The shape the breasts lie in, every vertex of it, and each breast collider's bone transform in that shape
			std::vector<CloudPoint> cloud;
			std::size_t cloudBreasts = 0;
			std::array<std::optional<RE::NiTransform>, kLayout.size()> breastToBone;
			for (const auto& input : a_input.shapes) {
				auto* partition = input.partition.get();
				auto* data = input.data.get();
				auto desc = partition->partitions[0].buffData->vertexDesc;
				using A = RE::BSGraphics::Vertex::Attribute;
				const auto stride = desc.GetSize();
				const auto skinning = desc.GetAttributeOffset(A::VA_SKINNING);
				// Positions sit first, as halves unless the next attribute starts past 8 bytes
				std::uint32_t next = stride;
				for (const auto attribute : { A::VA_TEXCOORD0, A::VA_TEXCOORD1, A::VA_NORMAL, A::VA_BINORMAL, A::VA_COLOR, A::VA_SKINNING, A::VA_EYEDATA }) {
					if (const auto offset = desc.GetAttributeOffset(attribute); offset != 0 && offset < next) {
						next = offset;
					}
				}
				const bool bHalves = next <= 8;
				const auto vertexCount = partition->vertexCount;
				const auto* raw = partition->partitions[0].buffData->rawVertexData;
				const auto* rawHigh = input.partitionHigh ? input.partitionHigh->partitions[0].buffData->rawVertexData : nullptr;
				const auto boneCount = (std::min)(static_cast<std::uint32_t>(input.bones.size()), data->GetBoneCount());

				// Which layout bone each of the shape's bones belongs to, and where each layout bone is in the shape's list
				std::vector<int> layoutOf(boneCount, -1);
				std::array<int, kLayout.size()> own;
				own.fill(-1);
				std::vector<std::uint8_t> chainOf(boneCount, 0);  // the breast chain a bone is in: 1 left, 2 right
				for (std::uint32_t i = 0; i < boneCount; ++i) {
					const std::string_view name = input.bones[i].c_str() ? input.bones[i].c_str() : "";
					for (std::size_t l = 0; !name.empty() && l < kLayout.size(); ++l) {
						if (name.size() == std::string_view(kLayout[l].node).size() && ContainsNoCase(name, kLayout[l].node)) {
							own[l] = static_cast<int>(i);
						}
						if (layoutOf[i] < 0 && BelongsTo(name, kLayout[l])) {
							layoutOf[i] = static_cast<int>(l);
						}
					}
					if (ContainsNoCase(name, "Breast")) {
						chainOf[i] = name.starts_with("L ") || name.find(" L ") != std::string_view::npos ? 1 : name.starts_with("R ") || name.find(" R ") != std::string_view::npos ? 2 : 0;
					}
				}
				// A helper counts only when the shape also has the bone its collider sits on
				bool bAny = false;
				for (auto& layout : layoutOf) {
					if (layout >= 0 && own[layout] < 0) {
						layout = -1;
					}
					bAny = bAny || layout >= 0;
				}
				if (!bAny) {
					continue;
				}

				// The shape's positions as the settled body has them, and its normals worked out from its triangles: a body
				// with a model-space normal map keeps none, and a morph would leave stored ones behind
				std::vector<RE::NiPoint3> positions(vertexCount);
				for (std::uint32_t v = 0; v < vertexCount; ++v) {
					BodyMorph::ReadVertexPosition(raw + static_cast<std::size_t>(v) * stride, bHalves, positions[v].x, positions[v].y, positions[v].z);
					// Between the body's two weights, as the game builds it
					if (rawHigh) {
						RE::NiPoint3 heavy;
						BodyMorph::ReadVertexPosition(rawHigh + static_cast<std::size_t>(v) * stride, bHalves, heavy.x, heavy.y, heavy.z);
						positions[v] += (heavy - positions[v]) * a_input.weight;
					}
				}
				if (input.tri && input.triShape < input.tri->size()) {
					const auto& morphs = (*input.tri)[input.triShape].morphs;
					auto weight = a_input.weights.begin();
					for (const auto& [index, morph] : input.morphs) {
						while (weight != a_input.weights.end() && weight->index < index) {
							++weight;
						}
						if (weight == a_input.weights.end()) {
							break;
						}
						if (weight->index != index || weight->value == 0.f) {
							continue;
						}
						for (const auto& delta : morphs[morph].deltas) {
							if (delta.index < vertexCount) {
								positions[delta.index] += RE::NiPoint3{ delta.x, delta.y, delta.z } * weight->value;
							}
						}
					}
				}
				std::vector<RE::NiPoint3> normals(vertexCount);
				bool bHaveNormals = false;
				for (std::uint32_t i = 0; i < partition->numPartitions; ++i) {
					const auto& part = partition->partitions[i];
					if (!part.triList) {
						continue;
					}
					for (std::uint32_t t = 0; t < part.triangles; ++t) {
						const auto a = part.triList[t * 3];
						const auto b = part.triList[t * 3 + 1];
						const auto c = part.triList[t * 3 + 2];
						if (a >= vertexCount || b >= vertexCount || c >= vertexCount) {
							continue;
						}
						const auto face = (positions[b] - positions[a]).Cross(positions[c] - positions[a]);
						normals[a] += face;
						normals[b] += face;
						normals[c] += face;
						bHaveNormals = true;
					}
				}
				for (auto& normal : normals) {
					if (const float length = normal.Length(); length > 0.f) {
						normal = normal / length;
					}
				}
				// Out of the body whichever way the triangles wind: most normals face away from the shape's middle
				if (bHaveNormals) {
					RE::NiPoint3 middle;
					for (const auto& position : positions) {
						middle += position;
					}
					middle = middle / static_cast<float>((std::max)(vertexCount, 1u));
					std::int64_t outward = 0;
					for (std::uint32_t v = 0; v < vertexCount; ++v) {
						outward += (positions[v] - middle).Dot(normals[v]) >= 0.f ? 1 : -1;
					}
					if (outward < 0) {
						for (auto& normal : normals) {
							normal = normal * -1.f;
						}
					}
				}

				for (auto& list : found) {
					list.clear();
				}
				std::vector<CloudPoint> shapeCloud;
				shapeCloud.reserve(vertexCount);
				std::size_t shapeBreasts = 0;
				for (std::uint32_t v = 0; v < vertexCount; ++v) {
					const auto* vertex = raw + static_cast<std::size_t>(v) * stride;
					std::uint16_t halves[4];
					std::memcpy(halves, vertex + skinning, sizeof(halves));
					const auto* indices = vertex + skinning + 8;
					// What each layout bone, with its helpers, carries of the vertex
					std::array<std::pair<int, float>, 4> shares{};
					std::size_t count = 0;
					CloudPoint point{ .p = positions[v], .n = normals[v] };
					for (int k = 0; k < 4; ++k) {
						const auto index = indices[k];
						const float weight = BodyMorph::Half(halves[k]);
						if (weight <= 0.f || index >= boneCount) {
							continue;
						}
						if (chainOf[index] != 0) {
							(chainOf[index] == 1 ? point.left : point.right) += weight;
						}
						if (layoutOf[index] < 0) {
							continue;
						}
						if (layoutOf[index] < 6) {
							point.own[layoutOf[index]] += weight;
						}
						auto share = std::ranges::find(shares.begin(), shares.begin() + count, layoutOf[index], &std::pair<int, float>::first);
						if (share == shares.begin() + count) {
							*share = { layoutOf[index], 0.f };
							++count;
						}
						share->second += weight;
					}
					shapeBreasts += point.left >= .25f || point.right >= .25f;
					if (std::isfinite(point.p.x) && std::isfinite(point.p.y) && std::isfinite(point.p.z)) {
						shapeCloud.push_back(point);
					}
					for (std::size_t s = 0; s < count; ++s) {
						const auto [layout, weight] = shares[s];
						if (weight < .05f) {
							continue;
						}
						const auto& toBone = data->GetBoneDataSkinToBone(static_cast<std::uint32_t>(own[layout]));
						const auto local = toBone * positions[v];
						if (std::isfinite(local.x) && std::isfinite(local.y) && std::isfinite(local.z)) {
							found[layout].push_back({ local, weight, toBone.rotate * normals[v] });
						}
					}
				}
				for (std::size_t l = 0; l < kLayout.size(); ++l) {
					if (found[l].size() > best[l].size()) {
						best[l] = std::move(found[l]);
					}
				}
				if (bHaveNormals && shapeBreasts > cloudBreasts) {
					cloudBreasts = shapeBreasts;
					cloud = std::move(shapeCloud);
					for (std::size_t l = 0; l < kLayout.size(); ++l) {
						breastToBone[l] = IsBreast(kLayout[l].node) && own[l] >= 0 ? std::optional(data->GetBoneDataSkinToBone(static_cast<std::uint32_t>(own[l]))) : std::nullopt;
					}
				}
			}

			std::vector<Fitted> fitted;
			for (std::size_t l = 0; l < kLayout.size(); ++l) {
				const auto& bone = kLayout[l];
				if (bone.bFixed) {
					if (a_input.present[l]) {
						const RE::NiPoint3 a{ bone.a[0], bone.a[1], bone.a[2] };
						const RE::NiPoint3 b = bone.bCapsule ? RE::NiPoint3{ bone.b[0], bone.b[1], bone.b[2] } : a;
						fitted.push_back({ .node = bone.node, .bCapsule = bone.bCapsule, .bPushed = bone.bPushed, .a = a, .b = b, .radiusA = bone.radiusA, .radiusB = bone.bCapsule ? bone.radiusB : bone.radiusA, .bPushes = bone.bPushes });
					}
					continue;
				}
				// A vertex counts when it carries half the most this bone carries anywhere, a quarter at most: 3BA's butt and
				// belly carry little anywhere
				if (!best[l].empty()) {
					float strongest = 0.f;
					for (const auto& point : best[l]) {
						strongest = (std::max)(strongest, point.w);
					}
					const float cut = (std::min)(.25f, strongest * .5f);
					std::erase_if(best[l], [&](const Weighted& a_point) { return a_point.w < cut; });
				}
				if (best[l].size() < kFewestVertices) {
					continue;
				}
				const float offset = IsBreast(bone.node) ? a_input.breastOffset : a_input.offset;
				fitted.push_back(bone.bCapsule ? FitCapsule(bone, best[l], offset) : FitSphere(bone, best[l], offset));
				// A breast's sphere the biggest under its own patch, in place of one fitted to the patch's skin
				if (IsBreast(bone.node) && breastToBone[l] && !cloud.empty()) {
					const auto& toBone = *breastToBone[l];
					const auto patch = Mean(best[l]);
					RE::NiPoint3 facing;
					for (const auto& point : best[l]) {
						facing += point.n * point.w;
					}
					const float facingLength = facing.Length();
					// The skin this bone itself is painted on: half the most it carries anywhere, a quarter at most
					float strongest = 0.f;
					for (const auto& point : cloud) {
						strongest = (std::max)(strongest, point.own[l]);
					}
					const float cut = (std::min)(.25f, strongest * .5f);
					std::vector<Placed> placed;
					placed.reserve(cloud.size() / 2);
					for (const auto& point : cloud) {
						const auto p = toBone * point.p;
						if ((p - patch).SqrLength() <= 14.f * 14.f) {
							placed.push_back({ p, toBone.rotate * point.n, point.own[l] >= cut && strongest > 0.f });
						}
					}
					if (const auto inscribed = facingLength > 0.f ? UnderPatch(placed, patch, facing / -facingLength) : std::nullopt) {
						auto& last = fitted.back();
						last.a = last.b = inscribed->first;
						last.radiusA = last.radiusB = (std::max)(inscribed->second + offset, 0.1f);
					}
				}
			}
			return fitted;
		}

		// ---- CBPC through its Papyrus functions ----

		template <class... Args>
		void Call(std::string_view a_function, RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> a_callback, Args... a_args)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || !vm->DispatchStaticCall(kScript, a_function, RE::MakeFunctionArguments(std::move(a_args)...), a_callback)) {
				if (static bool bSaid = false; !bSaid) {
					bSaid = true;
					logger::error("cbpc: {}.{} could not be called; is CBPC's script installed?", kScript, a_function);
				}
			}
		}

		// The sentinel's detach: false means CBPC no longer holds what DCMF attached
		class Missing : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit Missing(std::shared_ptr<std::atomic<bool>> a_flag) :
				_flag(std::move(a_flag)) {}

			void operator()(RE::BSScript::Variable a_result) override
			{
				if (a_result.IsBool() && !a_result.GetBool()) {
					*_flag = true;
				}
			}

			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::shared_ptr<std::atomic<bool>> _flag;
		};

		std::vector<float> Point(const RE::NiPoint3& a_point)
		{
			return { a_point.x, a_point.y, a_point.z };
		}

		void DetachAll(RE::Actor* a_actor)
		{
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			for (const auto& bone : kLayout) {
				for (const std::uint32_t type : { 0u, 1u }) {
					Call("DetachCollider", none, a_actor, RE::BSFixedString(bone.node), type, kIndex, false);
					if (bone.bPushed) {
						Call("DetachCollider", none, a_actor, RE::BSFixedString(bone.node), type, kIndex, true);
					}
				}
			}
			Call("DetachCollider", none, a_actor, RE::BSFixedString(kSentinelNode), 0u, kSentinelIndex, false);
		}

		// Ours taken off and the fitted put on, the sentinel with them
		void Send(RE::Actor* a_actor, const std::vector<Fitted>& a_fitted)
		{
			DetachAll(a_actor);
			const RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			for (const auto& fitted : a_fitted) {
				for (const bool bAffected : { false, true }) {
					if ((bAffected && !fitted.bPushed) || (!bAffected && !fitted.bPushes)) {
						continue;
					}
					if (fitted.bCapsule) {
						Call("AttachColliderCapsule", none, a_actor, RE::BSFixedString(fitted.node), Point(fitted.a), fitted.radiusA, Point(fitted.b), fitted.radiusB, 1.f, kIndex, bAffected);
					} else {
						Call("AttachColliderSphere", none, a_actor, RE::BSFixedString(fitted.node), Point(fitted.a), fitted.radiusA, 1.f, kIndex, bAffected);
					}
				}
			}
			Call("AttachColliderSphere", none, a_actor, RE::BSFixedString(kSentinelNode), std::vector<float>{ 0.f, 0.f, 0.f }, 0.01f, 1.f, kSentinelIndex, false);
		}

		void Draw(RE::FormID a_id, RE::Actor* a_actor, const std::vector<Fitted>& a_fitted)
		{
			std::vector<UI::ColliderDraw::Collider> drawn;
			if (a_actor) {
				for (const auto& fitted : a_fitted) {
					drawn.push_back({ .actor = a_actor->GetHandle(), .node = fitted.node, .end1 = fitted.a, .radius1 = fitted.radiusA, .end2 = fitted.b, .radius2 = fitted.radiusB, .bCapsule = fitted.bCapsule, .bAffected = fitted.bPushed });
				}
				for (const auto* bone : kBreastBones) {
					drawn.push_back({ .actor = a_actor->GetHandle(), .node = bone, .radius1 = .4f, .bBone = true });
				}
				// Each breast bone as a line from its parent: the chain's direction and each link's length
				if (auto* root = a_actor->Get3D(false)) {
					for (const auto* bone : kBreastBones) {
						const auto* node = root->GetObjectByName(bone);
						const auto* parent = node ? node->parent : nullptr;
						if (parent && parent->name.c_str() && *parent->name.c_str()) {
							drawn.push_back({ .actor = a_actor->GetHandle(), .node = parent->name.c_str(), .radius1 = .12f, .radius2 = .12f, .bCapsule = true, .bBone = true, .endNode = bone });
						}
					}
				}
			}
			UI::ColliderDraw::Set(std::format("cbpc.{:08X}", a_id), std::move(drawn));
		}

		bool SameWeights(const std::vector<NumberSample>& a_left, const std::vector<NumberSample>& a_right)
		{
			return a_left.size() == a_right.size() && std::equal(a_left.begin(), a_left.end(), a_right.begin(), [](const NumberSample& a, const NumberSample& b) { return a.index == b.index && a.value == b.value; });
		}

		struct Entry
		{
			RE::FormID actor = 0;
			std::vector<Fitted> fitted;
			std::vector<NumberSample> weights;
			std::uint32_t partsGeneration = 0;
			float offset = 0.f;
			float breastOffset = 0.f;
			std::string body;  // the naked body's file the fit read
			float actorWeight = -1.f;
			std::array<bool, kLayout.size()> present{};
			std::uint32_t presentGeneration = 0;
			bool bPresentRead = false;
			bool bFitted = false;
			bool bAsked = false;
			std::future<std::vector<Fitted>> pending;
			Clock::time_point checkedAt{};
			std::shared_ptr<std::atomic<bool>> bMissing = std::make_shared<std::atomic<bool>>(true);
		};
		std::unordered_map<const ActorState::State*, Entry> g_entries;
		// Fits still running for actors let go: kept until done, since dropping one would wait for it
		std::vector<std::future<std::vector<Fitted>>> g_orphans;


		bool Active()
		{
			return Settings::bPatchCbpc && Settings::bCbpcInstalled;
		}

		// DCMF.esp's faction: an actor in it gets DCMF's collision config, and every other keeps CBPC's own
		RE::TESFaction* Faction()
		{
			static RE::TESFaction* faction = [] {
				auto* dataHandler = RE::TESDataHandler::GetSingleton();
				auto* found = dataHandler ? dataHandler->LookupForm<RE::TESFaction>(0x800, "DCMF.esp") : nullptr;
				if (!found) {
					logger::error("cbpc: DCMF.esp is not active; the fitted collider patch fits no one until it is");
				}
				return found;
			}();
			return faction;
		}

		// In or out of the faction, CBPC choosing the actor's config again
		void Enrol(RE::Actor* a_actor, bool a_bIn)
		{
			auto* faction = Faction();
			if (!faction || a_actor->IsInFaction(faction) == a_bIn) {
				return;
			}
			if (a_bIn) {
				a_actor->AddToFaction(faction, 0);
			} else {
				a_actor->RemoveFromFaction(faction);
			}
			Call("RefreshActorCollisionSettings", {}, a_actor);
		}

		void Forget(const ActorState::State* a_state)
		{
			const auto it = g_entries.find(a_state);
			if (it == g_entries.end()) {
				return;
			}
			if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(it->second.actor); actor && Settings::bCbpcInstalled) {
				DetachAll(actor);
				Enrol(actor, false);
			}
			Draw(it->second.actor, nullptr, {});
			if (it->second.pending.valid()) {
				g_orphans.push_back(std::move(it->second.pending));
			}
			g_entries.erase(it);
		}
	}

	void SyncConfigFile()
	{
		std::error_code error;
		const bool bInstalled = std::filesystem::exists("Data/SKSE/Plugins/cbp.dll", error);
		// Off unless the user turned it on
		const bool bOn = Settings::bPatchCbpcChosen && Settings::bPatchCbpc;
		if (bOn && bInstalled) {
			const auto text = ConfigText();
			std::string current;
			if (std::ifstream in(kConfigPath, std::ios::binary); in) {
				current.assign(std::istreambuf_iterator<char>(in), {});
			}
			if (current != text) {
				std::ofstream(kConfigPath, std::ios::binary | std::ios::trunc) << text;
				logger::info("cbpc: wrote {}", kConfigPath);
			}
		} else if (std::filesystem::exists(kConfigPath, error)) {
			std::filesystem::remove(kConfigPath, error);
			logger::info("cbpc: removed {}{}", kConfigPath, error ? std::format(" -- {}", error.message()) : "");
		}
	}

	void Frame()
	{
		std::erase_if(g_orphans, [](const auto& a_fit) { return a_fit.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
		static bool bWasActive = Active();
		const bool bActive = Active();
		if (bActive == bWasActive) {
			return;
		}
		bWasActive = bActive;
		// CBPC reads the config again and drops every actor: ours are attached again as each misses them
		SyncConfigFile();
		if (Settings::bCbpcInstalled) {
			Call("ReloadConfig", {});
		}
		if (!bActive) {
			while (!g_entries.empty()) {
				Forget(g_entries.begin()->first);
			}
		}
		logger::info("cbpc: the fitted collider patch is {}", bActive ? "on" : "off");
	}

	void Update(ActorState::State& a_state)
	{
		if (!Active()) {
			return;
		}
		const auto refr = a_state.handle.get();
		auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
		auto* base = actor ? actor->GetActorBase() : nullptr;
		const auto parts = a_state.parts.With([](const auto& a_parts) { return a_parts; });
		if (!actor || !base || base->GetSex() != RE::SEX::kFemale || !actor->Is3DLoaded() || !parts || !parts->bLoaded) {
			// Why an actor is left without colliders, said once per actor
			static std::unordered_set<RE::FormID> said;
			if (actor && said.insert(actor->GetFormID()).second) {
				logger::info("cbpc: {:08X} not fitted: {}", actor->GetFormID(), !base ? "no base" : base->GetSex() != RE::SEX::kFemale ? "not female" : !actor->Is3DLoaded() ? "3D not loaded" : !parts ? "parts not read" : "parts not loaded");
			}
			Forget(&a_state);
			return;
		}
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		static const std::vector<NumberSample> none;
		const auto& weights = sampled && sampled->settledBody ? *sampled->settledBody : none;

		auto& entry = g_entries[&a_state];
		entry.actor = actor->GetFormID();
		const auto now = Clock::now();
		// Fitted again off the main thread when the settled body, the parts, an offset or the bound shapes changed; one fit
		// at a time, a change during it answered by the next
		const auto body = BodyTypes::BodyPathOf(actor);
		const float actorWeight = actor->GetWeight();
		const bool bStale = !entry.bAsked || entry.partsGeneration != parts->generation || entry.offset != Settings::fPatchCbpcSkinOffset || entry.breastOffset != Settings::fPatchCbpcBreastOffset ||
		                    entry.body != body || entry.actorWeight != actorWeight || !SameWeights(entry.weights, weights);
		if (bStale && !entry.pending.valid()) {
			if (!entry.bPresentRead || entry.presentGeneration != parts->generation) {
				if (auto* root = actor->Get3D(false)) {
					entry.present = PresentIn(root);
					entry.presentGeneration = parts->generation;
					entry.bPresentRead = true;
				}
			}
			if (auto input = body.empty() ? std::nullopt : Gather(actor, body, weights, entry.present)) {
				entry.pending = std::async(std::launch::async, [input = std::move(*input)]() { return Compute(input); });
				entry.weights = weights;
				entry.partsGeneration = parts->generation;
				entry.offset = Settings::fPatchCbpcSkinOffset;
				entry.breastOffset = Settings::fPatchCbpcBreastOffset;
				entry.body = body;
				entry.actorWeight = actorWeight;
				entry.bAsked = true;
			}
		}
		if (entry.pending.valid() && entry.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			auto fitted = entry.pending.get();
			const bool bChanged = !entry.bFitted || fitted.size() != entry.fitted.size() ||
			                      !std::ranges::equal(fitted, entry.fitted, [](const Fitted& a, const Fitted& b) { return a.node == b.node && a.a == b.a && a.b == b.b && a.radiusA == b.radiusA && a.radiusB == b.radiusB; });
			if (!entry.bFitted) {
				logger::info("cbpc: {:08X}: {} of {} colliders fitted to the body", entry.actor, fitted.size(), kLayout.size());
			}
			entry.fitted = std::move(fitted);
			entry.bFitted = true;
			if (bChanged) {
				*entry.bMissing = true;
				Draw(entry.actor, actor, entry.fitted);
			}
		}
		if (!entry.bFitted) {
			return;
		}
		if (entry.bMissing->exchange(false)) {
			// Into the faction first, so CBPC gives the actor DCMF's config before the colliders go on
			Enrol(actor, true);
			Send(actor, entry.fitted);
			entry.checkedAt = now;
			return;
		}
		// Whether CBPC still holds them: a cell change or a reload drops every actor's
		if (now - entry.checkedAt >= kCheckEvery) {
			entry.checkedAt = now;
			Call("DetachCollider", RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>(new Missing(entry.bMissing)), static_cast<RE::Actor*>(actor), RE::BSFixedString(kSentinelNode), 0u, kSentinelIndex, false);
			Call("AttachColliderSphere", {}, static_cast<RE::Actor*>(actor), RE::BSFixedString(kSentinelNode), std::vector<float>{ 0.f, 0.f, 0.f }, 0.01f, 1.f, kSentinelIndex, false);
		}
	}

	void Release(ActorState::State& a_state)
	{
		Forget(&a_state);
	}


}
