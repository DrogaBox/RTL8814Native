# Correcciones propuestas para el código de etapa 1

Propuesta de parche, no aplicada, para los seis defectos de la tabla §4 de
`AWDL_STAGE1_SPEC_DELTA.md`, contrastados con los documentos posteriores y con el desensamblado.
Base: kernelcache de macOS 26.7.1 (25G241, x86_64); IDA headless (`idat -A -S`) sobre copias de
`boot_awdl.kc.i64`. No se ha editado ningún fichero de código, parche ni especificación.

Las líneas son las del árbol en el momento de escribir esto. El bloque de 50 overrides de
`AWDLInterface.h` está en `:91-145`; el delta lo cita como `:96-145` (cinco líneas de desplazamiento).
Los números de `AWDLInterface.cpp` y de `Tools/awdl_stage1_hook.patch` sí coinciden con el delta.

## 0. Verificación de partida

| Comprobación | Resultado |
|---|---|
| `clang++` de `AWDLInterface.cpp` con los flags de `build_airlayer.sh` | compila, `rc=0`, 62 792 B (`/tmp/awdlobj/AWDLInterface.o`); no se ha enlazado el kext |
| `nm -u` del objeto | 540 símbolos indefinidos; **93** de la clase `IO80211VirtualInterface` |
| Los 93 anteriores en `Docs/re/io80211_25.6.0/syms.txt` | **93/93 con tipo `0f`**; ninguno sin resolver |
| Los 50 overrides de `:91-145` | 48 aparecen como indefinidos (todos `0f`); `free` y `stop` tienen cuerpo local y no aparecen |
| Símbolos `IO80211*`/`IOSkywalk*` indefinidos sin `0f` en los dos volcados | **0** |
| Cabeza de función de las 50 direcciones citadas y de las 9 auxiliares | 59/59 `get_func(ea).start_ea == ea` |

Las direcciones de la tabla de §5 son direcciones de símbolo nlist; se verificó que cada una es
cabeza de función con `ida_funcs.get_func`. No se citan direcciones interiores.

Resumen de los seis defectos y su orden:

| # | Defecto | Fichero:línea | Gravedad | Bloqueante |
|---:|---|---|---|---|
| 1 | Acción nula en la cola TX submission | `AWDLInterface.cpp:140` | el arranque falla, no hay `awdl0` | **sí** |
| 2 | Rollback sin retirar las colas del work loop | `AWDLInterface.cpp:186-205` | evento colgante si falla el registro o el nombre | no |
| 3 | `rtl_vif_init` declarado `void`, resultado ignorado | `AWDLInterface.cpp:47`, `:110` | pierde el fallo del `init` base | no |
| 4 | Publicación con `setAwdlInterface`, sin lector | `AWDLInterface.cpp:270-273`, `.h:407` | superficie inútil | no |
| 5 | 50 overrides sin comprobar el enlace | `AWDLInterface.h:91-145` | ninguno: 93/93 resuelven `0f` | no |
| 6 | Headroom en `+0x3A` en vez de `+0x3C` | `AWDLInterface.cpp:82` | corrompe la mitad alta de `unit` y deja headroom 0 | no |

## 1. Defecto 1: acción de la cola TX submission

### 1.1 Evidencia

| VA | Hecho |
|---|---|
| `0xffffff8002b0c9b4` | cabeza de `IOSkywalkTxSubmissionQueue::withPool`; crea el objeto y llama al `initWithPool` virtual; si devuelve 0, lo libera y devuelve nulo. |
| `0xffffff8002b0caac` | cabeza de `IOSkywalkTxSubmissionQueue::initWithPool`; guarda `if (a4 >= 8 && a2 != nullptr && a8 != nullptr && a10 <= 0x1F)`. `a8` es la acción de paquetes, con la firma `unsigned int (*)(OSObject*, IOSkywalkTxSubmissionQueue*, IOSkywalkPacket* const*, unsigned int, void*)`. Con acción nula devuelve 0 y `withPool` devuelve nulo. |
| `0xffffff8002b2c860` | cabeza de `IOSkywalkTxSubmissionQueue::legacyDequeue`; llama a la acción con `(owner, this, ring+índice, pendientes, refCon)` y usa el retorno como **número de paquetes consumidos**: si es 0 pone el bit de parada (`|2`), guarda el instante y sale sin avanzar el anillo; si es > 0 avanza el anillo esa cantidad; si es menor que el lote, cuenta una salida parcial. |
| `0xffffff8002b2dbb6` | cabeza de `IOSkywalkTxCompletionQueue::enqueuePackets`; con la cola habilitada copia el lote y llama a `completePackets` bajo su gate, que es quien completa y devuelve los paquetes al pool. No usa la acción del `IOEventSource`. |
| `0xffffff8002b2da78` | cabeza de `IOSkywalkTxCompletionQueue::checkForWork`; sólo actúa si hay pendientes y la cola está habilitada; no es la vía que usa `enqueuePackets`. `?` Si esa vía puede alcanzarse con la acción nula y paquetes pendientes; con el no-op de abajo el lote se completa dentro de `enqueuePackets`, así que no debería quedar nada pendiente. |

Verificado en el desensamblado: `legacyDequeue` guarda el retorno de la acción como `unsigned int`; con 0 pone el bit `|2` y sale, con `n` avanza el anillo `n` posiciones y suma `n` al contador. `enqueuePackets` (`0xffffff8002b2dbb6`) exige `0 < count < *(this+0xAC)` (capacidad de la cola, 128 en `AWDLInterface.cpp:141`, de modo que los lotes de 64 cumplen) y devuelve `0xE00002C2`-familia en los demás casos; el segundo `unsigned int` del mangling no se usa. `deallocatePacket` resuelve en `0xffffff8002b2c0de`.

Conclusión: la acción debe **consumir el lote entero y devolver `count`**; devolver 0 detiene la cola
y los paquetes quedan en el anillo. No debe limitarse a devolver el número sin soltar los paquetes:
`legacyDequeue` no los libera, y el anillo deja de referenciarlos al avanzar, con lo que el pool los
pierde. La vía con la propiedad ya probada en el camino STA es devolverlos por la cola TX completion
(`RTLAirLayer.cpp:418-421`); si ese envío falla, se liberan en el pool para no filtrar memoria.

La firma se copia de `RTLAirInterface::txAction` (`RTLAirLayer.hpp:118-119`), que es la que ya acepta
`withPool` en producción: el segundo parámetro es la cola y el tercero el vector de paquetes; el
`extern` de `rtl_txsub_withPool` los pasa como `void *`, así que la comprobación de tipos no interviene.

### 1.2 Antes y después

`Source/AirLayer/AWDLInterface.h:396-397` (antes):

```cpp
	bool createDatapath();
	static IOReturn removeQueues(OSObject *owner, void *, void *, void *, void *);
```

Después:

```cpp
	bool createDatapath();
	void teardownDatapath();
	static IOReturn txNoop(OSObject *owner, void *queue, void *const *pkts,
	                       uint32_t count, void *refCon);
	static IOReturn removeQueues(OSObject *owner, void *, void *, void *, void *);
```

`Source/AirLayer/AWDLInterface.cpp`, tras el `extern` de `rtl_rxcomp_withPool` (`:44-46`), añadir:

```cpp
IOReturn rtl_txcomp_enqueue(void *q, void *const *pkts, uint32_t n, uint32_t opts)
	__asm("__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
void rtl_pool_free(void *pool, void *pkt)
	__asm("__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket");
```

`Source/AirLayer/AWDLInterface.cpp:140` (antes):

```cpp
	_txSubQ = rtl_txsub_withPool(_txPool, 128, 0, this, nullptr, nullptr, 0);
```

Después:

```cpp
	_txSubQ = rtl_txsub_withPool(_txPool, 128, 0, this,
	                             (void *)&RTLAwdInterface::txNoop, nullptr, 0);
```

Implementación, después de `removeQueues` (`:228`):

```cpp
/*
 * TX submission action, stage 1: nothing is transmitted. The packets go back
 * through the TX completion queue, as RTLAirInterface::txAction does once the
 * radio has consumed them; the completion queue completes them under its gate
 * and returns them to the pool. legacyDequeue advances its ring by the returned
 * count, so returning 0 would stall the queue: consume the whole batch.
 */
IOReturn RTLAwdInterface::txNoop(OSObject *owner, void *, void *const *pkts,
                                 uint32_t count, void *)
{
	RTLAwdInterface *me = OSDynamicCast(RTLAwdInterface, owner);
	if (!me || !count || !me->_txCompQ)
		return 0;
	/* enqueuePackets exige count < capacidad; 64 < 128 siempre. */
	for (uint32_t done = 0; done < count; done += 64) {
		uint32_t n = count - done > 64 ? 64 : count - done;
		if (rtl_txcomp_enqueue(me->_txCompQ, pkts + done, n, 0) != kIOReturnSuccess)
			for (uint32_t i = 0; i < n; i++)
				rtl_pool_free(me->_txPool, pkts[done + i]);
	}
	return (IOReturn)count;
}
```

El `(void *)&RTLAwdInterface::txNoop` replica el cast de `RTLAirLayer.cpp:365`; el `extern` declara la
acción como `void *`, de modo que el compilador no comprueba la firma y sólo cuenta la ABI.

`?` El camino de `checkForWork` (`0xffffff8002b2da78`) con acción nula y paquetes pendientes no se ha
provocado: no hay una ruta conocida que deje pendientes si `enqueuePackets` los completa en el acto.

## 2. Defecto 2: rollback del datapath

### 2.1 Evidencia

`RTLAwdInterface::removeQueues` (`:212-228`) ya hace lo que falta: deshabilita, quita del work loop,
libera y pone a nulo cada cola, con salto de las ya nulas. `stop` (`:230-238`) ya elige entre
`runAction` y llamada directa según haya work queue. El fallo está en `startWithRegistration`: los dos
`return false` posteriores a `createDatapath` (`:192-193` y `:201-203`) dejan las tres colas en el work
loop; el parche hace `terminate()` + `release()` (`Tools/awdl_stage1_hook.patch:175-179`) y `free()`
(`:240-252`) libera los punteros sin quitarlos antes, dejando el work loop con eventos colgantes.

### 2.2 Antes y después

`Source/AirLayer/AWDLInterface.cpp:190-193` (antes):

```cpp
	IOReturn r = rtl_eth_register(this, _regInfo, queues, 3, _txPool, _rxPool, 0);
	LOG("awdl: registerEthernetInterface 0x%x", r);
	if (r != kIOReturnSuccess)
		return false;
```

Después:

```cpp
	IOReturn r = rtl_eth_register(this, _regInfo, queues, 3, _txPool, _rxPool, 0);
	LOG("awdl: registerEthernetInterface 0x%x", r);
	if (r != kIOReturnSuccess) {
		teardownDatapath();
		return false;
	}
```

`Source/AirLayer/AWDLInterface.cpp:201-203` (antes):

```cpp
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		return false;
	}
```

Después:

```cpp
	if (!name || !name[0]) {
		LOG("awdl: no bsd name");
		teardownDatapath();
		return false;
	}
```

Helper y `stop` (`:230-238`):

```cpp
void RTLAwdInterface::teardownDatapath()
{
	IOWorkLoop *wq = (IOWorkLoop *)rtl_sky_getWorkQueue(this);
	if (wq)
		wq->runAction(&RTLAwdInterface::removeQueues, this);
	else
		removeQueues(this, nullptr, nullptr, nullptr, nullptr);
}

void RTLAwdInterface::stop(IOService *provider)
{
	teardownDatapath();
	IO80211VirtualInterface::stop(provider);
}
```

`removeQueues` es idempotente: los punteros se ponen a nulo y las llamadas siguientes no hacen nada, de
modo que un `terminate()` que llegue a `stop()` después del rollback no repite trabajo. Si el fallo es
de `createDatapath` antes de `addEventSource` (`:143-150`), las colas no están en el work loop y
`free()` las libera; no hace falta teardown.

`?` El alta fallida con nombre vacío deja el `registerService` hecho y no se desregistra: la
especificación §11 pide `deregisterEthernetInterface(uint)` (símbolo `0f` en `0xffffff8002b10af4`), pero
el valor del argumento (unidad BSD) no está fijado. Queda fuera de esta propuesta.

## 3. Defecto 3: resultado de `rtl_vif_init`

### 3.1 Evidencia

`IO80211VirtualInterface::init(IO80211Controller *, ether_addr *, uint32_t, const char *)` está en
`0xffffff80023305f8`. Devuelve 0 en cada rama de fallo (`LODWORD(v6) = 0`: super `init`, `IOMalloc`,
`IOLockAlloc`, `bpfLock`, `bpfListHead`, array de procesos denegados) y 1 en la de éxito
(`LOBYTE(v6) = 1`). El propio encabezado la declara `bool` en `AWDLInterface.h:149`. El `extern` de
`AWDLInterface.cpp:47-49` la declara `void`, así que el valor se descarta y `initWithController`
devuelve `true` con el `init` base fallido.

### 3.2 Antes y después

`Source/AirLayer/AWDLInterface.cpp:47-49` (antes):

```cpp
void rtl_vif_init(void *self, IO80211Controller *ctrl, ether_addr *mac, uint32_t role,
                 const char *name)
	__asm("__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc");
```

Después:

```cpp
bool rtl_vif_init(void *self, IO80211Controller *ctrl, ether_addr *mac, uint32_t role,
                  const char *name)
	__asm("__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc");
```

`Source/AirLayer/AWDLInterface.cpp:110` (antes):

```cpp
	rtl_vif_init(this, ctrl, &ea, AWDL_ROLE, "awdl");
```

Después:

```cpp
	if (!rtl_vif_init(this, ctrl, &ea, AWDL_ROLE, "awdl")) {
		LOG("awdl: base init failed");
		return false;
	}
```

El cambio de tipo de retorno no altera el símbolo (el retorno no forma parte del mangling) y la
convención es la misma que ya usa el encabezado. El fallo llega al parche, que hace `terminate()` +
`release()` (`Tools/awdl_stage1_hook.patch:175-179`).

`?` `free()` sobre un `init` base a medias llama a `IO80211VirtualInterface::free`; es el patrón
habitual de IOKit (cada ivar se libera si existe), pero no se ha decompilado `free` para confirmar que
tolera todos los estados parciales de `init`.

## 4. Defecto 4: publicación con `setAwdlInterface`

### 4.1 Evidencia

`IO80211Controller::setAwdlInterface` (`0xffffff8002299c24`) y `getAwdlInterface`
(`0xffffff8002293256`) tienen **0 xrefs** en todo el kernelcache. El 97 no los consulta: lee
`getPrimarySkywalkInterface` (`AWDL_STAGE1_VIRTUAL_IF_PARENT.md` §3-§7), y el primario ya está adjunto
(`RTLAirLayer.cpp:1493`). El objeto `_awdl` del controlador sigue siendo necesario para el EEXIST y el
teardown; lo que sobra es la llamada a la familia.

### 4.2 Antes y después

`Source/AirLayer/AWDLInterface.cpp:52-53` (borrar):

```cpp
IOReturn rtl_ctrl_setAwdlInterface(IO80211Controller *ctrl, void *awdl)
	__asm("__ZN17IO80211Controller16setAwdlInterfaceEP23IO80211VirtualInterface");
```

`Source/AirLayer/AWDLInterface.cpp:268-273` (borrar el bloque entero):

```cpp
/* Publishes the interface on the controller, so getAwdlInterface() answers it.
 * The handler calls it once the start succeeded; NULL unpublishes. */
IOReturn rtlAwdlPublish(IO80211Controller *ctrl, RTLAwdInterface *awdl)
{
	return rtl_ctrl_setAwdlInterface(ctrl, awdl);
}
```

`Source/AirLayer/AWDLInterface.h:405-407` (borrar):

```cpp
/* IO80211Controller::setAwdlInterface, wrapped so the handler that creates the
 * interface does not carry the family symbol. NULL unpublishes it. */
IOReturn rtlAwdlPublish(IO80211Controller *ctrl, RTLAwdInterface *awdl);
```

`Tools/awdl_stage1_hook.patch:183` (borrar `rtlAwdlPublish(this, awdl);`) y `:200` (borrar
`rtlAwdlPublish(this, nullptr);`). El resto de `createAwdlInterface` y `destroyAwdlInterface`
no cambia: `_awdl` sigue guardando el objeto y `terminate()` + `release()` siguen siendo el teardown.

Conviene ajustar el comentario de cabecera de `AWDLInterface.h:1-22` («the interface exists and is
published») para no describir una publicación que ya no se hace.

`?` Si una etapa posterior necesita `getAwdlInterface` para otro cliente; hoy no hay ninguna
referencia en el kernelcache.

## 5. Defecto 5: los 50 overrides de `AWDLInterface.h:91-145`

No requieren cambio. Se compiló `AWDLInterface.cpp` y se contrastó cada símbolo indefinido con
`Docs/re/io80211_25.6.0/syms.txt`; los 93 símbolos de `IO80211VirtualInterface` que emite el objeto
tienen tipo `0f` y **ninguno** quedaría sin resolver. De los 50 declarados, 48 llegan al objeto (los
otros dos son `free` y `stop`, que `RTLAwdInterface` define) y los 48 son `0f`.

| Slot | Método | Símbolo exportado |
|---|---|---|
| `0x428` | `configureReport` | `0f ffffff80023343f2` |
| `0x430` | `updateReport` | `0f ffffff80023344e0` |
| `0x5C0` | `start` | `0f ffffff80023308aa` |
| `0x600` | `terminate` | `0f ffffff8002330c50` |
| `0x670` | `attach` | `0f ffffff800233023a` |
| `0x678` | `detach` | `0f ffffff800233027c` |
| `0x770` | `newUserClient` | `0f ffffff80023349d8` |
| `0x780` | `stringFromReturn` | `0f ffffff8002330b24` |
| `0x8E8` | `prepareBSDInterface` | `0f ffffff8002331ada` |
| `0x910` | `processBSDCommand` | `0f ffffff8002331b36` |
| `0x930` | `setInterfaceEnable` | `0f ffffff8002331ba6` |
| `0x9B8` | `getFeatureFlags` | `0f ffffff8002331b4a` |
| `0x9E0` | `bpfTap` | `0f ffffff8002330e42` |
| `0xA88` | `setMulticastAddresses` | `0f ffffff8002331c6c` |
| `0xB08` | `createPeerManager` | `0f ffffff80023308c2` |
| `0xB18` | `postMessage` | `0f ffffff8002333192` |
| `0xB38` | `logTxPacket` | `0f ffffff8002333fae` |
| `0xB40` | `logTxCompletionPacket` | `0f ffffff800233403a` |
| `0xB50` | `inputPacket` | `0f ffffff8002333cac` |
| `0xB60` | `logSkywalkTxReqPacket` | `0f ffffff800233218a` |
| `0xB68` | `pendingPackets` | `0f ffffff8002332378` |
| `0xB70` | `packetSpace` | `0f ffffff8002332446` |
| `0xB88` | `linkState` | `0f ffffff80023333f8` |
| `0xB90` | `setScanningState` | `0f ffffff8002331eb0` |
| `0xBB0` | `updateLinkParameters` | `0f ffffff8002331f70` |
| `0xBB8` | `updateInterfaceCoexRiskPct` | `0f ffffff8002332010` |
| `0xBE8` | `setInterfaceNF` | `0f ffffff8002333862` |
| `0xC00` | `setDebugFlags` | `0f ffffff8002333376` |
| `0xC08` | `debugFlags` | `0f ffffff80023333e2` |
| `0xC10` | `setInterfaceChipCounters` | `0f ffffff8002333884` |
| `0xC18` | `setInterfaceMIBdot11` | `0f ffffff80023338a2` |
| `0xC20` | `setFrameStats` | `0f ffffff80023338c0` |
| `0xC28` | `setInfraSpecificFrameStats` | `0f ffffff80023338de` |
| `0xC38` | `getWmeTxCounters` | `0f ffffff800233209e` |
| `0xC48` | `setWoWEnabled` | `0f ffffff80023333f2` |
| `0xC50` | `wowEnabled` | `0f ffffff8002334c34` |
| `0xC58` | `printDataPath` | `0f ffffff8002334344` |
| `0xC68` | `findOrCreateFlowQueue` | `0f ffffff800233270a` |
| `0xC70` | `findOrCreateFlowQueueWithCache` | `0f ffffff8002332c38` |
| `0xC78` | `findExistingFlowQueue` | `0f ffffff80023324e4` |
| `0xC80` | `removePacketQueue` | `0f ffffff80023322e6` |
| `0xC88` | `flushPacketQueues` | `0f ffffff8002332290` |
| `0xC90` | `cachePeer` | `0f ffffff80023324fc` |
| `0xC98` | `shouldLog` | `0f ffffff80023343d6` |
| `0xCA0` | `vlogDebug` | `0f ffffff800233436a` |
| `0xCA8` | `vlogDebugBPF` | `0f ffffff80023343d0` |
| `0xCC0` | `getP2PSkywalkPeerMgr` | `0f ffffff8002334c1a` |
| `0xCD0` | `findPeer` | `0f ffffff8002334ec6` |
| `0xDB8` | `getEventPipeSize` | `0f ffffff80023347d2` |
| `0xDC0` | `createEventPipe` | `0f ffffff80023347fc` |

Ninguno debe quitarse: son overrides de slots ya existentes y su único efecto es que la celda de la
vtable apunte a la implementación de la familia en lugar de a la de `IO80211SkywalkInterface` o
`IOService`. La comprobación de enlace que pide P0.4 se puede repetir sin IDA:

```sh
clang++ $CXXFLAGS -c Source/AirLayer/AWDLInterface.cpp -o /tmp/AWDLInterface.o
nm -u /tmp/AWDLInterface.o | grep '__ZN23IO80211VirtualInterface' |
while read s; do grep -q " 0f $s\$" Docs/re/io80211_25.6.0/syms.txt || echo "SIN 0f: $s"; done
```

`?` El listado de `nm` debe hacerse con el objeto recién compilado: si un override se elimina de la
cabecera, el compilador vuelve a resolver esa celda en la clase base y el fallo aparece como símbolo de
otra clase, no como este.

## 6. Defecto 6: headroom en `+0x3A` frente a `+0x3C`

### 6.1 Evidencia

`AppleBCMWLANCore::createProxInterface` (cabeza `0xffffff800165dcbe`) construye el `RegistrationInfo`
del rol 6 antes de llamar a `AppleBCMWLANProximityInterface::start`. Las direcciones de la tabla son
instrucciones dentro de esa función, no cabezas:

| Instrucción | Escritura |
|---|---|
| `0xffffff800165de82` | `[r12+30h] = r15` (puntero de prefijo) |
| `0xffffff800165de87` | `[r12+38h] = r14d` (unidad BSD) |
| `0xffffff800165de8c` | `word [r12+3Ch] = 40h` (**64: headroom**) |
| `0xffffff800165de94` | `[r12+40h] = eax` (flags) |

`IOSkywalkEthernetInterface::initRegistrationInfo` (cabeza `0xffffff8002b108e2`) escribe
`*(_DWORD *)(a2 + 56) = getBSDUnitNumber()` (`+0x38`) y **no** toca `+0x3C` (el `bzero` de 0x128 bytes
lo deja a 0). `IOSkywalkNetworkInterface::registerNetworkInterface` (cabeza `0xffffff8002b0a204`) copia
`a2[9]` y `a2[10]` (`+0x24` y `+0x28`) al descriptor (`+0x30`/`+0x34`), y compone el nombre con los
getters de la interfaz (`vtable+2536`/`+2544`), no con `+0x30`/`+0x38`; el `memmove` de 0x108 bytes
incluye `+0x3C`, así que el headroom viaja en la copia.

El código actual escribe `u16 64` en `+0x3A`:

- `+0x3A` es la mitad alta del `u32` de unidad en `+0x38`; con `unit = 0` en `:179`, el valor queda
  `0x00400000` (4 194 304) y el headroom real (`+0x3C`) sigue 0.
- El nombre de `awdl0` no cambia por este campo: lo fijan las properties `IOInterfaceNamePrefix` /
  `IOInterfaceUnit`, que vienen de los getters de la interfaz.
- La copia en `interface+0xC0` queda con unidad corrupta y headroom 0.

### 6.2 Antes y después

`Source/AirLayer/AWDLInterface.cpp:82` (antes):

```cpp
#define AWDL_REGINFO_HEADROOM   0x3a    /* u16, 64                           */
```

Después:

```cpp
#define AWDL_REGINFO_HEADROOM   0x3c    /* u16, 64; +0x38 is the BSD unit     */
```

`:180` no cambia:

```cpp
	*(uint16_t *)(_regInfo + AWDL_REGINFO_HEADROOM) = 64;
```

`?` No se ha localizado ningún lector de `+0x3C` en la copia de 0x108 bytes; el campo se siembra como
en el camino de referencia, pero su consumidor (si existe) no está identificado.

## 7. Orden de aplicación e independencia

| Orden | Defecto | Ficheros | Depende de |
|---:|---|---|---|
| 1 | 6, headroom | `AWDLInterface.cpp:82` | nada |
| 2 | 3, `rtl_vif_init` | `AWDLInterface.cpp:47-49`, `:110` | nada |
| 3 | 1, acción no-op | `AWDLInterface.h:396-397`, `AWDLInterface.cpp:44-46`, `:140`, `:228+` | nada; **bloqueante** |
| 4 | 2, rollback | `AWDLInterface.cpp:190-193`, `:201-203`, `:230-238`, `.h:396` | el 3 para poder probarlo (sin cola TX no hay datapath que retirar), pero las líneas son distintas |
| 5 | 4, `setAwdlInterface` | `AWDLInterface.cpp:52-53`, `:268-273`, `.h:405-407`, `awdl_stage1_hook.patch:183`, `:200` | nada |
| 6 | 5, overrides | ninguno: sólo la comprobación de `nm` | nada |

Independientes entre sí: los defectos 6, 3, 1 y 4. Los defectos 1 y 2 tocan la misma función
(`startWithRegistration`), pero líneas distintas: aplicar el 1 antes del 2. El defecto 4 es el único
que toca el parche y la cabecera a la vez; si se aplica sin el 1, el parche sigue compilando (el
símbolo deja de usarse).

Después de aplicar, y con OK expreso para compilar:

1. `./build_airlayer.sh` y comprobar que enlaza; si falla por un símbolo
   `__ZN23IO80211VirtualInterface...`, la tabla de §5 dice cuál y si es `0f`.
2. Repetir el `nm -u` de §5 sobre `Build/obj_airlayer/AWDLInterface.o`.
3. Cargar con `rtlawdl=1` y ejecutar T3.5 (subir `awdl0` y provocar IPv6/ND): con el no-op los
   paquetes se completan y se descartan, y no debe haber pánico; con el rollback, un fallo de registro
   no debe dejar eventos colgantes en el work loop.

## 8. Pendientes que esta propuesta no cierra

- `?` Lector de `+0x3C` en la copia del `RegistrationInfo` (§6).
- `?` `deregisterEthernetInterface` y su argumento en el fallo con nombre vacío (§2).
- `?` Tolerancia de `IO80211VirtualInterface::free` a un `init` base a medias (§3).
- `?` Vía `checkForWork` de la cola TX completion con acción nula (§1).
- `?` Necesidad futura de `getAwdlInterface` (§4).
