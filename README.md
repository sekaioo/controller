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
