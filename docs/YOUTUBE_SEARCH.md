# Búsqueda con mando: piloto DIAG40

Esta fase permite buscar desde Tube5 y pedir al PC que prepare un resultado.
**Sigue necesitando el puente temporal. No es búsqueda/reproducción autónoma**
y no satisface aún el requisito de lanzamiento sin PC.

## Prueba en consola

Con Tube5 cerrada, copiar la carpeta completa `dist/PPSA99999/` al destino
habitual. Incluye `console-token.txt`, necesario para esta build emparejada.
No compartir esta carpeta ni su ZIP públicamente: contienen una clave privada
de desarrollo. El código público no incluye la clave, medios ni cachés.

1. Abrir Biblioteca LAN y pulsar Triángulo.
2. Cruceta: mover el cursor del teclado. X: escribir. ESP: espacio.
   Options: borrar el último carácter. Máximo 48 caracteres ASCII.
3. Escribir `VERITASIUM ELECTRICITY` y pulsar Triángulo para buscar.
4. Seleccionar `The Biggest Misconception About Electricity` con cruceta y X.
   Ya está en nuestra biblioteca: reutiliza el medio, sin otra descarga.
5. Al terminar la preparación, vuelve a la biblioteca con el vídeo seleccionado.
   X reproduce; controles y SponsorBlock son los existentes.

Triángulo desde resultados vuelve al teclado. Círculo desde resultados vuelve
al teclado; desde teclado/espera vuelve a la biblioteca. Cancelar la espera
en consola **no cancela** una preparación ya aceptada por el PC.
Probar además búsqueda sin resultados, borrar texto y salir mientras busca.
La validación física de DIAG40 en PS5 está pendiente.

## Servidor de desarrollo

```sh
python3 tools/http-test-server.py --bind 192.168.8.176 --port 8080 \
  --youtube-control --console-ip 192.168.8.200 --sponsorblock \
  --clip .local/youtube/qf31PkeESoU/resolution-1080p-600s.mp4
```

`--console-ip` es opt-in y requiere `--youtube-control` y un bind IPv4 concreto.
Se reutiliza el puerto LAN 8080; 8081 sigue reservado al control loopback del PC.
Arrancar el servidor antes de compilar crea `.local/bridge/console-token`, que
la build copia a `/app0/console-token.txt`. Persiste entre reinicios del servidor.
Para revocarlo, retirar la clave local con el servidor detenido, reiniciar y
recompilar/copiar; una consola con la clave anterior ya no estará autorizada.
El fichero es 0600 en el PC; la carpeta de consola conserva los permisos
compatibles con el loader del prototipo. HTTP LAN no cifra la clave: usar solo
en la red privada de pruebas. Este emparejamiento no es un sistema de cuentas.

## Límites y separación del proveedor

`YouTubeSearch` entrega a la interfaz estructuras acotadas y estados; el teclado
y listado no interpretan JSON de YouTube ni URLs firmadas. El transporte LAN
es sustituible, pero no se ha implementado todavía un proveedor autónomo.
El proveedor temporal usa yt-dlp con metadatos planos: buscar no descarga medios,
no usa cookies ni cuentas. Devuelve hasta ocho vídeos de 20 s a una hora;
excluye directos y duraciones desconocidas. No hay paginación, miniaturas,
orden personalizado ni selección de calidad: preparar usa el perfil completo
360p existente. La biblioteca sigue limitada a ocho entradas (dos base y seis
dinámicas); una biblioteca llena rechaza nuevas preparaciones.

Protocolo TSV versionado, respuestas de hasta 2048 B:

- GET `/tube5/search?q=...`: inicia/consulta búsqueda, estados searching/ready/failed.
- POST `/tube5/prepare?id=...`, cuerpo vacío: inicia/reutiliza preparación.
- GET `/tube5/job`: estado de la única preparación activa.

Las rutas requieren IP permitida, Host exacto y Bearer privado; rechazan Origin
de navegador, IDs inválidos y cuerpos de preparación. No registran consultas ni
credenciales en el log HTTP. La consulta no pasa por un shell. Una búsqueda
simultánea distinta devuelve ocupado; resultados se cachean cinco minutos y
fallos cinco segundos. El subprocess está limitado a 45 s y 128 KiB.
La app usa un hilo auxiliar con deadline de ocho segundos por petición; sondea
cada segundo, con límite total de 60 s para búsqueda y unos 42 min para preparar.
Las preparaciones siguen aplicando los límites y validación del puente existente.

Pruebas host: parser/teclado, GET y POST del cliente C++ contra HTTP real local,
autenticación/IP/Host/Origin, persistencia de clave, validación de entradas,
filtros y caché, fallo genérico, cancelación, timeout y respuesta sobredimensionada.
La suite del player conserva reproducción, seeks, SponsorBlock y pruebas de
fallos HTTP con sanitizadores. La búsqueda real `VERITASIUM ELECTRICITY`
devuelve el vídeo de prueba en primer lugar en la comprobación del 11/10/2026.
Estas pruebas no equivalen a ejecutar la build nativa en PS5.
