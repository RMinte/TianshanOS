#!/usr/bin/env python3
from pathlib import Path
from zipfile import ZipFile
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];os.chdir(ROOT)
p=argparse.ArgumentParser();p.add_argument('--source-package');args=p.parse_args()
IDF=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'));cjson=IDF/'components/json/cJSON'
with tempfile.TemporaryDirectory(prefix='ts-ws-input-') as tmp:
    if args.source_package:
        rel='components/ts_webui/src/ts_webui_ws.c'
        with ZipFile(args.source_package) as z:
            name='current-source/'+rel
            old=z.read(name) if name in z.namelist() else subprocess.check_output(['git','show','35e081ac8:'+rel])
        Path(tmp,'ts_webui_ws.c').write_bytes(old)
    exe=Path(tmp,'test')
    include=['tests/ws_subscriptions/stubs','components/ts_webui/include','components/ts_webui/src','components/ts_security/include','components/ts_automation/include','components/ts_drivers/include','components/ts_console/include','components/ts_net/include',str(cjson)]
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-Wno-macro-redefined','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+tmp,*['-I'+p for p in include],'tests/ws_subscriptions/test_input_admission.c',str(cjson/'cJSON.c'),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
