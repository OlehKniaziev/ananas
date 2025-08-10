#include "print.h"

HeliosStringView AnanasPrint(HeliosAllocator allocator, AnanasValue node) {
    switch (node.type) {
    case AnanasValueType_Int: {
        int required_bytes = snprintf(NULL, 0, "%ld", node.u.integer);
        U8 *buffer = HeliosAlloc(allocator, required_bytes + 1);
        sprintf((char *)buffer, "%ld", node.u.integer);
        return (HeliosStringView) {.data = buffer, .count = required_bytes};
    }
    case AnanasValueType_String: {
        int required_bytes = snprintf(NULL, 0, "\"" HELIOS_SV_FMT "\"", HELIOS_SV_ARG(node.u.string));
        U8 *buffer = HeliosAlloc(allocator, required_bytes + 1);
        sprintf((char *)buffer, "\"" HELIOS_SV_FMT "\"", HELIOS_SV_ARG(node.u.string));
        return (HeliosStringView) {.data = buffer, .count = required_bytes};

    }
    case AnanasValueType_Symbol: {
        int required_bytes = snprintf(NULL, 0, HELIOS_SV_FMT, HELIOS_SV_ARG(node.u.symbol));
        U8 *buffer = HeliosAlloc(allocator, required_bytes + 1);
        sprintf((char *)buffer, HELIOS_SV_FMT, HELIOS_SV_ARG(node.u.symbol));
        return (HeliosStringView) {.data = buffer, .count = required_bytes};
    }
    case AnanasValueType_Macro: {
        return HELIOS_SV_LIT("<macro>");
    }
    case AnanasValueType_Function: {
        /* int required_bytes = snprintf(NULL, 0, "", HELIOS_SV_ARG(node.u.symbol)); */
        /* U8 *buffer = HeliosAlloc(allocator, required_bytes + 1); */
        /* sprintf((char *)buffer, HELIOS_SV_FMT, HELIOS_SV_ARG(node.u.symbol)); */
        /* return (HeliosStringView) {.data = buffer, .count = required_bytes}; */
        return HELIOS_SV_LIT("<function>");
    }
    case AnanasValueType_List: {
        UZ buffer_cap = 32;
        U8 *buffer = HeliosAlloc(allocator, buffer_cap);

        buffer[0] = '(';

        UZ offset = 1;

        AnanasList *list = node.u.list;
        while (list != NULL) {
            HeliosStringView car = AnanasPrint(allocator, list->car);
            if (car.count + offset + 1 >= buffer_cap) {
                buffer = HeliosRealloc(allocator, buffer, buffer_cap, buffer_cap * 2);
                buffer_cap *= 2;
            }

            for (UZ i = 0; i < car.count; ++i) {
                buffer[i + offset] = car.data[i];
            }

            if (list->cdr != NULL) {
                buffer[car.count + offset] = ' ';
                offset += car.count + 1;
            } else {
                offset += car.count;
            }

            list = list->cdr;
        }

        HELIOS_VERIFY(offset < buffer_cap);

        buffer[offset] = ')';

        return (HeliosStringView) {.data = buffer, .count = offset + 1};
    }
    }
}
