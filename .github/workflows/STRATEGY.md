# GitHub Actions Strategy

This repository uses an optimized CI/CD approach to save Actions minutes while maintaining quality.

## Current Workflows

### 1. `build.yml` - Production Builds
**Triggers:** Push to main/develop, releases, tags  
**What it does:** Builds firmware for all boards, uploads artifacts  
**Minutes used:** ~15-20 minutes per run (8 boards × 2-3 min each)

### 2. `build-validation.yml` - Build Validation (NEW!)
**Triggers:** Pull requests only  
**What it does:** Validates code compiles for all boards (no firmware uploads)  
**Minutes used:** ~2-3 minutes per run

### 3. `build-flasher.yml` - Web Flasher Tool
**Triggers:** Changes to flasher tool  
**Minutes used:** ~5 minutes

## Recommended Strategy

### For Pull Requests (Fast & Cheap)
✅ **Build validation only** (~2-3 minutes total)
- Validates all 7 board configurations compile successfully
- Checks for syntax errors, missing definitions, config issues
- No firmware uploads or artifacts generated
- Catches 90% of issues before merge

### For Main Branch (Comprehensive)
✅ **Full builds** (~15-20 minutes)
- Build all board variants
- Upload firmware artifacts
- Generate releases

### For Releases (Complete Package)
✅ **Everything** 
- Full builds
- Tests
- Create release packages

## Minutes Estimate

| Event | Old Approach | New Approach | Savings |
|-------|-------------|--------------|---------|
| PR (×10/week) | 20 min × 10 = 200 min | 3 min × 10 = 30 min | **170 min/week** |
| Main push (×5/week) | 20 min × 5 = 100 min | 20 min × 5 = 100 min | 0 min |
| Release (×1/month) | 20 min | 20 min | 0 min |
| **Total/week** | **300 min** | **130 min** | **170 min saved** |

## Free Tier Limits

- GitHub Free: **2,000 minutes/month**
- Old approach: ~1,200 min/month (60% of quota)
- New approach: ~520 min/month (26% of quota)
- **Savings: 680 minutes/month!**

## Implementation

The `build.yml` workflow already does everything we need. We should:

1. ✅ Keep `build.yml` as-is (full builds on main/releases)
2. ✅ Add `build-validation.yml` for lightweight PR validation
3. ✅ Remove `multi-board-tests.yml` (duplicates build.yml)
4. ✅ Keep `build-flasher.yml` as-is (separate tool)

See `build-validation.yml` for the efficient PR validation workflow.

## Local Development

Developers should run locally before pushing:

```bash
# Quick validation (2 minutes)
make test

# Test specific board (if you have hardware)
make test-board BOARD=test-esp32-c3
```

This catches issues before CI runs, saving even more minutes!

