#ifndef HARDWARE_INFO_H
#define HARDWARE_INFO_H

#include <stdint.h>

#define MAX_CORES 128
#define BUFFER_SIZE 1024
#define UUID_LENGTH 37
#define SERIAL_LENGTH 65
#define MODEL_LENGTH 256
#define VENDOR_LENGTH 64

typedef enum {
    VIRT_NONE,
    VIRT_KVM,
    VIRT_QEMU,
    VIRT_VMWARE,
    VIRT_VIRTUALBOX,
    VIRT_XEN,
    VIRT_HYPERV,
    VIRT_DOCKER,
    VIRT_LXC,
    VIRT_OPENVZ,
    VIRT_PARALLELS,
    VIRT_CLOUD,
    VIRT_UNKNOWN
} VirtualizationType;

typedef struct {
    char system_uuid[UUID_LENGTH];
    char motherboard_serial[SERIAL_LENGTH];
    char product_name[MODEL_LENGTH];
    char bios_vendor[VENDOR_LENGTH];
    char bios_version[VENDOR_LENGTH];
    char cpu_model[MODEL_LENGTH];
    char cpu_vendor[VENDOR_LENGTH];
    uint32_t cpu_family;
    uint32_t cpu_stepping;
    uint64_t cpu_microcode;
    int is_arm;
    int is_virtual;
    VirtualizationType virt_type;
    char hypervisor_vendor[VENDOR_LENGTH];
} HardwareInfo;

typedef struct {
    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
    uint64_t total;
} CPUStats;

typedef struct {
    double usage;
    int temperature;
    CPUStats stats;
} CoreInfo;

typedef struct {
    HardwareInfo hw_info;
    CoreInfo cores[MAX_CORES];
    int num_cores;
    uint64_t total_memory;
    uint64_t free_memory;
    uint64_t available_memory;
    uint64_t cached_memory;
    uint64_t swap_total;
    uint64_t swap_free;
} SystemInfo;

void collect_hardware_info(HardwareInfo *info);
void collect_system_info(SystemInfo *info, SystemInfo *prev_info);
void output_json(const SystemInfo *info);

#endif
