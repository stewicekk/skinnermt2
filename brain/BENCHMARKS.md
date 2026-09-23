# BENCHMARKS.md — Metin2 Rigging Studio baseline

> Založeno 2026-09-21 (krok 1 programu 10x). Pravidla (viz skill
> `build-ci`): každé číslo = binárka + preset + asset + wall time.
> "Measured-by-construction" odhady se zde nevedou — jen měřené nebo
> explicitně označené převzaté hodnoty. Žádné profilery v repu dosud
> (`docs/AGENT_STATE.md` Wave 18); wall-time end-to-end (parse + práce +
> write), není-li uvedeno jinak.

## Prostředí posledního měření (2026-09-21)

- Windows, `windows-release` preset (MSVC 17.9.8, `/W4 /WX /FS`),
  `m2rig_cli 0.10.0` + `m2rig_tests` z téhož buildu.
- Specifikace stroje/GPU nezaznamenány — čísla jsou relativní baseline
  pro tento repo-stav, ne absolutní výkonnostní tvrzení.

## Měřeno 2026-09-21 (release)

| # | Pipeline | Asset | Výsledek | Wall |
|---|----------|-------|----------|------|
| B1 | `gr22smd` (grnreader98 bridge + parse + coordsys + repair + write) | `Data/Models/warrior_m.gr2` (37 kostí, 5134 verts, 7426 tris, 2 materiály) | SMD venku, `orient` = SANE (37 joints, 0 findings; head 159.34 / feet 12.26; worst rigid 30.89 Bip01 R Thigh) | **1214 ms** |
| B2 | `lod --ratio 0.5` (parse + decimate + write) | warrior_m SMD z B1 | 7426 -> 3713 tris (50.0 %), 5134 -> 2737 verts, 37 kostí / 2 materiály zachovány | **442 ms** |
| B3 | `ctest --preset windows-release` | celý strom | 12/12 suites (unit binárka 137/137 cases incl. 3 utf8 + 1 sRGB + 3 skinning + 1 PBR + 1 IBL + 5 spatial + 4 SH/half/tangent; 3 self-learn CLI sady) | **~44 s** |
| B4 | debug `m2rig_tests` bez triage (`M2RIG_HEAP_TRIAGE=OFF`, Step 4) | celý strom | 128/128 cases green, sRGB + skinning + PBR pixely identické s release | **~124 s wall** |
| B5 | Wave-24 playback shape (strukturální, ne wall-clock: GUI timeline nemá CLI benchmark) | warrior_m (5134 verts) | před: full VB re-upload (~361 KB) každý playback frame; po: bind VB + skin (~160 KB) ONCE + 16 KB paleta/frame | **~23x méně bytů/frame** |
| B6 | Wave-25a GR2 data proof (`gr22smd` + `orient`, uživatelská Data/Models) | 31 GR2 (41–102 joints) | 31/31 exit 0 (vč. diakritiky `ninjałka.gr2`); 30/31 SANE s 0 findings; Gryphon 1-node static CHECK (gate funguje) | **neměřeno** (jednorázový loop, ne opakovatelný benchmark) |
| B7 | Wave-26 index (pozorováno, ne rozpočet) | syntetika | knn 10k x 200: 12 ms brute vs 3 ms tree (stejné odpovědi); transfer 5040v: 106 ms wall, 5040/5040 mapped | **pozorování z testů** |

Reprodukce:

```powershell
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
.\build\release\Release\m2rig_cli.exe gr22smd Data\Models\warrior_m.gr2 $env:TEMP\w.smd
.\build\release\Release\m2rig_cli.exe lod $env:TEMP\w.smd $env:TEMP\w_lod.smd --ratio 0.5
.\build\release\Release\m2rig_cli.exe info $env:TEMP\w_lod.smd
.\build\release\Release\m2rig_cli.exe orient $env:TEMP\w.smd
```

## Převzato z AGENT_STATE (neměřeno v kroku 1, nesrovnávat 1:1)

- Ninja LOD 3208 -> 1603 tris, 2070 -> 1089 verts, 4 submeshe, 0 errors,
  **0.14 s wall** (Wave 19 — samotný decimate, bez parse/write; proto
  není přímo srovnatelné s B2 end-to-end).
- warrior_m.gr2: 37 kostí / 7426 tris; sura_m.gr2: 36 kostí;
  ninja.fbx: 90 kostí / 9624 verts / 3208 tris (Waves 7/12, Characters pass).

## Není baseline (explicitně)

- Debug full-run s triage (`M2RIG_HEAP_TRIAGE=ON`): záměrný CRT heap-check,
  řádově desítky minut — není výkonnostní údaj, jen crash-triage nástroj.
  Default je OFF (B4 je rychlá cesta).
- WARP vs HW GPU u smoke testu: nerozlišeno (fallback povolen).

## Rozpočty navržené pro `build-ci` (`ctest -L perf`, po schválení)

- ninja LOD decimate-only <= 0.30 s (2x hlava nad 0.14 s).
- `gr22smd` warrior_m end-to-end <= 3.0 s (2.5x hlava nad 1214 ms).
- `m2rig_tests` release wall <= 120 s.
