#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <shader.h>
#include <filesystem.h>

#include "ComputeShader.h"
#include "FluidGPU3D.h"
#include <camera.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>
#include <limits>
#include <thread>
#include <map>
#include <vector>

void frameBufferSizeCallback(GLFWwindow* window, int width, int height);
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void mouseCallBack(GLFWwindow* window, double xpos, double ypos);
void processInput(GLFWwindow* window);

// settings
//#define FULLSCREEN
#ifdef FULLSCREEN
const unsigned int WIDTH = 1920;
const unsigned int HEIGHT = 1080;
#else
const unsigned int WIDTH = 1000;
const unsigned int HEIGHT = 1000;
#endif

//const float WORLD_WIDTH = 226.65f;
//const float WORLD_HEIGHT = 127.5f;
const float FIX_DT = 1.0f / 60.0f;
float deltaTime = 0.0f;
float lastTime = 0.0f;

// glfw
GLFWwindow* window = nullptr;
void toggleFullscreen(GLFWwindow* window);
float mouseX = 0.0f;
float mouseY = 0.0f;
float lastX = WIDTH / 2.0f;
float lastY = HEIGHT / 2.0f;
FluidGPU3D* fluidPtr = nullptr;
//FluidGPU* fluidPtr = nullptr;
bool isMouseDown = false;
bool isRightMouseDown = false;
bool showFreeSpace = false;
int z = 64;


// rendering
unsigned int vao;
unsigned int vbo;
unsigned int ebo;
unsigned int smokeTexture;

// controls
std::map<unsigned, bool> keyDownMap;
bool getKeyDown(GLFWwindow* window, unsigned int key);

Camera camera;

void get2DTextureSliceFrom3DTexture(ComputeShader& sliceShader, int z, int sizeX, int sizeY, int sizeZ, unsigned int source, unsigned int destination);

int main() {
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
	#ifdef FULLSCREEN
	window = glfwCreateWindow(WIDTH, HEIGHT, "3D_FluidSim", primaryMonitor, NULL);
	#else
	window = glfwCreateWindow(WIDTH, HEIGHT, "3D_FluidSim", NULL, NULL);
	glfwSetWindowPos(window, (mode->width / 2) - (WIDTH / 2), (mode->height / 2) - (HEIGHT / 2));
	#endif
	if (window == NULL) {
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	//glfwSwapInterval(0);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}

	glfwSetFramebufferSizeCallback(window, frameBufferSizeCallback);
	glfwSetMouseButtonCallback(window, mouseButtonCallback);
	glfwSetCursorPosCallback(window, mouseCallBack);

	srand(time(NULL));

	stbi_set_flip_vertically_on_load(true);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//glDisable(GL_CULL_FACE);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);

	FluidGPU3D fluid(DENSITY, 128, 128, 128, SPACING, OBSTACLE_RADIUS);
	fluidPtr = &fluid;

	// setup quad
	float quadVertices[] = {
		// position          // uv
		 1.0f,  1.0f, 0.0f,  1.0f, 1.0f,   
		 1.0f, -1.0f, 0.0f,  1.0f, 0.0f,   
		-1.0f, -1.0f, 0.0f,  0.0f, 0.0f,   
		-1.0f,  1.0f, 0.0f,  0.0f, 1.0f    
	};

	unsigned int quadIndices[] = {
		0, 3, 2, 
		2, 1, 0  
	};

	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glGenBuffers(1, &ebo);
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(quadIndices), quadIndices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glBindVertexArray(0);

	// setup smoke texture
	glGenTextures(1, &smokeTexture);
	glBindTexture(GL_TEXTURE_2D, smokeTexture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32F, fluid.getSizeX(), fluid.getSizeY());

	Shader shader = Shader("smoke.vert", "smoke.frag");
	shader.use();
	shader.setInt("smokeTexture", 0);

	ComputeShader sliceShader = ComputeShader("slice.comp");

	while (!glfwWindowShouldClose(window)) {
		processInput(window);

		float currentTime = (float)glfwGetTime();
		deltaTime = currentTime - lastTime;
		lastTime = currentTime;

		fluidPtr->update(1.0f / 30.0f, 9.81f, 40);

		// update
		//if (isMouseDown) {
		//	fluidPtr->setObstacle(1.0f / 60.0f, mouseX, mouseY, isRightMouseDown);
		//}

		// render	
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		//glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
		//glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		get2DTextureSliceFrom3DTexture(sliceShader, z, fluid.getSizeX(), fluid.getSizeY(), fluid.getSizeZ(), fluid.getSmokeTexture(), smokeTexture);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, smokeTexture);

		shader.use();
		//glm::mat4 projection = glm::ortho(0.0f, (float)WIDTH, 0.0f, (float)HEIGHT);
		//glm::mat4 projection(1.0f);
		glm::mat4 view = camera.GetViewMatrix();
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)WIDTH / (float)HEIGHT, 0.1f, 1000.0f);
		glm::mat4 model(1.0f);
		model = glm::scale(model, glm::vec3(2.0f));
		//shader.setMat4("view", view);
		//shader.setMat4("projection", projection);
		//shader.setMat4("model", model);
		//glBindVertexArray(vao);
		//glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

		glDisable(GL_DEPTH_TEST);
		fluid.render(projection, view, model, camera);
		glEnable(GL_DEPTH_TEST);

		// show average FPS
		static unsigned int frameNum = 0;
		static double timeElapsed = 0.0;
		static double fps = 0.0;

		timeElapsed += deltaTime;
		frameNum++;

		if (timeElapsed >= 1.0f) {
			fps = frameNum / timeElapsed;
			timeElapsed = 0.0f;
			frameNum = 0;
			std::cout << "FPS: " << fps << std::endl;
		}

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	return 0; 
}

void frameBufferSizeCallback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
}

void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
		isMouseDown = true;
	}
	else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
		isMouseDown = false;
	}

	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
		isRightMouseDown = true;
	}
	else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
		isRightMouseDown = false;
	}
}

void mouseCallBack(GLFWwindow* window, double xpos, double ypos) {
	if (fluidPtr == nullptr) return;

	int width, height;
	glfwGetWindowSize(window, &width, &height);

	float x = (float)xpos / (float)width;
	float y = ((float)height - (float)ypos) / (float)height;

	float worldWidth = fluidPtr->getSizeX() * fluidPtr->getSpacing();
	float worldHeight = fluidPtr->getSizeY() * fluidPtr->getSpacing();

	mouseX = y * worldWidth;
	mouseY = x * worldHeight;


	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos; 
	lastX = xpos;
	lastY = ypos;
	if (isRightMouseDown)
		camera.ProcessMouseMovement(xoffset, yoffset);
}

void processInput(GLFWwindow* window) {
	if (getKeyDown(window, GLFW_KEY_ESCAPE)) {
		glfwSetWindowShouldClose(window, true);
	}

	if (getKeyDown(window, GLFW_KEY_SPACE)) {
		showFreeSpace = !showFreeSpace;
	}

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.ProcessKeyboard(FORWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.ProcessKeyboard(LEFT, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.ProcessKeyboard(RIGHT, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
		camera.ProcessKeyboard(UP, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		camera.ProcessKeyboard(DOWN, deltaTime);

	//if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
	//	z += 1;
	//if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
	//	z -= 1;
	//z = glm::clamp(z, 0, 127);
	//std::cout << "z: " << z << std::endl;
	//std::cout << "campos: " << camera.Position.x << ", " << camera.Position.y << ", " << camera.Position.z << std::endl;

	if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS)
		fluidPtr->gravityDirection += camera.Up;
	if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS)
		fluidPtr->gravityDirection += -camera.Up;
	if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS)
		fluidPtr->gravityDirection += -camera.Right;
	if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS)
		fluidPtr->gravityDirection += camera.Right;
	fluidPtr->gravityDirection = glm::normalize(fluidPtr->gravityDirection);
}

void toggleFullscreen(GLFWwindow* window) {
	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

	if (glfwGetWindowMonitor(window)) { // full screen -> windowed
		glfwSetWindowMonitor(window, NULL, (mode->width / 2) - (WIDTH / 2), (mode->height / 2) - (HEIGHT / 2), WIDTH, HEIGHT, GLFW_DONT_CARE);
	}
	else { // windowed -> full screen
		glfwSetWindowMonitor(window, primaryMonitor, 0, 0, mode->width, mode->height, mode->refreshRate);
	}
}

bool getKeyDown(GLFWwindow* window, unsigned int key) {
	// init
	if (keyDownMap.count(key) == 0) {
		keyDownMap[key] = false;
		return false;
	}

	if (glfwGetKey(window, key) == GLFW_PRESS && keyDownMap.at(key)) {
		return false;
	}

	if (glfwGetKey(window, key) == GLFW_RELEASE && keyDownMap.at(key)) {
		keyDownMap[key] = false;
		return false;
	}

	if (glfwGetKey(window, key) == GLFW_PRESS && !keyDownMap.at(key)) {
		keyDownMap[key] = true;
		return true;
	}
}

glm::vec3 getSciColor(float val, float minVal, float maxVal) {
	val = std::min(std::max(val, minVal), maxVal - 0.0001f);
	float d = maxVal - minVal;
	val = isNear(d, 0.0f) ? 0.5f : (val - minVal) / d;
	float m = 0.25f;
	int num = std::floor(val / m);
	float s = (val - num * m) / m;
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;

	switch (num) {
		case 0: r = 0.0f; g = s; b = 1.0f; break;
		case 1: r = 0.0f; g = 1.0f; b = 1.0f - s; break;
		case 2: r = s; g = 1.0f; b = 0.0f; break;
		case 3: r = 1.0f; g = 1.0f - s; b = 0.0f; break;
	}

	return glm::vec3(r, g, b);
}

void get2DTextureSliceFrom3DTexture(ComputeShader& sliceShader, int z, int sizeX, int sizeY, int sizeZ, unsigned int source, unsigned int destination) {
	sliceShader.use();
	sliceShader.setInt("index", z);
	sliceShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_3D, source);
	sliceShader.setInt("tex3D", 0);

	glBindImageTexture(0, destination, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);

	glDispatchCompute((sizeX + 15) / 16, (sizeY + 15) / 16, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}