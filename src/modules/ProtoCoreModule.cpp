#include "ProtoCoreModule.h"
#include "../TypeBridge.h"
#include "../JSContext.h"
#include "quickjs.h"
#include <iostream>

namespace protojs {

static JSClassID protojs_set_class_id;
static JSClassID protojs_multiset_class_id;
static JSClassID protojs_sparselist_class_id;

// Helper to get JSContextWrapper
static JSContextWrapper* getWrapper(JSContext* ctx) {
    return static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
}

void ProtoCoreModule::init(JSContext* ctx) {
    JSContextWrapper* wrapper = getWrapper(ctx);
    if (!wrapper) {
        std::cerr << "ProtoCoreModule: JSContextWrapper not found" << std::endl;
        return;
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    // Register Set class
    JS_NewClassID(&protojs_set_class_id);
    JSClassDef setClassDef = {
        "ProtoSet",
        SetFinalizer
    };
    JS_NewClass(JS_GetRuntime(ctx), protojs_set_class_id, &setClassDef);
    
    JSValue setProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, setProto, "add", JS_NewCFunction(ctx, SetAdd, "add", 1));
    JS_SetPropertyStr(ctx, setProto, "has", JS_NewCFunction(ctx, SetHas, "has", 1));
    JS_SetPropertyStr(ctx, setProto, "remove", JS_NewCFunction(ctx, SetRemove, "remove", 1));
    JS_SetPropertyStr(ctx, setProto, "size", JS_NewCFunction(ctx, SetSize, "size", 0));
    JS_SetClassProto(ctx, protojs_set_class_id, setProto);
    
    JSValue setCtor = JS_NewCFunction2(ctx, SetConstructor, "Set", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, setCtor, setProto);
    
    // Register Multiset class
    JS_NewClassID(&protojs_multiset_class_id);
    JSClassDef multisetClassDef = {
        "ProtoMultiset",
        MultisetFinalizer
    };
    JS_NewClass(JS_GetRuntime(ctx), protojs_multiset_class_id, &multisetClassDef);
    
    JSValue multisetProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, multisetProto, "add", JS_NewCFunction(ctx, MultisetAdd, "add", 1));
    JS_SetPropertyStr(ctx, multisetProto, "count", JS_NewCFunction(ctx, MultisetCount, "count", 1));
    JS_SetPropertyStr(ctx, multisetProto, "remove", JS_NewCFunction(ctx, MultisetRemove, "remove", 1));
    JS_SetPropertyStr(ctx, multisetProto, "size", JS_NewCFunction(ctx, MultisetSize, "size", 0));
    JS_SetClassProto(ctx, protojs_multiset_class_id, multisetProto);
    
    JSValue multisetCtor = JS_NewCFunction2(ctx, MultisetConstructor, "Multiset", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, multisetCtor, multisetProto);
    
    // Register SparseList class
    JS_NewClassID(&protojs_sparselist_class_id);
    JSClassDef sparseListClassDef = {
        "ProtoSparseList",
        SparseListFinalizer
    };
    JS_NewClass(JS_GetRuntime(ctx), protojs_sparselist_class_id, &sparseListClassDef);
    
    JSValue sparseListProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, sparseListProto, "set", JS_NewCFunction(ctx, SparseListSet, "set", 2));
    JS_SetPropertyStr(ctx, sparseListProto, "get", JS_NewCFunction(ctx, SparseListGet, "get", 1));
    JS_SetPropertyStr(ctx, sparseListProto, "has", JS_NewCFunction(ctx, SparseListHas, "has", 1));
    JS_SetPropertyStr(ctx, sparseListProto, "size", JS_NewCFunction(ctx, SparseListSize, "size", 0));
    JS_SetClassProto(ctx, protojs_sparselist_class_id, sparseListProto);
    
    JSValue sparseListCtor = JS_NewCFunction2(ctx, SparseListConstructor, "SparseList", 0, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, sparseListCtor, sparseListProto);
    
    // Create protoCore module object
    JSValue protoCoreModule = JS_NewObject(ctx);
    
    // Add constructors
    JS_SetPropertyStr(ctx, protoCoreModule, "Set", setCtor);
    JS_SetPropertyStr(ctx, protoCoreModule, "Multiset", multisetCtor);
    JS_SetPropertyStr(ctx, protoCoreModule, "SparseList", sparseListCtor);
    
    // Add utility functions
    JS_SetPropertyStr(ctx, protoCoreModule, "Tuple", JS_NewCFunction(ctx, Tuple, "Tuple", 1));
    JS_SetPropertyStr(ctx, protoCoreModule, "ImmutableObject", JS_NewCFunction(ctx, ImmutableObject, "ImmutableObject", 1));
    JS_SetPropertyStr(ctx, protoCoreModule, "MutableObject", JS_NewCFunction(ctx, MutableObject, "MutableObject", 1));
    JS_SetPropertyStr(ctx, protoCoreModule, "isImmutable", JS_NewCFunction(ctx, IsImmutable, "isImmutable", 1));
    JS_SetPropertyStr(ctx, protoCoreModule, "makeImmutable", JS_NewCFunction(ctx, MakeImmutable, "makeImmutable", 1));
    JS_SetPropertyStr(ctx, protoCoreModule, "makeMutable", JS_NewCFunction(ctx, MakeMutable, "makeMutable", 1));
    
    // Add to global scope
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "protoCore", protoCoreModule);
    JS_FreeValue(ctx, global_obj);
}

// Set implementation
JSValue ProtoCoreModule::SetConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSContextWrapper* wrapper = getWrapper(ctx);
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "ProtoCoreModule: JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    const proto::ProtoSet* protoSet = pContext->newSet();
    
    // If initial values provided, add them
    if (argc > 0 && JS_IsArray(ctx, argv[0])) {
        JSValue lenVal = JS_GetPropertyStr(ctx, argv[0], "length");
        uint32_t len;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        
        for (uint32_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
            const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, item, pContext);
            protoSet = protoSet->add(pContext, pItem);
            JS_FreeValue(ctx, item);
        }
    }
    
    JSValue obj = JS_NewObjectClass(ctx, protojs_set_class_id);
    if (JS_IsException(obj)) return obj;
    
    // Store ProtoSet pointer
    const proto::ProtoSet** setPtr = (const proto::ProtoSet**)js_mallocz(ctx, sizeof(const proto::ProtoSet*));
    *setPtr = protoSet;
    JS_SetOpaque(obj, setPtr);
    
    return obj;
}

JSValue ProtoCoreModule::SetAdd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Set.add expects a value");
    }
    
    const proto::ProtoSet** setPtr = (const proto::ProtoSet**)JS_GetOpaque(this_val, protojs_set_class_id);
    if (!setPtr) {
        return JS_ThrowTypeError(ctx, "Invalid Set object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, argv[0], pContext);
    const proto::ProtoSet* newSet = (*setPtr)->add(pContext, pItem);
    
    // Update stored pointer
    *setPtr = newSet;
    
    return JS_DupValue(ctx, this_val);
}

JSValue ProtoCoreModule::SetHas(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Set.has expects a value");
    }
    
    const proto::ProtoSet** setPtr = (const proto::ProtoSet**)JS_GetOpaque(this_val, protojs_set_class_id);
    if (!setPtr) {
        return JS_ThrowTypeError(ctx, "Invalid Set object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, argv[0], pContext);
    const proto::ProtoObject* result = (*setPtr)->has(pContext, pItem);
    
    return JS_NewBool(ctx, result == PROTO_TRUE);
}

JSValue ProtoCoreModule::SetRemove(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Set.remove expects a value");
    }
    
    const proto::ProtoSet** setPtr = (const proto::ProtoSet**)JS_GetOpaque(this_val, protojs_set_class_id);
    if (!setPtr) {
        return JS_ThrowTypeError(ctx, "Invalid Set object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, argv[0], pContext);
    const proto::ProtoSet* newSet = (*setPtr)->remove(pContext, pItem);
    
    // Update stored pointer
    *setPtr = newSet;
    
    return JS_DupValue(ctx, this_val);
}

JSValue ProtoCoreModule::SetSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const proto::ProtoSet** setPtr = (const proto::ProtoSet**)JS_GetOpaque(this_val, protojs_set_class_id);
    if (!setPtr) {
        return JS_ThrowTypeError(ctx, "Invalid Set object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    unsigned long size = (*setPtr)->getSize(pContext);
    return JS_NewInt32(ctx, size);
}

void ProtoCoreModule::SetFinalizer(JSRuntime* rt, JSValue val) {
    const proto::ProtoSet** setPtr = (const proto::ProtoSet**)JS_GetOpaque(val, protojs_set_class_id);
    if (setPtr) {
        js_free_rt(rt, setPtr);
    }
}

// Multiset implementation (similar pattern)
JSValue ProtoCoreModule::MultisetConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSContextWrapper* wrapper = getWrapper(ctx);
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "ProtoCoreModule: JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    const proto::ProtoMultiset* protoMultiset = pContext->newMultiset();
    
    // If initial values provided, add them
    if (argc > 0 && JS_IsArray(ctx, argv[0])) {
        JSValue lenVal = JS_GetPropertyStr(ctx, argv[0], "length");
        uint32_t len;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        
        for (uint32_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
            const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, item, pContext);
            protoMultiset = protoMultiset->add(pContext, pItem);
            JS_FreeValue(ctx, item);
        }
    }
    
    JSValue obj = JS_NewObjectClass(ctx, protojs_multiset_class_id);
    if (JS_IsException(obj)) return obj;
    
    const proto::ProtoMultiset** multisetPtr = (const proto::ProtoMultiset**)js_mallocz(ctx, sizeof(const proto::ProtoMultiset*));
    *multisetPtr = protoMultiset;
    JS_SetOpaque(obj, multisetPtr);
    
    return obj;
}

JSValue ProtoCoreModule::MultisetAdd(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Multiset.add expects a value");
    }
    
    const proto::ProtoMultiset** multisetPtr = (const proto::ProtoMultiset**)JS_GetOpaque(this_val, protojs_multiset_class_id);
    if (!multisetPtr) {
        return JS_ThrowTypeError(ctx, "Invalid Multiset object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, argv[0], pContext);
    const proto::ProtoMultiset* newMultiset = (*multisetPtr)->add(pContext, pItem);
    
    *multisetPtr = newMultiset;
    
    return JS_DupValue(ctx, this_val);
}

JSValue ProtoCoreModule::MultisetCount(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Multiset.count expects a value");
    }
    
    const proto::ProtoMultiset** multisetPtr = (const proto::ProtoMultiset**)JS_GetOpaque(this_val, protojs_multiset_class_id);
    if (!multisetPtr) {
        return JS_ThrowTypeError(ctx, "Invalid Multiset object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, argv[0], pContext);
    const proto::ProtoObject* countObj = (*multisetPtr)->count(pContext, pItem);
    
    return TypeBridge::toJS(ctx, countObj, pContext);
}

JSValue ProtoCoreModule::MultisetRemove(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Multiset.remove expects a value");
    }
    
    const proto::ProtoMultiset** multisetPtr = (const proto::ProtoMultiset**)JS_GetOpaque(this_val, protojs_multiset_class_id);
    if (!multisetPtr) {
        return JS_ThrowTypeError(ctx, "Invalid Multiset object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, argv[0], pContext);
    const proto::ProtoMultiset* newMultiset = (*multisetPtr)->remove(pContext, pItem);
    
    *multisetPtr = newMultiset;
    
    return JS_DupValue(ctx, this_val);
}

JSValue ProtoCoreModule::MultisetSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const proto::ProtoMultiset** multisetPtr = (const proto::ProtoMultiset**)JS_GetOpaque(this_val, protojs_multiset_class_id);
    if (!multisetPtr) {
        return JS_ThrowTypeError(ctx, "Invalid Multiset object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    unsigned long size = (*multisetPtr)->getSize(pContext);
    return JS_NewInt32(ctx, size);
}

void ProtoCoreModule::MultisetFinalizer(JSRuntime* rt, JSValue val) {
    const proto::ProtoMultiset** multisetPtr = (const proto::ProtoMultiset**)JS_GetOpaque(val, protojs_multiset_class_id);
    if (multisetPtr) {
        js_free_rt(rt, multisetPtr);
    }
}

// SparseList implementation
JSValue ProtoCoreModule::SparseListConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    JSContextWrapper* wrapper = getWrapper(ctx);
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "ProtoCoreModule: JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    const proto::ProtoSparseList* protoSparseList = pContext->newSparseList();
    
    JSValue obj = JS_NewObjectClass(ctx, protojs_sparselist_class_id);
    if (JS_IsException(obj)) return obj;
    
    const proto::ProtoSparseList** sparseListPtr = (const proto::ProtoSparseList**)js_mallocz(ctx, sizeof(const proto::ProtoSparseList*));
    *sparseListPtr = protoSparseList;
    JS_SetOpaque(obj, sparseListPtr);
    
    return obj;
}

JSValue ProtoCoreModule::SparseListSet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "SparseList.set expects index and value");
    }
    
    const proto::ProtoSparseList** sparseListPtr = (const proto::ProtoSparseList**)JS_GetOpaque(this_val, protojs_sparselist_class_id);
    if (!sparseListPtr) {
        return JS_ThrowTypeError(ctx, "Invalid SparseList object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    uint32_t index;
    JS_ToUint32(ctx, &index, argv[0]);
    const proto::ProtoObject* pValue = TypeBridge::fromJS(ctx, argv[1], pContext);
    
    const proto::ProtoSparseList* newSparseList = (*sparseListPtr)->setAt(pContext, index, pValue);
    *sparseListPtr = newSparseList;
    
    return JS_DupValue(ctx, this_val);
}

JSValue ProtoCoreModule::SparseListGet(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "SparseList.get expects an index");
    }
    
    const proto::ProtoSparseList** sparseListPtr = (const proto::ProtoSparseList**)JS_GetOpaque(this_val, protojs_sparselist_class_id);
    if (!sparseListPtr) {
        return JS_ThrowTypeError(ctx, "Invalid SparseList object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    uint32_t index;
    JS_ToUint32(ctx, &index, argv[0]);
    
    if ((*sparseListPtr)->has(pContext, index)) {
        const proto::ProtoObject* pValue = (*sparseListPtr)->getAt(pContext, index);
        return TypeBridge::toJS(ctx, pValue, pContext);
    }
    
    return JS_UNDEFINED;
}

JSValue ProtoCoreModule::SparseListHas(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "SparseList.has expects an index");
    }
    
    const proto::ProtoSparseList** sparseListPtr = (const proto::ProtoSparseList**)JS_GetOpaque(this_val, protojs_sparselist_class_id);
    if (!sparseListPtr) {
        return JS_ThrowTypeError(ctx, "Invalid SparseList object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    uint32_t index;
    JS_ToUint32(ctx, &index, argv[0]);
    
    return JS_NewBool(ctx, (*sparseListPtr)->has(pContext, index));
}

JSValue ProtoCoreModule::SparseListSize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    const proto::ProtoSparseList** sparseListPtr = (const proto::ProtoSparseList**)JS_GetOpaque(this_val, protojs_sparselist_class_id);
    if (!sparseListPtr) {
        return JS_ThrowTypeError(ctx, "Invalid SparseList object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    unsigned long size = (*sparseListPtr)->getSize(pContext);
    return JS_NewInt32(ctx, size);
}

void ProtoCoreModule::SparseListFinalizer(JSRuntime* rt, JSValue val) {
    const proto::ProtoSparseList** sparseListPtr = (const proto::ProtoSparseList**)JS_GetOpaque(val, protojs_sparselist_class_id);
    if (sparseListPtr) {
        js_free_rt(rt, sparseListPtr);
    }
}

// Tuple - factory function (not constructor)
JSValue ProtoCoreModule::Tuple(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsArray(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Tuple expects an array");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "ProtoCoreModule: JSContextWrapper not found");
    }
    
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    // Convert JS array to ProtoList, then to ProtoTuple
    const proto::ProtoList* protoList = pContext->newList();
    JSValue lenVal = JS_GetPropertyStr(ctx, argv[0], "length");
    uint32_t len;
    JS_ToUint32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);
    
    for (uint32_t i = 0; i < len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        const proto::ProtoObject* pItem = TypeBridge::fromJS(ctx, item, pContext);
        protoList = protoList->appendLast(pContext, pItem);
        JS_FreeValue(ctx, item);
    }
    
    const proto::ProtoTuple* protoTuple = pContext->newTupleFromList(protoList);
    if (protoTuple) {
        return TypeBridge::toJS(ctx, protoTuple->asObject(pContext), pContext);
    }
    return JS_UNDEFINED;
}

// Mutability utilities
JSValue ProtoCoreModule::ImmutableObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "ImmutableObject expects an object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    // Convert JS object to ProtoObject with mutable_ref = 0 (immutable)
    const proto::ProtoObject* pObj = TypeBridge::fromJS(ctx, argv[0], pContext);
    // Clone as immutable (mutable_ref = 0)
    const proto::ProtoObject* immutableObj = pObj->clone(pContext, false);
    
    return TypeBridge::toJS(ctx, immutableObj, pContext);
}

JSValue ProtoCoreModule::MutableObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "MutableObject expects an object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    // Convert JS object to ProtoObject with mutable_ref > 0 (mutable)
    const proto::ProtoObject* pObj = TypeBridge::fromJS(ctx, argv[0], pContext);
    // Clone as mutable (mutable_ref > 0)
    const proto::ProtoObject* mutableObj = pObj->clone(pContext, true);
    
    return TypeBridge::toJS(ctx, mutableObj, pContext);
}

JSValue ProtoCoreModule::IsImmutable(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "isImmutable expects an object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pObj = TypeBridge::fromJS(ctx, argv[0], pContext);
    if (!pObj->isCell(pContext)) {
        return JS_NewBool(ctx, true); // Primitives are immutable
    }
    
    // Check if object has mutable_ref > 0
    // For Fase 1, simplified check
    // TODO: Implement proper mutable_ref check
    return JS_NewBool(ctx, false); // Placeholder
}

JSValue ProtoCoreModule::MakeImmutable(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "makeImmutable expects an object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pObj = TypeBridge::fromJS(ctx, argv[0], pContext);
    const proto::ProtoObject* immutableObj = pObj->clone(pContext, false);
    
    return TypeBridge::toJS(ctx, immutableObj, pContext);
}

JSValue ProtoCoreModule::MakeMutable(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "makeMutable expects an object");
    }
    
    JSContextWrapper* wrapper = getWrapper(ctx);
    proto::ProtoContext* pContext = wrapper->getProtoContext();
    
    const proto::ProtoObject* pObj = TypeBridge::fromJS(ctx, argv[0], pContext);
    const proto::ProtoObject* mutableObj = pObj->clone(pContext, true);
    
    return TypeBridge::toJS(ctx, mutableObj, pContext);
}

} // namespace protojs
