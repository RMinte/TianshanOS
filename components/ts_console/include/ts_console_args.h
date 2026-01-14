/**
 * @file ts_console_args.h
 * @brief Console Argument Parsing Utilities
 * 
 * Helper functions for creating argument tables compatible with
 * argtable3 used by esp_console.
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#ifndef TS_CONSOLE_ARGS_H
#define TS_CONSOLE_ARGS_H

#include "argtable3/argtable3.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================*/
/*                          Argument Macros                                   */
/*===========================================================================*/

/**
 * @brief Create a literal argument (keyword)
 */
#define TS_ARG_LIT(short, long, glossary) \
    arg_lit0(short, long, glossary)

/**
 * @brief Create a required literal argument
 */
#define TS_ARG_LIT_REQ(short, long, glossary) \
    arg_lit1(short, long, glossary)

/**
 * @brief Create an integer argument
 */
#define TS_ARG_INT(short, long, datatype, glossary) \
    arg_int0(short, long, datatype, glossary)

/**
 * @brief Create a required integer argument
 */
#define TS_ARG_INT_REQ(short, long, datatype, glossary) \
    arg_int1(short, long, datatype, glossary)

/**
 * @brief Create a double argument
 */
#define TS_ARG_DBL(short, long, datatype, glossary) \
    arg_dbl0(short, long, datatype, glossary)

/**
 * @brief Create a string argument
 */
#define TS_ARG_STR(short, long, datatype, glossary) \
    arg_str0(short, long, datatype, glossary)

/**
 * @brief Create a required string argument
 */
#define TS_ARG_STR_REQ(short, long, datatype, glossary) \
    arg_str1(short, long, datatype, glossary)

/**
 * @brief Create a file argument
 */
#define TS_ARG_FILE(short, long, datatype, glossary) \
    arg_file0(short, long, datatype, glossary)

/**
 * @brief Create an end marker for argument table
 */
#define TS_ARG_END(maxerrs) \
    arg_end(maxerrs)

/*===========================================================================*/
/*                          Argument Helpers                                  */
/*===========================================================================*/

/**
 * @brief Validate argument table
 * 
 * @param argtable Argument table
 * @return true if valid
 */
bool ts_arg_validate(void **argtable);

/**
 * @brief Free argument table
 * 
 * @param argtable Argument table
 */
void ts_arg_free(void **argtable);

/**
 * @brief Parse arguments
 * 
 * @param argtable Argument table
 * @param argc Argument count
 * @param argv Argument values
 * @return Number of errors (0 = success)
 */
int ts_arg_parse(void **argtable, int argc, char **argv);

/**
 * @brief Print argument errors
 * 
 * @param argtable Argument table
 * @param progname Program name for error messages
 */
void ts_arg_print_errors(void **argtable, const char *progname);

/**
 * @brief Print usage/syntax
 * 
 * @param argtable Argument table
 * @param progname Program name
 */
void ts_arg_print_syntax(void **argtable, const char *progname);

/**
 * @brief Print glossary (help text)
 * 
 * @param argtable Argument table
 */
void ts_arg_print_glossary(void **argtable);

/**
 * @brief Get argument end structure for error access
 * 
 * @param argtable Argument table
 * @return Pointer to arg_end structure
 */
struct arg_end *ts_arg_get_end(void **argtable);

#ifdef __cplusplus
}
#endif

#endif /* TS_CONSOLE_ARGS_H */
