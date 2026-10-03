#pragma once

#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/colour.h>

namespace bv::theme {

inline wxColour Bg() { return wxColour(14, 15, 18); }
inline wxColour Elevated() { return wxColour(22, 24, 29); }
inline wxColour Field() { return wxColour(12, 13, 16); }
inline wxColour Hover() { return wxColour(34, 37, 44); }
inline wxColour Line() { return wxColour(42, 46, 54); }
inline wxColour Text() { return wxColour(243, 244, 246); }
inline wxColour Muted() { return wxColour(147, 155, 166); }
inline wxColour Faint() { return wxColour(94, 102, 114); }
inline wxColour Accent() { return wxColour(61, 222, 176); }
inline wxColour AccentInk() { return wxColour(7, 36, 28); }
inline wxColour Danger() { return wxColour(255, 93, 108); }
inline wxColour Warn() { return wxColour(231, 181, 73); }

inline wxFont Ui(int points, bool semibold = false) {
    wxFontInfo info(points);
    info.FaceName("Segoe UI");
    info.Weight(semibold ? wxFONTWEIGHT_SEMIBOLD : wxFONTWEIGHT_NORMAL);
    return wxFont(info);
}

inline wxFont Mono(int points) {
    wxFontInfo info(points);
    info.FaceName("Cascadia Mono");
    info.Family(wxFONTFAMILY_TELETYPE);
    return wxFont(info);
}

} // namespace bv::theme
