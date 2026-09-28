if(CONFIG_OPENBMC_PHOSPHOR_STATE_MANAGER)
  # Zephyr build for phosphor-state-manager.
  #
  # Upstream builds one executable per daemon; on Zephyr every daemon is a
  # thread of the single BMC application instead, so all the selected
  # daemons are compiled into one library. Each *_main() entry point is
  # renamed (main -> <daemon>_main) inside #ifdef __ZEPHYR__ and connects
  # to the in-process dbus-broker with connect_to_dbroker().

  zephyr_library_named(phosphor-state-manager)

  # Sources live in the module root (one level up from this zephyr/ subdir).
  set(SRC_DIR ${ZEPHYR_CURRENT_MODULE_DIR})

  # NOTE: these are deliberately *library-scoped* (not zephyr_include_directories).
  # This directory holds our own config.h, and phosphor-bmc-code-mgt's zephyr/
  # directory (also on the global include path) holds one too -- exposing ours
  # globally would make one of the two modules silently pick up the wrong
  # config.h depending on CMake include order.
  #
  # zephyr/config.h replaces the meson-generated config.h of the upstream build.
  zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
  # Module root: utils.hpp, settings.hpp, bmc_state_manager.hpp, ...
  zephyr_library_include_directories(${SRC_DIR})

  # Common sources, shared by every daemon.
  zephyr_library_sources(
      ${SRC_DIR}/utils.cpp
  )

  if(CONFIG_OPENBMC_PHOSPHOR_STATE_MANAGER_BMC)
    zephyr_library_sources(
        ${SRC_DIR}/bmc_state_manager.cpp
        ${SRC_DIR}/bmc_state_manager_main.cpp
    )
  endif()

  if(CONFIG_OPENBMC_PHOSPHOR_STATE_MANAGER_HOST)
    # NOTE: settings.cpp is deliberately NOT built here. Its settings::Objects
    # class collides with the identically-named copy in phosphor-host-ipmid
    # (both live in the global "settings" namespace), which is a hard link
    # error once both modules are in the image. The Host daemon does not need
    # it on Zephyr either: there is no settings daemon to look the
    # AutoReboot / PowerRestore policy up from, so the Host constructor must
    # not build a settings::Objects at all (see host_state_manager.hpp).
    zephyr_library_sources(
        ${SRC_DIR}/host_state_manager.cpp
        ${SRC_DIR}/host_state_manager_main.cpp
        ${SRC_DIR}/host_check.cpp
    )
  endif()

  if(CONFIG_OPENBMC_PHOSPHOR_STATE_MANAGER_CHASSIS)
    zephyr_library_sources(
        ${SRC_DIR}/chassis_state_manager.cpp
        ${SRC_DIR}/chassis_state_manager_main.cpp
    )
  endif()

  if(CONFIG_OPENBMC_PHOSPHOR_STATE_MANAGER_HYPERVISOR)
    zephyr_library_sources(
        ${SRC_DIR}/hypervisor_state_manager.cpp
        ${SRC_DIR}/hypervisor_state_manager_main.cpp
    )
  endif()

  # Boost.Asio on Zephyr: avoid threads/RTTI and the deprecated APIs.
  target_compile_definitions(phosphor-state-manager PRIVATE
      -DBOOST_ERROR_CODE_HEADER_ONLY
      -DBOOST_SYSTEM_NO_DEPRECATED
      -DBOOST_ASIO_DISABLE_THREADS
      -DBOOST_NO_RTTI
      -DBOOST_ASIO_NO_DEPRECATED
  )

  zephyr_library_link_libraries(sdbusplus)
  zephyr_library_link_libraries(phosphor-dbus-interfaces)
  zephyr_library_link_libraries(phosphor-logging)
  zephyr_library_link_libraries(stdplus)
  zephyr_library_link_libraries(boost)
  zephyr_library_link_libraries(cereal)
  zephyr_library_link_libraries(fmt)

  # sdeventplus is only needed by the chassis POH timer.
  if(CONFIG_OPENBMC_PHOSPHOR_STATE_MANAGER_CHASSIS)
    zephyr_library_link_libraries(sdeventplus)
  endif()
endif()
