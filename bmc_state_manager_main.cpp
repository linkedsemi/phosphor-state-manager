#include "config.h"

#include "bmc_state_manager.hpp"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/manager.hpp>

#ifdef __ZEPHYR__
#include <systemd/sd-bus.h>

#include <dbus_broker.h>
#include <phosphor-logging/lg2.hpp>
#include <thread_dependency_mgr.hpp>
#include <zephyr/kernel.h>

/* Defined in app/src/task_def.cpp; given once this daemon owns its bus name
 * so the thread dependency manager can release the dependent threads. */
extern struct k_sem bmc_state_ready_sem;

/* Upstream flips CurrentBMCState to Ready when multi-user.target completes,
 * i.e. when every service is up. The Zephyr equivalent is
 * start_all_auto_threads() returning (all auto-start threads READY).
 *
 * This daemon is only the 12th of 22 threads, so without this wait the BMC
 * would claim to be Ready ~30 s before entity-manager, the sensors and the
 * KCS bridge even exist on the bus.
 *
 * Fail-safe timeout: if some thread hangs or registers no ready semaphore,
 * we must not sit in NotReady forever -- bmcweb/Redfish and IPMI consumers
 * would never consider the BMC usable. */
constexpr int bmcReadyTimeoutSec = 180;

int bmc_state_main()
{
    /* On Zephyr the bus is the in-process dbus-broker, reached through an
     * AF_UNIX socketpair instead of the Linux system bus socket that
     * sdbusplus::bus::new_default() would try to open. */
    sd_bus* sdbus = nullptr;
    int rc = connect_to_dbroker(&sdbus);
    if (rc < 0)
    {
        lg2::error("Failed to connect to dbroker: {RC}", "RC", rc);
        return rc;
    }

    /* std::false_type == take ownership of the sd_bus pointer. */
    sdbusplus::bus::bus bus(sdbus, std::false_type());

    // For now, we only have one instance of the BMC
    // 0 is for the current instance
    auto objPathInst = std::string(BMC_OBJPATH) + '0';

    // Add sdbusplus ObjectManager.
    sdbusplus::server::manager_t objManager(bus, objPathInst.c_str());

    /* The constructor discovers the initial state; on Zephyr that is
     * NotReady (see BMC::discoverInitialState()). */
    phosphor::state::manager::BMC manager(bus, objPathInst.c_str());

    bus.request_name(BMC_BUSNAME);

    /* Unblocks the thread dependency manager so it can go on starting the
     * remaining threads. MUST happen before waiting on
     * all_threads_ready_sem, otherwise the two wait on each other. */
    k_sem_give(&bmc_state_ready_sem);

    if (k_sem_take(&all_threads_ready_sem, K_SECONDS(bmcReadyTimeoutSec)) != 0)
    {
        lg2::warning(
            "Timed out after {SECS}s waiting for all BMC threads; forcing "
            "BMCState=Ready",
            "SECS", bmcReadyTimeoutSec);
    }
    else
    {
        lg2::info("All BMC threads ready, switching BMCState to Ready");
    }

    manager.currentBMCState(
        phosphor::state::manager::BMC::BMCState::Ready);

    // Wait for client request
    bus.process_loop();

    return 0;
}
#else
int main()
{
    auto bus = sdbusplus::bus::new_default();

    // For now, we only have one instance of the BMC
    // 0 is for the current instance
    auto objPathInst = std::string(BMC_OBJPATH) + '0';

    // Add sdbusplus ObjectManager.
    sdbusplus::server::manager_t objManager(bus, objPathInst.c_str());

    phosphor::state::manager::BMC manager(bus, objPathInst.c_str());

    bus.request_name(BMC_BUSNAME);

    while (true)
    {
        bus.process_discard();
        bus.wait();
    }

    exit(EXIT_SUCCESS);
}
#endif
