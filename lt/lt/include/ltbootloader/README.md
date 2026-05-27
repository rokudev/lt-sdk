
# LT Bootloader Architecture Versions

Multiple versions of LT Bootloader **common** source code are maintained here to
provide stability and decrease maintenance costs for LT Bootloader code going
forward.

After choosing an architecture version, a bootloader developer will have the
reasonable expectation of continuous stability for bootloader interfaces and
functionality.  LT Bootloaders should ideally never change in the field, but
when they do, the number of regressions should be zero.

The LTBootloader Architecture Versions are as follows:

 * Architecture Version 1.1
    * Modular architecture
        * Allows the optional use of platform-specific ROM code, hardware
           accelerators, vendor libraries, etc.
    * LT Standard Feature Support
        * LT Partition Table
        * LT Ping-Pong OTA Mechansim
        * LT Authentication Token (LTAT)
        * LT Bootloader Mailbox
        * LT Application Header
    * Standard Library
        * A minimal number of optional LIB-C compatible functions are provided.
    * Software Cryptographic Support
        * SHA-256 and SHA-512 checksum
        * Ed25519 signature checking
    * Hardening against fault injection attacks
 * Architecture Version 1.0 (Historical, not provided)

