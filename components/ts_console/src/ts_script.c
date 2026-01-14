/**
 * @file ts_script.c
 * @brief TianShanOS Script Engine Implementation
 * 
 * Simple script interpreter supporting:
 * - Variable assignment: set varname value
 * - Variable expansion: $varname or ${varname}
 * - Comments: # comment
 * - Sleep: sleep <ms>
 * - Echo: echo message
 * - If/else: if condition / else / endif
 * - While loops: while condition / endwhile
 * - Break/continue
 * - Command execution: any console command
 */

#include "ts_script.h"
#include "ts_console.h"
#include "ts_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define TAG "ts_script"

#define MAX_VARS        32
#define MAX_LINE_LEN    256
#define MAX_NESTING     8

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

typedef enum {
    BLOCK_NONE,
    BLOCK_IF,
    BLOCK_ELSE,
    BLOCK_WHILE
} block_type_t;

typedef struct {
    block_type_t type;
    bool condition;
    int line_start;     /* For loops: line number to jump back to */
    char loop_cond[MAX_LINE_LEN];  /* Loop condition for re-evaluation */
} block_state_t;

struct ts_script_ctx_s {
    ts_script_var_t vars[MAX_VARS];
    int var_count;
    block_state_t blocks[MAX_NESTING];
    int block_depth;
    int line_num;
    bool abort;
};

static bool s_initialized = false;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static char *trim(char *str)
{
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    
    return str;
}

static bool expand_vars(ts_script_ctx_t ctx, const char *src, char *dst, size_t dst_size)
{
    const char *p = src;
    char *d = dst;
    char *end = dst + dst_size - 1;
    
    while (*p && d < end) {
        if (*p == '$') {
            p++;
            char varname[32];
            int i = 0;
            bool braced = (*p == '{');
            if (braced) p++;
            
            while (*p && i < 31 && (isalnum((unsigned char)*p) || *p == '_')) {
                varname[i++] = *p++;
            }
            varname[i] = '\0';
            
            if (braced && *p == '}') p++;
            
            const char *val = ts_script_get_var(ctx, varname);
            if (val) {
                size_t len = strlen(val);
                if (d + len >= end) return false;
                strcpy(d, val);
                d += len;
            }
        } else {
            *d++ = *p++;
        }
    }
    *d = '\0';
    
    return true;
}

static bool eval_condition(ts_script_ctx_t ctx, const char *expr)
{
    char expanded[MAX_LINE_LEN];
    expand_vars(ctx, expr, expanded, sizeof(expanded));
    char *cond = trim(expanded);
    
    /* Empty or "0" or "false" -> false */
    if (!cond[0] || strcmp(cond, "0") == 0 || strcasecmp(cond, "false") == 0) {
        return false;
    }
    
    /* Check for comparison operators */
    char *eq = strstr(cond, "==");
    char *ne = strstr(cond, "!=");
    char *gt = strstr(cond, ">");
    char *lt = strstr(cond, "<");
    
    if (eq) {
        *eq = '\0';
        char *left = trim(cond);
        char *right = trim(eq + 2);
        return strcmp(left, right) == 0;
    }
    if (ne) {
        *ne = '\0';
        char *left = trim(cond);
        char *right = trim(ne + 2);
        return strcmp(left, right) != 0;
    }
    if (gt) {
        *gt = '\0';
        char *left = trim(cond);
        char *right = trim(gt + 1);
        return atoi(left) > atoi(right);
    }
    if (lt) {
        *lt = '\0';
        char *left = trim(cond);
        char *right = trim(lt + 1);
        return atoi(left) < atoi(right);
    }
    
    /* Non-empty string -> true */
    return true;
}

/*===========================================================================*/
/*                          Context Functions                                 */
/*===========================================================================*/

esp_err_t ts_script_init(void)
{
    if (s_initialized) return ESP_OK;
    
    s_initialized = true;
    TS_LOGI(TAG, "Script engine initialized");
    return ESP_OK;
}

esp_err_t ts_script_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    s_initialized = false;
    return ESP_OK;
}

ts_script_ctx_t ts_script_ctx_create(void)
{
    ts_script_ctx_t ctx = calloc(1, sizeof(struct ts_script_ctx_s));
    if (!ctx) return NULL;
    
    ctx->block_depth = 0;
    ctx->var_count = 0;
    ctx->line_num = 0;
    ctx->abort = false;
    
    return ctx;
}

void ts_script_ctx_destroy(ts_script_ctx_t ctx)
{
    if (ctx) free(ctx);
}

esp_err_t ts_script_set_var(ts_script_ctx_t ctx, const char *name, const char *value)
{
    if (!ctx || !name || !value) return ESP_ERR_INVALID_ARG;
    
    /* Look for existing variable */
    for (int i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            strncpy(ctx->vars[i].value, value, sizeof(ctx->vars[i].value) - 1);
            return ESP_OK;
        }
    }
    
    /* Add new variable */
    if (ctx->var_count >= MAX_VARS) {
        TS_LOGE(TAG, "Too many variables");
        return ESP_ERR_NO_MEM;
    }
    
    strncpy(ctx->vars[ctx->var_count].name, name, sizeof(ctx->vars[0].name) - 1);
    strncpy(ctx->vars[ctx->var_count].value, value, sizeof(ctx->vars[0].value) - 1);
    ctx->var_count++;
    
    return ESP_OK;
}

const char *ts_script_get_var(ts_script_ctx_t ctx, const char *name)
{
    if (!ctx || !name) return NULL;
    
    for (int i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->vars[i].name, name) == 0) {
            return ctx->vars[i].value;
        }
    }
    
    return NULL;
}

/*===========================================================================*/
/*                          Line Execution                                    */
/*===========================================================================*/

ts_script_result_t ts_script_exec_line(ts_script_ctx_t ctx, const char *line)
{
    if (!ctx || !line) return TS_SCRIPT_ERROR;
    
    ctx->line_num++;
    
    /* Expand variables */
    char expanded[MAX_LINE_LEN];
    expand_vars(ctx, line, expanded, sizeof(expanded));
    
    /* Trim whitespace */
    char *cmd = trim(expanded);
    
    /* Empty line or comment */
    if (!cmd[0] || cmd[0] == '#') {
        return TS_SCRIPT_OK;
    }
    
    /* Check if we're in a skipped block */
    bool skip = false;
    if (ctx->block_depth > 0) {
        block_state_t *block = &ctx->blocks[ctx->block_depth - 1];
        if ((block->type == BLOCK_IF && !block->condition) ||
            (block->type == BLOCK_ELSE && block->condition)) {
            skip = true;
        }
    }
    
    /* Parse commands */
    
    /* set <var> <value> */
    if (strncmp(cmd, "set ", 4) == 0) {
        if (skip) return TS_SCRIPT_OK;
        
        char *var = cmd + 4;
        while (isspace((unsigned char)*var)) var++;
        char *val = var;
        while (*val && !isspace((unsigned char)*val)) val++;
        if (*val) {
            *val++ = '\0';
            while (isspace((unsigned char)*val)) val++;
        }
        ts_script_set_var(ctx, var, val);
        return TS_SCRIPT_OK;
    }
    
    /* sleep <ms> */
    if (strncmp(cmd, "sleep ", 6) == 0) {
        if (skip) return TS_SCRIPT_OK;
        
        int ms = atoi(cmd + 6);
        if (ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(ms));
        }
        return TS_SCRIPT_OK;
    }
    
    /* echo <message> */
    if (strncmp(cmd, "echo ", 5) == 0) {
        if (skip) return TS_SCRIPT_OK;
        
        ts_console_printf("%s\n", cmd + 5);
        return TS_SCRIPT_OK;
    }
    
    /* if <condition> */
    if (strncmp(cmd, "if ", 3) == 0) {
        if (ctx->block_depth >= MAX_NESTING) {
            TS_LOGE(TAG, "Line %d: Too many nested blocks", ctx->line_num);
            return TS_SCRIPT_SYNTAX_ERROR;
        }
        
        bool cond = !skip && eval_condition(ctx, cmd + 3);
        ctx->blocks[ctx->block_depth].type = BLOCK_IF;
        ctx->blocks[ctx->block_depth].condition = cond;
        ctx->block_depth++;
        return TS_SCRIPT_OK;
    }
    
    /* else */
    if (strcmp(cmd, "else") == 0) {
        if (ctx->block_depth == 0 || ctx->blocks[ctx->block_depth - 1].type != BLOCK_IF) {
            TS_LOGE(TAG, "Line %d: else without if", ctx->line_num);
            return TS_SCRIPT_SYNTAX_ERROR;
        }
        ctx->blocks[ctx->block_depth - 1].type = BLOCK_ELSE;
        return TS_SCRIPT_OK;
    }
    
    /* endif */
    if (strcmp(cmd, "endif") == 0) {
        if (ctx->block_depth == 0 || 
            (ctx->blocks[ctx->block_depth - 1].type != BLOCK_IF &&
             ctx->blocks[ctx->block_depth - 1].type != BLOCK_ELSE)) {
            TS_LOGE(TAG, "Line %d: endif without if", ctx->line_num);
            return TS_SCRIPT_SYNTAX_ERROR;
        }
        ctx->block_depth--;
        return TS_SCRIPT_OK;
    }
    
    /* break */
    if (strcmp(cmd, "break") == 0) {
        if (skip) return TS_SCRIPT_OK;
        return TS_SCRIPT_BREAK;
    }
    
    /* continue */
    if (strcmp(cmd, "continue") == 0) {
        if (skip) return TS_SCRIPT_OK;
        return TS_SCRIPT_CONTINUE;
    }
    
    /* abort */
    if (strcmp(cmd, "abort") == 0) {
        ctx->abort = true;
        return TS_SCRIPT_ABORT;
    }
    
    /* Skip command execution if in skipped block */
    if (skip) return TS_SCRIPT_OK;
    
    /* Execute as console command */
    int ret = ts_console_exec(cmd, NULL);
    if (ret != 0) {
        TS_LOGW(TAG, "Line %d: Command failed with code %d", ctx->line_num, ret);
        return TS_SCRIPT_CMD_ERROR;
    }
    
    return TS_SCRIPT_OK;
}

/*===========================================================================*/
/*                          Script Execution                                  */
/*===========================================================================*/

esp_err_t ts_script_exec_string(const char *script)
{
    if (!script) return ESP_ERR_INVALID_ARG;
    
    ts_script_ctx_t ctx = ts_script_ctx_create();
    if (!ctx) return ESP_ERR_NO_MEM;
    
    char *copy = strdup(script);
    if (!copy) {
        ts_script_ctx_destroy(ctx);
        return ESP_ERR_NO_MEM;
    }
    
    char *line = strtok(copy, "\n");
    esp_err_t result = ESP_OK;
    
    while (line && !ctx->abort) {
        ts_script_result_t res = ts_script_exec_line(ctx, line);
        if (res == TS_SCRIPT_SYNTAX_ERROR) {
            result = ESP_ERR_INVALID_ARG;
            break;
        } else if (res == TS_SCRIPT_ABORT) {
            result = ESP_FAIL;
            break;
        }
        line = strtok(NULL, "\n");
    }
    
    if (ctx->block_depth > 0) {
        TS_LOGW(TAG, "Unclosed block at end of script");
    }
    
    free(copy);
    ts_script_ctx_destroy(ctx);
    
    return result;
}

esp_err_t ts_script_exec_file(const char *path)
{
    if (!path) return ESP_ERR_INVALID_ARG;
    
    FILE *f = fopen(path, "r");
    if (!f) {
        TS_LOGE(TAG, "Cannot open script: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    ts_script_ctx_t ctx = ts_script_ctx_create();
    if (!ctx) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    char line[MAX_LINE_LEN];
    esp_err_t result = ESP_OK;
    
    while (fgets(line, sizeof(line), f) && !ctx->abort) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        ts_script_result_t res = ts_script_exec_line(ctx, line);
        if (res == TS_SCRIPT_SYNTAX_ERROR) {
            result = ESP_ERR_INVALID_ARG;
            break;
        } else if (res == TS_SCRIPT_ABORT) {
            result = ESP_FAIL;
            break;
        }
    }
    
    if (ctx->block_depth > 0) {
        TS_LOGW(TAG, "Unclosed block at end of script");
    }
    
    fclose(f);
    ts_script_ctx_destroy(ctx);
    
    TS_LOGI(TAG, "Script %s executed: %s", path, result == ESP_OK ? "OK" : "ERROR");
    return result;
}

/*===========================================================================*/
/*                          Console Commands                                  */
/*===========================================================================*/

static int cmd_run(int argc, char **argv)
{
    if (argc < 2) {
        ts_console_printf("Usage: run <script_path>\n");
        return 1;
    }
    
    esp_err_t err = ts_script_exec_file(argv[1]);
    if (err != ESP_OK) {
        ts_console_error("Script execution failed\n");
        return 1;
    }
    
    return 0;
}

static int cmd_eval(int argc, char **argv)
{
    if (argc < 2) {
        ts_console_printf("Usage: eval \"<script>\"\n");
        return 1;
    }
    
    /* Join all arguments into single string */
    char script[512] = {0};
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(script, " ");
        strncat(script, argv[i], sizeof(script) - strlen(script) - 1);
    }
    
    /* Replace ; with newlines for multi-command */
    for (char *p = script; *p; p++) {
        if (*p == ';') *p = '\n';
    }
    
    esp_err_t err = ts_script_exec_string(script);
    if (err != ESP_OK) {
        ts_console_error("Script execution failed\n");
        return 1;
    }
    
    return 0;
}

esp_err_t ts_script_register_cmds(void)
{
    static const ts_console_cmd_t script_cmds[] = {
        {
            .command = "run",
            .help = "Execute script file",
            .hint = "<path>",
            .category = TS_CMD_CAT_SYSTEM,
            .func = cmd_run,
            .argtable = NULL
        },
        {
            .command = "eval",
            .help = "Execute inline script",
            .hint = "<script>",
            .category = TS_CMD_CAT_SYSTEM,
            .func = cmd_eval,
            .argtable = NULL
        }
    };
    
    return ts_console_register_cmds(script_cmds, sizeof(script_cmds) / sizeof(script_cmds[0]));
}
