#include "Scan.h"
#include "Xml.h"

#include <charconv>

namespace Scan
{
	namespace
	{
		// BodySlide's CalienteTools/BodySlide/SliderPresets/*.xml: a preset is a value per slider at each weight
		class BodySlidePresets : public ISource
		{
		public:
			std::string_view GetName() const override { return "BodySlide presets"sv; }
			Target GetTarget() const override { return Target::kBodyPresets; }

			void Run(Context& a_context) override
			{
				const auto files = Files::List("CalienteTools/BodySlide/SliderPresets"sv, ".xml"sv, true);
				a_context.progress.total = static_cast<uint32_t>(files.size());

				for (const auto& file : files) {
					++a_context.progress.done;

					const auto origin = Files::Normalize(file.generic_string());
					Xml::Node root;
					std::string error;
					if (!Xml::Parse(Files::ReadText(file), root, error)) {
						a_context.Report(origin, error);
						continue;
					}

					const Provenance provenance{ std::string(GetName()), origin };
					for (const auto& preset : root.children) {
						if (preset.name != "Preset") {
							continue;
						}
						BodyPreset out;
						out.name = preset.Attribute("name");
						out.set = preset.Attribute("set");
						out.origin = origin;
						for (const auto& child : preset.children) {
							if (child.name == "Group") {
								out.groups.emplace_back(child.Attribute("name"));
							} else if (child.name == "SetSlider") {
								const auto name = child.Attribute("name");
								const auto text = child.Attribute("value");
								float value = 0.f;
								std::from_chars(text.data(), text.data() + text.size(), value);
								auto slider = std::ranges::find(out.sliders, name, &BodyPreset::Slider::name);
								if (slider == out.sliders.end()) {
									slider = out.sliders.insert(out.sliders.end(), { std::string(name), 0.f, 0.f });
								}
								(child.Attribute("size") == "small" ? slider->atZero : slider->atHundred) = value;
							}
						}
						out.foundBy.push_back(provenance);
						a_context.out.bodyPresets.push_back(std::move(out));
					}
				}
			}
		};
	}

	void AddPresetSources(Scanner& a_scanner)
	{
		a_scanner.AddSource(std::make_unique<BodySlidePresets>());
	}
}
