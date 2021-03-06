if(DEFINED ENV{SOAP_ROOT})
    set(SOAP_ROOT "$ENV{SOAP_ROOT}")
    message("-- SOAP_ROOT is set: ${SOAP_ROOT}")
else()
    message(FATAL_ERROR "-- Note: SOAP_ROOT not set.")
endif()
message("-- SOAP_ROOT is set: ${SOAP_ROOT}")
set(SOAP_LIBRARIES "${SOAP_ROOT}/soap/_soapxx.so")
set(SOAP_INCLUDE_DIRS "${SOAP_ROOT}/soap/include")
include_directories(${SOAP_INCLUDE_DIRS})

