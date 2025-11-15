# BLE UUID Mapping

## Service and Characteristics

This document maps the UUIDs used in the Kotlin provisioning app to the ESP32 implementation.

### Service UUID
```
00467768-6228-2272-4663-277478268000
```
- **Purpose**: WiFi Provisioning Service
- **ESP32**: `wifi_service_uuid`
- **Kotlin**: `WIFI_SERVICE_UUID`

### Characteristic UUIDs

#### 1. State Characteristic
```
00467768-6228-2272-4663-277478268001
```
- **Properties**: Read
- **Purpose**: Read current provisioning state
- **ESP32**: `state_char_uuid` → `IDX_STATE_VAL`
- **Kotlin**: `STATE_CHAR_UUID`
- **Value**: String representing current state (e.g., "IDLE", "BLE_CONNECTED", etc.)

#### 2. WiFi Credentials Characteristic
```
00467768-6228-2272-4663-277478268002
```
- **Properties**: Write
- **Purpose**: Receive WiFi credentials from mobile app
- **ESP32**: `wifi_creds_char_uuid` → `IDX_WIFI_CRED_VAL`
- **Kotlin**: `WIFICREDS_CHAR_UUID`
- **Value Format**: JSON string
  ```json
  {
    "ssid": "NetworkName",
    "password": "NetworkPassword"
  }
  ```

#### 3. Status Characteristic
```
00467768-6228-2272-4663-277478268003
```
- **Properties**: Read, Notify
- **Purpose**: Send real-time status updates to mobile app
- **ESP32**: `status_char_uuid` → `IDX_STATUS_VAL`
- **Kotlin**: `STATUS_CHAR_UUID`
- **Value Format**: JSON string
  ```json
  {
    "state": "WIFI_CONNECTING",
    "status": "SUCCESS",
    "message": "Connecting... (attempt 1/5)",
    "timestamp": 12345
  }
  ```

### Client Characteristic Configuration Descriptor (CCCD)
```
00002902-0000-1000-8000-00805f9b34fb
```
- **Purpose**: Enable/disable notifications on Status characteristic
- **ESP32**: `character_client_config_uuid` → `IDX_STATUS_CFG`
- **Kotlin**: `CLIENT_CHARACTERISTIC_CONFIG`
- **Standard**: Bluetooth SIG defined UUID

## UUID Byte Order

ESP32 uses little-endian byte order for 128-bit UUIDs. The UUID:
```
00467768-6228-2272-4663-277478268000
```

Is stored in ESP32 as:
```c
{0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
 0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00}
```

## Communication Flow

```
Mobile App                          ESP32-S3
    |                                  |
    |--- Scan & Discover Service ----->|
    |<------ Advertisement ------------|
    |                                  |
    |-------- Connect ---------------->|
    |<------ Connected ----------------|
    |                                  |
    |-------- Bond Request ----------->|
    |<------ Bond Success -------------|
    |                                  |
    |-- Discover Characteristics ----->|
    |<------ Characteristics ----------|
    |                                  |
    |-- Enable Notifications --------->|
    |   (Write 0x0001 to CCCD)         |
    |<------ Confirmed ----------------|
    |                                  |
    |-- Write WiFi Credentials ------->|
    |   (JSON to 0x...8002)            |
    |                                  |
    |<-- Notification: CREDENTIALS_RX--|
    |<-- Notification: WIFI_CONNECTING-|
    |<-- Notification: WIFI_CONNECTING-|
    |<-- Notification: WIFI_CONNECTED--|
    |<-- Notification: PROVISIONED ----|
    |                                  |
    |-------- Disconnect ------------->|
```

## Testing with nRF Connect

1. **Scan** for "ESP32_WiFi_Prov"
2. **Connect** and **Bond**
3. **Discover Services** - look for `...8000`
4. **Subscribe** to Status characteristic (`...8003`)
5. **Write** to WiFi Credentials characteristic (`...8002`):
   ```
   7B2273736964223A22546573744E6574222C2270617373776F7264223A227061737331323
   3227D
   ```
   (Hex for: `{"ssid":"TestNet","password":"pass123"}`)
6. **Monitor** notifications on Status characteristic

## Kotlin Code Reference

```kotlin
val WIFI_SERVICE_UUID: UUID = UUID.fromString("00467768-6228-2272-4663-277478268000")
val STATE_CHAR_UUID: UUID = UUID.fromString("00467768-6228-2272-4663-277478268001")
val WIFICREDS_CHAR_UUID: UUID = UUID.fromString("00467768-6228-2272-4663-277478268002")
val STATUS_CHAR_UUID: UUID = UUID.fromString("00467768-6228-2272-4663-277478268003")
val CLIENT_CHARACTERISTIC_CONFIG: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
```

## ESP32 Code Reference

```c
static const uint8_t wifi_service_uuid[16] = {
    0x00, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};

static const uint8_t state_char_uuid[16] = {
    0x01, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};

static const uint8_t wifi_creds_char_uuid[16] = {
    0x02, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};

static const uint8_t status_char_uuid[16] = {
    0x03, 0x80, 0x26, 0x78, 0x74, 0x27, 0x63, 0x46,
    0x72, 0x22, 0x28, 0x62, 0x68, 0x77, 0x46, 0x00
};
```
