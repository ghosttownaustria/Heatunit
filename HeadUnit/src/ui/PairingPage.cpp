#include "ui/PairingPage.h"
#include "ui/MenuStyle.h"
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
// Layout in design units, like the settings page: the Android Auto tile at the left, the question in the column at its
// right, the code large in the lit orange.
const QRectF kTile(25, 100, 250, 400);
constexpr double kLeft = 300;
constexpr double kTitleBaseline = 132, kPhoneBaseline = 172, kCodeBaseline = 300, kHintBaseline = 350;
constexpr double kButtonTop = 400, kButtonHeight = 80, kButtonWidth = 240, kButtonGap = 30;
constexpr int kTimeoutMs = 60000;
const char* const kButtonTexts[] = {"Pair", "Cancel"};
}

PairingPage::PairingPage(QWidget* parent) : MenuPage(parent), m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] { Answer(false); });
}

void PairingPage::Ask(const QString& phone, const QString& code, std::function<void(bool)> answer)
{
    if (m_answer) std::exchange(m_answer, nullptr)(false);
    m_phone = phone;
    m_code = code;
    m_answer = std::move(answer);
    m_focus = 0;
    m_pressed.reset();
    m_timeout->start(kTimeoutMs);
    update();
    if (onChange) onChange();
}
void PairingPage::End()
{
    if (!m_answer) return;
    m_answer = nullptr;
    m_timeout->stop();
    if (onChange) onChange();
}
void PairingPage::Answer(bool isAccepted)
{
    if (!m_answer) return;
    std::exchange(m_answer, nullptr)(isAccepted);
    m_timeout->stop();
    if (onChange) onChange();
}
void PairingPage::Turn(int steps)
{
    m_focus = std::clamp(m_focus + steps, 0, 1);
    update();
}
void PairingPage::Nudge(unsigned keycode)
{
    if (keycode == keys::DpadLeft) m_focus = 0;
    else if (keycode == keys::DpadRight) m_focus = 1;
    update();
}
void PairingPage::Push() { Answer(m_focus == 0); }
bool PairingPage::Back()
{
    if (!m_answer) return false;
    Answer(false);
    return true;
}
QRectF PairingPage::ButtonRect(int index) const
{
    return QRectF(kLeft + index * (kButtonWidth + kButtonGap), kButtonTop, kButtonWidth, kButtonHeight);
}
std::optional<int> PairingPage::ButtonAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    for (int index = 0; index < 2; ++index)
        if (ButtonRect(index).contains(*point)) return index;
    return std::nullopt;
}
void PairingPage::Paint(QPainter& painter)
{
    using namespace menu;
    DrawTile(painter, kTile, HomeMenuEntry::AndroidAuto, true);
    const QFont titleFont = Font(30), phoneFont = Font(24), codeFont = Font(110), hintFont = Font(22);
    const double width = Width() - kTile.left() - kLeft;
    painter.setFont(titleFont);
    painter.setPen(kText);
    painter.drawText(QPointF(kLeft, kTitleBaseline), Elided("Bluetooth pairing", titleFont, width));
    painter.setFont(phoneFont);
    painter.setPen(kDim);
    painter.drawText(QPointF(kLeft, kPhoneBaseline), Elided(m_phone + " wants to pair", phoneFont, width));
    // Grouped in threes, as the phone shows it.
    painter.setFont(codeFont);
    painter.setPen(kOrange);
    painter.drawText(QPointF(kLeft, kCodeBaseline), m_code.size() == 6 ? m_code.left(3) + " " + m_code.mid(3) : m_code);
    painter.setFont(hintFont);
    painter.setPen(kDim);
    painter.drawText(QPointF(kLeft, kHintBaseline), Elided("Pair if the phone shows the same code, and confirm there too.", hintFont, width));
    for (int index = 0; index < 2; ++index) DrawTextButton(painter, ButtonRect(index), kButtonTexts[index], index == m_focus);
}
void PairingPage::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_pressed = ButtonAt(event->position());
}
void PairingPage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    if (!pressed || pressed != ButtonAt(event->position())) return;
    m_focus = *pressed;
    Answer(*pressed == 0);
}
}
