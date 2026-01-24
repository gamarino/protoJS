#ifndef PROTOJS_SCRIPTEXECUTOR_H
#define PROTOJS_SCRIPTEXECUTOR_H
#include <string>
#include <vector>
namespace protojs {
class ScriptExecutor {
public:
    static int executeScript(const std::string& scriptName, const std::string& packageJsonPath, const std::vector<std::string>& args);
};
} // namespace protojs
#endif
