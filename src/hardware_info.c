#include "hardware_info.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/utsname.h>

#define RASPBERRY_PI_MODEL "/sys/firmware/devicetree/base/model"
#define THERMAL_ZONE "/sys/class/thermal/thermal_zone0/temp"
#define CPUINFO "/proc/cpuinfo"
#define SYSFS_DMI "/sys/class/dmi/id"
#define DOCKER_CHECK "/proc/1/cgroup"
#define OPENVZ_CHECK "/proc/vz"
#define LXC_CHECK "/proc/1/environ"

static void trim(char *str) {
    if (!str) return;
    char *end;
    while (isspace(*str)) str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    *(end + 1) = '\0';
}

static void safe_strcpy(char *dest, const char *src, size_t size) {
    if (size > 0) {
        size_t i;
        for (i = 0; i < size - 1 && src[i] != '\0'; i++) {
            dest[i] = src[i];
        }
        dest[i] = '\0';
    }
}

static int read_file_line(const char *filepath, char *buffer, size_t size) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) return 0;
    char *result = fgets(buffer, size, fp);
    fclose(fp);
    if (result) {
        trim(buffer);
        return 1;
    }
    return 0;
}

static int file_exists(const char *filepath) {
    return access(filepath, F_OK) == 0;
}

static VirtualizationType detect_virtualization() {
    char buffer[BUFFER_SIZE];
    FILE *fp;

    // Check systemd-detect-virt if available
    fp = popen("systemd-detect-virt 2>/dev/null", "r");
    if (fp) {
        if (fgets(buffer, sizeof(buffer), fp)) {
            trim(buffer);
            pclose(fp);
            
            if (strcmp(buffer, "kvm") == 0) return VIRT_KVM;
            if (strcmp(buffer, "qemu") == 0) return VIRT_QEMU;
            if (strcmp(buffer, "vmware") == 0) return VIRT_VMWARE;
            if (strcmp(buffer, "virtualbox") == 0) return VIRT_VIRTUALBOX;
            if (strcmp(buffer, "xen") == 0) return VIRT_XEN;
            if (strcmp(buffer, "microsoft") == 0) return VIRT_HYPERV;
            if (strcmp(buffer, "docker") == 0) return VIRT_DOCKER;
            if (strcmp(buffer, "lxc") == 0) return VIRT_LXC;
        }
        pclose(fp);
    }

    // Check /proc/cpuinfo
    if (read_file_line(CPUINFO, buffer, sizeof(buffer))) {
        if (strstr(buffer, "QEMU Virtual CPU")) return VIRT_QEMU;
        if (strstr(buffer, "VMware")) return VIRT_VMWARE;
        if (strstr(buffer, "VirtualBox")) return VIRT_VIRTUALBOX;
        if (strstr(buffer, "Xen")) return VIRT_XEN;
    }

    // Check DMI for hypervisor
    if (read_file_line("/sys/class/dmi/id/sys_vendor", buffer, sizeof(buffer))) {
        if (strstr(buffer, "VMware")) return VIRT_VMWARE;
        if (strstr(buffer, "VirtualBox")) return VIRT_VIRTUALBOX;
        if (strstr(buffer, "Xen")) return VIRT_XEN;
        if (strstr(buffer, "Microsoft Corporation")) return VIRT_HYPERV;
        if (strstr(buffer, "QEMU")) return VIRT_QEMU;
        if (strstr(buffer, "Amazon EC2")) return VIRT_CLOUD;
        if (strstr(buffer, "Google")) return VIRT_CLOUD;
        if (strstr(buffer, "Azure")) return VIRT_CLOUD;
    }

    // Check for containers
    if (file_exists(DOCKER_CHECK)) {
        fp = fopen(DOCKER_CHECK, "r");
        if (fp) {
            while (fgets(buffer, sizeof(buffer), fp)) {
                if (strstr(buffer, "docker")) {
                    fclose(fp);
                    return VIRT_DOCKER;
                }
            }
            fclose(fp);
        }
    }

    if (file_exists(OPENVZ_CHECK)) return VIRT_OPENVZ;

    if (file_exists(LXC_CHECK)) {
        fp = fopen(LXC_CHECK, "r");
        if (fp) {
            if (fgets(buffer, sizeof(buffer), fp)) {
                if (strstr(buffer, "container=lxc")) {
                    fclose(fp);
                    return VIRT_LXC;
                }
            }
            fclose(fp);
        }
    }

    // Final check for any virtualization
    fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            if (strstr(buffer, "hypervisor")) {
                fclose(fp);
                return VIRT_UNKNOWN;
            }
        }
        fclose(fp);
    }

    return VIRT_NONE;
}

static void get_vm_uuid(HardwareInfo *hw) {
    char buffer[BUFFER_SIZE];
    const char *uuid_paths[] = {
        "/sys/class/dmi/id/product_uuid",
        "/sys/devices/virtual/dmi/id/product_uuid",
        "/etc/machine-id",
        "/var/lib/dbus/machine-id",
        NULL
    };

    for (int i = 0; uuid_paths[i] != NULL; i++) {
        if (read_file_line(uuid_paths[i], buffer, sizeof(buffer))) {
            safe_strcpy(hw->system_uuid, buffer, UUID_LENGTH);
            return;
        }
    }

    switch (hw->virt_type) {
        case VIRT_DOCKER:
            if (read_file_line("/proc/self/cgroup", buffer, sizeof(buffer))) {
                char *id = strrchr(buffer, '/');
                if (id) safe_strcpy(hw->system_uuid, id + 1, UUID_LENGTH);
            }
            break;

        case VIRT_LXC:
            if (read_file_line("/proc/self/environ", buffer, sizeof(buffer))) {
                char *id = strstr(buffer, "container_uuid=");
                if (id) safe_strcpy(hw->system_uuid, id + 15, UUID_LENGTH);
            }
            break;

        default:
            FILE *fp = fopen("/proc/cpuinfo", "r");
            if (fp) {
                unsigned long hash = 5381;
                int c;
                while ((c = fgetc(fp)) != EOF) {
                    hash = ((hash << 5) + hash) + c;
                }
                fclose(fp);
                snprintf(buffer, sizeof(buffer), "vm-%lx", hash);
                safe_strcpy(hw->system_uuid, buffer, UUID_LENGTH);
            }
            break;
    }
}

static void get_vm_info(HardwareInfo *hw) {
    char buffer[BUFFER_SIZE];
    
    hw->is_virtual = 1;
    get_vm_uuid(hw);

    switch (hw->virt_type) {
        case VIRT_KVM:
            safe_strcpy(hw->hypervisor_vendor, "KVM", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "KVM Virtual Machine", MODEL_LENGTH);
            break;

        case VIRT_QEMU:
            safe_strcpy(hw->hypervisor_vendor, "QEMU", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "QEMU Virtual Machine", MODEL_LENGTH);
            break;

        case VIRT_VMWARE:
            safe_strcpy(hw->hypervisor_vendor, "VMware", VENDOR_LENGTH);
            if (read_file_line("/sys/class/dmi/id/product_name", buffer, sizeof(buffer))) {
                safe_strcpy(hw->product_name, buffer, MODEL_LENGTH);
            } else {
                safe_strcpy(hw->product_name, "VMware Virtual Machine", MODEL_LENGTH);
            }
            break;

        case VIRT_VIRTUALBOX:
            safe_strcpy(hw->hypervisor_vendor, "VirtualBox", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "VirtualBox Virtual Machine", MODEL_LENGTH);
            break;

        case VIRT_XEN:
            safe_strcpy(hw->hypervisor_vendor, "Xen", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "Xen Virtual Machine", MODEL_LENGTH);
            break;

        case VIRT_HYPERV:
            safe_strcpy(hw->hypervisor_vendor, "Microsoft Hyper-V", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "Hyper-V Virtual Machine", MODEL_LENGTH);
            break;

        case VIRT_DOCKER:
            safe_strcpy(hw->hypervisor_vendor, "Docker", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "Docker Container", MODEL_LENGTH);
            break;

        case VIRT_LXC:
            safe_strcpy(hw->hypervisor_vendor, "LXC", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "LXC Container", MODEL_LENGTH);
            break;

        case VIRT_OPENVZ:
            safe_strcpy(hw->hypervisor_vendor, "OpenVZ", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "OpenVZ Container", MODEL_LENGTH);
            break;

        case VIRT_CLOUD:
            if (read_file_line("/sys/class/dmi/id/sys_vendor", buffer, sizeof(buffer))) {
                safe_strcpy(hw->hypervisor_vendor, buffer, VENDOR_LENGTH);
                if (read_file_line("/sys/class/dmi/id/product_name", buffer, sizeof(buffer))) {
                    safe_strcpy(hw->product_name, buffer, MODEL_LENGTH);
                } else {
                    safe_strcpy(hw->product_name, "Cloud Instance", MODEL_LENGTH);
                }
            }
            break;

        case VIRT_UNKNOWN:
            safe_strcpy(hw->hypervisor_vendor, "Unknown", VENDOR_LENGTH);
            safe_strcpy(hw->product_name, "Virtual Machine", MODEL_LENGTH);
            break;

        default:
            hw->is_virtual = 0;
            break;
    }

    if (hw->is_virtual) {
        safe_strcpy(hw->motherboard_serial, "Virtual Environment", SERIAL_LENGTH);
        
        if (read_file_line("/sys/class/dmi/id/bios_vendor", buffer, sizeof(buffer))) {
            safe_strcpy(hw->bios_vendor, buffer, VENDOR_LENGTH);
        }
        if (read_file_line("/sys/class/dmi/id/bios_version", buffer, sizeof(buffer))) {
            safe_strcpy(hw->bios_version, buffer, VENDOR_LENGTH);
        }
    }
}
static int is_raspberry_pi() {
    return file_exists(RASPBERRY_PI_MODEL);
}

static void read_raspberry_pi_info(HardwareInfo *hw) {
    char buffer[BUFFER_SIZE];
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            char *value;
            if ((value = strstr(buffer, ": "))) {
                value += 2;
                trim(value);
                if (strncmp(buffer, "Hardware", 8) == 0) {
                    safe_strcpy(hw->cpu_model, value, MODEL_LENGTH);
                }
                else if (strncmp(buffer, "Revision", 8) == 0) {
                    safe_strcpy(hw->motherboard_serial, value, SERIAL_LENGTH);
                }
                else if (strncmp(buffer, "Serial", 6) == 0) {
                    safe_strcpy(hw->system_uuid, value, UUID_LENGTH);
                }
                else if (strncmp(buffer, "model name", 10) == 0) {
                    safe_strcpy(hw->cpu_model, value, MODEL_LENGTH);
                }
            }
        }
        fclose(fp);
    }

    if (read_file_line(RASPBERRY_PI_MODEL, buffer, sizeof(buffer))) {
        safe_strcpy(hw->product_name, buffer, MODEL_LENGTH);
    }
    
    safe_strcpy(hw->cpu_vendor, "ARM", VENDOR_LENGTH);
    hw->is_arm = 1;
    hw->is_virtual = 0;
    hw->virt_type = VIRT_NONE;
}

static void read_cpu_info(HardwareInfo *hw) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return;

    char line[BUFFER_SIZE];
    char *value;

    while (fgets(line, sizeof(line), fp)) {
        if ((value = strstr(line, ": "))) {
            value += 2;
            trim(value);
            if (strncmp(line, "model name", 10) == 0) {
                safe_strcpy(hw->cpu_model, value, MODEL_LENGTH);
            }
            else if (strncmp(line, "vendor_id", 9) == 0) {
                safe_strcpy(hw->cpu_vendor, value, VENDOR_LENGTH);
            }
            else if (strncmp(line, "cpu family", 10) == 0) {
                hw->cpu_family = atoi(value);
            }
            else if (strncmp(line, "stepping", 8) == 0) {
                hw->cpu_stepping = atoi(value);
            }
            else if (strncmp(line, "microcode", 9) == 0) {
                hw->cpu_microcode = strtoull(value, NULL, 16);
            }
        }
    }
    fclose(fp);
}

static void read_physical_info(HardwareInfo *hw) {
    char buffer[BUFFER_SIZE];

    if (read_file_line("/sys/class/dmi/id/product_uuid", buffer, sizeof(buffer))) {
        safe_strcpy(hw->system_uuid, buffer, UUID_LENGTH);
    }
    if (read_file_line("/sys/class/dmi/id/board_serial", buffer, sizeof(buffer))) {
        safe_strcpy(hw->motherboard_serial, buffer, SERIAL_LENGTH);
    }
    if (read_file_line("/sys/class/dmi/id/product_name", buffer, sizeof(buffer))) {
        safe_strcpy(hw->product_name, buffer, MODEL_LENGTH);
    }
    if (read_file_line("/sys/class/dmi/id/bios_vendor", buffer, sizeof(buffer))) {
        safe_strcpy(hw->bios_vendor, buffer, VENDOR_LENGTH);
    }
    if (read_file_line("/sys/class/dmi/id/bios_version", buffer, sizeof(buffer))) {
        safe_strcpy(hw->bios_version, buffer, VENDOR_LENGTH);
    }
}

static int read_cpu_temp(int core) {
    char buffer[BUFFER_SIZE];
    
    if (read_file_line(THERMAL_ZONE, buffer, sizeof(buffer))) {
        return atoi(buffer) / 1000;
    }

    char path[BUFFER_SIZE];
    snprintf(path, sizeof(path), "/sys/devices/platform/coretemp.0/hwmon/hwmon*/temp%d_input", core + 1);
    if (read_file_line(path, buffer, sizeof(buffer))) {
        return atoi(buffer) / 1000;
    }

    return 0;
}

static void read_cpu_stats(CPUStats *stats, const char *line) {
    uint64_t guest, guest_nice;
    sscanf(line, "cpu%*d %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
           &stats->user, &stats->nice, &stats->system, &stats->idle,
           &stats->iowait, &stats->irq, &stats->softirq, &stats->steal,
           &guest, &guest_nice);
    stats->total = stats->user + stats->nice + stats->system + stats->idle +
                  stats->iowait + stats->irq + stats->softirq + stats->steal;
}

static void calculate_cpu_usage(CoreInfo *prev, CoreInfo *curr) {
    uint64_t total_diff = curr->stats.total - prev->stats.total;
    uint64_t idle_diff = curr->stats.idle - prev->stats.idle;
    if (total_diff > 0) {
        curr->usage = 100.0 * (1.0 - ((double)idle_diff / total_diff));
    }
}

void collect_hardware_info(HardwareInfo *info) {
    memset(info, 0, sizeof(HardwareInfo));
    
    info->virt_type = detect_virtualization();
    
    if (is_raspberry_pi()) {
        read_raspberry_pi_info(info);
    } else if (info->virt_type != VIRT_NONE) {
        get_vm_info(info);
        read_cpu_info(info);
    } else {
        info->is_virtual = 0;
        read_physical_info(info);
        read_cpu_info(info);
    }
}

void collect_system_info(SystemInfo *info, SystemInfo *prev_info) {
    FILE *fp;
    char line[BUFFER_SIZE];
    struct sysinfo si;

    fp = fopen("/proc/stat", "r");
    if (fp) {
        int core = -1;
        while (fgets(line, sizeof(line), fp) && core < MAX_CORES) {
            if (strncmp(line, "cpu", 3) == 0) {
                if (core >= 0) {
                    read_cpu_stats(&info->cores[core].stats, line);
                    info->cores[core].temperature = read_cpu_temp(core);
                    if (prev_info) {
                        calculate_cpu_usage(&prev_info->cores[core], &info->cores[core]);
                    }
                }
                core++;
            } else {
                break;
            }
        }
        info->num_cores = core;
        fclose(fp);
    }

    if (sysinfo(&si) == 0) {
        info->total_memory = si.totalram * si.mem_unit;
        info->free_memory = si.freeram * si.mem_unit;
        info->swap_total = si.totalswap * si.mem_unit;
        info->swap_free = si.freeswap * si.mem_unit;
    }

    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "Cached:", 7) == 0) {
                uint64_t cached;
                sscanf(line, "Cached: %lu", &cached);
                info->cached_memory = cached * 1024;
                break;
            }
        }
        fclose(fp);
    }

    info->available_memory = info->free_memory + info->cached_memory;
}

void output_json(const SystemInfo *info) {
    printf("{\n");
    
    printf("  \"hardware\": {\n");
    printf("    \"system_uuid\": \"%s\",\n", info->hw_info.system_uuid);
    printf("    \"motherboard_serial\": \"%s\",\n", info->hw_info.motherboard_serial);
    printf("    \"product_name\": \"%s\",\n", info->hw_info.product_name);
    
    printf("    \"virtualization\": {\n");
    printf("      \"is_virtual\": %s,\n", info->hw_info.is_virtual ? "true" : "false");
    if (info->hw_info.is_virtual) {
        printf("      \"type\": \"%s\",\n", info->hw_info.hypervisor_vendor);
        const char *virt_types[] = {
            "none", "kvm", "qemu", "vmware", "virtualbox", "xen",
            "hyper-v", "docker", "lxc", "openvz", "parallels", "cloud", "unknown"
        };
        printf("      \"hypervisor\": \"%s\"\n", virt_types[info->hw_info.virt_type]);
    } else {
        printf("      \"type\": null,\n");
        printf("      \"hypervisor\": null\n");
    }
    printf("    },\n");
    
    printf("    \"cpu\": {\n");
    printf("      \"model\": \"%s\",\n", info->hw_info.cpu_model);
    printf("      \"vendor\": \"%s\",\n", info->hw_info.cpu_vendor);
    printf("      \"family\": %u,\n", info->hw_info.cpu_family);
    printf("      \"stepping\": %u,\n", info->hw_info.cpu_stepping);
    printf("      \"microcode\": \"0x%lx\",\n", info->hw_info.cpu_microcode);
    printf("      \"architecture\": \"%s\"\n", info->hw_info.is_arm ? "ARM" : "x86");
    printf("    },\n");
    
    printf("    \"bios\": {\n");
    printf("      \"vendor\": \"%s\",\n", info->hw_info.bios_vendor);
    printf("      \"version\": \"%s\"\n", info->hw_info.bios_version);
    printf("    }\n");
    printf("  },\n");
    
    printf("  \"cpu_usage\": {\n");
    printf("    \"cores\": %d,\n", info->num_cores - 1);
    printf("    \"total_usage\": %.2f,\n", info->cores[0].usage);
    printf("    \"core_info\": [\n");
    
    for (int i = 1; i < info->num_cores; i++) {
        printf("      {\n");
        printf("        \"core\": %d,\n", i - 1);
        printf("        \"usage\": %.2f,\n", info->cores[i].usage);
        printf("        \"temperature\": %d\n", info->cores[i].temperature);
        printf("      }%s\n", i < info->num_cores - 1 ? "," : "");
    }
    printf("    ]\n");
    printf("  },\n");
    
    printf("  \"memory\": {\n");
    printf("    \"total\": %lu,\n", info->total_memory);
    printf("    \"free\": %lu,\n", info->free_memory);
    printf("    \"available\": %lu,\n", info->available_memory);
    printf("    \"cached\": %lu,\n", info->cached_memory);
    printf("    \"swap_total\": %lu,\n", info->swap_total);
    printf("    \"swap_free\": %lu\n", info->swap_free);
    printf("  }\n");
    printf("}\n");
}
