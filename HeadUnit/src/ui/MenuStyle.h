#pragma once
#include "ui/HomeMenuLayout.h"
#include <QColor>
#include <QFont>
#include <QRectF>
#include <QString>

class QPainter;

// The look of the radio's own screens, taken from the home menu's design (docs/design/home-menu.svg): black screen,
// light grey frames and text, grey diagonal stripes, and orange for what is lit (the focus, or what plays). All sizes are
// design units (the screen is 600 high, see HomeMenuLayout.h).
namespace headunit::menu {
inline const QColor kText{213, 213, 213};
inline const QColor kDim{140, 140, 140};
inline const QColor kLine{50, 50, 50};
inline const QColor kFaintFrame{85, 85, 85};
inline const QColor kOrange{255, 118, 0};
constexpr double kFrameWidth = 3.94;
constexpr int kFontSize = 33;

// The design's font (Roboto, else the system's sans serif), `pixels` design units high.
QFont Font(int pixels);
// `text` shortened with "..." to fit `width` in `font`.
QString Elided(const QString& text, const QFont& font, double width);
// A tile as on the home menu: stripes, symbol, frame and title. Lit: stripes and symbol in orange.
void DrawTile(QPainter& painter, const QRectF& tile, HomeMenuEntry entry, bool isLit);
// Marks what the controller is on: the tiles' frame with small orange stripes in two corners.
void DrawFocus(QPainter& painter, const QRectF& box);

enum class Symbol { Previous, Play, Pause, Stop, Next };
// A framed button like a small tile. `isOn` draws the symbol in orange (the player is playing).
void DrawButton(QPainter& painter, const QRectF& box, Symbol symbol, bool isFocused, bool isOn = false);
void DrawTextButton(QPainter& painter, const QRectF& box, const QString& text, bool isFocused);
// One row of a list: text at the left, a dim note at the right. The current row (what plays) has its text in orange.
void DrawRow(QPainter& painter, const QRectF& row, const QString& text, const QString& note, bool isFocused, bool isCurrent);
}
