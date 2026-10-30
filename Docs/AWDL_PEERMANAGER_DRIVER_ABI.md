# Contrato binario entre un driver AWDL y IO80211AWDLPeerManager

Este documento fija, byte a byte, la interfaz que un driver de terceros debe implementar para que
`IO80211AWDLPeerManager` (IO80211Family, macOS 26, Darwin 25.6.0) funcione como gestor de protocolo AWDL sin
firmware AWDL debajo. Complementa `AWDL_PEERMANAGER_SOFTWARE.md` (qué calcula el manager en
software) y `AWDL_EVENTS.md` (canales de eventos hacia userspace); aquí sólo se describe el
intercambio driver↔manager: qué bytes se pasan, en qué orden, quién los libera y qué comprueba el
manager en cada dirección.

Fuente: kernelcache `BootKernelExtensions.kc` (macOS 26, Darwin 25.6.0, md5 `f489ff8be80e6013a1bf47561dcaef45`),
copia del IDB en `Build/ida_awdl/task_awdl_peermgr/kc.i64` y una copia propia en
`Build/ida_awdl/task_awdl_pm_abi/kc.i64` para las funciones que faltaban
(`reportDataPathEvents`, `ioctl_internal`, `bpfTapInput`, `handleActionFrame_rx`,
`routeToP2PInterface`, constructores de TLV, `dequeueTxPacket` base). Todas las direcciones son VA
del kernelcache y fueron leídas en el IDB. Los offsets de ivars y de estructuras pertenecen a esta
build: no son ABI ni están congelados por ninguna cabecera publicada. Lo no confirmado se marca `?`.

Convención de ranuras: el offset de una celda se da como desplazamiento desde el `vptr`
(`celda = offset / 8`, contando desde el destructor en la celda 0). El `vptr` apunta 0x10 B después
del inicio del símbolo `__ZTV`, que guarda dos entradas de cabecera; la vtable citada
(`0xffffff800245a868`) es ya la dirección del `vptr`.

---

## 0. Mapa de direcciones

| Símbolo | Dirección | Papel en el contrato |
|---|---|---|
| `__ZTV22IO80211AWDLPeerManager` (vtable) | `0xffffff800245a868` | tabla de la que cuelgan todas las celdas citadas |
| `IO80211AWDLPeerManager::actionFrameInput` | `0xffffff800220e1ae` | celda 71, `+0x238`: entrada de action frames |
| `IO80211AWDLPeerManager::actionFrameProcessInput` | `0xffffff800220e8b0` | parseo y alta de peers |
| `IO80211AWDLPeerManager::isValidAF` | `0xffffff800220fa64` | validación previa (longitud + OUI) |
| `IO80211AWDLPeerManager::actionFrameHasCorrectOUI` | `0xffffff8002210200` | comprobación de OUI sobre el buffer |
| `IO80211AWDLPeerManager::actionFrameProcessThread` | `0xffffff80021c3ebc` | drenaje del anillo interno |
| `IO80211AWDLPeerManager::reportDataPathEvents` | `0xffffff80021dd8f4` | celda 83, `+0x298`: eventos del driver |
| `IO80211AWDLPeerManager::monitorTimeline` | `0xffffff80021e11ca` | valida el evento 41 |
| `IO80211AWDLPeerManager::logAWStart` | `0xffffff80021e1b42` | registro del evento 41 |
| `IO80211AWDLPeerManager::topMasterChanged` | `0xffffff80021e3c08` | reacción a cambio de top master |
| `IO80211AWDLPeerManager::runElection` | `0xffffff80021e3e7c` | elección disparada por el evento 43 |
| `IO80211AWDLPeerManager::dequeueTxPacket` | `0xffffff800221675e` | celda 85, `+0x2A8`: tirón de TX |
| `IO80211AWDLPeerManager::getUnicastPackets` | `0xffffff8002216a4e` | celda 46, `+0x170` |
| `IO80211AWDLPeerManager::getMulticastPackets` | `0xffffff8002216a2e` | celda 45, `+0x168` |
| `IO80211PeerManager::dequeueTxPacket` | `0xffffff8002250554` | implementación base reutilizada |
| `IO80211PeerManager::getUnicastPackets` | `0xffffff800224ffdc` | implementación base |
| `IO80211PeerManager::getMulticastPackets` | `0xffffff800224fe4e` | implementación base |
| `IO80211PeerManager::ioctl_internal` | `0xffffff80022524e8` | emisión de ioctls internos |
| `IO80211PeerManager::ioctl_internal_gated` | `0xffffff800225238a` | decodifica `ioctl_internal_req` |
| `IO80211VirtualInterface::bpfTapInput(IO80211Buffer*,...)` | `0xffffff8002331070` | **vía real de inyección de AF** |
| `IO80211VirtualInterface::dequeueTxPackets` | `0xffffff80023313b2` | **vía real del tirón de TX** |
| `IO80211InfraInterface::routeToP2PInterface` | `0xffffff800236364e` | reenvío de eventos al manager |
| `IO80211SkywalkInterface::reportDataPathEventsGated` | `0xffffff80022f8d1a` | reenvío de eventos al manager |
| `AppleBCMWLANProximityInterface::handleActionFrame_rx` | `0xffffff800171795c` | constructor del header en Broadcom |
| `IO80211AWDLPeerManager::enableAWDL` | `0xffffff80021dcf58` | lectura `awdl_cap` y 117 |
| `IO80211AWDLPeerManager::setSyncFramePeriod` | `0xffffff80021ed024` | ioctl 116 |
| `IO80211AWDLPeerManager::updateSyncFrameTemplate` | `0xffffff80021dc5b2` | ioctl 151 |
| `IO80211AWDLPeerManager::programChipWithNewMaster` | `0xffffff80021ec4b6` | ioctls 229, 134, 155 |
| `IO80211AWDLPeerManager::initFWElectionParams` | `0xffffff80021ed200` | ioctl 155 al habilitar |
| `IO80211AWDLPeerManager::setChannelSequenceUnconditional` | `0xffffff80021cb69a` | ioctl 129 |
| `IO80211AWDLPeerManager::programMasterChannels` | `0xffffff80021d494a` | ioctls 132 y 205 |
| `IO80211AWDLPeerManager::setAFTXMode` | `0xffffff80021f7d06` | ioctl 208 |
| `IO80211AWDLPeerManager::sendUnicastMI` | `0xffffff8002213c0a` | ioctl 149 |
| `IO80211AWDLPeerManager::buildServiceParamTlv` | `0xffffff80022118a6` | TLV 6 de la plantilla |
| `IO80211AWDLPeerManager::buildAWDLElectionParamsVer2` | `0xffffff8002210a3c` | TLV 24 de la plantilla |
| `IO80211AWDLPeerManager::buildSyncTreeTlv` | `0xffffff80022112fe` | TLV 20 de la plantilla |
| `IO80211AWDLPeerManager::cleanUpForPeerRemovedFromNDP` | `0xffffff8002214cf2` | limpieza de peer (no por evento) |

---

## 1. `actionFrameInput`: inyección de action frames AWDL

### 1.1 Firma y convención de llamada

```c
// celda 71 de la vtable 0xffffff800245a868, offset +0x238
int IO80211AWDLPeerManager::actionFrameInput(IO80211Buffer *buf,   // rsi
                                            uint32_t dlt,          // edx
                                            void *hdr,             // rcx  (apple80211_act_frm_hdr)
                                            size_t hdrLen);        // r8
```

Comprobado en `0xffffff800220e1ae`: la función **no valida nada al entrar** (ni `buf`, ni `hdr`, ni
`hdrLen`); en todas las rutas devuelve 0. La primera comprobación real es la copia del header al
anillo interno: si `hdrLen >= 0x49` se ejecuta una instrucción `ud1` deliberada (misma pauta en
`0xffffff800220e1ae` y en `actionFrameProcessThread` `0xffffff80021c3ebc`), es decir, **un header de
más de 72 B provoca un trap del kernel**. El límite es exactamente
`sizeof(struct apple80211_act_frm_hdr) == 0x48`.

`dlt` se propaga sin interpretar hasta `IO80211VirtualInterface::sendToBpfTap`; el valor que usa el
camino de Broadcom es 12 (`0xC`, DLT IEEE802.11). El manager no lo compara.

### 1.2 `IO80211Buffer`

`IO80211Buffer` es un `OSObject` de 24 B (`OSObject_typed_operator_new(&IO80211Buffer_ktv, 24)`,
vtable `0xffffff8002473e90`). Layout verificado en `initWithOptions`
(`0xffffff800233b8a2`), `setLength` (`0xffffff800233bcbe`), `getIndex` (`0xffffff800233bcf6`) y
`writeBytes` (`0xffffff800233bc74`):

```c
struct IO80211Buffer {              // 24 B
    void        **vptr;             // +0x00
    uint32_t      retainCount;      // +0x08 (OSObject)
    uint32_t      pad;              // +0x0c
    struct io80211_buffer_desc *desc; // +0x10
};

struct io80211_buffer_desc {        // kalloc_type_view_106
    uint64_t                unknown0;  // +0x00 ?
    void                   *slab;      // +0x10
    struct IO80211Buffer   *owner;     // +0x18
    uint32_t                length;    // +0x20
    uint32_t                index;     // +0x24
    /* tamaño total ? */
};
```

El manager **nunca lee esos campos**: usa exclusivamente la vtable. Celdas relevantes (offset desde
`vptr`, leídas en la vtable de `0xffffff8002473e90`):

| Offset | Método | Uso en el contrato |
|---:|---|---|
| `+0x118` | `returnBuffer` | devuelve el buffer a su pool; lo llama el manager al terminar |
| `+0x130` | `getLength` | `isValidAF` lo usa en el mensaje de error; `actionFrameProcessInput` lo llama |
| `+0x138` | `setLength` | lo usa el productor (Broadcom) antes de inyectar |
| `+0x140` | `getCapacity` | no usado por el manager |
| `+0x148` | `getCursor` (no const) | el productor obtiene el cursor de destino |
| `+0x150` | `getCursor` (const) | `isValidAF` obtiene el cursor de origen |
| `+0x158` | `getBytesNoCopy` | `actionFrameProcessInput` obtiene el puntero base |
| `+0x178` | `writeBytes` | no usado por el manager |

Un *cursor* es un par de 16 B copiado en el stack del llamante:

```c
struct IO80211BufferCursor {
    const void *ptr;   // +0x00
    uint64_t    len;   // +0x08
};
```

`isValidAF` (`0xffffff800220fa64`) llama a `+0x150` con un destino de 16 B y exige que ambos campos
sean distintos de cero; `actionFrameHasCorrectOUI` (`0xffffff8002210200`) exige además `len >= 0x10`.

**Propiedad**: `actionFrameInput` consume el buffer. En la ruta con hilo propio lo guarda en el
anillo y `actionFrameProcessThread` (`0xffffff80021c3ebc`) llama a `returnBuffer` (`+0x118`) al
terminar; en las rutas de descarte lo devuelve él mismo antes de retornar. El llamante no debe
tocar el buffer después de la llamada ni liberarlo.

### 1.3 `struct apple80211_act_frm_hdr` (72 B)

Estructura **empaquetada** (hay escrituras a mitad de palabra en `handleActionFrame_rx`). Campos
leídos por el manager y campos escritos por Broadcom:

| Offset | Tipo | Campo | Quién lo lee/escribe |
|---:|---|---|---|
| `+0x00` | `uint8_t[6]` | MAC de origen del AF | escrito en `0xffffff800171795c` (`*(u32*)(ev+24)`, `*(u16*)(ev+28)`); leído en `0xffffff800220e8b0` (`findPeer`, `addAWDLPeer`) y comparado con el parent en `+37237` |
| `+0x06` | `uint16_t` | sin uso (`?`) | queda a cero |
| `+0x08` | `uint8_t[4]` | sin uso (`?`) | queda a cero |
| `+0x0c` | `apple80211_channel` (12 B) | canal del AF | escrito con `ChanSpecConvToApple80211Channel` en `0xffffff800171795c`; no leído por el manager |
| `+0x18` | `uint8_t[16]` | ceros | — |
| `+0x28` | `uint32_t` | tasa de RX (`rate` del evento) | escrito en `0xffffff800171795c`; no leído por el manager |
| `+0x2c` | `int32_t` | **RSSI del AF en dBm** (`rssi` del evento) | leído en `actionFrameProcessInput` (`mov eax, [r15+2Ch]` en `0xffffff800220ecac` y `0xffffff800220ecce`) y comparado **con signo** con el umbral RSSI de borde `ivars+10268` |
| `+0x30` | `uint32_t` | `mactime` del evento, µs `?` | escrito en `0xffffff800171795c`; no leído por el manager |
| `+0x34` | `uint8_t[10]` | ceros | — |
| `+0x40` | `uint32_t` | `1` (sólo versión 2) | escrito en `0xffffff800171795c`; no leído |
| `+0x44` | `uint8_t[4]` | copia de 4 B del evento (sólo v2) | escrito en `0xffffff800171795c`; no leído |

Notas:

- El `+0x2c` es el RSSI, no una marca temporal (corrección de la versión anterior de esta nota). Los
  tres valores de `+0x28/+0x2c/+0x30` salen de `struct brcmf_rx_mgmt_data` (`version`, `chanspec`,
  `rssi`, `mactime`, `rate`, todos big-endian), que sigue a la cabecera de evento de 48 B
  (`brcmf_event_msg_be`). Por eso en v1 `rssi` está en `ev+52`, `mactime` en `ev+56` y `rate` en
  `ev+60` (definiciones públicas de brcmfmac, `fwil_types.h` y `fweh.h`; que el firmware de Apple use
  la misma estructura es coherente con los offsets, `?`). Broadcom los guarda ya en orden de host
  (`_byteswap_ulong`).
- `ivars+10268` es el **umbral RSSI de borde del firmware**: `initParamsToDefaults` lo pone a −70
  (`mov dword ptr [rcx+281Ch], 0FFFFFFBAh` en `0xffffff80021c7cdf`), lo cambian
  `determineAndSetEdgeRssiThreshold` y `setAwdlElectionRssiThresholds`, lo devuelve `getEdgeRssi`, y
  `printState` lo imprime como «Firmware edge RSSI threshold». En `actionFrameProcessInput`, con un
  par conocido, `cmp eax, [rcx+281Ch]` / `jge` (`0xffffff800220ecb4`) deja pasar el AF si el RSSI
  llega al umbral; si no, se libera el par y se descarta el frame. Con un emisor desconocido,
  `cmp`/`jl` (`0xffffff800220ecd6`) descarta el AF sin llamar a `addAWDLPeer`. Un driver que inyecte
  con `+0x2c = 0` pasa siempre el filtro; debe poner el RSSI real del frame para que el manager
  ignore pares lejanos.
- El manager **no** obtiene el cuerpo del frame de este header: el cuerpo viaja en el
  `IO80211Buffer`. El header es sólo metadato (MAC, canal, tiempos).
- MAC destino, BSSID y RSSI no aparecen en ningún acceso del manager a este header; si el driver los
  necesita para su propio datapath debe llevarlos en su propia copia.

### 1.4 Anillo interno de 104 B por entrada

Con el boot-arg `wlan.splitAWDLAFThread` activo (defecto; bit 0 de `ivars+16880`), `actionFrameInput`
encola en un anillo propio:

```c
struct af_ring_entry {          // 104 B (0x68)
    IO80211Buffer *buf;         // +0x00
    uint32_t       dlt;         // +0x08
    uint8_t        pad[4];      // +0x0c
    uint8_t        hdr[72];     // +0x10  copia de apple80211_act_frm_hdr
    uint64_t       hdrLen;      // +0x58
    uint64_t       enqueueTime; // +0x60  mach_absolute_time
};
```

Metadatos del anillo en el bloque de ivars AWDL (`*(manager+0x20)`):

| Offset | Campo |
|---:|---|
| `+16840` | base del anillo (`u8 *`) |
| `+16848` | head (`u32`) |
| `+16852` | tail (`u32`) |
| `+16856` | tamaño actual (entradas) |
| `+16860` | máscara (`size-1`) |
| `+16868` | flag de desbordamiento |
| `+16872` | última marca de actividad |
| `+10408` | `IO80211TimerSource` del hilo `actionFrameProcessThread` |
| `+10416` | flag "hilo armado" |

Avance del head, dentro de `0xffffff800220e1ae`: `head = (head + 1) & mask`. Si el anillo está lleno se marca
`+16868 = 1`, se arma el timer (`vtable +464` sobre `ivars+10408`) y el frame se manda al tap BPF sin
procesar. `growAFRing` (`0xffffff800220dd0a`) y `shrinkAFRing` (`0xffffff800220dfca`) ajustan el tamaño.

Con el boot-arg desactivado, `actionFrameInput` llama directamente a `actionFrameProcessInput` y sólo
manda el frame al tap BPF si el parseo devolvió "no válido".

### 1.5 Cómo construye hoy la llamada `AppleBCMWLANProximityInterface`

La cadena real es **indirecta a través del tap BPF**, no una llamada directa al manager:

1. `AppleBCMWLANProximityInterface::handleActionFrame_rx` (`0xffffff800171795c`) recibe el evento
   `wl_event_rx_frame_data` y valida longitudes: `len >= 0x30`, `*(u32*)(ev+20)` (versión) y el
   prefijo variable (16 B para v1, 24 B para v2).
2. Construye en stack un `apple80211_act_frm_hdr` de 72 B: copia la MAC de origen desde `ev+24`/`ev+28`,
   convierte el canal del evento (`AppleBCMWLANChanSpec::getAppleChannelSpec` +
   `ChanSpecConvToApple80211Channel`) al `apple80211_channel` de `+0x0c`, y rellena los tres
   `uint32_t` big-endian del evento con `_byteswap_ulong` (quedan en orden de host): en v1
   `+0x2c ← ev+52` (rssi), `+0x30 ← ev+56` (mactime), `+0x28 ← ev+60` (rate); en v2 `+0x2c ← ev+56`, `+0x30 ← ev+60`, `+0x28 ← ev+64`. En v2 añade `1` en
   `+0x40` y copia 4 B de `ev+68..71` en `+0x44`.
3. Pide un `IO80211Buffer` al pool de management frames (`getMgmtFramePool` → `alloc` →
   `+0x148 getCursor` → `IO80211BufferCursor::copyFrom` → `+0x138 setLength(len)`) y copia el cuerpo
   del frame (el evento menos su prefijo) dentro.
4. Llama `IO80211VirtualInterface::bpfTapInput(iface, buf, 12, &hdr, 72)` (`0xffffff8002331070`).
5. `bpfTapInput` toma el peer manager en `*(*(iface+288)+40)` y, **sólo si `dlt == 12`** y el manager
   existe, ejecuta `(*(pm->vptr + 0x238))(pm, buf, 12, hdr, hdrLen)`; si esa llamada devuelve 0 el
   buffer ya está consumido y no se hace nada más. Si devuelve distinto de cero, o no hay manager, se
   llama a `sendToBpfTap` y se devuelve el buffer (`+0x118`).

Consecuencia para un driver propio: **la puerta soportada es `bpfTapInput` con DLT 12**, no la celda
71 directamente. Llamar a la celda 71 exige replicar el contrato de propiedad del buffer (el manager
lo devuelve a su pool, por lo que el buffer debe venir de un pool válido y no debe reutilizarse).

### 1.6 Qué valida el manager y qué descarta

`isValidAF` → `actionFrameHasCorrectOUI` (`0xffffff8002210200`) sobre el **cursor del buffer**, no
sobre el header:

- `hdrLen > 0x47` y `buf != NULL` y `hdr != NULL`, si no: log `"Invalid frame/header, headerLen %lu"`
  y descarte.
- Cursor con `ptr` y `len` no nulos y `len >= 0x10`, si no: descarte.
- Bytes del frame en el cursor: `[1] == 0x00`, `[2] == 0x17`, `[3] == 0xF2`, `[4] == 0x08`,
  `[5] != 0`, si no: `"BAD OUI in AF"` y descarte (se vuelca el frame completo en el log). El byte
  `[0]` no se comprueba (`?` sobre su significado: categoría/action del frame).
- `*(u32*)(hdr+44) < *(u32*)(ivars+10268)` → descarte silencioso en `actionFrameProcessInput`.

Un AF aceptado crea o actualiza el peer (`IO80211AWDLPeer::withAddressAndManager` →
`initWithAddressAndManager`), se parsea TLV a TLV en `IO80211AWDLPeer::actionFrameReport`
(`0xffffff80021af6be`) y, si corresponde, se encola el UMI y se rearma la elección.

---

## 2. `reportDataPathEvents`: eventos de datapath del driver

### 2.1 Entradas y convención de retorno

```c
// celda 83 de la vtable, offset +0x298
int IO80211AWDLPeerManager::reportDataPathEvents(uint32_t id, void *data, size_t len);
```

Dos llamantes reales, ambos con la misma firma y la misma lectura del retorno:

- `IO80211SkywalkInterface::reportDataPathEventsGated` (`0xffffff80022f8d1a`):
  `pm = *(*(skywalk+288)+40)`; si `pm == NULL` **o** la llamada devuelve **distinto de cero**, el
  evento se publica a userspace con `postMessageInternal`. Retorno 0 ⇒ evento consumido por AWDL.
- `IO80211InfraInterface::routeToP2PInterface` (`0xffffff800236364e`): reenvía al manager sólo los
  ids de la lista blanca **2, 3, 4, 9, 66, 80, 125, 126, 127, 139, 160, 198, 242** (más el
  encaminamiento al manager NAN), resolviendo el manager por `*(*(infra+37*8)+360)`
  → `*(*(iface+288)+944)` → `safeMetaCast(..., IO80211AWDLPeerManager::gMetaClass)`
  y llamando `(*(pm->vptr + 0x298))(pm, id, data, len)`.

Al final de la función (`0xffffff80021dd8f4`, epílogo) se llama
`IO80211PeerManager::recordDataPathEventsProcessing(handled, bySubclass, !bySubclass)`
(celda 84, `0xffffff800224de5a`) y se devuelve el valor calculado. El **valor de retorno sólo tiene
ese significado de puerta**: 0 = consumido, ≠0 = dejar pasar a userspace.

`data` puede ser `NULL` en varios ids (asserts explícitos en 41/42/43/49/160); los ids 41, 42, 43, 46
y 47 no comprueban `len`.

### 2.2 Evento 41 — `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START`

Rama `case 41` del switch de `0xffffff80021dd8f4`, con precondiciones:

- Salidas tempranas, en este orden: reset activo con AWDL apagado, AWDL apagado (`+16096` bit 1 a 0)
  o deshabilitación en curso (`+414` bit 0) → se registra
  `"Exiting because of isAwdlResetActive=..., isAwdlOff=... isAwdlDisableInProgres=..."` y se
  devuelve 0 sin tocar nada.
- `data == NULL` → `"APPLE80211_M_AWDL_AVAILABILITY_WINDOW_START no param!!"` y retorno 0.

Payload (36 B leídos; el manager no valida `len`):

| Offset | Tipo | Campo | Destino / uso |
|---:|---|---|---|
| `+0x00` | `uint16_t` | `AwNumber` | `monitorTimeline` (`0xffffff80021e11ca`) lo compara con el anterior; en la misma rama del 41 se copia a `ivars+16104` como `uint32_t` |
| `+0x02` | `uint8_t[6]` | reservado, copiado tal cual | copia íntegra de 36 B a `ivars+776..811` |
| `+0x04` | `uint32_t` | estado de sync del firmware | comparado con `ivars+17028`; si cambia se guarda `mach_absolute_time()` en `ivars+42728` y se copia el valor |
| `+0x08` | `uint8_t[6]` | MAC del maestro actual | `monitorTimeline` (`memcmp` con `ivars+784`) y log |
| `+0x0e` | `uint8_t[6]` | MAC del top master (raíz) | `monitorTimeline` (`memcmp` con `ivars+790`); en la rama del 41 se copia a `ivars+37231` si cambia y `ivars+16289` está activo |
| `+0x14` | `uint32_t` | `FwAwStartTime` **big-endian** | `monitorTimeline` hace `_byteswap_ulong` y lo compara con el esperado |
| `+0x18` | `uint32_t` | flags/banda del canal (`?`) | `ivars+42824` (qword `+0x18..+0x1f`) |
| `+0x1c` | `uint32_t` | número de canal de la ventana | `ivars+42828`; el evento 43 lo compara con su tabla de canales |
| `+0x20` | `uint32_t` | flags de la ventana | `ivars+42832` (bits `0x2018` consultados en el evento 43) |

Acciones inmediatas tras `monitorTimeline`, todas dentro de la rama del 41 de
`0xffffff80021dd8f4`:

- `ivars+772 = 1` → `inAvailabilityWindow()` (`0xffffff8002219af4`) empieza a devolver cierto.
- Copia 36 B a `ivars+776`, y `+0x18`/`+0x20` a `ivars+42824`/`+42832`.
- `lockDataPath` (vtable de `datapath+280`, vía `*(manager+24)+120`), `ivars+0 = 1`, `ivars+408 = 0`.
- Calcula el índice de slot: `ivars+16128 = min(15, (awNumber % ((+42867+1)*+42820)) / (+42867+1))`
  y llama `(*(iface->vptr + 3768))(iface, slot, resto)` con la interfaz `*(*(manager+24)+1376)` —
  slot y sub-slot dentro de la ventana (`?` sobre el nombre del método).
- `ivars+16108 = awNumber & 0xFFFFFFC0`; si cambió respecto a `+16112`, guarda `mach_absolute_time()`
  en `+16120` y actualiza `+16112`.
- `logAWStart` + `sanityCheckAWStart` (`?` no decompilada).
- `ivars+42696 = mach_absolute_time()` (inicio de ventana en tiempo de host).
- Si el slot actual devuelto por `getCurrentAWSlot` (celda 94, `+0x2F0`) es 0 y `ivars+66376 != 0`:
  si han pasado menos de 64 ms desde el anterior se registra `"slot 0 start too soon (%dms), ignore"`
  y **se abandona el evento con retorno 0**.
- `postSyncStateChanged` **no** se llama aquí; el estado de sync se copia en `+17028` desde `+0x04`
  (ver abajo) y sólo se publica al cambiar.

### 2.3 Qué valida `monitorTimeline` y qué ocurre si no cuadra

`monitorTimeline` (`0xffffff80021e11ca`, llamada desde la rama del evento 41 de
`0xffffff80021dd8f4`) mantiene en `ivars+776..811` el último `apple80211_availability_window_start`
aceptado:

| Ivar | Campo espejo |
|---:|---|
| `+776` | qword `+0x00..+0x07` (AwNumber y reservado) |
| `+784` | MAC maestro |
| `+790` | MAC top master |
| `+796` | `FwAwStartTime` (se relee con `_byteswap_ulong`) |
| `+808` | dword `+0x20` |
| `+812` | flag "primera muestra pendiente" |

Algoritmo, en orden:

1. **Primera muestra** (`ivars+812` bit 0): copia los 36 B, limpia el flag (`+812 = 0`) y devuelve
   `false` (no hay nada que comparar). No se registra nada.
2. `AwNumber` nuevo (`+0x00`) vs anterior (`+776`), `FwAwStartTime` nuevo (`bswap(+0x14)`) vs
   anterior (`bswap(+796)`), `masterChanged = memcmp(data+8, ivars+784, 6)`,
   `topMasterChanged = memcmp(data+14, ivars+790, 6)`. Traza siempre:
   `"AwNumber=%d->%d, FwAwStartTime=%u->%u, Master=...(Changed=%d), TopMaster=...(Changed=%d)"`.
3. **Cambio de maestro**:
   - Si `ivars+10304` (cambio de maestro pendiente de aplicar) está a 0 y el maestro recibido **no**
     coincide con el esperado (`ivars+37465`): sólo log
     `"Waiting for Master=..., recieved Master=..."`.
   - Si `ivars+10304` está a 0 y coincide: log `"Detected that host Master change was applied"`,
     `ivars+10304 = 1` y `topMasterChanged = false` para el resto de la función.
   - Si `ivars+10304` está a 1 y `masterChanged`: log
     `"Detected Master change with no reason, Master=...->..."` y marca `unexpectedMaster = true`
     (variable `v17` del descompilado), que después decide el texto del aviso de discontinuidad.
   - Si no hay cambio de maestro pero `topMasterChanged`: log
     `"Detected TopMaster change without master change, TopMaster=...->..."` y
     `unexpectedMaster = false`.
   - Sin cambios: `unexpectedMaster = true`.
4. **Continuidad del `AwNumber`**. Se calculan dos valores esperados a partir del `AwNumber` anterior
   y de la secuencia de canales vigente (`+42872`, entradas de 12 B) y de la anterior (`+43256`):
   - índice base `idx = (aw_prev >> 2) & 0xF`;
   - se busca, en sentido circular módulo 16, el siguiente slot ocupado en cada secuencia
     (`v23` en la actual, `v25` en la anterior); "ocupado" = canal distinto de cero en `entry+4`;
   - `expected1 = aw_prev + 4*((v23 - idx) mod 16)`, `expected2 = aw_prev + 4*((v25 - idx) mod 16)`,
     con detección de desbordamiento de 16 bits (`"expectedAwNumber1/2 overflowed"`).
   - Si el `AwNumber` recibido no es ninguno de los dos: log
     `"Detected AW Number discontinuity without master change, receivedAwNumber=%d, expectedAwNumber=%d or %d"`
     o `"...with master change"` según `unexpectedMaster`, y en el primer caso vuelca las dos
     secuencias de canales completas (`prevChanSeq` / `newChanSeq`, 16 entradas).
   - **No hay corrección ni resincronización**: sólo registro. El estado se actualiza igualmente.
5. **Desfase temporal**. Con el `AwNumber` aceptado: `expectedStart = bswap(+796) + ((4*delta) << 14)`
   donde `delta` es el salto de slot usado (es decir, `delta * 65536` unidades de firmware); si
   `|FwAwStartTime - expectedStart| >= 0x1401` (5121 µs)
   se registra `"Detected AW start time inaccuracy: expetedAwStartFwTime = %u, currAwStartFwTime = %u, delta = %d [usec]"`.
   De nuevo, sólo traza: no se reprograma nada.
6. Guarda los 36 B nuevos en `+776..811` y devuelve `topMasterChanged`.

El retorno de `monitorTimeline` sólo se usa para decidir si el top master cambió; la consecuencia
está en el llamante: si `ivars+10307` (elección fijada) está activo se llama `topMasterChanged`
directamente; si no, cuando `ivars+16289` está activo y la MAC de top master del payload difiere de
`ivars+37231`, se sobrescribe `+37231..37236` con `data+14` y se llama `topMasterChanged`
(`0xffffff80021e3c08`).

### 2.4 Evento 42 — `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_START` (`?` nombre)

Rama `case 42` del switch de `0xffffff80021dd8f4`:

- Si `ivars+66118` bit 0 (reset) → log `"Exiting because reset is active"` y retorno 0.
- `data == NULL` → mismo mensaje `"...no param!!"` que el 41.
- Traza opcional (`shouldLog 0x4000`) con `xten:`/`Xten:` y el tiempo de firmware.
- **Única escritura**: `ivars+408 = *(u32*)data`. Ese ivar es el contador de paquetes/ventana que
  `dequeueTxPacket` compara con 4 (§4.2) y que el evento 41 pone a 0.
- Retorno 0.

Payload: sólo se lee `+0x00` (`uint32_t`); `+0x04` se usa en la traza como timestamp de firmware
big-endian. `len` no se comprueba.

### 2.5 Evento 43 — `APPLE80211_M_AWDL_AVAILABILITY_WINDOW_EXTENSIONS_END`

Rama `case 43` del switch de `0xffffff80021dd8f4`. Efectos, en orden:

1. `ivars+772 = 0` (fin de ventana), `ivars+757 = 0`, `ivars+0 = 0`,
   `ivars+16400 = mach_absolute_time()`.
2. Si `ivars+66118` bit 0 → log y retorno 0 sin nada más.
3. **Elección**: `if (shouldRunElection()) runElection();` (`0xffffff80021e3cca` /
   `0xffffff80021e3e7c`), todavía sin el datapath bloqueado. Es el único disparador periódico de la
   elección además del parseo de Sync Params de un peer.
4. `lockDataPath`; si falla se registra `"Event:%s, lockDataPath failed"` y se sale con retorno 0.
   Con éxito, `ivars+17024 = 0`.
5. `ivars+42676 = *(u32*)(data+0x0c)` (dword del payload, uso `?`).
6. **CCA** (sólo si capacidad `CCA_STATS` en `ivars+16577` y `*(u16*)(data+0x10) == 1`):
   con `dw = 10 * *(u32*)(data+0x18)` (0 ⇒ se usan tres contadores a 0), calcula
   `a = *(u32*)(data+0x1c)/dw`, `b = *(u32*)(data+0x20)/dw`, `c = *(u32*)(data+0x24)/dw`,
   `total = a+b+c`, y llama `reportP2PCCA(channel = ivars+42828, total, a, b, c)`.
   Después actualiza la tabla de 3 entradas de 12 B en `ivars+53308`
   (índice por canal) y, si procede, `airplayStatsBuildAvgCCAHistogramForChannel` /
   `rtgStatsBuildAvgCCAHistogramForChannel`.
7. `logAWEnd` + `sanityCheckAWEnd` (`?`), `unlockDataPath`.
8. Si `ivars+17032` (modo datos) ≠ 0: `IO80211Controller::setCurrentChannel`.
9. Piggyback scan: si `ivars+42820 != 0` y `ivars+17032 != 0` y la aritmética de secuencia cuadra,
   con `ivars+10480 == 3` se envía el ioctl `0xD3802869C8` (tipo 211, "parar piggyback scan") y se
   llama `setScheduleState`; con `== 2` se llama `runPiggybackScanStateMachine`.

Payload (40 B leídos; `len` no se comprueba):

| Offset | Tipo | Campo |
|---:|---|---|
| `+0x00` | `uint8_t[12]` | cabecera común con el evento 41 (no usada aquí) |
| `+0x0c` | `uint32_t` | valor copiado a `ivars+42676` (`?`) |
| `+0x10` | `uint16_t` | bandera "hay CCA" (debe ser 1) |
| `+0x14` | `uint32_t` | reservado |
| `+0x18` | `uint32_t` | base de tiempo en unidades de 0,1 µs (`dw = 10*x`) |
| `+0x1c` | `uint32_t` | contador CCA 1 |
| `+0x20` | `uint32_t` | contador CCA 2 |
| `+0x24` | `uint32_t` | contador CCA 3 |

### 2.6 Evento 47 y resto de ids

**El evento 47 no lo consume el manager.** La rama `case 47` del switch de
`0xffffff80021dd8f4` comparte destino con 44, 45, 48 y otros (`goto LABEL_52`), que delega en
`IO80211PeerManager::reportDataPathEvents` (`0xffffff800224cd4c`); esa función tampoco tiene rama
para 47 (sus ids propios son 1, 2, 3, 4, 7, 12, 13, 39, 40, 49…58, 85, 87, 160, 198, 204, 227–233 y
el resto cae en el `default`), de modo que el evento sale con retorno ≠ 0 y termina publicado a
userspace por `postMessageInternal`. `cleanUpForPeerRemovedFromNDP` (`0xffffff8002214cf2`) **no
cuelga de ningún evento**: su único xref es `IO80211AWDLPeer::setPeerNotReachable`
(`0xffffff80021aa717`, llamada interna), corrección respecto de `AWDL_PEERMANAGER_SOFTWARE.md` §5.

Ids que el manager AWDL sí trata y su efecto:

| Id | Efecto | Payload mínimo leído |
|---:|---|---|
| 41 | §2.2 (arranque de ventana) | 36 B |
| 42 | §2.4 (`ivars+408 = data[0]`) | 4 B |
| 43 | §2.5 (fin de ventana, CCA, elección) | 40 B |
| 46 | si `data[0] != 0`: nada (sólo se reenvía a userspace). Con `data[0] == 0`: limpia `+16096` bit 1, `+17028` y `+10480`, llama `setEnableState(false)` y, si antes estaba habilitado, `postSyncStateChanged` | 4 B |
| 49 | `APPLE80211_M_RESET_INTERFACE`: assert `len == 4`, guarda `data[0]` en `ivars+16100`, reenvía a la base como id `0x31`; con `data[0] != 0` marca recuperación AWDL y reconfigura | 4 B |
| 58 | estado de interfaz (`IFACE_STATE`) → `setDataPathState` | `u32` |
| 64 | SDB mode changed (assert `len == 8`) → reconstruye y baja la plantilla de sync y el MI | 8 B |
| 82 | roam completado → `setSteerComplete`, `setScheduleState` | `u32` |
| 160 | assert `len == 8`; `APPLE80211_M_SDB_MODE_CHANGED` | 8 B |
| 179, 182, 172, 227–233, 243 | consumidos sin efecto lateral | — |
| 183 | DFS suspend/resume (`data[2]`, `data[3]`) | 4 B |
| 198 | overlay de scan 6 GHz → `setChannelSequenceUnconditional` | `u32` |
| 204 | dwell start (`data[2] != 0`) | 4 B |

Los ids que no figuran arriba ni en la lista blanca de `routeToP2PInterface` no llegan nunca al
manager.

---

## 3. Ioctls internos

### 3.1 `ioctl_internal_req` y codificación

`IO80211PeerManager::ioctl_internal` (`0xffffff80022524e8`) toma un puntero a esta estructura de 24 B
y la ejecuta en el workloop del controlador
(`*(*(manager+24)+1312)` → `vtable+3192` → `vtable+288(gated)`). Layout verificado en
`ioctl_internal_gated` (`0xffffff800225238a`, lecturas `*(u32*)a2`, `*(u32*)(a2+4)`,
`*(void**)(a2+8)`, `*(u32*)(a2+16)`, `*(u8*)(a2+20)`, `*(u8*)(a2+21)`):

```c
struct ioctl_internal_req {
    uint32_t ioc;        // +0x00  codificación IOC (0x802869C8 = IOC_IN, tam. 40, 'i', núm. 200)
    uint32_t type;       // +0x04  número apple80211 (116, 117, ...)
    void    *payload;    // +0x08  buffer del llamante, consumido en la misma llamada
    uint32_t length;     // +0x10  longitud del payload (0 en todos los usos del manager)
    uint8_t  flags;      // +0x14  bit0: encaminar por IO80211Glue::sendIOUCToWcl
    uint8_t  ifaceSel;   // +0x15  bit0: usar la interfaz del controlador
    uint16_t pad;        // +0x16
};                       // 24 B
```

- Con `flags` a 0 (todos los ioctls del manager) la ruta es
  `apple80211RouteInternal(ioc, type, iface, payload)`, con la interfaz AWDL `*(manager+24)+1376`
  (o `+1368` si la primera es nula).
- `length` sólo se usa en la ruta WCL (`sendIOUCToWcl`); el manager deja siempre 0 y el tamaño real
  va dentro del payload.
- Retorno: 0 = éxito; cualquier otra cosa es un `errno` que el manager traduce con
  `(*(controller->vptr + 1920))(controller, ret)` en los logs. Salvo los dos casos señalados en
  §3.13 (117 y 132), un fallo sólo se registra y el manager continúa.

Codificación observada: la mitad baja del qword es `0x802869C8` (dirección `IOC_IN`, tamaño 40,
grupo `'i'`, número 200) en todos los comandos menos la lectura de capacidades, que usa
`0xC02869C9` (dirección `IOC_INOUT`, número 201); la mitad alta es el número apple80211 (`type`).

| Comando (qword) | type | Nombre | Payload |
|---|---:|---|---|
| `0x75802869C8` | 117 | `setAWDL_SYNC_ENABLED` | 12 B |
| `0x155C02869C9` | 341 | lectura `awdl_cap` | 8 B (salida) |
| `0x74802869C8` | 116 | `setAWDL_SYNC_PARAMS` | 20 B |
| `0x86802869C8` | 134 | `setAWDL_SYNC_STATE` | 24 B |
| `0x9B802869C8` | 155 | `setAWDL_ELECTION_PARAMS` | 36 B |
| `0x81802869C8` | 129 | `setAWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE` | 400 B |
| `0x84802869C8` | 132 | `setAWDL_MASTER_CHANNEL` | 8 B |
| `0xCD802869C8` | 205 | `setAWDL_SECONDARY_MASTER_CHANNEL` | 8 B |
| `0xD0802869C8` | 208 | `setAWDL_AF_TX_MODE` | 8 B |
| `0x95802869C8` | 149 | envío de UMI | 56 B |
| `0x97802869C8` | 151 | `setAWDL_SYNC_FRAME_TEMPLATE` | 16 B |
| `0xE5802869C8` | 229 | `setAWDL_AWDL_ADVERTISERS` | 44 B |
| `0xD3802869C8` | 211 | parada de piggyback scan (evento 43) | 8 B |

Los números de `type` se deducen de la mitad alta del qword. Los nombres proceden de la tabla de
`AWDL_APPLE80211_DISPATCH.md` y de los setters de Broadcom; 149 y 151 no figuran en esa tabla y se
identifican por su contenido (UMI y plantilla), por lo que su nombre queda como `?`.

### 3.2 117 — `setAWDL_SYNC_ENABLED` (`0xffffff80021dcf58`)

```c
struct awdl_sync_enabled {   // 12 B
    uint32_t reserved0;      // +0x00  = 0
    uint32_t reserved1;      // +0x04  = 0
    uint32_t enabled;        // +0x08  = 1 al habilitar
};
```

Escrito en `enableAWDL` (`0xffffff80021dcf58`, `v50 = 0; v51 = 1; cmd = 0x75802869C8`). Si el retorno
es distinto de 0: `ivars+414 = 0`, se llama `(*(vptr+432))(manager, 0)` (fallo de enable) y `enableAWDL`
devuelve el error. Es el **único** ioctl cuyo fallo revierte el estado del manager.

### 3.3 341 — lectura `awdl_cap` (`0xffffff80021dcf58`, inicio)

```c
struct awdl_cap {            // 8 B, salida
    uint16_t version;        // +0x00
    uint16_t length;         // +0x02
    uint8_t  caps;           // +0x04
    uint8_t  pad[3];
};
```

Bits consumidos (`v55`): `0x01` → `ivars+16576` `SEC_PAYLOAD`; `0x02` → `ivars+16577` `CCA_STATS`;
`0x08` → `ivars+16578` `PSF_DWELL_MI`; `0x10` → `ivars+16579` `PSF_DWELL_UMI`. El test de `0x01` se
hace sobre el qword (`v55 & 0x100000000`), los demás sobre el byte `+0x04`. Si el ioctl falla se
registra `"ERROR: ioctl awdl_cap failed ret %x, awdl_cap 0x%X"` y **no se declara ninguna
capacidad** (todo queda a 0): es la respuesta segura para un driver sin soporte.

### 3.4 116 — `setAWDL_SYNC_PARAMS` (`0xffffff80021ed024`)

```c
struct awdl_sync_params {      // 20 B
    uint32_t reserved;         // +0x00  = 0
    uint32_t aw_length;        // +0x04  ivars+62944  (getAvailabilityWindowLength)
    uint32_t aw_period;        // +0x08  ivars+62940  (getAvailabilityWindowPeriod)
    uint32_t ext_length;       // +0x0c  ivars+62948
    uint32_t sync_frame_period;// +0x10  argumento a2 (µs? ver nota)
};
```

Escrito en `setSyncFramePeriod` (`0xffffff80021ed024`) con `cmd = 0x74802869C8`. Condiciones:

- `a2 == 0` → assert (`"Error: Assert failure"`, línea 25927) y retorno `-536870212`
  (`0xE00002BC`) sin emitir.
- Se omite si el periodo no cambió y `force == 0`, o si `ivars+16943` (PSF dwell) y
  `ivars+16960 == a2` y `ivars+62952 == a2` (`"+++ Skipping Setting PSF period"`).
- Retorno 0 ⇒ `ivars+62952 = a2`. Retorno ≠ 0 ⇒ log `"fail to set PSF period to %u: %x"` y el ivar
  **no** se actualiza (por eso el manager reintenta en cada `programChipWithNewMaster`).
- Invocado desde `programChipWithNewMaster` (`0xffffff80021ec4b6`, tres veces) y desde la ruta de
  habilitación; el valor `a2` sale de `ivars+62952` (periodo de master indication), nunca de
  `aw_length`/`aw_period`.

**La unidad de `sync_frame_period` no se deduce del código `?`**: la traza lo imprime como
`"period %u ms %u"`. Un driver debe tratarlo como el periodo de repetición de la plantilla del 151.

### 3.5 134 — `setAWDL_SYNC_STATE` (`0xffffff80021ec4b6`, bloque `LABEL_3`)

```c
struct awdl_sync_state {        // 24 B
    uint32_t reserved;          // +0x00  = 0
    uint32_t valid;             // +0x04  = 1
    uint8_t  master[6];         // +0x08  ivars+37237 (parent/master elegido)
    uint8_t  pad[2];            // +0x0e
    uint32_t op_mode;           // +0x10  1 = normal; 2 si ivars+37484 (maestro forzado);
                                //        ivars+10307 (elección fijada) fuerza 1
    uint8_t  flags;             // +0x14  0x20 si (*(*(ivars+480)+16)+12) == 1 (warmup)
    uint8_t  pad2[3];
};
```

Escrito en `programChipWithNewMaster` (`0xffffff80021ec4b6`) con `cmd = 0x86802869C8`. Retorno ≠ 0: log
`"error: %s  AWDL can't set op_mode for mast ..."` y se continúa con el 155.

### 3.6 155 — `setAWDL_ELECTION_PARAMS` (`0xffffff80021ec4b6` y `0xffffff80021ed200`)

```c
struct awdl_election_params {   // 36 B
    uint32_t reserved0;         // +0x00  = 0
    uint32_t reserved1;         // +0x04  = 0
    uint64_t valid;             // +0x08  = 1
    uint8_t  op_mode;           // +0x10  ivars+37263
    uint8_t  pad;               // +0x11
    uint16_t election_id;       // +0x12  ivars+37259
    uint8_t  hop_count;         // +0x14  ivars+37247
    uint8_t  pad2;              // +0x15
    uint8_t  root_mac[6];       // +0x16  ivars+37231
    uint32_t root_metric;       // +0x1c  ivars+37251
    uint32_t self_metric;       // +0x20  ivars+37255
};
```

Escrito en `0xffffff80021ec4b6` (`v35`/`v36`) y en `0xffffff80021ed200` (`v7[2]`/`v7[3]`/`v7[4]`,
mismo patrón). `initFWElectionParams` se llama al habilitar; `programChipWithNewMaster` en cada
cambio de maestro. Retorno 0 ⇒ `ivars+728 = election_id`, `ivars+16056 = 0` y se reemite el 116 con
`ivars+62952`. Retorno ≠ 0 ⇒ log `"AWDL can't send election params error %s"` y **no** se actualiza
`ivars+728` (lo que deja el debounce de 1001 ms de `ivars+10304` pendiente).

### 3.7 129 — secuencia de canales de sincronización (`0xffffff80021cb69a`)

```c
struct awdl_chan_seq {              // 400 B
    uint32_t version;               // +0x00  = 1
    uint8_t  flags;                 // +0x04  ivars+43636, con bits SDB:
                                    //         |0x10 = SDB ON, |0x20 = SDB OFF, &0xCF = sin SDB
    uint8_t  num_channels;          // +0x05  ivars+42820 (tope real de entradas)
    uint8_t  unknown6;              // +0x06  ivars+42866
    uint8_t  unknown7;              // +0x07  ivars+42867 (sub-slots por ventana)
    uint8_t  unknown8;              // +0x08  ivars+42848
    uint8_t  unknown9;              // +0x09  ivars+42856
    uint8_t  pad[2];                // +0x0a
    struct apple80211_channel entry[32]; // +0x0c, 12 B cada una
};
```

- Cada entrada se copia de `ivars+42868 + 12*i` (12 B) con el mismo formato que
  `struct apple80211_channel`: `+0x00 uint32_t version/flags`, `+0x04 uint32_t channel`,
  `+0x08 uint32_t flags`. El manager lee el canal de una entrada en `monitorTimeline` como
  `*(u32*)(base + 12*i + 4)` y `setMyChanSeq` (`0xffffff8002235302`) escribe 8 B de `+0x00` y 4 B de
  `+0x08`, lo que confirma esos tres campos.
- Límite: `num_channels` no se acota en el bucle de copia (`while (i < *(u8*)(ivars+42820))`); con
  más de 32 entradas se desbordarían los 400 B. Un driver debe asumir `num_channels <= 32` (`?`
  sobre si algún camino lo garantiza).
- Se compara con la copia en `ivars+8` (`memcmp(..., 0x190)`) y, si `force == 0` y no cambió y el
  flag de SDB no cambió, **no se emite**.
- Efecto del retorno: éxito ⇒ copia los 400 B a `ivars+8` y actualiza `ivars+63023`; error ⇒
  `recoverFromSetChanSeqFailure` (`0xffffff80021f3710`) reintenta con la secuencia anterior.

### 3.8 132 y 205 — canales sociales (`0xffffff80021d494a`)

```c
struct awdl_master_channel {   // 8 B
    uint32_t reserved;         // +0x00 = 0
    uint32_t channel;          // +0x04 ivars+17060 (132) / ivars+17072 (205)
};
```

Ambos se emiten en `programMasterChannels`: primero `0x84802869C8` (132) y después `0xCD802869C8`
(205). Antes, si `ivars+16940` o `ivars+16941` están activos, se refresca la lista con
`updateFWSocialChanListWithPSFSuppressionInfo` (`0xffffff800220db4e`). Si el 132 falla se registra
`"Unable to set master channel %u ret %d (%s)"` y **se Aborta** antes del 205; si falla el 205 se
registra y se devuelve el error.

### 3.9 208 — `setAWDL_AF_TX_MODE` (`0xffffff80021f7d06`)

```c
struct awdl_af_tx_mode {       // 8 B
    uint32_t reserved;         // +0x00 = 0
    uint32_t mode;             // +0x04 valor solicitado; forzado a 3 en 2,4 GHz sin país
};
```

- Se omite si el par `(host, fw)` ya coincide con `ivars+37144`/`ivars+37148`.
- Si `mode == 5` (supresión) y la infra está en 2,4 GHz (byte `*(u8*)(ctrl+2496)` en 1..14 y
  `*(u32*)(ctrl+2500) & 0x2000 == 0`), o si `ivars+17044 == 0`, se envía `3` en lugar de `5`.
- Retorno 0 ⇒ actualiza ambos ivars y, si el valor enviado fue 0, llama `programMasterChannels`.

### 3.10 149 — envío de UMI (`0xffffff8002213c0a`)

```c
struct awdl_umi {                    // 56 B
    uint32_t reserved0;              // +0x00  = 0
    uint8_t  dst[6];                 // +0x04  MAC destino (peer+88)
    uint8_t  pad0[10];               // +0x0a
    uint16_t option;                 // +0x14  6 = normal, 14 = "UMIOnDwell"
    uint8_t  self_bssid[6];          // +0x16  ivars+37204/+37208
    uint32_t reserved1;              // +0x1c  = 0
    uint16_t template_len;           // +0x20  ivars+10248
    uint8_t  pad1[6];                // +0x22
    uint64_t template;               // +0x28  ivars+9048 (plantilla MI en ivars)
};                                    // total 0x38
```

Precondiciones: `ivars+16096` bit 1 (AWDL conectado), `>= 10 ms` desde el último UMI al peer
(`peer_ivars+1632`), `ivars+772` bit 0 (dentro de ventana) y `buildMasterIndicationTemplate` con
éxito. La opción 14 sólo se usa si `ivars+16943` y `ivars+16579` (PSF_DWELL_UMI) están activos, el
peer no se descubrió en 5 GHz y el último dwell fue hace ≤ 109 ms. Retorno 0 ⇒ actualiza
`ivars+42736` y `peer_ivars+1632`.

### 3.11 151 — plantilla de sync frame (`0xffffff80021dc5b2`)

```c
struct awdl_sync_frame_template {   // 16 B
    uint32_t reserved;              // +0x00  = 0 (¿versión?) ?
    uint32_t length;                // +0x04  bytes válidos de la plantilla
    uint64_t template;              // +0x08  puntero al buffer de 512 B
};
```

La plantilla se compone en un buffer de **512 B** (`secure_memset(..., 0xAA, 0x200)`) y se emite
sólo si `buildServiceParamTlv` devolvió `< 512`; el `length` es el total acumulado de TLVs.

Composición exacta (cada TLV es `{u8 type, u16 len, body}` con la longitud en el orden de máquina del
kernel, **little-endian** en x86_64: `*(u16*)(tlv+1)`):

| Orden | Tipo | Constructor | Tamaño | Contenido |
|---:|---:|---|---|---|
| 1 | 6 | `buildServiceParamTlv` `0xffffff80022118a6` | `12 + n` | `+0 type=6`, `+1 u16 len = n+9`, `+3 = 0`, `+4..+5 reservado`, `+6 u16 ivars+43656`, `+8 u32 ivars+43660` (bitmap de 32 servicios), `+12..` los `n` bytes de servicio (`ivars+43664`, sólo los bits activos) |
| 2 | 24 | `buildAWDLElectionParamsVer2` `0xffffff8002210a3c` | 43 | `+3 root_mac[6]`, `+9 master_mac[6]`, `+15 rootLFC`, `+19 hopCount`, `+23 rootMetric`, `+27 selfMetric`, `+31 electionId (u32 + 4 ceros)`, `+39 selfLFC (ivars+37560)` |
| 3 | `?` | `buildAwdlDataPathStateTlv` `?` | variable | estado del datapath, con la capacidad `SEC_PAYLOAD` |
| 4 | 7 | inline en `0xffffff80021dc5b2` | `3 + len` | `buildEnhDataRateTlv`, sólo si `*(ctrl+1564)` en 1..25 |
| 5 | `?` | `buildRangingTlv` `?` | variable | sólo si `ivars+42545` |
| 6 | `?` | `buildRealTimeGroupTlv` `?` | variable | sólo si `ivars+64418` o `ivars+65477` |
| 7 | 17 | inline (`*(v21-3) = 17`) | `3 + len` | `buildIEEE80211CntnrTlv` con 14 B de `ctrl+2401`, sólo si `*(ctrl+1568)` |
| 8 | `?` | `buildAWDLVersionTlv` `?` | variable | siempre |
| 9 | 20 | `buildSyncTreeTlv` `0xffffff80022112fe` | `3 + 6n` | `n = min(hopCount+1, 10)`, `len = 6n`, cuerpo copiado de `ivars+37500`; sólo si `ivars+10308` |
| 10 | `?` | `buildNanSyncTlv` `?` | variable | siempre |
| 11 | `?` | `buildPrefChanTlv` `?` | variable | siempre |

**No hay campos de temporización en la plantilla**: ningún constructor escribe en ella los ivars de
periodo/longitud de ventana (`+62940/+62944`) ni el periodo de sync (`+62952`). La temporización se
contrata por el ioctl 116 y por los eventos 41/42/43; la plantilla sólo lleva parámetros de elección,
servicios, capacidades, árbol de sincronía y canal preferido. Si el firmware real inserta el
`AwNumber`/`FwAwStartTime` en el sync frame, lo hace fuera de este contrato: un driver sin firmware
debe componer esos campos él mismo a partir de su propio reloj (`?` sobre el formato exacto que
espera el aire; la plantilla del 151 es sólo la parte "estática" de los TLVs).

### 3.12 229 — advertisers del maestro (`0xffffff80021ec4b6`)

```c
struct awdl_advertisers {      // 44 B
    uint8_t  mi_copy[44];      // copia de los campos del MI del maestro
};
```

Se construye copiando, campo a campo, el MI del peer maestro (`*(peer+0x20)`): 10 bytes iniciales
(contador AW y fechas), el canal normalizado con la misma regla `-10`/`-100` que aplica el log
`aw %u dc %u tx_delay %u period %u ...`, un `bswap` de un contador de tramas y las MACs de
root/relay del árbol del maestro. El mapeo byte a byte de cada campo depende del layout de ivars de
`IO80211AWDLPeer` y queda sin fijar aquí (`?`); lo vinculante es el tamaño (44 B) y que el contenido
es una copia literal del MI del maestro, no una estructura propia del driver.

Precondición: `memcmp(ivars+37231, ivars+37216, 6) != 0` (no se es maestro) o `ivars+37484` bit 0
(maestro forzado). Antes de emitir se hace `lockDataPath`, se busca el peer maestro
(`findPeer(ivars+37237)`), se exige que su MI tenga longitud 16 (`LOWORD(...) != 16` ⇒
`"Error, master Ext length not valid"` y salida). Retorno ≠ 0 ⇒
`"FAIL: %s AWDL can't send AWDL_ADVERTISERS for parent ..."` y se continúa con el 134.

### 3.13 Retornos que el manager comprueba (resumen)

| Comando | ¿Fallo revierte estado? | ¿Reintenta? |
|---|---|---|
| 117 | sí (`ivars+414 = 0`, `failToEnable`) | al siguiente `enableAWDL` |
| 341 | no (declara 0 capacidades) | sólo en el próximo enable |
| 116 | no (no actualiza `+62952`) | en cada `programChipWithNewMaster` |
| 134 | no | en cada cambio de maestro |
| 155 | no (no actualiza `+728`) | en cada cambio de maestro |
| 129 | sí (`recoverFromSetChanSeqFailure`) | con la secuencia anterior |
| 132 | sí (no emite el 205) | en cada `programMasterChannels` |
| 205 | no | en cada `programMasterChannels` |
| 208 | no | cuando cambie el par host/fw |
| 149 | no (no actualiza marcas de tiempo) | siguiente intento de UMI |
| 151 | no | siguiente `updateSyncFrameTemplate` |
| 229 | no | siguiente `programChipWithNewMaster` |
| 211 | no | — |

---

## 4. Tirón de TX: `dequeueTxPacket`, `getUnicastPackets`, `getMulticastPackets`

### 4.1 `TxPacketRequest` (96 B)

Layout reconstruido de `IO80211PeerManager::dequeueTxPacket` (`0xffffff8002250554`),
`IO80211PeerManager::getUnicastPackets` (`0xffffff800224ffdc`),
`IO80211PeerManager::getMulticastPackets` (`0xffffff800224fe4e`) y
`IO80211AWDLPeerManager::dequeueTxPacket` (`0xffffff800221675e`):

```c
struct TxPacketRequest {           // 96 B (0x60)
    uint16_t flags;                // +0x00  bit1 = ? (cambia el orden de las AC)
    uint16_t num_packets;          // +0x02  total solicitado / restante
    uint16_t num_unicast;          // +0x04  total unicast solicitado / restante
    uint16_t num_multicast;        // +0x06  total multicast solicitado / restante
    uint16_t ac_count[8];          // +0x08..+0x17  remanente por cola/AC
    mbuf_t   packets[8];           // +0x18..+0x57  listas de mbufs por cola
    uint16_t tail_status;          // +0x58  base: bitmap de colas activas
                                   //   (`ctrl+2504`); manager AWDL: 0
    uint8_t  pad[6];               // +0x5a..+0x5f
};
```

Detalles verificados, que son los que importan al driver:

- **Punteros**: la implementación base recorre `i = 0..7` leyendo `*(mbuf**)(req + 24 + 8*i)` y
escribiendo el recuento en `*(u16*)(req + 8 + 2*i)`. La variante AWDL recorre sólo `i = 0..3`
(punteros `+0x18, +0x20, +0x28, +0x30`, recuentos `+8, +10, +12, +14`), y por eso `packets[4..7]`
queda sin usar en AWDL.
- **Conteos**: `num_packets` es el total; `num_unicast`/`num_multicast` se decrementan en cada
  extracción; `ac_count[i]` es el remanente por cola. `getUnicastPackets` extrae
  `min(ac_count[ac], num_unicast, num_packets)` y descuenta de los tres; `getMulticastPackets` extrae
  `min(num_packets, num_multicast)` y descuenta de ambos.
- **Cola de destino**: los mbufs se encadenan con `mbuf_setnextpkt` al final de
  `req->packets[i]`, nunca se sustituye la cabeza si ya hay paquetes.
- **`+0x58`**: `*(u16*)(req+88)`. La base escribe ahí el bitmap de colas activas
  (`*(u16*)(ctrl+2504)`); el manager AWDL escribe 0 (`a2[44] = 0`) cuando no hay ventana.
- El llamante (`IO80211VirtualInterface::dequeueTxPackets`) recorre los 8 punteros
  `req[3..10]` para contabilizar en `IO80211InterfaceMonitor::recordOutputPacket`, y si no hay peer
  manager pone a cero los 96 B.

### 4.2 Quién llama y cuándo

```
Skywalk (TX de la interfaz) → IO80211VirtualInterface::dequeueTxPackets(req)   [0xffffff80023313b2]
        └─ (*(pm->vptr + 0x2A8))(pm, req)  = IO80211AWDLPeerManager::dequeueTxPacket  [0xffffff800221675e]
                ├─ si procede: IO80211PeerManager::dequeueTxPacket(pm, req)  [0xffffff8002250554]
                └─ y por AC: (*(pm->vptr + 0x170))(pm, req, ac)  = getUnicastPackets [0xffffff8002216a4e]
                             (*(pm->vptr + 0x168))(pm, req)      = getMulticastPackets [0xffffff8002216a2e]
```

`IO80211AWDLPeerManager::dequeueTxPacket` (`0xffffff800221675e`) impone:

1. Asserts de estado base: `*(pm+24)+1328 != 0` (interfaz), `+1336 == 0`, `+1344 != 0` (cola). Si
   fallan, log `"Error: Assert failure"` y retorno 0 (sin paquetes).
2. Pone a cero los cuatro punteros `+24..+48`.
3. `lockDataPath` (`*(*(pm+24)+120)` → `vtable+280`); si falla, log
   `"lockDataPath failed"` y retorno 0.
4. **Decisión de ventana**, en este orden:
   - Si `*(u32*)ivars != 0` (AWDL habilitado) y `*(u32*)(ivars+408) >= 4` (ya se sirvieron 4
     peticiones en la ventana actual): pone `*(u16*)(req+6) = 0`, es decir **anula el multicast** de
     la petición antes de extraer.
   - Si `*(u32*)ivars == 0` (AWDL deshabilitado) y (`*(u32*)(ivars+10468) != 5` y
     `*(u8*)(ivars+17032) != 0`, modo datos): pone `+88 = 0`, deja la petición vacía y sale sin
     extraer.
   - En el resto de casos no toca la petición.
5. Extracción: `IO80211PeerManager::dequeueTxPacket` (que llama a `getMulticastPackets` si
   `flags & 2`, y a `getUnicastPackets` con el orden de AC `{3,2,3,1}` si `flags & 2 == 0` y
   `{0,2,0,1}` si está activo), y después cuenta los mbuf de cada una de las 4 colas en
   `ac_count[i]`.
6. `unlockDataPath` y traza `"[t:%u mU:%u mM:%u pktCnt %u %u %u %u] numpeers %u reqTx 0x%x txav%u data mode %u dpopen %u got %d pkts"`.

`getUnicastPackets`/`getMulticastPackets` del manager AWDL sólo envuelven a las base y suman el
resultado a contadores (`ivars+16360` y `ivars+16368`).

### 4.3 Obligaciones del driver

- Servir `dequeueTxPacket` (o dejar que el llamante lo haga) sólo con el datapath abierto: el manager
  no transporta nada, sólo rellena `TxPacketRequest` con mbufs ya formateados por
  `formatOutputPacket` (celda 40). El instante de transmisión dentro de la ventana lo decide el
  driver.
- No retener los mbufs: la propiedad pasa al llamante en cuanto se encadenan.
- Llamar con `req` a cero antes de cada petición: el manager sólo limpia los cuatro punteros que usa
  y los recuentos que decrementa; el resto del estado lo aporta el llamante.

---

## 5. Mínimos que debe cumplir un driver sin firmware AWDL

1. **Inyectar AF** por `bpfTapInput(iface, buf, 12, hdr, 72)` con un `IO80211Buffer` de un pool
   válido y un `apple80211_act_frm_hdr` de exactamente 72 B, MAC de origen en `+0` y RSSI en dBm
   (`int32_t`, en orden de host) en `+0x2c`; por debajo del umbral de borde (−70 por defecto) el AF se
   descarta. Nunca reutilizar el buffer después: el manager lo devuelve a su pool (`+0x118`).
2. **Publicar los eventos 41/42/43** con los payloads de §2: 36 B para el 41 (`AwNumber` en `+0`,
   maestro en `+8`, top master en `+0x0e`, `FwAwStartTime` big-endian en `+0x14`, canal en `+0x1c`),
   4 B para el 42 (`sn` de la ventana), 40 B para el 43 (bandera CCA en `+0x10`, base en `+0x18`,
   contadores en `+0x1c/+0x20/+0x24`). Sin el 41 el manager nunca entra en ventana
   (`ivars+772` queda a 0) y `sendUnicastMI` rechaza todo.
3. **Aceptar los ioctls 116/117/129/132/134/149/151/155/205/208/229** con los layouts de §3 y
   devolver 0. El único cuyo fallo revierte el estado es el 117; el 341 debe devolver 0 capacidades
   si no se implementan `SEC_PAYLOAD`, `CCA_STATS` ni `PSF_DWELL_*`.
4. **Repetir la plantilla del 151** cada `sync_frame_period` (116) alineada con la línea temporal
   compartida: es la única obligación que el manager delega por completo y de la que no tiene
   realimentación.
5. **Servir el tirón de TX** durante las ventanas (§4), sin retener mbufs.

## 6. Correcciones a `AWDL_PEERMANAGER_SOFTWARE.md`

- El evento 47 **no** se traduce en `cleanUpForPeerRemovedFromNDP`: el manager lo delega en la clase
  base y ésta tampoco lo trata; termina publicado a userspace. Esa limpieza cuelga de
  `IO80211AWDLPeer::setPeerNotReachable`.
- El payload del ioctl 116 es `{0, aw_length, aw_period, ext_length, sync_frame_period}`, no
  `{periodo, longitud, ?, periodo}`.
- El payload del ioctl 155 tiene `op_mode` en `+0x10`, no en `+0`; los 16 B iniciales son reservados
  más un `u64` a 1.
- La plantilla del 151 no contiene campos de temporización; el periodo y la longitud de ventana
  viajan sólo por el 116 y por los eventos.
- El `len` del TLV 20 (sync tree) es `6n`; el tamaño total devuelto es `3 + 6n`.
- El retorno de `reportDataPathEvents` significa "dejar pasar a userspace" (≠0), no "manejado".
