#pragma once
#include "entity.h"
#include "camera.h"
#include "terrain.h"

class Player : public Entity {
private:

    glm::vec3 m_acceleration;
    bool m_flightMode = true;
    // Noclip (spectator) flight passes through terrain - this is the
    // spec's collision-free flight mode, on its own toggle (G) so that
    // normal flight can collide with the world like Minecraft creative.
    bool m_noclip = false;
    bool m_onground = true;
    // True while any part of the player overlaps WATER or LAVA; movement
    // slows to 2/3 speed and Space swims upward instead of jumping.
    bool m_inLiquid = false;
    // Lava specifically is buoyant - the player bobs on its surface
    bool m_inLava = false;
    // Accumulated look pitch, so the camera can be clamped at +/-89 degrees
    // and never flips upside down.
    float m_pitchDegrees = 0.f;
    glm::vec3 m_velocity;
    Camera m_camera;
    const Terrain &mcr_terrain;

    void updateLiquidState();
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

    // Directly set flight vs. ground mode (the F key toggles it in play;
    // the scripted self-test needs deterministic control)
    void setFlightMode(bool on) {
        m_flightMode = on;
        if (!on) m_velocity.y = 0.f;
    }
    // Directly set noclip (collision-free flight). The scripted demo turns
    // this on for vertical fly-throughs so an ascent never snags on a tree
    // or boulder, and off again before it needs to land on solid ground.
    void setNoclip(bool on) {
        m_noclip = on;
        if (on) m_flightMode = true;
    }
    // Current look pitch in degrees, clamped to [-89, 89]
    float pitch() const { return m_pitchDegrees; }
    // Horizontal speed (blocks/second). The self-test reads this steady-state
    // value to compare walking vs sprinting without depending on frame
    // timing, which a streaming hitch can otherwise skew.
    float horizontalSpeed() const {
        return glm::length(glm::vec2(m_velocity.x, m_velocity.z));
    }
    // Signed vertical speed, read by the self-test to confirm ascend/descend
    // without depending on a frame-timed position delta.
    float verticalSpeed() const { return m_velocity.y; }

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

    // Returns the block that was broken (left), placed (right), or the
    // lever that was toggled; EMPTY when nothing happened. The caller uses
    // this to collect drops and consume inventory.
    BlockType removeAddBlock(bool right, bool left, Terrain &terrain, float shootingRange,
                             BlockType placeType = STONE);

    // Grid-marches the crosshair ray; returns true and the hit block's cell
    // if a non-empty block lies within shootingRange. Used for the
    // targeted-block highlight.
    bool raycastBlock(const Terrain &terrain, glm::ivec3 &outBlock) const;
};

