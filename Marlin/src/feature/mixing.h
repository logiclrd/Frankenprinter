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
#pragma once

#include "../inc/MarlinConfig.h"

#if ENABLED(MIXING_EXTRUDER)
//#define MIXER_NORMALIZER_DEBUG

#if ENABLED(ADVANCED_PAUSE_FEATURE)
  #include "pause.h"
#endif

#ifndef __AVR__ // 
  // Use 16-bit (or fastest) data for the integer mix factors
  typedef uint_fast16_t mixer_comp_t;
  typedef uint_fast16_t mixer_accu_t;
  #define COLOR_A_MASK 0x8000
  #define COLOR_MASK 0x7FFF
#else
  // Use 8-bit data for the integer mix factors
  // Exactness is sacrificed for speed
  #define MIXER_ACCU_SIGNED
  typedef uint8_t mixer_comp_t;
  typedef int8_t mixer_accu_t;
  #define COLOR_A_MASK 0x80
  #define COLOR_MASK 0x7F
#endif

#if !defined(DEFAULT_MIXING_SWITCH)
#define	DEFAULT_MIXING_SWITCH 	true
#endif

typedef int8_t mixer_perc_t;

#ifndef MIXING_VIRTUAL_TOOLS
  #define MIXING_VIRTUAL_TOOLS 1
#endif

enum MixTool {
    FIRST_USER_VIRTUAL_TOOL = 0
  , LAST_USER_VIRTUAL_TOOL = MIXING_VIRTUAL_TOOLS - 1
  , NR_USER_VIRTUAL_TOOLS
  , MIXER_DIRECT_SET_TOOL = NR_USER_VIRTUAL_TOOLS
  #if HAS_MIXER_SYNC_CHANNEL
    , MIXER_AUTORETRACT_TOOL
  #endif
  , NR_MIXING_VIRTUAL_TOOLS
};

#define MAX_VTOOLS TERN(HAS_MIXER_SYNC_CHANNEL, 254, 255)
static_assert(NR_MIXING_VIRTUAL_TOOLS <= MAX_VTOOLS, "MIXING_VIRTUAL_TOOLS must be <= " STRINGIFY(MAX_VTOOLS) "!");

#define MIXER_STEPPER_LOOP(VAR) for (uint_fast8_t VAR = 0; VAR < MIXING_STEPPERS; VAR++)

#if ENABLED(GRADIENT_MIX)
typedef struct {
  bool enabled;                         			// This gradient is enabled
  mixer_comp_t color[MIXING_STEPPERS];  			// The current gradient color
  float start_z, end_z;                 			// Region for gradient
  int8_t start_vtool, end_vtool;        			// Start and end virtual tools
  mixer_perc_t start_mix[MIXING_STEPPERS],   	// Start and end mixes from those tools
               end_mix[MIXING_STEPPERS];
  TERN_(GRADIENT_VTOOL, int8_t vtool_index); // Use this virtual tool number as index
} gradient_t;
#endif

#if ENABLED(RANDOM_MIX)
typedef struct {
  bool enabled;    
  float start_z, end_z;
  float height;							//Minimum height of changing mixing rate  
  uint8_t extruders;
}randommix_t;
#endif

/**
 * @brief Mixer class
 * @details Contains data and behaviors for a Mixing Extruder
 */
class Mixer {
  public:	
	static bool mixing_enabled;
  static mixer_perc_t percentmix[MIXING_STEPPERS];  // Scratch array for the Mix in proportion to 100, also editable from LCD
  static float collector[MIXING_STEPPERS];    			// M163 components
  static int8_t selected_vtool;
  static mixer_comp_t color[NR_MIXING_VIRTUAL_TOOLS][MIXING_STEPPERS];
  
  static void init(); // Populate colors at boot time
  static void reset_vtools(bool force_reset = false);
  static void refresh_collector(const float proportion=1.0, const uint8_t t=selected_vtool, float (&c)[MIXING_STEPPERS]=collector);

  // Used up to Planner level
  FORCE_INLINE static void set_collector(const uint8_t c, const float f) { collector[c] = _MAX(f, 0.0f); }
  FORCE_INLINE static void set_Percentmix(const uint8_t c, const uint8_t d) { percentmix[c] = d; }
  FORCE_INLINE static void reset_collector(const uint8_t t){
  	MIXER_STEPPER_LOOP(i) set_collector(i, i == t ? 1.0 : 0.0);
	}

  static void copy_percentmix_to_collector();
	#if ENABLED(USE_PRECENT_MIXVALUE)
  static void copy_collector_to_percentmix();
	static void normalize_mixer_percent(mixer_perc_t mix[MIXING_STEPPERS]);	
	#endif
  static void init_collector(const uint8_t index);
	
	
  //
  static void normalize(const uint8_t tool_index);
  FORCE_INLINE static void normalize() { normalize(selected_vtool); }

  FORCE_INLINE static uint8_t get_current_vtool() { return selected_vtool; }

  FORCE_INLINE static void T(const uint_fast8_t c) {
    selected_vtool = c;
    TERN_(GRADIENT_VTOOL, refresh_gradient());
    update_mix_from_vtool();		
  }

  // Used when dealing with blocks
  FORCE_INLINE static void populate_block(mixer_comp_t b_color[MIXING_STEPPERS]) {
    #if ENABLED(GRADIENT_MIX)
    if (gradient.enabled) {
      MIXER_STEPPER_LOOP(i) b_color[i] = gradient.color[i];
      return;
    }
    #endif
    MIXER_STEPPER_LOOP(i) b_color[i] = color[selected_vtool][i];
  }

  FORCE_INLINE static void stepper_setup(mixer_comp_t b_color[MIXING_STEPPERS]) {
    MIXER_STEPPER_LOOP(i) s_color[i] = b_color[i];
  }

  static inline void copy_percentmix_to_color(mixer_comp_t (&tcolor)[MIXING_STEPPERS]) {
  	 normalize_mixer_percent(&percentmix[0]);
    // Scale each component to the largest one in terms of COLOR_A_MASK
    // So the largest component will be COLOR_A_MASK and the other will be in proportion to it
    const float scale = (COLOR_A_MASK) * RECIPROCAL(_MAX(
      LIST_N(MIXING_STEPPERS, percentmix[0], percentmix[1], percentmix[2], percentmix[3], percentmix[4], percentmix[5])
    ));

    // Scale all values so their maximum is COLOR_A_MASK
    MIXER_STEPPER_LOOP(i) tcolor[i] = percentmix[i] * scale;

    #ifdef MIXER_NORMALIZER_DEBUG
    SERIAL_ECHOLNPGM("copy_percentmix_to_color >> ");
      SERIAL_ECHOPGM("Percentmix [ ");
      SERIAL_ECHOLIST_N(MIXING_STEPPERS, int(percentmix[0]), int(percentmix[1]), int(percentmix[2]), int(percentmix[3]), int(percentmix[4]), int(percentmix[5]));
      SERIAL_ECHOPGM(" ] to Color [ ");
      SERIAL_ECHOLIST_N(MIXING_STEPPERS, int(tcolor[0]), int(tcolor[1]), int(tcolor[2]), int(tcolor[3]), int(tcolor[4]), int(tcolor[5]));
      SERIAL_ECHOLNPGM(" ]");
    #endif
  }

  static void update_mix_from_vtool(const uint8_t j=selected_vtool) {
    float ctot = 0;
    MIXER_STEPPER_LOOP(i) ctot += color[j][i];
    MIXER_STEPPER_LOOP(i) percentmix[i] = (mixer_perc_t)(100.0f * color[j][i] / ctot);	  
	  normalize_mixer_percent(&percentmix[0]);	

    #ifdef MIXER_NORMALIZER_DEBUG
	  	SERIAL_ECHOLNPGM("update_mix_from_vtool");
	  	SERIAL_EOL();		
      SERIAL_ECHOPAIR("V-tool ", int(j), " [ ");
      SERIAL_ECHOLIST_N(MIXING_STEPPERS, int(color[j][0]), int(color[j][1]), int(color[j][2]), int(color[j][3]), int(color[j][4]), int(color[j][5]));
      SERIAL_ECHOPGM(" ] to Percentmix [ ");
      SERIAL_ECHOLIST_N(MIXING_STEPPERS, int(percentmix[0]), int(percentmix[1]), int(percentmix[2]), int(percentmix[3]), int(percentmix[4]), int(percentmix[5]));
      SERIAL_ECHOLNPGM(" ]");
			SERIAL_EOL();
    #endif
		copy_percentmix_to_collector();
  }

 	static float mix_prev_z;
	
  #if ENABLED(GRADIENT_MIX)
  static gradient_t gradient; 
  // Update the current mix from the gradient for a given Z
  static void update_gradient_for_z(const float z, const bool force = false);
  static void update_gradient_for_planner_z(const bool force = false);
  static inline void gradient_control(const float z){
	  if (gradient.enabled && did_pause_print == 0) {
	    if (z >= gradient.end_z){
	      T(gradient.end_vtool);
			#if DISABLED(GRADIENT_VTOOL)
			  gradient.start_vtool = gradient.end_vtool = 0;
			  gradient.end_z = gradient.start_z = 0;
			  gradient.enabled = false;
			#endif
	    }
	    else{
	      update_gradient_for_z(z);
	    }
	  }
	}

  static inline void update_mix_from_gradient() {
    float ctot = 0;
    MIXER_STEPPER_LOOP(i) ctot += gradient.color[i];
    MIXER_STEPPER_LOOP(i) percentmix[i] = (mixer_perc_t)CEIL(100.0f * gradient.color[i] / ctot);
  #ifdef MIXER_NORMALIZER_DEBUG
		SERIAL_ECHOLNPGM("update_mix_from_gradient");
		SERIAL_EOL();
		SERIAL_ECHOPGM("Gradient [ ");
		SERIAL_ECHOLIST_N(MIXING_STEPPERS, int(gradient.color[0]), int(gradient.color[1]), int(gradient.color[2]), int(gradient.color[3]), int(gradient.color[4]), int(gradient.color[5]));
		SERIAL_ECHOPGM(" ] to Mix [ ");
		SERIAL_ECHOLIST_N(MIXING_STEPPERS, int(percentmix[0]), int(percentmix[1]), int(percentmix[2]), int(percentmixmix[3]), int(percentmix[4]), int(percentmix[5]));
		SERIAL_ECHOLNPGM(" ]");
		SERIAL_EOL();
  #endif
  }

  // Refresh the gradient after a change
  static void refresh_gradient() {
    #if ENABLED(GRADIENT_VTOOL)
      const bool is_grd = (gradient.vtool_index == -1 || selected_vtool == (uint8_t)gradient.vtool_index);
    #else
      constexpr bool is_grd = true;
    #endif
    gradient.enabled = is_grd && gradient.start_vtool != gradient.end_vtool && gradient.start_z < gradient.end_z;
    if (gradient.enabled) {
  	#if ENABLED(RANDOM_MIX)
			random_mix.start_z = random_mix.end_z = 0;
			random_mix.enabled = false;
		#endif
      //mixer_perc_t mix_bak[MIXING_STEPPERS];
      //COPY(mix_bak, percentmix);
      update_mix_from_vtool(gradient.start_vtool);
      COPY(gradient.start_mix, percentmix);
      update_mix_from_vtool(gradient.end_vtool);
      COPY(gradient.end_mix, percentmix);
      update_gradient_for_planner_z(true);
      //COPY(percentmix, mix_bak);
			mix_prev_z = -999.9;
    }
  }
  #endif // GRADIENT_MIX
  
  #if ENABLED(RANDOM_MIX)
   static randommix_t random_mix;
   static void update_randommix_for_z(const float z, const bool force = false);
   static void update_randommix_for_planner_z(const bool force = false);   
   static inline void randommix_control(const float z){
		if (random_mix.enabled && did_pause_print == 0) {
			if (z <= random_mix.end_z){
		    update_randommix_for_z(z);
		  }
			else{
			  random_mix.enabled = false;
			  random_mix.end_z = random_mix.start_z = 0;
			  random_mix.height = 0.2;
			  random_mix.extruders = MIXING_STEPPERS;
		  }
		}
	}

    // Refresh the random after a change
    static void refresh_random_mix() {
      random_mix.enabled = random_mix.start_z < random_mix.end_z;
      if (random_mix.enabled) {
		  	selected_vtool = 0;
		  #if ENABLED(GRADIENT_MIX)		
				gradient.start_vtool = 0;
				gradient.end_vtool = 1;
				gradient.start_z = gradient.end_z = 0;
				gradient.enabled = false;
			#endif
				update_randommix_for_planner_z(true);
				mix_prev_z = -999.9;
      }
    }
  #endif

  // Used in Stepper
  FORCE_INLINE static uint8_t get_stepper() { return runner; }
  FORCE_INLINE static uint8_t get_next_stepper() {
    for (;;) {
      if (--runner < 0) runner = MIXING_STEPPERS - 1;
      accu[runner] += s_color[runner];
      if (
        #ifdef MIXER_ACCU_SIGNED
          accu[runner] < 0
        #else
          accu[runner] & COLOR_A_MASK
        #endif
      ) {
        accu[runner] &= COLOR_MASK;
        return runner;
      }
    }
  }

  private:
  // Used up to Planner level
  //static uint_fast8_t selected_vtool;
  //static mixer_comp_t color[NR_MIXING_VIRTUAL_TOOLS][MIXING_STEPPERS];

  // Used in Stepper
  static int_fast8_t  runner;
  static mixer_comp_t s_color[MIXING_STEPPERS];
  static mixer_accu_t accu[MIXING_STEPPERS];

};

extern Mixer mixer;

#endif // MIXING_EXTRUDER
