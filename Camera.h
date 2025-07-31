// Camera.h
#ifndef CAMERA_H
#define CAMERA_H

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "imgui.h"

class Camera {
public:
	glm::vec3 target;
	float distance;
	float pitch, yaw;
	float nearPlane, farPlane; // Add these members

	Camera()
		: target(0.0f), distance(10.0f), pitch(0.0f), yaw(-90.0f),
		lastX(0.0f), lastY(0.0f), firstMouse(true),
		rightButtonPressed(false), middleButtonPressed(false),
		nearPlane(0.1f), farPlane(10000.0f) {
	} // Initialize near and far planes

	glm::mat4 getViewMatrix() {
		glm::vec3 position;
		// spherical coords with Z-up
		position.x = target.x + distance * cos(glm::radians(pitch)) * cos(glm::radians(yaw));
		position.y = target.y + distance * cos(glm::radians(pitch)) * sin(glm::radians(yaw));
		position.z = target.z + distance * sin(glm::radians(pitch));

		// world-up is now +Z
		return glm::lookAt(position, target, glm::vec3(0, 0, 1));
	}

	glm::mat4 getProjectionMatrix(float aspectRatio, float nearPlane, float farPlane) {
		return glm::perspective(glm::radians(45.0f), aspectRatio, nearPlane, farPlane);
	}

	glm::vec3 getPosition() {
		glm::vec3 position;
		position.x = target.x + distance * cos(glm::radians(pitch)) * cos(glm::radians(yaw));
		position.y = target.y + distance * cos(glm::radians(pitch)) * sin(glm::radians(yaw));
		position.z = target.z + distance * sin(glm::radians(pitch));
		return position;
	}

	void setTarget(const glm::vec3& p) { target = p; }

	void processInput(GLFWwindow* window) {
		ImGuiIO& io = ImGui::GetIO();
		if (io.WantCaptureMouse) return;

		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);

		// Handle rotation with right mouse button
		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
			if (!rightButtonPressed) {
				firstMouse = true;
				rightButtonPressed = true;
			}

			if (firstMouse) {
				lastX = xpos;
				lastY = ypos;
				firstMouse = false;
			}

			float xoffset = xpos - lastX;
			float yoffset = lastY - ypos; // Reversed since y-coordinates go from bottom to top

			lastX = xpos;
			lastY = ypos;

			float sensitivity = 0.1f;
			xoffset *= sensitivity;
			yoffset *= sensitivity;

			yaw += xoffset;
			pitch += yoffset;

			if (pitch > 89.0f) pitch = 89.0f;
			if (pitch < -89.0f) pitch = -89.0f;
		}
		else {
			rightButtonPressed = false;
		}

		// Handle panning with middle mouse button
		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
			if (!middleButtonPressed) {
				firstMouse = true;
				middleButtonPressed = true;
			}

			if (firstMouse) {
				lastX = xpos;
				lastY = ypos;
				firstMouse = false;
			}

			float xoffset = xpos - lastX;
			float yoffset = lastY - ypos;

			lastX = xpos;
			lastY = ypos;

			float panSensitivity = 0.005f; // Adjust as needed

			xoffset *= panSensitivity * distance;
			yoffset *= panSensitivity * distance;

			// Calculate right and up vectors
			glm::vec3 front;
			front.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
			front.y = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
			front.z = sin(glm::radians(pitch));

			glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 0, 1)));
			glm::vec3 up = glm::normalize(glm::cross(right, front));

			// Adjust the target position
			target -= right * xoffset;
			target += up * yoffset;
		}
		else {
			middleButtonPressed = false;
		}
	}

	static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
		Camera* cam = static_cast<Camera*>(glfwGetWindowUserPointer(window));
		if (cam) {
			cam->distance *= (1.0f - static_cast<float>(yoffset) * 0.1f);
			//if (cam->distance < 1.0f) cam->distance = 1.0f;
			//if (cam->distance > 1000.0f) cam->distance = 1000.0f;
		}
	}

private:
	double lastX, lastY;
	bool firstMouse;
	bool rightButtonPressed;
	bool middleButtonPressed;
};

#endif // CAMERA_H
