import sys

def modify_file(filepath):
    with open(filepath, 'r') as f:
        lines = f.readlines()

    # Find the include block and add forward declarations
    for i, line in enumerate(lines):
        if line.startswith('namespace protojs {'):
            insert_idx = i + 1
            lines.insert(insert_idx, "static bool arrHas(proto::ProtoContext* ctx, const proto::ProtoObject* arr, unsigned long idx);\n")
            lines.insert(insert_idx+1, "static bool arrHasProperty(proto::ProtoContext* ctx, const proto::ProtoObject* arr, unsigned long idx);\n")
            break

    # Fix syntax error around line 2138 (the -- token error)
    # The error "expected unqualified-id before '--' token" means there's a comment `--------------------` that is missing `//`.
    for i, line in enumerate(lines):
        if line.startswith('---'):
            lines[i] = '// ' + line

    with open(filepath, 'w') as f:
        f.writelines(lines)

modify_file('src/ArrayPrototype.cpp')
