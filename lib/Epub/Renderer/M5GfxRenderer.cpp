#include "M5GfxRenderer.h"
#include <M5GFX.h>
#include <lgfx/v1/lgfx_fonts.hpp>
#include <efont.h>
#include <efontEnableAll.h>

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
void M5GfxRenderer::show_busy() { /* TODO */ }
void M5GfxRenderer::show_img(int x, int y, int width, int height, const uint8_t *img_buffer) { /* TODO: map to drawJpg, drawPng etc */ }
int M5GfxRenderer::get_space_width() { return 8; }
