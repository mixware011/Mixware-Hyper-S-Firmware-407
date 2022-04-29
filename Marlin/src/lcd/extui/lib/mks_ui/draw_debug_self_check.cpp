/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#include "../../../../inc/MarlinConfigPre.h"

#if HAS_TFT_LVGL_UI

#include "draw_ui.h"
#include <lv_conf.h>

#include "../../../../inc/MarlinConfig.h"
#include "../../../../module/temperature.h"
#include "../../../../module/motion.h"
#include "../../../../module/planner.h"
#include "../../../../gcode/gcode.h"
#include "../../../../feature/bltouch.h"


extern lv_group_t *g;
static lv_obj_t *scr;

static lv_obj_t *l_debug;
static lv_obj_t *l_temp_nozzle,
                *l_heat_nozzle,
                #if HAS_HEATED_BED
                  *l_temp_bed,
                  *l_heat_bed,
                #endif
                *l_axis_x_motor,
                *l_axis_y_motor,
                *l_axis_z_motor,
                *l_axis_e_motor,
                *l_fan,
                *l_servo;

static millis_t h_timer;
static bool b_motor_ok;

enum {
  DEBUG_NULL = 1,
  DEBUG_CHECK_TEMP_E,
  #if HAS_HEATED_BED
    DEBUG_CHECK_TEMP_B,
  #endif
  DEBUG_CHECK_HEAT_E,
  #if HAS_HEATED_BED
    DEBUG_CHECK_HEAT_B,
  #endif
  DEBUG_CHECK_FAN,
  DEBUG_CHECK_HOME_X,
  DEBUG_CHECK_HOME_Y,
  DEBUG_CHECK_HOME_Z,
  DEBUG_CHECK_HOME_E,
  DEBUG_CHECK_SERVO
};
static uint8_t debug_state = DEBUG_NULL,
               debug_state_n = DEBUG_NULL;

enum {
  ID_DEBUG_S_CHECK = 1,
  ID_DEBUG_S_ZAXIS,
  ID_DEBUG_S_RETURN
};

#define DEBUG_LABEL_POS_X   (250)
#define DEBUG_LABEL_POS_Y(n) (32+26*n)
#define DEBUG_LABEL_SIZE_W  (50)
#define DEBUG_LABEL_SIZE_H  (24)

static void l_selfc_reset_label() {
  lv_label_set_text(l_temp_nozzle, "--");
  lv_label_set_text(l_heat_nozzle, "--");
  lv_label_set_text(l_fan, "--");
  lv_label_set_text(l_axis_x_motor, "--");
  lv_label_set_text(l_axis_y_motor, "--");
  lv_label_set_text(l_axis_z_motor, "--");
  lv_label_set_text(l_axis_e_motor, "--");
  lv_label_set_text(l_servo, "--");

  lv_obj_set_style(l_temp_nozzle, &tft_style_label_rel);
  lv_obj_set_style(l_heat_nozzle, &tft_style_label_rel);
  lv_obj_set_style(l_fan, &tft_style_label_rel);
  lv_obj_set_style(l_axis_x_motor, &tft_style_label_rel);
  lv_obj_set_style(l_axis_y_motor, &tft_style_label_rel);
  lv_obj_set_style(l_axis_z_motor, &tft_style_label_rel);
  lv_obj_set_style(l_axis_e_motor, &tft_style_label_rel);
  lv_obj_set_style(l_servo, &tft_style_label_rel);

  #if HAS_HEATED_BED
    lv_label_set_text(l_temp_bed, "--");
    lv_label_set_text(l_heat_bed, "--");
    lv_obj_set_style(l_temp_bed, &tft_style_label_rel);
    lv_obj_set_style(l_heat_bed, &tft_style_label_rel);
  #endif
}

static void l_selfc_label_create(lv_coord_t y, const char *text) {
  lv_obj_t *l_tips = lv_label_create(scr, 40, y, text);
  lv_obj_set_size(l_tips, 200, DEBUG_LABEL_SIZE_H);
}

static void event_handler(lv_obj_t *obj, lv_event_t event) {
  if (event != LV_EVENT_RELEASED) return;
  switch (obj->mks_obj_id) {
    case ID_DEBUG_S_CHECK:
      if (debug_state_n == DEBUG_NULL) {
        l_selfc_reset_label();
        gcode.process_subcommands_now(PSTR("M107"));
        debug_state_n = DEBUG_CHECK_TEMP_E;
        lv_label_set_text(l_debug, debug_menu.selfc_checking);
      }
      // else {
      //   debug_state = debug_state_n = DEBUG_NULL;
      //   lv_label_set_text(l_debug, debug_menu.selfc_confirm);
      // }
      break;
    case ID_DEBUG_S_ZAXIS:
      lv_clear_debug_selfc();
      lv_draw_debug_zaxis();
      break;
    case ID_DEBUG_S_RETURN:
      if (debug_state == DEBUG_CHECK_HEAT_E) {
        thermalManager.setTargetHotend(0, uiCfg.desireSprayerTempBak);
      }
      #if HAS_HEATED_BED
        else if (debug_state == DEBUG_CHECK_HEAT_B) {
          thermalManager.setTargetBed(uiCfg.desireSprayerTempBak);
        }
      #endif
      else if (debug_state == DEBUG_CHECK_HOME_X || debug_state == DEBUG_CHECK_HOME_Y || debug_state == DEBUG_CHECK_HOME_Z) {
        break;
      }

      b_motor_ok = false;
      gcode.process_subcommands_now(PSTR("M107"));

      lv_clear_cur_ui();
      lv_draw_return_ui();
      break;
  }
}

void lv_draw_debug_selfc(void) {
  scr = lv_screen_create(DEBUG_SELFC_UI);

  debug_state = debug_state_n = DEBUG_NULL;

  uint8_t index = 0;
  b_motor_ok = false;
  // Create an Image button
  l_temp_nozzle = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
  l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_etemp);
  lv_obj_set_size(l_temp_nozzle, DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);

  #if HAS_HEATED_BED
    l_temp_bed = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
    l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_btemp);
    lv_obj_set_size(l_temp_bed, DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);
  #endif

  l_heat_nozzle = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
  l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_eheat);
  lv_obj_set_size(l_heat_nozzle,  DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);

  #if HAS_HEATED_BED
    l_heat_bed = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
    l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_bheat);
    lv_obj_set_size(l_heat_bed, DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);
  #endif

  l_fan = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
  l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_fan);
  lv_obj_set_size(l_fan, DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);

  l_axis_x_motor  = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
  l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_x);
  lv_obj_set_size(l_axis_x_motor, DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);

  l_axis_y_motor  = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
  l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_y);
  lv_obj_set_size(l_axis_y_motor, DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);

  l_axis_z_motor  = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
  l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_z);
  lv_obj_set_size(l_axis_z_motor, DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);

  l_axis_e_motor  = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
  l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_e);
  lv_obj_set_size(l_axis_e_motor, DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);

  l_servo         = lv_label_create(scr, DEBUG_LABEL_POS_X, DEBUG_LABEL_POS_Y(index), nullptr);
  l_selfc_label_create(DEBUG_LABEL_POS_Y(index++), debug_menu.selfc_tips_servo);
  lv_obj_set_size(l_servo,        DEBUG_LABEL_SIZE_W, DEBUG_LABEL_SIZE_H);

  l_selfc_reset_label();

  lv_obj_t* btn_check = lv_btn_create(scr, 20, 290, 280, 64, event_handler, ID_DEBUG_S_CHECK);
  l_debug = lv_label_create(btn_check, debug_menu.selfc_confirm);
  lv_btn_set_style_both(btn_check, &style_para_button);
  lv_obj_align(l_debug, btn_check, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t* btn_zaxis = lv_btn_create(scr, 20, 290+69, 280, 64, event_handler, ID_DEBUG_S_ZAXIS);
  lv_obj_t* l_zaxis = lv_label_create(btn_zaxis, debug_menu.zaxis_title);
  lv_btn_set_style_both(btn_zaxis, &style_para_button);
  lv_obj_align(l_zaxis, btn_zaxis, LV_ALIGN_CENTER, 0, 0);

  lv_screen_menu_item_return(scr, event_handler, ID_DEBUG_S_RETURN);
}

void dis_cur_debug() {
  static float cur_temp;
  static bool b_bitouch;

  if (debug_state != debug_state_n) {
    debug_state = debug_state_n;
    switch (debug_state) {
      case DEBUG_NULL:
        break;
      case DEBUG_CHECK_TEMP_E:
        if (thermalManager.temp_hotend[0].celsius > -10) {
          lv_label_set_text(l_temp_nozzle, PSTR("OK"));
        }
        else {
          lv_label_set_text(l_temp_nozzle, PSTR("NG"));
          lv_obj_set_style(l_temp_nozzle, &tft_style_label_HT);
        }

        debug_state_n++;
        break;
      case DEBUG_CHECK_HEAT_E:
        cur_temp = thermalManager.degHotend(0);

        if (cur_temp < -10) {
          lv_label_set_text(l_heat_nozzle, PSTR("NG"));
          lv_obj_set_style(l_heat_nozzle, &tft_style_label_HT);

          debug_state_n++;
        }
        else {
          uiCfg.desireSprayerTempBak = thermalManager.degTargetHotend(0);
          if (uiCfg.desireSprayerTempBak < 245) {
            thermalManager.temp_hotend[0].target = cur_temp+15;
          }
          else
            thermalManager.temp_hotend[0].target = 0;

          if (uiCfg.desireSprayerTempBak < 75) {
            thermalManager.temp_hotend[0].target = 75;
          }
          thermalManager.start_watching_hotend(0);

          if (cur_temp < 60) {
            h_timer = millis() + ((int)(60-cur_temp)/3+20)*1000;
          }
          else
            h_timer = millis() + 20000;
        }
        break;
      #if HAS_HEATED_BED
        case DEBUG_CHECK_TEMP_B:
          if (thermalManager.temp_bed.celsius > -10) {
            lv_label_set_text(l_temp_bed, PSTR("OK"));
          }
          else {
            lv_label_set_text(l_temp_bed, PSTR("NG"));
            lv_obj_set_style(l_temp_bed, &tft_style_label_HT);
          }

          debug_state_n++;
          break;
        case DEBUG_CHECK_HEAT_B:
          cur_temp = thermalManager.degBed();

          if (cur_temp < -10) {
            lv_label_set_text(l_heat_bed, PSTR("NG"));
            lv_obj_set_style(l_heat_bed, &tft_style_label_HT);

            debug_state_n++;
          }
          else {
            uiCfg.desireSprayerTempBak = thermalManager.degTargetBed();
            if (uiCfg.desireSprayerTempBak < 90)
              thermalManager.setTargetBed(100);
            else
              thermalManager.setTargetBed(0);

            h_timer = millis() + 20000;
          }
          break;
      #endif
      case DEBUG_CHECK_FAN:
        gcode.process_subcommands_now(PSTR("M106 S255"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        lv_label_set_text(l_fan, PSTR("ON"));
        debug_state_n++;
        break;
      case DEBUG_CHECK_HOME_X:
        gcode.process_subcommands_now(PSTR("G91\nG1 X10 F1200\nG90"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(X_MIN_PIN) == !X_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_x_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_x_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        gcode.process_subcommands_now(PSTR("G28X"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(X_MIN_PIN) == X_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_x_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_x_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        gcode.process_subcommands_now(PSTR("G0 X85 F1200"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(X_MIN_PIN) == !X_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_x_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_x_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        planner.synchronize();
        queue.inject(PSTR("G1 X255 F3000\nG1 X285 F1000\nG1 X185 F2400\nG1 X10 F4200"));

        b_motor_ok = true;
        h_timer = millis() + 10000;
        break;
      case DEBUG_CHECK_HOME_Y:
        gcode.process_subcommands_now(PSTR("G91\nG1 Y10 F1200\nG90"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(Y_MIN_PIN) == !Y_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_y_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_y_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        gcode.process_subcommands_now(PSTR("G28Y"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(Y_MIN_PIN) == Y_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_y_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_y_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        gcode.process_subcommands_now(PSTR("G0 Y85 F1200"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(Y_MIN_PIN) == !Y_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_y_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_y_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        planner.synchronize();
        queue.inject(PSTR("G1 Y255 F3000\nG1 Y285 F1000\nG1 Y185 F2400\nG1 Y10 F4200"));

        b_motor_ok = true;
        h_timer = millis() + 10000;
        break;
      case DEBUG_CHECK_HOME_Z:
        gcode.process_subcommands_now(PSTR("G91\nG1 Z10 F1200\nG90"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(Z_MIN_PIN) == !Z_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_z_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_z_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        gcode.process_subcommands_now(PSTR("G28Z\nG1 Z0"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(Z_MIN_PIN) == Z_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_z_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_z_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        gcode.process_subcommands_now(PSTR("G0 Z185 F1200"));
        while(planner.has_blocks_queued())
          watchdog_refresh();
        if (READ(Z_MIN_PIN) == !Z_MIN_ENDSTOP_INVERTING) {
          lv_label_set_text(l_axis_z_motor, PSTR("NG"));
          lv_obj_set_style(l_axis_z_motor, &tft_style_label_HT);
          debug_state_n++;
          break;
        }

        planner.synchronize();
        queue.inject(PSTR("G1 Z325 F2200\nG1 Z385 F600\nG1 Z235 F2000\nG1 Z10 F1200"));

        b_motor_ok = true;
        h_timer = millis() + 10000;
        break;
      case DEBUG_CHECK_HOME_E:
        gcode.process_subcommands_now(PSTR("G0 X150 Y150 Z100 F2000"));
        while(planner.has_blocks_queued())
          watchdog_refresh();

        planner.synchronize();
        queue.inject(PSTR("T0\nG91\nG1 E10 F100\nG1 E-80 F800\nG1 E40 F200\nG90"));

        b_motor_ok = true;
        h_timer = millis() + 10000;
        break;
      case DEBUG_CHECK_SERVO:
        b_bitouch = false;
        bltouch.init();
        if (!bltouch.deploy()) {
          lv_label_set_text(l_servo, PSTR("OK"));
          b_bitouch = true;
        }
        else {
          lv_label_set_text(l_servo, PSTR("NG"));
          lv_obj_set_style(l_servo, &tft_style_label_HT);
        }

        if (!bltouch.stow() && b_bitouch) {
          lv_label_set_text(l_servo, PSTR("OK"));
        }
        else {
          lv_label_set_text(l_servo, PSTR("NG"));
          lv_obj_set_style(l_servo, &tft_style_label_HT);
        }

        debug_state = debug_state_n = DEBUG_NULL;
        lv_label_set_text(l_debug, debug_menu.selfc_confirm);
        gcode.process_subcommands_now(PSTR("M107\nM84"));

        thermalManager.setTargetHotend(0, uiCfg.desireSprayerTempBak);
        thermalManager.setTargetBed(uiCfg.desireSprayerTempBak);
        break;
    }
  }

  if (debug_state != DEBUG_NULL && debug_state_n != DEBUG_NULL) {
    static char debug_buf[30] = { 0 };
    const char debug_checking_buf[3][8] = { ".", "..", "..."};
    static uint8_t index = 0;
    sprintf_P(debug_buf, PSTR("%s%s"), debug_menu.selfc_checking, debug_checking_buf[index++]);
    if (index >= 3)
      index = 0;
    lv_label_set_text(l_debug, debug_buf);

    if (debug_state == debug_state_n) {
      if (debug_state == DEBUG_CHECK_HEAT_E) {
        if (millis() < h_timer)
          return;

        if (abs(thermalManager.temp_hotend[0].celsius - cur_temp) >= 5) {
          lv_label_set_text(l_heat_nozzle, PSTR("OK"));
        }
        else {
          lv_label_set_text(l_heat_nozzle, PSTR("NG"));
          lv_obj_set_style(l_heat_nozzle, &tft_style_label_HT);
        }

        thermalManager.temp_hotend[0].target = 200;
        thermalManager.start_watching_hotend(0);
        debug_state_n++;
      }
      #if HAS_HEATED_BED
        else if (debug_state == DEBUG_CHECK_HEAT_B) {
          if (millis() < h_timer)
            return;

          if ((thermalManager.temp_bed.celsius - cur_temp) >= 2) {
            lv_label_set_text(l_heat_bed, PSTR("OK"));
          }
          else {
            lv_label_set_text(l_heat_bed, PSTR("NG"));
            lv_obj_set_style(l_heat_bed, &tft_style_label_HT);
          }

          debug_state_n++;
        }
      #endif
      else if (debug_state == DEBUG_CHECK_HOME_X || debug_state == DEBUG_CHECK_HOME_Y || debug_state == DEBUG_CHECK_HOME_Z || debug_state == DEBUG_CHECK_HOME_E) {
        if (millis() < h_timer)
          return;

        if (!planner.has_blocks_queued()) {
          if (b_motor_ok) {
            b_motor_ok = false;
            if (debug_state == DEBUG_CHECK_HOME_X) lv_label_set_text(l_axis_x_motor, PSTR("OK"));
            if (debug_state == DEBUG_CHECK_HOME_Y) lv_label_set_text(l_axis_y_motor, PSTR("OK"));
            if (debug_state == DEBUG_CHECK_HOME_Z) lv_label_set_text(l_axis_z_motor, PSTR("OK"));
            if (debug_state == DEBUG_CHECK_HOME_E) lv_label_set_text(l_axis_e_motor, PSTR("OK"));
            debug_state_n++;
          }
        }
      }
    }
  }
}

void lv_clear_debug_selfc() {
  #if HAS_ROTARY_ENCODER
    if (gCfgItems.encoder_enable) lv_group_remove_all_objs(g);
  #endif
  lv_obj_del(scr);
}

#endif // HAS_TFT_LVGL_UI
