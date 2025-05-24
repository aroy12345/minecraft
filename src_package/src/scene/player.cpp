#include "player.h"
#include <Qstring>
#include "iostream"
Player::Player(glm::vec3 pos, const Terrain &terrain)
    : Entity(pos), m_velocity(0,0,0), m_acceleration(0,0,0),
    m_camera(pos + glm::vec3(0, 1.5f, 0)), mcr_terrain(terrain),
    mcr_camera(m_camera)
{}

Player::~Player()
{}

void Player::tick(float dT, InputBundle &input) {
    processInputs(input);
    computePhysics(dT, mcr_terrain);
    m_camera.updatePos(m_position);

}


void Player::processInputs(InputBundle &inputs){

    //toggle b/w flightmode and groundmode
    if (inputs.fPressed){
        inputs.fPressed = false;
        m_flightMode = !m_flightMode;
        if (!m_flightMode){
            m_onground = true;
            m_velocity.y = 0;
        }
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
        if (inputs.ePressed){
            m_acceleration += m_up;
        }
        if (inputs.qPressed){
            m_acceleration -= m_up;
        }
    } else{
        //ground mode
        glm::vec3 groundforward = glm::normalize(glm::vec3(m_forward.x, 0.f, m_forward.z));
        glm::vec3 groundright = glm::normalize(glm::vec3(m_right.x, 0.f, m_right.z));

        const float JUMPVEL = 20.f;

        if (inputs.wPressed){
            m_acceleration += groundforward;
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
            if (m_onground==true){
                m_velocity.y = JUMPVEL;
                m_onground=false;
            }
        }
        if(inputs.rPressed){
            shootingRange += 1.f;
        }
    }

}

void Player::computePhysics(float dT, const Terrain &terrain) {
    const float GRAVITY = -9.8f * 4;
    const float DRAG = 0.9f;
    const float accelAmount = 20.f;
    float toMove;

    m_acceleration *= accelAmount;
    m_velocity *= DRAG;

    if (!m_flightMode){
        // if ground apply gravity
        m_velocity.y += GRAVITY * dT;
    }

    m_velocity += m_acceleration * dT;
    glm::vec3 displacement = m_velocity*dT;

    if(m_flightMode){
        // moveForwardGlobal(displacement.z);
        // moveRightGlobal(displacement.x);
        // moveUpGlobal(displacement.y);

        // moveForwardLocal(displacement.z);
        // moveRightLocal(displacement.x);
        // moveUpGlobal(displacement.y);
        m_position += displacement;
    }


    else{
        //player + camera totally is considered to be 2 minecraft blocks stacked on top of each other
        //cross checking against 3 layers of the 2 blocks
        std::vector<glm::vec3> playerBlockpoints = {
            m_position + glm::vec3(0.5f, 0.f, 0.5f), // bottom of bottom block
            m_position + glm::vec3(0.5f, 1.f, 0.5f), // top of bottom / bottom of top block
            m_position + glm::vec3(0.5f, 2.f, 0.5f), // top of top block
            m_position + glm::vec3(-0.5f, 0.f, 0.5f),
            m_position + glm::vec3(-0.5f, 1.f, 0.5f),
            m_position + glm::vec3(-0.5f, 2.f, 0.5f),
            m_position + glm::vec3(0.5f, 0.f, -0.5f),
            m_position + glm::vec3(0.5f, 1.f, -0.5f),
            m_position + glm::vec3(0.5f, 2.f, -0.5f),
            m_position + glm::vec3(-0.5f, 0.f, -0.5f),
            m_position + glm::vec3(-0.5f, 1.f, -0.5f),
            m_position + glm::vec3(-0.5f, 2.f, -0.5f),
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

                    // skip if blocks are empty or water considering i can traverse through them, can also add more blocks here based on terrain
                    if (terrainblock==EMPTY || terrainblock==WATER){
                        distanceTravelled += resolution;
                        continue;
                    }

                    //if collision
                    else if (terrainblock!=EMPTY){
                        if(std::abs(distanceTravelled) < std::abs(actualDistance)){
                            // other blocks considered obstacle and hence actual traversable path would just be this
                            actualDistance = distanceTravelled;
                        }
                        break;
                    }

                    //if no collision, keep moving further
                    distanceTravelled += resolution;
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
        // if stuck slide across the wall until u can actually move
        if (stuck && m_velocity!=glm::vec3(0.f)) toMove += 0.01f;
        // moveForwardLocal(computedPosition.z);
        // moveRightLocal(computedPosition.x);
        moveUpLocal(toMove);
        m_position.x = computedPosition.x;
        m_position.z = computedPosition.z;
    }
}

void Player::removeAddBlock(bool right, bool left, Terrain &terrain, float shootingRange){


    // std::cout<<"Block will be removed or added if it's in 3units distance !!";
    // can adjust resolution based on performance
    float resolution = 0.2f;
    float distTravelled = 0.f;
    //camera centre of top block
    glm::vec3 cameraDir = glm::normalize(m_forward);
    glm::vec3 cameraOrig = m_position + glm::vec3(0.f, 1.5f, 0.f);
    glm::vec3 position = cameraOrig; // start with orig

    //default shooting range is 3 units, but adding a way to increase the range for fun!
    while(distTravelled < shootingRange){
        glm::vec3 floorPos = glm::floor(position);
        if(!terrain.hasChunkAt(floorPos.x, floorPos.z)){
            position += resolution*cameraDir;
            distTravelled += resolution;
            continue;
        }
        BlockType currentBlock = terrain.getGlobalBlockAt(floorPos);
        if (currentBlock!=EMPTY){
            if (right){
                //place a block one step before the block identified
                // std::cout<<"adding block !!";
                glm::vec3 newBlockPos = glm::floor(position - cameraDir*resolution);
                terrain.setGlobalBlockAt(newBlockPos.x, newBlockPos.y, newBlockPos.z, currentBlock);
                // send VBO data
                if (terrain.hasChunkAt(glm::floor(newBlockPos.x / 16) * 16,
                                       glm::floor(newBlockPos.z / 16) * 16
                                       )){
                    terrain.getChunkAt(glm::floor(newBlockPos.x / 16) * 16,
                                       glm::floor(newBlockPos.z / 16) * 16
                                       )->createVBOdata();
                }
            } else if (left){
                // std::cout<<"removing block !!";
                terrain.setGlobalBlockAt(floorPos.x, floorPos.y, floorPos.z, EMPTY);
                //SEND vbo data to update this
                if (terrain.hasChunkAt(glm::floor(floorPos.x / 16) * 16,
                                       glm::floor(floorPos.z / 16) * 16
                                       )){
                    terrain.getChunkAt(glm::floor(floorPos.x / 16) * 16,
                                       glm::floor(floorPos.z / 16) * 16
                                       )->createVBOdata();
                }
            }
            break;
        }
        distTravelled += resolution;
        position += resolution*cameraDir;
    }
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
