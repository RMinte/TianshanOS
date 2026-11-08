#pragma once

/* One OTA event contract for publishers and subscribers, independent of SDK
 * headers and include order. Existing event-bus IDs remain unchanged. */
typedef enum {
    TS_EVENT_OTA_PROGRESS_UPDATE = 0x0601,
    TS_EVENT_OTA_PROGRESS = TS_EVENT_OTA_PROGRESS_UPDATE,
    TS_EVENT_OTA_STARTED = 0x0602,
    TS_EVENT_OTA_FINISHED = 0x0603,
    TS_EVENT_OTA_COMPLETED = TS_EVENT_OTA_FINISHED,
    TS_EVENT_OTA_FAILED = 0x0604,
    TS_EVENT_OTA_ABORTED = 0x0605,
    TS_EVENT_OTA_PENDING_REBOOT = 0x0606,
    TS_EVENT_OTA_ROLLBACK_PENDING = 0x0607,
    TS_EVENT_OTA_ROLLBACK_EXECUTED = 0x0608,
    TS_EVENT_OTA_VALIDATED = 0x0609
} ts_event_ota_id_t;
