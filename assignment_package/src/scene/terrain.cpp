#include "terrain.h"
#include "cube.h"
#include <stdexcept>
#include <iostream>
#include <thread>
#include <glm/gtc/random.hpp>
std::atomic<int> activeZoneThreads = 0;
const int MAX_ACTIVE_ZONE_THREADS = 6; // You can tune this


Terrain::Terrain(OpenGLContext *context)
    : m_chunks(), m_generatedTerrain(), m_geomCube(context),
    m_chunkVBOsNeedUpdating(true), mp_context(context),
    m_biomeGenerator(12345) // Initialize BiomeGenerator with a seed
{}

Terrain::~Terrain() {
    m_geomCube.destroyVBOdata();
}

// Combine two 32-bit ints into one 64-bit int
// where the upper 32 bits are X and the lower 32 bits are Z
int64_t toKey(int x, int z) {
    int64_t xz = 0xffffffffffffffff;
    int64_t x64 = x;
    int64_t z64 = z;
    // Set all lower 32 bits to 1 so we can & with Z later
    xz = (xz & (x64 << 32)) | 0x00000000ffffffff;
    // Set all upper 32 bits to 1 so we can & with XZ
    z64 = z64 | 0xffffffff00000000;
    // Combine
    xz = xz & z64;
    return xz;
}

glm::ivec2 toCoords(int64_t k) {
    // Z is lower 32 bits
    int64_t z = k & 0x00000000ffffffff;
    // If the most significant bit of Z is 1, then it's a negative number
    // so we have to set all the upper 32 bits to 1.
    // Note the 8    V
    if(z & 0x0000000080000000) {
        z = z | 0xffffffff00000000;
    }
    int64_t x = (k >> 32);
    return glm::ivec2(x, z);
}

// Surround calls to this with try-catch if you don't know whether
// the coordinates at x, y, z have a corresponding Chunk
BlockType Terrain::getGlobalBlockAt(int x, int y, int z) const
{
    if(hasChunkAt(x, z)) {
        // Just disallow action below or above min/max height,
        // but don't crash the game over it.
        if(y < 0 || y >= 256) {
            return EMPTY;
        }
        const uPtr<Chunk> &c = getChunkAt(x, z);
        glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
        return c->getLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x),
                                  static_cast<unsigned int>(y),
                                  static_cast<unsigned int>(z - chunkOrigin.y));
    }
    else {
        throw std::out_of_range("Coordinates " + std::to_string(x) +
                                " " + std::to_string(y) + " " +
                                std::to_string(z) + " have no Chunk!");
    }
}

BlockType Terrain::getGlobalBlockAt(glm::vec3 p) const {
    return getGlobalBlockAt(p.x, p.y, p.z);
}

bool Terrain::hasChunkAt(int x, int z) const {
    // Map x and z to their nearest Chunk corner
    // By flooring x and z, then multiplying by 16,
    // we clamp (x, z) to its nearest Chunk-space corner,
    // then scale back to a world-space location.
    int xFloor = static_cast<int>(glm::floor(x / 16.f));
    int zFloor = static_cast<int>(glm::floor(z / 16.f));
    return m_chunks.find(toKey(16 * xFloor, 16 * zFloor)) != m_chunks.end();
}

uPtr<Chunk>& Terrain::getChunkAt(int x, int z) {
    int xFloor = static_cast<int>(glm::floor(x / 16.f));
    int zFloor = static_cast<int>(glm::floor(z / 16.f));
    return m_chunks[toKey(16 * xFloor, 16 * zFloor)];
}

const uPtr<Chunk>& Terrain::getChunkAt(int x, int z) const {
    int xFloor = static_cast<int>(glm::floor(x / 16.f));
    int zFloor = static_cast<int>(glm::floor(z / 16.f));
    return m_chunks.at(toKey(16 * xFloor, 16 * zFloor));
}

void Terrain::setGlobalBlockAt(int x, int y, int z, BlockType t)
{
    if(hasChunkAt(x, z)) {
        uPtr<Chunk> &c = getChunkAt(x, z);
        glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
        c->setLocalBlockAt(static_cast<unsigned int>(x - chunkOrigin.x),
                           static_cast<unsigned int>(y),
                           static_cast<unsigned int>(z - chunkOrigin.y),
                           t);
        // Update VBO data for this chunk
        c->createVBOdata();
        VBO_mut.lock();
        chunksNeedToBeBuffered.push_back(c.get());
        VBO_mut.unlock();

        // Also update neighboring chunks if this block is on an edge
        int localX = x - static_cast<int>(chunkOrigin.x);
        int localZ = z - static_cast<int>(chunkOrigin.y);
        if(localX == 0 && hasChunkAt(x - 1, z)) {
            uPtr<Chunk>& c = getChunkAt(x - 1, z);
            c->createVBOdata();
            VBO_mut.lock();
            chunksNeedToBeBuffered.push_back(c.get());
            VBO_mut.unlock();
        }
        else if(localX == 15 && hasChunkAt(x + 1, z)) {
            uPtr<Chunk>& c = getChunkAt(x + 1, z);
            c->createVBOdata();
            VBO_mut.lock();
            chunksNeedToBeBuffered.push_back(c.get());
            VBO_mut.unlock();
        }
        if(localZ == 0 && hasChunkAt(x, z - 1)) {
            uPtr<Chunk>& c  = getChunkAt(x, z-1);
            c->createVBOdata();
            VBO_mut.lock();
            chunksNeedToBeBuffered.push_back(c.get());
            VBO_mut.unlock();
        }
        else if(localZ == 15 && hasChunkAt(x, z + 1)) {
            uPtr<Chunk>& c = getChunkAt(x, z + 1);
            c->createVBOdata();
            VBO_mut.lock();
            chunksNeedToBeBuffered.push_back(c.get());
            VBO_mut.unlock();
        }
    }
    else {
        throw std::out_of_range("Coordinates " + std::to_string(x) +
                                " " + std::to_string(y) + " " +
                                std::to_string(z) + " have no Chunk!");
    }
}

Chunk* Terrain::instantiateChunkAt(int x, int z) {
    uPtr<Chunk> chunk = mkU<Chunk>(mp_context, x, z);
    Chunk *cPtr = chunk.get();
    m_chunks[toKey(x, z)] = move(chunk);
    // Set the neighbor pointers of itself and its neighbors
    if(hasChunkAt(x, z + 16)) {
        auto &chunkNorth = m_chunks[toKey(x, z + 16)];
        cPtr->linkNeighbor(chunkNorth, ZPOS);
    }
    if(hasChunkAt(x, z - 16)) {
        auto &chunkSouth = m_chunks[toKey(x, z - 16)];
        cPtr->linkNeighbor(chunkSouth, ZNEG);
    }
    if(hasChunkAt(x + 16, z)) {
        auto &chunkEast = m_chunks[toKey(x + 16, z)];
        cPtr->linkNeighbor(chunkEast, XPOS);
    }
    if(hasChunkAt(x - 16, z)) {
        auto &chunkWest = m_chunks[toKey(x - 16, z)];
        cPtr->linkNeighbor(chunkWest, XNEG);
    }
    return cPtr;
}

// Draw opaque blocks
void Terrain::drawOpaque(int minX, int maxX, int minZ, int maxZ, ShaderProgram *sh) {
    for (int x = minX; x < maxX; x += 16) {
        for (int z = minZ; z < maxZ; z += 16) {
            if (!hasChunkAt(x, z)) continue;
            auto &c = getChunkAt(x, z);
            glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(x, 0, z));
            sh->setUnifMat4("u_Model", model);
            sh->drawInterleaved(*c, BufferType::OPAQUE_INTERLEAVED, BufferType::OPAQUE_INDEX);
        }
    }
}

// Draw transparent blocks
void Terrain::drawTransparent(int minX, int maxX, int minZ, int maxZ, ShaderProgram *sh) {
    for (int x = minX; x < maxX; x += 16) {
        for (int z = minZ; z < maxZ; z += 16) {
            if (!hasChunkAt(x, z)) continue;
            auto &c = getChunkAt(x, z);
            glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(x, 0, z));
            sh->setUnifMat4("u_Model", model);
            sh->drawInterleaved(*c, BufferType::TRANSPARENT_INTERLEAVED, BufferType::TRANSPARENT_INDEX);
        }
    }
}

void Terrain::draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram) {
    // Draw opaque blocks first
    drawOpaque(minX, maxX, minZ, maxZ, shaderProgram);
    // Draw transparent blocks after
    drawTransparent(minX, maxX, minZ, maxZ, shaderProgram);
}

// void Terrain::CreateTestScene() {
//     std::vector<std::thread> BlockTypeWorkers;
//     std::vector<std::thread> VBOWorkers;

//     // For each 64x64 zone
//     for (int zonex = -128; zonex <= 128; zonex += 64) {
//         for (int zonez = -128; zonez <= 128; zonez += 64) {
//             m_generatedTerrain.insert(toKey(zonex, zonez));
//             BlockTypeWorkers.emplace_back([=]() {
//                 for (int chunkx = zonex; chunkx < zonex + 64; chunkx += 16) {
//                     for (int chunkz = zonez; chunkz < zonez + 64; chunkz += 16) {
//                         generateChunks(chunkx, chunkz);
//                     }
//                 }
//             });
//         }
//     }

//     for (auto& t: BlockTypeWorkers) {
//         if (t.joinable()) {
//             t.join();
//         }
//     }

//     // for (int x = -128; x < 192; x += 16) {
//     //     for (int z = -128; z < 192; z += 16) {
//     //         if (hasChunkAt(x, z)) {
//     //             Chunk* c = getChunkAt(x, z).get();
//     //             VBOWorkers.push_back(std::thread(&Terrain::generateVBO, this, c));
//     //         }
//     //     }
//     // }

//     // for (auto& t: VBOWorkers) {
//     //     if (t.joinable()) {
//     //         t.join();
//     //     }
//     // }

//     // for (int x = -128; x < 192; x += 16) {
//     //     for (int z = -128; z < 192; z += 16) {
//     //         if (hasChunkAt(x, z)) {
//     //             getChunkAt(x, z)->bufferInterleaved();
//     //         }
//     //     }
//     // }
//     consumeChunkWork();
// }



void Terrain::CreateTestScene() {
    std::vector<std::thread> BlockTypeWorkers;
    std::vector<std::thread> VBOWorkers;

    // For each 64x64 zone
    for (int zonex = -128; zonex <= 128; zonex += 64) {
        for (int zonez = -128; zonez <= 128; zonez += 64) {
            m_generatedTerrain.insert(toKey(zonex, zonez));
            BlockTypeWorkers.emplace_back([=]() {
                for (int chunkx = zonex; chunkx < zonex + 64; chunkx += 16) {
                    for (int chunkz = zonez; chunkz < zonez + 64; chunkz += 16) {
                        generateChunks(chunkx, chunkz);
                    }
                }
            });
        }
    }


    for (auto& t: BlockTypeWorkers) {
        if (t.joinable()) {
            t.join();
        }
    }

    for (int i = 0; i < 50; ++i) {
        float x = glm::linearRand(30.f, 90.f);
        float z = glm::linearRand(60.f, 120.f);

        if (hasChunkAt(x, z)) {
            bool foundGround = false;
            float spawnY = 128; // placeholder

            for (int y = 255; y >= 0; --y) {
                BlockType block = getGlobalBlockAt(floor(x), y, floor(z));
                if (block != EMPTY && block != WATER) {
                    spawnY = y + 1.5f;
                    foundGround = true;
                    break;
                }
            }

            if (foundGround) {
                m_sheep.push_back(std::make_unique<Sheep>(glm::vec3(x, spawnY, z), *this));
            }
        }
    }

    consumeChunkWork();
}

void Terrain::generateChunks(int newx, int newz) {
    Chunk* c = instantiateChunkAt(newx, newz);
    fillChunkWithTerrain(c, newx, newz);
    block_mut.lock();
    chunksNeedVBO.push_back(c);
    block_mut.unlock();
}

void Terrain::fillChunkWithTerrain(Chunk* chunk, int chunkX, int chunkZ) {
    for (int x = 0; x < 16; x++) {
        for (int z = 0; z < 16; z++) {
            // Calculate global coordinates
            int globalX = chunkX + x;
            int globalZ = chunkZ + z;
            // Get the height at this position
            int height = m_biomeGenerator.getHeightAt(globalX, globalZ);
            // Fill only needed blocks to improve performance
            // Surface layer and a few blocks below
            int startY = std::max(height - 5, 128);
            // Fill from startY up to the terrain height with appropriate blocks
            for (int y = startY; y <= 255; y++) {
                BlockType blockType;
                if (y <= height) {
                    // Get the appropriate block type from the biome generator
                    blockType = m_biomeGenerator.getBlockAt(globalX, y, globalZ);
                } else if (y <= 138 && y > 128 && y > height) {
                    // Fill with water if empty and between Y=128 and Y=138
                    blockType = WATER;
                } else {
                    // Empty block above the terrain
                    blockType = EMPTY;
                }
                // Set the block in the chunk
                if (y < 256) { // Ensure we're within valid Y range
                    chunk->setLocalBlockAt(x, y, z, blockType);
                }
            }
            // Add bedrock at the bottom
            for (int y = 0; y < 5; y++) {
                chunk->setLocalBlockAt(x, y, z, STONE);
            }
            // Fill middle portion with stone (skipping most to improve performance)
            if ((x + z) % 8 == 0) { // Only fill every 8th column fully
                for (int y = 5; y < startY; y++) {
                    chunk->setLocalBlockAt(x, y, z, STONE);
                }
            } else {
                // For other columns, just set a sparse pattern of stone
                for (int y = 5; y < startY; y += 16) {
                    chunk->setLocalBlockAt(x, y, z, STONE);
                }
            }
        }
    }

}

void Terrain::generateVBO(Chunk* c) {
    c->createVBOdata();
    VBO_mut.lock();
    chunksNeedToBeBuffered.push_back(c);
    VBO_mut.unlock();
}

void Terrain::createcheckchunks(int newChunkX, int newChunkZ) {
    Chunk* newChunk = instantiateChunkAt(newChunkX, newChunkZ);
    fillChunkWithTerrain(newChunk, newChunkX, newChunkZ);
    // Queue VBO generation
    block_mut.lock();
    chunksNeedVBO.push_back(newChunk);
    block_mut.unlock();
}

// In Terrain::consumeChunkWork
/* void Terrain::consumeChunkWork() {
    // Limit how many VBO operations we do per frame to prevent stuttering
    const int MAX_VBO_UPLOADS_PER_FRAME = 4;

    // Step 1: Multithreaded VBO creation (no limit here)
    std::vector<std::thread> VBOWorkers;
    block_mut.lock();
    std::vector<Chunk*> chunks_to_process = chunksNeedVBO;
    chunksNeedVBO.clear();
    block_mut.unlock();

    for (Chunk* c : chunks_to_process) {
        VBOWorkers.emplace_back(&Terrain::generateVBO, this, c);
    }

    for (auto& t : VBOWorkers) {
        if (t.joinable()) t.join();
    }

    // Step 2: Main-thread GPU buffer upload (limited per frame)
    VBO_mut.lock();
    std::vector<Chunk*> chunks_to_buffer = chunksNeedToBeBuffered;
    // Only take a subset of chunks to buffer this frame
    if (chunks_to_buffer.size() > MAX_VBO_UPLOADS_PER_FRAME) {
        std::vector<Chunk*> remaining(
            chunks_to_buffer.begin() + MAX_VBO_UPLOADS_PER_FRAME,
            chunks_to_buffer.end());
        chunksNeedToBeBuffered = remaining;
        chunks_to_buffer.resize(MAX_VBO_UPLOADS_PER_FRAME);
    } else {
        chunksNeedToBeBuffered.clear();
    }
    VBO_mut.unlock();

    // Upload VBO data to GPU for the limited subset
    for (Chunk* c : chunks_to_buffer) {
        c->bufferInterleaved();
    }
}


// Checks player position and creates new chunks as needed
void Terrain::checkAndCreateNewChunks(const glm::vec3 &playerPos) {
    // Check if we've entered a new terrain zone and create new zones if needed
    int zoneXCenter = static_cast<int>(glm::floor(playerPos.x / 64.f)) * 64;
    int zoneZCenter = static_cast<int>(glm::floor(playerPos.z / 64.f)) * 64;

    // Gather all chunks that need to be created
    std::vector<std::pair<float, std::pair<int, int>>> chunksToCreate;

    // Loop over 5x5 zone window centered on player
    for (int zoneX = zoneXCenter - 128; zoneX <= zoneXCenter + 128; zoneX += 64) {
        for (int zoneZ = zoneZCenter - 128; zoneZ <= zoneZCenter + 128; zoneZ += 64) {
            int64_t zoneKey = toKey(zoneX, zoneZ);
            if (m_generatedTerrain.find(zoneKey) != m_generatedTerrain.end()) {
                continue;
            }

            // Mark zone as generated
            m_generatedTerrain.insert(zoneKey);

            // Add all chunks in this zone to our priority queue
            for (int chunkX = zoneX; chunkX < zoneX + 64; chunkX += 16) {
                for (int chunkZ = zoneZ; chunkZ < zoneZ + 64; chunkZ += 16) {
                    if (!hasChunkAt(chunkX, chunkZ)) {
                        // Calculate squared distance to player
                        float distSq =
                            pow(chunkX + 8 - playerPos.x, 2) +
                            pow(chunkZ + 8 - playerPos.z, 2);

                        chunksToCreate.push_back({distSq, {chunkX, chunkZ}});
                    }
                }
            }
        }
    }

    // Sort chunks by distance (closest first)
    std::sort(chunksToCreate.begin(), chunksToCreate.end(),
              [](const auto& a, const auto& b) {
                  return a.first < b.first;
              });

    // Limit the number of chunks created per frame to avoid stuttering
    const size_t MAX_CHUNKS_PER_FRAME = 8;
    size_t chunksToProcess = std::min(chunksToCreate.size(), MAX_CHUNKS_PER_FRAME);

    // Only create the closest chunks
    std::vector<std::thread> chunkThreads;
    for (size_t i = 0; i < chunksToProcess; i++) {
        int chunkX = chunksToCreate[i].second.first;
        int chunkZ = chunksToCreate[i].second.second;

        chunkThreads.emplace_back([=]() {
            createcheckchunks(chunkX, chunkZ);
        });
    }

    // Join threads
    for (auto& t : chunkThreads) {
        if (t.joinable()) t.join();
    }

    // Store the remaining chunks for next frame if needed
    if (chunksToCreate.size() > MAX_CHUNKS_PER_FRAME) {
        // You would need to add a member variable to store these:
        // std::vector<std::pair<float, std::pair<int, int>>> m_pendingChunks;
        m_pendingChunks.clear();
        for (size_t i = MAX_CHUNKS_PER_FRAME; i < chunksToCreate.size(); i++) {
            m_pendingChunks.push_back(chunksToCreate[i]);
        }
    }
}*/


void Terrain::checkAndCreateNewChunks(const glm::vec3 &playerPos) {
    int playerZoneX = static_cast<int>(glm::floor(playerPos.x / 64.f)) * 64;
    int playerZoneZ = static_cast<int>(glm::floor(playerPos.z / 64.f)) * 64;

    glm::vec3 playerDir = glm::normalize(glm::vec3(playerPos.x, 0.f, playerPos.z));

    for (int zoneX = playerZoneX - 128; zoneX <= playerZoneX + 128; zoneX += 64) {
        for (int zoneZ = playerZoneZ - 128; zoneZ <= playerZoneZ + 128; zoneZ += 64) {
            int64_t zoneKey = toKey(zoneX, zoneZ);

            if (m_generatedTerrain.find(zoneKey) == m_generatedTerrain.end()) {
                // Prevent multiple threads for the same zone
                m_generatedTerrain.insert(zoneKey);

                // Directional prioritization (optional)
                glm::vec3 toZone = glm::normalize(glm::vec3(zoneX - playerPos.x, 0.f, zoneZ - playerPos.z));
                float dot = glm::dot(playerDir, toZone);
                if (dot < -0.5f) continue;

                // Thread limiting
                if (activeZoneThreads >= MAX_ACTIVE_ZONE_THREADS) continue;

                activeZoneThreads++;

                std::thread([=]() {
                    for (int chunkX = zoneX; chunkX < zoneX + 64; chunkX += 16) {
                        for (int chunkZ = zoneZ; chunkZ < zoneZ + 64; chunkZ += 16) {
                            if (!hasChunkAt(chunkX, chunkZ)) {
                                createcheckchunks(chunkX, chunkZ);
                            }
                        }
                    }
                    activeZoneThreads--;
                }).detach();
            }
        }
    }
}

void Terrain::consumeChunkWork() {
    // Step 1: Multithreaded VBO creation
    std::vector<std::thread> VBOWorkers;

    block_mut.lock();
    for (Chunk* c : chunksNeedVBO) {
        VBOWorkers.emplace_back(&Terrain::generateVBO, this, c);
    }
    chunksNeedVBO.clear();
    block_mut.unlock();

    for (auto& t : VBOWorkers) {
        if (t.joinable()) t.join();
    }

    // Step 2: Main-thread GPU buffer upload
    VBO_mut.lock();
    for (Chunk* c : chunksNeedToBeBuffered) {
        c->bufferInterleaved();  // GL calls = main thread only
    }
    chunksNeedToBeBuffered.clear();
    VBO_mut.unlock();
}
