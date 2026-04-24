#version 330 core

layout(location = 0) out vec4 FragColor;

in vec3 uvw;

uniform sampler3D volume;

void main() {
	float density = texture(volume, uvw).r;
	vec3 smokeColor = vec3(1.0, 1.0, 1.0);
	FragColor = vec4(smokeColor, density);
	//FragColor = vec4(1.0f);
}