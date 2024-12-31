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

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

import CR.Engine;

import std;

namespace cecore = CR::Engine::Core;
namespace cep    = CR::Engine::Platform;

namespace fs = std::filesystem;

using namespace std::literals;

enum class AppState { Idle, Converting, Cancelling };

struct ConversionJob {
	fs::path source;
	fs::path dest;
};

const fs::path c_configPath{"config.json"};

AppState appState{AppState::Idle};

fs::path sourcePath;
fs::path destPath;
std::string sourcePathString;
std::string destPathString;

std::mutex dataMutex;
int32_t numJobs{};
int32_t completedJobs{};
float convertProgress{};
std::string Operation;
std::mutex OperationMutex;
std::mutex ErrorLogMutex;
std::deque<std::string> ErrorLog;
std::deque<std::move_only_function<void()>> workQueue;
std::atomic_bool CancelWork;
std::atomic_bool WorkCancelled;
GLFWwindow* window{};

std::jthread workerThread;

template<typename... T>
void AddError(fmt::format_string<T...> formatString, T&&... args) {
	std::string logLine = fmt::format(formatString, std::forward<T>(args)...);
	std::scoped_lock lock(ErrorLogMutex);
	ErrorLog.push_back(logLine);
}

template<typename... T>
void SetOperation(fmt::format_string<T...> formatString, T&&... args) {
	std::scoped_lock lock(OperationMutex);
	Operation = fmt::format(formatString, std::forward<T>(args)...);
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

std::string EscapePathForJson(const fs::path& path) {
	std::string result;
	for(const auto achar : path.string()) {
		if(achar == '\\') {
			result += "\\\\";
		} else {
			result += achar;
		}
	}
	return result;
}

void SaveConfig() {
	constexpr auto c_outputFormat = R"({{"source_path":"{}", "dest_path":"{}"}})";

	auto outputString = fmt::format(fmt::runtime(c_outputFormat), EscapePathForJson(sourcePath),
	                                EscapePathForJson(destPath));

	std::ofstream outputFile(c_configPath);
	outputFile << outputString;
}

void ConvertFile(const ConversionJob& job) {
	if(CancelWork.load()) { return; }

	if(!fs::exists(job.source)) {
		AddError("{} doesn't exist, logic error in app", job.source.string());
		return;
	}

	if(fs::exists(job.dest)) { fs::remove(job.dest); }

	cep::MemoryMappedFile sourceFile(job.source);

	struct FlacInfo {
		uint32_t numFrames{};
		uint32_t sampleRate{};
		uint32_t numChannels{};
	};
	FlacInfo flacInfo{};

	auto metaData = [](void* pUserData, drflac_metadata* pMetadata) {
		if(pMetadata->type == DRFLAC_METADATA_BLOCK_TYPE_STREAMINFO) {
			FlacInfo* info    = (FlacInfo*)pUserData;
			info->numFrames   = (uint32_t)pMetadata->data.streaminfo.totalPCMFrameCount;
			info->sampleRate  = pMetadata->data.streaminfo.sampleRate;
			info->numChannels = pMetadata->data.streaminfo.channels;
		}
	};
	auto drFlac = drflac_open_memory_with_metadata(sourceFile.data(), sourceFile.size(), metaData,
	                                               &flacInfo, nullptr);
	if(flacInfo.numFrames == 0) {
		AddError("{} could not read flac uncompressed size", job.source.string());
		return;
	}

	cecore::StorageBuffer<float> pcmData;
	pcmData.prepare(flacInfo.numFrames * flacInfo.numChannels);

	auto framesRead = drflac_read_pcm_frames_f32(drFlac, flacInfo.numFrames, pcmData.data());
	while(framesRead < flacInfo.numFrames) {
		framesRead += drflac_read_pcm_frames_f32(drFlac, flacInfo.numFrames - framesRead,
		                                         pcmData.data() + framesRead * flacInfo.numChannels);
	}
	drflac_close(drFlac);

	pcmData.commit(flacInfo.numFrames * flacInfo.numChannels);

	if(CancelWork.load()) { return; }
}

void FinishedJob() {
	++completedJobs;
	convertProgress = (float)completedJobs / numJobs;
}

void StartConversion() {
	ClearErrorLog();
	CancelWork.store(false);
	sourcePath = sourcePathString;
	destPath   = destPathString;
	if(!fs::exists(sourcePath)) {
		AddError("Source Path {} doesn't exist", sourcePath.string());
		return;
	}
	if(!fs::exists(destPath)) {
		AddError("Destination Path {} doesn't exist", destPath.string());
		return;
	}

	// First lets delete any directories in dest that aren't in source
	std::vector<fs::path> pathsToDelete;
	for(const auto& entry : fs::recursive_directory_iterator(destPath)) {
		if(entry.is_directory()) {
			auto relPath     = fs::relative(entry.path(), destPath);
			auto pathToCheck = sourcePath / relPath;
			if(!fs::exists(pathToCheck)) { pathsToDelete.push_back(entry.path()); }
		}
	}

	// Now add any missing folders
	std::vector<fs::path> pathsToAdd;
	for(const auto& entry : fs::recursive_directory_iterator(sourcePath)) {
		if(entry.is_directory()) {
			auto relPath     = fs::relative(entry.path(), sourcePath);
			auto pathToCheck = destPath / relPath;
			if(!fs::exists(pathToCheck)) { pathsToAdd.push_back(pathToCheck); }
		}
	}

	// now need to add conversion work. this work must happen after the folder structure is correct
	std::vector<ConversionJob> pathsToConvert;
	for(const auto& entry : fs::recursive_directory_iterator(sourcePath)) {
		if(!entry.is_directory()) {
			auto relPath         = fs::relative(entry.path(), sourcePath);
			auto pathToCheck     = destPath / relPath;
			bool needsConversion = false;
			if(!fs::exists(pathToCheck)) {
				needsConversion = true;
			} else if(fs::last_write_time(entry.path()) > fs::last_write_time(pathToCheck)) {
				needsConversion = true;
			}
			if(needsConversion) { pathsToConvert.emplace_back(entry.path(), pathToCheck); }
		}
	}

	// adding and removing folders are 1 job each.
	numJobs         = (int32_t)pathsToConvert.size() + 2;
	completedJobs   = 0;
	convertProgress = 0.0f;

	// and push a work item to make these changes.
	{
		std::scoped_lock loc(dataMutex);
		workQueue.emplace_back([pathsToDelete = std::move(pathsToDelete)]() {
			for(const auto& path : pathsToDelete) {
				SetOperation("removing path {}", path.string());
				// may have already been deleted if its a sub folder
				if(fs::exists(path)) { fs::remove_all(path); }
			}
			FinishedJob();
		});
		workQueue.emplace_back([pathsToAdd = std::move(pathsToAdd)]() {
			for(const auto& path : pathsToAdd) {
				SetOperation("Adding path {}", path.string());
				// may have already been added if a sub folder was already added
				if(!fs::exists(path)) { fs::create_directories(path); }
			}
			FinishedJob();
		});
		workQueue.emplace_back([pathsToConvert = std::move(pathsToConvert)]() {
			for(const auto& job : pathsToConvert) {
				SetOperation("Converting from {} to {}", job.source.string(), job.dest.string());
				ConvertFile(job);
				FinishedJob();
			}
		});
	}
}

void CancelConversion() {
	CancelWork.store(true);
	WorkCancelled.store(false);
}

void WorkerMain(std::stop_token stoken) {
	std::move_only_function<void()> workItem;
	while(!stoken.stop_requested()) {
		if(CancelWork.load() == true) {
			std::scoped_lock loc(dataMutex);
			workQueue.clear();
			WorkCancelled.store(true);
		}
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
					if(completedJobs == numJobs) {
						numJobs         = 0;
						completedJobs   = 0;
						convertProgress = 0.0f;
						appState        = AppState::Idle;
					}
				}
			} else if(appState == AppState::Cancelling) {
				ImGui::BeginDisabled();
				ImGui::Button("Canceling", {0, 0});
				ImGui::EndDisabled();

				if(WorkCancelled.load()) {
					numJobs         = 0;
					completedJobs   = 0;
					convertProgress = 0.0f;
					appState        = AppState::Idle;
				}
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