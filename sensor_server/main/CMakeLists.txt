# sensor_server/main/CMakeLists.txt - MODIFICATO
set(srcs "main.c"
        "board.c")


#AGGIUNGI TUTTI I FILE ECOLOGIERE
list(APPEND srcs
        "../ecolumiere/ecolumiere.c"
        "../ecolumiere/luxmeter.c"
        "../ecolumiere/pwmcontroller.c"
        "../ecolumiere/zerocross.c"
        "../ecolumiere/lightcode.c"
        "../ecolumiere/storage.c"
        "../ecolumiere/datarecorder.c"
        "../ecolumiere/slave_role.c"
        "../ecolumiere/scheduler.c"
        "../ecolumiere/ecolumiere_system.c"
        "../ble_mesh_ecolumiere/ble_mesh_ecolumiere.c"
)

idf_component_register(SRCS "${srcs}"
        INCLUDE_DIRS "."
        PRIV_INCLUDE_DIRS "../ecolumiere" "../ble_mesh_ecolumiere"
        REQUIRES
        nvs_flash
        esp_driver_gpio
        driver
        esp_adc
        esp_timer
)
