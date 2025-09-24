[Click here](../README.md) to view the README.

## Design and implementation

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secured boot process starts from the ROM boot with the secured enclave (SE) as the root of trust (RoT). From the secured enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. 

In the CM33 non-secure application, the clocks and system resources are initialized by the BSP initialization function. The retarget-io middleware is configured to use the debug UART. Then the user button (**USER BTN1**) is initialized and `scan_task` is created before starting the FreeRTOS scheduler. The `scan_task` is responsible for initializing the Wi-Fi device and starting the scan based on the filter type. The WCM middleware supports the following filter types:

- **No filter:** As the name indicates, all the Wi-Fi networks are provided to the scan callback function

- **Filtering for SSID:** The scan callback receives only the networks whose SSID matches the SSID filter parameter

- **Filtering for MAC address:** The scan callback receives only the networks whose MAC address matches the MAC address filter parameter

- **Filtering for frequency band:** The scan callback receives only the networks that advertise in the frequency band provided as the filter parameter

- **Filtering for RSSI:** The scan callback receives only the networks that have RSSI greater than the RSSI provided as the filter parameter

The scan callback function executes under the context of the WCM middleware's worker thread. After the scan is complete, the scan callback sends a task notification to `scan_task` because `cy_wcm_start_scan` is a non-blocking function and returns without waiting for the scan to complete.

In this example, you can switch to a different type of filter by pressing SW2 (**USER_BTN1**). An ISR sets the flag and based on the flag set, the `scan_filter_mode_select` global variable of the `scan_filter_mode` enumeration type is incremented to let the `scan_task` know the type of filter to be applied. The value of `scan_filter_mode` is reset to `SCAN_FILTER_NONE` when the variable is incremented to `SCAN_FILTER_INVALID`.
