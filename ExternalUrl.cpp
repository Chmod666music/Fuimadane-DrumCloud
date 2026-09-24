#include "ExternalUrl.hpp"

#if defined(_WIN32)
# include <windows.h>
# include <shellapi.h>
#else
# include <cerrno>
# include <cstdlib>
# include <sys/types.h>
# include <sys/wait.h>
# include <unistd.h>
#endif

namespace DrumCloud {

bool openExternalUrl(const char* const url) noexcept
{
    if (url == nullptr || url[0] == '\0')
        return false;

#if defined(_WIN32)
    const HINSTANCE result = ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    // Do not ask the plugin host to open the URL: several DAWs deliberately
    // route that request to an embedded browser. A double fork lets the OS
    // opener outlive the short UI callback without leaving a zombie in the
    // host process.
    const pid_t child = fork();
    if (child < 0)
        return false;

    if (child == 0)
    {
        const pid_t launcher = fork();
        if (launcher < 0)
            _exit(1);
        if (launcher > 0)
            _exit(0);

# if defined(__APPLE__)
        execl("/usr/bin/open", "open", url, static_cast<char*>(nullptr));
# else
        execlp("xdg-open", "xdg-open", url, static_cast<char*>(nullptr));
        execlp("gio", "gio", "open", url, static_cast<char*>(nullptr));
# endif
        _exit(127);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

} // namespace DrumCloud
