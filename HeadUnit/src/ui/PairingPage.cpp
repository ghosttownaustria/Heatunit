#include "ui/PairingPage.h"
#include "androidauto/ProjectionKeys.h"
#include "ui/MenuStyle.h"
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
// The question in the column right of the Android Auto tile, the code large in the lit orange.
constexpr double kPhoneBaseline = 172, kCodeBaseline = 300, kHintBaseline = 350;
constexpr double kButtonTop = 400, kButtonHeight = 80, kButtonWidth = 240, kButtonGap = 30;
constexpr int kButtonCount = 2;
constexpr int kTimeoutMs = 60000;
const char* const kButtonTexts[kButtonCount] = {"Pair", "Cancel"};
}

// A page without a question; an unanswered question is refused after a minute.
PairingPage::PairingPage(QWidget* parent) : MenuPage(parent), m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] { Answer(false); });
}

// Who hears that the question came or went: the window shows or hides the page.
void PairingPage::SetChangeHandler(std::function<void()> handler)
{
    m_onChange = std::move(handler);
}

// Shows the question; `answer` gets the choice, at most once. A question still open is refused first.
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
    if (m_onChange) m_onChange();
}

// The question is over without an answer here (the phone cancelled, or the pairing finished).
void PairingPage::End()
{
    if (!m_answer) return;
    m_answer = nullptr;
    m_timeout->stop();
    if (m_onChange) m_onChange();
}

// Whether a question waits for its answer.
bool PairingPage::IsAsking() const
{
    return static_cast<bool>(m_answer);
}

// Turning chooses between the two buttons.
void PairingPage::Turn(int steps)
{
    m_focus = std::clamp(m_focus + steps, 0, kButtonCount - 1);
    update();
}

// Left chooses "Pair", right "Cancel".
void PairingPage::Nudge(unsigned keycode)
{
    if (keycode == keys::DpadLeft) m_focus = 0;
    else if (keycode == keys::DpadRight) m_focus = 1;
    update();
}

// Pushing answers with the focused button.
void PairingPage::Push()
{
    Answer(m_focus == 0);
}

// Back cancels the pairing.
bool PairingPage::Back()
{
    if (!m_answer) return false;
    Answer(false);
    return true;
}

// Draws the Android Auto tile, the question, the code grouped in threes (as the phone shows it) and the two buttons.
void PairingPage::Paint(QPainter& painter)
{
    using namespace menu;
    DrawTile(painter, kPageTile, HomeMenuEntry::AndroidAuto, true);
    const QFont titleFont = Font(30);
    const QFont phoneFont = Font(24);
    const QFont codeFont = Font(110);
    const QFont hintFont = Font(22);
    const double width = Width() - kPageTile.left() - kPageColumnLeft;
    painter.setFont(titleFont);
    painter.setPen(kText);
    painter.drawText(QPointF(kPageColumnLeft, kPageTitleBaseline), Elided("Bluetooth pairing", titleFont, width));
    painter.setFont(phoneFont);
    painter.setPen(kDim);
    painter.drawText(QPointF(kPageColumnLeft, kPhoneBaseline), Elided(m_phone + " wants to pair", phoneFont, width));
    painter.setFont(codeFont);
    painter.setPen(kOrange);
    painter.drawText(QPointF(kPageColumnLeft, kCodeBaseline), m_code.size() == 6 ? m_code.left(3) + " " + m_code.mid(3) : m_code);
    painter.setFont(hintFont);
    painter.setPen(kDim);
    painter.drawText(QPointF(kPageColumnLeft, kHintBaseline), Elided("Pair if the phone shows the same code, and confirm there too.", hintFont, width));
    for (int index = 0; index < kButtonCount; ++index) DrawTextButton(painter, ButtonRect(index), kButtonTexts[index], index == m_focus);
}

// Remembers the button a click starts on.
void PairingPage::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_pressed = ButtonAt(event->position());
}

// A click that ends on the button it started on answers with it.
void PairingPage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    if (!pressed || pressed != ButtonAt(event->position())) return;
    m_focus = *pressed;
    Answer(*pressed == 0);
}

// Hands the answer over (once) and takes the question away.
void PairingPage::Answer(bool isAccepted)
{
    if (!m_answer) return;
    std::exchange(m_answer, nullptr)(isAccepted);
    m_timeout->stop();
    if (m_onChange) m_onChange();
}

// Where button `index` is, in design units.
QRectF PairingPage::ButtonRect(int index) const
{
    return QRectF(menu::kPageColumnLeft + index * (kButtonWidth + kButtonGap), kButtonTop, kButtonWidth, kButtonHeight);
}

// The button at a widget position.
std::optional<int> PairingPage::ButtonAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    for (int index = 0; index < kButtonCount; ++index) {
        if (ButtonRect(index).contains(*point)) return index;
    }
    return std::nullopt;
}
}
