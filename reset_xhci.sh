#!/bin/bash

# Only on Robbe's PC

sudo bash -c "echo '0000:02:00.0' > /sys/bus/pci/drivers/xhci_hcd/unbind"
sleep 0.5
sudo bash -c "echo '0000:02:00.0' > /sys/bus/pci/drivers/xhci_hcd/bind"
