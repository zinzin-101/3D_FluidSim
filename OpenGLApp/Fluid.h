#pragma once
#include "FluidConfig.h"
#include <cmath>
#include <vector>

bool isNear(float a, float b);

enum Fields {
	U_FIELD,
	V_FIELD,
	S_FIELD
};

class Fluid {
protected:
	float density;
	int sizeX;
	int sizeY;
	int totalCells;
	float spacing;
	std::vector<float> uSpeed;
	std::vector<float> vSpeed;
	std::vector<float> newSpeedU;
	std::vector<float> newSpeedV;
	std::vector<float> pressure;
	std::vector<float> freeSpace;
	std::vector<float> smoke;
	std::vector<float> newSmoke;

	int frameCount;
	float obstacleRadius;
	float obstacleX;
	float obstacleY;

	virtual void integrate(float dt, float gravity) {
		int n = sizeY;
		for (int x = BORDER_OFFSET; x < sizeX; x++) {
			for (int y = BORDER_OFFSET; y < sizeY; y++) {
				// check if it's a border cell
				float current = freeSpace.at(x * n + y);
				float below = freeSpace.at(x * n + y - 1);
				if (!isNear(current, 0.0f) && !isNear(below, 0.0f)) {
					vSpeed[x * n + y] += gravity * dt;
				}
			}
		}
	}

	virtual void solveIncompressibility(float dt, int iterations) {
		int n = sizeY;
		float pressureScaling = density * spacing / dt;
		for (int i = 0; i < iterations; i++) {
			for (int x = BORDER_OFFSET; x < sizeX - BORDER_OFFSET; x++) {
				for (int y = BORDER_OFFSET; y < sizeY - BORDER_OFFSET; y++) {
					float current = freeSpace.at(x * n + y);
					if (isNear(current, 0.0f)) continue;

					float left = freeSpace.at((x - 1) * n + y);
					float right = freeSpace.at((x + 1) * n + y);;
					float top = freeSpace.at(x * n + y + 1);
					float bottom = freeSpace.at(x * n + y - 1);
					float sum = left + right + top + bottom;
					if (isNear(sum, 0.0f)) continue;

					float div = uSpeed.at((x + 1) * n + y) - uSpeed.at(x * n + y) + vSpeed.at(x * n + y + 1) - vSpeed.at(x * n + y);
					float pressureCorrection = -div / sum;
					pressureCorrection *= OVERRELAXATION;
					pressure[x * n + y] += pressureScaling * pressureCorrection;

					uSpeed[x * n + y] -= left * pressureCorrection;
					uSpeed[(x + 1) * n + y] += right * pressureCorrection;
					vSpeed[x * n + y] -= bottom * pressureCorrection;
					vSpeed[x * n + y + 1] += top * pressureCorrection;
				}
			}
		}
	}

	virtual void extrapolate() {
		int n = sizeY;
		for (int x = 0; x < sizeX; x++) {
			uSpeed[x * n + 0] = uSpeed.at(x * n + 1);
			uSpeed[x * n + sizeY - BORDER_OFFSET] = uSpeed.at(x * n + sizeY - BORDER_SIZE);
		}
		for (int y = 0; y < sizeY; y++) {
			vSpeed[0 * n + y] = vSpeed.at(1 * n + y);
			vSpeed[(sizeX - BORDER_OFFSET) * n + y] = vSpeed.at((sizeX - BORDER_SIZE) * n + y);
		}
	}

	virtual float sampleField(float x, float y, Fields field) {
		int n = sizeY;
		float spacing1 = 1.0f / spacing;
		float spacing2 = 0.5f * spacing;

		x = std::max(std::min(x, sizeX * spacing), spacing);
		y = std::max(std::min(y, sizeY * spacing), spacing);

		float dx = 0.0f;
		float dy = 0.0f;
		std::vector<float>* f = nullptr;
		switch (field) {
		case U_FIELD:
			f = &uSpeed;
			dy = spacing2;
			break;

		case V_FIELD:
			f = &vSpeed;
			dx = spacing2;
			break;

		case S_FIELD:
			f = &smoke;
			dx = spacing2;
			dy = spacing2;
			break;
		}

		int x0 = std::min((int)std::floor((x - dx) * spacing1), sizeX - BORDER_OFFSET);
		float tx = ((x - dx) - x0 * spacing) * spacing1;
		int x1 = std::min(x0 + 1, sizeX - BORDER_OFFSET);

		int y0 = std::min((int)std::floor((y - dy) * spacing1), sizeY - BORDER_OFFSET);
		float ty = ((y - dy) - y0 * spacing) * spacing1;
		int y1 = std::min(y0 + 1, sizeY - BORDER_OFFSET);

		float sx = 1.0f - tx;
		float sy = 1.0f - ty;

		if (f == nullptr) return 0.0f;

		std::vector<float>& temp = *f;
		float value =
			sx * sy * temp.at(x0 * n + y0) +
			tx * sy * temp.at(x1 * n + y0) +
			tx * ty * temp.at(x1 * n + y1) +
			sx * ty * temp.at(x0 * n + y1);

		return value;
	}

	float getAverageUSpeedAtCell(int x, int y) {
		int n = sizeY;
		float u = (uSpeed.at(x * n + y - 1) + uSpeed.at(x * n + y) +
			uSpeed.at((x + 1) * n + y - 1) + uSpeed.at((x + 1) * n + y)) * 0.25f;
		return u;
	}

	float getAverageVSpeedAtCell(int x, int y) {
		int n = sizeY;
		float v = (vSpeed.at((x - 1) * n + y) + vSpeed.at(x * n + y) +
			vSpeed.at((x - 1) * n + y + 1) + vSpeed.at(x * n + y + 1)) * 0.25f;
		return v;
	}

	virtual void advectVelocity(float dt) {
		newSpeedU = uSpeed;
		newSpeedV = vSpeed;

		int n = sizeY;
		float spacing2 = 0.5f * spacing;

		for (int i = BORDER_OFFSET; i < sizeX; i++) {
			for (int j = BORDER_OFFSET; j < sizeY; j++) {
				// u component
				if (freeSpace.at(i * n + j) != 0.0 && freeSpace.at((i - BORDER_OFFSET) * n + j) != 0.0 && j < sizeY - BORDER_OFFSET) {
					float x = i * spacing;
					float y = j * spacing + spacing2;
					float u = uSpeed.at(i * n + j);
					float v = getAverageVSpeedAtCell(i, j);
					x -= u * dt;
					y -= v * dt;
					u = sampleField(x, y, U_FIELD);
					newSpeedU[i * n + j] = u;
				}
				// v component
				if (!isNear(freeSpace.at(i * n + j), 0.0f) && !isNear(freeSpace.at(i * n + j - BORDER_OFFSET), 0.0f) && i < sizeX - BORDER_OFFSET) {
					float x = i * spacing + spacing2;
					float y = j * spacing;
					float u = getAverageUSpeedAtCell(i, j);
					float v = vSpeed.at(i * n + j);
					x -= u * dt;
					y -= v * dt;
					v = sampleField(x, y, V_FIELD);
					newSpeedV[i * n + j] = v;
				}
			}
		}

		uSpeed = newSpeedU;
		vSpeed = newSpeedV;
	}

	virtual void advectSmoke(float dt) {
		newSmoke = smoke;

		int n = sizeY;
		float spacing2 = 0.5f * spacing;

		for (int i = BORDER_OFFSET; i < sizeX - BORDER_OFFSET; i++) {
			for (int j = BORDER_OFFSET; j < sizeY - BORDER_OFFSET; j++) {

				if (!isNear(freeSpace.at(i * n + j), 0.0f)) {
					float u = (uSpeed.at(i * n + j) + uSpeed.at((i + 1) * n + j)) * 0.5f;
					float v = (vSpeed.at(i * n + j) + vSpeed.at(i * n + j + 1)) * 0.5f;
					float x = i * spacing + spacing2 - dt * u;
					float y = j * spacing + spacing2 - dt * v;

					newSmoke[i * n + j] = sampleField(x, y, S_FIELD);
				}
			}
		}

		smoke = newSmoke;
	}


public:
	Fluid(float density, int width, int height, float spacing, float obstacleRadius, int sizeZ = 0) :
		density(density), sizeX(width + BORDER_SIZE), sizeY(height + BORDER_SIZE), spacing(spacing), obstacleRadius(obstacleRadius) {
		totalCells = sizeX * sizeY;

		if (sizeZ > 0) {
			totalCells *= sizeZ;
		}

		uSpeed = std::vector<float>(totalCells);
		vSpeed = std::vector<float>(totalCells);
		newSpeedU = std::vector<float>(totalCells);
		newSpeedV = std::vector<float>(totalCells);
		pressure = std::vector<float>(totalCells);
		freeSpace = std::vector<float>(totalCells);
		smoke = std::vector<float>(totalCells);
		newSmoke = std::vector<float>(totalCells);

		std::fill(smoke.begin(), smoke.end(), 1.0f);
		std::fill(freeSpace.begin(), freeSpace.end(), 1.0f);

		frameCount = 0;
		obstacleX = 0.0f;
		obstacleY = 0.0f;
	}

	virtual void update(float dt, float gravity, int iterations) {
		integrate(dt, gravity);

		std::fill(pressure.begin(), pressure.end(), 0.0f);
		solveIncompressibility(dt, iterations);

		extrapolate();
		advectVelocity(dt);
		advectSmoke(dt);

		frameCount++;
	}

	virtual void setObstacle(float dt, float x, float y, bool reset) {
		float vx = 0.0f;
		float vy = 0.0f;
		if (!reset) {
			vx = (x - obstacleX) / dt;
			vy = (y - obstacleY) / dt;
		}

		obstacleX = x;
		obstacleY = y;

		float r = obstacleRadius;
		int n = sizeY;
		float cellDiagonalLength = std::sqrt(2.0f) * spacing;

		for (int i = BORDER_OFFSET; i < sizeX - BORDER_SIZE; i++) {
			for (int j = BORDER_OFFSET; j < sizeY - BORDER_SIZE; j++) {
				freeSpace[i * n + j] = 1.0f;

				float dx = (i + 0.5f) * spacing - x;
				float dy = (j + 0.5f) * spacing - y;

				if (dx * dx + dy * dy < r * r) {
					freeSpace[i * n + j] = 0.0f;
					//smoke[i * n + j] = 0.5f + 0.5f * std::sin(0.1f * (float)frameCount);
					smoke[i * n + j] = 0.0f;
					uSpeed[i * n + j] = vx;
					uSpeed[(i + 1) * n + j] = vx;
					vSpeed[i * n + j] = vy;
					vSpeed[i * n + j + 1] = vy;
				}

			}
		}
	}

	int getSizeX() const {
		return sizeX;
	}

	int getSizeY() const {
		return sizeY;
	}

	float* getMaterialData() {
		return smoke.data();
	}

	float* getPressureData() {
		return pressure.data();
	}

	float getSpacing() const {
		return spacing;
	}
};


bool isNear(float a, float b) {
	return std::abs(a - b) < 1e-9;
}