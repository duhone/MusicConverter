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

#include <samplerate.h>

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

	cecore::StorageBuffer<float> pcmData[2];
	uint32_t currentBufferIndex = 0;
	uint32_t workingBufferIndex = 1;
	auto nextBuffer             = [&] { std::swap(currentBufferIndex, workingBufferIndex); };

	auto currentBuffer = [&]() -> auto& { return pcmData[currentBufferIndex]; };
	auto workingBuffer = [&]() -> auto& { return pcmData[workingBufferIndex]; };

	currentBuffer().prepare(flacInfo.numFrames * flacInfo.numChannels);

	auto framesRead = drflac_read_pcm_frames_f32(drFlac, flacInfo.numFrames, currentBuffer().data());
	while(framesRead < flacInfo.numFrames) {
		framesRead +=
		    drflac_read_pcm_frames_f32(drFlac, flacInfo.numFrames - framesRead,
		                               currentBuffer().data() + framesRead * flacInfo.numChannels);
	}
	drflac_close(drFlac);

	currentBuffer().commit(flacInfo.numFrames * flacInfo.numChannels);

	if(CancelWork.load()) { return; }

	// flac says by default channel layout is
	// 1 channel - mono
	// 2 channel - left, right
	// 3 channel - left, right, center
	// 4 channel - left, right, back left, back right
	// 5 channel - left, right, center, back left, back right
	// 6 channel - left, right, center, LFE, back left, back right
	// 7 channel - left, right, center, LFE, back center, back left, back right
	// 8 channel - left, right, center, LFE, back left, back right, side left, side right
	// mostly just following suggestion in
	// https://www.atsc.org/wp-content/uploads/2015/03/A52-201212-17.pdf to merge
	// I don't have sdev and cdev, so just going to mix center at -3db and rears at -6db.

	constexpr uint32_t numOutputChannels = 2;

	// only going to output stereo, so need to merge/duplicate channels as appropriate.
	switch(flacInfo.numChannels) {
		case 1:
			// mono, need to duplicate
			{
				nextBuffer();
				currentBuffer().prepare(flacInfo.numFrames * numOutputChannels);
				auto srcPtr  = workingBuffer().data();
				auto destPtr = currentBuffer().data();
				for(uint32_t frame = 0; frame < flacInfo.numFrames; ++frame) {
					destPtr[2 * frame + 0] = srcPtr[frame];
					destPtr[2 * frame + 1] = srcPtr[frame];
				}
				currentBuffer().commit(flacInfo.numFrames * numOutputChannels);
			}
			break;
		case 2:
			// already stereo, nothing to do
			break;
		case 3:
			// left, right, center
			{
				nextBuffer();
				currentBuffer().prepare(flacInfo.numFrames * numOutputChannels);
				auto srcPtr  = workingBuffer().data();
				auto destPtr = currentBuffer().data();
				for(uint32_t frame = 0; frame < flacInfo.numFrames; ++frame) {
					// mix center channel at -3db into left and right
					constexpr float mainChannelMix   = 1.0f / 1.707f;
					constexpr float centerChannelMix = 0.707f / 1.707f;
					destPtr[2 * frame + 0] =
					    mainChannelMix * srcPtr[3 * frame + 0] + centerChannelMix * srcPtr[3 * frame + 2];
					destPtr[2 * frame + 1] =
					    mainChannelMix * srcPtr[3 * frame + 1] + centerChannelMix * srcPtr[3 * frame + 2];
				}
				currentBuffer().commit(flacInfo.numFrames * numOutputChannels);
			}
			break;
		case 4:
			// left, right, back left, back right
			{
				nextBuffer();
				currentBuffer().prepare(flacInfo.numFrames * numOutputChannels);
				auto srcPtr  = workingBuffer().data();
				auto destPtr = currentBuffer().data();
				for(uint32_t frame = 0; frame < flacInfo.numFrames; ++frame) {
					// mix rear channels at -6db
					constexpr float mainChannelMix = 1.0f / 1.5f;
					constexpr float rearChannelMix = 0.5f / 1.5f;
					destPtr[2 * frame + 0] =
					    mainChannelMix * srcPtr[4 * frame + 0] + rearChannelMix * srcPtr[4 * frame + 2];
					destPtr[2 * frame + 1] =
					    mainChannelMix * srcPtr[4 * frame + 1] + rearChannelMix * srcPtr[4 * frame + 3];
				}
				currentBuffer().commit(flacInfo.numFrames * numOutputChannels);
			}
			break;
		case 5:
			// left, right, center, back left, back right
			{
				nextBuffer();
				currentBuffer().prepare(flacInfo.numFrames * numOutputChannels);
				auto srcPtr  = workingBuffer().data();
				auto destPtr = currentBuffer().data();
				for(uint32_t frame = 0; frame < flacInfo.numFrames; ++frame) {
					// mix center at -3db, rear channels at -6db
					constexpr float channelMixTotal  = 1.0f + 0.707f + 0.5f;
					constexpr float mainChannelMix   = 1.0f / channelMixTotal;
					constexpr float centerChannelMix = 0.707f / channelMixTotal;
					constexpr float rearChannelMix   = 0.5f / channelMixTotal;
					destPtr[2 * frame + 0]           = mainChannelMix * srcPtr[5 * frame + 0] +
					                         centerChannelMix * srcPtr[5 * frame + 2] +
					                         rearChannelMix * srcPtr[5 * frame + 3];
					destPtr[2 * frame + 1] = mainChannelMix * srcPtr[5 * frame + 1] +
					                         centerChannelMix * srcPtr[5 * frame + 2] +
					                         rearChannelMix * srcPtr[5 * frame + 4];
				}
				currentBuffer().commit(flacInfo.numFrames * numOutputChannels);
			}
			break;
		case 6:
			// left, right, center, LFE, back left, back right
			{
				nextBuffer();
				currentBuffer().prepare(flacInfo.numFrames * numOutputChannels);
				auto srcPtr  = workingBuffer().data();
				auto destPtr = currentBuffer().data();
				for(uint32_t frame = 0; frame < flacInfo.numFrames; ++frame) {
					// mix center at -3db, rear channels at -6db, LFE at identity
					constexpr float channelMixTotal  = 1.0f + 0.707f + 0.5f + 1.0f;
					constexpr float mainChannelMix   = 1.0f / channelMixTotal;
					constexpr float centerChannelMix = 0.707f / channelMixTotal;
					constexpr float lfeChannelMix    = 1.0f / channelMixTotal;
					constexpr float rearChannelMix   = 0.5f / channelMixTotal;
					destPtr[2 * frame + 0] =
					    mainChannelMix * srcPtr[6 * frame + 0] + centerChannelMix * srcPtr[6 * frame + 2] +
					    lfeChannelMix * srcPtr[6 * frame + 3] + rearChannelMix * srcPtr[6 * frame + 4];
					destPtr[2 * frame + 1] =
					    mainChannelMix * srcPtr[6 * frame + 1] + centerChannelMix * srcPtr[6 * frame + 2] +
					    lfeChannelMix * srcPtr[6 * frame + 3] + rearChannelMix * srcPtr[6 * frame + 5];
				}
				currentBuffer().commit(flacInfo.numFrames * numOutputChannels);
			}
			break;
		case 7:
			// left, right, center, LFE, back center, back left, back right
			{
				nextBuffer();
				currentBuffer().prepare(flacInfo.numFrames * numOutputChannels);
				auto srcPtr  = workingBuffer().data();
				auto destPtr = currentBuffer().data();
				for(uint32_t frame = 0; frame < flacInfo.numFrames; ++frame) {
					// mix center at -3db, rear channels at -6db, LFE at identity
					constexpr float channelMixTotal  = 1.0f + 0.707f + 0.5f + 1.0f + 0.5f;
					constexpr float mainChannelMix   = 1.0f / channelMixTotal;
					constexpr float centerChannelMix = 0.707f / channelMixTotal;
					constexpr float lfeChannelMix    = 1.0f / channelMixTotal;
					constexpr float rearChannelMix   = 0.5f / channelMixTotal;
					destPtr[2 * frame + 0] =
					    mainChannelMix * srcPtr[7 * frame + 0] + centerChannelMix * srcPtr[7 * frame + 2] +
					    lfeChannelMix * srcPtr[7 * frame + 3] + rearChannelMix * srcPtr[7 * frame + 4] +
					    rearChannelMix * srcPtr[7 * frame + 5];
					destPtr[2 * frame + 1] =
					    mainChannelMix * srcPtr[7 * frame + 1] + centerChannelMix * srcPtr[7 * frame + 2] +
					    lfeChannelMix * srcPtr[7 * frame + 3] + rearChannelMix * srcPtr[7 * frame + 4] +
					    rearChannelMix * srcPtr[7 * frame + 6];
				}
				currentBuffer().commit(flacInfo.numFrames * numOutputChannels);
			}
			break;
		case 8:
			// left, right, center, LFE, back left, back right, side left, side right
			{
				nextBuffer();
				currentBuffer().prepare(flacInfo.numFrames * numOutputChannels);
				auto srcPtr  = workingBuffer().data();
				auto destPtr = currentBuffer().data();
				for(uint32_t frame = 0; frame < flacInfo.numFrames; ++frame) {
					// mix center at -3db, rear channels at -6db, LFE at identity
					constexpr float channelMixTotal  = 1.0f + 0.707f + 0.5f + 1.0f + 0.5f;
					constexpr float mainChannelMix   = 1.0f / channelMixTotal;
					constexpr float centerChannelMix = 0.707f / channelMixTotal;
					constexpr float lfeChannelMix    = 1.0f / channelMixTotal;
					constexpr float rearChannelMix   = 0.5f / channelMixTotal;
					destPtr[2 * frame + 0] =
					    mainChannelMix * srcPtr[8 * frame + 0] + centerChannelMix * srcPtr[8 * frame + 2] +
					    lfeChannelMix * srcPtr[8 * frame + 3] + rearChannelMix * srcPtr[8 * frame + 4] +
					    rearChannelMix * srcPtr[8 * frame + 6];
					destPtr[2 * frame + 1] =
					    mainChannelMix * srcPtr[8 * frame + 1] + centerChannelMix * srcPtr[8 * frame + 2] +
					    lfeChannelMix * srcPtr[8 * frame + 3] + rearChannelMix * srcPtr[8 * frame + 5] +
					    rearChannelMix * srcPtr[8 * frame + 7];
				}
				currentBuffer().commit(flacInfo.numFrames * numOutputChannels);
			}
			break;
		default:
			AddError("{} had an usupported number of channels {}", job.source.string(),
			         flacInfo.numChannels);
			return;
			break;
	}

	if(CancelWork.load()) { return; }

	constexpr uint32_t c_targetSampleRate = 48000;

	uint64_t outputFrames = flacInfo.numFrames;
	if(flacInfo.sampleRate != c_targetSampleRate) {
		outputFrames = ((uint64_t)flacInfo.numFrames * c_targetSampleRate) / flacInfo.sampleRate;
		nextBuffer();
		currentBuffer().prepare(outputFrames * numOutputChannels);
		auto srcPtr  = workingBuffer().data();
		auto destPtr = currentBuffer().data();

		SRC_DATA srcData;
		srcData.end_of_input  = 0;
		srcData.src_ratio     = static_cast<double>(c_targetSampleRate) / flacInfo.sampleRate;
		srcData.data_in       = srcPtr;
		srcData.input_frames  = flacInfo.numFrames;
		srcData.data_out      = destPtr;
		srcData.output_frames = (uint32_t)outputFrames;

		int error = src_simple(&srcData, SRC_SINC_BEST_QUALITY, 2);
		if(error != 0) {
			AddError("error converted sample rate {}", src_strerror(error));
			return;
		}
		currentBuffer().commit(outputFrames * numOutputChannels);
	}

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
				auto extension = job.source.extension().string();
				for(char& c : extension) { c = (char)std::tolower(c); }
				if(extension == ".mp3" || extension == ".ogg") {
					SetOperation("Copying from {} to {}", job.source.string(), job.dest.string());
					fs::copy_file(job.source, job.dest, fs::copy_options::overwrite_existing);
				} else {
					SetOperation("Converting from {} to {}", job.source.string(), job.dest.string());
					ConvertFile(job);
				}
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