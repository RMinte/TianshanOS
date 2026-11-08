#!/usr/bin/env python3
"""Complete native WS authorization suite, optionally with frozen pre-fix sources."""
from pathlib import Path
from zipfile import ZipFile
import argparse,os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--source-package');p.add_argument('--case',choices=['delivery','privacy','control'],default='delivery');args=p.parse_args();idf=Path(os.environ.get('IDF_PATH','/Users/massif/esp/v5.5.2/esp-idf'))
with tempfile.TemporaryDirectory(prefix='ts-log-delivery-') as tmp:
    work=Path(tmp)
    if args.source_package:
        with ZipFile(args.source_package) as z:
            for path in ['components/ts_webui/src/ts_webui_ws.c','components/ts_webui/src/ts_ws_transport.c']:
                data=z.read('current-source/'+path) if 'current-source/'+path in z.namelist() else subprocess.check_output(['git','show','35e081ac8:'+path],cwd=ROOT)
                (work/Path(path).name).write_bytes(data)
    if args.case=='control':
        path='components/ts_api/src/ts_api_ssh.c'
        if args.source_package:
            with ZipFile(args.source_package) as z:
                text=z.read('current-source/'+path).decode() if 'current-source/'+path in z.namelist() else subprocess.check_output(['git','show','35e081ac8:'+path],cwd=ROOT,text=True)
        else:text=(ROOT/path).read_text()
        parts=[]
        names=['api_ssh_cancel_impl','api_ssh_cancel','api_ssh_cancel_for_session'] if 'static esp_err_t api_ssh_cancel_impl(' in text else ['api_ssh_cancel']
        for name in names:
            start=text.index('static esp_err_t '+name+'(');end=text.index('\n}',start)+2;parts.append(text[start:end])
        (work/'api_cancel.inc').write_text('\n'.join(parts))
    includes=[work,ROOT/'tests/ws_subscriptions/stubs',ROOT/'components/ts_webui/include',ROOT/'components/ts_webui/src',ROOT/'components/ts_security/include',ROOT/'components/ts_automation/include',ROOT/'components/ts_drivers/include',ROOT/'components/ts_console/include',ROOT/'components/ts_net/include',idf/'components/json/cJSON']
    exe=work/'log-delivery'
    subprocess.run(['cc','-std=gnu11','-g','-Wno-deprecated-declarations','-Wno-macro-redefined','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(x) for x in includes],str(ROOT/'tests/ws_subscriptions'/({'delivery':'test_terminal_auth.c','privacy':'test_ws_log_privacy.c','control':'test_control.c'}[args.case])),str(idf/'components/json/cJSON/cJSON.c'),'-lm','-o',str(exe)],check=True,cwd=ROOT)
    subprocess.run([str(exe)],check=True,cwd=ROOT,timeout=30)
