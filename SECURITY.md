# Security Policy

## Reporting

Do not publish exploitable security issues in a public issue. Report them privately to the repository owner through an agreed private channel.

## Windows 1.0 security posture

- untrusted audio is treated as hostile input
- heavy ML work is isolated in a worker process
- model downloads must be checksum-verified before loading
- the application has no telemetry or crash-log upload transport
- local crash logs are disabled by default and sanitize user-directory paths
- any future cloud upload requires explicit transport security, consent, and a
  published data-retention policy
- logs must not contain raw audio or sensitive local file paths
