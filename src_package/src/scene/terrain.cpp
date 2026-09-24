#include "terrain.h"
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <thread>
#include <glm/gtc/random.hpp>

namespace {
constexpr int CHUNK_SIZE = 16;
constexpr int ZONE_SIZE = 64;
// Zones examined around the player each tick: a 5x5 window (radius 2)
constexpr int ZONE_RADIUS = 3;
// GPU uploads allowed per frame, to spread loading cost over time
// GPU uploads run on the main thread, so keep the per-frame batch modest to
// avoid stalling the frame; the nearest-first mesh ordering means these
// slots always go to the terrain closest to the player.
constexpr size_t MAX_UPLOADS_PER_FRAME = 20;
constexpr int SHEEP_COUNT = 50;

bool isRedstone(BlockType t) {
    return t == WIRE_OFF || t == WIRE_ON || t == TORCH ||
           t == LEVER_OFF || t == LEVER_ON || t == LAMP_OFF || t == LAMP_ON;
}
}

Terrain::Terrain(OpenGLContext *context)
    : m_chunks(), m_generatedTerrain(), mp_context(context),
      m_biomeGenerator(12345), m_shutdown(false)
{
    // Worker pool shared by BlockTypeWorkers and VBOWorkers
    unsigned int n = std::thread::hardware_concurrency();
    unsigned int workerCount = n > 4 ? n - 2 : 2;
    for (unsigned int i = 0; i < workerCount; ++i) {
        m_workers.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lk(m_taskMut);
                    m_taskCv.wait(lk, [this]() { return m_shutdown || !m_tasks.empty(); });
                    if (m_shutdown && m_tasks.empty()) return;
                    task = std::move(m_tasks.front());
                    m_tasks.pop_front();
                }
                task();
            }
        });
    }

    // Sheep spawn positions in the meadows around the player spawn; each
    // sheep appears once its column has terrain data (underwater columns
    // are skipped, so the flock ends up on the lake shores)
    for (int i = 0; i < SHEEP_COUNT; ++i) {
        m_pendingSheep.push_back(glm::vec2(glm::linearRand(30.f, 170.f),
                                           glm::linearRand(300.f, 430.f)));
    }
}

Terrain::~Terrain() {
    {
        std::lock_guard<std::mutex> lk(m_taskMut);
        m_shutdown = true;
    }
    m_taskCv.notify_all();
    for (auto &w : m_workers) {
        if (w.joinable()) w.join();
    }
}

void Terrain::enqueueTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(m_taskMut);
        m_tasks.push_back(std::move(task));
    }
    m_taskCv.notify_one();
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

// Re-mesh and re-upload a chunk right away so block edits give instant
// feedback. If a VBOWorker currently owns the chunk's vboData, queue the
// re-mesh for the worker pipeline instead of racing it.
void Terrain::remeshChunkNow(Chunk *c) {
    if (c->m_meshing.load() || c->m_readyToBuffer.load()) {
        m_awaitingMesh.insert(c);
        return;
    }
    c->createVBOdata();
    c->bufferInterleaved();
}

void Terrain::setGlobalBlockAt(int x, int y, int z, BlockType t)
{
    if(hasChunkAt(x, z)) {
        uPtr<Chunk> &c = getChunkAt(x, z);
        glm::vec2 chunkOrigin = glm::vec2(floor(x / 16.f) * 16, floor(z / 16.f) * 16);
        int localX = x - static_cast<int>(chunkOrigin.x);
        int localZ = z - static_cast<int>(chunkOrigin.y);
        c->setLocalBlockAt(static_cast<unsigned int>(localX),
                           static_cast<unsigned int>(y),
                           static_cast<unsigned int>(localZ),
                           t);
        remeshChunkNow(c.get());

        // Circuits react to edits near any redstone element
        bool nearRedstone = isRedstone(t);
        if (!nearRedstone) {
            const glm::ivec3 rd[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
            for (const glm::ivec3 &d : rd) {
                glm::ivec3 n = glm::ivec3(x, y, z) + d;
                if (hasChunkAt(n.x, n.z) && n.y >= 1 && n.y <= 255 &&
                    isRedstone(getGlobalBlockAt(n.x, n.y, n.z))) {
                    nearRedstone = true;
                    break;
                }
            }
        }
        if (nearRedstone) {
            recomputeRedstone(glm::ivec3(x, y, z));
        }

        // Fluids react to edits: newly placed fluid spreads, and removing
        // a block lets adjacent fluid flow into the gap
        if (t == WATER || t == LAVA) {
            m_fluidQueue.push_back({glm::ivec3(x, y, z), 0});
        } else if (t == EMPTY) {
            const glm::ivec3 p(x, y, z);
            const glm::ivec3 dirs[5] = {{1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1}, {0,1,0}};
            for (const glm::ivec3 &d : dirs) {
                glm::ivec3 n = p + d;
                if (hasChunkAt(n.x, n.z)) {
                    BlockType nb = getGlobalBlockAt(n.x, n.y, n.z);
                    if (nb == WATER || nb == LAVA) m_fluidQueue.push_back({n, 0});
                }
            }
        }

        // Also update neighboring chunks if this block is on an edge
        if(localX == 0 && hasChunkAt(x - 1, z)) {
            remeshChunkNow(getChunkAt(x - 1, z).get());
        }
        else if(localX == 15 && hasChunkAt(x + 1, z)) {
            remeshChunkNow(getChunkAt(x + 1, z).get());
        }
        if(localZ == 0 && hasChunkAt(x, z - 1)) {
            remeshChunkNow(getChunkAt(x, z - 1).get());
        }
        else if(localZ == 15 && hasChunkAt(x, z + 1)) {
            remeshChunkNow(getChunkAt(x, z + 1).get());
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
    m_chunks[toKey(x, z)] = std::move(chunk);
    // Set the neighbor pointers of itself and its neighbors
    if(hasChunkAt(x, z + 16)) {
        cPtr->linkNeighbor(m_chunks[toKey(x, z + 16)], ZPOS);
    }
    if(hasChunkAt(x, z - 16)) {
        cPtr->linkNeighbor(m_chunks[toKey(x, z - 16)], ZNEG);
    }
    if(hasChunkAt(x + 16, z)) {
        cPtr->linkNeighbor(m_chunks[toKey(x + 16, z)], XPOS);
    }
    if(hasChunkAt(x - 16, z)) {
        cPtr->linkNeighbor(m_chunks[toKey(x - 16, z)], XNEG);
    }
    return cPtr;
}

bool Terrain::neighborsHaveBlockData(const Chunk *c) const {
    // The light grid spans this chunk plus its full 3x3 neighbourhood, so a
    // chunk must not mesh until all EIGHT surrounding chunks (diagonals
    // included) have block data - otherwise its edges bake with missing
    // neighbour light and render dark until a re-mesh corrects them, which
    // reads as dark patches popping in as the player moves.
    int cx = c->getMinX(), cz = c->getMinZ();
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            if (dx == 0 && dz == 0) continue;
            int nx = cx + dx * CHUNK_SIZE, nz = cz + dz * CHUNK_SIZE;
            if (!hasChunkAt(nx, nz)) continue;   // not yet created: a world edge
            if (!getChunkAt(nx, nz)->m_blocksFilled.load(std::memory_order_acquire)) {
                return false;
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Per-tick pipeline
// ---------------------------------------------------------------------------

void Terrain::primeSpawnArea(const glm::vec3 &playerPos) {
    int cx0 = static_cast<int>(glm::floor(playerPos.x / CHUNK_SIZE)) * CHUNK_SIZE;
    int cz0 = static_cast<int>(glm::floor(playerPos.z / CHUNK_SIZE)) * CHUNK_SIZE;

    // Fill a compact chunk radius around the player in parallel on the
    // worker pool (blocking), then mesh the inner ring - also in parallel -
    // so the near field is solid on frame one without a long stall. The
    // outer ring is filled only to give the meshed chunks seamless
    // boundaries; async streaming grows the rest over the next frames.
    // Fill radius must exceed mesh radius by one so every meshed chunk has
    // neighbor block data. Zones are NOT marked generated here; the async
    // BlockTypeWorker skips chunks we already filled (see updateTerrain).
    // Mesh out to ~96 blocks so the whole visible field (draw distance 112,
    // most of it already fading into fog past 96) is complete on the very
    // first frame - no terrain visibly building itself in as the game opens.
    constexpr int FILL_R = 11;  // 23x23 chunks filled
    constexpr int MESH_R = 10;  // 21x21 chunks meshed (~160-block field)

    std::vector<Chunk*> filled, toMesh;
    for (int dx = -FILL_R; dx <= FILL_R; ++dx) {
        for (int dz = -FILL_R; dz <= FILL_R; ++dz) {
            int cx = cx0 + dx * CHUNK_SIZE, cz = cz0 + dz * CHUNK_SIZE;
            Chunk *c = hasChunkAt(cx, cz) ? getChunkAt(cx, cz).get()
                                          : instantiateChunkAt(cx, cz);
            filled.push_back(c);
            if (std::abs(dx) <= MESH_R && std::abs(dz) <= MESH_R) toMesh.push_back(c);
        }
    }

    std::atomic<int> fdone{0};
    for (Chunk *c : filled) {
        enqueueTask([this, c, &fdone]() {
            if (!c->m_blocksFilled.load(std::memory_order_acquire)) {
                fillChunkWithTerrain(c);
                c->m_blocksFilled.store(true, std::memory_order_release);
            }
            fdone.fetch_add(1, std::memory_order_release);
        });
    }
    while (fdone.load(std::memory_order_acquire) < static_cast<int>(filled.size())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::atomic<int> mdone{0};
    for (Chunk *c : toMesh) {
        c->m_meshing.store(true);
        enqueueTask([c, &mdone]() {
            c->createVBOdata();
            c->m_readyToBuffer.store(true, std::memory_order_release);
            c->m_meshing.store(false, std::memory_order_release);
            mdone.fetch_add(1, std::memory_order_release);
        });
    }
    while (mdone.load(std::memory_order_acquire) < static_cast<int>(toMesh.size())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    for (Chunk *c : toMesh) {
        c->bufferInterleaved();
        c->m_readyToBuffer.store(false, std::memory_order_release);
    }
}

void Terrain::updateTerrain(const glm::vec3 &playerPos) {
    int zoneX = static_cast<int>(glm::floor(playerPos.x / ZONE_SIZE)) * ZONE_SIZE;
    int zoneZ = static_cast<int>(glm::floor(playerPos.z / ZONE_SIZE)) * ZONE_SIZE;

    // 1. Spawn a BlockTypeWorker for every ungenerated zone in the 5x5
    //    window around the player. Chunk objects are created here, on the
    //    main thread, so the chunk map never sees concurrent writes.
    for (int zx = zoneX - ZONE_RADIUS * ZONE_SIZE; zx <= zoneX + ZONE_RADIUS * ZONE_SIZE; zx += ZONE_SIZE) {
        for (int zz = zoneZ - ZONE_RADIUS * ZONE_SIZE; zz <= zoneZ + ZONE_RADIUS * ZONE_SIZE; zz += ZONE_SIZE) {
            int64_t zoneKey = toKey(zx, zz);
            if (m_generatedTerrain.find(zoneKey) != m_generatedTerrain.end()) {
                continue;
            }
            m_generatedTerrain.insert(zoneKey);

            std::vector<Chunk*> zoneChunks;
            zoneChunks.reserve(16);
            for (int cx = zx; cx < zx + ZONE_SIZE; cx += CHUNK_SIZE) {
                for (int cz = zz; cz < zz + ZONE_SIZE; cz += CHUNK_SIZE) {
                    if (!hasChunkAt(cx, cz)) {
                        zoneChunks.push_back(instantiateChunkAt(cx, cz));
                    }
                }
            }

            enqueueTask([this, zoneChunks]() { // BlockTypeWorker
                for (Chunk *c : zoneChunks) {
                    // Skip chunks the spawn primer already filled, so we
                    // never overwrite (or race) their block data.
                    if (c->m_blocksFilled.load(std::memory_order_acquire)) continue;
                    fillChunkWithTerrain(c);
                    c->m_blocksFilled.store(true, std::memory_order_release);
                }
                std::lock_guard<std::mutex> lk(m_filledMut);
                m_filledChunks.insert(m_filledChunks.end(), zoneChunks.begin(), zoneChunks.end());
            });
        }
    }

    // 2. Newly filled chunks need meshing; filled neighbors that were
    //    already meshed need re-meshing so their boundary walls disappear.
    std::vector<Chunk*> filled;
    {
        std::lock_guard<std::mutex> lk(m_filledMut);
        filled.swap(m_filledChunks);
    }
    for (Chunk *c : filled) {
        m_awaitingMesh.insert(c);
        for (Direction d : {XPOS, XNEG, ZPOS, ZNEG}) {
            Chunk *n = c->getNeighbor(d);
            if (n && n->m_blocksFilled.load(std::memory_order_acquire)) {
                m_awaitingMesh.insert(n);
            }
        }
    }

    // 3. Dispatch VBOWorkers NEAREST-FIRST: gather every waiting chunk whose
    //    neighbours have block data, sort by distance to the player, and mesh
    //    the closest ones first so terrain about to enter view is ready
    //    before anything distant - the player never outruns the near field.
    const glm::vec2 pp(playerPos.x, playerPos.z);
    auto distToPlayer = [&pp](const Chunk *c) {
        glm::vec2 mid(c->getMinX() + 8, c->getMinZ() + 8);
        return glm::dot(mid - pp, mid - pp);
    };
    // Only mesh chunks within (draw distance + a margin). The generation
    // window reaches further out purely to buffer the draw edge; meshing that
    // outer ring would be wasted work since it is never drawn. Chunks stay in
    // the queue and get meshed once the player moves close enough.
    constexpr float MESH_LIMIT_SQ = 208.f * 208.f;
    std::vector<Chunk*> toMesh;
    for (auto it = m_awaitingMesh.begin(); it != m_awaitingMesh.end();) {
        Chunk *c = *it;
        if (c->m_meshing.load() || c->m_readyToBuffer.load() ||
            !c->m_blocksFilled.load() || !neighborsHaveBlockData(c) ||
            distToPlayer(c) > MESH_LIMIT_SQ) {
            ++it;
            continue;
        }
        toMesh.push_back(c);
        it = m_awaitingMesh.erase(it);
    }
    std::sort(toMesh.begin(), toMesh.end(),
              [&](const Chunk *a, const Chunk *b) { return distToPlayer(a) < distToPlayer(b); });
    for (Chunk *c : toMesh) {
        c->m_meshing.store(true);
        enqueueTask([this, c]() { // VBOWorker
            c->createVBOdata();
            c->m_readyToBuffer.store(true, std::memory_order_release);
            c->m_meshing.store(false, std::memory_order_release);
            std::lock_guard<std::mutex> lk(m_readyMut);
            m_readyChunks.push_back(c);
        });
    }

    // 4. Upload finished VBO data to the GPU (main thread only), nearest
    //    first and capped per frame so loading never stalls a frame for long.
    std::vector<Chunk*> ready;
    {
        // Workers finish roughly nearest-first (they were dispatched that
        // way), so the front of the queue is already the closest terrain -
        // no need to re-sort under the lock and contend with the workers.
        std::lock_guard<std::mutex> lk(m_readyMut);
        size_t n = std::min(m_readyChunks.size(), MAX_UPLOADS_PER_FRAME);
        ready.assign(m_readyChunks.begin(), m_readyChunks.begin() + n);
        m_readyChunks.erase(m_readyChunks.begin(), m_readyChunks.begin() + n);
    }
    for (Chunk *c : ready) {
        c->bufferInterleaved();
        c->m_readyToBuffer.store(false, std::memory_order_release);
    }

    spawnReadySheep();
}

void Terrain::spawnReadySheep() {
    for (auto it = m_pendingSheep.begin(); it != m_pendingSheep.end();) {
        int x = static_cast<int>(glm::floor(it->x));
        int z = static_cast<int>(glm::floor(it->y));
        if (!hasChunkAt(x, z) || !getChunkAt(x, z)->m_blocksFilled.load()) {
            ++it;
            continue;
        }
        // Find the ground surface, ignoring water and tree canopies.
        // Columns whose ground sits below the waterline get no sheep.
        for (int y = WorldGen::MAX_HEIGHT; y > 0; --y) {
            BlockType b = getGlobalBlockAt(x, y, z);
            if (b == EMPTY || b == WATER || b == LEAF || b == WOOD) continue;
            if (y > WorldGen::WATER_LEVEL) {
                m_sheep.push_back(std::make_unique<Sheep>(glm::vec3(it->x, y + 1.5f, it->y), *this));
            }
            break;
        }
        it = m_pendingSheep.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

// AABB vs. frustum: the chunk is culled only if it lies entirely on the
// negative side of one of the six planes.
bool Terrain::chunkInFrustum(int x, int z, const std::array<glm::vec4, 6> &planes) {
    for (const glm::vec4 &p : planes) {
        glm::vec3 positiveVertex(p.x > 0 ? x + CHUNK_SIZE : x,
                                 p.y > 0 ? 256.f : 0.f,
                                 p.z > 0 ? z + CHUNK_SIZE : z);
        if (glm::dot(glm::vec3(p), positiveVertex) + p.w < 0.f) {
            return false;
        }
    }
    return true;
}

void Terrain::drawOpaque(int minX, int maxX, int minZ, int maxZ, ShaderProgram *sh,
                         const std::array<glm::vec4, 6> *frustumPlanes) {
    for (int x = minX; x < maxX; x += CHUNK_SIZE) {
        for (int z = minZ; z < maxZ; z += CHUNK_SIZE) {
            if (!hasChunkAt(x, z)) continue;
            if (frustumPlanes && !chunkInFrustum(x, z, *frustumPlanes)) continue;
            auto &c = getChunkAt(x, z);
            if (c->elemCount(BufferType::OPAQUE_INDEX) <= 0) continue;
            glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(x, 0, z));
            sh->setUnifMat4("u_Model", model);
            sh->setUnifMat4("u_ModelInvTr", model); // translation-only: inv-transpose == model
            sh->drawInterleaved(*c, BufferType::OPAQUE_INTERLEAVED, BufferType::OPAQUE_INDEX);
        }
    }
}

void Terrain::drawTransparent(int minX, int maxX, int minZ, int maxZ, ShaderProgram *sh,
                              const std::array<glm::vec4, 6> *frustumPlanes) {
    for (int x = minX; x < maxX; x += CHUNK_SIZE) {
        for (int z = minZ; z < maxZ; z += CHUNK_SIZE) {
            if (!hasChunkAt(x, z)) continue;
            if (frustumPlanes && !chunkInFrustum(x, z, *frustumPlanes)) continue;
            auto &c = getChunkAt(x, z);
            if (c->elemCount(BufferType::TRANSPARENT_INDEX) <= 0) continue;
            glm::mat4 model = glm::translate(glm::mat4(1.f), glm::vec3(x, 0, z));
            sh->setUnifMat4("u_Model", model);
            sh->setUnifMat4("u_ModelInvTr", model);
            sh->drawInterleaved(*c, BufferType::TRANSPARENT_INTERLEAVED, BufferType::TRANSPARENT_INDEX);
        }
    }
}
