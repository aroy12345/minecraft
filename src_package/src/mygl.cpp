#include "mygl.h"
#include <glm_includes.h>

#include <QApplication>
#include <QKeyEvent>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>

#ifdef Q_OS_MACOS
// Raw hardware mouse deltas for smooth FPS-style camera look
#include <ApplicationServices/ApplicationServices.h>
#endif

namespace {
// Half-width of the box of chunks drawn around the player. Kept just above
// the fog end distance so terrain fades out before the world edge shows.
constexpr int DRAW_RADIUS = 176;
// The sun's shadow map only covers a ~130-block box around the player, so
// the shadow pass draws a smaller radius than the far view distance.
constexpr int SHADOW_RADIUS = 128;
constexpr float FOG_END = 174.f;

}

MyGL::MyGL(QWidget *parent)
    : OpenGLContext(parent),
    m_worldAxes(this),
    m_progLambert(this), m_progFlat(this), m_progPost(this), m_progSky(this),
    m_progWeather(this),
    // Spawn just above the grass at a meadow's edge overlooking the beach
    // and open ocean, so the view the player actually settles into is a
    // scenic coastline (the look direction is set in initializeGL)
    m_terrain(this), m_player(glm::vec3(112.f, 147.f, 366.f), m_terrain),
    m_sky(this),
    m_weather(this),
    m_frameBuffer(this, 1, 1, 1),
    m_quad(this),
    m_progShadow(this),
    m_shadowMap(this, 2048),
    m_thirdPerson(false),
    m_tpDistance(3.f),
    m_solidFade(0.f),
    m_playerSpeed(0.f),
    m_armSwing(0.f),
    m_prevPlayerPos(0.f),
    m_blockHighlight(this),
    m_hotbarPages{{{GRASS, DIRT, STONE, SAND, WOOD, PLANK, BRICK, WATER},
                   {WIRE_OFF, TORCH, LEVER_OFF, LAMP_OFF, LEAF, SNOW, STONE, WATER}}},
    m_hotbar(m_hotbarPages[0]),
    m_hotbarPage(0),
    m_selectedSlot(2),
    m_hudDirty(true),
    m_hudFrame(this),
    m_hudIcons(this),
    m_progHud(this),
    m_craftOpen(false),
    m_craftSel(0),
    m_craftDirty(true),
    m_craftFrame(this),
    m_craftIcons(this),
    m_mouseCaptured(false),
    m_showAxes(false),
    m_pilotStage(0),
    m_prevTime(QDateTime::currentMSecsSinceEpoch()),
    texture(nullptr),
    m_currentTime(0.f),
    m_sheepCube(this)
{
    // Connect the timer to a function so that when the timer ticks the function is executed
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(tick()));
    // Tell the timer to redraw 60 times per second
    m_timer.start(16);
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true); // MyGL will track the mouse's movements even if a mouse button is not pressed
    setCursor(Qt::ArrowCursor);

    // Start with a gentle downward tilt so terrain, not sky, greets the player
    m_player.rotateOnRightLocal(-12.f);

    // Start on foot like Minecraft survival - F lifts off into flight.
    // (The physics tick below holds the player until the ground exists.)
    m_player.setFlightMode(false);

    // Starting supplies: basics to build with; everything else is mined
    // or crafted (see Crafting::RECIPES)
    for (BlockType t : {GRASS, DIRT, STONE, SAND}) m_inventory[t] = 32;
    m_inventory[WOOD] = 16;
    m_inventory[LEAF] = 16;
    m_inventory[SNOW] = 16;

    // MC_TP=1 starts in third-person view (demo/verification aid)
    if (qEnvironmentVariableIsSet("MC_TP")) m_thirdPerson = true;

    // MC_SPAWN="x,y,z[,pitch[,yaw]]" overrides the spawn point - handy for
    // headless verification and for framing demo-video shots
    if (qEnvironmentVariableIsSet("MC_SPAWN")) {
        const QStringList p = qEnvironmentVariable("MC_SPAWN").split(',');
        // A trailing "g" scouts a grounded landing (the player falls to the
        // surface); otherwise the requested flight pose is held.
        if (!(p.size() >= 6 && p[5] == "g")) m_player.setFlightMode(true);
        if (p.size() >= 3) {
            glm::vec3 target(p[0].toFloat(), p[1].toFloat(), p[2].toFloat());
            m_player.moveAlongVector(target - m_player.mcr_position);
        }
        if (p.size() >= 4) m_player.rotateOnRightLocal(p[3].toFloat());
        if (p.size() >= 5) m_player.rotateOnUpGlobal(p[4].toFloat());
    } else if (!qEnvironmentVariableIsSet("MC_DEMO") &&
               !qEnvironmentVariableIsSet("MC_PILOT")) {
        // Look out over the meadow toward the beach and open ocean for a
        // scenic opening frame: grass foreground, sand, then sea fading to a
        // foggy horizon. The scripted demo and self-test set their own
        // poses, so this only affects normal play.
        m_player.rotateOnRightLocal(-4.f);
        m_player.rotateOnUpGlobal(270.f);
    }
}

MyGL::~MyGL() {
    makeCurrent();
    glDeleteVertexArrays(1, &vao);
    m_frameBuffer.destroy();
    delete texture;
}

void MyGL::moveMouseToCenter() {
    QCursor::setPos(this->mapToGlobal(QPoint(width() / 2, height() / 2)));
}

// Minecraft-style cursor grab. On macOS the cursor is decoupled from the
// physical mouse entirely (CGAssociateMouseAndMouseCursorPosition) and the
// camera is driven from raw hardware deltas each tick - no cursor warping,
// no jitter. Elsewhere we fall back to hide-and-recenter.
void MyGL::setMouseCaptured(bool captured) {
    m_mouseCaptured = captured;
    if (captured) {
        setCursor(Qt::BlankCursor);
#ifdef Q_OS_MACOS
        CGAssociateMouseAndMouseCursorPosition(false);
        int32_t dx, dy;
        CGGetLastMouseDelta(&dx, &dy); // discard any pending delta
#else
        moveMouseToCenter();
#endif
    } else {
#ifdef Q_OS_MACOS
        CGAssociateMouseAndMouseCursorPosition(true);
#endif
        setCursor(Qt::ArrowCursor);
    }
}

void MyGL::focusOutEvent(QFocusEvent *) {
    // Don't fight the user for the cursor when they switch apps, and drop
    // any held movement keys so the player doesn't keep walking. The
    // scripted self-test owns the inputs, so its runs stay undisturbed.
    setMouseCaptured(false);
    if (!qEnvironmentVariableIsSet("MC_PILOT")) {
        m_inputs = InputBundle();
    }
}

void MyGL::initializeGL()
{
    // Create an OpenGL context using Qt's QOpenGLFunctions_3_2_Core class
    // If you were programming in a non-Qt context you might use GLEW (GL Extension Wrangler)instead
    initializeOpenGLFunctions();
    // Print out some information about the current OpenGL context
    debugContextVersion();

    // Set a few settings/modes in OpenGL rendering
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);

    // Enable alpha blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Create a Vertex Attribute Object
    glGenVertexArrays(1, &vao);
    // Create the instance of the world axes
    m_worldAxes.createVBOdata();
    m_sheepCube.createVBOdata();
    m_blockHighlight.createVBOdata();
    // Full-screen quad for the post-process pass
    m_quad.createVBOdata();

    // Create and set up the diffuse shader
    m_progLambert.create(":/glsl/lambert.vert.glsl", ":/glsl/lambert.frag.glsl");
    // Create and set up the flat lighting shader
    m_progFlat.create(":/glsl/flat.vert.glsl", ":/glsl/flat.frag.glsl");
    // Create and set up the post-process shader
    m_progPost.create(":/glsl/post.vert.glsl", ":/glsl/post.frag.glsl");
    // Depth-only shader + buffer for shadow mapping
    m_progShadow.create(":/glsl/shadow.vert.glsl", ":/glsl/shadow.frag.glsl");
    m_shadowMap.create();
    // Create and set up the sky shader
    m_progSky.create(":/glsl/sky.vert.glsl", ":/glsl/sky.frag.glsl");
    // Create and set up the weather shaders
    m_progWeather.create(":/glsl/weather.vert.glsl", ":/glsl/weather.frag.glsl");
    // HUD shader for the hotbar icons
    m_progHud.create(":/glsl/hud.vert.glsl", ":/glsl/hud.frag.glsl");

    // We have to have a VAO bound in OpenGL 3.2 Core. But if we're not
    // using multiple VAOs, we can just bind one once.
    glBindVertexArray(vao);

    // Load the block texture atlas
    QImage textureImage(":/textures/minecraft_textures_all.png");
    if (textureImage.isNull()) {
        qFatal("Failed to load the block texture atlas");
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    texture = new QOpenGLTexture(textureImage.flipped(Qt::Vertical));
#else
    texture = new QOpenGLTexture(textureImage.mirrored());
#endif
    texture->setMinificationFilter(QOpenGLTexture::Nearest);
    texture->setMagnificationFilter(QOpenGLTexture::Nearest);
    texture->setWrapMode(QOpenGLTexture::Repeat);

    // Offscreen frame buffer for the post-process pipeline
    m_frameBuffer.resize(width(), height(), devicePixelRatio());
    m_frameBuffer.create();

    // Create sky VBO data
    m_sky.createVBOdata();

    // Initialize weather system
    m_weather.initialize();
    m_sky.setWeatherSystem(&m_weather);

    // MC_TIME="hours" pins the starting time of day (demo/verification aid)
    if (qEnvironmentVariableIsSet("MC_TIME")) {
        m_sky.setTimeOfDay(qEnvironmentVariable("MC_TIME").toFloat());
    }

    // Build the immediate spawn neighborhood synchronously so the very
    // first frame already shows solid ground, not empty sky, then kick off
    // async streaming for everything beyond it.
    m_terrain.primeSpawnArea(m_player.mcr_position);
    m_terrain.updateTerrain(m_player.mcr_position);
}

void MyGL::resizeGL(int w, int h) {
    //This code sets the concatenated view and perspective projection matrices used for
    //our scene's camera view.
    m_player.setCameraWidthHeight(static_cast<unsigned int>(w), static_cast<unsigned int>(h));
    glm::mat4 viewproj = m_player.mcr_camera.getViewProj();
    // Upload the view-projection matrix to our shaders (i.e. onto the graphics card)
    m_progLambert.setUnifMat4("u_ViewProj", viewproj);
    m_progFlat.setUnifMat4("u_ViewProj", viewproj);

    // Set the inverse view-projection matrix for the sky shader
    m_progSky.setInvViewProj(glm::inverse(viewproj));

    m_hudDirty = true; // hotbar layout depends on the aspect ratio

    // The offscreen buffer must track the widget's size
    m_frameBuffer.destroy();
    m_frameBuffer.resize(w, h, devicePixelRatio());
    m_frameBuffer.create();

    printGLErrorLog();
}

void MyGL::tick() {
    // Terrain uploads and painting need a live GL context
    if (!isValid()) {
        return;
    }
    makeCurrent();

    // Get current time for delta time calculation
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    float dT = (currentTime - m_prevTime) / 1000.f;
    m_prevTime = currentTime;
    // MC_FIXEDSTEP: offline-render mode for the demo recorder. Every tick
    // advances the simulation exactly 1/30s no matter how long the frame
    // took, so the captured sequence plays back as flawless 30fps video
    // even though the game runs slower than wall-clock while encoding.
    static const bool fixedStep = qEnvironmentVariableIsSet("MC_FIXEDSTEP");
    if (fixedStep) dT = 1.f / 30.f;

    // Update time for animation
    m_currentTime += dT;
    m_lastDt = dT;

    // Update sky time
    m_sky.updateTime(dT);

    // Update weather system
    m_weather.update(dT, m_player.mcr_position, &m_terrain);

    // Expand the world around the player and move generated chunks through
    // the multithreaded meshing pipeline
    m_terrain.updateTerrain(m_player.mcr_position);

    // Advance any in-progress fluid flow
    m_terrain.processFluids();

#ifdef Q_OS_MACOS
    // Camera look from raw mouse deltas while captured - smooth and
    // frame-rate independent, exactly like a native FPS
    if (m_mouseCaptured && isActiveWindow()) {
        int32_t mdx, mdy;
        CGGetLastMouseDelta(&mdx, &mdy);
        const float sensitivity = 0.10f;
        if (mdx != 0) m_player.rotateOnUpGlobal(-mdx * sensitivity);
        if (mdy != 0) m_player.rotateOnRightLocal(-mdy * sensitivity);
    }
#endif

    // MC_PILOT: scripted self-test drives the inputs instead of the user
    if (qEnvironmentVariableIsSet("MC_PILOT")) {
        runPilot();
    } else if (qEnvironmentVariableIsSet("MC_DEMO") ||
               qEnvironmentVariableIsSet("MC_SHOTS")) {
        runDemo(dT);
    }
    // MC_RECORD=<dir>: dump every rendered frame as a JPEG for the demo
    // video. Encoding runs on worker threads; frames drop rather than
    // stall the game when the writers fall behind.
    recordFrame();
    // MC_SHOTS=<dir>: grab full-resolution stills at curated demo moments.
    captureShots();

    // Pass dT to player's tick function - held until the spawn chunk has
    // block data so the player lands on ground instead of falling through
    // a world that hasn't streamed in yet
    int ppx = static_cast<int>(glm::floor(m_player.mcr_position.x));
    int ppz = static_cast<int>(glm::floor(m_player.mcr_position.z));
    if (m_terrain.hasChunkAt(ppx, ppz) &&
        m_terrain.getChunkAt(ppx, ppz)->m_blocksFilled.load()) {
        m_player.tick(dT, m_inputs);
    }

    // Wander the sheep
    for (auto &sheep : m_terrain.m_sheep) {
        sheep->tick(dT, m_inputs);
    }

    // Third-person animation state: horizontal speed and arm-swing decay
    glm::vec2 dxz(m_player.mcr_position.x - m_prevPlayerPos.x,
                  m_player.mcr_position.z - m_prevPlayerPos.z);
    m_playerSpeed = dT > 0.f ? glm::length(dxz) / dT : 0.f;
    m_prevPlayerPos = m_player.mcr_position;
    m_armSwing = glm::max(0.f, m_armSwing - dT);

    update(); // Calls paintGL() as part of a larger QOpenGLWidget pipeline
    sendPlayerDataToGUI(); // Updates the info in the secondary window displaying player data

    // Average frame time, logged every 5 seconds while MC_AUTOSHOT is set
    if (qEnvironmentVariableIsSet("MC_AUTOSHOT")) {
        static float fpsAccum = 0.f;
        static int fpsFrames = 0;
        fpsAccum += dT;
        fpsFrames++;
        if (fpsAccum > 5.f) {
            qDebug() << "avg frame ms:" << (fpsAccum / fpsFrames) * 1000.f;
            fpsAccum = 0.f;
            fpsFrames = 0;
        }
    }

    // With MC_AUTOSHOT set, save a frame grab every few seconds (used to
    // verify rendering headlessly; grabFramebuffer works even unfocused)
    if (qEnvironmentVariableIsSet("MC_AUTOSHOT")) {
        static float shotTimer = 0.f;
        static int shotIndex = 0;
        shotTimer += dT;
        if (shotTimer > 5.f) {
            shotTimer = 0.f;
            grabFramebuffer().save(QString("/tmp/mc_frame_%1.png").arg(shotIndex++ % 4));
        }
    }
}

int MyGL::invCount(BlockType t) const {
    if (t == WATER) return -1; // infinite
    auto it = m_inventory.find(Crafting::invItemFor(t));
    return it == m_inventory.end() ? 0 : it->second;
}

void MyGL::invAdd(BlockType t, int n) {
    if (t == WATER || t == LAVA) return;
    m_inventory[Crafting::invItemFor(t)] += n;
    m_hudDirty = m_craftDirty = true;
}

bool MyGL::invTake(BlockType t, int n) {
    if (t == WATER) return true;
    int &have = m_inventory[Crafting::invItemFor(t)];
    if (have < n) return false;
    have -= n;
    m_hudDirty = m_craftDirty = true;
    return true;
}

void MyGL::craftSelected() {
    const CraftRecipe &r = Crafting::RECIPES[m_craftSel];
    if (invCount(r.in1) < r.n1) return;
    if (r.in2 != EMPTY && invCount(r.in2) < r.n2) return;
    invTake(r.in1, r.n1);
    if (r.in2 != EMPTY) invTake(r.in2, r.n2);
    invAdd(r.out, r.nOut);
}

void MyGL::sendPlayerDataToGUI() const {
    emit sig_sendPlayerPos(m_player.posAsQString());
    emit sig_sendPlayerVel(m_player.velAsQString());
    emit sig_sendPlayerAcc(m_player.accAsQString());
    emit sig_sendPlayerLook(m_player.lookAsQString());
    glm::vec2 pPos(m_player.mcr_position.x, m_player.mcr_position.z);
    glm::ivec2 chunk(16 * glm::ivec2(glm::floor(pPos / 16.f)));
    glm::ivec2 zone(64 * glm::ivec2(glm::floor(pPos / 64.f)));
    emit sig_sendPlayerChunk(QString::fromStdString("( " + std::to_string(chunk.x) + ", " + std::to_string(chunk.y) + " )"));
    emit sig_sendPlayerTerrainZone(QString::fromStdString("( " + std::to_string(zone.x) + ", " + std::to_string(zone.y) + " )"));
}

std::array<glm::vec4, 6> MyGL::computeFrustumPlanes(const glm::mat4 &vp) {
    // glm matrices are column-major: row(i)[j] == vp[j][i]
    auto row = [&vp](int i) {
        return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]);
    };
    glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    return { r3 + r0,   // left
             r3 - r0,   // right
             r3 + r1,   // bottom
             r3 - r1,   // top
             r3 + r2,   // near
             r3 - r2 }; // far
}

void MyGL::paintGL() {
    // ---- Pass 0: render scene depth from the sun for shadow mapping ----
    glm::vec3 sunDir = m_sky.getSunDirection();
    bool shadowsOn = sunDir.y > 0.06f;
    glm::mat4 lightVP(1.f);
    if (shadowsOn) {
        glm::vec3 center = m_player.mcr_position;
        glm::mat4 lightView = glm::lookAt(center + glm::normalize(sunDir) * 170.f,
                                          center, glm::vec3(0.f, 1.f, 0.f));
        glm::mat4 lightProj = glm::ortho(-130.f, 130.f, -130.f, 130.f, 10.f, 360.f);
        lightVP = lightProj * lightView;
        m_shadowMap.bindFrameBuffer();
        glViewport(0, 0, m_shadowMap.size(), m_shadowMap.size());
        glClear(GL_DEPTH_BUFFER_BIT);
        m_progShadow.setUnifMat4("u_ViewProj", lightVP);
        glm::vec2 sp(m_player.mcr_position.x, m_player.mcr_position.z);
        glm::ivec2 sc = 16 * glm::ivec2(glm::floor(sp / 16.f));
        m_terrain.drawOpaque(sc.x - SHADOW_RADIUS, sc.x + SHADOW_RADIUS + 16,
                             sc.y - SHADOW_RADIUS, sc.y + SHADOW_RADIUS + 16,
                             &m_progShadow, nullptr);

    }

    // Active viewpoint: first-person head, or third-person pulled back
    // along the look direction (stopping short of solid terrain)
    m_activeCamEye = m_player.mcr_camera.mcr_position;
    if (m_thirdPerson) {
        // Pull back along the look ray from a slightly raised anchor,
        // stopping with a safety margin before any solid block so the
        // camera never clips inside terrain at the frame edges
        glm::vec3 f = m_player.mcr_camera.forward();
        glm::vec3 anchor = m_player.mcr_camera.mcr_position + glm::vec3(0.f, 0.35f, 0.f);
        auto solidNear = [this](glm::vec3 q) {
            for (float ox : {-0.22f, 0.22f}) {
                for (float oy : {-0.1f, 0.3f}) {
                    glm::vec3 s = q + glm::vec3(ox, oy, ox * 0.5f);
                    if (!m_terrain.hasChunkAt(glm::floor(s.x), glm::floor(s.z))) continue;
                    BlockType b = m_terrain.getGlobalBlockAt(glm::floor(s.x), glm::floor(s.y), glm::floor(s.z));
                    if (b != EMPTY && b != WATER && b != LAVA) return true;
                }
            }
            return false;
        };
        float dist = 0.8f;
        for (float d = 0.8f; d <= 4.6f; d += 0.15f) {
            if (solidNear(anchor - f * d)) break;
            dist = d;
        }
        // Ease outward so the camera never pops when the pull-back ray
        // crosses block corners, but snap inward instantly: a lagging eye
        // that drifts into a block for a few frames reads as a black flash
        // (especially while jumping or pillaring up).
        if (dist < m_tpDistance) m_tpDistance = dist;
        else m_tpDistance += (dist - m_tpDistance) * glm::min(1.f, 10.f * m_lastDt);
        m_activeCamEye = anchor - f * m_tpDistance;
        m_activeViewProj = m_player.mcr_camera.getViewProjFrom(m_activeCamEye);
    } else {
        m_activeViewProj = m_player.mcr_camera.getViewProj();
    }

    // ---- Pass 1: render the 3D scene into the offscreen frame buffer ----
    m_frameBuffer.bindFrameBuffer();
    glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Sky first (background)
    renderSky();

    // Set projection matrices for 3D rendering
    glm::mat4 viewproj = m_activeViewProj;
    m_progLambert.setUnifMat4("u_ViewProj", viewproj);
    m_progFlat.setUnifMat4("u_ViewProj", viewproj);

    // Shadow-map uniforms for the terrain shader
    m_progLambert.setUnifMat4("u_ShadowVP", lightVP);
    m_progLambert.setUnifInt("u_ShadowsOn", shadowsOn ? 1 : 0);
    m_shadowMap.bindToTextureSlot(3);
    m_progLambert.setUnifInt("u_ShadowMap", 3);

    // Update lighting parameters based on sky and weather
    m_progLambert.setUnifVec3("u_LightDir", m_sky.getSunDirection());
    m_progLambert.setUnifVec3("u_LightColor", m_sky.getLightColor());
    m_progLambert.setUnifFloat("u_LightIntensity", m_sky.getLightIntensity());

    // Distance fog fades terrain into the sky's horizon color
    glm::vec3 fogColor = m_sky.m_currentSkyHorizon * m_weather.getWeatherSkyFactor();
    m_progLambert.setUnifVec3("u_FogColor", fogColor);
    m_progLambert.setUnifFloat("u_FogDistance", std::min(m_weather.getWeatherFogDistance(), FOG_END));
    m_progLambert.setUnifVec3("u_CamPos", m_activeCamEye);

    // Bind texture and set uniforms for the shader
    texture->bind(0);
    m_progLambert.setUnifInt("u_Texture", 0);
    m_progLambert.setUnifFloat("u_Time", m_currentTime);

    renderTerrain();

    // Outline the block the crosshair is aiming at
    glm::ivec3 target;
    if (m_player.raycastBlock(m_terrain, target)) {
        m_progFlat.setUnifMat4("u_Model", glm::translate(glm::mat4(1.f), glm::vec3(target)));
        m_progFlat.setUnifVec4("u_Tint", glm::vec4(1.f));
        m_progFlat.draw(m_blockHighlight);
    }

    renderSheep();
    if (m_thirdPerson &&
        glm::distance(m_activeCamEye, m_player.mcr_camera.mcr_position) > 1.4f) {
        renderPlayerModel();
    }
    renderWeather();

    // ---- Pass 2: post-process the scene texture onto the screen ----
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, width() * devicePixelRatio(), height() * devicePixelRatio());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    // Blue overlay underwater, red in lava, near-black while the camera
    // is inside a solid block (flying through terrain), like Minecraft
    int inWater = 0, inLava = 0, inSolid = 0;
    glm::vec3 camPos = m_activeCamEye;
    if (m_terrain.hasChunkAt(glm::floor(camPos.x), glm::floor(camPos.z))) {
        BlockType camBlock = m_terrain.getGlobalBlockAt(glm::floor(camPos.x),
                                                        glm::floor(camPos.y),
                                                        glm::floor(camPos.z));
        inWater = (camBlock == WATER) ? 1 : 0;
        inLava = (camBlock == LAVA) ? 1 : 0;
        // The buried-camera blackout is for first-person noclip flight. In
        // third person the pull-back collision already keeps the camera out
        // of terrain, so suppressing it here avoids a black flash when a
        // rising jump momentarily tucks the eye behind a ledge.
        inSolid = (!m_thirdPerson && camBlock != EMPTY &&
                   camBlock != WATER && camBlock != LAVA) ? 1 : 0;
    }
    // Fade the buried-camera blackout in and out - a single-frame flash
    // when the camera clips a ceiling reads as a glitch otherwise
    m_solidFade += ((inSolid ? 1.f : 0.f) - m_solidFade) * glm::min(1.f, 14.f * m_lastDt);

    m_frameBuffer.bindToTextureSlot(1);
    m_progPost.setUnifInt("u_Texture", 1);
    m_progPost.setUnifInt("u_InWater", inWater);
    m_progPost.setUnifInt("u_InLava", inLava);
    m_progPost.setUnifFloat("u_InSolid", m_solidFade);
    WeatherType wk = m_weather.getCurrentWeather();
    m_progPost.setUnifInt("u_WeatherKind", wk == RAIN ? 1 : (wk == SNOWY ? 2 : 0));
    m_progPost.setUnifFloat("u_WeatherStrength", m_weather.getWeatherIntensity());
    // Hide the crosshair in third person: the camera sits behind the
    // player, so a centered crosshair lands on the character's body instead
    // of marking where they aim (Minecraft hides it in third person too).
    bool showCrosshair = m_hudVisible && !m_thirdPerson &&
                         (m_mouseCaptured || qEnvironmentVariableIsSet("MC_PILOT") ||
                          qEnvironmentVariableIsSet("MC_DEMO"));
    m_progPost.setUnifInt("u_Crosshair", showCrosshair ? 1 : 0);
    m_progPost.setUnifFloat("u_Time", m_currentTime);
    m_progPost.draw(m_quad);

    // Debug world-axes overlay (toggled with O)
    if (m_showAxes) {
        m_progFlat.setUnifMat4("u_Model", glm::mat4());
        m_progFlat.setUnifVec4("u_Tint", glm::vec4(1.f));
        m_progFlat.draw(m_worldAxes);
    }

    if (m_hudVisible) renderHud();
    glEnable(GL_DEPTH_TEST);
}

