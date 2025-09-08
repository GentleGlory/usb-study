#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>


#define MOUSE_PROTOCOL	2

// Global flag to control program termination
static volatile bool running = true;

// Signal handler for Ctrl+C (SIGINT)
void sigint_handler(int sig) {
    printf("\nReceived Ctrl+C signal, exiting gracefully...\n");
    running = false;
}

libusb_device_handle *find_mouse(uint8_t *ep_addr, uint8_t* interface_num)
{
	int ret;
	libusb_device_handle *handle = NULL;
	ssize_t cnt;
	libusb_device **list = NULL;

	cnt = libusb_get_device_list(NULL, &list);
	if (cnt >= 0 ) {
		for (ssize_t dev = 0; dev < cnt; dev++) {
			struct libusb_device_descriptor desc;

			ret = libusb_get_device_descriptor(list[dev], &desc);
			if (ret) {
				printf("Failed to get device descriptor, error:%d\n",ret);
				continue;
			}

			for (uint8_t conf = 0; conf < desc.bNumConfigurations; conf++) {
				struct libusb_config_descriptor *config;
				
				if (libusb_get_config_descriptor(list[dev], conf, &config) == 0) {
					for (uint8_t i = 0; i < config->bNumInterfaces; i++) {
						const struct libusb_interface *interface = &config->interface[i];

						for (int n = 0; n < interface->num_altsetting; n++) {
							const struct libusb_interface_descriptor *alt_set = &interface->altsetting[n];
							
							if (alt_set->bInterfaceClass != LIBUSB_CLASS_HID ||
								alt_set->bInterfaceProtocol != MOUSE_PROTOCOL) {
								continue;
							}

							for (int endp = 0; endp < alt_set->bNumEndpoints; endp++) {
								const struct libusb_endpoint_descriptor *ep = &alt_set->endpoint[endp];

								if ((ep->bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_INTERRUPT &&
									(ep->bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN ) {
									*ep_addr = ep->bEndpointAddress;
									*interface_num = i;

									ret = libusb_open(list[dev], &handle);
									if (!handle) {
										printf("Failed to open device err:%d\n", ret);
										continue;
									}

									libusb_set_auto_detach_kernel_driver(handle, 1);

									// Check current configuration before setting
									int current_config;
									ret = libusb_get_configuration(handle, &current_config);
									if (ret == 0 && current_config == config->bConfigurationValue) {
										printf("Device already configured with config %d\n", current_config);
									} else {
										if ((ret = libusb_set_configuration(handle, config->bConfigurationValue)) != 0) {
											printf("Failed to set configuration, %d, err:%d\n",config->bConfigurationValue, ret);
											libusb_close(handle);
											handle = NULL;
											continue;
										}
									}

									printf("mouse vendor id :%d, product id:%d\n",desc.idVendor, desc.idProduct);
									break;
								}
							}

							if (handle)
								break;
						}

						if (handle)
							break;
					}

					libusb_free_config_descriptor(config);
				}

				if (handle)
					break;
			}

			if (handle)
				break;
		}

		libusb_free_device_list(list, 1);
	} else {
		fprintf(stderr,"Failed to get device list, error:%ld\n",cnt);		
	}

	return handle;
}


int main(int argc, char *argv[])
{
	int ret = 0;
 	uint8_t endp_addr = 0;
	uint8_t interface_num = 0;
	libusb_device_handle * handle = NULL;
	unsigned char buffer[16];
	int transferred;
	
	ret = libusb_init(NULL);
	if (ret) {
		fprintf(stderr,"Failed to init libusb, error:%d\n",ret);
		return -1;
	}

	// Register signal handler for Ctrl+C
	signal(SIGINT, sigint_handler);

	handle = find_mouse(&endp_addr, &interface_num);
	if (handle) {
		ret = libusb_claim_interface(handle, interface_num);
		if (ret) {
			printf("Failed to claim interface\n");
		} else {
			printf("Mouse found, endpoint: 0x%02x, interface: %d\n", endp_addr, interface_num);
			printf("Press Ctrl+C to exit...\n");
			
			// Main loop - continue until Ctrl+C is pressed
			while (running) {
				
				ret = libusb_interrupt_transfer(handle, endp_addr, buffer, 8, &transferred,5000);
				
				if (!ret) {
					// Parse mouse data
					if (transferred == 8) {
						uint8_t buttons = buffer[0];
						int8_t scroll = (int8_t)buffer[3];
						int16_t dx = (int16_t)((buffer[4]) | (buffer[5] << 8));
						int16_t dy = (int16_t)((buffer[6]) | (buffer[7] << 8));
						
						// Print raw data for debugging
						printf("Raw data (%d bytes): ", transferred);
						for (int i = 0; i < transferred; i++) {
							printf("0x%02x ", buffer[i]);
						}
						printf("\n");
						
						// Parse button states
						bool left_btn = buttons & 0x01;
						bool right_btn = buttons & 0x02;
						bool middle_btn = buttons & 0x04;
						
						// Only print when there's activity
						if (dx != 0 || dy != 0 || scroll != 0 || buttons != 0) {
							printf("Buttons: L=%d R=%d M=%d | Movement: X=%d Y=%d | Scroll=%d\n",
								   left_btn, right_btn, middle_btn, dx, dy, scroll);
						}
					}

				} else if (ret == LIBUSB_ERROR_TIMEOUT){
					printf("libusb_interrupt_transfer timeout\n");
				} else {
					printf("libusb_interrupt_transfer error:%d\n", ret);
					break;
				}
				
				usleep(10000);  // Sleep for 10ms for better responsiveness
			}
			
			printf("Cleaning up...\n");
			libusb_release_interface(handle, interface_num);
		}

		libusb_close(handle);
	}

	libusb_exit(NULL);
	return ret;
}