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

void renderPrimitives(Shader& shaderProgram, const std::vector<Primitive>& primitives, int selectedIndex);

struct Frame {
	std::vector<Primitive> primitives;
};

std::vector<Frame> frames;
std::vector<Primitive> overlayPrimitives;
int currentFrameIndex = 0;
Camera camera;
bool fitView = true;
bool depthTestNonOverlay = true;
int g_SelectedPrimitive = -1;   // index within current frame, -1 = none
static int  g_PrevSelected = -1;
static bool g_RequestScrollToSelection = false;

// whenever you set g_SelectedPrimitive (from picking or list click), do:
auto setSelection = [](int idx) {
	g_PrevSelected = g_SelectedPrimitive;
	if (idx != g_SelectedPrimitive) {
		g_SelectedPrimitive = idx;
		g_RequestScrollToSelection = true;   // ask GUI to scroll next frame
	}
	};

// Create a random number generator and distribution
std::mt19937 rng(std::random_device{}());
std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

// --- Stable "random" color from an integer id -------------------------------
static inline uint32_t pcg_hash(uint32_t x) {
	// PCG-inspired integer hash. Fast and decent distribution.
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

static inline void hsv2rgb(float H, float S, float V, float& r, float& g, float& b) {
	// H in [0,1), S,V in [0,1]
	float h = H * 6.0f;
	int i = (int)h;
	float f = h - i;
	float p = V * (1.0f - S);
	float q = V * (1.0f - S * f);
	float t = V * (1.0f - S * (1.0f - f));
	switch (i % 6) {
	case 0: r = V; g = t; b = p; break;
	case 1: r = q; g = V; b = p; break;
	case 2: r = p; g = V; b = t; break;
	case 3: r = p; g = q; b = V; break;
	case 4: r = t; g = p; b = V; break;
	case 5: r = V; g = p; b = q; break;
	}
}

// Optional: tweak to taste. Lower S gives more pastel, higher V gives brighter.
static inline ImVec4 stableColorFromId(uint32_t id, float S = 0.65f, float V = 1.0f, float A = 1.0f) {
	uint32_t h = pcg_hash(id);
	// Use different hashed bits for hue/sat/val slight jitter (keeps variety)
	float H = (h & 0xFFFFu) / 65535.0f;                 // hue in [0,1)
	float sJit = ((h >> 16) & 0xFFu) / 255.0f * 0.10f;  // +/- 0.05 jitter
	float vJit = ((h >> 24) & 0xFFu) / 255.0f * 0.10f;  // +/- 0.05 jitter
	float r, g, b;
	hsv2rgb(H, std::clamp(S - 0.05f + sJit, 0.3f, 0.9f),
		std::clamp(V - 0.05f + vJit, 0.6f, 1.0f), r, g, b);
	return ImVec4(r, g, b, A);
}

// If you want separation by primitive type too, combine like this:
template <class T>
static inline uint32_t hashCombine32(uint32_t a, T b) {
	return pcg_hash(a ^ (uint32_t)pcg_hash((uint32_t)b + 0x9e3779b9u + (a << 6) + (a >> 2)));
}


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

static float g_LastLPressX = 0.f, g_LastLPressY = 0.f;
static double g_LastLPressTime = 0.0;
static bool g_LeftPressed = false;

// Build a world-space ray from mouse (NDC → world)
static void makePickRay(GLFWwindow* window, const Camera& cam, glm::vec3& rayOrig, glm::vec3& rayDir)
{
	double mx, my; glfwGetCursorPos(window, &mx, &my);
	int w, h;      glfwGetFramebufferSize(window, &w, &h);
	if (h <= 0) { rayOrig = glm::vec3(0); rayDir = glm::vec3(0, 0, -1); return; }

	float nx = (2.0f * float(mx) / float(w)) - 1.0f;
	float ny = 1.0f - (2.0f * float(my) / float(h));

	glm::mat4 proj = cam.getProjectionMatrix(float(w) / float(h), cam.nearPlane, cam.farPlane);
	glm::mat4 view = cam.getViewMatrix();
	glm::mat4 invPV = glm::inverse(proj * view);

	glm::vec4 pNear = invPV * glm::vec4(nx, ny, -1.f, 1.f);
	glm::vec4 pFar = invPV * glm::vec4(nx, ny, 1.f, 1.f);
	pNear /= pNear.w; pFar /= pFar.w;

	rayOrig = glm::vec3(pNear);
	rayDir = glm::normalize(glm::vec3(pFar) - rayOrig);
}

// Distance from ray to segment (squared)
static float raySegmentDist2(const glm::vec3& ro, const glm::vec3& rd,
	const glm::vec3& a, const glm::vec3& b)
{
	// Based on closest points between two lines (ray and segment), clamped to segment.
	const glm::vec3 u = rd;                 // normalized
	const glm::vec3 v = b - a;              // segment direction
	const glm::vec3 w0 = ro - a;
	float aUU = glm::dot(u, u);             // =1, but keep general
	float bUV = glm::dot(u, v);
	float cVV = glm::dot(v, v);
	float dUW0 = glm::dot(u, w0);
	float eVW0 = glm::dot(v, w0);

	float denom = aUU * cVV - bUV * bUV;
	float sc = 0.f, tc = 0.f;
	if (denom > 1e-12f) {
		sc = (bUV * eVW0 - cVV * dUW0) / denom;   // along ray
		tc = (aUU * eVW0 - bUV * dUW0) / denom;   // along segment
	}
	else {
		// nearly parallel: project a→ray and clamp tc
		sc = -dUW0 / aUU;
		tc = 0.f;
	}
	tc = glm::clamp(tc, 0.f, 1.f);
	// closest points
	glm::vec3 Pc = ro + sc * u;
	glm::vec3 Qc = a + tc * v;
	return glm::dot(Pc - Qc, Pc - Qc);
}

// Distance from ray to point (squared)
static float rayPointDist2(const glm::vec3& ro, const glm::vec3& rd, const glm::vec3& p)
{
	glm::vec3 w = p - ro;
	float t = glm::dot(w, rd);          // along ray
	glm::vec3 closest = ro + t * rd;
	glm::vec3 d = p - closest;
	return glm::dot(d, d);
}

// Convert a constant pixel radius to world units at depth d.
// Assumes 45° vertical FOV like Camera::getProjectionMatrix.
static float pixelRadiusToWorld(float pixels, float depth, int viewportHeight)
{
	// size of 1 pixel at depth d: (2 * d * tan(fov/2)) / H
	const float fovY_deg = 45.0f;
	const float fovY = glm::radians(fovY_deg);
	float pixelWorld = (2.0f * depth * tanf(fovY * 0.5f)) / float(std::max(1, viewportHeight));
	return pixels * pixelWorld;
}

void mouse_button_callback(GLFWwindow* window,
	int button, int action, int /*mods*/)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS) {
			double mx, my; glfwGetCursorPos(window, &mx, &my);
			g_LastLPressX = (float)mx;
			g_LastLPressY = (float)my;
			g_LastLPressTime = glfwGetTime();
			g_LeftPressed = true;
		}
		else if (action == GLFW_RELEASE && g_LeftPressed) {
			g_LeftPressed = false;

			// Click heuristics: short time, little movement
			double now = glfwGetTime();
			double dt = now - g_LastLPressTime;
			double mx, my; glfwGetCursorPos(window, &mx, &my);
			float dx = float(mx) - g_LastLPressX;
			float dy = float(my) - g_LastLPressY;
			float drag2 = dx * dx + dy * dy;

			const double maxClickTime = 0.25;   // seconds
			const float  maxDrag2 = 6.0f * 6.0f; // pixels^2

			if (dt <= maxClickTime && drag2 <= maxDrag2) {
				// Perform picking
				int w, h; glfwGetFramebufferSize(window, &w, &h);
				if (h > 0 && !frames.empty()) {
					Camera* cam = static_cast<Camera*>(glfwGetWindowUserPointer(window));
					glm::vec3 ro, rd; makePickRay(window, *cam, ro, rd);

					// Threshold in pixels → world-radius near the hit depth guess.
					// We'll test using a few depth guesses; starting with distance to target.
					float depthGuess = glm::length(cam->getPosition() - cam->target);
					float pickRadius = pixelRadiusToWorld(6.0f, depthGuess, h); // ~6px

					const auto& f = frames[currentFrameIndex];
					int bestIdx = -1;
					float bestMetric = 1e30f; // smaller is better

					// Triangles: normal ray-triangle hit (use t as metric)
					for (size_t i = 0; i < f.primitives.size(); ++i) {
						const auto& prim = f.primitives[i];
						if (prim.type == "drawtriangle" && prim.vertices.size() >= 3) {
							float t;
							if (rayTriangleIntersect(ro, rd,
								prim.vertices[0].position,
								prim.vertices[1].position,
								prim.vertices[2].position, t))
							{
								if (t < bestMetric) {
									bestMetric = t;
									bestIdx = (int)i;
								}
							}
						}
					}

					// Lines: ray-to-segment distance < pickRadius
					for (size_t i = 0; i < f.primitives.size(); ++i) {
						const auto& prim = f.primitives[i];
						if ((prim.type == "drawline" || prim.type == "overlayline") && prim.vertices.size() >= 2) {
							float d2 = raySegmentDist2(ro, rd,
								prim.vertices[0].position,
								prim.vertices[1].position);
							if (d2 < pickRadius * pickRadius) {
								// Use distance along ray to a midpoint as tie-breaker
								glm::vec3 mid = 0.5f * (prim.vertices[0].position + prim.vertices[1].position);
								float t = glm::dot((mid - ro), rd);
								if (t > 0.0f && t < bestMetric) {
									bestMetric = t;
									bestIdx = (int)i;
								}
							}
						}
					}

					// Points: ray-to-point distance < pickRadius
					for (size_t i = 0; i < f.primitives.size(); ++i) {
						const auto& prim = f.primitives[i];
						if (prim.type == "drawpoint" && !prim.vertices.empty()) {
							float d2 = rayPointDist2(ro, rd, prim.vertices[0].position);
							if (d2 < pickRadius * pickRadius) {
								float t = glm::dot((prim.vertices[0].position - ro), rd);
								if (t > 0.0f && t < bestMetric) {
									bestMetric = t;
									bestIdx = (int)i;
								}
							}
						}
					}

					// Do NOT consider overlay mesh/triangles for selection (as requested)

					setSelection(bestIdx); // -1 if nothing hit
				}
			}
		}
	}

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
		{
			++currentFrameIndex;
			setSelection(-1);
		}
	}
	else if (key == GLFW_KEY_LEFT || key == GLFW_KEY_COMMA) {
		if (currentFrameIndex > 0)
		{
			--currentFrameIndex;
			setSelection(-1);
		}
	}
	else if (key == GLFW_KEY_F) {
		fitView = true;
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
			setSelection(-1);
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
		setSelection(-1);
		fitView = true;
	}

	ImGui::Checkbox("Z-buffer test for non-overlay", &depthTestNonOverlay);

	ImGui::SameLine();
	if (ImGui::Button("Fit View")) {
		fitView = true;   // next render pass will call fitDataIntoView()
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Compute bounds of current frame + overlay and frame the view");

	if (!frames.empty()) {
		if (ImGui::ArrowButton("##frame_left", ImGuiDir_Left)) {
			if (currentFrameIndex > 0)
			{
				--currentFrameIndex;
				setSelection(-1);
			}
		}

		ImGui::SameLine();

		if (ImGui::SliderInt("Frame", &currentFrameIndex, 0, frames.size() - 1)) {
			// optional: fitView = true;
		}

		ImGui::SameLine();

		if (ImGui::ArrowButton("##frame_right", ImGuiDir_Right)) {
			if (currentFrameIndex < static_cast<int>(frames.size()) - 1)
			{
				++currentFrameIndex;
				setSelection(-1);
			}
		}

		ImGui::Text("Primitives:");

		// Give the list its own scroll area (height: choose what you like)
		ImGui::BeginChild("PrimitiveList", ImVec2(0, 260), true, ImGuiWindowFlags_HorizontalScrollbar);

		for (size_t i = 0; i < frames[currentFrameIndex].primitives.size(); ++i) {
			const auto& prim = frames[currentFrameIndex].primitives[i];
			const bool selected = ((int)i == g_SelectedPrimitive);

			char label[256];
			snprintf(label, sizeof(label), "%s %zu (%s)", prim.name.c_str(), i, prim.type.c_str());

			if (ImGui::Selectable(label, selected)) {
				setSelection((int)i);
			}

			// After the row is submitted, if it’s the (newly) selected one, scroll it into view.
			if (selected && g_RequestScrollToSelection) {
				// 0.35 puts it slightly below the top; use 0.5f to center if you prefer.
				ImGui::SetScrollHereY(0.35f);
				g_RequestScrollToSelection = false;
			}
		}

		ImGui::EndChild();

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
		renderPrimitives(shaderProgram, overlayPrimitives, -1);
	}

	// Render current-frame primitives  (non-overlay)
	if (!frames.empty()) {
		if (depthTestNonOverlay)
			glEnable(GL_DEPTH_TEST);
		else
			glDisable(GL_DEPTH_TEST);
		const Frame& frame = frames[currentFrameIndex];
		renderPrimitives(shaderProgram, frame.primitives, g_SelectedPrimitive);
		glEnable(GL_DEPTH_TEST);
	}
}

void renderPrimitives(Shader& shaderProgram, const std::vector<Primitive>& primitives, int selectedIndex) {
	for (size_t i = 0; i < primitives.size(); ++i) {
		const auto& prim = primitives[i];
		if (prim.vertices.empty()) continue;

		const bool isSelected = (int)i == selectedIndex;

		// --- Overlay mesh stays as-is (never selected) ---
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

				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
				glEnableVertexAttribArray(0);

				glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
				glEnableVertexAttribArray(1);

				numIndices = prim.indices.size();

				glBindVertexArray(0);
			}

			shaderProgram.setBool("useLighting", true);
			shaderProgram.setVec4("primitiveColor", glm::vec4(0.7f, 0.7f, 0.7f, 1.0f));
			glBindVertexArray(overlayVAO);
			glDrawElements(GL_TRIANGLES, (GLsizei)numIndices, GL_UNSIGNED_INT, 0);
			glBindVertexArray(0);
			continue;
		}

		// --- Non-overlay (points/lines/triangles) ---
		GLuint VAO = 0, VBO = 0;
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);

		std::vector<float> positions;
		positions.reserve(prim.vertices.size() * 3);
		for (const auto& v : prim.vertices) {
			positions.push_back(v.position.x);
			positions.push_back(v.position.y);
			positions.push_back(v.position.z);
		}

		glBindVertexArray(VAO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(float), positions.data(), GL_STATIC_DRAW);

		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(0);

		shaderProgram.setBool("useLighting", false);

		// Compute draw color (boost/saturate when selected)
		auto boosted = [&](const glm::vec4& c) -> glm::vec4 {
			// mix toward yellowish for visibility, clamp to 1
			glm::vec3 target(1.0f, 1.0f, 0.2f);
			glm::vec3 rgb = glm::mix(glm::vec3(c), target, 0.5f);
			return glm::vec4(glm::min(rgb * 1.1f, glm::vec3(1.0f)), 1.0f);
			};
		glm::vec4 drawColor = isSelected ? boosted(prim.color) : prim.color;
		shaderProgram.setVec4("primitiveColor", drawColor);

		if (prim.type == "drawtriangle") {
			// filled
			glDrawArrays(GL_TRIANGLES, 0, 3);

			if (isSelected) {
				// quick outline for clarity
				glLineWidth(2.5f);
				shaderProgram.setVec4("primitiveColor", glm::vec4(1.0f, 1.0f, 0.2f, 1.0f));
				glDrawArrays(GL_LINE_LOOP, 0, 3);
				glLineWidth(1.0f);
			}
		}
		else if (prim.type == "drawline" || prim.type == "overlayline") {
			if (isSelected) glLineWidth(3.0f);
			glDrawArrays(GL_LINES, 0, 2);
			if (isSelected) glLineWidth(1.0f);
		}
		else if (prim.type == "drawpoint") {
			if (isSelected) glPointSize(9.0f); else glPointSize(5.0f);
			glDrawArrays(GL_POINTS, 0, (GLsizei)prim.vertices.size());
			glPointSize(1.0f); // restore default
		}

		// Cleanup
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		glDeleteBuffers(1, &VBO);
		glDeleteVertexArrays(1, &VAO);
	}
}


// Updated parseInputData to handle optional RGBA color bracket
void parseInputData(const std::string& data) {
	std::string::const_iterator it = data.begin();
	Frame currentFrame;
	bool inFrame = false;

	auto parseOptionalColor = [&](glm::vec4& color, size_t index) {
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
			ImVec4 stableColor = stableColorFromId(static_cast<uint32_t>(index));
			color = glm::vec4(stableColor.x, stableColor.y, stableColor.z, 1.0f);
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
			parseOptionalColor(prim.color, currentFrame.primitives.size());

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
			parseOptionalColor(prim.color, currentFrame.primitives.size());

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
			parseOptionalColor(prim.color, currentFrame.primitives.size());

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
