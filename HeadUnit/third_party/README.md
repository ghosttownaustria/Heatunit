# Third-party code

- `aasdk/`: the Android Auto protocol library (OpenCarDev AASDK, GPL-3.0-or-later) with the local changes listed in
  `aasdk/PATCHES.md`. Runs on Windows and Linux.
- `libusb/`: the unmodified libusb 1.0.30 header and Windows x64 DLL/import library, with license and
  download/hash records. **Windows only**: Linux builds use the system's libusb (libusb-1.0-0-dev, found through
  pkg-config, version 1.0.16 or newer).
- `miniaudio/`: [miniaudio](https://github.com/mackron/miniaudio) 0.11.25, the single header `miniaudio.h`
  unmodified from tag `0.11.25`, with its `LICENSE` (public domain / MIT-0, your choice). It plays the phone's audio
  on Linux (PulseAudio, ALSA, JACK, chosen at run time); Windows uses WASAPI directly and only uses miniaudio when
  built with `-DHEADUNIT_AUDIO_BACKEND=miniaudio`. Source: https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/miniaudio.h
  (SHA-256 `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89`); `LICENSE` has SHA-256
  `457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb`. To update, replace both files from the new
  tag and record the new hashes here.

Current Qt binaries are supplied through the external SDK (Windows) or the system packages (Linux). Before adopting
another project, pin its exact commit, preserve copyright/license notices, record local patches and add reproducible
dependency configuration. See `../docs/android_auto.md` and `../docs/dependencies.md`.
