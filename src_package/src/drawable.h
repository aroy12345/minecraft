#pragma once
#include <openglcontext.h>
#include <glm_includes.h>
#include <unordered_map>


enum BufferType : unsigned char {
    INDEX,
    POSITION, NORMAL, COLOR, UV,
    OPAQUE_INTERLEAVED, TRANSPARENT_INTERLEAVED,
    OPAQUE_INDEX, TRANSPARENT_INDEX
};



//This defines a class which can be rendered by our shader program.
//Make any geometry a subclass of ShaderProgram::Drawable in order to render it with the ShaderProgram class.
class Drawable
{
protected:
    std::unordered_map<BufferType, GLuint> bufHandles;
    std::unordered_map<BufferType, bool> bufGenerated;
    // The length of the index buffer indicated by the key.
    // Unless you have more than one index buffer, this map
    // will have just one key-value pair.
    std::unordered_map<BufferType, int> indexCounts;

    OpenGLContext* mp_context; // Since Qt's OpenGL support is done through classes like QOpenGLFunctions_3_2_Core,
                               // we need to pass our OpenGL context to the Drawable in order to call GL functions
                               // from within this class.

public:
    Drawable(OpenGLContext* context);
    virtual ~Drawable();

    virtual void createVBOdata() = 0; // To be implemented by subclasses. Populates the VBOs of the Drawable.
    virtual void destroyVBOdata(); // Frees the VBOs of the Drawable.

    // Getter functions for various GL data
    virtual GLenum drawMode();
    int elemCount(BufferType);

    // Call these functions when you want to call glGenBuffers on the buffers stored in the Drawable
    // These will properly set the values of idxBound etc. which need to be checked in ShaderProgram::draw()
    void generateBuffer(BufferType buf);

    bool bindBuffer(BufferType buf);
};
