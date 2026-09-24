#include "player.h"
#include <QString>
#include <algorithm>
#include <QDebug>

Player::Player(glm::vec3 pos, const Terrain &terrain)
    : Entity(pos), m_acceleration(0,0,0), m_velocity(0,0,0),
    m_camera(pos + glm::vec3(0, 1.5f, 0)), mcr_terrain(terrain),
    mcr_camera(m_camera)
{}

Player::~Player()
{}

void Player::tick(float dT, InputBundle &input) {
    updateLiquidState();
    processInputs(input);
    computePhysics(dT, mcr_terrain);
    m_camera.updatePos(m_position);
}

void Player::updateLiquidState() {
    m_inLiquid = false;
    m_inLava = false;
    for (float dy : {0.5f, 1.5f}) { // center of the lower and upper block
        glm::vec3 p = m_position + glm::vec3(0.f, dy, 0.f);
        if (!mcr_terrain.hasChunkAt(glm::floor(p.x), glm::floor(p.z))) continue;
        BlockType b = mcr_terrain.getGlobalBlockAt(glm::floor(p.x), glm::floor(p.y), glm::floor(p.z));
        if (b == WATER || b == LAVA) {
            m_inLiquid = true;
            if (b == LAVA) m_inLava = true;
            return;
        }
    }
}


void Player::processInputs(InputBundle &inputs){

    //toggle b/w flightmode and groundmode
    if (inputs.fPressed){
        inputs.fPressed = false;
        m_flightMode = !m_flightMode;
        if (!m_flightMode){
            m_onground = true;
            m_velocity.y = 0;
            m_noclip = false;
        }
    }

    // Noclip toggle: collision-free flight that phases through terrain
    if (inputs.gPressed){
        inputs.gPressed = false;
        m_noclip = !m_noclip;
        if (m_noclip) m_flightMode = true; // noclip implies flying
    }

    //reset accel
    m_acceleration = glm::vec3(0.f);

    //start with flightmode
    if(m_flightMode){
        //flight mode
        if (inputs.wPressed){
            m_acceleration += m_forward;
        }
        if (inputs.sPressed){
            m_acceleration -= m_forward;
        }
        if (inputs.dPressed){
            m_acceleration += m_right;
        }
        if (inputs.aPressed){
            m_acceleration -= m_right;
        }
        // Vertical flight is along the world up axis regardless of look
        // pitch, like Minecraft creative - the tilted local up would drift
        // the player horizontally while ascending or descending
        if (inputs.ePressed){
            m_acceleration += glm::vec3(0.f, 1.f, 0.f);
        }
        if (inputs.qPressed){
            m_acceleration -= glm::vec3(0.f, 1.f, 0.f);
        }
    } else{
        //ground mode
        glm::vec3 groundforward = glm::normalize(glm::vec3(m_forward.x, 0.f, m_forward.z));
        glm::vec3 groundright = glm::normalize(glm::vec3(m_right.x, 0.f, m_right.z));

        // A higher jump than a real 1-block hop: clears roughly two blocks,
        // so terraced ledges and shallow dug shafts can be jumped straight
        // out of. (For a deeper shaft, jump while placing a block beneath
        // you to pillar back up.)
        const float JUMPVEL = 13.5f;
        const float SWIM_UP_SPEED = 6.5f;

        if (inputs.wPressed){
            m_acceleration += groundforward;
            // Swim-out lunge: stroking toward a 1-block bank while in
            // water boosts the player up over its lip, like Minecraft's
            // water climb - without it river banks are impossible to exit
            if (m_inLiquid) {
                glm::vec3 ahead = m_position + groundforward * 0.7f;
                if (mcr_terrain.hasChunkAt(glm::floor(ahead.x), glm::floor(ahead.z))) {
                    auto at = [this, &ahead](float dy) {
                        return mcr_terrain.getGlobalBlockAt(
                            glm::floor(ahead.x), glm::floor(ahead.y + dy), glm::floor(ahead.z));
                    };
                    BlockType lip = at(0.f), above1 = at(1.f), above2 = at(2.f);
                    bool lipSolid = lip != EMPTY && lip != WATER && lip != LAVA;
                    bool clearAbove = (above1 == EMPTY || above1 == WATER) && above2 == EMPTY;
                    if (lipSolid && clearAbove) {
                        m_velocity.y = glm::max(m_velocity.y, 7.5f);
                    }
                }
            }
        }
        if (inputs.sPressed){
            m_acceleration -= groundforward;
        }
        if (inputs.dPressed){
            m_acceleration += groundright;
        }
        if (inputs.aPressed){
            m_acceleration -= groundright;
        }
        if(inputs.spacePressed){
            if (m_inLiquid){
                // Swim upward at a constant rate while Space is held
                m_velocity.y = SWIM_UP_SPEED;
            }
            else if (m_onground==true){
                m_velocity.y = JUMPVEL;
                m_onground=false;
            }
        }
        if(inputs.rPressed){
            shootingRange = std::min(shootingRange + 1.f, 10.f);
        }
    }

    // Sprint while Shift is held (both movement modes)
    if (inputs.shiftPressed) {
        m_acceleration *= 2.f;
    }
}

void Player::computePhysics(float dT, const Terrain &terrain) {
    const float GRAVITY = -9.8f * 4;
    const float DRAG = 0.9f;
    // Flight is fast for covering distance; walking lands near Minecraft's
    // pace. Steady-state speed is accel * dT / (1 - DRAG).
    const float accelAmount = m_flightMode ? 66.f : 34.f;
    // In water or lava, both lateral movement and gravity run at 2/3 speed
    const float liquidScale = m_inLiquid ? (2.f / 3.f) : 1.f;
    float toMove;

    m_acceleration *= accelAmount * liquidScale;

    if (m_flightMode) {
        // Flight (including E/Q) is acceleration + drag on every axis, which
        // gives a smooth, capped cruising speed.
        m_velocity *= DRAG;
    } else {
        // Ground: horizontal drag is friction, but the vertical axis must be
        // pure gravity. Applying the same heavy drag to it sapped every jump
        // to a stub - barely half a block - no matter how strong the launch.
        m_velocity.x *= DRAG;
        m_velocity.z *= DRAG;
        m_velocity.y += GRAVITY * liquidScale * dT;
        // Lava is buoyant: the player bobs at its surface and can never be
        // pulled under, which reads far better than a red-tinted dive
        if (m_inLava) {
            m_velocity.y = glm::max(m_velocity.y, 4.5f);
        }
        // Cap fall speed so a long drop can't outrun the collision sampler
        m_velocity.y = glm::max(m_velocity.y, -55.f);
    }

    m_velocity += m_acceleration * dT;
    glm::vec3 displacement = m_velocity*dT;

    if(m_flightMode && m_noclip){
        // Spectator-style flight: no terrain collisions at all
        m_position += displacement;
    }


    else{
        // Normal flight and walking both collide with the world, like
        // Minecraft creative - the only ways underground are digging or
        // walking into a cave.
        // If the player is embedded in solid terrain (e.g. leaving noclip
        // inside a hill), pop up to the surface like Minecraft
        // does instead of leaving them wedged and unable to move
        auto solidAt = [&terrain](glm::vec3 q) -> bool {
            if (!terrain.hasChunkAt(glm::floor(q.x), glm::floor(q.z))) return false;
            BlockType b = terrain.getGlobalBlockAt(glm::floor(q.x), glm::floor(q.y), glm::floor(q.z));
            return b != EMPTY && b != WATER && b != LAVA;
        };
        // The whole box must be tested, not just the center column: a
        // teleport or noclip-exit can leave only a corner inside a wall,
        // which would wedge every movement axis at zero
        auto boxEmbedded = [&solidAt](glm::vec3 base) -> bool {
            const float r = 0.29f;
            for (float h : {0.05f, 1.05f, 1.95f}) {
                for (int sx = -1; sx <= 1; sx += 2) {
                    for (int sz = -1; sz <= 1; sz += 2) {
                        if (solidAt(base + glm::vec3(sx * r, h, sz * r))) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        int unstuckGuard = 0;
        while (unstuckGuard++ < 80 && boxEmbedded(m_position)) {
            m_position.y += 1.f;
            m_velocity.y = 0.f;
        }

        // The collision volume is two stacked blocks tall but 0.6 wide like
        // Minecraft's real player box, so the player fits into 1-block
        // holes (digging straight down!) and slides through tight tunnels
        // without snagging. Corners of the box are sampled at 3 heights.
        const float R = 0.3f;
        std::vector<glm::vec3> playerBlockpoints = {
            m_position + glm::vec3( R, 0.f,  R),
            m_position + glm::vec3( R, 1.f,  R),
            m_position + glm::vec3( R, 2.f,  R),
            m_position + glm::vec3(-R, 0.f,  R),
            m_position + glm::vec3(-R, 1.f,  R),
            m_position + glm::vec3(-R, 2.f,  R),
            m_position + glm::vec3( R, 0.f, -R),
            m_position + glm::vec3( R, 1.f, -R),
            m_position + glm::vec3( R, 2.f, -R),
            m_position + glm::vec3(-R, 0.f, -R),
            m_position + glm::vec3(-R, 1.f, -R),
            m_position + glm::vec3(-R, 2.f, -R),
        };
        // maintaining a stuck var coz this will help me slide over walls or obstacles later
        bool stuck = true;
        glm::vec3 computedPosition = m_position; //start with current position

        // gotta check every axis seperately acc to whats mentioned in the instructions
        for (int axis=0; axis<3; axis++){
            // can adjust my resolution based on performance
            float resolution = 0.1f*glm::sign(m_velocity[axis]*dT);
            // projected distance based on vel
            float distanceToTravel = m_velocity[axis]*dT;
            // what I would actually be able to travel
            float actualDistance = m_velocity[axis]*dT;

            for (const auto& point: playerBlockpoints){
                float distanceTravelled = 0.0f;

                while(std::abs(distanceTravelled) < std::abs(distanceToTravel)){
                    glm::vec3 cornerPos = point;
                    cornerPos[axis] += distanceTravelled + resolution; // each axis dealt seperately

                    // if theres no terrain, dont even go ahead and check for collision
                    if (!terrain.hasChunkAt(floor(cornerPos.x), floor(cornerPos.z))){
                        distanceTravelled += resolution;
                        continue;
                    }

                    //the type of block well be at
                    BlockType terrainblock = terrain.getGlobalBlockAt(floor(cornerPos.x), floor(cornerPos.y), floor(cornerPos.z));

                    // Fluids don't block movement - the player swims
                    // through water and lava at reduced speed instead
                    if (terrainblock==EMPTY || terrainblock==WATER || terrainblock==LAVA){
                        distanceTravelled += resolution;
                        continue;
                    }

                    //if collision
                    if(std::abs(distanceTravelled) < std::abs(actualDistance)){
                        // other blocks considered obstacle and hence actual traversable path would just be this
                        actualDistance = distanceTravelled;
                    }
                    break;
                }
            }

            // release my stuck var if i can move
            if (actualDistance != 0) stuck = false;
            else if(axis==1) {
                m_onground =true; // if i touch the ground my bool value turns back to true so I can jump again
                m_velocity.y = 0;
            }
            computedPosition[axis] = m_position[axis] + actualDistance;
            if (axis==1) toMove = actualDistance;
        }
        // (a legacy "nudge upward when wedged" hack lived here; the 0.6-wide
        // box plus the de-embed pass above make it unnecessary, and it made
        // the camera vibrate when pressing into corners)
        (void)stuck;
        // moveForwardLocal(computedPosition.z);
        // moveRightLocal(computedPosition.x);
        // Vertical movement is along the WORLD up axis - the local up tilts
        // with the camera pitch, which used to slow falling and jumping to
        // a crawl whenever the player looked up or down
        m_position.y += toMove;
        m_position.x = computedPosition.x;
        m_position.z = computedPosition.z;
    }
}

// Grid-march a ray from the camera through the crosshair. Left click
// removes the first block hit; right click places a copy of it in the cell
// the ray passed through just before the hit. setGlobalBlockAt re-meshes
// and re-uploads the affected chunks.
BlockType Player::removeAddBlock(bool right, bool left, Terrain &terrain, float shootingRange,
                                 BlockType placeType){
    // can adjust resolution based on performance
    float resolution = 0.2f;
    float distTravelled = 0.f;
    //camera centre of top block
    glm::vec3 cameraDir = glm::normalize(m_forward);
    glm::vec3 position = m_position + glm::vec3(0.f, 1.5f, 0.f);

    //default shooting range is 3 units, but adding a way to increase the range for fun!
    while(distTravelled < shootingRange){
        glm::vec3 floorPos = glm::floor(position);
        if(!terrain.hasChunkAt(floorPos.x, floorPos.z)){
            position += resolution*cameraDir;
            distTravelled += resolution;
            continue;
        }
        BlockType currentBlock = terrain.getGlobalBlockAt(floorPos);
        // Clicks target solid blocks only - the ray passes through fluids,
        // so you can't "break" water or place copies of it
        if (currentBlock!=EMPTY && currentBlock!=WATER && currentBlock!=LAVA){
            // Right-clicking a lever flips it instead of placing a block
            if (right && (currentBlock == LEVER_OFF || currentBlock == LEVER_ON)) {
                terrain.setGlobalBlockAt(floorPos.x, floorPos.y, floorPos.z,
                                         currentBlock == LEVER_OFF ? LEVER_ON : LEVER_OFF);
                return currentBlock;
            }
            if (right){
                //place a block one step before the block identified
                glm::vec3 newBlockPos = glm::floor(position - cameraDir*resolution);
                // Never place a block through the player's head, and only
                // into the feet cell when jumping mostly clear of it - the
                // embedded-in-terrain pop then lands you on top, which is
                // exactly how pillaring up feels in Minecraft
                glm::vec3 feet = glm::floor(m_position);
                glm::vec3 head = glm::floor(m_position + glm::vec3(0.f, 1.f, 0.f));
                bool blockedByPlayer =
                    (newBlockPos == head) ||
                    (newBlockPos == feet && m_position.y - newBlockPos.y <= 0.45f);
                if (!blockedByPlayer) {
                    terrain.setGlobalBlockAt(newBlockPos.x, newBlockPos.y, newBlockPos.z, placeType);
                    return placeType;
                }
            } else if (left && currentBlock != BEDROCK){
                // Bedrock is unbreakable
                terrain.setGlobalBlockAt(floorPos.x, floorPos.y, floorPos.z, EMPTY);
                return currentBlock;
            }
            break;
        }
        distTravelled += resolution;
        position += resolution*cameraDir;
    }
    return EMPTY;
}

bool Player::raycastBlock(const Terrain &terrain, glm::ivec3 &outBlock) const {
    const float resolution = 0.1f;
    glm::vec3 dir = glm::normalize(m_forward);
    glm::vec3 position = m_position + glm::vec3(0.f, 1.5f, 0.f);
    for (float dist = 0.f; dist < shootingRange; dist += resolution) {
        glm::vec3 floorPos = glm::floor(position);
        if (terrain.hasChunkAt(floorPos.x, floorPos.z)) {
            BlockType b = terrain.getGlobalBlockAt(floorPos);
            if (b != EMPTY && b != WATER) {
                outBlock = glm::ivec3(floorPos);
                return true;
            }
        }
        position += resolution * dir;
    }
    return false;
}


void Player::setCameraWidthHeight(unsigned int w, unsigned int h) {
    m_camera.setWidthHeight(w, h);
}

void Player::moveAlongVector(glm::vec3 dir) {
    Entity::moveAlongVector(dir);
    m_camera.moveAlongVector(dir);
}
void Player::moveForwardLocal(float amount) {
    Entity::moveForwardLocal(amount);
    m_camera.moveForwardLocal(amount);
}
void Player::moveRightLocal(float amount) {
    Entity::moveRightLocal(amount);
    m_camera.moveRightLocal(amount);
}
void Player::moveUpLocal(float amount) {
    Entity::moveUpLocal(amount);
    m_camera.moveUpLocal(amount);
}
void Player::moveForwardGlobal(float amount) {
    Entity::moveForwardGlobal(amount);
    m_camera.moveForwardGlobal(amount);
}
void Player::moveRightGlobal(float amount) {
    Entity::moveRightGlobal(amount);
    m_camera.moveRightGlobal(amount);
}
void Player::moveUpGlobal(float amount) {
    Entity::moveUpGlobal(amount);
    m_camera.moveUpGlobal(amount);
}
void Player::rotateOnForwardLocal(float degrees) {
    Entity::rotateOnForwardLocal(degrees);
    m_camera.rotateOnForwardLocal(degrees);
}
void Player::rotateOnRightLocal(float degrees) {
    // Clamp look pitch so the camera can never flip past straight up/down
    float newPitch = std::clamp(m_pitchDegrees + degrees, -89.f, 89.f);
    degrees = newPitch - m_pitchDegrees;
    m_pitchDegrees = newPitch;
    Entity::rotateOnRightLocal(degrees);
    m_camera.rotateOnRightLocal(degrees);
}
void Player::rotateOnUpLocal(float degrees) {
    Entity::rotateOnUpLocal(degrees);
    m_camera.rotateOnUpLocal(degrees);
}
void Player::rotateOnForwardGlobal(float degrees) {
    Entity::rotateOnForwardGlobal(degrees);
    m_camera.rotateOnForwardGlobal(degrees);
}
void Player::rotateOnRightGlobal(float degrees) {
    Entity::rotateOnRightGlobal(degrees);
    m_camera.rotateOnRightGlobal(degrees);
}
void Player::rotateOnUpGlobal(float degrees) {
    Entity::rotateOnUpGlobal(degrees);
    m_camera.rotateOnUpGlobal(degrees);
}

void Player::updatePos(glm::vec3 pos) {
    m_position = pos + glm::vec3(0, 1.5f, 0);
}

QString Player::posAsQString() const {
    std::string str("( " + std::to_string(m_position.x) + ", " + std::to_string(m_position.y) + ", " + std::to_string(m_position.z) + ")");
    return QString::fromStdString(str);
}
QString Player::velAsQString() const {
    std::string str("( " + std::to_string(m_velocity.x) + ", " + std::to_string(m_velocity.y) + ", " + std::to_string(m_velocity.z) + ")");
    return QString::fromStdString(str);
}
QString Player::accAsQString() const {
    std::string str("( " + std::to_string(m_acceleration.x) + ", " + std::to_string(m_acceleration.y) + ", " + std::to_string(m_acceleration.z) + ")");
    return QString::fromStdString(str);
}
QString Player::lookAsQString() const {
    std::string str("( " + std::to_string(m_forward.x) + ", " + std::to_string(m_forward.y) + ", " + std::to_string(m_forward.z) + ")");
    return QString::fromStdString(str);
}
