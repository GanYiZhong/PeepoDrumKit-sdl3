> [!IMPORTANT]
> **This repository is a fork.** Peepo Drum Kit was created by **[samyuu](https://github.com/samyuu)** — original project: [samyuu/PeepoDrumKit](https://github.com/samyuu/PeepoDrumKit).
>
> Fork chain this work is built on (all credit for the editor itself goes to them):
>
> | | Repository | Contribution |
> |---|---|---|
> | Original author | [samyuu/PeepoDrumKit](https://github.com/samyuu/PeepoDrumKit) | Peepo Drum Kit (Copyright (c) 2022 Samyuu) |
> | Upstream | [0auBSQ/PeepoDrumKit](https://github.com/0auBSQ/PeepoDrumKit) | TJAPlayer3 / OpenTaiko features and continued development |
> | SDL3 port | [Steve-xmh/PeepoDrumKit](https://github.com/Steve-xmh/PeepoDrumKit) | Cross-platform SDL3 / SDL_GPU / xmake refactor |
> | Upstream | [IepIweidieng/PeepoDrumKit](https://github.com/IepIweidieng/PeepoDrumKit) and [IepIweidieng/PeepoDrumKit-sdl3](https://github.com/IepIweidieng/PeepoDrumKit-sdl3) | Maintained Windows version and the SDL3 branch **this repository is forked from** |
>
> What this fork adds (branch `web-emscripten`): an **experimental WebAssembly build** that runs the editor in a browser, plus a small
> JavaScript API for embedding it in another page. See **[web/README.md](web/README.md)** for build steps, the API and known limitations.
> The desktop build is unchanged.
>
> **License:** the upstream projects do not ship an open-source license for Peepo Drum Kit itself (`license_txt/PeepoDrumKit.txt` only states
> "Copyright (c) 2022 Samyuu"), and this fork does not and cannot grant one. Third-party licenses are kept in [`license_txt/`](license_txt/).
> Please ask the original author before redistributing or hosting builds.

# Peepo Drum Kit

Fork of PeepoDrumKit with large refractor for Official Taiko Charting & Modding.

<img width="1392" height="944" alt="A screenshot of the application" src="https://github.com/user-attachments/assets/f695d018-1839-4ff9-adbf-ef7b4dc4c16b" />

## Features

- Cross-platform via using SDL3 with native rendering backend (Tested on Windows and macOS, maybe works on Linux?)
- Low latency WASAPI share mode output when the mix format is in 16bit 44100Hz 2ch.
- ...Maybe more for official charting i guess xd

## Build

```bash
xmake
xmake build
```

### Web (WebAssembly) build

```bash
python web/build.py --serve   # needs an activated Emscripten SDK, then open http://localhost:8080
```

Details: [web/README.md](web/README.md)

## Credits

- [samyuu/PeepoDrumKit](https://github.com/samyuu/PeepoDrumKit)
- [0auBSQ/PeepoDrumKit](https://github.com/0auBSQ/PeepoDrumKit)
- [Steve-xmh/PeepoDrumKit](https://github.com/Steve-xmh/PeepoDrumKit) (SDL3 port)
- [IepIweidieng/PeepoDrumKit](https://github.com/IepIweidieng/PeepoDrumKit) / [IepIweidieng/PeepoDrumKit-sdl3](https://github.com/IepIweidieng/PeepoDrumKit-sdl3)
