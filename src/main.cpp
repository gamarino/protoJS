#include "JSContext.h"
#include "Deferred.h"
#include "quickjs.h"
#include <iostream>
#include <fstream>
#include <sstream>

namespace protojs {
    void init_console(JSContext* ctx);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: protojs <filename.js> or protojs -e \"code\"" << std::endl;
        return 1;
    }

    protojs::JSContextWrapper wrapper;
    protojs::init_console(wrapper.getJSContext());
    protojs::Deferred::init(wrapper.getJSContext());

    std::string code;
    std::string filename = "eval";

    if (std::string(argv[1]) == "-e" && argc > 2) {
        code = argv[2];
    } else {
        filename = argv[1];
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Could not open file: " << filename << std::endl;
            return 1;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        code = ss.str();
    }

    JSValue result = wrapper.eval(code, filename);
    JS_FreeValue(wrapper.getJSContext(), result);

    return 0;
}
