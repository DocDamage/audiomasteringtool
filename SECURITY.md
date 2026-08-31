# Security Policy

## Reporting

Do not publish exploitable security issues in a public issue. Report them privately to the repository owner through an agreed private channel.

## Phase 0 security posture

- untrusted audio is treated as hostile input
- heavy ML work is isolated in a worker process
- model downloads must be checksum-verified before loading
- future cloud upload requires explicit transport security and data-retention policy
- logs must not contain raw audio or sensitive local file paths unless diagnostic mode is explicitly enabled
