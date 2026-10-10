# Diagnóstico de arranque en PS5

La build 01.000.001 compiló y pasó los tests en PC, pero el propietario informa
de un fallo en PS5 Fat FW 13.20: pantalla de carga de la plantilla, aviso de
bloqueo antes de pausar kstuff y CE-108255-1. Los juegos del SSD externo siguen
arrancando tras actualizar los payloads. No se ha confirmado la causa.

La app añade SDL, FFmpeg y dependencias de PacBrew respecto a la base que sí
arrancaba. Enlazar sus símbolos no prueba su disponibilidad en un título nativo.
Hay imports de Keyboard, ImeDialog y PosixForWebKit a revisar.

## Build con puntos de control

```bash
PS5_CLANG=clang USE_CCACHE=0 make APP_DEFINITIONS=TUBE5_DIAGNOSTICS BUILD_LABEL=BOOT-DIAG-1
```

Cerrar Tube5 y sustituir la carpeta completa `dist/PPSA99999/` en la consola,
incluyendo `sce_module/`. No cambiar los payloads ni el firmware para esta prueba.
Grabar los avisos y comunicar el último código Dxx visible.

| Último aviso | La siguiente operación pendiente |
| --- | --- |
| Ninguno | Entrada del ejecutable, imports, loader o envío de la primera notificación |
| D00 | Lectura de parámetros del proceso y `_init_env` |
| D01 | Registro de destructores y constructores estáticos |
| D02 | Entrada en `main` |
| D03 | Lectura de etiqueta de build |
| D04 | UserService |
| D05 | SDL vídeo, incluidos teclado/remote inicializados por su driver PS5 |
| D06 | SDL audio |
| D07 | SDL mando |
| D08 | Ventana |
| D09 | Renderer software |
| D10 | Textura, framebuffer y conexión del mando |
| D11 | SDL_kitchensink |
| D12 | Eventos, dibujo y presentación |
| D13 | Primer frame presentado; evaluar el fallo posterior |

Los avisos se pueden encolar: el último visible es una pista, no una traza de
crash. Si SDL devuelve un error, se muestran etapa y detalle y la app permanece
viva para leerlos. El hook anterior al runtime no usa heap, stdio ni SDL.

Para una build normal, omitir `APP_DEFINITIONS` y `BUILD_LABEL`: los avisos de
diagnóstico quedan desactivados. La compatibilidad de esta build sigue pendiente
de la prueba en la consola.
