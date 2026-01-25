#include "REPL.h"
#include <iostream>
#include <string>
#include <vector>

namespace protojs {

void REPL::start(JSContext* ctx) {
    std::cout << "protoJS REPL v0.1.0" << std::endl;
    std::cout << "Type .help for commands, .exit to exit" << std::endl;
    
    std::vector<std::string> history;
    std::string input;
    int lineCount = 0;
    
    while (true) {
        if (lineCount == 0) {
            std::cout << "> ";
        } else {
            std::cout << "... ";
        }
        
        std::string line = readLine();
        if (line.empty() && lineCount == 0) {
            continue;
        }
        
        if (lineCount == 0 && isSpecialCommand(line)) {
            handleSpecialCommand(ctx, line);
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
                
                JSValue result = JS_Eval(ctx, input.c_str(), input.length(), "<repl>", JS_EVAL_TYPE_GLOBAL);
                
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

std::string REPL::readLine() {
    std::string line;
    std::getline(std::cin, line);
    return line;
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

void REPL::handleSpecialCommand(JSContext* ctx, const std::string& command) {
    if (command == ".exit" || command == ".quit") {
        std::cout << "Exiting REPL" << std::endl;
        exit(0);
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
}

} // namespace protojs
