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

using namespace std::literals;

enum class AppState { Idle, Converting, Cancelling };

const fs::path c_configPath{"config.json"};

AppState appState{AppState::Idle};

fs::path sourcePath;
fs::path destPath;
std::string sourcePathString;
std::string destPathString;

std::mutex dataMutex;
float convertProgress{};
std::string Operation;
std::mutex OperationMutex;
std::mutex ErrorLogMutex;
std::deque<std::string> ErrorLog;
std::deque<std::move_only_function<void()>> workQueue;

GLFWwindow* window{};

std::jthread workerThread;

template<typename... T>
void AddError(fmt::format_string<T...> formatString, T&&... args) {
	std::string logLine = fmt::format(formatString, args...);
	std::scoped_lock lock(ErrorLogMutex);
	ErrorLog.push_back(logLine);
}

template<typename... T>
void SetOperation(fmt::format_string<T...> formatString, T&&... args) {
	std::scoped_lock lock(OperationMutex);
	Operation = fmt::format(formatString, args...);
}

std::string GetErrorLog() {
	std::scoped_lock lock(ErrorLogMutex);
	std::string result;
	for(const auto& line : ErrorLog) {
		if(!result.empty()) { result += '\n'; }
		result += line;
	}
	return result;
}

std::string GetOperation() {
	std::scoped_lock lock(OperationMutex);
	return Operation;
}

void ClearErrorLog() {
	std::scoped_lock loc(ErrorLogMutex);
	ErrorLog.clear();
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

void StartConversion() {
	ClearErrorLog();
}

void CancelConversion() {
	std::scoped_lock loc(dataMutex);
	workQueue.clear();
}

void WorkerMain(std::stop_token stoken) {
	std::move_only_function<void()> workItem;
	while(!stoken.stop_requested()) {
		{
			std::scoped_lock loc(dataMutex);
			if(!workQueue.empty()) {
				workItem = std::move(workQueue.front());
				workQueue.pop_front();
			}
		}
		if(workItem) {
			workItem();
			workItem = nullptr;
		} else {
			std::this_thread::sleep_for(50ms);
		}
	}
}

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
				SetOperation("Idle");
				if(ImGui::Button("Convert Files", {0, 0})) {
					appState = AppState::Converting;
					SetOperation("Starting Conversion");
					StartConversion();
				}

			} else if(appState == AppState::Converting) {
				if(ImGui::Button("Cancel Conversion", {0, 0})) {
					appState = AppState::Cancelling;
					SetOperation("Canceling Conversion");
					CancelConversion();
				} else {
					std::scoped_lock loc(dataMutex);
					if(workQueue.empty()) { appState = AppState::Idle; }
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
			std::string operation = GetOperation();
			ImGui::InputText("Current Operation", &operation, ImGuiInputTextFlags_ReadOnly);
			std::string log = GetErrorLog();
			ImGui::InputTextMultiline("##log_text", &log, ImVec2(1260, 510),
			                          ImGuiInputTextFlags_ReadOnly |
			                              ImGuiInputTextFlags_NoHorizontalScroll);
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

	workerThread = std::jthread(WorkerMain);

	SetOperation("Idle");

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

	CancelConversion();
	workerThread.request_stop();
	workerThread.join();

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