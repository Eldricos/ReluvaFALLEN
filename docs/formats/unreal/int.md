# The Fallen: documents, objectives and map strings (int/*.int)

Status: **partly implemented** (`src/engines/unreal/vm_documents.cpp`): the
documents, the maps' objectives and titles are read. The actors' strings
(messages, tricorder readings) aren't yet.
**Checked on The Fallen** (October 2026).

Beside the dialogue tables ([dialogue.md](dialogue.md)), the install's
`int` folder holds `Document.int`, the readable documents, and one `.int`
per map, named after it (`M01_KiraL1A.int`). Some maps have none (the cut
scenes'); a few names have no map (`M01_KiraL1.int`, `M10_L2.int`).

They're Windows text files, lines ending CR LF, laid out as `.ini` files:
`[Section]` lines and `Key=Value` lines under them. Some start with a row
of asterisks. A key can come many times in a section, and the order
counts: the lines are drawn in it.

`Document.int` has a section for each document, `[Document_1]` to
`[Document_11]`:

| Key | Meaning |
|---|---|
| `Title` | its title in the list ("GRIGARI") |
| `MissionLevel` | the first mission it's shown in (3, 5 or 10) |
| `Color` | the colour of the next line: Purple, PurpleBright, Orange, OrangeBright, Gold, GoldBright, Blue, BlueBright (`CTVE_Document.SetColor`) |
| `Text` | a line of text, empty for a blank line |
| `IMAGE` | a texture to show (`DS9_Objectives.M11_Grigari`), then options (`Justify=Left`, the only one used) |

A map's `.int` has:
- **`[SaveGameString]`**: its `Title` ("Bajor - Monastery").
- **`[Objective_1]` onwards**: each objective's `Title`, `bCritical` and
  `bActive` (True or False, any case), its lines (`Color`, `Text`,
  `IMAGE`), and a `CompleteMessage`.
- **A section for each of its actors that shows text**, named after the
  actor's tag (`[consx1]`). These hold `MESSAGE`, the tricorder's
  `TRICORDERNAME`, `TRICORDERBIG1`/`2` and `TRICORDERSUB1` to `3`,
  `SCANSTRING` and a few others: the actors' localised strings.
