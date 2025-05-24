#include "mygl.h"
#include <glm_includes.h>
#include <iostream>
#include <QApplication>
#include <QKeyEvent>
#include <QDateTime>
#include <QPainter>
#include <QDir>
#include <QDebug>
#include "scene/terrain.h"
#include "weathersystem.h"



MyGL::MyGL(QWidget *parent)
    : OpenGLContext(parent),
    m_worldAxes(this),
    m_progLambert(this), m_progFlat(this), m_progInstanced(this), m_progSky(this),
    m_progWeather(this),
    m_terrain(this), m_player(glm::vec3(48.f, 180.f, 48.f), m_terrain),
    m_sky(this),
    m_weather(this),
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
    setCursor(Qt::CrossCursor);


}

MyGL::~MyGL() {
    makeCurrent();
    glDeleteVertexArrays(1, &vao);
    if (texture) {
        delete texture;
    }
}

void MyGL::moveMouseToCenter() {
    QCursor::setPos(this->mapToGlobal(QPoint(width() / 2, height() / 2)));
}
// Update the initializeGL method in mygl.cpp to initialize our weather components

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

    // Set the color with which the screen is filled at the start of each render call.
    // This is now handled by the sky rendering
    // glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    // printGLErrorLog();

    // Create a Vertex Attribute Object
    glGenVertexArrays(1, &vao);
    //Create the instance of the world axes
    m_worldAxes.createVBOdata();

       m_sheepCube.createVBOdata();

    // Create and set up the diffuse shader
    m_progLambert.create(":/glsl/lambert.vert.glsl", ":/glsl/lambert.frag.glsl");
    // Create and set up the flat lighting shader
    m_progFlat.create(":/glsl/flat.vert.glsl", ":/glsl/flat.frag.glsl");
    m_progInstanced.create(":/glsl/instanced.vert.glsl", ":/glsl/lambert.frag.glsl");
    // Create and set up the sky shader
    m_progSky.create(":/glsl/sky.vert.glsl", ":/glsl/sky.frag.glsl");
    // Create and set up the weather shaders
    m_progWeather.create(":/glsl/weather.vert.glsl", ":/glsl/weather.frag.glsl");

    // We have to have a VAO bound in OpenGL 3.2 Core. But if we're not
    // using multiple VAOs, we can just bind one once.
    glBindVertexArray(vao);

    // Load texture
    QDir currentDir = QDir::current();
    qDebug() << "Current directory using Qt:" << currentDir.absolutePath();

    QImage textureImage(":/textures/minecraft_textures_all.png");
    if (textureImage.isNull()) {
        qDebug() << "Failed to load texture!";
    } else {
        qDebug() << "Texture loaded successfully!";
        texture = new QOpenGLTexture(textureImage.mirrored());
        texture->setMinificationFilter(QOpenGLTexture::Nearest);
        texture->setMagnificationFilter(QOpenGLTexture::Nearest);
        texture->setWrapMode(QOpenGLTexture::Repeat);
    }


    // Create sky VBO data
    m_sky.createVBOdata();

    // Initialize weather system
    m_weather.initialize();


    m_sky.setWeatherSystem(&m_weather);


    // Generate procedural terrain
    m_terrain.CreateTestScene();
}


void MyGL::resizeGL(int w, int h) {
    //This code sets the concatenated view and perspective projection matrices used for
    //our scene's camera view.
    m_player.setCameraWidthHeight(static_cast<unsigned int>(w), static_cast<unsigned int>(h));
    glm::mat4 viewproj = m_player.mcr_camera.getViewProj();
    // Upload the view-projection matrix to our shaders (i.e. onto the graphics card)
    m_progLambert.setUnifMat4("u_ViewProj", viewproj);
    m_progFlat.setUnifMat4("u_ViewProj", viewproj);
    m_progInstanced.setUnifMat4("u_ViewProj", viewproj);

    // Set the inverse view-projection matrix for the sky shader
    glm::mat4 invViewProj = glm::inverse(viewproj);
    m_progSky.setInvViewProj(invViewProj);

    printGLErrorLog();
}

// Update the tick method in mygl.cpp to update our weather system

void MyGL::tick() {
    // Get current time for delta time calculation
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    float dT = (currentTime - m_prevTime) / 1000.f;
    m_prevTime = currentTime;

    // Update time for animation
    m_currentTime += dT;

    // Update sky time
    m_sky.updateTime(dT);

    // Update weather system
    m_weather.update(dT, m_player.mcr_position, &m_terrain);

    // Check if we need to create new chunks
    m_terrain.checkAndCreateNewChunks(m_player.mcr_position);

    // Process multithreaded chunk work
    m_terrain.consumeChunkWork();

    // Pass dT to player's tick function
    m_player.tick(dT, m_inputs);

    update(); // Calls paintGL() as part of a larger QOpenGLWidget pipeline
    sendPlayerDataToGUI(); // Updates the info in the secondary window displaying player data
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



// Update the paintGL method in mygl.cpp to render weather effects

void MyGL::paintGL() {
    // Clear the screen so that we only see newly drawn images
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // First render the sky (background)
    renderSky();

    // Set projection matrices for 3D rendering
    glm::mat4 viewproj = m_player.mcr_camera.getViewProj();
    m_progLambert.setUnifMat4("u_ViewProj", viewproj);
    m_progFlat.setUnifMat4("u_ViewProj", viewproj);
    m_progInstanced.setUnifMat4("u_ViewProj", viewproj);

    // Update lighting parameters based on sky and weather
    m_progLambert.setUnifVec3("u_LightDir", m_sky.getSunDirection());
    m_progLambert.setUnifVec3("u_LightColor", m_sky.getLightColor());
    m_progLambert.setUnifFloat("u_LightIntensity", m_sky.getLightIntensity());


    // Bind texture and set uniforms for the shader
    texture->bind(0);
    m_progLambert.setUnifInt("u_Texture", 0);
    m_progLambert.setUnifFloat("u_Time", m_currentTime);

    // Render the terrain
    renderTerrain();

    for (const auto& sheep : m_terrain.m_sheep) {
        glm::vec3 basePos = sheep->getPosition() + glm::vec3(0.f, 0.05f, 0.f);
        glm::vec3 forward = sheep->getForward();

        float sheepScale = 0.5f; // Shrink everything by half

        // ----- Calculate rotation from forward -----
        float angle = glm::degrees(atan2(forward.z, forward.x)); // Heading angle

        // Body
        glm::mat4 model = glm::mat4(1.f);
        model = glm::translate(model, basePos);
        model = glm::rotate(model, glm::radians(angle), glm::vec3(0,1,0));
        model = glm::scale(model, glm::vec3(sheepScale) * glm::vec3(1.4f, 0.8f, 0.9f));
        m_progFlat.setUnifMat4("u_Model", model);
        m_progFlat.draw(m_sheepCube);

        // Head
        glm::mat4 headModel = glm::mat4(1.f);
        headModel = glm::translate(headModel, basePos + glm::mat3(glm::rotate(glm::mat4(1.f), glm::radians(angle), glm::vec3(0,1,0))) * glm::vec3(0.8f, 0.2f, 0.f) * sheepScale);
        headModel = glm::rotate(headModel, glm::radians(angle), glm::vec3(0,1,0));
        headModel = glm::scale(headModel, glm::vec3(sheepScale) * glm::vec3(0.5f, 0.5f, 0.5f));
        m_progFlat.setUnifMat4("u_Model", headModel);
        m_progFlat.draw(m_sheepCube);

        // Legs
        glm::vec3 legOffsets[] = {
            glm::vec3( 0.5f, -0.6f,  0.3f),
            glm::vec3(-0.5f, -0.6f,  0.3f),
            glm::vec3( 0.5f, -0.6f, -0.3f),
            glm::vec3(-0.5f, -0.6f, -0.3f),
        };

        for (glm::vec3 offset : legOffsets) {
            glm::mat4 legModel = glm::mat4(1.f);
            legModel = glm::translate(legModel, basePos + glm::mat3(glm::rotate(glm::mat4(1.f), glm::radians(angle), glm::vec3(0,1,0))) * offset * sheepScale);
            legModel = glm::rotate(legModel, glm::radians(angle), glm::vec3(0,1,0));
            legModel = glm::scale(legModel, glm::vec3(sheepScale) * glm::vec3(0.15f, 0.5f, 0.15f));
            m_progFlat.setUnifMat4("u_Model", legModel);
            m_progFlat.draw(m_sheepCube);
        }
    }


    // Render weather effects (particles)
    renderWeather();

    // Render the world axes
    glDisable(GL_DEPTH_TEST);
    m_progFlat.setUnifMat4("u_Model", glm::mat4());
    m_progFlat.draw(m_worldAxes);
    glEnable(GL_DEPTH_TEST);
}


void MyGL::renderSky() {
    // Disable depth testing for sky rendering (always behind everything)
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    // Set up sky shader uniforms
    m_progSky.useMe();

    // Set inverse view projection matrix
    glm::mat4 viewproj = m_player.mcr_camera.getViewProj();
    glm::mat4 invViewProj = glm::inverse(viewproj);
    m_progSky.setInvViewProj(invViewProj);

    // Get weather information
    WeatherType currentWeather = m_weather.getCurrentWeather();
    float weatherIntensity = m_weather.getWeatherIntensity();
    glm::vec3 weatherSkyFactor = m_weather.getWeatherSkyFactor();
    float fogDistance = m_weather.getWeatherFogDistance();

    // Apply weather effects to sky colors
    glm::vec3 modifiedZenith = m_sky.m_currentSkyZenith * weatherSkyFactor;
    glm::vec3 modifiedHorizon = m_sky.m_currentSkyHorizon * weatherSkyFactor;
    glm::vec3 modifiedSunColor = m_sky.m_currentSunColor * weatherSkyFactor;

    // Set sky colors based on current time, modified by weather
    m_progSky.setSunDirection(m_sky.getSunDirection());
    m_progSky.setSkyZenithColor(modifiedZenith);
    m_progSky.setSkyHorizonColor(modifiedHorizon);
    m_progSky.setSunColor(modifiedSunColor);

    // Set weather-related uniforms
    m_progSky.setFogDistance(fogDistance);
    m_progSky.setWeatherType(static_cast<int>(currentWeather));
    m_progSky.setWeatherIntensity(weatherIntensity);
    m_progSky.setTime(m_currentTime);

    // Draw sky quad
    m_progSky.draw(m_sky);

    // Re-enable depth testing for terrain rendering
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}


// void MyGL::renderWeather() {
//     // Skip rendering if weather is clear or intensity is too low
//     if (m_weather.getCurrentWeather() == CLEAR || m_weather.getWeatherIntensity() < 0.1f) {
//         return;
//     }

//     // Get view projection matrix and camera position
//     glm::mat4 viewproj = m_player.mcr_camera.getViewProj();
//     glm::vec3 cameraPos = m_player.mcr_camera.mcr_position;

//     // Calculate simple wind direction based on time
//     glm::vec3 windDir = glm::vec3(
//         sin(m_currentTime * 0.1f),
//         -1.0f, // Wind always blows slightly downward for precipitation
//         cos(m_currentTime * 0.1f)
//         );
//     windDir = glm::normalize(windDir);

//     // Wind strength varies by weather type
//     float windStrength = 0.0f;
//     if (m_weather.getCurrentWeather() == RAIN) {
//         windStrength = 1.0f + sin(m_currentTime * 0.3f) * 0.5f;
//     } else { // SNOW
//         windStrength = 0.3f + sin(m_currentTime * 0.2f) * 0.2f;
//     }

//     // Set up weather shader
//     m_progWeather.useMe();
//     m_progWeather.setWindDirection(windDir);
//     m_progWeather.setWindStrength(windStrength);
//     m_progWeather.setTime(m_currentTime);

//     // Draw weather particles
//     m_weather.drawParticles(&m_progWeather, viewproj, cameraPos);
// }

void MyGL::renderWeather() {
    // Skip rendering if weather is clear or intensity is too low
    if (m_weather.getCurrentWeather() == CLEAR || m_weather.getWeatherIntensity() < 0.1f) {
        return;
    }

    // Get view projection matrix and camera position
    glm::mat4 viewproj = m_player.mcr_camera.getViewProj();
    glm::vec3 cameraPos = m_player.mcr_camera.mcr_position;

    // Calculate simple wind direction based on time
    glm::vec3 windDir = glm::vec3(
        sin(m_currentTime * 0.1f),
        -1.0f, // Wind always blows slightly downward for precipitation
        cos(m_currentTime * 0.1f)
        );
    windDir = glm::normalize(windDir);

    // Wind strength varies by weather type
    float windStrength = 0.0f;
    if (m_weather.getCurrentWeather() == RAIN) {
        windStrength = 1.0f + sin(m_currentTime * 0.3f) * 0.5f;
    } else { // SNOW
        windStrength = 0.3f + sin(m_currentTime * 0.2f) * 0.2f;
    }

    // Set up weather shader
    m_progWeather.useMe();
    m_progWeather.setWindDirection(windDir);
    m_progWeather.setWindStrength(windStrength);
    m_progWeather.setTime(m_currentTime);
    m_progWeather.setCameraPos(cameraPos);

    // Enable blending for particles
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Disable depth writing (but keep depth testing)
    glDepthMask(GL_FALSE);

    // Draw weather particles
    m_weather.drawParticles(&m_progWeather, viewproj, cameraPos);

    // Restore state
    glDepthMask(GL_TRUE);
}


// Updated to render the nine zones of generated terrain
// that surround the player, with separate passes for opaque and transparent blocks
void MyGL::renderTerrain() {
    glm::vec2 p(m_player.mcr_position.x, m_player.mcr_position.z);
    glm::ivec2 zone = 64 * glm::ivec2(glm::floor(p/64.f));

    // OPAQUE PASS
    m_terrain.drawOpaque(zone.x-64, zone.x+64+16,
                         zone.y-64, zone.y+64+16,
                         &m_progLambert);

    // TRANSPARENT PASS
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_terrain.drawTransparent(zone.x-64, zone.x+64+16,
                              zone.y-64, zone.y+64+16,
                              &m_progLambert);
}

void MyGL::keyPressEvent(QKeyEvent *e) {
    float amount = 2.0f;
    if(e->modifiers() & Qt::ShiftModifier){
        amount = 10.0f;
    }
    if (e->key() == Qt::Key_Escape) {
        QApplication::quit();
    } else if (e->key() == Qt::Key_Right) {
        m_player.rotateOnUpGlobal(-amount);
    } else if (e->key() == Qt::Key_Left) {
        m_player.rotateOnUpGlobal(amount);
    } else if (e->key() == Qt::Key_Up) {
        m_player.rotateOnRightLocal(-amount);
    } else if (e->key() == Qt::Key_Down) {
        m_player.rotateOnRightLocal(amount);
    } else if (e->key() == Qt::Key_W) {
        m_inputs.wPressed = true;
    } else if (e->key() == Qt::Key_S) {
        m_inputs.sPressed = true;
    } else if (e->key() == Qt::Key_D) {
        m_inputs.dPressed = true;
    } else if (e->key() == Qt::Key_A) {
        m_inputs.aPressed = true;
    } else if (e->key() == Qt::Key_Q) {
        m_inputs.qPressed = true;
    } else if (e->key() == Qt::Key_E) {
        m_inputs.ePressed = true;
    } else if (e->key() == Qt::Key_F) {
        m_inputs.fPressed = true;
    } else if (e->key() == Qt::Key_Space) {
        m_inputs.spacePressed = true;
    } else if (e->key() == Qt::Key_R){
        m_inputs.rPressed = true;
    } else if (e->key() == Qt::Key_T) {
        // Set time to morning when T is pressed
        m_sky.setTimeOfDay(6.0f);
    } else if (e->key() == Qt::Key_Y) {
        // Set time to noon when Y is pressed
        m_sky.setTimeOfDay(12.0f);
    } else if (e->key() == Qt::Key_U) {
        // Set time to sunset when U is pressed
        m_sky.setTimeOfDay(18.0f);
    } else if (e->key() == Qt::Key_I) {
        // Set time to midnight when I is pressed
        m_sky.setTimeOfDay(0.0f);
    } else if (e->key() == Qt::Key_1) {
        // Set weather to clear when 1 is pressed
        m_weather.setWeather(CLEAR);
    } else if (e->key() == Qt::Key_2) {
        // Set weather to rain when 2 is pressed
        m_weather.setWeather(RAIN);
    } else if (e->key() == Qt::Key_3) {
        // Set weather to snow when 3 is pressed
        m_weather.setWeather(SNOWY);
    }
}

void MyGL::keyReleaseEvent(QKeyEvent *e) {
    switch(e->key()) {
    case Qt::Key_W: m_inputs.wPressed = false; break;
    case Qt::Key_S: m_inputs.sPressed = false; break;
    case Qt::Key_D: m_inputs.dPressed = false; break;
    case Qt::Key_A: m_inputs.aPressed = false; break;
    case Qt::Key_Space: m_inputs.spacePressed = false; break;
    case Qt::Key_E: m_inputs.ePressed = false; break;
    case Qt::Key_Q: m_inputs.qPressed = false; break;
    case Qt::Key_F: m_inputs.fPressed = false; break;
    case Qt::Key_R: m_inputs.rPressed = false; break;
    }
}

void MyGL::mouseMoveEvent(QMouseEvent *e) {
    QPoint center(width() / 2, height() / 2);
    QPoint delta = e->pos() - center;
    float sensitivity = 0.005f; // Reduced sensitivity for smoother camera movement
    m_player.rotateOnUpGlobal(-delta.x() * sensitivity);
    m_player.rotateOnRightLocal(-delta.y() * sensitivity);
    moveMouseToCenter();
}

void MyGL::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::RightButton) {
        m_player.removeAddBlock(true, false, m_terrain, m_player.shootingRange);
    } else if (e->button() == Qt::LeftButton) {
        m_inputs.leftpressed = true;
        m_player.removeAddBlock(false, true, m_terrain, m_player.shootingRange);
    }
    update();
}

void MyGL::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_inputs.leftpressed = false;
    }
}
