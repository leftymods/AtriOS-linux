uboot_name=yandex_station_v1
project_path=$TARGET_PRODUCT

./mk $uboot_name --systemroot;

cp build/u-boot.bin ../../device/yandex/$project_path/bootloader.img;
cp build/u-boot.bin.usb.bl2 ../../device/yandex/$project_path/upgrade/u-boot.bin.usb.bl2;
cp build/u-boot.bin.usb.tpl ../../device/yandex/$project_path/upgrade/u-boot.bin.usb.tpl;
cp build/u-boot.bin.sd.bin ../../device/yandex/$project_path/upgrade/u-boot.bin.sd.bin;
