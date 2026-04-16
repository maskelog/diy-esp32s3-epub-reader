#pragma once

#include <M5Unified.h>
#include "Renderer.h"

#ifndef EPD_FULL_REFRESH_INTERVAL
// GL16 ghost-clearing refresh interval. GL16 is gentler than GC16 (no big
// white flash) but still removes residual ghosting left by DU page-flips.
// Lower value = more frequent cleanup (trades frequency for less jarring
// appearance compared to the old epd_quality/GC16 approach).
#define EPD_FULL_REFRESH_INTERVAL 20
#endif

class M5GfxRenderer : public Renderer
{
private:
    static constexpr int EFONT_TEXT_SCALE = 2; // efont base glyph is 16px; scale=2 → 32px rendered

    LGFX_Sprite *framebuffer;
    int m_refresh_count = 0;
    bool dither_images = false;

    // Decodes one UTF-8 character from *str (advancing the pointer) and
    // returns its rendered pixel width.  Defined in M5GfxRenderer.cpp.
    static const char *decode_efont_char(const char *str, uint16_t *out_utf16, int *out_width);

public:
    M5GfxRenderer();
    ~M5GfxRenderer();

    virtual void draw_pixel(int x, int y, uint8_t color);
    virtual int get_text_width(const char *text, bool bold = false, bool italic = false);
    virtual void draw_text(int x, int y, const char *text, bool bold = false, bool italic = false);
    virtual uint8_t map_image_gray(uint8_t gray) { return gray; }
    virtual void draw_rect(int x, int y, int width, int height, uint8_t color = 0);
    virtual void draw_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color);
    virtual void draw_circle(int x, int y, int r, uint8_t color = 0);

    virtual void fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color);
    virtual void fill_rect(int x, int y, int width, int height, uint8_t color = 0);
    virtual void fill_circle(int x, int y, int r, uint8_t color = 0);

    virtual void needs_gray(uint8_t color);
    virtual bool has_gray();
    virtual void show_busy();
    virtual void show_img(int x, int y, int width, int height, const uint8_t *img_buffer);
    virtual void clear_screen();
    virtual void flush_display();
    void flush_display_full();  // Force full refresh to clear ghosting

    virtual int get_page_width();
    virtual int get_page_height();
    virtual int get_space_width();
    virtual int get_line_height();

    virtual void reset();

    // Use the shared image helpers to enforce 1-bit rendering.
    virtual void draw_image(const std::string &filename, const uint8_t *data, size_t data_size, int x, int y, int width, int height);
    virtual bool get_image_size(const std::string &filename, const uint8_t *data, size_t data_size, int *width, int *height);
};
