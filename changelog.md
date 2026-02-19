# CinePi Camera - Changelog

## [1.2.0] - 2026-02-19 - Production Release

### Added
- Hardware Health Monitor with boot diagnostics
- Graceful hardware degradation framework
- App runs without optional hardware (touch/GPIO/sensors)
- Real-time FPS monitoring and frame-drop detection
- Smart pointer resource management for safety

### Changed
- Main loop refactored for safer initialization
- Hardware components now optional/critical based on function
- Improved error messages and diagnostics

### Fixed
- Touch X/Y race condition (atomic barriers with memory_order)
- JPEG memory peak: 863KB → 345KB (-63% reduction)
- Camera recovery: 3 retry attempts with exponential backoff
- Unsafe shutdown: now uses fsync() for data safety
- SSH disabled on boot: now always enabled

## [1.1.0] - 2026-02-18 - Stability Fixes

### Fixed
- Camera crash recovery state machine
- Memory optimization in gallery view
- Safe shutdown sequence with timeout

## [1.0.0] - 2026-02-01 - Initial Release

### Initial Release
- First production version