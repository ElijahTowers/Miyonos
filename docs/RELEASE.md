# Release process

1. Update `VERSION`, `CMakeLists.txt`, About text, `CHANGELOG.md`, and the
   release snapshot in `PROJECT_HANDOFF.md`.
2. Recheck pinned upstreams and hashes in `DEPENDENCIES.md`.
3. Run `./scripts/test-all.sh`; this includes the existing test suite,
   simulator input/fixture/screenshot checks, desktop build, macOS app bundle,
   and the ARM build when Docker is available.
4. Run `./scripts/build-miyoo.sh` separately if Docker was unavailable during
   the full runner.
5. Verify the release executable is stripped ARM EABI5 hard-float, the symbol
   file retains debug info, the ZIP begins at `App/Miyonos`, launch/binary
   permissions are executable, runtime dependencies are present, and the ZIP
   remains below 20 MB.
6. Validate the simulator signature/ZIP, `config.json`, all shell scripts, mock
   Python syntax, fixed 640 × 480 reference, and SHA-256 files. Scan ARM and
   OnionOS artifacts to confirm simulator code, fixtures, and development data
   are absent.
7. Execute the full hardware checklist in `TESTING.md`. If hardware is not
   available, state that explicitly in `FINAL_STATUS.md` and do not promote the
   mock result to stable.
8. Publish the ZIP, `.sha256`, changelog, and known limitations together.

The technical-preview package is installed by extracting it at the OnionOS SD
card root. Updates must never remove `App/Miyonos/data`. Debug symbols are for
maintainers and are not copied to the handheld.
