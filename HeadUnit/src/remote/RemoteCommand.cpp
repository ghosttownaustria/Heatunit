#include "remote/RemoteCommand.h"
#include "androidauto/ProjectionKeys.h"
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <map>
#include <optional>
#include <utility>

namespace headunit {
namespace {
constexpr int kMaxDetents = 20;
constexpr int kMaxVolumeDelta = 30;

// One value of a command line. Only text and whole numbers are used; anything else (true, null, fractions, numbers too
// large for a long long) is kept as Other so that a field of the wrong kind can be refused by name.
struct Field {
    enum class Kind { Text, Integer, Other };
    Kind kind{Kind::Other};
    std::string text;
    long long number{};
};

using Fields = std::map<std::string, Field>;

// Reads one flat JSON object: string keys, and text, number, true, false or null values (no nesting).
class FlatJsonReader {
public:
    explicit FlatJsonReader(const std::string& text) : m_text(text) {}

    // Reads the whole text as one object; on failure `error` says what was wrong.
    bool Read(Fields& fields, std::string& error)
    {
        error = "invalid JSON: a single flat object is expected";
        if (!Expect('{')) return false;
        SkipSpace();
        if (Peek() == '}') {
            ++m_position;
        } else {
            do {
                std::string key;
                Field value;
                if (!ReadString(key) || !Expect(':') || !ReadValue(value)) return false;
                if (!fields.emplace(std::move(key), std::move(value)).second) {
                    error = "invalid JSON: a field appears twice";
                    return false;
                }
            } while (Accept(','));
            if (!Expect('}')) return false;
        }
        SkipSpace();
        return m_position == m_text.size();
    }

private:
    const std::string& m_text;
    std::size_t m_position{};

    char Peek() const { return m_position < m_text.size() ? m_text[m_position] : '\0'; }

    void SkipSpace()
    {
        while (m_position < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_position]))) ++m_position;
    }

    // Takes the next non-space character when it is `expected`.
    bool Accept(char expected)
    {
        SkipSpace();
        if (Peek() != expected) return false;
        ++m_position;
        return true;
    }

    bool Expect(char expected) { return Accept(expected); }

    // Reads a quoted string with the usual escapes; \u escapes become '?' unless they are plain ASCII.
    bool ReadString(std::string& result)
    {
        if (!Expect('"')) return false;
        while (m_position < m_text.size()) {
            const char c = m_text[m_position++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                result += c;
            } else if (!ReadEscape(result)) {
                return false;
            }
        }
        return false;
    }

    // Reads what follows a backslash inside a string.
    bool ReadEscape(std::string& result)
    {
        if (m_position >= m_text.size()) return false;
        const char c = m_text[m_position++];
        switch (c) {
        case '"': case '\\': case '/': result += c; return true;
        case 'b': result += '\b'; return true;
        case 'f': result += '\f'; return true;
        case 'n': result += '\n'; return true;
        case 'r': result += '\r'; return true;
        case 't': result += '\t'; return true;
        case 'u': return ReadUnicodeEscape(result);
        default: return false;
        }
    }

    bool ReadUnicodeEscape(std::string& result)
    {
        if (m_position + 4 > m_text.size()) return false;
        unsigned code = 0;
        for (int digit = 0; digit < 4; ++digit) {
            const char c = m_text[m_position++];
            if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
            code = code * 16 + static_cast<unsigned>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(c) - 'a' + 10);
        }
        result += code < 0x80 ? static_cast<char>(code) : '?';
        return true;
    }

    bool ReadValue(Field& value)
    {
        SkipSpace();
        const char c = Peek();
        if (c == '"') {
            value.kind = Field::Kind::Text;
            return ReadString(value.text);
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return ReadNumber(value);
        return ReadLiteral(value);
    }

    // Reads a number; only an integer without fraction or exponent that fits a long long becomes an Integer.
    bool ReadNumber(Field& value)
    {
        const std::size_t start = m_position;
        if (Peek() == '-') ++m_position;
        const std::size_t digitsStart = m_position;
        while (std::isdigit(static_cast<unsigned char>(Peek()))) ++m_position;
        if (m_position == digitsStart) return false;
        bool isWhole = true;
        while (Peek() == '.' || Peek() == 'e' || Peek() == 'E' || Peek() == '+' || Peek() == '-' || std::isdigit(static_cast<unsigned char>(Peek()))) {
            isWhole = false;
            ++m_position;
        }
        if (!isWhole) return true;
        const std::string token = m_text.substr(start, m_position - start);
        errno = 0;
        const long long number = std::strtoll(token.c_str(), nullptr, 10);
        if (errno != 0) return true;
        value.kind = Field::Kind::Integer;
        value.number = number;
        return true;
    }

    // Reads true, false or null.
    bool ReadLiteral(Field&)
    {
        for (const char* literal : {"true", "false", "null"}) {
            const std::string word = literal;
            if (m_text.compare(m_position, word.size(), word) == 0) {
                m_position += word.size();
                return true;
            }
        }
        return false;
    }
};

// The reply for a command that failed.
std::string ErrorReply(const std::string& message)
{
    return "{\"ok\":false,\"error\":\"" + message + "\"}";
}

const std::string kOkReply = "{\"ok\":true}";

// The text field `name`; on failure `error` says why.
std::optional<std::string> TextField(const Fields& fields, const std::string& name, std::string& error)
{
    const auto found = fields.find(name);
    if (found == fields.end()) {
        error = "missing field: " + name;
        return std::nullopt;
    }
    if (found->second.kind != Field::Kind::Text) {
        error = "field " + name + " must be text";
        return std::nullopt;
    }
    return found->second.text;
}

// The whole number field `name` between -limit and limit but not 0; on failure `error` says why.
std::optional<int> CountField(const Fields& fields, const std::string& name, int limit, std::string& error)
{
    const auto found = fields.find(name);
    if (found == fields.end()) {
        error = "missing field: " + name;
        return std::nullopt;
    }
    const Field& field = found->second;
    if (field.kind != Field::Kind::Integer || field.number == 0 || field.number < -limit || field.number > limit) {
        error = "field " + name + " must be a whole number from -" + std::to_string(limit) + " to " + std::to_string(limit) + ", not 0";
        return std::nullopt;
    }
    return static_cast<int>(field.number);
}

// The value that goes with `name` in a table of names, or nullopt after saying so in `error`.
template <typename Value, std::size_t Count>
std::optional<Value> LookUp(const std::pair<const char*, Value> (&table)[Count], const std::string& name, const char* what, std::string& error)
{
    for (const auto& [known, value] : table) {
        if (name == known) return value;
    }
    error = std::string("unknown ") + what;
    return std::nullopt;
}

// A key that goes down and up again, as one tap on a button.
void Tap(const RemoteCommandDeps& deps, unsigned keycode)
{
    deps.sendKey(keycode, true);
    deps.sendKey(keycode, false);
}

// The console key command.
std::string RunConsole(const RemoteCommandDeps& deps, const Fields& fields)
{
    static const std::pair<const char*, ConsoleKey> kKeys[] = {{"home", ConsoleKey::Home}, {"menu", ConsoleKey::Menu}, {"option", ConsoleKey::Option},
        {"media", ConsoleKey::Media}, {"radio", ConsoleKey::Radio}, {"tel", ConsoleKey::Tel}, {"nav", ConsoleKey::Nav}, {"map", ConsoleKey::Map},
        {"back", ConsoleKey::Back}, {"projection", ConsoleKey::Projection}};
    std::string error;
    const auto name = TextField(fields, "key", error);
    if (!name) return ErrorReply(error);
    const auto key = LookUp(kKeys, *name, "console key", error);
    if (!key) return ErrorReply(error);
    deps.pressConsole(*key);
    return kOkReply;
}

// The media key command.
std::string RunKey(const RemoteCommandDeps& deps, const Fields& fields)
{
    static const std::pair<const char*, unsigned> kMediaKeys[] = {
        {"previous", keys::MediaPrevious}, {"play_pause", keys::MediaPlayPause}, {"next", keys::MediaNext}};
    std::string error;
    const auto name = TextField(fields, "name", error);
    if (!name) return ErrorReply(error);
    const auto keycode = LookUp(kMediaKeys, *name, "media key", error);
    if (!keycode) return ErrorReply(error);
    Tap(deps, *keycode);
    return kOkReply;
}

// The knob command: its push and its four arrows.
std::string RunKnob(const RemoteCommandDeps& deps, const Fields& fields)
{
    static const std::pair<const char*, unsigned> kActions[] = {{"press", keys::DpadCenter}, {"up", keys::DpadUp}, {"down", keys::DpadDown},
        {"left", keys::DpadLeft}, {"right", keys::DpadRight}};
    std::string error;
    const auto name = TextField(fields, "action", error);
    if (!name) return ErrorReply(error);
    const auto keycode = LookUp(kActions, *name, "knob action", error);
    if (!keycode) return ErrorReply(error);
    Tap(deps, *keycode);
    return kOkReply;
}

// The rotate command.
std::string RunRotate(const RemoteCommandDeps& deps, const Fields& fields)
{
    std::string error;
    const auto detents = CountField(fields, "detents", kMaxDetents, error);
    if (!detents) return ErrorReply(error);
    deps.rotate(*detents);
    return kOkReply;
}

// The volume command.
std::string RunVolume(const RemoteCommandDeps& deps, const Fields& fields)
{
    std::string error;
    const auto delta = CountField(fields, "delta", kMaxVolumeDelta, error);
    if (!delta) return ErrorReply(error);
    deps.changeVolume(*delta);
    return kOkReply;
}

// The reply of the status command.
std::string StatusReply(const RemoteStatus& status)
{
    return "{\"ok\":true,\"volume\":" + std::to_string(status.volume) + ",\"muted\":" + (status.isMuted ? "true" : "false") +
        ",\"page\":\"" + status.page + "\",\"projecting\":" + (status.isProjecting ? "true" : "false") + "}";
}
}

// The name of a screen in the status reply.
const char* RemotePageName(ConsoleController::Screen screen)
{
    switch (screen) {
    case ConsoleController::Screen::Projection: return "projection";
    case ConsoleController::Screen::ProjectionHome: return "projection_home";
    case ConsoleController::Screen::RadioHome: return "radio_home";
    case ConsoleController::Screen::Multimedia: return "multimedia";
    case ConsoleController::Screen::Radio: return "radio";
    case ConsoleController::Screen::Settings: return "settings";
    case ConsoleController::Screen::Bluetooth: return "bluetooth";
    }
    return "unknown";
}

RemoteCommand::RemoteCommand(RemoteCommandDeps deps) : m_deps(std::move(deps))
{
}

// Reads one line and does what it asks; the result is the reply line (without the line break).
std::string RemoteCommand::Execute(const std::string& line)
{
    Fields fields;
    std::string error;
    if (!FlatJsonReader(line).Read(fields, error)) return ErrorReply(error);
    const auto command = TextField(fields, "cmd", error);
    if (!command) return ErrorReply(error);
    if (*command == "console") return RunConsole(m_deps, fields);
    if (*command == "key") return RunKey(m_deps, fields);
    if (*command == "knob") return RunKnob(m_deps, fields);
    if (*command == "rotate") return RunRotate(m_deps, fields);
    if (*command == "volume") return RunVolume(m_deps, fields);
    if (*command == "mute") {
        m_deps.toggleMute();
        return kOkReply;
    }
    if (*command == "status") return StatusReply(m_deps.readStatus());
    return ErrorReply("unknown command");
}
}
