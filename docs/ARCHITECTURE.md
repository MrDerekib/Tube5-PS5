# Arquitectura de Tube5

Tube5 es una aplicación multimedia nativa para PS5. El prototipo actual reproduce
vídeo local y HTTP LAN con apertura de red cancelable y manejo de fallos. No
modifica el cliente oficial ni resuelve enlaces YouTube desde la consola.
El [primer piloto de YouTube](YOUTUBE_TEST.md) prepara un MP4 en el PC y lo sirve
por LAN: no es streaming directo ni el proveedor definitivo de catálogo.

## Implementado

- `main.cpp`: eventos SDL2, interfaz 1080p y mando. En PS5 compone sobre una
  superficie RGBA con renderer software directo, sin ventana ni driver de vídeo SDL.
- `navigation.hpp`: navegación independiente del hardware, con pruebas unitarias.
- `demo_renderer`: dibujo CPU; framebuffer lineal para SDL, conversión acotada
  a tiled y presentación VideoOut nativa de la plantilla.
- `native_sdl`: carga comprobada de Keyboard y wrappers optativos de diagnóstico.
- `media_player`: propiedad y limpieza del decodificador, textura y dispositivo de
  audio. SDL_kitchensink 2.0.0-a2 sobre FFmpeg 7.0.1 del port PacBrew.
- `native_compat.c`: `dladdr` sin información para el código de bibliotecas que lo
  consulta. El enlace incorpora funciones POSIX del SDK público donde son necesarias.

La pausa conserva una posición propia porque el reloj de esta versión de
SDL_kitchensink continúa avanzando en pausa. Las peticiones de seek se agrupan
y se ejecutan al reproducir para evitar perderlas mientras el demuxer está
ocupado. El clip usa H.264 baseline/AAC y la decodificación es por CPU.

## Capas previstas

1. **Shell/UI:** navegación con mando, búsqueda, biblioteca y ajustes.
2. **Catálogo:** una interfaz de proveedor que devuelve metadatos y opciones
   de reproducción. El proveedor concreto se seleccionará sólo tras revisar
   sus términos, disponibilidad y estabilidad.
3. **Reproductor:** reproduce una URL de medio autorizada y expone tiempo,
   pausa, seek y estado. Su contrato no conoce detalles del catálogo.
4. **SponsorBlock:** consulta segmentos para el identificador de vídeo y pide
   al reproductor un seek cuando corresponde. Usará la consulta privada por
   prefijo SHA-256, almacenará preferencias localmente y mostrará atribución.

## Decisiones iniciales

- Lenguaje y runtime: C++20 sobre `ps5-native-app-boilerplate`.
- Clase PS5: media (`applicationCategoryType: 65536`).
- Identidad actual: `PPSA99999` es exclusivamente de desarrollo local. No se
  distribuirá con ella; se asignará un ID único antes del primer release.
- Licencia: GPL-3.0-or-later, heredada de la plantilla. Cualquier código que
  se tome de otro proyecto se evaluará y atribuirá antes de incorporarse.

## Límites del primer hito

El objetivo inmediato es validar el nuevo reproductor y el mando en PS5 13.20.
La compatibilidad con una fuente de
vídeo concreta no se presupone ni se anuncia hasta contar con una integración
probada y permitida.
