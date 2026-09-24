#pragma once

#include "openglcontext.h"
#include "shaderprogram.h"
#include "skyshaderprogram.h"
#include "weathershaderprogram.h"
#include "scene/worldaxes.h"
#include "scene/camera.h"
#include "scene/terrain.h"
#include "scene/player.h"
#include "scene/framebuffer.h"
#include "scene/quad.h"
#include "scene/blockhighlight.h"
#include "scene/hud.h"
#include "scene/sheepcube.h"
#include "skyrenderer.h"
#include "weathersystem.h"

#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <smartpointerhelp.h>
#include <array>
#include <memory>
#include <unordered_map>

class MyGL : public OpenGLContext
{
    Q_OBJECT
private:
    WorldAxes m_worldAxes; // A wireframe representation of the world axes. It is hard-coded to sit centered at (32, 128, 32).
    ShaderProgram m_progLambert;// A shader program that uses lambertian reflection
    ShaderProgram m_progFlat;// A shader program that uses "flat" reflection (no shadowing at all)
    ShaderProgram m_progPost;// Post-process shader for the underwater / lava screen overlays
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

    // Post-process pipeline: the 3D scene renders into this frame buffer,
    // which is then drawn to the screen through m_progPost on m_quad.
    FrameBuffer m_frameBuffer;
    Quad m_quad;
    // Shadow mapping: the sun renders scene depth into this buffer each
    // frame, and the lambert shader tests fragments against it
    ShaderProgram m_progShadow;
    DepthFrameBuffer m_shadowMap;
    // Third-person mode (V): camera pulled back behind an animated player
    // model
    bool m_thirdPerson;
    // Whether the HUD (hotbar + crosshair) is drawn. Always true in normal
    // play; the screenshot tour toggles it per shot for clean vistas.
    bool m_hudVisible = true;
    float m_tpDistance;   // smoothed camera pull-back distance
    float m_solidFade;    // smoothed inside-a-block blackout factor
    float m_lastDt = 0.016f;
    float m_playerSpeed;
    float m_armSwing;
    glm::vec3 m_prevPlayerPos;
    void renderPlayerModel();
    // View state for the current frame (first- or third-person)
    glm::mat4 m_activeViewProj;
    glm::vec3 m_activeCamEye;
    // Wireframe outline around the block the crosshair targets
    BlockHighlight m_blockHighlight;

    // Inventory hotbar (Milestone 3 GUI): keys 1-8 pick the block that
    // right-click places, Tab flips between the building and redstone
    // pages, and per-slot counts render as seven-segment digits.
    // Draw distance (half-width of the chunk box) sits just past the fog
    // end so terrain fades out before the world edge shows
    static constexpr int DRAW_RADIUS = 176;
    static constexpr float FOG_END = 174.f;

    static constexpr int HOTBAR_SIZE = 8;
    static constexpr int HOTBAR_PAGES = 2;
    std::array<std::array<BlockType, HOTBAR_SIZE>, HOTBAR_PAGES> m_hotbarPages;
    std::array<BlockType, HOTBAR_SIZE> m_hotbar; // the active page
    int m_hotbarPage;
    int m_selectedSlot;
    bool m_hudDirty;
    HudFrame m_hudFrame;
    HudIcons m_hudIcons;
    ShaderProgram m_progHud;
    void renderHud();

    // Survival-lite inventory: breaking collects, placing consumes (water
    // is infinite). ON-state redstone blocks normalize to their OFF item.
    std::unordered_map<int, int> m_inventory;
    int invCount(BlockType t) const;
    void invAdd(BlockType t, int n);
    bool invTake(BlockType t, int n);

    // Crafting menu (B): recipes turn mined resources into buildables
    bool m_craftOpen;
    int m_craftSel;
    bool m_craftDirty;
    CraftFrame m_craftFrame;
    CraftIcons m_craftIcons;
    void craftSelected();

    // Height-map and OBJ importers (Milestone 3). The dialog versions ask
    // for a file; the cores are separated so the self-test can drive them.
    void importHeightmapDialog();
    void voxelizeObjDialog();
    void applyHeightmap(const QImage &img);
    bool voxelizeObjFile(const QString &path);

    // Minecraft-style mouse capture: clicking the window grabs the cursor
    // for camera look; Escape releases it (and quits when already free).
    bool m_mouseCaptured;
    void setMouseCaptured(bool captured);
    // Debug world-axes overlay, hidden unless toggled with O
    bool m_showAxes;
    // MC_PILOT self-test: scripted flight/walk/build/dive sequence that
    // saves frame grabs, used to verify gameplay without touching the
    // real mouse or keyboard
    int m_pilotStage;
    void runPilot();
    // MC_DEMO: a continuous, cinematic scripted playthrough used to record
    // the feature-demo video - smooth eased camera, no teleports
    void runDemo(float dT);
    // MC_RECORD: saves every rendered frame as a JPEG on worker threads
    void recordFrame();
    // MC_SHOTS=<dir>: while the demo plays, grab a full-resolution PNG at a
    // handful of curated moments (with HUD on or off per shot) for portfolio
    // screenshots, then quit once the last one is captured.
    void captureShots();
    glm::ivec3 m_demoCircuit{0};

    QTimer m_timer; // Timer linked to tick(). Fires approximately 60 times per second.

    qint64 m_prevTime;

    void moveMouseToCenter(); // Forces the mouse position to the screen's center. You should call this
        // from within a mouse move event after reading the mouse movement so that
        // your mouse stays within the screen bounds and is always read.

    void sendPlayerDataToGUI() const;

    QOpenGLTexture* texture;
    float m_currentTime;
    SheepCube m_sheepCube;

    // Extracts the six view-frustum planes from a view-projection matrix
    // (Gribb-Hartmann). Used to cull chunks outside the camera's view.
    static std::array<glm::vec4, 6> computeFrustumPlanes(const glm::mat4 &viewproj);

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

    // Called from paintGL(). Draws the opaque and transparent terrain passes.
    void renderTerrain();

    // Render the sky
    void renderSky();

    // Render the sheep NPCs
    void renderSheep();

    // Render weather effects
    void renderWeather();

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
    // Releases the mouse and clears held keys when the window loses focus
    void focusOutEvent(QFocusEvent *e) override;


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
