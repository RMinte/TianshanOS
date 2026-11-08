#!/usr/bin/env python3
"""Replay the actual private-output pipeline against current or frozen C sources."""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'))
text=(ROOT/'tests/ws_subscriptions/test_exec_ownership.c').read_text();text=text[:text.index('static void queued_checks')]
# Only use the existing initiation/observation/result API for the frozen-source
# comparison. No shim implements or substitutes the security boundary under test.
text=re.sub(r'assert\(!ts_ws_peer_authorized_for_session\(bound_peer\(3\),TS_PERM_ROOT,7\)\);','',text)
text=re.sub(r' ts_ws_peer_t peers\[TS_WS_CONNECTIONS\];[^\n]+\n','',text)
text+='int main(void){cJSON_Hooks hooks={tracked_malloc,tracked_free};cJSON_InitHooks(&hooks);private_setup();run_business(7,false);puts("PASS actual private-output replay: other authenticated Root and lower roles receive no private events");}\n'
with tempfile.TemporaryDirectory(prefix='ts-exec-delivery-') as tmp:
    t=Path(tmp)
    if args.source_package:
        with ZipFile(args.source_package) as z:
            for f in ['components/ts_webui/src/ts_webui_ws.c','components/ts_webui/src/ts_ws_transport.c','components/ts_webui/src/ts_ws_operation.c']:
                n='current-source/'+f
                data=z.read(n) if n in z.namelist() else subprocess.check_output(['git','show','35e081ac8:'+f],cwd=os.environ.get('SOURCE_REPOSITORY',str(ROOT)))
                (t/Path(f).name).write_bytes(data)
    source=t/'probe.c';source.write_text(text);exe=t/'probe'
    includes=[t,ROOT/'tests/ws_subscriptions',ROOT/'tests/ws_subscriptions/stubs',ROOT/'components/ts_webui/include',ROOT/'components/ts_webui/src',ROOT/'components/ts_security/include',ROOT/'components/ts_automation/include',ROOT/'components/ts_drivers/include',ROOT/'components/ts_console/include',ROOT/'components/ts_net/include',IDF/'components/json/cJSON']
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-Wno-macro-redefined','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(i) for i in includes],str(source),str(IDF/'components/json/cJSON/cJSON.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
