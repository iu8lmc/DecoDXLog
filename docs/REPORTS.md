# Report export context

The activity report's **Export CSV** saves the rows from the current completed report snapshot. **Export context** separately saves a JSON description of that same snapshot, including report kind, UTC year (0 means all years), band, station, mode, dimensions, aggregate, generation time and row count. The context does not include the log path, log identity or QSO rows.

The `csvSha256` field is the SHA-256 of the CSV bytes that the current snapshot produces. Compare it with the file's SHA-256 to establish that a context file corresponds to that CSV. Export both before running a different report. They are separate operations, not an atomic pair; a failed or skipped context export does not create a sidecar automatically. This is particularly useful for an empty CSV, whose header cannot contain per-row filter values without creating a fictitious data row. The digest identifies bytes, not their authenticity or award eligibility.
