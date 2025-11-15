# Quick Reference - Kotlin App Integration

## UUIDs (Already Configured ✅)

```kotlin
val WIFI_SERVICE_UUID: UUID = UUID.fromString("00467768-6228-2272-4663-277478268000")
val STATE_CHAR_UUID: UUID = UUID.fromString("00467768-6228-2272-4663-277478268001")
val WIFICREDS_CHAR_UUID: UUID = UUID.fromString("00467768-6228-2272-4663-277478268002")
val STATUS_CHAR_UUID: UUID = UUID.fromString("00467768-6228-2272-4663-277478268003")
```

## Device Name
**Advertised Name**: `ESP32_WiFi_Prov`

## JSON Format for WiFi Credentials

### Write to WIFICREDS_CHAR_UUID:
```json
{
  "ssid": "YourNetworkName",
  "password": "YourPassword"
}
```

### Example Kotlin:
```kotlin
val credentials = JSONObject().apply {
    put("ssid", ssidEditText.text.toString())
    put("password", passwordEditText.text.toString())
}
wifiCredsCharacteristic.write(credentials.toString().toByteArray(Charsets.UTF_8))
```

## JSON Format for Status Notifications

### Received from STATUS_CHAR_UUID:
```json
{
  "state": "WIFI_CONNECTING",
  "status": "SUCCESS",
  "message": "Connecting... (attempt 1/5)",
  "timestamp": 12345
}
```

### Example Kotlin Handler:
```kotlin
statusCharacteristic.setNotificationEnabled(true)
statusCharacteristic.onNotificationReceived { data ->
    try {
        val statusJson = JSONObject(String(data, Charsets.UTF_8))
        val state = statusJson.getString("state")
        val status = statusJson.getString("status")
        val message = statusJson.getString("message")
        
        when (state) {
            "IDLE" -> updateUI("Waiting for connection...")
            "BLE_CONNECTED" -> updateUI("Connected, ready for credentials")
            "CREDENTIALS_RECEIVED" -> updateUI("Credentials received, validating...")
            "WIFI_CONNECTING" -> updateUI(message)
            "WIFI_CONNECTED" -> updateUI("WiFi connected!")
            "PROVISIONED" -> {
                updateUI("Success! Device IP: $message")
                // Credentials saved, can disconnect
                disconnect()
            }
            "WIFI_FAILED" -> {
                showError("WiFi connection failed: $message")
                // Status codes: AUTH_FAILED, NO_AP_FOUND, TIMEOUT
            }
            "ERROR" -> {
                showError("Error: $message")
            }
        }
    } catch (e: JSONException) {
        Log.e(TAG, "Failed to parse status notification", e)
    }
}
```

## Provisioning States

| State | Description | UI Action |
|-------|-------------|-----------|
| `IDLE` | Waiting for BLE connection | Show "Scanning..." |
| `BLE_CONNECTED` | Client connected | Enable credential input |
| `CREDENTIALS_RECEIVED` | JSON parsed successfully | Show "Validating..." |
| `WIFI_CONNECTING` | Attempting WiFi connection | Show progress with retry count |
| `WIFI_CONNECTED` | Connected to WiFi | Show success message |
| `PROVISIONED` | Credentials saved to NVS | Show device IP, enable disconnect |
| `WIFI_FAILED` | Connection failed after retries | Show error, allow retry |
| `ERROR` | Parsing or validation error | Show error details |

## Status Codes

| Code | Meaning | User Action |
|------|---------|-------------|
| `SUCCESS` | Operation successful | Continue |
| `ERROR_INVALID_JSON` | Malformed JSON | Check app code |
| `ERROR_MISSING_SSID` | No "ssid" field | Verify JSON format |
| `ERROR_MISSING_PASSWORD` | No "password" field | Verify JSON format |
| `ERROR_WIFI_TIMEOUT` | Connection timeout | Check signal strength |
| `ERROR_WIFI_AUTH_FAILED` | Wrong password | Re-enter password |
| `ERROR_WIFI_NO_AP_FOUND` | SSID not found | Check SSID, verify 2.4GHz |
| `ERROR_STORAGE_FAILED` | NVS write failed | Device issue, restart ESP32 |

## Complete Provisioning Flow

```kotlin
fun provisionDevice(ssid: String, password: String) {
    scope.launch {
        try {
            // 1. Scan for device
            val device = scanForDevice("ESP32_WiFi_Prov")
            
            // 2. Connect
            device.connect()
            
            // 3. Bond (required for security)
            device.createBond()
            delay(2000) // Wait for bonding
            
            // 4. Discover services
            val service = device.getService(WIFI_SERVICE_UUID)
                ?: throw Exception("Service not found")
            
            val wifiCredsChar = service.getCharacteristic(WIFICREDS_CHAR_UUID)
                ?: throw Exception("WiFi credentials characteristic not found")
            
            val statusChar = service.getCharacteristic(STATUS_CHAR_UUID)
                ?: throw Exception("Status characteristic not found")
            
            // 5. Subscribe to status notifications
            statusChar.setNotificationEnabled(true)
            statusChar.onNotificationReceived { data ->
                handleStatusNotification(data)
            }
            
            // 6. Send WiFi credentials
            val credentials = JSONObject().apply {
                put("ssid", ssid)
                put("password", password)
            }
            
            wifiCredsChar.write(credentials.toString().toByteArray(Charsets.UTF_8))
            
            // 7. Wait for provisioning to complete
            // (handled by notification callbacks)
            
        } catch (e: Exception) {
            Log.e(TAG, "Provisioning failed", e)
            showError("Provisioning failed: ${e.message}")
        }
    }
}
```

## Security Notes

1. **Bonding Required**: ESP32 will only accept credentials after successful bonding
2. **Encryption**: All communication is encrypted after pairing
3. **Pairing Method**: Currently "Just Works" - no PIN required
4. **One-time Setup**: After successful provisioning, ESP32 stops BLE to save power

## Testing Checklist

- [ ] Scan finds "ESP32_WiFi_Prov"
- [ ] Connection succeeds
- [ ] Bonding completes successfully
- [ ] Service `...8000` discovered
- [ ] All 3 characteristics found (`...8001`, `...8002`, `...8003`)
- [ ] Can enable notifications on `...8003`
- [ ] Can write JSON to `...8002`
- [ ] Receive notifications with valid JSON
- [ ] State progresses: IDLE → BLE_CONNECTED → CREDENTIALS_RECEIVED → WIFI_CONNECTING → PROVISIONED
- [ ] Correct error handling for wrong password
- [ ] Correct error handling for wrong SSID
- [ ] Device reconnects automatically after power cycle

## Troubleshooting

### "Service not found"
- Ensure device is bonded first
- Wait 1-2 seconds after bonding before discovering services
- Verify ESP32 is advertising (check serial monitor)

### "Characteristic not found"
- Service discovery may be incomplete
- Try rediscovering services
- Check ESP32 logs for GATT errors

### "Notifications not received"
- Verify CCCD write succeeded
- Check bonding is complete
- Ensure MTU is sufficient (minimum 23 bytes)

### "WiFi connection always fails"
- Verify network is 2.4GHz (ESP32 doesn't support 5GHz)
- Check password is correct
- Ensure SSID is visible (not hidden network)
- Check signal strength at ESP32 location

### "Device not advertising after provisioning"
- This is expected! Device stops BLE after successful provisioning
- To re-provision, clear credentials or erase flash
