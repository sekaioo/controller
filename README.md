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

## `config.json` **Structure**

> **Note:** `template.json` and `config.json` are located in the `data/` directory  
> **Note:** Profile files are located in the `data/profiles/` directory

| Field                  | Type     | Description                        |
|------------------------|----------|------------------------------------|
| `lang`                 | `string` | Language code (files in `lang/`)   |
| `ua`                   | `string` | User-Agent for profile downloads   |
| `block_network`        | `bool`   | Block network when stopping kernel |
| `kernel`               | `object` | Object of kernel                   |
| `kernel[].path`        | `string` | Kernel path                        |
| `kernel[].command`     | `string` | Kernel command                     |
| `kernel[].config_path` | `string` | Kernel config file                 |
| `profiles`             | `array`  | Array of profile objects           |
| `profiles[].name`      | `string` | Display name for profile           |
| `profiles[].path`      | `string` | Local path to save profile         |
| `profiles[].url`       | `string` | Update profile URL                 |

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
  "profile": [
    {
      "name": "example",
      "path": "example.json",
      "url": "https://example.com"
    }
  ]
}
```

---

## NOTICE

### Third-Party Libraries

- **[rapidjson](https://rapidjson.org/)**  
  Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip. All rights reserved.
  Licensed under the [MIT License](https://github.com/Tencent/rapidjson/blob/master/license.txt)

