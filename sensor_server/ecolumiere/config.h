//
// Created by Admin on 23/10/2025.
//

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// ECOLIUMIERE CONFIGURATION - SLAVE ONLY ARCHITECTURE
// ============================================================================

/**
 * @brief CONFIGURAZIONE RUOLO DISPOSITIVO
 * 🎯 ARCHITETTURA SLAVE-ONLY - Tutti i nodi sono SLAVE, il Gateway è MASTER
 */

#define CONFIG_ECO_SLAVE 1     // ✅ Tutti i dispositivi sono SLAVE

// ============================================================================
// BLE MESH SETTINGS
// ============================================================================

/**
 * @brief Configurazione BLE Mesh
 */

#define BLE_MESH_NODE 1        // ✅ Questo dispositivo è un nodo Mesh
// #define BLE_MESH_GATEWAY 1   // ❌ Non usare - il Gateway è separato

// ============================================================================
// DEBUG SETTINGS (Opzionali)
// ============================================================================

/**
 * @brief DEBUG SETTINGS - Decommenta per abilitare
 */

// #define CONFIG_ECO_DEBUG 1     // 🐛 Debug dettagliato per modulo Ecolumiere
// #define CONFIG_PWM_DEBUG 1     // 🎛️ Debug per PWM controller
// #define CONFIG_LUX_DEBUG 1     // 💡 Debug per luxmeter
// #define CONFIG_MESH_DEBUG 1    // 📡 Debug per BLE Mesh

// ============================================================================
// HARDWARE SETTINGS
// ============================================================================

/**
 * @brief CONFIGURAZIONE HARDWARE - Modifica solo se necessario
 */

// #define PWM_OUT_PIN                 16      // GPIO per uscita PWM
// #define LUX_SENSOR_PIN              4       // GPIO per sensore luce
// #define LIGHTCODE_SENSE_PIN         27      // GPIO per lightcode
// #define ZERO_CROSS_PIN              18      // GPIO per zero-cross detection

// ============================================================================
// COSTANTI DI SISTEMA - NON MODIFICARE
// ============================================================================

// ✅ RUOLO FISSO - SEMPRE SLAVE
#define ECO_ROLE_SLAVE

// Timing constants (identiche al Nordic)
#define SLOT_COUNT            10
#define LIGHT_MAX_LEVEL       32
//#define SLOT_TIME_MS          20     // 20ms come Nordic originale

// BLE Mesh constants
#define MESH_NODE_NAME_PREFIX "ECL_SLAVE"
#define MESH_PROV_TIMEOUT_MS  30000  // 30 secondi per provisioning

// ============================================================================
// VERIFICA CONFIGURAZIONE
// ============================================================================

#ifndef CONFIG_ECO_SLAVE
#error "CONFIG_ECO_SLAVE must be defined - This firmware is SLAVE-only"
#endif

#ifdef CONFIG_ECO_MASTER
#error "CONFIG_ECO_MASTER is not supported in slave-only architecture"
#endif


#endif // CONFIG_H
