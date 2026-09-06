#include "esp_lcd_touch_axs5106l.h"

static TwoWire *g_touch_i2c;
static uint16_t g_width;
static uint16_t g_height;
static uint16_t g_rotation;
static touch_data_t g_touch_data;
static volatile bool g_touch_int_flag = false;

static bool touch_i2c_read(uint8_t driver_addr, uint8_t reg_addr, uint8_t *data, uint32_t length) {
  g_touch_i2c->beginTransmission(driver_addr);
  g_touch_i2c->write(reg_addr);
  if (g_touch_i2c->endTransmission() != 0) {
    return false;
  }
  g_touch_i2c->requestFrom(driver_addr, length);
  if (g_touch_i2c->available() != static_cast<int>(length)) {
    return false;
  }
  g_touch_i2c->readBytes(data, length);
  return true;
}

static void IRAM_ATTR touch_int_cb() {
  g_touch_int_flag = true;
}

void bsp_touch_init(TwoWire *touch_i2c, int tp_rst, int tp_int, uint16_t rotation, uint16_t width, uint16_t height) {
  g_touch_i2c = touch_i2c;
  g_width = width;
  g_height = height;
  g_rotation = rotation;

  pinMode(tp_rst, OUTPUT);
  digitalWrite(tp_rst, LOW);
  delay(200);
  digitalWrite(tp_rst, HIGH);
  delay(300);

  pinMode(tp_int, INPUT);
  attachInterrupt(tp_int, touch_int_cb, FALLING);

  uint8_t data[3] = {0};
  touch_i2c_read(AXS5106L_ADDR, AXS5106L_ID_REG, data, 3);
}

void bsp_touch_read() {
  uint8_t data[14] = {0};
  g_touch_data.touch_num = 0;

  if (!g_touch_int_flag) {
    return;
  }
  g_touch_int_flag = false;

  touch_i2c_read(AXS5106L_ADDR, AXS5106L_TOUCH_DATA_REG, data, 14);

  g_touch_data.touch_num = data[1];
  if (g_touch_data.touch_num == 0) {
    return;
  }
  for (uint8_t i = 0; i < g_touch_data.touch_num; i++) {
    g_touch_data.coords[i].x = (static_cast<uint16_t>(data[2 + i * 6] & 0x0f)) << 8;
    g_touch_data.coords[i].x |= data[3 + i * 6];
    g_touch_data.coords[i].y = (static_cast<uint16_t>(data[4 + i * 6] & 0x0f)) << 8;
    g_touch_data.coords[i].y |= data[5 + i * 6];
  }
}

bool bsp_touch_get_coordinates(touch_data_t *touch_data) {
  if (touch_data == nullptr || g_touch_data.touch_num == 0) {
    return false;
  }

  for (uint8_t i = 0; i < g_touch_data.touch_num; i++) {
    switch (g_rotation) {
      case 1:
        touch_data->coords[i].y = g_touch_data.coords[i].x;
        touch_data->coords[i].x = g_touch_data.coords[i].y;
        break;
      case 2:
        touch_data->coords[i].x = g_touch_data.coords[i].x;
        touch_data->coords[i].y = g_height - 1 - g_touch_data.coords[i].y;
        break;
      case 3:
        touch_data->coords[i].y = g_height - 1 - g_touch_data.coords[i].x;
        touch_data->coords[i].x = g_width - 1 - g_touch_data.coords[i].y;
        break;
      default:
        touch_data->coords[i].x = g_width - 1 - g_touch_data.coords[i].x;
        touch_data->coords[i].y = g_touch_data.coords[i].y;
        break;
    }
  }
  touch_data->touch_num = g_touch_data.touch_num;
  return true;
}
