# Portabilidad de eventos de temporizador, ruta postMessage y marcos de acción AWDL a macOS 14.8.9 y 15.8.2

Estudio comparativo y tabla de correspondencias entre macOS 26.7.1 (build 25G241), macOS 14.8.9 (build 23J631) y macOS 15.8.2 (build 24H232) en arquitectura x86_64 para la implementación de soporte AWDL sin firmware dedicado en el controlador RTL8814AU (`RTL8814Native.kext` / `RTL8814AirLayer.kext`).

Las fuentes de verdad base tomadas del repositorio corresponden a:
- `Docs/AWDL_WINDOW_TIMER.md`: eventos 41, 42 y 43, temporizador de franjas/ventanas y estado de sincronismo.
- `Docs/AWDL_STAGE3_EVENT_EMITTERS.md`: matriz de emisores de eventos y cadena de llamadas `postMessage`.
- `Docs/AWDL_AF_TO_DAEMON.md`: camino de recepción de marcos de acción (AF), filtros de entrada y evento 48.
- `Docs/AWDL_PSF_MIF_EMISSION.md`: construcción y entrega de plantillas de sincronismo (PSF) y master indication (MIF/UMI) vía ioctls internos.

Todas las direcciones citadas han sido verificadas como cabezas de instrucción ejecutables (`code && head`) en las colecciones de kernel correspondientes.

---

## 1. Resumen ejecutivo

1. **Estructuras de eventos e ioctls idénticas**: Los formatos de carga útil de los eventos 41 (36 B), 42 (8 B), 43 (40 B), 46 (16 B firmware / 0 B helper), 47 (68 B), 48 (72 B cabecera + trama) y 195 (400 B) son rigurosamente idénticos en las tres versiones del sistema operativo. Asimismo, las estructuras de los ioctls internos 151 (16 B), 149 (56 B), 225 (56 B) y 342 (16 B) mantienen exactamente el mismo diseño de campos.
2. **Divergencia en ranuras virtuales de interfaz**: En macOS 26.7.1 (25G241), `IO80211VirtualInterface::postMessage` se ubica en la ranura 355 (`+0xB18`) y enlaza a `IO80211SkywalkInterface::reportDataPathEvents` en la ranura 356 (`+0xB20`). En macOS 14.8.9 (23J631) y macOS 15.8.2 (24H232), `postMessage` se encuentra desplazado 8 bytes hacia atrás, en la ranura 354 (`+0xB10`), y enlaza a `reportDataPathEvents` en la ranura 355 (`+0xB18`).
3. **Divergencia en ranuras de `IO80211AWDLPeerManager`**:
   - `actionFrameInput`: ranura 71 (`+0x238`) en macOS 26 y macOS 14, pero ranura 70 (`+0x230`) en macOS 15.
   - `reportDataPathEvents`: ranura 83 (`+0x298`) en macOS 26; ranura 81 (`+0x288`) en macOS 14 y macOS 15.
   - `dequeueTxPacket`: ranura 85 (`+0x2A8`) en macOS 26; ranura 84 (`+0x2A0`) en macOS 14; ranura 83 (`+0x298`) en macOS 15.
4. **Desplazamiento crítico en `peerReachability`**: En macOS 26 y macOS 15, el campo `peerReachability` del par (`IO80211AWDLPeer`) se ubica en el desplazamiento `priv+0x78` (`priv = *(peer+0x20)`). En macOS 14 se ubica en `priv+0x70` (diferencia de −8 bytes). La comprobación `peerReachability == 1` requerida para publicar el evento 48 se evalúa en `priv+0x70` en macOS 14.
5. **Cadena `postMessage` en macOS 14**: `IO80211SkywalkInterface::reportDataPathEventsGated` salta directamente a `postMessageSync`, omitiendo la función intermedia `postMessageInternal` que existe en macOS 15 y macOS 26. Adicionalmente, el emisor Broadcom `AppleBCMWLANProximityInterface` en macOS 14 invoca directamente la ranura virtual `+0xB10` de la interfaz en lugar del intermediario `IO80211Controller::postMessage`.

---

## 2. Eventos de temporizador de ventanas y consumo (`AWDL_WINDOW_TIMER.md`)

| Item | 25G241 | 23J631 | 24H232 | Igual (sí/no/?) | Nota |
|---|---|---|---|---|---|
| ID evento inicio ventana disponibilidad (41) | `41` (`0x29`) | `41` (`0x29`) | `41` (`0x29`) | sí | Constante `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`. |
| Carga útil evento 41 | 36 B | 36 B | 36 B | sí | `u16 AwNumber@0`, `u32 syncState@4`, `MAC master@8`, `MAC root@14`, `u32 BE FwAwStartTime@20`, `u32 chanFlags@24`, `u32 chanNum@28`, `u32 winFlags@32`. |
| Consumidor kernel evento 41 | `0xffffff80021dd8f4` | `0xffffff80020ccaaa` | `0xffffff800217adb8` | sí | `IO80211AWDLPeerManager::reportDataPathEvents` (caso 41 / `')'`). |
| Ranura vtable consumidor evento 41 | Ranura 83 (`+0x298`) | Ranura 81 (`+0x288`) | Ranura 81 (`+0x288`) | no | Ranura de `reportDataPathEvents` en `IO80211AWDLPeerManager`. |
| Bandera `inAvailabilityWindow()` | `ivars+772` | `ivars+772` | `ivars+772` | sí | `u8` en `*(_BYTE *)(ivars + 772) = 1` al inicio de ventana. |
| Reinicio de cuenta de extensión en evento 41 | `ivars+408 = 0` | `ivars+408 = 0` | `ivars+408 = 0` | sí | Se pone a 0 al inicio de ventana en las 3 versiones. |
| Marca inicio de host (`mach_absolute_time`) | `ivars+42696` | `ivars+32600` | `ivars+32680` | no | Desplazamiento variable en la estructura privada de ivars. |
| Umbral intervalo mínimo franja 0 (64 ms) | `ivars+66376` | `ivars+51392` | `ivars+52016` | no | Registra «slot 0 start too soon» si el intervalo es inferior. |
| Índice de franja en secuencia (`slot(t)`) | `ivars+16128` | `ivars+16104` | `ivars+16136` | no | Cálculo `min(15, (AwNumber % (step+1)*n) / (step+1))`. |
| ID evento extensión ventana (42) | `42` (`0x2A`) | `42` (`0x2A`) | `42` (`0x2A`) | sí | Constante `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSION`. |
| Carga útil evento 42 | 8 B | 8 B | 8 B | sí | `u32 sn@0` (cuenta extensión), `u32 fwAwStartTime@4`. |
| Consumidor kernel evento 42 | `0xffffff80021dd8f4` | `0xffffff80020ccaaa` | `0xffffff800217adb8` | sí | `IO80211AWDLPeerManager::reportDataPathEvents` (caso 42 / `'*'`). |
| Escritura contador de extensión | `ivars+408 = *a3` | `ivars+408 = *a3` | `ivars+408 = *a3` | sí | `*(_DWORD *)(ivars + 408) = *(DWORD *)a3` en las 3 versiones. |
| Comprobación de extensión en TX | `ivars+408 < 4` | `ivars+408 < 4` | `ivars+408 < 4` | sí | Evaluado en `IO80211AWDLPeerManager::dequeueTxPacket`. |
| Ranura vtable `dequeueTxPacket` | Ranura 85 (`+0x2A8`) | Ranura 84 (`+0x2A0`) | Ranura 83 (`+0x298`) | no | `0xffffff800221675e` (26), `0xffffff8002100f30` (14), `0xffffff80021b0e40` (15). |
| ID evento fin de ventana (43) | `43` (`0x2B`) | `43` (`0x2B`) | `43` (`0x2B`) | sí | Constante `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_END`. |
| Carga útil evento 43 | 40 B | 40 B | 40 B | sí | Estadísticas CCA y uso de canal de firmware (relleno con ceros válido). |
| Consumidor kernel evento 43 | `0xffffff80021dd8f4` | `0xffffff80020ccaaa` | `0xffffff800217adb8` | sí | `IO80211AWDLPeerManager::reportDataPathEvents` (caso 43 / `'+'`). |
| Efecto evento 43: cierre de ventana | `ivars+772 = 0` | `ivars+772 = 0` | `ivars+772 = 0` | sí | Restablece `inAvailabilityWindow() = 0`. |
| Efecto evento 43: bandera secundaria | `ivars+757 = 0` | `ivars+757 = 0` | `ivars+757 = 0` | sí | Pone a cero la bandera de estado de ventana activo. |
| Efecto evento 43: palabra de estado | `ivars+0 = 0` | `ivars+0 = 0` | `ivars+0 = 0` | sí | Pone a cero la palabra en el offset 0 de ivars. |
| Marca fin de ventana (`mach_absolute_time`) | `ivars+16400` | `ivars+16368` | `ivars+16408` | no | Guarda el instante de cierre de la franja. |
| Disparo periódico de elección en evento 43 | `runElection` | `runElection` | `runElection` | sí | Llama a `IO80211AWDLPeerManager::runElection` si `shouldRunElection` es cierto. |

---

## 3. Emisores de eventos y ruta postMessage (`AWDL_STAGE3_EVENT_EMITTERS.md`)

| Item | 25G241 | 23J631 | 24H232 | Igual (sí/no/?) | Nota |
|---|---|---|---|---|---|
| Emisor firmware evento 41 | `0xffffff8001718756` (sitio `0xffffff80017188d1`) | `0xffffff8001681fe6` (sitio `0xffffff800168217e`) | `0xffffff80016a8cd6` (sitio `0xffffff80016a8e51`) | no | 26 y 15 llaman a `IO80211Controller::postMessage`; 14 llama directo a virtual `+0xB10`. |
| Emisor firmware evento 42 | `0xffffff8001718942` (sitio `0xffffff80017189ac`) | `0xffffff80016821f0` (sitio `0xffffff8001682247`) | `0xffffff80016a8ec2` (sitio `0xffffff80016a8f2c`) | no | 26 y 15 llaman a `IO80211Controller::postMessage`; 14 llama directo a virtual `+0xB10`. |
| Emisor firmware evento 43 | `0xffffff80017184b6` (sitio `0xffffff8001718649`) | `0xffffff8001681d50` (sitio `0xffffff8001681ed8`) | `0xffffff80016a8a32` (sitio `0xffffff80016a8bc9`) | no | 26 y 15 llaman a `IO80211Controller::postMessage`; 14 llama directo a virtual `+0xB10`. |
| Emisores firmware evento 46 (4 sitios) | `0xffffff80017189e4`, `0xffffff8001718ad6`, `0xffffff8001718bd4`, `0xffffff8001719cec` | `0xffffff8001682280`, `0xffffff800168236e`, `0xffffff8001682462`, `0xffffff80016842b4` | `0xffffff80016a8f64`, `0xffffff80016a9056`, `0xffffff80016a9154`, `0xffffff80016aa272` | no | 26 y 15 usan glue de controlador; 14 invoca virtual `+0xB10` con `esi = 0x2E`. |
| Carga útil evento 46 firmware | 16 B | 16 B | 16 B | sí | `u32@0`: 1 maestro, 2 esclavo, 3 maestro sin elección, 0 caído. |
| Helper familia evento 46 (`postSyncStateChanged`) | `0xffffff8002332054` (sitio `0xffffff8002332073`) | `0xffffff80021da8bc` (sitio `0xffffff80021da8d2`) | `0xffffff80022a7b2e` (sitio `0xffffff80022a7b44`) | sí | `IO80211VirtualInterface::postSyncStateChanged`, comprueba rol 6 en `ivars+88`, `len = 0`. |
| Helper manager evento 46 (`postSyncStateChanged`) | `0xffffff80021e58b4` (sitio `0xffffff80021e594d`) | `0xffffff80020d3eac` (sitio `0xffffff80020d3ee0`) | `0xffffff80021823a8` (sitio `0xffffff80021823dc`) | sí | `IO80211AWDLPeerManager::postSyncStateChanged`, invoca el helper de interfaz. |
| Emisor evento 47 (`removePeer`) | `0xffffff8002254096` (sitio `0xffffff8002254248`) | `0xffffff800213a1a4` (sitio `0xffffff800213a351`) | `0xffffff80021e9d1c` (sitio `0xffffff80021e9ec9`) | sí | `IO80211PeerManager::removePeer`, `postMessageSync` directa con `esi = 0x2F`. |
| Emisor evento 47 (`cleanUpForPeerRemovedFromNDP`) | `0xffffff8002214cf2` (sitio `0xffffff8002214d6d`) | `0xffffff80020ff5e8` (sitio `0xffffff80020ff65e`) | `0xffffff80021af406` (sitio `0xffffff80021af47c`) | sí | `IO80211AWDLPeerManager`, `postMessageSync` directa con `esi = 0x2F`. |
| Carga útil evento 47 | 68 B | 68 B | 68 B | sí | MAC en `@0..5`, `u32@8 = 1`, resto 0 (notificación de ausencia de peer). |
| Emisor evento 48 (`postAFToP2pd`) | `0xffffff800220fc2c` (sitio `0xffffff800220fce2`) | `0xffffff80020fa752` (sitio `0xffffff80020fa823`) | `0xffffff80021aa66c` (sitio `0xffffff80021aa71c`) | no | 26 llama a ranura virtual `+0xB18`; 14 y 15 llaman a ranura virtual `+0xB10`. |
| Carga útil evento 48 | 72 B + frame | 72 B + frame | 72 B + frame | sí | Cabecera `apple80211_act_frm_hdr` de 72 B concatenada con cuerpo de trama. |
| Emisor evento 195 (`setChannelSequenceUnconditional`) | `0xffffff80021cb69a` (sitio `0xffffff80021cc29b`) | `0xffffff80020bbee6` (sitio `0xffffff80020bc8a5`) | `0xffffff8002169b06` (sitio `0xffffff800216a627`) | no | 26 llama a ranura virtual `+0xB18`; 14 y 15 llaman a ranura virtual `+0xB10`. |
| Carga útil evento 195 | 400 B | 400 B | 400 B | sí | `u32@0 = 1`, secuencia de canales programada. |
| Paso 1 cadena: `VirtualInterface::postMessage` | Ranura 355 (`+0xB18`) `0xffffff8002333192` | Ranura 354 (`+0xB10`) `0xffffff80021db9e4` | Ranura 354 (`+0xB10`) `0xffffff80022a8c6e` | no | Desplazamiento de ranura en vtable de `IO80211VirtualInterface`. |
| Paso 1 sitio llamada a Paso 2 | `0xffffff800233334b` (`call [rax+0B20h]`) | `0xffffff80021dbb1c` (`call [rax+0B18h]`) | `0xffffff80022a8e27` (`call [rax+0B18h]`) | no | Llama al método virtual de `reportDataPathEvents`. |
| Paso 2 cadena: `SkywalkInterface::reportDataPathEvents` | Ranura 356 (`+0xB20`) `0xffffff80022f8dc8` | Ranura 355 (`+0xB18`) `0xffffff80021a3102` | Ranura 355 (`+0xB18`) `0xffffff8002266276` | no | Ranura en vtable de `IO80211SkywalkInterface`. |
| Paso 2 sitio llamada a Paso 3 | `0xffffff80022f8e2a` (`call [r10+850h]`) | `0xffffff80021a3157` (`call [r14+850h]`) | `0xffffff80022662d8` (`call [r10+850h]`) | sí | Invoca `executeCommand` a través de la ranura `+0x850` del controlador. |
| Paso 3 cadena: `IONetworkController::executeCommand` | `0xffffff80026f51e0` | `0xffffff80025156ee` | `0xffffff8002630c98` | sí | Ejecución dentro de la puerta serializada del controlador. |
| Paso 4 cadena: `reportDataPathEventsGated` | `0xffffff80022f8d1a` | `0xffffff80021a306e` | `0xffffff80022661cc` | no | Callback con la puerta del controlador tomada. |
| Paso 4 filtro previo | `call [rax+298h]` (`0xffffff80022f8d98`) | `call [rax+288h]` (`0xffffff80021a30d8`) | `call [rax+288h]` (`0xffffff8002266246`) | no | Desplazamiento de ranura de filtro (`+0x298` en 26, `+0x288` en 14 y 15). |
| Paso 4 enlace a siguiente paso | `call postMessageInternal` (`0xffffff80022f8db2`) | `call postMessageSync` (`0xffffff80021a30f1`) | `call postMessageInternal` (`0xffffff8002266260`) | no | macOS 14 omite `postMessageInternal` y llama directamente a `postMessageSync`. |
| Paso 5 cadena: `postMessageInternal` | `0xffffff80022f8600` (sitio `0xffffff80022f862a`) | Ausente (no implementado) | `0xffffff8002265aba` (sitio `0xffffff8002265ae4`) | no | Enlace a `postMessageSync` con flag 0. En macOS 14 la función no existe. |
| Paso 6 cadena: `postMessageSync` | `0xffffff80022f8abc` | `0xffffff80021a2ce2` | `0xffffff8002265f6e` | sí | Función de entrega síncrona a los pipes de usuarios. |
| Paso 6 verificación de pipe (`isIOUCPipeOpened`) | `call [rax+0DE0h]` (`0xffffff80022f8aed`) | `call [rax+0DE0h]` (`0xffffff80021a2d23`) | `call [rax+0DE0h]` (`0xffffff8002265f9f`) | sí | Ranura `+0xDE0` en la vtable de `IO80211SkywalkInterface` en las 3 versiones. |
| Paso 6 llamada a `postMessageIOUC` | `call [r9+0DD8h]` (`0xffffff80022f8b15`) | `call [r9+0DD8h]` (`0xffffff80021a2d5f`) | `call [r9+0DD8h]` (`0xffffff8002265fc7`) | sí | Ranura `+0xDD8` en la vtable de `IO80211SkywalkInterface` en las 3 versiones. |
| Paso 7 y 8: filtro y copia `writePipe` | `0xffffff80022a7e7c` | `0xffffff800217b97c` | `0xffffff8002239404` | sí | `IO80211IOUCEventPipe::writePipe` (en macOS 14 no recibe el parámetro `int &`). |

---

## 4. Recepción de Action Frames hacia daemon (`AWDL_AF_TO_DAEMON.md`)

| Item | 25G241 | 23J631 | 24H232 | Igual (sí/no/?) | Nota |
|---|---|---|---|---|---|
| Ranura vtable `actionFrameInput` | Ranura 71 (`+0x238`) | Ranura 71 (`+0x238`) | Ranura 70 (`+0x230`) | no | `0xffffff800220e1ae` (26), `0xffffff80020f8a78` (14), `0xffffff80021a8bf4` (15). |
| Tamaño de elemento de anillo AF | 104 B (`0x68`) | 104 B (`0x68`) | 104 B (`0x68`) | sí | Entrada en el buffer circular de recepción. |
| Ivar puntero base del anillo | `ivars+16840` | `ivars+16808` | `ivars+16848` | no | Desplazamientos del anillo en `IO80211AWDLPeerManager`. |
| Ivar cabecera del anillo (`head`) | `ivars+16848` | `ivars+16816` | `ivars+16856` | no | Índice de escritura del productor. |
| Ivar cola del anillo (`tail`) | `ivars+16852` | `ivars+16820` | `ivars+16860` | no | Índice de lectura del consumidor (hilo de proceso). |
| Ivar tamaño del anillo (`size`) | `ivars+16856` | `ivars+16824` | `ivars+16864` | no | Capacidad máxima de entradas. |
| Ivar máscara del anillo (`mask`) | `ivars+16860` | `ivars+16828` | `ivars+16868` | no | Máscara de módulo (`size - 1`). |
| Ivar bandera de desbordamiento | `ivars+16868` | `ivars+16836` | `ivars+16876` | no | Se activa en 1 al llenarse la cola de AFs. |
| Ivar bandera AWDL habilitado (`AWDLOn`) | `ivars+16096` bit 1 | `ivars+16072` bit 1 | `ivars+16104` bit 1 | no | Comprobado antes de encolar en el anillo. |
| Función de proceso `actionFrameProcessInput` | `0xffffff800220e8b0` | `0xffffff80020f9318` | `0xffffff80021a9302` | sí | Núcleo de parseo y filtrado de marcos de acción. |
| Filtro de validez AF (`isValidAF`) | `0xffffff800220fa64` | `0xffffff80020fa570` | `0xffffff80021aa4b0` | sí | Comprueba `hdrLen <= 0x47`, cursor válido y OUI. |
| Comprobación OUI (`actionFrameHasCorrectOUI`) | `0xffffff8002210200` | `0xffffff80020fad74` | `0xffffff80021aac34` | sí | `00:17:F2`, tipo 8, versión != 0 en las 3 versiones. |
| Filtro RSSI par conocido (`hdr+0x2C`) | `0xffffff800220ecb4` | `0xffffff80020f977e` | `0xffffff80021a9715` | sí | `cmp eax, [rcx+281Ch]` (`ivars+10268`, −70/−78 dBm). |
| Filtro RSSI par desconocido (`hdr+0x2C`) | `0xffffff800220ecd6` | `0xffffff80020f97c0` | `0xffffff80021a9737` | sí | `cmp eax, [rcx+281Ch]` (`ivars+10268`, −70/−78 dBm). |
| Puerta de error de contexto (`ctx+0x2C`) | `0xffffff800220ed36` | `0xffffff80020f9811` | `0xffffff80021a9797` | sí | `cmp dword ptr [r15+2Ch], 0`; si != 0 descarta sin evento. |
| Campo `peerReachability` del par | `priv+0x78` | `priv+0x70` | `priv+0x78` | no | **Desplazamiento −8 bytes en macOS 14** (`priv = *(peer+0x20)`). |
| Comprobación `peerReachability == 1` | `0xffffff800220f930` (`[priv+78h]`) | `0xffffff80020fa42e` (`[priv+70h]`) | `0xffffff80021aa36d` (`[priv+78h]`) | no | Condición obligatoria previa a la llamada de publicación. |
| Sitio llamada a `postAFToP2pd` | `0xffffff800220f94c` | `0xffffff80020fa445` | `0xffffff80021aa384` | sí | Única llamada a `postAFToP2pd` en todo el kernelcache. |
| Cabeza de función `postAFToP2pd` | `0xffffff800220fc2c` | `0xffffff80020fa752` | `0xffffff80021aa66c` | sí | Constructor de buffer y publicador del evento 48. |
| Publicación virtual en `postAFToP2pd` | `0xffffff800220fce2` (`+0B18h`) | `0xffffff80020fa823` (`+0B10h`) | `0xffffff80021aa71c` (`+0B10h`) | no | 26 llama a ranura 355; 14 y 15 llaman a ranura 354. |

---

## 5. Emisión de plantillas PSF y MIF (`AWDL_PSF_MIF_EMISSION.md`)

| Item | 25G241 | 23J631 | 24H232 | Igual (sí/no/?) | Nota |
|---|---|---|---|---|---|
| Constructor plantilla PSF (`updateSyncFrameTemplate`) | `0xffffff80021dc5b2` | `0xffffff80020cb764` | `0xffffff8002179b0e` | sí | Construye la plantilla periódica del marco de sincronismo. |
| Inicialización buffer PSF (512 B, `0xAA`) | `0xffffff80021dc5f1` (sitio `0xffffff80021dc5f6`) | `0xffffff80020cb7a3` (sitio `0xffffff80020cb7a8`) | `0xffffff8002179b4d` (sitio `0xffffff8002179b52`) | sí | `secure_memset(..., 0xAA, 0x200)` en las 3 versiones. |
| Carga de ioctl 151 (`0x97802869C8`) | `0xffffff80021dc97b` | `0xffffff80020cbb98` | `0xffffff8002179ed1` | sí | `mov rax, 97802869C8h` en las 3 versiones. |
| Llamada `ioctl_internal` en 151 | `0xffffff80021dc996` | `0xffffff80020cbbb3` | `0xffffff8002179eec` | sí | Entrega la estructura `awdl_sync_frame_template_req` (16 B). |
| TLVs apilados en plantilla PSF | 11 TLVs (6, 24, 12, 7, 30, RTG, 17, 21, 20, 23, 33) | 11 TLVs (6, 24, 12, 7, 30, RTG, 17 inlined, 21, 20, 23, 33) | 11 TLVs (6, 24, 12, 7, 30, RTG, 17, 21, 20, 23, 33) | sí | TLVs 4, 5 y 18 ausentes (deben ser añadidos por el driver). |
| Constructor plantilla MI (`buildMasterIndicationTemplate`) | `0xffffff8002211b42` | `0xffffff80020fc59c` | `0xffffff80021ac4c6` | sí | Construye la plantilla de Master Indication (1200 B). |
| Ivar buffer plantilla MI (1200 B) | `ivars+9048` | `ivars+9048` | `ivars+9048` | sí | Idéntico en las 3 versiones. |
| Ivar longitud plantilla MI (`template_len`) | `ivars+10248` | `ivars+10248` | `ivars+10248` | sí | Idéntico en las 3 versiones. |
| Puntero descriptor ARPA IPv6 (`_rarpv6Service`) | `ivars+42448` (`0xA5D0`) | `ivars+32360` (`0x7E68`) | `ivars+32432` (`0x7EB0`) | no | Desplazamiento variable de `_rarpv6Service` en ivars. |
| Escritura tipo TLV 16 ARPA en MI | `0xffffff800221202d` (`mov byte [rsi-4], 10h`) | `0xffffff80020fca8f` (`mov byte [rsi-4], 10h`) | `0xffffff80021ac98d` (`mov byte [rsi-4], 10h`) | sí | Inserción directa de cabecera de TLV 16 en la plantilla MI. |
| Copia de datos TLV 16 ARPA (`copyDataOnly`) | `0xffffff8002212053` | `0xffffff80020fcab3` | `0xffffff80021ac9b1` | sí | `IO80211ServiceRequestDescriptor::copyDataOnly(uchar *, ulong)`. |
| Emisor UMI unicast (`sendUnicastMI`) | `0xffffff8002213c0a` | `0xffffff80020fe74c` | `0xffffff80021ae404` | sí | Genera petición de transmisión unicast para un par. |
| Carga de ioctl 149 (`0x95802869C8`) en UMI | `0xffffff8002213f22` | `0xffffff80020fe98a` | `0xffffff80021ae6eb` | sí | `mov rax, 95802869C8h` en las 3 versiones. |
| Llamada `ioctl_internal` en UMI | `0xffffff8002213f32` | `0xffffff80020fe99a` | `0xffffff80021ae6fb` | sí | Estructura `awdl_mi_request` de 56 B con MAC de par. |
| Emisor MI broadcast (`updatePrimaryPayloadMI`) | `0xffffff80021fb36a` | `0xffffff80020e7806` | `0xffffff8002196df2` | sí | Genera transmisión inmediata (149) o periódica de firmware (225). |
| Carga de ioctl 149 ("send now") en MI primario | `0xffffff80021fb533` (`mov [var+4], 95h`) | `0xffffff80020e79c0` (`mov [var+4], 95h`) | `0xffffff8002196fb5` (`mov [var+4], 95h`) | sí | Inyección inmediata de MIF en la ventana actual. |
| Carga de ioctl 225 ("template to fw") en MI primario | `0xffffff80021fb546` (`mov [var+4], 0E1h`) | `0xffffff80020e79cc` (`mov [var+4], 0E1h`) | `0xffffff8002196fc8` (`mov [var+4], 0E1h`) | sí | Programación periódica por ventana de disponibilidad. |
| Emisor MI secundario (`updateSecondaryPayloadMI`) | `0xffffff80022141ae` | `0xffffff80020feb3e` | `0xffffff80021ae978` | sí | Genera plantilla secundaria para operación SDB. |
| Carga de ioctl 342 (`0x156802869C8`) en MI secundario | `0xffffff8002214304` | `0xffffff80020fec94` | `0xffffff80021aeace` | sí | `mov rax, 156802869C8h` en las 3 versiones. |
| Llamada `ioctl_internal` en MI secundario | `0xffffff8002214314` | `0xffffff80020feca4` | `0xffffff80021aeade` | sí | Estructura `awdl_sec_mi_request` de 16 B sobre buffer compartido. |

---

## 6. Qué cambia para una capa driver que deba soportar macOS 14, 15 y 26

Para que la capa de compatibilidad AWDL en `RTL8814Native.kext` / `RTL8814AirLayer.kext` funcione unificada en macOS 14.8.9, 15.8.2 y 26.7.1, se deben resolver dinámicamente las siguientes divergencias:

### 6.1 Despacho dinámico de métodos virtuales
Las ranuras de vtable no son estables entre las tres ramas del sistema operativo. La capa driver no debe compilar llamadas con offsets fijos ni macros de vtable fijas, sino resolver las ranuras según la versión del kernel en ejecución (`version_major`):

1. **`postMessage` en `IO80211VirtualInterface`**:
   - macOS 14 (Darwin 23) y macOS 15 (Darwin 24): ranura 354 (`+0x0B10`).
   - macOS 26 (Darwin 25): ranura 355 (`+0x0B18`).
2. **`actionFrameInput` en `IO80211AWDLPeerManager`**:
   - macOS 14 (Darwin 23) y macOS 26 (Darwin 25): ranura 71 (`+0x0238`).
   - macOS 15 (Darwin 24): ranura 70 (`+0x0230`).
3. **`reportDataPathEvents` en `IO80211AWDLPeerManager`**:
   - macOS 14 (Darwin 23) y macOS 15 (Darwin 24): ranura 81 (`+0x0288`).
   - macOS 26 (Darwin 25): ranura 83 (`+0x0298`).
4. **`dequeueTxPacket` en `IO80211AWDLPeerManager`**:
   - macOS 14 (Darwin 23): ranura 84 (`+0x02A0`).
   - macOS 15 (Darwin 24): ranura 83 (`+0x0298`).
   - macOS 26 (Darwin 25): ranura 85 (`+0x02A8`).

### 6.2 Offset dinámico de `peerReachability`
En `IO80211AWDLPeer`, el campo `peerReachability` determina si los marcos de acción recibidos son publicados a `wifip2pd` a través del evento 48. Si el driver necesita inspeccionar o validar el estado del par a partir de `priv = *(peer+0x20)`:
- En macOS 14 (Darwin 23): `*(uint32_t*)(priv + 0x70)`.
- En macOS 15 (Darwin 24) y macOS 26 (Darwin 25): `*(uint32_t*)(priv + 0x78)`.

### 6.3 Offsets de variables de instancia en `IO80211AWDLPeerManager`
El tamaño y la disposición de ivars privadas de `IO80211AWDLPeerManager` varían notablemente:
- **Anillo de recepción de Action Frames (104 B por entrada)**:
  - Base puntero: `+16808` (14), `+16848` (15), `+16840` (26).
  - Head: `+16816` (14), `+16856` (15), `+16848` (26).
  - Tail: `+16820` (14), `+16860` (15), `+16852` (26).
  - Capacidad / Size: `+16824` (14), `+16864` (15), `+16856` (26).
  - Máscara: `+16828` (14), `+16868` (15), `+16860` (26).
  - Flag de desbordamiento: `+16836` (14), `+16876` (15), `+16868` (26).
- **Bandera `AWDLOn`**: `+16072` bit 1 (14), `+16104` bit 1 (15), `+16096` bit 1 (26).
- **Descriptor ARPA IPv6 (`_rarpv6Service`)**: `+32360` (14), `+32432` (15), `+42448` (26).
- **Marca de tiempo de inicio de host**: `+32600` (14), `+32680` (15), `+42696` (26).
- **Umbral gap franja 0**: `+51392` (14), `+52016` (15), `+66376` (26).
- **Contador de extensión de ventana**: invariante en `ivars+408` en las tres versiones.
- **Bandera `inAvailabilityWindow`**: invariante en `ivars+772` en las tres versiones.

### 6.4 Emisión de eventos y ruta síncrona
En macOS 14 no existe `IO80211SkywalkInterface::postMessageInternal`. El método `reportDataPathEventsGated` salta directamente a `postMessageSync`. Si la capa driver implementa inyección o simulación de eventos mediante llamadas virtuales sobre la interfaz:
- Invocar la ranura virtual de `postMessage` (`+0xB10` en 14/15, `+0xB18` en 26) garantiza que en todas las versiones se tome la compuerta del controlador (`IONetworkController::executeCommand` en `+0x850`) y se complete la entrega síncrona al pipe de eventos de usuario sin depender de símbolos no exportados.

---

## 7. Puntos abiertos

1. `?` **Comportamiento en caliente de la compuerta `executeCommand`**: En las tres versiones, el paso 3 de la cadena `postMessage` atraviesa `IONetworkController::executeCommand`, adquiriendo la compuerta del controlador. Resta confirmar experimentalmente en hardware si invocar este camino desde una rutina de atención de interrupción diferida (workloop secundario o callback USB) introduce latencias de programación de eventos 41/43 incompatibles con la precisión requerida (≤ 3 TU).
2. `?` **Uso de TLV 17 inlined en macOS 14.8.9**: En `updateSyncFrameTemplate`, macOS 14 codifica la construcción del TLV 17 contenedor 802.11 de manera completamente inlined en lugar de invocar `buildIEEE80211CntnrTlv` como hacen macOS 15 y 26. Resta validar si algún campo interno del sub-TLV varía al operar en modo SDB real.
3. `?` **Consumo de eventos 41/42/43 por componentes auxiliares**: En ninguna de las tres versiones analizadas el daemon `wifip2pd` consume los eventos 41, 42 o 43. No se ha examinado si daemons complementarios como `wifivelocityd` o `airportd` en macOS 14/15 se suscriben pasivamente a estos eventos para telemetría.
