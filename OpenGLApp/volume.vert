#version 330 core
  
layout(location = 0) in vec3 position;

uniform mat4 mvp;

smooth out vec3 uvw;

void main() {  
	gl_Position = mvp *vec4(position.xyz,1);
	uvw = position + vec3(0.5); // convert from [-0.5, 0.5] to [0, 1]
}