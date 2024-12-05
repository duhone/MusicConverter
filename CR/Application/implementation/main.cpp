#include <fmt/format.h>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <simdjson.h>

import CR.Engine;

import std;

namespace cep = CR::Engine::Platform;

namespace fs = std::filesystem;

const fs::path c_configPath{"config.json"};

fs::path sourcePath;
fs::path destPath;

void LoadConfig() {
	if(fs::exists(c_configPath)) {
		simdjson::ondemand::parser parser;
		auto json = simdjson::padded_string::load(c_configPath.string());

		simdjson::ondemand::document doc = parser.iterate(json);
		sourcePath                       = std::string_view(doc["source_path"]);
		destPath                         = std::string_view(doc["dest_path"]);
	}
}

void SaveConfig() {
	constexpr auto c_outputFormat = R"({{"source_path":"{}", "dest_path":"{}"}})";

	auto outputString =
	    fmt::format(fmt::runtime(c_outputFormat), sourcePath.string(), destPath.string());

	std::ofstream outputFile(c_configPath);
	outputFile << outputString;
}

int main(int, char**) {
	using namespace ftxui;

	fs::current_path(cep::GetCurrentProcessPath());

	LoadConfig();

	// Define the document
	Element document = hbox({
	    text("left") | border,
	    text("middle") | border | flex,
	    text("right") | border,
	});

	auto screen = Screen::Create(Dimension::Full(),          // Width
	                             Dimension::Fit(document)    // Height
	);
	Render(screen, document);
	screen.Print();

	SaveConfig();

	return EXIT_SUCCESS;
}