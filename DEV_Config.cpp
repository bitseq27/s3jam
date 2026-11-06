/*****************************************************************************
| File      : DEV_Config.cpp
| Function  : Hardware underlying interface
*****************************************************************************/

#include "DEV_Config.h"

uint slice_num;
SPIClass *vspi = NULL;

void DEV_Digital_Write(uint16_t Pin, uint8_t Value)
{
  digitalWrite(Pin, Value);
}

uint8_t DEV_Digital_Read(uint16_t Pin)
{
  return digitalRead(Pin);
}

void DEV_SPI_WriteByte(uint8_t Value)
{
  vspi->transfer(Value);
}

void DEV_SPI_Write_nByte(uint8_t *pData, uint32_t Len)
{
  // DMA-friendly raw transmit.
  vspi->transferBytes(pData, nullptr, Len);
}

void DEV_I2C_Write_Byte(uint8_t addr, uint8_t reg, uint8_t Value)
{
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(Value);
  Wire.endTransmission();
}

void DEV_I2C_Write_Register(uint8_t addr, uint8_t reg, uint16_t value)
{
  uint8_t tmp[3];

  tmp[0] = reg;
  tmp[1] = (value >> 8) & 0xFF;
  tmp[2] = value & 0xFF;

  Wire.beginTransmission(addr);
  Wire.write(tmp, 3);
  Wire.endTransmission();
}

void DEV_I2C_Write_nByte(uint8_t addr, uint8_t *pData, uint32_t Len)
{
  Wire.beginTransmission(addr);
  Wire.write(pData, Len);
  Wire.endTransmission();
}

uint8_t DEV_I2C_Read_Byte(uint8_t addr, uint8_t reg)
{
  uint8_t value = 0;

  Wire.beginTransmission(addr);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return 0;
  }

  if (Wire.requestFrom(addr, (uint8_t)1) == 1) {
    value = Wire.read();
  }

  return value;
}

void DEV_I2C_Read_Register(uint8_t addr, uint8_t reg, uint16_t *value)
{
  uint8_t tmp[2] = {0, 0};

  Wire.beginTransmission(addr);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    *value = 0;
    return;
  }

  Wire.requestFrom(addr, (uint8_t)2);

  for (uint8_t i = 0; i < 2; i++) {
    if (Wire.available()) {
      tmp[i] = Wire.read();
    }
  }

  *value = (((uint16_t)tmp[0] << 8) | (uint16_t)tmp[1]);
}

void DEV_I2C_Read_nByte(uint8_t addr, uint8_t reg, uint8_t *pData, uint32_t Len)
{
  Wire.beginTransmission(addr);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    for (uint32_t i = 0; i < Len; i++) {
      pData[i] = 0;
    }
    return;
  }

  Wire.requestFrom(addr, (size_t)Len);

  for (uint32_t i = 0; i < Len; i++) {
    if (Wire.available()) {
      pData[i] = Wire.read();
    } else {
      pData[i] = 0;
    }
  }
}

uint16_t DEC_ADC_Read(void)
{
  return analogReadMilliVolts(BAT_ADC_PIN);
}

void DEV_GPIO_Mode(uint16_t Pin, uint16_t Mode)
{
  if (Mode == 0) {
    pinMode(Pin, INPUT);
  } else {
    pinMode(Pin, OUTPUT);
  }
}

void DEV_KEY_Config(uint16_t Pin)
{
  pinMode(Pin, INPUT_PULLUP);
}

void DEV_SET_Backlight(uint8_t Value)
{
  if (Value > 100) {
    printf("DEV_SET_Backlight Error\r\n");
  } else {
    analogWrite(LCD_BL_PIN, (int)(Value * 2.55f));
  }
}

void DEV_Delay_ms(uint32_t xms)
{
  delay(xms);
}

void DEV_Delay_us(uint32_t xus)
{
  delayMicroseconds(xus);
}

void DEV_GPIO_Init(void)
{
  DEV_GPIO_Mode(LCD_RST_PIN, 1);
  DEV_GPIO_Mode(LCD_DC_PIN, 1);
  DEV_GPIO_Mode(LCD_CS_PIN, 1);
  DEV_GPIO_Mode(LCD_BL_PIN, 1);

  DEV_GPIO_Mode(BAT_ADC_PIN, 0);
  analogReadResolution(12);

  DEV_Digital_Write(LCD_CS_PIN, 1);
  DEV_Digital_Write(LCD_DC_PIN, 0);

  DEV_SET_Backlight(50);
}

uint8_t DEV_Module_Init(void)
{
  Serial.begin(115200);
  DEV_Delay_ms(100);

  DEV_GPIO_Init();

  vspi = new SPIClass(VSPI);
  vspi->begin(LCD_CLK_PIN, LCD_MISO_PIN, LCD_MOSI_PIN, LCD_CS_PIN);

  pinMode(vspi->pinSS(), OUTPUT);
  vspi->beginTransaction(SPISettings(80000000, MSBFIRST, SPI_MODE0));

  Wire.setPins(DEV_SDA_PIN, DEV_SCL_PIN);
  Wire.setClock(400000);
  Wire.begin();

  return 0;
}

void DEV_Module_Exit(void)
{
  vspi->end();
  Wire.end();
}