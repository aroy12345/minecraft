#pragma once
#include "entity.h"
#include "camera.h"
#include "terrain.h"

class Player : public Entity {
private:

    glm::vec3 m_acceleration;
    bool m_flightMode = true;
    bool m_onground = true;
    glm::vec3 m_velocity;
    Camera m_camera;
    const Terrain &mcr_terrain;
    float moveSpeed;
    float gravity;

    void processInputs(InputBundle &inputs);
    void computePhysics(float dT, const Terrain &terrain);



public:
    // Readonly public reference to our camera
    // for easy access from MyGL
    float shootingRange = 3.f;
    const Camera& mcr_camera;

    Player(glm::vec3 pos, const Terrain &terrain);
    virtual ~Player() override;

    void setCameraWidthHeight(unsigned int w, unsigned int h);

    void tick(float dT, InputBundle &input) override;

    // Player overrides all of Entity's movement
    // functions so that it transforms its camera
    // by the same amount as it transforms itself.
    void moveAlongVector(glm::vec3 dir) override ;
    void moveForwardLocal(float amount) override;
    void moveRightLocal(float amount) override;
    void moveUpLocal(float amount) override;
    void moveForwardGlobal(float amount) override;
    void moveRightGlobal(float amount) override;
    void moveUpGlobal(float amount) override;
    void rotateOnForwardLocal(float degrees) override;
    void rotateOnRightLocal(float degrees) override;
    void rotateOnUpLocal(float degrees) override;
    void rotateOnForwardGlobal(float degrees) override;
    void rotateOnRightGlobal(float degrees) override;
    void rotateOnUpGlobal(float degrees) override;
    void updatePos(glm::vec3 pos);


    // For sending the Player's data to the GUI
    // for display
    QString posAsQString() const;
    QString velAsQString() const;
    QString accAsQString() const;
    QString lookAsQString() const;

    void removeAddBlock(bool right, bool left, Terrain &terrain, float shootingRange);


};

