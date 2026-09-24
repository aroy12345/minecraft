#include "terrain.h"
#include <stdexcept>

namespace {
constexpr int CHUNK_SIZE = 16;
}

// ---------------------------------------------------------------------------
// Terrain generation
// ---------------------------------------------------------------------------

void Terrain::fillChunkWithTerrain(Chunk* chunk) {
    using namespace WorldGen;
    int chunkX = chunk->getMinX();
    int chunkZ = chunk->getMinZ();

    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            int globalX = chunkX + x;
            int globalZ = chunkZ + z;
            // Height and biome are computed once per column, not per block
            ColumnInfo info = m_biomeGenerator.getColumnInfo(globalX, globalZ);

            // Climate drives the biome-tinted grass color in the mesher
            chunk->m_climate[x + 16 * z] =
                static_cast<unsigned char>(m_biomeGenerator.getClimate(globalX, globalZ) * 255.f);

            // Unbreakable bedrock floor
            chunk->setLocalBlockAt(x, 0, z, BEDROCK);

            // Underground: solid stone carved by 3D Perlin caves, with lava
            // pooling at the lowest depths
            for (int y = 1; y <= STONE_CEILING; y++) {
                BlockType b;
                if (m_biomeGenerator.isCave(globalX, y, globalZ)) {
                    b = (y < LAVA_LEVEL) ? LAVA : EMPTY;
                } else {
                    b = m_biomeGenerator.getOreAt(globalX, y, globalZ);
                }
                chunk->setLocalBlockAt(x, y, z, b);
            }

            // Surface: biome-dependent blocks up to the column height, then
            // water filling any empty space up to the water level. Dry
            // columns can still be carved by strong cave tunnels, which is
            // what opens walk-in cave entrances on hillsides.
            bool canCarveSurface = info.height > WATER_LEVEL + 1;
            for (int y = STONE_CEILING + 1; y <= MAX_HEIGHT; y++) {
                if (y <= info.height) {
                    BlockType b = m_biomeGenerator.getSurfaceBlock(info, y);
                    if (canCarveSurface && m_biomeGenerator.isCave(globalX, y, globalZ)) {
                        b = EMPTY;
                    }
                    chunk->setLocalBlockAt(x, y, z, b);
                } else if (y <= WATER_LEVEL) {
                    chunk->setLocalBlockAt(x, y, z, WATER);
                } else {
                    break; // blocks above are already EMPTY
                }
            }
        }
    }

    stampTrees(chunk);
    stampStructures(chunk);
}

void Terrain::setBlockDeferred(int x, int y, int z, BlockType t) {
    if (!hasChunkAt(x, z) || y < 1 || y > WorldGen::MAX_HEIGHT) return;
    uPtr<Chunk> &c = getChunkAt(x, z);
    int lx = x - c->getMinX();
    int lz = z - c->getMinZ();
    c->setLocalBlockAt(static_cast<unsigned int>(lx), static_cast<unsigned int>(y),
                       static_cast<unsigned int>(lz), t);
    m_awaitingMesh.insert(c.get());
    // edits on a chunk border change the neighbor's boundary faces too
    if (lx == 0 && hasChunkAt(x - 1, z)) m_awaitingMesh.insert(getChunkAt(x - 1, z).get());
    if (lx == 15 && hasChunkAt(x + 1, z)) m_awaitingMesh.insert(getChunkAt(x + 1, z).get());
    if (lz == 0 && hasChunkAt(x, z - 1)) m_awaitingMesh.insert(getChunkAt(x, z - 1).get());
    if (lz == 15 && hasChunkAt(x, z + 1)) m_awaitingMesh.insert(getChunkAt(x, z + 1).get());
}

void Terrain::setColumn(int x, int z, int height, BlockType top) {
    if (!hasChunkAt(x, z)) return;
    using namespace WorldGen;
    height = std::clamp(height, STONE_CEILING + 1, 200);
    for (int y = STONE_CEILING + 1; y <= 200; ++y) {
        BlockType b = EMPTY;
        if (top == WATER) {
            // water pixels become a pool: bed then water to the fill line
            if (y <= 133) b = STONE;
            else if (y <= WATER_LEVEL) b = WATER;
        } else if (y <= height) {
            if (y == height) b = top;
            else if (y > height - 3) b = (top == GRASS) ? DIRT : top;
            else b = STONE;
        }
        setBlockDeferred(x, y, z, b);
    }
}

void Terrain::setBlocksBulk(const std::vector<glm::ivec3> &cells, BlockType t) {
    for (const glm::ivec3 &c : cells) {
        setBlockDeferred(c.x, c.y, c.z, t);
    }
}

