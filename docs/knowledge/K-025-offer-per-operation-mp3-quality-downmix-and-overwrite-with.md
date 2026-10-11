---
id: K-025
kind: decision
title: "Offer per-operation MP3 quality downmix and overwrite with safe defaults"
area: [FlacConvert]
status: active
supersedes: [K-019]
created: 2026-10-11
reviewed: 2026-10-11
cites:
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConfigureManualMp3
  - path: FlacConvert/src/FlacConvertMain.cpp
    symbol: ConvertFLACToMP3
---

**Fact:** Manual menu option 6 converts active configured libraries to `OutDirectory/MP3/<mediaName>` (falling back to the source folder name). Each selection prompts for VBR quality 0-9, multichannel stereo downmix and overwrite, defaulting to 0/NO/NO. Enter keeps defaults, invalid values retry, and Back/Exit or EOF cancels. Settings are per operation, not persisted. The preview shows the chosen profile; RUN alone authorizes execution through the shared ConversionOptions and folder plan. Overwrite covers output MP3, sidecars and companions, never sources; duplicate destinations and source/output overlaps remain errors.

**Why it matters:** Users can select supported encoding behavior without leaving the menu, while safe defaults prevent accidental channel reduction or output replacement. Metadata preservation and separate per-library output paths must remain the same across manual and explicit conversion.

**Rejected:** Fixed manual settings required switching to the explicit command for supported choices. Persisting interactive choices or enabling overwrite/downmix by default risks applying a previous destructive choice to a later operation. A second generic Run prompt would duplicate the existing preview confirmation.
