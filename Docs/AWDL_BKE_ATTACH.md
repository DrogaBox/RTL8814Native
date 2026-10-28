# Adjunto rea/IDA a BootKernelExtensions.kc (kernelcache macOS 26)

Registro de la sesión de re-adjunto del adaptador rea/IDA (modo `attached`) a la base que la GUI de IDA tiene abierta desde el cambio del 2026-10-28: `Build/ida_awdl/BootKernelExtensions.i64` (737 MB), creada desde el kernelcache de macOS 26. Solo lectura; no se abrió ningún `.i64` por ruta. Lo no visto va como `?`.

## 1) Identidad del módulo (metadatos reportados por el upstream en cada llamada)

| Campo | Valor |
|---|---|
| path | `/System/Library/KernelCollections/BootKernelExtensions.kc` |
| module | `BootKernelExtensions.kc` |
| base | `0xffffff8000200000` |
| size | `0x3fe506c` (~67 MB) |
| filesize | `0x4074000` |
| md5 | `f489ff8be80e6013a1bf47561dcaef45` |
| sha256 | `21d6da1c4c3e7b72dddf5144da8a4c4b48e294249a16018a5006b61bb2155ccc` |
| crc32 | `0xd2ee2fed` |
| upstream | ida-pro-mcp 1.30.0, adapter rea ida v1, modo `attached`, `cache_policy: live` |

El md5 coincide exactamente con el del archivo de sistema `/System/Library/KernelCollections/BootKernelExtensions.kc` (67.584.000 B, fecha 24-sep): **el IDB fue creado desde ese kernelcache** y el archivo original existe, a diferencia del caso wifip2pd.

## 2) Problema encontrado y resolución del re-adjunto

Tras cambiar la base en la GUI, la sesión REA seguía ligada a `wifip2pd.x86_64` (`binary_session` reportaba `open: true` con el path viejo). Con ese binding stale, toda operación al upstream fallaba:

- `list_documents` → error `unreadable_output` / `execution_failure` "Analysis returned an unreadable result. Run `rea doctor`…" (sin `diagnostics.reason`).
- `procedure_address("0xffffff80022886a8")` y `("0xffffff80016788f4")` → mismo error `unreadable_output`.

**Resolución:** `open_binary(path="/System/Library/KernelCollections/BootKernelExtensions.kc", provider_id="ida")`. El adaptador validó la identidad contra la base abierta en la GUI (coincide) y re-ató la sesión; a partir de ahí cada resultado trae `metadata_before/metadata_after` con `module: BootKernelExtensions.kc`, lo que permite verificar por llamada qué base respondió. No se abrió ninguna base nueva ni headless.

## 3) Anclas verificadas (direcciones de `AWDL_ROLE6_RE.md`, ahora con símbolo y tamaño)

| VA | Símbolo (nombre real del IDB) | Tamaño | Evidencia |
|---|---|---:|---|
| `0xffffff80022886a8` | `__ZL20setVIRTUAL_IF_CREATEP17IO80211ControllerP23IO80211SkywalkInterfaceP20IO80211APIUserClientP13apple80211req` | `0xbe` | `procedure_address` → `get_function_by_address/name`, doc `AWDL_ROLE6_RE.md` §1 |
| `0xffffff80016788f4` | `__ZN16AppleBCMWLANCore20setVIRTUAL_IF_CREATEEP30apple80211_virt_if_create_data` | `0x6d2` | `procedure_address`, doc `AWDL_ROLE6_RE.md` §2 |

Notas de las firmas observadas (macOS 26): el `setVIRTUAL_IF_CREATE` estático de IO80211Family recibe `IO80211Controller*, IO80211SkywalkInterface*, IO80211APIUserClient*, apple80211req*` (el doc previo hablaba de longitud 32 y puntero de datos en `apple80211req`; la firma del IDB añade el Skywalk interface explícito — solo lo que muestra el símbolo, sin interpretar). El override de `AppleBCMWLANCore` recibe `apple80211_virt_if_create_data*` — coherente con la lectura del rol como dword en `request+12` documentada; el contenido de la función no se revisó en esta pasada (`?`).

## 4) Limitaciones operativas observadas en esta base

- `binary_overview` **no responde en el timeout de 30 s** (inventario completo de funciones/strings sobre el IDB de 737 MB). Conteo exacto de procedimientos/strings: `?`. Usar consultas dirigidas (`procedure_address`, `xrefs`, `procedure_callees/callers`, `procedure_pseudo_code`) en lugar del overview.
- `read_bytes` sigue sin estar declarado por el proveedor ida (mismo límite que con wifip2pd); para bytes crudos, mapear VA→offset de archivo con `otool -l` sobre el `.kc` (67 MB, viable).
- `procedure_info` no está declarado por este proveedor (error `capability_unavailable`, "operation is not declared by this provider"); usar `procedure_address` para entrada canónica y tamaño.
- `search_procedures`/`search_strings` no filtran (comportamiento ya conocido del adaptador); resolver por nombre exacto con `procedure_address` o extraer strings del `.kc` desde el archivo.
- Las direcciones del espacio kernel son del segmento base `0xffffff8000200000`; para lecturas de archivo, el mapeo VA→file offset de este kernelcache no fue calculado todavía (`?`).

## 5) Estado

Sesión REA re-atada a `BootKernelExtensions.kc` y operativa (2/2 anclas resueltas con símbolo). Para volver a wifip2pd cuando la GUI cambie de base: mismo procedimiento con `open_binary("…/Build/ida_awdl/wifip2pd.x86_64", provider_id="ida")`.
