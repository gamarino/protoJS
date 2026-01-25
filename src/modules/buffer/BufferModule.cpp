#include "BufferModule.h"
#include "../../JSContext.h"
#include "../../TypeBridge.h"
#include "../../GCBridge.h"
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace protojs {

static JSClassID buffer_class_id;

struct BufferData {
    const proto::ProtoObject* bufferObj;  // Store ProtoObject instead
    proto::ProtoContext* pContext;
    JSRuntime* rt;
    
    BufferData(const proto::ProtoObject* buf, proto::ProtoContext* ctx, JSRuntime* r)
        : bufferObj(buf), pContext(ctx), rt(r) {}
    
    // Helper to get buffer pointer (workaround for asByteBuffer)
    const proto::ProtoByteBuffer* getByteBuffer() const {
        // Since ProtoByteBuffer::asObject returns ProtoObject*, we can reverse-cast
        // This is a workaround - in production, protoCore should expose asByteBuffer
        return reinterpret_cast<const proto::ProtoByteBuffer*>(bufferObj);
    }
    
    unsigned long getSize() const {
        const proto::ProtoByteBuffer* buf = getByteBuffer();
        return buf ? buf->getSize(pContext) : 0;
    }
    
    const char* getBufferPtr() const {
        const proto::ProtoByteBuffer* buf = getByteBuffer();
        return buf ? buf->getBuffer(pContext) : nullptr;
    }
    
    // Explicitly disable copy constructor to avoid ambiguity
    BufferData(const BufferData&) = delete;
    BufferData& operator=(const BufferData&) = delete;
};

void BufferModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register Buffer class
    JS_NewClassID(&buffer_class_id);
    JSClassDef bufferClassDef = {
        "Buffer",
        BufferFinalizer
    };
    JS_NewClass(rt, buffer_class_id, &bufferClassDef);
    
    JSValue bufferProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, bufferProto, "toString", JS_NewCFunction(ctx, bufferToString, "toString", 3));
    JS_SetPropertyStr(ctx, bufferProto, "slice", JS_NewCFunction(ctx, bufferSlice, "slice", 2));
    JS_SetPropertyStr(ctx, bufferProto, "copy", JS_NewCFunction(ctx, bufferCopy, "copy", 4));
    JS_SetPropertyStr(ctx, bufferProto, "fill", JS_NewCFunction(ctx, bufferFill, "fill", 4));
    JS_SetPropertyStr(ctx, bufferProto, "indexOf", JS_NewCFunction(ctx, bufferIndexOf, "indexOf", 3));
    JS_SetPropertyStr(ctx, bufferProto, "includes", JS_NewCFunction(ctx, bufferIncludes, "includes", 2));
    JS_SetClassProto(ctx, buffer_class_id, bufferProto);
    
    JSValue bufferCtor = JS_NewCFunction2(ctx, BufferConstructor, "Buffer", 2, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, bufferCtor, "from", JS_NewCFunction(ctx, bufferFrom, "from", 2));
    JS_SetPropertyStr(ctx, bufferCtor, "alloc", JS_NewCFunction(ctx, bufferAlloc, "alloc", 3));
    JS_SetPropertyStr(ctx, bufferCtor, "concat", JS_NewCFunction(ctx, bufferConcat, "concat", 2));
    JS_SetPropertyStr(ctx, bufferCtor, "isBuffer", JS_NewCFunction(ctx, bufferIsBuffer, "isBuffer", 1));
    JS_SetConstructor(ctx, bufferCtor, bufferProto);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "Buffer", bufferCtor);
    JS_FreeValue(ctx, global_obj);
}

JSValue BufferModule::BufferConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Buffer constructor requires at least one argument");
    }
    
    const proto::ProtoObject* bufferObj = nullptr;
    
    if (JS_IsNumber(argv[0])) {
        // Buffer(size)
        int32_t size;
        if (JS_ToInt32(ctx, &size, argv[0]) < 0) {
            return JS_EXCEPTION;
        }
        if (size < 0) {
            return JS_ThrowTypeError(ctx, "Buffer size must be non-negative");
        }
        bufferObj = pContext->newBuffer(size);
    } else if (JS_IsString(argv[0])) {
        // Buffer(string, encoding)
        const char* str = JS_ToCString(ctx, argv[0]);
        if (!str) return JS_EXCEPTION;
        
        const char* encoding = "utf8";
        if (argc > 1 && JS_IsString(argv[1])) {
            encoding = JS_ToCString(ctx, argv[1]);
        }
        
        // Use helper function from anonymous namespace
        auto decodeHelper = [](const char* s, size_t l, const char* enc) -> std::vector<uint8_t> {
            std::vector<uint8_t> result;
            if (strcmp(enc, "utf8") == 0 || strcmp(enc, "utf-8") == 0) {
                result.assign(reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + l);
            } else if (strcmp(enc, "hex") == 0) {
                for (size_t i = 0; i < l; i += 2) {
                    if (i + 1 < l) {
                        char hex[3] = {s[i], s[i + 1], '\0'};
                        result.push_back(static_cast<uint8_t>(strtoul(hex, nullptr, 16)));
                    }
                }
            } else {
                result.assign(reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + l);
            }
            return result;
        };
        std::vector<uint8_t> bytes;
        if (strcmp(encoding, "utf8") == 0 || strcmp(encoding, "utf-8") == 0) {
            bytes.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + strlen(str));
        } else if (strcmp(encoding, "hex") == 0) {
            for (size_t i = 0; i < strlen(str); i += 2) {
                if (i + 1 < strlen(str)) {
                    char hex[3] = {str[i], str[i + 1], '\0'};
                    bytes.push_back(static_cast<uint8_t>(strtoul(hex, nullptr, 16)));
                }
            }
        } else {
            bytes.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + strlen(str));
        }
        if (encoding != "utf8") {
            JS_FreeCString(ctx, encoding);
        }
        JS_FreeCString(ctx, str);
        
        bufferObj = pContext->newBuffer(bytes.size());
        if (bufferObj && !bytes.empty()) {
            const proto::ProtoByteBuffer* byteBuffer = reinterpret_cast<const proto::ProtoByteBuffer*>(bufferObj);
            char* buffer = const_cast<char*>(byteBuffer->getBuffer(pContext));
            memcpy(buffer, bytes.data(), bytes.size());
        }
    } else if (JS_IsArray(ctx, argv[0])) {
        // Buffer(array)
        JSValue lenVal = JS_GetPropertyStr(ctx, argv[0], "length");
        uint32_t len;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        
        bufferObj = pContext->newBuffer(len);
        if (bufferObj) {
            const proto::ProtoByteBuffer* byteBuffer = reinterpret_cast<const proto::ProtoByteBuffer*>(bufferObj);
            char* buffer = const_cast<char*>(byteBuffer->getBuffer(pContext));
            for (uint32_t i = 0; i < len; i++) {
                JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
                int32_t byte;
                if (JS_ToInt32(ctx, &byte, item) >= 0) {
                    buffer[i] = static_cast<char>(byte & 0xFF);
                }
                JS_FreeValue(ctx, item);
            }
        }
    } else {
        return JS_ThrowTypeError(ctx, "Buffer constructor: invalid argument type");
    }
    
    if (!bufferObj) {
        return JS_ThrowTypeError(ctx, "Failed to create Buffer");
    }
    
    JSValue obj = JS_NewObjectClass(ctx, buffer_class_id);
    if (JS_IsException(obj)) return obj;
    
    BufferData* data = new BufferData(bufferObj, pContext, JS_GetRuntime(ctx));
    JS_SetOpaque(obj, data);
    
    // Set length property
    unsigned long size = data->getSize();
    JS_SetPropertyStr(ctx, obj, "length", JS_NewInt64(ctx, size));
    JS_SetPropertyStr(ctx, obj, "byteLength", JS_NewInt64(ctx, size));
    
    return obj;
}

void BufferModule::BufferFinalizer(JSRuntime* rt, JSValue val) {
    BufferData* data = static_cast<BufferData*>(JS_GetOpaque(val, buffer_class_id));
    if (data) {
        delete data;
    }
}

const proto::ProtoByteBuffer* BufferModule::getBufferData(JSContext* ctx, JSValueConst val) {
    BufferData* data = static_cast<BufferData*>(JS_GetOpaque(val, buffer_class_id));
    if (data) {
        return data->getByteBuffer();
    }
    return nullptr;
}

JSValue BufferModule::bufferFrom(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return BufferConstructor(ctx, this_val, argc, argv);
}

JSValue BufferModule::bufferAlloc(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Buffer.alloc requires size");
    }
    
    int32_t size;
    if (JS_ToInt32(ctx, &size, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    if (size < 0) {
        return JS_ThrowTypeError(ctx, "Buffer size must be non-negative");
    }
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    const proto::ProtoObject* bufObj = pContext->newBuffer(size);
    const proto::ProtoByteBuffer* byteBuffer = reinterpret_cast<const proto::ProtoByteBuffer*>(bufObj);
    
    // Fill with value if provided
    if (argc > 1 && bufObj && byteBuffer) {
        char* buffer = const_cast<char*>(byteBuffer->getBuffer(pContext));
        char fill = 0;
        
        if (JS_IsNumber(argv[1])) {
            int32_t fillVal;
            JS_ToInt32(ctx, &fillVal, argv[1]);
            fill = static_cast<char>(fillVal & 0xFF);
        } else if (JS_IsString(argv[1])) {
            const char* fillStr = JS_ToCString(ctx, argv[1]);
            if (fillStr && strlen(fillStr) > 0) {
                fill = fillStr[0];
            }
            JS_FreeCString(ctx, fillStr);
        }
        
        memset(buffer, fill, size);
    } else if (bufObj && byteBuffer) {
        char* buffer = const_cast<char*>(byteBuffer->getBuffer(pContext));
        memset(buffer, 0, size);
    }
    
    JSValue obj = JS_NewObjectClass(ctx, buffer_class_id);
    if (JS_IsException(obj)) return obj;
    
    BufferData* data = new BufferData(bufObj, pContext, JS_GetRuntime(ctx));
    JS_SetOpaque(obj, data);
    
    unsigned long bufSize = data->getSize();
    JS_SetPropertyStr(ctx, obj, "length", JS_NewInt64(ctx, bufSize));
    JS_SetPropertyStr(ctx, obj, "byteLength", JS_NewInt64(ctx, bufSize));
    
    return obj;
}

JSValue BufferModule::bufferConcat(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsArray(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Buffer.concat requires array of buffers");
    }
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    JSValue lenVal = JS_GetPropertyStr(ctx, argv[0], "length");
    uint32_t len;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);
    
    // Calculate total length
    size_t totalLen = 0;
    std::vector<const proto::ProtoByteBuffer*> buffers;
    
    for (uint32_t i = 0; i < len; i++) {
        JSValue buf = JS_GetPropertyUint32(ctx, argv[0], i);
        const proto::ProtoByteBuffer* byteBuffer = getBufferData(ctx, buf);
        if (byteBuffer) {
            buffers.push_back(byteBuffer);
            totalLen += byteBuffer->getSize(pContext);
        }
        JS_FreeValue(ctx, buf);
    }
    
    // Create new buffer
    const proto::ProtoObject* bufObj = pContext->newBuffer(totalLen);
    const proto::ProtoByteBuffer* resultBuffer = reinterpret_cast<const proto::ProtoByteBuffer*>(bufObj);
    
    if (resultBuffer) {
        char* result = const_cast<char*>(resultBuffer->getBuffer(pContext));
        size_t offset = 0;
        
        for (const auto* buf : buffers) {
            size_t bufSize = buf->getSize(pContext);
            const char* src = buf->getBuffer(pContext);
            memcpy(result + offset, src, bufSize);
            offset += bufSize;
        }
    }
    
    JSValue obj = JS_NewObjectClass(ctx, buffer_class_id);
    if (JS_IsException(obj)) return obj;
    
    BufferData* data = new BufferData(bufObj, pContext, JS_GetRuntime(ctx));
    JS_SetOpaque(obj, data);
    
    JS_SetPropertyStr(ctx, obj, "length", JS_NewInt64(ctx, totalLen));
    JS_SetPropertyStr(ctx, obj, "byteLength", JS_NewInt64(ctx, totalLen));
    
    return obj;
}

JSValue BufferModule::bufferIsBuffer(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    
    const proto::ProtoByteBuffer* buf = getBufferData(ctx, argv[0]);
    return JS_NewBool(ctx, buf != nullptr);
}

JSValue BufferModule::bufferToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const proto::ProtoByteBuffer* byteBuffer = getBufferData(ctx, this_val);
    if (!byteBuffer) {
        return JS_ThrowTypeError(ctx, "Not a Buffer");
    }
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    const char* encoding = "utf8";
    
    if (argc > 0 && JS_IsString(argv[0])) {
        encoding = JS_ToCString(ctx, argv[0]);
    }
    
    unsigned long size = byteBuffer->getSize(pContext);
    const char* buffer = byteBuffer->getBuffer(pContext);
    
    int start = 0;
    int end = size;
    
    if (argc > 1 && JS_IsNumber(argv[1])) {
        JS_ToInt32(ctx, &start, argv[1]);
    }
    if (argc > 2 && JS_IsNumber(argv[2])) {
        JS_ToInt32(ctx, &end, argv[2]);
    }
    
    if (start < 0) start = 0;
    if (end > static_cast<int>(size)) end = size;
    if (start > end) start = end;
    
    // Encode string
    std::string result;
    if (strcmp(encoding, "utf8") == 0 || strcmp(encoding, "utf-8") == 0) {
        result = std::string(buffer + start, end - start);
    } else if (strcmp(encoding, "hex") == 0) {
        std::ostringstream oss;
        for (int i = start; i < end; i++) {
            oss << std::hex << std::setw(2) << std::setfill('0') << (static_cast<unsigned>(buffer[i]) & 0xFF);
        }
        result = oss.str();
    } else {
        result = std::string(buffer + start, end - start);
    }
    
    if (encoding != "utf8") {
        JS_FreeCString(ctx, encoding);
    }
    
    return JS_NewString(ctx, result.c_str());
}

JSValue BufferModule::bufferSlice(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const proto::ProtoByteBuffer* byteBuffer = getBufferData(ctx, this_val);
    if (!byteBuffer) {
        return JS_ThrowTypeError(ctx, "Not a Buffer");
    }
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    unsigned long size = byteBuffer->getSize(pContext);
    
    int start = 0;
    int end = size;
    
    if (argc > 0 && JS_IsNumber(argv[0])) {
        JS_ToInt32(ctx, &start, argv[0]);
    }
    if (argc > 1 && JS_IsNumber(argv[1])) {
        JS_ToInt32(ctx, &end, argv[1]);
    }
    
    if (start < 0) start += size;
    if (end < 0) end += size;
    if (start < 0) start = 0;
    if (end > static_cast<int>(size)) end = size;
    if (start > end) start = end;
    
    // Create new buffer with slice
    size_t sliceLen = end - start;
    const proto::ProtoObject* bufObj = pContext->newBuffer(sliceLen);
    const proto::ProtoByteBuffer* sliceBuffer = reinterpret_cast<const proto::ProtoByteBuffer*>(bufObj);
    
    if (bufObj && sliceBuffer) {
        char* slice = const_cast<char*>(sliceBuffer->getBuffer(pContext));
        const char* src = byteBuffer->getBuffer(pContext);
        memcpy(slice, src + start, sliceLen);
    }
    
    JSValue obj = JS_NewObjectClass(ctx, buffer_class_id);
    if (JS_IsException(obj)) return obj;
    
    BufferData* data = new BufferData(bufObj, pContext, JS_GetRuntime(ctx));
    JS_SetOpaque(obj, data);
    
    JS_SetPropertyStr(ctx, obj, "length", JS_NewInt64(ctx, sliceLen));
    JS_SetPropertyStr(ctx, obj, "byteLength", JS_NewInt64(ctx, sliceLen));
    
    return obj;
}

JSValue BufferModule::bufferCopy(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const proto::ProtoByteBuffer* srcBuffer = getBufferData(ctx, this_val);
    if (!srcBuffer) {
        return JS_ThrowTypeError(ctx, "Not a Buffer");
    }
    
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "copy requires target buffer");
    }
    
    const proto::ProtoByteBuffer* targetBuffer = getBufferData(ctx, argv[0]);
    if (!targetBuffer) {
        return JS_ThrowTypeError(ctx, "target must be a Buffer");
    }
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    unsigned long srcSize = srcBuffer->getSize(pContext);
    unsigned long targetSize = targetBuffer->getSize(pContext);
    
    int targetStart = 0;
    int sourceStart = 0;
    int sourceEnd = srcSize;
    
    if (argc > 1 && JS_IsNumber(argv[1])) {
        JS_ToInt32(ctx, &targetStart, argv[1]);
    }
    if (argc > 2 && JS_IsNumber(argv[2])) {
        JS_ToInt32(ctx, &sourceStart, argv[2]);
    }
    if (argc > 3 && JS_IsNumber(argv[3])) {
        JS_ToInt32(ctx, &sourceEnd, argv[3]);
    }
    
    if (targetStart < 0 || sourceStart < 0 || sourceEnd < 0) {
        return JS_NewInt32(ctx, 0);
    }
    
    if (sourceStart > static_cast<int>(srcSize)) sourceStart = srcSize;
    if (sourceEnd > static_cast<int>(srcSize)) sourceEnd = srcSize;
    if (sourceStart > sourceEnd) sourceStart = sourceEnd;
    
    size_t copyLen = std::min(static_cast<size_t>(sourceEnd - sourceStart),
                              static_cast<size_t>(targetSize - targetStart));
    
    if (copyLen > 0) {
        char* target = const_cast<char*>(targetBuffer->getBuffer(pContext));
        const char* src = srcBuffer->getBuffer(pContext);
        memcpy(target + targetStart, src + sourceStart, copyLen);
    }
    
    return JS_NewInt32(ctx, copyLen);
}

JSValue BufferModule::bufferFill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const proto::ProtoByteBuffer* byteBuffer = getBufferData(ctx, this_val);
    if (!byteBuffer) {
        return JS_ThrowTypeError(ctx, "Not a Buffer");
    }
    
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fill requires a value");
    }
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    unsigned long size = byteBuffer->getSize(pContext);
    
    char fill = 0;
    if (JS_IsNumber(argv[0])) {
        int32_t fillVal;
        JS_ToInt32(ctx, &fillVal, argv[0]);
        fill = static_cast<char>(fillVal & 0xFF);
    } else if (JS_IsString(argv[0])) {
        const char* fillStr = JS_ToCString(ctx, argv[0]);
        if (fillStr && strlen(fillStr) > 0) {
            fill = fillStr[0];
        }
        JS_FreeCString(ctx, fillStr);
    }
    
    int offset = 0;
    int end = size;
    
    if (argc > 1 && JS_IsNumber(argv[1])) {
        JS_ToInt32(ctx, &offset, argv[1]);
    }
    if (argc > 2 && JS_IsNumber(argv[2])) {
        JS_ToInt32(ctx, &end, argv[2]);
    }
    
    if (offset < 0) offset = 0;
    if (end > static_cast<int>(size)) end = size;
    if (offset > end) offset = end;
    
    char* buffer = const_cast<char*>(byteBuffer->getBuffer(pContext));
    memset(buffer + offset, fill, end - offset);
    
    return JS_DupValue(ctx, this_val);
}

JSValue BufferModule::bufferIndexOf(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const proto::ProtoByteBuffer* byteBuffer = getBufferData(ctx, this_val);
    if (!byteBuffer) {
        return JS_ThrowTypeError(ctx, "Not a Buffer");
    }
    
    if (argc < 1) {
        return JS_NewInt32(ctx, -1);
    }
    
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    unsigned long size = byteBuffer->getSize(pContext);
    const char* buffer = byteBuffer->getBuffer(pContext);
    
    // Get search value
    std::vector<uint8_t> searchBytes;
    if (JS_IsNumber(argv[0])) {
        int32_t val;
        JS_ToInt32(ctx, &val, argv[0]);
        searchBytes.push_back(static_cast<uint8_t>(val & 0xFF));
    } else if (JS_IsString(argv[0])) {
        const char* str = JS_ToCString(ctx, argv[0]);
        const char* encoding = "utf8";
        if (argc > 2 && JS_IsString(argv[2])) {
            encoding = JS_ToCString(ctx, argv[2]);
        }
        // Decode search string
        if (strcmp(encoding, "utf8") == 0 || strcmp(encoding, "utf-8") == 0) {
            searchBytes.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + strlen(str));
        } else if (strcmp(encoding, "hex") == 0) {
            for (size_t i = 0; i < strlen(str); i += 2) {
                if (i + 1 < strlen(str)) {
                    char hex[3] = {str[i], str[i + 1], '\0'};
                    searchBytes.push_back(static_cast<uint8_t>(strtoul(hex, nullptr, 16)));
                }
            }
        } else {
            searchBytes.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + strlen(str));
        }
        JS_FreeCString(ctx, str);
        if (encoding != "utf8") {
            JS_FreeCString(ctx, encoding);
        }
    } else if (getBufferData(ctx, argv[0])) {
        const proto::ProtoByteBuffer* searchBuf = getBufferData(ctx, argv[0]);
        unsigned long searchSize = searchBuf->getSize(pContext);
        const char* searchData = searchBuf->getBuffer(pContext);
        searchBytes.assign(searchData, searchData + searchSize);
    }
    
    if (searchBytes.empty()) {
        return JS_NewInt32(ctx, -1);
    }
    
    int byteOffset = 0;
    if (argc > 1 && JS_IsNumber(argv[1])) {
        JS_ToInt32(ctx, &byteOffset, argv[1]);
    }
    
    if (byteOffset < 0) byteOffset += size;
    if (byteOffset < 0) byteOffset = 0;
    if (byteOffset >= static_cast<int>(size)) {
        return JS_NewInt32(ctx, -1);
    }
    
    // Search for pattern
    for (int i = byteOffset; i <= static_cast<int>(size) - static_cast<int>(searchBytes.size()); i++) {
        bool match = true;
        for (size_t j = 0; j < searchBytes.size(); j++) {
            if (buffer[i + j] != static_cast<char>(searchBytes[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return JS_NewInt32(ctx, i);
        }
    }
    
    return JS_NewInt32(ctx, -1);
}

JSValue BufferModule::bufferIncludes(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue indexOfResult = bufferIndexOf(ctx, this_val, argc, argv);
    if (JS_IsException(indexOfResult)) {
        return indexOfResult;
    }
    
    int32_t index;
    if (JS_ToInt32(ctx, &index, indexOfResult) >= 0) {
        JS_FreeValue(ctx, indexOfResult);
        return JS_NewBool(ctx, index >= 0);
    }
    
    JS_FreeValue(ctx, indexOfResult);
    return JS_NewBool(ctx, false);
}

// Helper function implementations (in anonymous namespace for internal use)
namespace {
std::string encodeStringHelper(const char* data, size_t len, const char* encoding) {
    if (strcmp(encoding, "utf8") == 0 || strcmp(encoding, "utf-8") == 0) {
        return std::string(data, len);
    } else if (strcmp(encoding, "hex") == 0) {
        std::ostringstream oss;
        for (size_t i = 0; i < len; i++) {
            oss << std::hex << std::setw(2) << std::setfill('0') << (static_cast<unsigned>(data[i]) & 0xFF);
        }
        return oss.str();
    } else if (strcmp(encoding, "base64") == 0) {
        // Basic base64 encoding (simplified)
        const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        for (size_t i = 0; i < len; i += 3) {
            uint32_t val = (static_cast<unsigned char>(data[i]) << 16);
            if (i + 1 < len) val |= (static_cast<unsigned char>(data[i + 1]) << 8);
            if (i + 2 < len) val |= static_cast<unsigned char>(data[i + 2]);
            
            result += base64_chars[(val >> 18) & 63];
            result += base64_chars[(val >> 12) & 63];
            result += (i + 1 < len) ? base64_chars[(val >> 6) & 63] : '=';
            result += (i + 2 < len) ? base64_chars[val & 63] : '=';
        }
        return result;
    } else if (strcmp(encoding, "ascii") == 0) {
        std::string result;
        for (size_t i = 0; i < len; i++) {
            result += static_cast<char>(static_cast<unsigned char>(data[i]) & 0x7F);
        }
        return result;
    } else if (strcmp(encoding, "latin1") == 0) {
        return std::string(data, len);
    }
    
    // Default to utf8
    return std::string(data, len);
}

std::vector<uint8_t> decodeStringHelper(const char* str, size_t len, const char* encoding) {
    std::vector<uint8_t> result;
    
    if (strcmp(encoding, "utf8") == 0 || strcmp(encoding, "utf-8") == 0) {
        result.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + len);
    } else if (strcmp(encoding, "hex") == 0) {
        for (size_t i = 0; i < len; i += 2) {
            if (i + 1 < len) {
                char hex[3] = {str[i], str[i + 1], '\0'};
                result.push_back(static_cast<uint8_t>(strtoul(hex, nullptr, 16)));
            }
        }
    } else if (strcmp(encoding, "base64") == 0) {
        // Basic base64 decoding (simplified)
        const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < len; i += 4) {
            uint32_t val = 0;
            for (int j = 0; j < 4 && i + j < len; j++) {
                const char* pos = strchr(base64_chars, str[i + j]);
                if (pos) {
                    val = (val << 6) | (pos - base64_chars);
                }
            }
            result.push_back((val >> 16) & 0xFF);
            if (i + 2 < len && str[i + 2] != '=') {
                result.push_back((val >> 8) & 0xFF);
            }
            if (i + 3 < len && str[i + 3] != '=') {
                result.push_back(val & 0xFF);
            }
        }
    } else if (strcmp(encoding, "ascii") == 0) {
        result.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + len);
    } else if (strcmp(encoding, "latin1") == 0) {
        result.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + len);
    } else {
        // Default to utf8
        result.assign(reinterpret_cast<const uint8_t*>(str), reinterpret_cast<const uint8_t*>(str) + len);
    }
    
    return result;
}
} // anonymous namespace

} // namespace protojs
