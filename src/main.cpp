#include "JSContext.h"
#include "Deferred.h"
#include "EventLoop.h"
#include "console.h"
#include "modules/IOModule.h"
#include "modules/ProtoCoreModule.h"
#include "modules/ProcessModule.h"
#include "quickjs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " [options] <filename.js> or " << programName << " -e \"code\"" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --cpu-threads N      Number of CPU threads (default: number of CPU cores)" << std::endl;
    std::cerr << "  --io-threads N       Number of I/O threads (default: 3-4x CPU cores)" << std::endl;
    std::cerr << "  --io-threads-factor F  Multiplier for I/O threads (default: 3.0)" << std::endl;
    std::cerr << "  -e \"code\"            Execute code directly" << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    size_t cpuThreads = 0;
    size_t ioThreads = 0;
    double ioFactor = 3.0;
    std::string code;
    std::string filename = "eval";
    bool executeCode = false;

    // Parse arguments
    int i = 1;
    while (i < argc) {
        std::string arg = argv[i];
        
        if (arg == "--cpu-threads" && i + 1 < argc) {
            cpuThreads = std::stoul(argv[++i]);
        } else if (arg == "--io-threads" && i + 1 < argc) {
            ioThreads = std::stoul(argv[++i]);
        } else if (arg == "--io-threads-factor" && i + 1 < argc) {
            ioFactor = std::stod(argv[++i]);
        } else if (arg == "-e" && i + 1 < argc) {
            executeCode = true;
            code = argv[++i];
        } else if (arg[0] != '-') {
            filename = arg;
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Could not open file: " << filename << std::endl;
                return 1;
            }
            std::stringstream ss;
            ss << file.rdbuf();
            code = ss.str();
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        i++;
    }

    if (code.empty() && !executeCode) {
        std::cerr << "No code to execute" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Create wrapper with thread pool configuration
    protojs::JSContextWrapper wrapper(cpuThreads, ioThreads, ioFactor);
    
    // Initialize modules
    protojs::Console::init(wrapper.getJSContext());
    protojs::Deferred::init(wrapper.getJSContext(), &wrapper);
    protojs::IOModule::init(wrapper.getJSContext());
    protojs::ProtoCoreModule::init(wrapper.getJSContext());
    protojs::ProcessModule::init(wrapper.getJSContext(), argc, argv);

    // Evaluate code
    JSValue result = wrapper.eval(code, filename);
    
    // Process event loop to handle any Deferred callbacks
    // In a full implementation, this would be a proper event loop
    // For now, we'll process a few times to handle immediate callbacks
    for (int j = 0; j < 10 && protojs::EventLoop::getInstance().hasPendingCallbacks(); ++j) {
        protojs::EventLoop::getInstance().processCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    JS_FreeValue(wrapper.getJSContext(), result);

    return 0;
}
