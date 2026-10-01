# Publishing and releasing RigReel Studio

## First publication

The initial repository should be created privately, reviewed on GitHub, and then made public.

1. Configure the author identity you want displayed on the initial commit:

   ```powershell
   git config user.name "YOUR NAME"
   git config user.email "YOUR VERIFIED GITHUB EMAIL"
   ```

   A GitHub-provided no-reply address is appropriate if you do not want a personal email in commit history.

2. Review and create the initial commit:

   ```powershell
   git status --short
   git diff --cached --check
   git commit -m "Initial open-source release"
   ```

3. On GitHub, create an empty private repository named `rigreel`. Do not add a README, license, or `.gitignore`, because they already exist locally.

4. Connect and push:

   ```powershell
   git remote add origin https://github.com/YOUR_ACCOUNT/rigreel.git
   git push -u origin main
   ```

   The equivalent GitHub CLI command is:

   ```powershell
   gh repo create rigreel --private --source . --remote origin --push
   ```

5. On GitHub, verify the detected GPL-3.0 license, README rendering, Actions result, tracked file list, and security settings. Enable private vulnerability reporting and branch protection for `main` when available.

6. Add the repository description and topics:

   - Description: `Open-source animation and machinima studio for creating films with games you own.`
   - Topics: `animation`, `machinima`, `qt`, `qml`, `re-engine`, `reframework`, `game-modding`, `filmmaking`

7. Change repository visibility to public only after this review.

## Alpha release

Use semantic prerelease tags, beginning with `v0.1.0-alpha.1`:

```powershell
git tag -a v0.1.0-alpha.1 -m "RigReel Studio 0.1.0 alpha 1"
git push origin v0.1.0-alpha.1
```

Create a GitHub prerelease from the tag. State exactly which games and workflows were tested, list major limitations, and link to the build instructions.

Do not commit `app/` or a release ZIP. Attach a Windows ZIP to the GitHub Release after running `studio/qt/package.sh` and completing the binary-license audit in `THIRD_PARTY_NOTICES.md`. In particular, provide corresponding source and notices for the exact Qt and FFmpeg binaries shipped.

Suggested asset name:

```text
RigReelStudio-Windows-x64-v0.1.0-alpha.1.zip
```

## Every release

- Start from a clean clone and repeat the Release build, Python/Lua/shell checks, unit tests, and startup smoke test.
- Confirm no extracted game content, credentials, personal paths, local settings, or generated media are tracked.
- Update compatibility claims conservatively.
- Review every deployed DLL and its license.
- Preserve legacy project-path compatibility or document migrations before changing saved-data locations.
