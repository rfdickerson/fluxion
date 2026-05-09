# Security Policy

Fluxion is experimental pre-release software. Do not use it for production
safety-critical systems or hard realtime certification work.

## Reporting A Vulnerability

Please report security issues privately to the repository maintainers instead
of opening a public issue. Include:

- A short description of the issue.
- Steps to reproduce it.
- The affected commit or release, if known.
- Any proof-of-concept input needed to understand the problem.

The maintainers will acknowledge reports as quickly as practical and coordinate
public disclosure after a fix or mitigation is available.

## Scope

Security-sensitive areas include:

- Compiler or JIT crashes triggered by untrusted `.flx` input.
- Runtime memory safety issues.
- Incorrect execution of checked programs.
- Telemetry output that leaks data unexpectedly.

The language and VM are still evolving, so correctness bugs may be handled as
normal issues unless they have a clear security impact.
