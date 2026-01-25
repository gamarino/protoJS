#ifndef PROTOJS_REPL_H
#define PROTOJS_REPL_H

#include "quickjs.h"

namespace protojs {

class REPL {
public:
    static void start(JSContext* ctx);
    
private:
    static std::string readLine();
    static bool isCompleteInput(const std::string& input);
    static void printResult(JSContext* ctx, JSValue result);
    static void printError(JSContext* ctx, JSValue exception);
    static bool isSpecialCommand(const std::string& input);
    static void handleSpecialCommand(JSContext* ctx, const std::string& command);
};

} // namespace protojs

#endif // PROTOJS_REPL_H
