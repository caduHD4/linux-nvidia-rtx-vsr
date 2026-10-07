# Public Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Publish current source and an installable Linux preview without NVIDIA redistribution.
**Architecture:** Shared SDK root resolver, existing Chromium seccomp TSYNC, user-space archive/installer.
**Tech Stack:** C++20, Chromium GN, Python3, Bash, GitHub Releases.
**Spec:** docs/specs/2026-10-06-public-preview-design.md

## Global Constraints
Pinned Chromium b510e9d7cd3a2fbd78d0ddc42234103206c5f78d; SDK1.3.0.0; no proprietary artifacts or credentials published; no sandbox disabling or relaxed security checks.

## Review Focus
- Relative/empty SDK paths must not grant cwd or broad broker access.
- Missing SDK must explain setup, not silently claim VSR.
- Paths with spaces must survive launcher and desktop entry.
- An incomplete install must not replace an existing usable version.
- GPU-created threads must all receive seccomp; video playback alone is insufficient.

### Task1: Runtime path and sandbox
- [x] Add resolver tests for default, absolute override, empty and relative override. Observe failure.
- [x] Implement resolver in policy; hook and worker call it. Compile exact affected GN units.
- [x] Establish source/log evidence for multiple GPU threads; attempt existing TSYNC support without relaxing checks.
- [x] Inspect GPU thread seccomp; real secured inference blocked (-14, parent-relative feature discovery). Recorded evidence; binary withheld.

### Task2: Package and install
- [x] Add package/launcher/install/uninstall scripts and essential notices.
- [x] Verify temporary installation, missing SDK and paths with spaces; archive excludes proprietary/runtime/user data.
- [ ] Validate staged browser playback and sandbox with SDK external.

### Task3: Source and release
- [x] Update documentation, preserve published README commits, review tracked diff for secrets/artifacts.
- [x] Commit current integration and distribution tooling; pushed source snapshot 9724b70 fast-forward, preserving README history.
- [x] Source progress published; binary withheld because secured NGX discovery is blocked. No GitHub browser Release created.

Ruling: TSYNC alone exposed BrokerProcess::Fork single-thread DCHECK (2 threads). --gpu-sandbox-start-early alone still preloads VA-API first. Move broker creation before VA-API initialization, retain the same permissions and TSYNC policy; never remove the DCHECK. Evidence: validation/public-preview-sandbox-attempt.log and public-preview-early-sandbox.log.

Current verification: CTest12/12, Python27/27 including installer9/9; actual GPU
seccomp scheduler filter10cases. Development 4K playback restored:480 enhanced
selections/20sec, continuous after warmup. Public binary gate remains blocked;
no archive upload or installable release claim. Diagnostic sandbox is explicit
opt-in; no parent-path checks or ANGLE checks were removed.

Final packaging check: external staging-preview-2-unvalidated contains987files,
clean-source BUILD-INFO referencing public9724b70, no NVIDIA binaries or symlinks.
SHA256SUMS verified; actual package installed/uninstalled in temporary XDG paths
with spaces and launch.py --check passed. No secured playback claim follows from
that prerequisite check. Relocated-package playback remains an open release gate.


## Explicit publication override

After disclosure of the blocker, user instructed “publique de qualquer forma!!!”.
Publish an experimental browser using the verified local path with the GPU
sandbox limitation prominently disclosed. Early sandbox remains diagnostic opt-in;
no sandbox-disable flags or removed checks. Validate installed/relocated package,
archive SHA256, licensing and exclusion of NVIDIA assets, then publish prerelease.
GitHub API authentication is required for asset upload; Git SSH alone is not enough.
