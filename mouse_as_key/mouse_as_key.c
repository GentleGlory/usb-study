#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>

struct mouse_as_key_desc {
	char name[128];
	char phys[64];
	struct usb_device		*usb_dev;
	struct input_dev 		*input_dev;
	struct usb_interface	*intf;
	signed char 			*data;
	dma_addr_t				data_dma;
	struct urb				*urb;
	int						maxp;
};

static void mouse_as_key_irq(struct urb *urb)
{
	struct mouse_as_key_desc *desc = urb->context;
	int status;
	signed char* data = desc->data;

	switch (urb->status) {
	case 0:
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default:
		goto resubmit;
	}

	input_report_key(desc->input_dev, KEY_L, data[0] & 0x1);
	input_report_key(desc->input_dev, KEY_S, data[0] & 0x2);
	input_report_key(desc->input_dev, KEY_ENTER, data[0] & 0x4);

	input_sync(desc->input_dev);
resubmit:
	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status) {
		dev_err(&desc->usb_dev->dev, "Failed to resubmit urb, status:%d", status);
	}
}


static int mouse_as_key_open(struct input_dev *dev)
{
	struct mouse_as_key_desc *desc = input_get_drvdata(dev);
	
	if (usb_submit_urb(desc->urb, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void mouse_as_key_close(struct input_dev *dev)
{
	struct mouse_as_key_desc *desc = input_get_drvdata(dev);

	usb_kill_urb(desc->urb);
}

static int mouse_as_key_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct mouse_as_key_desc *desc;
	struct input_dev *input_dev;
	struct device *dev = &usb_dev->dev;
	struct usb_host_interface *interface;
	struct usb_endpoint_descriptor *endpoint;
	int pipe;
	int ret = 0;

	interface = intf->cur_altsetting;
	if (interface->desc.bNumEndpoints != 1)
		return -ENODEV;

	endpoint = &interface->endpoint[0].desc;
	if (!usb_endpoint_is_int_in(endpoint))
		return -ENODEV;

	input_dev = input_allocate_device();
	if (!input_dev) {
		dev_err(dev, "Failed to alloc input device\n");
		ret = -ENOMEM;
		goto input_alloc_error;
	}

	desc = devm_kzalloc(dev, sizeof(struct mouse_as_key_desc), GFP_KERNEL);
	if (!desc) {
		dev_err(dev, "Failed to alloc private data\n");
		ret = -ENOMEM;
		goto desc_alloc_error;
	}

	pipe = usb_rcvintpipe(usb_dev, endpoint->bEndpointAddress);
	desc->maxp = usb_maxpacket(usb_dev, pipe);

	desc->intf = intf;
	desc->input_dev = input_dev;
	
	if (usb_dev->manufacturer)
		strscpy(desc->name, usb_dev->manufacturer, sizeof(desc->name));

	if (usb_dev->product) {
		if (usb_dev->manufacturer)
			strlcat(desc->name, " ", sizeof(desc->name));
		strlcat(desc->name, usb_dev->product, sizeof(desc->name));
	}

	if (!strlen(desc->name))
		snprintf(desc->name, sizeof(desc->name),
			 "USB HIDBP Mouse %04x:%04x",
			 le16_to_cpu(usb_dev->descriptor.idVendor),
			 le16_to_cpu(usb_dev->descriptor.idProduct));

	usb_make_path(usb_dev, desc->phys, sizeof(desc->phys));
	strlcat(desc->phys, "/input0", sizeof(desc->phys));

	input_dev->name = desc->name;
	input_dev->phys = desc->phys;
	usb_to_input_id(usb_dev, &input_dev->id);
	
	desc->data = usb_alloc_coherent(usb_dev, desc->maxp, GFP_KERNEL, &desc->data_dma);
	if (!desc->data) {
		dev_err(dev, "Failed to alloc dma\n");
		ret = -ENOMEM;
		goto desc_alloc_error;
	}

	desc->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!desc->urb) {
		dev_err(dev, "Failed to alloc urb\n");
		ret = -ENOMEM;
		goto desc_alloc_error;
	}

	usb_fill_int_urb(desc->urb, usb_dev, pipe, desc->data,
		desc->maxp, mouse_as_key_irq, desc, endpoint->bInterval);
	desc->urb->transfer_dma = desc->data_dma;
	desc->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	input_dev->dev.parent = &intf->dev;
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(KEY_L, input_dev->keybit);
	__set_bit(KEY_S, input_dev->keybit);
	__set_bit(KEY_ENTER, input_dev->keybit);

	input_dev->open = mouse_as_key_open;
	input_dev->close = mouse_as_key_close;

	input_set_drvdata(input_dev, desc);

	ret = input_register_device(desc->input_dev);
	if (ret) {
		dev_err(dev, "Failed to register input_dev\n");
		goto input_register_error;
	}

	usb_set_intfdata(intf, desc);

	return ret;

input_register_error:
	usb_free_urb(desc->urb);
	usb_free_coherent(usb_dev, desc->maxp, desc->data, desc->data_dma);
desc_alloc_error:
	input_free_device(input_dev);
input_alloc_error:
	return ret;
}

static void mouse_as_key_disconnect(struct usb_interface *intf)
{
	struct mouse_as_key_desc *desc = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	
	usb_kill_urb(desc->urb);
	input_unregister_device(desc->input_dev);
	usb_free_urb(desc->urb);
	usb_free_coherent(desc->usb_dev, desc->maxp, desc->data, desc->data_dma);
}

static const struct usb_device_id mouse_as_key_id_table[] = {
	{ USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
		USB_INTERFACE_PROTOCOL_MOUSE) },
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, mouse_as_key_id_table);

static struct usb_driver mouse_as_key_driver = {
	.name = "mouse_as_key",
	.probe = mouse_as_key_probe,
	.disconnect = mouse_as_key_disconnect,
	.id_table = mouse_as_key_id_table,
};

module_usb_driver(mouse_as_key_driver);

MODULE_LICENSE("GPL");