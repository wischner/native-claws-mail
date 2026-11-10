# Releasing

[`.github/workflows/release.yml`](../../.github/workflows/release.yml) builds
Blaise in two ways, both in Docker and both with the same commands you run
locally:

- **`make` job:** `make`, `make headless` and `make test` (ASan and UBSan),
  exactly as on a developer machine. Its `bin/` is kept as the workflow
  artifact `make-bin-<version>`.
- **`build` jobs:** one per backend in
  [`platforms.json`](../../scripts/build/platforms.json), each running
  `make dist PLATFORM=<backend> VERSION=<version>`. That builds the backend with
  CMake in its SDK image and writes `dist/blaise-<version>-<backend>.zip`, for
  example `blaise-1.2.3-linux-sdl2.zip` or `blaise-1.2.3-haiku.zip`.

A tag release attaches every zip and a `SHA256SUMS` file.

## Inputs

Nothing comes from a developer machine. The workflow checks out the repository,
fetches [Native](https://github.com/wischner/native) at the commit pinned in
[`fetch_native.sh`](../../scripts/build/fetch_native.sh), pulls the SDK images
at the versions named in `platforms.json`, and builds the rest from Dockerfiles:
`blaise-linux-sdl` from
[`scripts/build/docker/linux-sdl`](../../scripts/build/docker/linux-sdl), and the
Wayland and Windows SDKs from Native's own Dockerfiles.

To build against a newer Native, change the commit in `fetch_native.sh`, run
`make test` and `make platform-test`, and commit the change.

## Publish a version

```sh
git tag v1.2.3
git push origin v1.2.3
```

Pushing a tag of the form `vX.Y.Z` starts the workflow; the version in the zips
and in `blaise --version` is the tag without the `v`. If `make test` and every
backend pass, it creates the GitHub release `v1.2.3` with the zips attached. If
anything fails, no release is created; fix the problem, then move the tag:

```sh
git tag -f v1.2.3 && git push -f origin v1.2.3
```

## Test without making a release

- **Locally, one backend:** `make dist PLATFORM=haiku VERSION=9.9.9` writes
  `dist/blaise-9.9.9-haiku.zip`. It runs the same command as CI. It needs
  `python3`, `patchelf` and `zip` as well as Docker.
- **On GitHub, no tag:** go to Actions > Release > Run workflow. Choose the
  branch, and either `all` or a list such as `linux-sdl2 haiku`. A Native commit,
  tag or branch may be given to try another Native. The zips appear as artifacts
  on the run page, versioned `0.0.0-dev.<run>`. No tag or release is created.
- **The tag path itself:** push a throwaway tag to a fork. To push one here,
  delete it afterwards with
  `gh release delete v0.0.1 --cleanup-tag --yes`.

## What goes in each zip

`scripts/build/package.py` decides what each zip contains:

- The executable, `README.md` written for that backend from the `package`
  entry in `platforms.json`, `LICENSE` and `THIRD-PARTY-NOTICES.md`.
- `BLAISE.PNG`, the desktop icon from `assets/icons/`, beside the executable.
  GEMiX images convert it to `/opt/gem/share/gem/icons/blaise.rsc`; see
  [Desktop icon](../../README.md#desktop-icon).
- **Linux:** every library in the executable's closure that no Ubuntu package
  owns, copied to `lib/` (the executable's RUNPATH is `$ORIGIN/lib`). Libraries
  that a package owns are not copied; the README lists their packages on an
  `apt install` line.
- **Windows:** the MinGW runtime DLLs, the Windows App Runtime bootstrap DLL
  and the Windows App SDK license texts. Users install Windows App Runtime 1.8.
- **Haiku:** the executable alone; it links only system libraries.

Only `linux-sdl2` has TLS; the README of every other zip says so. There is no
macOS zip yet; see [PLATFORM-STATUS.md](PLATFORM-STATUS.md).

The GEMix zip is a `gemd` client and needs a GEMix server already running.
