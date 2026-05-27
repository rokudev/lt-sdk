#!/bin/bash

#
# WIP: Currently this is for ESP32 only
#

set -e

function usage {
    echo "usage: $script [OPTION...] <filename>"
    echo "Signs the secure booatloader and the firmware with a supplied ";
    echo "or a default secure boot key, and flashes both to the target";
    echo "";
    echo "<filename> is the path to an unsigned binary file, usually fw_image.bin";
    echo "OPTIONS:";
    echo "  -k, --keyfile           secure boot RSA pem file";
    echo "                          defaults to secure_boot_test_key.pem";
    echo "  -e, --encrypt           encrypt selected partitions using the key already on the device";
    echo "  -D, --dev               serial device name";
    echo "  -h, --help              give this help text";
}

# validate required args
if [[ -z "$LT_PLATFORM_ROOT" ]]; then
    LT_PLATFORM_ROOT="$(dirname $0)/../../platforms/esp32"
fi

MASTERING_DIR=$LT_PLATFORM_ROOT/source/esp32/mastering

# Defaults
device="/dev/ttyUSB0"
dar="false"
key="$MASTERING_DIR/config/secure_boot_test_key.pem"
encryption=""
filename=""

function parse_args {
  script=$0

  args=()

  while [ "$1" != "" ]; do
      case "$1" in
          -k | --keyfile )              key="$2";                shift;;
          -e | --encrypt )              encryption="--encrypt";;
          -D | --dev )                  device="$2";             shift;;
          -h | --help )                 usage;                   exit;; # quit and show usage
          * )                           filename="$1"             # this must be a filename
      esac
      shift # move to next argument
  done

  if [[ -z "$filename" ]]; then
      echo "Must include filename of an unsigned image"
      usage
      exit
  fi
}

parse_args "$@"

REBOOT_ARGS=
if [[ "$dar" == "true" ]]; then
    REBOOT_ARGS="--before no_reset --after no_reset"
fi

# sign the app using the test key

SIGN_CMD="python $MASTERING_DIR/image/espsecure.py sign_data --keyfile $key -v 2 --output $(dirname $filename)/signed_$(basename $filename) $filename"

# If we are using the default secure boot key, we already have a signed
# bootloader for that key. Otherwise, we sign the unsigned bootloader with the
# key passed on the command line.

if [[ "$key" == $MASTERING_DIR/config/secure_boot_test_key.pem ]]; then
    SIGNED_BOOTLOADER="$MASTERING_DIR/config/signed_secure_bootloader.bin"
else
    echo "$key"
    echo $MASTERING_DIR/config/secure_boot_test_key
    SIGN_BOOT_CMD="python $MASTERING_DIR/image/espsecure.py sign_data --keyfile $key -v 2 --output $(dirname $filename)/signed_secure_bootloader.bin $MASTERING_DIR/config/secure_bootloader.bin"
    SIGNED_BOOTLOADER="$(dirname $filename)/signed_secure_bootloader.bin"
    echo -e "\n### Signing the bootloader with $key"
    echo $SIGN_BOOT_CMD
    $SIGN_BOOT_CMD
fi

RUN_CMD="python $MASTERING_DIR/image/esptool.py -p $device -b 460800 $REBOOT_ARGS --chip esp32 --after no_reset  write_flash $encryption --flash_mode dio --flash_size keep --flash_freq keep 0x1000 $SIGNED_BOOTLOADER 0x10000 $MASTERING_DIR/config/partition-table.bin 0x15000 $MASTERING_DIR/config/ota_data_initial.bin 0x20000 $(dirname $filename)/signed_$(basename $filename) 0x3FF000 $MASTERING_DIR/config/provision_full.bin"


echo -e "\n### Signing the firmware with $key"
echo $SIGN_CMD
$SIGN_CMD

echo -e "\n### Flashing the signed image"
echo $RUN_CMD
$RUN_CMD

