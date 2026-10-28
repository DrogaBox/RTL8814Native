# Slots de las vtables AWDL en IO80211Family

## Método y límites

Inventario derivado sólo de los artefactos guardados: `Build/ida_awdl/awdl_vtable_probe.log`, `Build/ida_awdl/class_meta_probe.log`, `Docs/re/io80211_25.6.0/syms.txt` y `Docs/re/io80211_25.6.0/syms_bcm.txt`. No se consultó IDA/MCP ni se generaron nuevos volcados.

Los índices reproducen el índice del probe de vtables: la palabra `__ZTV + 0x00` y `+0x08` son cabecera ABI, no métodos; el primer slot de función aparece en índice 2 (`__ZTV + 0x10`). “Dirección de entrada” es la dirección de la celda del puntero, calculada como `base de __ZTV + índice*8`; “target VA” es el valor/puntero que el probe leyó de esa celda. Los nombres sólo se muestran si el target VA aparece en uno de los symbol dumps indicados; si no, se escribe `?` aunque el probe lo haya nombrado. Los tipos `0e`/`0f` se preservan entre paréntesis.

Los slots de instancia observables son `IO80211VirtualInterface` 2–506 y `IO80211AWDLProtocol` 2–569. El probe de comparación conserva el rango puro AWDL 507–569; 512–569 se añadieron desde esa comparación porque el probe de vtable simple escanea sólo hasta índice 511. Las palabras posteriores que el script encuentra (ceros, metaclass u objetos vecinos) no se consideran slots de instancia.

El cruce con `AppleBCMWLANProximityInterface` se limita al símbolo de vtable listado en `syms_bcm.txt` (`0xffffff80017f5b68`, tipo `0e`). Ningún artefacto guardado contiene las entradas de esa vtable: por tanto, el target Broadcom equivalente y su símbolo quedan `?` para cada slot puro. No se asignan nombres por orden o similitud con métodos no virtuales.

## `IO80211VirtualInterface`

Base de vtable: `0xffffff8002472b78` (`__ZTV23IO80211VirtualInterface`, `awdl_vtable_probe.log:1071`). Slots listados: 2–506; slot puro observado: 490.

| Índice | Dirección de entrada | Target VA | Símbolo en dumps | Tipo | Slot equivalente en Proximity |
|---:|---:|---:|---|---|---|
| 2 | `0xffffff8002472b88` | `0xffffff800239f11e` | `__ZN23IO80211VirtualInterfaceD1Ev` (0f) | — | — |
| 3 | `0xffffff8002472b90` | `0xffffff800239f124` | `__ZN23IO80211VirtualInterfaceD0Ev` (0f) | — | — |
| 4 | `0xffffff8002472b98` | `0xffffff8000a1c520` | `?` | — | — |
| 5 | `0xffffff8002472ba0` | `0xffffff8000a1c540` | `?` | — | — |
| 6 | `0xffffff8002472ba8` | `0xffffff8000a1c550` | `?` | — | — |
| 7 | `0xffffff8002472bb0` | `0xffffff8000a1c560` | `?` | — | — |
| 8 | `0xffffff8002472bb8` | `0xffffff8000a1c570` | `?` | — | — |
| 9 | `0xffffff8002472bc0` | `0xffffff8002330068` | `__ZNK23IO80211VirtualInterface12getMetaClassEv` (0f) | — | — |
| 10 | `0xffffff8002472bc8` | `0xffffff8000a1a7e0` | `?` | — | — |
| 11 | `0xffffff8002472bd0` | `0xffffff8000a1c6f0` | `?` | — | — |
| 12 | `0xffffff8002472bd8` | `0xffffff8000a1c760` | `?` | — | — |
| 13 | `0xffffff8002472be0` | `0xffffff8000a1c780` | `?` | — | — |
| 14 | `0xffffff8002472be8` | `0xffffff8000a682b0` | `?` | — | — |
| 15 | `0xffffff8002472bf0` | `0xffffff8000bb4d60` | `?` | — | — |
| 16 | `0xffffff8002472bf8` | `0xffffff8000bb4d90` | `?` | — | — |
| 17 | `0xffffff8002472c00` | `0xffffff8000bb4dc0` | `?` | — | — |
| 18 | `0xffffff8002472c08` | `0xffffff8000bb4df0` | `?` | — | — |
| 19 | `0xffffff8002472c10` | `0xffffff80022f568e` | `__ZN23IO80211SkywalkInterface4initEv` (0f) | — | — |
| 20 | `0xffffff8002472c18` | `0xffffff8002330b8c` | `__ZN23IO80211VirtualInterface4freeEv` (0f) | — | — |
| 21 | `0xffffff8002472c20` | `0xffffff8000bb5060` | `?` | — | — |
| 22 | `0xffffff8002472c28` | `0xffffff8000bb5080` | `?` | — | — |
| 23 | `0xffffff8002472c30` | `0xffffff8000bb50a0` | `?` | — | — |
| 24 | `0xffffff8002472c38` | `0xffffff8000bb50c0` | `?` | — | — |
| 25 | `0xffffff8002472c40` | `0xffffff8000bb50e0` | `?` | — | — |
| 26 | `0xffffff8002472c48` | `0xffffff8000bb5100` | `?` | — | — |
| 27 | `0xffffff8002472c50` | `0xffffff8000bb5120` | `?` | — | — |
| 28 | `0xffffff8002472c58` | `0xffffff8000bb5140` | `?` | — | — |
| 29 | `0xffffff8002472c60` | `0xffffff8000bb5160` | `?` | — | — |
| 30 | `0xffffff8002472c68` | `0xffffff8000bb5180` | `?` | — | — |
| 31 | `0xffffff8002472c70` | `0xffffff8000bb51a0` | `?` | — | — |
| 32 | `0xffffff8002472c78` | `0xffffff8000bb51c0` | `?` | — | — |
| 33 | `0xffffff8002472c80` | `0xffffff8000bb51e0` | `?` | — | — |
| 34 | `0xffffff8002472c88` | `0xffffff8000bb5200` | `?` | — | — |
| 35 | `0xffffff8002472c90` | `0xffffff8000bb5220` | `?` | — | — |
| 36 | `0xffffff8002472c98` | `0xffffff8000bb5240` | `?` | — | — |
| 37 | `0xffffff8002472ca0` | `0xffffff8000a8c310` | `?` | — | — |
| 38 | `0xffffff8002472ca8` | `0xffffff8000a8c470` | `?` | — | — |
| 39 | `0xffffff8002472cb0` | `0xffffff8000a8c5d0` | `?` | — | — |
| 40 | `0xffffff8002472cb8` | `0xffffff8000a8c730` | `?` | — | — |
| 41 | `0xffffff8002472cc0` | `0xffffff8000a8c7a0` | `?` | — | — |
| 42 | `0xffffff8002472cc8` | `0xffffff8000a8c810` | `?` | — | — |
| 43 | `0xffffff8002472cd0` | `0xffffff8000bb6770` | `?` | — | — |
| 44 | `0xffffff8002472cd8` | `0xffffff8000bb6790` | `?` | — | — |
| 45 | `0xffffff8002472ce0` | `0xffffff8000bb67b0` | `?` | — | — |
| 46 | `0xffffff8002472ce8` | `0xffffff8000bb67d0` | `?` | — | — |
| 47 | `0xffffff8002472cf0` | `0xffffff8000bb67f0` | `?` | — | — |
| 48 | `0xffffff8002472cf8` | `0xffffff8000bb6810` | `?` | — | — |
| 49 | `0xffffff8002472d00` | `0xffffff8000bb6830` | `?` | — | — |
| 50 | `0xffffff8002472d08` | `0xffffff8000bb6850` | `?` | — | — |
| 51 | `0xffffff8002472d10` | `0xffffff8000bb6870` | `?` | — | — |
| 52 | `0xffffff8002472d18` | `0xffffff8000bb6890` | `?` | — | — |
| 53 | `0xffffff8002472d20` | `0xffffff8000bb68b0` | `?` | — | — |
| 54 | `0xffffff8002472d28` | `0xffffff8000bb68d0` | `?` | — | — |
| 55 | `0xffffff8002472d30` | `0xffffff8000bb68f0` | `?` | — | — |
| 56 | `0xffffff8002472d38` | `0xffffff8000bb6910` | `?` | — | — |
| 57 | `0xffffff8002472d40` | `0xffffff8000bb6930` | `?` | — | — |
| 58 | `0xffffff8002472d48` | `0xffffff8000bb6950` | `?` | — | — |
| 59 | `0xffffff8002472d50` | `0xffffff8000bb6970` | `?` | — | — |
| 60 | `0xffffff8002472d58` | `0xffffff8000bb6990` | `?` | — | — |
| 61 | `0xffffff8002472d60` | `0xffffff8000bb69b0` | `?` | — | — |
| 62 | `0xffffff8002472d68` | `0xffffff8000bb69d0` | `?` | — | — |
| 63 | `0xffffff8002472d70` | `0xffffff8000bb69f0` | `?` | — | — |
| 64 | `0xffffff8002472d78` | `0xffffff8000bb6a10` | `?` | — | — |
| 65 | `0xffffff8002472d80` | `0xffffff8000bb6a30` | `?` | — | — |
| 66 | `0xffffff8002472d88` | `0xffffff8000bb6a50` | `?` | — | — |
| 67 | `0xffffff8002472d90` | `0xffffff8000bb6a70` | `?` | — | — |
| 68 | `0xffffff8002472d98` | `0xffffff8000bb6a90` | `?` | — | — |
| 69 | `0xffffff8002472da0` | `0xffffff8000bb6ab0` | `?` | — | — |
| 70 | `0xffffff8002472da8` | `0xffffff8000bb6ad0` | `?` | — | — |
| 71 | `0xffffff8002472db0` | `0xffffff8000bb6af0` | `?` | — | — |
| 72 | `0xffffff8002472db8` | `0xffffff8000bb6b10` | `?` | — | — |
| 73 | `0xffffff8002472dc0` | `0xffffff8000bb6b30` | `?` | — | — |
| 74 | `0xffffff8002472dc8` | `0xffffff8000bb6b50` | `?` | — | — |
| 75 | `0xffffff8002472dd0` | `0xffffff8002b10820` | `?` | — | — |
| 76 | `0xffffff8002472dd8` | `0xffffff8000a8cab0` | `?` | — | — |
| 77 | `0xffffff8002472de0` | `0xffffff8000a8cb50` | `?` | — | — |
| 78 | `0xffffff8002472de8` | `0xffffff8000a8cc70` | `?` | — | — |
| 79 | `0xffffff8002472df0` | `0xffffff8000a8ccc0` | `?` | — | — |
| 80 | `0xffffff8002472df8` | `0xffffff8000a8cd10` | `?` | — | — |
| 81 | `0xffffff8002472e00` | `0xffffff8000a8cd80` | `?` | — | — |
| 82 | `0xffffff8002472e08` | `0xffffff8000a8cdf0` | `?` | — | — |
| 83 | `0xffffff8002472e10` | `0xffffff8000a8ce90` | `?` | — | — |
| 84 | `0xffffff8002472e18` | `0xffffff8000a8cf30` | `?` | — | — |
| 85 | `0xffffff8002472e20` | `0xffffff8000a8cfb0` | `?` | — | — |
| 86 | `0xffffff8002472e28` | `0xffffff8000a8cff0` | `?` | — | — |
| 87 | `0xffffff8002472e30` | `0xffffff8000a8d030` | `?` | — | — |
| 88 | `0xffffff8002472e38` | `0xffffff8000a8d0b0` | `?` | — | — |
| 89 | `0xffffff8002472e40` | `0xffffff8000a8d0f0` | `?` | — | — |
| 90 | `0xffffff8002472e48` | `0xffffff8000a8d130` | `?` | — | — |
| 91 | `0xffffff8002472e50` | `0xffffff8000a8d290` | `?` | — | — |
| 92 | `0xffffff8002472e58` | `0xffffff8000a8d3f0` | `?` | — | — |
| 93 | `0xffffff8002472e60` | `0xffffff8000a8d550` | `?` | — | — |
| 94 | `0xffffff8002472e68` | `0xffffff8000a8d5e0` | `?` | — | — |
| 95 | `0xffffff8002472e70` | `0xffffff8000a8d670` | `?` | — | — |
| 96 | `0xffffff8002472e78` | `0xffffff8000a8d700` | `?` | — | — |
| 97 | `0xffffff8002472e80` | `0xffffff8000a92b40` | `?` | — | — |
| 98 | `0xffffff8002472e88` | `0xffffff8000a8d860` | `?` | — | — |
| 99 | `0xffffff8002472e90` | `0xffffff8000a8d870` | `?` | — | — |
| 100 | `0xffffff8002472e98` | `0xffffff8000a8d9a0` | `?` | — | — |
| 101 | `0xffffff8002472ea0` | `0xffffff8000a8daa0` | `?` | — | — |
| 102 | `0xffffff8002472ea8` | `0xffffff8000a8dad0` | `?` | — | — |
| 103 | `0xffffff8002472eb0` | `0xffffff8000a8dc00` | `?` | — | — |
| 104 | `0xffffff8002472eb8` | `0xffffff8000a8dd00` | `?` | — | — |
| 105 | `0xffffff8002472ec0` | `0xffffff8000a8dd30` | `?` | — | — |
| 106 | `0xffffff8002472ec8` | `0xffffff8000a8de60` | `?` | — | — |
| 107 | `0xffffff8002472ed0` | `0xffffff8000a8df90` | `?` | — | — |
| 108 | `0xffffff8002472ed8` | `0xffffff8000a8e0c0` | `?` | — | — |
| 109 | `0xffffff8002472ee0` | `0xffffff8000a8e1c0` | `?` | — | — |
| 110 | `0xffffff8002472ee8` | `0xffffff8000a8e4b0` | `?` | — | — |
| 111 | `0xffffff8002472ef0` | `0xffffff8000a8e6b0` | `?` | — | — |
| 112 | `0xffffff8002472ef8` | `0xffffff8000a8e7c0` | `?` | — | — |
| 113 | `0xffffff8002472f00` | `0xffffff8000a8e9b0` | `?` | — | — |
| 114 | `0xffffff8002472f08` | `0xffffff8000a8ea30` | `?` | — | — |
| 115 | `0xffffff8002472f10` | `0xffffff8000a8ebc0` | `?` | — | — |
| 116 | `0xffffff8002472f18` | `0xffffff8000a8ec60` | `?` | — | — |
| 117 | `0xffffff8002472f20` | `0xffffff8000a8ed10` | `?` | — | — |
| 118 | `0xffffff8002472f28` | `0xffffff8000a8ee60` | `?` | — | — |
| 119 | `0xffffff8002472f30` | `0xffffff8000a8eec0` | `?` | — | — |
| 120 | `0xffffff8002472f38` | `0xffffff8000a8efa0` | `?` | — | — |
| 121 | `0xffffff8002472f40` | `0xffffff8000a8eff0` | `?` | — | — |
| 122 | `0xffffff8002472f48` | `0xffffff8000a8f030` | `?` | — | — |
| 123 | `0xffffff8002472f50` | `0xffffff8000a8f0b0` | `?` | — | — |
| 124 | `0xffffff8002472f58` | `0xffffff8000a8f120` | `?` | — | — |
| 125 | `0xffffff8002472f60` | `0xffffff8000a8f170` | `?` | — | — |
| 126 | `0xffffff8002472f68` | `0xffffff8000a8f720` | `?` | — | — |
| 127 | `0xffffff8002472f70` | `0xffffff8000a8f810` | `?` | — | — |
| 128 | `0xffffff8002472f78` | `0xffffff8000a92b50` | `?` | — | — |
| 129 | `0xffffff8002472f80` | `0xffffff8000a92bb0` | `?` | — | — |
| 130 | `0xffffff8002472f88` | `0xffffff8002b0bfea` | `?` | — | — |
| 131 | `0xffffff8002472f90` | `0xffffff8002afbe12` | `?` | — | — |
| 132 | `0xffffff8002472f98` | `0xffffff8000abc370` | `?` | — | — |
| 133 | `0xffffff8002472fa0` | `0xffffff8000abe4a0` | `?` | — | — |
| 134 | `0xffffff8002472fa8` | `0xffffff8000a92d20` | `?` | — | — |
| 135 | `0xffffff8002472fb0` | `0xffffff80023343f2` | `__ZN23IO80211VirtualInterface15configureReportEP19IOReportChannelListjPvS2_` (0f) | — | — |
| 136 | `0xffffff8002472fb8` | `0xffffff80023344e0` | `__ZN23IO80211VirtualInterface12updateReportEP19IOReportChannelListjPvS2_` (0f) | — | — |
| 137 | `0xffffff8002472fc0` | `0xffffff8000bb6ba0` | `?` | — | — |
| 138 | `0xffffff8002472fc8` | `0xffffff8000bb6bc0` | `?` | — | — |
| 139 | `0xffffff8002472fd0` | `0xffffff8000bb6be0` | `?` | — | — |
| 140 | `0xffffff8002472fd8` | `0xffffff8000bb6c00` | `?` | — | — |
| 141 | `0xffffff8002472fe0` | `0xffffff8000bb6c20` | `?` | — | — |
| 142 | `0xffffff8002472fe8` | `0xffffff8000bb6c40` | `?` | — | — |
| 143 | `0xffffff8002472ff0` | `0xffffff8000bb6c60` | `?` | — | — |
| 144 | `0xffffff8002472ff8` | `0xffffff8000bb6c80` | `?` | — | — |
| 145 | `0xffffff8002473000` | `0xffffff8000bb6ca0` | `?` | — | — |
| 146 | `0xffffff8002473008` | `0xffffff8000bb6cc0` | `?` | — | — |
| 147 | `0xffffff8002473010` | `0xffffff8000bb6ce0` | `?` | — | — |
| 148 | `0xffffff8002473018` | `0xffffff8000bb6d00` | `?` | — | — |
| 149 | `0xffffff8002473020` | `0xffffff8000bb6d20` | `?` | — | — |
| 150 | `0xffffff8002473028` | `0xffffff8000bb6d40` | `?` | — | — |
| 151 | `0xffffff8002473030` | `0xffffff8000bb6d60` | `?` | — | — |
| 152 | `0xffffff8002473038` | `0xffffff8000bb6d80` | `?` | — | — |
| 153 | `0xffffff8002473040` | `0xffffff8000bb6da0` | `?` | — | — |
| 154 | `0xffffff8002473048` | `0xffffff8000bb6dc0` | `?` | — | — |
| 155 | `0xffffff8002473050` | `0xffffff8000bb6de0` | `?` | — | — |
| 156 | `0xffffff8002473058` | `0xffffff8000bb6e00` | `?` | — | — |
| 157 | `0xffffff8002473060` | `0xffffff8000bb6e20` | `?` | — | — |
| 158 | `0xffffff8002473068` | `0xffffff8000bb6e40` | `?` | — | — |
| 159 | `0xffffff8002473070` | `0xffffff8000bb6e60` | `?` | — | — |
| 160 | `0xffffff8002473078` | `0xffffff8000bb6e80` | `?` | — | — |
| 161 | `0xffffff8002473080` | `0xffffff8000bb6ea0` | `?` | — | — |
| 162 | `0xffffff8002473088` | `0xffffff8000bb6ec0` | `?` | — | — |
| 163 | `0xffffff8002473090` | `0xffffff8000bb6ee0` | `?` | — | — |
| 164 | `0xffffff8002473098` | `0xffffff8000bb6f00` | `?` | — | — |
| 165 | `0xffffff80024730a0` | `0xffffff8000bb6f20` | `?` | — | — |
| 166 | `0xffffff80024730a8` | `0xffffff8000bb6f40` | `?` | — | — |
| 167 | `0xffffff80024730b0` | `0xffffff8000bb6f60` | `?` | — | — |
| 168 | `0xffffff80024730b8` | `0xffffff8000bb6f80` | `?` | — | — |
| 169 | `0xffffff80024730c0` | `0xffffff8000bb6fa0` | `?` | — | — |
| 170 | `0xffffff80024730c8` | `0xffffff8000bb6fc0` | `?` | — | — |
| 171 | `0xffffff80024730d0` | `0xffffff8000bb6fe0` | `?` | — | — |
| 172 | `0xffffff80024730d8` | `0xffffff8000bb7000` | `?` | — | — |
| 173 | `0xffffff80024730e0` | `0xffffff8000bb7020` | `?` | — | — |
| 174 | `0xffffff80024730e8` | `0xffffff8000bb7040` | `?` | — | — |
| 175 | `0xffffff80024730f0` | `0xffffff8000bb7060` | `?` | — | — |
| 176 | `0xffffff80024730f8` | `0xffffff8000bb7080` | `?` | — | — |
| 177 | `0xffffff8002473100` | `0xffffff8000bb70a0` | `?` | — | — |
| 178 | `0xffffff8002473108` | `0xffffff8000bb70c0` | `?` | — | — |
| 179 | `0xffffff8002473110` | `0xffffff8000bb70e0` | `?` | — | — |
| 180 | `0xffffff8002473118` | `0xffffff8000bb7100` | `?` | — | — |
| 181 | `0xffffff8002473120` | `0xffffff8000bb7120` | `?` | — | — |
| 182 | `0xffffff8002473128` | `0xffffff8000bb7140` | `?` | — | — |
| 183 | `0xffffff8002473130` | `0xffffff8000a93240` | `?` | — | — |
| 184 | `0xffffff8002473138` | `0xffffff8000a93250` | `?` | — | — |
| 185 | `0xffffff8002473140` | `0xffffff8000a93450` | `?` | — | — |
| 186 | `0xffffff8002473148` | `0xffffff80023308aa` | `__ZN23IO80211VirtualInterface5startEP9IOService` (0f) | — | — |
| 187 | `0xffffff8002473150` | `0xffffff8002330cee` | `__ZN23IO80211VirtualInterface4stopEP9IOService` (0f) | — | — |
| 188 | `0xffffff8002473158` | `0xffffff8000a934a0` | `?` | — | — |
| 189 | `0xffffff8002473160` | `0xffffff8000a935c0` | `?` | — | — |
| 190 | `0xffffff8002473168` | `0xffffff8000a936d0` | `?` | — | — |
| 191 | `0xffffff8002473170` | `0xffffff8002afbc08` | `?` | — | — |
| 192 | `0xffffff8002473178` | `0xffffff8002afbc54` | `?` | — | — |
| 193 | `0xffffff8002473180` | `0xffffff8002afbc9c` | `?` | — | — |
| 194 | `0xffffff8002473188` | `0xffffff8002330c50` | `__ZN23IO80211VirtualInterface9terminateEj` (0f) | — | — |
| 195 | `0xffffff8002473190` | `0xffffff8000a937c0` | `?` | — | — |
| 196 | `0xffffff8002473198` | `0xffffff8000a939d0` | `?` | — | — |
| 197 | `0xffffff80024731a0` | `0xffffff8000a93df0` | `?` | — | — |
| 198 | `0xffffff80024731a8` | `0xffffff8000a94000` | `?` | — | — |
| 199 | `0xffffff80024731b0` | `0xffffff8000a94050` | `?` | — | — |
| 200 | `0xffffff80024731b8` | `0xffffff8000a94060` | `?` | — | — |
| 201 | `0xffffff80024731c0` | `0xffffff8000a940a0` | `?` | — | — |
| 202 | `0xffffff80024731c8` | `0xffffff8000a940c0` | `?` | — | — |
| 203 | `0xffffff80024731d0` | `0xffffff8000a940d0` | `?` | — | — |
| 204 | `0xffffff80024731d8` | `0xffffff8000a94110` | `?` | — | — |
| 205 | `0xffffff80024731e0` | `0xffffff8000a94250` | `?` | — | — |
| 206 | `0xffffff80024731e8` | `0xffffff8000a942c0` | `?` | — | — |
| 207 | `0xffffff80024731f0` | `0xffffff8000a94330` | `?` | — | — |
| 208 | `0xffffff80024731f8` | `0xffffff800233023a` | `__ZN23IO80211VirtualInterface6attachEP9IOService` (0f) | — | — |
| 209 | `0xffffff8002473200` | `0xffffff800233027c` | `__ZN23IO80211VirtualInterface6detachEP9IOService` (0f) | — | — |
| 210 | `0xffffff8002473208` | `0xffffff8000a94a10` | `?` | — | — |
| 211 | `0xffffff8002473210` | `0xffffff8000a94a60` | `?` | — | — |
| 212 | `0xffffff8002473218` | `0xffffff8000a94a90` | `?` | — | — |
| 213 | `0xffffff8002473220` | `0xffffff8000a94ab0` | `?` | — | — |
| 214 | `0xffffff8002473228` | `0xffffff8000a94ba0` | `?` | — | — |
| 215 | `0xffffff8002473230` | `0xffffff8000a94bc0` | `?` | — | — |
| 216 | `0xffffff8002473238` | `0xffffff8000a94be0` | `?` | — | — |
| 217 | `0xffffff8002473240` | `0xffffff8000a94cd0` | `?` | — | — |
| 218 | `0xffffff8002473248` | `0xffffff8000a94df0` | `?` | — | — |
| 219 | `0xffffff8002473250` | `0xffffff8000a94e70` | `?` | — | — |
| 220 | `0xffffff8002473258` | `0xffffff8000a94e80` | `?` | — | — |
| 221 | `0xffffff8002473260` | `0xffffff8000a94ee0` | `?` | — | — |
| 222 | `0xffffff8002473268` | `0xffffff8000a94f50` | `?` | — | — |
| 223 | `0xffffff8002473270` | `0xffffff8000a94f90` | `?` | — | — |
| 224 | `0xffffff8002473278` | `0xffffff8000a94fe0` | `?` | — | — |
| 225 | `0xffffff8002473280` | `0xffffff8000a95000` | `?` | — | — |
| 226 | `0xffffff8002473288` | `0xffffff8000a950c0` | `?` | — | — |
| 227 | `0xffffff8002473290` | `0xffffff8000a95150` | `?` | — | — |
| 228 | `0xffffff8002473298` | `0xffffff8000a951f0` | `?` | — | — |
| 229 | `0xffffff80024732a0` | `0xffffff8000a95260` | `?` | — | — |
| 230 | `0xffffff80024732a8` | `0xffffff8000a952d0` | `?` | — | — |
| 231 | `0xffffff80024732b0` | `0xffffff8000a95340` | `?` | — | — |
| 232 | `0xffffff80024732b8` | `0xffffff8000a95350` | `?` | — | — |
| 233 | `0xffffff80024732c0` | `0xffffff8000a95360` | `?` | — | — |
| 234 | `0xffffff80024732c8` | `0xffffff8000a95630` | `?` | — | — |
| 235 | `0xffffff80024732d0` | `0xffffff8000a95680` | `?` | — | — |
| 236 | `0xffffff80024732d8` | `0xffffff8000a95760` | `?` | — | — |
| 237 | `0xffffff80024732e0` | `0xffffff8000a95780` | `?` | — | — |
| 238 | `0xffffff80024732e8` | `0xffffff8000a957a0` | `?` | — | — |
| 239 | `0xffffff80024732f0` | `0xffffff8000a957f0` | `?` | — | — |
| 240 | `0xffffff80024732f8` | `0xffffff80023349d8` | `__ZN23IO80211VirtualInterface13newUserClientEP4taskPvjP12OSDictionaryPP12IOUserClient` (0f) | — | — |
| 241 | `0xffffff8002473300` | `0xffffff8000a95a10` | `?` | — | — |
| 242 | `0xffffff8002473308` | `0xffffff8002330b24` | `__ZN23IO80211VirtualInterface16stringFromReturnEi` (0f) | — | — |
| 243 | `0xffffff8002473310` | `0xffffff80022f997c` | `__ZN23IO80211SkywalkInterface15errnoFromReturnEi` (0f) | — | — |
| 244 | `0xffffff8002473318` | `0xffffff8000aab950` | `?` | — | — |
| 245 | `0xffffff8002473320` | `0xffffff8000ab9a20` | `?` | — | — |
| 246 | `0xffffff8002473328` | `0xffffff8002b0b8b0` | `?` | — | — |
| 247 | `0xffffff8002473330` | `0xffffff8000aba9f0` | `?` | — | — |
| 248 | `0xffffff8002473338` | `0xffffff8000abb3f0` | `?` | — | — |
| 249 | `0xffffff8002473340` | `0xffffff8000abc140` | `?` | — | — |
| 250 | `0xffffff8002473348` | `0xffffff8002b0b8f6` | `?` | — | — |
| 251 | `0xffffff8002473350` | `0xffffff8000abc3c0` | `?` | — | — |
| 252 | `0xffffff8002473358` | `0xffffff8000ab9d40` | `?` | — | — |
| 253 | `0xffffff8002473360` | `0xffffff8000aba840` | `?` | — | — |
| 254 | `0xffffff8002473368` | `0xffffff8000abc1e0` | `?` | — | — |
| 255 | `0xffffff8002473370` | `0xffffff80022f8782` | `__ZN23IO80211SkywalkInterface13setPowerStateEmP9IOService` (0f) | — | — |
| 256 | `0xffffff8002473378` | `0xffffff80022f8a68` | `__ZN23IO80211SkywalkInterface27maxCapabilityForDomainStateEm` (0f) | — | — |
| 257 | `0xffffff8002473380` | `0xffffff80022f8a32` | `__ZN23IO80211SkywalkInterface31initialPowerStateForDomainStateEm` (0f) | — | — |
| 258 | `0xffffff8002473388` | `0xffffff8000abe410` | `?` | — | — |
| 259 | `0xffffff8002473390` | `0xffffff8000abe470` | `?` | — | — |
| 260 | `0xffffff8002473398` | `0xffffff8000abe480` | `?` | — | — |
| 261 | `0xffffff80024733a0` | `0xffffff8000abcdd0` | `?` | — | — |
| 262 | `0xffffff80024733a8` | `0xffffff8000abcde0` | `?` | — | — |
| 263 | `0xffffff80024733b0` | `0xffffff8000abd130` | `?` | — | — |
| 264 | `0xffffff80024733b8` | `0xffffff8000abd750` | `?` | — | — |
| 265 | `0xffffff80024733c0` | `0xffffff8000abd890` | `?` | — | — |
| 266 | `0xffffff80024733c8` | `0xffffff8000abd910` | `?` | — | — |
| 267 | `0xffffff80024733d0` | `0xffffff8000abe490` | `?` | — | — |
| 268 | `0xffffff80024733d8` | `0xffffff80023308b4` | `__ZN23IO80211VirtualInterface6enableEj` (0f) | — | — |
| 269 | `0xffffff80024733e0` | `0xffffff80022f9922` | `__ZN23IO80211SkywalkInterface7disableEj` (0f) | — | — |
| 270 | `0xffffff80024733e8` | `0xffffff8002afbcfe` | `?` | — | — |
| 271 | `0xffffff80024733f0` | `0xffffff8002afbd06` | `?` | — | — |
| 272 | `0xffffff80024733f8` | `0xffffff8002afbe74` | `?` | — | — |
| 273 | `0xffffff8002473400` | `0xffffff8002afbe86` | `?` | — | — |
| 274 | `0xffffff8002473408` | `0xffffff8002afbe9a` | `?` | — | — |
| 275 | `0xffffff8002473410` | `0xffffff8002afbeb0` | `?` | — | — |
| 276 | `0xffffff8002473418` | `0xffffff8002afbec6` | `?` | — | — |
| 277 | `0xffffff8002473420` | `0xffffff8002afbedc` | `?` | — | — |
| 278 | `0xffffff8002473428` | `0xffffff8002afbef2` | `?` | — | — |
| 279 | `0xffffff8002473430` | `0xffffff8002afbf08` | `?` | — | — |
| 280 | `0xffffff8002473438` | `0xffffff8002afbf1e` | `?` | — | — |
| 281 | `0xffffff8002473440` | `0xffffff8002afbf34` | `?` | — | — |
| 282 | `0xffffff8002473448` | `0xffffff8002afbf4a` | `?` | — | — |
| 283 | `0xffffff8002473450` | `0xffffff8002afbf60` | `?` | — | — |
| 284 | `0xffffff8002473458` | `0xffffff8002b0a430` | `?` | — | — |
| 285 | `0xffffff8002473460` | `0xffffff8002b0a7d4` | `?` | — | — |
| 286 | `0xffffff8002473468` | `0xffffff80022f635c` | `__ZN23IO80211SkywalkInterface26initBSDInterfaceParametersEP18ifnet_init_eparamsPP11sockaddr_dl` (0f) | — | — |
| 287 | `0xffffff8002473470` | `0xffffff8002331ada` | `__ZN23IO80211VirtualInterface19prepareBSDInterfaceEP7__ifnetj` (0f) | — | — |
| 288 | `0xffffff8002473478` | `0xffffff8002b0aba4` | `?` | — | — |
| 289 | `0xffffff8002473480` | `0xffffff8002b0abf6` | `?` | — | — |
| 290 | `0xffffff8002473488` | `0xffffff8002b0ac08` | `?` | — | — |
| 291 | `0xffffff8002473490` | `0xffffff8002b0ac2a` | `?` | — | — |
| 292 | `0xffffff8002473498` | `0xffffff8002331b36` | `__ZN23IO80211VirtualInterface17processBSDCommandEP7__ifnetjPv` (0f) | — | — |
| 293 | `0xffffff80024734a0` | `0xffffff8002b0b40e` | `?` | — | — |
| 294 | `0xffffff80024734a8` | `0xffffff8002b0b7b2` | `?` | — | — |
| 295 | `0xffffff80024734b0` | `0xffffff8002b0b7be` | `?` | — | — |
| 296 | `0xffffff80024734b8` | `0xffffff8002331ba6` | `__ZN23IO80211VirtualInterface18setInterfaceEnableEb` (0f) | — | — |
| 297 | `0xffffff80024734c0` | `0xffffff80022f85d2` | `__ZN23IO80211SkywalkInterface15setRunningStateEb` (0f) | — | — |
| 298 | `0xffffff80024734c8` | `0xffffff80022f8f5c` | `__ZN23IO80211SkywalkInterface17handleChosenMediaEj` (0f) | — | — |
| 299 | `0xffffff80024734d0` | `0xffffff80022f8e5e` | `__ZN23IO80211SkywalkInterface22getSupportedMediaArrayEPjS0_` (0f) | — | — |
| 300 | `0xffffff80024734d8` | `0xffffff8002b10bb6` | `?` | — | — |
| 301 | `0xffffff80024734e0` | `0xffffff8002b0b568` | `?` | — | — |
| 302 | `0xffffff80024734e8` | `0xffffff8002b0b836` | `?` | — | — |
| 303 | `0xffffff80024734f0` | `0xffffff8002b1219a` | `?` | — | — |
| 304 | `0xffffff80024734f8` | `0xffffff8002b0ac48` | `?` | — | — |
| 305 | `0xffffff8002473500` | `0xffffff8002b122de` | `?` | — | — |
| 306 | `0xffffff8002473508` | `0xffffff8002b0b9a0` | `?` | — | — |
| 307 | `0xffffff8002473510` | `0xffffff8002b122fc` | `?` | — | — |
| 308 | `0xffffff8002473518` | `0xffffff8002b0b9da` | `?` | — | — |
| 309 | `0xffffff8002473520` | `0xffffff8002b0b9ee` | `?` | — | — |
| 310 | `0xffffff8002473528` | `0xffffff8002b10d8e` | `?` | — | — |
| 311 | `0xffffff8002473530` | `0xffffff8002b10dac` | `?` | — | — |
| 312 | `0xffffff8002473538` | `0xffffff8002b10dbe` | `?` | — | — |
| 313 | `0xffffff8002473540` | `0xffffff8002331b4a` | `__ZN23IO80211VirtualInterface15getFeatureFlagsEv` (0f) | — | — |
| 314 | `0xffffff8002473548` | `0xffffff8002b0baba` | `?` | — | — |
| 315 | `0xffffff8002473550` | `0xffffff8002b0b8a0` | `?` | — | — |
| 316 | `0xffffff8002473558` | `0xffffff8002b0b8a8` | `?` | — | — |
| 317 | `0xffffff8002473560` | `0xffffff8002b0b9d2` | `?` | — | — |
| 318 | `0xffffff8002473568` | `0xffffff8002330e42` | `__ZN23IO80211VirtualInterface6bpfTapEjj` (0f) | — | — |
| 319 | `0xffffff8002473570` | `0xffffff8002b1231a` | `?` | — | — |
| 320 | `0xffffff8002473578` | `0xffffff8002b0ba70` | `?` | — | — |
| 321 | `0xffffff8002473580` | `0xffffff80022f9934` | `__ZN23IO80211SkywalkInterface17classNameOverrideEv` (0f) | — | — |
| 322 | `0xffffff8002473588` | `0xffffff8002b0bd6c` | `?` | — | — |
| 323 | `0xffffff8002473590` | `0xffffff8002b0bf86` | `?` | — | — |
| 324 | `0xffffff8002473598` | `0xffffff8002b0bfde` | `?` | — | — |
| 325 | `0xffffff80024735a0` | `0xffffff8002b0c090` | `?` | — | — |
| 326 | `0xffffff80024735a8` | `0xffffff8002b0c0a4` | `?` | — | — |
| 327 | `0xffffff80024735b0` | `0xffffff8002b0c0ba` | `?` | — | — |
| 328 | `0xffffff80024735b8` | `0xffffff8002b0c0d0` | `?` | — | — |
| 329 | `0xffffff80024735c0` | `0xffffff8002b0c0e6` | `?` | — | — |
| 330 | `0xffffff80024735c8` | `0xffffff8002b0c0fc` | `?` | — | — |
| 331 | `0xffffff80024735d0` | `0xffffff8002b0c112` | `?` | — | — |
| 332 | `0xffffff80024735d8` | `0xffffff8002b0c128` | `?` | — | — |
| 333 | `0xffffff80024735e0` | `0xffffff8002b0c13e` | `?` | — | — |
| 334 | `0xffffff80024735e8` | `0xffffff8002b0c154` | `?` | — | — |
| 335 | `0xffffff80024735f0` | `0xffffff80022f83d0` | `__ZN23IO80211SkywalkInterface18getHardwareAddressEP10ether_addr` (0f) | — | — |
| 336 | `0xffffff80024735f8` | `0xffffff80022f8468` | `__ZN23IO80211SkywalkInterface18setHardwareAddressEP10ether_addr` (0f) | — | — |
| 337 | `0xffffff8002473600` | `0xffffff8002b10ddc` | `?` | — | — |
| 338 | `0xffffff8002473608` | `0xffffff8002b115e2` | `?` | — | — |
| 339 | `0xffffff8002473610` | `0xffffff8002331c6c` | `__ZN23IO80211VirtualInterface21setMulticastAddressesEPK10ether_addrj` (0f) | — | — |
| 340 | `0xffffff8002473618` | `0xffffff8002b116c0` | `?` | — | — |
| 341 | `0xffffff8002473620` | `0xffffff80022f8fdc` | `__ZN23IO80211SkywalkInterface24setPromiscuousModeEnableEbj` (0f) | — | — |
| 342 | `0xffffff8002473628` | `0xffffff8002b12126` | `?` | — | — |
| 343 | `0xffffff8002473630` | `0xffffff8002b122d8` | `?` | — | — |
| 344 | `0xffffff8002473638` | `0xffffff8002b1233c` | `?` | — | — |
| 345 | `0xffffff8002473640` | `0xffffff8002b12350` | `?` | — | — |
| 346 | `0xffffff8002473648` | `0xffffff8002b12366` | `?` | — | — |
| 347 | `0xffffff8002473650` | `0xffffff8002b1237c` | `?` | — | — |
| 348 | `0xffffff8002473658` | `0xffffff8002b12392` | `?` | — | — |
| 349 | `0xffffff8002473660` | `0xffffff8002b123a8` | `?` | — | — |
| 350 | `0xffffff8002473668` | `0xffffff8002b123be` | `?` | — | — |
| 351 | `0xffffff8002473670` | `0xffffff8002b123d4` | `?` | — | — |
| 352 | `0xffffff8002473678` | `0xffffff8002b123ea` | `?` | — | — |
| 353 | `0xffffff8002473680` | `0xffffff8002b12400` | `?` | — | — |
| 354 | `0xffffff8002473688` | `0xffffff8002b12416` | `?` | — | — |
| 355 | `0xffffff8002473690` | `0xffffff80023308c2` | `__ZN23IO80211VirtualInterface17createPeerManagerEv` (0f) | — | — |
| 356 | `0xffffff8002473698` | `0xffffff80022f7bf6` | `__ZN23IO80211SkywalkInterface10createPeerEPKhP18IO80211PeerManager` (0f) | — | — |
| 357 | `0xffffff80024736a0` | `0xffffff8002333192` | `__ZN23IO80211VirtualInterface11postMessageEjPvmb` (0f) | — | — |
| 358 | `0xffffff80024736a8` | `0xffffff80022f8dc8` | `__ZN23IO80211SkywalkInterface20reportDataPathEventsEjPvmb` (0f) | — | — |
| 359 | `0xffffff80024736b0` | `0xffffff80022f9032` | `__ZN23IO80211SkywalkInterface19recordOutputPacketsEP24TxSubmissionDequeueStatsS1_` (0f) | — | — |
| 360 | `0xffffff80024736b8` | `0xffffff80022f9014` | `__ZN23IO80211SkywalkInterface18recordOutputPacketE17apple80211_wme_acii` (0f) | — | — |
| 361 | `0xffffff80024736c0` | `0xffffff8002333fae` | `__ZN23IO80211VirtualInterface11logTxPacketEP20IO80211NetworkPacketP20PacketSkywalkScratch17apple80211_wme_acb` (0f) | — | — |
| 362 | `0xffffff80024736c8` | `0xffffff800233403a` | `__ZN23IO80211VirtualInterface21logTxCompletionPacketEP20IO80211NetworkPacketP20PacketSkywalkScratchPh17apple80211_wme_acijbb` (0f) | — | — |
| 363 | `0xffffff80024736d0` | `0xffffff80022f9076` | `__ZN23IO80211SkywalkInterface23recordCompletionPacketsEP24TxCompletionEnqueueStatsS1_` (0f) | — | — |
| 364 | `0xffffff80024736d8` | `0xffffff8002333cac` | `__ZN23IO80211VirtualInterface11inputPacketEP20IO80211NetworkPacketP15packet_info_tagP12ether_headerPbb` (0f) | — | — |
| 365 | `0xffffff80024736e0` | `0xffffff80022f907c` | `__ZN23IO80211SkywalkInterface24forwardInfraRelayPacketsEP20IO80211NetworkPacketP12ether_header` (0f) | — | — |
| 366 | `0xffffff80024736e8` | `0xffffff800233218a` | `__ZN23IO80211VirtualInterface21logSkywalkTxReqPacketEP20IO80211NetworkPacketP20PacketSkywalkScratchPh17apple80211_wme_acb` (0f) | — | — |
| 367 | `0xffffff80024736f0` | `0xffffff8002332378` | `__ZN23IO80211VirtualInterface14pendingPacketsEh` (0f) | — | — |
| 368 | `0xffffff80024736f8` | `0xffffff8002332446` | `__ZN23IO80211VirtualInterface11packetSpaceEh` (0f) | — | — |
| 369 | `0xffffff8002473700` | `0xffffff80022f953a` | `__ZN23IO80211SkywalkInterface20isChipInterfaceReadyEv` (0f) | — | — |
| 370 | `0xffffff8002473708` | `0xffffff80022f85ca` | `__ZN23IO80211SkywalkInterface17isDebounceOnGoingEv` (0f) | — | — |
| 371 | `0xffffff8002473710` | `0xffffff80023333f8` | `__ZN23IO80211VirtualInterface9linkStateEv` (0f) | — | — |
| 372 | `0xffffff8002473718` | `0xffffff8002331eb0` | `__ZN23IO80211VirtualInterface16setScanningStateEjbP20apple80211_scan_datai` (0f) | — | — |
| 373 | `0xffffff8002473720` | `0xffffff80022f9566` | `__ZN23IO80211SkywalkInterface16setDataPathStateEb` (0f) | — | — |
| 374 | `0xffffff8002473728` | `0xffffff80022f9870` | `__ZN23IO80211SkywalkInterface14getScanManagerEv` (0f) | — | — |
| 375 | `0xffffff8002473730` | `0xffffff80022f957a` | `__ZN23IO80211SkywalkInterface13getControllerEv` (0f) | — | — |
| 376 | `0xffffff8002473738` | `0xffffff8002331f70` | `__ZN23IO80211VirtualInterface20updateLinkParametersEP33apple80211_interface_availability` (0f) | — | — |
| 377 | `0xffffff8002473740` | `0xffffff8002332010` | `__ZN23IO80211VirtualInterface26updateInterfaceCoexRiskPctEy` (0f) | — | — |
| 378 | `0xffffff8002473748` | `0xffffff80022f95b8` | `__ZN23IO80211SkywalkInterface6setLQMEy` (0f) | — | — |
| 379 | `0xffffff8002473750` | `0xffffff80022f95ce` | `__ZN23IO80211SkywalkInterface16updateLinkStatusEv` (0f) | — | — |
| 380 | `0xffffff8002473758` | `0xffffff80022f95e4` | `__ZN23IO80211SkywalkInterface21updateLinkStatusGatedEv` (0f) | — | — |
| 381 | `0xffffff8002473760` | `0xffffff80022f95fa` | `__ZN23IO80211SkywalkInterface23setInterfaceExtendedCCAE18apple80211_channelP21apple80211_cca_report` (0f) | — | — |
| 382 | `0xffffff8002473768` | `0xffffff80022f9610` | `__ZN23IO80211SkywalkInterface15setInterfaceCCAE18apple80211_channeli` (0f) | — | — |
| 383 | `0xffffff8002473770` | `0xffffff8002333862` | `__ZN23IO80211VirtualInterface14setInterfaceNFE18apple80211_channelx` (0f) | — | — |
| 384 | `0xffffff8002473778` | `0xffffff80022f9848` | `__ZN23IO80211SkywalkInterface23setInterfaceOFDMDesenseE18apple80211_channelx` (0f) | — | — |
| 385 | `0xffffff8002473780` | `0xffffff80022f9626` | `__ZN23IO80211SkywalkInterface17removePacketQueueEP20IO80211FlowQueueHash` (0f) | — | — |
| 386 | `0xffffff8002473788` | `0xffffff8002333376` | `__ZN23IO80211VirtualInterface13setDebugFlagsEyj` (0f) | — | — |
| 387 | `0xffffff8002473790` | `0xffffff80023333e2` | `__ZN23IO80211VirtualInterface10debugFlagsEv` (0f) | — | — |
| 388 | `0xffffff8002473798` | `0xffffff8002333884` | `__ZN23IO80211VirtualInterface24setInterfaceChipCountersEP22apple80211_stat_reportP27apple80211_chip_counters_txP33apple80211_chip_error_counters_txP27apple80211_chip_counters_rx` (0f) | — | — |
| 389 | `0xffffff80024737a0` | `0xffffff80023338a2` | `__ZN23IO80211VirtualInterface20setInterfaceMIBdot11EP22apple80211_stat_reportP50apple80211_ManagementInformationBasedot11_counters` (0f) | — | — |
| 390 | `0xffffff80024737a8` | `0xffffff80023338c0` | `__ZN23IO80211VirtualInterface13setFrameStatsEP22apple80211_stat_reportP25apple80211_frame_counters` (0f) | — | — |
| 391 | `0xffffff80024737b0` | `0xffffff80023338de` | `__ZN23IO80211VirtualInterface26setInfraSpecificFrameStatsEP22apple80211_stat_reportP31apple80211_infra_specific_stats` (0f) | — | — |
| 392 | `0xffffff80024737b8` | `0xffffff80022f96a6` | `__ZN23IO80211SkywalkInterface19setRxDataStallStatsEP22apple80211_stat_reportP31apple80211_rx_data_stall_report` (0f) | — | — |
| 393 | `0xffffff80024737c0` | `0xffffff800233209e` | `__ZN23IO80211VirtualInterface16getWmeTxCountersEPy` (0f) | — | — |
| 394 | `0xffffff80024737c8` | `0xffffff80022f96e6` | `__ZN23IO80211SkywalkInterface21setPeerManagerLogFlagEjjj` (0f) | — | — |
| 395 | `0xffffff80024737d0` | `0xffffff80023333f2` | `__ZN23IO80211VirtualInterface13setWoWEnabledEb` (0f) | — | — |
| 396 | `0xffffff80024737d8` | `0xffffff8002334c34` | `__ZN23IO80211VirtualInterface10wowEnabledEv` (0f) | — | — |
| 397 | `0xffffff80024737e0` | `0xffffff8002334344` | `__ZN23IO80211VirtualInterface13printDataPathEP12userPrintCtx` (0f) | — | — |
| 398 | `0xffffff80024737e8` | `0xffffff80022f82b6` | `__ZN23IO80211SkywalkInterface17getDataQueueDepthEv` (0f) | — | — |
| 399 | `0xffffff80024737f0` | `0xffffff800233270a` | `__ZN23IO80211VirtualInterface21findOrCreateFlowQueueE20IO80211FlowQueueHash` (0f) | — | — |
| 400 | `0xffffff80024737f8` | `0xffffff8002332c38` | `__ZN23IO80211VirtualInterface30findOrCreateFlowQueueWithCacheE20IO80211FlowQueueHashPb` (0f) | — | — |
| 401 | `0xffffff8002473800` | `0xffffff80023324e4` | `__ZN23IO80211VirtualInterface21findExistingFlowQueueE20IO80211FlowQueueHash` (0f) | — | — |
| 402 | `0xffffff8002473808` | `0xffffff80023322e6` | `__ZN23IO80211VirtualInterface17removePacketQueueEPK20IO80211FlowQueueHash` (0f) | — | — |
| 403 | `0xffffff8002473810` | `0xffffff8002332290` | `__ZN23IO80211VirtualInterface17flushPacketQueuesEv` (0f) | — | — |
| 404 | `0xffffff8002473818` | `0xffffff80023324fc` | `__ZN23IO80211VirtualInterface9cachePeerEP10ether_addrPj` (0f) | — | — |
| 405 | `0xffffff8002473820` | `0xffffff80023343d6` | `__ZN23IO80211VirtualInterface9shouldLogEy` (0f) | — | — |
| 406 | `0xffffff8002473828` | `0xffffff800233436a` | `__ZN23IO80211VirtualInterface9vlogDebugEyPKcP13__va_list_tag` (0f) | — | — |
| 407 | `0xffffff8002473830` | `0xffffff80023343d0` | `__ZN23IO80211VirtualInterface12vlogDebugBPFEyPKcP13__va_list_tag` (0f) | — | — |
| 408 | `0xffffff8002473838` | `0xffffff80022f9708` | `__ZN23IO80211SkywalkInterface24createLinkQualityMonitorEP11IO80211Peerb` (0f) | — | — |
| 409 | `0xffffff8002473840` | `0xffffff80022f9714` | `__ZN23IO80211SkywalkInterface25releaseLinkQualityMonitorEP11IO80211Peer` (0f) | — | — |
| 410 | `0xffffff8002473848` | `0xffffff8002334c1a` | `__ZN23IO80211VirtualInterface20getP2PSkywalkPeerMgrEv` (0f) | — | — |
| 411 | `0xffffff8002473850` | `0xffffff80022f9bce` | `__ZN23IO80211SkywalkInterface19isCommandProhibitedEi` (0f) | — | — |
| 412 | `0xffffff8002473858` | `0xffffff8002334ec6` | `__ZN23IO80211VirtualInterface8findPeerER10ether_addr` (0f) | — | — |
| 413 | `0xffffff8002473860` | `0xffffff80022f985e` | `__ZN23IO80211SkywalkInterface23setNotificationPropertyEPK8OSSymbolPK8OSObject` (0f) | — | — |
| 414 | `0xffffff8002473868` | `0xffffff80022f9722` | `__ZN23IO80211SkywalkInterface21getWorkerMatchingDictEP8OSString` (0f) | — | — |
| 415 | `0xffffff8002473870` | `0xffffff80022f56e2` | `__ZN23IO80211SkywalkInterface4initEP9IOServiceP10ether_addr` (0f) | — | — |
| 416 | `0xffffff8002473878` | `0xffffff80022f972a` | `__ZN23IO80211SkywalkInterface18isInterfaceEnabledEv` (0f) | — | — |
| 417 | `0xffffff8002473880` | `0xffffff80022f841c` | `__ZN23IO80211SkywalkInterface14getSelfMacAddrEv` (0f) | — | — |
| 418 | `0xffffff8002473888` | `0xffffff80022f973c` | `__ZN23IO80211SkywalkInterface13getPacketPoolEP8OSString` (0f) | — | — |
| 419 | `0xffffff8002473890` | `0xffffff80022f9744` | `__ZNK23IO80211SkywalkInterface9getLoggerEv` (0f) | — | — |
| 420 | `0xffffff8002473898` | `0xffffff80022f854a` | `__ZN23IO80211SkywalkInterface17handleSIOCSIFADDREv` (0f) | — | — |
| 421 | `0xffffff80024738a0` | `0xffffff80022f9756` | `__ZN23IO80211SkywalkInterface12debugHandlerEP24apple80211_debug_command` (0f) | — | — |
| 422 | `0xffffff80024738a8` | `0xffffff80022f9878` | `__ZN23IO80211SkywalkInterface9statsDumpEv` (0f) | — | — |
| 423 | `0xffffff80024738b0` | `0xffffff80022f987e` | `__ZN23IO80211SkywalkInterface19powerOnNotificationEv` (0f) | — | — |
| 424 | `0xffffff80024738b8` | `0xffffff80022f9884` | `__ZN23IO80211SkywalkInterface20powerOffNotificationEv` (0f) | — | — |
| 425 | `0xffffff80024738c0` | `0xffffff80022f988a` | `__ZN23IO80211SkywalkInterface15getTxQueueDepthEv` (0f) | — | — |
| 426 | `0xffffff80024738c8` | `0xffffff80022f9892` | `__ZN23IO80211SkywalkInterface18getRxQueueCapacityEv` (0f) | — | — |
| 427 | `0xffffff80024738d0` | `0xffffff80022f989a` | `__ZN23IO80211SkywalkInterface15updateRxCounterEy` (0f) | — | — |
| 428 | `0xffffff80024738d8` | `0xffffff80022f98a0` | `__ZN23IO80211SkywalkInterface17getMultiCastQueueEv` (0f) | — | — |
| 429 | `0xffffff80024738e0` | `0xffffff80022f98a8` | `__ZN23IO80211SkywalkInterface13getAssocStateEv` (0f) | — | — |
| 430 | `0xffffff80024738e8` | `0xffffff80022f98be` | `__ZN23IO80211SkywalkInterface16notifyQueueStateE17apple80211_wme_act` (0f) | — | — |
| 431 | `0xffffff80024738f0` | `0xffffff80022f98c4` | `__ZN23IO80211SkywalkInterface13getTxHeadroomEv` (0f) | — | — |
| 432 | `0xffffff80024738f8` | `0xffffff80022f98cc` | `__ZN23IO80211SkywalkInterface14getRxCompQueueEv` (0f) | — | — |
| 433 | `0xffffff8002473900` | `0xffffff80022f98d4` | `__ZN23IO80211SkywalkInterface14getTxCompQueueEv` (0f) | — | — |
| 434 | `0xffffff8002473908` | `0xffffff80022f98dc` | `__ZN23IO80211SkywalkInterface13getTxSubQueueE17apple80211_wme_ac` (0f) | — | — |
| 435 | `0xffffff8002473910` | `0xffffff80022f98e4` | `__ZN23IO80211SkywalkInterface15getTxPacketPoolEv` (0f) | — | — |
| 436 | `0xffffff8002473918` | `0xffffff80022f98ec` | `__ZN23IO80211SkywalkInterface15getRxPacketPoolEv` (0f) | — | — |
| 437 | `0xffffff8002473920` | `0xffffff80022f98f4` | `__ZN23IO80211SkywalkInterface14enableDatapathEv` (0f) | — | — |
| 438 | `0xffffff8002473928` | `0xffffff80022f98fc` | `__ZN23IO80211SkywalkInterface15disableDatapathEv` (0f) | — | — |
| 439 | `0xffffff8002473930` | `0xffffff80022f9904` | `__ZN23IO80211SkywalkInterface14getNumTxQueuesEv` (0f) | — | — |
| 440 | `0xffffff8002473938` | `0xffffff80022f9864` | `__ZN23IO80211SkywalkInterface13getLQMSummaryEP22apple80211_lqm_summary` (0f) | — | — |
| 441 | `0xffffff8002473940` | `0xffffff80023347d2` | `__ZN23IO80211VirtualInterface16getEventPipeSizeEv` (0f) | — | — |
| 442 | `0xffffff8002473948` | `0xffffff80023347fc` | `__ZN23IO80211VirtualInterface15createEventPipeEP20IO80211APIUserClient` (0f) | — | — |
| 443 | `0xffffff8002473950` | `0xffffff80022f6cf0` | `__ZN23IO80211SkywalkInterface16destroyEventPipeEP20IO80211APIUserClient` (0f) | — | — |
| 444 | `0xffffff8002473958` | `0xffffff80022f6e1a` | `__ZN23IO80211SkywalkInterface17setUserBufferInfoEP18IOMemoryDescriptory` (0f) | — | — |
| 445 | `0xffffff8002473960` | `0xffffff80022f66a2` | `__ZN23IO80211SkywalkInterface15postMessageIOUCEPKcjPvm` (0f) | — | — |
| 446 | `0xffffff8002473968` | `0xffffff80022f6678` | `__ZN23IO80211SkywalkInterface16isIOUCPipeOpenedEv` (0f) | — | — |
| 447 | `0xffffff8002473970` | `0xffffff80022f6560` | `__ZN23IO80211SkywalkInterface9getRingMDEP20IO80211APIUserClienty` (0f) | — | — |
| 448 | `0xffffff8002473978` | `0xffffff80022f9c56` | `__ZN23IO80211SkywalkInterface10attachPeerEP10ether_addr` (0f) | — | — |
| 449 | `0xffffff8002473980` | `0xffffff80022f9c5c` | `__ZN23IO80211SkywalkInterface10detachPeerEP10ether_addr` (0f) | — | — |
| 450 | `0xffffff8002473988` | `0xffffff80022f9c62` | `__ZN23IO80211SkywalkInterface21setDebugTrafficReportEb` (0f) | — | — |
| 451 | `0xffffff8002473990` | `0xffffff80022f9c7a` | `__ZN23IO80211SkywalkInterface25getDataPathInterfaceStatsEP36apple80211_data_path_interface_stats` (0f) | — | — |
| 452 | `0xffffff8002473998` | `0xffffff80022f9c86` | `__ZN23IO80211SkywalkInterface20getDataPathPeerStatsEP31apple80211_data_path_peer_stats` (0f) | — | — |
| 453 | `0xffffff80024739a0` | `0xffffff80022f9c92` | `__ZN23IO80211SkywalkInterface22getLastQueuePacketTimeEP10ether_addr` (0f) | — | — |
| 454 | `0xffffff80024739a8` | `0xffffff80022f9c9a` | `__ZN23IO80211SkywalkInterface32getLastRxUnicastLinkActivityTimeEP10ether_addr` (0f) | — | — |
| 455 | `0xffffff80024739b0` | `0xffffff80022f9ca2` | `__ZN23IO80211SkywalkInterface24updateInterfaceDataStatsEP36apple80211_data_path_interface_stats` (0f) | — | — |
| 456 | `0xffffff80024739b8` | `0xffffff80022f9dae` | `__ZN23IO80211SkywalkInterface19updatePeerDataStatsEP31apple80211_data_path_peer_stats` (0f) | — | — |
| 457 | `0xffffff80024739c0` | `0xffffff80022fa1f6` | `__ZN23IO80211SkywalkInterface12logTxLatencyEPhjy` (0f) | — | — |
| 458 | `0xffffff80024739c8` | `0xffffff80022fa204` | `__ZN23IO80211SkywalkInterface12logRxLatencyEjy` (0f) | — | — |
| 459 | `0xffffff80024739d0` | `0xffffff80022fa20a` | `__ZN23IO80211SkywalkInterface20getNClearTxRxLatencyEP25apple80211_latency_all_acS1_` (0f) | — | — |
| 460 | `0xffffff80024739d8` | `0xffffff80022fa216` | `__ZN23IO80211SkywalkInterface18getLastTxTimeStampERy` (0f) | — | — |
| 461 | `0xffffff80024739e0` | `0xffffff80022fa222` | `__ZN23IO80211SkywalkInterface18getLastRxTimeStampERy` (0f) | — | — |
| 462 | `0xffffff80024739e8` | `0xffffff80022fa22e` | `__ZN23IO80211SkywalkInterface12syncDPSStatsEP26apple82011_postMessage_dps` (0f) | — | — |
| 463 | `0xffffff80024739f0` | `0xffffff80023305f8` | `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc` (0f) | — | — |
| 464 | `0xffffff80024739f8` | `0xffffff8002333408` | `__ZN23IO80211VirtualInterface12setLinkStateE16IO80211LinkStatej` (0f) | — | — |
| 465 | `0xffffff8002473a00` | `0xffffff8002333120` | `__ZN23IO80211VirtualInterface13forwardPacketEP20IO80211NetworkPacket` (0f) | — | — |
| 466 | `0xffffff8002473a08` | `0xffffff8002331c88` | `__ZN23IO80211VirtualInterface11handleIoctlEP7__ifnetmPv` (0f) | — | — |
| 467 | `0xffffff8002473a10` | `0xffffff8002334bb2` | `__ZN23IO80211VirtualInterface14addPeerToCacheEPh` (0f) | — | — |
| 468 | `0xffffff8002473a18` | `0xffffff8002334bce` | `__ZN23IO80211VirtualInterface19deletePeerFromCacheEPh` (0f) | — | — |
| 469 | `0xffffff8002473a20` | `0xffffff800233411c` | `__ZN23IO80211VirtualInterface14handleDebugCmdEP24apple80211_debug_command` (0f) | — | — |
| 470 | `0xffffff8002473a28` | `0xffffff8002334eae` | `__ZN23IO80211VirtualInterface20getPeerSlotDataStatsEP10ether_addrbP17peerSlotDataStats` (0f) | — | — |
| 471 | `0xffffff8002473a30` | `0xffffff8002334eb6` | `__ZN23IO80211VirtualInterface16getPeerDataStatsEP10ether_addrbP16packetCounters_t` (0f) | — | — |
| 472 | `0xffffff8002473a38` | `0xffffff8002334ebe` | `__ZN23IO80211VirtualInterface18clearPeerDataStatsEP10ether_addrb` (0f) | — | — |
| 473 | `0xffffff8002473a40` | `0xffffff8002334ea8` | `__ZN23IO80211VirtualInterface34setAwdlCurrentChannelSequenceIndexEj` (0f) | — | — |
| 474 | `0xffffff8002473a48` | `0xffffff8002333098` | `__ZN23IO80211VirtualInterface22isOutputFlowControlledEv` (0f) | — | — |
| 475 | `0xffffff8002473a50` | `0xffffff80023330c8` | `__ZN23IO80211VirtualInterface23setOutputFlowControlledEv` (0f) | — | — |
| 476 | `0xffffff8002473a58` | `0xffffff80023330f4` | `__ZN23IO80211VirtualInterface25clearOutputFlowControlledEv` (0f) | — | — |
| 477 | `0xffffff8002473a60` | `0xffffff800233235e` | `__ZN23IO80211VirtualInterface15flushFlowQueuesEP10ether_addr` (0f) | — | — |
| 478 | `0xffffff8002473a68` | `0xffffff8002330b6e` | `__ZN23IO80211VirtualInterface18configureAQMOutputEv` (0f) | — | — |
| 479 | `0xffffff8002473a70` | `0xffffff8002330386` | `__ZN23IO80211VirtualInterface11attachToBpfEv` (0f) | — | — |
| 480 | `0xffffff8002473a78` | `0xffffff8002331314` | `__ZN23IO80211VirtualInterface14configureIfnetEv` (0f) | — | — |
| 481 | `0xffffff8002473a80` | `0xffffff8002334bea` | `__ZN23IO80211VirtualInterface5resetEv` (0f) | — | — |
| 482 | `0xffffff8002473a88` | `0xffffff8002334c82` | `__ZN23IO80211VirtualInterface25dupAndTransmitMcastPacketEP20IO80211NetworkPacketPhS2_` (0f) | — | — |
| 483 | `0xffffff8002473a90` | `0xffffff8002334d5a` | `__ZN23IO80211VirtualInterface19freeMulticastPacketEP20IO80211NetworkPacket` (0f) | — | — |
| 484 | `0xffffff8002473a98` | `0xffffff8002334d62` | `__ZN23IO80211VirtualInterface22triggerAWDLMulticastTxEjh` (0f) | — | — |
| 485 | `0xffffff8002473aa0` | `0xffffff8002334d6a` | `__ZN23IO80211VirtualInterface14p2pMulticastTxEPP20IO80211NetworkPacketj` (0f) | — | — |
| 486 | `0xffffff8002473aa8` | `0xffffff8002334d98` | `__ZN23IO80211VirtualInterface14p2pMulticastTxEjb` (0f) | — | — |
| 487 | `0xffffff8002473ab0` | `0xffffff8002334e44` | `__ZN23IO80211VirtualInterface22signalMulticastPacketsEjj` (0f) | — | — |
| 488 | `0xffffff8002473ab8` | `0xffffff8002334e68` | `__ZN23IO80211VirtualInterface25sendPendingPacketsToStackEPP20IO80211NetworkPacketj` (0f) | — | — |
| 489 | `0xffffff8002473ac0` | `0xffffff8002334e88` | `__ZN23IO80211VirtualInterface18freePendingPacketsEPP20IO80211NetworkPacketj` (0f) | — | — |
| 490 | `0xffffff8002473ac8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 491 | `0xffffff8002473ad0` | `0xffffff80023300dc` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface0Ev` (0f) | — | — |
| 492 | `0xffffff8002473ad8` | `0xffffff80023300f0` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface1Ev` (0f) | — | — |
| 493 | `0xffffff8002473ae0` | `0xffffff8002330106` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface2Ev` (0f) | — | — |
| 494 | `0xffffff8002473ae8` | `0xffffff800233011c` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface3Ev` (0f) | — | — |
| 495 | `0xffffff8002473af0` | `0xffffff8002330132` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface4Ev` (0f) | — | — |
| 496 | `0xffffff8002473af8` | `0xffffff8002330148` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface5Ev` (0f) | — | — |
| 497 | `0xffffff8002473b00` | `0xffffff800233015e` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface6Ev` (0f) | — | — |
| 498 | `0xffffff8002473b08` | `0xffffff8002330174` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface7Ev` (0f) | — | — |
| 499 | `0xffffff8002473b10` | `0xffffff800233018a` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface8Ev` (0f) | — | — |
| 500 | `0xffffff8002473b18` | `0xffffff80023301a0` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface9Ev` (0f) | — | — |
| 501 | `0xffffff8002473b20` | `0xffffff80023301b6` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface10Ev` (0f) | — | — |
| 502 | `0xffffff8002473b28` | `0xffffff80023301cc` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface11Ev` (0f) | — | — |
| 503 | `0xffffff8002473b30` | `0xffffff80023301e2` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface12Ev` (0f) | — | — |
| 504 | `0xffffff8002473b38` | `0xffffff80023301f8` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface13Ev` (0f) | — | — |
| 505 | `0xffffff8002473b40` | `0xffffff800233020e` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface14Ev` (0f) | — | — |
| 506 | `0xffffff8002473b48` | `0xffffff8002330224` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface15Ev` (0f) | — | — |

## `IO80211AWDLProtocol`

Base de vtable: `0xffffff8002456440` (`__ZTV19IO80211AWDLProtocol`, `awdl_vtable_probe.log:47`). Slots listados: 2–569; slot 490 sigue siendo puro y los slots 507–569 son puros según `class_meta_probe.log:71–133`.

| Índice | Dirección de entrada | Target VA | Símbolo en dumps | Tipo | Slot equivalente en Proximity |
|---:|---:|---:|---|---|---|
| 2 | `0xffffff8002456450` | `0xffffff800239ef24` | `__ZN19IO80211AWDLProtocolD1Ev` (0f) | — | — |
| 3 | `0xffffff8002456458` | `0xffffff800239ef2a` | `__ZN19IO80211AWDLProtocolD0Ev` (0f) | — | — |
| 4 | `0xffffff8002456460` | `0xffffff8000a1c520` | `?` | — | — |
| 5 | `0xffffff8002456468` | `0xffffff8000a1c540` | `?` | — | — |
| 6 | `0xffffff8002456470` | `0xffffff8000a1c550` | `?` | — | — |
| 7 | `0xffffff8002456478` | `0xffffff8000a1c560` | `?` | — | — |
| 8 | `0xffffff8002456480` | `0xffffff8000a1c570` | `?` | — | — |
| 9 | `0xffffff8002456488` | `0xffffff8002198c2e` | `__ZNK19IO80211AWDLProtocol12getMetaClassEv` (0f) | — | — |
| 10 | `0xffffff8002456490` | `0xffffff8000a1a7e0` | `?` | — | — |
| 11 | `0xffffff8002456498` | `0xffffff8000a1c6f0` | `?` | — | — |
| 12 | `0xffffff80024564a0` | `0xffffff8000a1c760` | `?` | — | — |
| 13 | `0xffffff80024564a8` | `0xffffff8000a1c780` | `?` | — | — |
| 14 | `0xffffff80024564b0` | `0xffffff8000a682b0` | `?` | — | — |
| 15 | `0xffffff80024564b8` | `0xffffff8000bb4d60` | `?` | — | — |
| 16 | `0xffffff80024564c0` | `0xffffff8000bb4d90` | `?` | — | — |
| 17 | `0xffffff80024564c8` | `0xffffff8000bb4dc0` | `?` | — | — |
| 18 | `0xffffff80024564d0` | `0xffffff8000bb4df0` | `?` | — | — |
| 19 | `0xffffff80024564d8` | `0xffffff80022f568e` | `__ZN23IO80211SkywalkInterface4initEv` (0f) | — | — |
| 20 | `0xffffff80024564e0` | `0xffffff8002330b8c` | `__ZN23IO80211VirtualInterface4freeEv` (0f) | — | — |
| 21 | `0xffffff80024564e8` | `0xffffff8000bb5060` | `?` | — | — |
| 22 | `0xffffff80024564f0` | `0xffffff8000bb5080` | `?` | — | — |
| 23 | `0xffffff80024564f8` | `0xffffff8000bb50a0` | `?` | — | — |
| 24 | `0xffffff8002456500` | `0xffffff8000bb50c0` | `?` | — | — |
| 25 | `0xffffff8002456508` | `0xffffff8000bb50e0` | `?` | — | — |
| 26 | `0xffffff8002456510` | `0xffffff8000bb5100` | `?` | — | — |
| 27 | `0xffffff8002456518` | `0xffffff8000bb5120` | `?` | — | — |
| 28 | `0xffffff8002456520` | `0xffffff8000bb5140` | `?` | — | — |
| 29 | `0xffffff8002456528` | `0xffffff8000bb5160` | `?` | — | — |
| 30 | `0xffffff8002456530` | `0xffffff8000bb5180` | `?` | — | — |
| 31 | `0xffffff8002456538` | `0xffffff8000bb51a0` | `?` | — | — |
| 32 | `0xffffff8002456540` | `0xffffff8000bb51c0` | `?` | — | — |
| 33 | `0xffffff8002456548` | `0xffffff8000bb51e0` | `?` | — | — |
| 34 | `0xffffff8002456550` | `0xffffff8000bb5200` | `?` | — | — |
| 35 | `0xffffff8002456558` | `0xffffff8000bb5220` | `?` | — | — |
| 36 | `0xffffff8002456560` | `0xffffff8000bb5240` | `?` | — | — |
| 37 | `0xffffff8002456568` | `0xffffff8000a8c310` | `?` | — | — |
| 38 | `0xffffff8002456570` | `0xffffff8000a8c470` | `?` | — | — |
| 39 | `0xffffff8002456578` | `0xffffff8000a8c5d0` | `?` | — | — |
| 40 | `0xffffff8002456580` | `0xffffff8000a8c730` | `?` | — | — |
| 41 | `0xffffff8002456588` | `0xffffff8000a8c7a0` | `?` | — | — |
| 42 | `0xffffff8002456590` | `0xffffff8000a8c810` | `?` | — | — |
| 43 | `0xffffff8002456598` | `0xffffff8000bb6770` | `?` | — | — |
| 44 | `0xffffff80024565a0` | `0xffffff8000bb6790` | `?` | — | — |
| 45 | `0xffffff80024565a8` | `0xffffff8000bb67b0` | `?` | — | — |
| 46 | `0xffffff80024565b0` | `0xffffff8000bb67d0` | `?` | — | — |
| 47 | `0xffffff80024565b8` | `0xffffff8000bb67f0` | `?` | — | — |
| 48 | `0xffffff80024565c0` | `0xffffff8000bb6810` | `?` | — | — |
| 49 | `0xffffff80024565c8` | `0xffffff8000bb6830` | `?` | — | — |
| 50 | `0xffffff80024565d0` | `0xffffff8000bb6850` | `?` | — | — |
| 51 | `0xffffff80024565d8` | `0xffffff8000bb6870` | `?` | — | — |
| 52 | `0xffffff80024565e0` | `0xffffff8000bb6890` | `?` | — | — |
| 53 | `0xffffff80024565e8` | `0xffffff8000bb68b0` | `?` | — | — |
| 54 | `0xffffff80024565f0` | `0xffffff8000bb68d0` | `?` | — | — |
| 55 | `0xffffff80024565f8` | `0xffffff8000bb68f0` | `?` | — | — |
| 56 | `0xffffff8002456600` | `0xffffff8000bb6910` | `?` | — | — |
| 57 | `0xffffff8002456608` | `0xffffff8000bb6930` | `?` | — | — |
| 58 | `0xffffff8002456610` | `0xffffff8000bb6950` | `?` | — | — |
| 59 | `0xffffff8002456618` | `0xffffff8000bb6970` | `?` | — | — |
| 60 | `0xffffff8002456620` | `0xffffff8000bb6990` | `?` | — | — |
| 61 | `0xffffff8002456628` | `0xffffff8000bb69b0` | `?` | — | — |
| 62 | `0xffffff8002456630` | `0xffffff8000bb69d0` | `?` | — | — |
| 63 | `0xffffff8002456638` | `0xffffff8000bb69f0` | `?` | — | — |
| 64 | `0xffffff8002456640` | `0xffffff8000bb6a10` | `?` | — | — |
| 65 | `0xffffff8002456648` | `0xffffff8000bb6a30` | `?` | — | — |
| 66 | `0xffffff8002456650` | `0xffffff8000bb6a50` | `?` | — | — |
| 67 | `0xffffff8002456658` | `0xffffff8000bb6a70` | `?` | — | — |
| 68 | `0xffffff8002456660` | `0xffffff8000bb6a90` | `?` | — | — |
| 69 | `0xffffff8002456668` | `0xffffff8000bb6ab0` | `?` | — | — |
| 70 | `0xffffff8002456670` | `0xffffff8000bb6ad0` | `?` | — | — |
| 71 | `0xffffff8002456678` | `0xffffff8000bb6af0` | `?` | — | — |
| 72 | `0xffffff8002456680` | `0xffffff8000bb6b10` | `?` | — | — |
| 73 | `0xffffff8002456688` | `0xffffff8000bb6b30` | `?` | — | — |
| 74 | `0xffffff8002456690` | `0xffffff8000bb6b50` | `?` | — | — |
| 75 | `0xffffff8002456698` | `0xffffff8002b10820` | `?` | — | — |
| 76 | `0xffffff80024566a0` | `0xffffff8000a8cab0` | `?` | — | — |
| 77 | `0xffffff80024566a8` | `0xffffff8000a8cb50` | `?` | — | — |
| 78 | `0xffffff80024566b0` | `0xffffff8000a8cc70` | `?` | — | — |
| 79 | `0xffffff80024566b8` | `0xffffff8000a8ccc0` | `?` | — | — |
| 80 | `0xffffff80024566c0` | `0xffffff8000a8cd10` | `?` | — | — |
| 81 | `0xffffff80024566c8` | `0xffffff8000a8cd80` | `?` | — | — |
| 82 | `0xffffff80024566d0` | `0xffffff8000a8cdf0` | `?` | — | — |
| 83 | `0xffffff80024566d8` | `0xffffff8000a8ce90` | `?` | — | — |
| 84 | `0xffffff80024566e0` | `0xffffff8000a8cf30` | `?` | — | — |
| 85 | `0xffffff80024566e8` | `0xffffff8000a8cfb0` | `?` | — | — |
| 86 | `0xffffff80024566f0` | `0xffffff8000a8cff0` | `?` | — | — |
| 87 | `0xffffff80024566f8` | `0xffffff8000a8d030` | `?` | — | — |
| 88 | `0xffffff8002456700` | `0xffffff8000a8d0b0` | `?` | — | — |
| 89 | `0xffffff8002456708` | `0xffffff8000a8d0f0` | `?` | — | — |
| 90 | `0xffffff8002456710` | `0xffffff8000a8d130` | `?` | — | — |
| 91 | `0xffffff8002456718` | `0xffffff8000a8d290` | `?` | — | — |
| 92 | `0xffffff8002456720` | `0xffffff8000a8d3f0` | `?` | — | — |
| 93 | `0xffffff8002456728` | `0xffffff8000a8d550` | `?` | — | — |
| 94 | `0xffffff8002456730` | `0xffffff8000a8d5e0` | `?` | — | — |
| 95 | `0xffffff8002456738` | `0xffffff8000a8d670` | `?` | — | — |
| 96 | `0xffffff8002456740` | `0xffffff8000a8d700` | `?` | — | — |
| 97 | `0xffffff8002456748` | `0xffffff8000a92b40` | `?` | — | — |
| 98 | `0xffffff8002456750` | `0xffffff8000a8d860` | `?` | — | — |
| 99 | `0xffffff8002456758` | `0xffffff8000a8d870` | `?` | — | — |
| 100 | `0xffffff8002456760` | `0xffffff8000a8d9a0` | `?` | — | — |
| 101 | `0xffffff8002456768` | `0xffffff8000a8daa0` | `?` | — | — |
| 102 | `0xffffff8002456770` | `0xffffff8000a8dad0` | `?` | — | — |
| 103 | `0xffffff8002456778` | `0xffffff8000a8dc00` | `?` | — | — |
| 104 | `0xffffff8002456780` | `0xffffff8000a8dd00` | `?` | — | — |
| 105 | `0xffffff8002456788` | `0xffffff8000a8dd30` | `?` | — | — |
| 106 | `0xffffff8002456790` | `0xffffff8000a8de60` | `?` | — | — |
| 107 | `0xffffff8002456798` | `0xffffff8000a8df90` | `?` | — | — |
| 108 | `0xffffff80024567a0` | `0xffffff8000a8e0c0` | `?` | — | — |
| 109 | `0xffffff80024567a8` | `0xffffff8000a8e1c0` | `?` | — | — |
| 110 | `0xffffff80024567b0` | `0xffffff8000a8e4b0` | `?` | — | — |
| 111 | `0xffffff80024567b8` | `0xffffff8000a8e6b0` | `?` | — | — |
| 112 | `0xffffff80024567c0` | `0xffffff8000a8e7c0` | `?` | — | — |
| 113 | `0xffffff80024567c8` | `0xffffff8000a8e9b0` | `?` | — | — |
| 114 | `0xffffff80024567d0` | `0xffffff8000a8ea30` | `?` | — | — |
| 115 | `0xffffff80024567d8` | `0xffffff8000a8ebc0` | `?` | — | — |
| 116 | `0xffffff80024567e0` | `0xffffff8000a8ec60` | `?` | — | — |
| 117 | `0xffffff80024567e8` | `0xffffff8000a8ed10` | `?` | — | — |
| 118 | `0xffffff80024567f0` | `0xffffff8000a8ee60` | `?` | — | — |
| 119 | `0xffffff80024567f8` | `0xffffff8000a8eec0` | `?` | — | — |
| 120 | `0xffffff8002456800` | `0xffffff8000a8efa0` | `?` | — | — |
| 121 | `0xffffff8002456808` | `0xffffff8000a8eff0` | `?` | — | — |
| 122 | `0xffffff8002456810` | `0xffffff8000a8f030` | `?` | — | — |
| 123 | `0xffffff8002456818` | `0xffffff8000a8f0b0` | `?` | — | — |
| 124 | `0xffffff8002456820` | `0xffffff8000a8f120` | `?` | — | — |
| 125 | `0xffffff8002456828` | `0xffffff8000a8f170` | `?` | — | — |
| 126 | `0xffffff8002456830` | `0xffffff8000a8f720` | `?` | — | — |
| 127 | `0xffffff8002456838` | `0xffffff8000a8f810` | `?` | — | — |
| 128 | `0xffffff8002456840` | `0xffffff8000a92b50` | `?` | — | — |
| 129 | `0xffffff8002456848` | `0xffffff8000a92bb0` | `?` | — | — |
| 130 | `0xffffff8002456850` | `0xffffff8002b0bfea` | `?` | — | — |
| 131 | `0xffffff8002456858` | `0xffffff8002afbe12` | `?` | — | — |
| 132 | `0xffffff8002456860` | `0xffffff8000abc370` | `?` | — | — |
| 133 | `0xffffff8002456868` | `0xffffff8000abe4a0` | `?` | — | — |
| 134 | `0xffffff8002456870` | `0xffffff8000a92d20` | `?` | — | — |
| 135 | `0xffffff8002456878` | `0xffffff80023343f2` | `__ZN23IO80211VirtualInterface15configureReportEP19IOReportChannelListjPvS2_` (0f) | — | — |
| 136 | `0xffffff8002456880` | `0xffffff80023344e0` | `__ZN23IO80211VirtualInterface12updateReportEP19IOReportChannelListjPvS2_` (0f) | — | — |
| 137 | `0xffffff8002456888` | `0xffffff8000bb6ba0` | `?` | — | — |
| 138 | `0xffffff8002456890` | `0xffffff8000bb6bc0` | `?` | — | — |
| 139 | `0xffffff8002456898` | `0xffffff8000bb6be0` | `?` | — | — |
| 140 | `0xffffff80024568a0` | `0xffffff8000bb6c00` | `?` | — | — |
| 141 | `0xffffff80024568a8` | `0xffffff8000bb6c20` | `?` | — | — |
| 142 | `0xffffff80024568b0` | `0xffffff8000bb6c40` | `?` | — | — |
| 143 | `0xffffff80024568b8` | `0xffffff8000bb6c60` | `?` | — | — |
| 144 | `0xffffff80024568c0` | `0xffffff8000bb6c80` | `?` | — | — |
| 145 | `0xffffff80024568c8` | `0xffffff8000bb6ca0` | `?` | — | — |
| 146 | `0xffffff80024568d0` | `0xffffff8000bb6cc0` | `?` | — | — |
| 147 | `0xffffff80024568d8` | `0xffffff8000bb6ce0` | `?` | — | — |
| 148 | `0xffffff80024568e0` | `0xffffff8000bb6d00` | `?` | — | — |
| 149 | `0xffffff80024568e8` | `0xffffff8000bb6d20` | `?` | — | — |
| 150 | `0xffffff80024568f0` | `0xffffff8000bb6d40` | `?` | — | — |
| 151 | `0xffffff80024568f8` | `0xffffff8000bb6d60` | `?` | — | — |
| 152 | `0xffffff8002456900` | `0xffffff8000bb6d80` | `?` | — | — |
| 153 | `0xffffff8002456908` | `0xffffff8000bb6da0` | `?` | — | — |
| 154 | `0xffffff8002456910` | `0xffffff8000bb6dc0` | `?` | — | — |
| 155 | `0xffffff8002456918` | `0xffffff8000bb6de0` | `?` | — | — |
| 156 | `0xffffff8002456920` | `0xffffff8000bb6e00` | `?` | — | — |
| 157 | `0xffffff8002456928` | `0xffffff8000bb6e20` | `?` | — | — |
| 158 | `0xffffff8002456930` | `0xffffff8000bb6e40` | `?` | — | — |
| 159 | `0xffffff8002456938` | `0xffffff8000bb6e60` | `?` | — | — |
| 160 | `0xffffff8002456940` | `0xffffff8000bb6e80` | `?` | — | — |
| 161 | `0xffffff8002456948` | `0xffffff8000bb6ea0` | `?` | — | — |
| 162 | `0xffffff8002456950` | `0xffffff8000bb6ec0` | `?` | — | — |
| 163 | `0xffffff8002456958` | `0xffffff8000bb6ee0` | `?` | — | — |
| 164 | `0xffffff8002456960` | `0xffffff8000bb6f00` | `?` | — | — |
| 165 | `0xffffff8002456968` | `0xffffff8000bb6f20` | `?` | — | — |
| 166 | `0xffffff8002456970` | `0xffffff8000bb6f40` | `?` | — | — |
| 167 | `0xffffff8002456978` | `0xffffff8000bb6f60` | `?` | — | — |
| 168 | `0xffffff8002456980` | `0xffffff8000bb6f80` | `?` | — | — |
| 169 | `0xffffff8002456988` | `0xffffff8000bb6fa0` | `?` | — | — |
| 170 | `0xffffff8002456990` | `0xffffff8000bb6fc0` | `?` | — | — |
| 171 | `0xffffff8002456998` | `0xffffff8000bb6fe0` | `?` | — | — |
| 172 | `0xffffff80024569a0` | `0xffffff8000bb7000` | `?` | — | — |
| 173 | `0xffffff80024569a8` | `0xffffff8000bb7020` | `?` | — | — |
| 174 | `0xffffff80024569b0` | `0xffffff8000bb7040` | `?` | — | — |
| 175 | `0xffffff80024569b8` | `0xffffff8000bb7060` | `?` | — | — |
| 176 | `0xffffff80024569c0` | `0xffffff8000bb7080` | `?` | — | — |
| 177 | `0xffffff80024569c8` | `0xffffff8000bb70a0` | `?` | — | — |
| 178 | `0xffffff80024569d0` | `0xffffff8000bb70c0` | `?` | — | — |
| 179 | `0xffffff80024569d8` | `0xffffff8000bb70e0` | `?` | — | — |
| 180 | `0xffffff80024569e0` | `0xffffff8000bb7100` | `?` | — | — |
| 181 | `0xffffff80024569e8` | `0xffffff8000bb7120` | `?` | — | — |
| 182 | `0xffffff80024569f0` | `0xffffff8000bb7140` | `?` | — | — |
| 183 | `0xffffff80024569f8` | `0xffffff8000a93240` | `?` | — | — |
| 184 | `0xffffff8002456a00` | `0xffffff8000a93250` | `?` | — | — |
| 185 | `0xffffff8002456a08` | `0xffffff8000a93450` | `?` | — | — |
| 186 | `0xffffff8002456a10` | `0xffffff80023308aa` | `__ZN23IO80211VirtualInterface5startEP9IOService` (0f) | — | — |
| 187 | `0xffffff8002456a18` | `0xffffff8002330cee` | `__ZN23IO80211VirtualInterface4stopEP9IOService` (0f) | — | — |
| 188 | `0xffffff8002456a20` | `0xffffff8000a934a0` | `?` | — | — |
| 189 | `0xffffff8002456a28` | `0xffffff8000a935c0` | `?` | — | — |
| 190 | `0xffffff8002456a30` | `0xffffff8000a936d0` | `?` | — | — |
| 191 | `0xffffff8002456a38` | `0xffffff8002afbc08` | `?` | — | — |
| 192 | `0xffffff8002456a40` | `0xffffff8002afbc54` | `?` | — | — |
| 193 | `0xffffff8002456a48` | `0xffffff8002afbc9c` | `?` | — | — |
| 194 | `0xffffff8002456a50` | `0xffffff8002330c50` | `__ZN23IO80211VirtualInterface9terminateEj` (0f) | — | — |
| 195 | `0xffffff8002456a58` | `0xffffff8000a937c0` | `?` | — | — |
| 196 | `0xffffff8002456a60` | `0xffffff8000a939d0` | `?` | — | — |
| 197 | `0xffffff8002456a68` | `0xffffff8000a93df0` | `?` | — | — |
| 198 | `0xffffff8002456a70` | `0xffffff8000a94000` | `?` | — | — |
| 199 | `0xffffff8002456a78` | `0xffffff8000a94050` | `?` | — | — |
| 200 | `0xffffff8002456a80` | `0xffffff8000a94060` | `?` | — | — |
| 201 | `0xffffff8002456a88` | `0xffffff8000a940a0` | `?` | — | — |
| 202 | `0xffffff8002456a90` | `0xffffff8000a940c0` | `?` | — | — |
| 203 | `0xffffff8002456a98` | `0xffffff8000a940d0` | `?` | — | — |
| 204 | `0xffffff8002456aa0` | `0xffffff8000a94110` | `?` | — | — |
| 205 | `0xffffff8002456aa8` | `0xffffff8000a94250` | `?` | — | — |
| 206 | `0xffffff8002456ab0` | `0xffffff8000a942c0` | `?` | — | — |
| 207 | `0xffffff8002456ab8` | `0xffffff8000a94330` | `?` | — | — |
| 208 | `0xffffff8002456ac0` | `0xffffff800233023a` | `__ZN23IO80211VirtualInterface6attachEP9IOService` (0f) | — | — |
| 209 | `0xffffff8002456ac8` | `0xffffff800233027c` | `__ZN23IO80211VirtualInterface6detachEP9IOService` (0f) | — | — |
| 210 | `0xffffff8002456ad0` | `0xffffff8000a94a10` | `?` | — | — |
| 211 | `0xffffff8002456ad8` | `0xffffff8000a94a60` | `?` | — | — |
| 212 | `0xffffff8002456ae0` | `0xffffff8000a94a90` | `?` | — | — |
| 213 | `0xffffff8002456ae8` | `0xffffff8000a94ab0` | `?` | — | — |
| 214 | `0xffffff8002456af0` | `0xffffff8000a94ba0` | `?` | — | — |
| 215 | `0xffffff8002456af8` | `0xffffff8000a94bc0` | `?` | — | — |
| 216 | `0xffffff8002456b00` | `0xffffff8000a94be0` | `?` | — | — |
| 217 | `0xffffff8002456b08` | `0xffffff8000a94cd0` | `?` | — | — |
| 218 | `0xffffff8002456b10` | `0xffffff8000a94df0` | `?` | — | — |
| 219 | `0xffffff8002456b18` | `0xffffff8000a94e70` | `?` | — | — |
| 220 | `0xffffff8002456b20` | `0xffffff8000a94e80` | `?` | — | — |
| 221 | `0xffffff8002456b28` | `0xffffff8000a94ee0` | `?` | — | — |
| 222 | `0xffffff8002456b30` | `0xffffff8000a94f50` | `?` | — | — |
| 223 | `0xffffff8002456b38` | `0xffffff8000a94f90` | `?` | — | — |
| 224 | `0xffffff8002456b40` | `0xffffff8000a94fe0` | `?` | — | — |
| 225 | `0xffffff8002456b48` | `0xffffff8000a95000` | `?` | — | — |
| 226 | `0xffffff8002456b50` | `0xffffff8000a950c0` | `?` | — | — |
| 227 | `0xffffff8002456b58` | `0xffffff8000a95150` | `?` | — | — |
| 228 | `0xffffff8002456b60` | `0xffffff8000a951f0` | `?` | — | — |
| 229 | `0xffffff8002456b68` | `0xffffff8000a95260` | `?` | — | — |
| 230 | `0xffffff8002456b70` | `0xffffff8000a952d0` | `?` | — | — |
| 231 | `0xffffff8002456b78` | `0xffffff8000a95340` | `?` | — | — |
| 232 | `0xffffff8002456b80` | `0xffffff8000a95350` | `?` | — | — |
| 233 | `0xffffff8002456b88` | `0xffffff8000a95360` | `?` | — | — |
| 234 | `0xffffff8002456b90` | `0xffffff8000a95630` | `?` | — | — |
| 235 | `0xffffff8002456b98` | `0xffffff8000a95680` | `?` | — | — |
| 236 | `0xffffff8002456ba0` | `0xffffff8000a95760` | `?` | — | — |
| 237 | `0xffffff8002456ba8` | `0xffffff8000a95780` | `?` | — | — |
| 238 | `0xffffff8002456bb0` | `0xffffff8000a957a0` | `?` | — | — |
| 239 | `0xffffff8002456bb8` | `0xffffff8000a957f0` | `?` | — | — |
| 240 | `0xffffff8002456bc0` | `0xffffff80023349d8` | `__ZN23IO80211VirtualInterface13newUserClientEP4taskPvjP12OSDictionaryPP12IOUserClient` (0f) | — | — |
| 241 | `0xffffff8002456bc8` | `0xffffff8000a95a10` | `?` | — | — |
| 242 | `0xffffff8002456bd0` | `0xffffff8002330b24` | `__ZN23IO80211VirtualInterface16stringFromReturnEi` (0f) | — | — |
| 243 | `0xffffff8002456bd8` | `0xffffff80022f997c` | `__ZN23IO80211SkywalkInterface15errnoFromReturnEi` (0f) | — | — |
| 244 | `0xffffff8002456be0` | `0xffffff8000aab950` | `?` | — | — |
| 245 | `0xffffff8002456be8` | `0xffffff8000ab9a20` | `?` | — | — |
| 246 | `0xffffff8002456bf0` | `0xffffff8002b0b8b0` | `?` | — | — |
| 247 | `0xffffff8002456bf8` | `0xffffff8000aba9f0` | `?` | — | — |
| 248 | `0xffffff8002456c00` | `0xffffff8000abb3f0` | `?` | — | — |
| 249 | `0xffffff8002456c08` | `0xffffff8000abc140` | `?` | — | — |
| 250 | `0xffffff8002456c10` | `0xffffff8002b0b8f6` | `?` | — | — |
| 251 | `0xffffff8002456c18` | `0xffffff8000abc3c0` | `?` | — | — |
| 252 | `0xffffff8002456c20` | `0xffffff8000ab9d40` | `?` | — | — |
| 253 | `0xffffff8002456c28` | `0xffffff8000aba840` | `?` | — | — |
| 254 | `0xffffff8002456c30` | `0xffffff8000abc1e0` | `?` | — | — |
| 255 | `0xffffff8002456c38` | `0xffffff80022f8782` | `__ZN23IO80211SkywalkInterface13setPowerStateEmP9IOService` (0f) | — | — |
| 256 | `0xffffff8002456c40` | `0xffffff80022f8a68` | `__ZN23IO80211SkywalkInterface27maxCapabilityForDomainStateEm` (0f) | — | — |
| 257 | `0xffffff8002456c48` | `0xffffff80022f8a32` | `__ZN23IO80211SkywalkInterface31initialPowerStateForDomainStateEm` (0f) | — | — |
| 258 | `0xffffff8002456c50` | `0xffffff8000abe410` | `?` | — | — |
| 259 | `0xffffff8002456c58` | `0xffffff8000abe470` | `?` | — | — |
| 260 | `0xffffff8002456c60` | `0xffffff8000abe480` | `?` | — | — |
| 261 | `0xffffff8002456c68` | `0xffffff8000abcdd0` | `?` | — | — |
| 262 | `0xffffff8002456c70` | `0xffffff8000abcde0` | `?` | — | — |
| 263 | `0xffffff8002456c78` | `0xffffff8000abd130` | `?` | — | — |
| 264 | `0xffffff8002456c80` | `0xffffff8000abd750` | `?` | — | — |
| 265 | `0xffffff8002456c88` | `0xffffff8000abd890` | `?` | — | — |
| 266 | `0xffffff8002456c90` | `0xffffff8000abd910` | `?` | — | — |
| 267 | `0xffffff8002456c98` | `0xffffff8000abe490` | `?` | — | — |
| 268 | `0xffffff8002456ca0` | `0xffffff80023308b4` | `__ZN23IO80211VirtualInterface6enableEj` (0f) | — | — |
| 269 | `0xffffff8002456ca8` | `0xffffff80022f9922` | `__ZN23IO80211SkywalkInterface7disableEj` (0f) | — | — |
| 270 | `0xffffff8002456cb0` | `0xffffff8002afbcfe` | `?` | — | — |
| 271 | `0xffffff8002456cb8` | `0xffffff8002afbd06` | `?` | — | — |
| 272 | `0xffffff8002456cc0` | `0xffffff8002afbe74` | `?` | — | — |
| 273 | `0xffffff8002456cc8` | `0xffffff8002afbe86` | `?` | — | — |
| 274 | `0xffffff8002456cd0` | `0xffffff8002afbe9a` | `?` | — | — |
| 275 | `0xffffff8002456cd8` | `0xffffff8002afbeb0` | `?` | — | — |
| 276 | `0xffffff8002456ce0` | `0xffffff8002afbec6` | `?` | — | — |
| 277 | `0xffffff8002456ce8` | `0xffffff8002afbedc` | `?` | — | — |
| 278 | `0xffffff8002456cf0` | `0xffffff8002afbef2` | `?` | — | — |
| 279 | `0xffffff8002456cf8` | `0xffffff8002afbf08` | `?` | — | — |
| 280 | `0xffffff8002456d00` | `0xffffff8002afbf1e` | `?` | — | — |
| 281 | `0xffffff8002456d08` | `0xffffff8002afbf34` | `?` | — | — |
| 282 | `0xffffff8002456d10` | `0xffffff8002afbf4a` | `?` | — | — |
| 283 | `0xffffff8002456d18` | `0xffffff8002afbf60` | `?` | — | — |
| 284 | `0xffffff8002456d20` | `0xffffff8002b0a430` | `?` | — | — |
| 285 | `0xffffff8002456d28` | `0xffffff8002b0a7d4` | `?` | — | — |
| 286 | `0xffffff8002456d30` | `0xffffff80022f635c` | `__ZN23IO80211SkywalkInterface26initBSDInterfaceParametersEP18ifnet_init_eparamsPP11sockaddr_dl` (0f) | — | — |
| 287 | `0xffffff8002456d38` | `0xffffff8002331ada` | `__ZN23IO80211VirtualInterface19prepareBSDInterfaceEP7__ifnetj` (0f) | — | — |
| 288 | `0xffffff8002456d40` | `0xffffff8002b0aba4` | `?` | — | — |
| 289 | `0xffffff8002456d48` | `0xffffff8002b0abf6` | `?` | — | — |
| 290 | `0xffffff8002456d50` | `0xffffff8002b0ac08` | `?` | — | — |
| 291 | `0xffffff8002456d58` | `0xffffff8002b0ac2a` | `?` | — | — |
| 292 | `0xffffff8002456d60` | `0xffffff8002331b36` | `__ZN23IO80211VirtualInterface17processBSDCommandEP7__ifnetjPv` (0f) | — | — |
| 293 | `0xffffff8002456d68` | `0xffffff8002b0b40e` | `?` | — | — |
| 294 | `0xffffff8002456d70` | `0xffffff8002b0b7b2` | `?` | — | — |
| 295 | `0xffffff8002456d78` | `0xffffff8002b0b7be` | `?` | — | — |
| 296 | `0xffffff8002456d80` | `0xffffff8002331ba6` | `__ZN23IO80211VirtualInterface18setInterfaceEnableEb` (0f) | — | — |
| 297 | `0xffffff8002456d88` | `0xffffff80022f85d2` | `__ZN23IO80211SkywalkInterface15setRunningStateEb` (0f) | — | — |
| 298 | `0xffffff8002456d90` | `0xffffff80022f8f5c` | `__ZN23IO80211SkywalkInterface17handleChosenMediaEj` (0f) | — | — |
| 299 | `0xffffff8002456d98` | `0xffffff80022f8e5e` | `__ZN23IO80211SkywalkInterface22getSupportedMediaArrayEPjS0_` (0f) | — | — |
| 300 | `0xffffff8002456da0` | `0xffffff8002b10bb6` | `?` | — | — |
| 301 | `0xffffff8002456da8` | `0xffffff8002b0b568` | `?` | — | — |
| 302 | `0xffffff8002456db0` | `0xffffff8002b0b836` | `?` | — | — |
| 303 | `0xffffff8002456db8` | `0xffffff8002b1219a` | `?` | — | — |
| 304 | `0xffffff8002456dc0` | `0xffffff8002b0ac48` | `?` | — | — |
| 305 | `0xffffff8002456dc8` | `0xffffff8002b122de` | `?` | — | — |
| 306 | `0xffffff8002456dd0` | `0xffffff8002b0b9a0` | `?` | — | — |
| 307 | `0xffffff8002456dd8` | `0xffffff8002b122fc` | `?` | — | — |
| 308 | `0xffffff8002456de0` | `0xffffff8002b0b9da` | `?` | — | — |
| 309 | `0xffffff8002456de8` | `0xffffff8002b0b9ee` | `?` | — | — |
| 310 | `0xffffff8002456df0` | `0xffffff8002b10d8e` | `?` | — | — |
| 311 | `0xffffff8002456df8` | `0xffffff8002b10dac` | `?` | — | — |
| 312 | `0xffffff8002456e00` | `0xffffff8002b10dbe` | `?` | — | — |
| 313 | `0xffffff8002456e08` | `0xffffff8002331b4a` | `__ZN23IO80211VirtualInterface15getFeatureFlagsEv` (0f) | — | — |
| 314 | `0xffffff8002456e10` | `0xffffff8002b0baba` | `?` | — | — |
| 315 | `0xffffff8002456e18` | `0xffffff8002b0b8a0` | `?` | — | — |
| 316 | `0xffffff8002456e20` | `0xffffff8002b0b8a8` | `?` | — | — |
| 317 | `0xffffff8002456e28` | `0xffffff8002b0b9d2` | `?` | — | — |
| 318 | `0xffffff8002456e30` | `0xffffff8002330e42` | `__ZN23IO80211VirtualInterface6bpfTapEjj` (0f) | — | — |
| 319 | `0xffffff8002456e38` | `0xffffff8002b1231a` | `?` | — | — |
| 320 | `0xffffff8002456e40` | `0xffffff8002b0ba70` | `?` | — | — |
| 321 | `0xffffff8002456e48` | `0xffffff80022f9934` | `__ZN23IO80211SkywalkInterface17classNameOverrideEv` (0f) | — | — |
| 322 | `0xffffff8002456e50` | `0xffffff8002b0bd6c` | `?` | — | — |
| 323 | `0xffffff8002456e58` | `0xffffff8002b0bf86` | `?` | — | — |
| 324 | `0xffffff8002456e60` | `0xffffff8002b0bfde` | `?` | — | — |
| 325 | `0xffffff8002456e68` | `0xffffff8002b0c090` | `?` | — | — |
| 326 | `0xffffff8002456e70` | `0xffffff8002b0c0a4` | `?` | — | — |
| 327 | `0xffffff8002456e78` | `0xffffff8002b0c0ba` | `?` | — | — |
| 328 | `0xffffff8002456e80` | `0xffffff8002b0c0d0` | `?` | — | — |
| 329 | `0xffffff8002456e88` | `0xffffff8002b0c0e6` | `?` | — | — |
| 330 | `0xffffff8002456e90` | `0xffffff8002b0c0fc` | `?` | — | — |
| 331 | `0xffffff8002456e98` | `0xffffff8002b0c112` | `?` | — | — |
| 332 | `0xffffff8002456ea0` | `0xffffff8002b0c128` | `?` | — | — |
| 333 | `0xffffff8002456ea8` | `0xffffff8002b0c13e` | `?` | — | — |
| 334 | `0xffffff8002456eb0` | `0xffffff8002b0c154` | `?` | — | — |
| 335 | `0xffffff8002456eb8` | `0xffffff80022f83d0` | `__ZN23IO80211SkywalkInterface18getHardwareAddressEP10ether_addr` (0f) | — | — |
| 336 | `0xffffff8002456ec0` | `0xffffff80022f8468` | `__ZN23IO80211SkywalkInterface18setHardwareAddressEP10ether_addr` (0f) | — | — |
| 337 | `0xffffff8002456ec8` | `0xffffff8002b10ddc` | `?` | — | — |
| 338 | `0xffffff8002456ed0` | `0xffffff8002b115e2` | `?` | — | — |
| 339 | `0xffffff8002456ed8` | `0xffffff8002331c6c` | `__ZN23IO80211VirtualInterface21setMulticastAddressesEPK10ether_addrj` (0f) | — | — |
| 340 | `0xffffff8002456ee0` | `0xffffff8002b116c0` | `?` | — | — |
| 341 | `0xffffff8002456ee8` | `0xffffff80022f8fdc` | `__ZN23IO80211SkywalkInterface24setPromiscuousModeEnableEbj` (0f) | — | — |
| 342 | `0xffffff8002456ef0` | `0xffffff8002b12126` | `?` | — | — |
| 343 | `0xffffff8002456ef8` | `0xffffff8002b122d8` | `?` | — | — |
| 344 | `0xffffff8002456f00` | `0xffffff8002b1233c` | `?` | — | — |
| 345 | `0xffffff8002456f08` | `0xffffff8002b12350` | `?` | — | — |
| 346 | `0xffffff8002456f10` | `0xffffff8002b12366` | `?` | — | — |
| 347 | `0xffffff8002456f18` | `0xffffff8002b1237c` | `?` | — | — |
| 348 | `0xffffff8002456f20` | `0xffffff8002b12392` | `?` | — | — |
| 349 | `0xffffff8002456f28` | `0xffffff8002b123a8` | `?` | — | — |
| 350 | `0xffffff8002456f30` | `0xffffff8002b123be` | `?` | — | — |
| 351 | `0xffffff8002456f38` | `0xffffff8002b123d4` | `?` | — | — |
| 352 | `0xffffff8002456f40` | `0xffffff8002b123ea` | `?` | — | — |
| 353 | `0xffffff8002456f48` | `0xffffff8002b12400` | `?` | — | — |
| 354 | `0xffffff8002456f50` | `0xffffff8002b12416` | `?` | — | — |
| 355 | `0xffffff8002456f58` | `0xffffff80023308c2` | `__ZN23IO80211VirtualInterface17createPeerManagerEv` (0f) | — | — |
| 356 | `0xffffff8002456f60` | `0xffffff80022f7bf6` | `__ZN23IO80211SkywalkInterface10createPeerEPKhP18IO80211PeerManager` (0f) | — | — |
| 357 | `0xffffff8002456f68` | `0xffffff8002333192` | `__ZN23IO80211VirtualInterface11postMessageEjPvmb` (0f) | — | — |
| 358 | `0xffffff8002456f70` | `0xffffff80022f8dc8` | `__ZN23IO80211SkywalkInterface20reportDataPathEventsEjPvmb` (0f) | — | — |
| 359 | `0xffffff8002456f78` | `0xffffff80022f9032` | `__ZN23IO80211SkywalkInterface19recordOutputPacketsEP24TxSubmissionDequeueStatsS1_` (0f) | — | — |
| 360 | `0xffffff8002456f80` | `0xffffff80022f9014` | `__ZN23IO80211SkywalkInterface18recordOutputPacketE17apple80211_wme_acii` (0f) | — | — |
| 361 | `0xffffff8002456f88` | `0xffffff8002333fae` | `__ZN23IO80211VirtualInterface11logTxPacketEP20IO80211NetworkPacketP20PacketSkywalkScratch17apple80211_wme_acb` (0f) | — | — |
| 362 | `0xffffff8002456f90` | `0xffffff800233403a` | `__ZN23IO80211VirtualInterface21logTxCompletionPacketEP20IO80211NetworkPacketP20PacketSkywalkScratchPh17apple80211_wme_acijbb` (0f) | — | — |
| 363 | `0xffffff8002456f98` | `0xffffff80022f9076` | `__ZN23IO80211SkywalkInterface23recordCompletionPacketsEP24TxCompletionEnqueueStatsS1_` (0f) | — | — |
| 364 | `0xffffff8002456fa0` | `0xffffff8002333cac` | `__ZN23IO80211VirtualInterface11inputPacketEP20IO80211NetworkPacketP15packet_info_tagP12ether_headerPbb` (0f) | — | — |
| 365 | `0xffffff8002456fa8` | `0xffffff80022f907c` | `__ZN23IO80211SkywalkInterface24forwardInfraRelayPacketsEP20IO80211NetworkPacketP12ether_header` (0f) | — | — |
| 366 | `0xffffff8002456fb0` | `0xffffff800233218a` | `__ZN23IO80211VirtualInterface21logSkywalkTxReqPacketEP20IO80211NetworkPacketP20PacketSkywalkScratchPh17apple80211_wme_acb` (0f) | — | — |
| 367 | `0xffffff8002456fb8` | `0xffffff8002332378` | `__ZN23IO80211VirtualInterface14pendingPacketsEh` (0f) | — | — |
| 368 | `0xffffff8002456fc0` | `0xffffff8002332446` | `__ZN23IO80211VirtualInterface11packetSpaceEh` (0f) | — | — |
| 369 | `0xffffff8002456fc8` | `0xffffff80022f953a` | `__ZN23IO80211SkywalkInterface20isChipInterfaceReadyEv` (0f) | — | — |
| 370 | `0xffffff8002456fd0` | `0xffffff80022f85ca` | `__ZN23IO80211SkywalkInterface17isDebounceOnGoingEv` (0f) | — | — |
| 371 | `0xffffff8002456fd8` | `0xffffff80023333f8` | `__ZN23IO80211VirtualInterface9linkStateEv` (0f) | — | — |
| 372 | `0xffffff8002456fe0` | `0xffffff8002331eb0` | `__ZN23IO80211VirtualInterface16setScanningStateEjbP20apple80211_scan_datai` (0f) | — | — |
| 373 | `0xffffff8002456fe8` | `0xffffff80022f9566` | `__ZN23IO80211SkywalkInterface16setDataPathStateEb` (0f) | — | — |
| 374 | `0xffffff8002456ff0` | `0xffffff80022f9870` | `__ZN23IO80211SkywalkInterface14getScanManagerEv` (0f) | — | — |
| 375 | `0xffffff8002456ff8` | `0xffffff80022f957a` | `__ZN23IO80211SkywalkInterface13getControllerEv` (0f) | — | — |
| 376 | `0xffffff8002457000` | `0xffffff8002331f70` | `__ZN23IO80211VirtualInterface20updateLinkParametersEP33apple80211_interface_availability` (0f) | — | — |
| 377 | `0xffffff8002457008` | `0xffffff8002332010` | `__ZN23IO80211VirtualInterface26updateInterfaceCoexRiskPctEy` (0f) | — | — |
| 378 | `0xffffff8002457010` | `0xffffff80022f95b8` | `__ZN23IO80211SkywalkInterface6setLQMEy` (0f) | — | — |
| 379 | `0xffffff8002457018` | `0xffffff80022f95ce` | `__ZN23IO80211SkywalkInterface16updateLinkStatusEv` (0f) | — | — |
| 380 | `0xffffff8002457020` | `0xffffff80022f95e4` | `__ZN23IO80211SkywalkInterface21updateLinkStatusGatedEv` (0f) | — | — |
| 381 | `0xffffff8002457028` | `0xffffff80022f95fa` | `__ZN23IO80211SkywalkInterface23setInterfaceExtendedCCAE18apple80211_channelP21apple80211_cca_report` (0f) | — | — |
| 382 | `0xffffff8002457030` | `0xffffff80022f9610` | `__ZN23IO80211SkywalkInterface15setInterfaceCCAE18apple80211_channeli` (0f) | — | — |
| 383 | `0xffffff8002457038` | `0xffffff8002333862` | `__ZN23IO80211VirtualInterface14setInterfaceNFE18apple80211_channelx` (0f) | — | — |
| 384 | `0xffffff8002457040` | `0xffffff80022f9848` | `__ZN23IO80211SkywalkInterface23setInterfaceOFDMDesenseE18apple80211_channelx` (0f) | — | — |
| 385 | `0xffffff8002457048` | `0xffffff80022f9626` | `__ZN23IO80211SkywalkInterface17removePacketQueueEP20IO80211FlowQueueHash` (0f) | — | — |
| 386 | `0xffffff8002457050` | `0xffffff8002333376` | `__ZN23IO80211VirtualInterface13setDebugFlagsEyj` (0f) | — | — |
| 387 | `0xffffff8002457058` | `0xffffff80023333e2` | `__ZN23IO80211VirtualInterface10debugFlagsEv` (0f) | — | — |
| 388 | `0xffffff8002457060` | `0xffffff8002333884` | `__ZN23IO80211VirtualInterface24setInterfaceChipCountersEP22apple80211_stat_reportP27apple80211_chip_counters_txP33apple80211_chip_error_counters_txP27apple80211_chip_counters_rx` (0f) | — | — |
| 389 | `0xffffff8002457068` | `0xffffff80023338a2` | `__ZN23IO80211VirtualInterface20setInterfaceMIBdot11EP22apple80211_stat_reportP50apple80211_ManagementInformationBasedot11_counters` (0f) | — | — |
| 390 | `0xffffff8002457070` | `0xffffff80023338c0` | `__ZN23IO80211VirtualInterface13setFrameStatsEP22apple80211_stat_reportP25apple80211_frame_counters` (0f) | — | — |
| 391 | `0xffffff8002457078` | `0xffffff80023338de` | `__ZN23IO80211VirtualInterface26setInfraSpecificFrameStatsEP22apple80211_stat_reportP31apple80211_infra_specific_stats` (0f) | — | — |
| 392 | `0xffffff8002457080` | `0xffffff80022f96a6` | `__ZN23IO80211SkywalkInterface19setRxDataStallStatsEP22apple80211_stat_reportP31apple80211_rx_data_stall_report` (0f) | — | — |
| 393 | `0xffffff8002457088` | `0xffffff800233209e` | `__ZN23IO80211VirtualInterface16getWmeTxCountersEPy` (0f) | — | — |
| 394 | `0xffffff8002457090` | `0xffffff80022f96e6` | `__ZN23IO80211SkywalkInterface21setPeerManagerLogFlagEjjj` (0f) | — | — |
| 395 | `0xffffff8002457098` | `0xffffff80023333f2` | `__ZN23IO80211VirtualInterface13setWoWEnabledEb` (0f) | — | — |
| 396 | `0xffffff80024570a0` | `0xffffff8002334c34` | `__ZN23IO80211VirtualInterface10wowEnabledEv` (0f) | — | — |
| 397 | `0xffffff80024570a8` | `0xffffff8002334344` | `__ZN23IO80211VirtualInterface13printDataPathEP12userPrintCtx` (0f) | — | — |
| 398 | `0xffffff80024570b0` | `0xffffff80022f82b6` | `__ZN23IO80211SkywalkInterface17getDataQueueDepthEv` (0f) | — | — |
| 399 | `0xffffff80024570b8` | `0xffffff800233270a` | `__ZN23IO80211VirtualInterface21findOrCreateFlowQueueE20IO80211FlowQueueHash` (0f) | — | — |
| 400 | `0xffffff80024570c0` | `0xffffff8002332c38` | `__ZN23IO80211VirtualInterface30findOrCreateFlowQueueWithCacheE20IO80211FlowQueueHashPb` (0f) | — | — |
| 401 | `0xffffff80024570c8` | `0xffffff80023324e4` | `__ZN23IO80211VirtualInterface21findExistingFlowQueueE20IO80211FlowQueueHash` (0f) | — | — |
| 402 | `0xffffff80024570d0` | `0xffffff80023322e6` | `__ZN23IO80211VirtualInterface17removePacketQueueEPK20IO80211FlowQueueHash` (0f) | — | — |
| 403 | `0xffffff80024570d8` | `0xffffff8002332290` | `__ZN23IO80211VirtualInterface17flushPacketQueuesEv` (0f) | — | — |
| 404 | `0xffffff80024570e0` | `0xffffff80023324fc` | `__ZN23IO80211VirtualInterface9cachePeerEP10ether_addrPj` (0f) | — | — |
| 405 | `0xffffff80024570e8` | `0xffffff80023343d6` | `__ZN23IO80211VirtualInterface9shouldLogEy` (0f) | — | — |
| 406 | `0xffffff80024570f0` | `0xffffff800233436a` | `__ZN23IO80211VirtualInterface9vlogDebugEyPKcP13__va_list_tag` (0f) | — | — |
| 407 | `0xffffff80024570f8` | `0xffffff80023343d0` | `__ZN23IO80211VirtualInterface12vlogDebugBPFEyPKcP13__va_list_tag` (0f) | — | — |
| 408 | `0xffffff8002457100` | `0xffffff80022f9708` | `__ZN23IO80211SkywalkInterface24createLinkQualityMonitorEP11IO80211Peerb` (0f) | — | — |
| 409 | `0xffffff8002457108` | `0xffffff80022f9714` | `__ZN23IO80211SkywalkInterface25releaseLinkQualityMonitorEP11IO80211Peer` (0f) | — | — |
| 410 | `0xffffff8002457110` | `0xffffff8002334c1a` | `__ZN23IO80211VirtualInterface20getP2PSkywalkPeerMgrEv` (0f) | — | — |
| 411 | `0xffffff8002457118` | `0xffffff80022f9bce` | `__ZN23IO80211SkywalkInterface19isCommandProhibitedEi` (0f) | — | — |
| 412 | `0xffffff8002457120` | `0xffffff8002334ec6` | `__ZN23IO80211VirtualInterface8findPeerER10ether_addr` (0f) | — | — |
| 413 | `0xffffff8002457128` | `0xffffff80022f985e` | `__ZN23IO80211SkywalkInterface23setNotificationPropertyEPK8OSSymbolPK8OSObject` (0f) | — | — |
| 414 | `0xffffff8002457130` | `0xffffff80022f9722` | `__ZN23IO80211SkywalkInterface21getWorkerMatchingDictEP8OSString` (0f) | — | — |
| 415 | `0xffffff8002457138` | `0xffffff80022f56e2` | `__ZN23IO80211SkywalkInterface4initEP9IOServiceP10ether_addr` (0f) | — | — |
| 416 | `0xffffff8002457140` | `0xffffff80022f972a` | `__ZN23IO80211SkywalkInterface18isInterfaceEnabledEv` (0f) | — | — |
| 417 | `0xffffff8002457148` | `0xffffff80022f841c` | `__ZN23IO80211SkywalkInterface14getSelfMacAddrEv` (0f) | — | — |
| 418 | `0xffffff8002457150` | `0xffffff80022f973c` | `__ZN23IO80211SkywalkInterface13getPacketPoolEP8OSString` (0f) | — | — |
| 419 | `0xffffff8002457158` | `0xffffff80022f9744` | `__ZNK23IO80211SkywalkInterface9getLoggerEv` (0f) | — | — |
| 420 | `0xffffff8002457160` | `0xffffff80022f854a` | `__ZN23IO80211SkywalkInterface17handleSIOCSIFADDREv` (0f) | — | — |
| 421 | `0xffffff8002457168` | `0xffffff80022f9756` | `__ZN23IO80211SkywalkInterface12debugHandlerEP24apple80211_debug_command` (0f) | — | — |
| 422 | `0xffffff8002457170` | `0xffffff80022f9878` | `__ZN23IO80211SkywalkInterface9statsDumpEv` (0f) | — | — |
| 423 | `0xffffff8002457178` | `0xffffff80022f987e` | `__ZN23IO80211SkywalkInterface19powerOnNotificationEv` (0f) | — | — |
| 424 | `0xffffff8002457180` | `0xffffff80022f9884` | `__ZN23IO80211SkywalkInterface20powerOffNotificationEv` (0f) | — | — |
| 425 | `0xffffff8002457188` | `0xffffff80022f988a` | `__ZN23IO80211SkywalkInterface15getTxQueueDepthEv` (0f) | — | — |
| 426 | `0xffffff8002457190` | `0xffffff80022f9892` | `__ZN23IO80211SkywalkInterface18getRxQueueCapacityEv` (0f) | — | — |
| 427 | `0xffffff8002457198` | `0xffffff80022f989a` | `__ZN23IO80211SkywalkInterface15updateRxCounterEy` (0f) | — | — |
| 428 | `0xffffff80024571a0` | `0xffffff80022f98a0` | `__ZN23IO80211SkywalkInterface17getMultiCastQueueEv` (0f) | — | — |
| 429 | `0xffffff80024571a8` | `0xffffff80022f98a8` | `__ZN23IO80211SkywalkInterface13getAssocStateEv` (0f) | — | — |
| 430 | `0xffffff80024571b0` | `0xffffff80022f98be` | `__ZN23IO80211SkywalkInterface16notifyQueueStateE17apple80211_wme_act` (0f) | — | — |
| 431 | `0xffffff80024571b8` | `0xffffff80022f98c4` | `__ZN23IO80211SkywalkInterface13getTxHeadroomEv` (0f) | — | — |
| 432 | `0xffffff80024571c0` | `0xffffff80022f98cc` | `__ZN23IO80211SkywalkInterface14getRxCompQueueEv` (0f) | — | — |
| 433 | `0xffffff80024571c8` | `0xffffff80022f98d4` | `__ZN23IO80211SkywalkInterface14getTxCompQueueEv` (0f) | — | — |
| 434 | `0xffffff80024571d0` | `0xffffff80022f98dc` | `__ZN23IO80211SkywalkInterface13getTxSubQueueE17apple80211_wme_ac` (0f) | — | — |
| 435 | `0xffffff80024571d8` | `0xffffff80022f98e4` | `__ZN23IO80211SkywalkInterface15getTxPacketPoolEv` (0f) | — | — |
| 436 | `0xffffff80024571e0` | `0xffffff80022f98ec` | `__ZN23IO80211SkywalkInterface15getRxPacketPoolEv` (0f) | — | — |
| 437 | `0xffffff80024571e8` | `0xffffff80022f98f4` | `__ZN23IO80211SkywalkInterface14enableDatapathEv` (0f) | — | — |
| 438 | `0xffffff80024571f0` | `0xffffff80022f98fc` | `__ZN23IO80211SkywalkInterface15disableDatapathEv` (0f) | — | — |
| 439 | `0xffffff80024571f8` | `0xffffff80022f9904` | `__ZN23IO80211SkywalkInterface14getNumTxQueuesEv` (0f) | — | — |
| 440 | `0xffffff8002457200` | `0xffffff80022f9864` | `__ZN23IO80211SkywalkInterface13getLQMSummaryEP22apple80211_lqm_summary` (0f) | — | — |
| 441 | `0xffffff8002457208` | `0xffffff80023347d2` | `__ZN23IO80211VirtualInterface16getEventPipeSizeEv` (0f) | — | — |
| 442 | `0xffffff8002457210` | `0xffffff80023347fc` | `__ZN23IO80211VirtualInterface15createEventPipeEP20IO80211APIUserClient` (0f) | — | — |
| 443 | `0xffffff8002457218` | `0xffffff80022f6cf0` | `__ZN23IO80211SkywalkInterface16destroyEventPipeEP20IO80211APIUserClient` (0f) | — | — |
| 444 | `0xffffff8002457220` | `0xffffff80022f6e1a` | `__ZN23IO80211SkywalkInterface17setUserBufferInfoEP18IOMemoryDescriptory` (0f) | — | — |
| 445 | `0xffffff8002457228` | `0xffffff80022f66a2` | `__ZN23IO80211SkywalkInterface15postMessageIOUCEPKcjPvm` (0f) | — | — |
| 446 | `0xffffff8002457230` | `0xffffff80022f6678` | `__ZN23IO80211SkywalkInterface16isIOUCPipeOpenedEv` (0f) | — | — |
| 447 | `0xffffff8002457238` | `0xffffff80022f6560` | `__ZN23IO80211SkywalkInterface9getRingMDEP20IO80211APIUserClienty` (0f) | — | — |
| 448 | `0xffffff8002457240` | `0xffffff80022f9c56` | `__ZN23IO80211SkywalkInterface10attachPeerEP10ether_addr` (0f) | — | — |
| 449 | `0xffffff8002457248` | `0xffffff80022f9c5c` | `__ZN23IO80211SkywalkInterface10detachPeerEP10ether_addr` (0f) | — | — |
| 450 | `0xffffff8002457250` | `0xffffff80022f9c62` | `__ZN23IO80211SkywalkInterface21setDebugTrafficReportEb` (0f) | — | — |
| 451 | `0xffffff8002457258` | `0xffffff80022f9c7a` | `__ZN23IO80211SkywalkInterface25getDataPathInterfaceStatsEP36apple80211_data_path_interface_stats` (0f) | — | — |
| 452 | `0xffffff8002457260` | `0xffffff80022f9c86` | `__ZN23IO80211SkywalkInterface20getDataPathPeerStatsEP31apple80211_data_path_peer_stats` (0f) | — | — |
| 453 | `0xffffff8002457268` | `0xffffff80022f9c92` | `__ZN23IO80211SkywalkInterface22getLastQueuePacketTimeEP10ether_addr` (0f) | — | — |
| 454 | `0xffffff8002457270` | `0xffffff80022f9c9a` | `__ZN23IO80211SkywalkInterface32getLastRxUnicastLinkActivityTimeEP10ether_addr` (0f) | — | — |
| 455 | `0xffffff8002457278` | `0xffffff80022f9ca2` | `__ZN23IO80211SkywalkInterface24updateInterfaceDataStatsEP36apple80211_data_path_interface_stats` (0f) | — | — |
| 456 | `0xffffff8002457280` | `0xffffff80022f9dae` | `__ZN23IO80211SkywalkInterface19updatePeerDataStatsEP31apple80211_data_path_peer_stats` (0f) | — | — |
| 457 | `0xffffff8002457288` | `0xffffff80022fa1f6` | `__ZN23IO80211SkywalkInterface12logTxLatencyEPhjy` (0f) | — | — |
| 458 | `0xffffff8002457290` | `0xffffff80022fa204` | `__ZN23IO80211SkywalkInterface12logRxLatencyEjy` (0f) | — | — |
| 459 | `0xffffff8002457298` | `0xffffff80022fa20a` | `__ZN23IO80211SkywalkInterface20getNClearTxRxLatencyEP25apple80211_latency_all_acS1_` (0f) | — | — |
| 460 | `0xffffff80024572a0` | `0xffffff80022fa216` | `__ZN23IO80211SkywalkInterface18getLastTxTimeStampERy` (0f) | — | — |
| 461 | `0xffffff80024572a8` | `0xffffff80022fa222` | `__ZN23IO80211SkywalkInterface18getLastRxTimeStampERy` (0f) | — | — |
| 462 | `0xffffff80024572b0` | `0xffffff80022fa22e` | `__ZN23IO80211SkywalkInterface12syncDPSStatsEP26apple82011_postMessage_dps` (0f) | — | — |
| 463 | `0xffffff80024572b8` | `0xffffff80023305f8` | `__ZN23IO80211VirtualInterface4initEP17IO80211ControllerP10ether_addrjPKc` (0f) | — | — |
| 464 | `0xffffff80024572c0` | `0xffffff8002333408` | `__ZN23IO80211VirtualInterface12setLinkStateE16IO80211LinkStatej` (0f) | — | — |
| 465 | `0xffffff80024572c8` | `0xffffff8002333120` | `__ZN23IO80211VirtualInterface13forwardPacketEP20IO80211NetworkPacket` (0f) | — | — |
| 466 | `0xffffff80024572d0` | `0xffffff8002331c88` | `__ZN23IO80211VirtualInterface11handleIoctlEP7__ifnetmPv` (0f) | — | — |
| 467 | `0xffffff80024572d8` | `0xffffff8002334bb2` | `__ZN23IO80211VirtualInterface14addPeerToCacheEPh` (0f) | — | — |
| 468 | `0xffffff80024572e0` | `0xffffff8002334bce` | `__ZN23IO80211VirtualInterface19deletePeerFromCacheEPh` (0f) | — | — |
| 469 | `0xffffff80024572e8` | `0xffffff800233411c` | `__ZN23IO80211VirtualInterface14handleDebugCmdEP24apple80211_debug_command` (0f) | — | — |
| 470 | `0xffffff80024572f0` | `0xffffff8002334eae` | `__ZN23IO80211VirtualInterface20getPeerSlotDataStatsEP10ether_addrbP17peerSlotDataStats` (0f) | — | — |
| 471 | `0xffffff80024572f8` | `0xffffff8002334eb6` | `__ZN23IO80211VirtualInterface16getPeerDataStatsEP10ether_addrbP16packetCounters_t` (0f) | — | — |
| 472 | `0xffffff8002457300` | `0xffffff8002334ebe` | `__ZN23IO80211VirtualInterface18clearPeerDataStatsEP10ether_addrb` (0f) | — | — |
| 473 | `0xffffff8002457308` | `0xffffff8002334ea8` | `__ZN23IO80211VirtualInterface34setAwdlCurrentChannelSequenceIndexEj` (0f) | — | — |
| 474 | `0xffffff8002457310` | `0xffffff8002333098` | `__ZN23IO80211VirtualInterface22isOutputFlowControlledEv` (0f) | — | — |
| 475 | `0xffffff8002457318` | `0xffffff80023330c8` | `__ZN23IO80211VirtualInterface23setOutputFlowControlledEv` (0f) | — | — |
| 476 | `0xffffff8002457320` | `0xffffff80023330f4` | `__ZN23IO80211VirtualInterface25clearOutputFlowControlledEv` (0f) | — | — |
| 477 | `0xffffff8002457328` | `0xffffff800233235e` | `__ZN23IO80211VirtualInterface15flushFlowQueuesEP10ether_addr` (0f) | — | — |
| 478 | `0xffffff8002457330` | `0xffffff8002330b6e` | `__ZN23IO80211VirtualInterface18configureAQMOutputEv` (0f) | — | — |
| 479 | `0xffffff8002457338` | `0xffffff8002330386` | `__ZN23IO80211VirtualInterface11attachToBpfEv` (0f) | — | — |
| 480 | `0xffffff8002457340` | `0xffffff8002331314` | `__ZN23IO80211VirtualInterface14configureIfnetEv` (0f) | — | — |
| 481 | `0xffffff8002457348` | `0xffffff8002334bea` | `__ZN23IO80211VirtualInterface5resetEv` (0f) | — | — |
| 482 | `0xffffff8002457350` | `0xffffff8002334c82` | `__ZN23IO80211VirtualInterface25dupAndTransmitMcastPacketEP20IO80211NetworkPacketPhS2_` (0f) | — | — |
| 483 | `0xffffff8002457358` | `0xffffff8002334d5a` | `__ZN23IO80211VirtualInterface19freeMulticastPacketEP20IO80211NetworkPacket` (0f) | — | — |
| 484 | `0xffffff8002457360` | `0xffffff8002334d62` | `__ZN23IO80211VirtualInterface22triggerAWDLMulticastTxEjh` (0f) | — | — |
| 485 | `0xffffff8002457368` | `0xffffff8002334d6a` | `__ZN23IO80211VirtualInterface14p2pMulticastTxEPP20IO80211NetworkPacketj` (0f) | — | — |
| 486 | `0xffffff8002457370` | `0xffffff8002334d98` | `__ZN23IO80211VirtualInterface14p2pMulticastTxEjb` (0f) | — | — |
| 487 | `0xffffff8002457378` | `0xffffff8002334e44` | `__ZN23IO80211VirtualInterface22signalMulticastPacketsEjj` (0f) | — | — |
| 488 | `0xffffff8002457380` | `0xffffff8002334e68` | `__ZN23IO80211VirtualInterface25sendPendingPacketsToStackEPP20IO80211NetworkPacketj` (0f) | — | — |
| 489 | `0xffffff8002457388` | `0xffffff8002334e88` | `__ZN23IO80211VirtualInterface18freePendingPacketsEPP20IO80211NetworkPacketj` (0f) | — | — |
| 490 | `0xffffff8002457390` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 491 | `0xffffff8002457398` | `0xffffff80023300dc` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface0Ev` (0f) | — | — |
| 492 | `0xffffff80024573a0` | `0xffffff80023300f0` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface1Ev` (0f) | — | — |
| 493 | `0xffffff80024573a8` | `0xffffff8002330106` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface2Ev` (0f) | — | — |
| 494 | `0xffffff80024573b0` | `0xffffff800233011c` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface3Ev` (0f) | — | — |
| 495 | `0xffffff80024573b8` | `0xffffff8002330132` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface4Ev` (0f) | — | — |
| 496 | `0xffffff80024573c0` | `0xffffff8002330148` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface5Ev` (0f) | — | — |
| 497 | `0xffffff80024573c8` | `0xffffff800233015e` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface6Ev` (0f) | — | — |
| 498 | `0xffffff80024573d0` | `0xffffff8002330174` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface7Ev` (0f) | — | — |
| 499 | `0xffffff80024573d8` | `0xffffff800233018a` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface8Ev` (0f) | — | — |
| 500 | `0xffffff80024573e0` | `0xffffff80023301a0` | `__ZN23IO80211VirtualInterface33_RESERVEDIO80211VirtualInterface9Ev` (0f) | — | — |
| 501 | `0xffffff80024573e8` | `0xffffff80023301b6` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface10Ev` (0f) | — | — |
| 502 | `0xffffff80024573f0` | `0xffffff80023301cc` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface11Ev` (0f) | — | — |
| 503 | `0xffffff80024573f8` | `0xffffff80023301e2` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface12Ev` (0f) | — | — |
| 504 | `0xffffff8002457400` | `0xffffff80023301f8` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface13Ev` (0f) | — | — |
| 505 | `0xffffff8002457408` | `0xffffff800233020e` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface14Ev` (0f) | — | — |
| 506 | `0xffffff8002457410` | `0xffffff8002330224` | `__ZN23IO80211VirtualInterface34_RESERVEDIO80211VirtualInterface15Ev` (0f) | — | — |
| 507 | `0xffffff8002457418` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 508 | `0xffffff8002457420` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 509 | `0xffffff8002457428` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 510 | `0xffffff8002457430` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 511 | `0xffffff8002457438` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 512 | `0xffffff8002457440` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 513 | `0xffffff8002457448` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 514 | `0xffffff8002457450` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 515 | `0xffffff8002457458` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 516 | `0xffffff8002457460` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 517 | `0xffffff8002457468` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 518 | `0xffffff8002457470` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 519 | `0xffffff8002457478` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 520 | `0xffffff8002457480` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 521 | `0xffffff8002457488` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 522 | `0xffffff8002457490` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 523 | `0xffffff8002457498` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 524 | `0xffffff80024574a0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 525 | `0xffffff80024574a8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 526 | `0xffffff80024574b0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 527 | `0xffffff80024574b8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 528 | `0xffffff80024574c0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 529 | `0xffffff80024574c8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 530 | `0xffffff80024574d0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 531 | `0xffffff80024574d8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 532 | `0xffffff80024574e0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 533 | `0xffffff80024574e8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 534 | `0xffffff80024574f0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 535 | `0xffffff80024574f8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 536 | `0xffffff8002457500` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 537 | `0xffffff8002457508` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 538 | `0xffffff8002457510` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 539 | `0xffffff8002457518` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 540 | `0xffffff8002457520` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 541 | `0xffffff8002457528` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 542 | `0xffffff8002457530` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 543 | `0xffffff8002457538` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 544 | `0xffffff8002457540` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 545 | `0xffffff8002457548` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 546 | `0xffffff8002457550` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 547 | `0xffffff8002457558` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 548 | `0xffffff8002457560` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 549 | `0xffffff8002457568` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 550 | `0xffffff8002457570` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 551 | `0xffffff8002457578` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 552 | `0xffffff8002457580` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 553 | `0xffffff8002457588` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 554 | `0xffffff8002457590` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 555 | `0xffffff8002457598` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 556 | `0xffffff80024575a0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 557 | `0xffffff80024575a8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 558 | `0xffffff80024575b0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 559 | `0xffffff80024575b8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 560 | `0xffffff80024575c0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 561 | `0xffffff80024575c8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 562 | `0xffffff80024575d0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 563 | `0xffffff80024575d8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 564 | `0xffffff80024575e0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 565 | `0xffffff80024575e8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 566 | `0xffffff80024575f0` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 567 | `0xffffff80024575f8` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 568 | `0xffffff8002457600` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |
| 569 | `0xffffff8002457608` | `0xffffff8000bb59c0` | `?` | PURO | `?` (vtable de Proximity no volcada) |

## Resultado del cruce solicitado

- `IO80211VirtualInterface` slot 490: el target es `___cxa_pure_virtual`; equivalente Broadcom en `AppleBCMWLANProximityInterface`: `?` (no hay entradas de su vtable en los logs conservados).
- `IO80211AWDLProtocol` slot 490: hereda el mismo target puro; equivalente Broadcom: `?`.
- `IO80211AWDLProtocol` slots 507–569: los 63 targets son `___cxa_pure_virtual`; para cada uno, equivalente Broadcom: `?` por la misma falta de volcado de vtable. Los métodos de `AppleBCMWLANProximityInterface` enumerados en `syms_bcm.txt` no proporcionan una asignación slot-a-función sin sus celdas de vtable.
- No se puede recuperar el nombre de ninguna implementación pura con evidencia guardada. `syms_bcm.txt` demuestra que existe el símbolo `__ZTV30AppleBCMWLANProximityInterface` en `0xffffff80017f5b68`, pero no conserva el contenido de esa tabla. PENDIENTE: comparar por índice con las entradas del vtable de Proximity en la sesión IDA del usuario.

## Fuentes precisas

- `Build/ida_awdl/awdl_vtable_probe.log:47–558`: tabla AWDL (incluye el tramo visible 2–511); `:1071–1579`: tabla VirtualInterface.
- `Build/ida_awdl/class_meta_probe.log:47–65`: metaclasses, vtable bases y herencia; `:67–133`: comparación AWDL vs VirtualInterface y slots puros 507–569; `:699+`: comparación VirtualInterface vs Skywalk.
- `Docs/re/io80211_25.6.0/syms.txt:18550,18599`: símbolos globales de vtable IO80211 AWDL/VirtualInterface.
- `Docs/re/io80211_25.6.0/syms_bcm.txt:4086`: símbolo local de vtable AppleBCMWLANProximityInterface; no hay en este dump entradas indexadas.
