"""Compile the real SDK JSON target under the actual application CMake policy.
The baseline option compiles unchanged SDK defaults for a negative comparison.
No SDK source edits, devices or network are involved.
"""
from pathlib import Path
import argparse, os, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[2]
IDF = Path(os.environ.get('IDF_PATH', '/Users/massif/esp/v5.5.2/esp-idf'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
cmake = shutil.which('cmake') or '/Users/massif/.espressif/tools/cmake/3.30.2/CMake.app/Contents/bin/cmake'
code = r'''
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned live, peak, checks;
static void *counted_malloc(size_t size) {void *p=malloc(size);if(p){++live;if(live>peak)peak=live;}return p;}
static void counted_free(void *p) {if(p){assert(live);--live;free(p);}}
static void check(int ok,const char *why) {++checks;if(!ok){fprintf(stderr,"FAIL %s\n",why);abort();}}
static char *nested(unsigned depth,int object) {
    char *text=malloc(depth*7+8),*at=text;assert(text);
    for(unsigned i=0;i<depth;++i) {if(object){memcpy(at,"{\"v\":",5);at+=5;}else *at++='[';}
    *at++='0';for(unsigned i=0;i<depth;++i)*at++=object?'}':']';*at=0;return text;
}
int main(void) {
    cJSON_Hooks hooks={counted_malloc,counted_free};cJSON_InitHooks(&hooks);
    for(unsigned object=0;object<2;++object) {
        for(unsigned depth=1;depth<=32;++depth) {
            char *text=nested(depth,object);cJSON *value=cJSON_Parse(text);
            check(value!=NULL,"existing JSON nesting levels1..32 remain accepted");
            char *roundtrip=cJSON_PrintUnformatted(value);check(roundtrip&&!strcmp(roundtrip,text),"exact normal nested content round trips");
            cJSON_free(roundtrip);cJSON_Delete(value);free(text);check(!live,"normal JSON frees all parser allocations");
        }
        for(unsigned depth=33;depth<=4096;depth*=2) {
            peak=0;char *text=nested(depth,object);cJSON *value=cJSON_Parse(text);
            if(value){cJSON_Delete(value);fprintf(stderr,"FAIL actual SDK parser accepts JSON beyond application nesting budget\n");free(text);return 1;}
            check(peak<=65,"adversarial nesting cannot allocate beyond the32 level parser budget");
            check(!live,"rejected deep JSON frees every partial tree");free(text);
        }
    }
    const char *normal="{\"brackets\":\"[[[{{{\\\"not nesting\\\"}}}]]]\",\"rules\":[{\"conditions\":[{\"value\":0}],\"actions\":[{\"type\":\"log\",\"message\":\"fixture\"}]}]}";
    cJSON *value=cJSON_Parse(normal);check(value!=NULL,"quoted/escaped delimiters and normal WebUI-style rule remain accepted");cJSON_Delete(value);check(!live,"final JSON allocation balance");
    printf("%u checks: actual SDK cJSON recursion bound,levels1..32 roundtrip,33..4096 rejection,bounded partial allocations and cleanup\n",checks);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='ts-json-depth-') as folder:
    temp = Path(folder)
    (temp/'test.c').write_text(code)
    policy = '' if args.baseline else f'include("{ROOT}/cmake/json_limits.cmake")\n'
    (temp/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.16)
project(ts_json_depth C)
add_library(sdk_json STATIC "{IDF}/components/json/cJSON/cJSON.c" "{IDF}/components/json/cJSON/cJSON_Utils.c")
target_include_directories(sdk_json PUBLIC "{IDF}/components/json/cJSON")
function(idf_component_get_property result component property)
  if(NOT component STREQUAL "json" OR NOT property STREQUAL "COMPONENT_LIB")
    message(FATAL_ERROR "Unexpected SDK component policy target")
  endif()
  set(${{result}} sdk_json PARENT_SCOPE)
endfunction()
{policy}add_executable(json_depth test.c)
target_link_libraries(json_depth PRIVATE sdk_json m)
target_compile_options(sdk_json PRIVATE -g -fsanitize=address,undefined)
target_compile_options(json_depth PRIVATE -g -fsanitize=address,undefined)
target_link_options(json_depth PRIVATE -fsanitize=address,undefined)
''')
    subprocess.run([cmake,'-S',str(temp),'-B',str(temp/'build')],check=True,timeout=30,capture_output=True)
    subprocess.run([cmake,'--build',str(temp/'build'),'-j2'],check=True,timeout=30,capture_output=True)
    subprocess.run([str(temp/'build/json_depth')],check=True,timeout=30)
