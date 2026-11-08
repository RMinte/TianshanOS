#!/usr/bin/env python3
"""Production auth unit + actual generic_handler, disposable PKI, no network."""
from pathlib import Path
from datetime import datetime,timezone
import argparse,os,re,subprocess,tempfile
from cryptography import x509
from cryptography.x509.oid import NameOID,ExtendedKeyUsageOID
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument("--source-ref",help="Use auth/server production code from a git revision")
args=parser.parse_args()
IDF=Path(os.environ.get("IDF_PATH","/Users/massif/esp/v5.5.2/esp-idf"))
MBED=Path(os.environ.get("CERT_MBEDTLS_BUILD","/tmp/tianshan-cert-mbedtls"))
def source(path):
    return subprocess.check_output(["git","show",f"{args.source_ref}:{path}"],cwd=ROOT,text=True) if args.source_ref else (ROOT/path).read_text()
def function(text,name):
    match=re.search(r"^(?:static )?[^\n;]+\b"+name+r"\([^;]+?\)\s*\{",text,re.M)
    assert match,name
    return text[match.start():text.index("\n}",match.start())+2]+"\n"
with tempfile.TemporaryDirectory(prefix="ts-mtls-auth-") as tmp:
    d=Path(tmp);stub=d/"stubs";stub.mkdir()
    (stub/"esp_http_server.h").write_text("""#pragma once
#include "esp_err.h"
typedef void *httpd_handle_t;
typedef int httpd_method_t;
typedef struct { void *user_ctx; } httpd_req_t;
""")
    ca_key=ec.generate_private_key(ec.SECP256R1())
    leaf_key=ec.generate_private_key(ec.SECP256R1())
    issuer=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,"Isolated auth test root")])
    def issue(label,role_values,serial,ca=False):
        subject=issuer if ca else x509.Name([
            x509.NameAttribute(NameOID.COMMON_NAME,"fixture"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME,"Test Organization"),
            *[x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME,v) for v in role_values]])
        cert=(x509.CertificateBuilder().subject_name(subject).issuer_name(issuer)
            .public_key((ca_key if ca else leaf_key).public_key()).serial_number(serial)
            .not_valid_before(datetime(2020,1,1,tzinfo=timezone.utc))
            .not_valid_after(datetime(2099,1,1,tzinfo=timezone.utc))
            .add_extension(x509.BasicConstraints(ca=ca,path_length=None),critical=True)
            .add_extension(x509.KeyUsage(digital_signature=True,content_commitment=False,
                key_encipherment=False,data_encipherment=False,key_agreement=False,
                key_cert_sign=ca,crl_sign=ca,encipher_only=None,decipher_only=None),critical=True))
        if not ca:cert=cert.add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CLIENT_AUTH]),critical=False)
        (d/(label+".pem")).write_bytes(cert.sign(ca_key,hashes.SHA256()).public_bytes(serialization.Encoding.PEM))
    issue("root",[],1,True)
    cases={n:[n] for n in ["anonymous","viewer","operator","admin","device"]}
    cases.update({"mixed-case":["AdMiN"],"missing-ou":[],"unknown-ou":["NotAdmin"],
        "duplicate-same":["admin","admin"],"duplicate-mixed":["admin","device"],
        "nul-role":["admin\0device"],"long-role":["admin"+"x"*35]})
    for i,(name,roles) in enumerate(cases.items(),2):issue(name,roles,i)
    server=source("components/ts_https/src/ts_https.c")
    pieces=[]
    # The older server had a separate extractor; include it only for before evidence.
    if "static void extract_auth_from_cert(" in server:
        pieces += [function(server,"extract_dn_field"),
            '#define OID_CN "\\x55\\x04\\x03"\n#define OID_O "\\x55\\x04\\x0a"\n#define OID_OU "\\x55\\x04\\x0b"\n',
            function(server,"extract_auth_from_cert")]
    if "client_auth_current" in server:
        pieces.append(function(source("components/ts_cert/src/ts_cert.c"),"ts_cert_prerequisites"))
    pieces.append(function(server,"generic_handler"))
    (d/"https_auth_handler.inc").write_text("\n".join(pieces))
    auth=d/"auth.c";auth.write_text(source("components/ts_https/src/ts_https_auth.c"))
    includes=[d,stub,ROOT/"tests/certificate/stubs",ROOT/"components/ts_https/include",
        ROOT/"components/ts_https/src",ROOT/"components/ts_cert/include",IDF/"components/mbedtls/mbedtls/include"]
    cmd=["cc","-std=gnu11","-g","-fsanitize=address,undefined","-fno-sanitize-recover=all",
        *["-I"+str(p) for p in includes],str(ROOT/"tests/certificate/test_https_auth.c"),str(auth),
        str(ROOT/"components/ts_cert/src/ts_cert_time.c"),
        *[str(MBED/"library"/("lib"+n+".a")) for n in ["mbedtls","mbedx509","mbedcrypto"]],
        "-lpthread","-lm","-o",str(d/"auth")]
    subprocess.run(cmd,check=True,cwd=ROOT)
    subprocess.run([str(d/"auth"),str(d)],check=True,cwd=ROOT)
