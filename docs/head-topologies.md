# Head topologies present in this load order

Produced by running `tri_dump` over every loose `femalehead.tri` under the Mod
Organizer mods folder. All 23 parsed to exactly end-of-file.

| Vertices | Morphs | Source |
|---|---|---|
| 996 | 45 | Expressive Facial Animation (replaces the vanilla head path) |
| 996 | 45 | Project ja-Kha'jay (Khajiit ohmes-raht) |
| 996 | **134** | Expressive Facegen Morphs SE |
| 3832 | 45 | High Poly Head, and 8 NPC replacers built on it |
| 11498 | 49 | UBE 2.0 |
| 11498 | **14** | UBE 2.0, `facegenmorphs/morphs/LRO/EXPR2` |
| 12206 | 38 | Brelyna / Illia / Jordis / Lydia DF Edit |
| 12206 | 45 | Eola / Fura / Jenassa DF Edit |
| 12206 | 47 | Camilla / Sapphire DF Edit |

## Why this matters

The design document names three topologies -- vanilla 996, High Poly Head 3832,
and "UBE differs again". The reality on one ordinary load order is **five**
(996, 3832, 11498, 12206, plus whatever BSAs hold), and the morph counts run
from 14 to 134.

Two consequences for the design:

1. **D4 is correct and then some.** Region masks must be derived per-vertex at
   load. Authoring them per-topology was never going to scale to this.

2. **Same topology does not mean same morph set.** The 12206 family splits three
   ways -- 38, 45 and 47 morphs -- across mods that are otherwise
   interchangeable. A sequence authored against a 47-morph head goes partly
   inert on a 38-morph one *of identical topology*. That is exactly the case
   `MorphRegistry::Classify` distinguishes: `WrongTopology` (this head lacks a
   morph others have) is a different diagnostic from `UnknownName` (nobody has
   it), and section 6.7 wants the first surfaced rather than silently dropped.

The 996/134 entry from Expressive Facegen Morphs is the extended-morph case the
MFEE interop in section 6.4 exists to serve: three times the vanilla morph count
on vanilla topology.
