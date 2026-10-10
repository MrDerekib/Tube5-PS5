# Tube5

Nombre de la aplicación: **Tube5**. Repositorio público: **Tube5-PS5**.

Aplicación multimedia homebrew nativa para PlayStation 5, diseñada para una
experiencia de salón y preparada para integrar saltos de segmentos mediante
SponsorBlock.

> Estado: prototipo con navegación y vídeo local con audio. Todavía no reproduce
> YouTube ni bloquea anuncios; SponsorBlock está pendiente.

## Prototipo 01.000.001

Menú con vídeo de prueba, ajustes informativos y créditos. El clip sintético de
20 segundos es propio y no requiere Internet. Se reproduce mediante SDL2,
SDL_kitchensink y FFmpeg, con decodificación por software.

- Cruceta: navegar; izquierda/derecha durante el vídeo: saltar 5 segundos.
- X: abrir o pausar/reanudar. Círculo: volver. Options: ajustes fuera del vídeo.

La versión inicial arrancó en una PS5 Fat con FW 13.20 y kstuff activo.
Este nuevo prototipo está probado en PC con sanitizadores y pendiente de probar
en esa consola. No se garantiza compatibilidad con otros firmwares.

Para actualizar la prueba: cerrar Tube5 y copiar **toda** la carpeta
`dist/PPSA99999/` a `/data/homebrew/PPSA99999/`, sustituyendo la anterior.
Incluye `eboot.bin`, `sce_sys/`, `sce_module/` y `assets/`; no basta con el ejecutable.

## Primera arquitectura

La aplicación separará interfaz, catálogo, reproductor y el adaptador de
SponsorBlock. Así podremos comprobar cada componente en PS5 sin depender de
una sola fuente de vídeo. Los detalles están en
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Desarrollo local

La plantilla usa C++20, Make, Clang, Ninja y el SDK público PS5 Payload SDK.
El SDK y las dependencias del proyecto se descargan bajo `.deps/` al preparar
el entorno:

```bash
make deps USE_CCACHE=0
PS5_CLANG=clang USE_CCACHE=0 make
```

La compilación genera `dist/PPSA99999/` y un ZIP con la carpeta para el
loader homebrew compatible. `PPSA99999` es una identidad temporal de desarrollo
y debe cambiarse antes de cualquier distribución.

En este host está disponible Clang 22 con el nombre `clang`, no el binario
`clang-18` que busca la plantilla por defecto. Por ello los comandos anteriores
lo seleccionan explícitamente. La compilación ha sido validada localmente, pero
la plantilla sólo declara soporte de Clang 18: antes de publicar un release se
hará una prueba en hardware y, si es posible, con la versión de Clang soportada.
El lint global con clang-format 22 detecta diferencias de formato en ejemplos
heredados de la plantilla; los archivos de este prototipo sí pasan el chequeo
de formato local. CI utiliza clang-format 18.

Pruebas de navegación, framebuffer y herramientas: `USE_CCACHE=0 make test`.
El ensayo del reproductor en PC requiere SDL2, FFmpeg, libass, CMake y Ninja:

```bash
git clone https://github.com/katajakasa/SDL_kitchensink.git .deps/host/SDL_kitchensink
git -C .deps/host/SDL_kitchensink checkout c1bac5287e14122b2cf886628bdb802cf3737d83
git -C .deps/host/SDL_kitchensink apply ../../../tools/patches/kitchensink-video-timing.patch
bash tools/test-host-media.sh
```

Usa la misma corrección de tiempo de vídeo que el port de PacBrew. Comprueba
apertura, decodificación, pausa, seek, fin de clip y reapertura; genera capturas
en `build/`. Las pruebas headless no validan la salida física de audio ni el
mando de PS5. El clip se regenera con `bash tools/generate-test-clip.sh`.

Consulta [el roadmap](docs/ROADMAP.md), incluida la futura opción discreta de
donaciones. Tube5 seguirá siendo gratuita.

## Créditos y licencia

Tube5 parte de
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
licenciado GPL-3.0-or-later. Este repositorio también se publica bajo
GPL-3.0-or-later; consulta [LICENSE](LICENSE) y
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

SponsorBlock ofrece una API pública y exige respetar su atribución y sus
límites de uso. La futura integración incluirá los avisos correspondientes.
