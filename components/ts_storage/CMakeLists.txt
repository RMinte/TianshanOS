# TianShanOS Storage Component

set(TS_STORAGE_SRCS
    "src/ts_storage.c"
    "src/ts_storage_spiffs.c"
    "src/ts_storage_sd.c"
)

idf_component_register(
    SRCS ${TS_STORAGE_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ts_core fatfs spiffs vfs
    PRIV_REQUIRES driver sdmmc
)
