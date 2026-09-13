# METIN2 RIGGING STUDIO — CONTINUATION MASTER PROMPT

## Wave Continuation: Advanced 3D Preview + Noesis Pipeline + Skin/Weight Engineering

POKRAČUJ PŘESNĚ PODLE EXISTUJÍCÍHO PLÁNU A AKTUÁLNÍHO STAVU REPOSITORY.

Nezačínej od nuly.
Neprováděj zbytečný rewrite.
Neodstraňuj funkční části.
Neimplementuj fake/mock funkcionalitu.

Nejdříve analyzuj aktuální stav projektu, předchozí waves, `AGENTS.md`, dokumentaci, zdrojové kódy, testy, existující importéry/exportéry, renderer, UI a všechny dostupné Metin2 sample assets.

Poté pokračuj další koherentní vývojovou vlnou.

Cílem této vlny je posunout **METIN2 RIGGING STUDIO** z rigging/skinning nástroje na kompletní profesionální **Metin2 3D Asset Inspection + Rigging + Skinning + Weight Transfer + Animation + Conversion Workspace**.

---

# 1. ABSOLUTNÍ PRIORITA

Všechno musí být:

* skutečně funkční
* propojené
* stabilní
* realtime, pokud je to možné
* offline-first
* Metin2-oriented
* přesné
* testovatelné
* snadno použitelné
* profesionálně prezentované

Nechci pouze nové panely nebo tlačítka.

Každá UI funkce musí být napojená na skutečný backend/core systém.

Pokud tlačítko existuje:

MUSÍ něco reálně dělat.

Pokud určitá operace není technicky možná:

UI musí zobrazit skutečný stav a důvod.

Nikdy:

* fake progress bar
* fake export
* fake GR2
* fake weight transfer
* fake AI
* fake preview
* placeholder data
* hardcoded demo-only výsledky

---

# 2. NEJDŘÍVE AUDIT

Před implementací proveď:

## Repository audit

Analyzuj:

* aktuální C++ strukturu
* CMake
* dependencies
* renderer
* UI
* asset core
* skeleton systém
* weight systém
* SMD parser
* MSM parser
* GR2 systém
* FBX systém
* animation systém
* project systém
* validation
* tests
* scripts
* Noesis integration
* existující Python backend
* existující web frontend
* dokumentaci
* sample assets

Najdi:

* co již funguje
* co je částečně implementované
* co je rozbité
* co je pouze placeholder
* co lze znovu použít
* co je potřeba refaktorovat
* duplicity
* architecture debt
* chybějící dependencies

Nenič funkční existující implementaci.

---

# 3. NOVÝ HLAVNÍ CÍL

Studio musí umožnit tento workflow:

METIN2 ASSET
↓
IMPORT
↓
FORMAT DETECTION
↓
CONVERSION / EXTRACTION
↓
3D PREVIEW
↓
MESH INSPECTION
↓
SKELETON INSPECTION
↓
SKIN INSPECTION
↓
WEIGHT INSPECTION
↓
OPTIONAL STRIP WEIGHTS
↓
FIT / ALIGN
↓
SKELETON ASSIGNMENT
↓
WEIGHT TRANSFER
↓
WEIGHT REPAIR
↓
SYMMETRY
↓
ANIMATION PREVIEW
↓
VALIDATION
↓
EXPORT
↓
METIN2 OUTPUT

Toto musí být jeden propojený workflow.

---

# 4. UNIVERSAL METIN2 ASSET VIEWER

Vytvoř plnohodnotný realtime 3D asset viewer.

Musí být schopen zobrazit všechny podporované assety, které je možné bezpečně načíst přímo nebo přes definovaný konverzní pipeline.

Primární formáty:

* GR2
* SMD
* FBX
* OBJ
* případně další formáty pouze pokud mají smysl pro Metin2 workflow

Metin2 priority:

GR2
SMD
MSM
MSA
DDS
TGA

FBX/OBJ slouží hlavně jako pracovní/interchange formáty.

---

# 5. FORMAT DETECTION

Při importu automaticky:

1. zjisti extension
2. zjisti skutečný file signature, pokud je možné
3. identifikuj formát
4. zjisti dostupný importer
5. zjisti dostupný converter
6. zjisti závislosti
7. zobraz stav

Příklad:

`warrior_armor.gr2`

→ Metin2 GR2 detected

→ direct reader available

nebo:

→ Noesis conversion available

→ conversion preview available

Uživatel musí vždy vědět:

SOURCE FORMAT
CONVERSION PATH
TARGET FORMAT
IMPORT STATUS
COMPATIBILITY STATUS

---

# 6. NOESIS INTEGRATION

Integruj **Noesis jako externí conversion/inspection backend**, nikoliv jako náhradu za vlastní core.

Noesis musí být volitelný externí nástroj.

Podporuj konfiguraci:

NOESIS_PATH

Například:

`C:\Tools\Noesis\Noesis.exe`

Nikdy necpat pevnou cestu do zdrojového kódu.

---

# 7. NOESIS PIPELINE

Vytvoř Noesis adapter:

`NoesisBridge`

Responsibility:

* detect installation
* validate executable
* query supported conversion
* launch conversion
* capture stdout
* capture stderr
* capture exit code
* timeout
* cancellation
* output validation
* cleanup temporary files
* report conversion errors

Pipeline:

Metin2 GR2
→ Noesis
→ FBX
→ internal importer
→ canonical asset representation
→ realtime viewer.

---

# 8. GR2 → FBX

Implementuj první-class workflow:

## GR2 → FBX

Uživatel:

IMPORT GR2

Studio:

1. detekuje GR2
2. zkontroluje interní GR2 reader
3. pokud není dostupná bezpečná přímá cesta:
   použije Noesis
4. vytvoří temporary/intermediate FBX
5. načte FBX
6. převede ho do canonical representation
7. zobrazí asset

Uživatel musí vidět:

`GR2 → FBX → METIN2 ASSET`

---

# 9. NOESIS CONVERSION PROFILE

Vytvoř:

Noesis Profile

Obsah:

* executable
* arguments
* input extension
* output extension
* output directory
* overwrite policy
* timeout
* post-processing
* temporary directory

Profily:

Metin2 GR2 → FBX
Metin2 GR2 → OBJ
Metin2 GR2 → SMD pokud konkrétní plugin/pipeline skutečně podporuje
atd.

Nepředstírej podporu konverze, kterou Noesis nebo lokální plugin nemá.

---

# 10. TEMPORARY CONVERSION CACHE

Noesis output neukládej automaticky jako permanentní asset.

Používej:

`cache/conversions/`

Hash:

source path
+
timestamp/file hash
+
converter version
+
arguments

Pokud je konverze již dostupná:

reuse cache.

---

# 11. CONVERSION VALIDATION

Po každé konverzi:

* existuje output?
* lze ho načíst?
* obsahuje mesh?
* obsahuje skeleton?
* obsahuje materials?
* obsahuje textures?
* obsahuje animation?
* počet vertices?
* počet triangles?
* počet bones?
* počet submeshes?

Pokud conversion vytvoří poškozený FBX:

NEPOKLÁDEJ HO ZA ÚSPĚŠNÝ.

---

# 12. CANONICAL ASSET MODEL

Všechny importéry musí převést data do společného interního modelu.

Pipeline:

GR2
→ adapter
→ CanonicalAsset

SMD
→ adapter
→ CanonicalAsset

FBX
→ adapter
→ CanonicalAsset

OBJ
→ adapter
→ CanonicalAsset

Renderer nesmí řešit jednotlivé formáty.

Renderer pracuje pouze s:

CanonicalMesh
CanonicalSkeleton
CanonicalSkin
CanonicalMaterial
CanonicalAnimation.

---

# 13. ASSET INSPECTOR

Vytvoř profesionální Asset Inspector.

Zobraz:

## General

Name
Format
File size
Hash
Source
Import method
Compatibility

## Geometry

Vertices
Triangles
Submeshes
Materials
Bounds
Normals
UVs
Tangents

## Skeleton

Bone count
Root
Hierarchy depth
Missing bones
Duplicate bones

## Skin

Weighted vertices
Unweighted vertices
Influence count
Normalization

## Animation

Animation count
Frames
FPS
Duration

---

# 14. REALTIME RENDERER

Viewport musí být skutečný realtime renderer.

Implementuj:

* DirectX 11
* hardware accelerated rendering
* depth buffer
* backface culling
* alpha blending
* alpha testing where required
* texture sampling
* normal handling
* material rendering
* skeletal GPU skinning

---

# 15. VIEWPORT MODES

Provide:

SOLID
MATERIAL
TEXTURED
WIREFRAME
WIREFRAME OVERLAY
NORMALS
UV
WEIGHT HEATMAP
BONE WEIGHTS
SKELETON
COLLISION
BOUNDING BOX

Switching musí být realtime.

---

# 16. METIN2 MATERIAL PREVIEW

Primárně podporuj Metin2 relevantní texture workflow.

Zobraz:

* DDS
* TGA
* texture paths
* material references
* alpha
* diffuse
* normal map pokud skutečně dostupná
* transparency

Missing texture:

zobraz explicitní warning.

Nepoužívej náhodnou náhradní texturu bez označení.

---

# 17. TEXTURE PATH RESOLUTION

Při načtení assetu zkus:

1. absolute path
2. project root
3. asset-relative path
4. Metin2 virtual path
5. configured client roots
6. configured search paths

Zobraz:

FOUND
MISSING
AMBIGUOUS

---

# 18. CAMERA

Profesionální viewport:

Orbit
Pan
Zoom
Focus Selected
Focus All

Hotkeys:

F
Home
NumPad 1
NumPad 3
NumPad 7
NumPad 5

Podporuj:

Perspective
Orthographic.

---

# 19. LIGHTING

Preview lighting:

Environment
Key light
Fill light
Rim light

Presets:

Neutral
Studio
Metin2-like
Dark
Bright

Lighting nesmí měnit exportovaná data.

---

# 20. GRID / AXIS

Zobraz:

X
Y
Z

Grid
World origin
Object origin
Bone axis.

---

# 21. OBJECT SELECTION

Podporuj:

* mesh
* submesh
* vertex
* face
* bone
* material

Selection musí být synchronizovaná mezi:

Viewport
Scene tree
Properties.

---

# 22. SCENE TREE

Hierarchie:

Project
→ Asset
→ Mesh
→ Submesh
→ Material
→ Skeleton
→ Bone
→ Animation.

Kliknutí na objekt:

selects it in viewport.

Kliknutí ve viewportu:

selects corresponding tree item.

---

# 23. SKELETON VIEWER

Plnohodnotný skeleton inspector.

Zobraz:

* bones
* parent
* children
* local transform
* global transform
* bind transform
* inverse bind matrix
* length
* influence count

Bone selection musí okamžitě aktualizovat weight visualization.

---

# 24. BONE WEIGHT INSPECTOR

Po výběru vertexu:

Vertex #1234

Influences:

Bone A: 0.62
Bone B: 0.28
Bone C: 0.10

Zobraz:

* sorted influences
* weight
* bone
* mirror bone
* distance

Allow manual editing.

---

# 25. STRIP WEIGHTS

IMPLEMENTUJ plnohodnotnou funkci:

## STRIP SKIN / REMOVE WEIGHTS

Tato funkce je zásadní.

Musí umět odstranit skinning z již skinned modelu.

Operace:

SKINNED MESH
↓
REMOVE SKIN
↓
STATIC MESH

Zachovej:

* vertex positions
* normals
* UVs
* indices
* topology
* materials
* submeshes

Odstraň:

* bone influences
* skin weights
* skin deformation dependency

Podle režimu může zachovat:

* skeleton jako samostatný asset
* bind pose transform
* animation data jako oddělený resource

---

# 26. STRIP WEIGHTS MODES

Provide:

### Remove Weights

Odstraní pouze influence data.

### Bake Current Pose

Nejdříve aplikuje aktuální skeletal deformation.

Potom odstraní skin.

### Bake Bind Pose

Vypočítá mesh zpět do bind/reference pose.

Potom odstraní skin.

### Extract Skeleton

Odstraní skin, ale uloží skeleton jako samostatný asset.

---

# 27. STRIP WEIGHTS SAFETY

Nikdy nepřepisuj originál.

Create:

`*_unskinned`

or internal project variant.

Preview:

BEFORE
AFTER

Statistics:

Vertices affected
Bones removed
Weights removed
Materials preserved
UV preserved
Topology preserved.

---

# 28. WEIGHT TRANSFER ENGINE

Rozšiř stávající weight transfer engine na profesionální pipeline.

Priority:

1. Nearest vertex
2. Nearest surface
3. Barycentric
4. Normal-aware
5. Distance-weighted
6. Bone-aware
7. UV-aware
8. Symmetry-aware
9. Hybrid

---

# 29. WEIGHT TRANSFER INPUT

Source:

SKINNED MODEL

Target:

UNSKINNED MODEL

Skeleton:

TARGET METIN2 SKELETON

Pipeline:

SOURCE
→ analyze
→ TARGET
→ analyze
→ alignment
→ candidate search
→ transfer
→ normalization
→ repair
→ confidence
→ preview
→ apply.

---

# 30. AUTOMATIC ALIGNMENT

Before transfer calculate:

bounding box
center
scale
orientation
principal axes

Offer:

Auto Align

but NEVER automatically apply destructive transforms.

Preview first.

---

# 31. TRANSFER METHODS

Allow user to choose:

Fast
Balanced
High Quality
Metin2 Armor

`Metin2 Armor` should combine:

surface distance
normal compatibility
bone proximity
symmetry
local geometry.

---

# 32. TRANSFER QUALITY

For each target vertex calculate:

confidence 0..1

Classify:

HIGH
MEDIUM
LOW
FAILED

Display heatmap.

---

# 33. TRANSFER PREVIEW

Before Apply:

show:

OLD
NEW
DIFF
CONFIDENCE

Allow:

Apply All
Apply High Confidence
Apply Selected
Cancel.

---

# 34. WEIGHT REPAIR

After transfer:

Detect:

* zero-weight vertices
* NaN
* infinity
* negative weights
* duplicate influences
* over-limit influences
* unnormalized vertices
* isolated bone influence
* extreme influence changes

Automatic repair:

sanitize
→ merge
→ sort
→ reduce
→ normalize
→ validate.

---

# 35. WEIGHT LIMITS

Do not blindly assume a universal influence count.

Determine the target Metin2 export requirements from:

* actual target format
* target exporter
* verified samples
* configured compatibility profile.

If reduction occurs:

show:

Before:
6 influences

After:
4 influences

Removed:
0.031 weight mass

---

# 36. WEIGHT PAINTING

Professional brush engine:

ADD
SUBTRACT
SMOOTH
BLUR
NORMALIZE
SHARPEN
FLOOD
COPY
PASTE
MIRROR

Properties:

Radius
Strength
Falloff
Symmetry
Occlusion
Front-face
Connected-only.

---

# 37. WEIGHT SYMMETRY

Support:

X
Y
Z

For Metin2 character armor:

X should be optimized and easy to use.

Maintain:

bone mirror map
vertex mirror map
center-line tolerance.

---

# 38. METIN2 ARMOR FITTING

Add tools:

AutoFit
Shrinkwrap
Surface Offset
Body Clearance
Clip Detection

Provide presets:

Chest
Shoulders
Arms
Hands
Legs
Boots
Pelvis
Cape.

---

# 39. CLIPPING DETECTION

Compare armor against reference body.

Detect:

inside-body penetration
external gap
extreme overlap.

Display:

CLIPPING
CLEARANCE
UNKNOWN.

Never silently modify mesh.

---

# 40. ANIMATION PREVIEW

Animation system must drive actual skeleton deformation.

Support:

timeline
play
pause
loop
scrubbing
FPS
frame stepping.

Viewport updates:

skeleton
mesh
weights
materials.

---

# 41. POSE TESTING

Provide:

Bind Pose
T-Pose
Idle
Walk
Run
Attack
Skill
Death

Only use animations actually available in the loaded asset/project.

Do not fake missing animations.

---

# 42. DEFORMATION HEATMAP

During animation:

detect extreme vertex displacement.

Highlight:

* stretching
* collapse
* spikes
* detached regions
* unexpected movement.

Provide deformation diagnostics.

---

# 43. REALTIME WEIGHT EDITING

Changing weight must immediately update deformation.

Pipeline:

Brush
→ CPU weight update
or
→ GPU buffer update
→ skinning
→ viewport.

Do not require export/reload.

---

# 44. GPU SKINNING

Implement:

bone matrix palette
vertex bone IDs
vertex weights
vertex shader skinning.

Provide CPU fallback:

DEBUG CPU SKINNING

Compare CPU/GPU results.

---

# 45. CPU/GPU VALIDATION

For selected mesh:

CPU result
vs
GPU result

Calculate positional difference.

Report:

Max Error
Average Error
Vertices Over Threshold.

---

# 46. METIN2 COMPATIBILITY PROFILE

Create selectable profile:

`Metin2`

Future architecture can support:

`Metin2 Legacy`
`Metin2 Custom Client`

But do not create generic game profiles unnecessarily.

---

# 47. METIN2 PATH / PROJECT ROOTS

Allow configuring:

Metin2 client root
PC
PC2
PC3
Locale
Item
Effect
etc.

Do not assume every client has identical directory layout.

---

# 48. DEPENDENCY GRAPH

Every imported asset must be able to show:

MODEL
↓
SKELETON
↓
TEXTURE
↓
MSM
↓
ANIMATION
↓
OTHER REFERENCES.

Missing dependencies must be visible.

---

# 49. CONVERSION CENTER

Create dedicated UI:

## CONVERSION CENTER

Input
Output
Converter
Status
Progress
Logs

Example:

`warrior.gr2`

GR2
→ Noesis
→ FBX
→ Internal Asset

Buttons:

Convert
Preview
Open Result
Open Folder
Retry
Cancel.

---

# 50. CONVERSION JOB SYSTEM

Long operations run asynchronously.

Support:

queue
cancel
retry
parallel jobs where safe
progress
logs.

UI must never freeze.

---

# 51. DRAG & DROP

Support drag/drop:

GR2
SMD
FBX
OBJ
MSM
DDS
TGA

into viewport.

Automatically detect type.

---

# 52. IMPORT WORKSPACE

When importing:

show modal:

Detected:
Metin2 GR2

Possible pipelines:

Direct GR2
Noesis → FBX

Choose.

If direct parser is verified and safe:

prefer direct.

If not:

Noesis fallback.

---

# 53. IMPORT COMPARISON

If both direct GR2 and Noesis FBX are available:

allow:

Compare Direct vs Noesis.

Show:

vertex count
triangle count
bones
materials
bounds
animations
textures.

This is extremely important for detecting conversion discrepancies.

---

# 54. METIN2 ROUND-TRIP TESTING

Create automated:

GR2
→ Noesis FBX
→ internal representation
→ validation

Compare against original where possible.

Metrics:

vertex count
bounds
skeleton
bone names
materials
texture references
animation count.

Do not expect byte-identical conversion.

Compare semantic compatibility.

---

# 55. EXPORT CENTER

Dedicated panel:

SOURCE
TARGET
FORMAT
VALIDATION
OUTPUT

Possible:

SMD
FBX
MSM
GR2 through verified pipeline/compiler.

Never present unsupported targets as available.

---

# 56. PRE-EXPORT VALIDATION

Before export:

Skeleton valid
Weights valid
Mesh valid
Materials valid
Textures valid
Paths valid
Animation valid
Format valid
Converter available.

Display:

READY TO EXPORT

or:

BLOCKED

with exact reasons.

---

# 57. UI DESIGN

The GUI must be visually polished.

Dark professional technical-art workstation.

Avoid clutter.

Use clear hierarchy.

Main layout:

TOP:
Project / Import / Export / Undo / Redo / Validation

LEFT:
Asset Browser
Scene
Skeleton

CENTER:
3D Viewport

RIGHT:
Inspector
Weights
Transfer
Material

BOTTOM:
Timeline
Validation
Console.

---

# 58. CONTEXTUAL UI

Do not display every advanced option at once.

Example:

If no mesh selected:

show asset properties.

If bone selected:

show bone tools.

If vertex selected:

show weights.

If Weight Transfer active:

show transfer configuration.

This keeps UI easy to use.

---

# 59. BEGINNER / ADVANCED

Provide:

Simple Mode

and:

Advanced Mode.

Simple workflow:

Load
→ Fit
→ Transfer
→ Preview
→ Validate
→ Export.

Advanced exposes:

KD-tree
BVH
normal weighting
thresholds
bone-space scoring
symmetry tolerance
influence reduction
etc.

---

# 60. TOOLTIP SYSTEM

Every advanced control gets tooltip:

What
Why
Effect
Recommended value.

Example:

`Normal Weight`

"Controls how strongly surface-normal similarity affects source triangle selection during weight transfer."

---

# 61. LIVE STATUS BAR

Always display:

Asset
Vertices
Bones
Weights
FPS
Memory
GPU
Validation status.

Example:

`Warrior_Armor | 12,483 verts | 24 bones | Weighted | 60 FPS | VALID`

---

# 62. PERFORMANCE

Target:

60 FPS viewport.

Optimize:

* GPU skinning
* BVH
* KD-tree
* caching
* dirty updates
* async import
* async conversion
* async weight transfer.

Never block UI on heavy operations.

---

# 63. CANCELLATION

Long jobs must support cancellation:

Import
Conversion
Weight transfer
Validation
Batch processing.

Cancellation must safely clean temporary resources.

---

# 64. FILE SAFETY

Never modify original assets automatically.

Always work through:

project copy
or
explicit output path.

---

# 65. UNDO / REDO

All destructive operations:

Weight transfer
Strip weights
Normalize
Mirror
Fit
Bake
Transform

must be undoable where practical.

Large operations may use snapshot-based undo.

---

# 66. PROJECT SNAPSHOTS

Before destructive operations optionally create:

Auto Snapshot

Example:

Before_WeightTransfer
Before_StripWeights
Before_BakePose.

---

# 67. DIAGNOSTICS

Add:

Performance Monitor
Memory Monitor
Asset Diagnostics
Conversion Log
Import Log
Weight Transfer Log.

---

# 68. ERROR REPORTING

Every failure must say:

WHAT
WHERE
WHY
HOW TO FIX.

Example:

`GR2 conversion failed`

instead of:

`Error 1`

---

# 69. TEST SUITE

Add tests for:

GR2 detection
Noesis detection
Noesis conversion
FBX import
SMD import
SMD export
MSM parsing
weight stripping
weight normalization
weight transfer
symmetry
skeleton mapping
animation
texture resolution
validation.

---

# 70. GOLDEN TESTS

Use real Metin2 sample files where legally available.

For every asset store expected metadata.

Do not depend exclusively on synthetic meshes.

---

# 71. WEIGHT TRANSFER REGRESSION

Create deterministic test:

Source weighted armor
+
Target unweighted armor
=======================

expected transfer characteristics.

Test:

* normalized
* no NaN
* no negative
* valid bone IDs
* confidence range
* deterministic output.

Same input must produce same output unless a deliberately nondeterministic algorithm is selected.

---

# 72. STRIP WEIGHTS REGRESSION

Test:

Skinned
→ Strip
→ Static

Verify:

positions preserved according to selected mode
normals preserved
UV preserved
indices preserved
materials preserved
weights removed.

Test Bind Pose and Current Pose modes separately.

---

# 73. NOESIS TESTING

Noesis is an external dependency.

Tests must distinguish:

Noesis installed
Noesis unavailable
Noesis invalid
Noesis conversion failed
Noesis conversion succeeded.

Do not make the entire application fail because Noesis is missing.

---

# 74. OFFLINE-FIRST

Core functionality must work without internet.

Required:

* viewer
* SMD
* weights
* transfer
* validation
* project
* snapshots

Optional external dependencies:

Noesis
licensed Granny compiler
optional AI runtime.

---

# 75. NOESIS CONFIGURATION UI

Settings:

Noesis executable
Auto-detect
Test installation
Reset
Conversion cache
Temporary directory
Timeout.

Button:

`TEST NOESIS`

Result:

`Noesis detected: version ...`

or:

`Noesis unavailable`.

Do not assume a version if it cannot be detected.

---

# 76. AI

Keep AI optional.

AI may assist:

weight prediction
bone mapping
problem detection
transfer suggestions.

But deterministic algorithms must remain functional without AI.

AI result must pass:

normalization
bone constraints
validation
confidence.

---

# 77. CODE QUALITY

Use:

C++20
RAII
strong types
clear ownership
thread-safe job system
explicit errors
const correctness
modern filesystem
safe bounds checking.

Avoid:

global mutable state
raw ownership
magic constants
duplicated algorithms
format logic inside UI.

---

# 78. ARCHITECTURE RULE

The dependency direction must remain:

UI
↓
Application
↓
Services
↓
Core
↓
Format adapters

Never:

UI
→ directly parse GR2
UI
→ directly execute arbitrary shell commands
Renderer
→ modify project files.

---

# 79. NOESIS SECURITY

External process execution must be controlled.

Use:

validated executable path
argument escaping
working directory
timeout
process termination
output validation.

Do not execute arbitrary user-provided commands.

---

# 80. DOCUMENTATION

Update:

`docs/ARCHITECTURE.md`

`docs/METIN2_FORMATS.md`

`docs/WEIGHTS.md`

`docs/WEIGHT_TRANSFER.md`

`docs/CONVERSION.md`

`docs/NOESIS.md`

`docs/USER_GUIDE.md`

`docs/AGENT_STATE.md`

`CHANGELOG.md`

Document only what is actually implemented.

---

# 81. AGENT STATE

At the end of this wave update:

`docs/AGENT_STATE.md`

Include:

Completed
Working
Partial
Broken
Verified
Unknown
Next wave.

---

# 82. BUILD DISCIPLINE

After every significant implementation:

Clean configure
→ Build
→ Unit tests
→ Integration tests
→ Launch application
→ Runtime smoke test
→ Inspect logs
→ Fix errors
→ Rebuild.

Never leave a knowingly broken build.

---

# 83. VISUAL QA

Do not only compile.

Launch the actual application.

Inspect:

* startup
* viewport
* import
* asset selection
* skeleton
* weights
* transfer
* strip weights
* animation
* validation
* conversion UI
* export UI.

Check:

alignment
spacing
text overflow
broken controls
incorrect state
stale data
FPS
crashes.

---

# 84. END-TO-END TEST

Perform at least one complete real workflow:

1. Load real Metin2 asset.
2. Import GR2.
3. If necessary convert using Noesis.
4. Load FBX result.
5. Display textured mesh.
6. Display skeleton.
7. Inspect weights.
8. Strip weights.
9. Validate static mesh.
10. Load target Metin2 skeleton.
11. Transfer weights.
12. Preview transfer.
13. Apply.
14. Paint/repair weights.
15. Mirror.
16. Normalize.
17. Play animation.
18. Validate deformation.
19. Run final validator.
20. Export supported output.
21. Re-import exported asset.
22. Compare result.

This workflow must work without manually modifying internal project files.

---

# 85. IMPORTANT: DO NOT STOP AT UI

Do not consider this wave complete merely because:

* panels exist
* buttons exist
* viewport renders
* Noesis path can be selected.

The feature is complete only when:

UI
+
core
+
renderer
+
asset model
+
conversion
+
validation
+
tests

are connected and working.

---

# 86. PRIORITY ORDER FOR THIS WAVE

P0:
Repository audit and architecture verification.

P1:
Canonical asset/mesh/skeleton integration.

P2:
Professional realtime viewport.

P3:
GR2/FBX/SMD import pipeline.

P4:
Noesis bridge.

P5:
GR2 → FBX conversion workflow.

P6:
Asset inspector.

P7:
Skeleton/weight inspector.

P8:
Strip weights.

P9:
Advanced weight transfer.

P10:
Weight repair + symmetry.

P11:
Animation/deformation preview.

P12:
Validation.

P13:
Export center.

P14:
UX polish.

P15:
Regression tests.

---

# 87. DEFINITION OF DONE

This wave is NOT complete until:

[ ] Application builds
[ ] Application launches
[ ] No critical runtime errors
[ ] Viewport works
[ ] Real asset can be imported
[ ] GR2 workflow works where supported
[ ] Noesis integration works when configured
[ ] GR2 → FBX works when supported
[ ] FBX can be loaded into canonical representation
[ ] SMD works
[ ] Skeleton displays
[ ] Materials display
[ ] Textures resolve
[ ] Existing weights can be inspected
[ ] Strip Weights works
[ ] Weight transfer works
[ ] Transfer confidence works
[ ] Weight repair works
[ ] Symmetry works
[ ] Animation preview works
[ ] Validation works
[ ] Undo/redo works
[ ] Project remains stable
[ ] Tests pass
[ ] Documentation updated
[ ] End-to-end workflow tested.

---

# 88. FINAL AGENT INSTRUCTION

DO NOT blindly implement everything in one enormous change.

Break this wave into coherent sub-waves.

After each sub-wave:

ANALYZE
→ IMPLEMENT
→ BUILD
→ TEST
→ RUN
→ REVIEW
→ DOCUMENT
→ CONTINUE.

If an existing implementation is already better than the proposed one:

KEEP IT.

If a feature is broken:

FIX ROOT CAUSE.

If a feature is missing:

IMPLEMENT IT.

If an assumption about Metin2 is uncertain:

INVESTIGATE IT.

If a format behavior cannot be verified:

MARK IT UNKNOWN.

Never invent compatibility.

The final product must behave like a serious professional technical-art tool specifically designed around the Metin2 asset pipeline.

The user should be able to open a Metin2 model, inspect it visually and technically, convert it when necessary, remove existing skinning, fit it to a Metin2 skeleton, transfer/repair weights, preview actual animation deformation, validate the result and export it through a verified Metin2-compatible pipeline — all from one coherent, polished and easy-to-use application.
