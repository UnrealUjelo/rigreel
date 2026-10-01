# Security policy

RigReel parses complex binary formats from local game installations and exposes a control service on `127.0.0.1`. Treat malformed files and unintended network exposure as security-sensitive.

## Reporting a vulnerability

Please use GitHub's private vulnerability reporting feature when it is enabled for the repository. Do not open a public issue for a vulnerability that could expose user files, execute code, escape local-only network assumptions, or cause unsafe memory access.

Include the affected version or commit, a minimal reproduction that contains no copyrighted game content, the expected impact, and any proposed mitigation. Please do not test against systems or accounts you do not own.

## Supported versions

Until the first stable release, security fixes are made on the latest `main` branch and the newest published alpha release only.
