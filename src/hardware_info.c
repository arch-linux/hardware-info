#include "hardware_info.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <ctype.h>
#include <fcntl.h>


static void trim(char *str) {
    if (!str) return;
    
    char *end;
    while (isspace(*str)) str++;
    
    if (*str == 0) return;
    
    end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    *(end + 1) = '\0';
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


static void read_dmi_info(HardwareInfo *hw) {
    char buffer[BUFFER_SIZE];

    
    if (read_file_line("/sys/class/dmi/id/product_uuid", buffer, sizeof(buffer))) {
        strncpy(hw->system_uuid, buffer, UUID_LENGTH - 1);
    } else {
        strncpy(hw->system_uuid, "unknown", UUID_LENGTH - 1);
    }

    
    if (read_file_line("/sys/class/dmi/id/board_serial", buffer, sizeof(buffer))) {
        strncpy(hw->motherboard_serial, buffer, SERIAL_LENGTH - 1);
    } else {
        strncpy(hw->motherboard_serial, "unknown", SERIAL_LENGTH - 1);
    }

    
    if (read_file_line("/sys/class/dmi/id/product_name", buffer, sizeof(buffer))) {
        strncpy(hw->product_name, buffer, MODEL_LENGTH - 1);
    } else {
        strncpy(hw->product_name, "unknown", MODEL_LENGTH - 1);
    }

    
    if (read_file_line("/sys/class/dmi/id/bios_vendor", buffer, sizeof(buffer))) {
        strncpy(hw->bios_vendor, buffer, VENDOR_LENGTH - 1);
    } else {
        strncpy(hw->bios_vendor, "unknown", VENDOR_LENGTH - 1);
    }

    if (read_file_line("/sys/class/dmi/id/bios_version", buffer, sizeof(buffer))) {
        strncpy(hw->bios_version, buffer, VENDOR_LENGTH - 1);
    } else {
        strncpy(hw->bios_version, "unknown", VENDOR_LENGTH - 1);
    }
}


static void read_cpu_info(HardwareInfo *hw) {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return;

    char line[BUFFER_SIZE];
    char *value;

    
    strncpy(hw->cpu_model, "unknown", MODEL_LENGTH - 1);
    strncpy(hw->cpu_vendor, "unknown", VENDOR_LENGTH - 1);
    hw->cpu_family = 0;
    hw->cpu_stepping = 0;
    hw->cpu_microcode = 0;

    while (fgets(line, sizeof(line), fp)) {
        if ((value = strstr(line, ": "))) {
            value += 2;
            trim(value);

            if (strncmp(line, "model name", 10) == 0) {
                strncpy(hw->cpu_model, value, MODEL_LENGTH - 1);
            }
            else if (strncmp(line, "vendor_id", 9) == 0) {
                strncpy(hw->cpu_vendor, value, VENDOR_LENGTH - 1);
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


static int read_cpu_temp(int core) {
    char path[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];
    
    snprintf(path, sizeof(path), 
             "/sys/devices/platform/coretemp.0/hwmon/hwmon*/temp%d_input", 
             core + 1);

    if (read_file_line(path, buffer, sizeof(buffer))) {
        return atoi(buffer) / 1000; 
    }
    return 0;
}


static void calculate_cpu_usage(CoreInfo *prev, CoreInfo *curr) {
    uint64_t total_diff = curr->stats.total - prev->stats.total;
    uint64_t idle_diff = curr->stats.idle - prev->stats.idle;

    if (total_diff > 0) {
        curr->usage = 100.0 * (1.0 - ((double)idle_diff / total_diff));
    }
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

void collect_hardware_info(HardwareInfo *info) {
    read_dmi_info(info);
    read_cpu_info(info);
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
                        calculate_cpu_usage(&prev_info->cores[core], 
                                         &info->cores[core]);
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
    printf("    \"cpu\": {\n");
    printf("      \"model\": \"%s\",\n", info->hw_info.cpu_model);
    printf("      \"vendor\": \"%s\",\n", info->hw_info.cpu_vendor);
    printf("      \"family\": %u,\n", info->hw_info.cpu_family);
    printf("      \"stepping\": %u,\n", info->hw_info.cpu_stepping);
    printf("      \"microcode\": \"0x%lx\"\n", info->hw_info.cpu_microcode);
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
