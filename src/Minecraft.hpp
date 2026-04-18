#pragma once
#include <Arduino.h>
#include "Micro3D/Micro3D.hpp"
#include "App.hpp"
#include "InputHandler.hpp"

class Minecraft : public App {
private:
    enum class VoxelType : uint8_t {
        AIR,
        GRASS,
        STONE,
        PLACE_CURSOR,
        DESTROY_CURSOR,
        DIRT,
        BRICKS,
        PLANKS,
        SAND

    };
    VoxelType selectedBlock = VoxelType::STONE;
    Micro3D<uint16_t,int16_t,uint16_t> renderer;
    M3D::Index<uint8_t>* indices = nullptr;
    M3D::Vertex::D3::Textured<int16_t>* vertices = nullptr; 
    M3D::Camera playerCam;
    uint16_t* textureAtlas; 

    unsigned long lastFpsTime = 0;
    int frameCount = 0;

    // === CHUNK 
    const uint8_t CHUNKSIZE = 8;
    const uint8_t CHUNK_VIEW_DISTANCE = 2;
    uint8_t* chunkDatas = nullptr;
    const uint32_t bytesPerChunk = (CHUNKSIZE*CHUNKSIZE*CHUNKSIZE);
    const uint32_t visibleChunks = (CHUNK_VIEW_DISTANCE*2-1 )*(CHUNK_VIEW_DISTANCE*2-1)*(CHUNK_VIEW_DISTANCE*2-1);
    const uint32_t allocChunkBytes = visibleChunks*bytesPerChunk;
    uint8_t gridSide; 
    uint16_t gridPlane;

    const uint32_t renderBufferBytes = 16384;
    const uint32_t renderBufferBytesPerChunk = renderBufferBytes / visibleChunks;
    uint16_t* renderBuffer = nullptr;

    uint16_t* blocksRendered = nullptr;
    
  void createTextureAtlas(){
        textureAtlas = new uint16_t[128 * 16];
       
        for(int y = 0; y < 16; y++) {
           
            // --- GRASS ---
            for(int x = 0; x < 16; x++) {
                uint16_t c = tft.color565(
                    hash1DRange(x, y, 0, 100),   // Low Red
                    hash1DRange(x, y, 150, 255), // High Green (Back to normal!)
                    hash1DRange(x, y, 0, 100)    // Low Blue
                );
                // Swap the bytes before saving!
                textureAtlas[y * 128 + x] = (c >> 8) | (c << 8); 
            }
           
            // --- STONE ---
            for(int x = 16; x < 32; x++) {
                uint16_t grayVal = hash1DRange(x, y, 5, 25);
                uint16_t perfectGray = (grayVal << 11) | (grayVal << 6) | grayVal;
                
                // Swap the bytes before saving!
                textureAtlas[y * 128 + x] = (perfectGray >> 8) | (perfectGray << 8);
            } 
            
            // --- BLOCK PLACEMENT SELECTION ---
            for(int x = 32; x < 48; x++) {
                int locX = x-32;
                if(locX < 4 || locX > 12 || y < 4 || y > 12){
                    textureAtlas[y * 128 + x] = (0 >> 8) | (0 << 8);
                }else{
                    textureAtlas[y * 128 + x] = tft.color565(255,255,255);
                }

                
            }   
            // --- BLOCK DESTRUCTION SELECTION ---
            for(int x = 48; x < 64; x++) {
                int locX = x-48;
                if(locX < 4 || locX > 12 || y < 4 || y > 12){
                    textureAtlas[y * 128 + x] = (0 >> 8) | (0 << 8);
                }else{
                    textureAtlas[y * 128 + x] = tft.color565(0,255,0);
                }
            }
            //DIRT
            for(int x = 64; x < 80; x++) {
                uint8_t shade = hash1DRange(x, y, 0, 80);
                uint16_t c = tft.color565(
                    shade *2,   
                    shade, 
                    0    
                );
                textureAtlas[y * 128 + x] = (c >> 8) | (c << 8); 
            }
          // --- BRICKS ---
            for(int x = 80; x < 96; x++) {
                int lx = x - 80; // Local X from 0 to 15

                // 1. Calculate Mortar Lines
                // Every 4th pixel vertically is a horizontal mortar line
                bool isHorizontalMortar = (y % 4 == 3);
                bool isVerticalMortar = false;
                
                // Offset the vertical mortar lines on every other row
                if ((y / 4) % 2 == 0) { 
                    // Even rows (0 and 2): Mortar at 7 and 15
                    isVerticalMortar = (lx % 8 == 7);
                } else { 
                    // Odd rows (1 and 3): Mortar at 3 and 11
                    isVerticalMortar = ((lx + 4) % 8 == 7);
                }

                bool isMortar = isHorizontalMortar || isVerticalMortar;

                uint16_t c;
                if (isMortar) {
                    // Mortar color: Light gray with a tiny bit of noise
                    uint8_t m = hash1DRange(x, y, 170, 190); 
                    c = tft.color565(m, m, m);
                } else {
                    // Brick color: Reddish-brown with noise
                    int r = hash1DRange(x, y, 160, 210);
                    int g = hash1DRange(x, y, 50, 80);
                    int b = hash1DRange(x, y, 30, 60);
                    
                 
                    bool isTopEdge = (y % 4 == 0);
                    bool isLeftEdge = ((y / 4) % 2 == 0) ? (lx % 8 == 0) : (lx % 8 == 4);
                    
                    if (isTopEdge || isLeftEdge) {
                        r = min(255, r + 40);
                        g = min(255, g + 40);
                        b = min(255, b + 40);
                    }
                    
                   
                    /*
                    bool isBottomEdge = (y % 4 == 2);
                    if(isBottomEdge){
                         r = max(0, r - 30);
                         g = max(0, g - 30);
                         b = max(0, b - 30);
                    }
                    */

                    c = tft.color565(r, g, b);
                }

                // Swap the bytes before saving!
                textureAtlas[y * 128 + x] = (c >> 8) | (c << 8); 
            }

            //PLANKS
            for(int x = 96; x < 112; x++){
                int lx = x - 96;
                if(y %4  == 0){
                    uint8_t shade = hash1DRange(x, y, 0, 99);
                uint16_t c = tft.color565(
                    shade ,  
                    shade* 0.9,  
                    shade / 2    
                );
                textureAtlas[y * 128 + x] = (c >> 8) | (c << 8); 
                }else{
                      uint8_t shade = hash1DRange(x, y, 100, 212);
                uint16_t c = tft.color565(
                    shade ,   
                    shade* 0.9, 
                    shade / 2    
                );
                textureAtlas[y * 128 + x] = (c >> 8) | (c << 8); 
                }
            }
            //SAND
            for(int x = 112; x < 128; x++) {
                uint8_t shade = hash1DRange(x, y, 100, 255);
                uint16_t c = tft.color565(
                    shade ,   
                    shade, 
                    shade / 3    
                );
                textureAtlas[y * 128 + x] = (c >> 8) | (c << 8); 
            }
        }
    }

    void setupRenderingEngine(){
      renderer.setContext(tft, 0, 0, 120, 120);
      renderer.setClearColor(tft.color565(100, 100, 255));
      renderer.enableFrameBuffer();
      renderer.enableDepthTest();
      renderer.enableDoubleBuffering();
      renderer.setUpscaling(tft.height(),tft.height(),false);
      renderer.syncDisplay();
      renderer.clearColor();
      renderer.display();
    }
    
    struct VoxelRenderData{
      int8_t x;
      int8_t y;
      int8_t z;

    };
    void setupChunks(){
      if(chunkDatas == nullptr){
        chunkDatas = new uint8_t[allocChunkBytes]();
      }
    }
    void setupRenderBuffer(){
      if(renderBuffer == nullptr){
        renderBuffer = new uint16_t[renderBufferBytes / sizeof(uint16_t)]();
        blocksRendered = new uint16_t[visibleChunks]();
      }
    }
    
    uint16_t packRenderData(uint16_t val, bool b1, bool b2, bool b3, bool b4, bool b5, bool b6, bool b7) {
      // Mask val to 9 bits (0x1FF) just in case it's larger
      uint16_t result = (val & 0x1FF); 
      
      // Shift booleans into bits 9 through 15
      if (b1) result |= (1 << 9);
      if (b2) result |= (1 << 10);
      if (b3) result |= (1 << 11);
      if (b4) result |= (1 << 12);
      if (b5) result |= (1 << 13);
      if (b6) result |= (1 << 14);
      if (b7) result |= (1 << 15);
      
      return result;
    }
    



    uint16_t convertCoordinateToIndex(uint8_t x, uint8_t y, uint8_t z) {
      
        return x | (y << 3) | (z << 6); 
    }

    void convertIndexToCoordinate(uint16_t index, uint8_t &x, uint8_t &y, uint8_t &z) {
        x = index & 7;          // Bits 0, 1, 2
        y = (index >> 3) & 7;   // Bits 3, 4, 5
        z = (index >> 6) & 7;   // Bits 6, 7, 8
    }

   
void buildRenderBuffer(uint8_t chunkID, uint32_t starterRenderBufIdx, uint32_t endRenderBufIdx){
      uint32_t starterIdx = (uint32_t) chunkID * bytesPerChunk;
      uint16_t localRenderBufferCount = 0;
      
      // 1. Calculate 3D Chunk Grid Coordinates (for boundary checking)
      int32_t gx = chunkID % gridSide;
      int32_t gy = (chunkID / gridSide) % gridSide;
      int32_t gz = chunkID / gridPlane;

      // 2. Pre-calculate Adjacent Chunk Base Indices (keeps the hot loop fast)
      uint32_t leftChunkIdx   = (chunkID - 1) * bytesPerChunk;
      uint32_t rightChunkIdx  = (chunkID + 1) * bytesPerChunk;
      uint32_t topChunkIdx    = (chunkID - gridSide) * bytesPerChunk;
      uint32_t bottomChunkIdx = (chunkID + gridSide) * bytesPerChunk;
      uint32_t frontChunkIdx  = (chunkID - gridPlane) * bytesPerChunk;
      uint32_t backChunkIdx   = (chunkID + gridPlane) * bytesPerChunk;

      for(uint8_t x = 0; x < CHUNKSIZE; x++){
        for(uint8_t y = 0; y < CHUNKSIZE; y++){
          for(uint8_t z = 0; z < CHUNKSIZE; z++){
            uint16_t locIdx = convertCoordinateToIndex(x,y,z);
            uint32_t globIdx = starterIdx + locIdx;
            
            VoxelType type = (VoxelType)chunkDatas[globIdx];
            
            if(type != VoxelType::AIR){
              bool hasFront = false;
              bool hasBack = false;
              bool hasLeft = false;
              bool hasRight = false;
              bool hasTop = false;
              bool hasBottom = false;
              
              bool isEdge = (x == 0 || x == 7 || y == 0 || y == 7 || z == 0 || z == 7);

              if (!isEdge) {
                
                hasTop    = chunkDatas[globIdx - 8]  == 0; // -8 (Decrease Y -> Top)
                hasBottom = chunkDatas[globIdx + 8]  == 0; // +8 (Increase Y -> Bottom)
                hasFront  = chunkDatas[globIdx - 64] == 0; // -64 (Decrease Z -> Front)
                hasBack   = chunkDatas[globIdx + 64] == 0; // +64 (Increase Z -> Back)
                hasRight  = chunkDatas[globIdx + 1]  == 0;  // +1 (Increase X -> Right)
                hasLeft   = chunkDatas[globIdx - 1]  == 0; // -1 (Decrease X ->Left)
              } else {
                // --- CROSS-CHUNK EDGE CULLING ---
                
                // Check Top (y == 0)
                if (y == 0) {
                    // If a top chunk exists, check its bottom block (y=7)
                    if (gy > 0) hasTop = chunkDatas[topChunkIdx + convertCoordinateToIndex(x, 7, z)] == 0;
                    else hasTop = true; // No chunk above, draw the face
                } else {
                    hasTop = chunkDatas[globIdx - 8] == 0;
                }

                // Check Bottom (y == 7)
                if (y == 7) {
                    // If a bottom chunk exists, check its top block (y=0)
                    if (gy < gridSide - 1) hasBottom = chunkDatas[bottomChunkIdx + convertCoordinateToIndex(x, 0, z)] == 0;
                    else hasBottom = true;
                } else {
                    hasBottom = chunkDatas[globIdx + 8] == 0;
                }

                // Check Front (z == 0)
                if (z == 0) {
                    // If a front chunk exists, check its back block (z=7)
                    if (gz > 0) hasFront = chunkDatas[frontChunkIdx + convertCoordinateToIndex(x, y, 7)] == 0;
                    else hasFront = true;
                } else {
                    hasFront = chunkDatas[globIdx - 64] == 0;
                }

                // Check Back (z == 7)
                if (z == 7) {
                    // If a back chunk exists, check its front block (z=0)
                    if (gz < gridSide - 1) hasBack = chunkDatas[backChunkIdx + convertCoordinateToIndex(x, y, 0)] == 0;
                    else hasBack = true;
                } else {
                    hasBack = chunkDatas[globIdx + 64] == 0;
                }

                // Check Left (x == 0)
                if (x == 0) {
                    // If a left chunk exists, check its right block (x=7)
                    if (gx > 0) hasLeft = chunkDatas[leftChunkIdx + convertCoordinateToIndex(7, y, z)] == 0;
                    else hasLeft = true;
                } else {
                    hasLeft = chunkDatas[globIdx - 1] == 0;
                }

                // Check Right (x == 7)
                if (x == 7) {
                    // If a right chunk exists, check its left block (x=0)
                    if (gx < gridSide - 1) hasRight = chunkDatas[rightChunkIdx + convertCoordinateToIndex(0, y, z)] == 0;
                    else hasRight = true;
                } else {
                    hasRight = chunkDatas[globIdx + 1] == 0;
                }
              }

              if(localRenderBufferCount >= endRenderBufIdx){
                blocksRendered[chunkID] = localRenderBufferCount;
                return;
              }
              
              if(hasLeft || hasRight || hasTop || hasBottom || hasBack || hasFront){
                renderBuffer[starterRenderBufIdx + localRenderBufferCount] = packRenderData(locIdx, hasLeft, hasRight, hasTop, hasBottom, hasBack, hasFront, false);
                localRenderBufferCount++;
              }
            }
          }
        }
      }
      blocksRendered[chunkID] = localRenderBufferCount;
    }
    void buildMesh(){
       vertices = new M3D::Vertex::D3::Textured<int16_t>[24]; 
      
      // Standard Axis-Aligned Cube (50x50x50)
      // Centered at X=0, Y=0. Z depth goes from 100 to 150.
      // Format: {X, Y, Z, U, V}
      int8_t rawData[24][5] = {
          // FRONT FACE (Z = 100, Facing Camera)
          {-25, -25, -25,  1,  1 },  // 0: Top-Left
          {-25,  25, -25,  1, 16 },  // 1: Bottom-Left
          { 25,  25, -25, 15, 15 },  // 2: Bottom-Right
          { 25, -25, -25, 15,  1 },  // 3: Top-Right

          // BACK FACE (Z = 25, Facing Away)
          { 25, -25, 25,  1,  1 },  // 4: Top-Left (from back perspective)
          { 25,  25, 25,  1, 15 },  // 5: Bottom-Left
          {-25,  25, 25, 15, 15 },  // 6: Bottom-Right
          {-25, -25, 25, 15,  1 },  // 7: Top-Right

          // TOP FACE (Y = -25)
          {-25, -25, 25,  1,  1 },  // 8: Back-Left
          {-25, -25, -25,  1, 15 },  // 9: Front-Left
          { 25, -25, -25, 15, 15 },  // 10: Front-Right
          { 25, -25, 25, 15,  1 },  // 11: Back-Right

          // BOTTOM FACE (Y = 25)
          {-25,  25, -25,  1,  1 },  // 12: Front-Left
          {-25,  25, 25,  1, 15 },  // 13: Back-Left
          { 25,  25, 25, 15, 15 },  // 14: Back-Right
          { 25,  25, -25, 15,  1 },  // 15: Front-Right

          // LEFT FACE (X = -25)
          {-25, -25, 25,  1,  1 },  // 16: Back-Top
          {-25,  25, 25,  1, 15 },  // 17: Back-Bottom
          {-25,  25, -25, 15, 15 },  // 18: Front-Bottom
          {-25, -25, -25, 15,  1 },  // 19: Front-Top

          // RIGHT FACE (X = 25)
          { 25, -25, -25,  1,  1 },  // 20: Front-Top
          { 25,  25, -25,  1, 15 },  // 21: Front-Bottom
          { 25,  25, 25, 15, 15 },  // 22: Back-Bottom
          { 25, -25, 25, 15,  1 }   // 23: Back-Top
      };

      for(int i = 0; i < 24; i++) {
          vertices[i].x = rawData[i][0];
          vertices[i].y = rawData[i][1];
          vertices[i].z = rawData[i][2];
          vertices[i].u = rawData[i][3];
          vertices[i].v = rawData[i][4];
      }
      
      // --- SETUP THE INDICES ---
      indices = new M3D::Index<uint8_t>[36]; 
      
    
      uint8_t rawIndices[36] = {
          0,1,2,    0,2,3,       // Front Face
          4,5,6,    4,6,7,       // Back Face
          8,9,10,   8,10,11,     // Top Face
          12,13,14, 12,14,15,    // Bottom Face
          16,17,18, 16,18,19,    // Left Face
          20,21,22, 20,22,23     // Right Face
      };

      for(int i = 0; i < 36; i+=9) {
        indices[i].i = rawIndices[i];
        indices[i+1].i = rawIndices[i+1];
        indices[i+2].i = rawIndices[i+2];
        indices[i+3].i = rawIndices[i+3];
        indices[i+4].i = rawIndices[i+4];
        indices[i+5].i = rawIndices[i+5];
        indices[i+6].i = rawIndices[i+6];
        indices[i+7].i = rawIndices[i+7];
        indices[i+8].i = rawIndices[i+8];
      }

    }

    // Returns the chunkID (0 to 26) in the x of the vector and returns the localIndex in the y of the vector
    M3D::Vec2<int32_t> getChunkAndBlockIndex(int32_t worldX, int32_t worldY, int32_t worldZ) {
        // 1. Define the sizes in world units
        const int32_t BLOCK_SIZE = 50;
        const int32_t CHUNK_WORLD_SIZE = CHUNKSIZE * BLOCK_SIZE; 

        // 2. Calculate the global chunk coordinates
    
        int32_t globalChunkX = (int32_t)floor((float)worldX / CHUNK_WORLD_SIZE);
        int32_t globalChunkY = (int32_t)floor((float)worldY / CHUNK_WORLD_SIZE);
        int32_t globalChunkZ = (int32_t)floor((float)worldZ / CHUNK_WORLD_SIZE);

        // 3. Convert to your loaded 3x3x3 grid (gx, gy, gz)

        int32_t gx = globalChunkX + (CHUNK_VIEW_DISTANCE - 1);
        int32_t gy = globalChunkY + (CHUNK_VIEW_DISTANCE - 1);
        int32_t gz = globalChunkZ + (CHUNK_VIEW_DISTANCE - 1);

        // 4. Bounds Check
        if (gx < 0 || gx >= gridSide || 
            gy < 0 || gy >= gridSide || 
            gz < 0 || gz >= gridSide) {
            return {-1,-1}; // Out of bounds!
        }

   
        int32_t chunkID = gx + (gy * gridSide) + (gz * gridPlane);

        // --- CALCULATE LOCAL VOXEL INDEX ---
        // Find the absolute block coordinate (-infinity to +infinity)
        int32_t absoluteVoxelX = (int32_t)floor((float)worldX / BLOCK_SIZE);
        int32_t absoluteVoxelY = (int32_t)floor((float)worldY / BLOCK_SIZE);
        int32_t absoluteVoxelZ = (int32_t)floor((float)worldZ / BLOCK_SIZE);

       
        uint8_t localX = absoluteVoxelX & (CHUNKSIZE - 1);
        uint8_t localY = absoluteVoxelY & (CHUNKSIZE - 1);
        uint8_t localZ = absoluteVoxelZ & (CHUNKSIZE - 1);

        // Pack it into optimized 1D index
        int32_t outLocalIndex = convertCoordinateToIndex(localX, localY, localZ);

        return {chunkID,outLocalIndex};
    }

    bool menuOpen = false;
    bool dirtyMenu = false;
    uint32_t menuSelectedOption = 0;
  public:
    Minecraft() : App(3) {} 

      bool handleMenuButton() override {
        menuOpen = !menuOpen;
        
        if(menuOpen){
            renderer.pauseDisplayTask();
            delay(10);
            tft.fillScreen(tft.color565(52, 103, 235));
            tft.fillRect(0, 0, 320, 30, tft.color565(24, 219, 219)); 
            tft.setTextColor(TFT_WHITE);
            tft.setTextSize(2);
            tft.setCursor(65, 8);
            tft.print("Game menu");
            dirtyMenu = true;
        }else{
            renderer.resumeDisplayTask();
            delay(10);
        }
        return true;
    }

    void start() override {
        gridSide = (CHUNK_VIEW_DISTANCE * 2 - 1); 
        gridPlane = gridSide * gridSide;           
        tft.fillScreen(TFT_WHITE);
    
        setupRenderingEngine();

        createTextureAtlas();
        renderer.bindTexture565(textureAtlas, 128, 16);

        // --- SETUP THE RAW 3D GEOMETRY ---
        buildMesh();
        setupChunks();
        setupRenderBuffer();
        if (!readSDArray("/worldData.bin", chunkDatas, allocChunkBytes)) {
            for(uint32_t chunkID = 0; chunkID < 27;chunkID++){
                int32_t gx = chunkID % gridSide;
                int32_t gy = (chunkID / gridSide) % gridSide;
                int32_t gz = chunkID / gridPlane;

                if(gy == 2){
                    for(uint cx = 0; cx < CHUNKSIZE;cx++){
                        for(uint cz = 0; cz < CHUNKSIZE;cz++){
                            uint16_t locID = convertCoordinateToIndex(cx,0,cz);
                            chunkDatas[(chunkID * bytesPerChunk) + locID] = (uint8_t)VoxelType::GRASS;
                            for(uint cy = 1; cy < CHUNKSIZE;cy++){
                                locID = convertCoordinateToIndex(cx,cy,cz);
                                chunkDatas[(chunkID * bytesPerChunk) + locID] = (uint8_t)VoxelType::STONE;
                                
                            }
                        }
                    }
                }
                
            }
        }
        for(uint32_t i = 0; i < 27;i++){
            buildRenderBuffer(i,renderBufferBytesPerChunk / 2 * i,renderBufferBytesPerChunk / 2);
        }
        
        renderer.setFaceCulling(FaceCulling::BACK);

        Serial.println("=== MEMORY REPORT ===");
        
        // 1. Check Heap (Global dynamic memory)
        Serial.print("Free Heap: ");
        Serial.print(ESP.getFreeHeap());
        Serial.println(" bytes");

     
        Serial.print("Largest Free Block: ");
        Serial.print(ESP.getMaxAllocHeap());
        Serial.println(" bytes");

    
        uint32_t freeStack = uxTaskGetStackHighWaterMark(NULL) * 4;
        Serial.print("Free Stack (Watermark): ");
        Serial.print(freeStack);
        Serial.println(" bytes");
        Serial.println("=====================");
        lastFpsTime = millis();

      
    }
    float playerX,playerY,playerZ;
    float playerXVel,playerYVel,playerZVel;

    bool isGrounded = false; // Tracks if the player is touching the floor

    // Checks if a bounding box overlaps with ANY solid voxel in the world
    bool checkVoxelCollision(float testX, float testY, float testZ, float w, float h, float d) {
        // 1. Find the absolute min/max bounds of the player's bounding box
        float minX = testX - (w / 2.0f);
        float maxX = testX + (w / 2.0f);
        float minY = testY - (h / 2.0f); 
        float maxY = testY + (h / 2.0f);
        float minZ = testZ - (d / 2.0f);
        float maxZ = testZ + (d / 2.0f);

        // 2. Snap the min/max coordinates to the 50-unit block grid
        // round() perfectly aligns our floating point bounds to the block centers!
        int32_t startX = round(minX / 50.0f) * 50;
        int32_t endX   = round(maxX / 50.0f) * 50;
        int32_t startY = round(minY / 50.0f) * 50;
        int32_t endY   = round(maxY / 50.0f) * 50;
        int32_t startZ = round(minZ / 50.0f) * 50;
        int32_t endZ   = round(maxZ / 50.0f) * 50;

        // 3. Loop through every block that touches our player's hitbox
        for (int32_t bx = startX; bx <= endX; bx += 50) {
            for (int32_t by = startY; by <= endY; by += 50) {
                for (int32_t bz = startZ; bz <= endZ; bz += 50) {
                    
                    // Grab the exact chunk and local index
                    M3D::Vec2<int32_t> chunkLoc = getChunkAndBlockIndex(bx, by, bz);
                    
                    if (chunkLoc.x != -1) { // If it exists in loaded RAM
                        uint32_t index = (chunkLoc.x * bytesPerChunk) + chunkLoc.y;
                        if (chunkDatas[index] != (uint8_t)VoxelType::AIR) {
                            return true; // Hit a solid block!
                        }
                    }
                }
            }
        }
        return false; // Free space!
    }

    template <typename T, typename A>
    void doPhysicsCollisions(T& x, T& y, T& z, A& xVel, A& yVel, A& zVel, T hitboxWidth, T hitboxHeight, T hitboxDepth, float deltaTime) {
        
        // 1. APPLY GRAVITY
     
        float gravity = 91.0f; 
        yVel += gravity * deltaTime; 
        

        if (yVel > 250.0f) yVel = 250.0f; 

        // 2. SEPARATE AXIS COLLISION
        
        // --- X AXIS ---
        if (xVel != 0.0f) {
            if (!checkVoxelCollision(x + xVel, y, z, hitboxWidth, hitboxHeight, hitboxDepth)) {
                x += xVel; // Move is safe
            } else {
                xVel = 0;  // Hit a wall, stop moving in X
            }
        }

        // --- Y AXIS (Falling / Jumping) ---
        if (yVel != 0.0f) {
            if (!checkVoxelCollision(x, y + yVel, z, hitboxWidth, hitboxHeight, hitboxDepth)) {
                y += yVel;
                isGrounded = false; //  are in the air
            } else {
                //  hit something vertically!
                if (yVel > 0) {
                    isGrounded = true; //  landed on a floor
                }
                yVel = 0; //  falling/jumping
            }
        }

        // --- Z AXIS ---
        if (zVel != 0.0f) {
            if (!checkVoxelCollision(x, y, z + zVel, hitboxWidth, hitboxHeight, hitboxDepth)) {
                z += zVel;
            } else {
                zVel = 0; // Hit a wall, stop moving in Z
            }
        }

       
        xVel = 0;
        zVel = 0;
    }

    void updateBlock(uint32_t dataIndex, uint32_t chunkID, uint32_t localIndex){
        // 1. ALWAYS update the chunk where the block was modified
        uint32_t elementsPerChunk = renderBufferBytesPerChunk / 2;
        buildRenderBuffer(chunkID, chunkID * elementsPerChunk, elementsPerChunk);

        // 2. Decode the 1D local index back into 3D chunk coordinates (0 to 7)
        uint8_t vx, vy, vz;
        convertIndexToCoordinate(localIndex, vx, vy, vz);

        // 3. Find where this chunk lives in the 3x3x3 loaded grid (0, 1, or 2)
        int32_t gx = chunkID % gridSide;
        int32_t gy = (chunkID / gridSide) % gridSide;
        int32_t gz = chunkID / gridPlane;

        // --- 4. CHECK BOUNDARIES AND UPDATE NEIGHBORS ---
        
        // Check X-Axis (Left / Right)
        if (vx == 0 && gx > 0) {
            // Block is on the LEFT edge. Update the chunk to the left.
            uint32_t leftChunk = chunkID - 1;
            buildRenderBuffer(leftChunk, leftChunk * elementsPerChunk, elementsPerChunk);
        } else if (vx == 7 && gx < gridSide - 1) {
            // Block is on the RIGHT edge. Update the chunk to the right.
            uint32_t rightChunk = chunkID + 1;
            buildRenderBuffer(rightChunk, rightChunk * elementsPerChunk, elementsPerChunk);
        }

        // Check Y-Axis (Top / Bottom)
    
        if (vy == 0 && gy > 0) {
            // Block is on the TOP edge. Update the chunk above.
            uint32_t topChunk = chunkID - gridSide;
            buildRenderBuffer(topChunk, topChunk * elementsPerChunk, elementsPerChunk);
        } else if (vy == 7 && gy < gridSide - 1) {
            // Block is on the BOTTOM edge. Update the chunk below.
            uint32_t bottomChunk = chunkID + gridSide;
            buildRenderBuffer(bottomChunk, bottomChunk * elementsPerChunk, elementsPerChunk);
        }

        // Check Z-Axis (Front / Back)
        if (vz == 0 && gz > 0) {
            // Block is on the FRONT edge. Update the chunk in front.
            uint32_t frontChunk = chunkID - gridPlane;
            buildRenderBuffer(frontChunk, frontChunk * elementsPerChunk, elementsPerChunk);
        } else if (vz == 7 && gz < gridSide - 1) {
            // Block is on the BACK edge. Update the chunk behind.
            uint32_t backChunk = chunkID + gridPlane;
            buildRenderBuffer(backChunk, backChunk * elementsPerChunk, elementsPerChunk);
        }
    }

    int run(InputHandler& in) override {
        if(menuOpen){
            if(dirtyMenu){
                tft.fillRect(20, 40, 280, 200, tft.color565(52, 103, 235)); 
                switch(menuSelectedOption){
                    case 0:{
                        tft.fillRoundRect(10, 60, 220, 70,8, tft.color565(36, 255, 54)); 
                        break;
                    }
                    case 1:{
                        tft.fillRoundRect(10, 140, 220, 70,8, tft.color565(36, 255, 54)); 
                        break;
                    }
                }
                tft.fillRect(20, 70, 200, 50, tft.color565(235, 64, 52)); 
                tft.setTextColor(TFT_WHITE);
                tft.setTextSize(2);
                tft.setCursor(30, 80);
                tft.print("Place: ");
                switch(selectedBlock){ //Printing out what type of block or destroy mode is selected
                    case VoxelType::AIR:{
                        tft.fillRect(30,80,30,20, tft.color565(52, 103, 235)); //Erase "Place: "
                        tft.print("Destroy ");
                        break;
                    }
                    
                    case VoxelType::STONE:{
                        tft.print("  STONE");
                        break;
                    }
                    
                    case VoxelType::GRASS:{
                        tft.print("  GRASS");
                        break;
                    } 
                    
                    case VoxelType::PLACE_CURSOR:{
                        tft.print("  PLACE_CURSOR");
                        break;
                    }
                    case VoxelType::DESTROY_CURSOR:{
                        tft.print("  DESTROY_CURSOR");
                        break;
                    }
                    case VoxelType::DIRT:{
                        tft.print("  DIRT");
                        break;
                    }
                    
                    case VoxelType::BRICKS:{
                        tft.print("  BRICKS");
                        break;
                    }
                    
                    case VoxelType::PLANKS:{
                        tft.print("  PLANKS");
                        break;
                    }
                    
                    case VoxelType::SAND:{
                        tft.print("  SAND");
                        break;
                    }
                }

                tft.fillRect(20, 150, 100, 50, tft.color565(235, 64, 52)); 
                tft.setTextColor(TFT_WHITE);
                tft.setTextSize(2);
                tft.setCursor(30, 160);
                tft.print("  EXIT");
                dirtyMenu = false;
            }
            delay(20);

            if(in.joyBtnPressed && menuSelectedOption == 1){
                exit = true;
            }

            if(in.btn3Pressed){
                menuSelectedOption++;
                dirtyMenu = true;
            }
            if(in.btn1Pressed){
                menuSelectedOption--;
                dirtyMenu = true;
            }

            if(menuSelectedOption > 1){
                menuSelectedOption = 1;
            }else if(menuSelectedOption < 0){
                menuSelectedOption = 0;
            }

            if(menuSelectedOption == 0){
                if(in.btn4Pressed){
                    selectedBlock = (VoxelType)((uint8_t)selectedBlock + 1);
                    dirtyMenu = true;
                }
                if(in.btn2Pressed){
                    if(selectedBlock == (VoxelType)0){
                        selectedBlock = (VoxelType)8;
                        dirtyMenu = true;
                    }else{
                    selectedBlock = (VoxelType)((uint8_t)selectedBlock - 1);
                    dirtyMenu = true;
                    }
                }
                selectedBlock = (VoxelType)((uint8_t)selectedBlock %9);
            }
        }else{

            renderer.clearColor();
            renderer.clearDepth(); 
            float sinY = sin(playerCam.rotY);
            float cosY = cos(playerCam.rotY);
            
           
            float moveSpeed = 240 *in.deltaTime; 

            //  Forward / Backward (Local Z axis)
            if(in.btn1Down){ // Move Forward
                playerXVel += (float)(sinY * moveSpeed);
                playerZVel += (float)(cosY * moveSpeed);
            }  
            if(in.btn3Down && isGrounded){ // Jump
                playerYVel = -30.0f; 
            }  
            
            //  Strafe Left / Right (Local X axis)
           
            if(in.btn2Down){ // Strafe Left
                playerXVel -= (float)(cosY * moveSpeed);
                playerZVel += (float)(sinY * moveSpeed);
            }  
            if(in.btn4Down){ // Strafe Right
                playerXVel += (float)(cosY * moveSpeed);
                playerZVel -= (float)(sinY * moveSpeed);
            }
            playerCam.y += 50;

            float fY = playerCam.y;

            doPhysicsCollisions(playerCam.x, fY, playerCam.z, playerXVel, playerYVel, playerZVel, 30.0f, 85.0f, 30.0f, in.deltaTime);

            playerCam.y = fY-50;
     
            int joyCenter = 1850; 
            int deadzone = 400;

         
            float maxOffset = 2000.0f; 

            //  Calculate raw offsets
            float offsetX = (float)(in.joyX - joyCenter);
            float offsetY = (float)(in.joyY - joyCenter);

            // Base sensitivity: Maximum radians per second the camera can turn at full tilt
            float maxTurnSpeed = 2.5f; 

            // Helper function to process an axis
            auto processAxis = [deadzone, maxOffset](float offset) {
                float absOffset = abs(offset);

                // 1. Check deadzone
                if (absOffset <= deadzone) return 0.0f;

                // 2. Smooth Deadzone Rescaling
                // This maps the raw value (e.g., 400 to 2000) to a clean 0.0 to 1.0 range
                float normalized = (absOffset - deadzone) / (maxOffset - deadzone);

                // Clamp to 1.0 in case the joystick physically pushes slightly past maxOffset
                if (normalized > 1.0f) normalized = 1.0f;

                // 3. Apply the Cubic Curve (x^3) for precise inner control
                float curved = normalized * normalized * normalized;

                // 4. Reapply the sign (positive/negative direction)
                return (offset > 0) ? curved : -curved;
            };

            //  Get normalized, curved input (-1.0 to 1.0)
            float finalInputX = processAxis(offsetX);
            float finalInputY = processAxis(offsetY);

            //  Apply the rotation safely

            if (finalInputX != 0.0f) {
                playerCam.rotY -= finalInputX * maxTurnSpeed * in.deltaTime;
            }

            if (finalInputY != 0.0f) {
                playerCam.rotX += finalInputY * maxTurnSpeed * in.deltaTime;
            }
                

        

            renderer.calculateCameraMatrices(playerCam);
            renderer.handleFrameBuffer();

          
            using M3D_Data = decltype(renderer)::DrawData<uint8_t>;

   
            M3D_Data drawDat = {vertices, indices, 0, 36, playerCam};



            for(uint16_t chunkID = 0; chunkID < visibleChunks; chunkID++) {
            // 1. Extract Local Grid Coordinates (0, 1, 2)
            int32_t gx = chunkID % gridSide;
            int32_t gy = (chunkID / gridSide) % gridSide;
            int32_t gz = chunkID / gridPlane;

            // 2. Convert Grid Coordinates to World Coordinates
            // Center the grid so the player is in the middle chunk
            int32_t worldX = (gx - (CHUNK_VIEW_DISTANCE -1)) * (CHUNKSIZE * 50); 
            int32_t worldY = (gy - (CHUNK_VIEW_DISTANCE -1)) * (CHUNKSIZE * 50);
            int32_t worldZ = (gz - (CHUNK_VIEW_DISTANCE -1)) * (CHUNKSIZE * 50);
            if(renderer.isPointVisible(M3D::Vec3<int32_t>{worldX,worldY,worldZ},CHUNKSIZE*150,playerCam)){

                // 3. Set the transform for this specific chunk
        
                uint16_t startIdx = chunkID * (renderBufferBytesPerChunk / sizeof(uint16_t));
                uint16_t endIdx = startIdx + blocksRendered[chunkID];

                for(uint16_t r = startIdx; r < endIdx; r++) {
                uint16_t render = renderBuffer[r]; // the packed data.
                uint16_t locIdx = render & 0x1FF;    // Mask first 9 bits

                uint32_t globIdx = (uint32_t)chunkID *bytesPerChunk + (uint32_t)locIdx;

                VoxelType type = (VoxelType)chunkDatas[globIdx];

                uint8_t flags = render >> 9;        // Shift out the 7 boolean bits
                
                // 2. Get 3D coords from packed index
                uint8_t vx, vy, vz;
                convertIndexToCoordinate(locIdx, vx, vy, vz);

                // 3. Set world transform for this voxel

                renderer.setTransform(worldX + (vx * 50), worldY + (vy * 50), worldZ + (vz * 50));
                renderer.setUVOffset(((uint8_t)(type)-1)*16,0);
                drawDat.indexCount = 6;
                // 4. Draw ONLY the visible faces using bitmasking
   
                if (flags & (1 << 0)) {drawDat.indexStart = 24;renderer.drawProjectedMesh<uint8_t>(&drawDat);}
                if (flags & (1 << 1)) {drawDat.indexStart = 30;renderer.drawProjectedMesh<uint8_t>(&drawDat);}
                if (flags & (1 << 2)) {drawDat.indexStart = 12;renderer.drawProjectedMesh<uint8_t>(&drawDat);}
                if (flags & (1 << 3)) {drawDat.indexStart = 18;renderer.drawProjectedMesh<uint8_t>(&drawDat);}
                if (flags & (1 << 4)) {drawDat.indexStart = 6;renderer.drawProjectedMesh<uint8_t>(&drawDat);}
                if (flags & (1 << 5)) {drawDat.indexStart = 0;renderer.drawProjectedMesh<uint8_t>(&drawDat);}
                }
            }
        
            }
            uint32_t zMid = renderer.getzBufferMiddle(); // The closest distance where's a block where the camera is looking.

      
            if (zMid > 0 && zMid < 65000) { 
                
                // 2. Calculate the camera's true forward vector in 3D space
                float pitch = playerCam.rotX;
                float yaw   = playerCam.rotY;

                float dirX = sin(yaw) * cos(pitch);
                float dirY = sin(pitch);
                float dirZ = cos(yaw) * cos(pitch);

                // 3. Find the exact surface point the camera is looking at
                float hitX = playerCam.x + (dirX * zMid*8);
                float hitY = playerCam.y + (dirY * zMid*8);
                float hitZ = playerCam.z + (dirZ * zMid*8);

              
                float placeX = hitX;
                float placeY = hitY;
                float placeZ = hitZ;
                switch(selectedBlock){ // Shift the cursor to be IN a block or ON a block
                    case VoxelType::AIR:{
                        placeX += (dirX * 26.0f);
                        placeY += (dirY * 26.0f);
                        placeZ += (dirZ * 26.0f);
                        break;
                    }
                    default:{
                        placeX -= (dirX * 26.0f);
                        placeY -= (dirY * 26.0f);
                        placeZ -= (dirZ * 26.0f);
                        break;
                    }
                }

           
                int32_t blockWorldX = round(placeX / 50.0f) * 50;
                int32_t blockWorldY = round(placeY / 50.0f) * 50;
                int32_t blockWorldZ = round(placeZ / 50.0f) * 50;

              
                M3D::Vec2<int32_t> chunkAndLocInd = getChunkAndBlockIndex(blockWorldX, blockWorldY, blockWorldZ);
                uint32_t index = (chunkAndLocInd.x * (bytesPerChunk)) + chunkAndLocInd.y;

                switch(selectedBlock){
                    case VoxelType::AIR:{
                        renderer.setTransform(blockWorldX, blockWorldY, blockWorldZ);
                        renderer.setUVOffset((3)*16,0);
                        drawDat.indexCount = 36;
                        drawDat.indexStart = 0;
                        renderer.drawProjectedMesh<uint8_t>(&drawDat);
                        break;
                    }
                    default:{
                        renderer.setTransform(blockWorldX, blockWorldY, blockWorldZ);
                        renderer.setUVOffset((2)*16,0);
                        drawDat.indexCount = 36;
                        drawDat.indexStart = 0;
                        renderer.drawProjectedMesh<uint8_t>(&drawDat);
                        break;
                    }
                }

                
                if (in.joyBtnPressed) { 
                    chunkDatas[index] = (uint8_t)selectedBlock;
                    updateBlock(index,chunkAndLocInd.x,chunkAndLocInd.y);
                }
            }

            renderer.syncDisplay(); 
            renderer.display();
        
            // --- FPS CALCULATION ---
            frameCount++;
            unsigned long currentMillis = millis();
            if (currentMillis - lastFpsTime >= 1000) {
                Serial.print("FPS: ");
                Serial.println(frameCount);
                frameCount = 0;
                lastFpsTime = currentMillis;
            }
        }
        return myId;
    }

    ~Minecraft() {
        delete[] indices;
        delete[] vertices;
        delete[] textureAtlas;
        delete[] chunkDatas;
        delete[] renderBuffer;
        delete[] blocksRendered;
    }

    void onExit() override {
        writeSDArray("/worldData.bin", chunkDatas, allocChunkBytes);
    }
};