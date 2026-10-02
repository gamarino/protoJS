#include "REPL.h"
#include "../MicrotaskQueue.h"
#include "../JSContext.h"
#include "quickjs.h"
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "../platform/Posix.h" // <windows.h>, widen/narrow
#endif

namespace protojs {

void REPL::start(JSContext* ctx) {
    std::cout << "protoJS REPL v0.1.0" << std::endl;
    std::cout << "Type .help for commands, .exit to exit" << std::endl;

    std::vector<std::string> history;
    std::string input;
    int lineCount = 0;

    JSContextWrapper* w =
        static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    // As in Node's REPL, an unhandled rejection is reported and the session
    // goes on (MicrotaskQueue.h).
    MicrotaskQueue::setUnhandledRejectionsAreFatal(false);
    proto::ProtoContext* pctx = w ? w->getProtoContext() : nullptr;

    while (true) {
        if (lineCount == 0) {
            std::cout << "> ";
        } else {
            std::cout << "... ";
        }

        std::string line;
        bool haveLine;
        {
            proto::ProtoContext::UnmanagedScope u(pctx);
            haveLine = readLine(line);
        }
        if (!haveLine) {
            // End of input (a closed pipe, Ctrl+D, or Ctrl+Z at a Windows
            // console): leave as Node's REPL does, after a final newline so
            // the shell prompt does not follow ours on the same line.
            std::cout << std::endl;
            return;
        }
        if (line.empty() && lineCount == 0) {
            continue;
        }
        
        if (lineCount == 0 && isSpecialCommand(line)) {
            if (handleSpecialCommand(ctx, line)) {
                return;
            }
            continue;
        }
        
        input += line;
        if (lineCount > 0) {
            input += "\n";
        }
        lineCount++;
        
        if (isCompleteInput(input) || line.empty()) {
            if (!input.empty()) {
                history.push_back(input);

                JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
                JSValue result;
                if (!wrapper) {
                    std::cerr << "[REPL] No JSContextWrapper; cannot evaluate." << std::endl;
                    result = JS_UNDEFINED;
                } else {
                    result = wrapper->eval(input, "<repl>");
                }

                if (JS_IsException(result)) {
                    printError(ctx, JS_GetException(ctx));
                } else if (!JS_IsUndefined(result)) {
                    printResult(ctx, result);
                }

                JS_FreeValue(ctx, result);
            }
            
            input.clear();
            lineCount = 0;
        }
    }
}

bool REPL::readLine(std::string& line) {
    line.clear();
#if defined(_WIN32)
    // A console is read as UTF-16 and converted to UTF-8, so non-ASCII input
    // works whatever the console's code page; the narrow C runtime read would
    // hand over code-page bytes (or nothing, for characters outside it).
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (in != INVALID_HANDLE_VALUE && in != nullptr && ::GetConsoleMode(in, &mode)) {
        std::wstring wline;
        wchar_t buf[512];
        for (;;) {
            DWORD got = 0;
            if (!::ReadConsoleW(in, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])), &got,
                                nullptr) || got == 0) {
                if (wline.empty()) return false;
                break;
            }
            wline.append(buf, got);
            if (wline.back() == L'\n') break;
        }
        // Ctrl+Z at the start of a line is the console's end of input.
        if (!wline.empty() && wline[0] == 0x1A) return false;
        line = protojs::platform::narrow(wline);
    } else
#endif
    {
        if (!std::getline(std::cin, line)) {
            if (line.empty()) return false;
        }
    }
    // A line may end in "\r\n": a Windows console, or input piped from a
    // Windows program, through standard streams that are binary on every
    // platform (main.cpp, prepareStandardStreams). The terminator is not part
    // of the line, or ".exit\r" would not be ".exit" and "\r" not blank.
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return true;
}

bool REPL::isCompleteInput(const std::string& input) {
    // Simple check: count braces, brackets, parentheses
    int braces = 0, brackets = 0, parens = 0;
    bool inString = false;
    char stringChar = 0;
    
    for (char c : input) {
        if (!inString && (c == '"' || c == '\'')) {
            inString = true;
            stringChar = c;
        } else if (inString && c == stringChar) {
            inString = false;
        } else if (!inString) {
            if (c == '{') braces++;
            else if (c == '}') braces--;
            else if (c == '[') brackets++;
            else if (c == ']') brackets--;
            else if (c == '(') parens++;
            else if (c == ')') parens--;
        }
    }
    
    return braces == 0 && brackets == 0 && parens == 0 && !inString;
}

void REPL::printResult(JSContext* ctx, JSValue result) {
    const char* str = JS_ToCString(ctx, result);
    if (str) {
        std::cout << str << std::endl;
        JS_FreeCString(ctx, str);
    }
}

void REPL::printError(JSContext* ctx, JSValue exception) {
    const char* error = JS_ToCString(ctx, exception);
    if (error) {
        std::cerr << "Error: " << error << std::endl;
        JS_FreeCString(ctx, error);
    }
}

bool REPL::isSpecialCommand(const std::string& input) {
    return input.length() > 0 && input[0] == '.';
}

bool REPL::handleSpecialCommand(JSContext* /*ctx*/, const std::string& command) {
    if (command == ".exit" || command == ".quit") {
        // Return to main() instead of calling exit(): exit() ran the static
        // destructors while the runtime's threads and the protoCore space were
        // still alive, and the process crashed on the way out.
        std::cout << "Exiting REPL" << std::endl;
        return true;
    } else if (command == ".help") {
        std::cout << "Special commands:" << std::endl;
        std::cout << "  .help    Show this help" << std::endl;
        std::cout << "  .exit    Exit REPL" << std::endl;
        std::cout << "  .clear   Clear screen (not implemented)" << std::endl;
    } else if (command == ".clear") {
        // Clear screen (basic)
        std::cout << "\033[2J\033[H";
    } else {
        std::cout << "Unknown command: " << command << std::endl;
    }
    return false;
}

} // namespace protojs
