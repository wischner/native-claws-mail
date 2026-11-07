//
// Exports the original Blaise toolbar drawings as color PNGs and binary
// ink masks. Run explicitly to regenerate the checked-in resource artwork.
//
// GPL 3.0 or later (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#include <native.h>

#include <filesystem>
#include <iostream>

#include <array>
#include <utility>

namespace {

using native::gpx;
using native::point;
using native::rect;
using native::rgba;

constexpr rgba ink(40, 44, 52, 255);
constexpr rgba paper(255, 255, 255, 255);
constexpr rgba folder(226, 180, 72, 255);
constexpr rgba folder_edge(150, 110, 30, 255);
constexpr rgba disk(62, 104, 170, 255);
constexpr rgba accent(198, 60, 48, 255);
constexpr rgba arrow(46, 125, 80, 255);

// Draw a line between two points in one color.
void line(gpx &g, rgba color, int x1, int y1, int x2, int y2,
          int width = 1) {
    g.set_ink(color).set_pen(static_cast<std::uint8_t>(width));
    g.draw_line(point(static_cast<native::coord>(x1),
                      static_cast<native::coord>(y1)),
                point(static_cast<native::coord>(x2),
                      static_cast<native::coord>(y2)));
}

// Fill a rectangle.
void fill(gpx &g, rgba color, int x, int y, int width, int height) {
    g.set_ink(color).draw_rect(
        rect(static_cast<native::coord>(x), static_cast<native::coord>(y),
             static_cast<native::dim>(width),
             static_cast<native::dim>(height)),
        true);
}

// Outline a rectangle.
void frame(gpx &g, rgba color, int x, int y, int width, int height) {
    g.set_ink(color).set_pen(1);
    g.draw_rect(
        rect(static_cast<native::coord>(x), static_cast<native::coord>(y),
             static_cast<native::dim>(width),
             static_cast<native::dim>(height)),
        false);
}

// Draw a sheet of paper with a folded corner.
void sheet(gpx &g, int x, int y, int width, int height) {
    fill(g, paper, x, y, width, height);
    frame(g, ink, x, y, width, height);
    line(g, ink, x + width - 4, y, x + width - 1, y + 3);
}

void draw_new(gpx &g) {
    sheet(g, 3, 1, 10, 14);
}

void draw_open(gpx &g) {
    fill(g, folder, 1, 4, 13, 10);
    frame(g, folder_edge, 1, 4, 13, 10);
    fill(g, folder, 2, 2, 5, 2);
    line(g, folder_edge, 2, 2, 6, 2);
}

void draw_save(gpx &g) {
    fill(g, disk, 2, 2, 12, 12);
    fill(g, paper, 4, 3, 8, 5);
    fill(g, ink, 5, 10, 6, 4);
    fill(g, paper, 9, 11, 1, 2);
}

// Draw a curved arrow; direction is -1 for undo, 1 for redo.
void draw_turn(gpx &g, int direction, int width = 1) {
    const int middle = 8;
    const auto x = [&](int offset) { return middle + direction * offset; };
    line(g, arrow, x(-4), 11, x(2), 11, width);
    line(g, arrow, x(2), 11, x(4), 9, width);
    line(g, arrow, x(4), 9, x(4), 7, width);
    line(g, arrow, x(4), 7, x(2), 5, width);
    line(g, arrow, x(2), 5, x(-4), 5, width);
    line(g, arrow, x(-4), 5, x(-2), 3, width);
    line(g, arrow, x(-4), 5, x(-2), 7, width);
}

void draw_undo(gpx &g) {
    draw_turn(g, 1);
}

void draw_redo(gpx &g) {
    draw_turn(g, -1);
}

void draw_cut(gpx &g) {
    line(g, ink, 4, 2, 10, 11);
    line(g, ink, 11, 2, 5, 11);
    frame(g, accent, 2, 11, 4, 4);
    frame(g, accent, 9, 11, 4, 4);
}

void draw_copy(gpx &g) {
    sheet(g, 2, 1, 8, 10);
    sheet(g, 6, 5, 8, 10);
}

void draw_paste(gpx &g) {
    fill(g, folder, 2, 3, 11, 12);
    frame(g, folder_edge, 2, 3, 11, 12);
    fill(g, ink, 5, 1, 5, 3);
    sheet(g, 6, 7, 8, 8);
}

void draw_find(gpx &g) {
    g.set_ink(ink).set_pen(2);
    g.draw_ellipse(rect(2, 2, 9, 9), false);
    g.set_pen(1);
    line(g, ink, 10, 10, 14, 14);
    line(g, ink, 11, 10, 14, 13);
}

// Draw an envelope.
void envelope(gpx &g, int x, int y, int width, int height) {
    fill(g, paper, x, y, width, height);
    frame(g, ink, x, y, width, height);
    line(g, ink, x, y, x + width / 2, y + height / 2);
    line(g, ink, x + width - 1, y, x + width / 2, y + height / 2);
}

// Draw a short arrow head at a point; direction is -1 left or 1 right.
void arrow_head(gpx &g, rgba color, int x, int y, int direction) {
    line(g, color, x, y, x - direction * 3, y - 3);
    line(g, color, x, y, x - direction * 3, y + 3);
}

void draw_get_mail(gpx &g) {
    envelope(g, 1, 7, 14, 8);
    line(g, arrow, 8, 1, 8, 6, 2);
    line(g, arrow, 8, 7, 5, 4);
    line(g, arrow, 8, 7, 11, 4);
}

void draw_send_mail(gpx &g) {
    envelope(g, 1, 7, 14, 8);
    line(g, arrow, 8, 1, 8, 6, 2);
    line(g, arrow, 8, 1, 5, 4);
    line(g, arrow, 8, 1, 11, 4);
}

void draw_reply(gpx &g) {
    envelope(g, 5, 7, 10, 8);
    line(g, arrow, 2, 4, 11, 4, 2);
    arrow_head(g, arrow, 1, 4, -1);
}

void draw_reply_all(gpx &g) {
    envelope(g, 5, 8, 10, 7);
    line(g, arrow, 6, 4, 12, 4, 2);
    arrow_head(g, arrow, 5, 4, -1);
    arrow_head(g, arrow, 1, 4, -1);
}

void draw_forward(gpx &g) {
    envelope(g, 1, 7, 10, 8);
    line(g, arrow, 4, 4, 13, 4, 2);
    arrow_head(g, arrow, 14, 4, 1);
}

void draw_delete_mail(gpx &g) {
    fill(g, paper, 4, 4, 8, 11);
    frame(g, ink, 4, 4, 8, 11);
    fill(g, ink, 2, 3, 12, 1);
    fill(g, ink, 6, 1, 4, 2);
    line(g, accent, 6, 6, 6, 12);
    line(g, accent, 8, 6, 8, 12);
    line(g, accent, 10, 6, 10, 12);
}

void draw_next_unread(gpx &g) {
    envelope(g, 1, 1, 14, 7);
    line(g, arrow, 8, 9, 8, 14, 2);
    line(g, arrow, 8, 15, 5, 12);
    line(g, arrow, 8, 15, 11, 12);
}

void draw_previous_unread(gpx &g) {
    envelope(g, 1, 8, 14, 7);
    line(g, arrow, 8, 2, 8, 7, 2);
    line(g, arrow, 8, 1, 5, 4);
    line(g, arrow, 8, 1, 11, 4);
}

void draw_mark(gpx &g) {
    line(g, ink, 4, 1, 4, 15);
    fill(g, accent, 5, 2, 8, 6);
}

void draw_address_book(gpx &g) {
    fill(g, disk, 3, 1, 11, 14);
    frame(g, ink, 3, 1, 11, 14);
    fill(g, paper, 7, 4, 4, 4);
    fill(g, paper, 6, 9, 6, 3);
    fill(g, ink, 2, 4, 2, 1);
    fill(g, ink, 2, 8, 2, 1);
    fill(g, ink, 2, 12, 2, 1);
}

void draw_attach(gpx &g) {
    line(g, ink, 11, 4, 11, 11);
    line(g, ink, 11, 11, 9, 13);
    line(g, ink, 9, 13, 7, 13);
    line(g, ink, 7, 13, 5, 11);
    line(g, ink, 5, 11, 5, 4);
    line(g, ink, 5, 4, 7, 2);
    line(g, ink, 7, 2, 8, 2);
    line(g, ink, 8, 2, 9, 4);
    line(g, ink, 9, 4, 9, 10);
    line(g, ink, 7, 5, 7, 10);
}

void draw_threads(gpx &g) {
    line(g, ink, 3, 3, 3, 12);
    line(g, ink, 3, 7, 6, 7);
    line(g, ink, 3, 12, 6, 12);
    fill(g, disk, 2, 1, 11, 3);
    fill(g, arrow, 7, 6, 7, 3);
    fill(g, accent, 7, 11, 7, 3);
}

using painter = void (*)(gpx &);

// Command names and their drawings. The first eight are the suite's.
constexpr std::array<std::pair<std::string_view, painter>, 21> icons{{
    {"fileNew", draw_new},
    {"fileOpen", draw_open},
    {"fileSave", draw_save},
    {"undo", draw_undo},
    {"redo", draw_redo},
    {"cut", draw_cut},
    {"copy", draw_copy},
    {"paste", draw_paste},
    {"find", draw_find},
    {"getMail", draw_get_mail},
    {"sendMail", draw_send_mail},
    {"reply", draw_reply},
    {"replyAll", draw_reply_all},
    {"forward", draw_forward},
    {"deleteMail", draw_delete_mail},
    {"nextUnread", draw_next_unread},
    {"previousUnread", draw_previous_unread},
    {"markMessage", draw_mark},
    {"addressBook", draw_address_book},
    {"attachFile", draw_attach},
    {"threadView", draw_threads},
}};

} // namespace

// Export both variants without starting a window or using a toolkit API.
int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: export_icons OUTPUT_DIRECTORY\n";
        return 2;
    }
    try {
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output / "color");
        std::filesystem::create_directories(output / "monochrome");
        for (const auto &[name, draw] : icons) {
            native::img image(16, 16);
            auto &graphics = image.get_gpx();
            graphics.set_clip(rect(0, 0, 16, 16));
            graphics.clear(rgba(0, 0, 0, 0));
            draw(graphics);
            const auto filename = std::string(name) + ".png";
            image.save(output / "color" / filename);
            // Two-pixel strokes retain the arrow shape when GEM applies
            // its disabled checkerboard. Keep the color artwork unchanged.
            if (name == "undo" || name == "redo") {
                graphics.clear(rgba(0, 0, 0, 0));
                draw_turn(graphics, name == "undo" ? 1 : -1, 2);
            }
            // Preserve outlines; remove paper and folder interiors so the
            // Native toolbar can tint all ink, including reverse video.
            for (int index = 0; index < 16 * 16; ++index) {
                auto &pixel = image.pixels()[index];
                const bool interior = pixel == paper || pixel == folder;
                pixel = rgba(0, 0, 0, pixel.a && !interior ? 255 : 0);
            }
            image.save(output / "monochrome" / filename);
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
