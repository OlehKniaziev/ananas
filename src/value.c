#include "value.h"

ERMIS_IMPL_ARRAY(AnanasValue, AnanasValueArray)

ERMIS_DECL_ARRAY(HeliosStringView, AnanasParamsArray)
ERMIS_IMPL_ARRAY(HeliosStringView, AnanasParamsArray)

B32 AnanasParseParamsFromList(HeliosAllocator arena_allocator,
                              AnanasList *params_list,
                              AnanasParams *out_params,
                              AnanasErrorContext *error_ctx) {
    AnanasParamsArray params_array;
    AnanasParamsArrayInit(&params_array, arena_allocator, 16);

    out_params->variable = 0;

    while (params_list != NULL) {
        AnanasValue param_node = params_list->car;
        if (param_node.type != AnanasValueType_Symbol) {
            AnanasErrorContextMessage(error_ctx, param_node.token.row, param_node.token.col, "expected a symbol as a parameter name");
            return 0;
        }

        HeliosStringView param = param_node.u.symbol;

        if (HeliosStringViewEqualCStr(param, ".")) {
            if (params_list->cdr == NULL) {
                AnanasErrorContextMessage(error_ctx,
                                          param_node.token.row,
                                          param_node.token.col,
                                          "expected a param name after '.'");
                return 0;
            }

            if (params_list->cdr->cdr != NULL) {
                AnanasErrorContextMessage(error_ctx,
                                          param_node.token.row,
                                          param_node.token.col,
                                          "expected only one parameter name after '.'");
                return 0;
            }

            param_node = params_list->cdr->car;
            if (param_node.type != AnanasValueType_Symbol) {
                AnanasErrorContextMessage(error_ctx,
                                          param_node.token.row,
                                          param_node.token.col,
                                          "expected a symbol as a parameter name");
                return 0;
            }

            param = param_node.u.symbol;
            AnanasParamsArrayPush(&params_array, param);
            out_params->variable = 1;
            break;
        }

        AnanasParamsArrayPush(&params_array, param);

        params_list = params_list->cdr;
    }

    out_params->names = params_array.items;
    out_params->count = params_array.count;

    return 1;
}

B32 AnanasEval(AnanasValue, HeliosAllocator, struct AnanasEnv *, AnanasValue *, AnanasErrorContext *);

B32 AnanasUnquoteForm(AnanasList *args,
                      HeliosAllocator arena,
                      struct AnanasEnv *env,
                      AnanasErrorContext *error_ctx) {
    while (args != NULL) {
        AnanasValue arg = args->car;
        AnanasList *current_args = args;
        args = args->cdr;

        if (arg.type != AnanasValueType_List) continue;

        AnanasList *arg_list = arg.u.list;
        if (arg_list == NULL) continue;
        if (!AnanasUnquoteForm(arg_list, arena, env, error_ctx)) return 0;

        AnanasValue car = arg_list->car;
        if (car.type != AnanasValueType_Symbol) continue;

        HeliosStringView car_symbol = car.u.symbol;
        if (HeliosStringViewEqualCStr(car_symbol, "unquote")) {
            AnanasList *unquote_args = arg_list->cdr;
            if (unquote_args == NULL) {
                AnanasErrorContextMessage(error_ctx, car.token.row, car.token.col, "no argument passed to 'unquote' form");
                return 0;
            }

            if (unquote_args->cdr != NULL) {
                AnanasErrorContextMessage(error_ctx, car.token.row, car.token.col, "'unquote' expects exactly 1 argument");
                return 0;
            }

            AnanasValue unquote_arg = unquote_args->car;
            if (unquote_arg.type == AnanasValueType_List) {
                unquote_arg.u.list = AnanasListCopy(arena, unquote_arg.u.list);
            }
            if (!AnanasEval(unquote_arg, arena, env, &current_args->car, error_ctx)) return 0;
        } else if (HeliosStringViewEqualCStr(car_symbol, "unquote-splice")) {
            AnanasList *unquote_args = arg_list->cdr;
            if (unquote_args == NULL) {
                AnanasErrorContextMessage(error_ctx, car.token.row, car.token.col, "no argument passed to 'unquote-splice' form");
                return 0;
            }

            if (unquote_args->cdr != NULL) {
                AnanasErrorContextMessage(error_ctx, car.token.row, car.token.col, "'unquote-splice' expects exactly 1 argument");
                return 0;
            }

            AnanasValue unquote_arg = unquote_args->car;
            AnanasValue unquoted_form;
            if (!AnanasEval(unquote_arg, arena, env, &unquoted_form, error_ctx)) return 0;

            if (unquoted_form.type != AnanasValueType_List || unquoted_form.u.list == NULL) {
                current_args->car = unquoted_form;
            } else {
                AnanasList *unquoted_list = AnanasListCopy(arena, unquoted_form.u.list);
                if (unquoted_list != NULL) {
                    AnanasList *current_unquoted_list = unquoted_list;
                    while (current_unquoted_list->cdr != NULL) {
                        current_unquoted_list = current_unquoted_list->cdr;
                    }

                    current_unquoted_list->cdr = args;

                    current_args->car = unquoted_list->car;
                    current_args->cdr = unquoted_list->cdr;
                }
            }
        }
    }

    return 1;
}
