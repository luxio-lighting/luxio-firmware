# Standalone repositories

`next/` contains eight product repositories:

- `luxio-firmware`: firmware, recovery loader, protocol definitions, device simulator, protocol and hardware tests, and project records under `docs/project/`.
- `luxio-ios`: SwiftUI app and its Swift/protocol/preset tests.
- `luxio-android`: Compose app and Gradle configuration.
- `luxio-cli`: CLI, protocol sources and command tests.
- `api.luxio.lighting`: API, tests and Docker configuration.
- `setup.luxio.lighting`: USB setup site and its local sources.
- `studio.luxio.lighting`: Studio and its local simulator/browser sources.
- `luxio.lighting`: reserved product website repository; no application build yet.

Every repository builds from its own checkout with documented platform tools. No sibling checkout, symlink, Git submodule or unpublished package is required. Protocol and simulator sources used by multiple products are ordinary local copies; update each copy explicitly and run its owner's tests.

The firmware repository owns the wire protocol definition in `Luxio/luxio.proto`. Its `npm run sync:protocol` regenerates its TypeScript schema. Other consumers retain their own protocol definitions and build independently.

The isolated checkout checks recorded on 22 October 2026 passed before consolidation: all seven Node packages installed and typechecked independently, application builds and CLI/protocol/Swift/firmware host tests passed. Android build/unit tests and both firmware images also passed. The removed library's simulator and eight tests now live in the firmware repository.

After consolidation, firmware TypeScript checking, C++ sanitizer tests, and all eight migrated protocol/simulator tests passed.
