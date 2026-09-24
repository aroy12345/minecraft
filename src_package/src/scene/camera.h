#pragma once
#include "glm_includes.h"
#include "scene/entity.h"

//A perspective projection camera
//Receives its eye position and reference point from the scene XML file
class Camera : public Entity {
private:
    float m_fovy;
    unsigned int m_width, m_height;  // Screen dimensions
    float m_near_clip;  // Near clip plane distance
    float m_far_clip;  // Far clip plane distance
    float m_aspect;    // Aspect ratio

public:
    Camera(glm::vec3 pos);
    Camera(unsigned int w, unsigned int h, glm::vec3 pos);
    Camera(const Camera &c);
    void setWidthHeight(unsigned int w, unsigned int h);

    void tick(float dT, InputBundle &input) override;

    glm::mat4 getViewProj() const;
    // View-projection from an arbitrary eye looking the way the camera
    // faces - used by the third-person mode's pulled-back viewpoint
    glm::mat4 getViewProjFrom(const glm::vec3 &eye) const;
    glm::vec3 forward() const { return m_forward; }
    void updatePos(glm::vec3 position);

};
