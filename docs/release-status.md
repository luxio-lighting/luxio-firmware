# Firmware qualification

Build 9 includes Matter, manual recovery updates, Identify fade-blink and Luxio Lighting / DIY metadata. S3 Mini hardware passed interrupted/corrupt downloads, manual retry and reset during writing. The user confirmed Apple Home re-pairing and Identify.

Public release remains blocked on Matter/Luxio GATT coexistence, production attestation/commissioning credentials, conformance, reverse state synchronization, wider board/strip qualification and further resilience tests. No full application rollback image is retained. See recovery-updates.md for update behavior.

## Build 23 firmware matrix (2026-10-24)

All five targets compile with Matter enabled using the pinned Arduino ESP32 3.3.3 SDK. Main/recovery binaries pass embedded chip-ID, partition-table, image-size and compiler stack-budget checks. These are build results, not physical board qualification.

| Chip | Main bytes | Recovery bytes | Minimum flash |
| --- | --- | --- | --- |
| esp32 | 2,141,072 | 1,102,608 | 4 MB |
| esp32s3 | 2,290,816 | 1,100,656 | 4 MB |
| esp32c3 | 2,348,768 | 1,180,768 | 4 MB |
| esp32c5 | 3,014,864 | 1,235,408 | 8 MB |
| esp32c6 | 2,989,312 | 1,186,960 | 8 MB |

Validation includes 56 Setup browser tests; OTA API integration tests for per-chip publication, exact target selection, channel isolation and malformed layouts; 45 package cases; the firmware host/native tests; and persisted-update validation for all five target layouts. Actual compiled packages are also checked through the publisher and a local OTA API/MongoDB instance, with artifact downloads served from local files. No production update records are written by that verification.

ESP32/S3 keep their existing recovery layout. C3 uses a larger recovery slot within 4 MB. C5/C6 use an 8 MB layout because their complete Matter application and HTTPS recovery do not fit the 4 MB layout. C2/C61 remain unavailable with the pinned precompiled SDK. The existing public release/hardware qualification restrictions above still apply; no devices were flashed during this matrix work.
