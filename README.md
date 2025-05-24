# Mini Minecraft

A sophisticated 3D voxel-based game engine inspired by Minecraft, built with Qt, OpenGL, and C++. This project implements procedural terrain generation, efficient chunk-based rendering, realistic physics, and various gameplay features.

## 🎮 Features

### Core Gameplay
- **First-Person Player Control**: WASD movement with mouse look controls
- **Flight Mode**: Free-flying camera mode with Q/E for vertical movement
- **Ground Mode**: Realistic physics with gravity, collision detection, and jumping
- **Block Interaction**: Left-click to remove blocks, right-click to place blocks
- **Variable Interaction Range**: Press R to increase block interaction range (default: 3 units)

### Advanced Rendering
- **Efficient Chunk-Based Rendering**: Optimized terrain rendering using per-chunk VBOs instead of per-block rendering
- **Texture Mapping**: Comprehensive texture atlas with animated water blocks
- **Transparency Support**: Proper alpha blending for water and transparent blocks
- **Sky Rendering**: Dynamic sky with shader-based rendering
- **Weather System**: Weather effects with particle-based overlays
- **Normal Mapping**: Enhanced visual quality with normal map support

### Procedural World Generation
- **Biome System**: Two distinct biomes with smooth transitions:
  - **Grassland**: Rolling hills using Voronoi noise for organic shapes
  - **Mountains**: Jagged mountain ranges with ridged noise algorithms
- **3D Perlin Noise**: Full gradient-based Perlin noise implementation
- **FBM (Fractal Brownian Motion)**: Multi-octave noise for detailed terrain features
- **Dynamic Terrain**: Infinite world generation as players explore
- **Water Bodies**: Procedural water placement between y-levels 128-138

### Physics & Collision
- **Realistic Player Physics**: Gravity, acceleration, and velocity-based movement
- **Collision Detection**: Multi-point collision detection using ray casting from player corners
- **Ground/Flight Mode Toggle**: Switch between physics-enabled and free-flight modes
- **Sliding Mechanics**: Smooth movement when colliding with terrain

### NPCs & Audio
- **Sheep NPCs**: Animated sheep entities that roam the world
- **Sound System**: Audio integration with sheep sounds and ambient effects
- **Entity Management**: Proper entity spawning and management system

## 🏗️ Technical Architecture

### Rendering Pipeline
- **Interleaved VBO Format**: Optimized vertex data storage (Position, Normal, Color, UV)
- **Instanced Rendering**: Efficient rendering of similar objects
- **Shader Programs**: 
  - Lambert shading for terrain
  - Sky shader for atmospheric effects
  - Weather shader for particle effects
  - Post-processing effects

### Performance Optimizations
- **Frustum Culling**: Only render visible chunks
- **Face Culling**: Only generate geometry for visible block faces
- **Dynamic Loading**: Chunks generated on-demand as players explore
- **Efficient Memory Management**: Smart pointer usage and proper resource cleanup

## 🛠️ Building and Running

### Prerequisites
- Qt 6.x with OpenGL widgets support
- C++17 compatible compiler
- OpenGL 3.3+ compatible graphics card

### Build Instructions

1. **Clone the repository:**
   ```bash
   git clone <repository-url>
   cd mini-minecraft/assignment_package
   ```

2. **Build with Qt:**
   ```bash
   qmake miniMinecraft.pro
   make
   ```

3. **Run the application:**
   ```bash
   ./MiniMinecraft
   ```


## 🎯 Controls

### Movement
- **W/A/S/D**: Move forward/left/backward/right
- **Mouse**: Look around (camera rotation)
- **Space**: Jump (in ground mode)
- **E/Q**: Move up/down (in flight mode only)

### Interaction
- **Left Click**: Remove block (within interaction range)
- **Right Click**: Place block adjacent to targeted surface
- **R**: Increase interaction range by 1 unit

### Modes
- **F**: Toggle between flight and ground mode
- **ESC**: Access game menu



## 🎨 Technical Highlights

### Terrain Generation Algorithm
The terrain system uses a sophisticated multi-noise approach:
1. **Voronoi noise** for organic grassland hills
2. **Ridged Perlin noise** for jagged mountain features  
3. **Large-scale interpolation** for smooth biome transitions
4. **Multi-octave FBM** for fine detail layering

### Rendering Optimizations
- **Chunk-based VBOs**: Reduces draw calls from thousands to dozens
- **Interleaved vertex data**: Optimal GPU memory layout
- **Dynamic LOD**: Chunks generated based on player proximity
- **Transparent rendering pass**: Proper alpha sorting for water blocks



## 🚀 Future Enhancements

- **Multiplayer Support**: Network-based multiplayer functionality
- **Advanced Biomes**: Additional biome types and structures
- **Inventory System**: Item management and crafting
- **Day/Night Cycle**: Dynamic lighting and time progression
- **Particle Effects**: Enhanced visual effects for block breaking/placing
- **Save/Load System**: World persistence functionality

---

*Explore infinite procedurally generated worlds in this feature-rich voxel engine!*