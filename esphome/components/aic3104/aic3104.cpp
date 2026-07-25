#include "aic3104.h"

#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aic3104 {

static const char *const TAG = "aic3104";

#define ERROR_CHECK(err, msg) \
  if (!(err)) { \
    ESP_LOGE(TAG, msg); \
    this->mark_failed(); \
    return; \
  }

void AIC3104::setup() {
  // Read the output-stage registers without modifying them, once now and again
  // after the XMOS has had time to (re)configure the codec at boot.
  this->log_output_registers_("setup");
  this->set_timeout(8000, [this]() { this->log_output_registers_("boot+8s"); });
}

void AIC3104::log_output_registers_(const char *phase) {
  if (!this->write_byte(AIC3104_PAGE_CTRL, 0x00)) {
    ESP_LOGW(TAG, "Register dump [%s]: failed to select page 0", phase);
    return;
  }

  struct RegInfo {
    uint8_t addr;
    const char *name;
  };
  // HP and LINE output paths plus the DAC digital volume, both channels.
  static const RegInfo REGS[] = {
      {AIC3104_DAC_POWER_OUTPUT, "0x25 DAC_POWER_OUTPUT"},
      {AIC3104_DAC_OUTPUT_SWITCH, "0x29 DAC_OUTPUT_SWITCH"},
      {AIC3104_LEFT_DAC_VOLUME, "0x2B LEFT_DAC_VOLUME"},
      {AIC3104_RIGHT_DAC_VOLUME, "0x2C RIGHT_DAC_VOLUME"},
      {AIC3104_DAC_L1_HPLOUT_VOLUME, "0x2F DAC_L1->HPLOUT_VOL"},
      {AIC3104_HPLOUT_LEVEL, "0x33 HPLOUT_LEVEL"},
      {AIC3104_DAC_R1_HPROUT_VOLUME, "0x40 DAC_R1->HPROUT_VOL"},
      {AIC3104_HPROUT_LEVEL, "0x41 HPROUT_LEVEL"},
      {AIC3104_DAC_L1_LEFT_LOP_VOLUME, "0x52 DAC_L1->LEFT_LOP_VOL"},
      {AIC3104_LEFT_LOP_LEVEL, "0x56 LEFT_LOP_LEVEL"},
      {AIC3104_DAC_R1_RIGHT_LOP_VOLUME, "0x5C DAC_R1->RIGHT_LOP_VOL"},
      {AIC3104_RIGHT_LOP_LEVEL, "0x5D RIGHT_LOP_LEVEL"},
      {AIC3104_MODULE_POWER_STATUS, "0x5E MODULE_POWER_STATUS"},
  };

  ESP_LOGI(TAG, "AIC3104 output register dump [%s]:", phase);
  for (const auto &reg : REGS) {
    uint8_t value = 0;
    if (this->read_byte(reg.addr, &value)) {
      ESP_LOGI(TAG, "  %s = 0x%02X", reg.name, value);
    } else {
      ESP_LOGW(TAG, "  %s = <read failed>", reg.name);
    }
  }
}

void AIC3104::dump_config() {
  ESP_LOGCONFIG(TAG, "AIC3104:");
  LOG_I2C_DEVICE(this);

  if (this->is_failed()) {
    ESP_LOGE(TAG, ESP_LOG_MSG_COMM_FAIL);
  }
}

bool AIC3104::set_mute_off() {
  this->is_muted_ = false;
  return this->write_mute_();
}

bool AIC3104::set_mute_on() {
  this->is_muted_ = true;
  return this->write_mute_();
}

bool AIC3104::set_volume(float volume) {
  this->volume_ = clamp<float>(volume, 0.0, 1.0);
  ESP_LOGD(TAG, "AIC3104 set_volume called: %.2f", this->volume_);
  bool result = this->write_volume_();
  ESP_LOGD(TAG, "AIC3104 write_volume result: %s", result ? "SUCCESS" : "FAILED");
  return result;
}

bool AIC3104::is_muted() { return this->is_muted_; }

float AIC3104::volume() { return this->volume_; }

bool AIC3104::write_mute_() {
  // XVF3800/AIC3104 mute control - setting volume to maximum attenuation
  uint8_t mute_value = this->is_muted_ ? 0x80 : ((1.0f - this->volume_) * 0x80);
  
  if (!this->write_byte(AIC3104_PAGE_CTRL, 0x00) || 
      !this->write_byte(AIC3104_LEFT_DAC_VOLUME, mute_value) ||
      !this->write_byte(AIC3104_RIGHT_DAC_VOLUME, mute_value)) {
    ESP_LOGE(TAG, "Writing mute failed");
    return false;
  }
  
  ESP_LOGVV(TAG, "Mute %s (volume=0x%.2x)", this->is_muted_ ? "ON" : "OFF", mute_value);
  return true;
}

bool AIC3104::write_volume_() {
  ESP_LOGD(TAG, "write_volume_() called - volume: %.2f", this->volume_);
  
  if (!this->write_byte(AIC3104_PAGE_CTRL, 0x00)) {
    ESP_LOGE(TAG, "Failed to set page 0");
    return false;
  }
  
  // Map volume 0.0-1.0 to DAC range 0x80-0x00 (inverted)
  // 0x00 = 0dB (loudest), 0x7F = -63.5dB (quietest), 0x80 = mute
  uint8_t dac_val = (uint8_t)((1.0f - this->volume_) * 0x80);
  dac_val = clamp<uint8_t>(dac_val, 0x00, 0x80);
  
  ESP_LOGD(TAG, "Writing DAC volume: 0x%.2x (%.1fdB attenuation) to registers 0x2B/0x2C", 
           dac_val, -(float)dac_val);
  
  if (!this->write_byte(AIC3104_LEFT_DAC_VOLUME, dac_val) ||
      !this->write_byte(AIC3104_RIGHT_DAC_VOLUME, dac_val)) {
    ESP_LOGE(TAG, "Writing DAC volume failed");
    return false;
  }
  
  ESP_LOGD(TAG, "Volume %.1f%% → DAC: 0x%.2x (%.1fdB attenuation) - SUCCESS", 
           this->volume_ * 100.0f, dac_val, -(float)dac_val);
  
  return true;
}

}  // namespace aic3104
}  // namespace esphome
