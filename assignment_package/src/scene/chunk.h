#pragma once
#include "smartpointerhelp.h"
#include "glm_includes.h"
#include "drawable.h"
#include <array>
#include <unordered_map>
#include <cstddef>
#include <vector>

// C++ 11 allows us to define the size of an enum. This lets us use only one byte
// of memory to store our different block types. By default, the size of a C++ enum
// is that of an int (so, usually four bytes). This *does* limit us to only 256 different
// block types, but in the scope of this project we'll never get anywhere near that many.
enum BlockType : unsigned char
{
    EMPTY, GRASS, DIRT, STONE, WATER, SNOW, LAVA, BEDROCK
};

// The six cardinal directions in 3D space
enum Direction : unsigned char
{
    XPOS, XNEG, YPOS, YNEG, ZPOS, ZNEG
};

// Lets us use any enum class as the key of a
// std::unordered_map
struct EnumHash {
    template <typename T>
    size_t operator()(T t) const {
        return static_cast<size_t>(t);
    }
};

struct VBOData {
    std::vector<glm::vec4> opaqueVBO;
    std::vector<GLuint> opaqueIndex;
    std::vector<glm::vec4> transparentVBO;
    std::vector<GLuint> transparentIndex;
};

// One Chunk is a 16 x 256 x 16 section of the world,
// containing all the Minecraft blocks in that area.
// We divide the world into Chunks in order to make
// recomputing its VBO data faster by not having to
// render all the world at once, while also not having
// to render the world block by block.
class Chunk : public Drawable {
private:
    // All of the blocks contained within this Chunk
    std::array<BlockType, 65536> m_blocks;
    // The coordinates of the chunk's lower-left corner in world space
    int minX, minZ;
    // This Chunk's four neighbors to the north, south, east, and west
    // The third input to this map just lets us use a Direction as
    // a key for this map.
    // These allow us to properly determine
    std::unordered_map<Direction, Chunk*, EnumHash> m_neighbors;

    // Helper function to check if a block is potentially visible
    bool isBlockVisible(int x, int y, int z) const;
    // Helper function to get block color
    glm::vec3 getColorForBlock(BlockType t);
    // For creating face data
    void createFace(std::vector<glm::vec4> &interleaved, std::vector<GLuint> &indices,
                    glm::vec3 pos, Direction dir, BlockType type, bool isOpaque);

public:
    Chunk(OpenGLContext* context, int x, int z);
    virtual ~Chunk();

    BlockType getLocalBlockAt(unsigned int x, unsigned int y, unsigned int z) const;
    BlockType getLocalBlockAt(int x, int y, int z) const;
    void setLocalBlockAt(unsigned int x, unsigned int y, unsigned int z, BlockType t);

    // Get adjacent chunk blocks that might affect VBO creation
    BlockType getAdjacentChunkBlockAt(int x, int y, int z) const;

    void linkNeighbor(uPtr<Chunk>& neighbor, Direction dir);
    void createVBOdata() override;
    void destroyVBOdata() override;

    int getMinX() const;
    int getMinZ() const;
    void sendVBOdataToGPU();
    bool hasVBOData() const;

    // For multithreading
    VBOData vboData;
    void bufferInterleaved();
};

