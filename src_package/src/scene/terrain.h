
#pragma once
#include "smartpointerhelp.h"
#include "glm_includes.h"
#include "chunk.h"
#include <array>
#include <unordered_map>
#include <unordered_set>
#include "shaderprogram.h"
#include "cube.h"
#include "biomegenerator.h"
#include <mutex>
#include "sheep.h"

// Helper functions to convert (x, z) to and from hash map key
int64_t toKey(int x, int z);
glm::ivec2 toCoords(int64_t k);

// The container class for all of the Chunks in the game.
// Ultimately, while Terrain will always store all Chunks,
// not all Chunks will be drawn at any given time as the world
// expands.
class Terrain {
private:
    // Stores every Chunk according to the location of its lower-left corner
    // in world space.
    // We combine the X and Z coordinates of the Chunk's corner into one 64-bit int
    // so that we can use them as a key for the map, as objects like std::pairs or
    // glm::ivec2s are not hashable by default, so they cannot be used as keys.
    std::unordered_map<int64_t, uPtr<Chunk>> m_chunks;
    // We will designate every 64 x 64 area of the world's x-z plane
    // as one "terrain generation zone". Every time the player moves
    // near a portion of the world that has not yet been generated
    // (i.e. its lower-left coordinates are not in this set), a new
    // 4 x 4 collection of Chunks is created to represent that area
    // of the world.
    // The world that exists when the base code is run consists of exactly
    // one 64 x 64 area with its lower-left corner at (0, 0).
    // When milestone 1 has been implemented, the Player can move around the
    // world to add more "terrain generation zone" IDs to this set.
    // While only the 3 x 3 collection of terrain generation zones
    // surrounding the Player should be rendered, the Chunks
    // in the Terrain will never be deleted until the program is terminated.
    std::unordered_set<int64_t> m_generatedTerrain;
    // The instance of a unit cube we can use to render any cube.
    // Will be removed in future milestones.
    Cube m_geomCube;
    // Set this to "true" whenever you modify the blocks
    // in your terrain. NOT NEEDED ONCE MILESTONE 1's CHUNKING
    // IS IMPLEMENTED.
    bool m_chunkVBOsNeedUpdating;
    OpenGLContext* mp_context;
    BiomeGenerator m_biomeGenerator;

    // Multithreading-related
    std::mutex block_mut, VBO_mut;
    std::vector<Chunk*> chunksNeedVBO;
    std::vector<Chunk*> chunksNeedToBeBuffered;
    std::vector<std::pair<float, std::pair<int, int>>> m_pendingChunks;

    // Generate 4*4 chunks in a zone
    void generateChunks(int x, int z);
    void generateVBO(Chunk* c);
    void fillChunkWithTerrain(Chunk* chunk, int chunkX, int chunkZ);

public:
    Terrain(OpenGLContext *context);
    ~Terrain();
    // Instantiates a new Chunk and stores it in
    // our chunk map at the given coordinates.
    // Returns a pointer to the created Chunk.
    Chunk* instantiateChunkAt(int x, int z);
    // Do these world-space coordinates lie within
    // a Chunk that exists?
    bool hasChunkAt(int x, int z) const;
    // Assuming a Chunk exists at these coords,
    // return a mutable reference to it

    // Assuming a Chunk exists at these coords,
    // return a mutable reference to it
    uPtr<Chunk>& getChunkAt(int x, int z);
    // Assuming a Chunk exists at these coords,
    // return a const reference to it
    const uPtr<Chunk>& getChunkAt(int x, int z) const;
    // Given a world-space coordinate (which may have negative
    // values) return the block stored at that point in space.
    BlockType getGlobalBlockAt(int x, int y, int z) const;
    BlockType getGlobalBlockAt(glm::vec3 p) const;
    // Given a world-space coordinate (which may have negative
    // values) set the block at that point in space to the
    // given type.
    void setGlobalBlockAt(int x, int y, int z, BlockType t);
    // Draws every Chunk that falls within the bounding box
    // described by the min and max coords, using the provided
    // ShaderProgram
    void draw(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram);
    // Initializes the Chunks that store the 64 x 256 x 64 block scene you
    // see when the base code is run.
    void CreateTestScene();
    // Creates procedural terrain with biomes
    void generateTerrain();
    // Checks player position and creates new chunks as needed
    void checkAndCreateNewChunks(const glm::vec3 &playerPos);
    void checkAndCreateNewChunksperZone(const glm::vec3 &playerPos);
    void createcheckchunks(int newChunkX, int newChunkZ);
    void consumeChunkWork();

    // Draw opaque and transparent blocks separately
    void drawOpaque(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram);
    void drawTransparent(int minX, int maxX, int minZ, int maxZ, ShaderProgram *shaderProgram);

    // Accessor for testing
    std::unordered_set<int64_t> getGenerated() {return m_generatedTerrain;}

    std::vector<std::unique_ptr<Sheep>> m_sheep;
    Cube& getCube() { return m_geomCube; }

};
