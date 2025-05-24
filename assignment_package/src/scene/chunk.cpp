#include "chunk.h"
#include "drawable.h"
#include <smartpointerhelp.h>

Chunk::Chunk(OpenGLContext* context, int x, int z) : Drawable(context)
{
    minX = x;
    minZ = z;
    m_neighbors = {{XPOS, nullptr}, {XNEG, nullptr}, {ZPOS, nullptr}, {ZNEG, nullptr}};
    std::fill_n(m_blocks.begin(), 65536, EMPTY);

}

// Does bounds checking with at()
BlockType Chunk::getLocalBlockAt(unsigned int x, unsigned int y, unsigned int z) const {
    return m_blocks.at(x + 16 * y + 16 * 256 * z);
}

// Exists to get rid of compiler warnings about int -> unsigned int implicit conversion
BlockType Chunk::getLocalBlockAt(int x, int y, int z) const {
    return getLocalBlockAt(static_cast<unsigned int>(x), static_cast<unsigned int>(y), static_cast<unsigned int>(z));
}

// Does bounds checking with at()
void Chunk::setLocalBlockAt(unsigned int x, unsigned int y, unsigned int z, BlockType t) {
    m_blocks.at(x + 16 * y + 16 * 256 * z) = t;
}

// Get block in adjacent chunks
BlockType Chunk::getAdjacentChunkBlockAt(int x, int y, int z) const {
    // Handle the case where the coordinates are outside this chunk
    if (x < 0) {
        if (m_neighbors.at(XNEG) != nullptr) {
            return m_neighbors.at(XNEG)->getLocalBlockAt(x + 16, y, z);
        }
        return EMPTY;
    } else if (x >= 16) {
        if (m_neighbors.at(XPOS) != nullptr) {
            return m_neighbors.at(XPOS)->getLocalBlockAt(x - 16, y, z);
        }
        return EMPTY;
    } else if (z < 0) {
        if (m_neighbors.at(ZNEG) != nullptr) {
            return m_neighbors.at(ZNEG)->getLocalBlockAt(x, y, z + 16);
        }
        return EMPTY;
    } else if (z >= 16) {
        if (m_neighbors.at(ZPOS) != nullptr) {
            return m_neighbors.at(ZPOS)->getLocalBlockAt(x, y, z - 16);
        }
        return EMPTY;
    } else if (y < 0 || y >= 256) {
        return EMPTY;
    }
    // If we're still here, just return our own block
    return getLocalBlockAt(x, y, z);
}

const static std::unordered_map<Direction, Direction, EnumHash> oppositeDirection {
    {XPOS, XNEG},
    {XNEG, XPOS},
    {YPOS, YNEG},
    {YNEG, YPOS},
    {ZPOS, ZNEG},
    {ZNEG, ZPOS}
};

void Chunk::linkNeighbor(uPtr<Chunk> &neighbor, Direction dir) {
    if(neighbor != nullptr) {
        this->m_neighbors[dir] = neighbor.get();
        neighbor->m_neighbors[oppositeDirection.at(dir)] = this;
    }
}

void Chunk::destroyVBOdata() {
    Drawable::destroyVBOdata();
}

Chunk::~Chunk() {
    destroyVBOdata();
}

// Check if a block is visible (has at least one empty or transparent neighbor)
bool Chunk::isBlockVisible(int x, int y, int z) const {
    BlockType blockType = getLocalBlockAt(x, y, z);
    if (blockType == EMPTY) return false;

    // Check all six directions
    auto isNeighborTransparentOrEmpty = [](BlockType t) -> bool {
        return t == EMPTY || t == WATER; // Add other transparent blocks here
    };

    return (x == 0 || x == 15 || z == 0 || z == 15 || y == 0 || y == 255 ||
            isNeighborTransparentOrEmpty(getAdjacentChunkBlockAt(x + 1, y, z)) ||
            isNeighborTransparentOrEmpty(getAdjacentChunkBlockAt(x - 1, y, z)) ||
            isNeighborTransparentOrEmpty(getAdjacentChunkBlockAt(x, y + 1, z)) ||
            isNeighborTransparentOrEmpty(getAdjacentChunkBlockAt(x, y - 1, z)) ||
            isNeighborTransparentOrEmpty(getAdjacentChunkBlockAt(x, y, z + 1)) ||
            isNeighborTransparentOrEmpty(getAdjacentChunkBlockAt(x, y, z - 1)));
}

glm::vec3 Chunk::getColorForBlock(BlockType t) {
    switch(t) {
    case GRASS:
        return glm::vec3(95.f, 159.f, 53.f) / 255.f;
    case DIRT:
        return glm::vec3(121.f, 85.f, 58.f) / 255.f;
    case STONE:
        return glm::vec3(0.5f);
    case WATER:
        return glm::vec3(0.f, 0.f, 0.75f);
    case SNOW:
        return glm::vec3(1.f);
    case LAVA:
        return glm::vec3(1.f, 0.5f, 0.f);
    case BEDROCK:
        return glm::vec3(0.2f);
    default:
        return glm::vec3(1.f, 0.f, 1.f); // Debug purple
    }
}

void Chunk::createFace(std::vector<glm::vec4> &interleaved, std::vector<GLuint> &indices,
                       glm::vec3 pos, Direction dir, BlockType type, bool isOpaque) {
    glm::vec4 color(getColorForBlock(type), type == WATER ? 0.5f : 1.0f);
    glm::vec4 baseUV;
    bool isAnimatable = (type == WATER || type == LAVA);

    // Set UV coordinates based on block type and face direction
    switch(type) {
    case GRASS:
        if (dir == YPOS) {
            baseUV = glm::vec4(8.0f/16.0f, 13.0f/16.0f, 0.0f, 0.0f);
        } else {
            baseUV = glm::vec4(3.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        }
        break;
    case DIRT:
        baseUV = glm::vec4(2.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        break;
    case STONE:
        baseUV = glm::vec4(1.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        break;
    case WATER:
        baseUV = glm::vec4(13.0f/16.0f, 3.0f/16.0f, isAnimatable ? 1.0f : 0.0f, 0.0f);
        break;
    case SNOW:
        baseUV = glm::vec4(2.0f/16.0f, 11.0f/16.0f, 0.0f, 0.0f);
        break;
    case LAVA:
        baseUV = glm::vec4(13.0f/16.0f, 1.0f/16.0f, isAnimatable ? 1.0f : 0.0f, 0.0f);
        break;
    case BEDROCK:
        baseUV = glm::vec4(1.0f/16.0f, 14.0f/16.0f, 0.0f, 0.0f);
        break;
    default:
        baseUV = glm::vec4(7.0f/16.0f, 1.0f/16.0f, 0.0f, 0.0f); // Debug texture
    }

    // Define face vertices based on direction
    GLuint startIdx = interleaved.size() / 4; // Each vertex consists of 4 vec4s (pos, nor, col, uv)

    glm::vec4 normal;
    std::array<glm::vec4, 4> positions;
    std::array<glm::vec4, 4> uvs;

    switch(dir) {
    case XPOS: // Right face (+X)
        normal = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        positions[0] = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[1] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[2] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[3] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        break;
    case XNEG: // Left face (-X)
        normal = glm::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
        positions[0] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[1] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[2] = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[3] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        break;
    case YPOS: // Top face (+Y)
        normal = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        positions[0] = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[1] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[2] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[3] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        break;
    case YNEG: // Bottom face (-Y)
        normal = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        positions[0] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[1] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[2] = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[3] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        break;
    case ZPOS: // Front face (+Z)
        normal = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        positions[0] = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[1] = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[2] = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[3] = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f) + glm::vec4(pos, 0.0f);
        break;
    case ZNEG: // Back face (-Z)
        normal = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        positions[0] = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[1] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[2] = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        positions[3] = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) + glm::vec4(pos, 0.0f);
        break;
    }

    // Standard UVs
    uvs[0] = glm::vec4(0.0f, 0.0f, isAnimatable ? 1.0f : 0.0f, 0.0f) + baseUV;
    uvs[1] = glm::vec4(1.0f/16.0f, 0.0f, isAnimatable ? 1.0f : 0.0f, 0.0f) + baseUV;
    uvs[2] = glm::vec4(1.0f/16.0f, 1.0f/16.0f, isAnimatable ? 1.0f : 0.0f, 0.0f) + baseUV;
    uvs[3] = glm::vec4(0.0f, 1.0f/16.0f, isAnimatable ? 1.0f : 0.0f, 0.0f) + baseUV;

    // Add all vertex data to the interleaved array
    for (int i = 0; i < 4; i++) {
        interleaved.push_back(positions[i]);
        interleaved.push_back(normal);
        interleaved.push_back(color);
        interleaved.push_back(uvs[i]);
    }

    // Add indices for two triangles using CCW winding
    indices.push_back(startIdx);
    indices.push_back(startIdx + 1);
    indices.push_back(startIdx + 2);
    indices.push_back(startIdx);
    indices.push_back(startIdx + 2);
    indices.push_back(startIdx + 3);
}

void Chunk::bufferInterleaved() {
    // Buffer opaque data
    if (!vboData.opaqueVBO.empty()) {
        generateBuffer(BufferType::OPAQUE_INTERLEAVED);
        bindBuffer(BufferType::OPAQUE_INTERLEAVED);
        mp_context->glBufferData(GL_ARRAY_BUFFER, vboData.opaqueVBO.size() * sizeof(glm::vec4),
                                 vboData.opaqueVBO.data(), GL_STATIC_DRAW);

        generateBuffer(BufferType::OPAQUE_INDEX);
        bindBuffer(BufferType::OPAQUE_INDEX);
        mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, vboData.opaqueIndex.size() * sizeof(GLuint),
                                 vboData.opaqueIndex.data(), GL_STATIC_DRAW);

        indexCounts[BufferType::OPAQUE_INDEX] = vboData.opaqueIndex.size();
    }

    // Buffer transparent data
    if (!vboData.transparentVBO.empty()) {
        generateBuffer(BufferType::TRANSPARENT_INTERLEAVED);
        bindBuffer(BufferType::TRANSPARENT_INTERLEAVED);
        mp_context->glBufferData(GL_ARRAY_BUFFER, vboData.transparentVBO.size() * sizeof(glm::vec4),
                                 vboData.transparentVBO.data(), GL_STATIC_DRAW);

        generateBuffer(BufferType::TRANSPARENT_INDEX);
        bindBuffer(BufferType::TRANSPARENT_INDEX);
        mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, vboData.transparentIndex.size() * sizeof(GLuint),
                                 vboData.transparentIndex.data(), GL_STATIC_DRAW);

        indexCounts[BufferType::TRANSPARENT_INDEX] = vboData.transparentIndex.size();
    }
}

void Chunk::createVBOdata() {
    // Clear existing VBO data
    vboData.opaqueVBO.clear();
    vboData.opaqueIndex.clear();
    vboData.transparentVBO.clear();
    vboData.transparentIndex.clear();

    // Iterate through all blocks in the chunk
    for(int x = 0; x < 16; ++x) {
        for(int y = 0; y < 256; ++y) {
            for(int z = 0; z < 16; ++z) {
                BlockType type = getLocalBlockAt(x, y, z);

                // Skip empty blocks
                if(type == EMPTY) {
                    continue;
                }

                // Skip blocks that aren't potentially visible
                if (!isBlockVisible(x, y, z)) {
                    continue;
                }

                // Check each of the six directions
                glm::vec3 pos(x, y, z);
                bool isBlockTransparent = (type == WATER);

                // Determine if each face needs to be created
                // For each direction, check if the adjacent block is empty or transparent
                auto shouldCreateFace = [this, isBlockTransparent](int x, int y, int z) -> bool {
                    BlockType adjacentType = this->getAdjacentChunkBlockAt(x, y, z);
                    // Always create face between transparent and opaque blocks
                    return adjacentType == EMPTY ||
                           (adjacentType == WATER && !isBlockTransparent) ||
                           (isBlockTransparent && adjacentType != WATER && adjacentType != EMPTY);
                };

                // XPOS face
                if(shouldCreateFace(x + 1, y, z)) {
                    if (isBlockTransparent) {
                        createFace(vboData.transparentVBO, vboData.transparentIndex, pos, XPOS, type, false);
                    } else {
                        createFace(vboData.opaqueVBO, vboData.opaqueIndex, pos, XPOS, type, true);
                    }
                }

                // XNEG face
                if(shouldCreateFace(x - 1, y, z)) {
                    if (isBlockTransparent) {
                        createFace(vboData.transparentVBO, vboData.transparentIndex, pos, XNEG, type, false);
                    } else {
                        createFace(vboData.opaqueVBO, vboData.opaqueIndex, pos, XNEG, type, true);
                    }
                }

                // YPOS face
                if(shouldCreateFace(x, y + 1, z)) {
                    if (isBlockTransparent) {
                        createFace(vboData.transparentVBO, vboData.transparentIndex, pos, YPOS, type, false);
                    } else {
                        createFace(vboData.opaqueVBO, vboData.opaqueIndex, pos, YPOS, type, true);
                    }
                }

                // YNEG face
                if(shouldCreateFace(x, y - 1, z)) {
                    if (isBlockTransparent) {
                        createFace(vboData.transparentVBO, vboData.transparentIndex, pos, YNEG, type, false);
                    } else {
                        createFace(vboData.opaqueVBO, vboData.opaqueIndex, pos, YNEG, type, true);
                    }
                }

                // ZPOS face
                if(shouldCreateFace(x, y, z + 1)) {
                    if (isBlockTransparent) {
                        createFace(vboData.transparentVBO, vboData.transparentIndex, pos, ZPOS, type, false);
                    } else {
                        createFace(vboData.opaqueVBO, vboData.opaqueIndex, pos, ZPOS, type, true);
                    }
                }

                // ZNEG face
                if(shouldCreateFace(x, y, z - 1)) {
                    if (isBlockTransparent) {
                        createFace(vboData.transparentVBO, vboData.transparentIndex, pos, ZNEG, type, false);
                    } else {
                        createFace(vboData.opaqueVBO, vboData.opaqueIndex, pos, ZNEG, type, true);
                    }
                }
            }
        }
    }
}

int Chunk::getMinX() const {
    return minX;
}

int Chunk::getMinZ() const {
    return minZ;
}

void Chunk::sendVBOdataToGPU() {
    bufferInterleaved();
}

bool Chunk::hasVBOData() const {
    // Check if the buffer handles for opaque and transparent geometry exist
    return (bufHandles.find(OPAQUE_INTERLEAVED) != bufHandles.end() && bufHandles.at(OPAQUE_INTERLEAVED) != 0) ||
           (bufHandles.find(TRANSPARENT_INTERLEAVED) != bufHandles.end() && bufHandles.at(TRANSPARENT_INTERLEAVED) != 0);
}
