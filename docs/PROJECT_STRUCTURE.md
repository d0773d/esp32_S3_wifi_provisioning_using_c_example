# Project Structure Guide

This document explains the organization of the ESP32-S3 WiFi BLE Provisioning project.

## Directory Layout

```
esp32_S3/
├── README.md                   # Quick start guide and project overview
├── CMakeLists.txt              # Root CMake configuration
├── sdkconfig                   # Generated ESP-IDF configuration (gitignored)
│
├── main/                       # Application source code
│   ├── CMakeLists.txt         # Main component build configuration
│   ├── main.c                 # Application entry point and orchestration
│   ├── ble_provisioning.c     # BLE GATT server implementation
│   ├── ble_provisioning.h     # BLE provisioning public API
│   ├── wifi_manager.c         # WiFi connection and NVS storage
│   ├── wifi_manager.h         # WiFi manager public API
│   ├── provisioning_state.c   # State machine implementation
│   └── provisioning_state.h   # State machine definitions
│
├── config/                     # Configuration files
│   ├── sdkconfig.defaults     # Default ESP-IDF configuration
│   └── partitions.csv         # Flash partition table
│
├── docs/                       # Documentation
│   ├── README.md              # Complete project documentation
│   ├── KOTLIN_INTEGRATION.md  # Android/Kotlin integration guide
│   ├── UUID_MAPPING.md        # BLE UUID reference
│   └── PROJECT_STRUCTURE.md   # This file
│
├── test/                       # Test files and examples
│   ├── test_credentials.json  # Sample credentials for testing
│   └── test_credentials_examples.txt
│
├── build/                      # Build artifacts (generated, gitignored)
│   ├── wifi_ble_provisioning.bin
│   ├── wifi_ble_provisioning.elf
│   ├── wifi_ble_provisioning.map
│   ├── bootloader/
│   └── partition_table/
│
└── .git/                       # Git version control

```

## Source Code Organization

### Main Component (`main/`)

#### `main.c` (170 lines)
**Purpose**: Application entry point and system orchestration

**Responsibilities**:
- Initialize NVS flash storage
- Initialize state machine
- Check for stored WiFi credentials
- Start BLE provisioning if needed
- Handle auto-reconnect with stored credentials
- Coordinate between BLE and WiFi components

**Key Functions**:
- `app_main()` - Entry point
- `state_change_handler()` - Callback for state machine events

---

#### `ble_provisioning.c/h` (645 lines)
**Purpose**: Bluetooth Low Energy GATT server

**Responsibilities**:
- BLE initialization and advertising
- GATT service and characteristic management
- Secure bonding/pairing (LE Secure Connections)
- WiFi credential reception via JSON
- Status notification to mobile app
- MTU negotiation (up to 517 bytes)
- Multiple write methods (single, fragmented, long write)

**Key Functions**:
- `ble_provisioning_init()` - Initialize BLE stack
- `ble_provisioning_start_advertising()` - Start advertising
- `ble_provisioning_send_status()` - Send status notification
- `handle_wifi_credentials()` - Process received credentials
- `gap_event_handler()` - Handle BLE GAP events
- `gatts_event_handler()` - Handle GATT server events

**Security**:
- All characteristics use encrypted read/write permissions
- Bonding required before credential exchange
- MITM protection enabled
- 128-bit custom UUIDs

---

#### `wifi_manager.c/h` (338 lines)
**Purpose**: WiFi connection lifecycle management

**Responsibilities**:
- Initialize WiFi subsystem
- Connect to WiFi networks
- Handle connection events (connect, disconnect, IP assignment)
- Retry logic (5 attempts with exponential backoff)
- Save credentials to NVS
- Retrieve stored credentials
- Error mapping and reporting

**Key Functions**:
- `wifi_manager_init()` - Initialize WiFi
- `wifi_manager_connect()` - Connect to network
- `wifi_manager_get_stored_credentials()` - Retrieve from NVS
- `wifi_manager_clear_credentials()` - Factory reset
- `wifi_event_handler()` - Event-driven WiFi handling
- `save_credentials_to_nvs()` - Persist credentials

**NVS Storage**:
- Namespace: `wifi_config`
- Keys: `ssid`, `password`, `provisioned`

---

#### `provisioning_state.c/h` (127 lines)
**Purpose**: Provisioning state machine

**States** (8):
- `PROV_STATE_IDLE` - Waiting for BLE connection
- `PROV_STATE_BLE_CONNECTED` - Client connected
- `PROV_STATE_CREDENTIALS_RECEIVED` - JSON parsed successfully
- `PROV_STATE_WIFI_CONNECTING` - Attempting WiFi connection
- `PROV_STATE_WIFI_CONNECTED` - Connected to WiFi
- `PROV_STATE_PROVISIONED` - Credentials saved, provisioning complete
- `PROV_STATE_WIFI_FAILED` - Connection failed after retries
- `PROV_STATE_ERROR` - Error during provisioning

**Status Codes** (8):
- `STATUS_SUCCESS`
- `STATUS_ERROR_INVALID_JSON`
- `STATUS_ERROR_MISSING_SSID`
- `STATUS_ERROR_MISSING_PASSWORD`
- `STATUS_ERROR_WIFI_TIMEOUT`
- `STATUS_ERROR_WIFI_AUTH_FAILED`
- `STATUS_ERROR_WIFI_NO_AP_FOUND`
- `STATUS_ERROR_STORAGE_FAILED`

**Key Functions**:
- `provisioning_state_init()` - Initialize state machine
- `provisioning_state_set()` - Transition to new state
- `provisioning_state_get()` - Get current state
- `provisioning_state_register_callback()` - Register state change callback
- `provisioning_state_to_string()` - State to string conversion
- `provisioning_status_to_string()` - Status code to string conversion

---

## Configuration Files (`config/`)

### `sdkconfig.defaults`
Default ESP-IDF configuration settings:

```makefile
# Bluetooth configuration
CONFIG_BT_ENABLED=y
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y

# WiFi configuration
CONFIG_ESP_WIFI_AUTH_WPA2_PSK=y

# Flash size
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y

# Partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="config/partitions.csv"

# Log levels
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_VERBOSE=y

# ESP32-S3 specific
CONFIG_IDF_TARGET="esp32s3"
```

### `partitions.csv`
Flash memory partition layout:

```csv
# Name,      Type,    SubType, Offset,  Size,    Flags
nvs,         data,    nvs,     0x9000,  0x6000,  
phy_init,    data,    phy,     0xf000,  0x1000,  
factory,     app,     factory, 0x10000, 0x300000,
```

**Partitions**:
- **nvs** (24 KB): Non-Volatile Storage for WiFi credentials
- **phy_init** (4 KB): RF calibration data
- **factory** (3 MB): Application firmware

---

## Documentation (`docs/`)

### `README.md`
Complete project documentation including:
- Features overview
- Architecture explanation
- BLE service structure
- Provisioning states and status codes
- Build and flash instructions
- Usage guide (first boot vs. subsequent boots)
- Mobile app development guide with Kotlin examples
- Security considerations
- Troubleshooting
- Power consumption estimates
- Future enhancements

### `KOTLIN_INTEGRATION.md`
Quick reference for Android developers:
- UUIDs (service and characteristics)
- Device name
- JSON format for credentials
- JSON format for status notifications
- Complete provisioning flow example in Kotlin
- State/status code mapping table
- Testing checklist
- Troubleshooting specific to mobile integration

### `UUID_MAPPING.md`
BLE UUID documentation:
- Service UUID
- Characteristic UUIDs (State, WiFiCreds, Status)
- CCCD UUID
- Byte order explanation (little-endian)
- Communication flow diagram
- nRF Connect testing instructions
- Code references for both Kotlin and ESP32

### `PROJECT_STRUCTURE.md`
This document - explains file organization and responsibilities.

---

## Test Files (`test/`)

### `test_credentials.json`
Sample WiFi credentials for testing:
```json
{
  "ssid": "MyHomeNetwork",
  "password": "SecurePassword123"
}
```

### `test_credentials_examples.txt`
Multiple credential format examples:
- Standard format with whitespace
- Minimal compact format
- Format with special characters in SSID/password

**Note**: These are for development only. Never commit real credentials.

---

## Build Output (`build/`)

Generated by ESP-IDF build system. **Not tracked in Git.**

Key files:
- `wifi_ble_provisioning.bin` - Application binary
- `wifi_ble_provisioning.elf` - Executable with debug symbols
- `wifi_ble_provisioning.map` - Memory map
- `bootloader/bootloader.bin` - Bootloader binary
- `partition_table/partition-table.bin` - Partition table binary
- `compile_commands.json` - For IDE integration

---

## Development Workflow

### Building
```powershell
cd c:\Code\esp32_S3
idf.py build
```

### Flashing
```powershell
idf.py -p COM3 flash
```

### Monitoring
```powershell
idf.py -p COM3 monitor
```

### Clean Build
```powershell
idf.py fullclean
idf.py build
```

### Configuration
```powershell
idf.py menuconfig
```

---

## Dependencies

### ESP-IDF Components
- **nvs_flash** - Non-Volatile Storage
- **bt** - Bluetooth stack (Bluedroid)
- **esp_wifi** - WiFi driver
- **json** - cJSON parser
- **esp_timer** - High-resolution timers
- **freertos** - Real-Time Operating System
- **esp_netif** - Network interface abstraction
- **esp_event** - Event loop system

### External Dependencies
None - all dependencies are provided by ESP-IDF.

---

## Code Metrics

| File | Lines | Purpose |
|------|-------|---------|
| `main/ble_provisioning.c` | 645 | BLE GATT server |
| `main/wifi_manager.c` | 338 | WiFi + NVS |
| `main/main.c` | 170 | Application entry |
| `main/provisioning_state.c` | 98 | State machine |
| **Total Source Code** | **~1,250** | |
| | | |
| `docs/README.md` | 323 | Main documentation |
| `docs/KOTLIN_INTEGRATION.md` | 201 | Integration guide |
| `docs/UUID_MAPPING.md` | 93 | BLE reference |
| **Total Documentation** | **~617** | |

---

## Git Repository

**Repository**: `esp32_S3_wifi_provisioning_using_c_example`  
**Owner**: `d0773d`  
**Branch**: `main`

### `.gitignore`
Ignores:
- `build/` - Build artifacts
- `sdkconfig`, `sdkconfig.old` - Generated config
- `*.bin`, `*.elf`, `*.map` - Binary files
- `dependencies.lock`, `managed_components/` - Dependencies
- IDE files (`.vscode/`, `.idea/`)
- OS files (`.DS_Store`, `Thumbs.db`)
- `credentials.json`, `secrets.h` - Never commit real credentials

---

## Version Information

**Project Version**: 1.0.0  
**ESP-IDF Version**: 5.5.1-2  
**Target**: ESP32-S3  
**Date**: November 2025

---

## Quick Reference

### Directory Purposes

| Directory | Purpose | Tracked in Git |
|-----------|---------|----------------|
| `main/` | Application source code | ✅ Yes |
| `config/` | Configuration files | ✅ Yes |
| `docs/` | Documentation | ✅ Yes |
| `test/` | Test files and examples | ✅ Yes |
| `build/` | Build artifacts | ❌ No |

### File Extensions

- `.c` - C source code
- `.h` - C header files
- `.md` - Markdown documentation
- `.csv` - Partition table
- `.json` - Test data
- `.txt` - Plain text documentation

### Key Concepts

- **NVS**: Non-Volatile Storage - Flash memory storage for persistent data
- **GATT**: Generic Attribute Profile - BLE service/characteristic structure
- **MTU**: Maximum Transmission Unit - Maximum packet size (517 bytes)
- **CCCD**: Client Characteristic Configuration Descriptor - Enable/disable notifications
- **Bonding**: Secure pairing with key storage for reconnection

---

## Related Resources

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [ESP32 BLE Examples](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth)
- [Bluetooth Core Specification](https://www.bluetooth.com/specifications/specs/)

---

Last Updated: November 15, 2025
