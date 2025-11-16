#!/usr/bin/env python3
"""
Generate self-signed SSL certificate for ESP32 HTTPS server
"""

from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
import datetime

def main():
    print("=" * 60)
    print("ESP32 Self-Signed Certificate Generator")
    print("=" * 60)
    print("\nThis will generate a certificate valid for 10 years.")
    print("Press Enter to use default values shown in [brackets]\n")
    
    # Prompt for certificate details
    country = input("Country Code (2 letters) [US]: ").strip() or "US"
    state = input("State/Province [California]: ").strip() or "California"
    city = input("City/Locality [San Francisco]: ").strip() or "San Francisco"
    org = input("Organization Name [ESP32 IoT Device]: ").strip() or "ESP32 IoT Device"
    org_unit = input("Organizational Unit [IoT]: ").strip() or "IoT"
    common_name = input("Common Name [esp32.local]: ").strip() or "esp32.local"
    
    print("\n" + "=" * 60)
    print("Generating certificate with the following details:")
    print(f"  Country: {country}")
    print(f"  State: {state}")
    print(f"  City: {city}")
    print(f"  Organization: {org}")
    print(f"  Unit: {org_unit}")
    print(f"  Common Name: {common_name}")
    print("=" * 60 + "\n")
    
    # Generate private key
    print("Generating 2048-bit RSA private key...")
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
    )
    print("✓ Private key generated")
    
    # Build certificate subject
    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, country),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, state),
        x509.NameAttribute(NameOID.LOCALITY_NAME, city),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, org),
        x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, org_unit),
        x509.NameAttribute(NameOID.COMMON_NAME, common_name),
    ])
    
    # Generate certificate
    print("Generating certificate...")
    certificate = x509.CertificateBuilder().subject_name(
        subject
    ).issuer_name(
        issuer
    ).public_key(
        private_key.public_key()
    ).serial_number(
        x509.random_serial_number()
    ).not_valid_before(
        datetime.datetime.utcnow()
    ).not_valid_after(
        datetime.datetime.utcnow() + datetime.timedelta(days=3650)  # Valid for 10 years
    ).add_extension(
        x509.SubjectAlternativeName([
            x509.DNSName(common_name),
            x509.DNSName("localhost"),
            x509.IPAddress(ipaddress.IPv4Address("192.168.0.1")),  # Placeholder, will be actual device IP
        ]),
        critical=False,
    ).sign(private_key, hashes.SHA256())
    print("✓ Certificate generated")
    
    # Write private key to file
    print("\nWriting files...")
    with open("main/certs/server_key.pem", "wb") as f:
        f.write(private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption()
        ))
    print("✓ Private key saved to: main/certs/server_key.pem")
    
    # Write certificate to file
    with open("main/certs/server_cert.pem", "wb") as f:
        f.write(certificate.public_bytes(serialization.Encoding.PEM))
    print("✓ Certificate saved to: main/certs/server_cert.pem")
    
    print("\n" + "=" * 60)
    print("SUCCESS! Certificate and key generated.")
    print("=" * 60)
    print("\nIMPORTANT:")
    print("  - These files will be embedded in your ESP32 firmware")
    print("  - Your browser will show a security warning (self-signed)")
    print("  - You'll need to accept the certificate in your browser")
    print("  - Certificate is valid for 10 years")
    print("\nNext steps:")
    print("  1. Build and flash your firmware: idf.py build flash")
    print("  2. Access dashboard at: https://<device-ip>")
    print("  3. Accept the security warning in your browser")
    print("=" * 60 + "\n")

if __name__ == "__main__":
    import ipaddress
    main()
