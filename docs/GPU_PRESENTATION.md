# Presentación eficiente a 1080p

Prioridad acordada: validar eficiencia y presentación GPU a 1080p antes de
habilitar resoluciones mayores. Conservar DIAG63 como baseline funcional y
la ruta CPU/SDL como fallback. No afirmar ahorro de CPU/energía sin medirlo.

## Ruta elegida para el primer experimento

Usar AGC directo siguiendo el presenter VideoDec2 de ProsperoLight, sin añadir
el stack completo RADV/Granite a Tube5 para la primera imagen. Referencias
locales fijadas: ProsperoLight `91c0cf8b6bc6c9cbb2d48c8cc6994fbccaa2d61f`
y PS5_Vulkan `5b5e4fc2d80fdb67a4f61ba9e6a8a424026bb8a5`.
Son evidencia externa de implementación, no pruebas de FW13.20.
No incorporar módulos Sony ni shaders extraídos de software propietario.
Conservar licencias y atribución de código reutilizado.

La ruta actual copia NV12 nativo a un AVFrame, convierte a IYUV y compone con
SDL/CPU. El objetivo es que un shader muestree los planos NV12, aplique matriz
y rango correctos y escriba el target que escanea VideoOut. Audio, reloj,
extracción, SponsorBlock y lógica de seek permanecen en el player existente.

## Pasos acotados

1. DIAG64: cargar módulos del sistema AGC/driver, resolver exports, llamar
   `sceAgcInit(8)` y consultar la existencia de defaults sin dereferenciarlos.
   No hay shaders, comandos, submissions ni modificación de VideoOut.
2. Presentar un frame NV12 propio por GPU y comprobar color, stride/crop 1088→1080,
   target tiled, finalización y limpieza. Una imagen correcta no valida vídeo.
3. Clip corto 1080p: sincronizar consumo GPU con el decoder y el reloj, luego
   introducir controles y overlay. Un único propietario de los flips VideoOut.
4. Comparar tiempos de copias/conversión/composición y ocupación del hilo CPU
   con la baseline, incluyendo pausa/seek y cierre. Reducir las copias restantes
   solo después de validar ownership. Resoluciones mayores quedan para después.

## Reglas de propiedad

- Nunca reutilizar un slot NV12 que aún pueda leer la GPU. Mantenerlo hasta el
  fence/completion correspondiente, no solo hasta acabar el submit en CPU.
- En timeout, no liberar memoria que la GPU pueda seguir usando; registrar el
  fallo y evitar reusar recursos inciertos. Preservar fallback/recuperación.
- Tablas y descriptores dereferenciados por GPU deben estar en memoria accesible
  por GPU; respetar coherencia y barreras, no pasar tablas de stack.
- Evitar competir con los flips CPU del renderer existente y restaurar el menú
  al terminar el player. No subir una build sobre una app abierta.

DIAG64 escribe `/download0/tube5-gpu.log`, numérico y sin tokens/URLs.
Una comprobación de acceso correcta no demuestra rendering ni una mejora de
rendimiento. Solo hay que arrancar y dejar la app en el menú para leer ese log.
