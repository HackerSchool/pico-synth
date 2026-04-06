if(NOT DEFINED UF2_PATH)
    message(FATAL_ERROR "UF2_PATH is required")
endif()

if(NOT DEFINED BIN_PATH)
    message(FATAL_ERROR "BIN_PATH is required")
endif()

if(NOT DEFINED FLASH_SIZE_BYTES)
    message(FATAL_ERROR "FLASH_SIZE_BYTES is required")
endif()

if(NOT EXISTS "${UF2_PATH}")
    message(FATAL_ERROR "UF2 file not found: ${UF2_PATH}")
endif()

if(NOT EXISTS "${BIN_PATH}")
    message(FATAL_ERROR "BIN file not found: ${BIN_PATH}")
endif()

file(SIZE "${UF2_PATH}" UF2_SIZE_BYTES)
file(SIZE "${BIN_PATH}" BIN_SIZE_BYTES)

math(EXPR FLASH_SIZE_KIB "${FLASH_SIZE_BYTES} / 1024")
math(EXPR BIN_SIZE_KIB "(${BIN_SIZE_BYTES} + 1023) / 1024")
math(EXPR UF2_SIZE_KIB "(${UF2_SIZE_BYTES} + 1023) / 1024")

math(EXPR USAGE_PERCENT_X100 "(${BIN_SIZE_BYTES} * 10000) / ${FLASH_SIZE_BYTES}")
math(EXPR USAGE_PERCENT_INT "${USAGE_PERCENT_X100} / 100")
math(EXPR USAGE_PERCENT_FRAC "${USAGE_PERCENT_X100} % 100")
if(USAGE_PERCENT_FRAC LESS 10)
    set(USAGE_PERCENT_FRAC "0${USAGE_PERCENT_FRAC}")
endif()

message("")
message("==== Pico Flash Usage ====")
message("UF2 file size:        ${UF2_SIZE_BYTES} bytes (${UF2_SIZE_KIB} KiB)")
message("Flash image size:     ${BIN_SIZE_BYTES} bytes (${BIN_SIZE_KIB} KiB)")
message("Total Pico flash:     ${FLASH_SIZE_BYTES} bytes (${FLASH_SIZE_KIB} KiB)")
message("Flash usage:          ${USAGE_PERCENT_INT}.${USAGE_PERCENT_FRAC}%")
message("==========================")
message("")
