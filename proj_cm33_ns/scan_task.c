/*******************************************************************************
* File Name        : scan_task.c
*
* Description      : This file contains functions that perform network related 
*                    tasks like connecting to an AP, scanning for APs, 
*                    connection event callbacks, and utility function for 
*                    printing scan results.
*
* Related Document : See README.md
*
********************************************************************************
* Copyright 2024-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
* Header file includes
*******************************************************************************/
#include "cybsp.h"
#include <inttypes.h>
#include "scan_task.h"
#include "retarget_io_init.h"


/*******************************************************************************
* Macros
*******************************************************************************/
#define RESET_VAL                                    (0U)
#define ULVALUE_DATA                                 (0U)
#define BITS_TO_CLEARONENTRY                         (0U)
#define BITS_TO_CLEARONEXIT                          (0U)
#define BTN1_INTERRUPT_PRIORITY                      (7U)
#define APP_SDIO_INTERRUPT_PRIORITY                  (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY             (2U)
#define APP_SDIO_FREQUENCY_HZ                        (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK                      (64U)


/*******************************************************************************
* Global Variables
*******************************************************************************/
TaskHandle_t scan_task_handle;

/* This variable holds the value of the total number of the scan results that is
 * available in the scan callback function in the current scan.
 */
uint32_t num_scan_result;

/* This is used to decide the type of scan filter to be used. The valid values
 * are provided in the enumeration scan_filter_mode.
 */
enum scan_filter_mode scan_filter_mode_select = SCAN_FILTER_NONE;

const char* band_string[] =
{
    [CY_WCM_WIFI_BAND_ANY]    = "2.4 GHz, 5 GHz and 6 GHz",
    [CY_WCM_WIFI_BAND_2_4GHZ] = "2.4 GHz",
    [CY_WCM_WIFI_BAND_5GHZ]   = "5 GHz",
    [CY_WCM_WIFI_BAND_6GHZ]   = "6 GHz"
};

/* Flag to keep track of button press event*/
bool button_pressed = false;
static mtb_hal_sdio_t sdio_instance;
cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

/* SysPm callback parameter structure for SDHC */
static cy_stc_syspm_callback_params_t sdcardDSParams =
{
    .context   = &sdhc_host_context,
    .base      = CYBSP_WIFI_SDIO_HW
};

/* SysPm callback structure for SDHC*/
static cy_stc_syspm_callback_t sdhcDeepSleepCallbackHandler =
{
    .callback           = Cy_SD_Host_DeepSleepCallback,
    .skipMode           = SYSPM_SKIP_MODE,
    .type               = CY_SYSPM_DEEPSLEEP,
    .callbackParams     = &sdcardDSParams,
    .prevItm            = NULL,
    .nextItm            = NULL,
    .order              = SYSPM_CALLBACK_ORDER
};
#endif


/*******************************************************************************
* Function Definitions
*******************************************************************************/

/*******************************************************************************
* Function Name: sdio_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for SDIO instance.
*******************************************************************************/
static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

/*******************************************************************************
* Function Name: host_wake_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for the host wake up input pin.
*******************************************************************************/
static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

/*******************************************************************************
* Function Name: app_sdio_init
********************************************************************************
* Summary:
* This function configures and initializes the SDIO instance used in 
* communication between the host MCU and the wireless device.
*******************************************************************************/
static void app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;
    
    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };

    cy_stc_sysint_t host_wake_intr_cfg =
    {
            .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
            .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    /* Initialize the SDIO interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status = Cy_SysInt_Init(&sdio_intr_cfg, sdio_interrupt_handler);

    /* SDIO interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    /* Setup SDIO using the HAL object and desired configuration */
    result = mtb_hal_sdio_setup(&sdio_instance, &CYBSP_WIFI_SDIO_sdio_hal_config, NULL, &sdhc_host_context);

    /* SDIO setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Initialize and Enable SD HOST */
    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW, CYBSP_WIFI_SDIO_sdio_hal_config.host_config, &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);

    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size = SDHC_SDIO_64BYTES_BLOCK;

    /* Configure SDIO */
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
    /* SDHC SysPm callback registration */
    Cy_SysPm_RegisterCallback(&sdhcDeepSleepCallbackHandler);
#endif /* (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */

    /* Setup GPIO using the HAL object for WIFI WL REG ON  */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin, CYBSP_WIFI_WL_REG_ON_PORT_NUM, CYBSP_WIFI_WL_REG_ON_PIN);

    /* Setup GPIO using the HAL object for WIFI HOST WAKE PIN  */
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin, CYBSP_WIFI_HOST_WAKE_PORT_NUM, CYBSP_WIFI_HOST_WAKE_PIN);

    /* Initialize the Host wakeup interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status_host_wake =  Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler);

    /* Host wake up interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status_host_wake)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
}

/*******************************************************************************
* Function Name: button_interrupt_handler
********************************************************************************
* Summary:
*  Interrupt handler for the user button.
*******************************************************************************/
static void button_interrupt_handler(void)
{
    if (Cy_GPIO_GetInterruptStatus(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN))
    {
        button_pressed = true;
    }

    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT, CYBSP_USER_BTN1_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);

    /* CYBSP_USER_BTN1 (SW2) and CYBSP_USER_BTN2 (SW4) share the same port in
     * the PSOC™ Edge E84 evaluation kit and hence they share the same NVIC IRQ
     * line. Since both the buttons are configured for falling edge interrupt in
     * the BSP, pressing any button will trigger the execution of this ISR. Therefore,
     * we must clear the interrupt flag of the user button (CYBSP_USER_BTN2) to avoid
     * issues in case if user presses BTN2 by mistake.
     */
    #ifdef CYBSP_USER_BTN2_ENABLED
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT, CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
    #endif
}

/*******************************************************************************
* Function Name: user_button_init
********************************************************************************
*
* Summary:
*  Initialize the button with Interrupt
*
* Parameters:
*  None
*
* Return:
*  None
*
*******************************************************************************/
void user_button_init(void)
{
    cy_en_sysint_status_t btn_interrupt_init_status ;

    /* Interrupt config structure */
    cy_stc_sysint_t intrCfg =
    {
        .intrSrc = CYBSP_USER_BTN_IRQ,
        .intrPriority = BTN1_INTERRUPT_PRIORITY
    };

    /* CYBSP_USER_BTN1 (SW2) and CYBSP_USER_BTN2 (SW4) share the same port in the
    * PSOC™ Edge E84 evaluation kit and hence they share the same NVIC IRQ line.
    * Since both are configured in the BSP via the Device Configurator, the
    * interrupt flags for both the buttons are set right after they get initialized
    * through the call to cybsp_init(). The flags must be cleared before initializing
    * the interrupt, otherwise the interrupt line will be constantly asserted.
    */
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN1_PORT,CYBSP_USER_BTN1_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN1_IRQ);
    #ifdef CYBSP_USER_BTN2_ENABLED
    Cy_GPIO_ClearInterrupt(CYBSP_USER_BTN2_PORT,CYBSP_USER_BTN2_PIN);
    NVIC_ClearPendingIRQ(CYBSP_USER_BTN2_IRQ);
    #endif

    /* Initialize the interrupt and register interrupt callback */
    btn_interrupt_init_status = Cy_SysInt_Init(&intrCfg, &button_interrupt_handler);

    /* Button interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != btn_interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable the interrupt in the NVIC */
    NVIC_EnableIRQ(intrCfg.intrSrc);
}

/*******************************************************************************
* Function Name: print_scan_result
********************************************************************************
* Summary: This function prints the scan result accumulated by the scan
*          handler.
*
* Parameters:
*  cy_wcm_scan_result_t *result: Pointer to the scan result.
*
* Return:
*  void
*
*******************************************************************************/
static void print_scan_result(cy_wcm_scan_result_t *result)
{
    char* security_type_string;

    /* Convert the security type of the scan result to the corresponding
     * security string
     */
    switch (result->security)
    {
        case CY_WCM_SECURITY_OPEN:
            security_type_string = SECURITY_OPEN;
            break;
        case CY_WCM_SECURITY_WEP_PSK:
            security_type_string = SECURITY_WEP_PSK;
            break;
        case CY_WCM_SECURITY_WEP_SHARED:
            security_type_string = SECURITY_WEP_SHARED;
            break;
        case CY_WCM_SECURITY_WPA_TKIP_PSK:
            security_type_string = SECURITY_WEP_TKIP_PSK;
            break;
        case CY_WCM_SECURITY_WPA_AES_PSK:
            security_type_string = SECURITY_WPA_AES_PSK;
            break;
        case CY_WCM_SECURITY_WPA_MIXED_PSK:
            security_type_string = SECURITY_WPA_MIXED_PSK;
            break;
        case CY_WCM_SECURITY_WPA2_AES_PSK:
            security_type_string = SECURITY_WPA2_AES_PSK;
            break;
        case CY_WCM_SECURITY_WPA2_TKIP_PSK:
            security_type_string = SECURITY_WPA2_TKIP_PSK;
            break;
        case CY_WCM_SECURITY_WPA2_MIXED_PSK:
            security_type_string = SECURITY_WPA2_MIXED_PSK;
            break;
        case CY_WCM_SECURITY_WPA2_FBT_PSK:
            security_type_string = SECURITY_WPA2_FBT_PSK;
            break;
        case CY_WCM_SECURITY_WPA3_SAE:
            security_type_string = SECURITY_WPA3_SAE;
            break;
        case CY_WCM_SECURITY_WPA3_WPA2_PSK:
            security_type_string = SECURITY_WPA3_WPA2_PSK;
            break;
        case CY_WCM_SECURITY_IBSS_OPEN:
            security_type_string = SECURITY_IBSS_OPEN;
            break;
        case CY_WCM_SECURITY_WPS_SECURE:
            security_type_string = SECURITY_WPS_SECURE;
            break;
        case CY_WCM_SECURITY_UNKNOWN:
            security_type_string = SECURITY_UNKNOWN;
            break;
        case CY_WCM_SECURITY_WPA2_WPA_AES_PSK:
            security_type_string = SECURITY_WPA2_WPA_AES_PSK;
            break;
        case CY_WCM_SECURITY_WPA2_WPA_MIXED_PSK:
            security_type_string = SECURITY_WPA2_WPA_MIXED_PSK;
            break;
        case CY_WCM_SECURITY_WPA_TKIP_ENT:
            security_type_string = SECURITY_WPA_TKIP_ENT;
            break;
        case CY_WCM_SECURITY_WPA_AES_ENT:
            security_type_string = SECURITY_WPA_AES_ENT;
            break;
        case CY_WCM_SECURITY_WPA_MIXED_ENT:
            security_type_string = SECURITY_WPA_MIXED_ENT;
            break;
        case CY_WCM_SECURITY_WPA2_TKIP_ENT:
            security_type_string = SECURITY_WPA2_TKIP_ENT;
            break;
        case CY_WCM_SECURITY_WPA2_AES_ENT:
            security_type_string = SECURITY_WPA2_AES_ENT;
            break;
        case CY_WCM_SECURITY_WPA2_MIXED_ENT:
            security_type_string = SECURITY_WPA2_MIXED_ENT;
            break;
        case CY_WCM_SECURITY_WPA2_FBT_ENT:
            security_type_string = SECURITY_WPA2_FBT_ENT;
            break;
        default:
            security_type_string = SECURITY_UNKNOWN;
            break;
    }

    printf(" %2"PRIu32"   %-32s     %4d     %2d      %02X:%02X:%02X:%02X:%02X:%02X         %-15s\n",
           num_scan_result, result->SSID,
           result->signal_strength, result->channel, result->BSSID[0], 
           result->BSSID[1],result->BSSID[2], result->BSSID[3], 
           result->BSSID[4], result->BSSID[5],
           security_type_string);
}

/*******************************************************************************
* Function Name: scan_callback
********************************************************************************
* Summary: The callback function which accumulates the scan results. After
* completing the scan, it sends a task notification to scan_task.
*
* Parameters:
*  cy_wcm_scan_result_t *result_ptr: Pointer to the scan result
*  void *user_data: User data.
*  cy_wcm_scan_status_t status: Status of scan completion.
*
* Return:
*  void
*
*******************************************************************************/
static void scan_callback(cy_wcm_scan_result_t *result_ptr, void *user_data, 
            cy_wcm_scan_status_t status)
{

    uint8_t ssid_len = RESET_VAL;

    if(NULL != result_ptr)
    {
        ssid_len = strlen((const char *)result_ptr->SSID);
    }

    if ((RESET_VAL != ssid_len) && (status == CY_WCM_SCAN_INCOMPLETE))
    {
        /* Increment the number of scan results and print the result*/
        num_scan_result++;
        print_scan_result(result_ptr);
    }

    if ((CY_WCM_SCAN_COMPLETE == status) )
    {
        /* Reset the number of scan results to 0 for the next scan.*/
        num_scan_result = RESET_VAL;

        /* Notify that scan has completed.*/
        xTaskNotify(scan_task_handle, ULVALUE_DATA, eNoAction);
    }
}

/*******************************************************************************
* Function Name: scan_task
********************************************************************************
* Summary: This task initializes the Wi-Fi device, Wi-Fi transport, lwIP
* network stack, and issues a scan for the available networks. A scan filter
* is applied after button press depending on the value of scan_filter_mode_select. 
* After starting the scan, it waits for the notification from the scan callback 
* for completion of the scan. It then waits for a delay specified by 
* SCAN_DELAY_PERIOD_MS before repeating the process.
*
*
* Parameters:
*  void* arg: Task parameter defined during task creation (unused).
*
* Return:
*  void
*
*******************************************************************************/
void scan_task(void *arg)
{
    cy_wcm_scan_filter_t scan_filter;
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_wcm_mac_t scan_for_mac_value = {SCAN_FOR_MAC_ADDRESS};

    memset(&scan_filter, RESET_VAL, sizeof(cy_wcm_scan_filter_t));

    app_sdio_init();

    /* Initialize wcm */
    wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
    wcm_config.wifi_interface_instance = &sdio_instance;

    result = cy_wcm_init(&wcm_config);

    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    while (true)
    {
        /* check if button_pressed flag is updated to true in the button ISR */
        if(true == button_pressed )
        {
            /* Increment and check if the scan filter selected is invalid. 
             * If invalid,reset to no scan filter.
             */
            if(++scan_filter_mode_select >= SCAN_FILTER_INVALID)
            {
                scan_filter_mode_select = SCAN_FILTER_NONE;
            }

            button_pressed = false;
        }

        /* Select the type of filter to use.*/
        switch (scan_filter_mode_select)
        {
            case SCAN_FILTER_NONE:
                APP_INFO(("Scanning without any filter\n"));
                break;

            case SCAN_FILTER_SSID:
                APP_INFO(("Scanning for %s.\n", SCAN_FOR_SSID_VALUE));

                /* Configure the scan filter for SSID specified by
                 * SCAN_FOR_SSID_VALUE.
                 */
                scan_filter.mode = CY_WCM_SCAN_FILTER_TYPE_SSID;
                memcpy(scan_filter.param.SSID, SCAN_FOR_SSID_VALUE, 
                sizeof(SCAN_FOR_SSID_VALUE));
                break;

            case SCAN_FILTER_RSSI:
                APP_INFO(("Scanning for RSSI > %d dBm.\n", SCAN_FOR_RSSI_VALUE));

                /* Configure the scan filter for RSSI range specified by
                 * SCAN_FOR_RSSI_VALUE.
                 */
                scan_filter.mode = CY_WCM_SCAN_FILTER_TYPE_RSSI;
                scan_filter.param.rssi_range = SCAN_FOR_RSSI_VALUE;
                break;

            case SCAN_FILTER_MAC:
                APP_INFO(("Scanning for %02X:%02X:%02X:%02X:%02X:%02X.\n", 
                scan_for_mac_value[0], 
                scan_for_mac_value[1], scan_for_mac_value[2], scan_for_mac_value[3], 
                scan_for_mac_value[4], scan_for_mac_value[5]));

                /* Configure the scan filter for MAC specified by scan_for_mac_value
                 */
                scan_filter.mode = CY_WCM_SCAN_FILTER_TYPE_MAC;
                memcpy(scan_filter.param.BSSID, &scan_for_mac_value, sizeof(scan_for_mac_value));
                break;

            case SCAN_FILTER_BAND:
                APP_INFO(("Scanning in %s band.\n", band_string[SCAN_FOR_BAND_VALUE]));

                /* Configure the scan filter for band specified by
                 * SCAN_FOR_BAND_VALUE.
                 */
                scan_filter.mode = CY_WCM_SCAN_FILTER_TYPE_BAND;
                scan_filter.param.band = SCAN_FOR_BAND_VALUE;
                break;

            default:
                break;
        }

        PRINT_SCAN_TEMPLATE();

        if(SCAN_FILTER_NONE == scan_filter_mode_select)
        {
            result = cy_wcm_start_scan(scan_callback, NULL, NULL);
        }
        else
        {
            result = cy_wcm_start_scan(scan_callback, NULL, &scan_filter);
        }

        /* Wait for scan completion if scan was started successfully. The API
         * cy_wcm_start_scan doesn't wait for scan completion. If it is called
         * again when the scan hasn't completed, the API returns
         * CY_WCM_RESULT_SCAN_IN_PROGRESS.
         */
        if (CY_RSLT_SUCCESS == result)
        {
            xTaskNotifyWait(BITS_TO_CLEARONENTRY, BITS_TO_CLEARONEXIT, NULL, 
            portMAX_DELAY);
        }

        vTaskDelay(pdMS_TO_TICKS(SCAN_DELAY_MS));
    }
}

/* [] END OF FILE */
