#include "skyrenderer.h"
#include <glm/gtx/transform.hpp>
#include <QDebug>
#include "scene/worldaxes.h"

SkyRenderer::SkyRenderer(OpenGLContext* context)
    : Drawable(context), mp_context(context),
    m_timeOfDay(6.0f), // Start at dawn (6 AM)
    m_dayCycleLength(300.0f), // 5 minutes per day cycle
    m_sunDirection(0.f, 1.f, 0.f) // Start with sun directly up
{
    initializeTimeColors();
    updateColors();
    updateSunPosition();
}

SkyRenderer::~SkyRenderer() {
    destroyVBOdata();
}

void SkyRenderer::initializeTimeColors() {
    // Clear any existing time colors
    m_timeColors.clear();

    // Define colors for different times of day
    // Time, SkyZenith, SkyHorizon, SunColor, LightColor, LightIntensity

    // Midnight (0:00)
    m_timeColors.push_back({
        0.0f,
        glm::vec3(0.0f, 0.0f, 0.1f),     // Dark blue zenith
        glm::vec3(0.0f, 0.0f, 0.05f),    // Nearly black horizon
        glm::vec3(0.0f, 0.0f, 0.0f),     // No sun
        glm::vec3(0.1f, 0.1f, 0.2f),     // Bluish light
        0.2f                            // Low intensity
    });

    // Dawn (6:00)
    m_timeColors.push_back({
        6.0f,
        glm::vec3(0.2f, 0.4f, 0.8f),     // Blue zenith
        glm::vec3(0.8f, 0.6f, 0.4f),     // Orange horizon
        glm::vec3(1.0f, 0.7f, 0.4f),     // Orange-yellow sun
        glm::vec3(0.8f, 0.7f, 0.5f),     // Warm light
        0.7f                            // Medium intensity
    });

    // Morning (8:00)
    m_timeColors.push_back({
        8.0f,
        glm::vec3(0.4f, 0.6f, 0.9f),     // Light blue zenith
        glm::vec3(0.5f, 0.7f, 0.9f),     // Light blue horizon
        glm::vec3(1.0f, 0.9f, 0.7f),     // Yellow-white sun
        glm::vec3(0.9f, 0.9f, 0.8f),     // Almost white light
        0.9f                            // High intensity
    });

    // Noon (12:00)
    m_timeColors.push_back({
        12.0f,
        glm::vec3(0.3f, 0.5f, 1.0f),     // Deep blue zenith
        glm::vec3(0.5f, 0.7f, 1.0f),     // Light blue horizon
        glm::vec3(1.0f, 1.0f, 0.9f),     // White-yellow sun
        glm::vec3(1.0f, 1.0f, 1.0f),     // White light
        1.0f                            // Full intensity
    });

    // Afternoon (16:00)
    m_timeColors.push_back({
        16.0f,
        glm::vec3(0.4f, 0.5f, 0.9f),     // Blue zenith
        glm::vec3(0.5f, 0.7f, 0.9f),     // Light blue horizon
        glm::vec3(1.0f, 0.9f, 0.7f),     // Yellow-white sun
        glm::vec3(0.9f, 0.9f, 0.8f),     // Almost white light
        0.9f                            // High intensity
    });

    // Sunset (18:00)
    m_timeColors.push_back({
        18.0f,
        glm::vec3(0.2f, 0.2f, 0.5f),     // Deep blue zenith
        glm::vec3(0.9f, 0.5f, 0.2f),     // Orange-red horizon
        glm::vec3(1.0f, 0.5f, 0.2f),     // Orange-red sun
        glm::vec3(0.8f, 0.5f, 0.3f),     // Orange light
        0.7f                            // Medium intensity
    });

    // Dusk (20:00)
    m_timeColors.push_back({
        20.0f,
        glm::vec3(0.1f, 0.1f, 0.3f),     // Dark blue zenith
        glm::vec3(0.2f, 0.2f, 0.4f),     // Dark blue horizon
        glm::vec3(0.5f, 0.2f, 0.1f),     // Dim red sun
        glm::vec3(0.3f, 0.2f, 0.2f),     // Dim reddish light
        0.4f                            // Low intensity
    });

    // Back to midnight to complete the cycle
    m_timeColors.push_back({
        24.0f,
        glm::vec3(0.0f, 0.0f, 0.1f),     // Dark blue zenith
        glm::vec3(0.0f, 0.0f, 0.05f),    // Nearly black horizon
        glm::vec3(0.0f, 0.0f, 0.0f),     // No sun
        glm::vec3(0.1f, 0.1f, 0.2f),     // Bluish light
        0.2f                            // Low intensity
    });
}

void SkyRenderer::updateTime(float deltaTimeSeconds) {
    // Update time of day (0-24 hours)
    float timeIncrement = (24.0f / m_dayCycleLength) * deltaTimeSeconds;
    m_timeOfDay = fmod(m_timeOfDay + timeIncrement, 24.0f);

    // Update colors and sun position based on new time
    updateColors();
    updateSunPosition();
}


void SkyRenderer::setTimeOfDay(float hours) {
    m_timeOfDay = fmod(hours, 24.0f);
    if (m_timeOfDay < 0.0f) {
        m_timeOfDay += 24.0f;
    }
    // Update colors and sun position based on new time
    updateColors();
    updateSunPosition();
}

void SkyRenderer::updateColors() {
    // Find the two time points to interpolate between
    int lowerIndex = 0;
    int upperIndex = 0;

    for (size_t i = 0; i < m_timeColors.size() - 1; ++i) {
        if (m_timeOfDay >= m_timeColors[i].time && m_timeOfDay < m_timeColors[i + 1].time) {
            lowerIndex = i;
            upperIndex = i + 1;
            break;
        }
    }

    // Calculate interpolation factor between the two time points
    float lowerTime = m_timeColors[lowerIndex].time;
    float upperTime = m_timeColors[upperIndex].time;
    float factor = (m_timeOfDay - lowerTime) / (upperTime - lowerTime);

    // Interpolate colors
    m_currentSkyZenith = glm::mix(
        m_timeColors[lowerIndex].skyZenith,
        m_timeColors[upperIndex].skyZenith,
        factor
        );

    m_currentSkyHorizon = glm::mix(
        m_timeColors[lowerIndex].skyHorizon,
        m_timeColors[upperIndex].skyHorizon,
        factor
        );

    m_currentSunColor = glm::mix(
        m_timeColors[lowerIndex].sunColor,
        m_timeColors[upperIndex].sunColor,
        factor
        );

    m_currentLightColor = glm::mix(
        m_timeColors[lowerIndex].lightColor,
        m_timeColors[upperIndex].lightColor,
        factor
        );

    m_currentLightIntensity = glm::mix(
        m_timeColors[lowerIndex].lightIntensity,
        m_timeColors[upperIndex].lightIntensity,
        factor
        );
}

void SkyRenderer::updateSunPosition() {
    // Calculate sun angle based on time of day
    // Time is 0-24 hours, convert to 0-360 degrees
    float sunAngle = (m_timeOfDay / 24.0f) * 360.0f - 90.0f; // -90 to start at dawn

    // Convert to radians
    float sunAngleRadians = glm::radians(sunAngle);

    // Calculate sun direction based on angle
    m_sunDirection.x = 0.0f;
    m_sunDirection.y = sin(sunAngleRadians);
    m_sunDirection.z = cos(sunAngleRadians);

    // Normalize the direction vector
    m_sunDirection = glm::normalize(m_sunDirection);
}

void SkyRenderer::createVBOdata() {
    // Create a full-screen quad for the sky
    std::vector<glm::vec4> positions;
    std::vector<glm::vec4> normals;
    std::vector<glm::vec4> colors;
    std::vector<GLuint> indices;

    // The full-screen quad consists of two triangles
    // Vertex positions for full-screen quad (in NDC)
    positions.push_back(glm::vec4(-1.0f, -1.0f, 1.0f, 1.0f)); // Bottom left
    positions.push_back(glm::vec4(1.0f, -1.0f, 1.0f, 1.0f));  // Bottom right
    positions.push_back(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));   // Top right
    positions.push_back(glm::vec4(-1.0f, 1.0f, 1.0f, 1.0f));  // Top left

    // All normals point forward (towards the camera)
    normals.push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    normals.push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    normals.push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    normals.push_back(glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));

    // Color is not really used for the sky, but we need to provide it
    colors.push_back(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    colors.push_back(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    colors.push_back(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    colors.push_back(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));


    // Indices for two triangles
    indices.push_back(0);
    indices.push_back(1);
    indices.push_back(2);
    indices.push_back(0);
    indices.push_back(2);
    indices.push_back(3);

    // Create and bind position buffer
    generateBuffer(POSITION);
    bindBuffer(POSITION);
    mp_context->glBufferData(GL_ARRAY_BUFFER, positions.size() * sizeof(glm::vec4), positions.data(), GL_STATIC_DRAW);

    // Create and bind normal buffer
    generateBuffer(NORMAL);
    bindBuffer(NORMAL);
    mp_context->glBufferData(GL_ARRAY_BUFFER, normals.size() * sizeof(glm::vec4), normals.data(), GL_STATIC_DRAW);

    // Create and bind color buffer
    generateBuffer(COLOR);
    bindBuffer(COLOR);
    mp_context->glBufferData(GL_ARRAY_BUFFER, colors.size() * sizeof(glm::vec4), colors.data(), GL_STATIC_DRAW);

    // Create and bind index buffer
    generateBuffer(INDEX);
    bindBuffer(INDEX);
    mp_context->glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

    // Store the number of indices for rendering
    indexCounts[INDEX] = indices.size();
}


glm::vec3 SkyRenderer::getWeatherModifiedSkyZenith() const {
    if (mp_weatherSystem) {
        // Apply weather modification to zenith color
        return m_currentSkyZenith * mp_weatherSystem->getWeatherSkyFactor();
    }
    return m_currentSkyZenith;
}

glm::vec3 SkyRenderer::getWeatherModifiedSkyHorizon() const {
    if (mp_weatherSystem) {
        // Apply weather modification to horizon color
        return m_currentSkyHorizon * mp_weatherSystem->getWeatherSkyFactor();
    }
    return m_currentSkyHorizon;
}

glm::vec3 SkyRenderer::getWeatherModifiedSunColor() const {
    if (mp_weatherSystem) {
        // Apply weather modification to sun color
        return m_currentSunColor * mp_weatherSystem->getWeatherSkyFactor();
    }
    return m_currentSunColor;
}

float SkyRenderer::getFogDistance() const {
    if (mp_weatherSystem) {
        return mp_weatherSystem->getWeatherFogDistance();
    }
    return 1000.0f; // Default fog distance (far away)
}
