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

    // Set E-Paper mode to fast partial refresh (reduces flickering)
    // epd_fast: partial refresh without full screen flash
    M5.Display.setEpdMode(epd_mode_t::epd_fast);

    // Track refresh count for periodic full refresh
    m_refresh_count = 0;

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
        uint8_t gray = color;
        if (dither_images)
        {
            static const uint8_t bayer8[64] = {
                0, 32, 8, 40, 2, 34, 10, 42,
                48, 16, 56, 24, 50, 18, 58, 26,
                12, 44, 4, 36, 14, 46, 6, 38,
                60, 28, 52, 20, 62, 30, 54, 22,
                3, 35, 11, 43, 1, 33, 9, 41,
                51, 19, 59, 27, 49, 17, 57, 25,
                15, 47, 7, 39, 13, 45, 5, 37,
                63, 31, 55, 23, 61, 29, 53, 21};
            const uint8_t t = static_cast<uint8_t>(bayer8[((y & 7) << 3) | (x & 7)] * 4 + 2);
            gray = (gray > t) ? 255 : 0;
        }
        uint16_t c = ((gray >> 3) << 11) | ((gray >> 2) << 5) | (gray >> 3);
        framebuffer->drawPixel(x, y, c);
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
    // Apply margins from base Renderer class
    int posX = x + margin_left;
    int posY = y + margin_top;
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
        framebuffer->drawRect(x + margin_left, y + margin_top, width, height, color);
    }
}

void M5GfxRenderer::fill_rect(int x, int y, int width, int height, uint8_t color)
{
    if (framebuffer)
    {
        framebuffer->fillRect(x + margin_left, y + margin_top, width, height, color);
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
        m_refresh_count++;

        // Periodic full refresh to clear ghosting (optional, less frequent)
        if (EPD_FULL_REFRESH_INTERVAL > 0 && m_refresh_count >= EPD_FULL_REFRESH_INTERVAL)
        {
            M5.Display.setEpdMode(epd_mode_t::epd_quality);
            framebuffer->pushSprite(0, 0);
            M5.Display.setEpdMode(epd_mode_t::epd_fast);
            m_refresh_count = 0;
        }
        else
        {
            // Normal fast partial refresh (no flickering)
            framebuffer->pushSprite(0, 0);
        }
    }
}

void M5GfxRenderer::flush_display_full()
{
    if (framebuffer)
    {
        // Force full quality refresh
        M5.Display.setEpdMode(epd_mode_t::epd_quality);
        framebuffer->pushSprite(0, 0);
        M5.Display.setEpdMode(epd_mode_t::epd_fast);
        m_refresh_count = 0;
    }
}

int M5GfxRenderer::get_page_width()
{
    return M5.Display.width() - margin_left - margin_right;
}

int M5GfxRenderer::get_page_height()
{
    return M5.Display.height() - margin_top - margin_bottom;
}

int M5GfxRenderer::get_line_height()
{
    // efont is 16px, with textsize=2 it becomes 32px
    // Add 4px padding for readability
    return apply_line_spacing(36); // 32px char height + 4px padding
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
    const bool prev_dither = dither_images;
    dither_images = true;
    Renderer::draw_image(filename, data, data_size, x, y, width, height);
    dither_images = prev_dither;
}

bool M5GfxRenderer::get_image_size(const std::string &filename, const uint8_t *data, size_t data_size, int *width, int *height)
{
    return Renderer::get_image_size(filename, data, data_size, width, height);
}
