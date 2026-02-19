# CinePi Camera - Action Plan & Priority Matrix

## Quick Reference: Critical Issues Summary

| Priority | Type | Issue | Fix Time | Impact |
|----------|------|-------|----------|--------|
| CRITICAL | Memory | DRM connector leak on failure | 5m | Permanent leak at startup |
| CRITICAL | Memory | LVGL buffer alloc fail leak | 5m | 150KB lost if malloc fails |
| CRITICAL | Memory | Camera request queue leak | 10m | Breaks preview after requests fail |
| CRITICAL | Safety | Null pointer in status bar (localtime) | 5m | Crash on RTC failure |
| HIGH | Safety | Plane set failures ignored | 15m | Silent display failure |
| HIGH | Thread | GPIO callback race condition | 20m | Callback called while being modified |
| HIGH | Error | Device write not verified | 10m | Corrupted files without error |
| HIGH | Error | I2C init errors ignored | 15m | Sensor misconfiguration |
| MEDIUM | Perf | LVGL buffer too large | 5m | Memory pressure, fragmentation |
| MEDIUM | Perf | Blocking status bar updates | 30m | Frame drops on disk I/O |
| MEDIUM | Thread | Config manager not thread-safe | 10m | Data corruption during writes |
| MEDIUM | Safety | Touch fd race on close | 10m | Use-after-close |

---

## Recommended Implementation Order

### Phase 1: Critical Fixes (Day 1 - 2 hours)
Fix the crashes and permanent leaks that block functional testing:

1. **FIX [FIRST]**: Scene Manager - localtime() null check (5m)
   - File: [src/ui/scene_manager.cpp](src/ui/scene_manager.cpp#L71)
   - Prevents crash on RTC failure
   - Quick fix, high impact

2. **FIX [SECOND]**: LVGL buffer leak (5m)
   - File: [src/ui/lvgl_driver.cpp](src/ui/lvgl_driver.cpp#L39)
   - Prevents 150KB leak at startup
   - Often allocation failure trigger

3. **FIX [THIRD]**: DRM connector leak (15m)
   - File: [src/drivers/drm_display.cpp](src/drivers/drm_display.cpp#L88-110)
   - Permanent leak if display detection fails
   - Add RAII cleanup to all exits

4. **FIX [FOURTH]**: Camera request queue error checking (10m)
   - File: [src/camera/camera_pipeline.cpp](src/camera/camera_pipeline.cpp#L113)
   - Prevents silent preview failure
   - Check queueRequest return value

5. **TEST**: Everything still compiles and runs
   ```bash
   cd /Users/selimgun/Downloads/procamv2
   make clean && make
   ./build/cinepi &
   sleep 10 && pkill cinepi
   ```

### Phase 2: Error Handling (Day 1 - 1 hour)
Make failures visible instead of silent:

6. **FIX [FIFTH]**: DRM plane set error checking (15m)
   - File: [src/drivers/drm_display.cpp](src/drivers/drm_display.cpp#L315-340)
   - Diagnose display issues
   - Check return values on plane operations

7. **FIX [SIXTH]**: I2C sensor configuration (15m)
   - File: [src/drivers/i2c_sensors.cpp](src/drivers/i2c_sensors.cpp#L94-105)
   - Prevent silent sensor misconfiguration
   - Check all i2c_write_reg calls

8. **FIX [SEVENTH]**: Photo capture disk write (10m)
   - File: [src/camera/photo_capture.cpp](src/camera/photo_capture.cpp#L40-55)
   - Prevent corrupted JPEG files
   - Check fwrite() return value

### Phase 3: Thread Safety (Day 2 - 1.5 hours)
Prevent race conditions and crashes:

9. **FIX [EIGHTH]**: GPIO callback thread safety (20m)
   - File: [src/drivers/gpio_driver.cpp](src/drivers/gpio_driver.cpp) + header
   - Prevent callback races
   - Add mutex protection

10. **FIX [NINTH]**: Config manager thread safety (10m)
    - File: [src/core/config.cpp](src/core/config.cpp)
    - Prevent torn reads during config updates
    - Add lock to get() method

11. **FIX [TENTH]**: Touch input fd close race (10m)
    - File: [src/drivers/touch_input.cpp](src/drivers/touch_input.cpp#L75)
    - Prevent use-after-close
    - Better shutdown sequence

### Phase 4: Performance (Day 2 - 1 hour)
Optimize for RPi 3A+ with 512MB RAM:

12. **FIX [ELEVENTH]**: LVGL buffer size (5m)
    - File: [include/core/constants.h](include/core/constants.h#L27)
    - Reduce from 40 to 20 lines
    - Monitor for frame drops, adjust if needed

13. **FIX [TWELFTH]**: GPIO polling interval (5m)
    - File: [src/drivers/gpio_driver.cpp](src/drivers/gpio_driver.cpp)
    - Increase from 10ms to 50ms
    - Reduce CPU waste

14. **REFACTOR [NICE-TO-HAVE]**: Async status bar (30m)
    - File: [src/ui/scene_manager.cpp](src/ui/scene_manager.cpp)
    - Move I/O out of render thread
    - Use background thread for updates

---

## Validation After Each Phase

### After Phase 1:
```bash
# Check for obvious crashes
./build/cinepi -t 30s  # Run for 30 seconds

# Check for warnings in startup
./build/cinepi 2>&1 | grep -i "error\|failed\|null\|leak"

# Monitor memory
ps aux | grep cinepi
# Should be ~20-30MB before camera starts
# ~40-50MB after camera initialized
```

### After Phase 2:
```bash
# Check error messages are informative
./build/cinepi 2>&1 | head -30
# Look for errno values and strerror() messages

# Disconnect display, GPIO, sensors one by one
# Should see clear error messages, not silent failures
```

### After Phase 3:
```bash
# Use helgrind thread checker (if available)
helgrind --tool=helgrind ./build/cinepi 2>&1 | grep "race"
# Should show no races

# Valgrind memory check
valgrind --leak-check=full ./build/cinepi 2>&1 | tail -20
# Should show "All heap blocks were freed"
```

### After Phase 4:
```bash
# Check performance metrics
./build/cinepi 2>&1 | grep "FPS"
# Should maintain 30 FPS or close

# Memory usage should be stable
# CPU usage should drop with longer polling intervals
```

---

## Code Review Checklist

Before committing each fix:

- **Compilation**: `make clean && make` - no errors or warnings
- **Logic**: Manual code walk-through (especially error paths)
- **Testing**: Run app for 30 seconds minimum
- **Memory**: Check for obvious leaks with valgrind
- **Documentation**: Update any related comments

---

## Expected Improvements

### Before Fixes:
```
[Issues]: 40 identified
[Critical]: 3 (crashes, leaks)
[High]: 15 (silent failures, race conditions)
[Memory Usage]: ~50MB (with fragmentation)
[FPS Stability]: Variable (frame drops on I/O)
[Reliability]: Low (can crash in edge cases)
```

### After All Fixes:
```
[Issues Fixed]: 40 → ~5 (non-critical polish items)
[Critical]: 3 → 0 (eliminated)
[High]: 15 → 0 (all addressed)
[Memory Usage]: ~50MB → ~38MB (optimized)
[FPS Stability]: Stable at 30 FPS
[Reliability]: High (handles edge cases)
[Thread Safety]: Protected against race conditions
[Error Reporting]: Clear diagnostic messages
```

---

## Verification Test Cases

### Test 1: Camera Preview
```bash
# Expected: Camera preview shows on display for 30 seconds
./build/cinepi &
sleep 30
pkill cinepi
echo "✓ Camera preview stable"
```

### Test 2: Memory Stability
```bash
# Expected: Memory grows by <5MB over 2 minutes
./build/cinepi &
sleep 120
ps aux | grep cinepi | grep -v grep | awk '{print $6}'
pkill cinepi
echo "Memory stable if delta < 5MB"
```

### Test 3: Error Handling
```bash
# Disconnect USB cable from camera
./build/cinepi 2>&1
# Expected: Clear error message "[Camera] Failed to ..."
# App should gracefully degrade
```

### Test 4: Touch Input
```bash
# Simulate touch, check for crashes
./build/cinepi &
# Touch screen multiple times
sleep 10
pkill cinepi
echo "✓ No crash on touch input"
```

### Test 5: Shutdown
```bash
# Test graceful shutdown
./build/cinepi &
sleep 5
pkill -INT cinepi  # SIGINT
# Expected: Clean shutdown message in <2 seconds
```

---

## Long-Term Improvements (Future)

Beyond the immediate fixes, consider:

1. **Structured Logging** (Low Priority)
   - Replace `fprintf(stderr, ...)` with log levels
   - Add runtime log level control
   - Benefit: Better diagnostics in the field

2. **Configuration Validation** (Low Priority)
   - Validate config file paths
   - Bounds-check all user inputs
   - Benefit: Prevent misuse

3. **Hardware Abstraction** (Medium Priority)
   - Abstract away device-specific paths
   - Support multiple display/touch hardware
   - Benefit: Easier porting to other RPi models

4. **Performance Profiling** (Medium Priority)
   - Add timing instrumentation
   - Profile memory allocation patterns
   - Benefit: Identify remaining bottlenecks

5. **Unit Tests** (High Priority)
   - Test critical paths (camera, display, sensors)
   - Mock hardware for CI/CD
   - Benefit: Prevent regressions

---

## Files That Need Changes

```
CRITICAL (Apply immediately):
  ✓ src/ui/scene_manager.cpp
  ✓ src/ui/lvgl_driver.cpp
  ✓ src/drivers/drm_display.cpp
  ✓ src/camera/camera_pipeline.cpp

HIGH (Apply today):
  ✓ src/drivers/drm_display.cpp (additional)
  ✓ src/drivers/i2c_sensors.cpp
  ✓ src/camera/photo_capture.cpp
  ✓ src/drivers/gpio_driver.cpp (+ .h)
  ✓ src/core/config.cpp (+ .h)

MEDIUM (Apply this week):
  ✓ include/core/constants.h
  ✓ src/drivers/i2c_sensors.cpp (additional)
  ✓ src/ui/scene_manager.cpp (additional)

LOW (Nice to have):
  - src/power/power_manager.cpp
  - src/core/hardware_health.cpp
```

---

## Rollback Plan

If any fix introduces regressions:

```bash
# Revert last commit
git revert HEAD

# Or reset to last known good
git reset --hard <commit-hash>

cd /Users/selimgun/Downloads/procamv2
make clean && make

# Test
./build/cinepi
```

---

## Success Criteria

After completing all phases:

✅ **No crashes** on normal operation
✅ **No memory leaks** (verified by valgrind)
✅ **No data corruption** (JPEG files valid)
✅ **Clear error messages** when hardware fails
✅ **Stable 30 FPS** during camera preview
✅ **No thread races** (verified by helgrind)
✅ **Smooth UI** (no frame drops from I/O)
✅ **Safe shutdown** in <2 seconds
✅ **Graceful degradation** when hardware unavailable

