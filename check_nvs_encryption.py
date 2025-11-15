#!/usr/bin/env python3
"""Check if NVS partition contains encrypted or plain text WiFi credentials"""

import sys

# Read the NVS dump
with open('nvs_dump.bin', 'rb') as f:
    data = f.read()

print("=" * 60)
print("NVS Encryption Verification")
print("=" * 60)

# Search for common WiFi-related strings
keywords = [
    b'wifi',
    b'ssid', 
    b'password',
    b'sta.ssid',
    b'sta.password',
    b'sta.authmode',
    b'WiFi',
    b'SSID',
    b'PASSWORD'
]

print("\nSearching for plain text WiFi credentials:")
print("-" * 60)

found_any = False
for keyword in keywords:
    if keyword in data:
        print(f"  ⚠️  FOUND: '{keyword.decode()}' (SECURITY RISK!)")
        found_any = True
    else:
        print(f"  ✓  '{keyword.decode()}' - Not found in plain text")

print("-" * 60)

if found_any:
    print("\n❌ WARNING: Plain text credentials found!")
    print("   NVS encryption may not be working correctly.")
else:
    print("\n✅ SUCCESS: No plain text credentials found!")
    print("   Your WiFi credentials appear to be encrypted.")

# Show entropy (randomness) - encrypted data should be high entropy
non_zero_bytes = sum(1 for b in data if b != 0 and b != 0xFF)
total_bytes = len(data)
entropy_percent = (non_zero_bytes / total_bytes) * 100

print(f"\nNVS Partition Statistics:")
print(f"  Total size: {total_bytes} bytes")
print(f"  Non-zero/FF bytes: {non_zero_bytes} ({entropy_percent:.1f}%)")
print(f"  Zero/FF bytes: {total_bytes - non_zero_bytes} ({100-entropy_percent:.1f}%)")

# Show first 256 bytes as hex
print("\nFirst 256 bytes of NVS partition (hex):")
print("-" * 60)
for i in range(0, min(256, len(data)), 16):
    hex_str = ' '.join(f'{b:02x}' for b in data[i:i+16])
    ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
    print(f"{i:04x}:  {hex_str:<48}  {ascii_str}")

print("=" * 60)
