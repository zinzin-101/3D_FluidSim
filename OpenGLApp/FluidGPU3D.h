#pragma once
#include "Fluid.h"
#include "ComputeShader.h"
#include <shader.h>
#include <camera.h>

class FluidGPU3D : public Fluid {
private:
	int sizeZ;

	ComputeShader integrateShader;
	ComputeShader incompressibilityShader;
	ComputeShader extrapolateShader;
	ComputeShader advectVelocityShader;
	ComputeShader advectSmokeShader;
	ComputeShader setObstacleShader;
	unsigned int velocityTexture; // R channel: U, G channel: V, B channel: W
	unsigned int freeSpaceTexture; // R channel only
	unsigned int pressureTexture; // R channel only
	unsigned int newVelocityTexture; // R channel: U, G channel: V, B channel: W
	unsigned int smokeTexture; // R channel only
	unsigned int newSmokeTexture; // R channel only

	// rendering
	std::vector<glm::vec3> volumeSlices; // for rendering with 3D texture slicing method
	unsigned int vao;
	unsigned int vbo;
	Shader volumeShader;

	virtual void integrate(float dt, float gravity) override {
		integrateShader.use();
		integrateShader.setFloat("dt", dt);
		integrateShader.setVec3("gravity", gravity * glm::normalize(gravityDirection));

		glBindImageTexture(0, velocityTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA32F);
		glBindImageTexture(1, freeSpaceTexture, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32F);

		glDispatchCompute((sizeX + 7) / 8, (sizeY + 7) / 8, (sizeZ + 7) / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	virtual void solveIncompressibility(float dt, int iterations) override {
		incompressibilityShader.use();

		// reset all to zero
		float zero = 0.0f;
		glClearTexImage(pressureTexture, 0, GL_RED, GL_FLOAT, &zero);

		float pressureScaling = density * spacing / dt;
		incompressibilityShader.setFloat("pressureScaling", pressureScaling);
		incompressibilityShader.setFloat("overrelaxation", 1.9f);
		incompressibilityShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);

		glBindImageTexture(0, velocityTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA32F);
		glBindImageTexture(1, freeSpaceTexture, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32F);
		glBindImageTexture(2, pressureTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);

		// using 8-color Gauss-Seidel method
		for (int i = 0; i < iterations; i++) {
			for (int j = 0; j < 8; j++) {
				incompressibilityShader.setInt("pass", j);
				glDispatchCompute((sizeX + 7) / 8, (sizeY + 7) / 8, (sizeZ + 7) / 8);
				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			}
		}
	}

	virtual void extrapolate() override {
		extrapolateShader.use();
		glBindImageTexture(0, velocityTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA32F);
		glDispatchCompute((sizeX + 7) / 8, (sizeY + 7) / 8, (sizeZ + 7) / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	virtual void advectVelocity(float dt) override {
		advectVelocityShader.use();
		advectVelocityShader.setFloat("dt", dt);
		advectVelocityShader.setFloat("spacing", spacing);

		glBindImageTexture(0, newVelocityTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_3D, velocityTexture);
		advectVelocityShader.setInt("velocityTexture", 2);

		glDispatchCompute((sizeX + 7) / 8, (sizeY + 7) / 8, (sizeZ + 7) / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		std::swap(velocityTexture, newVelocityTexture);
	}

	virtual void advectSmoke(float dt) override {
		advectSmokeShader.use();
		advectSmokeShader.setFloat("dt", dt);
		advectSmokeShader.setFloat("spacing", spacing);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, velocityTexture);
		advectSmokeShader.setInt("velocityTexture", 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_3D, smokeTexture);
		advectSmokeShader.setInt("smokeTexture", 1);

		glBindImageTexture(0, newSmokeTexture, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R32F);
		glBindImageTexture(1, freeSpaceTexture, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32F);

		glDispatchCompute((sizeX + 7) / 8, (sizeY + 7) / 8, (sizeZ + 7) / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		std::swap(smokeTexture, newSmokeTexture);
	}

	virtual void setInitialObstacle(float dt, bool reset) {
		setObstacleShader.use();

		setObstacleShader.setFloat("radius", obstacleRadius);
		setObstacleShader.setFloat("spacing", spacing);
		setObstacleShader.setBool("isReset", reset);
		setObstacleShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);
		setObstacleShader.setFloat("smokeColor", 1.0f);

		// Bind 3D Textures
		glBindImageTexture(0, velocityTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA32F);
		glBindImageTexture(1, freeSpaceTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);
		glBindImageTexture(2, smokeTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32F);

		//for (float x = -0.5f; x <= 0.5f; x += 0.5f) {
		//	for (float z = -0.5f; z <= 0.5f; z += 0.5f) {
		//		float centerX = (sizeX * spacing) * 0.5f + (obstacleRadius * x);
		//		float centerY = obstacleRadius * 2.0f + spacing;
		//		float centerZ = (sizeZ * spacing) * 0.5f + +(obstacleRadius * z);
		//		setObstacleShader.setVec3("obstaclePos", centerX, centerY, centerZ);
		//		glDispatchCompute((sizeX + 7) / 8, (sizeY + 7) / 8, (sizeZ + 7) / 8);
		//		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		//	}
		//}

		float centerX = (sizeX * spacing) * 0.5f;
		float centerY = (sizeY * spacing) * 0.5f;
		float centerZ = (sizeZ * spacing) * 0.5f;
		setObstacleShader.setVec3("obstaclePos", centerX, centerY, centerZ);
		glDispatchCompute((sizeX + 7) / 8, (sizeY + 7) / 8, (sizeZ + 7) / 8);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	int findAbsMaxDimension(glm::vec3 v) {
		v.x = std::abs(v.x);
		v.y = std::abs(v.y);
		v.z = std::abs(v.z);

		int maxDimension = 0;
		float value = v.x;
		if (v.y > value)
		{
			value = v.y;
			maxDimension = 1;
		}
		if (v.z > value)
		{
			value = v.z;
			maxDimension = 2;
		}
		return maxDimension;
	}

public:
	glm::vec3 gravityDirection;

	FluidGPU3D(float density, int width, int height, int depth, float spacing, float obstacleRadius) :
		Fluid(density, width, height, spacing, obstacleRadius, depth + BORDER_SIZE),
		sizeZ(depth + BORDER_SIZE),
		integrateShader("integrate.comp"),
		incompressibilityShader("incompressibility.comp"),
		extrapolateShader("extrapolate.comp"),
		advectVelocityShader("advect_velocity.comp"),
		advectSmokeShader("advect_smoke.comp"),
		setObstacleShader("set_obstacle.comp"),
		volumeShader("volume.vert", "volume.frag"),
		velocityTexture{}, freeSpaceTexture{}, pressureTexture{}, newVelocityTexture{}, smokeTexture{}, newSmokeTexture{}
	{
		integrateShader.use();
		integrateShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);

		incompressibilityShader.use();
		incompressibilityShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);

		extrapolateShader.use();
		extrapolateShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);

		advectVelocityShader.use();
		advectVelocityShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);

		advectSmokeShader.use();
		advectSmokeShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);

		setObstacleShader.use();
		setObstacleShader.setIVec3("gridSize", sizeX, sizeY, sizeZ);

		gravityDirection = glm::vec3(0.0f, 1.0f, 0.0f);

		// velocity
		glGenTextures(1, &velocityTexture);
		glBindTexture(GL_TEXTURE_3D, velocityTexture);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, sizeX, sizeY, sizeZ, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glGenTextures(1, &newVelocityTexture);
		glBindTexture(GL_TEXTURE_3D, newVelocityTexture);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, sizeX, sizeY, sizeZ, 0, GL_RGBA, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		// free space
		glGenTextures(1, &freeSpaceTexture);
		glBindTexture(GL_TEXTURE_3D, freeSpaceTexture);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sizeX, sizeY, sizeZ, 0, GL_RED, GL_FLOAT, freeSpace.data());
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		// pressure
		glGenTextures(1, &pressureTexture);
		glBindTexture(GL_TEXTURE_3D, pressureTexture);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sizeX, sizeY, sizeZ, 0, GL_RED, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		// smoke
		glGenTextures(1, &smokeTexture);
		glBindTexture(GL_TEXTURE_3D, smokeTexture);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sizeX, sizeY, sizeZ, 0, GL_RED, GL_FLOAT, smoke.data());
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glGenTextures(1, &newSmokeTexture);
		glBindTexture(GL_TEXTURE_3D, newSmokeTexture);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_R32F, sizeX, sizeY, sizeZ, 0, GL_RED, GL_FLOAT, NULL);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		setInitialObstacle(1.0f / 60.0f, false);

		// rendering
		volumeSlices.resize(MAX_NUMBER_OF_SLICES * 12);

		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * (unsigned int)volumeSlices.size(), nullptr, GL_DYNAMIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
		glBindVertexArray(0);
	}

	virtual void update(float dt, float gravity, int iterations) override {

		integrate(dt, gravity);
		solveIncompressibility(dt, iterations);

		extrapolate();
		advectVelocity(dt);
		advectSmoke(dt);

		frameCount++;
	}

	unsigned int getSmokeTexture() const {
		return smokeTexture;
	}

	unsigned int getVelocityTexture() const {
		return velocityTexture;
	}

	unsigned int getFreeSpaceTexture() const {
		return freeSpaceTexture;
	}

	int getSizeZ() const {
		return sizeZ;
	}

	void render(glm::mat4 projection, glm::mat4 view, glm::mat4 model, const Camera& camera, int sliceCount = 128) {
		/// assuming the fluid position is always at the origin (0, 0, 0) ///

		sliceCount = glm::clamp(sliceCount, 16, MAX_NUMBER_OF_SLICES);

		static const glm::vec3 cubeVerts[8] =
		{
			glm::vec3(-0.5f, -0.5f, -0.5f),
			glm::vec3(0.5f, -0.5f, -0.5f),
			glm::vec3(0.5f,  0.5f, -0.5f),
			glm::vec3(-0.5f,  0.5f, -0.5f),
			glm::vec3(-0.5f, -0.5f,  0.5f),
			glm::vec3(0.5f, -0.5f,  0.5f),
			glm::vec3(0.5f,  0.5f,  0.5f),
			glm::vec3(-0.5f,  0.5f,  0.5f)
		};

		static const int edgeList[8][12] = {
			{ 0,1,5,6,   4,8,11,9,  3,7,2,10 }, // v0 is front
			{ 0,4,3,11,  1,2,6,7,   5,9,8,10 }, // v1 is front
			{ 1,5,0,8,   2,3,7,4,   6,10,9,11}, // v2 is front
			{ 7,11,10,8, 2,6,1,9,   3,0,4,5  }, // v3 is front
			{ 8,5,9,1,   11,10,7,6, 4,3,0,2  }, // v4 is front
			{ 9,6,10,2,  8,11,4,7,  5,0,1,3  }, // v5 is front
			{ 9,8,5,4,   6,1,2,0,   10,7,11,3}, // v6 is front
			{ 10,9,6,5,  7,2,3,1,   11,4,8,0 }  // v7 is front
		};

		static const int edges[12][2] = {
			{0,1},{1,2},{2,3},{3,0}, 
			{0,4},{1,5},{2,6},{3,7}, 
			{4,5},{5,6},{6,7},{7,4}
		};

		glm::vec3 viewDir = glm::normalize(camera.Front);
		float maxDist = glm::dot(viewDir, cubeVerts[0]);
		float minDist = maxDist;
		int maxIndex = 0;
		int totalSliceVertices = 0;

		for (int i = 1; i < 8; i++) {
			float dist = glm::dot(viewDir, cubeVerts[i]);
			if (dist > maxDist) {
				maxDist = dist;
				maxIndex = i;
			}
			if (dist < minDist) {
				minDist = dist;
			}
		}

		int maxDimension = findAbsMaxDimension(viewDir);

		minDist -= FLT_EPSILON;
		maxDist += FLT_EPSILON;

		glm::vec3 vecStart[12];
		glm::vec3 vecDir[12];
		float lambda[12];
		float lambdaIncrement[12];
		float denom = 0;

		float planeDist = minDist;
		float planeDistIncrement = (maxDist - minDist) / (float)sliceCount;

		for (int i = 0; i < 12; i++) {
			vecStart[i] = cubeVerts[edges[edgeList[maxIndex][i]][0]];
			vecDir[i] = cubeVerts[edges[edgeList[maxIndex][i]][1]] - vecStart[i];

			denom = glm::dot(vecDir[i], viewDir);

			if (1.0f + denom != 1.0) {
				lambdaIncrement[i] = planeDistIncrement / denom;
				lambda[i] = (planeDist - glm::dot(vecStart[i], viewDir)) / denom;
			}
			else {
				lambda[i] = -1.0;
				lambdaIncrement[i] = 0.0;
			}
		}

		static const int indices[] = { 0,1,2, 0,2,3, 0,3,4, 0,4,5 };

		glm::vec3 intersection[6];
		float dL[12];

		for (int i = sliceCount - 1; i >= 0; i--) {
			
			for (int e = 0; e < 12; e++)
			{
				dL[e] = lambda[e] + i * lambdaIncrement[e];
			}

			if ((dL[0] >= 0.0) && (dL[0] < 1.0)) {
				intersection[0] = vecStart[0] + dL[0] * vecDir[0];
			}
			else if ((dL[1] >= 0.0) && (dL[1] < 1.0)) {
				intersection[0] = vecStart[1] + dL[1] * vecDir[1];
			}
			else if ((dL[3] >= 0.0) && (dL[3] < 1.0)) {
				intersection[0] = vecStart[3] + dL[3] * vecDir[3];
			}
			else continue;

			if ((dL[2] >= 0.0) && (dL[2] < 1.0)) {
				intersection[1] = vecStart[2] + dL[2] * vecDir[2];
			}
			else if ((dL[0] >= 0.0) && (dL[0] < 1.0)) {
				intersection[1] = vecStart[0] + dL[0] * vecDir[0];
			}
			else if ((dL[1] >= 0.0) && (dL[1] < 1.0)) {
				intersection[1] = vecStart[1] + dL[1] * vecDir[1];
			}
			else {
				intersection[1] = vecStart[3] + dL[3] * vecDir[3];
			}

			if ((dL[4] >= 0.0) && (dL[4] < 1.0)) {
				intersection[2] = vecStart[4] + dL[4] * vecDir[4];
			}
			else if ((dL[5] >= 0.0) && (dL[5] < 1.0)) {
				intersection[2] = vecStart[5] + dL[5] * vecDir[5];
			}
			else {
				intersection[2] = vecStart[7] + dL[7] * vecDir[7];
			}
			if ((dL[6] >= 0.0) && (dL[6] < 1.0)) {
				intersection[3] = vecStart[6] + dL[6] * vecDir[6];
			}
			else if ((dL[4] >= 0.0) && (dL[4] < 1.0)) {
				intersection[3] = vecStart[4] + dL[4] * vecDir[4];
			}
			else if ((dL[5] >= 0.0) && (dL[5] < 1.0)) {
				intersection[3] = vecStart[5] + dL[5] * vecDir[5];
			}
			else {
				intersection[3] = vecStart[7] + dL[7] * vecDir[7];
			}
			if ((dL[8] >= 0.0) && (dL[8] < 1.0)) {
				intersection[4] = vecStart[8] + dL[8] * vecDir[8];
			}
			else if ((dL[9] >= 0.0) && (dL[9] < 1.0)) {
				intersection[4] = vecStart[9] + dL[9] * vecDir[9];
			}
			else {
				intersection[4] = vecStart[11] + dL[11] * vecDir[11];
			}

			if ((dL[10] >= 0.0) && (dL[10] < 1.0)) {
				intersection[5] = vecStart[10] + dL[10] * vecDir[10];
			}
			else if ((dL[8] >= 0.0) && (dL[8] < 1.0)) {
				intersection[5] = vecStart[8] + dL[8] * vecDir[8];
			}
			else if ((dL[9] >= 0.0) && (dL[9] < 1.0)) {
				intersection[5] = vecStart[9] + dL[9] * vecDir[9];
			}
			else {
				intersection[5] = vecStart[11] + dL[11] * vecDir[11];
			}

			for (int i = 0; i < 12; i++) {
				volumeSlices[totalSliceVertices++] = intersection[indices[i]];
			}

		}

		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);

		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, totalSliceVertices * sizeof(glm::vec3), &(volumeSlices[0].x));

		glm::mat4 mvp = projection * view * model;
		volumeShader.use();
		volumeShader.setMat4("mvp", mvp);
		volumeShader.setInt("volume", 0);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_3D, smokeTexture);

		glBindVertexArray(vao);
		glDrawArrays(GL_TRIANGLES, 0, totalSliceVertices);
		glBindVertexArray(0);

		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
	}
};