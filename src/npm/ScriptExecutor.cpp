#include "ScriptExecutor.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
namespace protojs {
int ScriptExecutor::executeScript(const std::string& scriptName, const std::string& packageJsonPath, const std::vector<std::string>& args) {
    std::ifstream file(packageJsonPath);
    if (!file.is_open()) return 1;
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    size_t scriptsPos = content.find("\"scripts\"");
    if (scriptsPos == std::string::npos) return 1;
    size_t scriptPos = content.find("\"" + scriptName + "\"", scriptsPos);
    if (scriptPos == std::string::npos) return 1;
    size_t colonPos = content.find(':', scriptPos);
    size_t startQuote = content.find('"', colonPos) + 1;
    size_t endQuote = content.find('"', startQuote);
    if (endQuote == std::string::npos) return 1;
    std::string command = content.substr(startQuote, endQuote - startQuote);
    return system(command.c_str());
}
} // namespace protojs
