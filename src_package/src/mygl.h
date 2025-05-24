#pragma once

#include "openglcontext.h"
#include "shaderprogram.h"
#include "skyshaderprogram.h"
#include "weathershaderprogram.h"
#include "scene/worldaxes.h"
#include "scene/camera.h"
#include "scene/terrain.h"
#include "scene/player.h"
#include "skyrenderer.h"
#include "weathersystem.h"

#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <smartpointerhelp.h>
#include <QOpenGLTexture>
#include <memory>
#include "scene/sheepcube.h"

class MyGL : public OpenGLContext
{
    Q_OBJECT
private:
    WorldAxes m_worldAxes; // A wireframe representation of the world axes. It is hard-coded to sit centered at (32, 128, 32).
    ShaderProgram m_progLambert;// A shader program that uses lambertian reflection
    ShaderProgram m_progFlat;// A shader program that uses "flat" reflection (no shadowing at all)
    ShaderProgram m_progInstanced;// A shader program that is designed to be compatible with instanced rendering
    SkyShaderProgram m_progSky;// A shader program for rendering the sky using ray marching
    WeatherShaderProgram m_progWeather;// A shader program for rendering weather particles

    GLuint vao; // A handle for our vertex array object. This will store the VBOs created in our geometry classes.
        // Don't worry too much about this. Just know it is necessary in order to render geometry.

    Terrain m_terrain; // All of the Chunks that currently comprise the world.
    Player m_player; // The entity controlled by the user. Contains a camera to display what it sees as well.
    InputBundle m_inputs; // A collection of variables to be updated in keyPressEvent, mouseMoveEvent, mousePressEvent, etc.

    // Sky renderer component
    SkyRenderer m_sky;

    // Weather system component
    WeatherSystem m_weather;


    QTimer m_timer; // Timer linked to tick(). Fires approximately 60 times per second.

    qint64 m_prevTime;

    void moveMouseToCenter(); // Forces the mouse position to the screen's center. You should call this
        // from within a mouse move event after reading the mouse movement so that
        // your mouse stays within the screen bounds and is always read.

    void sendPlayerDataToGUI() const;

    QOpenGLTexture* texture;
    float m_currentTime;
    std::unique_ptr<Chunk> m_testChunk;
    SheepCube m_sheepCube;

public:
    explicit MyGL(QWidget *parent = nullptr);
    ~MyGL();

    // Called once when MyGL is initialized.
    // Once this is called, all OpenGL function
    // invocations are valid (before this, they
    // will cause segfaults)
    void initializeGL() override;
    // Called whenever MyGL is resized.
    void resizeGL(int w, int h) override;
    // Called whenever MyGL::update() is called.
    // In the base code, update() is called from tick().
    void paintGL() override;

    // Called from paintGL().
    // Calls Terrain::draw().
    void renderTerrain();

    // Render the sky
    void renderSky();

    // Render weather effects
    void renderWeather();

    glm::vec2 getBaseUVForBlock(BlockType block);

protected:
    // Automatically invoked when the user
    // presses a key on the keyboard
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    // Automatically invoked when the user
    // moves the mouse
    void mouseMoveEvent(QMouseEvent *e) override;
    // Automatically invoked when the user
    // presses a mouse button
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;


private slots:
    void tick(); // Slot that gets called ~60 times per second by m_timer firing.

signals:
    void sig_sendPlayerPos(QString) const;
    void sig_sendPlayerVel(QString) const;
    void sig_sendPlayerAcc(QString) const;
    void sig_sendPlayerLook(QString) const;
    void sig_sendPlayerChunk(QString) const;
    void sig_sendPlayerTerrainZone(QString) const;
};
