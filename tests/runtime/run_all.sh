#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
source tests/runtime/host_env.sh
export DEVELOPER_DIR="${DEVELOPER_DIR:-/Library/Developer/CommandLineTools}"
for suite in log codec ssh service_watch; do
    bash "tests/runtime/run_${suite}.sh"
done
python3 tests/runtime/test_sdk_diagnostic_privacy.py
bash tests/runtime/run_storage_paths.sh
python3 tests/runtime/test_external_storage_writes.py
python3 tests/runtime/test_external_storage_reads.py
python3 tests/runtime/test_led_file_reads.py
bash tests/runtime/run_rule_pack.sh
python3 tests/runtime/test_completion.py
python3 tests/runtime/test_service_control.py
python3 tests/runtime/test_sensitive_auth.py
python3 tests/runtime/test_request_params.py
python3 tests/runtime/test_api_access.py
for case_name in delivery privacy; do
    python3 tests/runtime/test_log_delivery.py --case "$case_name"
done
python3 tests/runtime/test_complete_api_inventory.py
python3 tests/runtime/test_default_deny_policy.py
python3 tests/runtime/test_api_diagnostic_privacy.py
python3 tests/runtime/test_exec_delivery.py
python3 tests/runtime/test_api_context.py
python3 tests/runtime/test_config_privacy.py
python3 tests/runtime/test_cert_admission.py
python3 tests/runtime/test_auth_persistence.py
python3 tests/runtime/test_auth_persistence.py --case tokens
python3 tests/runtime/test_auth_persistence.py --case session_race
python3 tests/runtime/test_auth_persistence.py --case revocation
python3 tests/runtime/test_auth_persistence.py --case lockout
python3 tests/runtime/test_api_numeric_security.py
python3 tests/runtime/test_unimplemented_results.py
python3 tests/runtime/test_port_forward.py
python3 tests/runtime/test_compute_monitor.py
python3 tests/runtime/test_http_file_stream.py
python3 tests/runtime/test_receive_budget.py
python3 tests/runtime/test_json_depth.py
python3 tests/runtime/test_proxy_ws_samples.py
python3 tests/runtime/test_ws_sdk_lifecycle.py
python3 tests/runtime/test_ws_diagnostic_transport.py
python3 tests/runtime/test_network_diagnostics.py
python3 tests/runtime/test_api_prebody.py
python3 tests/runtime/test_http_body_integrity.py
python3 tests/runtime/test_browser_origin.py
python3 tests/runtime/test_sftp_list.py
python3 tests/runtime/test_cli_option_contract.py
python3 tests/runtime/test_system_memory.py
python3 tests/runtime/test_system_reboot.py
python3 tests/runtime/test_registry_tables.py
python3 tests/runtime/test_console_lifecycle.py
python3 tests/runtime/test_power_lifecycle.py
python3 tests/runtime/test_agx_reset.py
python3 tests/runtime/test_dns_lifecycle.py
python3 tests/runtime/test_http_transport_polling.py
python3 tests/runtime/test_http_client_privacy.py
python3 tests/runtime/test_http_fetch.py
python3 tests/runtime/test_https_startup.py
python3 tests/runtime/test_pki_http.py
python3 tests/runtime/test_ota_upload_handlers.py
python3 tests/runtime/test_ota_lifecycle.py
python3 tests/runtime/test_source_sampling.py
python3 tests/runtime/test_dhcp_bindings.py
python3 tests/runtime/test_led_lifetimes.py
python3 tests/runtime/test_led_controls.py
python3 tests/runtime/test_led_image_bounds.py
python3 tests/runtime/test_power_settings.py
python3 tests/runtime/test_power_chip_crc.py
python3 tests/runtime/test_ui_widgets.py
python3 tests/runtime/test_scp_recv.py
python3 tests/runtime/test_condition_roundtrip.py
python3 tests/runtime/test_expansion.py
python3 tests/runtime/test_ssh_copyid.py
python3 tests/runtime/test_ssh_cli_controls.py
python3 tests/runtime/test_ssh_fingerprint_wire.py
python3 tests/runtime/test_failure_wire.py
python3 tests/runtime/test_copyid_quote.py
python3 tests/runtime/test_lpmu_access.py
python3 tests/runtime/test_device_names.py
python3 tests/runtime/test_input_repair.py
python3 tests/runtime/test_csr_input.py
python3 tests/runtime/test_keystore.py
python3 tests/runtime/test_probe.py
node --test tests/prompts/regression.test.cjs
for file in app api router terminal lang/en-US lang/zh-CN; do
    node --check "components/ts_webui/web/js/$file.js"
done

python3 tests/http_admission/run_host.py
python3 tests/terminal_jobs/run_host.py
bash tests/terminal_jobs/run_ws.sh
python3 tests/ws_subscriptions/run_input_admission.py
bash tests/exec_results/run_host.sh
node --test tests/http_admission/reconnect.test.cjs
