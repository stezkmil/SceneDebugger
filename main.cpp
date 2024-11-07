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
void parseOBJData(const std::string& data);
void renderGUI();
void fitDataIntoView();

struct Vector3 {
	float x, y, z;
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
};


struct Primitive {
	std::string name;
	std::string type; // "drawtriangle", "drawline", "overlaymesh"
	std::vector<Vertex> vertices; // Changed from Vector3 to Vertex
	std::vector<unsigned int> indices; // For indexed drawing
	glm::vec4 color;
};

void renderPrimitives(Shader& shaderProgram, const std::vector<Primitive>& primitives);

struct Frame {
	std::vector<Primitive> primitives;
};

std::vector<Frame> frames;
std::vector<Primitive> overlayPrimitives;
int currentFrameIndex = 0;
Camera camera;
bool fitView = true;

// Create a random number generator and distribution
std::mt19937 rng(std::random_device{}());
std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

void parseOBJData(const std::string& data) {

	// At the beginning of parseOBJData()
	static GLuint overlayVAO = 0, overlayVBO = 0, overlayEBO = 0;

	if (overlayVAO != 0) {
		glDeleteVertexArrays(1, &overlayVAO);
		glDeleteBuffers(1, &overlayVBO);
		glDeleteBuffers(1, &overlayEBO);
		overlayVAO = overlayVBO = overlayEBO = 0;
	}


	std::istringstream stream(data);
	std::string line;

	std::vector<Vertex> vertices; // Combined positions and normals
	std::vector<unsigned int> indices;

	std::vector<glm::vec3> objPositions; // Positions from 'v' lines
	std::vector<std::vector<unsigned int>> faceIndices; // Indices for each face

	// Clear previous overlay data
	overlayPrimitives.clear();

	while (std::getline(stream, line)) {
		// Remove comments
		size_t commentPos = line.find('#');
		if (commentPos != std::string::npos) {
			line = line.substr(0, commentPos);
		}

		std::istringstream linestream(line);
		std::string prefix;
		linestream >> prefix;

		if (prefix == "v") {
			// Vertex position
			float x, y, z;
			linestream >> x >> y >> z;
			objPositions.push_back(glm::vec3(x, y, z));
		}
		else if (prefix == "f") {
			// Face definition
			std::vector<unsigned int> face;
			std::string vertexStr;
			while (linestream >> vertexStr) {
				std::istringstream vertexStream(vertexStr);
				std::string indexStr;
				std::getline(vertexStream, indexStr, '/');
				int index = std::stoi(indexStr);
				face.push_back(index - 1); // OBJ indices start at 1
			}
			faceIndices.push_back(face);
		}
		// Ignore other prefixes (e.g., 'vn', 'vt')
	}

	// Calculate normals and build the vertex and index arrays
	for (const auto& face : faceIndices) {
		if (face.size() < 3) continue; // Skip degenerate faces

		// Triangulate faces with more than 3 vertices
		for (size_t i = 1; i < face.size() - 1; ++i) {
			// Get vertex positions
			glm::vec3 v0 = objPositions[face[0]];
			glm::vec3 v1 = objPositions[face[i]];
			glm::vec3 v2 = objPositions[face[i + 1]];

			// Calculate face normal
			glm::vec3 normal = glm::normalize(glm::cross(v1 - v0, v2 - v0));

			// Create vertices with positions and normals
			Vertex vertex0 = { v0, normal };
			Vertex vertex1 = { v1, normal };
			Vertex vertex2 = { v2, normal };

			// Add vertices and indices
			unsigned int indexOffset = vertices.size();
			vertices.push_back(vertex0);
			vertices.push_back(vertex1);
			vertices.push_back(vertex2);

			indices.push_back(indexOffset);
			indices.push_back(indexOffset + 1);
			indices.push_back(indexOffset + 2);
		}
	}

	// Store the mesh data in the overlayPrimitives (as a single primitive)
	Primitive meshPrim;
	meshPrim.type = "overlaymesh";
	meshPrim.name = "Overlay Mesh";
	meshPrim.vertices = vertices;
	meshPrim.indices = indices; // We need to add indices to the Primitive structure
	// We can set a fixed color or leave it empty as we'll use lighting
	overlayPrimitives.push_back(meshPrim);
}



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

		// Use the updated getProjectionMatrix() method with near and far planes
		glm::mat4 projection = camera.getProjectionMatrix(aspectRatio, camera.nearPlane, camera.farPlane);
		glm::mat4 view = camera.getViewMatrix();
		glm::mat4 model = glm::mat4(1.0f); // Identity matrix

		shaderProgram.setMat4("projection", projection);
		shaderProgram.setMat4("view", view);
		shaderProgram.setMat4("model", model);

		// Set lighting uniforms
		glm::vec3 lightPos = camera.target + glm::vec3(0.0f, 10.0f, 10.0f); // Light position
		glm::vec3 viewPos = camera.getPosition(); // Camera position
		shaderProgram.setVec3("lightPos", lightPos);
		shaderProgram.setVec3("viewPos", viewPos);

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

	if (ImGui::Button("Paste OBJ from Clipboard as Overlay")) {
		const char* clipboard = glfwGetClipboardString(NULL);
		if (clipboard) {
			parseOBJData(std::string(clipboard));
			fitView = true;
		}
	}

	if (ImGui::Button("Clear Overlay")) {
		overlayPrimitives.clear();
		fitView = true;
	}

	// Add the "Clear Frames" button
	if (ImGui::Button("Clear Frames")) {
		frames.clear();
		currentFrameIndex = 0;
		fitView = true;
	}

	if (!frames.empty()) {
		if (ImGui::SliderInt("Frame", &currentFrameIndex, 0, frames.size() - 1)) {
			//fitView = true;
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
	if (frames.empty() && overlayPrimitives.empty()) return;

	if (fitView) {
		fitDataIntoView();
		fitView = false;
	}

	// Render current frame primitives
	if (!frames.empty()) {
		const Frame& frame = frames[currentFrameIndex];
		renderPrimitives(shaderProgram, frame.primitives);
	}

	// Render overlay primitives
	if (!overlayPrimitives.empty()) {
		renderPrimitives(shaderProgram, overlayPrimitives);
	}
}

void renderPrimitives(Shader& shaderProgram, const std::vector<Primitive>& primitives) {
	for (const auto& prim : primitives) {
		if (prim.vertices.empty()) continue;

		// Special handling for overlay mesh
		if (prim.type == "overlaymesh") {
			static GLuint overlayVAO = 0, overlayVBO = 0, overlayEBO = 0;
			static size_t numIndices = 0;

			// If the VAO is not generated yet, generate and bind buffers
			if (overlayVAO == 0) {
				glGenVertexArrays(1, &overlayVAO);
				glGenBuffers(1, &overlayVBO);
				glGenBuffers(1, &overlayEBO);

				glBindVertexArray(overlayVAO);

				glBindBuffer(GL_ARRAY_BUFFER, overlayVBO);
				glBufferData(GL_ARRAY_BUFFER, prim.vertices.size() * sizeof(Vertex), prim.vertices.data(), GL_STATIC_DRAW);

				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, overlayEBO);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, prim.indices.size() * sizeof(unsigned int), prim.indices.data(), GL_STATIC_DRAW);

				// Vertex positions
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
				glEnableVertexAttribArray(0);

				// Vertex normals
				glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
				glEnableVertexAttribArray(1);

				numIndices = prim.indices.size();

				glBindVertexArray(0);
			}

			// Set shader uniforms
			shaderProgram.setVec4("primitiveColor", glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));

			// Bind and draw the mesh
			glBindVertexArray(overlayVAO);
			glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_INT, 0);
			glBindVertexArray(0);
		}
		else {
			GLuint VAO, VBO;
			glGenVertexArrays(1, &VAO);
			glGenBuffers(1, &VBO);

			std::vector<float> vertices;
			for (const auto& v : prim.vertices) {
				vertices.push_back(v.position.x);
				vertices.push_back(v.position.y);
				vertices.push_back(v.position.z);
			}

			glBindVertexArray(VAO);

			glBindBuffer(GL_ARRAY_BUFFER, VBO);
			glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

			// Vertex positions
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
			glEnableVertexAttribArray(0);

			// Set the primitive color uniform
			shaderProgram.setVec4("primitiveColor", prim.color);

			// Draw the primitive
			if (prim.type == "drawtriangle" || prim.type == "overlaytriangle") {
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
			else if (prim.type == "drawline" || prim.type == "overlayline") {
				glDrawArrays(GL_LINES, 0, 2);
			}

			// Cleanup
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0);
			glDeleteBuffers(1, &VBO);
			glDeleteVertexArrays(1, &VAO);
		}
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
			std::vector<Vertex> vertices;
			for (int i = 0; i < 3; ++i) {
				// Skip to '['
				while (it != data.end() && *it != '[') ++it;
				if (it == data.end()) break;
				++it; // Skip '['

				Vertex vertex;
				std::string numStr;
				int coordIndex = 0;
				while (it != data.end() && *it != ']') {
					if (std::isdigit(*it) || *it == '.' || *it == '-') {
						numStr += *it;
					}
					else if (*it == ',') {
						if (!numStr.empty()) {
							float val = std::stof(numStr);
							if (coordIndex == 0) vertex.position.x = val;
							else if (coordIndex == 1) vertex.position.y = val;
							numStr.clear();
							++coordIndex;
						}
					}
					++it;
				}
				// Capture the last coordinate (z)
				if (!numStr.empty()) {
					float val = std::stof(numStr);
					vertex.position.z = val;
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
			std::vector<Vertex> vertices;
			for (int i = 0; i < 2; ++i) {
				// Skip to '['
				while (it != data.end() && *it != '[') ++it;
				if (it == data.end()) break;
				++it; // Skip '['

				Vertex vertex;
				std::string numStr;
				int coordIndex = 0;
				while (it != data.end() && *it != ']') {
					if (std::isdigit(*it) || *it == '.' || *it == '-') {
						numStr += *it;
					}
					else if (*it == ',') {
						if (!numStr.empty()) {
							float val = std::stof(numStr);
							if (coordIndex == 0) vertex.position.x = val;
							else if (coordIndex == 1) vertex.position.y = val;
							numStr.clear();
							++coordIndex;
						}
					}
					++it;
				}
				// Capture the last coordinate (z)
				if (!numStr.empty()) {
					float val = std::stof(numStr);
					vertex.position.z = val;
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
	if (frames.empty() && overlayPrimitives.empty()) return;

	glm::vec3 minBounds(FLT_MAX);
	glm::vec3 maxBounds(-FLT_MAX);

	// Include current frame primitives
	if (!frames.empty()) {
		const Frame& frame = frames[currentFrameIndex];
		for (const auto& prim : frame.primitives) {
			for (const auto& vert : prim.vertices) {
				glm::vec3 position(vert.position.x, vert.position.y, vert.position.z);
				minBounds = glm::min(minBounds, position);
				maxBounds = glm::max(maxBounds, position);
			}
		}
	}

	// Include overlay primitives
	for (const auto& prim : overlayPrimitives) {
		if (prim.type == "overlaymesh") {
			for (const auto& vertex : prim.vertices) {
				glm::vec3 position = vertex.position;
				minBounds = glm::min(minBounds, position);
				maxBounds = glm::max(maxBounds, position);
			}
		}
		else {
			for (const auto& vert : prim.vertices) {
				glm::vec3 position(vert.position.x, vert.position.y, vert.position.z);
				minBounds = glm::min(minBounds, position);
				maxBounds = glm::max(maxBounds, position);
			}
		}
	}

	glm::vec3 center = (minBounds + maxBounds) / 2.0f;
	float radius = glm::length(maxBounds - minBounds) / 2.0f;
	radius = std::max(radius, 1.0f);

	// Update camera target and distance
	camera.target = center;
	camera.distance = radius * 2.0f;

	// Calculate distances from camera position to the near and far points of the bounding sphere
	float nearPlane = 0.1f; // Slightly before the closest point
	float farPlane = camera.distance + radius * 1.5f;  // Slightly beyond the farthest point

	// Ensure nearPlane is positive and not too close to zero
	nearPlane = std::max(nearPlane, 0.1f);

	// Update the camera's near and far planes
	camera.nearPlane = nearPlane;
	camera.farPlane = farPlane;
}


