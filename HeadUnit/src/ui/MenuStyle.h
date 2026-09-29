#pragma once
#include "androidauto/DisplayConfig.h"
#include "ui/HomeMenuEntry.h"
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QRectF>
#include <QString>

class QPainter;
class QSizeF;

// The look of the radio's own screens, taken from the home menu's design (docs/design/home-menu.svg): black screen, light
// grey frames and text, grey diagonal stripes, and orange for what is lit (the focus, or what plays). All sizes are
// design units (the screen is 600 high, see HomeMenuLayout.h).
namespace headunit::menu {
inline const QColor kText{213, 213, 213};
inline const QColor kDim{140, 140, 140};
inline const QColor kLine{50, 50, 50};
inline const QColor kFaintFrame{85, 85, 85};
inline const QColor kOrange{255, 118, 0};
inline constexpr double kFrameWidth = 3.94;
inline constexpr int kFontSize = 33;

// The layout the radio's pages share: the page's tile at the left as on the home menu, everything else in the column at
// its right, with the page's title at the top of that column.
inline const QRectF kPageTile{25, 100, 250, 400};
inline constexpr double kPageColumnLeft = 300;
inline constexpr double kPageTitleBaseline = 132;

// The symbols of the player's buttons.
enum class Symbol { Previous, Play, Pause, Stop, Next };

// The symbols of the status bar (docs/design/heatunit.svg).
enum class Icon { Speaker, Microphone, Home };

QRectF ScreenRectIn(const QSizeF& area, const DisplayConfig& display);
QFont Font(int pixels);
QString Elided(const QString& text, const QFont& font, double width);
void DrawTile(QPainter& painter, const QRectF& tile, HomeMenuEntry entry, bool isLit);
void DrawFocus(QPainter& painter, const QRectF& box);
void DrawButton(QPainter& painter, const QRectF& box, Symbol symbol, bool isFocused, bool isOn = false);
void DrawTextButton(QPainter& painter, const QRectF& box, const QString& text, bool isFocused);
void DrawCheck(QPainter& painter, const QRectF& box, bool isOn);
void DrawRow(QPainter& painter, const QRectF& row, const QString& text, const QString& note, bool isFocused, bool isCurrent);
void DrawIcon(QPainter& painter, Icon icon, const QRectF& box, bool isStruck, bool isLit = false);
QBrush LitBrush(const QRectF& box);
}
