/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Written against the shape of bootloader_esp32c3.c in this directory rather
 * than copied from IDF v5.4.  Upstream's esp32p4 file reaches cache_hal_init(),
 * mmu_hal_init(), the brownout/assist-debug/regi2c LL headers and
 * bootloader_print_banner(), none of which this tree carries.  Every HAL call
 * below is replaced either by the ROM entry it would have ended up in or by the
 * single register write it performs, named in a comment beside it.
 */
#include <stdint.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_image_format.h"
#include "esp_rom_sys.h"
#include "soc/soc.h"
#include "soc/chip_revision.h"
#include "soc/assist_debug_reg.h"
#include "soc/hp_sys_clkrst_reg.h"
#include "soc/i2c_ana_mst_reg.h"
#include "soc/lp_analog_peri_reg.h"
#include "soc/lp_wdt_reg.h"
#include "soc/regi2c_bias.h"
#include "soc/regi2c_cpll.h"
#include "soc/regi2c_syspll.h"
#include "esp32p4/rom/cache.h"
#include "esp32p4/rom/spi_flash.h"
#include "bootloader_common.h"
#include "bootloader_init.h"
#include "bootloader_clock.h"
#include "bootloader_console.h"
#include "bootloader_flash.h"
#include "bootloader_flash_config.h"
#include "bootloader_flash_priv.h"
#include "bootloader_mem.h"
#include "bootloader_soc.h"
#include "esp_private/regi2c_ctrl.h"
#include "hal/efuse_hal.h"

#include "LTBoot.h"
#include "LTBootPlatform.h"

static const char *TAG = "boot.esp32p4";

/* The write key for LP_WDT_SWD_WPROTECT_REG.  lp_wdt_reg.h names the field but
 * not the magic; it lives in v5.4 hal/esp32p4/include/hal/lpwdt_ll.h, which this
 * tree does not carry. */
#define LP_WDT_SWD_WKEY_VALUE 0x50D83AA1

static void bootloader_reset_mmu(void)
{
    /* rom/cache.h spells this trio ROM_boot_Cache_Suspend/Invalidate_All/Resume.
     * Cache_MMU_Init() is a macro for Cache_MSPI_MMU_Init(), which the ROM does
     * not export, so the flash half is initialized directly. */
    uint32_t autoload = Cache_Suspend_L2_Cache();
    Cache_Invalidate_All(CACHE_MAP_L2_CACHE);
    Cache_FLASH_MMU_Init();
    Cache_Resume_L2_Cache(autoload);

    /* No EXTMEM_ICACHE_SHUT_IBUS/SHUT_DBUS counterpart here: the esp32p4 has no
     * such control, and cache_ll_l1_enable_bus() is a documented no-op on it. */
}

static void update_flash_config(const esp_image_header_t *bootloader_hdr)
{
    uint32_t size;
    switch (bootloader_hdr->spi_size) {
    case ESP_IMAGE_FLASH_SIZE_1MB:
        size = 1;
        break;
    case ESP_IMAGE_FLASH_SIZE_2MB:
        size = 2;
        break;
    case ESP_IMAGE_FLASH_SIZE_4MB:
        size = 4;
        break;
    case ESP_IMAGE_FLASH_SIZE_8MB:
        size = 8;
        break;
    case ESP_IMAGE_FLASH_SIZE_16MB:
        size = 16;
        break;
    default:
        size = 2;
    }
    uint32_t autoload = Cache_Suspend_L2_Cache();
    // Set flash chip size
    esp_rom_spiflash_config_param(rom_spiflash_legacy_data->chip.device_id, size * 0x100000, 0x10000, 0x1000, 0x100, 0xffff);
    Cache_Resume_L2_Cache(autoload);
}

static void print_flash_info(const esp_image_header_t *bootloader_hdr)
{
    u8 spiSpeed = 20;
    switch (bootloader_hdr->spi_speed) {
    case ESP_IMAGE_SPI_SPEED_40M:
        spiSpeed = 40;
        break;
    case ESP_IMAGE_SPI_SPEED_26M:
        spiSpeed = 26;  /* Actually 26.7 */
        break;
    case ESP_IMAGE_SPI_SPEED_80M:
        spiSpeed = 80;
        break;
    }
    /* The image header tops out at 16MB, so the exponent runs to 4. */
    u8 flashSize = 2;
    if (bootloader_hdr->spi_size <= 4) {
        flashSize = 1 << bootloader_hdr->spi_size;
    }
    LTBootPlatform_printf("Flash size: %uMB SPI: %uMHz\n", flashSize, spiSpeed);
}

static void IRAM_ATTR bootloader_init_flash_configure(void)
{
    /* No pin or dummy configuration here, unlike the earlier parts.  The esp32p4
     * MSPI pads are dedicated rather than routed through the IO matrix, the ROM
     * exports no esp_rom_efuse_get_flash_gpio_info(), and there is no
     * SPI_MEM_C_FDUMMY_OUT to set. */
    bootloader_flash_cs_timing_config();
}

static esp_err_t bootloader_init_spi_flash(void)
{
    bootloader_init_flash_configure();

    bootloader_flash_unlock();

#if CONFIG_ESPTOOLPY_FLASHMODE_QIO || CONFIG_ESPTOOLPY_FLASHMODE_QOUT
    bootloader_enable_qio_mode();
#endif

    print_flash_info(&bootloader_image_hdr);
    update_flash_config(&bootloader_image_hdr);
    //ensure the flash is write-protected
    bootloader_enable_wp();
    return ESP_OK;
}

static void wdt_reset_cpu0_info_enable(void)
{
    /* Where the esp32c3 ungates assist-debug through SYSTEM_CPU_PERI_CLK_EN_REG,
     * the esp32p4 needs both the bus-monitor clock and the block's own gate. */
    REG_SET_BIT(HP_SYS_CLKRST_SOC_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_BUSMON_CPU_CLK_EN);
    REG_SET_BIT(ASSIST_DEBUG_CLOCK_GATE_REG, ASSIST_DEBUG_CLK_EN);
    REG_WRITE(ASSIST_DEBUG_CORE_0_RCD_EN_REG, ASSIST_DEBUG_CORE_0_RCD_PDEBUGEN | ASSIST_DEBUG_CORE_0_RCD_RECORDEN);
}

static void wdt_reset_info_dump(int cpu)
{
    (void) cpu;
    // saved PC was already printed by the ROM bootloader.
    // nothing to do here.
}

static void bootloader_check_wdt_reset(void)
{
    int wdt_rst = 0;
    soc_reset_reason_t rst_reason = esp_rom_get_reset_reason(0);
    if (rst_reason == RESET_REASON_CPU_MWDT || rst_reason == RESET_REASON_CPU_RWDT ||
        rst_reason == RESET_REASON_CORE_MWDT || rst_reason == RESET_REASON_CORE_RWDT ||
        rst_reason == RESET_REASON_SYS_RWDT) {
        ESP_LOGW(TAG, "PRO CPU has been reset by WDT.");
        wdt_rst = 1;
    }
    if (wdt_rst) {
        // if reset by WDT dump info from trace port
        wdt_reset_info_dump(0);
    }
    wdt_reset_cpu0_info_enable();
}

static void bootloader_super_wdt_auto_feed(void)
{
    /* The super watchdog moved from the RTC_CNTL block to LP_WDT on this part. */
    REG_WRITE(LP_WDT_SWD_WPROTECT_REG, LP_WDT_SWD_WKEY_VALUE);
    REG_SET_BIT(LP_WDT_SWD_CONFIG_REG, LP_WDT_SWD_AUTO_FEED_EN);
    REG_WRITE(LP_WDT_SWD_WPROTECT_REG, 0);
}

static inline void bootloader_init_mspi_clock(void)
{
    /* SPLL, divided by 6.  The divider field holds n-1. */
    REG_SET_BIT(HP_SYS_CLKRST_SOC_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_FLASH_SYS_CLK_EN);
    REG_SET_BIT(HP_SYS_CLKRST_PERI_CLK_CTRL00_REG, HP_SYS_CLKRST_REG_FLASH_PLL_CLK_EN);
    REG_SET_FIELD(HP_SYS_CLKRST_PERI_CLK_CTRL00_REG, HP_SYS_CLKRST_REG_FLASH_CLK_SRC_SEL, 1);
    REG_SET_BIT(HP_SYS_CLKRST_PERI_CLK_CTRL00_REG, HP_SYS_CLKRST_REG_FLASH_CORE_CLK_EN);
    REG_SET_FIELD(HP_SYS_CLKRST_PERI_CLK_CTRL00_REG, HP_SYS_CLKRST_REG_FLASH_CORE_CLK_DIV_NUM, 5);
}

static inline void bootloader_hardware_init(void)
{
    /* The esp32p4 ROM exports no regi2c entries, so the analog bus is driven by
     * the LT copy in esp_rom_regi2c_esp32p4.c.  Its master needs the 160MHz
     * source selected first. */
    REG_SET_BIT(I2C_ANA_MST_CLK160M_REG, I2C_ANA_MST_CLK_I2C_MST_SEL_160M);

    unsigned chip_version = efuse_hal_chip_revision();
    if (!ESP_CHIP_REV_ABOVE(chip_version, 1)) {
        /* ECO0 erratum: the PLLs come up too fast to be stable.  Drop cpu_pll to
         * 400MHz and sys_pll to 480MHz and let them settle. */
        REGI2C_WRITE_MASK(I2C_CPLL, I2C_CPLL_OC_DIV_7_0, 6);
        REGI2C_WRITE_MASK(I2C_SYSPLL, I2C_SYSPLL_OC_DIV_7_0, 8);
        esp_rom_delay_us(100);
    }
    REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1, 10);
    REGI2C_WRITE_MASK(I2C_BIAS, I2C_BIAS_DREG_1P1_PVT, 10);

    if (ESP_CHIP_REV_ABOVE(chip_version, 1)) {
        /* ECO0 cannot drive the MSPI off the PLL - it stays on the ROM's clock. */
        bootloader_init_mspi_clock();
    }
}

static inline void bootloader_ana_reset_config(void)
{
    //Enable super WDT reset.
    bootloader_ana_super_wdt_reset_config(true);

    /* Brownout reset, which is brownout_ll_ana_reset_enable(true) upstream: clear
     * the FIB override before arming BOD mode 1. */
    REG_CLR_BIT(LP_ANALOG_PERI_FIB_ENABLE_REG, LP_ANALOG_PERI_LP_ANA_FIB_BOD_RST);
    REG_SET_BIT(LP_ANALOG_PERI_BOD_MODE1_CNTL_REG, LP_ANALOG_PERI_BOD_MODE1_RESET_ENA);
}

esp_err_t bootloader_init(void)
{
    esp_err_t ret = ESP_OK;

    bootloader_hardware_init();
    bootloader_ana_reset_config();
    bootloader_super_wdt_auto_feed();
    // protect memory region
    bootloader_init_mem();
    /* check that static RAM is after the stack */
    assert(&_bss_start <= &_bss_end);
    assert(&_data_start <= &_data_end);
    // clear bss section
    bootloader_clear_bss_section();
    // reset MMU
    bootloader_reset_mmu();
    // config clock
    bootloader_clock_configure();
    // initialize console, from now on, we can use esp_log
    bootloader_console_init();
    // update flash ID
    bootloader_flash_update_id();
    // Check and run XMC startup flow
    if ((ret = bootloader_flash_xmc_startup()) != ESP_OK) {
        ESP_LOGE(TAG, "failed when running XMC startup flow, reboot!");
        goto err;
    }
    // read bootloader header
    if ((ret = bootloader_read_bootloader_header()) != ESP_OK) {
        goto err;
    }
    // initialize spi flash
    if ((ret = bootloader_init_spi_flash()) != ESP_OK) {
        goto err;
    }
    // check whether a WDT reset happend
    bootloader_check_wdt_reset();
    // config WDT
    bootloader_config_wdt();
    // enable RNG early entropy source
    bootloader_enable_random();
err:
    return ret;
}
