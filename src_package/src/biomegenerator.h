#pragma once
#include <cmath>
#include <random>
#include <algorithm>
#include <mutex>
#include <numeric>
#include <unordered_map>
#include <vector>
#include "glm_includes.h"
#include "scene/chunk.h" // For BlockType enum

// World-generation constants shared by Terrain and BiomeGenerator.
// Everything below STONE_CEILING is stone (carved by caves); water fills
// empty space in (STONE_CEILING, WATER_LEVEL]; caves below LAVA_LEVEL flood
// with lava; mountain tops above SNOW_LINE are snow-capped.
namespace WorldGen {
constexpr int STONE_CEILING = 128;
constexpr int WATER_LEVEL   = 138;
constexpr int SNOW_LINE     = 200;
constexpr int LAVA_LEVEL    = 25;
constexpr int MAX_HEIGHT    = 255;
// Caves may carve up to here so they occasionally breach hillsides as
// walk-in entrances (the spec suggests raising the ceiling for this)
constexpr int CAVE_CEILING  = 142;
}

enum class Biome { GRASSLAND, MOUNTAINS, DESERT, SNOWFIELD };

// Everything the terrain filler needs to know about one (x, z) column,
// computed once per column instead of once per block.
struct ColumnInfo {
    int height;
    Biome dominant;
};

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

    // Like smoothstep but 1 -> 0 as x goes edge0 -> edge1 (edge0 < edge1)
    float smoothstepDown(float edge0, float edge1, float x) const {
        return 1.0f - smoothstep(edge0, edge1, x);
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

    // Deterministic integer hash mapped to [0, 1). Used for scattering
    // features (trees) so that any chunk can recompute its neighbors'
    // features without storing them.
    static float hash01(int x, int z, unsigned int seed) {
        unsigned int h = static_cast<unsigned int>(x) * 374761393u
                       + static_cast<unsigned int>(z) * 668265263u
                       + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (h & 0x00ffffffu) / 16777216.0f;
    }
};

class BiomeGenerator {
private:
    NoiseGenerator noiseGen;
    unsigned int seed;

    // ------------------------------------------------------------------
    // Rivers (Milestone 3): L-system generated branching river networks.
    // Each 384-block region lazily expands one L-system into an arc-walked
    // polyline; getColumnInfo carves any column near a polyline down to a
    // river bed, and the normal water fill turns the channel into a river.
    // ------------------------------------------------------------------
    static constexpr int RIVER_REGION = 384;
    mutable std::unordered_map<long long, std::vector<glm::vec2>> m_riverCache;
    mutable std::mutex m_riverMutex;

    const std::vector<glm::vec2>& riverPoints(int rx, int rz) const {
        long long key = (static_cast<long long>(rx) << 32) ^ (rz & 0xffffffffLL);
        std::lock_guard<std::mutex> lk(m_riverMutex);
        auto it = m_riverCache.find(key);
        if (it != m_riverCache.end()) return it->second;

        std::vector<glm::vec2> pts;
        // L-system: axiom F, rule F -> F[+F]F[-F], expanded twice.
        // F walks a gentle arc; +/- turn; [] branch push/pop.
        std::string s = "F";
        for (int i = 0; i < 2; ++i) {
            std::string next;
            for (char c : s) next += (c == 'F') ? "F[+F]F[-F]" : std::string(1, c);
            s = next;
        }
        struct Turtle { glm::vec2 pos; float heading; };
        float h0 = NoiseGenerator::hash01(rx * 7 + 1, rz * 13 + 5, seed) * 6.2831853f;
        Turtle t{glm::vec2(rx * RIVER_REGION + RIVER_REGION * 0.5f,
                           rz * RIVER_REGION + RIVER_REGION * 0.5f), h0};
        std::vector<Turtle> stack;
        int salt = 0;
        for (char c : s) {
            float r = NoiseGenerator::hash01(rx * 31 + salt, rz * 17 - salt, seed + salt);
            salt++;
            if (c == 'F') {
                float curvature = (r - 0.5f) * 0.10f; // radians per step
                for (int step = 0; step < 14; ++step) {
                    t.heading += curvature;
                    t.pos += glm::vec2(std::cos(t.heading), std::sin(t.heading)) * 2.0f;
                    pts.push_back(t.pos);
                }
            } else if (c == '+') {
                t.heading += 0.5f + r * 0.5f;
            } else if (c == '-') {
                t.heading -= 0.5f + r * 0.5f;
            } else if (c == '[') {
                stack.push_back(t);
            } else if (c == ']') {
                t = stack.back();
                stack.pop_back();
            }
        }
        return m_riverCache.emplace(key, std::move(pts)).first->second;
    }

    // Squared distance from (x, z) to the nearest river centerline point
    float riverDistSq(float x, float z) const {
        int rx = static_cast<int>(std::floor(x / RIVER_REGION));
        int rz = static_cast<int>(std::floor(z / RIVER_REGION));
        float best = 1e9f;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (const glm::vec2 &p : riverPoints(rx + dx, rz + dz)) {
                    float ddx = p.x - x, ddz = p.y - z;
                    float d2 = ddx * ddx + ddz * ddz;
                    if (d2 < best) best = d2;
                }
            }
        }
        return best;
    }

public:
    BiomeGenerator(unsigned int seed = 12345) : noiseGen(seed), seed(seed) {}

    // Climate parameter in [0,1] used for smooth grass tinting across the
    // world (Minecraft's grass colormap, driven by our temperature field)
    float getClimate(int x, int z) const { return getTemperature(x, z); }

    // ------------------------------------------------------------------
    // Per-biome height fields
    // ------------------------------------------------------------------

    // Grassland height function - rolling hills with Voronoi-based features.
    // The base sits below the water level so valleys become lakes.
    int getGrasslandHeight(int x, int z) const {
        const int BASE_HEIGHT = 133;
        const float SCALE_XZ = 0.003f;
        const float AMPLITUDE = 16.0f;

        // Voronoi (F2-F1) is already in [0,1] - low along cell borders,
        // which carves pond and channel networks between the hills
        float voronoi = noiseGen.voronoiNoise(x * SCALE_XZ, z * SCALE_XZ);
        float perlin01 = noiseGen.fractalNoise(x * SCALE_XZ * 2.0f, z * SCALE_XZ * 2.0f, 0.0f, 2, 0.5f) * 0.5f + 0.5f;
        float combined = 0.7f * voronoi + 0.3f * perlin01;
        // Stretch the contrast so valleys dip below the water level
        float shaped = noiseGen.smoothstep(0.08f, 0.70f, combined);

        int height = BASE_HEIGHT + static_cast<int>(shaped * AMPLITUDE);
        return std::clamp(height, WorldGen::STONE_CEILING, WorldGen::MAX_HEIGHT);
    }

    // Mountain height function - jagged peaks using ridged fractal noise
    int getMountainHeight(int x, int z) const {
        const int BASE_HEIGHT = 150;
        const float SCALE_XZ = 0.01f;   // Controls horizontal scale of features
        const float AMPLITUDE = 90.0f;  // Higher amplitude for mountains

        float noise = noiseGen.fractalNoise(x * SCALE_XZ, z * SCALE_XZ, 0.0f, 6, 0.5f);
        float ridgedNoise = std::pow(1.0f - std::abs(noise), 1.5f);

        int height = BASE_HEIGHT + static_cast<int>(ridgedNoise * AMPLITUDE);
        return std::clamp(height, WorldGen::STONE_CEILING, WorldGen::MAX_HEIGHT);
    }

    // Desert height function - low, gently rolling dunes
    int getDesertHeight(int x, int z) const {
        const int BASE_HEIGHT = 135;
        const float SCALE_XZ = 0.008f;
        const float AMPLITUDE = 7.0f;

        float dunes = noiseGen.fractalNoise(x * SCALE_XZ, z * SCALE_XZ, 0.0f, 3, 0.5f) * 0.5f + 0.5f;
        int height = BASE_HEIGHT + static_cast<int>(dunes * AMPLITUDE);
        return std::clamp(height, WorldGen::STONE_CEILING, WorldGen::MAX_HEIGHT);
    }

    // Snowfield height function - gentle frozen plains
    int getSnowfieldHeight(int x, int z) const {
        const int BASE_HEIGHT = 137;
        const float SCALE_XZ = 0.005f;
        const float AMPLITUDE = 10.0f;

        float rolling = noiseGen.fractalNoise(x * SCALE_XZ, z * SCALE_XZ, 0.0f, 3, 0.5f) * 0.5f + 0.5f;
        int height = BASE_HEIGHT + static_cast<int>(rolling * AMPLITUDE);
        return std::clamp(height, WorldGen::STONE_CEILING, WorldGen::MAX_HEIGHT);
    }

    // ------------------------------------------------------------------
    // Biome distribution
    // ------------------------------------------------------------------

    // Elevation mask: 0 = lowland biomes, 1 = mountains. Very low frequency
    // Perlin pushed through smoothstep(0.25, 0.75) so large regions sit
    // fully inside one biome before blending into the next.
    float getMountainWeight(int x, int z) const {
        const float SCALE_XZ = 0.001f;
        float t = noiseGen.perlin(x * SCALE_XZ, z * SCALE_XZ) * 0.5f + 0.5f;
        return noiseGen.smoothstep(0.45f, 0.75f, t);
    }

    // Temperature and moisture maps decide which lowland biome dominates.
    // Offsets keep them decorrelated from each other and the elevation mask;
    // the 1.5x gain stretches Perlin's clustered output to cover [0, 1] so
    // the extreme biomes (desert, snowfield) actually occur.
    float getTemperature(int x, int z) const {
        const float SCALE_XZ = 0.0025f;
        float p = noiseGen.perlin((x + 5000) * SCALE_XZ, (z + 5000) * SCALE_XZ);
        return std::clamp(0.5f + p * 1.5f, 0.0f, 1.0f);
    }

    float getMoisture(int x, int z) const {
        const float SCALE_XZ = 0.0025f;
        float p = noiseGen.perlin((x - 5000) * SCALE_XZ, (z - 5000) * SCALE_XZ);
        return std::clamp(0.5f + p * 1.5f, 0.0f, 1.0f);
    }

    // Computes the blended terrain height and dominant biome for a column.
    // Heights are interpolated with continuous weights so biome borders
    // transition smoothly instead of stepping.
    ColumnInfo getColumnInfo(int x, int z) const {
        float mountainW = getMountainWeight(x, z);
        float T = getTemperature(x, z);
        float M = getMoisture(x, z);

        // Lowland weights: hot + dry -> desert, cold -> snowfield
        float wDesert = noiseGen.smoothstep(0.58f, 0.72f, T) * noiseGen.smoothstepDown(0.35f, 0.50f, M);
        float wSnow   = noiseGen.smoothstepDown(0.30f, 0.42f, T);
        float wGrass  = std::clamp(1.0f - wDesert - wSnow, 0.0f, 1.0f);
        float wSum = wDesert + wSnow + wGrass;
        wDesert /= wSum; wSnow /= wSum; wGrass /= wSum;

        float lowlandH = wGrass  * getGrasslandHeight(x, z)
                       + wDesert * getDesertHeight(x, z)
                       + wSnow   * getSnowfieldHeight(x, z);
        float h = lowlandH * (1.0f - mountainW) + getMountainHeight(x, z) * mountainW;

        ColumnInfo info;
        info.height = std::clamp(static_cast<int>(std::round(h)),
                                 WorldGen::STONE_CEILING, WorldGen::MAX_HEIGHT);

        // Rivers carve smooth-banked channels through low terrain; the
        // water fill then floods them up to the water level
        constexpr float RIVER_W = 4.5f, RIVER_BANK = 9.f;
        float rd2 = riverDistSq(static_cast<float>(x), static_cast<float>(z));
        if (rd2 < (RIVER_W + RIVER_BANK) * (RIVER_W + RIVER_BANK)) {
            float rd = std::sqrt(rd2);
            float channel = noiseGen.smoothstepDown(RIVER_W, RIVER_W + RIVER_BANK, rd);
            // rivers don't gouge through high mountains
            channel *= noiseGen.smoothstepDown(150.f, 168.f, static_cast<float>(info.height));
            float bed = WorldGen::WATER_LEVEL - 2.f;
            float carved = info.height * (1.f - channel) + bed * channel;
            info.height = std::min(info.height, static_cast<int>(carved));
        }
        // Rocky mountain surfacing only where the terrain is genuinely
        // high; low columns near the elevation-mask boundary read as
        // whichever lowland biome surrounds them instead of bare stone.
        if (mountainW > 0.5f && info.height >= 155) {
            info.dominant = Biome::MOUNTAINS;
        } else if (wDesert >= wSnow && wDesert >= wGrass) {
            info.dominant = Biome::DESERT;
        } else if (wSnow >= wGrass) {
            info.dominant = Biome::SNOWFIELD;
        } else {
            info.dominant = Biome::GRASSLAND;
        }
        return info;
    }

    // Surface block for a column above the stone ceiling, given its biome.
    // Grassland tops that sit at or below the water line grow dirt, not grass.
    BlockType getSurfaceBlock(const ColumnInfo &info, int y) const {
        switch (info.dominant) {
        case Biome::MOUNTAINS:
            return (y == info.height && y > WorldGen::SNOW_LINE) ? SNOW : STONE;
        case Biome::DESERT:
            return (y > info.height - 4) ? SAND : STONE;
        case Biome::SNOWFIELD:
            if (y == info.height) return SNOW;
            return (y > info.height - 4) ? DIRT : STONE;
        case Biome::GRASSLAND:
        default:
            // Columns at or just above the waterline become sandy beaches
            // and lakebeds instead of underwater grass
            if (info.height <= WorldGen::WATER_LEVEL + 1) {
                return (y > info.height - 3) ? SAND : STONE;
            }
            // Per the spec: grass on the very top block, dirt below it all
            // the way down to the stone layer
            return (y == info.height) ? GRASS : DIRT;
        }
    }

    // ------------------------------------------------------------------
    // Caves (3D Perlin noise, Milestone 2)
    // ------------------------------------------------------------------

    float getCaveNoise(int x, int y, int z) const {
        const float CAVE_SCALE = 0.08f;  // Controls cave size and frequency
        return noiseGen.perlin(x * CAVE_SCALE, y * CAVE_SCALE, z * CAVE_SCALE);
    }

    // A block is carved into cave when the 3D noise dips into its low tail,
    // producing winding tunnel networks like Minecraft's rather than vast
    // hollow caverns. Caves are roomiest at depth, tighten toward the
    // surface, and only the strongest tunnels break through hillsides as
    // walk-in entrances.
    bool isCave(int x, int y, int z) const {
        if (y <= 0 || y > WorldGen::CAVE_CEILING) {
            return false;
        }
        float threshold;
        if (y <= WorldGen::STONE_CEILING) {
            float t = y / static_cast<float>(WorldGen::STONE_CEILING);
            threshold = -0.18f - 0.14f * t;
        } else {
            threshold = -0.32f - 0.28f * (y - WorldGen::STONE_CEILING) /
                        static_cast<float>(WorldGen::CAVE_CEILING - WorldGen::STONE_CEILING);
        }
        return getCaveNoise(x, y, z) < threshold;
    }

    // ------------------------------------------------------------------
    // Ores (Milestone 3)
    // ------------------------------------------------------------------
    // Clumpy ore veins from tight thresholds on medium-frequency 3D
    // Perlin noise, depth-gated like Minecraft: coal high, iron mid,
    // gold low, diamond only near bedrock. Returns STONE when no ore.

    BlockType getOreAt(int x, int y, int z) const {
        if (y <= 18 &&
            noiseGen.perlin(x * 0.17f + 91.f, y * 0.17f, z * 0.17f + 37.f) > 0.52f) {
            return DIAMOND_ORE;
        }
        if (y <= 35 &&
            noiseGen.perlin(x * 0.16f + 53.f, y * 0.16f, z * 0.16f + 71.f) > 0.50f) {
            return GOLD_ORE;
        }
        if (y <= 90 &&
            noiseGen.perlin(x * 0.15f + 17.f, y * 0.15f, z * 0.15f + 13.f) > 0.46f) {
            return IRON_ORE;
        }
        if (y >= 30 &&
            noiseGen.perlin(x * 0.14f + 5.f, y * 0.14f, z * 0.14f + 3.f) > 0.42f) {
            return COAL_ORE;
        }
        return STONE;
    }

    // ------------------------------------------------------------------
    // Trees (Milestone 3)
    // ------------------------------------------------------------------
    // Tree placement is a pure function of (x, z) so neighboring chunks can
    // independently stamp the parts of a tree that overlap them.

    static constexpr int TREE_CANOPY_RADIUS = 2;

    // Cheap prefilter so callers can skip the expensive ColumnInfo
    // computation for the vast majority of columns with no tree.
    bool couldHaveTree(int x, int z) const {
        return NoiseGenerator::hash01(x, z, seed) < 0.010f;
    }

    bool hasTreeAt(int x, int z, const ColumnInfo &info) const {
        if (info.height <= WorldGen::WATER_LEVEL + 1) return false;
        float density;
        switch (info.dominant) {
        case Biome::GRASSLAND: density = 0.009f; break;
        case Biome::SNOWFIELD: density = 0.004f; break;
        default: return false;
        }
        return NoiseGenerator::hash01(x, z, seed) < density;
    }

    int getTreeTrunkHeight(int x, int z) const {
        return 4 + static_cast<int>(NoiseGenerator::hash01(x * 3 + 7, z * 5 + 11, seed) * 3.0f);
    }
};
