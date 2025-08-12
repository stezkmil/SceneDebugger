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
bool rayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir,
	const glm::vec3& v0, const glm::vec3& v1,
	const glm::vec3& v2, float& tOut);

void mouse_button_callback(GLFWwindow* window,
	int button, int action, int mods);

void key_callback(GLFWwindow* window, int key, int scancode,
	int action, int mods);

struct Vector3 {
	float x, y, z;
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
};

struct Primitive {
	std::string name;
	std::string type; // "drawtriangle", "drawline", "drawpoint", "overlaymesh"
	std::vector<Vertex> vertices;
	std::vector<unsigned int> indices; // For indexed drawing (overlaymesh)
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
bool depthTestNonOverlay = true;

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

void mouse_button_callback(GLFWwindow* window,
	int button, int action, int /*mods*/)
{
	// We only care about middle-button presses
	if (button != GLFW_MOUSE_BUTTON_MIDDLE || action != GLFW_PRESS) return;

	static double lastClick = 0.0;
	double now = glfwGetTime();
	const double dblClickTime = 0.30;          // seconds

	if (now - lastClick > dblClickTime) {       // first click
		lastClick = now;
		return;
	}
	lastClick = 0.0;                            // reset for next pair

	//--------------------------------------------------------------  
	// *** Double-click detected – cast a picking ray ***
	//--------------------------------------------------------------
	double mx, my; glfwGetCursorPos(window, &mx, &my);
	int w, h;      glfwGetFramebufferSize(window, &w, &h);
	if (h == 0) return;

	// Get camera from window user-ptr
	Camera* cam = static_cast<Camera*>(glfwGetWindowUserPointer(window));
	if (!cam) return;

	float nx = (2.0f * float(mx) / w) - 1.0f;      // NDC
	float ny = 1.0f - (2.0f * float(my) / h);

	glm::mat4  proj = cam->getProjectionMatrix(float(w) / h, cam->nearPlane, cam->farPlane);
	glm::mat4  view = cam->getViewMatrix();
	glm::mat4  invPV = glm::inverse(proj * view);

	glm::vec4 pNear = invPV * glm::vec4(nx, ny, -1.f, 1.f);
	glm::vec4 pFar = invPV * glm::vec4(nx, ny, 1.f, 1.f);
	pNear /= pNear.w;  pFar /= pFar.w;

	glm::vec3 rayOrig = glm::vec3(pNear);
	glm::vec3 rayDir = glm::normalize(glm::vec3(pFar) - rayOrig);

	//--------------------------------------------------------------  
	// Test the ray against every triangle we know about
	//--------------------------------------------------------------
	float  bestT = 1e30f;
	glm::vec3 bestPt;

	auto tryTriangle = [&](const glm::vec3& a,
		const glm::vec3& b,
		const glm::vec3& c)
		{
			float t;
			if (rayTriangleIntersect(rayOrig, rayDir, a, b, c, t) && t < bestT)
			{
				bestT = t;
				bestPt = rayOrig + rayDir * t;
			}
		};

	// 1) overlay mesh (usually the heavy one)
	for (const auto& prim : overlayPrimitives)
		if (prim.type == "overlaymesh")
			for (size_t i = 0; i + 2 < prim.indices.size(); i += 3)
				tryTriangle(prim.vertices[prim.indices[i]].position,
					prim.vertices[prim.indices[i + 1]].position,
					prim.vertices[prim.indices[i + 2]].position);

	// 2) triangles in the current frame
	if (!frames.empty()) {
		const Frame& f = frames[currentFrameIndex];
		for (const auto& prim : f.primitives)
			if (prim.type == "drawtriangle")
				tryTriangle(prim.vertices[0].position,
					prim.vertices[1].position,
					prim.vertices[2].position);
	}

	//--------------------------------------------------------------  
	// If we hit something – re-centre the camera
	//--------------------------------------------------------------
	if (bestT < 1e29f)
		cam->setTarget(bestPt);
}

void key_callback(GLFWwindow* /*window*/, int key, int /*scancode*/,
	int action, int /*mods*/)
{
	if (action != GLFW_PRESS && action != GLFW_REPEAT) return;   // only react to press / repeat

	ImGuiIO& io = ImGui::GetIO();
	if (io.WantCaptureKeyboard) return;                          // let ImGui handle typed text

	if (frames.empty()) return;

	if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_PERIOD) {
		if (currentFrameIndex < static_cast<int>(frames.size()) - 1)
			++currentFrameIndex;
	}
	else if (key == GLFW_KEY_LEFT || key == GLFW_KEY_COMMA) {
		if (currentFrameIndex > 0)
			--currentFrameIndex;
	}
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

	glfwSetMouseButtonCallback(window, mouse_button_callback);

	glfwSetKeyCallback(window, key_callback);

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

		glm::mat4 projection = camera.getProjectionMatrix(aspectRatio, camera.nearPlane, camera.farPlane);
		glm::mat4 view = camera.getViewMatrix();
		glm::mat4 model = glm::mat4(1.0f); // Identity matrix

		shaderProgram.setMat4("projection", projection);
		shaderProgram.setMat4("view", view);
		shaderProgram.setMat4("model", model);

		// Set lighting uniforms
		glm::vec3 lightPos = camera.target + glm::vec3(0.0f, 10.0f, 10.0f);
		glm::vec3 viewPos = camera.getPosition();
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
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

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

	if (ImGui::Button("Clear Frames")) {
		frames.clear();
		currentFrameIndex = 0;
		fitView = true;
	}

	ImGui::Checkbox("Z-buffer test for non-overlay", &depthTestNonOverlay);

	if (!frames.empty()) {
		if (ImGui::ArrowButton("##frame_left", ImGuiDir_Left)) {
			if (currentFrameIndex > 0) --currentFrameIndex;
		}

		ImGui::SameLine();

		if (ImGui::SliderInt("Frame", &currentFrameIndex, 0, frames.size() - 1)) {
			// optional: fitView = true;
		}

		ImGui::SameLine();

		if (ImGui::ArrowButton("##frame_right", ImGuiDir_Right)) {
			if (currentFrameIndex < static_cast<int>(frames.size()) - 1)
				++currentFrameIndex;
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


	// Render overlay primitives
	if (!overlayPrimitives.empty()) {
		renderPrimitives(shaderProgram, overlayPrimitives);
	}

	// Render current-frame primitives  (non-overlay)
	if (!frames.empty()) {
		if (depthTestNonOverlay)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);
		const Frame& frame = frames[currentFrameIndex];
		renderPrimitives(shaderProgram, frame.primitives);
		glEnable(GL_DEPTH_TEST);
	}
}

void renderPrimitives(Shader& shaderProgram, const std::vector<Primitive>& primitives) {
	for (const auto& prim : primitives) {
		if (prim.vertices.empty()) continue;

		// Special handling for overlay mesh
		if (prim.type == "overlaymesh") {
			static GLuint overlayVAO = 0, overlayVBO = 0, overlayEBO = 0;
			static size_t numIndices = 0;

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

			shaderProgram.setBool("useLighting", true);
			shaderProgram.setVec4("primitiveColor", glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
			glBindVertexArray(overlayVAO);
			glDrawElements(GL_TRIANGLES, numIndices, GL_UNSIGNED_INT, 0);
			glBindVertexArray(0);
		}
		else {
			GLuint VAO, VBO;
			glGenVertexArrays(1, &VAO);
			glGenBuffers(1, &VBO);

			std::vector<float> vertices;
			vertices.reserve(prim.vertices.size() * 3);
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

			shaderProgram.setBool("useLighting", false);

			// Set the primitive color uniform
			shaderProgram.setVec4("primitiveColor", prim.color);

			// Draw the primitive
			if (prim.type == "drawtriangle" || prim.type == "overlaytriangle") {
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
			else if (prim.type == "drawline" || prim.type == "overlayline") {
				glDrawArrays(GL_LINES, 0, 2);
			}
			else if (prim.type == "drawpoint") {
				glPointSize(5.0f); // Adjust the size as needed
				glDrawArrays(GL_POINTS, 0, (GLsizei)prim.vertices.size());
			}

			// Cleanup
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindVertexArray(0);
			glDeleteBuffers(1, &VBO);
			glDeleteVertexArrays(1, &VAO);
		}
	}
}

// Updated parseInputData to handle optional RGBA color bracket
void parseInputData(const std::string& data) {
	std::string::const_iterator it = data.begin();
	Frame currentFrame;
	bool inFrame = false;

	auto parseOptionalColor = [&](glm::vec4& color) {
		// Skip whitespace
		while (it != data.end() && std::isspace(*it)) ++it;

		// If the next character is '[', parse RGBA
		if (it != data.end() && *it == '[') {
			++it; // skip '['
			float rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			std::string numStr;
			int index = 0;
			while (it != data.end() && *it != ']') {
				if ((std::isdigit(*it) || *it == '.' || *it == '-')) {
					numStr += *it;
				}
				else if (*it == ',') {
					if (!numStr.empty()) {
						float val = std::stof(numStr);
						rgba[index] = val;
						numStr.clear();
						++index;
						if (index > 3) break; // in case too many
					}
				}
				++it;
			}
			// Capture the last value (A)
			if (!numStr.empty() && index < 4) {
				float val = std::stof(numStr);
				rgba[index] = val;
				numStr.clear();
			}
			color = glm::vec4(rgba[0], rgba[1], rgba[2], rgba[3]);

			if (it != data.end()) ++it; // skip ']'
		}
		else {
			// No bracket found, use random color
			color = glm::vec4(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);
		}
		};

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

			// Parse name (optional quotes)
			if (it != data.end() && *it == '"') {
				++it; // skip quote
				std::string name;
				while (it != data.end() && *it != '"') {
					name += *it;
					++it;
				}
				if (it != data.end()) ++it; // skip closing quote
				prim.name = name;
			}
			else {
				prim.name = "Unnamed Triangle";
			}

			// Parse 3 vertices
			std::vector<Vertex> vertices;
			for (int i = 0; i < 3; ++i) {
				while (it != data.end() && *it != '[') ++it;
				if (it == data.end()) break;
				++it; // skip '['

				Vertex vert{};
				std::string numStr;
				int coordIndex = 0;
				while (it != data.end() && *it != ']') {
					if (std::isdigit(*it) || *it == '.' || *it == '-') {
						numStr += *it;
					}
					else if (*it == ',') {
						if (!numStr.empty()) {
							float val = std::stof(numStr);
							if (coordIndex == 0) vert.position.x = val;
							else if (coordIndex == 1) vert.position.y = val;
							numStr.clear();
							++coordIndex;
						}
					}
					++it;
				}
				// last coordinate
				if (!numStr.empty()) {
					float val = std::stof(numStr);
					vert.position.z = val;
					numStr.clear();
				}
				vertices.push_back(vert);

				if (it != data.end()) ++it; // skip ']'
			}
			prim.vertices = vertices;

			// Parse optional color
			parseOptionalColor(prim.color);

			currentFrame.primitives.push_back(prim);
		}
		// Check for drawline
		else if (std::distance(it, data.end()) >= 8 && std::equal(it, it + 8, "drawline")) {
			it += 8;
			Primitive prim;
			prim.type = "drawline";

			// Skip whitespace
			while (it != data.end() && std::isspace(*it)) ++it;

			// Parse name
			if (it != data.end() && *it == '"') {
				++it;
				std::string name;
				while (it != data.end() && *it != '"') {
					name += *it;
					++it;
				}
				if (it != data.end()) ++it;
				prim.name = name;
			}
			else {
				prim.name = "Unnamed Line";
			}

			// Parse 2 vertices
			std::vector<Vertex> vertices;
			for (int i = 0; i < 2; ++i) {
				while (it != data.end() && *it != '[') ++it;
				if (it == data.end()) break;
				++it; // skip '['

				Vertex vert{};
				std::string numStr;
				int coordIndex = 0;
				while (it != data.end() && *it != ']') {
					if (std::isdigit(*it) || *it == '.' || *it == '-') {
						numStr += *it;
					}
					else if (*it == ',') {
						if (!numStr.empty()) {
							float val = std::stof(numStr);
							if (coordIndex == 0) vert.position.x = val;
							else if (coordIndex == 1) vert.position.y = val;
							numStr.clear();
							++coordIndex;
						}
					}
					++it;
				}
				// last coordinate
				if (!numStr.empty()) {
					float val = std::stof(numStr);
					vert.position.z = val;
					numStr.clear();
				}
				vertices.push_back(vert);

				if (it != data.end()) ++it; // skip ']'
			}
			prim.vertices = vertices;

			// Parse optional color
			parseOptionalColor(prim.color);

			currentFrame.primitives.push_back(prim);
		}
		// Check for drawpoint
		else if (std::distance(it, data.end()) >= 9 && std::equal(it, it + 9, "drawpoint")) {
			it += 9;
			Primitive prim;
			prim.type = "drawpoint";

			// Skip whitespace
			while (it != data.end() && std::isspace(*it)) ++it;

			// Parse name
			if (it != data.end() && *it == '"') {
				++it;
				std::string name;
				while (it != data.end() && *it != '"') {
					name += *it;
					++it;
				}
				if (it != data.end()) ++it;
				prim.name = name;
			}
			else {
				prim.name = "Unnamed Point";
			}

			// Parse single vertex
			while (it != data.end() && *it != '[') ++it;
			if (it != data.end()) ++it; // skip '['

			Vertex vtx{};
			{
				std::string numStr;
				int coordIndex = 0;
				while (it != data.end() && *it != ']') {
					if (std::isdigit(*it) || *it == '.' || *it == '-') {
						numStr += *it;
					}
					else if (*it == ',') {
						if (!numStr.empty()) {
							float val = std::stof(numStr);
							if (coordIndex == 0) vtx.position.x = val;
							else if (coordIndex == 1) vtx.position.y = val;
							numStr.clear();
							++coordIndex;
						}
					}
					++it;
				}
				// last coordinate
				if (!numStr.empty()) {
					float val = std::stof(numStr);
					vtx.position.z = val;
					numStr.clear();
				}
			}
			vtx.normal = glm::vec3(0.0f, 0.0f, 1.0f);
			if (it != data.end()) ++it; // skip ']'

			prim.vertices.push_back(vtx);

			// Parse optional color
			parseOptionalColor(prim.color);

			currentFrame.primitives.push_back(prim);
		}
		// framestart / frameend or unknown text
		else {
			if (it != data.end()) ++it;
		}
	}

	if (inFrame) {
		// If there's an unclosed frame, push it at the end (optional)
		frames.push_back(currentFrame);
	}
}

// Fit data into view
void fitDataIntoView() {
	if (frames.empty() && overlayPrimitives.empty()) return;

	glm::vec3 minBounds(FLT_MAX);
	glm::vec3 maxBounds(-FLT_MAX);
	bool anyVertex = false;

	// Include current frame primitives
	if (!frames.empty()) {
		const Frame& frame = frames[currentFrameIndex];
		for (const auto& prim : frame.primitives) {
			for (const auto& vert : prim.vertices) {
				glm::vec3 position = vert.position;
				minBounds = glm::min(minBounds, position);
				maxBounds = glm::max(maxBounds, position);
				anyVertex = true;
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
				anyVertex = true;
			}
		}
		else {
			for (const auto& vert : prim.vertices) {
				glm::vec3 position(vert.position.x, vert.position.y, vert.position.z);
				minBounds = glm::min(minBounds, position);
				maxBounds = glm::max(maxBounds, position);
				anyVertex = true;
			}
		}
	}

	if (!anyVertex) return;
	glm::vec3 center = (minBounds + maxBounds) / 2.0f;
	float radius = glm::length(maxBounds - minBounds) / 2.0f;
	radius = std::max(radius, 1.0f);

	// Update camera target and distance
	camera.target = center;
	camera.distance = radius * 2.0f;

	// Calculate distances from camera position to the near and far points of the bounding sphere
	float nearPlane = 0.1f;
	float farPlane = camera.distance + radius * 1.5f;

	nearPlane = std::max(nearPlane, 0.1f);

	//camera.nearPlane = nearPlane;
	//camera.farPlane = farPlane;
}

// Möller-Trumbore
bool rayTriangleIntersect(const glm::vec3& orig, const glm::vec3& dir,
	const glm::vec3& v0, const glm::vec3& v1,
	const glm::vec3& v2, float& tOut)
{
	const float EPS = 1e-6f;
	glm::vec3 e1 = v1 - v0;
	glm::vec3 e2 = v2 - v0;
	glm::vec3 p = glm::cross(dir, e2);
	float det = glm::dot(e1, p);
	if (fabs(det) < EPS) return false;
	float invDet = 1.0f / det;
	glm::vec3 t = orig - v0;
	float u = glm::dot(t, p) * invDet;
	if (u < 0.f || u > 1.f) return false;
	glm::vec3 q = glm::cross(t, e1);
	float v = glm::dot(dir, q) * invDet;
	if (v < 0.f || u + v > 1.f) return false;
	float tHit = glm::dot(e2, q) * invDet;
	if (tHit < EPS) return false;
	tOut = tHit;
	return true;
}
