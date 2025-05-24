#pragma once
#include <cmath>
#include <random>
#include <algorithm>
#include <numeric>
#include <vector>
#include "glm_includes.h"
#include "scene/chunk.h" // For BlockType enum

class NoiseGenerator {
private:
    // Permutation table for fast hash lookups
    std::vector<int> perm;

public:
    NoiseGenerator(unsigned int seed = 12345) {
        // Initialize permutation table with values 0-255
        perm.resize(512);
        std::vector<int> p(256);
        std::iota(p.begin(), p.end(), 0);
        std::mt19937 rng(seed);
        std::shuffle(p.begin(), p.end(), rng);

        for (int i = 0; i < 256; i++) {
            perm[i] = perm[i + 256] = p[i];
        }
    }

    // quintic function for perlin noise
    float quintic(float t) const {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float lerp(float t, float a, float b) const {
        return a + t * (b - a);
    }

    float smoothstep(float edge0, float edge1, float x) const {
        x = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return x * x * (3 - 2 * x);
    }

    float grad(int hash, float x, float y, float z) const {
        // Convert low 4 bits of hash into 12 gradient directions
        int h = hash & 15;
        float u = h < 8 ? x : y;
        float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }

    // 3D Perlin noise
    float perlin(float x, float y, float z = 0.0f) const {
        int X = static_cast<int>(std::floor(x)) & 255;
        int Y = static_cast<int>(std::floor(y)) & 255;
        int Z = static_cast<int>(std::floor(z)) & 255;

        x -= std::floor(x);
        y -= std::floor(y);
        z -= std::floor(z);

        float u = quintic(x);
        float v = quintic(y);
        float w = quintic(z);

        // Hash coordinates of the 8 cube corners
        int A = perm[X] + Y;
        int AA = perm[A] + Z;
        int AB = perm[A + 1] + Z;
        int B = perm[X + 1] + Y;
        int BA = perm[B] + Z;
        int BB = perm[B + 1] + Z;

        // And add blended results from 8 corners of cube
        return lerp(w, lerp(v, lerp(u, grad(perm[AA], x, y, z),
                                    grad(perm[BA], x - 1, y, z)),
                            lerp(u, grad(perm[AB], x, y - 1, z),
                                 grad(perm[BB], x - 1, y - 1, z))),
                    lerp(v, lerp(u, grad(perm[AA + 1], x, y, z - 1),
                                 grad(perm[BA + 1], x - 1, y, z - 1)),
                         lerp(u, grad(perm[AB + 1], x, y - 1, z - 1),
                              grad(perm[BB + 1], x - 1, y - 1, z - 1))));
    }

    // FBM - layered Perlin noise
    float fractalNoise(float x, float y, float z = 0.0f, int octaves = 6, float persistence = 0.5f) const {
        float total = 0.0f;
        float frequency = 1.0f;
        float amplitude = 1.0f;
        float maxValue = 0.0f;  // Used for normalizing result

        for(int i = 0; i < octaves; i++) {
            total += perlin(x * frequency, y * frequency, z * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2.0f;
        }

        return total / maxValue;
    }

    // Voronoi noise for the grasslands
    float voronoiNoise(float x, float y, float z = 0.0f) const {
        int xi = static_cast<int>(std::floor(x));
        int yi = static_cast<int>(std::floor(y));
        int zi = static_cast<int>(std::floor(z));

        float minDist1 = 1000.0f;
        float minDist2 = 1000.0f;

        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                for (int k = -1; k <= 1; k++) {
                    int curX = xi + i;
                    int curY = yi + j;
                    int curZ = zi + k;

                    float px = curX + perlin(curX, curY, curZ);
                    float py = curY + perlin(curY, curZ, curX);
                    float pz = curZ + perlin(curZ, curX, curY);

                    float dx = x - px;
                    float dy = y - py;
                    float dz = z - pz;
                    float dist = dx*dx + dy*dy + dz*dz;

                    if (dist < minDist1) {
                        minDist2 = minDist1;
                        minDist1 = dist;
                    } else if (dist < minDist2) {
                        minDist2 = dist;
                    }
                }
            }
        }
        minDist1 = std::sqrt(minDist1);
        minDist2 = std::sqrt(minDist2);
        return minDist2 - minDist1;
    }
};

class BiomeGenerator {
private:
    NoiseGenerator noiseGen;
    unsigned int seed;

public:
    BiomeGenerator(unsigned int seed = 12345) : noiseGen(seed), seed(seed) {}

    // Grassland height function - rolling hills with Voronoi-based features
    int getGrasslandHeight(int x, int z) const {
        // Base height for grasslands
        const int BASE_HEIGHT = 140;
        const float SCALE_XZ = 0.003f;
        const float AMPLITUDE = 12.0f;

        float voronoi = noiseGen.voronoiNoise(x * SCALE_XZ, z * SCALE_XZ);
        float perlin = noiseGen.fractalNoise(x * SCALE_XZ * 2.0f, z * SCALE_XZ * 2.0f, 0.0f, 2, 0.5f);

        // Combine noises to create rolling hills
        float combinedNoise = 0.7f * voronoi + 0.3f * perlin;

        // Remap from [-1,1] to [0,1]
        combinedNoise = combinedNoise * 0.5f + 0.5f;

        // Apply height mapping
        int height = BASE_HEIGHT + static_cast<int>(combinedNoise * AMPLITUDE);

        // Ensure height is in the valid range [128, 255]
        return std::clamp(height, 128, 255);
    }

    // Mountain height function - jagged peaks using fractal noise
    int getMountainHeight(int x, int z) const {
        // Base height for mountains
        const int BASE_HEIGHT = 150;
        const float SCALE_XZ = 0.01f;   // Controls horizontal scale of features
        const float AMPLITUDE = 90.0f;  // Higher amplitude for mountains

        float noise = noiseGen.fractalNoise(x * SCALE_XZ, z * SCALE_XZ, 0.0f, 6, 0.5f);

        float ridgedNoise = 1.0f - std::abs(noise);

        ridgedNoise = std::pow(ridgedNoise, 1.5f);

        // Apply height mapping
        int height = BASE_HEIGHT + static_cast<int>(ridgedNoise * AMPLITUDE);

        // Ensure height is in the valid range [128, 255]
        return std::clamp(height, 128, 255);
    }

    // Biome interpolation function using a third noise function
    float getBiomeInterpolation(int x, int z) const {
        // Use Perlin noise with a very large grid size for smooth transitions
        const float SCALE_XZ = 0.001f;  // Very low frequency for large biome regions

        // Get raw Perlin noise value [-1, 1]
        float perlin = noiseGen.perlin(x * SCALE_XZ, z * SCALE_XZ);

        // Remap to [0, 1]
        float t = perlin * 0.5f + 0.5f;

        // Apply smoothstep to increase contrast in transition regions
        return noiseGen.smoothstep(0.25f, 0.75f, t);
    }

    // Gets the interpolated height at a given (x, z) coordinate
    int getHeightAt(int x, int z) const {
        // Get individual biome heights
        int grassHeight = getGrasslandHeight(x, z);
        int mountainHeight = getMountainHeight(x, z);

        // Get the interpolation factor between biomes (0 = all grassland, 1 = all mountain)
        float t = getBiomeInterpolation(x, z);

        // Interpolate between the two heights
        return static_cast<int>(std::round(grassHeight * (1.0f - t) + mountainHeight * t));
    }

    BlockType getBlockAt(int x, int y, int z) const {
        int terrainHeight = getHeightAt(x, z);

        float biomeFactor = getBiomeInterpolation(x, z);

        if (y <= 128) {
            return STONE;
        }
        else if (y <= 138 && y > terrainHeight) {
            return WATER;
        }
        else if (y > terrainHeight) {
            return EMPTY;
        }
        else if (biomeFactor < 0.5f) {
            return (y == terrainHeight) ? GRASS : DIRT;
        }
        else {
            return (y == terrainHeight && y > 200) ? SNOW : STONE;
        }
    }
    // Generates 3D cave noise at a given position
    float getCaveNoise(int x, int y, int z) const {
        const float CAVE_SCALE = 0.08f;  // Controls cave size and frequency

        // Sample 3D Perlin noise
        return noiseGen.perlin(x * CAVE_SCALE, y * CAVE_SCALE, z * CAVE_SCALE);
    }

    // Determines if a block should be a cave (empty space)
    bool isCave(int x, int y, int z) const {
        // Only generate caves between y=1 and y=128
        if (y <= 0 || y > 128) {
            return false;
        }

        // Get 3D noise value at this position
        float caveNoise = getCaveNoise(x, y, z);

        // Cave density increases with depth
        float caveDensityThreshold = -0.2f + (y / 128.0f) * 0.3f;

        // If noise is below threshold, it's a cave
        return caveNoise < caveDensityThreshold;
    }

    // Function to get the appropriate block type considering caves
    BlockType getBlockWithCaves(int x, int y, int z) const {
        // Bedrock layer at y=0
        if (y == 0) {
            return BEDROCK;
        }

        // Check if this block should be a cave
        if (isCave(x, y, z)) {
            // Lava pools at the bottom of caves
            if (y < 25) {
                return LAVA;
            }
            return EMPTY;
        }

        // Otherwise use the regular block type
        return getBlockAt(x, y, z);
    }
};
