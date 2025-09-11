#include "son.h"
#include "print.h"

ERMIS_IMPL_ARRAY(AnanasSON_Node *, AnanasSON_NodeArray)

static AnanasSON_Node *NewNode(AnanasSON_CompilerState *cstate) {
    HeliosAllocator allocator = AnanasArenaToHeliosAllocator(cstate->arena);
    AnanasSON_Node *node = ANANAS_ARENA_STRUCT_ZERO(cstate->arena, AnanasSON_Node);
    AnanasSON_NodeArrayInit(&node->inputs, allocator, 5);
    AnanasSON_NodeArrayInit(&node->outputs, allocator, 5);
    return node;
}

static void AddInput(AnanasSON_Node *node, AnanasSON_Node *input) {
    AnanasSON_NodeArrayPush(&node->inputs, input);
    AnanasSON_NodeArrayPush(&input->outputs, node);
}

static AnanasSON_Node *NewConstantNode(AnanasSON_CompilerState *cstate, AnanasSON_Node *start, AnanasValue value) {
    HELIOS_ASSERT(start->type == AnanasSON_NodeType_Control && start->u.control.type == AnanasSON_NodeControl_Start);

    AnanasSON_Node *node = NewNode(cstate);
    node->type = AnanasSON_NodeType_Data;
    node->u.data.type = AnanasSON_NodeData_Const;
    node->u.data.u.constant = value;

    AddInput(node, start);
    return node;
}

static AnanasSON_Node *StartNode(AnanasSON_CompilerState *cstate) {
    if (cstate->start_node == NULL) {
        AnanasSON_Node *n = NewNode(cstate);
        n->type = AnanasSON_NodeType_Control;
        n->u.control.type = AnanasSON_NodeControl_Start;
        cstate->start_node = n;
    }

    return cstate->start_node;
}

void AnanasSON_CompilerStateInit(AnanasSON_CompilerState *cstate, AnanasArena *arena) {
    cstate->arena = arena;
    cstate->start_node = NULL;
}

AnanasSON_Node *AnanasSON_Compile(AnanasSON_CompilerState *cstate, AnanasValue value) {
    AnanasSON_Node *start = StartNode(cstate);

    switch (value.type) {
    case AnanasValueType_Function:
    case AnanasValueType_Macro:
    case AnanasValueType_Bool:
    case AnanasValueType_String:
    case AnanasValueType_Int: return NewConstantNode(cstate, start, value);
    case AnanasValueType_Symbol: HELIOS_TODO();
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

            while (lambda_body->cdr != NULL) {
                AnanasSON_Compile(cstate, lambda_body->car);
                lambda_body = lambda_body->cdr;
            }

            AnanasSON_Node *ret_value_node = AnanasSON_Compile(cstate, lambda_body->car);
            AnanasSON_Node *ret_node = NewNode(cstate);
            ret_node->type = AnanasSON_NodeType_Control;
            ret_node->u.control.type = AnanasSON_NodeControl_Return;
            AddInput(ret_node, start);
            AddInput(ret_node, ret_value_node);
            return ret_node;
        } else {
            HELIOS_PANIC_FMT("cannot compile list with " HELIOS_SV_FMT " as the car", HELIOS_SV_ARG(sym));
        }
    }
    }
}

static const char *control_node_types_strings[] = {
#define X(t) [AnanasSON_NodeControl_##t] = #t,
    ANANAS_SON_ENUM_CONTROL_NODES
    #undef X
};

static const char *data_node_types_strings[] = {
#define X(t) [AnanasSON_NodeData_##t] = #t,
    ANANAS_SON_ENUM_DATA_NODES
    #undef X
};

static const char *NodeName(AnanasSON_Node *node, HeliosAllocator allocator) {
    switch (node->type) {
    case AnanasSON_NodeType_Control: {
        return control_node_types_strings[node->u.control.type];
    }
    case AnanasSON_NodeType_Data: {
        const char *prefix = data_node_types_strings[node->u.data.type];

        switch (node->u.data.type) {
        case AnanasSON_NodeData_Const: {
            HeliosStringView const_sv = AnanasPrint(allocator, node->u.data.u.constant);

            #define FMT "%s " HELIOS_SV_FMT
            U32 n = snprintf(NULL, 0, FMT, prefix, HELIOS_SV_ARG(const_sv));
            U8 *buf = HeliosAlloc(allocator, n + 1);
            sprintf((char *)buf, FMT, prefix, HELIOS_SV_ARG(const_sv));
            #undef FMT

            return (char *)buf;
        }
        }
    }
    }

    HELIOS_UNREACHABLE();
}

static void FormatNode(AnanasSON_Node *node, HeliosString8 *s) {
    const char *color = NULL;
    const char *name = NodeName(node, s->allocator);

    switch (node->type) {
    case AnanasSON_NodeType_Control: {
        color = "red";
        break;
    }
    case AnanasSON_NodeType_Data: {
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
}

void AnanasSON_FormatNodeGraphInto(AnanasSON_CompilerState *cstate, HeliosString8 *s) {
    HeliosString8FormatAppend(s, "digraph { graph [dpi=200] [rankdir=\"BT\"]; ");

    FormatNode(cstate->start_node, s);

    HeliosString8FormatAppend(s, "}");
}
