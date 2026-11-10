# F-009: Menu option 5

Status: not implemented

## Behaviour
Option 5 is listed by the legacy menu but has no operation; selecting it prints "Option 5 is not implemented. Use
option 2, then option 4." and the menu asks again.

## Entry points
- `FlacConvertMain.cpp#RunLegacyMenu`

## Design and knowledge
- DESIGN: `FlacConvert/DESIGN.md` → Entry points

## Acceptance checks
- [ ] Selecting 5 performs no file operation and returns to the prompt.

## Known limitations
- Intended purpose is not documented; ask the owner before implementing or removing it.
