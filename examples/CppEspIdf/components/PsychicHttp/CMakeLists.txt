# Define the directory containing your source files
set(SOURCE_DIR "../../../../src")

# Use file(GLOB) to find all .cpp files in the 'src' directory
file(GLOB SOURCE_FILES "${SOURCE_DIR}/*.cpp")

idf_component_register(SRCS ${SOURCE_FILES}
                    INCLUDE_DIRS "../../../../src"
                    REQUIRES esp_netif esp_wifi esp_http_client esp_http_server esp_https_server arduinojson) 