cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ws183-os)

# Imagen SPIFFS de la particion "assets" a partir de firmware/assets/.
# Sin FLASH_IN_PROJECT, a proposito: idf.py flash no graba assets, asi un flash
# de desarrollo no pisa lo que haya en la placa. Se graba aparte con
# "idf.py -p COM3 assets-flash", target que crea esta misma llamada.
spiffs_create_partition_image(assets assets)
