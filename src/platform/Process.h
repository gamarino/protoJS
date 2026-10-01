/*
 * Process — starting and signalling child processes on Windows.
 *
 * POSIX protoJS forks and execs (child_process) and signals with kill(). Windows
 * has neither fork nor signals; these helpers give child_process the same
 * observable contract there: a new process running the command, identified by
 * its process id, which kill() terminates. Only used under _WIN32.
 */
#ifndef PROTOJS_PLATFORM_PROCESS_H
#define PROTOJS_PLATFORM_PROCESS_H

#if defined(_WIN32)

#include "Posix.h"

#include <string>
#include <vector>

namespace protojs::platform {

// Quote one argument so that the C runtime of the child (CommandLineToArgvW
// rules) reads it back unchanged.
inline std::wstring quoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++backslashes; }
        if (i == arg.size()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

// Start a process with an already built command line; returns its id, or -1.
inline int startProcess(std::wstring commandLine) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr,
                          /*bInheritHandles=*/TRUE, 0, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return static_cast<int>(pi.dwProcessId);
}

// spawn(command, args): the program is looked up on PATH as execvp does.
inline int spawnProcess(const std::vector<std::string>& argv) {
    std::wstring line;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i) line.push_back(L' ');
        line += quoteArgument(widen(argv[i]));
    }
    return startProcess(line);
}

// exec(line): the command interpreter runs the line, as /bin/sh -c does.
inline int spawnShell(const std::string& commandLine) {
    wchar_t comspec[MAX_PATH];
    const DWORD n = ::GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH);
    std::wstring shell = (n > 0 && n < MAX_PATH) ? std::wstring(comspec, n) : L"cmd.exe";
    return startProcess(quoteArgument(shell) + L" /d /s /c \"" + widen(commandLine) + L"\"");
}

// kill(pid, sig): Windows has no signals; every signal terminates the process.
inline int killProcess(int pid, int /*sig*/) {
    HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!h) return -1;
    const BOOL ok = ::TerminateProcess(h, 1);
    ::CloseHandle(h);
    return ok ? 0 : -1;
}

} // namespace protojs::platform

#endif // _WIN32

#endif // PROTOJS_PLATFORM_PROCESS_H
