#include "chunk_internal.h"
#include "drawable.h"
#include <smartpointerhelp.h>
#include <algorithm>

using namespace ChunkInternal;


Chunk::Chunk(OpenGLContext* context, int x, int z) : Drawable(context)
{
    minX = x;
    minZ = z;
    for (auto &n : m_neighbors) {
        n.store(nullptr, std::memory_order_relaxed);
    }
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
        if (Chunk *n = getNeighbor(XNEG)) {
            return n->getLocalBlockAt(x + 16, y, z);
        }
        return EMPTY;
    } else if (x >= 16) {
        if (Chunk *n = getNeighbor(XPOS)) {
            return n->getLocalBlockAt(x - 16, y, z);
        }
        return EMPTY;
    } else if (z < 0) {
        if (Chunk *n = getNeighbor(ZNEG)) {
            return n->getLocalBlockAt(x, y, z + 16);
        }
        return EMPTY;
    } else if (z >= 16) {
        if (Chunk *n = getNeighbor(ZPOS)) {
            return n->getLocalBlockAt(x, y, z - 16);
        }
        return EMPTY;
    } else if (y < 0 || y >= 256) {
        return EMPTY;
    }
    // If we're still here, just return our own block
    return getLocalBlockAt(x, y, z);
}

static Direction oppositeDirection(Direction dir) {
    switch (dir) {
    case XPOS: return XNEG;
    case XNEG: return XPOS;
    case YPOS: return YNEG;
    case YNEG: return YPOS;
    case ZPOS: return ZNEG;
    default:   return ZPOS;
    }
}

void Chunk::linkNeighbor(uPtr<Chunk> &neighbor, Direction dir) {
    if(neighbor != nullptr) {
        this->m_neighbors[dir].store(neighbor.get(), std::memory_order_release);
        neighbor->m_neighbors[oppositeDirection(dir)].store(this, std::memory_order_release);
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

void Chunk::createFace(std::vector<glm::vec4> &interleaved, std::vector<GLuint> &indices,
                       glm::vec3 pos, Direction dir, BlockType type,
                       const unsigned char *cells,
                       const unsigned char *skyLight,
                       const unsigned char *blockLight) {
    // Vertex color channels carry baked lighting for the fragment shader:
    // r = sky light, g = block light (lava glow), b = foliage-tint flag,
    // a < 1 marks water. Light and ambient occlusion are computed per
    // vertex below, which is what gives Minecraft its soft look.
    // color.b: 1.0 flags the leaf brightness boost; (0.05, 0.95) carries
    // the biome climate for grass-top tinting; 0 means untinted
    float tintFlag = (type == LEAF) ? 1.f : 0.f;
    if (type == GRASS && dir == YPOS) {
        int col = static_cast<int>(pos.x) + 16 * static_cast<int>(pos.z);
        tintFlag = 0.1f + 0.8f * (m_climate[col] / 255.f);
    }
    float alphaMark = (type == WATER) ? 0.5f : 1.f;
    glm::vec4 baseUV;
    bool isAnimatable = (type == WATER || type == LAVA);
    // vs_UV.w: 1 water (waves), 2 lava (sunken + emissive), 3 emissive
    // block (torches, lit lamps, powered redstone glow full-bright)
    bool emissive = (type == TORCH || type == LAMP_ON || type == WIRE_ON ||
                     type == LEVER_ON);
    float fluidFlag = (type == WATER) ? 1.0f : (type == LAVA) ? 2.0f
                                             : (emissive ? 3.0f : 0.0f);

    // Set UV coordinates based on block type and face direction.
    // Cells are (column, row) / 16 with a bottom-left origin, because the
    // atlas is uploaded vertically mirrored.
    switch(type) {
    case GRASS:
        if (dir == YPOS) {
            // the grey grass tile, tinted per-biome in the fragment shader
            baseUV = glm::vec4(0.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        } else {
            baseUV = glm::vec4(3.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        }
        break;
    case PLANK:
        baseUV = glm::vec4(4.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        break;
    case DIRT:
        baseUV = glm::vec4(2.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        break;
    case STONE:
        baseUV = glm::vec4(1.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        break;
    case WATER:
        baseUV = glm::vec4(13.0f/16.0f, 3.0f/16.0f, 0.0f, 0.0f);
        break;
    case SNOW:
        baseUV = glm::vec4(2.0f/16.0f, 11.0f/16.0f, 0.0f, 0.0f);
        break;
    case LAVA:
        baseUV = glm::vec4(13.0f/16.0f, 1.0f/16.0f, 0.0f, 0.0f);
        break;
    case BEDROCK:
        baseUV = glm::vec4(1.0f/16.0f, 14.0f/16.0f, 0.0f, 0.0f);
        break;
    case SAND:
        baseUV = glm::vec4(2.0f/16.0f, 14.0f/16.0f, 0.0f, 0.0f);
        break;
    case WOOD:
        if (dir == YPOS || dir == YNEG) {
            baseUV = glm::vec4(5.0f/16.0f, 14.0f/16.0f, 0.0f, 0.0f);
        } else {
            baseUV = glm::vec4(4.0f/16.0f, 14.0f/16.0f, 0.0f, 0.0f);
        }
        break;
    case LEAF:
        baseUV = glm::vec4(5.0f/16.0f, 12.0f/16.0f, 0.0f, 0.0f);
        break;
    case COAL_ORE:
        baseUV = glm::vec4(2.0f/16.0f, 13.0f/16.0f, 0.0f, 0.0f);
        break;
    case IRON_ORE:
        baseUV = glm::vec4(1.0f/16.0f, 13.0f/16.0f, 0.0f, 0.0f);
        break;
    case GOLD_ORE:
        baseUV = glm::vec4(0.0f/16.0f, 13.0f/16.0f, 0.0f, 0.0f);
        break;
    case DIAMOND_ORE:
        baseUV = glm::vec4(2.0f/16.0f, 12.0f/16.0f, 0.0f, 0.0f);
        break;
    case BRICK:
        baseUV = glm::vec4(7.0f/16.0f, 15.0f/16.0f, 0.0f, 0.0f);
        break;
    case WIRE_OFF:
    case WIRE_ON:
        baseUV = glm::vec4(3.0f/16.0f, 12.0f/16.0f, 0.0f, 0.0f); // redstone ore
        break;
    case TORCH:
        baseUV = glm::vec4(9.0f/16.0f, 9.0f/16.0f, 0.0f, 0.0f);  // glowstone
        break;
    case LEVER_OFF:
    case LEVER_ON:
        baseUV = glm::vec4(0.0f/16.0f, 14.0f/16.0f, 0.0f, 0.0f); // cobblestone
        break;
    case LAMP_OFF:
        baseUV = glm::vec4(5.0f/16.0f, 13.0f/16.0f, 0.0f, 0.0f); // obsidian
        break;
    case LAMP_ON:
        baseUV = glm::vec4(9.0f/16.0f, 9.0f/16.0f, 0.0f, 0.0f);  // glowstone
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

    // Standard UVs; z flags UV animation, w identifies the fluid type
    float animFlag = isAnimatable ? 1.0f : 0.0f;
    uvs[0] = glm::vec4(0.0f, 0.0f, animFlag, fluidFlag) + baseUV;
    uvs[1] = glm::vec4(1.0f/16.0f, 0.0f, animFlag, fluidFlag) + baseUV;
    uvs[2] = glm::vec4(1.0f/16.0f, 1.0f/16.0f, animFlag, fluidFlag) + baseUV;
    uvs[3] = glm::vec4(0.0f, 1.0f/16.0f, animFlag, fluidFlag) + baseUV;

    // Smooth lighting + ambient occlusion: each corner averages the light
    // of the four cells that touch it on the face's air side; the two edge
    // cells and the diagonal also darken the corner when solid
    glm::ivec3 n(static_cast<int>(normal.x), static_cast<int>(normal.y),
                 static_cast<int>(normal.z));
    int axisN = (n.x != 0) ? 0 : (n.y != 0 ? 1 : 2);
    glm::ivec3 airCell(static_cast<int>(pos.x) + 16 + n.x,
                       static_cast<int>(pos.y) + n.y,
                       static_cast<int>(pos.z) + 16 + n.z);

    for (int i = 0; i < 4; i++) {
        glm::vec3 rel = glm::vec3(positions[i]) - (pos + glm::vec3(0.5f))
                        - 0.5f * glm::vec3(n);
        glm::ivec3 o1(0), o2(0);
        bool first = true;
        for (int a = 0; a < 3; ++a) {
            if (a == axisN) continue;
            int s = rel[a] > 0.f ? 1 : -1;
            if (first) { o1[a] = s; first = false; }
            else       { o2[a] = s; }
        }

        float skySum = 0.f, blkSum = 0.f;
        int transparentCount = 0;
        bool solid1 = false, solid2 = false, solidC = false;
        const glm::ivec3 samples[4] = {airCell, airCell + o1, airCell + o2,
                                       airCell + o1 + o2};
        for (int s = 0; s < 4; ++s) {
            glm::ivec3 c = samples[s];
            c.y = std::clamp(c.y, 0, LH - 1);
            int id = lidx(c.x, c.y, c.z);
            BlockType t = static_cast<BlockType>(cells[id]);
            if (solidForAO(t)) {
                if (s == 1) solid1 = true;
                else if (s == 2) solid2 = true;
                else if (s == 3) solidC = true;
            } else {
                skySum += skyLight[id];
                blkSum += blockLight[id];
                transparentCount++;
            }
        }

        float ao = (solid1 && solid2)
                       ? 0.55f
                       : 1.0f - 0.15f * ((solid1 ? 1 : 0) + (solid2 ? 1 : 0) +
                                         (solidC ? 1 : 0));
        float inv = transparentCount > 0 ? 1.f / (15.f * transparentCount) : 0.f;
        glm::vec4 color(skySum * inv * ao, blkSum * inv * ao, tintFlag, alphaMark);

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
    } else {
        indexCounts[BufferType::OPAQUE_INDEX] = 0;
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
    } else {
        indexCounts[BufferType::TRANSPARENT_INDEX] = 0;
    }
}

void Chunk::createVBOdata() {
    // Clear existing VBO data
    vboData.opaqueVBO.clear();
    vboData.opaqueIndex.clear();
    vboData.transparentVBO.clear();
    vboData.transparentIndex.clear();

    // Bake the lighting for this chunk and its border: sunlight flood-fills
    // down and into caves, lava glows, and faces sample it per vertex
    // Per-thread scratch: the three 48x48x256 light grids are ~1.7MB, and
    // re-allocating them for every mesh made heavy remesh backlogs crawl
    static thread_local std::vector<unsigned char> cells, skyLight, blockLight;
    buildLightGrid(cells, skyLight, blockLight);

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

                glm::vec3 pos(x, y, z);
                bool isBlockTransparent = (type == WATER);
                std::vector<glm::vec4> &vbo = isBlockTransparent ? vboData.transparentVBO : vboData.opaqueVBO;
                std::vector<GLuint> &idx = isBlockTransparent ? vboData.transparentIndex : vboData.opaqueIndex;

                // Faces exist only on the boundary between a filled block
                // and air, plus opaque faces against water (terrain under
                // water still renders) and against lava (its surface sits
                // slightly below its block top, so the walls must be drawn).
                // Water itself never faces solids, matching Minecraft.
                auto shouldCreateFace = [this, isBlockTransparent, type](int x, int y, int z) -> bool {
                    BlockType adjacentType = this->getAdjacentChunkBlockAt(x, y, z);
                    if (adjacentType == EMPTY) return true;
                    if (isBlockTransparent) return false;
                    if (adjacentType == WATER) return true;
                    if (adjacentType == LAVA && type != LAVA) return true;
                    return false;
                };

                if(shouldCreateFace(x + 1, y, z)) createFace(vbo, idx, pos, XPOS, type, cells.data(), skyLight.data(), blockLight.data());
                if(shouldCreateFace(x - 1, y, z)) createFace(vbo, idx, pos, XNEG, type, cells.data(), skyLight.data(), blockLight.data());
                if(shouldCreateFace(x, y + 1, z)) createFace(vbo, idx, pos, YPOS, type, cells.data(), skyLight.data(), blockLight.data());
                if(shouldCreateFace(x, y - 1, z)) createFace(vbo, idx, pos, YNEG, type, cells.data(), skyLight.data(), blockLight.data());
                if(shouldCreateFace(x, y, z + 1)) createFace(vbo, idx, pos, ZPOS, type, cells.data(), skyLight.data(), blockLight.data());
                if(shouldCreateFace(x, y, z - 1)) createFace(vbo, idx, pos, ZNEG, type, cells.data(), skyLight.data(), blockLight.data());
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
