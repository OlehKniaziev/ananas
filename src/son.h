#ifndef ANANAS_SON_H_
#define ANANAS_SON_H_

#include "astron.h"
#include "value.h"

struct AnanasSON_Node;
typedef struct AnanasSON_Node AnanasSON_Node;

ERMIS_DECL_ARRAY(AnanasSON_Node *, AnanasSON_NodeArray)

#define ANANAS_SON_ENUM_CONTROL_NODES \
    X(Start) \
    X(Return)

#define ANANAS_SON_ENUM_DATA_NODES \
    X(Const)

typedef struct {
    enum {
        #define X(t) AnanasSON_NodeControl_##t,
        ANANAS_SON_ENUM_CONTROL_NODES
        #undef X
    } type;
} AnanasSON_NodeControl;

typedef struct {
    enum {
        #define X(t) AnanasSON_NodeData_##t,
        ANANAS_SON_ENUM_DATA_NODES
        #undef X
    } type;

    union {
        AnanasValue constant;
    } u;
} AnanasSON_NodeData;

struct AnanasSON_Node {
    enum {
        AnanasSON_NodeType_Control,
        AnanasSON_NodeType_Data,
    } type;

    union {
        AnanasSON_NodeControl control;
        AnanasSON_NodeData data;
    } u;

    AnanasSON_NodeArray inputs;
    AnanasSON_NodeArray outputs;
};

typedef struct {
    AnanasArena *arena;
    AnanasSON_Node *start_node;
} AnanasSON_CompilerState;

void AnanasSON_FormatNodeGraphInto(AnanasSON_CompilerState *, HeliosString8 *);

void AnanasSON_CompilerStateInit(AnanasSON_CompilerState *, AnanasArena *);

AnanasSON_Node *AnanasSON_Compile(AnanasSON_CompilerState *, AnanasValue);

#endif // ANANAS_SON_H_
