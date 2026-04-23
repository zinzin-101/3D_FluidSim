#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <shader.h>
#include <filesystem.h>

#include "Fluid.h"
#include "FluidGPU.h"
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
class Fluid;
class FluidGPU;
Fluid* fluidPtr = nullptr;
//FluidGPU* fluidPtr = nullptr;
bool isMouseDown = false;
bool isRightMouseDown = false;
bool showFreeSpace = false;
bool useGPU = true;


// rendering
unsigned int vao;
unsigned int vbo;
unsigned int ebo;
unsigned int smokeTexture;

// controls
std::map<unsigned, bool> keyDownMap;
bool getKeyDown(GLFWwindow* window, unsigned int key);

Camera camera;

int main() {
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
	#ifdef FULLSCREEN
	window = glfwCreateWindow(WIDTH, HEIGHT, "2D_FluidSim", primaryMonitor, NULL);
	#else
	window = glfwCreateWindow(WIDTH, HEIGHT, "2D_FluidSim", NULL, NULL);
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

	//Fluid fluid = Fluid(DENSITY, 512, 512, SPACING, OBSTACLE_RADIUS / 4.0f);
	Fluid fluid = Fluid(DENSITY, 512, 512, SPACING, OBSTACLE_RADIUS);
	FluidGPU fluidGPU = FluidGPU(DENSITY, 512, 512, SPACING, OBSTACLE_RADIUS);
	fluidPtr = &fluidGPU;

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

	glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, fluid.getSizeX(), fluid.getSizeY(), 0, GL_RED, GL_FLOAT, NULL);

	Shader shader = Shader("smoke.vert", "smoke.frag");
	shader.use();
	shader.setInt("smokeTexture", 0);

	while (!glfwWindowShouldClose(window)) {
		processInput(window);

		float currentTime = (float)glfwGetTime();
		deltaTime = currentTime - lastTime;
		lastTime = currentTime;

		if (useGPU) {
			fluidPtr = &fluidGPU;
			fluidPtr->update(1.0f / 60.0f, -98.1f, 40);
		}
		else {
			fluidPtr = &fluid;
			fluidPtr->update(1.0f / 60.0f, -9.81f, 40);
		}

		// update
		if (isMouseDown) {
			fluidPtr->setObstacle(1.0f / 60.0f, mouseX, mouseY, isRightMouseDown);
		}

		// render
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		//glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		//glBindTexture(GL_TEXTURE_2D, smokeTexture);
		//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fluid.getSizeX(), fluid.getSizeY(), GL_RED, GL_FLOAT, fluid.getMaterialData());
		switch (useGPU) {
			case true:
				glActiveTexture(GL_TEXTURE0);
				if (showFreeSpace) {
					glBindTexture(GL_TEXTURE_2D, fluidGPU.getFreeSpaceTexture());
				}
				else {
					glBindTexture(GL_TEXTURE_2D, fluidGPU.getSmokeTexture());
				}
				break;

			case false:
				glBindTexture(GL_TEXTURE_2D, smokeTexture);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fluid.getSizeX(), fluid.getSizeY(), GL_RED, GL_FLOAT, fluid.getMaterialData());
				break;
		}

		//glBindTexture(GL_TEXTURE_2D, fluid.getVelocityTexture());

		shader.use();
		//glm::mat4 projection = glm::ortho(0.0f, (float)WIDTH, 0.0f, (float)HEIGHT);
		//glm::mat4 projection(1.0f);
		glm::mat4 view = camera.GetViewMatrix();
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)WIDTH / (float)HEIGHT, 0.1f, 1000.0f);
		glm::mat4 model(1.0f);
		shader.setMat4("view", view);
		shader.setMat4("projection", projection);
		shader.setMat4("model", model);
		glBindVertexArray(vao);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);


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
			std::string mode = useGPU ? "GPU" : "CPU";
			std::cout << "FPS: " << fps << " mode: " << mode << std::endl;
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

	if (getKeyDown(window, GLFW_KEY_G)) {
		useGPU = !useGPU;
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