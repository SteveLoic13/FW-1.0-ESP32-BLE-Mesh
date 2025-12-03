# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# Aumenta stack size per FreeRTOS - DEVE VENIRE DOPO project()
project(sensor_server)

# ============================================================================
# CONFIGURAZIONE STACK SIZE (DOPO project())
# ============================================================================

# Aumenta stack size timer service
set(ESP_TIMER_TASK_STACK_SIZE 4096)

# Aumenta stack depth FreeRTOS
set(FREERTOS_TIMER_TASK_STACK_DEPTH 2048)

# Configurazione ottimizzazioni performance
set(COMPILER_OPTIMIZATION "-Os")

#target_compile_definitions(${COMPONENT_LIB} PUBLIC "CONFIG_ECO_SLAVE=1")

# ============================================================================
# CONFIGURAZIONE COMPONENTI (OPZIONALE)
# ============================================================================

# Se hai componenti personalizzati, aggiungili qui
# list(APPEND EXTRA_COMPONENT_DIRS components/ecolumiere_app)