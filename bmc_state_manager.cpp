#include "bmc_state_manager.hpp"

#include "utils.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#ifndef __ZEPHYR__
#include <gpiod.h>
#include <sys/sysinfo.h>
#else
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

/* <linux/watchdog.h> bootstatus bits; Zephyr has no such header but the
 * reboot-cause decoder below still switches on them. */
#ifndef WDIOF_EXTERN1
#define WDIOF_EXTERN1 0x0004 /* External relay 1 */
#endif
#ifndef WDIOF_CARDRESET
#define WDIOF_CARDRESET 0x0020 /* Card previously reset the CPU */
#endif
#endif

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/exception.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace phosphor
{
namespace state
{
namespace manager
{

PHOSPHOR_LOG2_USING;

// When you see server:: you know we're referencing our base class
namespace server = sdbusplus::xyz::openbmc_project::State::server;

using namespace phosphor::logging;
using sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

constexpr auto obmcQuiesceTarget = "obmc-bmc-service-quiesce@0.target";
constexpr auto obmcStandbyTarget = "multi-user.target";
constexpr auto signalDone = "done";
constexpr auto activeState = "active";

/* Map a transition to it's systemd target */
const std::map<server::BMC::Transition, const char*> SYSTEMD_TABLE = {
    {server::BMC::Transition::Reboot, "reboot.target"}};

constexpr auto SYSTEMD_SERVICE = "org.freedesktop.systemd1";
constexpr auto SYSTEMD_OBJ_PATH = "/org/freedesktop/systemd1";
constexpr auto SYSTEMD_INTERFACE = "org.freedesktop.systemd1.Manager";
constexpr auto SYSTEMD_PRP_INTERFACE = "org.freedesktop.DBus.Properties";

std::string BMC::getUnitState(const std::string& unitToCheck)
{
    std::variant<std::string> currentState;
    sdbusplus::message::object_path unitTargetPath;

    auto method = this->bus.new_method_call(SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
                                            SYSTEMD_INTERFACE, "GetUnit");

    method.append(unitToCheck);

    try
    {
        auto result = this->bus.call(method);
        result.read(unitTargetPath);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // Not all input units will have been loaded yet so just return an
        // empty string if an exception is caught in this path
        info("Unit {UNIT} not found: {ERROR}", "UNIT", unitToCheck, "ERROR", e);
        return std::string{};
    }

    method = this->bus.new_method_call(
        SYSTEMD_SERVICE,
        static_cast<const std::string&>(unitTargetPath).c_str(),
        SYSTEMD_PRP_INTERFACE, "Get");

    method.append("org.freedesktop.systemd1.Unit", "ActiveState");

    try
    {
        auto result = this->bus.call(method);

        // Is input target active or inactive?
        result.read(currentState);
    }
    catch (const sdbusplus::exception_t& e)
    {
        info("Error in ActiveState Get: {ERROR}", "ERROR", e);
        return std::string{};
    }
    return (std::get<std::string>(currentState));
}

void BMC::discoverInitialState()
{
#ifdef __ZEPHYR__
    /* No systemd targets to inspect -- and asking for them would block for
     * the sdbusplus default reply timeout because there is no
     * org.freedesktop.systemd1 service on the bus.
     *
     * Upstream only reports Ready once multi-user.target completed, i.e.
     * after every service is up. The Zephyr counterpart of that moment is
     * start_all_auto_threads() returning, which bmc_state_main() waits for
     * after this constructor. Until then report NotReady so clients polling
     * Redfish /ipmi do not see a usable BMC while the sensors, bmcweb, ...
     * are still coming up. */
    info("Setting the BMCState field to BMC_NOTREADY (startup not finished)");
    this->currentBMCState(BMCState::NotReady);
    return;
#else
    // First look to see if the BMC quiesce target is active
    auto currentStateStr = getUnitState(obmcQuiesceTarget);
    if (currentStateStr == activeState)
    {
        info("Setting the BMCState field to BMC_QUIESCED");
        this->currentBMCState(BMCState::Quiesced);
        return;
    }

    // If not quiesced, then check standby target
    currentStateStr = getUnitState(obmcStandbyTarget);
    if (currentStateStr == activeState)
    {
        info("Setting the BMCState field to BMC_READY");
        this->currentBMCState(BMCState::Ready);
    }
    else
    {
        info("Setting the BMCState field to BMC_NOTREADY");
        this->currentBMCState(BMCState::NotReady);
    }

    return;
#endif
}

void BMC::executeTransition(const Transition tranReq)
{
#ifdef __ZEPHYR__
    /* No systemd on Zephyr: both Reboot and HardReboot mean "reset the BMC
     * now". sys_reboot(SYS_REBOOT_COLD) never returns -- the SoC's
     * sys_arch_reboot() hands the QSPI read window back to bank 0 from RAM
     * *before* anything can trigger the reset, which is what keeps a
     * reboot from slot B bootable (see soc_reboot.c). This is the exact
     * path phosphor-bmc-code-mgt's Activation::rebootBmc() already uses,
     * so A/B slot switching and this path stay consistent. */
    info("RequestedBMCTransition received, rebooting BMC now");

    // Put BMC state to NotReady when issuing a BMC reboot and stop
    // monitoring for state changes.
    this->currentBMCState(BMCState::NotReady);
    this->stateSignal.reset();

    // Give the D-Bus reply for the property change, the log lines above and
    // the in-flight socket traffic a moment to drain, otherwise the console
    // line is cut in half by the reset (same trick as code-mgt).
    k_sleep(K_MSEC(500));

    sys_reboot(SYS_REBOOT_COLD);

    /* Not reached. */
    return;
#else
    // HardReboot does not shutdown any services and immediately transitions
    // into the reboot process
    if (server::BMC::Transition::HardReboot == tranReq)
    {
        // Put BMC state not NotReady when issuing a BMC reboot
        // and stop monitoring for state changes
        this->currentBMCState(BMCState::NotReady);
        this->stateSignal.reset();

        auto method = this->bus.new_method_call(
            SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH, SYSTEMD_INTERFACE, "Reboot");
        try
        {
            this->bus.call(method);
        }
        catch (const sdbusplus::exception_t& e)
        {
            info("Error in HardReboot: {ERROR}", "ERROR", e);
        }
    }
    else
    {
        // Check to make sure it can be found
        auto iter = SYSTEMD_TABLE.find(tranReq);
        if (iter == SYSTEMD_TABLE.end())
            return;

        const auto& sysdUnit = iter->second;

        auto method = this->bus.new_method_call(
            SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH, SYSTEMD_INTERFACE, "StartUnit");
        // The only valid transition is reboot and that
        // needs to be irreversible once started

        method.append(sysdUnit, "replace-irreversibly");

        // Put BMC state not NotReady when issuing a BMC reboot
        // and stop monitoring for state changes
        this->currentBMCState(BMCState::NotReady);
        this->stateSignal.reset();

        try
        {
            this->bus.call(method);
        }
        catch (const sdbusplus::exception_t& e)
        {
            info("Error in StartUnit - replace-irreversibly: {ERROR}", "ERROR",
                 e);
        }
    }
    return;
#endif
}

int BMC::bmcStateChange(sdbusplus::message_t& msg)
{
    uint32_t newStateID{};
    sdbusplus::message::object_path newStateObjPath;
    std::string newStateUnit{};
    std::string newStateResult{};

    // Read the msg and populate each variable
    msg.read(newStateID, newStateObjPath, newStateUnit, newStateResult);

    if ((newStateUnit == obmcQuiesceTarget) && (newStateResult == signalDone))
    {
        error("BMC has entered BMC_QUIESCED state");
        this->currentBMCState(BMCState::Quiesced);

        // There is no getting out of Quiesced once entered (other then BMC
        // reboot) so stop watching for signals
        auto method =
            this->bus.new_method_call(SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
                                      SYSTEMD_INTERFACE, "Unsubscribe");

        try
        {
            this->bus.call(method);
        }
        catch (const sdbusplus::exception_t& e)
        {
            info("Error in Unsubscribe: {ERROR}", "ERROR", e);
        }

        // disable the system state change object as well
        this->stateSignal.reset();

        return 0;
    }

    // Caught the signal that indicates the BMC is now BMC_READY
    if ((newStateUnit == obmcStandbyTarget) && (newStateResult == signalDone))
    {
        info("BMC_READY");
        this->currentBMCState(BMCState::Ready);
    }

    return 0;
}

BMC::Transition BMC::requestedBMCTransition(Transition value)
{
    info("Setting the RequestedBMCTransition field to "
         "{REQUESTED_BMC_TRANSITION}",
         "REQUESTED_BMC_TRANSITION", value);

    executeTransition(value);
    return server::BMC::requestedBMCTransition(value);
}

BMC::BMCState BMC::currentBMCState(BMCState value)
{
    info("Setting the BMCState field to {CURRENT_BMC_STATE}",
         "CURRENT_BMC_STATE", value);

    return server::BMC::currentBMCState(value);
}

BMC::RebootCause BMC::lastRebootCause(RebootCause value)
{
    info("Setting the RebootCause field to {LAST_REBOOT_CAUSE}",
         "LAST_REBOOT_CAUSE", value);

    return server::BMC::lastRebootCause(value);
}

uint64_t BMC::lastRebootTime() const
{
    using namespace std::chrono;
#ifdef __ZEPHYR__
    /* No <sys/sysinfo.h> on Zephyr: k_uptime_get() is milliseconds since
     * boot. */
    auto uptime = milliseconds(k_uptime_get());
#else
    struct sysinfo info;

    auto rc = sysinfo(&info);
    assert(rc == 0);

    // Uptime is in seconds.
    auto uptime = seconds(info.uptime);
#endif
    // Uptime is relative to now, so the boot timestamp is now - uptime.
    auto now = time_point_cast<milliseconds>(system_clock::now());
    auto rebootTime = now - duration_cast<milliseconds>(uptime);

    return rebootTime.time_since_epoch().count();
}

void BMC::discoverLastRebootCause()
{
#ifdef __ZEPHYR__
    /* There is no usable reset-cause register on this SoC. The generated
     * header defines a SEC_PMU->PMU_STATUS RST_SRC field group
     * (reg_sec_pmu_rg.h bits 30..24: POR/BOR/PAD/IWDT/SOFTWARE/CPU), but on
     * real hardware those bits never become set -- PMU_STATUS reads back
     * 0x0000000a after a cold boot, i.e. just the live HSI_EN|VREF_OK
     * status bits, and rst_src stays 0 across a commanded software reboot
     * too. The manual agrees: it documents no reset-cause register, only a
     * fatal-error cause one.
     *
     * So until a software marker is introduced (e.g. a flag written to the
     * overlay filesystem before a commanded reboot, cleared again at the
     * next boot), the only honest answer is Unknown. */
    info("No reset-cause register on this SoC, reporting Unknown");
    this->lastRebootCause(RebootCause::Unknown);
    return;
#else
    uint64_t bootReason = 0;
    std::ifstream file;
    auto bootstatusPath = "/sys/class/watchdog/watchdog0/bootstatus";

    file.exceptions(std::ifstream::failbit | std::ifstream::badbit |
                    std::ifstream::eofbit);

    try
    {
        file.open(bootstatusPath);
        file >> bootReason;
    }
    catch (const std::exception& e)
    {
        auto rc = errno;
        error("Failed to read sysfs file {FILE} with errno {ERRNO}", "FILE",
              bootstatusPath, "ERRNO", rc);
    }

    switch (bootReason)
    {
        case WDIOF_EXTERN1:
            this->lastRebootCause(RebootCause::Watchdog);
            return;
        case WDIOF_CARDRESET:
            this->lastRebootCause(RebootCause::POR);
            return;
        default:
            this->lastRebootCause(RebootCause::Unknown);
            // Continue below to see if more details can be found
            // on reason for reboot
            break;
    }

    // If the above code could not detect a reason, check to see
    // if an AC loss occured.
    size_t chassisId = 0;
    if (phosphor::state::manager::utils::checkACLoss(chassisId))
    {
        this->lastRebootCause(RebootCause::POR);
        return;
    }

    // If the above code could not detect a reason, look for a the
    // reset-cause-pinhole gpio to see if it is the reason for the reboot
    auto gpioval =
        phosphor::state::manager::utils::getGpioValue("reset-cause-pinhole");

    // A 0 indicates a pinhole reset occurred
    if (0 == gpioval)
    {
        info("The BMC reset was caused by a pinhole reset");
        this->lastRebootCause(RebootCause::PinholeReset);

        // Generate log telling user a pinhole reset has occurred
        const std::string errorMsg = "xyz.openbmc_project.State.PinholeReset";
        phosphor::state::manager::utils::createError(
            this->bus, errorMsg,
            sdbusplus::xyz::openbmc_project::Logging::server::Entry::Level::
                Notice);
    }

    return;
#endif
}

} // namespace manager
} // namespace state
} // namespace phosphor
