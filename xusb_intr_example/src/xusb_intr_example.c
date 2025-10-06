/******************************************************************************
 *
 * Copyright (C) 2017 Xilinx, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Except as contained in this notice, the name of the Xilinx shall not be used
 * in advertising or otherwise to promote the sale, use or other dealings in
 * this Software without prior written authorization from Xilinx.
 *
 ******************************************************************************/
/****************************************************************************/
/**
 *
 * @file xusb_intr_example.c
 *
 * This file implements the mass storage class example.
 *
 * <pre>
 * MODIFICATION HISTORY:
 *
 * Ver   Who  Date     Changes
 * ----- ---- -------- -------------------------------------------------------
 * 1.0   sg  06/06/16 First release
 *       ms  04/10/17 Modified filename tag to include the file in doxygen
 *                    examples.
 * 1.4   BK  12/01/18 Renamed the file and added changes to have a common
 *		      example for all USB IPs.
 *	 vak 22/01/18 Added changes for supporting microblaze platform
 *	 vak 13/03/18 Moved the setup interrupt system calls from driver to
 *		      example.
 * </pre>
 *
 *****************************************************************************/

/***************************** Include Files ********************************/
#include "xparameters.h"
#include "xil_printf.h"
#include "sleep.h"
#include <stdio.h>
#include "xusb_ch9_storage.h"
#include "xusb_class_storage.h"
#include "xusb_wrapper.h"
#include "xil_exception.h"
#include "aes_demo.h"

#ifdef __MICROBLAZE__
#ifdef XPAR_INTC_0_DEVICE_ID
#include "xintc.h"
#endif /* XPAR_INTC_0_DEVICE_ID */
#elif defined PLATFORM_ZYNQMP
#include "xscugic.h"
#endif

/************************** Constant Definitions ****************************/
#define MEMORY_SIZE (64 * 1024)
#ifdef __ICCARM__
#ifdef PLATFORM_ZYNQMP
#pragma data_alignment = 64
#else
#pragma data_alignment = 32
#endif
u8 Buffer[MEMORY_SIZE];
#pragma data_alignment = 4
#else
u8 Buffer[MEMORY_SIZE] ALIGNMENT_CACHELINE;
#endif

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/
void BulkOutHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed);
void BulkInHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed);
void EpCtrlOutHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed);
void EpCtrlInHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed);
void EpIntrInHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed);

static s32 SetupInterruptSystem(struct XUsbPsu *InstancePtr, u16 IntcDeviceID,
		u16 USB_INTR_ID, void *IntcPtr);

/************************** Variable Definitions *****************************/
struct Usb_DevData UsbInstance;

Usb_Config *UsbConfigPtr;

#ifdef __MICROBLAZE__
#ifdef XPAR_INTC_0_DEVICE_ID
XIntc	InterruptController;	/*XIntc interrupt controller instance */
#endif /* XPAR_INTC_0_DEVICE_ID */
#else
XScuGic	InterruptController;	/* Interrupt controller instance */
#endif

#ifdef __MICROBLAZE__		/* MICROBLAZE */
#ifdef	XPAR_INTC_0_DEVICE_ID
#define	INTC_DEVICE_ID		XPAR_INTC_0_DEVICE_ID
#define	USB_INT_ID		XPAR_AXI_INTC_0_ZYNQ_ULTRA_PS_E_0_PS_PL_IRQ_USB3_0_ENDPOINT_0_INTR
#endif /* MICROBLAZE */
#elif	defined	PLATFORM_ZYNQMP	/* ZYNQMP */
#define	INTC_DEVICE_ID		XPAR_SCUGIC_SINGLE_DEVICE_ID
#define	USB_INT_ID		XPAR_XUSBPS_0_INTR
#define	USB_WAKEUP_INTR_ID	XPAR_XUSBPS_0_WAKE_INTR
#else	/* OTHERS */
#define	INTC_DEVICE_ID		0
#define	USB_INT_ID		0
#endif

/* Buffer for virtual flash disk space. */
#ifdef __ICCARM__
#ifdef PLATFORM_ZYNQMP
#pragma data_alignment = 64
#else
#pragma data_alignment = 32
#endif
u8 VirtFlash[VFLASH_SIZE];
USB_CBW CBW;
USB_CSW CSW;
#pragma data_alignment = 4
#else
u8 VirtFlash[VFLASH_SIZE] ALIGNMENT_CACHELINE;
USB_CBW CBW ALIGNMENT_CACHELINE;
USB_CSW CSW ALIGNMENT_CACHELINE;
#endif

u8 Phase;
u32	rxBytesLeft;
u8 *VirtFlashWritePointer = VirtFlash;
u32 ReplyLen;

#define CMD_HEADER_SIZE 3 
#define CRC_SIZE 2

/* AES token context & key */
static TokenContext token;
static const uint8_t device_key[AES_KEY_SIZE] = {
    0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
    0xab,0xf7,0x97,0x75,0x46,0x7e,0xf3,0x1c
};

/* Initialize a DFU data structure */
static USBCH9_DATA storage_data = {
		.ch9_func = {
				/* Set the chapter9 hooks */
				.Usb_Ch9SetupDevDescReply =	Usb_Ch9SetupDevDescReply,
				.Usb_Ch9SetupCfgDescReply =	Usb_Ch9SetupCfgDescReply,
				.Usb_Ch9SetupBosDescReply =	Usb_Ch9SetupBosDescReply,
				.Usb_Ch9SetupStrDescReply =	Usb_Ch9SetupStrDescReply,
				.Usb_SetConfiguration 	  =	Usb_SetConfiguration,
				.Usb_SetConfigurationApp  = Usb_SetConfigurationApp,
				/* hook the set interface handler */
				.Usb_SetInterfaceHandler = NULL,
				/* hook up storage class handler */
				.Usb_ClassReq = ClassReq,
				.Usb_GetDescReply = NULL,
		},
		.data_ptr = (void *)NULL,
};

u8 xx = 0;
u32 inData = 0;
u32 outData = 0;
//static u8 Reply[USB_REQ_REPLY_LEN] ALIGNMENT_CACHELINE;

/****************************************************************************/
/**
* This function is the main function of the USB mass storage example.
*
* @param	None.
*
* @return
*		- XST_SUCCESS if successful,
*		- XST_FAILURE if unsuccessful.
*
* @note		None.
*
*
*****************************************************************************/
int main(void)
{
	s32 Status;

	xil_printf("\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n");
	xil_printf("CDC ACM Gadget Start...\r\n");

	/* Initialize the USB driver so that it's ready to use,
	 * specify the controller ID that is generated in xparameters.h
	 */
	UsbConfigPtr = LookupConfig(USB_DEVICE_ID);
	if (NULL == UsbConfigPtr) {
		return XST_FAILURE;
	}

	CacheInit();

	/* We are passing the physical base address as the third argument
	 * because the physical and virtual base address are the same in our
	 * example.  For systems that support virtual memory, the third
	 * argument needs to be the virtual base address.
	 */
	Status = CfgInitialize(&UsbInstance, UsbConfigPtr, UsbConfigPtr->BaseAddress);
	if (XST_SUCCESS != Status) {
		return XST_FAILURE;
	}

	/* hook up chapter9 handler */
	Set_Ch9Handler(UsbInstance.PrivateData, Ch9Handler);

	/* Assign the data to usb driver */
	Set_DrvData(UsbInstance.PrivateData, &storage_data);

	EpConfigure(UsbInstance.PrivateData, 0, USB_EP_DIR_OUT,  USB_EP_TYPE_CONTROL);	// To Device
	EpConfigure(UsbInstance.PrivateData, 0, USB_EP_DIR_IN,   USB_EP_TYPE_CONTROL);	// To Host

	EpConfigure(UsbInstance.PrivateData, 1, USB_EP_DIR_IN,  USB_EP_TYPE_INTERRUPT); // To Host

	EpConfigure(UsbInstance.PrivateData, 2, USB_EP_DIR_OUT, USB_EP_TYPE_BULK);		// To Device
	EpConfigure(UsbInstance.PrivateData, 2, USB_EP_DIR_IN,  USB_EP_TYPE_BULK);		// To Host

	Status = ConfigureDevice(UsbInstance.PrivateData, &Buffer[0], MEMORY_SIZE);
	if (XST_SUCCESS != Status) {
		return XST_FAILURE;
	}

	/*
	 * set endpoint handlers
	 * BulkOutHandler - to be called when data is received
	 * BulkInHandler -  to be called when data is sent
	 */
	SetEpHandler(UsbInstance.PrivateData, 0, USB_EP_DIR_OUT, EpCtrlOutHandler);
	SetEpHandler(UsbInstance.PrivateData, 0, USB_EP_DIR_IN, EpCtrlInHandler);

	SetEpHandler(UsbInstance.PrivateData, 1, USB_EP_DIR_IN,  EpIntrInHandler);

	SetEpHandler(UsbInstance.PrivateData, 2, USB_EP_DIR_OUT, BulkOutHandler);
	SetEpHandler(UsbInstance.PrivateData, 2, USB_EP_DIR_IN,  BulkInHandler);

	/* setup interrupts */
	Status = SetupInterruptSystem(UsbInstance.PrivateData, INTC_DEVICE_ID, USB_INT_ID, (void *)&InterruptController);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* 5. Initialize AES token */
	token_init(&token, "usb_user", device_key);
	xil_printf("AES initialized for token user 'usb_user'\r\n");

	/* Start the controller so that Host can see our device */
	Usb_Start(UsbInstance.PrivateData);

	while(1) {
		/* Rest is taken care by interrupts */
	}

	return XST_SUCCESS;
}

void EpCtrlOutHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed)
{
	xil_printf("     To Device\r\n");

	struct Usb_DevData *InstancePtr = CallBackRef;
	SetupPacket *rcvdataDrv = (SetupPacket *)Get_DrvData(InstancePtr);
	printf("\r\n");
	printf("    ------->>> bmRequestType 0x%x\r\n", rcvdataDrv->bRequestType);
	printf("    ------->>> bRequest      0x%x\r\n", rcvdataDrv->bRequest);
	printf("    ------->>> wValue        0x%x\r\n", rcvdataDrv->wValue);
	printf("    ------->>> wIndex        0x%x\r\n", rcvdataDrv->wIndex);
	printf("    ------->>> wLength       0x%x\r\n", rcvdataDrv->wLength);

}
void EpCtrlInHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed)
{
	xil_printf("     To Host\r\n");

	struct Usb_DevData *InstancePtr = CallBackRef;
	SetupPacket *rcvdataDrv = (SetupPacket *)Get_DrvData(InstancePtr);
	printf("\r\n");
	printf("    ------->>> bmRequestType 0x%x\r\n", rcvdataDrv->bRequestType);
	printf("    ------->>> bRequest      0x%x\r\n", rcvdataDrv->bRequest);
	printf("    ------->>> wValue        0x%x\r\n", rcvdataDrv->wValue);
	printf("    ------->>> wIndex        0x%x\r\n", rcvdataDrv->wIndex);
	printf("    ------->>> wLength       0x%x\r\n", rcvdataDrv->wLength);

}
void EpIntrInHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed)
{
	xil_printf("EpIntrInHandler\r\n");

}


/****************************************************************************/
/**
* This function is Bulk Out Endpoint handler/Callback called by driver when
* data is received.
*
* @param	CallBackRef is pointer to Usb_DevData instance.
* @param	RequestedBytes is number of bytes requested for reception.
* @param	BytesTxed is actual number of bytes received from Host.
*
* @return	None
*
* @note		None.
*
*****************************************************************************/
void BulkOutHandler(void *CallBackRef, u32 RequestedBytes, u32 BytesTxed)
{
	// xil_printf("Receivede data from PC \r\n");

	// struct Usb_DevData *InstancePtr = CallBackRef;
	// VirtFlash[10] = '\0';

	// xil_printf("Receivede data from PC : %d - %s \r\n", inData++, VirtFlash);

	// // send out the rcvd data
	// EpBufferSend(InstancePtr->PrivateData, 2, VirtFlash, 10);
//v1.0
	// struct Usb_DevData *InstancePtr = CallBackRef;

    // xil_printf("\r\n[BulkOutHandler] Received %lu bytes from PC\r\n", BytesTxed);

    // // 🆕 [THÊM MỚI] Chuẩn bị buffer cho mã hoá AES
    // uint8_t plaintext[AES_BLOCK_SIZE] = {0};
    // uint8_t ciphertext[AES_BLOCK_SIZE] = {0};

    // // 🆕 [THÊM MỚI] Copy tối đa 16 byte từ VirtFlash (padding nếu thiếu)
    // u32 len = (BytesTxed > AES_BLOCK_SIZE) ? AES_BLOCK_SIZE : BytesTxed;
    // memcpy(plaintext, VirtFlash, len);

    // // 🆕 [THÊM MỚI] Mã hoá AES block
    // memcpy(token.plaintext, plaintext, AES_BLOCK_SIZE);
    // memcpy(token.ciphertext, token.plaintext, AES_BLOCK_SIZE);
    // AES_encrypt(&token.aes, token.ciphertext);

    // xil_printf("Ciphertext: ");
    // for (int i = 0; i < AES_BLOCK_SIZE; i++)
    //     xil_printf("%02x ", token.ciphertext[i]);
    // xil_printf("\r\n");

    // // 🆕 [THÊM MỚI] Gửi ciphertext về lại PC
    // EpBufferSend(InstancePtr->PrivateData, 2, token.ciphertext, AES_BLOCK_SIZE);
//v1.01
    struct Usb_DevData *InstancePtr = CallBackRef;
    xil_printf("\r\n[BulkOutHandler] Received %lu bytes from PC\r\n", BytesTxed);

    /* minimum header check */
    if (BytesTxed < CMD_HEADER_SIZE) {
        xil_printf("[BulkOutHandler] Packet too short\r\n");
        return;
    }

    uint8_t *buf = VirtFlash; /* driver puts received data into VirtFlash[] */
    uint8_t cmd = buf[0];
    uint16_t payload_len = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);

    /* determine if CRC is present */
    int has_crc = 0;
    if (BytesTxed >= (size_t)(CMD_HEADER_SIZE + payload_len + CRC_SIZE))
        has_crc = 1;

    /* basic bounds check */
    if ((size_t)(CMD_HEADER_SIZE + payload_len) > BytesTxed) {
        xil_printf("[BulkOutHandler] Payload length mismatch: %u vs BytesTxed %lu\r\n", payload_len, BytesTxed);
        return;
    }

    /* optional CRC verification */
    if (has_crc) {
        uint16_t received_crc = (uint16_t)buf[CMD_HEADER_SIZE + payload_len] |
                                ((uint16_t)buf[CMD_HEADER_SIZE + payload_len + 1] << 8);
        uint16_t calc = crc16_ccitt(buf, CMD_HEADER_SIZE + payload_len);
        if (calc != received_crc) {
            xil_printf("[BulkOutHandler] CRC mismatch: calc=0x%04x recv=0x%04x\r\n", calc, received_crc);
            /* send error response: CMD same id, len=1, data=0x00 indicates CRC error */
            uint8_t resp_err[5];
            resp_err[0] = cmd;
            resp_err[1] = 0x01; resp_err[2] = 0x00; /* len = 1 */
            resp_err[3] = 0x00; /* error code */
            uint16_t crc_err = crc16_ccitt(resp_err, 4);
            resp_err[4] = crc_err & 0xFF;
            resp_err[5] = (crc_err >> 8) & 0xFF;
            EpBufferSend(InstancePtr->PrivateData, 2, resp_err, 6);
            return;
        }
    }

    uint8_t *payload = &buf[CMD_HEADER_SIZE];

    /* response buffer (max size choose reasonable e.g., 512) */
    static uint8_t response[512];
    size_t resp_len = 0;

    switch (cmd) {
        case 0x01: /* CMD_GET_INFO */
        {
            const char info[] = "EBOOST-TOKENv1";
            size_t info_len = strlen(info);
            response[0] = cmd;
            response[1] = (uint8_t)(info_len & 0xFF);
            response[2] = (uint8_t)((info_len >> 8) & 0xFF);
            memcpy(&response[3], info, info_len);
            resp_len = 3 + info_len;
            break;
        }

        case 0x02: /* CMD_AUTH_CHALLENGE */
        {
            /* Accept variable length challenge, we'll AES-CBC encrypt it and return */
            /* Pad to 16-byte using PKCS#7 */
            uint8_t local_in[256];
            if (payload_len > 240) { /* keep buffer safe */
                xil_printf("[BulkOutHandler] Challenge too large\n");
                payload_len = 240;
            }
            memcpy(local_in, payload, payload_len);
            size_t padded = pkcs7_pad(local_in, payload_len, AES_BLOCK_SIZE);

            /* use IV = zeros for demo (in production use random IV and send it) */
            uint8_t iv[AES_BLOCK_SIZE] = {0};
            uint8_t local_out[256];
            AES_encrypt_CBC(&token.aes, local_in, local_out, padded, iv);

            response[0] = cmd;
            response[1] = (uint8_t)(padded & 0xFF);
            response[2] = (uint8_t)((padded >> 8) & 0xFF);
            memcpy(&response[3], local_out, padded);
            resp_len = 3 + padded;
            break;
        }

        case 0x03: /* CMD_SIGN_DATA (demo: AES-CBC of data) */
        {
            uint8_t local_in[64];
            if (payload_len > 48) payload_len = 48;
            memcpy(local_in, payload, payload_len);
            size_t padded = pkcs7_pad(local_in, payload_len, AES_BLOCK_SIZE);
            uint8_t iv[AES_BLOCK_SIZE] = {0};
            uint8_t sig[64];
            AES_encrypt_CBC(&token.aes, local_in, sig, padded, iv);

            /* For demo we return ciphertext as "signature", in real device do ECDSA */
            response[0] = cmd;
            response[1] = (uint8_t)(padded & 0xFF);
            response[2] = (uint8_t)((padded >> 8) & 0xFF);
            memcpy(&response[3], sig, padded);
            resp_len = 3 + padded;
            break;
        }

        case 0x04: /* CMD_SET_KEY */
        {
            /* Only accept key length equal AES_KEY_SIZE */
            if (payload_len != AES_KEY_SIZE) {
                /* error */
                response[0] = cmd;
                response[1] = 0x01; response[2] = 0x00;
                response[3] = 0x00; /* fail */
                resp_len = 4;
            } else {
                token_set_key(&token, payload, AES_KEY_SIZE); /* [ADDED] */
                response[0] = cmd;
                response[1] = 0x01; response[2] = 0x00;
                response[3] = 0x01; /* OK */
                resp_len = 4;
            }
            break;
        }

        case 0x05: /* CMD_RESET */
        {
            token_reset(&token); /* wipe all sensitive state */
            response[0] = cmd;
            response[1] = 0x01; response[2] = 0x00;
            response[3] = 0x01; /* OK */
            resp_len = 4;
            break;
        }

        default:
        {
            response[0] = cmd;
            response[1] = 0x01; response[2] = 0x00;
            response[3] = 0xFF; /* unknown command */
            resp_len = 4;
            break;
        }
    } /* end switch */

    /* append CRC16 to response */
    uint16_t crc = crc16_ccitt(response, resp_len);
    response[resp_len + 0] = crc & 0xFF;
    response[resp_len + 1] = (crc >> 8) & 0xFF;
    resp_len += 2;

    /* send via Bulk IN endpoint */
    EpBufferSend(InstancePtr->PrivateData, 2, response, resp_len);

    xil_printf("[BulkOutHandler] Processed CMD 0x%02x, sent %lu bytes\r\n", cmd, resp_len);

}

/****************************************************************************/
/**
* This function is Bulk In Endpoint handler/Callback called by driver when
* data is sent.
*
* @param	CallBackRef is pointer to Usb_DevData instance.
* @param	RequestedBytes is number of bytes requested to send.
* @param	BytesTxed is actual number of bytes sent to Host.
*
* @return	None
*
* @note		None.
*
*****************************************************************************/
void BulkInHandler(void *CallBackRef, u32 RequestedBytes,
						   u32 BytesTxed)
{
//	xil_printf("Send Data OUT - BulkInHandler\r\n");

	struct Usb_DevData *InstancePtr = CallBackRef;

	xil_printf("[BulkInHandler] Sent packet #%lu\r\n", outData++);
	EpBufferRecv(InstancePtr->PrivateData, 2, VirtFlash, 64); // 🆕 [sửa nhẹ] buffer 64B

	// xil_printf("Send Data OUT - BulkInHandler %d \r\n", outData++);

	// EpBufferRecv(InstancePtr->PrivateData, 2, VirtFlash, 10);

}

/****************************************************************************/
/**
* This function setups the interrupt system such that interrupts can occur.
* This function is application specific since the actual system may or may not
* have an interrupt controller.  The USB controller could be
* directly connected to a processor without an interrupt controller.
* The user should modify this function to fit the application.
*
* @param	InstancePtr is a pointer to the XUsbPsu instance.
* @param	IntcDeviceID is the unique ID of the interrupt controller
* @param	USB_INTR_ID is the interrupt ID of the USB controller
* @param	IntcPtr is a pointer to the interrupt controller
*			instance.
*
* @return	XST_SUCCESS if successful, otherwise XST_FAILURE.
*
* @note		None.
*
*****************************************************************************/
static s32 SetupInterruptSystem(struct XUsbPsu *InstancePtr, u16 IntcDeviceID,
		u16 USB_INTR_ID, void *IntcPtr)
{
	/*
	 * This below is done to remove warnings which occur when usbpsu
	 * driver is compiled for platforms other than MICROBLAZE or ZYNQMP
	 */
	(void)InstancePtr;
	(void)IntcDeviceID;
	(void)IntcPtr;

#ifdef __MICROBLAZE__
#ifdef XPAR_INTC_0_DEVICE_ID
	s32 Status;

	XIntc *IntcInstancePtr = (XIntc *)IntcPtr;

	/*
	 * Initialize the interrupt controller driver.
	 */
	Status = XIntc_Initialize(IntcInstancePtr, IntcDeviceID);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}


	/*
	 * Connect a device driver handler that will be called when an interrupt
	 * for the USB device occurs.
	 */
	Status = XIntc_Connect(IntcInstancePtr, USB_INTR_ID,
			       (Xil_ExceptionHandler)XUsbPsu_IntrHandler,
			       (void *) InstancePtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Start the interrupt controller such that interrupts are enabled for
	 * all devices that cause interrupts, specific real mode so that
	 * the USB can cause interrupts through the interrupt controller.
	 */
	Status = XIntc_Start(IntcInstancePtr, XIN_REAL_MODE);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Enable the interrupt for the USB.
	 */
	XIntc_Enable(IntcInstancePtr, USB_INTR_ID);

	/*
	 * Initialize the exception table
	 */
	Xil_ExceptionInit();

	/*
	 * Enable interrupts for Reset, Disconnect, ConnectionDone, Link State
	 * Wakeup and Overflow events.
	 */
	XUsbPsu_EnableIntr(InstancePtr, XUSBPSU_DEVTEN_EVNTOVERFLOWEN |
                        XUSBPSU_DEVTEN_WKUPEVTEN |
                        XUSBPSU_DEVTEN_ULSTCNGEN |
                        XUSBPSU_DEVTEN_CONNECTDONEEN |
                        XUSBPSU_DEVTEN_USBRSTEN |
                        XUSBPSU_DEVTEN_DISCONNEVTEN);

	/*
	 * Register the interrupt controller handler with the exception table
	 */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
				(Xil_ExceptionHandler)XIntc_InterruptHandler,
				IntcInstancePtr);
#endif /* XPAR_INTC_0_DEVICE_ID */
#elif defined PLATFORM_ZYNQMP
	s32 Status;

	XScuGic_Config *IntcConfig; /* The configuration parameters of the
					interrupt controller */

	XScuGic *IntcInstancePtr = (XScuGic *)IntcPtr;

	/*
	 * Initialize the interrupt controller driver
	 */
	IntcConfig = XScuGic_LookupConfig(IntcDeviceID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig, IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Connect to the interrupt controller
	 */
	Status = XScuGic_Connect(IntcInstancePtr, USB_INTR_ID, (Xil_ExceptionHandler)XUsbPsu_IntrHandler, (void *)InstancePtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
#ifdef XUSBPSU_HIBERNATION_ENABLE
	Status = XScuGic_Connect(IntcInstancePtr, USB_WAKEUP_INTR_ID,
							(Xil_ExceptionHandler)XUsbPsu_WakeUpIntrHandler,
							(void *)InstancePtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
#endif

	/*
	 * Enable the interrupt for the USB
	 */
	XScuGic_Enable(IntcInstancePtr, USB_INTR_ID);
#ifdef XUSBPSU_HIBERNATION_ENABLE
	XScuGic_Enable(IntcInstancePtr, USB_WAKEUP_INTR_ID);
#endif

	/*
	 * Enable interrupts for Reset, Disconnect, ConnectionDone, Link State
	 * Wakeup and Overflow events.
	 */
	XUsbPsu_EnableIntr(InstancePtr, XUSBPSU_DEVTEN_EVNTOVERFLOWEN |
                        XUSBPSU_DEVTEN_WKUPEVTEN |
                        XUSBPSU_DEVTEN_ULSTCNGEN |
                        XUSBPSU_DEVTEN_CONNECTDONEEN |
                        XUSBPSU_DEVTEN_USBRSTEN |
                        XUSBPSU_DEVTEN_DISCONNEVTEN);

#ifdef XUSBPSU_HIBERNATION_ENABLE
	if (InstancePtr->HasHibernation)
		XUsbPsu_EnableIntr(InstancePtr,
				XUSBPSU_DEVTEN_HIBERNATIONREQEVTEN);
#endif

	/*
	 * Connect the interrupt controller interrupt handler to the hardware
	 * interrupt handling logic in the ARM processor.
	 */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, IntcInstancePtr);
#endif /* PLATFORM_ZYNQMP */

	/*
	 * Enable interrupts in the ARM
	 */
	Xil_ExceptionEnable();

	return XST_SUCCESS;
}
