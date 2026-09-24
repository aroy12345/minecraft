#include "blockhighlight.h"

BlockHighlight::BlockHighlight(OpenGLContext* context) : Drawable(context) {}

GLenum BlockHighlight::drawMode() {
    return GL_LINES;
}

void BlockHighlight::createVBOdata() {
    // Slightly inflated so the lines sit just outside the block's faces
    // instead of z-fighting with them
    const float lo = -0.004f, hi = 1.004f;
    std::vector<glm::vec4> pos {
        {lo, lo, lo, 1.f}, {hi, lo, lo, 1.f}, {hi, lo, hi, 1.f}, {lo, lo, hi, 1.f},
        {lo, hi, lo, 1.f}, {hi, hi, lo, 1.f}, {hi, hi, hi, 1.f}, {lo, hi, hi, 1.f}
    };
    std::vector<glm::vec4> col(8, glm::vec4(0.05f, 0.05f, 0.05f, 1.f));
    // 12 edges
    std::vector<GLuint> idx {
        0,1, 1,2, 2,3, 3,0,   // bottom
        4,5, 5,6, 6,7, 7,4,   // top
        0,4, 1,5, 2,6, 3,7    // verticals
    };

    indexCounts[INDEX] = idx.size();

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
