#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;

uniform vec4 primitiveColor;
uniform vec3 lightPos;
uniform vec3 viewPos;

// NEW: Controls whether to apply lighting or not
uniform bool useLighting;

void main()
{
	if (!useLighting) {
		// No lighting, just use the RGBA color as-is
		FragColor = primitiveColor;
		return;
	}

	// Existing lighting code for overlay meshes that do have normals set:
	float ambientStrength = 0.2;
	vec3 ambient = ambientStrength * primitiveColor.rgb;

	vec3 norm = normalize(Normal);
	vec3 lightDir = normalize(lightPos - FragPos);
	float diff = max(dot(norm, lightDir), 0.0);
	vec3 diffuse = diff * primitiveColor.rgb;

	float specularStrength = 0.5;
	vec3 viewDir = normalize(viewPos - FragPos);
	vec3 reflectDir = reflect(-lightDir, norm);
	float spec = pow(max(dot(viewDir, reflectDir), 0.0), 16);
	vec3 specular = specularStrength * spec * vec3(1.0);

	vec3 result = ambient + diffuse + specular;
	FragColor = vec4(result, primitiveColor.a);
}
