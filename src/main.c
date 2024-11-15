#include "hardware_info.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    SystemInfo info, prev_info;
    memset(&info, 0, sizeof(SystemInfo));
    memset(&prev_info, 0, sizeof(SystemInfo));
    collect_hardware_info(&info.hw_info);
    collect_system_info(&prev_info, NULL);
    sleep(1);
    collect_system_info(&info, &prev_info);
    output_json(&info);
    return 0;
}
