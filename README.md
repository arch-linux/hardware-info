
# Hardware Info

**A lightweight C utility for collecting and reporting detailed system hardware information, CPU usage, and memory statistics in Linux systems.**  
The tool outputs data in JSON format, making it ideal for integration with other applications.

---

## Features

### Hardware Identification
- **System Information**: UUID, motherboard serial number, and product name.
- **CPU Details**: Model, vendor, family, stepping, and microcode version.
- **BIOS Details**: Vendor, version, and related product information.

### CPU Statistics
- **Per-core Usage**: CPU usage statistics for each core.
- **Core Temperatures**: Where available, temperature readings per core.
- **General Details**: CPU family, stepping information, and microcode version.

### Memory Information
- **Total and Available Memory**: RAM details including cache usage.
- **Swap Space Statistics**: Total and free swap memory.

---

## Prerequisites

Install the necessary build tools based on your Linux distribution:

```bash
# Ubuntu/Debian
sudo apt-get install build-essential

# Fedora
sudo dnf install gcc make

# Arch Linux
sudo pacman -S base-devel
```

---

## Building

Clone the repository and build the project:

```bash
# Clone the repository
git clone https://github.com/arch-linux/hardware-info.git
cd hardware-info

# Build the project
make

# Install (optional)
sudo make install
```

---

## Usage

Run the utility:

```bash
hardware-info
```

### Example Output

```json
{
  "hardware": {
    "system_uuid": "12345678-1234-5678-1234-567812345678",
    "motherboard_serial": "MB12345678",
    "product_name": "MyComputer Model X",
    "cpu": {
      "model": "Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz",
      "vendor": "GenuineIntel",
      "family": 6,
      "stepping": 13,
      "microcode": "0xca"
    },
    "bios": {
      "vendor": "American Megatrends Inc.",
      "version": "2.17"
    }
  },
  "cpu_usage": {
    "cores": 8,
    "total_usage": 25.60,
    "core_info": [
      {
        "core": 0,
        "usage": 32.50,
        "temperature": 45
      }
    ]
  },
  "memory": {
    "total": 16777216000,
    "free": 8388608000,
    "available": 12582912000,
    "cached": 4194304000,
    "swap_total": 4294967296,
    "swap_free": 4294967296
  }
}
```

---

## Integration

The JSON output format makes it easy to integrate with other applications. Example using PHP:

```php
$process = new Process(['hardware-info']);
$process->run();

if ($process->isSuccessful()) {
    $data = json_decode($process->getOutput(), true);
    // Process the data
}
```

---

## Building from Source

The project uses a standard Makefile build system:

```bash
make           # Build the project
make clean     # Clean build artifacts
make install   # Install to system (requires root)
make uninstall # Remove from system (requires root)
```

---

## Permissions

Some hardware information requires root privileges to access. Run with `sudo` if you need complete information:

```bash
sudo hardware-info
```

---

## Contributing

1. Fork the repository.
2. Create your feature branch (`git checkout -b feature/amazing-feature`).
3. Commit your changes (`git commit -m 'Add some amazing feature'`).
4. Push to the branch (`git push origin feature/amazing-feature`).
5. Open a Pull Request.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

## Authors

- **Chrisotpher Allen** - *archlinuxusa@gmail.com*

---

## Acknowledgments

- Peanut Butter the Corgi
