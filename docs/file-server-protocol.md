# hms-cpap-fysetc File Server Protocol

## Architecture

The fysetc device is a **dumb read-only HTTP file server**.
It serves the CPAP SD card over WiFi. hms-cpap is responsible for all polling,
diffing, state tracking, and retry logic.

```
fysetc (ESP32, 192.168.x.y:80)
  GET /ls?path=DATALOG       ← hms-cpap calls this periodically
  GET /file?path=...&offset= ← hms-cpap fetches new/changed bytes

hms-cpap (your server)
  - polls /ls on a schedule
  - compares response against its own snapshot
  - fetches /file for new files and grown files
  - tracks confirmed byte offsets per file
```

No handshake. No negotiation. No state on the fysetc side.

---

## Endpoints

### `GET /ls?path=DATALOG`

Returns a flat JSON array of all files reachable from `path`.

**Parameters:**
| Param | Default | Description |
|-------|---------|-------------|
| `path` | `DATALOG` | Root path to list. Use `DATALOG` for everything, or `DATALOG/20260328` for one night. |

**Response — 200 OK:**
```json
[
  {"path":"DATALOG/STR.EDF","size":8192},
  {"path":"DATALOG/20260328/BRP.EDF","size":20480},
  {"path":"DATALOG/20260328/STR.EDF","size":4096},
  {"path":"DATALOG/20260329/BRP.EDF","size":16384}
]
```

- Array is flat — no nesting.
- Recurses exactly one level into subdirectories (date dirs).
- Files at the root level (like `DATALOG/STR.EDF`) are included directly.
- `size` is current byte size on disk.
- Returns `[]` if the SD can't be mounted or the path doesn't exist.

**Errors:**
| Code | Meaning |
|------|---------|
| 400 | Bad path (`..` detected) |
| 500 | SD busy or mount failed — retry after 10s |

---

### `GET /file?path=DATALOG/20260328/STR.EDF&offset=4096`

Streams raw bytes from `offset` to EOF.

**Parameters:**
| Param | Default | Description |
|-------|---------|-------------|
| `path` | (required) | Relative path as returned by `/ls` |
| `offset` | `0` | Byte offset to start from |

**Response — 200 OK:**
- `Content-Type: application/octet-stream`
- Chunked transfer of raw bytes from offset to EOF
- Empty body (0 bytes) if offset equals file size

**Errors:**
| Code | Meaning |
|------|---------|
| 400 | Missing or invalid path |
| 404 | File not found |
| 500 | SD busy or mount failed — retry after 10s |

---

## hms-cpap Polling Loop

```python
import requests, time

FYSETC = "http://192.168.x.x"  # Set a DHCP reservation for the ESP32 MAC
POLL_INTERVAL = 65               # seconds between scans

# Persistent state: path → confirmed_offset
snapshot: dict[str, int] = {}

def poll():
    try:
        r = requests.get(f"{FYSETC}/ls", params={"path": "DATALOG"}, timeout=10)
        r.raise_for_status()
        files = r.json()  # [{"path": "...", "size": N}, ...]
    except Exception as e:
        print(f"ls failed: {e}")
        return

    for f in files:
        path = f["path"]
        current_size = f["size"]
        confirmed_offset = snapshot.get(path, 0)

        if current_size > confirmed_offset:
            fetch_delta(path, confirmed_offset, current_size)

def fetch_delta(path: str, offset: int, size: int):
    print(f"Fetching {path} [{offset}→{size}] ({size-offset} bytes)")
    try:
        r = requests.get(
            f"{FYSETC}/file",
            params={"path": path, "offset": offset},
            timeout=60,
            stream=True,
        )
        if r.status_code == 500:
            print(f"SD busy, will retry next poll")
            return
        r.raise_for_status()

        data = b""
        for chunk in r.iter_content(chunk_size=4096):
            data += chunk

        # TODO: persist data to your storage
        print(f"  Got {len(data)} bytes")

        # Update confirmed offset
        snapshot[path] = offset + len(data)

    except Exception as e:
        print(f"fetch failed: {e}")

# Main loop
while True:
    poll()
    time.sleep(POLL_INTERVAL)
```

---

## Device Discovery

The fysetc does **not** announce itself. hms-cpap must find it by:

1. **Fixed IP** — set a DHCP reservation for the ESP32 MAC in your router (recommended).
2. **Boot log** — the device prints its IP on the serial console at startup.
3. **Status endpoint** — `GET /api/status` once you know the IP.

---

## Status Endpoint

### `GET /api/status`

```json
{
  "state": "FILE_SERVER",
  "wifi": true,
  "mqtt": false,
  "uptime": "0h12m34s"
}
```

---

## SD Bus Safety

- SD is mounted **read-only** and **only while a request is being served**.
- Between requests the bus is released to the CPAP.
- If the CPAP is actively using the SD when a request arrives, the endpoint
  returns HTTP 500. hms-cpap should retry after 10 seconds.
- Never issue concurrent requests — wait for the previous one to finish.

---

## Path Format

- Always forward slashes: `DATALOG/20260328/BRP.EDF`
- Always relative (no leading `/`)
- Never contains `..`
- Matches long filenames exactly as stored on the FAT32 filesystem

---

## Quick Test

```bash
FYSETC=http://192.168.x.x

# List all files
curl "$FYSETC/ls?path=DATALOG" | python3 -m json.tool

# Fetch a file from byte 0
curl "$FYSETC/file?path=DATALOG/STR.EDF&offset=0" --output /tmp/STR.EDF
xxd /tmp/STR.EDF | head -4   # should start with "0       " (EDF header)

# Fetch only new bytes (e.g. file grew from 4096 to 6144)
curl "$FYSETC/file?path=DATALOG/STR.EDF&offset=4096" --output /tmp/STR_delta.bin

# Status
curl "$FYSETC/api/status"
```
