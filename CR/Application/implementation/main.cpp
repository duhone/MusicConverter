#include <fmt/format.h>
#include <simdjson.h>

#define GLAD_GL_IMPLEMENTATION
#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

import CR.Engine;

import std;

namespace cep = CR::Engine::Platform;

namespace fs = std::filesystem;

enum class AppState { Idle, Converting, Cancelling };

const fs::path c_configPath{"config.json"};

AppState appState{AppState::Idle};

fs::path sourcePath;
fs::path destPath;
std::string sourcePathString;
std::string destPathString;

std::mutex dataMutex;
float convertProgress{};
std::deque<std::string> Log;
std::vector<std::move_only_function<void()>> workQueue;

GLFWwindow* window{};

std::string GetLog() {
	std::string result;
	for(const auto& line : Log) {
		if(!result.empty()) { result += '\n'; }
		result += line;
	}
	return result;
}

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

void StartConversion() {}

void CancelConversion() {}

void DrawUI() {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	glfwSetWindowTitle(window, "Music Converter");
	ImGui::SetNextWindowPos({0, 0});
	int tmpWidth, tmpHeight;
	glfwGetWindowSize(window, &tmpWidth, &tmpHeight);
	ImGui::SetNextWindowSize({(float)tmpWidth, (float)tmpHeight});
	bool tmpOpen;
	if(ImGui::Begin("###UI", &tmpOpen,
	                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
	                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
	                    ImGuiWindowFlags_NoSavedSettings)) {
		if(ImGui::IsWindowAppearing()) {
			glfwSetWindowSize(window, 1280, 720);
			glfwSetWindowAttrib(window, GLFW_RESIZABLE, true);
			glfwSetWindowAttrib(window, GLFW_DECORATED, true);
		}

		ImGui::SetNextItemWidth(1000);
		ImGui::InputText("Source Path", &sourcePathString, ImGuiInputTextFlags_CharsNoBlank);
		if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("Path to lossless source music files");

		ImGui::SetNextItemWidth(1000);
		ImGui::InputText("Destination Path", &destPathString, ImGuiInputTextFlags_CharsNoBlank);
		if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
			ImGui::SetTooltip("Path were mp3 files will be saved");

		ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

		{
			if(appState == AppState::Idle) {
				if(ImGui::Button("Convert Files", {0, 0})) {
					appState = AppState::Converting;
					StartConversion();
				}

			} else if(appState == AppState::Converting) {
				if(ImGui::Button("Cancel Conversion", {0, 0})) {
					appState = AppState::Cancelling;
					CancelConversion();
				}
			} else if(appState == AppState::Cancelling) {
				ImGui::BeginDisabled();
				ImGui::Button("Canceling", {0, 0});
				ImGui::EndDisabled();

				std::scoped_lock loc(dataMutex);
				if(workQueue.empty()) { appState = AppState::Idle; }
			}

			ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal);

			ImGui::ProgressBar(convertProgress, {1260, 0}, nullptr);

			ImGui::AlignTextToFramePadding();
			std::string log = GetLog();
			ImGui::InputTextMultiline("##log_text", &log, ImVec2(1260, 540),
			                          ImGuiInputTextFlags_ReadOnly);
		}

		ImGui::End();
	}
	ImGui::PopStyleVar();
}

int main(int, char**) {
	fs::current_path(cep::GetCurrentProcessPath());

	LoadConfig();

	if(!glfwInit()) { return EXIT_FAILURE; }

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	window = glfwCreateWindow(1280, 720, "Music Converter", nullptr, nullptr);
	if(!window) { return EXIT_FAILURE; }
	glfwMakeContextCurrent(window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init();

	ImGui::GetIO().FontGlobalScale = 2.0f;

	sourcePathString = sourcePath.string();
	destPathString   = destPath.string();

	while(!glfwWindowShouldClose(window)) {
		glfwPollEvents();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		DrawUI();

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

	sourcePath = sourcePathString;
	destPath   = destPathString;

	SaveConfig();

	return EXIT_SUCCESS;
}