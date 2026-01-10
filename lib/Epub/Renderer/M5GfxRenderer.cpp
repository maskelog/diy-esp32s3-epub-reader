#include "M5GfxRenderer.h"
#include <M5GFX.h>
#include <lgfx/v1/lgfx_fonts.hpp>
#include <efont.h>
#include <efontEnableAll.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cctype>

M5GfxRenderer::M5GfxRenderer()

{
    framebuffer = new LGFX_Sprite(&M5.Display);

#if defined(BOARD_HAS_PSRAM)
    // PSRAM 사용 활성화
    framebuffer->setPsram(true);
#endif

    void *result = framebuffer->createSprite(M5.Display.width(), M5.Display.height());

    if (result == nullptr)
    {
        ESP_LOGW("M5GfxRenderer", "Failed to create full-screen framebuffer, trying smaller buffer");

        // 전체 화면 실패 시 더 작은 캔버스 시도 (절반 높이)
        framebuffer->deleteSprite();
        result = framebuffer->createSprite(M5.Display.width(), M5.Display.height() / 2);

        if (result == nullptr)
        {
            ESP_LOGE("M5GfxRenderer", "Failed to create framebuffer");
            framebuffer->deleteSprite();
            delete framebuffer;
            framebuffer = nullptr;
            return;
        }
        else
        {
            ESP_LOGI("M5GfxRenderer", "Framebuffer created (half-screen): %dx%d",
                     M5.Display.width(), M5.Display.height() / 2);
        }
    }
    else
    {
        ESP_LOGI("M5GfxRenderer", "Framebuffer created successfully: %dx%d",
                 M5.Display.width(), M5.Display.height());
    }

    // efont는 draw_text에서 직접 사용됨
}

M5GfxRenderer::~M5GfxRenderer()

{

    if (framebuffer)

    {

        framebuffer->deleteSprite();

        delete framebuffer;
    }
}

void M5GfxRenderer::draw_pixel(int x, int y, uint8_t color)

{

    if (framebuffer)

    {

        framebuffer->drawPixel(x, y, color);
    }
}

int M5GfxRenderer::get_text_width(const char *text, bool bold, bool italic)

{
    if (!text)
        return 0;

    // efont를 사용하여 텍스트 폭 계산
    int width = 0;
    const char *str = text;
    int textsize = 2; // 기본 크기

    while (*str != 0x00)
    {
        if (*str == '\n')
        {
            break; // 줄바꿈까지의 폭만 계산
        }

        uint16_t strUTF16;
        str = efontUFT8toUTF16(&strUTF16, (char *)str);

        // 문자 폭 계산 (ASCII는 8px, 한글/일본어 등은 16px)
        int charWidth = (strUTF16 < 0x0100) ? (8 * textsize) : (16 * textsize);
        width += charWidth;
    }

    return width;
}

void M5GfxRenderer::draw_text(int x, int y, const char *text, bool bold, bool italic)

{
    if (!framebuffer || !text)
        return;

    // efont를 사용하여 텍스트 그리기 (m5book의 printEfontGeneric 방식)
    int posX = x;
    int posY = y;
    int textsize = 2; // 기본 크기

    byte font[32];
    const char *str = text;

    while (*str != 0x00)
    {
        if (*str == '\n')
        {
            posY += 16 * textsize;
            posX = x;
            str++;
            continue;
        }

        uint16_t strUTF16;
        str = efontUFT8toUTF16(&strUTF16, (char *)str);
        getefontData(font, strUTF16);

        // 문자 폭 계산 (ASCII는 8px, 한글/일본어 등은 16px)
        int width = (strUTF16 < 0x0100) ? (8 * textsize) : (16 * textsize);

        // 폰트 데이터를 framebuffer에 그리기
        for (uint8_t row = 0; row < 16; row++)
        {
            uint16_t fontdata = font[row * 2] * 256 + font[row * 2 + 1];
            for (uint8_t col = 0; col < 16; col++)
            {
                if ((0x8000 >> col) & fontdata)
                {
                    int drawX = posX + col * textsize;
                    int drawY = posY + row * textsize;
                    if (textsize == 1)
                    {
                        framebuffer->drawPixel(drawX, drawY, TFT_BLACK);
                    }
                    else
                    {
                        framebuffer->fillRect(drawX, drawY, textsize, textsize, TFT_BLACK);
                    }
                }
            }
        }

        posX += width;
    }
}

void M5GfxRenderer::draw_rect(int x, int y, int width, int height, uint8_t color)
{
    if (framebuffer)
    {
        framebuffer->drawRect(x, y, width, height, color);
    }
}

void M5GfxRenderer::fill_rect(int x, int y, int width, int height, uint8_t color)
{
    if (framebuffer)
    {
        framebuffer->fillRect(x, y, width, height, color);
    }
}

void M5GfxRenderer::clear_screen()
{
    if (framebuffer)
    {
        framebuffer->fillSprite(TFT_WHITE);
    }
}

void M5GfxRenderer::flush_display()
{
    if (framebuffer)
    {
        framebuffer->pushSprite(0, 0);
    }
}

int M5GfxRenderer::get_page_width()
{
    return M5.Display.width();
}

int M5GfxRenderer::get_page_height()
{
    return M5.Display.height();
}

int M5GfxRenderer::get_line_height()
{
    return apply_line_spacing(24); // based on efont size
}

void M5GfxRenderer::reset()
{
    M5.Display.clear(true);
}

// These methods are not fully implemented for brevity, but they should be mapped to M5GFX functions.
void M5GfxRenderer::draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color)
{
    if (framebuffer)
        framebuffer->drawTriangle(x0, y0, x1, y1, x2, y2, color);
}
void M5GfxRenderer::draw_circle(int x, int y, int r, uint8_t color)
{
    if (framebuffer)
        framebuffer->drawCircle(x, y, r, color);
}
void M5GfxRenderer::fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color)
{
    if (framebuffer)
        framebuffer->fillTriangle(x0, y0, x1, y1, x2, y2, color);
}
void M5GfxRenderer::fill_circle(int x, int y, int r, uint8_t color)
{
    if (framebuffer)
        framebuffer->fillCircle(x, y, r, color);
}
void M5GfxRenderer::needs_gray(uint8_t color) { /* M5GFX handles this automatically */ }
bool M5GfxRenderer::has_gray() { return true; }
void M5GfxRenderer::show_busy()
{
    if (!framebuffer)
        return;

    // Clear screen and show "Book loading" message
    framebuffer->fillSprite(TFT_WHITE);

    const char *msg = "Book loading";
    int page_width = get_page_width();
    int page_height = get_page_height();
    int line_height = get_line_height();

    if (page_width <= 0 || page_height <= 0 || line_height <= 0)
    {
        flush_display();
        return;
    }

    int text_width = get_text_width(msg, false, false);
    if (text_width < 0)
    {
        text_width = 0;
    }

    int x = (page_width - text_width) / 2;
    if (x < 0)
    {
        x = 0;
    }
    int center_y = page_height / 2;
    int y = center_y - (3 * line_height) / 4;

    draw_text(x, y, msg, false, false);
    flush_display();
}
void M5GfxRenderer::show_img(int x, int y, int width, int height, const uint8_t *img_buffer) { /* TODO: map to drawJpg, drawPng etc */ }
int M5GfxRenderer::get_space_width() { return 8; }

void M5GfxRenderer::draw_image(const std::string &filename, const uint8_t *data, size_t data_size, int x, int y, int width, int height)
{
    if (!framebuffer || !data || data_size == 0)
        return;

    // Skip very large images that might cause timeout
    if (data_size > 300000)
    {
        ESP_LOGW("M5GfxRenderer", "Skipping large image (%zu bytes)", data_size);
        // Draw placeholder
        framebuffer->fillRect(x, y, width, height, 0xCCCC);
        framebuffer->drawRect(x, y, width, height, 0x0000);
        return;
    }

    // Check file extension to determine image type
    bool is_jpg = false;
    bool is_png = false;
    size_t len = filename.length();
    if (len >= 4)
    {
        std::string ext = filename.substr(len - 4);
        for (auto &c : ext)
            c = tolower(c);
        is_jpg = (ext == ".jpg");
        is_png = (ext == ".png");
    }
    if (len >= 5 && !is_jpg)
    {
        std::string ext = filename.substr(len - 5);
        for (auto &c : ext)
            c = tolower(c);
        is_jpg = (ext == ".jpeg");
    }

    // Remove this task from watchdog during image decode (can take several seconds)
    esp_err_t wdt_err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
    bool was_subscribed = (wdt_err == ESP_OK);

    vTaskDelay(1);

    ESP_LOGI("M5GfxRenderer", "Drawing image: %s (%zu bytes) at (%d,%d) size %dx%d",
             filename.c_str(), data_size, x, y, width, height);

    // Draw to framebuffer using LGFX_Sprite's drawJpg/drawPng
    // Parameters: data, size, x, y, maxWidth, maxHeight, offX, offY, scale_x, scale_y
    bool success = false;
    if (is_jpg)
    {
        // Use drawJpg with maxWidth/maxHeight for scaling
        success = framebuffer->drawJpg(data, data_size, x, y, width, height, 0, 0, 1.0f, 1.0f);
    }
    else if (is_png)
    {
        success = framebuffer->drawPng(data, data_size, x, y, width, height, 0, 0, 1.0f, 1.0f);
    }
    else
    {
        // Default to JPEG
        success = framebuffer->drawJpg(data, data_size, x, y, width, height, 0, 0, 1.0f, 1.0f);
    }

    if (!success)
    {
        ESP_LOGW("M5GfxRenderer", "Failed to draw image, showing placeholder");
        // Draw error placeholder
        framebuffer->fillRect(x, y, width, height, 0xCCCC);
        framebuffer->drawRect(x, y, width, height, 0x0000);
    }
    else
    {
        ESP_LOGI("M5GfxRenderer", "Image drawn successfully");
    }

    vTaskDelay(1);

    // Re-add this task to watchdog
    if (was_subscribed)
    {
        esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    }
}

bool M5GfxRenderer::get_image_size(const std::string &filename, const uint8_t *data, size_t data_size, int *width, int *height)
{
    if (!data || data_size == 0 || !width || !height)
        return false;

    // For M5Paper, we skip the slow JPEG decode for size detection
    // M5GFX's drawJpg will handle scaling automatically
    // Just return a reasonable default size to indicate the image is valid
    *width = 200; // Reasonable cover size
    *height = 300;
    return true;
}
