/* Zephyr port of the meson-generated phosphor-state-manager config.h.
 *
 * Mirrors the defaults in meson_options.txt. The only difference is that
 * every filesystem path is remapped from the Linux FHS layout to the
 * Zephyr filesystem layout:
 *   /run/openbmc/...                     -> /overlay/run/openbmc/...
 *   /var/lib/phosphor-state-manager/...  -> /overlay/var/lib/phosphor-state-manager/...
 *
 * The phosphor-state-manager sources include "config.h" unquoted-by-path,
 * so this directory is put on the global include path by zephyr/CMakeLists.txt
 * (same mechanism phosphor-bmc-code-mgt and dbus-sensors use).
 */

#ifndef PHOSPHOR_STATE_MANAGER_ZEPHYR_CONFIG_H
#define PHOSPHOR_STATE_MANAGER_ZEPHYR_CONFIG_H

/* ---- D-Bus names and object paths ---- */
#define HOST_BUSNAME "xyz.openbmc_project.State.Host"
#define HOST_OBJPATH "/xyz/openbmc_project/state/host"
#define HOST_SCHED_OBJPATH "/xyz/openbmc_project/scheduled/host"

#define HYPERVISOR_BUSNAME "xyz.openbmc_project.State.Hypervisor"
#define HYPERVISOR_OBJPATH "/xyz/openbmc_project/state/hypervisor"

#define CHASSIS_BUSNAME "xyz.openbmc_project.State.Chassis"
#define CHASSIS_OBJPATH "/xyz/openbmc_project/state/chassis"

#define BMC_BUSNAME "xyz.openbmc_project.State.BMC"
#define BMC_OBJPATH "/xyz/openbmc_project/state/bmc"

#define SCHEDULED_HOST_TRANSITION_BUSNAME \
    "xyz.openbmc_project.State.ScheduledHostTransition"

/* ---- persisted state (fmt::format style "{}" placeholders) ---- */
#define HOST_STATE_PERSIST_PATH \
    "/overlay/var/lib/phosphor-state-manager/host{}-PersistData"
#define POH_COUNTER_PERSIST_PATH \
    "/overlay/var/lib/phosphor-state-manager/chassis{}-POHCounter"
#define CHASSIS_STATE_CHANGE_PERSIST_PATH \
    "/overlay/var/lib/phosphor-state-manager/chassis{}-StateChangeTime"
#define SCHEDULED_HOST_TRANSITION_PERSIST_PATH \
    "/overlay/var/lib/phosphor-state-manager/scheduledHostTransition"

/* ---- feature knobs ---- */
#define BOOT_COUNT_MAX_ALLOWED 3
#define CLASS_VERSION 2

/* sysfs does not exist on Zephyr. The macros are kept so the sources that
 * reference them still compile; every read of them simply fails and the
 * code falls back to its default. */
#define SYSFS_SECURE_BOOT_PATH "/sys/kernel/debug/aspeed/sbc/secure_boot"
#define SYSFS_ABR_IMAGE_PATH "/sys/kernel/debug/aspeed/sbc/abr_image"
#define SYSFS_TPM_MEASUREMENT_PATH "/sys/class/tpm/tpm0/pcr-sha256/0"

/* ---- runtime state files (Linux keeps them in /run/openbmc) ----
 * These are used with std::snprintf()/fmt::sprintf() so they keep the
 * printf-style "%d" placeholder of the upstream definitions. */
#define BASE_FILE_DIR "/overlay/run/openbmc/"
#define CHASSIS_LOST_POWER_FILE "/overlay/run/openbmc/chassis@%d-lost-power"
#define HOST_RUNNING_FILE "/overlay/run/openbmc/host@%d-on"
#define CHASSIS_ON_FILE "/overlay/run/openbmc/chassis@%d-on"

#endif /* PHOSPHOR_STATE_MANAGER_ZEPHYR_CONFIG_H */
