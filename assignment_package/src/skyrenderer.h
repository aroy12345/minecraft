#pragma once
#include "drawable.h"
#include "openglcontext.h"
#include "glm_includes.h"
#include "shaderprogram.h"
#include "weathersystem.h"

// A class that renders a procedural sky dome using ray tracing in the fragment shader
class SkyRenderer : public Drawable {
private:
    OpenGLContext* mp_context;

    // Time of day in hours (0-24)
    float m_timeOfDay;

    // Weather system reference
    WeatherSystem* mp_weatherSystem;


    // Day cycle length in real seconds
    float m_dayCycleLength;

    // Time colors for sky interpolation
    struct TimeColor {
        float time;  // Time of day in hours (0-24)
        glm::vec3 skyZenith;  // Color at the zenith (top of sky)
        glm::vec3 skyHorizon; // Color at the horizon
        glm::vec3 sunColor;   // Color of the sun
        glm::vec3 lightColor; // Color of the directional light
        float lightIntensity; // Intensity of the directional light
    };

    // Predefined time colors for different times of day
    std::vector<TimeColor> m_timeColors;


    // Sun position in the sky (updated based on time)
    glm::vec3 m_sunDirection;

    // Current interpolated colors
public:
    glm::vec3 m_currentSkyZenith;
    glm::vec3 m_currentSkyHorizon;
    glm::vec3 m_currentSunColor;
    glm::vec3 m_currentLightColor;
    float m_currentLightIntensity;


public:
    SkyRenderer(OpenGLContext* context);
    ~SkyRenderer();

    void setWeatherSystem(WeatherSystem* weatherSystem) { mp_weatherSystem = weatherSystem; }

    // Initialize sky colors for different times of day
    void initializeTimeColors();

    // Updates the sky based on elapsed time
    void updateTime(float deltaTimeSeconds);

    // Sets a specific time of day (0-24 hours)
    void setTimeOfDay(float hours);

    // Gets the current time of day
    float getTimeOfDay() const { return m_timeOfDay; }

    // Gets the current sun direction
    glm::vec3 getSunDirection() const { return m_sunDirection; }

    // Gets the current light color
    glm::vec3 getLightColor() const { return m_currentLightColor; }

    // Gets the current light intensity
    float getLightIntensity() const { return m_currentLightIntensity; }

    // Interpolate between two time colors based on current time
    void updateColors();

    // Calculate sun position based on time of day
    void updateSunPosition();

    // Create the full-screen quad for sky rendering
    void createVBOdata() override;


    // Weather-related methods
    float getFogDistance() const;
    glm::vec3 getWeatherModifiedSkyZenith() const;
    glm::vec3 getWeatherModifiedSkyHorizon() const;
    glm::vec3 getWeatherModifiedSunColor() const;
};
