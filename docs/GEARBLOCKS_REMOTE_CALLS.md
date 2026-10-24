# GearBlocks remote calls (build 0.8.9740)

The game's own remote method layer (`NetworkRemoteMethods`), as registered while a scene is running (listed with `gearblocks/plugin/Probe.cs`, action `Methods`). The handle is a hash of the method; payloads are byte arrays in the game's own format. `ToServer_*` go from a player to the server, `FromServer_*` from the server to all players (or one). The shipped build has no transport behind them: this is the seam a relay plugs into (see `GEARBLOCKS_MULTIPLAYER.md`).

94 registered. The `PartPaint` entries repeat once per paint slot.


## Construction.PopulateConstructionsAsset

- `ToServer_RequestSpawnPart` `cf2882cb`
- `ToServer_RequestSpawnConstruction` `0124efc7`
- `ToServer_RequestSpawnCharacterConstruction` `5d3766f4`
- `ToServer_RequestDuplicatePart` `632dac05`
- `ToServer_RequestDuplicateSelectedParts` `37269dd9`
- `ToServer_RequestDuplicateConstruction` `c54285cd`
- `ToServer_RequestDestroyPart` `20636f52`
- `ToServer_RequestDestroySelectedParts` `cc7f643c`
- `ToServer_RequestDestroyConstructions` `3b88d837`
- `FromServer_DeserializeConstruction` `1f959bd1`
- `FromServer_DeserializeConstructionCustomIDs` `453b7f1c`
- `FromServer_DematerialiseConstruction` `8b171fab`
- `FromServer_DestroyConstruction` `dc0bac48`

## Construction.ConstructionOperationsAsset

- `ToServer_RequestSetConstructionFrozen` `c8d6f8cd`
- `FromServer_BroadcastSetConstructionFrozen` `f1dcdd30`
- `ToServer_RequestToggleConstructionsFrozen` `df20aed2`
- `ToServer_RequestToggleConstructionsVulnerability` `fc008b2a`
- `FromServer_BroadcastToggleConstructionsVulnerability` `e36ae781`
- `ToServer_RequestSetPartDestroyable` `2e9fb41b`
- `ToServer_RequestSetConstructionDestroyable` `00b62e3f`
- `FromServer_BroadcastSetPartDestroyable` `93a08aa0`
- `FromServer_BroadcastSetConstructionDestroyable` `2e1f3134`
- `ToServer_RequestSetPartCollidable` `fd12032c`
- `ToServer_RequestSetConstructionCollidable` `422c1a88`
- `FromServer_BroadcastSetPartCollidable` `182ad915`
- `FromServer_BroadcastSetConstructionCollidable` `29dcba81`
- `ToServer_RequestSetPartsStageIdx` `24c620c5`
- `FromServer_BroadcastSetPartsStageIdx` `7fe429ba`
- `ToServer_RequestAssignBuilderPlayerID` `8fb4974a`
- `FromServer_BroadcastAssignBuilderPlayerID` `6dd07fef`
- `FromServer_ClearBuilderPlayerID` `690da1a6`

## Player.PopulatePlayersAsset

- `ToServer_RequestSpawnPlayer` `70ed68bb`
- `FromServer_DeserializePlayer` `1f63ad19`

## Construction.PartBehaviourOperationsAsset

- `ToServer_RequestTogglePartBehavioursActivated` `cf7b012b`
- `ToServer_RequestSetConstructionBehavioursActivated` `3cb53a79`
- `ToServer_RequestAddLinks` `ab2d7305`
- `ToServer_RequestRemoveLinks` `27ac81c6`
- `FromServer_BroadcastAddLinks` `460d9a8a`
- `FromServer_BroadcastRemoveLinks` `ff5630c7`

## Network.RemoteNotificationAsset

- `FromServer_NotifyFailure` `fe00d58b`
- `FromServer_NotifyError` `b84082f3`

## Construction.AttachmentOperationsAsset

- `ToServer_RequestCreateAttachments` `9ac93cf8`
- `ToServer_RequestDeleteAttachments` `b0d65d21`
- `ToServer_RequestDeleteAllPartAttachments` `9489ee05`
- `ToServer_RequestDeleteAllConstructionAttachments` `f5f053cd`
- `ToServer_RequestReplaceAttachments` `e65ac680`
- `ToServer_RequestLockAttachments` `dc6b2331`
- `FromServer_BroadcastCreateAttachments` `09da846d`
- `FromServer_BroadcastDeleteAttachments` `a7789c88`
- `FromServer_BroadcastReplaceAttachments` `880d6343`
- `FromServer_BroadcastLockAttachments` `211d3960`

## Game.GameSimulationAsset

- `FromServer_SetSimulationParams` `7c2f3030`

## Game.GameLoader

- `FromServer_NewScene` `50a46e2b`
- `FromServer_LoadScene` `87ce7a5b`

## Toolbox.PartSelection

- `FromServer_SetMovingSelection` `76c1cfe4`
- `ToServer_RequestBeginMove` `43b744a8`
- `ToServer_NotifyEndMoveSelection` `b5c59d8c`
- `ToServer_NotifyClearSelection` `4cfa81f9`
- `ToServer_NotifySelectConstruction` `cde7fac1`
- `ToServer_NotifySelectParts` `5e79e1f4`
- `ToServer_NotifySelectPart` `d4a5abc5`

## Construction.PartPaint

- `FromServer_BroadcastApplyPaint` `2134773e`
- `ToServer_NotifyApplyPaint` `86ece555`
- `FromServer_BroadcastApplyPaint` `223478d1`
- `ToServer_NotifyApplyPaint` `85ece3c2`
- `FromServer_BroadcastApplyPaint` `1b346dcc`
- `ToServer_NotifyApplyPaint` `88ece87b`
- `FromServer_BroadcastApplyPaint` `1c346f5f`
- `ToServer_NotifyApplyPaint` `87ece6e8`
- `FromServer_BroadcastApplyPaint` `1d3470f2`
- `ToServer_NotifyApplyPaint` `8aeceba1`
- `FromServer_BroadcastApplyPaint` `1e347285`
- `ToServer_NotifyApplyPaint` `89ecea0e`
- `FromServer_BroadcastApplyPaint` `17346780`
- `ToServer_NotifyApplyPaint` `8ceceec7`
- `FromServer_BroadcastApplyPaint` `18346913`
- `ToServer_NotifyApplyPaint` `8beced34`
- `FromServer_BroadcastApplyPaint` `19346aa6`
- `ToServer_NotifyApplyPaint` `8eecf1ed`
- `FromServer_BroadcastApplyPaint` `1a346c39`
- `ToServer_NotifyApplyPaint` `8decf05a`

## Construction.PlayerLocker

- `FromServer_Unlock` `cd580e28`
- `FromServer_LockToPart` `80183aad`

## Toolbox.Inventory

- `FromServer_Clear` `28335ea6`
- `FromServer_RemoveParts` `f0f60be7`
- `FromServer_AddParts` `3936fbba`
- `FromServer_SetMode` `0266c75c`

## Toolbox.Targeter

- `ToServer_NotifyEndUse` `ebe925c1`
- `ToServer_NotifyBeginUse` `3f02051d`

## Player.Player

- `FromServer_TriggerJump` `35caf844`
- `FromServer_BroadcastActivateTool` `d96e7c5c`
- `ToServer_NotifyActivateTool` `7079a7ab`

## Environment.Boundary

- `FromServer_SetEnabled` `b6055a1e`
- `FromServer_SetDims` `4f0615fe`
