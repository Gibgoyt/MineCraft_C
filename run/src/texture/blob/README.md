# run/src/texture/blob

**Purpose:** reads the texture blob: refuses anything that is not exactly what the format allows, and gives a view of each section.
**Tier:** Arena (depth 2), though it allocates nothing.
**Owns:** nothing. A view borrows the bytes it was given.
**Lifetime:** a view is valid while its bytes are; the embedded ones last as long as the process.
**Threads:** any. There is no shared state. `run/src/texture/` calls it on `main` at startup.
**Public functions** (`run/include/texture/blob/index.h`):

- `texture_blob_parse`: checks every field and the hash, and fills a view.
- `texture_blob_hash`: FNV-1a 64.
- `texture_blob_status_text`: one sentence about a status.

**May be called by:** `run/src/texture/` and its test.
**Depends on:** `run/include/types/texture_blob/`, which defines the format.
