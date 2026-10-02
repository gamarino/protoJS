/*
 * Process — starting and signalling child processes on Windows.
 *
 * POSIX protoJS forks and execs (child_process) and signals with kill(). Windows
 * has neither fork nor signals; these helpers give child_process the same
 * observable contract there: a new process running the command, identified by
 * its process id, which kill() terminates (signal 0 only checks that it runs).
 * Only used under _WIN32.
 */
#ifndef PROTOJS_PLATFORM_PROCESS_H
#define PROTOJS_PLATFORM_PROCESS_H

#if defined(_WIN32)

#include "Posix.h"

#include <mutex>
#include <string>
#include <unordered_map>
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

// The processes this program started, by id, with the handle CreateProcess
// returned. The handle is kept open for as long as protojs runs: an open
// handle keeps the process object -- and so its id -- from being reused after
// the child exits, so kill(pid) can never reach an unrelated process that
// happened to get the same id, and kill(pid, 0) can tell "exited" from
// "running". (POSIX has the same guarantee until the parent reaps the child.)
// A handle to an exited process costs a kernel object and no more.
class ChildProcessTable {
public:
    static ChildProcessTable& instance() {
        static ChildProcessTable table;
        return table;
    }
    void add(int pid, HANDLE h) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handles_.find(pid);
        if (it != handles_.end()) ::CloseHandle(it->second);
        handles_[pid] = h;
    }
    // The handle of a child this program started, or nullptr.
    HANDLE find(int pid) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handles_.find(pid);
        return it == handles_.end() ? nullptr : it->second;
    }
    ~ChildProcessTable() {
        for (auto& entry : handles_) ::CloseHandle(entry.second);
    }
private:
    std::mutex mutex_;
    std::unordered_map<int, HANDLE> handles_;
};

// The standard handles of this process that a child may inherit: those that
// exist and are marked inheritable, without duplicates (stdout and stderr are
// often the same handle).
inline std::vector<HANDLE> inheritableStandardHandles() {
    std::vector<HANDLE> out;
    const DWORD ids[] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    for (DWORD id : ids) {
        HANDLE h = ::GetStdHandle(id);
        if (h == nullptr || h == INVALID_HANDLE_VALUE) continue;
        DWORD flags = 0;
        if (!::GetHandleInformation(h, &flags) || !(flags & HANDLE_FLAG_INHERIT)) continue;
        bool seen = false;
        for (HANDLE o : out) seen = seen || o == h;
        if (!seen) out.push_back(h);
    }
    return out;
}

// Start a process with an already built command line; returns its id, or -1.
//
// The child inherits the standard handles and nothing else. CreateProcess with
// bInheritHandles=TRUE would hand it EVERY inheritable handle of this process
// -- listening and connected sockets among them, since a Winsock socket is
// inheritable unless created otherwise -- so a child could keep a port bound
// after protojs closed it. PROC_THREAD_ATTRIBUTE_HANDLE_LIST restricts the
// inheritance to the list (Windows Vista and later).
inline int startProcess(std::wstring commandLine) {
    std::vector<HANDLE> inherit = inheritableStandardHandles();

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    std::vector<unsigned char> attrStorage;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = nullptr;
    if (!inherit.empty()) {
        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        attrStorage.resize(size);
        attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrStorage.data());
        if (!::InitializeProcThreadAttributeList(attrs, 1, 0, &size)) return -1;
        if (!::UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                         inherit.data(), inherit.size() * sizeof(HANDLE),
                                         nullptr, nullptr)) {
            ::DeleteProcThreadAttributeList(attrs);
            return -1;
        }
        si.lpAttributeList = attrs;
    }

    PROCESS_INFORMATION pi{};
    const BOOL ok = ::CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr,
                                     /*bInheritHandles=*/inherit.empty() ? FALSE : TRUE,
                                     attrs ? EXTENDED_STARTUPINFO_PRESENT : 0, nullptr, nullptr,
                                     &si.StartupInfo, &pi);
    if (attrs) ::DeleteProcThreadAttributeList(attrs);
    if (!ok) return -1;
    ::CloseHandle(pi.hThread);
    const int pid = static_cast<int>(pi.dwProcessId);
    ChildProcessTable::instance().add(pid, pi.hProcess);
    return pid;
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

// kill(pid, sig) with kill(2)'s contract: 0 on success, -1 with errno set.
// Windows has no signals. Signal 0 is the existence check: it succeeds while
// the process runs and fails with ESRCH once it has exited. Every other signal
// terminates the process. A child this program started is reached through the
// handle kept since CreateProcess (ChildProcessTable), never by reopening its
// id, which another process may have been given since.
inline int killProcess(int pid, int sig) {
    HANDLE h = ChildProcessTable::instance().find(pid);
    bool owned = false;
    if (!h) {
        h = ::OpenProcess(sig == 0 ? SYNCHRONIZE : (PROCESS_TERMINATE | SYNCHRONIZE), FALSE,
                          static_cast<DWORD>(pid));
        if (!h) {
            errno = ::GetLastError() == ERROR_ACCESS_DENIED ? EPERM : ESRCH;
            return -1;
        }
        owned = true;
    }
    const bool running = ::WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    int result = 0;
    if (!running) {
        errno = ESRCH;
        result = -1;
    } else if (sig != 0 && !::TerminateProcess(h, 1)) {
        errno = EPERM;
        result = -1;
    }
    if (owned) ::CloseHandle(h);
    return result;
}

} // namespace protojs::platform

#endif // _WIN32

#endif // PROTOJS_PLATFORM_PROCESS_H
