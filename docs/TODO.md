- Make controls "context-aware" or "item-aware". For example, place a block by tapping and breaking it by holding. But
  when equipped with a weapon, instead add a crosshair and make it so tapping attacks.
- When breaking a block, show breaking progress on a bar below.
- A method to report warnings, non-fatal errors.
- Change error handling strategy to not duplicate resource-releasing code in both of the success and error paths.
- In our deferred material lookup system for terrain, reuse the `texture_layer` as material ID for another indirect lookup. This would allow much greater flexibility without exploding memory usage. Evaluate the mobile performance cost of this.
- An optimization technique for voxel models that culls completely occluded voxels by flood-filling transparent voxels
  from all the transparent voxels touching the edges.
