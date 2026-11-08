#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${IDF_PATH:?NOT RUN: set IDF_PATH to the project SDK}"
build=$(mktemp -d /tmp/ts-ws-adapter.XXXXXX)
trap 'rm -rf "$build"' EXIT
cjson="$IDF_PATH/components/json/cJSON"
python3 - "$build" <<'PYCODE'
from pathlib import Path
import sys
s=Path('components/ts_api/src/ts_api_ssh.c').read_text()
parts=[]
for name in ['api_ssh_cancel_impl','api_ssh_cancel','api_ssh_cancel_for_session']:
 a=s.index('static esp_err_t '+name+'(');b=s.index('\n}',a)+2;parts.append(s[a:b])
Path(sys.argv[1],'api_cancel.inc').write_text('\n'.join(parts))
PYCODE
"${CC:-cc}" -std=gnu11 -g -Wno-deprecated-declarations -Wno-macro-redefined -fsanitize=address,undefined \
 -I"$build" -Itests/ws_subscriptions/stubs -Icomponents/ts_webui/include -Icomponents/ts_webui/src -Icomponents/ts_security/include -Icomponents/ts_automation/include -Icomponents/ts_drivers/include -Icomponents/ts_console/include -Icomponents/ts_net/include -I"$cjson" \
 tests/ws_subscriptions/test_control.c "$cjson/cJSON.c" -o "$build/adapter"
"$build/adapter" "$@"
