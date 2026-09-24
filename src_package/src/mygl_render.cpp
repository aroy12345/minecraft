#include "mygl.h"
#include <glm_includes.h>

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>

// The hotbar: translucent slots with the selected slot outlined, and each
// slot's block drawn from the texture atlas
void MyGL::renderHud() {
    float aspect = height() > 0 ? width() / static_cast<float>(height()) : 1.f;
    if (m_hudDirty) {
        m_hudDirty = false;
        std::array<int, HOTBAR_SIZE> counts;
        for (int i = 0; i < HOTBAR_SIZE; ++i) counts[i] = invCount(m_hotbar[i]);
        m_hudFrame.build(HOTBAR_SIZE, m_selectedSlot, aspect, counts.data());
        m_hudIcons.build(m_hotbar.data(), HOTBAR_SIZE, aspect);
    }
    if (m_craftOpen && m_craftDirty) {
        m_craftDirty = false;
        std::vector<int> have1(Crafting::COUNT), have2(Crafting::COUNT);
        for (int i = 0; i < Crafting::COUNT; ++i) {
            have1[i] = invCount(Crafting::RECIPES[i].in1);
            have2[i] = Crafting::RECIPES[i].in2 == EMPTY ? 0 : invCount(Crafting::RECIPES[i].in2);
        }
        m_craftFrame.build(Crafting::RECIPES, Crafting::COUNT, m_craftSel,
                           have1.data(), have2.data(), aspect);
        m_craftIcons.build(Crafting::RECIPES, Crafting::COUNT, aspect);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_progFlat.setUnifMat4("u_Model", glm::mat4());
    m_progFlat.setUnifMat4("u_ViewProj", glm::mat4());
    m_progFlat.setUnifVec4("u_Tint", glm::vec4(1.f));
    m_progFlat.draw(m_hudFrame);
    texture->bind(0);
    m_progHud.setUnifInt("u_Texture", 0);
    m_progHud.draw(m_hudIcons);
    if (m_craftOpen) {
        m_progFlat.draw(m_craftFrame);
        m_progHud.draw(m_craftIcons);
    }
}

void MyGL::renderSky() {
    // Disable depth testing for sky rendering (always behind everything)
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);

    // Set up sky shader uniforms
    m_progSky.useMe();

    // Set inverse view projection matrix
    m_progSky.setInvViewProj(glm::inverse(m_activeViewProj));

    // Apply weather effects to sky colors
    glm::vec3 weatherSkyFactor = m_weather.getWeatherSkyFactor();
    m_progSky.setSunDirection(m_sky.getSunDirection());
    m_progSky.setSkyZenithColor(m_sky.m_currentSkyZenith * weatherSkyFactor);
    m_progSky.setSkyHorizonColor(m_sky.m_currentSkyHorizon * weatherSkyFactor);
    m_progSky.setSunColor(m_sky.m_currentSunColor * weatherSkyFactor);

    // Set weather-related uniforms
    m_progSky.setFogDistance(m_weather.getWeatherFogDistance());
    m_progSky.setWeatherType(static_cast<int>(m_weather.getCurrentWeather()));
    m_progSky.setWeatherIntensity(m_weather.getWeatherIntensity());
    m_progSky.setTime(m_currentTime);

    // Draw sky quad
    m_progSky.draw(m_sky);

    // Re-enable depth testing for terrain rendering
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

// Draws the chunks around the player in two passes (opaque, then
// transparent) so alpha blending on water resolves correctly. Chunks whose
// bounding boxes fall outside the view frustum are skipped.
void MyGL::renderTerrain() {
    glm::vec2 p(m_player.mcr_position.x, m_player.mcr_position.z);
    glm::ivec2 c = 16 * glm::ivec2(glm::floor(p / 16.f));

    std::array<glm::vec4, 6> frustum = computeFrustumPlanes(m_activeViewProj);

    // OPAQUE PASS
    m_terrain.drawOpaque(c.x - DRAW_RADIUS, c.x + DRAW_RADIUS + 16,
                         c.y - DRAW_RADIUS, c.y + DRAW_RADIUS + 16,
                         &m_progLambert, &frustum);

    // TRANSPARENT PASS
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_terrain.drawTransparent(c.x - DRAW_RADIUS, c.x + DRAW_RADIUS + 16,
                              c.y - DRAW_RADIUS, c.y + DRAW_RADIUS + 16,
                              &m_progLambert, &frustum);
}

void MyGL::renderSheep() {
    // Minecraft-style sheep: cream wool body and tail, tan face with a
    // lighter snout, dark legs that swing in diagonal pairs while walking
    const glm::vec4 WOOL(0.93f, 0.93f, 0.90f, 1.f);
    const glm::vec4 FACE(0.76f, 0.62f, 0.47f, 1.f);
    const glm::vec4 SNOUT(0.85f, 0.72f, 0.62f, 1.f);
    const glm::vec4 LEG(0.34f, 0.30f, 0.27f, 1.f);
    const float S = 0.5f; // overall sheep scale

    int idx = 0;
    for (const auto& sheep : m_terrain.m_sheep) {
        float phase = idx++ * 1.7f;
        bool walking = sheep->isWalking();
        float bob = walking ? glm::sin(m_currentTime * 7.f + phase) * 0.035f : 0.f;
        glm::vec3 basePos = sheep->getPosition() + glm::vec3(0.f, 0.05f + bob, 0.f);
        glm::vec3 forward = sheep->getForward();
        float angle = glm::degrees(atan2(forward.z, forward.x));
        glm::mat4 yaw = glm::rotate(glm::mat4(1.f), glm::radians(angle), glm::vec3(0, 1, 0));
        glm::mat3 yaw3(yaw);

        auto drawPart = [&](glm::vec3 offset, glm::vec3 scale, const glm::vec4 &tint,
                            float swingDeg) {
            glm::mat4 model = glm::translate(glm::mat4(1.f), basePos + yaw3 * (offset * S));
            model = model * yaw;
            if (swingDeg != 0.f) {
                // swing about the hip, sideways axis
                model = glm::rotate(model, glm::radians(swingDeg), glm::vec3(0, 0, 1));
                model = glm::translate(model, glm::vec3(0.f, -scale.y * 0.5f * S, 0.f));
            }
            model = glm::scale(model, scale * S);
            m_progFlat.setUnifMat4("u_Model", model);
            m_progFlat.setUnifVec4("u_Tint", tint);
            m_progFlat.draw(m_sheepCube);
        };

        drawPart(glm::vec3(0.f, 0.f, 0.f), glm::vec3(1.4f, 0.9f, 0.9f), WOOL, 0.f);   // body
        drawPart(glm::vec3(0.85f, 0.3f, 0.f), glm::vec3(0.5f, 0.48f, 0.42f), FACE, 0.f); // head
        drawPart(glm::vec3(1.12f, 0.22f, 0.f), glm::vec3(0.28f, 0.26f, 0.3f), SNOUT, 0.f); // snout
        drawPart(glm::vec3(-0.78f, 0.25f, 0.f), glm::vec3(0.2f, 0.24f, 0.18f), WOOL, 0.f); // tail

        float swing = walking ? glm::sin(m_currentTime * 7.f + phase) * 20.f : 0.f;
        glm::vec3 hips[4] = {
            { 0.5f, -0.4f,  0.3f}, {-0.5f, -0.4f,  0.3f},
            { 0.5f, -0.4f, -0.3f}, {-0.5f, -0.4f, -0.3f},
        };
        for (int leg = 0; leg < 4; ++leg) {
            // diagonal pairs swing in opposite phase
            float s = ((leg == 0 || leg == 3) ? swing : -swing);
            drawPart(hips[leg], glm::vec3(0.16f, 0.6f, 0.16f), LEG, s);
        }
    }
}

// The player avatar, visible in third-person mode: a classic blocky
// figure whose legs swing while walking and whose right arm swings when
// breaking or placing a block.
void MyGL::renderPlayerModel() {
    const glm::vec4 SKIN(0.87f, 0.69f, 0.55f, 1.f);
    const glm::vec4 SHIRT(0.22f, 0.45f, 0.82f, 1.f);
    const glm::vec4 PANTS(0.26f, 0.28f, 0.50f, 1.f);

    glm::vec3 base = m_player.mcr_position;
    glm::vec3 f = m_player.mcr_camera.forward();
    float yaw = glm::degrees(atan2(f.z, f.x));
    glm::mat4 yawRot = glm::rotate(glm::mat4(1.f), glm::radians(yaw), glm::vec3(0, 1, 0));
    glm::mat3 yaw3(yawRot);

    float walkSwing = (m_playerSpeed > 0.8f) ? glm::sin(m_currentTime * 9.f) * 28.f : 0.f;
    float punch = (m_armSwing > 0.f) ? glm::sin(m_armSwing / 0.3f * 3.14159f) * 70.f : 0.f;
    float bob = (m_playerSpeed > 0.8f) ? glm::abs(glm::sin(m_currentTime * 9.f)) * 0.05f : 0.f;
    base.y += bob;
    float headPitch = glm::clamp(m_player.pitch(), -55.f, 55.f);

    // Parts rotate about their attachment point: limbs hang below their
    // pivot (dir -1), the head sits above the neck (dir +1)
    auto drawPart = [&](glm::vec3 offset, glm::vec3 scale, const glm::vec4 &tint,
                        float swingDeg, float pivotDir) {
        glm::mat4 model = glm::translate(glm::mat4(1.f), base + yaw3 * offset);
        model = model * yawRot;
        if (pivotDir != 0.f) {
            model = glm::rotate(model, glm::radians(swingDeg), glm::vec3(0, 0, 1));
            model = glm::translate(model, glm::vec3(0.f, pivotDir * scale.y * 0.5f, 0.f));
        }
        model = glm::scale(model, scale);
        m_progFlat.setUnifMat4("u_Model", model);
        m_progFlat.setUnifVec4("u_Tint", tint);
        m_progFlat.draw(m_sheepCube);
    };

    drawPart({0.f, 1.125f, 0.f}, {0.26f, 0.75f, 0.5f}, SHIRT, 0.f, 0.f);           // torso
    drawPart({0.f, 1.52f, 0.f}, {0.42f, 0.42f, 0.42f}, SKIN, -headPitch, 1.f);     // head, pivots at the neck
    drawPart({0.f, 0.75f, 0.14f}, {0.24f, 0.75f, 0.22f}, PANTS, walkSwing, -1.f);  // legs
    drawPart({0.f, 0.75f, -0.14f}, {0.24f, 0.75f, 0.22f}, PANTS, -walkSwing, -1.f);
    drawPart({0.f, 1.45f, 0.37f}, {0.22f, 0.7f, 0.2f}, SKIN, -walkSwing * 0.7f, -1.f);        // left arm
    drawPart({0.f, 1.45f, -0.37f}, {0.22f, 0.7f, 0.2f}, SKIN, walkSwing * 0.7f - punch, -1.f); // right arm
}

void MyGL::renderWeather() {
    // Skip rendering if weather is clear or intensity is too low
    if (m_weather.getCurrentWeather() == CLEAR || m_weather.getWeatherIntensity() < 0.1f) {
        return;
    }

    // Get view projection matrix and camera position
    glm::mat4 viewproj = m_activeViewProj;
    glm::vec3 cameraPos = m_activeCamEye;

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

