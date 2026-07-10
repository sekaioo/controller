# Controller

A tray application supporting profile switching, profile updates, kernel start/stop control, and other
features, including the ability to add and manage a custom kernel.


---

## Build Instructions

### Prerequisites

- **Windows 10 / 11**
- **Visual Studio 2022** with:
    - Desktop development with C++
- **CMake** ≥ 3.30
- **vcpkg**

---

## config.json example

```json
{
  "lang": "en-US",
  "ua": "curl",
  "block_network": true,
  "log_level": "info",
  "kernel": {
    "path": "kernel/[kernel_file]",
    "command": "[kernel_command]",
    "config_path": "kernel/[kernel_config_file]"
  },
  "profiles": {
    "tag1": {
      "path": "tag1.json",
      "url": "https://example.com"
    },
    "tag2": {
      "path": "tag2.json",
      "url": "https://example.org"
    }
  }
}
```

### log_level (optional)

Controls the minimum severity written to `data/controller.log`
(rotated to `controller.log.old` past 512 KB). One of:

`all` | `info` | `warn` | `error` | `fatal` | `off`

Messages below the configured level are discarded. Defaults to `all`
(log everything) when the field is omitted; `off` disables logging.
