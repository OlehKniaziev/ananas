#include "son.h"
#include "print.h"

ERMIS_IMPL_ARRAY(AnanasSON_Node *, AnanasSON_NodeArray)

static AnanasSON_Node *NewNode(AnanasSON_CompilerState *cstate,
                               AnanasSON_NodeKind kind,
                               AnanasSON_NodeType type) {
    HeliosAllocator allocator = AnanasArenaToHeliosAllocator(cstate->arena);
    AnanasSON_Node *node = ANANAS_ARENA_STRUCT_ZERO(cstate->arena, AnanasSON_Node);
    node->kind = kind;
    node->type = type;
    AnanasSON_NodeArrayInit(&node->inputs, allocator, 5);
    AnanasSON_NodeArrayInit(&node->outputs, allocator, 5);
    return node;
}

static void AddInput(AnanasSON_Node *node, AnanasSON_Node *input) {
    AnanasSON_NodeArrayPush(&node->inputs, input);
    AnanasSON_NodeArrayPush(&input->outputs, node);
}

static AnanasSON_Node *NewConstantNode(AnanasSON_CompilerState *cstate,
                                       S64 value) {
    AnanasSON_NodeType node_type = {0};
    node_type.kind = AnanasSON_NodeType_Integer;
    node_type.is_constant = 1;
    node_type.u.const_integer = value;
    AnanasSON_Node *node = NewNode(cstate, AnanasSON_NodeKind_Const, node_type);

    AddInput(node, cstate->start_node);
    return node;
}

#define TYPE_BOTTOM ((AnanasSON_NodeType) {0})

void AnanasSON_CompilerStateInit(AnanasSON_CompilerState *cstate, AnanasArena *arena) {
    cstate->arena = arena;
    cstate->start_node = NewNode(cstate, AnanasSON_NodeKind_Start, TYPE_BOTTOM);
    cstate->cur_control_node = cstate->start_node;
}

static AnanasSON_NodeType ComputeType(AnanasSON_Node *node) {
#define IBINOP(op) do { \
    AnanasSON_Node *lhs = AnanasSON_NodeArrayAt(&node->inputs, 0); \
    AnanasSON_Node *rhs = AnanasSON_NodeArrayAt(&node->inputs, 1); \
    \
    if (lhs->type.kind == AnanasSON_NodeType_Integer && \
        rhs->type.kind == AnanasSON_NodeType_Integer && \
        lhs->type.is_constant && \
        rhs->type.is_constant) { \
        AnanasSON_NodeType type; \
        type.kind = AnanasSON_NodeType_Integer; \
        type.is_constant = 1; \
        type.u.const_integer = lhs->type.u.const_integer op rhs->type.u.const_integer; \
        return type; \
    } else return (AnanasSON_NodeType) {.kind = AnanasSON_NodeType_Bottom}; \
} while (0)

    switch (node->kind) {
    case AnanasSON_NodeKind_Add: {
        IBINOP(+);
        break;
    }
    case AnanasSON_NodeKind_Sub: {
        IBINOP(-);
        break;
    }
    case AnanasSON_NodeKind_Mul: {
        IBINOP(*);
        break;
    }
    case AnanasSON_NodeKind_Div: {
        IBINOP(/);
        break;
    }
    default: return node->type;
    }
}

static const char *node_kinds_strings[] = {
#define X(t) [AnanasSON_NodeKind_##t] = #t,
    ANANAS_SON_ENUM_NODE_KINDS
    #undef X
};

static const char *NodeName(AnanasSON_Node *node, HeliosAllocator allocator) {
    const char *prefix = node_kinds_strings[node->kind];

    switch (node->kind) {
    case AnanasSON_NodeKind_Const: {
#define FMT "%s %lld"
        U32 n = snprintf(NULL, 0, FMT, prefix, node->type.u.const_integer);
        U8 *buf = HeliosAlloc(allocator, n + 1);
        sprintf((char *)buf, FMT, prefix, node->type.u.const_integer);
        #undef FMT

        return (char *)buf;
    }
    case AnanasSON_NodeKind_Lookup:
    case AnanasSON_NodeKind_Define: {
#define FMT "%s " HELIOS_SV_FMT
        U32 n = snprintf(NULL, 0, FMT, prefix, HELIOS_SV_ARG(node->type.u.sym_name));
        U8 *buf = HeliosAlloc(allocator, n + 1);
        sprintf((char *)buf, FMT, prefix, HELIOS_SV_ARG(node->type.u.sym_name));
        #undef FMT

        return (char *)buf;
    }
    default: return prefix;
    }

    HELIOS_UNREACHABLE();
}

static B32 IsControlNode(AnanasSON_Node *node) {
    return node->kind == AnanasSON_NodeKind_Start ||
    node->kind == AnanasSON_NodeKind_Return ||
    node->kind == AnanasSON_NodeKind_Define;
}

static void Kill(AnanasSON_Node *node) {
    if (IsControlNode(node)) return;

    HELIOS_ASSERT(node->outputs.count == 0);

    for (UZ i = 0; i < node->inputs.count; ++i) {
        AnanasSON_Node *input = AnanasSON_NodeArrayAt(&node->inputs, i);

        SZ node_index = -1;
        for (UZ i = 0; i < input->outputs.count; ++i) {
            if (input->outputs.items[i] == node) {
                node_index = i;
                break;
            }
        }

        HELIOS_ASSERT(node_index >= 0);
        AnanasSON_NodeArrayOrderedRemove(&input->outputs, node_index);
        if (input->outputs.count == 0) Kill(input);
    }
}

ERMIS_IMPL_HASHMAP(HeliosStringView, AnanasSON_Node *, AnanasSON_ScopeMap, HeliosStringViewEqual, AnanasFnv1Hash)

static AnanasSON_Node *LookupSymbol(AnanasSON_CompilerState *cstate, HeliosStringView sym) {
    AnanasSON_Scope *scope = cstate->cur_scope;
    while (scope != NULL) {
        AnanasSON_Node *node;
        if (AnanasSON_ScopeMapFind(&scope->map, sym, &node)) return node;
        scope = scope->parent;
    }
    return NULL;
}

static AnanasSON_Node *Peephole(AnanasSON_CompilerState *cstate, AnanasSON_Node *node) {
    AnanasSON_NodeType type = ComputeType(node);
    node->type = type;

    if (node->kind != AnanasSON_NodeKind_Const && node->type.is_constant) {
        HELIOS_ASSERT(node->type.kind == AnanasSON_NodeType_Integer);
        AnanasSON_Node *cnode = NewConstantNode(cstate, node->type.u.const_integer);
        Kill(node);
        return Peephole(cstate, cnode);
    }

    if (node->kind == AnanasSON_NodeKind_Lookup) {
        AnanasSON_Node *sym_node = LookupSymbol(cstate, node->type.u.sym_name);
        if (sym_node != NULL) {
            Kill(node);
            return AnanasSON_NodeArrayAt(&sym_node->inputs, 0);
        }
    }

    return node;
}

static void PushScope(AnanasSON_CompilerState *cstate) {
    AnanasSON_Scope *scope = ANANAS_ARENA_STRUCT_ZERO(cstate->arena, AnanasSON_Scope);
    HeliosAllocator allocator = AnanasArenaToHeliosAllocator(cstate->arena);
    AnanasSON_ScopeMapInit(&scope->map, allocator, 37);
    scope->parent = cstate->cur_scope;
    cstate->cur_scope = scope;
}

static void PopScope(AnanasSON_CompilerState *cstate) {
    HELIOS_ASSERT(cstate->cur_scope != NULL);
    cstate->cur_scope = cstate->cur_scope->parent;
}

AnanasSON_Node *AnanasSON_Compile(AnanasSON_CompilerState *cstate, AnanasValue value) {
#define BINOP(k) do { \
AnanasList *args = list->cdr; \
HELIOS_ASSERT(args != NULL); \
HELIOS_ASSERT(args->cdr != NULL); \
\
AnanasValue lhs = args->car; \
AnanasValue rhs = args->cdr->car; \
\
AnanasSON_Node *lhs_node = AnanasSON_Compile(cstate, lhs); \
AnanasSON_Node *rhs_node = AnanasSON_Compile(cstate, rhs); \
\
AnanasSON_Node *node = NewNode(cstate, AnanasSON_NodeKind_##k, TYPE_BOTTOM); \
\
AddInput(node, lhs_node); \
AddInput(node, rhs_node); \
return Peephole(cstate, node); \
    } while (0)

    switch (value.type) {
    case AnanasValueType_Int: return Peephole(cstate, NewConstantNode(cstate, value.u.integer));
    case AnanasValueType_Function:
    case AnanasValueType_Macro:
    case AnanasValueType_Bool:
    case AnanasValueType_String: HELIOS_TODO();
    case AnanasValueType_Symbol: {
        HeliosStringView sym_name = value.u.symbol;
        AnanasSON_NodeType node_type = {0};
        node_type.u.sym_name = sym_name;
        AnanasSON_Node *node = NewNode(cstate, AnanasSON_NodeKind_Lookup, node_type);
        AddInput(node, cstate->cur_control_node);
        return Peephole(cstate, node);
    }
    case AnanasValueType_List: {
        AnanasList *list = value.u.list;
        if (list == NULL) {
            HELIOS_PANIC("cannot compile an empty list");
        }

        AnanasValue car = list->car;
        HELIOS_ASSERT(car.type == AnanasValueType_Symbol);

        HeliosStringView sym = car.u.symbol;
        if (HeliosStringViewEqualCStr(sym, "lambda")) {
            AnanasList *lambda_args_cons = list->cdr;
            HELIOS_ASSERT(lambda_args_cons != NULL);

            AnanasList *lambda_body = lambda_args_cons->cdr;

            HELIOS_ASSERT(lambda_body != NULL);

            PushScope(cstate);

            while (lambda_body->cdr != NULL) {
                AnanasSON_Compile(cstate, lambda_body->car);
                lambda_body = lambda_body->cdr;
            }

            AnanasSON_Node *ret_value_node = AnanasSON_Compile(cstate, lambda_body->car);
            AnanasSON_Node *ret_node = NewNode(cstate, AnanasSON_NodeKind_Return, TYPE_BOTTOM);
            AddInput(ret_node, cstate->cur_control_node);
            AddInput(ret_node, ret_value_node);

            PopScope(cstate);

            return Peephole(cstate, ret_node);
        } else if (HeliosStringViewEqualCStr(sym, "+")) {
            BINOP(Add);
        } else if (HeliosStringViewEqualCStr(sym, "-")) {
            BINOP(Sub);
        } else if (HeliosStringViewEqualCStr(sym, "*")) {
            BINOP(Mul);
        } else if (HeliosStringViewEqualCStr(sym, "/")) {
            BINOP(Div);
        } else if (HeliosStringViewEqualCStr(sym, "var")) {
            AnanasList *args = list->cdr;
            HELIOS_ASSERT(args != NULL);
            HELIOS_ASSERT(args->cdr != NULL);

            AnanasValue var_name_val = args->car;
            HELIOS_ASSERT(var_name_val.type == AnanasValueType_Symbol);
            HeliosStringView var_name = var_name_val.u.symbol;

            AnanasValue var_value_val = args->cdr->car;
            AnanasSON_Node *var_value = AnanasSON_Compile(cstate, var_value_val);

            AnanasSON_NodeType node_type = {0};
            node_type.u.sym_name = var_name;
            AnanasSON_Node *node = NewNode(cstate, AnanasSON_NodeKind_Define, node_type);

            if (!AnanasSON_ScopeMapInsert(&cstate->cur_scope->map, var_name, node)) {
                HELIOS_PANIC_FMT("duplicate var " HELIOS_SV_FMT, HELIOS_SV_ARG(var_name));
            }

            AddInput(node, var_value);

            cstate->cur_control_node = node;
            return Peephole(cstate, node);
        } else {
            HELIOS_PANIC_FMT("cannot compile list with " HELIOS_SV_FMT " as the car", HELIOS_SV_ARG(sym));
        }
    }
    }

    #undef BINOP

    HELIOS_UNREACHABLE();
}

static void FormatNode(AnanasSON_Node *node, HeliosString8 *s) {
    if (node->flags & AnanasSON_NodeFlag_Visited) return;

    const char *color = NULL;
    const char *name = NodeName(node, s->allocator);

    switch (node->kind) {
    case AnanasSON_NodeKind_Define:
    case AnanasSON_NodeKind_Return:
    case AnanasSON_NodeKind_Start: {
        color = "red";
        break;
    }
    case AnanasSON_NodeKind_Lookup:
    case AnanasSON_NodeKind_Add:
    case AnanasSON_NodeKind_Mul:
    case AnanasSON_NodeKind_Sub:
    case AnanasSON_NodeKind_Div:
    case AnanasSON_NodeKind_Const: {
        color = "black";
        break;
    }
    }

    HeliosString8FormatAppend(s, "\"%s\" [color=\"%s\"]; ", name, color);

    for (UZ i = 0; i < node->outputs.count; ++i) {
        AnanasSON_Node *output = AnanasSON_NodeArrayAt(&node->outputs, i);
        const char *output_name = NodeName(output, s->allocator);
        HeliosString8FormatAppend(s, "\"%s\" -> \"%s\"; ", output_name, name);
    }

    for (UZ i = 0; i < node->outputs.count; ++i) {
        AnanasSON_Node *output = AnanasSON_NodeArrayAt(&node->outputs, i);
        FormatNode(output, s);
    }

    node->flags |= AnanasSON_NodeFlag_Visited;
}

void AnanasSON_FormatNodeGraphInto(AnanasSON_CompilerState *cstate, HeliosString8 *s) {
    HeliosString8FormatAppend(s, "digraph { graph [dpi=200] [rankdir=\"BT\"]; ");

    FormatNode(cstate->start_node, s);

    HeliosString8FormatAppend(s, "}");
}
