#include "sheepcube.h"

SheepCube::SheepCube(OpenGLContext* context)
    : Drawable(context)
{}

void SheepCube::createVBOdata() {
    std::vector<glm::vec4> pos {
        glm::vec4(0.5f, 0.5f, 0.5f, 1.f),
        glm::vec4(0.5f, -0.5f, 0.5f, 1.f),
        glm::vec4(-0.5f, -0.5f, 0.5f, 1.f),
        glm::vec4(-0.5f, 0.5f, 0.5f, 1.f),
        glm::vec4(0.5f, 0.5f, -0.5f, 1.f),
        glm::vec4(0.5f, -0.5f, -0.5f, 1.f),
        glm::vec4(-0.5f, -0.5f, -0.5f, 1.f),
        glm::vec4(-0.5f, 0.5f, -0.5f, 1.f)
    };

    std::vector<glm::vec4> col (8, glm::vec4(1.f, 1.f, 1.f, 1.f));  // White color for all vertices

    std::vector<GLuint> idx {
        0,1,2,0,2,3,
        4,5,1,4,1,0,
        7,6,5,7,5,4,
        3,2,6,3,6,7,
        0,3,7,0,7,4,
        1,5,6,1,6,2
    };

    indexCounts[INDEX] = idx.size(); // <-- THIS IS CORRECT!

    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);

    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(glm::vec4), pos.data(), GL_STATIC_DRAW);

    generateBuffer(COLOR);
    bindBuffer(COLOR);
    mp_context->glBufferData(GL_ARRAY_BUFFER, col.size() * sizeof(glm::vec4), col.data(), GL_STATIC_DRAW);

}


GLenum SheepCube::drawMode() {
    return GL_TRIANGLES;
}
