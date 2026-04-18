#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>


namespace M3D {
   struct Camera {
        float x = 0, y = 0, z = 0;
        float rotX = 0.0f; 
        float rotY = 0.0f;
        int32_t fov = 110;
    };

    template<typename T> struct Vec2 { T x, y; };
    template<typename T> struct Vec3 { T x, y, z; };

    template<typename T> struct Index {T i;};

    namespace Vertex {
        // --- 2D COORDINATES ---
        namespace D2 {
            template<typename T> 
            struct Plain : Vec2<T> {};

            template<typename T> 
            struct Colored : Vec2<T> { uint16_t color; };
            
            template<typename T, typename U = uint8_t> 
            struct Textured : Vec2<T> { U u, v; };
         
        }

        // --- 3D COORDINATES ---
        namespace D3 {
            template<typename T> 
            struct Plain : Vec3<T> {};

            template<typename T> 
            struct Colored : Vec3<T> { uint16_t color; };

            template<typename T, typename U = uint8_t> 
            struct Textured : Vec3<T> { U u, v; };
        }
    }

}

enum class FaceCulling : uint8_t{
    NONE,
    BACK,
    FRONT
};

template <typename DepthPrecision = uint8_t, 
          typename PositionPrecision = int16_t, 
          typename UVPrecision = uint8_t>
class Micro3D {
    private:
        volatile bool _pauseRendering = false;
        volatile bool _isPaused = false;
        TFT_eSPI* _tft;
        TFT_eSprite* _buffer0; 
        TFT_eSprite* _buffer1;    
        uint16_t* _dmaBuffer0 = nullptr;
        uint16_t* _dmaBuffer1 = nullptr;
        bool doubleBuffers = false;
        volatile bool activeBuffer = 0;
        bool enabledFrameBuffer = false;

        uint16_t _width;
        uint16_t _height;

        uint16_t upscaleWidth;
        uint16_t upscaleHeight;
        bool upscaling = false;
        bool _antiAliasing = false;

        uint16_t clearColorValue;
        uint16_t _x;
        uint16_t _y;
        uint32_t pixelCount;

        DepthPrecision* _zBuffer;
        bool enabledDepthBuffer = false;
        FaceCulling faceCulling = FaceCulling::NONE;    

        bool isBoundTexture565;
        bool boundTexture = false;
        uint16_t boundTextureWidth,boundTextureHeight;
        uint16_t* textureBuf565;
        uint8_t* textureBuf323;
        

        TaskHandle_t _displayTaskHandle;
        SemaphoreHandle_t _frameSync;
        SemaphoreHandle_t _displayDoneSync; 

   
        static void displayTaskWrapper(void* parameter) {
            Micro3D* engine = static_cast<Micro3D*>(parameter);
            engine->displayTask(); 
        }


       void renderUpscaled(uint16_t* srcBuffer) {
            const uint16_t CHUNK_LINES = 16;
            
            if (!_dmaBuffer0 || !_dmaBuffer1) return; 

            uint32_t x_ratio = ((_width - 1) << 8) / upscaleWidth;
            uint32_t y_ratio = ((_height - 1) << 8) / upscaleHeight;

            bool dmaToggle = false; // This flips back and forth to swap buffers

            // Claim the SPI bus for a continuous DMA transaction
            _tft->startWrite();

            for (uint16_t startY = 0; startY < upscaleHeight; startY += CHUNK_LINES) {
                uint16_t linesThisChunk = (startY + CHUNK_LINES > upscaleHeight) ? (upscaleHeight - startY) : CHUNK_LINES;

         
                _tft->dmaWait();

            
                uint16_t* currentChunkPtr = dmaToggle ? _dmaBuffer1 : _dmaBuffer0;

                // --- CPU HEAVY LIFTING ---
                for (uint16_t cy = 0; cy < linesThisChunk; cy++) {
                    uint16_t dy = startY + cy; 
                    uint32_t gy = dy * y_ratio;
                    int gyi = gy >> 8;           
                    int ty = gy & 0xFF;          
                    int inv_ty = 256 - ty;       

                    int gy1 = (gyi + 1 < _height) ? gyi + 1 : _height - 1;
                    uint32_t y_offset0 = gyi * _width;
                    uint32_t y_offset1 = gy1 * _width;
                    
                    uint32_t chunkOffset = cy * upscaleWidth;

                    for (uint16_t dx = 0; dx < upscaleWidth; dx++) {
                        if (_antiAliasing) {
                            // Fast Bilinear Interpolation
                            uint32_t gx = dx * x_ratio;
                            int gxi = gx >> 8;
                            int tx = gx & 0xFF;
                            int inv_tx = 256 - tx;

                            int gx1 = (gxi + 1 < _width) ? gxi + 1 : _width - 1;

                            uint16_t c00 = srcBuffer[y_offset0 + gxi];
                            uint16_t c10 = srcBuffer[y_offset0 + gx1];
                            uint16_t c01 = srcBuffer[y_offset1 + gxi];
                            uint16_t c11 = srcBuffer[y_offset1 + gx1];

                            uint32_t r00 = (c00 >> 11) & 0x1F, g00 = (c00 >> 5) & 0x3F, b00 = c00 & 0x1F;
                            uint32_t r10 = (c10 >> 11) & 0x1F, g10 = (c10 >> 5) & 0x3F, b10 = c10 & 0x1F;
                            uint32_t r01 = (c01 >> 11) & 0x1F, g01 = (c01 >> 5) & 0x3F, b01 = c01 & 0x1F;
                            uint32_t r11 = (c11 >> 11) & 0x1F, g11 = (c11 >> 5) & 0x3F, b11 = c11 & 0x1F;

                            uint32_t w00 = inv_tx * inv_ty;
                            uint32_t w10 = tx * inv_ty;
                            uint32_t w01 = inv_tx * ty;
                            uint32_t w11 = tx * ty;

                            uint32_t r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 16;
                            uint32_t g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 16;
                            uint32_t b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 16;

                            currentChunkPtr[chunkOffset + dx] = (r << 11) | (g << 5) | b;
                        } else {
                            // Fast Nearest Neighbor
                            uint16_t sx = (dx * x_ratio) >> 8;
                            currentChunkPtr[chunkOffset + dx] = srcBuffer[y_offset0 + sx];
                        }
                    }
                }
                
     
                _tft->pushImageDMA(_x, _y + startY, upscaleWidth, linesThisChunk, currentChunkPtr);

                // Flip the toggle so the next loop uses the other buffer
                dmaToggle = !dmaToggle;
            }

            // Clean up: Wait for the very last chunk to finish transferring before leaving
            _tft->dmaWait();
            _tft->endWrite();
        }
      
        void displayTask() {
            while (true) {
                // --- 1. PAUSE CHECK ---
                if (_pauseRendering) {
                    _isPaused = true;               // Tell the main core we have safely stopped
                    vTaskDelay(pdMS_TO_TICKS(10));  // Sleep while paused
                    continue;                       // Skip the rendering logic below
                } else {
                    _isPaused = false;
                }

                // --- 2. RENDER WAKEUP ---
                // Wait for a frame sync
                if (xSemaphoreTake(_frameSync, pdMS_TO_TICKS(10)) == pdTRUE) {
                    if (doubleBuffers) {
                        if (activeBuffer == true && _buffer0 != nullptr) {
                            if(upscaling){
                                renderUpscaled((uint16_t*)_buffer0->getPointer());
                            } else {
                                _buffer0->pushSprite(_x, _y);
                            }
                        } else if (activeBuffer == false && _buffer1 != nullptr) {
                            if(upscaling){
                                renderUpscaled((uint16_t*)_buffer1->getPointer());
                            } else {
                                _buffer1->pushSprite(_x, _y);
                            }
                        }
                    } else if (enabledFrameBuffer && _buffer0 != nullptr) {
                        if(upscaling){
                            renderUpscaled((uint16_t*)_buffer0->getPointer());
                        } else {
                            _buffer0->pushSprite(_x, _y);
                        }
                    }

                    if (_displayDoneSync != nullptr) {
                        xSemaphoreGive(_displayDoneSync);
                    }
                }
                vTaskDelay(2);
            }
        }
        private:
    
        TFT_eSprite* getCanvas() {
            if (!enabledFrameBuffer) return nullptr;
            if (!doubleBuffers) return _buffer0;
            return activeBuffer ? _buffer1 : _buffer0;
        } TFT_eSprite* getOtherCanvas() {
            if (!enabledFrameBuffer) return nullptr;
            if (!doubleBuffers) return _buffer0;
            return activeBuffer ? _buffer0 : _buffer1;
        }
template <typename T>
void drawColoredLine(uint16_t*& buf, T x, T y, T w, uint16_t colStart, uint16_t colEnd) {
    // 1. Vertical clipping
    if (y < 0 || y >= _height || w <= 0) return;
    
    // 2. Total horizontal off-screen clipping
    T x_end = x + w - 1;
    if (x_end < 0 || x >= _width) return; 

    // Fixed-Point Math (Shifted by 8 bits for decimal precision)
    float alpha = 255.0f ; 
    float delta =-255.0f / max((float)w ,0.001f) ;
  
    // 3. Left edge screen clipping 
    if (x < 0) {
        alpha += delta * (-x); // Fast-forward the color interpolator
        w += x;                // Shrink the width
        x = 0;                 // Clamp to left edge
    }
    
    // 4. Right edge screen clipping
    if (x + w > _width) {
        w = _width - x;
    }

    for (T i = 0; i < w; i++) {

        uint8_t currentAlpha =(alpha ) ; 

       
       
        buf[_width * y + x++] = _tft->color565((i) / w*  255,0,0);
         alpha += delta; 
       
       
    }
}template <typename T>
void fillColoredTriangle(TFT_eSprite* canvas, T x0, T y0, uint16_t col0, T x1, T y1, uint16_t col1, T x2, T y2, uint16_t col2) {
    uint16_t* buf = (uint16_t*) canvas->getPointer();
    
    // 1. Find the Bounding Box and strictly clamp to screen
    T minX = max((T)0, min(x0, min(x1, x2)));
    T minY = max((T)0, min(y0, min(y1, y2)));
    T maxX = min((T)(_width - 1), max(x0, max(x1, x2)));
    T maxY = min((T)(_height - 1), max(y0, max(y1, y2)));

    if (minX > maxX || minY > maxY) return;

    // 2. Setup Edge Equations
    T dx12 = x1 - x2; T dy12 = y1 - y2;
    T dx20 = x2 - x0; T dy20 = y2 - y0;
    T dx01 = x0 - x1; T dy01 = y0 - y1;

    T area = (dx01 * dy20) - (dx20 * dy01);
    if (area == 0) return; 

    // 3. Winding Order Correction (Backface Culling swap)
    if (area < 0) {
        std::swap(x1, x2); std::swap(y1, y2); std::swap(col1, col2);
        dx12 = x1 - x2; dy12 = y1 - y2;
        dx20 = x2 - x0; dy20 = y2 - y0;
        dx01 = x0 - x1; dy01 = y0 - y1;
        area = -area;
    }
    
    // Cast area to unsigned for safe, clean division later
    T uarea = area * 8; 

    // 4. Pre-extract RGB565 components
    uint32_t r0 = (col0 >> 10) & 0x1F, g0 = (col0 >> 5) & 0x3F, b0 = col0 & 0x1F;
    uint32_t r1 = (col1 >> 10) & 0x1F, g1 = (col1 >> 5) & 0x3F, b1 = col1 & 0x1F;
    uint32_t r2 = (col2 >> 10) & 0x1F, g2 = (col2 >> 5) & 0x3F, b2 = col2 & 0x1F;

    // 5. Calculate initial Barycentric Weights at top-left
    T w0_row = (x1 - minX) * dy12 - (y1 - minY) * dx12;
    T w1_row = (x2 - minX) * dy20 - (y2 - minY) * dx20;
    T w2_row = (x0 - minX) * dy01 - (y0 - minY) * dx01;

    // 6. Rasterization Loop
    uint8_t detail = 3;
    uint8_t rPixel = 0;
    uint16_t currentColorR;
    // Using a 16-bit shift for precision
uint32_t inv_uarea = (1 << 16) / uarea;
    for (T y = minY; y <= maxY; y++) {
        T w0 = w0_row;
        T w1 = w1_row;
        T w2 = w2_row;
        
        uint32_t y_offset = y * _width;

        for (T x = minX; x <= maxX; x++) {
   
            if ((w0 | w1 | w2) >= 0) {
                if(rPixel >= detail){
   
                    uint32_t uw0 = w0, uw1 = w1, uw2 = w2;
                    
                 
                    uint32_t r = ((r0 * uw0 + r1 * uw1 + r2 * uw2) * inv_uarea) >> 16;
                    uint32_t g = ((g0 * uw0 + g1 * uw1 + g2 * uw2) * inv_uarea) >> 16;
                    uint32_t b = ((b0 * uw0 + b1 * uw1 + b2 * uw2) * inv_uarea) >> 16;
                    currentColorR = (r << 11) | (g << 5) | b;
                   
                    rPixel = 0;
                }
                 buf[y_offset + x] = currentColorR;
                rPixel++;
            }
            w0 -= dy12;
            w1 -= dy20;
            w2 -= dy01;
        }
        w0_row += dx12;
        w1_row += dx20;
        w2_row += dx01;
        rPixel = detail;
    }
}
template <typename T>
void fillFastColoredTriangle(uint16_t* buf, T x0, T y0, uint16_t col0, T x1, T y1, uint16_t col1, T x2, T y2, uint16_t col2) {


    // 1. Save original vertices/colors for accurate Barycentric interpolation

    T ox0 = x0, oy0 = y0; uint16_t ocol0 = col0;
    T ox1 = x1, oy1 = y1; uint16_t ocol1 = col1;
    T ox2 = x2, oy2 = y2; uint16_t ocol2 = col2;

    // 2. Setup Barycentric Edge Equations based on original layout
    T b_dx12 = ox1 - ox2; T b_dy12 = oy1 - oy2;
    T b_dx20 = ox2 - ox0; T b_dy20 = oy2 - oy0;
    T b_dx01 = ox0 - ox1; T b_dy01 = oy0 - oy1;

    T area = (b_dx01 * b_dy20) - (b_dx20 * b_dy01);
    if (area == 0) return; 

    // Winding Order Correction
    if (area < 0) {
        std::swap(ox1, ox2); std::swap(oy1, oy2); std::swap(ocol1, ocol2);
        b_dx12 = ox1 - ox2; b_dy12 = oy1 - oy2;
        b_dx20 = ox2 - ox0; b_dy20 = oy2 - oy0;
        b_dx01 = ox0 - ox1; b_dy01 = oy0 - oy1;
        area = -area;
    }


    T uarea = area * 8; 
    uint32_t inv_uarea = (1 << 16) / uarea;

    uint32_t r0 = (ocol0 >> 11) & 0x1F, g0 = (ocol0 >> 5) & 0x3F, b0 = ocol0 & 0x1F;
    uint32_t r1 = (ocol1 >> 11) & 0x1F, g1 = (ocol1 >> 5) & 0x3F, b1 = ocol1 & 0x1F;
    uint32_t r2 = (ocol2 >> 11) & 0x1F, g2 = (ocol2 >> 5) & 0x3F, b2 = ocol2 & 0x1F;

    // 3. Scanline Y-Sorting (Modifies x, y only for bounds finding)
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }

    // Completely off-screen check
    if (y2 < 0 || y0 >= _height) return; 

    int32_t dx01 = x1 - x0, dy01 = y1 - y0;
    int32_t dx02 = x2 - x0, dy02 = y2 - y0;
    int32_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;
    int32_t last = (y1 == y2) ? y1 : y1 - 1;

    // --- HELPER FUNCTION: DRAWS A SINGLE HORIZONTAL LINE ---
    auto drawSpan = [&](int32_t a, int32_t b, int32_t y) {
        if (y < 0 || y >= _height) return;
        if (a > b) std::swap(a, b);
        
        // Screen X clipping
        if (b < 0 || a >= _width) return;
        if (a < 0) a = 0;
        if (b >= _width) b = _width - 1;

        // Compute Barycentric weights ONLY for the starting pixel (a, y)
        T w0 = (ox1 - a) * b_dy12 - (oy1 - y) * b_dx12;
        T w1 = (ox2 - a) * b_dy20 - (oy2 - y) * b_dx20;
        T w2 = (ox0 - a) * b_dy01 - (oy0 - y) * b_dx01;

        uint32_t y_offset = y * _width;
        uint8_t detail = 3;
        uint8_t rPixel = detail; 
        uint16_t currentColorR = 0;

        for (int32_t x = a; x <= b; ++x) {
            if (rPixel >= detail) {
                // Quick clamp to prevent underflow artifacting on exact edges
                uint32_t uw0 = w0 > 0 ? w0 : 0;
                uint32_t uw1 = w1 > 0 ? w1 : 0;
                uint32_t uw2 = w2 > 0 ? w2 : 0;

            
                uint32_t r = ((r0 * uw0 + r1 * uw1 + r2 * uw2) * inv_uarea) >> 16;
                uint32_t g = ((g0 * uw0 + g1 * uw1 + g2 * uw2) * inv_uarea) >> 16;
                uint32_t b_col = ((b0 * uw0 + b1 * uw1 + b2 * uw2) * inv_uarea) >> 16;
                
                currentColorR = (r << 11) | (g << 5) | b_col;
                rPixel = 0;
            }
            
            buf[y_offset + x] = currentColorR;
            rPixel++;

            w0 -= b_dy12;
            w1 -= b_dy20;
            w2 -= b_dy01;
        }
    };
    // -------------------------------------------------------


    const int FRACTIONAL_BITS = 16;
    

    int32_t step_01 = (dy01 > 0) ? (int32_t)(((int64_t)dx01 << FRACTIONAL_BITS) / dy01) : 0;
    int32_t step_02 = (dy02 > 0) ? (int32_t)(((int64_t)dx02 << FRACTIONAL_BITS) / dy02) : 0;
    int32_t step_12 = (dy12 > 0) ? (int32_t)(((int64_t)dx12 << FRACTIONAL_BITS) / dy12) : 0;

    // 2. Initialize fixed-point accumulators
    int32_t cur_a = x0 << FRACTIONAL_BITS;
    int32_t cur_b = x0 << FRACTIONAL_BITS;

    int32_t y;
    
    // --- Upper part of triangle ---
    for (y = y0; y <= last; y++) {
        int32_t a = cur_a >> FRACTIONAL_BITS;
        int32_t b = cur_b >> FRACTIONAL_BITS;
        
        drawSpan(a, b, y);
        
        cur_a += step_01;
        cur_b += step_02;
    }


    cur_a = (x1 << FRACTIONAL_BITS) + step_12 * (y - y1);
    
  
    cur_b = (x0 << FRACTIONAL_BITS) + step_02 * (y - y0);

    for (; y <= y2; y++) {
        int32_t a = cur_a >> FRACTIONAL_BITS;
        int32_t b = cur_b >> FRACTIONAL_BITS;
        
        drawSpan(a, b, y);
        
        cur_a += step_12;
        cur_b += step_02;
    }
}
template <typename T, typename U, typename Z>
void fillInterpolatedTriangle(uint16_t* buf, 
                              T x0, T y0, U u0, U v0, Z z0, 
                              T x1, T y1, U u1, U v1, Z z1, 
                              T x2, T y2, U u2, U v2, Z z2) {


    T ox0 = x0, oy0 = y0; U ou0 = u0*8, ov0 = v0*8; Z oz0 = z0;
    T ox1 = x1, oy1 = y1; U ou1 = u1*8, ov1 = v1*8; Z oz1 = z1;
    T ox2 = x2, oy2 = y2; U ou2 = u2*8, ov2 = v2*8; Z oz2 = z2;


    T b_dx12 = ox1 - ox2; T b_dy12 = oy1 - oy2;
    T b_dx20 = ox2 - ox0; T b_dy20 = oy2 - oy0;
    T b_dx01 = ox0 - ox1; T b_dy01 = oy0 - oy1;

    T area = (b_dx01 * b_dy20) - (b_dx20 * b_dy01);
    if (area == 0) return; 

    // Winding Order Correction
    switch(faceCulling){
        case FaceCulling::NONE:{
             if (area < 0) {
                std::swap(ox1, ox2); std::swap(oy1, oy2);
                std::swap(ou1, ou2); std::swap(ov1, ov2); std::swap(oz1, oz2);
                
                b_dx12 = ox1 - ox2; b_dy12 = oy1 - oy2;
                b_dx20 = ox2 - ox0; b_dy20 = oy2 - oy0;
                b_dx01 = ox0 - ox1; b_dy01 = oy0 - oy1;
                area = -area;
            }
            break;
        }
        case FaceCulling::FRONT:{
            if (area > 0) return;
          
            std::swap(ox1, ox2); std::swap(oy1, oy2);
            std::swap(ou1, ou2); std::swap(ov1, ov2); std::swap(oz1, oz2);
            
            b_dx12 = ox1 - ox2; b_dy12 = oy1 - oy2;
            b_dx20 = ox2 - ox0; b_dy20 = oy2 - oy0;
            b_dx01 = ox0 - ox1; b_dy01 = oy0 - oy1;
            area = -area;
    
            break;
        }
         
        case FaceCulling::BACK:{
            if (area <= 0) return;
            break;
        }
    }


    T uarea = area * 8; 
    if(uarea == 0) return;
    uint32_t inv_uarea = (1 << 16) / uarea;

    // --- STEPPING METHOD OPTIMIZATION ---

    int32_t du_dx = (int32_t)((((int64_t)ou0 * -b_dy12) + ((int64_t)ou1 * -b_dy20) + ((int64_t)ou2 * -b_dy01)) * inv_uarea);
    int32_t dv_dx = (int32_t)((((int64_t)ov0 * -b_dy12) + ((int64_t)ov1 * -b_dy20) + ((int64_t)ov2 * -b_dy01)) * inv_uarea);
    int32_t dz_dx = (int32_t)((((int64_t)oz0 * -b_dy12) + ((int64_t)oz1 * -b_dy20) + ((int64_t)oz2 * -b_dy01)) * inv_uarea);
    // 3. Scanline Y-Sorting (Modifies x, y only for bounds finding)
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }

    // Completely off-screen check
    if (y2 < 0 || y0 >= _height) return; 

    int32_t dx01 = x1 - x0, dy01 = y1 - y0;
    int32_t dx02 = x2 - x0, dy02 = y2 - y0;
    int32_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t last = (y1 == y2) ? y1 : y1 - 1;

    // --- HELPER FUNCTION: DRAWS A SINGLE HORIZONTAL LINE ---
    auto drawSpan = [&](int32_t a, int32_t b, int32_t y) {
        if (y < 0 || y >= _height) return;
        if (a > b) std::swap(a, b);
        
        // Screen X clipping
        if (b < 0 || a >= _width) return;
        if (a < 0) a = 0;
        if (b >= _width) b = _width - 1;

        // Compute Barycentric weights ONLY for the starting pixel (a, y)
        int32_t w0 = (ox1 - a) * b_dy12 - (oy1 - y) * b_dx12;
        int32_t w1 = (ox2 - a) * b_dy20 - (oy2 - y) * b_dx20;
        int32_t w2 = (ox0 - a) * b_dy01 - (oy0 - y) * b_dx01;

        // Calculate the starting U, V, Z values for this scanline at x=a (in 16.16 fixed point)
        int32_t cur_u_fp = (int32_t)((((int64_t)ou0 * w0) + ((int64_t)ou1 * w1) + ((int64_t)ou2 * w2)) * inv_uarea);
        int32_t cur_v_fp = (int32_t)((((int64_t)ov0 * w0) + ((int64_t)ov1 * w1) + ((int64_t)ov2 * w2)) * inv_uarea);
        int32_t cur_z_fp = (int32_t)((((int64_t)oz0 * w0) + ((int64_t)oz1 * w1) + ((int64_t)oz2 * w2)) * inv_uarea);
        uint32_t y_offset = y * _width;

        for (int32_t x = a; x <= b; ++x) {
            
            // Extract the integer parts safely (prevent negative underflows at edges)
            U interp_u = (U)(cur_u_fp > 0 ? (cur_u_fp >> 16) : 0);
            U interp_v = (U)(cur_v_fp > 0 ? (cur_v_fp >> 16) : 0);
            Z interp_z = (Z)(cur_z_fp > 0 ? (cur_z_fp >> 16) : 0);

            // --- Z-BUFFER TEST & PIXEL DRAWING ---
            if (!enabledDepthBuffer || interp_z <= _zBuffer[y_offset + x]) { 
                
                if (enabledDepthBuffer) {
                    _zBuffer[y_offset + x] = interp_z; // Update Z-Buffer
                }
                
                // --- TEXTURE FETCHING LOGIC ---
              uint16_t pixelColor = 0x1FF8; 

                if (boundTexture) {
                    // 1. Safely wrap UV coordinates to texture dimensions
                    uint16_t texU = (uint16_t)interp_u & (boundTextureWidth - 1); 
                    uint16_t texV = (uint16_t)interp_v & (boundTextureHeight - 1);

                    // 2. Calculate the 1D index for the flat texture array
                    uint32_t texIndex = (texV * boundTextureWidth) + texU;

                    // 3. Fetch and convert color based on format
                    if (isBoundTexture565) {
                   
                        pixelColor = textureBuf565[texIndex];
                    } else {
                        // Fetch the 8-bit (RGB 323) color
                        uint8_t c = textureBuf323[texIndex];
                        
                        // Extract R (3 bits), G (2 bits), B (3 bits)
                        uint16_t r3 = (c >> 5) & 0x07;
                        uint16_t g2 = (c >> 3) & 0x03;
                        uint16_t b3 = c & 0x07;

                        // Scale them up to fill 5-6-5 bits smoothly
                        uint16_t r5 = (r3 << 2) | (r3 >> 1);
                        uint16_t g6 = (g2 << 4) | (g2 << 2) | g2;
                        uint16_t b5 = (b3 << 2) | (b3 >> 1);

                        // Pack them into standard RGB565
                        uint16_t standard565 = (r5 << 11) | (g6 << 5) | b5;
                        
                    
                        pixelColor = (standard565 >> 8) | (standard565 << 8);
                    }
                }

                // 4. Write the final colored pixel to the framebuffer
                buf[y_offset + x] = pixelColor;
            }


            cur_u_fp += du_dx;
            cur_v_fp += dv_dx;
            cur_z_fp += dz_dx;
        }
    };
    // -------------------------------------------------------

    const int FRACTIONAL_BITS = 16;
    
    // 1. Pre-calculate steps . 
    int32_t step_01 = (dy01 > 0) ? (int32_t)(((int32_t)dx01 << FRACTIONAL_BITS) / dy01) : 0;
    int32_t step_02 = (dy02 > 0) ? (int32_t)(((int32_t)dx02 << FRACTIONAL_BITS) / dy02) : 0;
    int32_t step_12 = (dy12 > 0) ? (int32_t)(((int32_t)dx12 << FRACTIONAL_BITS) / dy12) : 0;

    // 2. Initialize fixed-point accumulators
    int32_t cur_a = x0 << FRACTIONAL_BITS;
    int32_t cur_b = x0 << FRACTIONAL_BITS;

    int32_t y;
    
    // --- Upper part of triangle ---
    for (y = y0; y <= last; y++) {
        drawSpan(cur_a >> FRACTIONAL_BITS, cur_b >> FRACTIONAL_BITS, y);
        cur_a += step_01; 
        cur_b += step_02;
    }

    // --- Lower part of triangle ---
    cur_a = (x1 << FRACTIONAL_BITS) + step_12 * (y - y1);
    cur_b = (x0 << FRACTIONAL_BITS) + step_02 * (y - y0);

    for (; y <= y2; y++) {
        drawSpan(cur_a >> FRACTIONAL_BITS, cur_b >> FRACTIONAL_BITS, y);
        cur_a += step_12; 
        cur_b += step_02;
    }
}

template <typename T, typename Z>
void fillDepthTriangle(uint16_t* buf, 
                       T x0, T y0, Z z0, 
                       T x1, T y1, Z z1, 
                       T x2, T y2, Z z2, 
                       uint16_t color) { 


    T ox0 = x0, oy0 = y0; Z oz0 = z0;
    T ox1 = x1, oy1 = y1; Z oz1 = z1;
    T ox2 = x2, oy2 = y2; Z oz2 = z2;

    T b_dx12 = ox1 - ox2; T b_dy12 = oy1 - oy2;
    T b_dx20 = ox2 - ox0; T b_dy20 = oy2 - oy0;
    T b_dx01 = ox0 - ox1; T b_dy01 = oy0 - oy1;

    T area = (b_dx01 * b_dy20) - (b_dx20 * b_dy01);
    if (area == 0) return; 

    // Winding Order Correction
    if (area < 0) {
        std::swap(ox1, ox2); std::swap(oy1, oy2); std::swap(oz1, oz2);
        b_dx12 = ox1 - ox2; b_dy12 = oy1 - oy2;
        b_dx20 = ox2 - ox0; b_dy20 = oy2 - oy0;
        b_dx01 = ox0 - ox1; b_dy01 = oy0 - oy1;
        area = -area;
    }

    T uarea = area * 8; 
    uint32_t inv_uarea = (1 << 16) / uarea;

    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }

    if (y2 < 0 || y0 >= _height) return; 

    int32_t dx01 = x1 - x0, dy01 = y1 - y0;
    int32_t dx02 = x2 - x0, dy02 = y2 - y0;
    int32_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t last = (y1 == y2) ? y1 : y1 - 1;

    auto drawSpan = [&](int32_t a, int32_t b, int32_t y) {
        if (y < 0 || y >= _height) return;
        if (a > b) std::swap(a, b);
        
        if (b < 0 || a >= _width) return;
        if (a < 0) a = 0;
        if (b >= _width) b = _width - 1;

        int32_t w0 = (ox1 - a) * b_dy12 - (oy1 - y) * b_dx12;
        int32_t w1 = (ox2 - a) * b_dy20 - (oy2 - y) * b_dx20;
        int32_t w2 = (ox0 - a) * b_dy01 - (oy0 - y) * b_dx01;

        uint32_t y_offset = y * _width;

        for (int32_t x = a; x <= b; ++x) {
            uint32_t uw0 = w0 > 0 ? w0 : 0;
            uint32_t uw1 = w1 > 0 ? w1 : 0;
            uint32_t uw2 = w2 > 0 ? w2 : 0;

            Z interp_z = (Z)((((uint64_t)oz0 * uw0) + ((uint64_t)oz1 * uw1) + ((uint64_t)oz2 * uw2)) * inv_uarea >> 16);

            if (!enabledDepthBuffer || interp_z < _zBuffer[y_offset + x]) { 
                if (enabledDepthBuffer) _zBuffer[y_offset + x] = interp_z;
                buf[y_offset + x] = color;
            }

            w0 -= b_dy12; w1 -= b_dy20; w2 -= b_dy01;
        }
    };

    const int FRACTIONAL_BITS = 16;
    int32_t step_01 = (dy01 > 0) ? (int32_t)(((int64_t)dx01 << FRACTIONAL_BITS) / dy01) : 0;
    int32_t step_02 = (dy02 > 0) ? (int32_t)(((int64_t)dx02 << FRACTIONAL_BITS) / dy02) : 0;
    int32_t step_12 = (dy12 > 0) ? (int32_t)(((int64_t)dx12 << FRACTIONAL_BITS) / dy12) : 0;

    int32_t cur_a = x0 << FRACTIONAL_BITS;
    int32_t cur_b = x0 << FRACTIONAL_BITS;
    int32_t y;
    
    for (y = y0; y <= last; y++) {
        drawSpan(cur_a >> FRACTIONAL_BITS, cur_b >> FRACTIONAL_BITS, y);
        cur_a += step_01; cur_b += step_02;
    }

    cur_a = (x1 << FRACTIONAL_BITS) + step_12 * (y - y1);
    cur_b = (x0 << FRACTIONAL_BITS) + step_02 * (y - y0);

    for (; y <= y2; y++) {
        drawSpan(cur_a >> FRACTIONAL_BITS, cur_b >> FRACTIONAL_BITS, y);
        cur_a += step_12; cur_b += step_02;
    }
}

template <typename T, typename U>
void fillTexturedTriangle(  uint16_t* buf, 
                          T x0, T y0, U u0, U v0, 
                          T x1, T y1, U u1, U v1, 
                          T x2, T y2, U u2, U v2) {


    T ox0 = x0, oy0 = y0; U ou0 = u0, ov0 = v0;
    T ox1 = x1, oy1 = y1; U ou1 = u1, ov1 = v1;
    T ox2 = x2, oy2 = y2; U ou2 = u2, ov2 = v2;

    T b_dx12 = ox1 - ox2; T b_dy12 = oy1 - oy2;
    T b_dx20 = ox2 - ox0; T b_dy20 = oy2 - oy0;
    T b_dx01 = ox0 - ox1; T b_dy01 = oy0 - oy1;

    T area = (b_dx01 * b_dy20) - (b_dx20 * b_dy01);
    if (area == 0) return; 

    // Winding Order Correction
    if (area < 0) {
        std::swap(ox1, ox2); std::swap(oy1, oy2);
        std::swap(ou1, ou2); std::swap(ov1, ov2);
        b_dx12 = ox1 - ox2; b_dy12 = oy1 - oy2;
        b_dx20 = ox2 - ox0; b_dy20 = oy2 - oy0;
        b_dx01 = ox0 - ox1; b_dy01 = oy0 - oy1;
        area = -area;
    }

    T uarea = area * 8; 
    uint32_t inv_uarea = (1 << 16) / uarea;

    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }
    if (y1 > y2) { std::swap(y2, y1); std::swap(x2, x1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(x0, x1); }

    if (y2 < 0 || y0 >= _height) return; 

    int32_t dx01 = x1 - x0, dy01 = y1 - y0;
    int32_t dx02 = x2 - x0, dy02 = y2 - y0;
    int32_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t last = (y1 == y2) ? y1 : y1 - 1;

    auto drawSpan = [&](int32_t a, int32_t b, int32_t y) {
        if (y < 0 || y >= _height) return;
        if (a > b) std::swap(a, b);
        
        if (b < 0 || a >= _width) return;
        if (a < 0) a = 0;
        if (b >= _width) b = _width - 1;

        int32_t w0 = (ox1 - a) * b_dy12 - (oy1 - y) * b_dx12;
        int32_t w1 = (ox2 - a) * b_dy20 - (oy2 - y) * b_dx20;
        int32_t w2 = (ox0 - a) * b_dy01 - (oy0 - y) * b_dx01;

        uint32_t y_offset = y * _width;

        for (int32_t x = a; x <= b; ++x) {
            uint32_t uw0 = w0 > 0 ? w0 : 0;
            uint32_t uw1 = w1 > 0 ? w1 : 0;
            uint32_t uw2 = w2 > 0 ? w2 : 0;

            U interp_u = (U)((((uint64_t)ou0 * uw0) + ((uint64_t)ou1 * uw1) + ((uint64_t)ou2 * uw2)) * inv_uarea >> 16);
            U interp_v = (U)((((uint64_t)ov0 * uw0) + ((uint64_t)ov1 * uw1) + ((uint64_t)ov2 * uw2)) * inv_uarea >> 16);

           
        // --- TEXTURE FETCHING LOGIC ---
            uint16_t pixelColor = 0xF81F; // Default to Magenta (Error color) if no texture is bound

            if (boundTexture) {
                // 1. Safely wrap UV coordinates to texture dimensions
                // Cast to uint16_t and use modulo to repeat the texture if UVs go out of bounds
                uint16_t texU = (uint16_t)interp_u % boundTextureWidth;
                uint16_t texV = (uint16_t)interp_v % boundTextureHeight;

                // 2. Calculate the 1D index for the flat texture array
                uint32_t texIndex = (texV * boundTextureWidth) + texU;

                // 3. Fetch and convert color based on format
                if (isBoundTexture565) {
                    pixelColor = textureBuf565[texIndex];
                } else {
                    // Fetch the 8-bit (RGB 323) color
                    uint8_t c = textureBuf323[texIndex];
                    
                    // Extract R (3 bits), G (2 bits), B (3 bits)
                    uint16_t r3 = (c >> 5) & 0x07;
                    uint16_t g2 = (c >> 3) & 0x03;
                    uint16_t b3 = c & 0x07;

                
                    uint16_t r5 = (r3 << 2) | (r3 >> 1);
                    uint16_t g6 = (g2 << 4) | (g2 << 2) | g2;
                    uint16_t b5 = (b3 << 2) | (b3 >> 1);

                    // Pack them into RGB565
                    pixelColor = (r5 << 11) | (g6 << 5) | b5;
                }
            }

            // 4. Write the final colored pixel to the framebuffer
            buf[y_offset + x] = pixelColor;

            w0 -= b_dy12; w1 -= b_dy20; w2 -= b_dy01;
        }
    };

    const int FRACTIONAL_BITS = 16;
    int32_t step_01 = (dy01 > 0) ? (int32_t)(((int64_t)dx01 << FRACTIONAL_BITS) / dy01) : 0;
    int32_t step_02 = (dy02 > 0) ? (int32_t)(((int64_t)dx02 << FRACTIONAL_BITS) / dy02) : 0;
    int32_t step_12 = (dy12 > 0) ? (int32_t)(((int64_t)dx12 << FRACTIONAL_BITS) / dy12) : 0;

    int32_t cur_a = x0 << FRACTIONAL_BITS;
    int32_t cur_b = x0 << FRACTIONAL_BITS;
    int32_t y;
    
    for (y = y0; y <= last; y++) {
        drawSpan(cur_a >> FRACTIONAL_BITS, cur_b >> FRACTIONAL_BITS, y);
        cur_a += step_01; cur_b += step_02;
    }

    cur_a = (x1 << FRACTIONAL_BITS) + step_12 * (y - y1);
    cur_b = (x0 << FRACTIONAL_BITS) + step_02 * (y - y0);

    for (; y <= y2; y++) {
        drawSpan(cur_a >> FRACTIONAL_BITS, cur_b >> FRACTIONAL_BITS, y);
        cur_a += step_12; cur_b += step_02;
    }
}
    public: 
       Micro3D() {
            _buffer0 = nullptr;
            _zBuffer = nullptr;
            _buffer1 = nullptr;     
      
        }
    
        ~Micro3D() {
            if (_dmaBuffer0) heap_caps_free(_dmaBuffer0);
    if (_dmaBuffer1) heap_caps_free(_dmaBuffer1);
        // 1. Kill the background task running on Core 0
        if (_displayTaskHandle != NULL) {
            vTaskDelete(_displayTaskHandle);
            _displayTaskHandle = NULL;
        }

        // 2. Free up the RAM buffers
        disableFrameBuffer();
        disableDepthTest();

        // 3. Delete the semaphores
        if (_frameSync != NULL) vSemaphoreDelete(_frameSync);
        if (_displayDoneSync != NULL) vSemaphoreDelete(_displayDoneSync);
    }
       void setContext(TFT_eSPI& tftInstance, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
            _x = x;
            _y = y;
            _width = w;
            _height = h;
            _tft = &tftInstance; // Store the memory address of the TFT object
            pixelCount = w * h;
            _dmaBuffer0 = (uint16_t*)heap_caps_malloc(upscaleWidth * 16 * 2, MALLOC_CAP_DMA);
            _dmaBuffer1 = (uint16_t*)heap_caps_malloc(upscaleWidth * 16 * 2, MALLOC_CAP_DMA);
            // 1. Create the binary signaling flag
           _frameSync = xSemaphoreCreateBinary();
            _displayDoneSync = xSemaphoreCreateBinary();
            _tft->initDMA();
       
            if (_displayDoneSync != nullptr) {
                xSemaphoreGive(_displayDoneSync); 
            }

            // 3. Launch the background thread
            xTaskCreatePinnedToCore(displayTaskWrapper, "DisplayTask", 4096, this, 1, &_displayTaskHandle, 0);
        }
    void enableFrameBuffer() {
            if(!enabledFrameBuffer && _tft != nullptr) {
                _buffer0 = new TFT_eSprite(_tft);
                _buffer0->setColorDepth(16);
                void* memoryPtr = _buffer0->createSprite(_width, _height);
                enabledFrameBuffer = true;

                // --- MEMORY PROFILING LOGS ---
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

                if (memoryPtr == nullptr) {
                    Serial.println("FATAL ERROR: Not enough RAM for Framebuffer!");
                    while (1); 
                }   
            }
        }

     void disableFrameBuffer() {
            if (_buffer0 != nullptr) {
                _buffer0->deleteSprite();
                delete _buffer0;
                _buffer0 = nullptr;
            }
            if (_buffer1 != nullptr) {
                _buffer1->deleteSprite();
                delete _buffer1;
                _buffer1 = nullptr;
            }
            enabledFrameBuffer = false;
            doubleBuffers = false;
        }
        void enableDoubleBuffering(){
              if(!doubleBuffers && _tft != nullptr) {
                _buffer1 = new TFT_eSprite(_tft); 
                _buffer1->setColorDepth(16);
                void* memoryPtr = _buffer1->createSprite(_width, _height);
                doubleBuffers = true;

                if (memoryPtr == nullptr) {
                    Serial.println("FATAL ERROR: Not enough RAM for Framebuffer!");
                    while (1); 
                }   
            }
        }

        
   void setUpscaling(uint16_t outWidth, uint16_t outHeight, bool antiAliasing) {
            if(outWidth == _width && outHeight == _height){
                    upscaling = false;
            }else{
                upscaling = true;
                upscaleWidth = outWidth;
                upscaleHeight = outHeight;
                _antiAliasing = antiAliasing;

         
                if (_dmaBuffer0 != nullptr) {
                    heap_caps_free(_dmaBuffer0);
                    _dmaBuffer0 = nullptr;
                }
                if (_dmaBuffer1 != nullptr) {
                    heap_caps_free(_dmaBuffer1);
                    _dmaBuffer1 = nullptr;
                }

                
                size_t bufferSize = upscaleWidth * 16 * sizeof(uint16_t);

                // Ask the ESP32 specifically for DMA-capable RAM
                _dmaBuffer0 = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_DMA);
                _dmaBuffer1 = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_DMA);

                // 3. SAFETY CHECK: Did we actually get the memory?
                if (!_dmaBuffer0 || !_dmaBuffer1) {
                    Serial.println("CRITICAL ERROR: Out of DMA Memory!");
                    // If it fails, disable upscaling so the engine doesn't crash
                    upscaleWidth = _width; 
                    upscaleHeight = _height;
                }
            }
        }
        void setAntiAliasing(bool enable) {
            _antiAliasing = enable;
        }
       void syncDisplay() {
            if (_displayDoneSync != nullptr) {
         
                xSemaphoreTake(_displayDoneSync, portMAX_DELAY);
                
          
               
            }
        }
  
        void display() {
            // 1. Swap the canvas!
            if (doubleBuffers) {
                activeBuffer = !activeBuffer; 
            }

            // 2. Toss the flag to wake up Core 0
            if (_frameSync != nullptr) {
                xSemaphoreGive(_frameSync); 
            }
        }

        void enableDepthTest() {
            if(!enabledDepthBuffer) {
                _zBuffer = new DepthPrecision[pixelCount];
                memset(_zBuffer, 0xFF, pixelCount * sizeof(DepthPrecision));
                enabledDepthBuffer = true;
            }
        }

        void disableDepthTest() {
            if(enabledDepthBuffer) {
                delete[] _zBuffer; 
                _zBuffer = nullptr;
                enabledDepthBuffer = false;
            }
        }
      void clearColor() {
            if (enabledFrameBuffer) {
                TFT_eSprite* canvas = getCanvas();
                if (canvas != nullptr) {
                    canvas->fillSprite(clearColorValue);
                }
            } else {
                if (_tft != nullptr) {
                    _tft->fillRect(_x, _y, _width, _height, clearColorValue);
                }
            }
        }
    void clearDepth(){
            if(enabledDepthBuffer && _zBuffer != nullptr) {
                memset(_zBuffer, 0xFF, pixelCount * sizeof(DepthPrecision));
            }
        }
        void setClearColor(uint16_t col){
            clearColorValue = col;
        }
        template <typename T>
        void drawMesh(M3D::Vertex::D2::Plain<PositionPrecision>* vertices, M3D::Index<T>* indices, uint32_t indexCount) {
            TFT_eSprite* canvas = getCanvas();
            
            // Safety check just in case the framebuffer is disabled
            if (canvas == nullptr) return; 

            for(uint32_t i = 0; i < indexCount; i += 3) {
                // Access the '.i' member of your Index struct to get the actual integer index
                M3D::Vec2<PositionPrecision> screenPos0 = vertices[indices[i].i];
                M3D::Vec2<PositionPrecision> screenPos1 = vertices[indices[i + 1].i];
                M3D::Vec2<PositionPrecision> screenPos2 = vertices[indices[i + 2].i];
                
                canvas->fillTriangle(
                    screenPos0.x, screenPos0.y, 
                    screenPos1.x, screenPos1.y, 
                    screenPos2.x, screenPos2.y, 
                    _tft->color565(0, 255, 0)
                );
            }
        } 
        
        template <typename T>
        void drawMesh(M3D::Vertex::D2::Colored<PositionPrecision>* vertices, M3D::Index<T>* indices, uint32_t indexCount) {
            TFT_eSprite* canvas = getCanvas();
            
            // Safety check just in case the framebuffer is disabled
            if (canvas == nullptr) return; 

            for(uint32_t i = 0; i < indexCount; i += 3) {
                // Access the '.i' member of your Index struct to get the actual integer index
                M3D::Vec2<PositionPrecision> screenPos0 = vertices[indices[i].i];
                M3D::Vec2<PositionPrecision> screenPos1 = vertices[indices[i + 1].i];
                M3D::Vec2<PositionPrecision> screenPos2 = vertices[indices[i + 2].i];
                
            uint16_t* buf = (uint16_t*) canvas->getPointer();
              fillFastColoredTriangle(buf,
                screenPos0.x, screenPos0.y, vertices[indices[i].i].color,
                  screenPos1.x, screenPos1.y, vertices[indices[i + 1].i].color,
                  screenPos2.x, screenPos2.y, vertices[indices[i + 2].i].color
              );
            }
        }     
        template <typename T>
        void drawMesh(M3D::Vertex::D2::Textured<PositionPrecision>* vertices, M3D::Index<T>* indices, uint32_t indexCount) {
            TFT_eSprite* canvas = getCanvas();
            
            // Safety check just in case the framebuffer is disabled
            if (canvas == nullptr) return; 

            for(uint32_t i = 0; i < indexCount; i += 3) {
                // Access the '.i' member of your Index struct to get the actual integer index
                M3D::Vec2<PositionPrecision> screenPos0 = vertices[indices[i].i];
                M3D::Vec2<PositionPrecision> screenPos1 = vertices[indices[i + 1].i];
                M3D::Vec2<PositionPrecision> screenPos2 = vertices[indices[i + 2].i];
                
            uint16_t* buf = (uint16_t*) canvas->getPointer();
            
              fillTexturedTriangle(buf,
                screenPos0.x, screenPos0.y, vertices[indices[i].i].u, vertices[indices[i].i].v,
                  screenPos1.x, screenPos1.y, vertices[indices[i+1].i].u, vertices[indices[i+1].i].v,
                  screenPos2.x, screenPos2.y, vertices[indices[i+2].i].u, vertices[indices[i+2].i].v
              );
            }
        } 
        
        template <typename T>
        void drawMesh(M3D::Vertex::D3::Textured<PositionPrecision>* vertices, M3D::Index<T>* indices, uint32_t indexCount) {
            TFT_eSprite* canvas = getCanvas();
            
            // Safety check just in case the framebuffer is disabled
            if (canvas == nullptr) return; 

            for(uint32_t i = 0; i < indexCount; i += 3) {
                // Access the '.i' member of your Index struct to get the actual integer index
                M3D::Vec3<PositionPrecision> screenPos0 = vertices[indices[i].i];
                M3D::Vec3<PositionPrecision> screenPos1 = vertices[indices[i + 1].i];
                M3D::Vec3<PositionPrecision> screenPos2 = vertices[indices[i + 2].i];
                
            uint16_t* buf = (uint16_t*) canvas->getPointer();
            
              fillInterpolatedTriangle(buf,
                screenPos0.x, screenPos0.y, vertices[indices[i].i].u, vertices[indices[i].i].v,screenPos0.z,
                  screenPos1.x, screenPos1.y, vertices[indices[i+1].i].u, vertices[indices[i+1].i].v,screenPos1.z,
                  screenPos2.x, screenPos2.y, vertices[indices[i+2].i].u, vertices[indices[i+2].i].v,screenPos2.z
              );
            }
        }
        
        int32_t fix_sinX;
        int32_t fix_cosX;
        int32_t fix_sinY;
        int32_t fix_cosY;

        uint16_t* canvasFrameBuf;

        void calculateCameraMatrices(M3D::Camera& cam){
            fix_sinX = (int32_t)(sin(cam.rotX) * FIX_SCALE);
            fix_cosX = (int32_t)(cos(cam.rotX) * FIX_SCALE);
            fix_sinY = (int32_t)(sin(cam.rotY) * FIX_SCALE);
            fix_cosY = (int32_t)(cos(cam.rotY) * FIX_SCALE);
        }    
        void handleFrameBuffer(){
            TFT_eSprite* canvas = getCanvas();
            if (canvas == nullptr) return; 
            canvasFrameBuf = (uint16_t*) canvas->getPointer();
        }
        //Position
        PositionPrecision transformX = 0;
        PositionPrecision transformY = 0;
        PositionPrecision transformZ = 0;

        //Texture UV
        UVPrecision uOffset = 0;
        UVPrecision vOffset = 0;


        
        void setTransform(PositionPrecision x, PositionPrecision y, PositionPrecision z){
            transformX = x;
            transformY = y;
            transformZ = z;
        }

        void setUVOffset(UVPrecision u, UVPrecision v){
            uOffset = u;
            vOffset = v;
        }

        const int FIX_SHIFT = 8;
        const int32_t FIX_SCALE = 1 << FIX_SHIFT; 
        
        template <typename T>
        struct DrawData{
            M3D::Vertex::D3::Textured<PositionPrecision>* worldVertices;
            M3D::Index<T>* indices;
            uint32_t indexStart;
            uint32_t indexCount;
            M3D::Camera& cam;
        };
        


        template <typename T>
        void drawProjectedMesh(DrawData<T>* drawData) {
            
           
            
            M3D::Camera& cam = drawData->cam;

            for(uint32_t i = drawData->indexStart; i < drawData->indexStart + drawData->indexCount; i += 3) {
                auto v0 = drawData->worldVertices[drawData->indices[i].i];
                auto v1 = drawData->worldVertices[drawData->indices[i+1].i];
                auto v2 = drawData->worldVertices[drawData->indices[i+2].i];

                M3D::Vec3<PositionPrecision> s0, s1, s2;
                
              
                auto transformAndProject = [&](M3D::Vertex::D3::Textured<PositionPrecision>& vWorld, M3D::Vec3<PositionPrecision>& sScreen) {
                    // 1. TRANSLATION 
                    int32_t dx = vWorld.x + transformX - cam.x;
                    int32_t dy = vWorld.y + transformY - cam.y;
                    int32_t dz = vWorld.z + transformZ - cam.z;

                    // 2. YAW (Rotation Y)
                    int32_t rx = (dx * fix_cosY - dz * fix_sinY) >> FIX_SHIFT;
                    int32_t rz = (dx * fix_sinY + dz * fix_cosY) >> FIX_SHIFT;

                    // 3. PITCH (Rotation X)
                    int32_t ry = (dy * fix_cosX - rz * fix_sinX) >> FIX_SHIFT;
                    int32_t finalZ = (dy * fix_sinX + rz * fix_cosX) >> FIX_SHIFT;

                    // --- NEAR PLANE CULLING ---
                    if (finalZ < 1) finalZ = 1; 

                    // 4. PERSPECTIVE DIVIDE (Integers)
                    if(finalZ != 0){
                    sScreen.x = (PositionPrecision)((rx * cam.fov) / finalZ) + (_width / 2);
                    sScreen.y = (PositionPrecision)((ry * cam.fov) / finalZ) + (_height / 2);
                    }else{
                        sScreen.x = (PositionPrecision)(0) + (_width / 2);
                        sScreen.y = (PositionPrecision)(0) + (_height / 2);
                    }
                    // Pass the transformed Z depth to the Z-canvasFrameBuffer
                    sScreen.z = (DepthPrecision)finalZ; 
                };

                // Transform all three corners
                transformAndProject(v0, s0); 
                transformAndProject(v1, s1); 
                transformAndProject(v2, s2);

                // Draw the transformed, projected triangle
                if(s0.z > 1 && s1.z > 1 && s2.z > 1&&
                (
                    (s0.x >= 0 && s0.y >= 0 && s0.x < _width && s0.y < _height) ||
                    (s1.x >= 0 && s1.y >= 0 && s1.x < _width && s1.y < _height) || 
                    (s2.x >= 0 && s2.y >= 0 && s2.x < _width && s2.y < _height)
                )
                
                ){
                    fillInterpolatedTriangle(canvasFrameBuf,
                        s0.x, s0.y, v0.u + uOffset, v0.v + vOffset, s0.z,
                        s1.x, s1.y, v1.u + uOffset, v1.v + vOffset, s1.z,
                        s2.x, s2.y, v2.u + uOffset, v2.v + vOffset, s2.z
                    );
                    
                }
            }

            
        }
        
        template <typename T>
        bool isPointVisible(M3D::Vec3<T> point, M3D::Camera& cam) {
            // 1. TRANSLATION 
            int32_t dx = point.x + transformX - cam.x;
            int32_t dy = point.y + transformY - cam.y;
            int32_t dz = point.z + transformZ - cam.z;

            // 2. YAW (Rotation Y)
            int32_t rx = (dx * fix_cosY - dz * fix_sinY) >> FIX_SHIFT;
            int32_t rz = (dx * fix_sinY + dz * fix_cosY) >> FIX_SHIFT;

            // 3. PITCH (Rotation X)
            int32_t ry = (dy * fix_cosX - rz * fix_sinX) >> FIX_SHIFT;
            int32_t finalZ = (dy * fix_sinX + rz * fix_cosX) >> FIX_SHIFT;

            // --- Z CULLING (Near Plane) ---
            // If Z is 1 or less, it's behind the camera or too close to the near clipping plane.
            if (finalZ <= 1) {
                return false;
            }

            // 4. PERSPECTIVE DIVIDE (Integers)
            int32_t screenX = ((rx * cam.fov) / finalZ) + (_width / 2);
            int32_t screenY = ((ry * cam.fov) / finalZ) + (_height / 2);

            // 5. VIEWPORT BOUNDS CHECK
            if (screenX >= 0 && screenX < _width && screenY >= 0 && screenY < _height) {
                return true;
            }

            return false;
        }

        template <typename T>
        bool isPointVisible(M3D::Vec3<T> point, T radius, M3D::Camera& cam) {
            // 1. TRANSLATION
            int32_t dx = point.x + transformX - cam.x;
            int32_t dy = point.y + transformY - cam.y;
            int32_t dz = point.z + transformZ - cam.z;

            // 2. YAW (Rotation Y)
            int32_t rx = (dx * fix_cosY - dz * fix_sinY) >> FIX_SHIFT;
            int32_t rz = (dx * fix_sinY + dz * fix_cosY) >> FIX_SHIFT;

            // 3. PITCH (Rotation X)
            int32_t ry = (dy * fix_cosX - rz * fix_sinX) >> FIX_SHIFT;
            int32_t finalZ = (dy * fix_sinX + rz * fix_cosX) >> FIX_SHIFT;

            // --- SPHERE Z CULLING (Near Plane) ---
      
            if (finalZ + radius <= 1) {
                return false;
            }

      
            int32_t safeZ = (finalZ < 1) ? 1 : finalZ;

            // 4. PERSPECTIVE DIVIDE
            int32_t screenX = ((rx * cam.fov) / safeZ) + (_width / 2);
            int32_t screenY = ((ry * cam.fov) / safeZ) + (_height / 2);
            
            // Project the radius to find out how many pixels wide it is on screen
            int32_t screenRadius = (radius * cam.fov) / safeZ;

            // 5. VIEWPORT BOUNDS CHECK (Screen-space AABB)
            // Check if the "box" surrounding our projected circle overlaps the screen
            if (screenX + screenRadius >= 0 && screenX - screenRadius < _width &&
                screenY + screenRadius >= 0 && screenY - screenRadius < _height) {
                return true;
            }

            return false;
        }

        DepthPrecision getzBufferMiddle(){
            if(enabledDepthBuffer && _zBuffer != nullptr){
                uint32_t y_offset = (_height/2) * _width;
                return _zBuffer[y_offset + (_width/2)];
            }
            return 0;
        }

        void pauseDisplayTask() {
            _pauseRendering = true;
     
            while (!_isPaused) {
                vTaskDelay(1); 
            }
        }

  
        void resumeDisplayTask() {
            _pauseRendering = false;
        }

        void setFaceCulling(FaceCulling newFaceCulling){
            faceCulling = newFaceCulling;
        }
        void bindTexture565(uint16_t* textureArray,uint16_t width,uint16_t height){
            textureBuf565 = textureArray;
            textureBuf323 = nullptr;
            boundTexture = true;
            isBoundTexture565 = true;
            boundTextureWidth = width;
            boundTextureHeight = height;
        }
        void bindTexture323(uint8_t* textureArray,uint16_t width,uint16_t height){
            textureBuf323 = textureArray;
            textureBuf565 = nullptr;
            boundTexture = true;
            isBoundTexture565 = false;
            boundTextureWidth = width;
            boundTextureHeight = height;
        
        }
        
};