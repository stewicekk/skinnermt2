---
name: cz-localization
description: Czech string inventory + cs.json key plan + diacritics font gate; persisted keys NEVER translated
---

# cz-localization

Czech-first UI localization built on the LANDED Unicode dialog path.
Covers string inventory, `cs.json` key plan, diacritics font gate and
the persisted-key rule. Extends `workspace-restore` (persisted `ui`
flags, name-keyed sets) + `viewport-ux` (dialogs, theme, input).

## Current truth

- Dialog Unicode path LANDED: `src/app/file_dialog.cpp:112` uses
  `SHBrowseForFolderW` with UTF-8 end to end (`:90-122` — title,
  initial selection and result all travel as wide strings; the old ANSI
  mojibake on paths like `D:\modely\brnění` is fixed and documented in
  the function comment). The `viewport-ux` P0-bug entry predates this
  fix — do not re-litigate ANSI, gate what remains.
- 0x i18n in the UI: EN-only literals throughout `src/app/panels.cpp`;
  no string table, no locale switch (recorded in `viewport-ux` Current
  truth and `workspace-restore` skill).
- Persisted keys are EN literals with no migration story
  (`workspace-restore` skill Current truth): 14 panel flags in
  `user_prefs.json` (`ui` section) vs docking in `imgui.ini`
  (`main.cpp:179-180` per that skill); locks/hidden/sets persisted by
  bone NAME (`src/project_file.cpp` per that skill).
- `cs.json` does NOT exist anywhere in the repo (verified by search) —
  it is a plan item from `workspace-restore` Target contract + item 9
  of `docs/UPGRADE_10X_BRAINSTORM.md`, not landed content.

## Target contract

- String inventory: every user-visible literal in `src/app/panels.cpp`
  catalogued with file:line, grouped by panel; NO code change in this step.
- `cs.json` key plan: stable string ids (`panel.action.detail`), Czech
  values with full diacritics (ěščřžýáíéůúďťň); persisted keys
  (`user_prefs.json` `ui` flags, `.m2rig` fields, bone-name-keyed sets)
  are NEVER translated — migration table (old EN key -> new id) keeps
  old workspaces loading via tolerant parse.
- Diacritics font gate: selected ImGui font proves full Czech glyph
  coverage (rendered atlas probe: ěščřžýáíéůúďťň + EN base); dialog +
  viewport + tree labels screenshot-compared before/after; failure =
  missing-glyph tofu blocks the locale switch, never ships silent.
- Locale switch is runtime-toggleable with EN fallback per missing key;
  missing key logs once, never crashes, never shows an empty label.

## Entry points

- `src/app/panels.cpp` (string inventory source), `src/app/file_dialog.cpp`
  (`:90-122` landed Unicode reference pattern for all new string paths).
- `config/user_prefs.json` (`ui` section) + `imgui.ini` docking state;
  `src/project_file.cpp` (`saveWorkspace/loadWorkspace`, name-keyed sets).
- `Data/Models` + diacritics paths (e.g. `D:\modely\brnění`-class
  fixtures) as the font/dialog proof corpus.

## Test gate

- `cs_keys_cover_inventory` (every inventoried literal has a key; no
  orphan keys).
- `persisted_keys_stable_across_locale` (workspace saved in EN loads
  byte-identical under `cs`; tolerant parse ignores unknown fields).
- `diacritics_font_probe` (atlas contains the full Czech set; rendered
  label pixels differ from tofu reference).
- `dialog_unicode_roundtrip` (diacritics directory survives
  pick -> persist -> reload).

## Failure handling

Persisted data wins over display text: on any conflict the EN persisted
key is kept and the translation is fixed the same day (`docs-brain-sync`
rule). Never ship a locale that drops glyphs or renames persisted keys.
