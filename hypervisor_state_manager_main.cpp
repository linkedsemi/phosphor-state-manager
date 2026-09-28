#include "config.h"

#include "hypervisor_state_manager.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/manager.hpp>

#include <cstdlib>

#ifdef __ZEPHYR__
#include <systemd/sd-bus.h>

#include <dbus_broker.h>
#include <phosphor-logging/lg2.hpp>
#include <zephyr/kernel.h>

/* Defined in app/src/task_def.cpp. */
extern struct k_sem hypervisor_state_ready_sem;

int hypervisor_state_main()
{
    sd_bus* sdbus = nullptr;
    int rc = connect_to_dbroker(&sdbus);
    if (rc < 0)
    {
        lg2::error("Failed to connect to dbroker: {RC}", "RC", rc);
        return rc;
    }
    sdbusplus::bus::bus bus(sdbus, std::false_type());

    // For now, we only have one instance of the hypervisor
    auto objPathInst = std::string{HYPERVISOR_OBJPATH} + '0';

    // Add sdbusplus ObjectManager.
    sdbusplus::server::manager_t objManager(bus, objPathInst.c_str());

    phosphor::state::manager::Hypervisor manager(bus, objPathInst.c_str());

    bus.request_name(HYPERVISOR_BUSNAME);

    k_sem_give(&hypervisor_state_ready_sem);

    bus.process_loop();

    return 0;
}
#else
int main()
{
    auto bus = sdbusplus::bus::new_default();

    // For now, we only have one instance of the hypervisor
    auto objPathInst = std::string{HYPERVISOR_OBJPATH} + '0';

    // Add sdbusplus ObjectManager.
    sdbusplus::server::manager_t objManager(bus, objPathInst.c_str());

    phosphor::state::manager::Hypervisor manager(bus, objPathInst.c_str());

    bus.request_name(HYPERVISOR_BUSNAME);

    while (true)
    {
        bus.process_discard();
        bus.wait();
    }
    return 0;
}
#endif
