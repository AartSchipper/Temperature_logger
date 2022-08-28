
void read_DS_temperature(void) {
  static long last_action;    // Time of last action in ms
  byte data[12], type_s, error;
  float temperature;

  enum states { // States of the DS read state machine
    ds_start,
    ds_read,
    ds_pause,
  };

  static int state = ds_start;

  switch (state) {

    case ds_start: {
        ds.reset();
        ds.write(0xCC, 1);  // Skip ROM address, address all chips
        ds.write(0x44, 1);  // start conversion
        state = ds_read;
        last_action = millis();

      } break;

    case ds_read: { // read all sensors
        if ((millis() - last_action) < 800) break; // continue only after 800 ms

        for (int i = 0; i < (sizeof(ow_addresses) / (sizeof(byte) * 8)); i++) {

#ifdef debug_temp
          Serial.print("Sensor number: ");
          Serial.println(i);
#endif
          ds.reset();

          ds.select(ow_addresses[i]);

          ds.write(0xBE);         // Write: "Read Scratchpad"

          error = 1;

          for (int j = 0; j < 9; j++) {          // we need 9 bytes
            data[j] = ds.read();
#ifdef debug_temp
            Serial.print(data[j], HEX);
            Serial.print(" ");
#endif

            if (data[j] != 0xFF) error = 0; // If there is some data we assume the sensor is there.
          }

          switch (ow_addresses[i][0]) {
            case 0x10:
#ifdef debug_temp
              Serial.println("  Chip = DS18S20");  // or old DS1820
#endif
              type_s = 1;
              break;
            case 0x28:
#ifdef debug_temp
              Serial.println("  Chip = DS18B20");
#endif
              type_s = 0;
              break;
            case 0x22:
#ifdef debug_temp
              Serial.println("  Chip = DS1822");
#endif
              type_s = 0;
              break;
            default: {
#ifdef debug_temp
                Serial.println("Device is not a DS18x20 family device.");
#endif
              }
          }

          int16_t raw = (data[1] << 8) | data[0];
          if (type_s) {
            raw = raw << 3; // 9 bit resolution default
            if (data[7] == 0x10) {
              // "count remain" gives full 12 bit resolution
              raw = (raw & 0xFFF0) + 12 - data[6];
            }
          } else {
            byte cfg = (data[4] & 0x60);
            // at lower res, the low bits are undefined, so let's zero them
            if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
            else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
            else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
            //// default is 12 bit resolution, 750 ms conversion time
          }
          temperature = (float)raw / 16.00;

#ifdef debug_temp
          Serial.print("Temperature: ");
          Serial.print(temperature);
          Serial.print(" average ");
          Serial.print((float)sensor_output[i][0] / (float)sensor_output[i][1]);
          Serial.println(" Celsius, ");
          Serial.println (" - - - ");
#endif

          if (error == 0) {
            sensor_output[i][0] += temperature;
            sensor_output[i][1]++;
          }
        }

        last_action = millis();
        state = ds_pause;

      }

    case ds_pause: {
        if ((millis() - last_action) < READ_INTERVAL) break; // continue only after read_interval ms
#ifdef debug_temp
        Serial.println (" - - leave pause - -");
#endif
        state = ds_start;
      } break;
  }
}
