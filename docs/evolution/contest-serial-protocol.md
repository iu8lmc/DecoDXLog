# Experimental contest serial message contract

This is a core contract under development, not an enabled ContestNet feature. The existing `serialreq`/`serialgrant` flow remains volatile and is not made reliable by this document.

An operator or future session configuration must explicitly choose one scope and one authority node. The protocol does not elect a replacement authority and does not allocate locally when that authority is unavailable. Separate authority databases can allocate the same number; do not run two authorities for one scope.

The requester first persists a `pending` request with scope, requester ID, request ID, authority ID and minimum. An experimental `serial_request_v3` frame carries those fields and `serialProtocol: 1` inside the existing `DXLNET` envelope. A retry must reuse the same request ID and parameters. The authority checks the configured scope, its own ID and the envelope's requester ID, then commits a reservation before returning a `serial_grant_v3` frame containing the same fields plus `serial`. A duplicate request returns the persisted number. A reused request ID with a different minimum is rejected.

The requester checks the configured scope and authority, the envelope sender, all recorded request fields and the positive integer serial before committing the grant. Duplicate identical grants are idempotent; conflicting grants are rejected. A granted number is then bound to one explicit use ID; retrying that use ID is idempotent. This binding is not yet atomic with a QSO write or proof that a number was transmitted. Never silently renumber a transmitted number.

ContestNet UDP sender IDs are unauthenticated and may be spoofed. These checks prevent accidental misrouting, not hostile impersonation. This contract has not been negotiated with older clients, does not resolve split-brain authority, and is not wired to the controller or QSO workflow. See `PROGRESS.md` for actual test evidence and outstanding LAN, restart, partition and interoperability work.
