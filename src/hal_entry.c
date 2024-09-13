/***********************************************************************************************************************
 * File Name    : hal_entry.c
 * Description  : Main application
 **********************************************************************************************************************/
/***********************************************************************************************************************
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
 * other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
 * applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
 * EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
 * SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
 * SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
 * this software. By using this software, you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 *
 * Copyright (C) 2024 Renesas Electronics Corporation. All rights reserved.
 ***********************************************************************************************************************/
#include "hal_data.h"
#include "rtt_common_utils.h"
#include "application_utils.h"
#include "hdmi_cec_utils.h"
#include "hdmi_ddc_utils.h"

///####################### Application Option Setting #######################

#define APP_HDMI_DDC_PHYSICAL_ADDR_GET   (1) // 0: Use fixed value, 1: Get from sink device edid
#define APP_VENDOR_ID_INSTALL            (1) // 0: Use fixed value, 1: Install using SEGGER RTT Viewer

#define DEBUG_CEC_INTERRUPT_EVENT_OUTPUT (0) // 0: Disabled, 1: Enabled

///#################### End of Application Option Setting ###################

///########################### User Device Setting ##########################

/* My physical address. */
/* Specify your physical address. You can guess from the HDMI connector name you connected this device to.  */
/* If APP_HDMI_DDC_PHYSICAL_ADDR_GET is enabled (1), the value will be updated by information of EDID that acquired via HDMI-DDC (I2C). */
uint8_t my_physical_address[4] =                {0x0, 0x0, 0x1, 0x3}; // 3.1.0.0

/* My OSD name. */
/* The text will be displayed on TV menu. Maximum length is 14 bytes. */
#define MY_OSD_NAME_LENGTH                      (12)
const uint8_t my_osd_name[MY_OSD_NAME_LENGTH] = "RA CEC DEMO";

/* My Vendor ID. */
/* Specify vendor ID of your connected TV. For example, LG TV: {0x00, 0xE0, 0x91}. Toshiba TV: {0x00, 0x00, 0x39} */
/* If APP_VENDOR_ID_INSTALL is enabled (1), the value will be updated by SEGGER RTT Viewer installation. */
uint8_t my_vendor_id[3] =                       {0x00, 0x00, 0x00};

///####################### End of User Device Setting #######################

#if (DEBUG_CEC_INTERRUPT_EVENT_OUTPUT == 1)
#define RTT_DEBUG(fn_, ...)   APP_PRINT((fn_), ##__VA_ARGS__)
#else
#define RTT_DEBUG(...)
#endif

cec_addr_t my_logical_address = CEC_ADDR_UNREGISTERED;

volatile bool system_audio_mode_support_function = false;
volatile bool system_audio_mode_status = false;

/* List of CEC bus device status */
cec_device_status_t cec_bus_device_list[16];

/* User action request, type and target cec device */
volatile bool user_action_detect_flag = false;
uint8_t       user_action_type        = 0x0;
cec_addr_t    user_action_cec_target;

/* CEC action request and type */
volatile bool cec_action_request_detect_flag = false;
uint8_t       cec_action_type                = 0x0;

/* CEC interrupt event notification flags */
volatile bool        cec_rx_complete_flag = false;
volatile bool        cec_tx_complete_flag = false;
volatile bool        cec_err_flag = false;
volatile cec_error_t cec_err_type;

/* RX buffer for CEC reception data */
#define CEC_RX_DATA_BUFF_DATA_NUMBER (16 * 5)
cec_rx_message_buff_t cec_rx_data_buff[CEC_RX_DATA_BUFF_DATA_NUMBER];
volatile uint8_t      cec_rx_data_buff_next_store_point = 0;

fsp_err_t cec_message_send(cec_addr_t destination, uint8_t opcode, uint8_t const * data_buff, uint8_t data_buff_length);

fsp_err_t cec_logical_address_allocate(void);
fsp_err_t cec_logical_address_allocate_attempt(cec_addr_t local_addr);

void cec_system_audio_mode_support_enabling(void);
void cec_system_audio_mode_request(void);

void cec_rx_data_check(void);
void cec_system_auto_response(cec_rx_message_buff_t const * buff);

void cec_bus_scan(void);
void cec_bus_status_buffer_display(void);

void R_BSP_WarmStart(bsp_warm_start_event_t event);

void hal_entry(void)
{
    fsp_err_t fsp_err = FSP_SUCCESS;
    uint8_t cec_data[CEC_DATA_BUFFER_LENGTH];

    /* Print project banner */
    fsp_pack_version_t fsp_version = {RESET_VALUE};
    R_FSP_VersionGet(&fsp_version);
    APP_PRINT(BANNER_INFO, fsp_version.version_id_b.major, fsp_version.version_id_b.minor, fsp_version.version_id_b.patch);
    APP_PRINT(APP_DESCRIPTION);
    APP_PRINT(APP_LED_DESCRIPTION);

    /* Make 500 ms delay for HDMI connection. In deal, the MCU should check the hot plug pin of HDMI connector. */
    R_BSP_SoftwareDelay(500, BSP_DELAY_UNITS_MILLISECONDS);

    /* Initialize and enable external irq for user button detect */
    user_button_irq_initialize();

    /* Initialize LEDs. A pin for LED2 now starts PWM output. */
    demo_system_initialize();

    /* Get physical address */
#if (APP_HDMI_DDC_PHYSICAL_ADDR_GET == 0)
    APP_PRINT("Fixed physical address will be used\r\n");
#else
    APP_PRINT("Getting physical address from EDID via HDMI-DDC channel (I2C) ...\r\n");
    fsp_err = physical_address_get(&my_physical_address[0]);
    if(FSP_SUCCESS != fsp_err)
    {
        APP_PRINT("DDC physical address get failed.\r\n");
        ERROR_INDICATE_LED_ON; __BKPT(0);
    }
#endif
    APP_PRINT("My physical address is %x.%x.%x.%x.\r\n\r\n", my_physical_address[3], my_physical_address[2], my_physical_address[1], my_physical_address[0]);

    /* Set my vendor ID */
#if (APP_VENDOR_ID_INSTALL == 0)
    APP_PRINT("Fixed vendor ID will be used.\r\n");
#else
    APP_PRINT("Setting up my vendor ID ...\r\n");
    vendor_id_install(&my_vendor_id[0]);
#endif
    APP_PRINT("My vendor ID is 0x%02x, 0x%02x, 0x%02x.\r\n\r\n", my_vendor_id[0], my_vendor_id[1], my_vendor_id[2]);

    /* Open CEC module */
    fsp_err = R_CEC_Open(&g_cec0_ctrl, &g_cec0_cfg);
    if(FSP_SUCCESS != fsp_err){ ERROR_INDICATE_LED_ON; __BKPT(0); }

    /* Make 50 milliseconds delay. R_CEC_MediaInit may return FSP_ERR_IN_USE for up to 45 milliseconds after calling R_CEC_Open */
    R_BSP_SoftwareDelay(50, BSP_DELAY_UNITS_MILLISECONDS);

    /* Initialize CEC logical address */
    fsp_err = cec_logical_address_allocate();
    if(FSP_SUCCESS == fsp_err)
    {
        APP_PRINT("CEC logical address allocation completed.\r\n");

        /* Now we recognize my logical address, update internal cec bus device status buffer with my device info */
        cec_bus_device_list[my_logical_address].is_power_status_store = true;
        cec_bus_device_list[my_logical_address].power_status = 0x1;

        cec_bus_device_list[my_logical_address].is_version_store = true;
        cec_bus_device_list[my_logical_address].cec_version = CEC_VERSION_1_4;

        cec_bus_device_list[my_logical_address].is_physical_address_store = true;
        memcpy(&cec_bus_device_list[my_logical_address].physical_address[0], &my_physical_address[0], 4);

        cec_bus_device_list[my_logical_address].is_vendor_id_store = true;
        memcpy(&cec_bus_device_list[my_logical_address].vendor_id[0], &my_vendor_id[0], 3);
    }
    else
    {
        APP_PRINT("CEC logical address allocation failed.\r\n");
        ERROR_INDICATE_LED_ON; __BKPT(0);
    }

    /* Clear all internal flags */
    user_action_detect_flag = false;
    cec_rx_complete_flag = false;

    APP_PRINT(APP_COMMAND_OPTION,
              system_audio_mode_support_function ? SYS_AUDIO_FUNC_E : SYS_AUDIO_FUNC_D,
              system_audio_mode_status ? SYS_AUDIO_ON : SYS_AUDIO_OFF);

    while(1)
    {
        user_action_check();
        if(user_action_detect_flag)
        {
            user_action_detect_flag = false;

            /* Basically, operating the remote controller turns on the device, so turn POWER_STATUS_LED_ON on. */
            if(cec_bus_device_list[my_logical_address].power_status == 0x0)
            {
                APP_PRINT("[System] Power On.\r\n");
                demo_system_power_on();
                cec_bus_device_list[my_logical_address].power_status = 0x1;
            }

            switch(user_action_type)
            {
                case USER_ACTION_BUS_SCAN: /* Scan CEC bus */
                    cec_bus_scan();
                    break;
                case USER_ACTION_DISPLAY_CEC_BUS_STATUS_BUFF: /* Display CEC bus buffer data */
                    cec_bus_status_buffer_display();
                    break;
                case USER_ACTION_REQUEST_POWER_ON: /* Power On (Image View On 0x04) */
                    cec_message_send(user_action_cec_target, CEC_OPCODE_IMAGE_VIEW_ON, NULL, 0);
                    break;
                case USER_ACTION_REQUEST_POWER_OFF: /* Power Off (Standby 0x36) */
                    cec_message_send(user_action_cec_target, CEC_OPCODE_STANDBY, NULL, 0);
                    break;
                case USER_ACTION_ENABLING_SYSTEM_AUDIO_MODE_SUPPORT:
                    cec_system_audio_mode_support_enabling();
                    break;
                case USER_ACTION_SYSTEM_AUDIO_MODE_REQUEST:
                    cec_system_audio_mode_request();
                    break;
                case USER_ACTION_REQUEST_VOLUME_UP: /* Volume Up. User Control Pressed 0x44 => User Control Released 0x45 */
                    cec_data[0] = USER_CONTROL_VOLUME_UP;
                    cec_message_send(user_action_cec_target, CEC_OPCODE_USER_CONTROL_PRESSED, &cec_data[0], 1); /* User Control Pressed */
                    R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MILLISECONDS);
                    cec_message_send(user_action_cec_target, CEC_OPCODE_USER_CONTROL_RELEASED, NULL, 0); /* User Control Released */
                    break;
                case USER_ACTION_REQUEST_VOLUME_DONW: /* Volume Down. User Control Pressed 0x44 => User Control Released 0x45 */
                    cec_data[0] = USER_CONTROL_VOLUME_DOWN;
                    cec_message_send(user_action_cec_target, CEC_OPCODE_USER_CONTROL_PRESSED, &cec_data[0], 1); /* User Control Pressed */
                    R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MILLISECONDS);
                    cec_message_send(user_action_cec_target, CEC_OPCODE_USER_CONTROL_RELEASED, NULL, 0); /* User Control Released */
                    break;
                case USER_ACTION_REQUEST_VOLUME_MUTE: /* Mute. User Control Pressed 0x44 => User Control Released 0x45 */
                    cec_data[0] = USER_CONTROL_MUTE;
                    cec_message_send(user_action_cec_target, CEC_OPCODE_USER_CONTROL_PRESSED, &cec_data[0], 1); /* User Control Pressed */
                    R_BSP_SoftwareDelay(100, BSP_DELAY_UNITS_MILLISECONDS);
                    cec_message_send(user_action_cec_target, CEC_OPCODE_USER_CONTROL_RELEASED, NULL, 0); /* User Control Released */
                    break;
//                case <type defined> ToDo
//                {
//                    /* Add your additional operation */
//                    break;
//                }
                default:
                    break;
            }

            user_action_type = 0x0;
            APP_PRINT(APP_COMMAND_OPTION,
                      system_audio_mode_support_function ? SYS_AUDIO_FUNC_E : SYS_AUDIO_FUNC_D,
                      system_audio_mode_status ? SYS_AUDIO_ON : SYS_AUDIO_OFF);
        }

        cec_rx_data_check();
        if(cec_action_request_detect_flag)
        {
            cec_action_request_detect_flag = false;
            bool mute; uint8_t volume;

            switch(cec_action_type)
            {
                case CEC_ACTION_POWER_ON:
                    demo_system_power_on();
                    cec_bus_device_list[my_logical_address].power_status = 0x1;
                    APP_PRINT("[System] Power On.\r\n");
                    break;
                case CEC_ACTION_POWER_OFF:
                    demo_system_power_off();
                    cec_bus_device_list[my_logical_address].power_status = 0x0;
                    APP_PRINT("[System] Power Off.\r\n");
                    break;
                case CEC_ACTION_VOLUME_UP:
                    demo_system_volume_change(false, true);
                    demo_system_volume_status_get(&mute, &volume);
                    APP_PRINT("[System] Sound volume up. Volume: %d%%.\r\n", volume);
                    break;
                case CEC_ACTION_VOLUME_DOWN:
                    demo_system_volume_change(false, false);
                    demo_system_volume_status_get(&mute, &volume);
                    APP_PRINT("[System] Sound volume down. Volume: %d%%.\r\n", volume);
                    break;
                case CEC_ACTION_VOLUME_MUTE:
                    demo_system_volume_change(true, false);
                    demo_system_volume_status_get(&mute, &volume);
                    if(mute)
                    {
                        APP_PRINT("[System] Sound volume mute.\r\n");
                    }
                    else
                    {
                        APP_PRINT("[System] Sound volume unmute. Volume: %d%%.\r\n", volume);
                    }
                    break;
//                case <type defined> ToDo
//                {
//                    /* Add your additional operation */
//                    break;
//                }
                default:
                    /* Do nothing */
                    break;
            }
        }

        R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
    }
}

void cec_interrupt_callback(cec_callback_args_t *p_args)
{
    switch (p_args->event)
    {
        case CEC_EVENT_READY:
        {
            /* In this demo, the ready flag is received by R_CEC_StatusGet(). But application can also get with this interrupt event. */
            RTT_DEBUG("@@@ READY\r\n");
            break;
        }
        case CEC_EVENT_TX_COMPLETE:
        {
            /* Application processing after transmission has completed. */
            RTT_DEBUG("@@@ TX COMP\r\n");
            cec_tx_complete_flag = true;
            break;
        }
        case CEC_EVENT_RX_DATA:
        {
            RTT_DEBUG("@@@ RX 0x%x\r\n", p_args->data_byte);
            /* Application to store and process received data bytes. */
            cec_rx_message_buff_t* p_buff = &cec_rx_data_buff[cec_rx_data_buff_next_store_point];
            if(p_buff->byte_counter == 0)
            {
                p_buff->source = (uint8_t)(p_args->data_byte >> 4);
                p_buff->destination = (uint8_t)(p_args->data_byte & 0xF);
            }
            else if(p_buff->byte_counter == 1)
            {
                p_buff->opcode = p_args->data_byte;
            }
            else
            {
                p_buff->data_buff[p_buff->byte_counter - 2] = p_args->data_byte;
            }

            p_buff->byte_counter++;
            if(p_buff->byte_counter == CEC_DATA_BUFFER_LENGTH)
            {
                p_buff->byte_counter = 0;
            }
            break;
        }
        case CEC_EVENT_RX_COMPLETE:
        {
            /* Application processing for message reception complete. */
            RTT_DEBUG("@@@ RX COMP\r\n");

            cec_rx_data_buff[cec_rx_data_buff_next_store_point].is_new_data = true;

            cec_rx_data_buff_next_store_point++;
            if(cec_rx_data_buff_next_store_point >= CEC_RX_DATA_BUFF_DATA_NUMBER)
            {
                cec_rx_data_buff_next_store_point = 0;
            }
            break;
        }
        case CEC_EVENT_ERR:
        {
            cec_err_flag = true;
            cec_err_type = p_args->errors;

            if(cec_err_type & (CEC_ERROR_OERR | CEC_ERROR_TERR))
            {
                cec_rx_message_buff_t* p_buff = &cec_rx_data_buff[cec_rx_data_buff_next_store_point];
                if(p_buff->byte_counter > 0)
                {
                    p_buff->is_error = true;

                    /* Cancel on-going store buffer */
                    p_buff->is_new_data = true;
                    cec_rx_data_buff_next_store_point++;
                    if(cec_rx_data_buff_next_store_point >= CEC_RX_DATA_BUFF_DATA_NUMBER)
                    {
                        cec_rx_data_buff_next_store_point = 0;
                    }
                }
            }

            RTT_DEBUG("@@@ ERR 0x%x\r\n", cec_err_type);
            break;
        }
        default:
        {
            /* Do nothing */
            break;
        }
    }
}

fsp_err_t cec_message_send(cec_addr_t destination, uint8_t opcode, uint8_t const * data_buff, uint8_t data_buff_length)
{
    fsp_err_t fsp_err = FSP_SUCCESS;
    cec_message_t cec_tx_message;
    uint8_t       cec_tx_message_length; /* Total message size, including header, opcode, and data */
    uint8_t       err_detect = false;
    uint32_t      timeout_ms = (uint32_t)(5 + 40 * (2 + data_buff_length));

    APP_PRINT("[> CEC Out] Dest: %d (%s),\r\n", destination, &cec_logical_device_list[destination][0]);
    uint32_t opcode_list_point = opcode_description_find(opcode);
    APP_PRINT("            Opcode: 0x%x (%s)", opcode, &cec_opcode_list[opcode_list_point].opcode_desc_str[0]);
    if(data_buff_length > 0)
    {
        APP_PRINT(", Data: ");
        for(int j=0; j<data_buff_length; j++)
        {
            APP_PRINT("0x%x,", data_buff[j]);
        }
    }
    APP_PRINT(" sending ... ");

    /* Clear CEC TX message buffer */
    memset(&cec_tx_message, 0U, sizeof(cec_tx_message));

    /* Create message */
    cec_tx_message.destination = destination;
    cec_tx_message.opcode      = opcode;
    memcpy(&cec_tx_message.data[0], data_buff, data_buff_length);
    cec_tx_message_length      = 2U + data_buff_length;

    /* Clear error flag */
    cec_err_flag = false;
    cec_err_type = 0x0;

    do{
        fsp_err = R_CEC_Write(&g_cec0_ctrl, &cec_tx_message, cec_tx_message_length);
    }while(FSP_ERR_IN_USE == fsp_err);
    if(FSP_SUCCESS != fsp_err)
    {
        APP_PRINT("R_CEC_Write failed.\r\n");
        ERROR_INDICATE_LED_ON; __BKPT(0);
    }

    /* Wait for tx completion */
    while(!cec_tx_complete_flag)
    {
        if(cec_err_flag)
        {
            cec_err_flag = false;

            if(cec_err_type & (CEC_ERROR_UERR | CEC_ERROR_ACKERR | CEC_ERROR_TXERR | CEC_ERROR_AERR | CEC_ERROR_BLERR))
            {
                err_detect = true;
                break;
            }
        }

        timeout_ms--;
        if(timeout_ms == 0)
        {
            break;
        }
        R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
    }
    cec_tx_complete_flag = false;

    if(0 < timeout_ms)
    {
        if(!err_detect)
        {
            APP_PRINT("Success\r\n");

            return FSP_SUCCESS;
        }
        else
        {
            APP_PRINT("Error (0x%x)\r\n", cec_err_type);

            return FSP_ERR_ASSERTION;
        }
    }
    else
    {
        APP_PRINT("Timeout\r\n", cec_err_type);

        return FSP_ERR_TIMEOUT;
    }
}

fsp_err_t cec_logical_address_allocate(void)
{
    fsp_err_t fsp_err = FSP_SUCCESS;
    bool logical_address_allocate = false;

    APP_PRINT(CEC_LOGICAL_DEVICE_SELECT_MENU);

    while(1)
    {
        if(APP_CHECK_DATA)
        {
            /* Read data from RTT buffer */
            uint8_t rtt_read_data_c;
            SEGGER_RTT_Read(0, &rtt_read_data_c, 1);

            if(('1' <= rtt_read_data_c) && (rtt_read_data_c <= '5'))
            {
                switch(rtt_read_data_c)
                {
                    case '1':
                    {
                        fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_TV);
                        if(fsp_err == FSP_SUCCESS)
                        {
                            logical_address_allocate = true;
                            my_logical_address = CEC_ADDR_TV;
                        }
                        break;
                    }
                    case '2':
                    {
                        fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_RECORDING_DEVICE_1);
                        if(fsp_err == FSP_SUCCESS)
                        {
                            logical_address_allocate = true;
                            my_logical_address = CEC_ADDR_RECORDING_DEVICE_1;
                        }

                        if(logical_address_allocate == false)
                        {
                            fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_RECORDING_DEVICE_2);
                            if(fsp_err == FSP_SUCCESS)
                            {
                                logical_address_allocate = true;
                                my_logical_address = CEC_ADDR_RECORDING_DEVICE_2;
                            }
                        }

                        if(logical_address_allocate == false)
                        {
                            fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_RECORDING_DEVICE_3);
                            if(fsp_err == FSP_SUCCESS)
                            {
                                logical_address_allocate = true;
                                my_logical_address = CEC_ADDR_RECORDING_DEVICE_3;
                            }
                        }
                        break;
                    }
                    case '3':
                    {
                        fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_TUNER_1);
                        if(fsp_err == FSP_SUCCESS)
                        {
                            logical_address_allocate = true;
                            my_logical_address = CEC_ADDR_TUNER_1;
                        }

                        if(logical_address_allocate == false)
                        {
                            fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_TUNER_2);
                            if(fsp_err == FSP_SUCCESS)
                            {
                                logical_address_allocate = true;
                                my_logical_address = CEC_ADDR_TUNER_2;
                            }
                        }

                        if(logical_address_allocate == false)
                        {
                            fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_TUNER_3);
                            if(fsp_err == FSP_SUCCESS)
                            {
                                logical_address_allocate = true;
                                my_logical_address = CEC_ADDR_TUNER_3;
                            }
                        }

                        if(logical_address_allocate == false)
                        {
                            fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_TUNER_4);
                            if(fsp_err == FSP_SUCCESS)
                            {
                                logical_address_allocate = true;
                                my_logical_address = CEC_ADDR_TUNER_4;
                            }
                        }
                        break;
                    }
                    case '4':
                    {
                        fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_PLAYBACK_DEVICE_1);
                        if(fsp_err == FSP_SUCCESS)
                        {
                            logical_address_allocate = true;
                            my_logical_address = CEC_ADDR_PLAYBACK_DEVICE_1;
                        }

                        if(logical_address_allocate == false)
                        {
                            fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_PLAYBACK_DEVICE_2);
                            if(fsp_err == FSP_SUCCESS)
                            {
                                logical_address_allocate = true;
                                my_logical_address = CEC_ADDR_PLAYBACK_DEVICE_2;
                            }
                        }

                        if(logical_address_allocate == false)
                        {
                            fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_PLAYBACK_DEVICE_3);
                            if(fsp_err == FSP_SUCCESS)
                            {
                                logical_address_allocate = true;
                                my_logical_address = CEC_ADDR_PLAYBACK_DEVICE_3;
                            }
                        }
                        break;
                    }
                    case '5':
                    {
                        fsp_err = cec_logical_address_allocate_attempt(CEC_ADDR_AUDIO_SYSTEM);
                        if(fsp_err == FSP_SUCCESS)
                        {
                            logical_address_allocate = true;
                            my_logical_address = CEC_ADDR_AUDIO_SYSTEM;
                        }
                        break;
                    }
                    default:
                    {
                        /* Do nothing */
                        break;
                    }
                }

                if(logical_address_allocate == false)
                {
                    APP_PRINT("The Selected logical address type is in use by another device. Enter another one.\r\n");
                }
            }
            else if((rtt_read_data_c == '\r') || (rtt_read_data_c == '\n'))
            {
                /* Do nothing */
            }
            else
            {
                APP_PRINT("Invalid input.\r\n");
            }
        }

        if(logical_address_allocate)
        {
            break;
        }
    }

    if(logical_address_allocate)
    {
        cec_bus_device_list[my_logical_address].is_device_active = true;
        cec_bus_device_list[my_logical_address].is_my_device = true;
        APP_PRINT("Logical address %s has been allocated.\r\n", cec_logical_device_list[my_logical_address]);
        return FSP_SUCCESS;
    }
    else
    {
        return FSP_ERR_IN_USE;
    }
}

fsp_err_t cec_logical_address_allocate_attempt(cec_addr_t logical_addr)
{
    fsp_err_t fsp_err = FSP_SUCCESS;
    cec_status_t cec_status;
    uint32_t timeout_ms = 100;

    do{
        fsp_err = R_CEC_MediaInit(&g_cec0_ctrl, logical_addr);
    }while(FSP_ERR_IN_USE == fsp_err);
    if(FSP_SUCCESS != fsp_err){ ERROR_INDICATE_LED_ON; __BKPT(0); }

    /* Wait for local address allocation and CEC bus to be free */
    do{
        fsp_err = R_CEC_StatusGet(&g_cec0_ctrl, &cec_status);
        timeout_ms--;
        if(timeout_ms == 0)
        {
            break;
        }
        R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
    }while((FSP_SUCCESS == fsp_err) && (CEC_STATE_READY != cec_status.state));

    if(CEC_STATE_READY != cec_status.state)
    {
        return FSP_ERR_IN_USE;
    }

    return FSP_SUCCESS;
}

void cec_system_audio_mode_support_enabling(void)
{
    system_audio_mode_support_function = !system_audio_mode_support_function;

    if(system_audio_mode_support_function)
    {
        APP_PRINT("System Audio mode function support enabled.\r\n");
    }
    else
    {
        if(system_audio_mode_status)
        {
            uint8_t    cec_data[CEC_DATA_BUFFER_LENGTH];

            cec_data[0] = CEC_SYSTEM_AUDIO_STATUS_OFF;
            cec_message_send(CEC_ADDR_TV, CEC_OPCODE_SET_SYSTEM_AUDIO_MODE, &cec_data[0], 1);

            APP_PRINT("System Audio mode is disabled.\r\n");
        }

        APP_PRINT("System Audio mode function support disabled.\r\n");
    }
}

void cec_system_audio_mode_request(void)
{
    fsp_err_t fsp_err = FSP_SUCCESS;
    bool       active_source_find = false;
    uint8_t    cec_data[CEC_DATA_BUFFER_LENGTH];

    /* Enable System Audio mode support */
    system_audio_mode_support_function = true;

    if(system_audio_mode_status == false)
    {
        /* The Active source must be checked to request System Audio mode */
        for(int i=0; i<12; i++)
        {
            if(cec_bus_device_list[i].is_active_source)
            {
                APP_PRINT("Current active source is %s.\r\n", &cec_logical_device_list[i]);
                active_source_find = true;
                break;
            }
        }

        if(active_source_find)
        {
            /* Disable System Audio mode once */
            cec_data[0] = CEC_SYSTEM_AUDIO_STATUS_OFF;
            cec_message_send(CEC_ADDR_TV, CEC_OPCODE_SET_SYSTEM_AUDIO_MODE, &cec_data[0], 1);

            R_BSP_SoftwareDelay(300, BSP_DELAY_UNITS_MILLISECONDS);

            APP_PRINT("Sending System Audio On request ...\r\n");

            cec_data[0] = CEC_SYSTEM_AUDIO_STATUS_ON;
            fsp_err = cec_message_send(CEC_ADDR_TV, CEC_OPCODE_SET_SYSTEM_AUDIO_MODE, &cec_data[0], 1);
            if(FSP_SUCCESS == fsp_err)
            {
                APP_PRINT("System Audio mode is disabled.\r\n");
                system_audio_mode_status = true;
            }
            else
            {
                APP_PRINT("System Audio mode request failed.\r\n");
                system_audio_mode_status = false;
            }
        }
        else
        {
            APP_PRINT("Active source is not found. Try bus scan to get.\r\n");
        }
    }
    else
    {
        cec_data[0] = CEC_SYSTEM_AUDIO_STATUS_OFF;
        cec_message_send(CEC_ADDR_TV, CEC_OPCODE_SET_SYSTEM_AUDIO_MODE, &cec_data[0], 1);

        APP_PRINT("System Audio mode is disabled.\r\n");

        system_audio_mode_status = false;
    }

}

void cec_rx_data_check(void)
{
    static uint8_t buff_read_point = 0;
    cec_rx_message_buff_t* p_buff;

    for(uint32_t i=0; i<CEC_RX_DATA_BUFF_DATA_NUMBER; i++)
    {
        p_buff = &cec_rx_data_buff[buff_read_point];

        if(p_buff->is_new_data)
        {
            if(p_buff->is_error == false)
            {
                APP_PRINT("[< CEC In]  Src: %d (%s), Dest: %d (%s),\r\n", p_buff->source, &cec_logical_device_list[p_buff->source][0],
                          p_buff->destination, &cec_logical_device_list[p_buff->destination][0]);
                if(p_buff->byte_counter >= 2)
                {
                    uint32_t opcode_list_point = opcode_description_find(p_buff->opcode);
                    APP_PRINT("            Opcode: 0x%x (%s)", p_buff->opcode, &cec_opcode_list[opcode_list_point].opcode_desc_str[0]);

                    if(p_buff->byte_counter >= 3)
                    {
                        APP_PRINT(", Data: ");
                        for(int j=0; j<(p_buff->byte_counter-2); j++)
                        {
                            APP_PRINT("0x%x,", p_buff->data_buff[j]);
                        }
                    }

                    APP_PRINT("\r\n");

                    if(p_buff->source != my_logical_address)
                    {
                        switch(p_buff->opcode)
                        {
                            case CEC_OPCODE_IMAGE_VIEW_ON:
                            {
                                cec_action_request_detect_flag = true;
                                cec_action_type = CEC_ACTION_POWER_ON;
                                break;
                            }
                            case CEC_OPCODE_STANDBY:
                            {
                                cec_action_request_detect_flag = true;
                                cec_action_type = CEC_ACTION_POWER_OFF;
                                break;
                            }
                            case CEC_OPCODE_USER_CONTROL_PRESSED:
                            {
                                switch(p_buff->data_buff[0])
                                {
                                    case USER_CONTROL_VOLUME_UP:
                                        cec_action_request_detect_flag = true;
                                        cec_action_type = CEC_ACTION_VOLUME_UP;
                                        break;
                                    case USER_CONTROL_VOLUME_DOWN:
                                        cec_action_request_detect_flag = true;
                                        cec_action_type = CEC_ACTION_VOLUME_DOWN;
                                        break;
                                    case USER_CONTROL_MUTE:
                                        cec_action_request_detect_flag = true;
                                        cec_action_type = CEC_ACTION_VOLUME_MUTE;
                                        break;
                                    default:
                                        /* Do nothing */
                                        break;
                                }
                                break;
                            }
//                            case <opcode> ToDo
//                            {
//                                /* Add your additional operation */
//                                break;
//                            }
                            default:
                            {
                                /* Auto response to supporting (sysytem-level) commands */
                                cec_system_auto_response(p_buff);
                                break;
                            }
                        }
                    }
                    else
                    {
                        APP_PRINT("Logical address of received message is same as my logical address. Ignore this message.\r\n");
                    }
                }
            }

            /* Clear the data */
            memset(p_buff, 0x0, sizeof(cec_rx_message_buff_t));

            buff_read_point++;
            if(buff_read_point == CEC_RX_DATA_BUFF_DATA_NUMBER)
            {
                buff_read_point = 0;
            }
        }

        if(cec_action_request_detect_flag == true)
        {
            break;
        }
    }
}

void cec_system_auto_response(cec_rx_message_buff_t const * p_rx_data)
{
    fsp_err_t fsp_err = FSP_SUCCESS;
    uint8_t cec_data[CEC_DATA_BUFFER_LENGTH] = {0x0};

    switch(p_rx_data->opcode)
    {
        case CEC_OPCODE_GIVE_PHYSICAL_ADDRESS:
        { /* Give Physical Address (0x83) => Report Physical Address */
            if(cec_bus_device_list[my_logical_address].is_physical_address_store)
            {
                cec_data[0] = (uint8_t)((cec_bus_device_list[my_logical_address].physical_address[3] << 4) | cec_bus_device_list[my_logical_address].physical_address[2]);
                cec_data[1] = (uint8_t)((cec_bus_device_list[my_logical_address].physical_address[1] << 4) | cec_bus_device_list[my_logical_address].physical_address[0]);
            }
            else
            {
                cec_data[0] = (uint8_t)((my_physical_address[3] << 4) | my_physical_address[2]);
                cec_data[1] = (uint8_t)((my_physical_address[1] << 4) | my_physical_address[0]);
            }

            cec_device_type_t device_type = convert_logical_address_to_device_type(my_logical_address);
            if(device_type != CEC_DEVICE_TYPE_UNKNOWN)
            {
                cec_data[2] = device_type;
            }

            cec_message_send(CEC_ADDR_BROADCAST, CEC_OPCODE_REPORT_PHYSICAL_ADDRESS, &cec_data[0], 3);

            break;
        }
        case CEC_OPCODE_GIVE_DEVICE_VENDOR_ID:
        { /* Give Device Vendor ID (0x8C) => Device Vendor ID */
            if(cec_bus_device_list[my_logical_address].is_vendor_id_store)
            {
                cec_message_send(CEC_ADDR_BROADCAST, CEC_OPCODE_DEVICE_VENDOR_ID, &cec_bus_device_list[my_logical_address].vendor_id[0], 3);
            }
            else
            {
                cec_message_send(CEC_ADDR_BROADCAST, CEC_OPCODE_DEVICE_VENDOR_ID, &my_vendor_id[0], 3);
            }
            break;
        }
        case CEC_OPCODE_GIVE_OSD_NAME:
        { /* Give OSD Name (0x46) => Set OSD Name */
            cec_message_send(p_rx_data->source, CEC_OPCODE_SET_OSD_NAME, &my_osd_name[0], MY_OSD_NAME_LENGTH);
            break;
        }
        case CEC_OPCODE_GIVE_POWER_STATUS:
        { /* Give Device Power Status (0x8F) => Report Power Status */
            if(cec_bus_device_list[my_logical_address].power_status == 0x1)
            {
                cec_data[0] = CEC_POWER_STATUS_ON;
            }
            else
            {
                cec_data[0] = CEC_POWER_STATUS_STANDBY;
            }
            cec_message_send(p_rx_data->source, CEC_OPCODE_REPORT_POWER_STATUS, &cec_data[0], 1);
            break;
        }
        case CEC_OPCODE_GIVE_AUDIO_STATUS:
        { /* Give Audio Status (0x7A) => Report Audio Status */
            bool    mute;
            uint8_t volume;
            demo_system_volume_status_get(&mute, &volume);

            cec_data[0] = (uint8_t)((mute << 7) | (volume & 0x7F));

            cec_message_send(p_rx_data->source, CEC_OPCODE_REPORT_AUDIO_STATUS, &cec_data[0], 1);
            break;
        }
        case CEC_OPCODE_SYSTEM_AUDIO_MODE_REQUEST:
        { /* System Audio Mode Request (0x70) => If message has active source address, accept system audio mode enabling. */
            if(system_audio_mode_support_function)
            {
                /* If data field is filled, this means System Audio Mode is requested to be turned On. Otherwise, requested to be off */
                if(p_rx_data->byte_counter >= 3)
                {
                    cec_data[0] = CEC_SYSTEM_AUDIO_STATUS_ON;
                }
                else
                {
                    cec_data[0] = CEC_SYSTEM_AUDIO_STATUS_OFF;
                }

                fsp_err = cec_message_send(CEC_ADDR_TV, CEC_OPCODE_SET_SYSTEM_AUDIO_MODE, &cec_data[0], 1);
                if(FSP_SUCCESS == fsp_err)
                {
                    if(cec_data[0] == CEC_SYSTEM_AUDIO_STATUS_ON)
                    {
                        APP_PRINT("System Audio mode is enabled by TV.\r\n");
                        system_audio_mode_status = true;
                    }
                    else
                    {
                        APP_PRINT("System Audio mode is disabled by TV.\r\n");
                        system_audio_mode_status = false;
                    }
                }
            }
            else
            {
                APP_PRINT("Received System Audio mode request. But function is not enabled, so reject it.\r\n");
                system_audio_mode_status = false;

                cec_data[0] = CEC_SYSTEM_AUDIO_STATUS_OFF;
                cec_message_send(CEC_ADDR_TV, CEC_OPCODE_SET_SYSTEM_AUDIO_MODE, &cec_data[0], 1);
            }
            break;
        }
        case CEC_OPCODE_GIVE_SYSTEM_AUDIO_MODE_STATUS:
        { /* Give System Audio Mode Status (0x7D) => System Audio Mode Status (0x7E) */
            if(system_audio_mode_status)
            {
                cec_data[0] = 0x1;
            }
            else
            {
                cec_data[0] = 0x0;
            }

            cec_message_send(p_rx_data->source, CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS, &cec_data[0], 1);
            break;
        }
        case CEC_OPCODE_SYSTEM_AUDIO_MODE_STATUS:
        { /* System Audio Mode Status (0x7E) => (Internal data update) */
            if(p_rx_data->byte_counter >= 3)
            {
                if(p_rx_data->data_buff[0] == 0x1)
                {
                    APP_PRINT("System Audio mode is enabled.\r\n");
                    system_audio_mode_status = true;
                }
                else
                {
                    APP_PRINT("System Audio mode is disabled.\r\n");
                    system_audio_mode_status = false;
                }
            }
            break;
        }
        case CEC_OPCODE_REPORT_PHYSICAL_ADDRESS:
        { /* Report Physical Address (0x84) => (Internal buffer update) */
            if(p_rx_data->source != CEC_ADDR_UNREGISTERED)
            {
                /* Raise device active flag */
                cec_bus_device_list[p_rx_data->source].is_device_active = true;

                /* Store to internal buffer */
                cec_bus_device_list[p_rx_data->source].is_physical_address_store = true;
                cec_bus_device_list[p_rx_data->source].physical_address[3] = (uint8_t)(p_rx_data->data_buff[0] >> 4);
                cec_bus_device_list[p_rx_data->source].physical_address[2] = (uint8_t)(p_rx_data->data_buff[0] & 0x0F);
                cec_bus_device_list[p_rx_data->source].physical_address[1] = (uint8_t)(p_rx_data->data_buff[1] >> 4);
                cec_bus_device_list[p_rx_data->source].physical_address[0] = (uint8_t)(p_rx_data->data_buff[1] & 0x0F);
            }
            break;
        }
        case CEC_OPCODE_CEC_VERSION:
        { /* CEC Version (0x9E) => (Internal buffer update) */
            if(p_rx_data->source != CEC_ADDR_UNREGISTERED)
            {
                /* Raise device active flag */
                cec_bus_device_list[p_rx_data->source].is_device_active = true;

                /* Store to internal buffer */
                cec_bus_device_list[p_rx_data->source].is_version_store = true;
                cec_bus_device_list[p_rx_data->source].cec_version = p_rx_data->data_buff[0];
            }
            break;
        }
        case CEC_OPCODE_REPORT_POWER_STATUS:
        { /* Report Power Status (0x90) => (Internal buffer update) */
            if(p_rx_data->source != CEC_ADDR_UNREGISTERED)
            {
                /* Raise device active flag */
                cec_bus_device_list[p_rx_data->source].is_device_active = true;

                /* Store to internal buffer */
                cec_bus_device_list[p_rx_data->source].is_power_status_store = true;
                if((p_rx_data->data_buff[0] == CEC_POWER_STATUS_ON) || (p_rx_data->data_buff[0] == CEC_POWER_STATUS_IN_TRANSITION_TO_ON))
                {
                    cec_bus_device_list[p_rx_data->source].power_status = 0x1;
                }
                else if((p_rx_data->data_buff[0] == CEC_POWER_STATUS_STANDBY) || (p_rx_data->data_buff[0] == CEC_POWER_STATUS_IN_TRANSITION_TO_STANDBY))
                {
                    cec_bus_device_list[p_rx_data->source].power_status = 0x0;
                }
            }
            break;
        }
        case CEC_OPCODE_ACTIVE_SOURCE:
        { /* Active Source (0x82) => (Internal buffer update) */
            if(p_rx_data->source != CEC_ADDR_UNREGISTERED)
            {
                /* Raise device active flag */
                cec_bus_device_list[p_rx_data->source].is_device_active = true;

                /* Clear is_active_source flag for all devices once */
                for(int i=0; i<12; i++)
                {
                    cec_bus_device_list[i].is_active_source = false;
                }

                /* Set a flag for current active source device */
                cec_bus_device_list[p_rx_data->source].is_active_source = true;
            }
            break;
        }
        case CEC_OPCODE_DEVICE_VENDOR_ID:
        { /* Vendor ID (0x87) => (Internal buffer update) */
            if(p_rx_data->source != CEC_ADDR_UNREGISTERED)
            {
                /* Raise device active flag */
                cec_bus_device_list[p_rx_data->source].is_device_active = true;

                /* Store to internal buffer */
                cec_bus_device_list[p_rx_data->source].is_vendor_id_store = true;
                memcpy(&cec_bus_device_list[p_rx_data->source].vendor_id[0], &p_rx_data->data_buff[0], 3);
            }
            break;
        }
//        case <opcode>
//        {
//            /* Add your additional operation */
//            break;
//        }
        default:
        { /* Opcode that cannot be response => Feature Abort */
            if(p_rx_data->destination != CEC_ADDR_BROADCAST)
            {
                if(!((p_rx_data->opcode == CEC_OPCODE_FEATURE_ABORT) | (p_rx_data->opcode == CEC_OPCODE_ABORT)))
                {
                    cec_data[0] = p_rx_data->opcode;
                    cec_data[1] = CEC_ABOUT_REASON_UNRECOFNIZED_OPCODE;
                    cec_message_send(p_rx_data->source, CEC_OPCODE_FEATURE_ABORT, &cec_data[0], 2);
                }
            }
            break;
        }
    }
}

void cec_bus_scan(void)
{
    /* Request physical address to all devices sequentially */
    APP_PRINT("Requesting physical address ...\r\n");
    for(int i=0; i<12; i++)
    {
        if(i != my_logical_address)
        {
            cec_message_send(i, CEC_OPCODE_GIVE_PHYSICAL_ADDRESS, NULL, 0);

            R_BSP_SoftwareDelay(400, BSP_DELAY_UNITS_MILLISECONDS);
        }
    }

    /* Request vendor id to all devices sequentially */
    APP_PRINT("Requesting vendor id ...\r\n");
    for(int i=0; i<12; i++)
    {
        if(i != my_logical_address)
        {
            cec_message_send(i, CEC_OPCODE_GIVE_DEVICE_VENDOR_ID, NULL, 0);

            R_BSP_SoftwareDelay(400, BSP_DELAY_UNITS_MILLISECONDS);
        }
    }

    /* Request CEC version to all devices sequentially */
    APP_PRINT("Requesting CEC version ...\r\n");
    for(int i=0; i<12; i++)
    {
        if(i != my_logical_address)
        {
            cec_message_send(i, CEC_OPCODE_GET_CEC_VERSION, NULL, 0);

            R_BSP_SoftwareDelay(400, BSP_DELAY_UNITS_MILLISECONDS);
        }
    }

    /* Request Power status to all devices sequentially */
    APP_PRINT("Requesting power status ...\r\n");
    for(int i=0; i<12; i++)
    {
        if(i != my_logical_address)
        {
            cec_message_send(i, CEC_OPCODE_GIVE_POWER_STATUS, NULL, 0);

            R_BSP_SoftwareDelay(400, BSP_DELAY_UNITS_MILLISECONDS);
        }
    }

    /* Request Active Source to broadcast */
    APP_PRINT("Requesting active source ...\r\n");
    cec_message_send(CEC_ADDR_BROADCAST, CEC_OPCODE_REQUEST_ACTIVE_SOURCE, NULL, 0);
}

void cec_bus_status_buffer_display(void)
{
    for(int i=0; i<15; i++)
    {
        if(cec_bus_device_list[i].is_device_active)
        {
            cec_device_status_display(i, &cec_bus_device_list[i]);
        }
    }
}

void R_BSP_WarmStart(bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_POST_C == event)
    {
        /* Configure pins. */
        R_IOPORT_Open (&g_ioport_ctrl, &IOPORT_CFG_NAME);
    }
}
