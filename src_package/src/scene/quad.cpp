#include "quad.h"

Quad::Quad(OpenGLContext* context) : Drawable(context) {}

void Quad::createVBOdata() {
    // Positions are already in clip space; the post vertex shader passes
    // them through untransformed.
    std::vector<glm::vec4> pos {
        glm::vec4(-1.f, -1.f, 0.99f, 1.f),
        glm::vec4( 1.f, -1.f, 0.99f, 1.f),
        glm::vec4( 1.f,  1.f, 0.99f, 1.f),
        glm::vec4(-1.f,  1.f, 0.99f, 1.f)
    };

    std::vector<glm::vec4> uv {
        glm::vec4(0.f, 0.f, 0.f, 0.f),
        glm::vec4(1.f, 0.f, 0.f, 0.f),
        glm::vec4(1.f, 1.f, 0.f, 0.f),
        glm::vec4(0.f, 1.f, 0.f, 0.f)
    };

    std::vector<GLuint> idx {0, 1, 2, 0, 2, 3};

    indexCounts[INDEX] = idx.size();

    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLuint), idx.data(), GL_STATIC_DRAW);

    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER, pos.size() * sizeof(glm::vec4), pos.data(), GL_STATIC_DRAW);

    generateBuffer(UV);
    bindBuffer(UV);
    mp_context->glBufferData(GL_ARRAY_BUFFER, uv.size() * sizeof(glm::vec4), uv.data(), GL_STATIC_DRAW);
}
