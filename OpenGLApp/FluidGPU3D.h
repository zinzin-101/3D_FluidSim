#pragma once
#include "Fluid.h"
#include "ComputeShader.h"

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

	virtual void integrate(float dt, float gravity) override {
		integrateShader.use();
		integrateShader.setFloat("dt", dt);
		integrateShader.setFloat("gravity", gravity);

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
		incompressibilityShader.setIVec2("gridSize", sizeX, sizeY);

		glBindImageTexture(0, velocityTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
		glBindImageTexture(1, freeSpaceTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);
		glBindImageTexture(2, pressureTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);

		// using 4-color Gauss-Seidel method
		for (int i = 0; i < iterations; i++) {
			for (int j = 0; j < 4; j++) {
				incompressibilityShader.setInt("pass", j);
				glDispatchCompute((sizeX + 15) / 16, (sizeY + 15) / 16, 1);
				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
			}
		}
	}

	virtual void extrapolate() override {
		extrapolateShader.use();
		glBindImageTexture(0, velocityTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
		glDispatchCompute((sizeX + 15) / 16, (sizeY + 15) / 16, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	virtual void advectVelocity(float dt) override {
		advectVelocityShader.use();
		advectVelocityShader.setFloat("dt", dt);
		advectVelocityShader.setFloat("spacing", spacing);

		glBindImageTexture(0, newVelocityTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

		glActiveTexture(GL_TEXTURE2);
		glBindTexture(GL_TEXTURE_2D, velocityTexture);
		advectVelocityShader.setInt("velocityTexture", 2);

		glDispatchCompute((sizeX + 15) / 16, (sizeY + 15) / 16, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		std::swap(velocityTexture, newVelocityTexture);
	}

	virtual void advectSmoke(float dt) override {
		advectSmokeShader.use();
		advectSmokeShader.setFloat("dt", dt);
		advectSmokeShader.setFloat("spacing", spacing);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, velocityTexture);
		advectSmokeShader.setInt("velocityTexture", 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, smokeTexture);
		advectSmokeShader.setInt("smokeTexture", 1);

		glBindImageTexture(0, newSmokeTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
		glBindImageTexture(1, freeSpaceTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32F);

		glDispatchCompute((sizeX + 15) / 16, (sizeY + 15) / 16, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		std::swap(smokeTexture, newSmokeTexture);
	}

public:
	FluidGPU3D(float density, int width, int height, int depth, float spacing, float obstacleRadius) :
		Fluid(density, width, height, spacing, obstacleRadius, depth + BORDER_SIZE),
		sizeZ(depth + BORDER_SIZE),
		integrateShader("integrate.comp"),
		incompressibilityShader("incompressibility.comp"),
		extrapolateShader("extrapolate.comp"),
		advectVelocityShader("advect_velocity.comp"),
		advectSmokeShader("advect_smoke.comp"),
		setObstacleShader("set_obstacle.comp"),
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
	}

	virtual void update(float dt, float gravity, int iterations) override {
		integrate(dt, gravity);
		solveIncompressibility(dt, iterations);

		extrapolate();
		advectVelocity(dt);
		advectSmoke(dt);

		frameCount++;
	}

	virtual void setObstacle(float dt, float x, float y, bool reset) override {
		std::swap(x, y);
		float vx = 0.0f;
		float vy = 0.0f;
		if (!reset) {
			vx = (x - obstacleX) / dt;
			vy = (y - obstacleY) / dt;
		}

		obstacleX = x;
		obstacleY = y;

		setObstacleShader.use();
		setObstacleShader.setVec2("mousePos", x, y);
		setObstacleShader.setVec2("mouseVel", vx, vy);
		setObstacleShader.setFloat("radius", obstacleRadius);
		setObstacleShader.setFloat("spacing", spacing);
		setObstacleShader.setBool("isReset", reset);
		setObstacleShader.setIVec2("gridSize", sizeX, sizeY);

		//float smokeColor = 0.5f + 0.5f * sin((float)frameCount * 2.0f);
		float smokeColor = 0.0f;
		setObstacleShader.setFloat("smokeColor", smokeColor);

		glBindImageTexture(0, velocityTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
		glBindImageTexture(1, freeSpaceTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);
		glBindImageTexture(2, smokeTexture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32F);

		glDispatchCompute((sizeX + 15) / 16, (sizeY + 15) / 16, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
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
};