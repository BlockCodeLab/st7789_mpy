#define __ST7789_VERSION__ "0.2.0"

#include "py/builtin.h"
#include "py/mphal.h"
#include "py/obj.h"
#include "py/runtime.h"

// Fix for MicroPython > 1.21 https://github.com/ricksorensen
#if MICROPY_VERSION_MAJOR >= 1 && MICROPY_VERSION_MINOR > 21
#include "extmod/modmachine.h"
#else
#include "extmod/machine_spi.h"
#endif

#include "st7789.h"

#define _swap_int16_t(a, b)                                                    \
  {                                                                            \
    int16_t t = a;                                                             \
    a = b;                                                                     \
    b = t;                                                                     \
  }
#define _swap_bytes(val) ((((val) >> 8) & 0x00FF) | (((val) << 8) & 0xFF00))

#define ABS(N) (((N) < 0) ? (-(N)) : (N))
#define mp_hal_delay_ms(delay) (mp_hal_delay_us(delay * 1000))

// GPIO_NUM_NC is not defined in all ports, you may have to change this to
// a different value or type depending on your port. This works for esp32,
// stm32, and the samd ports that I've tested.

#ifndef GPIO_NUM_NC
#ifdef STM32_HAL_H
#define GPIO_NUM_NC NULL
#else
#define GPIO_NUM_NC -1
#endif
#endif

#define CS_LOW()                                                               \
  {                                                                            \
    if (self->cs != GPIO_NUM_NC) {                                             \
      mp_hal_pin_write(self->cs, 0);                                           \
    }                                                                          \
  }

#define CS_HIGH()                                                              \
  {                                                                            \
    if (self->cs != GPIO_NUM_NC) {                                             \
      mp_hal_pin_write(self->cs, 1);                                           \
    }                                                                          \
  }

#define DC_LOW() (mp_hal_pin_write(self->dc, 0))
#define DC_HIGH() (mp_hal_pin_write(self->dc, 1))

#define RESET_LOW()                                                            \
  {                                                                            \
    if (self->reset != GPIO_NUM_NC) {                                          \
      mp_hal_pin_write(self->reset, 0);                                        \
    }                                                                          \
  }

#define RESET_HIGH()                                                           \
  {                                                                            \
    if (self->reset != GPIO_NUM_NC) {                                          \
      mp_hal_pin_write(self->reset, 1);                                        \
    }                                                                          \
  }

//
// Default st7789 and st7735 display orientation tables
// can be overridden during init(), madctl values
// will be combined with color_mode
//

// { madctl, width, height, colstart, rowstart }

st7789_rotation_t ORIENTATIONS_240x320[4] = {{0x00, 240, 320, 0, 0},
                                             {0x60, 320, 240, 0, 0},
                                             {0xc0, 240, 320, 0, 0},
                                             {0xa0, 320, 240, 0, 0}};

st7789_rotation_t ORIENTATIONS_240x280[4] = {{0x00, 240, 280, 0, 20},
                                             {0x60, 280, 240, 20, 0},
                                             {0xc0, 240, 280, 0, 20},
                                             {0xa0, 280, 240, 20, 0}};

st7789_rotation_t ORIENTATIONS_170x320[4] = {{0x00, 170, 320, 35, 0},
                                             {0x60, 320, 170, 0, 35},
                                             {0xc0, 170, 320, 35, 0},
                                             {0xa0, 320, 170, 0, 35}};

st7789_rotation_t ORIENTATIONS_240x240[4] = {{0x00, 240, 240, 0, 0},
                                             {0x60, 240, 240, 0, 0},
                                             {0xc0, 240, 240, 0, 80},
                                             {0xa0, 240, 240, 80, 0}};

st7789_rotation_t ORIENTATIONS_135x240[4] = {{0x00, 135, 240, 52, 40},
                                             {0x60, 240, 135, 40, 53},
                                             {0xc0, 135, 240, 53, 40},
                                             {0xa0, 240, 135, 40, 52}};

st7789_rotation_t ORIENTATIONS_128x160[4] = {{0x00, 128, 160, 0, 0},
                                             {0x60, 160, 128, 0, 0},
                                             {0xc0, 128, 160, 0, 0},
                                             {0xa0, 160, 128, 0, 0}};

st7789_rotation_t ORIENTATIONS_80x160[4] = {{0x00, 80, 160, 26, 1},
                                            {0x60, 160, 80, 1, 26},
                                            {0xc0, 80, 160, 26, 1},
                                            {0xa0, 160, 80, 1, 26}};

st7789_rotation_t ORIENTATIONS_128x128[4] = {{0x00, 128, 128, 2, 1},
                                             {0x60, 128, 128, 1, 2},
                                             {0xc0, 128, 128, 2, 3},
                                             {0xa0, 128, 128, 3, 2}};

static void st7789_ST7789_print(const mp_print_t *print, mp_obj_t self_in,
                                mp_print_kind_t kind) {
  (void)kind;
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
  mp_printf(print, "<ST7789 width=%u, height=%u, spi=%p>", self->width,
            self->height, self->spi_obj);
}

static void write_spi(mp_obj_base_t *spi_obj, const uint8_t *buf, int len) {
#ifdef MP_OBJ_TYPE_GET_SLOT
  mp_machine_spi_p_t *spi_p =
      (mp_machine_spi_p_t *)MP_OBJ_TYPE_GET_SLOT(spi_obj->type, protocol);
#else
  mp_machine_spi_p_t *spi_p = (mp_machine_spi_p_t *)spi_obj->type->protocol;
#endif
  spi_p->transfer(spi_obj, len, buf, NULL);
}

static void write_cmd(st7789_ST7789_obj_t *self, uint8_t cmd,
                      const uint8_t *data, int len) {
  CS_LOW()
  if (cmd) {
    DC_LOW();
    write_spi(self->spi_obj, &cmd, 1);
  }
  if (len > 0) {
    DC_HIGH();
    write_spi(self->spi_obj, data, len);
  }
  CS_HIGH()
}

static mp_obj_t st7789_ST7789_write(mp_obj_t self_in, mp_obj_t command,
                                    mp_obj_t data) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

  mp_buffer_info_t src;
  if (data == mp_const_none) {
    write_cmd(self, (uint8_t)mp_obj_get_int(command), NULL, 0);
  } else {
    mp_get_buffer_raise(data, &src, MP_BUFFER_READ);
    write_cmd(self, (uint8_t)mp_obj_get_int(command), (const uint8_t *)src.buf,
              src.len);
  }

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(st7789_ST7789_write_obj, st7789_ST7789_write);

static void set_window(st7789_ST7789_obj_t *self, uint16_t x0, uint16_t y0,
                       uint16_t x1, uint16_t y1) {
  if (x0 > x1 || x1 >= self->width) {
    return;
  }
  if (y0 > y1 || y1 >= self->height) {
    return;
  }

  if (self->bounding) {
    if (x0 < self->min_x) {
      self->min_x = x0;
    }
    if (x1 > self->max_x) {
      self->max_x = x1;
    }
    if (y0 < self->min_y) {
      self->min_y = y0;
    }
    if (y1 > self->max_y) {
      self->max_y = y1;
    }
  }

  uint8_t bufx[4] = {(x0 + self->colstart) >> 8, (x0 + self->colstart) & 0xFF,
                     (x1 + self->colstart) >> 8, (x1 + self->colstart) & 0xFF};
  uint8_t bufy[4] = {(y0 + self->rowstart) >> 8, (y0 + self->rowstart) & 0xFF,
                     (y1 + self->rowstart) >> 8, (y1 + self->rowstart) & 0xFF};
  write_cmd(self, ST7789_CASET, bufx, 4);
  write_cmd(self, ST7789_RASET, bufy, 4);
  write_cmd(self, ST7789_RAMWR, NULL, 0);
}

static mp_obj_t st7789_ST7789_set_window(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t x0 = mp_obj_get_int(args[1]);
  mp_int_t y0 = mp_obj_get_int(args[2]);
  mp_int_t x1 = mp_obj_get_int(args[3]);
  mp_int_t y1 = mp_obj_get_int(args[4]);
  set_window(self, x0, y0, x1, y1);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_set_window_obj, 5, 5,
                                           st7789_ST7789_set_window);

static mp_obj_t st7789_ST7789_hard_reset(mp_obj_t self_in) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

  CS_LOW();
  RESET_HIGH();
  mp_hal_delay_ms(50);
  RESET_LOW();
  mp_hal_delay_ms(50);
  RESET_HIGH();
  mp_hal_delay_ms(150);
  CS_HIGH();
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_hard_reset_obj,
                                 st7789_ST7789_hard_reset);

static mp_obj_t st7789_ST7789_soft_reset(mp_obj_t self_in) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

  write_cmd(self, ST7789_SWRESET, NULL, 0);
  mp_hal_delay_ms(150);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_soft_reset_obj,
                                 st7789_ST7789_soft_reset);

static mp_obj_t st7789_ST7789_sleep_mode(mp_obj_t self_in, mp_obj_t value) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
  if (mp_obj_is_true(value)) {
    write_cmd(self, ST7789_SLPIN, NULL, 0);
  } else {
    write_cmd(self, ST7789_SLPOUT, NULL, 0);
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(st7789_ST7789_sleep_mode_obj,
                                 st7789_ST7789_sleep_mode);

static mp_obj_t st7789_ST7789_inversion_mode(mp_obj_t self_in, mp_obj_t value) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

  self->inversion = mp_obj_is_true(value);
  if (self->inversion) {
    write_cmd(self, ST7789_INVON, NULL, 0);
  } else {
    write_cmd(self, ST7789_INVOFF, NULL, 0);
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(st7789_ST7789_inversion_mode_obj,
                                 st7789_ST7789_inversion_mode);

static void fill_color_buffer(mp_obj_base_t *spi_obj, uint16_t color,
                              int length) {
  const int buffer_pixel_size = 128;
  int chunks = length / buffer_pixel_size;
  int rest = length % buffer_pixel_size;
  uint16_t color_swapped = _swap_bytes(color);
  uint16_t buffer[buffer_pixel_size]; // 128 pixels

  // fill buffer with color data

  for (int i = 0; i < length && i < buffer_pixel_size; i++) {
    buffer[i] = color_swapped;
  }
  if (chunks) {
    for (int j = 0; j < chunks; j++) {
      write_spi(spi_obj, (uint8_t *)buffer, buffer_pixel_size * 2);
    }
  }
  if (rest) {
    write_spi(spi_obj, (uint8_t *)buffer, rest * 2);
  }
}

int mod(int x, int m) {
  int r = x % m;
  return (r < 0) ? r + m : r;
}

static void draw_pixel(st7789_ST7789_obj_t *self, int16_t x, int16_t y,
                       uint16_t color) {
  if ((self->options & OPTIONS_WRAP)) {
    if ((self->options & OPTIONS_WRAP_H) && ((x >= self->width) || (x < 0))) {
      x = mod(x, self->width);
    }
    if ((self->options & OPTIONS_WRAP_V) && ((y >= self->height) || (y < 0))) {
      y = mod(y, self->height);
    }
  }

  if ((x < self->width) && (y < self->height) && (x >= 0) && (y >= 0)) {
    uint8_t hi = color >> 8, lo = color & 0xff;
    set_window(self, x, y, x, y);
    DC_HIGH();
    CS_LOW();
    write_spi(self->spi_obj, &hi, 1);
    write_spi(self->spi_obj, &lo, 1);
    CS_HIGH();
  }
}

static mp_obj_t st7789_ST7789_pixel(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t x = mp_obj_get_int(args[1]);
  mp_int_t y = mp_obj_get_int(args[2]);
  mp_int_t color = mp_obj_get_int(args[3]);

  draw_pixel(self, x, y, color);

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_pixel_obj, 4, 4,
                                           st7789_ST7789_pixel);

static void fast_hline(st7789_ST7789_obj_t *self, int16_t x, int16_t y,
                       int16_t w, uint16_t color) {
  if ((self->options & OPTIONS_WRAP) == 0) {
    if (y >= 0 && self->width > x && self->height > y) {
      if (0 > x) {
        w += x;
        x = 0;
      }

      if (self->width < x + w) {
        w = self->width - x;
      }

      if (w > 0) {
        int16_t x2 = x + w - 1;
        set_window(self, x, y, x2, y);
        DC_HIGH();
        CS_LOW();
        fill_color_buffer(self->spi_obj, color, w);
        CS_HIGH();
      }
    }
  } else {
    for (int d = 0; d < w; d++) {
      draw_pixel(self, x + d, y, color);
    }
  }
}

static mp_obj_t st7789_ST7789_hline(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t x = mp_obj_get_int(args[1]);
  mp_int_t y = mp_obj_get_int(args[2]);
  mp_int_t w = mp_obj_get_int(args[3]);
  mp_int_t color = mp_obj_get_int(args[4]);

  fast_hline(self, x, y, w, color);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_hline_obj, 5, 5,
                                           st7789_ST7789_hline);

static void fast_vline(st7789_ST7789_obj_t *self, int16_t x, int16_t y,
                       int16_t h, uint16_t color) {
  if ((self->options & OPTIONS_WRAP) == 0) {
    if (x >= 0 && self->width > x && self->height > y) {
      if (0 > y) {
        h += y;
        y = 0;
      }

      if (self->height < y + h) {
        h = self->height - y;
      }

      if (h > 0) {
        int16_t y2 = y + h - 1;
        set_window(self, x, y, x, y2);
        DC_HIGH();
        CS_LOW();
        fill_color_buffer(self->spi_obj, color, h);
        CS_HIGH();
      }
    }
  } else {
    for (int d = 0; d < h; d++) {
      draw_pixel(self, x, y + d, color);
    }
  }
}

static mp_obj_t st7789_ST7789_vline(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t x = mp_obj_get_int(args[1]);
  mp_int_t y = mp_obj_get_int(args[2]);
  mp_int_t w = mp_obj_get_int(args[3]);
  mp_int_t color = mp_obj_get_int(args[4]);

  fast_vline(self, x, y, w, color);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_vline_obj, 5, 5,
                                           st7789_ST7789_vline);

static mp_obj_t st7789_ST7789_rect(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t x = mp_obj_get_int(args[1]);
  mp_int_t y = mp_obj_get_int(args[2]);
  mp_int_t w = mp_obj_get_int(args[3]);
  mp_int_t h = mp_obj_get_int(args[4]);
  mp_int_t color = mp_obj_get_int(args[5]);

  fast_hline(self, x, y, w, color);
  fast_vline(self, x, y, h, color);
  fast_hline(self, x, y + h - 1, w, color);
  fast_vline(self, x + w - 1, y, h, color);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_rect_obj, 6, 6,
                                           st7789_ST7789_rect);

static mp_obj_t st7789_ST7789_fill_rect(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t x = mp_obj_get_int(args[1]);
  mp_int_t y = mp_obj_get_int(args[2]);
  mp_int_t w = mp_obj_get_int(args[3]);
  mp_int_t h = mp_obj_get_int(args[4]);
  mp_int_t color = mp_obj_get_int(args[5]);

  uint16_t right = x + w - 1;
  uint16_t bottom = y + h - 1;

  if (x < self->width && y < self->height) {
    if (right > self->width) {
      right = self->width;
    }

    if (bottom > self->height) {
      bottom = self->height;
    }

    set_window(self, x, y, right, bottom);
    DC_HIGH();
    CS_LOW();
    fill_color_buffer(self->spi_obj, color, w * h);
    CS_HIGH();
  }
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_fill_rect_obj, 6, 6,
                                           st7789_ST7789_fill_rect);

static mp_obj_t st7789_ST7789_fill(mp_obj_t self_in, mp_obj_t _color) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
  mp_int_t color = mp_obj_get_int(_color);

  set_window(self, 0, 0, self->width - 1, self->height - 1);
  DC_HIGH();
  CS_LOW();
  fill_color_buffer(self->spi_obj, color, self->width * self->height);
  CS_HIGH();

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(st7789_ST7789_fill_obj, st7789_ST7789_fill);

static void line(st7789_ST7789_obj_t *self, int16_t x0, int16_t y0, int16_t x1,
                 int16_t y1, int16_t color) {
  bool steep = ABS(y1 - y0) > ABS(x1 - x0);
  if (steep) {
    _swap_int16_t(x0, y0);
    _swap_int16_t(x1, y1);
  }

  if (x0 > x1) {
    _swap_int16_t(x0, x1);
    _swap_int16_t(y0, y1);
  }

  int16_t dx = x1 - x0, dy = ABS(y1 - y0);
  int16_t err = dx >> 1, ystep = -1, xs = x0, dlen = 0;

  if (y0 < y1) {
    ystep = 1;
  }

  // Split into steep and not steep for FastH/V separation
  if (steep) {
    for (; x0 <= x1; x0++) {
      dlen++;
      err -= dy;
      if (err < 0) {
        err += dx;
        if (dlen == 1) {
          draw_pixel(self, y0, xs, color);
        } else {
          fast_vline(self, y0, xs, dlen, color);
        }
        dlen = 0;
        y0 += ystep;
        xs = x0 + 1;
      }
    }
    if (dlen) {
      fast_vline(self, y0, xs, dlen, color);
    }
  } else {
    for (; x0 <= x1; x0++) {
      dlen++;
      err -= dy;
      if (err < 0) {
        err += dx;
        if (dlen == 1) {
          draw_pixel(self, xs, y0, color);
        } else {
          fast_hline(self, xs, y0, dlen, color);
        }
        dlen = 0;
        y0 += ystep;
        xs = x0 + 1;
      }
    }
    if (dlen) {
      fast_hline(self, xs, y0, dlen, color);
    }
  }
}

static mp_obj_t st7789_ST7789_line(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t x0 = mp_obj_get_int(args[1]);
  mp_int_t y0 = mp_obj_get_int(args[2]);
  mp_int_t x1 = mp_obj_get_int(args[3]);
  mp_int_t y1 = mp_obj_get_int(args[4]);
  mp_int_t color = mp_obj_get_int(args[5]);

  line(self, x0, y0, x1, y1, color);

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_line_obj, 6, 6,
                                           st7789_ST7789_line);

static mp_obj_t st7789_ST7789_blit_buffer(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_buffer_info_t buf_info;
  mp_get_buffer_raise(args[1], &buf_info, MP_BUFFER_READ);
  mp_int_t x = mp_obj_get_int(args[2]);
  mp_int_t y = mp_obj_get_int(args[3]);
  mp_int_t w = mp_obj_get_int(args[4]);
  mp_int_t h = mp_obj_get_int(args[5]);

  set_window(self, x, y, x + w - 1, y + h - 1);
  DC_HIGH();
  CS_LOW();

  const int buf_size = 256;
  int limit = MIN(buf_info.len, w * h * 2);
  int chunks = limit / buf_size;
  int rest = limit % buf_size;
  int i = 0;
  for (; i < chunks; i++) {
    write_spi(self->spi_obj, (const uint8_t *)buf_info.buf + i * buf_size,
              buf_size);
  }
  if (rest) {
    write_spi(self->spi_obj, (const uint8_t *)buf_info.buf + i * buf_size,
              rest);
  }
  CS_HIGH();

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_blit_buffer_obj, 6, 6,
                                           st7789_ST7789_blit_buffer);

// 0=Portrait, 1=Landscape, 2=Reverse Portrait (180), 3=Reverse Landscape (180)

static void set_rotation(st7789_ST7789_obj_t *self) {
  uint8_t madctl_value = self->color_order;

  if (self->rotation > self->rotations_len) {
    mp_raise_msg_varg(&mp_type_RuntimeError,
                      MP_ERROR_TEXT("Invalid rotation value %d > %d"),
                      self->rotation, self->rotations_len);
  }

  st7789_rotation_t *rotations = self->rotations;
  if (rotations == NULL) {
    if (self->display_width == 240 && self->display_height == 320) {
      rotations = ORIENTATIONS_240x320;
    } else if (self->display_width == 170 && self->display_height == 320) {
      rotations = ORIENTATIONS_170x320;
    } else if (self->display_width == 240 && self->display_height == 240) {
      rotations = ORIENTATIONS_240x240;
    } else if (self->display_width == 135 && self->display_height == 240) {
      rotations = ORIENTATIONS_135x240;
    } else if (self->display_width == 128 && self->display_height == 160) {
      rotations = ORIENTATIONS_128x160;
    } else if (self->display_width == 80 && self->display_height == 160) {
      rotations = ORIENTATIONS_80x160;
    } else if (self->display_width == 128 && self->display_height == 128) {
      rotations = ORIENTATIONS_128x128;
    }
  }

  if (rotations) {
    st7789_rotation_t *rotation = &rotations[self->rotation];
    madctl_value |= rotation->madctl;
    self->width = rotation->width;
    self->height = rotation->height;
    self->colstart = rotation->colstart;
    self->rowstart = rotation->rowstart;
  }

  self->madctl = madctl_value & 0xff;
  self->min_x = self->width;
  self->min_y = self->height;
  self->max_x = 0;
  self->max_y = 0;

  const uint8_t madctl[] = {madctl_value};
  write_cmd(self, ST7789_MADCTL, madctl, 1);
}

static mp_obj_t st7789_ST7789_rotation(mp_obj_t self_in, mp_obj_t value) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
  mp_int_t rotation = mp_obj_get_int(value) % 4;
  self->rotation = rotation;
  set_rotation(self);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(st7789_ST7789_rotation_obj,
                                 st7789_ST7789_rotation);

static mp_obj_t st7789_ST7789_width(mp_obj_t self_in) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
  return mp_obj_new_int(self->width);
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_width_obj, st7789_ST7789_width);

static mp_obj_t st7789_ST7789_height(mp_obj_t self_in) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
  return mp_obj_new_int(self->height);
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_height_obj,
                                 st7789_ST7789_height);

static mp_obj_t st7789_ST7789_vscrdef(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t tfa = mp_obj_get_int(args[1]);
  mp_int_t vsa = mp_obj_get_int(args[2]);
  mp_int_t bfa = mp_obj_get_int(args[3]);

  uint8_t buf[6] = {(tfa) >> 8,   (tfa) & 0xFF, (vsa) >> 8,
                    (vsa) & 0xFF, (bfa) >> 8,   (bfa) & 0xFF};
  write_cmd(self, ST7789_VSCRDEF, buf, 6);

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_vscrdef_obj, 4, 4,
                                           st7789_ST7789_vscrdef);

static mp_obj_t st7789_ST7789_vscsad(mp_obj_t self_in, mp_obj_t vssa_in) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);
  mp_int_t vssa = mp_obj_get_int(vssa_in);

  uint8_t buf[2] = {(vssa) >> 8, (vssa) & 0xFF};
  write_cmd(self, ST7789_VSCSAD, buf, 2);

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(st7789_ST7789_vscsad_obj,
                                 st7789_ST7789_vscsad);

static void custom_init(st7789_ST7789_obj_t *self) {
  size_t init_len;
  mp_obj_t *init_list;

  mp_obj_get_array(self->custom_init, &init_len, &init_list);

  for (int idx = 0; idx < init_len; idx++) {
    size_t init_cmd_len;
    mp_obj_t *init_cmd;
    mp_obj_get_array(init_list[idx], &init_cmd_len, &init_cmd);
    mp_buffer_info_t init_cmd_data_info;
    if (mp_get_buffer(init_cmd[0], &init_cmd_data_info, MP_BUFFER_READ)) {
      uint8_t *init_cmd_data = (uint8_t *)init_cmd_data_info.buf;

      if (init_cmd_data_info.len > 1) {
        write_cmd(self, init_cmd_data[0], &init_cmd_data[1],
                  init_cmd_data_info.len - 1);
      } else {
        write_cmd(self, init_cmd_data[0], NULL, 0);
      }
      mp_hal_delay_ms(10);

      // check for delay
      if (init_cmd_len > 1) {
        mp_int_t delay = mp_obj_get_int(init_cmd[1]);
        if (delay > 0) {
          mp_hal_delay_ms(delay);
        }
      }
    }
  }
}

static mp_obj_t st7789_ST7789_init(mp_obj_t self_in) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

  st7789_ST7789_hard_reset(self_in);

  if (self->custom_init == MP_OBJ_NULL) {
    st7789_ST7789_soft_reset(self_in);
    write_cmd(self, ST7789_SLPOUT, NULL, 0);

    const uint8_t color_mode[] = {COLOR_MODE_65K | COLOR_MODE_16BIT};
    write_cmd(self, ST7789_COLMOD, color_mode, 1);
    mp_hal_delay_ms(10);

    if (self->inversion) {
      write_cmd(self, ST7789_INVON, NULL, 0);
    } else {
      write_cmd(self, ST7789_INVOFF, NULL, 0);
    }

    write_cmd(self, ST7789_NORON, NULL, 0);
    mp_hal_delay_ms(10);

    write_cmd(self, ST7789_DISPON, NULL, 0);
    mp_hal_delay_ms(150);

  } else {
    custom_init(self);
  }

  set_rotation(self);
  mp_hal_delay_ms(10);

  const mp_obj_t args[] = {self_in,
                           mp_obj_new_int(0),
                           mp_obj_new_int(0),
                           mp_obj_new_int(self->width),
                           mp_obj_new_int(self->height),
                           mp_obj_new_int(BLACK)};

  st7789_ST7789_fill_rect(6, args);

  if (self->backlight != GPIO_NUM_NC) {
    mp_hal_pin_write(self->backlight, 1);
  }

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_init_obj, st7789_ST7789_init);

static mp_obj_t st7789_ST7789_on(mp_obj_t self_in) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

  if (self->backlight != GPIO_NUM_NC) {
    mp_hal_pin_write(self->backlight, 1);
    mp_hal_delay_ms(10);
  }

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_on_obj, st7789_ST7789_on);

static mp_obj_t st7789_ST7789_off(mp_obj_t self_in) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(self_in);

  if (self->backlight != GPIO_NUM_NC) {
    mp_hal_pin_write(self->backlight, 0);
    mp_hal_delay_ms(10);
  }

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(st7789_ST7789_off_obj, st7789_ST7789_off);

static mp_obj_t st7789_ST7789_madctl(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);

  if (n_args == 2) {
    mp_int_t madctl_value = mp_obj_get_int(args[1]) & 0xff;
    const uint8_t madctl[] = {madctl_value};
    write_cmd(self, ST7789_MADCTL, madctl, 1);
    self->madctl = madctl_value & 0xff;
  }
  return mp_obj_new_int(self->madctl);
}

static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_madctl_obj, 1, 2,
                                           st7789_ST7789_madctl);

static mp_obj_t st7789_ST7789_offset(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);
  mp_int_t colstart = mp_obj_get_int(args[1]);
  mp_int_t rowstart = mp_obj_get_int(args[2]);

  self->colstart = colstart;
  self->rowstart = rowstart;

  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_offset_obj, 3, 3,
                                           st7789_ST7789_offset);

static uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}

static mp_obj_t st7789_color565(mp_obj_t r, mp_obj_t g, mp_obj_t b) {
  return MP_OBJ_NEW_SMALL_INT(color565((uint8_t)mp_obj_get_int(r),
                                       (uint8_t)mp_obj_get_int(g),
                                       (uint8_t)mp_obj_get_int(b)));
}
static MP_DEFINE_CONST_FUN_OBJ_3(st7789_color565_obj, st7789_color565);

static void map_bitarray_to_rgb565(uint8_t const *bitarray, uint8_t *buffer,
                                   int length, int width, uint16_t color,
                                   uint16_t bg_color) {
  int row_pos = 0;
  for (int i = 0; i < length; i++) {
    uint8_t byte = bitarray[i];
    for (int bi = 7; bi >= 0; bi--) {
      uint8_t b = byte & (1 << bi);
      uint16_t cur_color = b ? color : bg_color;
      *buffer = (cur_color & 0xff00) >> 8;
      buffer++;
      *buffer = cur_color & 0xff;
      buffer++;

      row_pos++;
      if (row_pos >= width) {
        row_pos = 0;
        break;
      }
    }
  }
}

static mp_obj_t st7789_map_bitarray_to_rgb565(size_t n_args,
                                              const mp_obj_t *args) {
  mp_buffer_info_t bitarray_info;
  mp_buffer_info_t buffer_info;

  mp_get_buffer_raise(args[1], &bitarray_info, MP_BUFFER_READ);
  mp_get_buffer_raise(args[2], &buffer_info, MP_BUFFER_WRITE);
  mp_int_t width = mp_obj_get_int(args[3]);
  mp_int_t color = mp_obj_get_int(args[4]);
  mp_int_t bg_color = mp_obj_get_int(args[5]);
  map_bitarray_to_rgb565(bitarray_info.buf, buffer_info.buf, bitarray_info.len,
                         width, color, bg_color);
  return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_map_bitarray_to_rgb565_obj, 3,
                                           6, st7789_map_bitarray_to_rgb565);

static mp_obj_t st7789_ST7789_bounding(size_t n_args, const mp_obj_t *args) {
  st7789_ST7789_obj_t *self = MP_OBJ_TO_PTR(args[0]);

  mp_obj_t bounds[4] = {mp_obj_new_int(self->min_x),
                        mp_obj_new_int(self->min_y),
                        (n_args > 2 && mp_obj_is_true(args[2]))
                            ? mp_obj_new_int(self->max_x - self->min_x + 1)
                            : mp_obj_new_int(self->max_x),
                        (n_args > 2 && mp_obj_is_true(args[2]))
                            ? mp_obj_new_int(self->max_y - self->min_y + 1)
                            : mp_obj_new_int(self->max_y)};

  if (n_args > 1) {
    if (mp_obj_is_true(args[1])) {
      self->bounding = 1;
    } else {
      self->bounding = 0;
    }

    self->min_x = self->width;
    self->min_y = self->height;
    self->max_x = 0;
    self->max_y = 0;
  }
  return mp_obj_new_tuple(4, bounds);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(st7789_ST7789_bounding_obj, 1, 3,
                                           st7789_ST7789_bounding);

static const mp_rom_map_elem_t st7789_ST7789_locals_dict_table[] = {
    {MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&st7789_ST7789_write_obj)},
    {MP_ROM_QSTR(MP_QSTR_hard_reset),
     MP_ROM_PTR(&st7789_ST7789_hard_reset_obj)},
    {MP_ROM_QSTR(MP_QSTR_soft_reset),
     MP_ROM_PTR(&st7789_ST7789_soft_reset_obj)},
    {MP_ROM_QSTR(MP_QSTR_sleep_mode),
     MP_ROM_PTR(&st7789_ST7789_sleep_mode_obj)},
    {MP_ROM_QSTR(MP_QSTR_inversion_mode),
     MP_ROM_PTR(&st7789_ST7789_inversion_mode_obj)},
    {MP_ROM_QSTR(MP_QSTR_map_bitarray_to_rgb565),
     MP_ROM_PTR(&st7789_map_bitarray_to_rgb565_obj)},
    {MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&st7789_ST7789_init_obj)},
    {MP_ROM_QSTR(MP_QSTR_on), MP_ROM_PTR(&st7789_ST7789_on_obj)},
    {MP_ROM_QSTR(MP_QSTR_off), MP_ROM_PTR(&st7789_ST7789_off_obj)},
    {MP_ROM_QSTR(MP_QSTR_pixel), MP_ROM_PTR(&st7789_ST7789_pixel_obj)},
    {MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&st7789_ST7789_line_obj)},
    {MP_ROM_QSTR(MP_QSTR_blit_buffer),
     MP_ROM_PTR(&st7789_ST7789_blit_buffer_obj)},
    {MP_ROM_QSTR(MP_QSTR_set_window),
     MP_ROM_PTR(&st7789_ST7789_set_window_obj)},
    {MP_ROM_QSTR(MP_QSTR_fill_rect), MP_ROM_PTR(&st7789_ST7789_fill_rect_obj)},
    {MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&st7789_ST7789_fill_obj)},
    {MP_ROM_QSTR(MP_QSTR_hline), MP_ROM_PTR(&st7789_ST7789_hline_obj)},
    {MP_ROM_QSTR(MP_QSTR_vline), MP_ROM_PTR(&st7789_ST7789_vline_obj)},
    {MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&st7789_ST7789_rect_obj)},
    {MP_ROM_QSTR(MP_QSTR_rotation), MP_ROM_PTR(&st7789_ST7789_rotation_obj)},
    {MP_ROM_QSTR(MP_QSTR_width), MP_ROM_PTR(&st7789_ST7789_width_obj)},
    {MP_ROM_QSTR(MP_QSTR_height), MP_ROM_PTR(&st7789_ST7789_height_obj)},
    {MP_ROM_QSTR(MP_QSTR_vscrdef), MP_ROM_PTR(&st7789_ST7789_vscrdef_obj)},
    {MP_ROM_QSTR(MP_QSTR_vscsad), MP_ROM_PTR(&st7789_ST7789_vscsad_obj)},
    {MP_ROM_QSTR(MP_QSTR_madctl), MP_ROM_PTR(&st7789_ST7789_madctl_obj)},
    {MP_ROM_QSTR(MP_QSTR_offset), MP_ROM_PTR(&st7789_ST7789_offset_obj)},
    {MP_ROM_QSTR(MP_QSTR_bounding), MP_ROM_PTR(&st7789_ST7789_bounding_obj)},
};
static MP_DEFINE_CONST_DICT(st7789_ST7789_locals_dict,
                            st7789_ST7789_locals_dict_table);

/* methods end */

#ifdef MP_OBJ_TYPE_GET_SLOT

MP_DEFINE_CONST_OBJ_TYPE(st7789_ST7789_type, MP_QSTR_ST7789, MP_TYPE_FLAG_NONE,
                         print, st7789_ST7789_print, make_new,
                         st7789_ST7789_make_new, locals_dict,
                         (mp_obj_dict_t *)&st7789_ST7789_locals_dict);

#else

const mp_obj_type_t st7789_ST7789_type = {
    {&mp_type_type},
    .name = MP_QSTR_ST7789,
    .print = st7789_ST7789_print,
    .make_new = st7789_ST7789_make_new,
    .locals_dict = (mp_obj_dict_t *)&st7789_ST7789_locals_dict,
};

#endif

mp_obj_t st7789_ST7789_make_new(const mp_obj_type_t *type, size_t n_args,
                                size_t n_kw, const mp_obj_t *all_args) {
  enum {
    ARG_spi,
    ARG_width,
    ARG_height,
    ARG_reset,
    ARG_dc,
    ARG_cs,
    ARG_backlight,
    ARG_rotations,
    ARG_rotation,
    ARG_custom_init,
    ARG_color_order,
    ARG_inversion,
    ARG_options,
    ARG_buffer_size
  };
  static const mp_arg_t allowed_args[] = {
      {MP_QSTR_spi, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_width, MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = 0}},
      {MP_QSTR_height, MP_ARG_INT | MP_ARG_REQUIRED, {.u_int = 0}},
      {MP_QSTR_reset, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_dc, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_cs, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_backlight, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_rotations, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_rotation, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0}},
      {MP_QSTR_custom_init,
       MP_ARG_KW_ONLY | MP_ARG_OBJ,
       {.u_obj = MP_OBJ_NULL}},
      {MP_QSTR_color_order,
       MP_ARG_KW_ONLY | MP_ARG_INT,
       {.u_int = ST7789_MADCTL_RGB}},
      {MP_QSTR_inversion, MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = true}},
      {MP_QSTR_options, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0}},
  };
  mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
  mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed_args),
                            allowed_args, args);

  // create new object
  st7789_ST7789_obj_t *self = m_new_obj(st7789_ST7789_obj_t);
  self->base.type = &st7789_ST7789_type;

  // set parameters
  mp_obj_base_t *spi_obj = (mp_obj_base_t *)MP_OBJ_TO_PTR(args[ARG_spi].u_obj);
  self->spi_obj = spi_obj;
  self->display_width = args[ARG_width].u_int;
  self->width = args[ARG_width].u_int;
  self->display_height = args[ARG_height].u_int;
  self->height = args[ARG_height].u_int;

  self->rotations = NULL;
  self->rotations_len = 4;

  if (args[ARG_rotations].u_obj != MP_OBJ_NULL) {
    size_t len;
    mp_obj_t *rotations_array = MP_OBJ_NULL;
    mp_obj_get_array(args[ARG_rotations].u_obj, &len, &rotations_array);
    self->rotations_len = len;
    self->rotations = m_new(st7789_rotation_t, self->rotations_len);
    for (int i = 0; i < self->rotations_len; i++) {
      mp_obj_t *rotation_tuple = NULL;
      size_t rotation_tuple_len = 0;

      mp_obj_tuple_get(rotations_array[i], &rotation_tuple_len,
                       &rotation_tuple);
      if (rotation_tuple_len != 5) {
        mp_raise_ValueError(
            MP_ERROR_TEXT("rotations tuple must have 5 elements"));
      }

      self->rotations[i].madctl = mp_obj_get_int(rotation_tuple[0]);
      self->rotations[i].width = mp_obj_get_int(rotation_tuple[1]);
      self->rotations[i].height = mp_obj_get_int(rotation_tuple[2]);
      self->rotations[i].colstart = mp_obj_get_int(rotation_tuple[3]);
      self->rotations[i].rowstart = mp_obj_get_int(rotation_tuple[4]);
    }
  }

  self->rotation = args[ARG_rotation].u_int % self->rotations_len;
  self->custom_init = args[ARG_custom_init].u_obj;
  self->color_order = args[ARG_color_order].u_int;
  self->inversion = args[ARG_inversion].u_bool;
  self->options = args[ARG_options].u_int & 0xff;

  if (args[ARG_dc].u_obj == MP_OBJ_NULL) {
    mp_raise_ValueError(MP_ERROR_TEXT("must specify dc pin"));
  }

  if (args[ARG_reset].u_obj != MP_OBJ_NULL) {
    self->reset = mp_hal_get_pin_obj(args[ARG_reset].u_obj);
  } else {
    self->reset = GPIO_NUM_NC;
  }

  self->dc = mp_hal_get_pin_obj(args[ARG_dc].u_obj);

  if (args[ARG_cs].u_obj != MP_OBJ_NULL) {
    self->cs = mp_hal_get_pin_obj(args[ARG_cs].u_obj);
  } else {
    self->cs = GPIO_NUM_NC;
  }

  if (args[ARG_backlight].u_obj != MP_OBJ_NULL) {
    self->backlight = mp_hal_get_pin_obj(args[ARG_backlight].u_obj);
  } else {
    self->backlight = GPIO_NUM_NC;
  }

  self->bounding = 0;
  self->min_x = self->display_width;
  self->min_y = self->display_height;
  self->max_x = 0;
  self->max_y = 0;

  return MP_OBJ_FROM_PTR(self);
}

static const mp_map_elem_t st7789_module_globals_table[] = {
    {MP_ROM_QSTR(MP_QSTR___name__), MP_OBJ_NEW_QSTR(MP_QSTR_st7789)},
    {MP_ROM_QSTR(MP_QSTR_color565), (mp_obj_t)&st7789_color565_obj},
    {MP_ROM_QSTR(MP_QSTR_map_bitarray_to_rgb565),
     (mp_obj_t)&st7789_map_bitarray_to_rgb565_obj},
    {MP_ROM_QSTR(MP_QSTR_ST7789), (mp_obj_t)&st7789_ST7789_type},
    {MP_ROM_QSTR(MP_QSTR_BLACK), MP_ROM_INT(BLACK)},
    {MP_ROM_QSTR(MP_QSTR_BLUE), MP_ROM_INT(BLUE)},
    {MP_ROM_QSTR(MP_QSTR_RED), MP_ROM_INT(RED)},
    {MP_ROM_QSTR(MP_QSTR_GREEN), MP_ROM_INT(GREEN)},
    {MP_ROM_QSTR(MP_QSTR_CYAN), MP_ROM_INT(CYAN)},
    {MP_ROM_QSTR(MP_QSTR_MAGENTA), MP_ROM_INT(MAGENTA)},
    {MP_ROM_QSTR(MP_QSTR_YELLOW), MP_ROM_INT(YELLOW)},
    {MP_ROM_QSTR(MP_QSTR_WHITE), MP_ROM_INT(WHITE)},
    {MP_ROM_QSTR(MP_QSTR_MADCTL_MY), MP_ROM_INT(ST7789_MADCTL_MY)},
    {MP_ROM_QSTR(MP_QSTR_MADCTL_MX), MP_ROM_INT(ST7789_MADCTL_MX)},
    {MP_ROM_QSTR(MP_QSTR_MADCTL_MV), MP_ROM_INT(ST7789_MADCTL_MV)},
    {MP_ROM_QSTR(MP_QSTR_MADCTL_ML), MP_ROM_INT(ST7789_MADCTL_ML)},
    {MP_ROM_QSTR(MP_QSTR_MADCTL_MH), MP_ROM_INT(ST7789_MADCTL_MH)},
    {MP_ROM_QSTR(MP_QSTR_RGB), MP_ROM_INT(ST7789_MADCTL_RGB)},
    {MP_ROM_QSTR(MP_QSTR_BGR), MP_ROM_INT(ST7789_MADCTL_BGR)},
    {MP_ROM_QSTR(MP_QSTR_WRAP), MP_ROM_INT(OPTIONS_WRAP)},
    {MP_ROM_QSTR(MP_QSTR_WRAP_H), MP_ROM_INT(OPTIONS_WRAP_H)},
    {MP_ROM_QSTR(MP_QSTR_WRAP_V), MP_ROM_INT(OPTIONS_WRAP_V)}};

static MP_DEFINE_CONST_DICT(mp_module_st7789_globals,
                            st7789_module_globals_table);

const mp_obj_module_t mp_module_st7789 = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&mp_module_st7789_globals,
};

#if !defined(MICROPY_VERSION) || MICROPY_VERSION <= 70144
MP_REGISTER_MODULE(MP_QSTR_st7789, mp_module_st7789, MODULE_ST7789_ENABLE);
#else
MP_REGISTER_MODULE(MP_QSTR_st7789, mp_module_st7789);
#endif
