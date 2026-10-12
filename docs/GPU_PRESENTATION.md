# Presentación eficiente a 1080p

Prioridad acordada: validar eficiencia y presentación GPU a 1080p antes de
habilitar resoluciones mayores. Conservar DIAG65 como baseline funcional y
la ruta CPU/SDL como fallback. No afirmar ahorro de CPU/energía sin medirlo.

## Ruta elegida para el primer experimento

DIAG66 es un experimento aislado que usa el SDK público ps5-opengl 1.0.1
(GPL-3.0-or-later) para presentar un frame NV12 propio con shaders escritos
para Tube5. El SDK se descarga a `.deps/gpu-sdk/` y no se versiona. Su paquete
publicado solo tiene comprobaciones de host; la prueba en FW13.20 sigue pendiente.
ProsperoLight y PS5_Vulkan fueron referencias de investigación, pero sus
recursos privados no entran en la build.

La ruta actual copia NV12 nativo a un AVFrame, convierte a IYUV y compone con
SDL/CPU. El objetivo es que un shader muestree los planos NV12, aplique matriz
y rango correctos y escriba el target que escanea VideoOut. Audio, reloj,
extracción, SponsorBlock y lógica de seek permanecen en el player existente.

## Pasos acotados

1. DIAG64: cargar módulos del sistema AGC/driver, resolver exports, llamar
   `sceAgcInit(8)` y consultar la existencia de defaults sin dereferenciarlos.
   No hay shaders, comandos, submissions ni modificación de VideoOut.
   DIAG64 no pudo cargar los módulos por dlopen en FW13.20. DIAG65 usa la
   ruta de imports enlazados del título para init/defaults y un query driver,
   sin distribuir módulos del sistema. DIAG65 validada en FW13.20: init 0,
   defaults presentes, query driver 32 dwords, cero submissions. Todavía no
   demuestra ninguna imagen renderizada ni ahorro de CPU.
2. DIAG66: presentar un frame NV12 propio por GPU y comprobar color y crop
   1088→1080 contra cinco muestras CPU. Los dos planos se suben a texturas;
   esto valida la ruta gráfica, pero todavía no elimina copias ni valida vídeo.
3. Clip corto 1080p: sincronizar consumo GPU con el decoder y el reloj, luego
   introducir controles y overlay. Un único propietario de los flips VideoOut.
4. Comparar tiempos de copias/conversión/composición y ocupación del hilo CPU
   con la baseline, incluyendo pausa/seek y cierre. Reducir las copias restantes
   solo después de validar ownership. Resoluciones mayores quedan para después.

## Reglas de propiedad

- Nunca reutilizar un slot NV12 que aún pueda leer la GPU. Mantenerlo hasta el
  fence/completion correspondiente, no solo hasta acabar el submit en CPU.
  DIAG66 usa `glFinish` antes de liberar el frame.
- En timeout, no liberar memoria que la GPU pueda seguir usando; registrar el
  fallo y evitar reusar recursos inciertos. Preservar fallback/recuperación.
- Tablas y descriptores dereferenciados por GPU deben estar en memoria accesible
  por GPU; respetar coherencia y barreras, no pasar tablas de stack.
- Evitar competir con los flips CPU del renderer existente y restaurar el menú
  al terminar el player. No subir una build sobre una app abierta.

DIAG66 se compila con `bash tools/build-gpu-frame.sh` y escribe
`/download0/tube5-gpu-frame.log` y `/download0/tube5-gpu-decoder.log`, sin
tokens ni URLs. Sustituye temporalmente la interfaz por una imagen estática y
queda abierta para inspección; se cierra desde el menú PS. La build estable
DIAG65 se conserva en la consola antes de activar DIAG66.

DIAG66 validada por consola en FW13.20: el usuario vio el frame a pantalla
completa; el log reporta `GL ERROR 0` y cinco muestras con cero diferencias.
VideoDec2 entregó 1920×1088, pitch 2048, y terminó con `CLEANUP 0` y
`QUARANTINED 0`. Esto valida un frame y la presentación GPU; no mide todavía
rendimiento ni valida reproducción continua, seek o ownership sin copias.

La prueba continua (iniciada en DIAG67) reproduce el fixture propio
`hw-bclip.t5hc`: 150 frames, 1080p30, 5 segundos y B-frames. Compila los shaders
una vez, reserva dos texturas y actualiza sus planos con `glTexSubImage2D`.
Mantiene cada frame nativo hasta terminar dibujo y swap con `glFinish`, y libera
decoder, texturas, VAO y programa al finalizar. El contexto EGL queda abierto
para conservar la última imagen y permitir leer logs por FTP.

Los informes son `/download0/tube5-gpu-clip.log` y
`/download0/tube5-gpu-clip-decoder.log`. Las muestras de color se comprueban en
el primer y último frame; el decoder valida orden y CRC. Los máximos de
upload/draw/swap miden tiempo transcurrido en esas llamadas, no ocupación CPU;
la espera de pacing queda fuera de esos máximos. DIAG66 y DIAG65 permanecen
respaldadas.

DIAG67 validada en consola: 150 enviados/decodificados/dibujados, 150 CRC
correctos, cero errores de orden, diez muestras RGB sin diferencias y
`GL ERROR 0`. El usuario confirmó que termina en el frame 149. Decoder y
recursos GL se liberaron; `CLEANUP 0`, `QUARANTINED 0`.
El elapsed fue 5.657.421 µs y se contaron 150 frames tarde respecto al reloj
iniciado antes del primer frame. Máximos de llamadas upload/draw/swap:
37.174/16.619/345 µs. Estos máximos y el coste de readback inicial no establecen
todavía rendimiento sostenido de 30 fps; el contador SLOW del decoder incluye
esperas de pacing en el callback y tampoco mide coste CPU puro. El siguiente
paso es medir el tramo continuo tras el primer frame y refinar upload/pacing.

DIAG68 separó arranque y reproducción continua y agrupó dibujo/swap con una
espera final. Los 150 frames y las diez muestras RGB pasaron, pero el tramo
continuo tardó 5.648.655 µs (unos 26,4 fps): intervalos p50/p95/máximo
37.519/41.062/44.898 µs y trabajo gráfico sin pacing/oracle
37.206/37.742/39.466 µs. Hubo 143 retrasos mayores de un frame. El retraso
persistía tras el arranque.

DIAG69 intentó usar el NV12 como un texture buffer `R8`, pero la comprobación
de tamaño devolvió `-19` antes de crear el decoder o presentar un frame. El
`SIGSYS` posterior se produjo al volver de `main()` y llamar a `exit()`;
el backtrace `0x4001b3` coincide con esa llamada del CRT. La recuperación con
DIAG66 permitió leer el informe persistente. Una aplicación distinta abierta
causó además `Device busy` en ShadowMount, resuelto al cerrarla.

DIAG70 (`bash tools/build-gpu-clip.sh`) empaqueta NV12 en texels `RGBA32UI`
de 16 bytes. El shader extrae Y/U/V mediante índices y operaciones enteras;
no se realiza conversión ni swizzle por píxel en CPU. La reserva se divide en
hasta ocho texture buffers si lo exige el límite consultado. En esta consola,
`GL_MAX_TEXTURE_BUFFER_SIZE` es 1.048.576 texels, hay 16 unidades de textura
y basta un bloque de 6.684.672 bytes. El mismo límite en `R8` explicaba el
rechazo de DIAG69. Sigue habiendo copia CPU del NV12 a un buffer GL, por lo que
no es una ruta sin copias. Los errores propios de preparación conservan ahora
el proceso abierto para inspeccionar logs, en lugar de retornar de `main()`.

DIAG70 validada en FW13.20: 150 enviados/decodificados/dibujados, 150 CRC
correctos, diez muestras RGB correctas, cero errores de orden, `GL ERROR 0`,
`CLEANUP 0` y `QUARANTINED 0`. El tramo entre primer y último frame fue
4.966.807 µs (149 intervalos, aproximadamente 30 fps), con cero retrasos
mayores de un frame. Intervalos p50/p95/máximo: 33.335/33.390/33.868 µs.
Trabajo gráfico sin pacing ni oracle p50/p95/máximo: 4.214/6.446/6.571 µs;
medianas de llamadas upload/draw/swap+finish: 403/3.369/468 µs. El arranque
costó 68.204 µs. La reducción frente al trabajo mediano de DIAG68 es del 89 %
en tiempo transcurrido, no una medición de consumo CPU o energía. El contexto
permanece abierto; un ciclo no valida ausencia de crecimiento entre reaperturas.
El siguiente paso es integrar el presenter en el reproductor con un único
propietario de VideoOut, audio, pausa y seek a 1080p.

Dependencia local: [SDK público 1.0.1](https://github.com/blackbearreloaded/ps5-opengl/releases/tag/v1.0.1),
archivo `ps5-opengl-sdk-1.0.1.tar.gz`, extraído en
`.deps/gpu-sdk/ps5-opengl-sdk-1.0.1/`. SHA-256 del archivo:
`aaa2e8957f55e1fc0b654dcb35a36e585f7e635d63952da40a28be7f64fde741`.
