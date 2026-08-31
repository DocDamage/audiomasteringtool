# Worker IPC Protocol

Phase 0 defines the contract before implementing the production transport.

## Requirements

- local-only by default
- explicit protocol version
- request ID on every command
- cancellable jobs
- deterministic error envelope
- bounded message size
- no raw pointer/shared-process assumptions
- authentication token for any future socket transport
- worker restart without corrupting project state

## Initial message envelope

```json
{
  "protocol": 1,
  "requestId": "uuid",
  "type": "health|infer|cancel|shutdown",
  "payload": {}
}
```

Response:

```json
{
  "protocol": 1,
  "requestId": "uuid",
  "ok": true,
  "payload": {},
  "error": null
}
```

Binary audio/model tensors should use files or shared-memory handles referenced by metadata rather than giant JSON arrays.
