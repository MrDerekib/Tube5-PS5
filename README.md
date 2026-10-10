# Tube5

Aplicación multimedia homebrew nativa para PlayStation 5, diseñada para una
experiencia de salón y preparada para integrar saltos de segmentos mediante
SponsorBlock.

> Estado: fundación de desarrollo. Aún no reproduce contenido ni bloquea
> anuncios.

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

La primera compilación genera `dist/PPSA99999/` y un ZIP instalable para el
loader homebrew compatible. `PPSA99999` es una identidad temporal de desarrollo
y debe cambiarse antes de cualquier distribución.

En este host está disponible Clang 22 con el nombre `clang`, no el binario
`clang-18` que busca la plantilla por defecto. Por ello los comandos anteriores
lo seleccionan explícitamente. La compilación ha sido validada localmente, pero
la plantilla sólo declara soporte de Clang 18: antes de publicar un release se
hará una prueba en hardware y, si es posible, con la versión de Clang soportada.

## Créditos y licencia

Tube5 parte de
[ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate),
licenciado GPL-3.0-or-later. Este repositorio también se publica bajo
GPL-3.0-or-later; consulta [LICENSE](LICENSE) y
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

SponsorBlock ofrece una API pública y exige respetar su atribución y sus
límites de uso. La futura integración incluirá los avisos correspondientes.
