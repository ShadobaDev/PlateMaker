# Security Policy

## Supported versions

`libplatemaker` is a small project maintained by one person. Only the **latest released
version** receives fixes; there are no long-term support branches. The library is
pre-1.0, so consumers should expect to track the current release.

| Version | Supported |
|---|---|
| Latest release | ✅ |
| Anything older | ❌ — please update first |

## Reporting a vulnerability

**Please report security issues privately, not as a public issue.**

Use GitHub's private reporting form:
**<https://github.com/ShadobaDev/PlateMaker/security/advisories/new>**

Only the maintainer can see reports submitted this way, and nothing becomes public
until an advisory is published.

Please include, as far as you can:

- what the issue is and how to reproduce it,
- the `libplatemaker` version and the toolchain you built or downloaded for
  (Windows MinGW / Windows MSVC / Linux),
- a minimal input file or CLI invocation that triggers it, if applicable,
- anything you already know about impact.

**What to expect.** This is a hobby project, so please allow a few days for a first
response. You will get an acknowledgement, a discussion of the issue, and credit in the
advisory and release notes unless you prefer to stay anonymous.

## What is in scope

`libplatemaker` is an **offline image-processing library** (plus `platemaker-cli`). It
reads and writes local image and workspace files given to it by its host application and
performs no network communication of its own. In scope:

- memory-safety issues reachable by processing a **malicious or malformed image file** —
  the primary attack surface, since the pipeline is native C++ over libvips,
- issues triggered by a crafted **workspace/project JSON** file,
- path traversal or unintended file writes via workspace paths, output directories or
  the thumbnail cache,
- anything allowing code execution through `platemaker-cli`'s argument handling.

Vulnerabilities in bundled third-party libraries (libvips and its dependency DLLs shipped
in the Windows packages) are best reported to those projects directly, but tell us as well
so the bundled version can be updated.

## Verifying a download

Release archives publish SHA-256 sidecars and a GitHub build-provenance attestation, so a
package can be tied back to the commit and workflow run that produced it:

```powershell
gh attestation verify <archive.zip> --repo ShadobaDev/PlateMaker
```

The GUI application that consumes this library documents the wider download-integrity and
code-signing story in its
[code signing policy](https://github.com/ShadobaDev/Platemaker-qt/blob/main/docs/CODE-SIGNING-POLICY.md).

**Note on antivirus warnings:** unsigned binaries from small projects are routinely flagged
by machine-learning heuristics without anything actually being detected. That is a false
positive rather than a security issue — feel free to raise it as a normal
[public issue](https://github.com/ShadobaDev/PlateMaker/issues).
