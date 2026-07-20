/**
 * @file ssd1306.c
 * @brief Hardware implementation for SSD1306 OLED matching display_driver HAL.
 */

#include "drivers/ic/ssd1306.h"
#include "bus/i2c_bus.h"
#include "esp_log.h"
#include "string.h"

static const char *TAG = "ssd1306_driver";

// State internal driver (Singleton Pattern)
static i2c_bus_t              *s_i2c_bus        = NULL;
static i2c_master_dev_handle_t s_dev_handle     = NULL;
static uint16_t                s_scr_width      = 128;
static uint16_t                s_scr_height     = 64;
static bool                    s_is_initialized = false;

// Basic ASCII Font 8x8 bitmap lookup table (0x20 to 0x5F)
static const uint8_t font_8x8[][8] = {
    [0x00]       = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // Space (0x20)
    ['!' - 0x20] = {0x00, 0x00, 0x5f, 0x00, 0x00, 0x00, 0x00, 0x00},
    // Numbers
    ['0' - 0x20] = {0x3e, 0x51, 0x49, 0x45, 0x3e, 0x00, 0x00, 0x00},
    ['1' - 0x20] = {0x00, 0x42, 0x7f, 0x40, 0x00, 0x00, 0x00, 0x00},
    ['2' - 0x20] = {0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00, 0x00},
    ['3' - 0x20] = {0x21, 0x41, 0x45, 0x4b, 0x31, 0x00, 0x00, 0x00},
    ['4' - 0x20] = {0x18, 0x14, 0x12, 0x7f, 0x10, 0x00, 0x00, 0x00},
    ['5' - 0x20] = {0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00, 0x00},
    // Alphabets
    ['A' - 0x20] = {0x7c, 0x12, 0x11, 0x12, 0x7c, 0x00, 0x00, 0x00},
    ['B' - 0x20] = {0x7f, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00},
    ['C' - 0x20] = {0x3e, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00, 0x00},
    ['D' - 0x20] = {0x7f, 0x41, 0x41, 0x41, 0x3e, 0x00, 0x00, 0x00},
    ['E' - 0x20] = {0x7f, 0x49, 0x49, 0x49, 0x41, 0x00, 0x00, 0x00},
    ['H' - 0x20] = {0x7f, 0x08, 0x08, 0x08, 0x7f, 0x00, 0x00, 0x00},
    ['K' - 0x20] = {0x7f, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00},
    ['M' - 0x20] = {0x7f, 0x02, 0x0c, 0x02, 0x7f, 0x00, 0x00, 0x00},
    ['O' - 0x20] = {0x3e, 0x41, 0x41, 0x41, 0x3e, 0x00, 0x00, 0x00},
    ['P' - 0x20] = {0x7f, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00},
    ['R' - 0x20] = {0x7f, 0x09, 0x19, 0x29, 0x46, 0x00, 0x00, 0x00},
    ['S' - 0x20] = {0x46, 0x49, 0x49, 0x49, 0x31, 0x00, 0x00, 0x00},
    ['T' - 0x20] = {0x01, 0x01, 0x7f, 0x01, 0x01, 0x00, 0x00, 0x00},
    ['V' - 0x20] = {0x1f, 0x20, 0x40, 0x20, 0x1f, 0x00, 0x00, 0x00},
    ['W' - 0x20] = {0x3f, 0x40, 0x38, 0x40, 0x3f, 0x00, 0x00, 0x00},
    ['Y' - 0x20] = {0x03, 0x04, 0x78, 0x04, 0x03, 0x00, 0x00, 0x00},
};

/**
 * @brief Helper untuk mengirim satu byte perintah ke SSD1306
 */
static esp_err_t _ssd1306_send_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd}; // 0x00 = Control byte untuk single command
    return i2c_bus_write(s_i2c_bus, s_dev_handle, buf, sizeof(buf));
}

/**
 * @brief Inisialisasi IC SSD1306 via Jalur I2C Master Bus
 */
static esp_err_t ssd1306_init_hw(const display_config_t *cfg) {
    if (cfg == NULL || cfg->bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_i2c_bus    = cfg->bus;
    s_scr_width  = cfg->width;
    s_scr_height = cfg->height;

    // 1. Daftarkan device SSD1306 ke registry I2C bus kamu
    esp_err_t err =
        i2c_bus_add_device(s_i2c_bus, cfg->i2c_addr, 400000, "SSD1306_OLED", &s_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to attach SSD1306 device to I2C bus.");
        return err;
    }

    // 2. Sequence Command Init SSD1306 (Standard 128x64 Configuration)
    uint8_t init_cmds[] = {
        0xAE,       // Display OFF (Sleep Mode)
        0xD5, 0x80, // Set Display Clock Divide Ratio
        0xA8, 0x3F, // Set Multiplex Ratio (128x64 standard)
        0xD3, 0x00, // Set Display Offset to 0
        0x40,       // Set Display Start Line to 0
        0x8D, 0x14, // Charge Pump Enable (Wajib untuk layar OLED menyala)
        0x20, 0x02, // Memory Addressing Mode: Set ke Page Addressing Mode
        0xA1,       // Set Segment Re-map (Horizontal Flip biar tidak terbalik)
        0xC8,       // Set COM Output Scan Direction (Vertical Flip)
        0xDA, 0x12, // Set COM Pins Hardware Configuration
        0x81, 0xCF, // Set Contrast Control (Default Brightness cukup tinggi)
        0xD9, 0xF1, // Set Pre-charge Period
        0xDB, 0x40, // Set VCOMH Deselect Level
        0xA4,       // Entire Display ON (Resume to RAM content)
        0xA6,       // Set Normal Display (Bukan Inverted)
        0xAF        // Display ON (Wake up!)
    };

    for (size_t i = 0; i < sizeof(init_cmds); i++) {
        err = _ssd1306_send_cmd(init_cmds[i]);
        if (err != ESP_OK)
            return err;
    }

    s_is_initialized = true;
    ESP_LOGI(TAG, "SSD1306 physical OLED initialized successfully");

    // Clear screen
    return ssd1306_driver.clear();
}

/**
 * @brief Bersihkan seluruh layar (8 Page / Row)
 */
static esp_err_t ssd1306_clear_hw(void) {
    if (!s_is_initialized)
        return ESP_FAIL;

    uint8_t zero_buffer[129];
    zero_buffer[0] = 0x40; // Control byte untuk sekuensial data stream (RAM data write)
    memset(&zero_buffer[1], 0x00, 128);

    // Sapu bersih ke-8 page yang ada di OLED
    for (uint8_t page = 0; page < 8; page++) {
        _ssd1306_send_cmd(0xB0 + page); // Set target Page Start Address
        _ssd1306_send_cmd(0x00);        // Reset Lower Column Address
        _ssd1306_send_cmd(0x10);        // Reset Higher Column Address

        // Tembak data kosong 128 byte sekaligus per baris
        esp_err_t err = i2c_bus_write(s_i2c_bus, s_dev_handle, zero_buffer, 129);
        if (err != ESP_OK)
            return err;
    }
    return ESP_OK;
}

/**
 * @brief Cetak teks pada baris tertentu menggunakan translasi baris-ke-page
 */
static esp_err_t ssd1306_show_text_hw(uint8_t row, const char *text) {
    if (!s_is_initialized || text == NULL)
        return ESP_FAIL;

    // Karena tinggi font 8px dan tinggi layar 64px, maksimal ada 8 baris (0 - 7)
    if (row >= 8)
        return ESP_ERR_INVALID_ARG;

    // 1. Arahkan pointer kursor internal OLED ke Page target
    _ssd1306_send_cmd(0xB0 + row); // Map row ke Page Address
    _ssd1306_send_cmd(0x00);       // Set Column awal di kiri (Lower Column = 0)
    _ssd1306_send_cmd(0x10);       // Higher Column = 0

    // 2. Buat buffer stream I2C. Ukuran: 1 byte control data + 128 byte data kolom
    uint8_t data_stream[129];
    data_stream[0]    = 0x40; // 0x40 = Tanda data continuous stream ke GDDRAM
    size_t stream_idx = 1;

    size_t text_len  = strlen(text);
    size_t max_chars = s_scr_width / 8; // 128 / 8 = Maksimal 16 karakter font per baris

    // 3. Terjemahkan string teks menjadi visual bitmap kolom demi kolom
    for (size_t c = 0; c < max_chars; c++) {
        uint8_t font_idx = 0; // Default space jika string lebih pendek dari 16 karakter

        if (c < text_len) {
            char ch = text[c];
            // Proteksi range font yang kita punya (ASCII 0x20 s.d 0x5F)
            if (ch >= 0x20 && ch <= 0x5F) {
                font_idx = ch - 0x20;
            }
        }

        // Copy 8 byte pola horizontal font ke stream buffer
        for (int col = 0; col < 8; col++) {
            data_stream[stream_idx++] = font_8x8[font_idx][col];
        }
    }

    // 4. Flush seluruh baris ke layar fisik via I2C Bus dalam satu transaksi tunggal
    return i2c_bus_write(s_i2c_bus, s_dev_handle, data_stream, stream_idx);
}

/**
 * @brief Atur kontras/kecerahan layar (0-255)
 */
static esp_err_t ssd1306_set_brightness_hw(uint8_t level) {
    if (!s_is_initialized)
        return ESP_FAIL;

    esp_err_t err = _ssd1306_send_cmd(0x81); // Perintah ubah kontras
    if (err == ESP_OK) {
        err = _ssd1306_send_cmd(level); // Kirim nilai kecerahannya
    }
    return err;
}

/**
 * @brief De-inisialisasi display
 */
static void ssd1306_deinit_hw(void) {
    if (!s_is_initialized)
        return;

    _ssd1306_send_cmd(0xAE); // Matikan layar fisik sebelum mati total demi mematikan charge pump VCC
    s_is_initialized = false;
    s_i2c_bus        = NULL;
    s_dev_handle     = NULL;
    ESP_LOGI(TAG, "SSD1306 driver deactivated.");
}

/* --- Vtable Registration --- */
const display_driver_t ssd1306_driver = {.name           = "SSD1306_PHYSICAL",
                                         .init           = ssd1306_init_hw,
                                         .clear          = ssd1306_clear_hw,
                                         .show_text      = ssd1306_show_text_hw,
                                         .set_brightness = ssd1306_set_brightness_hw,
                                         .deinit         = ssd1306_deinit_hw};