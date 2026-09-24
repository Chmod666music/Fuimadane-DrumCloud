#pragma once

namespace DrumCloud {

// Launches a trusted URL in the user's system browser, independently of the
// plugin host. Returns whether the platform launcher was started.
bool openExternalUrl(const char* url) noexcept;

} // namespace DrumCloud
