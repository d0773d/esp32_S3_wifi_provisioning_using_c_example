# Security Implementation Guide

This document explains the security features implemented in the ESP32-S3 WiFi provisioning project.

## Overview

The project implements **three layers of security**:

1. ✅ **NVS Encryption** (Implemented) - WiFi credentials encrypted at rest
2. ⏳ **Flash Encryption** (Optional) - Entire firmware encrypted
3. ⏳ **Secure Boot V2** (Optional) - Only signed firmware can run

---

## Layer 1: NVS Encryption (IMPLEMENTED)

### What It Does
- **Encrypts WiFi credentials** (SSID + password) stored in flash memory
- Uses **AES-256-XTS** encryption algorithm
- Keys protected by **HMAC-based eFuse** mechanism
- Encryption keys stored in dedicated `nvs_keys` partition

### How It Works

```
┌─────────────────────┐
│ WiFi Credentials    │ Plain text in RAM
│ SSID: "MyWiFi"      │
│ Pass: "MyPassword"  │
└──────────┬──────────┘
           │
           ↓ [security.c: NVS Write]
┌──────────┴──────────┐
│ AES-256-XTS         │
│ Encryption Engine   │
└──────────┬──────────┘
           │
           ↓ [Encrypted Data]
┌──────────┴──────────┐
│ Flash Memory (NVS)  │ Encrypted at rest
│ 0x9a3f8c2e1d...     │
└─────────────────────┘

Encryption Key (256-bit)
     ↓
┌─────────────────────┐
│ nvs_keys Partition  │ Protected by HMAC
│ 0xf000 - 0x10000    │
└─────────────────────┘
     ↓
┌─────────────────────┐
│ eFuse HMAC Key      │ Hardware-protected
│ (Read-protected)    │ One-time programmable
└─────────────────────┘
```

### Configuration

**Partition Table** (`config/partitions.csv`):
```csv
# Name,      Type, SubType,  Offset,  Size
nvs,         data, nvs,      0x9000,  0x6000,   # Encrypted credentials
nvs_key,     data, nvs_keys, 0xf000,  0x1000,   # Encryption keys
factory,     app,  factory,  0x10000, 0x2F0000, # Application
phy_init,    data, phy,      0x300000, 0x1000,  # PHY calibration
```

**SDK Config** (`config/sdkconfig.defaults`):
```makefile
CONFIG_NVS_ENCRYPTION=y
CONFIG_NVS_SEC_KEY_PROTECTION_SCHEME=y
CONFIG_NVS_SEC_KEY_PROTECT_USING_HMAC=y
```

### First Boot Behavior

**First time device powers on:**
1. `security_init()` detects no encryption keys exist
2. Generates random 256-bit AES key
3. Stores key in `nvs_keys` partition
4. HMAC peripheral protects key access
5. Logs: `"NVS encryption keys generated successfully"`

**Subsequent boots:**
1. Reads encryption key from `nvs_keys` partition
2. Initializes NVS with existing key
3. Credentials decrypted automatically on read
4. Logs: `"NVS encryption keys loaded from partition"`

### Security Level

**Protection Against:**
- ✅ Flash dump attacks (data encrypted)
- ✅ Physical memory reading (key HMAC-protected)
- ✅ Casual credential extraction

**Does NOT Protect Against:**
- ❌ Sophisticated attacks with eFuse key extraction
- ❌ Side-channel attacks (power analysis, timing)
- ❌ Debug probe attacks if JTAG enabled

**Security Rating:** **MEDIUM-HIGH** for IoT devices

---

## Layer 2: Flash Encryption (OPTIONAL)

### What It Does
- Encrypts **entire flash memory** (firmware + data)
- Bootloader decrypts code at runtime
- Prevents reverse engineering
- AES-256 encryption

### When to Enable
✅ **Enable if:**
- Device will be deployed in field
- Firmware contains proprietary algorithms
- Compliance requirements mandate encryption
- Risk of firmware cloning/piracy

❌ **Skip if:**
- Development/prototyping phase
- Firmware is open-source
- Need easy debugging access

### How to Enable

**Step 1: Configure**
```bash
idf.py menuconfig
# Navigate to: Security features → Enable flash encryption on boot
# Set: Development mode (for testing) or Release mode (production)
```

**Step 2: Flash**
```bash
idf.py flash
# On first boot, bootloader will:
# 1. Generate encryption key
# 2. Burn key to eFuse (irreversible)
# 3. Encrypt entire flash
# 4. Reboot with encryption enabled
```

**⚠️ WARNING:** 
- Flash encryption is **ONE-TIME** and **IRREVERSIBLE**
- Cannot be disabled without erasing eFuse (requires hardware rework)
- Development mode allows 3 re-flashing attempts
- Release mode is **PERMANENT** - no re-flashing possible

### Testing Flash Encryption

**Before enabling on production devices, test on development board:**
1. Use "Development mode" first
2. Verify device still boots normally
3. Test OTA updates work with encrypted flash
4. Only then enable "Release mode" for production

---

## Layer 3: Secure Boot V2 (OPTIONAL)

### What It Does
- Verifies firmware **digital signature** before executing
- Bootloader checks signature on every boot
- Prevents unauthorized firmware from running
- RSA-3072 or ECDSA signatures

### When to Enable
✅ **Enable if:**
- Device deployed in hostile environment
- Risk of malicious firmware injection
- Compliance requirements (medical, automotive)
- High-value target (security systems)

❌ **Skip if:**
- Development phase
- Low-security application
- Frequent firmware updates needed

### How to Enable

**Step 1: Generate Signing Key**
```bash
# Generate RSA-3072 key (recommended)
espsecure.py generate_signing_key --version 2 secure_boot_signing_key.pem

# CRITICAL: Store this key securely!
# If lost, you cannot update firmware on deployed devices
```

**Step 2: Configure**
```bash
idf.py menuconfig
# Navigate to: Security features → Enable hardware Secure Boot in bootloader
# Select: Secure Boot V2
# Set path to signing key
```

**Step 3: Flash Signed Firmware**
```bash
idf.py build
idf.py flash
# First boot will burn eFuse with public key hash (irreversible)
```

**⚠️ CRITICAL:** 
- **Back up the signing key** - without it, you cannot update firmware
- Secure Boot is **PERMANENT** and **IRREVERSIBLE**
- All future firmware must be signed with same key
- Unsigned firmware will refuse to boot

---

## Combined Security Setup (Production)

### Recommended Production Configuration

**For maximum security, enable all three:**

```
┌───────────────────────────────────────────┐
│          Secure Boot V2                   │
│  (Verifies firmware signature)            │
└────────────────┬──────────────────────────┘
                 ↓
┌────────────────┴──────────────────────────┐
│          Flash Encryption                 │
│  (Encrypts firmware and data)             │
└────────────────┬──────────────────────────┘
                 ↓
┌────────────────┴──────────────────────────┐
│          NVS Encryption                   │
│  (Encrypts WiFi credentials)              │
└───────────────────────────────────────────┘
```

**Setup Order:**
1. **NVS Encryption** ← Already done! ✅
2. **Flash Encryption** ← Optional, enable before production
3. **Secure Boot V2** ← Optional, enable for high-security deployments

---

## Testing NVS Encryption

### Verify Encryption is Working

**Method 1: Check Logs**
```
I (655) SECURITY: =================================
I (660) SECURITY: Initializing Security Features
I (666) SECURITY: =================================
I (671) SECURITY: NVS keys partition found at 0xf000 (size: 4096 bytes)
I (679) SECURITY: Initializing NVS with encryption...
I (685) SECURITY: ✓ NVS encryption keys loaded from partition
I (691) SECURITY: ✓ NVS encryption initialized successfully
I (698) SECURITY: ✓ WiFi credentials will be stored encrypted
I (705) SECURITY: =================================
I (710) SECURITY: Security Status:
I (714) SECURITY:   NVS Encryption: ENABLED ✓
I (719) SECURITY:   Key Protection: HMAC-based (eFuse)
I (725) SECURITY: =================================
```

**Method 2: Dump Flash and Verify**
```bash
# Read NVS partition from flash
esptool.py --port COM3 read_flash 0x9000 0x6000 nvs_dump.bin

# Try to find plaintext credentials (should fail)
strings nvs_dump.bin | grep "YourSSID"
strings nvs_dump.bin | grep "YourPassword"
# If encryption works, you'll see garbage data, not plaintext
```

**Method 3: Use API**
```c
if (security_is_nvs_encrypted()) {
    ESP_LOGI(TAG, "Credentials are stored encrypted ✓");
} else {
    ESP_LOGW(TAG, "Credentials are stored in PLAIN TEXT! ✗");
}
```

### Performance Impact

- **Write Speed**: ~5% slower (encryption overhead)
- **Read Speed**: ~5% slower (decryption overhead)
- **Flash Wear**: Same (no additional writes)
- **RAM Usage**: +4KB for encryption context
- **Code Size**: +15KB for NVS encryption library

**Verdict:** Negligible impact for IoT applications

---

## Key Management

### Development Phase
- Keys auto-generated on first boot
- Stored in `nvs_keys` partition
- Easy to erase and regenerate
- `idf.py erase-flash` clears everything

### Production Phase

**Option 1: Unique Key Per Device (Recommended)**
```bash
# Generate unique key for each device during manufacturing
nvs_partition_gen.py encrypt input.csv output.bin \
    --keygen --keyfile device_1234.bin

# Flash to each device:
esptool.py write_flash 0xf000 device_1234.bin
```

**Option 2: Pre-provisioned Keys**
```bash
# Generate master key offline
espsecure.py generate_flash_encryption_key master_key.bin

# Burn to eFuse during production
espefuse.py burn_key BLOCK_KEY0 master_key.bin XTS_AES_256_KEY
```

**⚠️ Best Practice:**
- **Development**: Auto-generated keys (convenience)
- **Production**: Pre-provisioned unique keys (security)
- **Never** commit keys to Git
- **Always** back up production keys securely

---

## Troubleshooting

### "NVS encryption initialization failed"

**Cause**: Missing `nvs_keys` partition

**Solution**:
```bash
# Verify partition table
idf.py partition-table
# Should show: nvs_keys at 0xf000

# If missing, update config/partitions.csv and rebuild
idf.py fullclean build flash
```

### "Failed to read NVS security config"

**Cause**: Corrupted encryption keys

**Solution**:
```bash
# Erase NVS keys partition
idf.py erase-flash
# Or erase specific partition:
esptool.py erase_region 0xf000 0x1000

# Reboot - new keys will be generated
```

### "Credentials not encrypted after enabling"

**Cause**: Old credentials from before encryption

**Solution**:
```c
// Clear old credentials and re-provision
wifi_manager_clear_credentials();
// Restart provisioning - new credentials will be encrypted
```

---

## Security Checklist

### Development ✓
- [x] NVS Encryption enabled
- [x] Credentials verified encrypted
- [x] Auto-key generation working
- [ ] Flash Encryption (optional)
- [ ] Secure Boot (optional)

### Pre-Production
- [ ] Flash encryption enabled (recommended)
- [ ] Secure boot enabled (optional)
- [ ] Unique encryption keys per device
- [ ] Key backup and recovery process documented
- [ ] OTA updates tested with encryption

### Production Deployment
- [ ] All security features tested on pilot batch
- [ ] Key management process established
- [ ] Firmware signing process automated
- [ ] Device provisioning process includes key programming
- [ ] Security audit completed

---

## References

- [ESP-IDF NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/storage/nvs_encryption.html)
- [ESP-IDF Flash Encryption](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/security/flash-encryption.html)
- [ESP-IDF Secure Boot V2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/security/secure-boot-v2.html)
- [ESP32-S3 Security Features](https://www.espressif.com/en/products/socs/esp32-s3/resources)

---

**Last Updated**: November 15, 2025  
**Security Level**: MEDIUM-HIGH (with NVS encryption)  
**Recommended for**: Production IoT devices
