#ifndef PROTOJS_REPL_H
#define PROTOJS_REPL_H

#include "quickjs.h"
#include <string>

namespace protojs {

class REPL {
public:
    /** Runs until .exit / .quit or the end of standard input, then returns. */
    static void start(JSContext* ctx);
    
private:
    /** Reads one line without its terminator ("\n" or "\r\n"); false at end of input. */
    static bool readLine(std::string& line);
    static bool isCompleteInput(const std::string& input);
    static void printResult(JSContext* ctx, JSValue result);
    static void printError(JSContext* ctx, JSValue exception);
    static bool isSpecialCommand(const std::string& input);
    /** Runs a dot-command; true when the REPL should end. */
    static bool handleSpecialCommand(JSContext* ctx, const std::string& command);
};

} // namespace protojs

#endif // PROTOJS_REPL_H
