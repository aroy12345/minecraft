#pragma once
#include "smartpointerhelp.h"
#include "glm_includes.h"
#include "chunk.h"
#include "shaderprogram.h"
#include "biomegenerator.h"
#include "sheep.h"
#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Helper functions to convert (x, z) to and from hash map key
int64_t toKey(int x, int z);
glm::ivec2 toCoords(int64_t k);

// The container class for all of the Chunks in the game.
//
// Terrain generation is multithreaded (Milestone 2). Every tick,
// updateTerrain() examines the 5x5 window of 64x64 terrain generation zones
// around the player:
//  - For each zone that has never been generated, its 16 Chunks are
//    instantiated on the main thread (so the chunk map is only ever touched
//    by the main thread) and one BlockTypeWorker task fills their block
//    data on a worker thread.
//  - Once a Chunk and its instantiated neighbors have block data, a
//    VBOWorker task computes its interleaved vertex/index data.
//  - Completed VBO data is uploaded to the GPU on the main thread only,
//    a few chunks per frame to avoid hitches.
// Workers communicate with the main thread through mutex-guarded queues.
class Terrain {
private:
    // Stores every Chunk according to the location of its lower-left corner
    // in world space, keyed by the X and Z coordinates packed into one
    // 64-bit int. Written and read on the main thread only.
    std::unordered_map<int64_t, uPtr<Chunk>> m_chunks;
    // Lower-left corners of every 64 x 64 terrain generation zone whose
    // chunks have been created. Chunks are never deleted once generated.
    std::unordered_set<int64_t> m_generatedTerrain;
    OpenGLContext* mp_context;
    BiomeGenerator m_biomeGenerator;

    // --- Worker pool -----------------------------------------------------
    // BlockTypeWorker and VBOWorker tasks both run on this pool.
    std::vector<std::thread> m_workers;
    std::deque<std::function<void()>> m_tasks;
    std::mutex m_taskMut;
    std::condition_variable m_taskCv;
    bool m_shutdown;

    // Chunks whose block data a BlockTypeWorker just finished (shared).
    std::vector<Chunk*> m_filledChunks;
    std::mutex m_filledMut;
    // Chunks whose CPU-side VBO data a VBOWorker just finished (shared).
    std::vector<Chunk*> m_readyChunks;
    std::mutex m_readyMut;

    // Main-thread only: chunks that need a VBOWorker dispatched. A chunk
    // waits here until its instantiated neighbors have block data, so no
    // worker ever reads a half-filled neighbor.
    std::unordered_set<Chunk*> m_awaitingMesh;

    // Sheep whose spawn column has no terrain yet; spawned lazily once
    // their chunk's block data exists.
    std::vector<glm::vec2> m_pendingSheep;

    // Fluid "simulation" (Milestone 3): pending spread steps, processed a
    // few per tick so broken dams and placed water visibly flow outward.
    struct FluidSpread { glm::ivec3 pos; int depth; };
    std::deque<FluidSpread> m_fluidQueue;

    // Writes one block without the immediate remesh (bulk edits and fluid
    // flow batch their remeshes through m_awaitingMesh instead)
    void setBlockDeferred(int x, int y, int z, BlockType t);

    void enqueueTask(std::function<void()> task);
    void fillChunkWithTerrain(Chunk* chunk);
    void stampTrees(Chunk* chunk);
    void stampStructures(Chunk* chunk);
    Chunk* instantiateChunkAt(int x, int z);
    bool neighborsHaveBlockData(const Chunk* c) const;
    void spawnReadySheep();
    // Re-mesh a chunk immediately on the main thread (used for block edits).
    void remeshChunkNow(Chunk* c);

    // Chunk AABB vs. frustum test used by the draw passes
    static bool chunkInFrustum(int x, int z, const std::array<glm::vec4, 6> &planes);

public:
    Terrain(OpenGLContext *context);
    ~Terrain();

    // Do these world-space coordinates lie within a Chunk that exists?
    bool hasChunkAt(int x, int z) const;
    // Assuming a Chunk exists at these coords, return a reference to it
    uPtr<Chunk>& getChunkAt(int x, int z);
    const uPtr<Chunk>& getChunkAt(int x, int z) const;
    // Given a world-space coordinate (which may have negative
    // values) return the block stored at that point in space.
    BlockType getGlobalBlockAt(int x, int y, int z) const;
    BlockType getGlobalBlockAt(glm::vec3 p) const;
    // Given a world-space coordinate (which may have negative
    // values) set the block at that point in space to the
    // given type.
    void setGlobalBlockAt(int x, int y, int z, BlockType t);

    // Called every frame from MyGL::tick(): expands the world around the
    // player, moves chunk data through the worker pipeline, uploads
    // finished VBOs, spawns pending sheep, and advances fluid flow.
    void updateTerrain(const glm::vec3 &playerPos);

    // How many chunks are still waiting to be meshed or uploaded. The
    // self-test polls this so it only starts once the world has streamed in,
    // making its timing independent of how long the initial load takes.
    size_t pendingMeshCount() const { return m_awaitingMesh.size() + m_readyChunks.size(); }

    // Called once from initializeGL, before the first frame: synchronously
    // generates and meshes the chunks immediately around spawn so the world
    // is already there on frame one, instead of the player staring at empty
    // sky while worker threads catch up.
    void primeSpawnArea(const glm::vec3 &playerPos);

    // Fluid flow steps processed per tick (see m_fluidQueue)
    void processFluids();

    // Redstone (Milestone 3): recompute the circuit region around an
    // edited cell - levers and torches power wire (with distance decay)
    // and lamps light up when a powered element touches them.
    void recomputeRedstone(const glm::ivec3 &center);

    // Bulk world edits used by the height-map and OBJ importers: rebuild a
    // column to a new height/surface, or set a batch of individual blocks.
    void setColumn(int x, int z, int height, BlockType top);
    void setBlocksBulk(const std::vector<glm::ivec3> &cells, BlockType t);

    // Draws every Chunk with GPU data inside the bounding box, skipping
    // chunks outside the view frustum when planes are provided.
    void drawOpaque(int minX, int maxX, int minZ, int maxZ, ShaderProgram *sh,
                    const std::array<glm::vec4, 6> *frustumPlanes = nullptr);
    void drawTransparent(int minX, int maxX, int minZ, int maxZ, ShaderProgram *sh,
                         const std::array<glm::vec4, 6> *frustumPlanes = nullptr);

    std::vector<std::unique_ptr<Sheep>> m_sheep;
};
