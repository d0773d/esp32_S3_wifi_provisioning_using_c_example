# ESP32-S3 WiFi Provisioning over Secure BLE

This project implements WiFi provisioning for ESP32-S3 using a secure, bonded BLE connection. The device receives WiFi credentials via JSON over BLE, connects to the network, and saves the credentials to NVS upon successful connection. Real-time status updates are sent to the provisioning app via BLE notifications.

## Features

- ✅ **Secure BLE Communication**: Uses BLE bonding/pairing with encryption (MITM protection)
- ✅ **JSON-based Credentials**: Receives WiFi credentials in JSON format: `{"ssid":"network","password":"pass"}`
- ✅ **Real-time Notifications**: Sends provisioning status updates to mobile app via BLE notifications
- ✅ **NVS Storage**: Saves WiFi credentials to Non-Volatile Storage after successful connection
- ✅ **Auto-reconnect**: Automatically connects to stored WiFi on subsequent boots
- ✅ **Comprehensive Error Handling**: Detailed error codes for authentication failures, timeouts, etc.
- ✅ **State Machine**: Tracks provisioning progress through well-defined states
- ✅ **Power Efficient**: Disables BLE after successful provisioning

## Architecture

### Components

1. **BLE Provisioning** (`ble_provisioning.c/h`)
   - GATT server with custom service
   - WiFi credentials characteristic (write)
   - Provisioning status characteristic (notify)
   - Secure bonding implementation

2. **WiFi Manager** (`wifi_manager.c/h`)
   - WiFi connection handling
   - Event-driven architecture
   - NVS credential storage
   - Connection retry logic

3. **Provisioning State Machine** (`provisioning_state.c/h`)
   - State tracking and transitions
   - Status code definitions
   - Callback system for state changes

4. **Main Application** (`main.c`)
   - System initialization
   - Component orchestration
   - Stored credential handling

### BLE Service Structure

**Service UUID**: `00467768-6228-2272-4663-277478268000`

| Characteristic | UUID | Properties | Description |
|----------------|------|------------|-------------|
| State | `00467768-6228-2272-4663-277478268001` | Read | Current provisioning state |
| WiFi Credentials | `00467768-6228-2272-4663-277478268002` | Write | Receives JSON with SSID and password |
| Status | `00467768-6228-2272-4663-277478268003` | Read, Notify | Sends status updates to app |

### Provisioning States

| State | Description |
|-------|-------------|
| `IDLE` | Waiting for BLE connection |
| `BLE_CONNECTED` | Client connected, waiting for credentials |
| `CREDENTIALS_RECEIVED` | Valid credentials received |
| `WIFI_CONNECTING` | Attempting WiFi connection |
| `WIFI_CONNECTED` | Connected to WiFi, saving credentials |
| `PROVISIONED` | Successfully provisioned |
| `WIFI_FAILED` | WiFi connection failed |
| `ERROR` | Error occurred during provisioning |

### Status Codes

- `SUCCESS`: Operation successful
- `ERROR_INVALID_JSON`: Malformed JSON data
- `ERROR_MISSING_SSID`: SSID field missing
- `ERROR_MISSING_PASSWORD`: Password field missing
- `ERROR_WIFI_TIMEOUT`: Connection timeout
- `ERROR_WIFI_AUTH_FAILED`: Wrong password
- `ERROR_WIFI_NO_AP_FOUND`: SSID not found
- `ERROR_STORAGE_FAILED`: NVS write failed

## Prerequisites

- ESP-IDF v5.0 or later
- ESP32-S3 development board
- USB cable for flashing and monitoring

## Building and Flashing

### 1. Set up ESP-IDF environment

```powershell
# If not already set up, install ESP-IDF and configure the environment
# Follow: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/

# Activate ESP-IDF environment
. $env:IDF_PATH\export.ps1
```

### 2. Configure the project

```powershell
cd c:\Code\esp32_S3
idf.py set-target esp32s3
idf.py menuconfig
```

**Recommended menuconfig settings:**
- Component config → Bluetooth → Enable Bluetooth
- Component config → Bluetooth → Bluedroid Enable
- Component config → Bluetooth → Enable BLE only
- Component config → Bluetooth → Enable secure connections

### 3. Build the project

```powershell
idf.py build
```

### 4. Flash to ESP32-S3

```powershell
idf.py -p COM3 flash monitor
```

Replace `COM3` with your actual serial port.

## Optional Setup

### PowerShell Profile for Simplified `idf.py` Usage (Windows)

By default, you need to run `. $env:IDF_PATH\export.ps1` or the full export command before using `idf.py`. To avoid this, you can add a PowerShell function to your profile:

#### Setup Steps:

1. **Check if profile exists:**
   ```powershell
   Test-Path $PROFILE
   ```

2. **Create profile if it doesn't exist:**
   ```powershell
   New-Item -Path $PROFILE -Type File -Force
   ```

3. **Add the idf.py function:**
   ```powershell
   Add-Content -Path $PROFILE -Value "`n# ESP-IDF helper function`nfunction idf.py {`n    & C:\Espressif\frameworks\esp-idf-v5.5.1-2\export.ps1 | Out-Null`n    & python C:\Espressif\frameworks\esp-idf-v5.5.1-2\tools\idf.py @args`n}"
   ```
   
   *Note: Adjust the ESP-IDF path if your installation is in a different location.*

4. **Reload your profile:**
   ```powershell
   . $PROFILE
   ```

#### Benefits:
- ✅ Use `idf.py` directly from any PowerShell window
- ✅ No need to manually run export.ps1 each time
- ✅ Automatically loads ESP-IDF environment when needed
- ✅ Works in any directory

#### Usage After Setup:
```powershell
# Now you can use idf.py directly:
idf.py build
idf.py -p COM3 flash monitor
idf.py menuconfig
```

## Usage

### First Boot (No Stored Credentials)

1. Device starts and initializes BLE provisioning
2. BLE advertising begins with device name: `ESP32_WiFi_Prov`
3. Connect to the device from your provisioning app
4. Device will request bonding/pairing (Just Works method)
5. Send WiFi credentials via JSON to the WiFi Credentials characteristic:

```json
{
  "ssid": "YourNetworkName",
  "password": "YourPassword"
}
```

6. Monitor status updates via notifications on the Provisioning Status characteristic
7. Upon successful connection, credentials are saved to NVS
8. BLE provisioning service stops automatically

### Subsequent Boots (With Stored Credentials)

1. Device reads stored credentials from NVS
2. Automatically connects to the stored WiFi network
3. BLE provisioning is NOT started (power saving)
4. If connection fails, BLE provisioning starts as fallback

## Status Notification Format

The app receives JSON notifications with the following structure:

```json
{
  "state": "WIFI_CONNECTED",
  "status": "SUCCESS",
  "message": "192.168.1.100",
  "timestamp": 12345
}
```

## Mobile App Development Guide

### BLE Connection Flow

1. **Scan** for devices advertising the provisioning service
2. **Connect** to the ESP32 device
3. **Pair/Bond** - Accept the pairing request
4. **Discover** services and characteristics
5. **Subscribe** to notifications on the Status characteristic (`00467768-6228-2272-4663-277478268003`)
6. **Write** JSON credentials to the WiFi Credentials characteristic (`00467768-6228-2272-4663-277478268002`)
7. **Monitor** status notifications until `PROVISIONED` or `ERROR` state

### Example Mobile App Code (Kotlin)

```kotlin
// UUIDs matching your app
val WIFI_SERVICE_UUID = UUID.fromString("00467768-6228-2272-4663-277478268000")
val STATE_CHAR_UUID = UUID.fromString("00467768-6228-2272-4663-277478268001")
val WIFICREDS_CHAR_UUID = UUID.fromString("00467768-6228-2272-4663-277478268002")
val STATUS_CHAR_UUID = UUID.fromString("00467768-6228-2272-4663-277478268003")

// 1. Connect and bond
device.connect()
device.createBond()

// 2. Discover services
val service = device.getService(WIFI_SERVICE_UUID)
val wifiCredChar = service.getCharacteristic(WIFICREDS_CHAR_UUID)
val statusChar = service.getCharacteristic(STATUS_CHAR_UUID)

// 3. Subscribe to notifications
statusChar.setNotificationEnabled(true)
statusChar.onNotificationReceived { data ->
    val status = JSONObject(String(data))
    Log.d("Provision", "Status: ${status.getString("state")} - ${status.getString("message")}")
    
    when (status.getString("state")) {
        "PROVISIONED" -> {
            Log.i("Provision", "Success! Device IP: ${status.getString("message")}")
            device.disconnect()
        }
        "ERROR", "WIFI_FAILED" -> {
            Log.e("Provision", "Error: ${status.getString("status")} - ${status.getString("message")}")
        }
    }
}

// 4. Send credentials
val credentials = JSONObject().apply {
    put("ssid", "MyNetwork")
    put("password", "MyPassword123")
}

wifiCredChar.write(credentials.toString().toByteArray())
```

## Security Considerations

### Current Implementation
- **BLE Bonding**: Enabled with MITM protection
- **Pairing Method**: Just Works (no PIN required)
- **Encryption**: All BLE communication encrypted after pairing
- **NVS Encryption**: Enabled in sdkconfig (optional)

### Production Recommendations

1. **✅ 128-bit UUIDs**: Already implemented - matches your Kotlin app UUIDs
2. **Passkey Entry**: Change `ESP_IO_CAP_NONE` to `ESP_IO_CAP_OUT` and display a PIN
3. **Certificate Pinning**: Validate mobile app certificate before accepting credentials
4. **Rate Limiting**: Add delays between failed connection attempts
5. **Secure Storage**: Enable NVS encryption with unique keys per device
6. **OTA Updates**: Implement secure firmware updates
7. **Timeout**: Add timeout for provisioning mode

## Clearing Stored Credentials

To reset the device and clear stored WiFi credentials:

```c
// Call this function from your code
wifi_manager_clear_credentials();
```

Or erase NVS flash completely:

```powershell
idf.py -p COM3 erase-flash
```

## Troubleshooting

### Device not advertising
- Check BLE is enabled in menuconfig
- Verify `idf.py set-target esp32s3` was run
- Check logs for initialization errors

### Cannot pair/bond
- Ensure mobile app supports BLE bonding
- Check device has enough memory
- Try erasing flash and reflashing

### WiFi connection fails
- Verify SSID and password are correct
- Check WiFi network is 2.4GHz (ESP32 doesn't support 5GHz)
- Ensure WPA2-PSK authentication
- Check distance to access point

### Not receiving notifications
- Ensure characteristic notifications are subscribed
- Verify bonding completed successfully
- Check MTU size (default 517 bytes)

## Logging

The project uses ESP-IDF logging with the following tags:
- `MAIN`: Main application
- `BLE_PROV`: BLE provisioning
- `WIFI_MGR`: WiFi manager
- `PROV_STATE`: State machine

Set log level in menuconfig or via code:
```c
esp_log_level_set("BLE_PROV", ESP_LOG_DEBUG);
```

## Power Consumption

- **BLE Advertising**: ~50-80mA
- **BLE Connected**: ~60-100mA  
- **WiFi Connected**: ~80-150mA
- **Deep Sleep** (not implemented): ~10μA

After provisioning, BLE is disabled to save power.

## Future Enhancements

- [ ] Add support for WPA3 networks
- [ ] Implement AP mode fallback provisioning
- [ ] Add device reset button (GPIO long press)
- [ ] Support multiple WiFi credential storage
- [ ] Add web-based provisioning interface
- [ ] Implement cloud provisioning
- [ ] Add MQTT connectivity check
- [ ] Battery level monitoring and reporting

## License

This project is provided as-is for educational and commercial use.

## References

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
- [ESP32 BLE Examples](https://github.com/espressif/esp-idf/tree/master/examples/bluetooth)
- [WiFi Provisioning Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/provisioning/provisioning.html)

---

**Project**: ESP32-S3 WiFi BLE Provisioning  
**Version**: 1.0.0  
**Date**: November 2025
