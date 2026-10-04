#include "Scan.h"
#include "Xml.h"

namespace Scan
{
	namespace
	{
		// BodySlide's CalienteTools/BodySlide/SliderCategories/*.xml
		class BodySlideSliderCategories : public ISource
		{
		public:
			std::string_view GetName() const override { return "BodySlide slider categories"sv; }
			Target GetTarget() const override { return Target::kBodySlideCategories; }

			void Run(Context& a_context) override
			{
				const auto files = Files::List("CalienteTools/BodySlide/SliderCategories"sv, ".xml"sv, true);
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
					for (const auto& category : root.children) {
						if (category.name != "Category") {
							continue;
						}
						BodySlideCategory out;
						out.name = category.Attribute("name");
						for (const auto& slider : category.children) {
							if (slider.name != "Slider") {
								continue;
							}
							Morph morph;
							morph.name = slider.Attribute("name");
							morph.display = slider.Attribute("displayname");
							morph.foundBy.push_back(provenance);
							out.sliders.push_back(std::move(morph));
						}
						out.foundBy.push_back(provenance);
						a_context.out.categories.push_back(std::move(out));
					}
				}
			}
		};
	}

	void AddCategorySources(Scanner& a_scanner)
	{
		a_scanner.AddSource(std::make_unique<BodySlideSliderCategories>());
	}
}
