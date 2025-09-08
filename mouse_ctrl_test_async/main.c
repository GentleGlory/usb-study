#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

struct mouse_context {
	int						vid;
	int						pid;
	libusb_device_handle 	*handle;
	int 					ep_num;
	int 					interface_num;
	unsigned char			buffer[16];
	int 					max_package_size;
	struct libusb_transfer	*transfer;
	struct mouse_context	*next;
};

static struct mouse_context	*mouse_ctx_list = NULL;

#define MOUSE_PROTOCOL	2

// Global flag to control program termination
static volatile bool running = true;

// Signal handler for Ctrl+C (SIGINT)
void sigint_handler(int sig) {
    printf("\nReceived Ctrl+C signal, exiting gracefully...\n");
    running = false;
}

int find_mouse(struct mouse_context	**mouse_ctx_list)
{
	int ret;
	libusb_device_handle *handle = NULL;
	ssize_t cnt;
	libusb_device **list = NULL;
	struct mouse_context	*new_mouse = NULL; 
	struct mouse_context	*mouse_list = NULL;
	int mouse_cnt = 0;

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

									if (libusb_claim_interface(handle, i) != 0) {
										libusb_close(handle);
										handle = NULL;
										continue;
									}

									new_mouse = (struct mouse_context *) malloc(sizeof(struct mouse_context));
									if (!new_mouse) {
										printf("Failed to alloc mouse_context\n");
										libusb_release_interface(handle, i);
										libusb_close(handle);
										handle = NULL;
										continue;
									}
									memset(new_mouse, 0, sizeof(struct mouse_context));
									new_mouse->ep_num = ep->bEndpointAddress;
									new_mouse->interface_num = i;
									new_mouse->handle = handle;
									new_mouse->max_package_size = ep->wMaxPacketSize;
									new_mouse->vid = desc.idVendor;
									new_mouse->pid = desc.idProduct;

									if (mouse_list == NULL) {
										mouse_list = new_mouse;
									} else {
										new_mouse->next = mouse_list;
										mouse_list = new_mouse;
									}

									printf("mouse vendor id :%04x, product id:%04x, ep:%d, interface:%d\n",
											desc.idVendor, desc.idProduct, new_mouse->ep_num, new_mouse->interface_num);
									mouse_cnt ++;
									break;
								}
							}
						}
					}
					libusb_free_config_descriptor(config);
				}
			}
		}

		libusb_free_device_list(list, 1);
	} else {
		fprintf(stderr,"Failed to get device list, error:%ld\n",cnt);		
	}

	*mouse_ctx_list = mouse_list;
	return mouse_cnt;
}

void mouse_irq(struct libusb_transfer *transfer)
{
	unsigned char *buffer = transfer->buffer;
	int transferred = transfer->actual_length;
	struct mouse_context *ctx = transfer->user_data;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		goto err_free_transfer;
	}

	printf("VID:%04x, PID:%04x, Raw data (%d bytes): ", 
		ctx->vid, ctx->pid, transferred);
		for (int i = 0; i < transferred; i++) {
			printf("0x%02x ", buffer[i]);
		}
	printf("\n");

	if (libusb_submit_transfer(transfer) != 0) {
		printf("Failed to submit transfer in mouse_irq\n");
		goto err_free_transfer;
	}

	return;
err_free_transfer:

	libusb_free_transfer(ctx->transfer);
	ctx->transfer = NULL;
}

int start_transfer_mouse_ctx_list(struct mouse_context *list)
{
	struct mouse_context *mouse_ctx = list;
	int ret;
	while(mouse_ctx) {
		
		mouse_ctx->transfer = libusb_alloc_transfer(0);
		if (!mouse_ctx->transfer) {
			printf("Failed to alloc transfer\n");
			return -1;
		}

		libusb_fill_interrupt_transfer(mouse_ctx->transfer, mouse_ctx->handle,
				mouse_ctx->ep_num, mouse_ctx->buffer, mouse_ctx->max_package_size,
				mouse_irq, mouse_ctx, 0);

		if ((ret = libusb_submit_transfer(mouse_ctx->transfer)) != 0) {
			printf("Failed to submit transfer. %d\n",ret);
			libusb_free_transfer(mouse_ctx->transfer);
			mouse_ctx->transfer = NULL;
			return -1;
		}
		mouse_ctx = mouse_ctx->next;
	}

	return 0;
}

void cleanup_mouse_ctx_list(struct mouse_context *list)
{
	struct mouse_context *mouse_ctx = list;
	struct mouse_context *next = NULL;
	int need_wait = 0;

	printf("Cleaning up...\n");

	while (mouse_ctx != NULL) {
		if (mouse_ctx->transfer != NULL) {
			need_wait = 1;
			libusb_cancel_transfer(mouse_ctx->transfer);
		}
		mouse_ctx = mouse_ctx->next;
	}

	
	while (need_wait) {
		if (libusb_handle_events(NULL) < 0)
			break;

		need_wait = 0;
		mouse_ctx = list;
		while (mouse_ctx != NULL) {
			if (mouse_ctx->transfer != NULL) {
				need_wait = 1;
			}
			mouse_ctx = mouse_ctx->next;
		}
	}

	mouse_ctx = list;
	while (mouse_ctx != NULL) {
		next = mouse_ctx->next;

		if (mouse_ctx->transfer != NULL) {
			libusb_free_transfer(mouse_ctx->transfer);
			mouse_ctx->transfer = NULL;
		}

		libusb_release_interface(mouse_ctx->handle, mouse_ctx->interface_num);
		libusb_close(mouse_ctx->handle);
		free(mouse_ctx);
		mouse_ctx = next;
	}	
}

int main(int argc, char *argv[])
{
	int ret = 0;
	
	ret = libusb_init(NULL);
	if (ret) {
		fprintf(stderr,"Failed to init libusb, error:%d\n",ret);
		return -1;
	}

	// Register signal handler for Ctrl+C
	signal(SIGINT, sigint_handler);

	ret = find_mouse(&mouse_ctx_list);
	if (ret) {
		
		if (start_transfer_mouse_ctx_list(mouse_ctx_list) == 0) {
			while(1) {
				ret = libusb_handle_events(NULL);
				if (ret < 0) {
					printf("libusb_handle_events error:%d\n",ret);
					break;
				}
			}
		}

		cleanup_mouse_ctx_list(mouse_ctx_list);
	}

	libusb_exit(NULL);
	return ret;
}