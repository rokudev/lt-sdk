#!/bin/bash

set -e

function usage {
    echo "usage: $script [OPTION...] program"
    echo "";
    echo "  -c, --config            config file";
    echo "  -D, --dev               serial device name";
    echo "  -r, --releasedir        directory with LT binary images";
    echo "  -x encrypt              enable automatic encryption";
    echo "  --dap                   disable auto-programming features";
    echo "  --dar                   disable auto-reboot after program";
    echo "  --smash                 write boot, prov, and part partitions";
    echo "  --nostub                do not load/use stub flasher image program";
    echo "  --signed                use signed firmware image";
    echo "  -h, --help              give this help text";
}

# Defaults
config=""
device=""
dar="false"
dap="false"
extra=""
release_dir=""
smash="false"
signed="false"
nostub="false"

function parse_args {
    script=$0
    args=()

    while [ "$1" != "" ]; do
        case "$1" in
            -c | --config )               config="$2";             shift;;
            -D | --dev )                  device="$2";             shift;;
            --dar )                       dar="true";              ;;
            --dap )                       dap="true";              ;;
            -r | --releasedir )           release_dir="$2";        shift;;
            -x )                          extra="$2";              shift;;
            program )                     mode="program";          ;;
            --smash )                     smash="true";            ;;
            --nostub )                    nostub="true";           ;;
            --signed )                    signed="true";           ;;
            -h | --help )                 usage;                   exit;; # quit and show usage
            * )                           args+=("$1")             # if no match, add it to the positional args
        esac
        shift # move to next argument
    done

    # restore positional args
    set -- "${args[@]}"

    # set positionals to vars
    positional_1="${args[0]}"

    if [[ $mode != "program" ]]; then
        usage
        exit 64
    fi

    if [[ -z $release_dir ]]; then
        echo "Must provide release directory" >&2
        usage
        exit 64
    fi

    if [[ ! -d $release_dir ]]; then
        echo "$release_dir does not exist" >&2
        usage >&2
        exit 64
    fi

    if [[ -z $config ]]; then
        echo "Must include config" >&2
        usage >&2
        exit 64
    fi

    if [[ -n $extra ]]; then
        if [[ $extra != "encrypt" ]]; then
            echo "Invalid -x option"
            usage >&2
            exit 64
        fi
    fi
}

function run_rit_verbose {
    echo rit "$@"
    MAX_ATTEMPTS=2
    ATTEMPTS=0
    until [ $ATTEMPTS -eq $MAX_ATTEMPTS ] || $RIT_CMD "$@"; do
        ((ATTEMPTS++));
    done
}

parse_args "$@"

 if [[ -f $release_dir/LTFirmwareImage.bin ]]; then
    release_dir=$(realpath "$release_dir")
else
    if [[ -f release_dir/bin/LTFirmwareImage.bin ]]; then
        release_dir=$(realpath "$release_dir/bin")
    else
        echo "Can't find firmware in release directory '$release_dir', please build LT" >&2
        exit 72
    fi
fi

RIT_CMD=$(type -p rit 2>/dev/null || true)
if [[ -z $RIT_CMD ]]; then
    RIT_CMD="$release_dir/../../../../buildtools/bin/rit"
    if [[ ! -x $RIT_CMD ]]; then
        echo "Can't find 'rit', please build LT or add the 'rit' binary's location to the shell command path" >&2
        exit 72
    fi
    RIT_CMD=$(realpath "$RIT_CMD")
fi

if [[ -f $config ]]; then
    config_path=$(realpath "$config")
else
    config_path=$(cd "$release_dir/config" && realpath "$config" 2>/dev/null || true)
fi
if [[ -z $config_path ]] || [[ ! -f $config_path ]]; then
    echo "Can't config file '$config'" >&2
    exit 72
fi

DEV_ARG=
if [[ ! -z $device ]]; then
    DEV_ARG="-D $device"
fi

BEG_PROG_ARG="--dar"
MID_PROG_ARG="--dar"
END_PROG_ARG=
if [[ "$dap" == "true" ]]; then
    BEG_PROG_ARG="--dap"
    MID_PROG_ARG="--dap"
    END_PROG_ARG="--dap"
elif [[ "$dar" == "true" ]]; then
    END_PROG_ARG="--dar"
fi

NOSTUB_ARG=""
if [[ "$nostub" == "true" ]]; then
    NOSTUB_ARG="-x nostub"
fi

ENCRYPT_ARG=""
if [[ "$extra" == "encrypt" ]]; then
    ENCRYPT_ARG="-x encrypt"
fi

FIRMWARE_IMAGE="$release_dir/LTFirmwareImage.bin"
if [[ "$signed" == "true" ]]; then
    FIRMWARE_IMAGE="$release_dir/LTFirmwareImage_signed.bin"
fi

search_path="$LT_PLATFORM_BUILD_PLATFORM_VARIANT_DIR:$LT_PLATFORM_ROOT/build/image:."

if [[ "$smash" == "true" ]]; then
    run_rit_verbose $DEV_ARG -c $config_path -p $search_path  $BEG_PROG_ARG $NOSTUB_ARG $ENCRYPT_ARG -a boot -f $release_dir/LTBootloader.bin program
    run_rit_verbose $DEV_ARG -c $config_path -p $search_path  $MID_PROG_ARG $NOSTUB_ARG $ENCRYPT_ARG -a prov -f $release_dir/LTDeviceIdentity.bin program
    run_rit_verbose $DEV_ARG -c $config_path -p $search_path  $MID_PROG_ARG $NOSTUB_ARG $ENCRYPT_ARG -a part -f $release_dir/LTPartitionTable.bin program
    run_rit_verbose $DEV_ARG -c $config_path -p $search_path  $MID_PROG_ARG $NOSTUB_ARG $ENCRYPT_ARG -a settings erase
fi
run_rit_verbose $DEV_ARG -c $config_path -p $search_path  $MID_PROG_ARG $NOSTUB_ARG $ENCRYPT_ARG -a ota erase
run_rit_verbose $DEV_ARG -c $config_path -p $search_path  $END_PROG_ARG $NOSTUB_ARG $ENCRYPT_ARG -a fw0  -f $FIRMWARE_IMAGE program
