#pragma once
#include <cctype>
#include <cstdlib>
#include <gdiplus.h>
#include <windows.h>
namespace starlight::editor::style {
// Original Starlight vector artwork, translated from BuildDock.cs.
inline const char *dockIcon(int n) {
    static const char *paths[] = {
        "M1,20 L6,20 Q12,4 18,20 L23,20 M12,13 L12,1 M8,5 L12,1 16,5",       // raise
        "M1,17 Q6,4 12,14 Q18,24 23,9 M2,22 L22,22",                         // smooth
        "M1,18 L23,18 M12,1 L12,13 M8,9 L12,13 16,9",                        // flatten
        "M4,17 L15,3 21,8 9,21 Z M3,19 Q0,24 7,23",                          // paint
        "M1,22 Q12,-5 23,22 Z",                                              // hill
        "M1,22 L6,5 18,5 23,22 Z",                                           // plateau
        "M1,6 L6,6 Q12,25 18,6 L23,6 M1,22 L23,22",                          // crater
        "M12,1 Q27,18 17,22 Q1,28 5,14 Z M2,12 L22,12",                      // water-source
        "M12,1 L22,20 2,20 Z M12,8 L12,14",                                  // lava-source
        "M12,1 Q27,18 17,22 Q1,28 5,14 Z",                                   // water
        "M12,1 L23,7 23,18 12,24 1,18 1,7 Z M1,7 L12,13 23,7 M12,13 L12,24", // cube
        "M7,3 L23,7 23,19 17,23 1,19 1,7 Z M1,7 L17,11 23,7 M17,11 L17,23",  // rectangle
        "M1,22 L7,3 17,3 23,22 Z M7,3 L10,8 20,8 M20,8 L23,22",              // trapezoid
        "M7,1 L17,1 23,7 23,17 17,23 7,23 1,17 1,7 Z",                       // octagon
        "M12,1 L12,23 M1,12 L23,12 M8,5 L12,1 16,5 M8,19 L12,23 16,19 M5,8 L1,12 5,16 M19,8 L23,12 "
        "19,16",                                           // move
        "M20,7 A10,10 0 1 0 22,17 M16,7 L22,7 22,1",       // rotate
        "M2,22 L22,2 M14,2 L22,2 22,10 M2,14 L2,22 10,22", // scale
        "M9,4 L3,10 9,16 M3,10 L14,10 Q22,10 22,20", // undo
        "M15,4 L21,10 15,16 M21,10 L10,10 Q2,10 2,20", // redo
        "M2,6 L22,6 M2,12 L22,12 M2,18 L22,18 M7,3 L7,9 M17,9 L17,15 M10,15 L10,21", // settings
        "M5,5 L19,19 M19,5 L5,19", // close
    };
    return n >= 0 && n < int(sizeof(paths) / sizeof(paths[0])) ? paths[n] : nullptr;
}
inline void drawDockIcon(Gdiplus::Graphics &g, int n, float width, float height) {
    const char *s = dockIcon(n);
    if (!s)
        return;
    auto state = g.Save();
    g.TranslateTransform((width - 22) / 2, (height - 22) / 2);
    g.ScaleTransform(22.f / 24, 22.f / 24);
    Gdiplus::GraphicsPath path;
    float x = 0, y = 0;
    char op = 0;
    auto number = [&]() {
        while (*s && (std::isspace(static_cast<unsigned char>(*s)) || *s == ','))
            ++s;
        char *end = nullptr;
        float v = std::strtof(s, &end);
        s = end;
        return v;
    };
    if (n == 15) {
        path.AddArc(2.f, 2.f, 20.f, 20.f, 30.f, 285.f);
        path.StartFigure();
        path.AddLine(16.f, 7.f, 22.f, 7.f);
        path.AddLine(22.f, 7.f, 22.f, 1.f);
    } else
        while (*s) {
            while (*s && (std::isspace(static_cast<unsigned char>(*s)) || *s == ','))
                ++s;
            if (!*s)
                break;
            if (std::isalpha(static_cast<unsigned char>(*s)))
                op = *s++;
            if (op == 'Z') {
                path.CloseFigure();
                op = 0;
                continue;
            }
            if (op == 'M') {
                x = number();
                y = number();
                path.StartFigure();
                op = 'L';
            } else if (op == 'L') {
                float nx = number(), ny = number();
                path.AddLine(x, y, nx, ny);
                x = nx;
                y = ny;
            } else if (op == 'Q') {
                float cx = number(), cy = number(), nx = number(), ny = number();
                path.AddBezier(x, y, x + (cx - x) * 2 / 3, y + (cy - y) * 2 / 3, nx + (cx - nx) * 2 / 3,
                               ny + (cy - ny) * 2 / 3, nx, ny);
                x = nx;
                y = ny;
            } else
                break;
        }
    Gdiplus::Pen pen(Gdiplus::Color(255, 191, 217, 244), 1.8f);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawPath(&pen, &path);
    g.Restore(state);
}
} // namespace starlight::editor::style
