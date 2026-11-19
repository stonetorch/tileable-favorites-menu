# Build and deploy

- Start local builds with `./scripts/Build-Debug.ps1`. Treat its CLion, MSVC and shared CAF dependency paths as the workstation configuration.
- When vcpkg reports `current_path(...vcpkg): Access is denied`, classify the failure at the dependency access stage. Retry the same script with workspace-external access approval. Diagnose script or compiler changes only from errors reproduced with dependency access.
- When deployment is requested, use `./scripts/Build-Debug.ps1 -Deploy` for the existing WuBingV4 mod. Verify the success message and DLL hash. For isolated deployment tests, use `-DeployTo` with a workspace copy of the mod.
