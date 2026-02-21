# Phase/Subphase Completion Checklist

Use this checklist in each implementation PR for Phase 31.x work.

## Required

- [ ] Scope implemented exactly for this subphase
- [ ] Subphase exit criteria verified
- [ ] Required docs updated
- [ ] Required CI lanes green on new path
- [ ] Rollback path validated for this subphase
- [ ] Next subphase preconditions confirmed

## Dual-Run (when applicable)

- [ ] Legacy and new paths run in parallel
- [ ] Dual-run started date recorded
- [ ] Consecutive green runs count recorded
- [ ] Minimum gate met (5 green runs)
- [ ] Preferred gate met (10 green runs)
- [ ] Legacy path removal approved

## Traceability

- [ ] Related pins updated (`.ci/dependency-pins.yml`)
- [ ] Compatibility matrix updated (`.ci/compatibility-matrix.yml`)
- [ ] Rationale/changelog notes included
