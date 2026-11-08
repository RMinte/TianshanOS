/* Real certificate parsing/auth helpers and extracted production request handler.
 * Only the HTTP transport certificate lookup and response/effect boundaries are stubs. */
#include "ts_https_internal.h"
#include "ts_cert_time.h"
#include "ts_cert.h"
#include "mbedtls/ssl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define HTTPD_500_INTERNAL_SERVER_ERROR 500
static ts_https_config_t s_config = TS_HTTPS_CONFIG_DEFAULT();
static ts_cert_snapshot_t s_material={.generation=1};
static ts_cert_pki_status_t fixture_material={.generation=1,.has_private_key=true,.has_certificate=true,.has_ca_chain=true,.key_valid=true,.key_matches=true,.ca_valid=true,.time_ready=true,.cert_info={.is_valid=true}};
static esp_err_t fixture_material_error;
esp_err_t ts_cert_get_status(ts_cert_pki_status_t *out){*out=fixture_material;return fixture_material_error;}
static const mbedtls_x509_crt *fixture_cert;
static unsigned calls, checks, failures;
static int status;
static ts_https_auth_t observed;
static void check(bool condition, const char *label) {
    ++checks;
    if (!condition) { ++failures; fprintf(stderr, "FAIL %s\n", label); }
}
static const mbedtls_x509_crt *get_client_cert(httpd_req_t *r) { (void)r; return fixture_cert; }
static int httpd_resp_send_err(httpd_req_t *r,int code,const char *message) {
    (void)r; (void)message; status=code; return ESP_OK;
}
esp_err_t ts_https_send_error(ts_https_req_t *r,int code,const char *message) {(void)r;(void)message;status=code;return ESP_OK;}
esp_err_t ts_https_send_unauthorized(ts_https_req_t *r) { (void)r; status=401; return ESP_OK; }
esp_err_t ts_https_send_forbidden(ts_https_req_t *r) { (void)r; status=403; return ESP_OK; }
static int target(ts_https_req_t *r) { ++calls; observed=r->auth; status=200; return ESP_OK; }
#include "https_auth_handler.inc"

static void request(ts_https_role_t minimum, int expected) {
    unsigned before=calls;
    ts_https_endpoint_t endpoint={.handler=target,.min_role=minimum};
    httpd_req_t req={.user_ctx=&endpoint};
    check(generic_handler(&req)==ESP_OK,"HTTP response boundary succeeds");
    check(status==expected,"actual handler status matches policy");
    check(calls==before+(expected==200),"rejected request never calls business");
}
static void parse(const char *dir,const char *name,mbedtls_x509_crt *cert) {
    char path[1024]; snprintf(path,sizeof(path),"%s/%s.pem",dir,name);
    mbedtls_x509_crt_init(cert);
    assert(mbedtls_x509_crt_parse_file(cert,path)==0);
}
int main(int argc,char **argv) {
    assert(argc==2);
    const bool allowed[5][5]={
        {true,false,false,false,false},
        {true,true,false,false,false},
        {true,true,true,false,false},
        {true,true,true,true,false},
        {true,true,false,false,true}
    };
    for(int user=0;user<5;++user) for(int required=0;required<5;++required) {
        ts_https_auth_t auth={.authenticated=true,.role=user};
        check(ts_https_role_has_permission(user,required)==allowed[user][required],"5x5 role matrix");
        check(ts_https_check_permission(&auth,required)==allowed[user][required],"5x5 authenticated permission");
        auth.authenticated=false;
        check(ts_https_check_permission(&auth,required)==(required==0),"unauthenticated denies protected roles");
    }
    check(!ts_https_role_has_permission((ts_https_role_t)-1,TS_HTTPS_ROLE_VIEWER),"negative role denied");
    check(!ts_https_role_has_permission((ts_https_role_t)99,TS_HTTPS_ROLE_ANONYMOUS),"unknown role denied");
    check(!ts_https_role_has_permission(TS_HTTPS_ROLE_ADMIN,(ts_https_role_t)99),"unknown requirement denied");
    check(ts_https_check_permission(NULL,TS_HTTPS_ROLE_ANONYMOUS),"public health without auth");
    check(!ts_https_check_permission(NULL,TS_HTTPS_ROLE_VIEWER),"protected without auth");

    mbedtls_x509_crt ca;parse(argv[1],"root",&ca);
    const char *names[]={"anonymous","viewer","operator","admin","device","mixed-case","missing-ou","unknown-ou",
        "duplicate-same","duplicate-mixed","nul-role","long-role"};
    const int roles[]={0,1,2,3,4,3,0,0,0,0,0,0};
    for(unsigned i=0;i<sizeof(names)/sizeof(*names);++i) {
        mbedtls_x509_crt cert;parse(argv[1],names[i],&cert);
        uint32_t flags=0;
        check(mbedtls_x509_crt_verify(&cert,&ca,NULL,NULL,&flags,NULL,NULL)==0 && !flags,"real trusted fixture chain");
        bool invalid=i>=8;
        ts_https_auth_t auth;memset(&auth,0xa5,sizeof(auth));
        esp_err_t ret=ts_https_auth_from_cert(&cert,&auth);
        check(invalid?ret!=ESP_OK:ret==ESP_OK,names[i]);
        check(auth.role==roles[i],"helper role matches full OU");
        check(auth.authenticated==!invalid,"invalid role clears identity");
        if(!invalid) {
            check(!strcmp(auth.username,"fixture")&&!strcmp(auth.organization,"Test Organization"),"CN/O display preserved");
        }
        fixture_cert=&cert;
        for(int required=0;required<5;++required)
            request(required,allowed[roles[i]][required]?200:invalid?401:403);
        /* The public route and helper must see the same parsed identity. */
        request(TS_HTTPS_ROLE_ANONYMOUS,200);
        check(observed.role==auth.role&&observed.authenticated==auth.authenticated,"one identity extractor");
        mbedtls_x509_crt_free(&cert);
    }
    mbedtls_x509_crt admitted_peer;parse(argv[1],"admin",&admitted_peer);fixture_cert=&admitted_peer;
    request(TS_HTTPS_ROLE_ADMIN,200);
    fixture_material.ca_valid=false;request(TS_HTTPS_ROLE_ADMIN,503);
    request(TS_HTTPS_ROLE_ANONYMOUS,200);check(!observed.authenticated&&observed.role==TS_HTTPS_ROLE_ANONYMOUS,"public health cannot inherit identity from an unapproved context");
    fixture_material.ca_valid=true;fixture_material.generation=2;request(TS_HTTPS_ROLE_ADMIN,503);
    request(TS_HTTPS_ROLE_ANONYMOUS,200);check(!observed.authenticated,"stale TLS generation only gets anonymous public health");
    fixture_material.generation=1;fixture_material_error=ESP_FAIL;request(TS_HTTPS_ROLE_ADMIN,503);fixture_material_error=0;
    fixture_material.storage_error=true;request(TS_HTTPS_ROLE_ADMIN,503);fixture_material.storage_error=false;
    fixture_material.time_ready=false;request(TS_HTTPS_ROLE_ADMIN,503);fixture_material.time_ready=true;
    request(TS_HTTPS_ROLE_ADMIN,200);mbedtls_x509_crt_free(&admitted_peer);
    mbedtls_x509_crt_free(&ca);

    fixture_cert=NULL;
    request(TS_HTTPS_ROLE_ANONYMOUS,200);
    for(int role=1;role<5;++role) request(role,401);
    s_config.require_client_cert=false;
    request(TS_HTTPS_ROLE_ANONYMOUS,200); request(TS_HTTPS_ROLE_ADMIN,401);
    s_config.require_client_cert=true;
    ts_https_auth_t auth={.authenticated=true,.role=TS_HTTPS_ROLE_ADMIN};
    check(ts_https_auth_from_cert(NULL,&auth)!=ESP_OK&&!auth.authenticated&&auth.role==0,"NULL cert clears stale admin");
    auth=(ts_https_auth_t){.authenticated=true,.role=TS_HTTPS_ROLE_ADMIN};
    check(ts_https_get_client_auth(NULL,&auth)!=ESP_OK&&!auth.authenticated&&auth.role==0,"NULL SSL clears stale admin");
    mbedtls_ssl_context ssl;mbedtls_ssl_init(&ssl);
    auth=(ts_https_auth_t){.authenticated=true,.role=TS_HTTPS_ROLE_ADMIN};
    check(ts_https_get_client_auth(&ssl,&auth)!=ESP_OK&&!auth.authenticated&&auth.role==0,"missing peer cert clears stale admin");
    mbedtls_ssl_free(&ssl);
    /* A zero-length role value is tested at the parsed-name boundary. */
    mbedtls_x509_crt empty={0};unsigned char ou[]={0x55,4,11};
    empty.subject.oid.p=ou;empty.subject.oid.len=sizeof(ou);
    check(ts_https_auth_from_cert(&empty,&auth)!=ESP_OK&&!auth.authenticated,"empty role denied");
    printf("%u checks, %u failures: real mbedTLS PEM/chain, role matrix and actual generic_handler; no TLS sockets\n",checks,failures);
    return failures?1:0;
}
