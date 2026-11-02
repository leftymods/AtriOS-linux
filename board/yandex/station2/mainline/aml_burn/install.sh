#!/bin/bash
# Install AML burn gadget sources + Kconfig/Makefile hooks into a mainline U-Boot tree
set -e
D=$(dirname "$(readlink -f "$0")"); U="$1"
mkdir -p $U/include/amlogic
cp $D/f_aml_burn.h $U/include/amlogic/
cp $D/f_aml_burn.c $U/drivers/usb/gadget/f_aml_burn.c
cp $D/aml_burn_cmd.c $U/cmd/aml_burn.c
grep -q AML_BURN $U/drivers/usb/gadget/Makefile || echo 'obj-$(CONFIG_USB_FUNCTION_AML_BURN) += f_aml_burn.o' >> $U/drivers/usb/gadget/Makefile
grep -q AML_BURN $U/cmd/Makefile || echo 'obj-$(CONFIG_CMD_AML_BURN) += aml_burn.o' >> $U/cmd/Makefile
python3 - "$U" <<'PY'
import sys,re
u=sys.argv[1]
p=u+'/drivers/usb/gadget/Kconfig'; s=open(p).read()
if 'USB_FUNCTION_AML_BURN' not in s:
    blk='\nconfig USB_FUNCTION_AML_BURN\n\tbool "Amlogic USB Burning Tool gadget"\n\thelp\n\t  Amlogic USB Burning Tool (Optimus v2) protocol, VID 1b8e PID c003.\n\nconfig USB_GADGET_AML_BURN\n\tbool "Select Amlogic burn gadget"\n\tselect USB_FUNCTION_AML_BURN\n'
    i=s.index('endif # USB_GADGET\n')
    s=s[:i]+blk.lstrip('\n')+'\n'+s[i:]
    open(p,'w').write(s)
p=u+'/cmd/Kconfig'; s=open(p).read()
if 'CMD_AML_BURN' not in s:
    s=s.replace('config CMD_ROCKUSB','config CMD_AML_BURN\n\tbool "aml_burn"\n\tdepends on USB_FUNCTION_AML_BURN\n\thelp\n\t  Amlogic USB Burning Tool mode.\n\nconfig CMD_ROCKUSB',1)
    open(p,'w').write(s)
p=u+'/drivers/usb/gadget/composite.c'; s=open(p).read()
old='\t\tif (cdev->config->next_interface_id == 1)\n\t\t\tf = cdev->config->interface[intf];'
if old in s:
    s=s.replace(old,'\t\tif (cdev->config->next_interface_id == 1)\n\t\t\tf = cdev->config->interface[0];')
    open(p,'w').write(s)
PY
