#ifndef ANANAS_SON_H_
#define ANANAS_SON_H_

#include "astron.h"
#include "value.h"

struct AnanasSON_Node;
typedef struct AnanasSON_Node AnanasSON_Node;

ERMIS_DECL_ARRAY(AnanasSON_Node *, AnanasSON_NodeArray)

#define ANANAS_SON_ENUM_NODE_TYPES \
    X(Bottom) \
    X(Integer)

#define ANANAS_SON_ENUM_NODE_KINDS \
    X(Start) \
    X(Return) \
    X(Const) \
    X(Add) \
    X(Sub) \
    X(Mul) \
    X(Div) \
    X(Lookup) \
    X(Define)

typedef struct {
    enum {
        #define X(t) AnanasSON_NodeType_##t,
        ANANAS_SON_ENUM_NODE_TYPES
        #undef X
    } kind;

    B32 is_constant;
    union {
        S64 const_integer;
        HeliosStringView sym_name;
    } u;
} AnanasSON_NodeType;

_Static_assert(AnanasSON_NodeType_Bottom == 0, "Bottom should be 0");

enum {
    AnanasSON_NodeFlag_Visited = 1 << 0,
};

typedef enum {
    #define X(k) AnanasSON_NodeKind_##k,
    ANANAS_SON_ENUM_NODE_KINDS
    #undef X
} AnanasSON_NodeKind;

struct AnanasSON_Node {
    AnanasSON_NodeType type;

    AnanasSON_NodeKind kind;

    U32 flags;

    AnanasSON_NodeArray inputs;
    AnanasSON_NodeArray outputs;
};

typedef struct {
    AnanasArena *arena;
    AnanasSON_Node *start_node;
    AnanasSON_Node *cur_control_node;
} AnanasSON_CompilerState;

void AnanasSON_FormatNodeGraphInto(AnanasSON_CompilerState *, HeliosString8 *);

void AnanasSON_CompilerStateInit(AnanasSON_CompilerState *, AnanasArena *);

AnanasSON_Node *AnanasSON_Compile(AnanasSON_CompilerState *, AnanasValue);

#endif // ANANAS_SON_H_
