#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../deps/quickjs/quickjs.h"

static JSValue js_print(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    int i;
    const char *str;
    for (i = 0; i < argc; i++) {
        if (i != 0) printf(" ");
        str = JS_ToCString(ctx, argv[i]);
        if (!str) return JS_EXCEPTION;
        printf("%s", str);
        JS_FreeCString(ctx, str);
    }
    printf("\n");
    return JS_UNDEFINED;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: qjs_minimal <file.js>\n");
        return 1;
    }
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    
    // Basic console.log and print
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "print", JS_NewCFunction(ctx, js_print, "print", 1));
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_print, "log", 1));
    JS_SetPropertyStr(ctx, global_obj, "console", console);
    JS_FreeValue(ctx, global_obj);

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(size + 1);
    if (!buf) return 1;
    if (fread(buf, 1, size, f) != (size_t)size) return 1;
    buf[size] = 0;
    fclose(f);

    JSValue val = JS_Eval(ctx, buf, size, argv[1], JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        const char *str = JS_ToCString(ctx, exc);
        fprintf(stderr, "Exception in %s: %s\n", argv[1], str);
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, exc);
    }
    JS_FreeValue(ctx, val);
    free(buf);
    
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}
