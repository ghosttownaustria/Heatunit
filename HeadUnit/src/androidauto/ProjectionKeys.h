#pragma once

// Android key codes the simulated car controls send. Values are Android's KeyEvent codes; the 0x10000 range are
// Android Auto's own car keys.
namespace headunit::keys {
inline constexpr unsigned Home = 3;
inline constexpr unsigned Back = 4;
inline constexpr unsigned Call = 5;
inline constexpr unsigned EndCall = 6;
inline constexpr unsigned DpadUp = 19;
inline constexpr unsigned DpadDown = 20;
inline constexpr unsigned DpadLeft = 21;
inline constexpr unsigned DpadRight = 22;
inline constexpr unsigned DpadCenter = 23;
inline constexpr unsigned Menu = 82;                 // context menu ("Option")
inline constexpr unsigned Search = 84;               // voice assistant on most phones
inline constexpr unsigned MediaPlayPause = 85;
inline constexpr unsigned MediaStop = 86;
inline constexpr unsigned MediaNext = 87;
inline constexpr unsigned MediaPrevious = 88;
inline constexpr unsigned MediaPlay = 126;
inline constexpr unsigned MediaPause = 127;
inline constexpr unsigned RotaryController = 65536;  // relative event: delta = detents turned
inline constexpr unsigned Media = 65537;
inline constexpr unsigned Navigation = 65538;
inline constexpr unsigned Tel = 65540;
// What the phone may bind; anything else it asks for is answered but never sent.
inline constexpr unsigned Supported[] = {Home, Back, Call, EndCall, DpadUp, DpadDown, DpadLeft, DpadRight, DpadCenter, Menu, Search,
    MediaPlayPause, MediaStop, MediaNext, MediaPrevious, MediaPlay, MediaPause, RotaryController, Media, Navigation, Tel};
}
