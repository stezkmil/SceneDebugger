#version 330 core
out vec4 FragColor;

uniform vec4 primitiveColor;

void main() {
	FragColor = primitiveColor;
}
