#include <fmt/format.h>
#include <simdjson.h>

#define GLAD_GL_IMPLEMENTATION
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

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
	fs::current_path(cep::GetCurrentProcessPath());

	LoadConfig();

	if(!glfwInit()) { return EXIT_FAILURE; }

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	GLFWwindow* window = glfwCreateWindow(800, 600, "Music Converter", nullptr, nullptr);
	if(!window) { return EXIT_FAILURE; }
	glfwMakeContextCurrent(window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init();

	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::ShowDemoWindow();

		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);
	glfwTerminate();

	SaveConfig();

	return EXIT_SUCCESS;
}