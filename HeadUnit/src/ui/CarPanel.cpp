#include "ui/CarPanel.h"
#include "androidauto/ProjectionKeys.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>

namespace headunit {
namespace {
constexpr int kKeyHeight = 38;
const char* const kPanelStyle =
    "QPushButton { background: #2b303a; color: #e8ecf2; border: 1px solid #4a5364; border-radius: 6px; padding: 6px 4px; }"
    "QPushButton:hover { background: #363d4a; }"
    "QPushButton:pressed { background: #4c8dff; border-color: #8ab4ff; color: white; }";

// The projection key's symbol: a phone with a play triangle, drawn at twice the size so that it stays sharp.
QIcon ProjectionIcon(const QColor& colour)
{
    constexpr int kLogical = 32;
    QPixmap pixmap(kLogical * 2, kLogical * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPen pen(colour, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(6, 3, 15, 26), 3.2, 3.2);            // the phone
    painter.drawLine(QPointF(10.5, 24), QPointF(13.5, 24));              // its home bar
    // The play triangle overlaps the phone's edge: the edge under it is cleared first.
    const QPolygonF triangle({QPointF(17, 17), QPointF(28.5, 23), QPointF(17, 29)});
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.setPen(QPen(Qt::black, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::black);
    painter.drawPolygon(triangle);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(triangle);
    painter.end();
    return QIcon(pixmap);
}

// A thin horizontal line for the section heading.
QFrame* HorizontalRule(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet("color: #3a4252;");
    return line;
}
}

// Lays out the console from top to bottom: controller keys, knob, the other keys, volume and the audio display.
CarPanel::CarPanel(QWidget* parent) : QWidget(parent)
{
    setStyleSheet(kPanelStyle);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);
    AddControllerKeys(layout);
    AddKnob(layout);
    AddMoreKeys(layout);
    AddVolumeKeys(layout);
    m_display = new AudioDisplay(this);
    layout->addWidget(m_display);
    layout->addStretch(1);
}

// Who gets the controller keys (Home, Menu, Media, ...); what they mean is decided by the ConsoleController.
void CarPanel::SetConsoleHandler(std::function<void(ConsoleKey key)> handler)
{
    m_onConsole = std::move(handler);
}

// Who gets the keys that go straight to the phone: the controller's arrows and push (down and up together) and the
// track/play keys (down on press, up on release).
void CarPanel::SetKeyHandler(std::function<void(unsigned keycode, bool isDown)> handler)
{
    m_onKey = std::move(handler);
}

// Who gets the knob's turns.
void CarPanel::SetRotateHandler(std::function<void(int detents)> handler)
{
    m_onRotate = std::move(handler);
}

// Who gets the volume keys.
void CarPanel::SetVolumeHandler(std::function<void(int delta)> handler)
{
    m_onVolume = std::move(handler);
}

// Who gets the mute key.
void CarPanel::SetMuteHandler(std::function<void()> handler)
{
    m_onMute = std::move(handler);
}

// The audio readout at the bottom of the console.
AudioDisplay* CarPanel::Display()
{
    return m_display;
}

// The controller itself: MEDIA, TEL, NAV and the projection key in one row, HOME and BACK below.
void CarPanel::AddControllerKeys(QVBoxLayout* layout)
{
    auto* top = new QGridLayout();
    top->setSpacing(6);
    const auto hotKey = [&](const char* text, ConsoleKey key, const char* tip, int column) {
        auto* button = AddConsoleButton(text, key, tip);
        button->setFixedHeight(kKeyHeight);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        top->addWidget(button, 0, column);
        top->setColumnStretch(column, 1);
        return button;
    };
    hotKey("MEDIA", ConsoleKey::Media, "Medien: Spotify o.ae. auf dem Handy, ohne Handy der Musikordner des Radios (F3)", 0);
    hotKey("TEL", ConsoleKey::Tel, "Telefon auf dem Handy, Taste F5", 1);
    hotKey("NAV", ConsoleKey::Nav, "Navigation auf dem Handy, Taste F6", 2);
    auto* projection = hotKey("", ConsoleKey::Projection, "CarPlay / Android Auto: Projektion nach vorn holen; ohne Verbindung: verbinden (Taste F8)", 3);
    projection->setIcon(ProjectionIcon(QColor(232, 236, 242)));
    projection->setIconSize(QSize(28, 28));
    layout->addLayout(top);

    auto* home = new QHBoxLayout();
    const auto sideKey = [&](const char* text, ConsoleKey key, const char* tip) {
        auto* button = AddConsoleButton(text, key, tip);
        button->setFixedSize(96, kKeyHeight);
        home->addWidget(button);
    };
    sideKey("HOME", ConsoleKey::Home, "Android-Auto-Startbildschirm; nochmal: Startmenue des Radios (Pos1)");
    home->addStretch(1);
    sideKey("BACK", ConsoleKey::Back, "Zurueck (Esc)");
    layout->addLayout(home);
}

// The round controller with its four arrows; its arrows and push go to the phone like the arrow keys.
void CarPanel::AddKnob(QVBoxLayout* layout)
{
    m_knob = new RotaryKnob(this);
    m_knob->SetRotateHandler([this](int detents) {
        if (m_onRotate) m_onRotate(detents);
    });
    m_knob->SetPushHandler([this] { TapKey(keys::DpadCenter); });
    m_knob->SetNudgeHandler([this](unsigned keycode) { TapKey(keycode); });
    layout->addWidget(m_knob, 0, Qt::AlignHCenter);
}

// Everything else in a section of its own: MENU, OPTION, RADIO, MAP, then track skip and play/pause, which go straight
// to the phone's media session.
void CarPanel::AddMoreKeys(QVBoxLayout* layout)
{
    auto* heading = new QHBoxLayout();
    auto* caption = new QLabel("WEITERE TASTEN", this);
    caption->setStyleSheet("color: #8590a5; font-size: 10px;");
    heading->addWidget(HorizontalRule(this), 1);
    heading->addWidget(caption);
    heading->addWidget(HorizontalRule(this), 1);
    layout->addLayout(heading);

    auto* more = new QGridLayout();
    more->setSpacing(6);
    more->addWidget(AddConsoleButton("MENU", ConsoleKey::Menu, "Startmenue des Radios, Taste F1"), 0, 0);
    more->addWidget(AddConsoleButton("OPTION", ConsoleKey::Option, "Optionen/Kontextmenue der aktuellen Ansicht (F2)"), 0, 1);
    more->addWidget(AddConsoleButton("RADIO", ConsoleKey::Radio, "Radio: Internetradio nach Land, Taste F4"), 0, 2);
    more->addWidget(AddConsoleButton("MAP", ConsoleKey::Map, "Karte auf dem Handy, Taste F7"), 0, 3);
    layout->addLayout(more);

    auto* skip = new QHBoxLayout();
    skip->setSpacing(6);
    skip->addWidget(AddKeyButton(QString::fromUtf8("◀◀ Titel"), keys::MediaPrevious, "Voriger Titel (Bild hoch)"));
    skip->addWidget(AddKeyButton("Play/Pause", keys::MediaPlayPause, "Wiedergabe/Pause (Leertaste)"));
    skip->addWidget(AddKeyButton(QString::fromUtf8("Titel ▶▶"), keys::MediaNext, "Naechster Titel (Bild runter)"));
    layout->addLayout(skip);
}

// Volume down, mute and volume up.
void CarPanel::AddVolumeKeys(QVBoxLayout* layout)
{
    auto* volume = new QHBoxLayout();
    volume->setSpacing(6);
    auto* down = AddPlainButton("Leiser  -", "Lautstaerke verringern (Taste -)");
    auto* mute = AddPlainButton("Stumm", "Ton aus/an (Taste M)");
    auto* up = AddPlainButton("Lauter  +", "Lautstaerke erhoehen (Taste +)");
    connect(down, &QPushButton::clicked, this, [this] {
        if (m_onVolume) m_onVolume(-1);
    });
    connect(up, &QPushButton::clicked, this, [this] {
        if (m_onVolume) m_onVolume(+1);
    });
    connect(mute, &QPushButton::clicked, this, [this] {
        if (m_onMute) m_onMute();
    });
    volume->addWidget(down);
    volume->addWidget(mute);
    volume->addWidget(up);
    layout->addLayout(volume);
}

// A press and release of `keycode` for the phone.
void CarPanel::TapKey(unsigned keycode)
{
    if (!m_onKey) return;
    m_onKey(keycode, true);
    m_onKey(keycode, false);
}

// A button that sends `keycode`: down on press, up on release, so that a held key looks like a held key to the phone.
QPushButton* CarPanel::AddKeyButton(const QString& text, unsigned keycode, const QString& tip)
{
    auto* button = AddPlainButton(text, tip);
    connect(button, &QPushButton::pressed, this, [this, keycode] {
        if (m_onKey) m_onKey(keycode, true);
    });
    connect(button, &QPushButton::released, this, [this, keycode] {
        if (m_onKey) m_onKey(keycode, false);
    });
    return button;
}

// A controller key: it acts once per click; what it does depends on the console state.
QPushButton* CarPanel::AddConsoleButton(const QString& text, ConsoleKey key, const QString& tip)
{
    auto* button = AddPlainButton(text, tip);
    connect(button, &QPushButton::clicked, this, [this, key] {
        if (m_onConsole) m_onConsole(key);
    });
    return button;
}

// A button that never takes the keyboard focus (the keys work the window as a whole).
QPushButton* CarPanel::AddPlainButton(const QString& text, const QString& tip)
{
    auto* button = new QPushButton(text, this);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tip);
    return button;
}
}
