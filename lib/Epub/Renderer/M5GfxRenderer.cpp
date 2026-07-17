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

    create_framebuffer();

    // Set E-Paper mode to fast partial refresh (reduces flickering)
    // epd_fast: partial refresh without full screen flash
    M5.Display.setEpdMode(epd_mode_t::epd_fast);

    // Track refresh count for periodic full refresh
    m_refresh_count = 0;

    // efont는 draw_text에서 직접 사용됨
}

bool M5GfxRenderer::create_framebuffer()
{
    if (!framebuffer)
        return false;

    framebuffer->deleteSprite();
    int w = M5.Display.width();
    int h = M5.Display.height();
    void *result = framebuffer->createSprite(w, h);

    if (result == nullptr)
    {
        ESP_LOGW("M5GfxRenderer", "Failed to create full-screen framebuffer, trying half height");
        framebuffer->deleteSprite();
        result = framebuffer->createSprite(w, h / 2);
        if (result == nullptr)
        {
            ESP_LOGE("M5GfxRenderer", "Failed to create framebuffer");
            framebuffer->deleteSprite();
            return false;
        }
        ESP_LOGI("M5GfxRenderer", "Framebuffer created (half-screen): %dx%d", w, h / 2);
        return true;
    }
    ESP_LOGI("M5GfxRenderer", "Framebuffer created: %dx%d", w, h);
    return true;
}

void M5GfxRenderer::set_landscape(bool landscape)
{
    if (landscape == m_landscape && framebuffer && framebuffer->width() > 0)
        return;
    m_landscape = landscape;
    // M5Paper default rotation is 0 (portrait 540x960). Use rotation 1 for landscape (960x540).
    M5.Display.setRotation(landscape ? 1 : 0);
    if (!create_framebuffer())
    {
        delete framebuffer;
        framebuffer = nullptr;
        return;
    }
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
    m_refresh_count = 0;
    m_pending_full_refresh = true;
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

// static private helper: decode one UTF-8 char, return advanced pointer + pixel width
const char *M5GfxRenderer::decode_efont_char(const char *str, uint16_t *out_utf16, int *out_width)
{
    str = efontUFT8toUTF16(out_utf16, (char *)str);
    *out_width = (*out_utf16 < 0x0100) ? (8 * EFONT_TEXT_SCALE) : (16 * EFONT_TEXT_SCALE);
    return str;
}

int M5GfxRenderer::get_text_width(const char *text, bool bold, bool italic)
{
    if (!text)
        return 0;

    int width = 0;
    const char *str = text;
    while (*str != 0x00)
    {
        if (*str == '\n') break;
        uint16_t utf16;
        int charWidth;
        str = decode_efont_char(str, &utf16, &charWidth);
        width += charWidth;
    }
    return width;
}

void M5GfxRenderer::draw_text(int x, int y, const char *text, bool bold, bool italic)
{
    if (!framebuffer || !text)
        return;

    int posX = x + margin_left;
    int posY = y + margin_top;

    byte font[32];
    const char *str = text;

    while (*str != 0x00)
    {
        if (*str == '\n')
        {
            posY += 16 * EFONT_TEXT_SCALE;
            posX = x + margin_left;
            str++;
            continue;
        }

        uint16_t utf16;
        int width;
        str = decode_efont_char(str, &utf16, &width);
        getefontData(font, utf16);

        for (uint8_t row = 0; row < 16; row++)
        {
            uint16_t fontdata = font[row * 2] * 256 + font[row * 2 + 1];
            for (uint8_t col = 0; col < 16; col++)
            {
                if ((0x8000 >> col) & fontdata)
                {
                    int drawX = posX + col * EFONT_TEXT_SCALE;
                    int drawY = posY + row * EFONT_TEXT_SCALE;
                    if (EFONT_TEXT_SCALE == 1)
                        framebuffer->drawPixel(drawX, drawY, TFT_BLACK);
                    else
                        framebuffer->fillRect(drawX, drawY, EFONT_TEXT_SCALE, EFONT_TEXT_SCALE, TFT_BLACK);
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
        // One-shot GC16 full refresh requested by major screen transition
        // (e.g. book → reader menu): wipes ghosting carried over from the
        // previous screen. Without this, DU leaves visible bands like the
        // book's left text column ghosted into the menu background.
        if (m_pending_full_refresh)
        {
            M5.Display.setEpdMode(epd_mode_t::epd_quality); // GC16
            framebuffer->pushSprite(0, 0);
            M5.Display.setEpdMode(epd_mode_t::epd_fast);
            m_pending_full_refresh = false;
            m_refresh_count = 0;
            return;
        }

        m_refresh_count++;

        // Periodic ghost-clearing refresh using GL16 (epd_text).
        // GL16 is specifically designed to clear residual ghosting left by DU
        // (epd_fast) page-flips without the full visible white flash that GC16
        // (epd_quality) causes. This gives cleaner text with far less visual
        // disruption than the old epd_quality approach.
        if (EPD_FULL_REFRESH_INTERVAL > 0 && m_refresh_count >= EPD_FULL_REFRESH_INTERVAL)
        {
            M5.Display.setEpdMode(epd_mode_t::epd_text);   // GL16: ghost-clearing
            framebuffer->pushSprite(0, 0);
            M5.Display.setEpdMode(epd_mode_t::epd_fast);   // DU: back to fast mode
            m_refresh_count = 0;
        }
        else
        {
            // DU (epd_fast): 260 ms, monochrome, minimal visible flicker
            framebuffer->pushSprite(0, 0);
        }
    }
}

void M5GfxRenderer::flush_display_full()
{
    if (framebuffer)
    {
        // GC16 full quality refresh: eliminates all ghosting. Use this only
        // when the user explicitly requests a screen refresh or for important
        // single-shot displays (e.g., sleep cover image).
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
    // efont glyph is 16px tall; rendered height = 16 * EFONT_TEXT_SCALE, plus 4px padding.
    return apply_line_spacing(16 * EFONT_TEXT_SCALE + 4);
}

void M5GfxRenderer::reset()
{
    if (framebuffer)
    {
        // Clear the framebuffer to white so the next render starts clean.
        framebuffer->fillSprite(TFT_WHITE);
        // Push using GC16 (epd_quality) for a full waveform refresh that
        // completely eliminates any accumulated ghosting. This is only called
        // for major UI transitions (library ↔ reader, explicit "Refresh screen")
        // where a brief quality refresh is acceptable.
        M5.Display.setEpdMode(epd_mode_t::epd_quality);
        framebuffer->pushSprite(0, 0);
        M5.Display.setEpdMode(epd_mode_t::epd_fast);
        m_refresh_count = 0;
        m_pending_full_refresh = false;
    }
    else
    {
        // Fallback: no framebuffer available
        M5.Display.setEpdMode(epd_mode_t::epd_quality);
        M5.Display.clear();
        M5.Display.setEpdMode(epd_mode_t::epd_fast);
    }
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
// Return false so RubbishHtmlParser::render_page() skips the intermediate white-screen
// flush that is only needed for epdiy-based devices (see comment in RubbishHtmlParser.cpp).
// That intermediate flush was the cause of the full-screen flicker on every page turn.
bool M5GfxRenderer::has_gray() { return false; }
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
