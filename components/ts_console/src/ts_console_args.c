/**
 * @file ts_console_args.c
 * @brief Console Argument Parsing Utilities Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_console_args.h"
#include "ts_console.h"
#include <stdio.h>

/*===========================================================================*/
/*                          Argument Helpers                                  */
/*===========================================================================*/

bool ts_arg_validate(void **argtable)
{
    if (argtable == NULL) {
        return false;
    }
    
    /* Check that table ends with arg_end */
    int i = 0;
    while (argtable[i] != NULL) {
        i++;
    }
    
    return (i > 0);
}

void ts_arg_free(void **argtable)
{
    if (argtable == NULL) {
        return;
    }
    
    /* Count the number of entries in argtable (ends with NULL or arg_end) */
    int count = 0;
    while (argtable[count] != NULL) {
        count++;
    }
    arg_freetable(argtable, count);
}

int ts_arg_parse(void **argtable, int argc, char **argv)
{
    if (argtable == NULL || argc < 1 || argv == NULL) {
        return -1;
    }
    
    return arg_parse(argc, argv, argtable);
}

void ts_arg_print_errors(void **argtable, const char *progname)
{
    if (argtable == NULL) {
        return;
    }
    
    /* Find arg_end in table */
    struct arg_end *end = ts_arg_get_end(argtable);
    if (end) {
        arg_print_errors(stdout, end, progname);
    }
}

void ts_arg_print_syntax(void **argtable, const char *progname)
{
    if (argtable == NULL || progname == NULL) {
        return;
    }
    
    ts_console_printf("Usage: %s", progname);
    arg_print_syntax(stdout, argtable, "\n");
}

void ts_arg_print_glossary(void **argtable)
{
    if (argtable == NULL) {
        return;
    }
    
    arg_print_glossary(stdout, argtable, "  %-25s %s\n");
}

struct arg_end *ts_arg_get_end(void **argtable)
{
    if (argtable == NULL) {
        return NULL;
    }
    
    /* Find arg_end - it's the last non-NULL entry */
    int i = 0;
    while (argtable[i] != NULL) {
        i++;
    }
    
    /* The entry before NULL should be arg_end */
    if (i > 0) {
        return (struct arg_end *)argtable[i - 1];
    }
    
    return NULL;
}
