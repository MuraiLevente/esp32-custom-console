#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <TFT_eSPI.h> 
#include <Micro3D/Micro3D.hpp>
#include <WiFi.h>
#include <WebServer.h>
// --- Screen & SD Card ---
TFT_eSPI tft = TFT_eSPI();
#define SD_SCK  14
#define SD_MISO 27
#define SD_MOSI 25
#define SD_CS   26
bool sdReady = false;
SPIClass spiSD(HSPI);

#define TFT_LED_PIN 16 //TX2

// --- Inputs ---
const int JOY_X_PIN = 35;
const int JOY_Y_PIN = 34;
const int JOY_BTN_PIN = 32;

const int BTN1_PIN = 33; // UP
const int BTN2_PIN = 13; // LEFT 
const int BTN3_PIN = 22; // DOWN
const int BTN4_PIN = 21; // RIGHT
const int BTN5_PIN = 17; // HOME / EXIT RX2
// ==========================================
//           STORAGE UTILITIES
// ==========================================

static inline uint32_t hash1D(int32_t x, uint32_t seed) {
        // Large prime constants that perfectly scramble bits
        const uint32_t BIT_NOISE1 = 0x68E31DA4;
        const uint32_t BIT_NOISE2 = 0xB5297A4D;
        const uint32_t BIT_NOISE3 = 0x1B56C4E9;

        // Cast to unsigned to ensure predictable bit-shifting
        uint32_t mangled = (uint32_t)x;
        
        mangled *= BIT_NOISE1;
        mangled += seed;
        mangled ^= (mangled >> 8);
        mangled += BIT_NOISE2;
        mangled ^= (mangled << 8);
        mangled *= BIT_NOISE3;
        mangled ^= (mangled >> 8);
        
        return mangled;
}

static inline int32_t hash1DRange(int32_t x, uint32_t seed, int32_t min, int32_t max) {
        uint32_t h = hash1D(x, seed);
        return min + (h % (max - min));
}

// Writes a string to a file on the SD card
bool writeSD(const char* path, const String& data) {
  // Check if the SD card was successfully mounted at boot
  if (!sdReady) return false; 
  
  bool success = false;
  File file = SD.open(path, FILE_WRITE);
  if (file) {
    file.print(data);
    file.close();
    success = true;
  }
  
  return success;
}

// Reads a file from the SD card and returns it as a String
String readSD(const char* path) {
  String output = "";
  
  if (!sdReady) return output;

  // Check if the file actually exists before trying to read it
  if (SD.exists(path)) {
    File file = SD.open(path);
    if (file && !file.isDirectory()) {
      while (file.available()) {
        output += (char)file.read();
      }
      file.close();
    }
  }
  
  return output;
}



template <typename T>
bool writeSDBinary(const char* path, const T& data) {
    if (!sdReady) return false; 
    
    
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    
   
    size_t bytesWritten = file.write((const uint8_t*)&data, sizeof(T));
    file.close();
    
    // Return true only if the whole thing was written successfully
    return (bytesWritten == sizeof(T));
}

template <typename T>
bool readSDBinary(const char* path, T& data) {
    if (!sdReady || !SD.exists(path)) return false;
    
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) return false;
    
    // Read the bytes straight off the SD card into the variable's memory space
    size_t bytesRead = file.read((uint8_t*)&data, sizeof(T));
    file.close();
    
    return (bytesRead == sizeof(T));
}

template <typename T>
bool writeSDArray(const char* path, const T* array, size_t itemCount) {
    if (!sdReady || array == nullptr) return false; 
    
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    
    size_t totalBytes = sizeof(T) * itemCount;
    size_t bytesWritten = file.write((const uint8_t*)array, totalBytes);
    file.close();
    
    return (bytesWritten == totalBytes);
}

template <typename T>
bool readSDArray(const char* path, T* array, size_t itemCount) {
    if (!sdReady || !SD.exists(path) || array == nullptr) return false;
    
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) return false;
    
    size_t totalBytes = sizeof(T) * itemCount;
    size_t bytesRead = file.read((uint8_t*)array, totalBytes);
    file.close();
    
    return (bytesRead == totalBytes);
}


template <typename T>
bool writeSDArrayChunk(const char* path, const T* array, size_t arrayOffset, size_t itemCount, uint32_t fileOffsetBytes = 0) {
    if (!sdReady || array == nullptr || itemCount == 0) return false; 
    
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    
   
    if (fileOffsetBytes > 0) {
        file.seek(fileOffsetBytes);
    }
    
    size_t totalBytes = sizeof(T) * itemCount;
    
   
    size_t bytesWritten = file.write((const uint8_t*)(array + arrayOffset), totalBytes);
    file.close();
    
    return (bytesWritten == totalBytes);
}

template <typename T>
bool readSDArrayChunk(const char* path, T* array, size_t arrayOffset, size_t itemCount, uint32_t fileOffsetBytes = 0) {
    if (!sdReady || !SD.exists(path) || array == nullptr || itemCount == 0) return false;
    
    File file = SD.open(path, FILE_READ);
    if (!file || file.isDirectory()) return false;
    
    // Move the read cursor to skip the parts of the file we don't want
    if (fileOffsetBytes > 0) {
        file.seek(fileOffsetBytes);
    }
    
    size_t totalBytes = sizeof(T) * itemCount;
    
    // Read the file data directly into the exact spot in the array
    size_t bytesRead = file.read((uint8_t*)(array + arrayOffset), totalBytes);
    file.close();
    
    return (bytesRead == totalBytes);
}

void sleepScreen() {
  // 1. Turn off the heavy power draw (the LED backlight)
  digitalWrite(TFT_LED_PIN, LOW); 

  // 2. Tell the TFT logic chip to go into deep sleep
  tft.writecommand(TFT_DISPOFF); // Turn off display rendering
  tft.writecommand(0x10);   // Sleep IN
}

void wakeScreen() {
tft.writecommand(0x11);        // Universal hex command for Sleep OUT
  delay(120);                    // Give the chip 120ms to physically wake up 
  tft.writecommand(TFT_DISPON);  // Turn on display rendering

  // 2. Turn the heavy power back on (the LED backlight)
  digitalWrite(TFT_LED_PIN, HIGH);
}
// ---  Variables ---
const int TOTAL_APPS = 8; 
const int APP_MENU_ID = -1; 
 

// ==========================================
//     FORWARD DECLARATIONS 
// ==========================================
class App; 
typedef App* (*AppFactory)();
typedef void (*IconDrawFunc)(int x, int y, int size);

struct AppRecord {
  AppFactory create;       
  IconDrawFunc drawIcon;   
};


extern AppRecord appRegistry[TOTAL_APPS];

// ==========================================
//               INPUT SUBSYSTEM
// ==========================================
#include "InputHandler.hpp"
// Global Input instance
InputHandler input;


// ==========================================
//               APP ARCHITECTURE
// ==========================================

// 1. The Parent App Class
#include "App.hpp"

// 2. Main Menu App 
class MainMenuApp : public App {
  private:
    int selectedApp = 0;
    int lastPage = -1;
    bool needsUpdate = true;
    void drawMainMenuBackground(){
      int currentPage = selectedApp / 4;

      uint16_t bgColor = tft.color565(30, 30, 40);
      tft.fillScreen(bgColor);
      tft.fillRectVGradient(0,0,tft.width(),tft.height(),tft.color565(26, 0, 140),bgColor);

      
      tft.fillRect(0, 0, 320, 30, tft.color565(0, 120, 215)); 
      tft.drawRect(-10, -10, 340, 40, bgColor); 
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(2);
      tft.setCursor(65, 8);
      tft.print("Forgot...");

      tft.setTextSize(1);
      tft.setCursor(265, 12); 
      if (sdReady) { tft.setTextColor(TFT_GREEN); tft.print("SD: OK"); } 
      else { tft.setTextColor(TFT_RED); tft.print("SD: ERR"); }

      tft.setTextColor(TFT_WHITE);
      tft.setCursor(140, 220);
      tft.printf("Page %d", currentPage + 1);
    }
  void drawMenuIcon(int id, int x, int y, bool isSelected) {
      int size = 50; 
      int radius = 8; 
      uint16_t bgColor = tft.color565(30, 30, 40);

      // Draw the highlight box if selected
      if (isSelected) {
        tft.drawRoundRect(x - 4, y - 4, size + 8, size + 8, radius + 2, TFT_CYAN);
        tft.drawRoundRect(x - 3, y - 3, size + 6, size + 6, radius + 2, TFT_CYAN); 
      } else {
        tft.drawRoundRect(x - 4, y - 4, size + 8, size + 8, radius + 2, bgColor);
        tft.drawRoundRect(x - 3, y - 3, size + 6, size + 6, radius + 2, bgColor);
      }

      // Call the specific icon drawer directly from the registry!
      appRegistry[id].drawIcon(x, y, size);
    }

    void drawMainMenu(bool fullRedraw) {
      int currentPage = selectedApp / 4; 

      if (fullRedraw) {
        drawMainMenuBackground();
      }

      int startIndex = currentPage * 4;
      for (int i = 0; i < 4; i++) {
        int currentId = startIndex + i;
        int x = (i % 2 == 0) ? 80 : 190;
        int y = (i < 2) ? 60 : 140;
        
        if (currentId < TOTAL_APPS) {
          drawMenuIcon(currentId, x, y, (selectedApp == currentId));
        }
      }
    }

  public:
    MainMenuApp() : App(APP_MENU_ID) {}

    void start() override {
      needsUpdate = true;
      lastPage = -1;
    }

    int run(InputHandler& in) override {
      int currentPage = selectedApp / 4;

      if (needsUpdate) {
        drawMainMenu(currentPage != lastPage);
        lastPage = currentPage;
        needsUpdate = false; 
      }
      
      if (in.btn1Pressed) { if (selectedApp >= 2) selectedApp -= 2; needsUpdate = true; }
      if (in.btn3Pressed) { if (selectedApp + 2 < TOTAL_APPS) selectedApp += 2; needsUpdate = true; }
      if (in.btn2Pressed) { if (selectedApp > 0) selectedApp -= 1; needsUpdate = true; }
      if (in.btn4Pressed) { if (selectedApp < TOTAL_APPS - 1) selectedApp += 1; needsUpdate = true; }

      // Launch selected app!
      if (in.joyBtnPressed) {
        return selectedApp; 
      }
      delay(30);
      return myId; // Stay in menu
    }
};

// 3. The Pong App
class PongGame : public App {
  private:
    int paddleY, ballX, ballY, ballVelX, ballVelY;
    const int paddleHeight = 40, paddleWidth = 8, ballRadius = 4;

  public:
    PongGame() : App(0) {}

    void start() override {
      tft.fillScreen(TFT_BLACK);
      paddleY = (tft.height() - paddleHeight) / 2;
      ballX = tft.width() / 2; ballY = tft.height() / 2;
      ballVelX = 5; ballVelY = 4;
    }

    int run(InputHandler& in) override {
      tft.fillRect(10, paddleY, paddleWidth, paddleHeight, TFT_BLACK);
      tft.fillCircle(ballX, ballY, ballRadius, TFT_BLACK);

      if (in.joyY < 1600) paddleY -= 5; 
      if (in.joyY > 2200) paddleY += 5; 
      if (paddleY < 0) paddleY = 0;
      if (paddleY > tft.height() - paddleHeight) paddleY = tft.height() - paddleHeight;

      ballX += ballVelX; ballY += ballVelY;

      if (ballY <= ballRadius || ballY >= tft.height() - ballRadius) ballVelY = -ballVelY;
      if (ballX >= tft.width() - ballRadius) ballVelX = -ballVelX;
      
      if (ballX <= 10 + paddleWidth + ballRadius) { 
        if (ballY >= paddleY && ballY <= paddleY + paddleHeight) {
          ballVelX = -ballVelX; ballX = 10 + paddleWidth + ballRadius; 
        } else if (ballX <= 0) {
          ballX = tft.width() / 2; ballY = tft.height() / 2; delay(500); 
        }
      }

      tft.fillRect(10, paddleY, paddleWidth, paddleHeight, TFT_WHITE);
      tft.fillCircle(ballX, ballY, ballRadius, TFT_WHITE);
  delay(30);
      return myId;
    }
};


// 4. The Snake App
class SnakeGame : public App {
  private:
    static const int GRID_SIZE = 10;
    static const int MAX_LENGTH = 100; 
    int snakeX[MAX_LENGTH], snakeY[MAX_LENGTH]; 
    int snakeLength, dir, foodX, foodY;
    unsigned long lastMoveTime;
    int moveInterval; 
    bool gameOver;

    void spawnFood() {
      foodX = random(0, tft.width() / GRID_SIZE) * GRID_SIZE;
      foodY = random(0, tft.height() / GRID_SIZE) * GRID_SIZE;
    }

  public:
    SnakeGame() : App(1) {}

    void start() override {
      tft.fillScreen(TFT_BLACK);
      snakeLength = 4; dir = 1; moveInterval = 150; gameOver = false;
      for (int i = 0; i < snakeLength; i++) {
        snakeX[i] = (tft.width() / 2) - (i * GRID_SIZE); snakeY[i] = tft.height() / 2;
      }
      spawnFood(); lastMoveTime = millis();
    }

    int run(InputHandler& in) override {
      if (gameOver) {
        tft.setCursor(100, 110);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.print("GAME OVER");
          tft.setCursor(100, 130);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
  
        return myId; 
      }

    
      if (in.btn1Pressed && dir != 2) dir = 0;      // UP
      else if (in.btn3Pressed && dir != 0) dir = 2; // DOWN
      else if (in.btn4Pressed && dir != 1) dir = 1; // RIGHT
      else if (in.btn2Pressed && dir != 3) dir = 3; // LEFT

      if (millis() - lastMoveTime > moveInterval) {
        lastMoveTime = millis();
        tft.fillRect(snakeX[snakeLength - 1], snakeY[snakeLength - 1], GRID_SIZE, GRID_SIZE, TFT_BLACK);

        for (int i = snakeLength - 1; i > 0; i--) {
          snakeX[i] = snakeX[i - 1]; snakeY[i] = snakeY[i - 1];
        }

        if (dir == 0) snakeY[0] -= GRID_SIZE;
        if (dir == 1) snakeX[0] += GRID_SIZE;
        if (dir == 2) snakeY[0] += GRID_SIZE;
        if (dir == 3) snakeX[0] -= GRID_SIZE;

        if (snakeX[0] < 0 || snakeX[0] >= tft.width() || snakeY[0] < 0 || snakeY[0] >= tft.height()) gameOver = true;
        for (int i = 1; i < snakeLength; i++) { if (snakeX[0] == snakeX[i] && snakeY[0] == snakeY[i]) gameOver = true; }

        if (snakeX[0] == foodX && snakeY[0] == foodY) {
          if (snakeLength < MAX_LENGTH) snakeLength++;
          spawnFood();
          if (moveInterval > 50) moveInterval -= 2; 
        }

        tft.fillRect(foodX, foodY, GRID_SIZE, GRID_SIZE, TFT_RED);
        tft.fillRect(snakeX[0], snakeY[0], GRID_SIZE, GRID_SIZE, TFT_GREEN);
      }
      return myId;
    }
    void onExit() override {
      tft.fillScreen(TFT_BLACK);
      tft.setCursor(10, tft.height() / 2);
      tft.setTextColor(TFT_WHITE);
      tft.print("Saving game...");
      
     
      writeSD("/snake_hs.txt", String(snakeLength));
    }
};
// 5. The Tetris App
class TetrisGame : public App {
  private:
    static const int COLS = 10;
    static const int ROWS = 20;
    static const int BLOCK_SIZE = 10;
    int offsetX, offsetY;

    uint16_t board[ROWS][COLS]; 
    
    // The 7 Tetris pieces, each with 4 rotations. Each 16-bit hex represents a 4x4 grid.
    const uint16_t shapes[7][4] = {
      {0x0F00, 0x2222, 0x00F0, 0x4444}, // I
      {0x8E00, 0x6440, 0x0E20, 0x44C0}, // J
      {0x0E80, 0xC440, 0x2E00, 0x4460}, // L
      {0xCC00, 0xCC00, 0xCC00, 0xCC00}, // O
      {0x6C00, 0x4620, 0x06C0, 0x8C40}, // S
      {0x4E00, 0x4640, 0x0E40, 0x4C40}, // T
      {0xC600, 0x2640, 0x0C60, 0x4C80}  // Z
    };
    
    const uint16_t colors[7] = {
      TFT_CYAN, TFT_BLUE, TFT_ORANGE, TFT_YELLOW, 
      TFT_GREEN, TFT_MAGENTA, TFT_RED
    };

    int currentType, currentRot, currentX, currentY;
    unsigned long lastDropTime;
    int dropSpeed;
    bool gameOver;

    // Checks if the piece's bits match a 1 in a 4x4 grid
    bool isSolid(int type, int rot, int x, int y) {
      return (shapes[type][rot] & (1 << (15 - (y * 4 + x)))) != 0;
    }

    bool checkCollision(int tryX, int tryY, int tryRot) {
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
          if (isSolid(currentType, tryRot, x, y)) {
            int boardX = tryX + x;
            int boardY = tryY + y;
            // Hit walls or bottom?
            if (boardX < 0 || boardX >= COLS || boardY >= ROWS) return true;
            // Hit another block? (ignore above ceiling)
            if (boardY >= 0 && board[boardY][boardX] != 0) return true;
          }
        }
      }
      return false;
    }

    void drawBlock(int x, int y, uint16_t color) {
      tft.fillRect(offsetX + x * BLOCK_SIZE, offsetY + y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, color);
      if (color != TFT_BLACK) {
        tft.drawRect(offsetX + x * BLOCK_SIZE, offsetY + y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, TFT_BLACK);
      }
    }

    void drawPiece(int x, int y, int rot, uint16_t color) {
      for (int py = 0; py < 4; py++) {
        for (int px = 0; px < 4; px++) {
          if (isSolid(currentType, rot, px, py)) {
            if (y + py >= 0) drawBlock(x + px, y + py, color);
          }
        }
      }
    }

    void lockPiece() {
      // Transfer piece to board
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
          if (isSolid(currentType, currentRot, x, y)) {
            if (currentY + y < 0) { gameOver = true; return; }
            board[currentY + y][currentX + x] = colors[currentType];
          }
        }
      }
      checkLines();
      spawnPiece();
    }

    void checkLines() {
      int linesCleared = 0;
      for (int y = ROWS - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < COLS; x++) {
          if (board[y][x] == 0) full = false;
        }
        if (full) {
          linesCleared++;
          // Shift everything down
          for (int yy = y; yy > 0; yy--) {
            for (int x = 0; x < COLS; x++) {
              board[yy][x] = board[yy - 1][x];
            }
          }
          // Clear top row
          for (int x = 0; x < COLS; x++) board[0][x] = 0;
          y++; // Re-check this row index
        }
      }
      
      // If we cleared lines, redraw the whole board
      if (linesCleared > 0) {
        for (int y = 0; y < ROWS; y++) {
          for (int x = 0; x < COLS; x++) {
            drawBlock(x, y, board[y][x]);
          }
        }
      }
    }

    void spawnPiece() {
      currentType = random(0, 7);
      currentRot = 0;
      currentX = 3;
      currentY = -2; // Start slightly off screen
      if (checkCollision(currentX, currentY, currentRot)) {
        gameOver = true;
      } else {
        drawPiece(currentX, currentY, currentRot, colors[currentType]);
      }
    }

  public:
    TetrisGame() : App(2) {}

    void start() override {
      tft.fillScreen(TFT_BLACK);
      
      // Center the 100x200 board on the screen
      offsetX = (tft.width() - (COLS * BLOCK_SIZE)) / 2;
      offsetY = (tft.height() - (ROWS * BLOCK_SIZE)) / 2;

      // Draw border
      tft.drawRect(offsetX - 2, offsetY - 2, (COLS * BLOCK_SIZE) + 4, (ROWS * BLOCK_SIZE) + 4, TFT_WHITE);

      // Clear board array
      for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) board[y][x] = 0;
      }

      dropSpeed = 500;
      gameOver = false;
      spawnPiece();
      lastDropTime = millis();
    }

    int run(InputHandler& in) override {
      if (gameOver) {
        tft.setCursor(offsetX - 10, offsetY + 80);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.print("GAME OVER");
        return myId;
      }

      int newX = currentX;
      int newY = currentY;
      int newRot = currentRot;
      bool moved = false;

      // Input handling
      if (in.btn2Pressed) { newX--; moved = true; }      // LEFT
      if (in.btn4Pressed) { newX++; moved = true; }      // RIGHT
      if (in.btn1Pressed) { newRot = (newRot + 1) % 4; moved = true; } // UP = Rotate
      
      // DOWN = Soft drop
      int currentSpeed = in.btn3Down ? 50 : dropSpeed; 

      // Apply horizontal/rotation moves
      if (moved && !checkCollision(newX, currentY, newRot)) {
        drawPiece(currentX, currentY, currentRot, TFT_BLACK); // Erase old
        currentX = newX;
        currentRot = newRot;
        drawPiece(currentX, currentY, currentRot, colors[currentType]); // Draw new
      }

      // Gravity
      if (millis() - lastDropTime > currentSpeed) {
        lastDropTime = millis();
        if (!checkCollision(currentX, currentY + 1, currentRot)) {
          drawPiece(currentX, currentY, currentRot, TFT_BLACK);
          currentY++;
          drawPiece(currentX, currentY, currentRot, colors[currentType]);
        } else {
          lockPiece();
        }
      }

      return myId;
    }
};


#include "Minecraft.hpp"
// 7. The 2048 App
class Game2048 : public App {
  private:
    int board[4][4];
    int score;
    bool gameOver;
    bool needsDraw;

    uint16_t getTileColor(int val) {
      switch(val) {
        case 0: return tft.color565(205, 193, 180);
        case 2: return tft.color565(238, 228, 218);
        case 4: return tft.color565(237, 224, 200);
        case 8: return tft.color565(242, 177, 121);
        case 16: return tft.color565(245, 149, 99);
        case 32: return tft.color565(246, 124, 95);
        case 64: return tft.color565(246, 94, 59);
        case 128: return tft.color565(237, 207, 114);
        case 256: return tft.color565(237, 204, 97);
        case 512: return tft.color565(237, 200, 80);
        case 1024: return tft.color565(237, 197, 63);
        case 2048: return tft.color565(237, 194, 46);
        default: return tft.color565(60, 58, 50);
      }
    }

    void spawnTile() {
      int emptyCount = 0;
      int emptyX[16], emptyY[16];
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
          if (board[y][x] == 0) {
            emptyX[emptyCount] = x;
            emptyY[emptyCount] = y;
            emptyCount++;
          }
        }
      }
      if (emptyCount > 0) {
        int r = random(0, emptyCount);
        board[emptyY[r]][emptyX[r]] = (random(0, 10) < 9) ? 2 : 4;
      }
    }

    bool slideArray(int* arr) {
      bool moved = false;
      int pos = 0;
      // 1. Shift non-zeros to the left
      for (int i = 0; i < 4; i++) {
        if (arr[i] != 0) {
          if (i != pos) { arr[pos] = arr[i]; arr[i] = 0; moved = true; }
          pos++;
        }
      }
      // 2. Merge adjacent matches
      for (int i = 0; i < 3; i++) {
        if (arr[i] != 0 && arr[i] == arr[i + 1]) {
          arr[i] *= 2;
          score += arr[i]; // ADD TO SCORE!
          arr[i + 1] = 0;
          moved = true;
        }
      }
      // 3. Shift left again after merge gaps
      pos = 0;
      for (int i = 0; i < 4; i++) {
        if (arr[i] != 0) {
          if (i != pos) { arr[pos] = arr[i]; arr[i] = 0; moved = true; }
          pos++;
        }
      }
      return moved;
    }

    bool moveBoard(int dir) { // 0=UP, 1=RIGHT, 2=DOWN, 3=LEFT
      bool moved = false;
      for (int i = 0; i < 4; i++) {
        int temp[4];
        // Extract row/col
        for (int j = 0; j < 4; j++) {
          if (dir == 0) temp[j] = board[j][i];      // UP
          else if (dir == 1) temp[j] = board[i][3 - j]; // RIGHT
          else if (dir == 2) temp[j] = board[3 - j][i]; // DOWN
          else if (dir == 3) temp[j] = board[i][j];   // LEFT
        }
        
        if (slideArray(temp)) moved = true;
        
        // Put back row/col
        for (int j = 0; j < 4; j++) {
          if (dir == 0) board[j][i] = temp[j];
          else if (dir == 1) board[i][3 - j] = temp[j];
          else if (dir == 2) board[3 - j][i] = temp[j];
          else if (dir == 3) board[i][j] = temp[j];
        }
      }
      return moved;
    }

    bool checkGameOver() {
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
          if (board[y][x] == 0) return false;
          if (x < 3 && board[y][x] == board[y][x + 1]) return false;
          if (y < 3 && board[y][x] == board[y + 1][x]) return false;
        }
      }
      return true;
    }

    void drawGame() {
      // Draw grid
      int startX = 20, startY = 20;
      int tileSize = 45;
      int padding = 5;

      tft.fillRoundRect(startX - padding, startY - padding, (tileSize+padding)*4 + padding, (tileSize+padding)*4 + padding, 8, tft.color565(187, 173, 160));

      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
          int px = startX + x * (tileSize + padding);
          int py = startY + y * (tileSize + padding);
          int val = board[y][x];

          tft.fillRoundRect(px, py, tileSize, tileSize, 4, getTileColor(val));

          if (val > 0) {
            if (val <= 4) tft.setTextColor(tft.color565(119, 110, 101));
            else tft.setTextColor(TFT_WHITE);
            
            tft.setTextSize(2);
           
            int tx = px + 16;
            if (val > 9) tx = px + 10;
            if (val > 99) tx = px + 5;
            if (val > 999) { tx = px + 2; tft.setTextSize(1); py += 5; } // Shrink for big numbers

            tft.setCursor(tx, py + 15);
            tft.print(val);
          }
        }
      }

      // Draw Score Area
      tft.fillRect(230, 20, 80, 60, TFT_BLACK); // Clear score area
      tft.fillRoundRect(230, 20, 80, 50, 6, tft.color565(187, 173, 160));
      tft.setTextColor(tft.color565(238, 228, 218));
      tft.setTextSize(1);
      tft.setCursor(252, 25);
      tft.print("SCORE");
      tft.setTextColor(TFT_WHITE);
      tft.setTextSize(2);
      tft.setCursor(240, 42);
      tft.print(score);

      if (gameOver) {
        tft.fillRect(startX, startY + 80, 195, 40, tft.color565(187, 173, 160));
        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.setCursor(startX + 40, startY + 92);
        tft.print("GAME OVER");
      }
    }

  public:
    Game2048() : App(4) {} // App ID 4

    void start() override {
      tft.fillScreen(tft.color565(250, 248, 239)); // 2048 background color
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) 
          board[y][x] = 0;
      
      score = 0;
      gameOver = false;
      needsDraw = true;
      spawnTile();
      spawnTile();
    }

    int run(InputHandler& in) override {
      if (needsDraw) {
        drawGame();
        needsDraw = false;
      }

      if (gameOver) return myId;

      bool moved = false;
      if (in.btn1Pressed) moved = moveBoard(0); // UP
      if (in.btn3Pressed) moved = moveBoard(2); // DOWN
      if (in.btn2Pressed) moved = moveBoard(3); // LEFT
      if (in.btn4Pressed) moved = moveBoard(1); // RIGHT

      if (moved) {
        spawnTile();
        needsDraw = true;
        if (checkGameOver()) gameOver = true;
      }

      return myId;
    }
};class SDApp : public App {
private:
  int selectedOption;
  bool needsDraw;
  int connectedClients;
  String sdStatusStr;
  bool isHosting;
  WebServer* server;
  void drawMenu() {
    // Background and Header
    tft.fillScreen(tft.color565(30, 30, 40));
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(60, 10);
    tft.print("System Menu");

    // Menu Options
    String options[4] = {
      "Scan SD Card",
      "Start Hosting",
      "Stop Hosting",
      "Clients connected: " + String(connectedClients)
    };

    // Draw each option
    for (int i = 0; i < 4; i++) {
      int y = 50 + (i * 40);
      
      if (i == selectedOption) {
        // Highlighted Option
        tft.fillRoundRect(10, y - 5, 300, 30, 5, TFT_CYAN);
        tft.setTextColor(TFT_BLACK);
      } else {
        // Normal Option
        tft.fillRoundRect(10, y - 5, 300, 30, 5, tft.color565(50, 50, 60));
        if (i == 3) {
          tft.setTextColor(isHosting ? TFT_GREEN : TFT_DARKGREY);
        } else {
          tft.setTextColor(TFT_WHITE);
        }
      }

      tft.setCursor(20, y);
      tft.print(options[i]);
    }

    // Draw SD Scan Results Log
    tft.setTextSize(1);
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(10, 220);
    tft.print(sdStatusStr);
  }

public:
  SDApp() : App(5) {
    server = new WebServer(80);
  } 

  void start() override {
    selectedOption = 0;
    needsDraw = true;
    connectedClients = 0; 
    isHosting = false;
    sdStatusStr = "Ready. Press Joy Button to select.";
  }

  int run(InputHandler& in) override {
    if (needsDraw) {
      drawMenu();
      needsDraw = false;
    }

    int prevOption = selectedOption;

    // --- NAVIGATION LOGIC ---
    if (in.btn1Pressed || in.btn2Pressed) { 
      selectedOption--;
      if (selectedOption < 0) selectedOption = 2; 
    }
    if (in.btn3Pressed || in.btn4Pressed) { 
      selectedOption++;
      if (selectedOption > 2) selectedOption = 0; 
    }

    if (prevOption != selectedOption) {
      needsDraw = true;
    }

    // --- ACTIVATION LOGIC ---
    if (in.joyBtnPressed) {
      switch(selectedOption) {
       case 0: {
          // 1. SCAN SD CARD
          sdStatusStr = "Scanning SD...";
          drawMenu(); 
          
          
          if (sdReady) {
            uint64_t totalMB = SD.totalBytes() / (1024 * 1024);
            uint64_t usedMB = SD.usedBytes() / (1024 * 1024);
            uint64_t freeMB = totalMB - usedMB;
            sdStatusStr = "SD OK! Free: " + String((unsigned long)freeMB) + "MB";
          } else {
            sdStatusStr = "ERROR: SD Card not available.";
          }
          
          needsDraw = true; 
          break;
        }
        case 1: {
          // 2. START HOSTING
          if (!isHosting) {
            sleepScreen();
            WiFi.setTxPower(WIFI_POWER_8_5dBm);
              digitalWrite(TFT_LED_PIN, LOW);
              delay(100);
            sdStatusStr = "Starting WiFi...";
            drawMenu();

            // Set up the ESP32 as an Access Point (SSID, Password)
            WiFi.softAP("ESP32_OS", "12345678"); 
            
            // Define what happens when someone visits the IP address
            server->on("/", [this]() {
              String html = "<html><body style='font-family:sans-serif; text-align:center; margin-top:50px;'>";
              html += "<h1>Welcome to the ESP32 OS!</h1>";
              html += "<p>This page is being hosted directly from a microcontroller.</p>";
              html += "</body></html>";
              server->send(200, "text/html", html);
            });

            server->begin(); // Start the web server
            isHosting = true;
            sdStatusStr = "Hosting! Connect to: ESP32_OS";
            needsDraw = true;
          }
          break;
        }
        case 2: {
          // 3. STOP HOSTING
          if (isHosting) {
            server->stop(); // Stop the web server
            WiFi.softAPdisconnect(true); // Turn off the WiFi signal
            isHosting = false;
            connectedClients = 0;
            sdStatusStr = "Hosting Stopped.";
            needsDraw = true;
          }
            digitalWrite(TFT_LED_PIN, HIGH);
          break;
        }
      }
    }

    // --- REAL-TIME SERVER HANDLING ---
    if (isHosting) {
    
      server->handleClient(); 

      // Check how many devices are connected to the WiFi signal
      int currentClients = WiFi.softAPgetStationNum();
      
      // If the number changes, redraw the menu to update the tracker!
      if (currentClients != connectedClients) {
        connectedClients = currentClients;
        needsDraw = true;
      }
    }

    return myId;
  }

  ~SDApp() {
    delete server;
  }

  void onExit() override {
    //  Shut down the server if we exit the app
    if (isHosting) {
      server->stop();
      WiFi.softAPdisconnect(true);
      isHosting = false;
    }
  }
};
// 5. Fallback App
class FallbackApp : public App {
  public:
    FallbackApp(int id) : App(id) {} 
    
    void start() override {
      tft.fillScreen(tft.color565(30, 30, 40));
      tft.setCursor(10, 10); tft.setTextColor(TFT_WHITE);
      tft.print("App ID: "); tft.println(myId);
      tft.setCursor(10, 40); tft.print("Under Construction.");
    }
    
    int run(InputHandler& in) override {
      return myId;
    }
};

// ==========================================
//        APP LOOKUP TABLE & REGISTRY
// ==========================================
typedef App* (*AppFactory)();

typedef void (*IconDrawFunc)(int x, int y, int size);



// --- ICON DRAWING FUNCTIONS ---
void drawPongIcon(int x, int y, int size) {
  tft.fillRoundRect(x, y, size, size, 8, TFT_DARKGREEN);
  tft.fillRect(x + 5, y + 15, 4, 20, TFT_WHITE); 
  tft.fillCircle(x + 35, y + 25, 4, TFT_WHITE); 
  for(int i = 0; i < size; i += 8) tft.drawLine(x + size/2, y + i, x + size/2, y + i + 4, TFT_WHITE);
}

void drawSnakeIcon(int x, int y, int size) {
  tft.fillRoundRect(x, y, size, size, 8, TFT_ORANGE);
  tft.fillRect(x + 10, y + 25, 20, 8, TFT_DARKGREEN); 
  tft.fillRect(x + 22, y + 15, 8, 18, TFT_GREEN);    
  tft.fillRect(x + 35, y + 25, 6, 6, TFT_RED);       
}

void drawFallbackIcon(int x, int y, int size) {
  tft.fillRoundRect(x, y, size, size, 8, tft.color565(60, 60, 70));
  tft.setTextColor(tft.color565(100, 100, 110));
  tft.setCursor(x + 15, y + 18);
  tft.print("?");
}

void drawTetrisIcon(int x, int y, int size) {
  tft.fillRoundRect(x, y, size, size, 8, TFT_PURPLE);
  // Draw a little T-block
  tft.fillRect(x + 10, y + 15, 30, 10, TFT_CYAN); 
  tft.fillRect(x + 20, y + 25, 10, 10, TFT_CYAN); 
}
void drawMinecraftIcon(int x, int y, int size) {
  tft.fillRoundRect(x, y, size, size, 8, TFT_WHITE);
  tft.drawRoundRect(x, y, size, size, 8, TFT_BLACK); // Border
  
  tft.fillRect(x +10, y +10, size-20, size-20, TFT_BROWN);
  tft.fillRect(x +10, y +10, size-20, size/6, TFT_GREEN);
  
  
}
void draw2048Icon(int x, int y, int size) {
  tft.fillRoundRect(x, y, size, size, 8, tft.color565(237, 194, 46)); // Golden 2048 color
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(x + 3, y + 18);
  tft.print("2048");
}

void drawSDAppIcon(int x, int y, int size) {
  tft.fillRoundRect(x, y, size, size, 8, tft.color565(145, 255, 253));
  
  tft.fillRect(x+10, y+20, size-20, size-30, tft.color565(89, 89, 89)); 
  tft.fillRect(x+10, y+10, (size-20)/2, size-20, tft.color565(89, 89, 89)); 
  tft.fillRoundRect(x+(size)/2-10, y+10, (size)/2, size-20,12, tft.color565(89, 89, 89)); 

  tft.fillRect(x+15, y+12, 2, 5, tft.color565(255, 247, 0)); 
  tft.fillRect(x+20, y+12, 2, 5, tft.color565(255, 247, 0)); 
  tft.fillRect(x+25, y+12, 2, 5, tft.color565(255, 247, 0)); 
  
}



App* createPong() { return new PongGame(); }
App* createSnake() { return new SnakeGame(); }
App* createTetris() { return new TetrisGame(); }
App* createMinecraft() { return new Minecraft(); } 
App* create2048() { return new Game2048(); }
App* createSDApp() { return new SDApp(); }
App* createFallback3() { return new FallbackApp(3); }
App* createFallback4() { return new FallbackApp(4); }
App* createFallback5() { return new FallbackApp(5); }
App* createFallback6() { return new FallbackApp(6); }
App* createFallback7() { return new FallbackApp(7); }


AppRecord appRegistry[TOTAL_APPS] = {
  { createPong,      drawPongIcon },
  { createSnake,     drawSnakeIcon },
{ createTetris,    drawTetrisIcon },
{ createMinecraft,      drawMinecraftIcon },
 { create2048,      draw2048Icon },
  { createSDApp, drawSDAppIcon },
  { createFallback6, drawFallbackIcon },
  { createFallback7, drawFallbackIcon }
};
// Current App State
App* currentApp = nullptr;
int currentAppId = -2; // Start at an invalid ID to force the initial menu switch


// ==========================================
//               MAIN LOOP
// ==========================================

// Helper function to safely switch apps and manage memory
void switchApp(int newAppId) {
  if (currentApp != nullptr) {
    currentApp->onExit();
    delete currentApp; 
    currentApp = nullptr;
  }
  
  currentAppId = newAppId;
  
if (newAppId == APP_MENU_ID) {
    currentApp = new MainMenuApp();
  } else if (newAppId >= 0 && newAppId < TOTAL_APPS) {

    currentApp = appRegistry[newAppId].create();
  }

  if (currentApp != nullptr) {
    currentApp->start();
  }
}

void setup() {

  Serial.begin(115200); delay(500); 

  input.init(); // Setup pins

  pinMode(TFT_LED_PIN, OUTPUT); digitalWrite(TFT_LED_PIN, LOW); delay(100); 
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  // Mount the SD card ONCE and leave it active. 

  if (SD.begin(SD_CS, spiSD)) {
    sdReady = true;
  }

  tft.init(); 
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  digitalWrite(TFT_LED_PIN, HIGH);

  // Boot straight into the Main Menu App
  switchApp(APP_MENU_ID);
}

void loop() {
  // 1. Read all inputs once per frame
  input.update();

  // 2. Universal Level Exit (Home Button)
  // If btn5 is pressed AND we aren't already in the menu, force switch to menu.
  if ( (input.btn5Pressed && !currentApp->handleMenuButton() && currentAppId != APP_MENU_ID) || (currentApp->exit && currentAppId != APP_MENU_ID)) {
    switchApp(APP_MENU_ID);
  } 
  
  // 3. Normal App Execution
  else if (currentApp != nullptr) {
    int nextAppId = currentApp->run(input);
    
    // If the app returned an ID different from its own, switch to it!
    if (nextAppId != currentAppId) {
      switchApp(nextAppId);
    }
  }


}