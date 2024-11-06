// main.cpp

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <random>

// Include ImGui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Include GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Include custom headers
#include "Camera.h"
#include "Shader.h"

// Forward declarations
void processInput(GLFWwindow* window);
void renderScene(Shader& shaderProgram);
void parseInputData(const std::string& data);
void renderGUI();
void fitDataIntoView();

struct Vector3 {
	float x, y, z;
};

struct Primitive {
	std::string name;
	std::string type; // "drawtriangle" or "drawline"
	std::vector<Vector3> vertices;
	glm::vec4 color; // Add this member to store the color
};

struct Frame {
	std::vector<Primitive> primitives;
};

std::vector<Frame> frames;
int currentFrameIndex = 0;
Camera camera;
bool fitView = true;

// Create a random number generator and distribution
std::mt19937 rng(std::random_device{}());
std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	// Prevent division by zero
	if (height == 0) height = 1;
	glViewport(0, 0, width, height);
}

int main() {
	// Initialize GLFW
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW.\n";
		return -1;
	}

	// OpenGL version 3.3 Core Profile
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	// For MacOS
#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

	// Create window
	GLFWwindow* window = glfwCreateWindow(1280, 720, "Scene Debugger", NULL, NULL);
	if (!window) {
		std::cerr << "Failed to create GLFW window.\n";
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	// Set viewport
	glViewport(0, 0, 1280, 720);

	// Set framebuffer size callback
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// Set window user pointer to the camera instance
	glfwSetWindowUserPointer(window, &camera);

	// Set scroll callback
	glfwSetScrollCallback(window, Camera::scroll_callback);

	// Initialize GLEW
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	// Ignore invalid enum error from glewInit
	glGetError();
	if (err != GLEW_OK) {
		std::cerr << "Failed to initialize GLEW.\n";
		return -1;
	}

	// Set up Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// Set ImGui style
	ImGui::StyleColorsDark();

	// Initialize ImGui backend
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");

	// Enable depth testing
	glEnable(GL_DEPTH_TEST);

	// Load shaders
	Shader shaderProgram("vertex_shader.glsl", "fragment_shader.glsl");

	// Main loop
	while (!glfwWindowShouldClose(window)) {
		// Input handling
		processInput(window);

		// Start ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		// GUI code
		renderGUI();

		// Rendering
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Use shader program
		shaderProgram.use();

		// Set camera matrices
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		if (height == 0) height = 1; // Prevent division by zero
		float aspectRatio = width / (float)height;
		glm::mat4 projection = camera.getProjectionMatrix(aspectRatio);
		glm::mat4 view = camera.getViewMatrix();
		shaderProgram.setMat4("projection", projection);
		shaderProgram.setMat4("view", view);

		// Render 3D scene
		renderScene(shaderProgram);

		// Render ImGui
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		// Swap buffers and poll events
		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	// Cleanup ImGui and GLFW
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}

// Process input
void processInput(GLFWwindow* window) {
	// Close window on pressing 'Escape'
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	// Implement camera controls
	camera.processInput(window);
}

// Render GUI
void renderGUI() {
	ImGui::Begin("Controls");

	if (ImGui::Button("Paste from Clipboard")) {
		const char* clipboard = glfwGetClipboardString(NULL);
		if (clipboard) {
			frames.clear();
			parseInputData(std::string(clipboard));
			currentFrameIndex = 0;
			fitView = true;
		}
	}

	if (!frames.empty()) {
		if (ImGui::SliderInt("Frame", &currentFrameIndex, 0, frames.size() - 1)) {
			fitView = true;
		}

		ImGui::Text("Primitives:");
		for (size_t i = 0; i < frames[currentFrameIndex].primitives.size(); ++i) {
			const auto& prim = frames[currentFrameIndex].primitives[i];
			ImGui::BulletText("%s %zu (%s)", prim.name.c_str(), i, prim.type.c_str());
		}
	}
	else {
		ImGui::Text("No frames loaded.");
	}

	ImGui::End();
}

// Render the scene
void renderScene(Shader& shaderProgram) {
	if (frames.empty()) return;

	if (fitView) {
		fitDataIntoView();
		fitView = false;
	}

	const Frame& frame = frames[currentFrameIndex];

	for (const auto& prim : frame.primitives) {
		if (prim.vertices.empty()) continue;

		// Use the stored color for the primitive
		glm::vec4 primitiveColor = prim.color;

		GLuint VAO, VBO;
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);

		std::vector<float> vertices;
		for (const auto& v : prim.vertices) {
			vertices.push_back(v.x);
			vertices.push_back(v.y);
			vertices.push_back(v.z);
		}

		glBindVertexArray(VAO);

		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

		// Vertex positions
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);

		// Set the primitive color uniform
		shaderProgram.setVec4("primitiveColor", primitiveColor);

		// Draw the primitive
		if (prim.type == "drawtriangle") {
			glDrawArrays(GL_TRIANGLES, 0, 3);
		}
		else if (prim.type == "drawline") {
			glDrawArrays(GL_LINES, 0, 2);
		}

		// Cleanup
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		glDeleteBuffers(1, &VBO);
		glDeleteVertexArrays(1, &VAO);
	}
}


// Parse input data
void parseInputData(const std::string& data) {
	std::string::const_iterator it = data.begin();
	Frame currentFrame;
	bool inFrame = false;

	while (it != data.end()) {
		// Skip whitespace
		while (it != data.end() && std::isspace(*it)) ++it;

		// Check for framestart()
		if (std::distance(it, data.end()) >= 11 && std::equal(it, it + 11, "framestart(")) {
			inFrame = true;
			currentFrame = Frame();
			it += 11;
		}
		// Check for frameend()
		else if (std::distance(it, data.end()) >= 9 && std::equal(it, it + 9, "frameend(")) {
			if (inFrame) {
				frames.push_back(currentFrame);
				currentFrame = Frame();
				inFrame = false;
			}
			it += 9;
		}
		// Check for drawtriangle
		else if (std::distance(it, data.end()) >= 12 && std::equal(it, it + 12, "drawtriangle")) {
			it += 12; // Move iterator past "drawtriangle"
			Primitive prim;
			prim.type = "drawtriangle";

			// Skip whitespace
			while (it != data.end() && std::isspace(*it)) ++it;

			// Check for opening double quote
			if (it != data.end() && *it == '"') {
				++it; // Skip opening quote
				std::string name;
				while (it != data.end() && *it != '"') {
					name += *it;
					++it;
				}
				if (it != data.end()) {
					++it; // Skip closing quote
					prim.name = name;
				}
				else {
					// Handle error: unmatched quote
					prim.name = "Unnamed Triangle";
				}
			}
			else {
				// Handle error: name not provided or not in quotes
				prim.name = "Unnamed Triangle";
			}

			// Generate and assign a random color
			prim.color = glm::vec4(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);

			// Parse vertices
			std::vector<Vector3> vertices;
			for (int i = 0; i < 3; ++i) {
				// Skip to '['
				while (it != data.end() && *it != '[') ++it;
				if (it == data.end()) break;
				++it; // Skip '['

				Vector3 vertex;
				std::string numStr;
				int coordIndex = 0;
				while (it != data.end() && *it != ']') {
					if (std::isdigit(*it) || *it == '.' || *it == '-') {
						numStr += *it;
					}
					else if (*it == ',') {
						if (!numStr.empty()) {
							float val = std::stof(numStr);
							if (coordIndex == 0) vertex.x = val;
							else if (coordIndex == 1) vertex.y = val;
							numStr.clear();
							++coordIndex;
						}
					}
					++it;
				}
				// Capture the last coordinate (z)
				if (!numStr.empty()) {
					float val = std::stof(numStr);
					vertex.z = val;
					numStr.clear();
				}
				vertices.push_back(vertex);

				// Ensure we don't increment past the end
				if (it != data.end()) ++it; // Skip ']'
			}
			prim.vertices = vertices;
			currentFrame.primitives.push_back(prim);
		}
		// Check for drawline
		else if (std::distance(it, data.end()) >= 8 && std::equal(it, it + 8, "drawline")) {
			it += 8; // Move iterator past "drawline"
			Primitive prim;
			prim.type = "drawline";

			// Skip whitespace
			while (it != data.end() && std::isspace(*it)) ++it;

			// Check for opening double quote
			if (it != data.end() && *it == '"') {
				++it; // Skip opening quote
				std::string name;
				while (it != data.end() && *it != '"') {
					name += *it;
					++it;
				}
				if (it != data.end()) {
					++it; // Skip closing quote
					prim.name = name;
				}
				else {
					// Handle error: unmatched quote
					prim.name = "Unnamed Line";
				}
			}
			else {
				// Handle error: name not provided or not in quotes
				prim.name = "Unnamed Line";
			}

			// Generate and assign a random color
			prim.color = glm::vec4(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);

			// Parse vertices
			std::vector<Vector3> vertices;
			for (int i = 0; i < 2; ++i) {
				// Skip to '['
				while (it != data.end() && *it != '[') ++it;
				if (it == data.end()) break;
				++it; // Skip '['

				Vector3 vertex;
				std::string numStr;
				int coordIndex = 0;
				while (it != data.end() && *it != ']') {
					if (std::isdigit(*it) || *it == '.' || *it == '-') {
						numStr += *it;
					}
					else if (*it == ',') {
						if (!numStr.empty()) {
							float val = std::stof(numStr);
							if (coordIndex == 0) vertex.x = val;
							else if (coordIndex == 1) vertex.y = val;
							numStr.clear();
							++coordIndex;
						}
					}
					++it;
				}
				// Capture the last coordinate (z)
				if (!numStr.empty()) {
					float val = std::stof(numStr);
					vertex.z = val;
					numStr.clear();
				}
				vertices.push_back(vertex);

				// Ensure we don't increment past the end
				if (it != data.end()) ++it; // Skip ']'
			}
			prim.vertices = vertices;
			currentFrame.primitives.push_back(prim);
		}

		else {
			if (it != data.end()) ++it;
		}
	}
}


// Fit data into view
void fitDataIntoView() {
	if (frames.empty()) return;

	const Frame& frame = frames[currentFrameIndex];
	if (frame.primitives.empty()) return;

	glm::vec3 minBounds(FLT_MAX);
	glm::vec3 maxBounds(-FLT_MAX);

	for (const auto& prim : frame.primitives) {
		for (const auto& vert : prim.vertices) {
			minBounds = glm::min(minBounds, glm::vec3(vert.x, vert.y, vert.z));
			maxBounds = glm::max(maxBounds, glm::vec3(vert.x, vert.y, vert.z));
		}
	}

	glm::vec3 center = (minBounds + maxBounds) / 2.0f;
	float radius = glm::length(maxBounds - minBounds) / 2.0f;
	radius = std::max(radius, 1.0f);

	camera.target = center;
	camera.distance = radius * 2.0f;
}
